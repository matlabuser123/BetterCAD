#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"
#include "support/BracketModel.hpp"
#include "support/HoleModels.hpp"
#include "support/MeshAnalysis.hpp"
#include "support/TestFiles.hpp"
#include "support/occt/StepReadBack.hpp"

#include <bettercad/core/document/DependencyGraph.hpp>
#include <bettercad/features/HoleFeature.hpp>
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
using bettercad::geometry::HoleExtent;
using bettercad::test::bottomFace;
using bettercad::test::describe;
using bettercad::test::HoleBlockModel;
using bettercad::test::HoleVariants;
using bettercad::test::readFile;
using bettercad::test::requireReport;
using bettercad::test::TempDir;
using bettercad::test::volumeMm3;
using Catch::Matchers::ContainsSubstring;
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

// The top face of the block as the file stores it.
constexpr std::string_view kTopFace = R"("face": {
          "surface": "plane",
          "point": [
            0.0,
            0.0,
            0.02
          ],
          "normal": [
            0.0,
            0.0,
            1.0
          ]
        })";

// Drill's diameter as the file stores it.
constexpr std::string_view kDrillDiameter = "\"diameter\": 0.01,\n        \"diameter_parameter\": 4";

} // namespace

TEST_CASE("HoleFeature_SaveLoadPreservesEngineeringIntent", "[hole][io][acceptance]") {
    TempDir dir;
    HoleVariants m;
    // Pocket's depth is driven, so a depth parameter is saved too.
    const ParameterId pocketDepth = m.doc.createParameter("pocket_depth", 12_mm, units::mm).value();
    HoleDefinition pocket = m.definitionOf(m.pocket);
    pocket.depth = 0_mm;
    pocket.depthParameter = pocketDepth;
    m.setDefinition(m.pocket, pocket);
    Regenerator before;
    const RegenerationReport built = requireReport(before, m.doc);
    INFO(describe(built));
    REQUIRE(built.succeeded());
    CHECK_THAT(volumeMm3(before, m.sink), WithinRel(HoleVariants::expectedVolume(10), kRel));
    const HoleDefinition drill = m.definitionOf(m.drill);
    const HoleDefinition sink = m.definitionOf(m.sink);

    Document loaded = saveDestroyLoad(m.doc, dir.path() / "block.bcad");
    // Every field, bit for bit: target, face reference, centre, type, extent,
    // dimensions and their parameters.
    CHECK(loaded.findObjectAs<HoleFeature>(m.drill)->definition() == drill);
    CHECK(loaded.findObjectAs<HoleFeature>(m.pocket)->definition() == pocket);
    CHECK(loaded.findObjectAs<HoleFeature>(m.sink)->definition() == sink);
    CHECK(loaded.findObjectAs<HoleFeature>(m.sink)->name() == "Sink");

    Regenerator after;
    const RegenerationReport rebuilt = requireReport(after, loaded);
    CHECK(rebuilt.succeeded());
    CHECK(rebuilt.regenerated == built.regenerated); // same order
    for (const ObjectId feature : {m.pad, m.drill, m.pocket, m.sink}) {
        checkSameGeometry(before, after, feature);
    }

    // The loaded model is still parametric: diameter, depth and centre.
    REQUIRE(loaded.setParameterValue(m.diameter, 12_mm).has_value());
    const RegenerationReport changed = requireReport(after, loaded);
    CHECK(changed.regenerated == std::vector<ObjectId>{m.drill, m.pocket, m.sink});
    CHECK_THAT(volumeMm3(after, m.sink), WithinRel(HoleVariants::expectedVolume(12), kRel));
    REQUIRE(loaded.setParameterValue(pocketDepth, 10_mm).has_value());
    CHECK(requireReport(after, loaded).regenerated == std::vector<ObjectId>{m.pocket, m.sink});
    CHECK_THAT(volumeMm3(after, m.sink), WithinRel(HoleVariants::expectedVolume(12) + pi * 9.0 * 2.0, kRel));
    REQUIRE(loaded.setParameterValue(m.holeX, 60_mm).has_value());
    CHECK(requireReport(after, loaded).regenerated == std::vector<ObjectId>{m.drill, m.pocket, m.sink});
    CHECK_THAT(volumeMm3(after, m.sink), WithinRel(HoleVariants::expectedVolume(12) + pi * 9.0 * 2.0, kRel));
}

TEST_CASE("HoleFeature_SaveLoadKeepsAThroughHoleThrough", "[hole][io][acceptance]") {
    // A through hole from the bottom face: after loading, a thicker block is
    // still drilled through.
    TempDir dir;
    HoleBlockModel m;
    HoleDefinition d = m.definitionOf(m.drill);
    d.face = bottomFace();
    m.setDefinition(m.drill, d);
    Regenerator before;
    REQUIRE(requireReport(before, m.doc).succeeded());

    Document loaded = saveDestroyLoad(m.doc, dir.path() / "through.bcad");
    CHECK(loaded.findObjectAs<HoleFeature>(m.drill)->definition() == d);
    CHECK(loaded.findObjectAs<HoleFeature>(m.drill)->definition().face.normal == Direction3D::unitZ().reversed());
    Regenerator after;
    REQUIRE(requireReport(after, loaded).succeeded());
    checkSameGeometry(before, after, m.drill);

    REQUIRE(loaded.setParameterValue(m.height, 40_mm).has_value());
    REQUIRE(requireReport(after, loaded).succeeded());
    CHECK_THAT(volumeMm3(after, m.drill), WithinRel(HoleBlockModel::expectedVolume(100, 50, 40, 10), kRel));
}

TEST_CASE("HoleFeature_FailedHoleSavesAndLoadsUnchanged", "[hole][io]") {
    // A hole whose face no longer exists is still part of the model.
    TempDir dir;
    HoleBlockModel m;
    REQUIRE(m.doc.setParameterValue(m.height, 30_mm).has_value());
    Regenerator before;
    const RegenerationReport failed = requireReport(before, m.doc);
    REQUIRE(failed.failed == std::vector<ObjectId>{m.drill});

    Document loaded = saveDestroyLoad(m.doc, dir.path() / "failed.bcad");
    Regenerator after;
    const RegenerationReport again = requireReport(after, loaded);
    CHECK(again.failed == std::vector<ObjectId>{m.drill});
    CHECK(again.errors.at(m.drill).message == failed.errors.at(m.drill).message);

    REQUIRE(loaded.setParameterValue(m.height, 20_mm).has_value());
    REQUIRE(requireReport(after, loaded).succeeded());
    CHECK_THAT(volumeMm3(after, m.drill), WithinRel(HoleBlockModel::expectedVolume(100, 50, 20, 10), kRel));
}

TEST_CASE("HoleFeature_DataIsStoredAsTransparentJson", "[hole][io]") {
    HoleVariants m;
    const auto text = io::documentToJson(m.doc);
    REQUIRE(text.has_value());
    // A through hole has no depth in the file.
    CHECK_THAT(*text, ContainsSubstring(std::string{R"("type": "hole",
      "name": "Drill",
      "data": {
        "target": 6,
        )"} + std::string{kTopFace} + R"(,
        "center": [
          0.05,
          0.025
        ],
        "center_u_parameter": 7,
        "center_v_parameter": 8,
        "type": "simple",
        "extent": "through",
        "diameter": 0.01,
        "diameter_parameter": 4
      })"));
    CHECK_THAT(*text, ContainsSubstring(std::string{R"("name": "Pocket",
      "data": {
        "target": 9,
        )"} + std::string{kTopFace} + R"(,
        "center": [
          0.02,
          0.025
        ],
        "type": "counterbore",
        "extent": "blind",
        "diameter": 0.006,
        "depth": 0.012,
        "counterbore_diameter": 0.01,
        "counterbore_depth": 0.004
      })"));
    CHECK_THAT(*text, ContainsSubstring(R"("type": "countersink",
        "extent": "through",
        "diameter": 0.006,
        "countersink_diameter": 0.012,
        "countersink_angle": 1.5707963267948966
      })"));

    // The bottom face faces down; the file has no negative zeros.
    HoleBlockModel below;
    HoleDefinition d = below.definitionOf(below.drill);
    d.face = bottomFace();
    below.setDefinition(below.drill, d);
    const auto belowText = io::documentToJson(below.doc);
    REQUIRE(belowText.has_value());
    CHECK_THAT(*belowText, ContainsSubstring(R"("face": {
          "surface": "plane",
          "point": [
            0.0,
            0.0,
            0.0
          ],
          "normal": [
            0.0,
            0.0,
            -1.0
          ]
        })"));
    CHECK_THAT(*belowText, !ContainsSubstring("-0.0"));
}

TEST_CASE("HoleFeature_MalformedDataIsRejectedWithTheJsonPath", "[hole][io]") {
    HoleBlockModel m;
    const std::string good = io::documentToJson(m.doc).value();
    REQUIRE(io::documentFromJson(good).has_value());
    const auto loadError = [](const std::string& text) {
        const auto loaded = io::documentFromJson(text);
        REQUIRE_FALSE(loaded.has_value());
        UNSCOPED_INFO(loaded.error().message);
        return loaded.error();
    };
    const auto message = [&](const std::string& text) { return loadError(text).message; };
    const std::string diameter{kDrillDiameter};

    // Structure: every problem is reported where it is.
    CHECK(message(replaceOnce(good, kDrillDiameter, "\"diameter\": \"10 mm\", \"diameter_parameter\": 4")) ==
          "objects[2].data.diameter: expected a number");
    CHECK(message(replaceOnce(good, kDrillDiameter, "\"diameter_parameter\": 4")) ==
          "objects[2].data.diameter: missing required field");
    // Spotfaces and threads are known since P12-HOLE-001, and checked: a
    // spotface needs its dimensions, and a thread is an object.
    CHECK(message(replaceOnce(good, "\"type\": \"simple\"", "\"type\": \"spotface\"")) ==
          "objects[2].data: the spotface diameter must be positive and finite, got 0 mm");
    CHECK(message(replaceOnce(good, "\"extent\": \"through\"", "\"extent\": \"up_to_next\"")) ==
          "objects[2].data.extent: unknown value 'up_to_next'");
    CHECK(message(replaceOnce(good, kDrillDiameter, diameter + ", \"thread\": \"M10\"")) ==
          "objects[2].data.thread: expected an object");
    CHECK(message(replaceOnce(good, kTopFace,
                              R"("face": {"surface": "cylinder", "point": [0, 0, 0.02], "normal": [0, 0, 1]})")) ==
          "objects[2].data.face.surface: unknown value 'cylinder'");
    CHECK(message(replaceOnce(good, kTopFace,
                              R"("face": {"surface": "plane", "point": [0, 0, 0.02], "normal": [0, 0, 2]})")) ==
          "objects[2].data.face.normal: expected a unit vector");
    CHECK(message(replaceOnce(good, kTopFace,
                              R"("face": {"surface": "plane", "point": [0, 0.02], "normal": [0, 0, 1]})")) ==
          "objects[2].data.face.point: expected 3 numbers, got 2");
    CHECK(message(replaceOnce(good, kTopFace,
                              R"("face": {"surface": "plane", "point": [0, 0, 0.02], "normal": [0, 0, 1], "id": 7})")) ==
          "objects[2].data.face.id: unknown field");
    CHECK(message(replaceOnce(good, kTopFace, R"("face": 7)")) == "objects[2].data.face: expected an object");
    CHECK(message(replaceOnce(good, "\"center\": [\n          0.05,\n          0.025\n        ]", "\"center\": [0.05]")) ==
          "objects[2].data.center: expected 2 numbers, got 1");

    // Content: the definition's own rules, reported at the hole.
    const auto invalid = [&](const std::string& text) {
        const Error error = loadError(text);
        CHECK(error.code == ErrorCode::InvalidArgument);
        return error.message;
    };
    CHECK(invalid(replaceOnce(good, "\"extent\": \"through\"", "\"extent\": \"blind\"")) ==
          "objects[2].data: the hole depth must be positive and finite, got 0 mm");
    CHECK(invalid(replaceOnce(good, kDrillDiameter, diameter + ", \"depth\": 0.005")) ==
          "objects[2].data: a through hole takes no depth; it goes through all material");
    CHECK(invalid(replaceOnce(good, kDrillDiameter, "\"diameter\": -0.01")) ==
          "objects[2].data: the hole diameter must be positive and finite, got -10 mm");
    CHECK(invalid(replaceOnce(good, kDrillDiameter, diameter + ", \"counterbore_depth\": 0.004")) ==
          "objects[2].data: only a counterbore hole takes counterbore dimensions");
    CHECK(invalid(replaceOnce(good, "\"target\": 6", "\"target\": 0")) == "objects[2].data: a hole needs a target feature");
}

TEST_CASE("HoleFeature_ExportsStep", "[hole][io][export][acceptance]") {
    HoleVariants m;
    TempDir dir;
    const auto path = dir.path() / "block.step";
    const auto summary = io::exportStep(m.doc, path);
    REQUIRE(summary.has_value());
    REQUIRE(summary->bodies.size() == 1);
    CHECK(summary->bodies[0].name == "Sink");
    CHECK_THAT(readFile(path), ContainsSubstring("PRODUCT('Sink','Sink'"));
    const auto contents = test::readStepFile(path);
    REQUIRE(contents.has_value());
    CHECK(contents->solids == 1);
    CHECK(contents->valid);
    CHECK_THAT(contents->volumeMm3, WithinRel(HoleVariants::expectedVolume(10), kRelStep));
    CHECK_THAT(contents->areaMm2, WithinRel(HoleVariants::expectedArea(), kRelStep));
}

TEST_CASE("HoleFeature_ExportsClosedStl", "[hole][io][export][acceptance]") {
    HoleVariants m;
    const double exact = HoleVariants::expectedVolume(10);
    const double area = HoleVariants::expectedArea();
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
            // The hole walls are concave: their triangles are chords across the
            // hole, outside the material, so the mesh encloses slightly more,
            // and each point of the surface is within the deflection of it.
            const double meshed = test::enclosedVolume(mesh->triangles);
            CHECK(meshed > exact);
            CHECK(meshed - exact <= deflection.in(units::mm) * area);
        }
    }
}
