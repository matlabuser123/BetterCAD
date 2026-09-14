#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"
#include "support/BracketModel.hpp"
#include "support/MeshAnalysis.hpp"
#include "support/PatternModels.hpp"
#include "support/TestFiles.hpp"
#include "support/occt/StepReadBack.hpp"

#include <bettercad/core/document/DependencyGraph.hpp>
#include <bettercad/features/CircularPatternFeature.hpp>
#include <bettercad/features/Regeneration.hpp>
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
using bettercad::test::BoltCircleModel;
using bettercad::test::CubeRingModel;
using bettercad::test::describe;
using bettercad::test::PegModel;
using bettercad::test::readFile;
using bettercad::test::requireReport;
using bettercad::test::TempDir;
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

/// Holes' axis as the file stores it.
constexpr std::string_view kBoltsAxis = R"("axis": {
          "origin": [
            0.0,
            0.0,
            0.0
          ],
          "direction": [
            0.0,
            0.0,
            1.0
          ]
        },)";

// The count as the file stores it: driven by parameter 4.
constexpr std::string_view kBoltsCount = "\"count\": 6,\n        \"count_parameter\": 4,";

} // namespace

TEST_CASE("CircularPattern_SaveLoadPreservesDefinition", "[pattern][circular][io][acceptance]") {
    // Every field in use: a driven count and a driven included angle, the
    // negative direction, and an axis direction stored as given (0, 0, 2).
    TempDir dir;
    BoltCircleModel m;
    const ParameterId span = m.doc.createParameter("span", 180_deg, units::deg).value();
    REQUIRE(m.doc.setParameterValue(m.count, 4.0, kUnitless).has_value());
    CircularPatternDefinition d = m.definitionOf(m.bolts);
    d.axis.direction = {0.0, 0.0, 2.0};
    d.spacing = CircularSpacing::IncludedAngle;
    d.angleParameter = span;
    d.direction = RotationDirection::Negative;
    m.setDefinition(m.bolts, d);
    Regenerator before;
    const RegenerationReport built = requireReport(before, m.doc);
    INFO(describe(built));
    REQUIRE(built.succeeded());
    CHECK_THAT(volumeMm3(before, m.bolts), WithinRel(BoltCircleModel::expectedVolume(60, 10, 10, 4), kRel));
    const auto instances = resolveCircularPatternInstances(d, m.doc);
    REQUIRE(instances.has_value());
    REQUIRE(instances->size() == 4);
    CHECK_THAT(instances->back().angle.in(units::deg), WithinAbs(-180.0, 1e-12));

    Document loaded = saveDestroyLoad(m.doc, dir.path() / "bolts.bcad");
    // Every field, bit for bit: the source's ID, the axis, the count, spacing,
    // angle, direction and their parameters; so the same instances in the
    // same order.
    const auto* restored = loaded.findObjectAs<CircularPatternFeature>(m.bolts);
    REQUIRE(restored != nullptr);
    CHECK(restored->definition() == d);
    CHECK(restored->definition().source == FeatureId::fromValue(m.bolt.value()));
    CHECK(restored->definition().axis.direction == Vector3D{0.0, 0.0, 2.0});
    CHECK(restored->name() == "Bolts");
    CHECK(resolveCircularPatternInstances(restored->definition(), loaded).value() == *instances);

    Regenerator after;
    const RegenerationReport rebuilt = requireReport(after, loaded);
    CHECK(rebuilt.succeeded());
    CHECK(rebuilt.regenerated == built.regenerated); // same order
    for (const ObjectId feature : {m.flange, m.bolt, m.bolts}) {
        checkSameGeometry(before, after, feature);
    }

    // The loaded model is still parametric: the source, the angle and the count.
    REQUIRE(loaded.setParameterValue(m.diameter, 8_mm).has_value());
    CHECK(requireReport(after, loaded).regenerated == std::vector<ObjectId>{m.bolt, m.bolts});
    CHECK_THAT(volumeMm3(after, m.bolts), WithinRel(BoltCircleModel::expectedVolume(60, 10, 8, 4), kRel));
    REQUIRE(loaded.setParameterValue(span, 90_deg).has_value());
    CHECK(requireReport(after, loaded).regenerated == std::vector<ObjectId>{m.bolts});
    CHECK_THAT(volumeMm3(after, m.bolts), WithinRel(BoltCircleModel::expectedVolume(60, 10, 8, 4), kRel));
    REQUIRE(loaded.setParameterValue(m.count, 2.0, kUnitless).has_value());
    CHECK(requireReport(after, loaded).regenerated == std::vector<ObjectId>{m.bolts});
    CHECK_THAT(volumeMm3(after, m.bolts), WithinRel(BoltCircleModel::expectedVolume(60, 10, 8, 2), kRel));
}

TEST_CASE("CircularPattern_SaveLoadKeepsAnArbitraryAxis", "[pattern][circular][io]") {
    // Pegs about (1, 1, 1) through (5, -2, 1) mm, four 90 degrees apart (the
    // angle step mode): the axis is stored as written and the geometry comes
    // back bit for bit.
    TempDir dir;
    PegModel m;
    auto feature = CircularPatternFeature::create(
        "Pegs", {.source = FeatureId::fromValue(m.peg.value()),
                 .axis = {.origin = Point3D{5_mm, -(2_mm), 1_mm}, .direction = {1.0, 1.0, 1.0}},
                 .count = 4,
                 .spacing = CircularSpacing::AngleStep,
                 .angle = 90_deg});
    REQUIRE(feature.has_value());
    const ObjectId pegs = m.doc.addObject(std::move(*feature)).value();
    Regenerator before;
    REQUIRE(requireReport(before, m.doc).succeeded());
    CHECK_THAT(volumeMm3(before, pegs), WithinRel(4.0 * pi * 9.0 * 6.0, kRel));
    const auto text = io::documentToJson(m.doc);
    REQUIRE(text.has_value());
    CHECK_THAT(*text, ContainsSubstring(R"("axis": {
          "origin": [
            0.005,
            -0.002,
            0.001
          ],
          "direction": [
            1.0,
            1.0,
            1.0
          ]
        },
        "count": 4,
        "spacing": "angle_step",
        "angle": 1.5707963267948966,
        "rotation": "positive")"));

    Document loaded = saveDestroyLoad(m.doc, dir.path() / "pegs.bcad");
    CHECK(loaded.findObjectAs<CircularPatternFeature>(pegs)->definition().axis ==
          PatternAxis{.origin = Point3D{5_mm, -(2_mm), 1_mm}, .direction = {1.0, 1.0, 1.0}});
    Regenerator after;
    REQUIRE(requireReport(after, loaded).succeeded());
    checkSameGeometry(before, after, pegs);
}

TEST_CASE("CircularPattern_FailedPatternSavesAndLoadsUnchanged", "[pattern][circular][io]") {
    // A pattern whose holes overlap (40 on the bolt circle) is still part of
    // the model.
    TempDir dir;
    BoltCircleModel m;
    REQUIRE(m.doc.setParameterValue(m.count, 40.0, kUnitless).has_value());
    Regenerator before;
    const RegenerationReport failed = requireReport(before, m.doc);
    REQUIRE(failed.failed == std::vector<ObjectId>{m.bolts});

    Document loaded = saveDestroyLoad(m.doc, dir.path() / "failed.bcad");
    Regenerator after;
    const RegenerationReport again = requireReport(after, loaded);
    CHECK(again.failed == std::vector<ObjectId>{m.bolts});
    CHECK(again.errors.at(m.bolts).message == failed.errors.at(m.bolts).message);
    CHECK_THAT(again.errors.at(m.bolts).message, StartsWith("Bolts: circular pattern: instance 1 at 9 deg: hole: "));

    REQUIRE(loaded.setParameterValue(m.count, 6.0, kUnitless).has_value());
    REQUIRE(requireReport(after, loaded).succeeded());
    CHECK_THAT(volumeMm3(after, m.bolts), WithinRel(BoltCircleModel::expectedVolume(60, 10, 10, 6), kRel));
}

TEST_CASE("CircularPattern_DataIsStoredAsTransparentJson", "[pattern][circular][io]") {
    BoltCircleModel m;
    const auto text = io::documentToJson(m.doc);
    REQUIRE(text.has_value());
    // A full circle: no angle.
    CHECK_THAT(*text, ContainsSubstring(std::string{R"("type": "circular_pattern",
      "name": "Bolts",
      "data": {
        "source": 7,
        )"} + std::string{kBoltsAxis} + "\n        " + std::string{kBoltsCount} +
                                        R"(
        "spacing": "full_circle",
        "rotation": "positive"
      })"));

    // An included angle, driven, the other way round.
    const ParameterId span = m.doc.createParameter("span", 90_deg, units::deg).value();
    CircularPatternDefinition d = m.definitionOf(m.bolts);
    d.spacing = CircularSpacing::IncludedAngle;
    d.angle = 90_deg;
    d.angleParameter = span;
    d.direction = RotationDirection::Negative;
    m.setDefinition(m.bolts, d);
    const auto included = io::documentToJson(m.doc);
    REQUIRE(included.has_value());
    CHECK_THAT(*included, ContainsSubstring(std::string{kBoltsCount} + R"(
        "spacing": "included_angle",
        "angle": 1.5707963267948966,
        "angle_parameter": 9,
        "rotation": "negative"
      })"));
}

TEST_CASE("CircularPattern_MalformedDataIsRejectedWithTheJsonPath", "[pattern][circular][io]") {
    BoltCircleModel m;
    const std::string good = io::documentToJson(m.doc).value();
    REQUIRE(io::documentFromJson(good).has_value());
    const auto loadError = [](const std::string& text) {
        const auto loaded = io::documentFromJson(text);
        REQUIRE_FALSE(loaded.has_value());
        UNSCOPED_INFO(loaded.error().message);
        return loaded.error();
    };
    const auto message = [&](const std::string& text) { return loadError(text).message; };

    // Structure: every problem is reported where it is.
    CHECK(message(replaceOnce(good, kBoltsCount, "\"count\": \"6\",")) ==
          "objects[3].data.count: expected a non-negative integer");
    CHECK(message(replaceOnce(good, kBoltsCount, "\"count\": 5000000000,")) ==
          "objects[3].data.count: expected at most 4294967295");
    CHECK(message(replaceOnce(good, kBoltsAxis, R"("axis": {"origin": [0, 0, 0], "direction": [0, 1]},)")) ==
          "objects[3].data.axis.direction: expected 3 numbers, got 2");
    CHECK(message(replaceOnce(good, kBoltsAxis, R"("axis": {"direction": [0, 0, 1]},)")) ==
          "objects[3].data.axis.origin: missing required field");
    CHECK(message(replaceOnce(good, kBoltsAxis, "")) == "objects[3].data.axis: missing required field");
    CHECK(message(replaceOnce(good, kBoltsAxis,
                              R"("axis": {"origin": [0, 0, 0], "direction": [0, 0, 1], "radius": 0.04},)")) ==
          "objects[3].data.axis.radius: unknown field");
    CHECK(message(replaceOnce(good, "\"full_circle\"", "\"symmetric\"")) ==
          "objects[3].data.spacing: unknown value 'symmetric'");
    CHECK(message(replaceOnce(good, "\"positive\"", "\"clockwise\"")) ==
          "objects[3].data.rotation: unknown value 'clockwise'");
    CHECK(message(replaceOnce(good, "\"source\": 7", "\"source\": 7, \"radius\": 0.04")) ==
          "objects[3].data.radius: unknown field");

    // Content: the definition's own rules, reported at the pattern.
    const auto invalid = [&](const std::string& text) {
        const Error error = loadError(text);
        CHECK(error.code == ErrorCode::InvalidArgument);
        return error.message;
    };
    CHECK(invalid(replaceOnce(good, kBoltsAxis, R"("axis": {"origin": [0, 0, 0], "direction": [0, 0, 0]},)")) ==
          "objects[3].data: the axis direction must be a finite, non-zero vector, got (0, 0, 0)");
    CHECK(invalid(replaceOnce(good, kBoltsCount, "\"count\": 0,")) ==
          "objects[3].data: the count must be at least 1, got 0");
    CHECK(invalid(replaceOnce(good, kBoltsCount, "\"count\": 501,")) ==
          "objects[3].data: a circular pattern may have at most 500 instances, got 501");
    CHECK(invalid(replaceOnce(good, "\"spacing\": \"full_circle\"", "\"spacing\": \"full_circle\", \"angle\": 0.5")) ==
          "objects[3].data: a full-circle pattern takes no angle: its instances are 360 deg / count apart");
    CHECK(invalid(replaceOnce(good, "\"spacing\": \"full_circle\"",
                              "\"spacing\": \"included_angle\", \"angle\": 6.283185307179586")) ==
          "objects[3].data: the included angle must be less than 360 deg, got 360 deg: the last instance would land "
          "on the source (use a full circle)");
    CHECK(invalid(replaceOnce(good, "\"spacing\": \"full_circle\"", "\"spacing\": \"included_angle\"")) ==
          "objects[3].data: the included angle must be positive and finite, got 0 deg");
    CHECK(invalid(replaceOnce(replaceOnce(good, kBoltsCount, "\"count\": 4,"), "\"spacing\": \"full_circle\"",
                              "\"spacing\": \"angle_step\", \"angle\": 2.0943951023931953")) ==
          "objects[3].data: the instances would go all the way around: 4 instances 120 deg apart span 360 deg, which "
          "must stay below 360 deg");
    CHECK(invalid(replaceOnce(good, "\"source\": 7", "\"source\": 0")) ==
          "objects[3].data: a circular pattern needs a source feature");
}

TEST_CASE("CircularPattern_ExportsStep", "[pattern][circular][io][export][acceptance]") {
    TempDir dir;
    SECTION("the bolt circle") {
        BoltCircleModel m;
        const auto path = dir.path() / "bolts.step";
        const auto summary = io::exportStep(m.doc, path);
        REQUIRE(summary.has_value());
        REQUIRE(summary->bodies.size() == 1);
        CHECK(summary->bodies[0].name == "Bolts");
        CHECK_THAT(readFile(path), ContainsSubstring("PRODUCT('Bolts','Bolts'"));
        const auto contents = test::readStepFile(path);
        REQUIRE(contents.has_value());
        CHECK(contents->solids == 1);
        CHECK(contents->valid);
        CHECK_THAT(contents->volumeMm3, WithinRel(BoltCircleModel::expectedVolume(60, 10, 10, 6), kRelStep));
        CHECK_THAT(contents->areaMm2, WithinRel(8700.0 * pi, kRelStep));
        for (const std::size_t axis : {0U, 1U}) {
            CHECK_THAT(contents->minMm[axis], WithinAbs(-60.0, 1e-6));
            CHECK_THAT(contents->maxMm[axis], WithinAbs(60.0, 1e-6));
        }
        CHECK_THAT(contents->minMm[2], WithinAbs(0.0, 1e-6));
        CHECK_THAT(contents->maxMm[2], WithinAbs(10.0, 1e-6));
    }
    SECTION("a ring of separate cubes: one body of four solids") {
        CubeRingModel m;
        const auto path = dir.path() / "ring.step";
        const auto summary = io::exportStep(m.doc, path);
        REQUIRE(summary.has_value());
        REQUIRE(summary->bodies.size() == 1);
        CHECK(summary->bodies[0].name == "Ring");
        const auto contents = test::readStepFile(path);
        REQUIRE(contents.has_value());
        CHECK(contents->solids == 4);
        CHECK(contents->valid);
        CHECK_THAT(contents->volumeMm3, WithinRel(4000.0, kRelStep));
        for (const std::size_t axis : {0U, 1U}) {
            CHECK_THAT(contents->minMm[axis], WithinAbs(-55.0, 1e-6));
            CHECK_THAT(contents->maxMm[axis], WithinAbs(55.0, 1e-6));
        }
    }
}

TEST_CASE("CircularPattern_ExportsClosedStl", "[pattern][circular][io][export][acceptance]") {
    const Length deflection = 0.01_mm;
    TempDir dir;
    for (const io::StlFormat format : {io::StlFormat::Binary, io::StlFormat::Ascii}) {
        DYNAMIC_SECTION("the bolt circle, STL (" << (format == io::StlFormat::Binary ? "binary" : "ASCII") << ")") {
            BoltCircleModel m;
            const double exact = BoltCircleModel::expectedVolume(60, 10, 10, 6);
            const double area = 8700.0 * pi;
            const auto path = dir.path() / "bolts.stl";
            const auto summary = io::exportStl(m.doc, path, {.mesh = {.linearDeflection = deflection}, .format = format});
            REQUIRE(summary.has_value());
            REQUIRE(summary->bodies.size() == 1);
            const std::string bytes = readFile(path);
            const auto mesh = format == io::StlFormat::Binary ? test::parseBinaryStl(bytes) : test::parseAsciiStl(bytes);
            REQUIRE(mesh.has_value());
            CHECK(test::checkSurface(mesh->triangles).watertight());
            // The rim is convex (its chords cut into the material) and the
            // holes concave (their chords cross the holes); every point of
            // the mesh is within the deflection of the surface.
            const double meshed = test::enclosedVolume(mesh->triangles);
            INFO("meshed " << meshed << " mm^3, exact " << exact << " mm^3");
            CHECK(std::abs(meshed - exact) <= deflection.in(units::mm) * area);
        }
    }
    SECTION("separate cubes: four closed shells in one file, as for any body") {
        CubeRingModel m;
        const auto path = dir.path() / "ring.stl";
        const auto summary = io::exportStl(m.doc, path, {.mesh = {.linearDeflection = deflection}});
        REQUIRE(summary.has_value());
        const auto mesh = test::parseBinaryStl(readFile(path));
        REQUIRE(mesh.has_value());
        CHECK(mesh->triangles.size() == 4 * 12);
        CHECK(test::checkSurface(mesh->triangles).watertight());
        // Flat faces are meshed exactly; the quarter-turned vertices are
        // multiples of 5 mm to within 1e-14 mm, exact once stored in single
        // precision.
        CHECK_THAT(test::enclosedVolume(mesh->triangles), WithinRel(4000.0, 1e-12));
    }
}
