#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"
#include "support/BracketModel.hpp"
#include "support/FilletModels.hpp"
#include "support/MeshAnalysis.hpp"
#include "support/TestFiles.hpp"
#include "support/occt/StepReadBack.hpp"

#include <bettercad/core/document/DependencyGraph.hpp>
#include <bettercad/features/FilletFeature.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/io/ModelExport.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <bit>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::features;
using namespace bettercad::literals;
using bettercad::test::describe;
using bettercad::test::FilletBlockModel;
using bettercad::test::filletCorner;
using bettercad::test::FilletedShaft;
using bettercad::test::FilletVariants;
using bettercad::test::readFile;
using bettercad::test::requireReport;
using bettercad::test::TempDir;
using bettercad::test::volumeMm3;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::StartsWith;
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

// The Round fillet's reference as the file stores it.
constexpr std::string_view kRoundEdges = R"("edges": [
          {
            "curve": "line",
            "point": [
              0.0,
              0.0,
              0.02
            ],
            "direction": [
              1.0,
              0.0,
              0.0
            ]
          }
        ])";

} // namespace

TEST_CASE("FilletFeature_SaveLoadPreservesReferencesAndRadius", "[fillet][io][acceptance]") {
    TempDir dir;
    FilletVariants m;
    Regenerator before;
    const RegenerationReport built = requireReport(before, m.doc);
    INFO(describe(built));
    REQUIRE(built.succeeded());
    CHECK_THAT(volumeMm3(before, m.corner), WithinRel(FilletVariants::expectedVolume(5), kRel));
    const FilletDefinition round = m.definitionOf(m.round);
    const FilletDefinition corner = m.definitionOf(m.corner);

    Document loaded = saveDestroyLoad(m.doc, dir.path() / "block.bcad");
    // Every field, bit for bit: target, edge references, radius and its parameter.
    CHECK(loaded.findObjectAs<FilletFeature>(m.round)->definition() == round);
    CHECK(loaded.findObjectAs<FilletFeature>(m.corner)->definition() == corner);
    CHECK(loaded.findObjectAs<FilletFeature>(m.corner)->name() == "Corner");

    Regenerator after;
    const RegenerationReport rebuilt = requireReport(after, loaded);
    CHECK(rebuilt.succeeded());
    CHECK(rebuilt.regenerated == built.regenerated); // same order
    for (const ObjectId feature : {m.pad, m.round, m.corner}) {
        checkSameGeometry(before, after, feature);
    }

    // The loaded model is still parametric.
    REQUIRE(loaded.setParameterValue(m.radius, 3_mm).has_value());
    const RegenerationReport changed = requireReport(after, loaded);
    CHECK(changed.regenerated == std::vector<ObjectId>{m.round, m.corner});
    CHECK_THAT(volumeMm3(after, m.corner), WithinRel(FilletVariants::expectedVolume(3), kRel));
}

TEST_CASE("FilletFeature_SaveLoadPreservesCircleReferences", "[fillet][io][revolve]") {
    TempDir dir;
    FilletedShaft m;
    Regenerator before;
    REQUIRE(requireReport(before, m.doc).succeeded());
    CHECK_THAT(volumeMm3(before, m.rims), WithinRel(FilletedShaft::expectedVolume(360), kRel));
    const FilletDefinition definition = m.doc.findObjectAs<FilletFeature>(m.rims)->definition();

    Document loaded = saveDestroyLoad(m.doc, dir.path() / "shaft.bcad");
    CHECK(loaded.findObjectAs<FilletFeature>(m.rims)->definition() == definition);
    Regenerator after;
    REQUIRE(requireReport(after, loaded).succeeded());
    checkSameGeometry(before, after, m.rims);
}

TEST_CASE("FilletFeature_FailedFilletSavesAndLoadsUnchanged", "[fillet][io]") {
    // A fillet whose edge no longer exists is still part of the model.
    TempDir dir;
    FilletBlockModel m;
    REQUIRE(m.doc.setParameterValue(m.height, 30_mm).has_value());
    Regenerator before;
    const RegenerationReport failed = requireReport(before, m.doc);
    REQUIRE(failed.failed == std::vector<ObjectId>{m.round});

    Document loaded = saveDestroyLoad(m.doc, dir.path() / "failed.bcad");
    Regenerator after;
    const RegenerationReport again = requireReport(after, loaded);
    CHECK(again.failed == std::vector<ObjectId>{m.round});
    CHECK(again.errors.at(m.round).message == failed.errors.at(m.round).message);

    REQUIRE(loaded.setParameterValue(m.height, 20_mm).has_value());
    REQUIRE(requireReport(after, loaded).succeeded());
    CHECK_THAT(volumeMm3(after, m.round), WithinRel(FilletBlockModel::expectedVolume(100, 50, 20, 5), kRel));
}

TEST_CASE("FilletFeature_DataIsStoredAsTransparentJson", "[fillet][io]") {
    FilletVariants m;
    const auto text = io::documentToJson(m.doc);
    REQUIRE(text.has_value());
    CHECK_THAT(*text, ContainsSubstring(std::string{R"("type": "fillet",
      "name": "Round",
      "data": {
        "target": 6,
        )"} + std::string{kRoundEdges} + R"(,
        "radius": 0.005,
        "radius_parameter": 4
      })"));
    CHECK_THAT(*text, ContainsSubstring(R"("name": "Corner",
      "data": {
        "target": 7,
        "edges": [
          {
            "curve": "line",
            "point": [
              0.0,
              0.05,
              0.0
            ],
            "direction": [
              1.0,
              0.0,
              0.0
            ]
          },
          {
            "curve": "line",
            "point": [
              0.1,
              0.0,
              0.0
            ],
            "direction": [
              0.0,
              1.0,
              0.0
            ]
          },
          {
            "curve": "line",
            "point": [
              0.1,
              0.05,
              0.0
            ],
            "direction": [
              0.0,
              0.0,
              1.0
            ]
          }
        ],
        "radius": 0.003
      })"));

    FilletedShaft shaft;
    const auto shaftText = io::documentToJson(shaft.doc);
    REQUIRE(shaftText.has_value());
    CHECK_THAT(*shaftText, ContainsSubstring(R"("type": "fillet",
      "name": "Rims",
      "data": {
        "target": 10,
        "edges": [
          {
            "curve": "circle",
            "center": [
              0.0,
              0.0,
              0.04
            ],
            "axis": [
              0.0,
              0.0,
              1.0
            ],
            "radius": 0.015
          },)"));
}

TEST_CASE("FilletFeature_MalformedDataIsRejectedWithTheJsonPath", "[fillet][io]") {
    FilletBlockModel m;
    const std::string good = io::documentToJson(m.doc).value();
    REQUIRE(io::documentFromJson(good).has_value());
    const auto loadError = [](const std::string& text) {
        const auto loaded = io::documentFromJson(text);
        REQUIRE_FALSE(loaded.has_value());
        UNSCOPED_INFO(loaded.error().message);
        return loaded.error();
    };
    const auto message = [&](const std::string& text) { return loadError(text).message; };
    const std::string radius = "\"radius\": 0.005,\n        \"radius_parameter\": 4";

    // Structure: every problem is reported where it is.
    CHECK(message(replaceOnce(good, radius, "\"radius\": \"5 mm\"")) == "objects[2].data.radius: expected a number");
    CHECK(message(replaceOnce(good, radius, "\"radius_parameter\": 4")) ==
          "objects[2].data.radius: missing required field");
    CHECK(message(replaceOnce(good, radius, "\"radius\": 0.005, \"radius_parameter\": -1")) ==
          "objects[2].data.radius_parameter: expected an ID (a non-negative integer)");
    // A fillet has no mode yet; an unknown field is refused rather than ignored.
    CHECK(message(replaceOnce(good, radius, "\"radius\": 0.005, \"mode\": \"variable\"")) ==
          "objects[2].data.mode: unknown field");
    CHECK(message(replaceOnce(good, kRoundEdges,
                              R"("edges": [{"curve": "spline", "point": [0, 0, 0.02], "direction": [1, 0, 0]}])")) ==
          "objects[2].data.edges[0].curve: unknown value 'spline'");
    CHECK(message(replaceOnce(good, kRoundEdges, R"("edges": {"curve": "line"})")) ==
          "objects[2].data.edges: expected an array");

    // Content: the definition's own rules, reported at the fillet.
    const auto invalid = [&](const std::string& text) {
        const Error error = loadError(text);
        CHECK(error.code == ErrorCode::InvalidArgument);
        return error.message;
    };
    CHECK(invalid(replaceOnce(good, kRoundEdges, R"("edges": [])")) ==
          "objects[2].data: a fillet needs at least one edge");
    CHECK(invalid(replaceOnce(good, radius, "\"radius\": -0.005")) ==
          "objects[2].data: the fillet radius must be positive and finite, got -5 mm");
    CHECK(invalid(replaceOnce(good, "\"target\": 6", "\"target\": 0")) ==
          "objects[2].data: a fillet needs a target feature");
    CHECK(invalid(replaceOnce(good, kRoundEdges,
                              R"("edges": [{"curve": "line", "point": [0, 0, 0.02], "direction": [1, 0, 0]},
                                          {"curve": "line", "point": [0.03, 0, 0.02], "direction": [1, 0, 0]}])")) ==
          "objects[2].data: edge references 1 and 2 refer to the same line through (30, 0, 20) mm along (1, 0, 0)");
}

TEST_CASE("FilletFeature_ExportsStep", "[fillet][io][export][acceptance]") {
    FilletBlockModel m;
    TempDir dir;
    const auto path = dir.path() / "block.step";
    const auto summary = io::exportStep(m.doc, path);
    REQUIRE(summary.has_value());
    REQUIRE(summary->bodies.size() == 1);
    CHECK(summary->bodies[0].name == "Round");
    CHECK_THAT(readFile(path), ContainsSubstring("PRODUCT('Round','Round'"));
    const auto contents = test::readStepFile(path);
    REQUIRE(contents.has_value());
    CHECK(contents->solids == 1);
    CHECK(contents->valid);
    CHECK_THAT(contents->volumeMm3, WithinRel(FilletBlockModel::expectedVolume(100, 50, 20, 5), kRelStep));
    CHECK_THAT(contents->areaMm2,
               WithinRel(16000.0 - 1000.0 - 2.0 * filletCorner(5) + pi * 5.0 / 2.0 * 100.0, kRelStep));
}

TEST_CASE("FilletFeature_ExportsClosedStl", "[fillet][io][export][acceptance]") {
    FilletBlockModel m;
    const double exact = FilletBlockModel::expectedVolume(100, 50, 20, 5);
    const double area = 16000.0 - 1000.0 - 2.0 * filletCorner(5) + pi * 5.0 / 2.0 * 100.0;
    const Length deflection = 0.01_mm;
    TempDir dir;
    for (const io::StlFormat format : {io::StlFormat::Binary, io::StlFormat::Ascii}) {
        DYNAMIC_SECTION("STL (" << (format == io::StlFormat::Binary ? "binary" : "ASCII") << ")") {
            const auto path = dir.path() / "block.stl";
            const auto summary = io::exportStl(m.doc, path, {.mesh = {.linearDeflection = deflection}, .format = format});
            REQUIRE(summary.has_value());
            REQUIRE(summary->bodies.size() == 1);
            const std::string bytes = readFile(path);
            const auto mesh = format == io::StlFormat::Binary ? test::parseBinaryStl(bytes) : test::parseAsciiStl(bytes);
            REQUIRE(mesh.has_value());
            CHECK(test::checkSurface(mesh->triangles).watertight());
            // The rounded face is convex: its triangles lie inside the solid,
            // so the mesh is slightly smaller, and each point of the surface
            // is within the deflection of it.
            const double meshed = test::enclosedVolume(mesh->triangles);
            CHECK(meshed < exact);
            CHECK(exact - meshed <= deflection.in(units::mm) * area);
        }
    }
}
