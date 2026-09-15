#include "TestHelpers.hpp"
#include "core/geometry/GeometryTestSupport.hpp"
#include "features/FeatureTestSupport.hpp"
#include "support/BracketModel.hpp"
#include "support/LoftModels.hpp"
#include "support/MeshAnalysis.hpp"
#include "support/TestFiles.hpp"
#include "support/occt/StepReadBack.hpp"

#include <bettercad/core/document/DependencyGraph.hpp>
#include <bettercad/features/LoftFeature.hpp>
#include <bettercad/features/Regeneration.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/io/ModelExport.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <format>
#include <numbers>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::features;
using namespace bettercad::literals;
using bettercad::test::describe;
using bettercad::test::featureIdOf;
using bettercad::test::FrustumLoftModel;
using bettercad::test::frustumVolume;
using bettercad::test::OffsetLoftModel;
using bettercad::test::readFile;
using bettercad::test::RectangularFrustumModel;
using bettercad::test::requireReport;
using bettercad::test::sketchIdOf;
using bettercad::test::TaperedHoleModel;
using bettercad::test::TempDir;
using bettercad::test::ThreeSectionLoftModel;
using bettercad::test::volumeMm3;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::StartsWith;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

constexpr double pi = std::numbers::pi;
constexpr double kRel = 1e-12;
// STEP stores coordinates as decimal text; volumes computed from a re-read
// file agree with the original to well within this (as in StepExportTests).
constexpr double kRelStep = 1e-9;

std::uint64_t bits(double value) {
    return std::bit_cast<std::uint64_t>(value);
}

void checkSameGeometry(const Regenerator& expected, const Regenerator& actual, ObjectId feature) {
    INFO("feature " << feature);
    const geometry::Body* a = expected.body(feature);
    const geometry::Body* b = actual.body(feature);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    const auto pa = a->massProperties().value();
    const auto pb = b->massProperties().value();
    CHECK(bits(pa.volume.si()) == bits(pb.volume.si()));
    CHECK(bits(pa.surfaceArea.si()) == bits(pb.surfaceArea.si()));
    CHECK(bits(pa.centerOfMass.x.si()) == bits(pb.centerOfMass.x.si()));
    CHECK(bits(pa.centerOfMass.y.si()) == bits(pb.centerOfMass.y.si()));
    CHECK(bits(pa.centerOfMass.z.si()) == bits(pb.centerOfMass.z.si()));
    CHECK(a->boundingBox().value() == b->boundingBox().value());
    CHECK(a->topology() == b->topology());
}

/// Saves @p doc, replaces it with an empty document, loads the file and
/// checks that the model, its IDs, its order and its dependency graph are
/// unchanged.
Document saveDestroyLoad(Document& doc, const std::filesystem::path& path) {
    const Document expected = doc.clone();
    REQUIRE(io::saveDocument(doc, path).has_value());
    doc = Document("Closed");

    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());
    CHECK(equivalent(expected, *loaded));
    CHECK(loaded->itemIds() == expected.itemIds());
    const DocumentGraph a = buildDependencyGraph(expected);
    const DocumentGraph b = buildDependencyGraph(*loaded);
    REQUIRE(a.graph.nodes() == b.graph.nodes());
    for (const ObjectId node : a.graph.nodes()) {
        CHECK(a.graph.dependenciesOf(node) == b.graph.dependenciesOf(node));
    }
    return std::move(*loaded);
}

std::string replaceOnce(std::string text, std::string_view from, std::string_view to) {
    const auto pos = text.find(from);
    REQUIRE(pos != std::string::npos);
    text.replace(pos, from.size(), to);
    return text;
}

/// The tapered hole's sections as the file stores them.
constexpr std::string_view kTaperSections = R"("sections": [
          {
            "sketch": 7,
            "offset": 0.0
          },
          {
            "sketch": 8,
            "offset": 0.0,
            "offset_parameter": 4
          }
        ],)";

} // namespace

TEST_CASE("LoftFeature_SaveLoadPreservesSectionOrder", "[loft][io][acceptance]") {
    TempDir dir;
    SECTION("a cut from a target, a section facing down, driven offsets") {
        TaperedHoleModel m;
        Regenerator before;
        const RegenerationReport built = requireReport(before, m.doc);
        INFO(describe(built));
        REQUIRE(built.succeeded());
        const LoftDefinition definition = m.definitionOf<LoftFeature>(m.taper);

        Document loaded = saveDestroyLoad(m.doc, dir.path() / "taper.bcad");
        // Every field: the feature's ID and name, the sections in order with
        // their sketches and offsets, the interpolation, the operation, the
        // target and so the dependencies.
        const auto* restored = loaded.findObjectAs<LoftFeature>(m.taper);
        REQUIRE(restored != nullptr);
        CHECK(restored->name() == "Taper");
        CHECK(restored->featureId() == featureIdOf(m.taper));
        CHECK(restored->definition() == definition);
        REQUIRE(restored->definition().sections.size() == 2);
        CHECK(restored->definition().sections[0].sketch == sketchIdOf(m.mouth));
        CHECK(restored->definition().sections[1].sketch == sketchIdOf(m.tip));
        CHECK(restored->definition().sections[1].offsetParameter == m.extra);
        CHECK(restored->definition().interpolation == LoftInterpolation::Ruled);
        CHECK(restored->definition().operation == FeatureOperation::Cut);
        CHECK(restored->definition().target == featureIdOf(m.pad));
        CHECK(restored->dependencies() == std::vector<ObjectId>{m.mouth, m.tip, ObjectId{m.extra}, m.pad});

        Regenerator after;
        const RegenerationReport rebuilt = requireReport(after, loaded);
        CHECK(rebuilt.succeeded());
        CHECK(rebuilt.regenerated == built.regenerated); // same order
        for (const ObjectId feature : {m.pad, m.taper}) {
            checkSameGeometry(before, after, feature);
        }

        // Still parametric: the depth, then the target's height.
        REQUIRE(loaded.setParameterValue(m.extra, 10_mm).has_value());
        CHECK(requireReport(after, loaded).regenerated == std::vector<ObjectId>{m.taper});
        CHECK_THAT(volumeMm3(after, m.taper), WithinRel(TaperedHoleModel::expectedVolume(20, 10), kRel));
        REQUIRE(loaded.setParameterValue(m.height, 30_mm).has_value());
        CHECK(requireReport(after, loaded).regenerated == std::vector<ObjectId>{m.pad, m.taper});
        CHECK_THAT(volumeMm3(after, m.taper), WithinRel(TaperedHoleModel::expectedVolume(30, 10), kRel));
    }
    SECTION("sections listed against their IDs' order stay in that order") {
        // Top (7), Middle (6), Bottom (5): the loft runs down. Reloaded as
        // [7, 6, 5], never sorted to [5, 6, 7].
        ThreeSectionLoftModel m;
        LoftDefinition d = m.definitionOf(m.loft);
        std::swap(d.sections[0], d.sections[2]);
        m.setDefinition(m.loft, d);
        Regenerator before;
        REQUIRE(requireReport(before, m.doc).succeeded());
        Document loaded = saveDestroyLoad(m.doc, dir.path() / "barrel.bcad");
        const LoftDefinition& restored = loaded.findObjectAs<LoftFeature>(m.loft)->definition();
        CHECK(restored == d);
        REQUIRE(restored.sections.size() == 3);
        CHECK(restored.sections[0].sketch == sketchIdOf(m.top));
        CHECK(restored.sections[1].sketch == sketchIdOf(m.middle));
        CHECK(restored.sections[2].sketch == sketchIdOf(m.bottom));
        Regenerator after;
        REQUIRE(requireReport(after, loaded).succeeded());
        checkSameGeometry(before, after, m.loft);
        const auto text = io::documentToJson(loaded);
        REQUIRE(text.has_value());
        const auto at = [&](std::string_view sketch) { return text->find(sketch); };
        CHECK(at("\"sketch\": 7") < at("\"sketch\": 6"));
        CHECK(at("\"sketch\": 6") < at("\"sketch\": 5"));
    }
    SECTION("literal offsets round-trip bit for bit") {
        Document doc{"Stack"};
        LoftDefinition d;
        const std::array<std::pair<double, double>, 3> rings{{{10, 0.0}, {6, 20.3}, {8, 35.7}}};
        for (std::size_t i = 0; i < rings.size(); ++i) {
            const ObjectId id =
                doc.addObject(bettercad::test::drivenCircle(std::format("Ring{}", i + 1), Frame3D::xy(), 0, 0,
                                                            doc.createParameter(std::format("r{}", i + 1),
                                                                                rings[i].first * units::mm, units::mm)
                                                                .value()))
                    .value();
            d.sections.push_back({.sketch = sketchIdOf(id), .offset = rings[i].second * units::mm});
        }
        auto feature = LoftFeature::create("Stack", d);
        REQUIRE(feature.has_value());
        const ObjectId loft = doc.addObject(std::move(*feature)).value();
        Regenerator before;
        REQUIRE(requireReport(before, doc).succeeded());
        Document loaded = saveDestroyLoad(doc, dir.path() / "stack.bcad");
        CHECK(loaded.findObjectAs<LoftFeature>(loft)->definition() == d); // operator== on the doubles
        Regenerator after;
        REQUIRE(requireReport(after, loaded).succeeded());
        checkSameGeometry(before, after, loft);
        CHECK_THAT(volumeMm3(after, loft), WithinRel(frustumVolume(10, 6, 20.3) + frustumVolume(6, 8, 15.4), kRel));
    }
}

TEST_CASE("LoftFeature_FailedLoftSavesAndLoadsUnchanged", "[loft][io]") {
    // Sections on one plane are still part of the model.
    TempDir dir;
    FrustumLoftModel m;
    REQUIRE(m.doc.setParameterValue(m.height, 0_mm).has_value());
    Regenerator before;
    const RegenerationReport failed = requireReport(before, m.doc);
    REQUIRE(failed.failed == std::vector<ObjectId>{m.loft});

    Document loaded = saveDestroyLoad(m.doc, dir.path() / "failed.bcad");
    Regenerator after;
    const RegenerationReport again = requireReport(after, loaded);
    CHECK(again.failed == std::vector<ObjectId>{m.loft});
    CHECK(again.errors.at(m.loft).message == failed.errors.at(m.loft).message);
    CHECK_THAT(again.errors.at(m.loft).message, StartsWith("Loft: makeLoft: sections 1 and 2 lie on the same plane"));
    CHECK(after.body(m.loft) == nullptr);

    REQUIRE(loaded.setParameterValue(m.height, 30_mm).has_value());
    REQUIRE(requireReport(after, loaded).succeeded());
    CHECK_THAT(volumeMm3(after, m.loft), WithinRel(1750.0 * pi, kRel));
}

TEST_CASE("LoftFeature_DataIsStoredAsTransparentJson", "[loft][io]") {
    TaperedHoleModel m;
    const auto text = io::documentToJson(m.doc);
    REQUIRE(text.has_value());
    CHECK_THAT(*text, ContainsSubstring(std::string{R"("type": "loft",
      "name": "Taper",
      "data": {
        )"} + std::string{kTaperSections} +
                                        R"(
        "interpolation": "ruled",
        "operation": "cut",
        "target": 6
      })"));

    // A new body has no target; a literal offset is in metres.
    FrustumLoftModel frustum;
    LoftDefinition d = frustum.definitionOf(frustum.loft);
    d.sections[1] = {.sketch = sketchIdOf(frustum.top), .offset = 12.5_mm};
    frustum.setDefinition(frustum.loft, d);
    const auto frustumText = io::documentToJson(frustum.doc);
    REQUIRE(frustumText.has_value());
    CHECK_THAT(*frustumText, ContainsSubstring(R"("sections": [
          {
            "sketch": 4,
            "offset": 0.0
          },
          {
            "sketch": 5,
            "offset": 0.0125
          }
        ],
        "interpolation": "ruled",
        "operation": "new_body"
      })"));
}

TEST_CASE("LoftFeature_MalformedDataIsRejectedWithTheJsonPath", "[loft][io]") {
    TaperedHoleModel m;
    const std::string good = io::documentToJson(m.doc).value();
    REQUIRE(io::documentFromJson(good).has_value());
    const auto loadError = [](const std::string& text) {
        const auto loaded = io::documentFromJson(text);
        REQUIRE_FALSE(loaded.has_value());
        UNSCOPED_INFO(loaded.error().message);
        return loaded.error();
    };
    const auto message = [&](const std::string& text) { return loadError(text).message; };
    const auto withSections = [&](std::string_view replacement) {
        return replaceOnce(good, kTaperSections, replacement);
    };

    // Structure: every problem is reported where it is.
    CHECK(message(withSections("")) == "objects[4].data.sections: missing required field");
    CHECK(message(withSections(R"("sections": {"sketch": 7},)")) == "objects[4].data.sections: expected an array");
    CHECK(message(withSections(R"("sections": [7, 8],)")) == "objects[4].data.sections[0]: expected an object");
    CHECK(message(withSections(R"("sections": [{"sketch": 7, "offset": 0.0}, {"offset": 0.0}],)")) ==
          "objects[4].data.sections[1].sketch: missing required field");
    CHECK(message(withSections(R"("sections": [{"sketch": 7, "offset": 0.0}, {"sketch": 8}],)")) ==
          "objects[4].data.sections[1].offset: missing required field");
    CHECK(message(withSections(R"("sections": [{"sketch": 7, "offset": "up"}, {"sketch": 8, "offset": 0.0}],)")) ==
          "objects[4].data.sections[0].offset: expected a number");
    CHECK(message(withSections(
              R"("sections": [{"sketch": 7, "offset": 0.0}, {"sketch": 8, "offset": 0.0, "offset_parameter": -4}],)")) ==
          "objects[4].data.sections[1].offset_parameter: expected an ID (a non-negative integer)");
    CHECK(message(withSections(R"("sections": [{"sketch": 7, "offset": 0.0, "twist": 0}, {"sketch": 8, "offset": 0.0}],)")) ==
          "objects[4].data.sections[0].twist: unknown field");
    CHECK(message(replaceOnce(good, "\"ruled\"", "\"smooth\"")) == "objects[4].data.interpolation: unknown value 'smooth'");
    CHECK(message(replaceOnce(good, "\"interpolation\": \"ruled\",", "")) ==
          "objects[4].data.interpolation: missing required field");
    CHECK(message(replaceOnce(good, "\"interpolation\": \"ruled\",", "\"interpolation\": \"ruled\", \"guides\": [],")) ==
          "objects[4].data.guides: unknown field");

    // Content: the definition's own rules, reported at the loft.
    const auto invalid = [&](const std::string& text) {
        const Error error = loadError(text);
        CHECK(error.code == ErrorCode::InvalidArgument);
        return error.message;
    };
    CHECK(invalid(withSections(R"("sections": [{"sketch": 7, "offset": 0.0}],)")) ==
          "objects[4].data: a loft needs at least two sections, got 1");
    CHECK(invalid(withSections(R"("sections": [{"sketch": 7, "offset": 0.0}, {"sketch": 7, "offset": 0.0}],)")) ==
          "objects[4].data: section 2 repeats section 1: the same sketch at the same offset");
    CHECK(invalid(withSections(R"("sections": [{"sketch": 0, "offset": 0.0}, {"sketch": 8, "offset": 0.0}],)")) ==
          "objects[4].data: section 1 needs a sketch");
    CHECK(invalid(replaceOnce(good, ",\n        \"target\": 6", "")) == "objects[4].data: a cut feature needs a target feature");
}

TEST_CASE("LoftFeature_ExportsStep", "[loft][io][export][acceptance]") {
    TempDir dir;
    SECTION("the circular frustum") {
        FrustumLoftModel m;
        const auto path = dir.path() / "frustum.step";
        const auto summary = io::exportStep(m.doc, path);
        REQUIRE(summary.has_value());
        REQUIRE(summary->bodies.size() == 1);
        CHECK(summary->bodies[0].name == "Loft");
        CHECK_THAT(readFile(path), ContainsSubstring("PRODUCT('Loft','Loft'"));
        const auto contents = test::readStepFile(path);
        REQUIRE(contents.has_value());
        CHECK(contents->solids == 1);
        CHECK(contents->valid);
        CHECK_THAT(contents->volumeMm3, WithinRel(frustumVolume(10, 5, 30), kRelStep));
        CHECK_THAT(contents->areaMm2, WithinRel(pi * 15.0 * std::sqrt(925.0) + pi * 125.0, kRelStep));
        for (const std::size_t axis : {0U, 1U}) {
            CHECK_THAT(contents->minMm[axis], WithinAbs(-10.0, 1e-6));
            CHECK_THAT(contents->maxMm[axis], WithinAbs(10.0, 1e-6));
        }
        CHECK_THAT(contents->minMm[2], WithinAbs(0.0, 1e-6));
        CHECK_THAT(contents->maxMm[2], WithinAbs(30.0, 1e-6));
    }
    SECTION("the three-section loft") {
        ThreeSectionLoftModel m;
        const auto path = dir.path() / "barrel.step";
        REQUIRE(io::exportStep(m.doc, path).has_value());
        const auto contents = test::readStepFile(path);
        REQUIRE(contents.has_value());
        CHECK(contents->solids == 1);
        CHECK(contents->valid);
        CHECK_THAT(contents->volumeMm3, WithinRel(17500.0 * pi / 3.0, kRelStep));
        CHECK_THAT(contents->minMm[2], WithinAbs(0.0, 1e-6));
        CHECK_THAT(contents->maxMm[2], WithinAbs(100.0, 1e-6));
        CHECK_THAT(contents->maxMm[0], WithinAbs(10.0, 1e-6));
    }
    SECTION("the block with the tapered hole") {
        TaperedHoleModel m;
        const auto path = dir.path() / "taper.step";
        REQUIRE(io::exportStep(m.doc, path).has_value());
        const auto contents = test::readStepFile(path);
        REQUIRE(contents.has_value());
        CHECK(contents->solids == 1);
        CHECK(contents->valid);
        CHECK_THAT(contents->volumeMm3, WithinRel(TaperedHoleModel::expectedVolume(20, 15), kRelStep));
    }
}

TEST_CASE("LoftFeature_ExportsClosedStl", "[loft][io][export][acceptance]") {
    // Every mesh must be closed and consistently oriented, enclose a
    // positive volume, and lie within the deflection of the B-Rep
    // (|V_mesh - V| <= deflection x area).
    const Length deflection = 0.01_mm;
    TempDir dir;
    const auto check = [&](Document& doc, const std::string& name, io::StlFormat format, double exact, double area) {
        const auto path = dir.path() / name;
        const auto summary = io::exportStl(doc, path, {.mesh = {.linearDeflection = deflection}, .format = format});
        REQUIRE(summary.has_value());
        REQUIRE(summary->bodies.size() == 1);
        const std::string bytes = readFile(path);
        const auto mesh = format == io::StlFormat::Binary ? test::parseBinaryStl(bytes) : test::parseAsciiStl(bytes);
        REQUIRE(mesh.has_value());
        CHECK_FALSE(mesh->triangles.empty());
        const test::SurfaceCheck surface = test::checkSurface(mesh->triangles);
        CHECK(surface.watertight());
        bool finite = true;
        for (const test::Triangle& t : mesh->triangles) {
            for (const test::Vertex& v : t) {
                finite = finite && std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
            }
        }
        CHECK(finite);
        const double meshed = test::enclosedVolume(mesh->triangles);
        INFO(name << ": meshed " << meshed << " mm^3, exact " << exact << " mm^3, bound "
                  << deflection.in(units::mm) * area << " mm^3");
        CHECK(meshed > 0.0);
        CHECK(std::abs(meshed - exact) <= deflection.in(units::mm) * area);
    };
    const double frustumArea = pi * 15.0 * std::sqrt(925.0) + pi * 125.0;
    for (const io::StlFormat format : {io::StlFormat::Binary, io::StlFormat::Ascii}) {
        DYNAMIC_SECTION("the circular frustum (" << (format == io::StlFormat::Binary ? "binary" : "ASCII") << ")") {
            FrustumLoftModel m;
            check(m.doc, "frustum.stl", format, frustumVolume(10, 5, 30), frustumArea);
        }
    }
    SECTION("the rectangular frustum and the leaning frustum") {
        RectangularFrustumModel rectangles;
        const double sides = 2.0 * 15.0 * std::sqrt(906.25) + 2.0 * 7.5 * std::sqrt(925.0);
        check(rectangles.doc, "taper.stl", io::StlFormat::Binary, 3500.0, 250.0 + sides);
        OffsetLoftModel lean;
        REQUIRE(lean.doc.setParameterValue(lean.shift, 20_mm).has_value());
        Regenerator regenerator;
        REQUIRE(requireReport(regenerator, lean.doc).succeeded());
        const double area = regenerator.body(lean.loft)->massProperties()->surfaceArea.in(units::mm2);
        check(lean.doc, "lean.stl", io::StlFormat::Binary, frustumVolume(10, 5, 30), area);
    }
}
