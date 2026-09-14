#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"
#include "support/BracketModel.hpp"
#include "support/MeshAnalysis.hpp"
#include "support/PatternModels.hpp"
#include "support/TestFiles.hpp"
#include "support/occt/StepReadBack.hpp"

#include <bettercad/core/document/DependencyGraph.hpp>
#include <bettercad/features/HoleFeature.hpp>
#include <bettercad/features/LinearPatternFeature.hpp>
#include <bettercad/features/Regeneration.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/io/ModelExport.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <bit>
#include <cstdint>
#include <numbers>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::features;
using namespace bettercad::literals;
using bettercad::test::CubeRowModel;
using bettercad::test::describe;
using bettercad::test::HoleRowModel;
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

/// The hole row as a 4 x 3 grid: the drill at (20, 13), 20 mm along X (the
/// pitch parameter) by 12 mm along Y.
void makeGrid(HoleRowModel& m) {
    HoleDefinition drill = m.holeOf(m.drill);
    drill.center = Point2D{20_mm, 13_mm};
    m.setHole(m.drill, drill);
    LinearPatternDefinition grid = m.patternOf(m.holes);
    grid.first.countParameter.reset();
    grid.first.count = 4;
    grid.second = PatternDirection{.direction = {0.0, 1.0, 0.0}, .count = 3, .spacing = 12_mm};
    m.setPattern(m.holes, grid);
}

// Holes' first direction as the file stores it.
constexpr std::string_view kHolesFirst = R"("first": {
          "direction": [
            1.0,
            0.0,
            0.0
          ],
          "count": 5,
          "count_parameter": 7,
          "spacing": 0.02,
          "spacing_parameter": 8
        })";

} // namespace

TEST_CASE("LinearPattern_SaveLoadPreservesDefinition", "[pattern][io][acceptance]") {
    TempDir dir;
    HoleRowModel m;
    makeGrid(m);
    Regenerator before;
    const RegenerationReport built = requireReport(before, m.doc);
    INFO(describe(built));
    REQUIRE(built.succeeded());
    CHECK_THAT(volumeMm3(before, m.holes), WithinRel(HoleRowModel::expectedVolume(120, 20, 10, 12), kRel));
    const LinearPatternDefinition holes = m.patternOf(m.holes);
    const auto instances = resolvePatternInstances(holes, m.doc);
    REQUIRE(instances.has_value());

    Document loaded = saveDestroyLoad(m.doc, dir.path() / "grid.bcad");
    // Every field, bit for bit: the source's ID, both directions, counts,
    // spacings and their parameters; so the same instances in the same order.
    CHECK(loaded.findObjectAs<LinearPatternFeature>(m.holes)->definition() == holes);
    CHECK(loaded.findObjectAs<LinearPatternFeature>(m.holes)->definition().source == FeatureId::fromValue(m.drill.value()));
    CHECK(loaded.findObjectAs<LinearPatternFeature>(m.holes)->name() == "Holes");
    CHECK(resolvePatternInstances(loaded.findObjectAs<LinearPatternFeature>(m.holes)->definition(), loaded).value() ==
          *instances);

    Regenerator after;
    const RegenerationReport rebuilt = requireReport(after, loaded);
    CHECK(rebuilt.succeeded());
    CHECK(rebuilt.regenerated == built.regenerated); // same order
    for (const ObjectId feature : {m.pad, m.drill, m.holes}) {
        checkSameGeometry(before, after, feature);
    }

    // The loaded model is still parametric: the source and the pattern.
    REQUIRE(loaded.setParameterValue(m.diameter, 8_mm).has_value());
    CHECK(requireReport(after, loaded).regenerated == std::vector<ObjectId>{m.drill, m.holes});
    CHECK_THAT(volumeMm3(after, m.holes), WithinRel(HoleRowModel::expectedVolume(120, 20, 8, 12), kRel));
    REQUIRE(loaded.setParameterValue(m.pitch, 22_mm).has_value());
    CHECK(requireReport(after, loaded).regenerated == std::vector<ObjectId>{m.holes});
    CHECK_THAT(volumeMm3(after, m.holes), WithinRel(HoleRowModel::expectedVolume(120, 20, 8, 12), kRel));
}

TEST_CASE("LinearPattern_SaveLoadKeepsTheDirectionAsGiven", "[pattern][io]") {
    // (1, 1, 0) is stored as written and normalized only when used.
    TempDir dir;
    CubeRowModel m;
    LinearPatternDefinition d = m.definitionOf(m.row);
    d.first.direction = {1.0, 1.0, 0.0};
    m.setDefinition(m.row, d);
    Regenerator before;
    REQUIRE(requireReport(before, m.doc).succeeded());
    const auto text = io::documentToJson(m.doc);
    REQUIRE(text.has_value());
    CHECK_THAT(*text, ContainsSubstring(R"("direction": [
            1.0,
            1.0,
            0.0
          ])"));

    Document loaded = saveDestroyLoad(m.doc, dir.path() / "diagonal.bcad");
    CHECK(loaded.findObjectAs<LinearPatternFeature>(m.row)->definition().first.direction == Vector3D{1.0, 1.0, 0.0});
    Regenerator after;
    REQUIRE(requireReport(after, loaded).succeeded());
    checkSameGeometry(before, after, m.row);
}

TEST_CASE("LinearPattern_FailedPatternSavesAndLoadsUnchanged", "[pattern][io]") {
    // A pattern whose last instance does not fit is still part of the model.
    TempDir dir;
    HoleRowModel m;
    REQUIRE(m.doc.setParameterValue(m.count, 6.0, kUnitless).has_value());
    Regenerator before;
    const RegenerationReport failed = requireReport(before, m.doc);
    REQUIRE(failed.failed == std::vector<ObjectId>{m.holes});

    Document loaded = saveDestroyLoad(m.doc, dir.path() / "failed.bcad");
    Regenerator after;
    const RegenerationReport again = requireReport(after, loaded);
    CHECK(again.failed == std::vector<ObjectId>{m.holes});
    CHECK(again.errors.at(m.holes).message == failed.errors.at(m.holes).message);

    REQUIRE(loaded.setParameterValue(m.count, 5.0, kUnitless).has_value());
    REQUIRE(requireReport(after, loaded).succeeded());
    CHECK_THAT(volumeMm3(after, m.holes), WithinRel(HoleRowModel::expectedVolume(120, 20, 10, 5), kRel));
}

TEST_CASE("LinearPattern_DataIsStoredAsTransparentJson", "[pattern][io]") {
    HoleRowModel m;
    const auto text = io::documentToJson(m.doc);
    REQUIRE(text.has_value());
    // One direction: no "second".
    CHECK_THAT(*text, ContainsSubstring(std::string{R"("type": "linear_pattern",
      "name": "Holes",
      "data": {
        "source": 9,
        )"} + std::string{kHolesFirst} + R"(
      })"));

    makeGrid(m);
    const auto gridText = io::documentToJson(m.doc);
    REQUIRE(gridText.has_value());
    CHECK_THAT(*gridText, ContainsSubstring(R"("first": {
          "direction": [
            1.0,
            0.0,
            0.0
          ],
          "count": 4,
          "spacing": 0.02,
          "spacing_parameter": 8
        },
        "second": {
          "direction": [
            0.0,
            1.0,
            0.0
          ],
          "count": 3,
          "spacing": 0.012
        }
      })"));
}

TEST_CASE("LinearPattern_MalformedDataIsRejectedWithTheJsonPath", "[pattern][io]") {
    HoleRowModel m;
    const std::string good = io::documentToJson(m.doc).value();
    REQUIRE(io::documentFromJson(good).has_value());
    const auto loadError = [](const std::string& text) {
        const auto loaded = io::documentFromJson(text);
        REQUIRE_FALSE(loaded.has_value());
        UNSCOPED_INFO(loaded.error().message);
        return loaded.error();
    };
    const auto message = [&](const std::string& text) { return loadError(text).message; };
    const std::string count = "\"count\": 5,\n          \"count_parameter\": 7";

    // Structure: every problem is reported where it is.
    CHECK(message(replaceOnce(good, count, "\"count\": \"5\"")) ==
          "objects[3].data.first.count: expected a non-negative integer");
    CHECK(message(replaceOnce(good, count, "\"count\": -2")) ==
          "objects[3].data.first.count: expected a non-negative integer");
    CHECK(message(replaceOnce(good, count, "\"count\": 5000000000")) ==
          "objects[3].data.first.count: expected at most 4294967295");
    CHECK(message(replaceOnce(good, kHolesFirst, R"("first": {"direction": [1, 0], "count": 5, "spacing": 0.02})")) ==
          "objects[3].data.first.direction: expected 3 numbers, got 2");
    CHECK(message(replaceOnce(good, kHolesFirst, R"("second": {"direction": [0, 1, 0], "count": 2, "spacing": 0.01})")) ==
          "objects[3].data.first: missing required field");
    // Modes that do not exist are refused rather than ignored.
    CHECK(message(replaceOnce(good, "\"source\": 9", "\"source\": 9, \"mode\": \"symmetric\"")) ==
          "objects[3].data.mode: unknown field");
    CHECK(message(replaceOnce(good, count, count + ", \"total_length\": 0.1")) ==
          "objects[3].data.first.total_length: unknown field");

    // Content: the definition's own rules, reported at the pattern.
    const auto invalid = [&](const std::string& text) {
        const Error error = loadError(text);
        CHECK(error.code == ErrorCode::InvalidArgument);
        return error.message;
    };
    CHECK(invalid(replaceOnce(good, kHolesFirst,
                              R"("first": {"direction": [0, 0, 0], "count": 5, "spacing": 0.02})")) ==
          "objects[3].data: direction 1: the direction must be a finite, non-zero vector, got (0, 0, 0)");
    CHECK(invalid(replaceOnce(good, count, "\"count\": 0")) == "objects[3].data: direction 1: the count must be at "
                                                              "least 1, got 0");
    CHECK(invalid(replaceOnce(good, "\"spacing\": 0.02,\n          \"spacing_parameter\": 8", "\"spacing\": -0.02")) ==
          "objects[3].data: direction 1: the spacing must be positive and finite, got -20 mm");
    CHECK(invalid(replaceOnce(good, "\"source\": 9", "\"source\": 0")) ==
          "objects[3].data: a linear pattern needs a source feature");
    CHECK(invalid(replaceOnce(good, kHolesFirst,
                              std::string{kHolesFirst} +
                                  R"(, "second": {"direction": [2, 0, 0], "count": 2, "spacing": 0.01})")) ==
          "objects[3].data: the two directions must not be parallel, got (1, 0, 0) and (2, 0, 0)");
}

TEST_CASE("LinearPattern_ExportsStep", "[pattern][io][export][acceptance]") {
    TempDir dir;
    SECTION("a block with a row of holes") {
        HoleRowModel m;
        const auto path = dir.path() / "holes.step";
        const auto summary = io::exportStep(m.doc, path);
        REQUIRE(summary.has_value());
        REQUIRE(summary->bodies.size() == 1);
        CHECK(summary->bodies[0].name == "Holes");
        CHECK_THAT(readFile(path), ContainsSubstring("PRODUCT('Holes','Holes'"));
        const auto contents = test::readStepFile(path);
        REQUIRE(contents.has_value());
        CHECK(contents->solids == 1);
        CHECK(contents->valid);
        CHECK_THAT(contents->volumeMm3, WithinRel(HoleRowModel::expectedVolume(120, 20, 10, 5), kRelStep));
        CHECK_THAT(contents->areaMm2,
                   WithinRel(2.0 * (6000.0 + 2400.0 + 1000.0) + 5.0 * (200.0 * pi - 50.0 * pi), kRelStep));
        CHECK_THAT(contents->maxMm[0], WithinAbs(120.0, 1e-6));
        CHECK_THAT(contents->maxMm[2], WithinAbs(20.0, 1e-6));
    }
    SECTION("a row of separate cubes: one body of four solids") {
        CubeRowModel m;
        const auto path = dir.path() / "cubes.step";
        const auto summary = io::exportStep(m.doc, path);
        REQUIRE(summary.has_value());
        REQUIRE(summary->bodies.size() == 1);
        CHECK(summary->bodies[0].name == "Row");
        const auto contents = test::readStepFile(path);
        REQUIRE(contents.has_value());
        CHECK(contents->solids == 4);
        CHECK(contents->valid);
        CHECK_THAT(contents->volumeMm3, WithinRel(4000.0, kRelStep));
        CHECK_THAT(contents->minMm[0], WithinAbs(0.0, 1e-6));
        CHECK_THAT(contents->maxMm[0], WithinAbs(70.0, 1e-6));
    }
}

TEST_CASE("LinearPattern_ExportsStl", "[pattern][io][export][acceptance]") {
    const Length deflection = 0.01_mm;
    TempDir dir;
    for (const io::StlFormat format : {io::StlFormat::Binary, io::StlFormat::Ascii}) {
        DYNAMIC_SECTION("holes, STL (" << (format == io::StlFormat::Binary ? "binary" : "ASCII") << ")") {
            HoleRowModel m;
            const double exact = HoleRowModel::expectedVolume(120, 20, 10, 5);
            const double area = 2.0 * (6000.0 + 2400.0 + 1000.0) + 5.0 * (200.0 * pi - 50.0 * pi);
            const auto path = dir.path() / "holes.stl";
            const auto summary = io::exportStl(m.doc, path, {.mesh = {.linearDeflection = deflection}, .format = format});
            REQUIRE(summary.has_value());
            REQUIRE(summary->bodies.size() == 1);
            const std::string bytes = readFile(path);
            const auto mesh = format == io::StlFormat::Binary ? test::parseBinaryStl(bytes) : test::parseAsciiStl(bytes);
            REQUIRE(mesh.has_value());
            CHECK(test::checkSurface(mesh->triangles).watertight());
            // The hole walls are concave: their triangles are chords across the
            // holes, outside the material, so the mesh encloses slightly more,
            // and each point of the surface is within the deflection of it.
            const double meshed = test::enclosedVolume(mesh->triangles);
            CHECK(meshed > exact);
            CHECK(meshed - exact <= deflection.in(units::mm) * area);
        }
    }
    SECTION("separate cubes: four closed shells in one file, as for any body") {
        CubeRowModel m;
        const auto path = dir.path() / "cubes.stl";
        const auto summary = io::exportStl(m.doc, path, {.mesh = {.linearDeflection = deflection}});
        REQUIRE(summary.has_value());
        const auto mesh = test::parseBinaryStl(readFile(path));
        REQUIRE(mesh.has_value());
        CHECK(mesh->triangles.size() == 4 * 12);
        CHECK(test::checkSurface(mesh->triangles).watertight());
        // Flat faces are meshed exactly; the vertices (multiples of 10 mm) are
        // exact in single precision.
        CHECK_THAT(test::enclosedVolume(mesh->triangles), WithinRel(4000.0, 1e-12));
    }
}
