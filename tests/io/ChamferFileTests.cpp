#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"
#include "support/BracketModel.hpp"
#include "support/ChamferBlockModel.hpp"
#include "support/MeshAnalysis.hpp"
#include "support/TestFiles.hpp"
#include "support/TurnedPartModel.hpp"
#include "support/occt/StepReadBack.hpp"

#include <bettercad/core/document/DependencyGraph.hpp>
#include <bettercad/core/geometry/Edges.hpp>
#include <bettercad/features/ChamferFeature.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/io/ModelExport.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <bit>
#include <cmath>
#include <cstdint>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::features;
using namespace bettercad::literals;
using bettercad::geometry::ChamferMode;
using bettercad::test::ChamferBlockModel;
using bettercad::test::ChamferVariants;
using bettercad::test::describe;
using bettercad::test::readFile;
using bettercad::test::requireReport;
using bettercad::test::TempDir;
using bettercad::test::TurnedPartModel;
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

/// The turned part with both rims of its top face chamfered (circle references).
struct ChamferedShaft : TurnedPartModel {
    ObjectId rims;

    ChamferedShaft() {
        const auto outer = geometry::circleSignature(Point3D{0_mm, 0_mm, 40_mm}, Direction3D::unitZ(), 15_mm);
        const auto inner = geometry::circleSignature(Point3D{0_mm, 0_mm, 40_mm}, Direction3D::unitZ(), 5_mm);
        REQUIRE(outer.has_value());
        REQUIRE(inner.has_value());
        auto feature = ChamferFeature::create(
            "Rims", {.target = featureId(groove), .edges = {*outer, *inner}, .distance = 2_mm});
        REQUIRE(feature.has_value());
        rims = doc.addObject(std::move(*feature)).value();
    }

    static double expectedVolume() {
        return TurnedPartModel::expectedVolume(15, 40, 360, 5) - pi * 2.0 * 2.0 * (15 + 5);
    }
};

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
/// checks that the model, its IDs and its dependency graph are unchanged.
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

// The Edge chamfer's reference as the file stores it.
constexpr std::string_view kEdgeReference = R"("edges": [
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

TEST_CASE("ChamferFeature_SaveLoadPreservesDefinitionsAndGeometry", "[chamfer][io][acceptance]") {
    TempDir dir;
    ChamferVariants m;
    Regenerator before;
    const RegenerationReport built = requireReport(before, m.doc);
    INFO(describe(built));
    REQUIRE(built.succeeded());
    CHECK_THAT(volumeMm3(before, m.slope), WithinRel(ChamferVariants::expectedVolume(5), kRel));
    const std::vector<ChamferDefinition> definitions{m.definitionOf(m.edge), m.definitionOf(m.bevel),
                                                     m.definitionOf(m.slope)};

    Document loaded = saveDestroyLoad(m.doc, dir.path() / "block.bcad");
    // Every field of every definition, bit for bit: IDs, edge references,
    // mode, distances, angle, reference side and the driving parameter.
    const std::vector<ObjectId> chamfers{m.edge, m.bevel, m.slope};
    for (std::size_t i = 0; i < chamfers.size(); ++i) {
        const auto* chamfer = loaded.findObjectAs<ChamferFeature>(chamfers[i]);
        REQUIRE(chamfer != nullptr);
        CHECK(chamfer->definition() == definitions[i]);
        CHECK(chamfer->name() == std::vector<std::string>{"Edge", "Bevel", "Slope"}[i]);
    }

    Regenerator after;
    const RegenerationReport rebuilt = requireReport(after, loaded);
    CHECK(rebuilt.succeeded());
    CHECK(rebuilt.regenerated == built.regenerated);
    for (const ObjectId feature : {m.pad, m.edge, m.bevel, m.slope}) {
        checkSameGeometry(before, after, feature);
    }

    // The loaded model is still parametric.
    REQUIRE(loaded.setParameterValue(m.size, 3_mm).has_value());
    const RegenerationReport changed = requireReport(after, loaded);
    CHECK(changed.regenerated == std::vector<ObjectId>{m.edge, m.bevel, m.slope});
    CHECK_THAT(volumeMm3(after, m.slope), WithinRel(ChamferVariants::expectedVolume(3), kRel));
}

TEST_CASE("ChamferFeature_SaveLoadPreservesCircleReferences", "[chamfer][io][revolve]") {
    TempDir dir;
    ChamferedShaft m;
    Regenerator before;
    REQUIRE(requireReport(before, m.doc).succeeded());
    CHECK_THAT(volumeMm3(before, m.rims), WithinRel(ChamferedShaft::expectedVolume(), kRel));
    const ChamferDefinition definition = m.doc.findObjectAs<ChamferFeature>(m.rims)->definition();

    Document loaded = saveDestroyLoad(m.doc, dir.path() / "shaft.bcad");
    CHECK(loaded.findObjectAs<ChamferFeature>(m.rims)->definition() == definition);
    Regenerator after;
    REQUIRE(requireReport(after, loaded).succeeded());
    checkSameGeometry(before, after, m.rims);
}

TEST_CASE("ChamferFeature_FailedChamferSavesAndLoadsUnchanged", "[chamfer][io]") {
    // A chamfer whose edge no longer exists is still part of the model: it is
    // saved, loaded and reported the same way, and recovers when fixed.
    TempDir dir;
    ChamferBlockModel m;
    REQUIRE(m.doc.setParameterValue(m.height, 30_mm).has_value());
    Regenerator before;
    const RegenerationReport failed = requireReport(before, m.doc);
    REQUIRE(failed.failed == std::vector<ObjectId>{m.edge});

    Document loaded = saveDestroyLoad(m.doc, dir.path() / "failed.bcad");
    Regenerator after;
    const RegenerationReport again = requireReport(after, loaded);
    CHECK(again.failed == std::vector<ObjectId>{m.edge});
    CHECK(again.errors.at(m.edge).message == failed.errors.at(m.edge).message);

    REQUIRE(loaded.setParameterValue(m.height, 20_mm).has_value());
    REQUIRE(requireReport(after, loaded).succeeded());
    CHECK_THAT(volumeMm3(after, m.edge), WithinRel(98750.0, kRel));
}

TEST_CASE("ChamferFeature_DataIsStoredAsTransparentJson", "[chamfer][io]") {
    ChamferVariants m;
    const auto text = io::documentToJson(m.doc);
    REQUIRE(text.has_value());
    CHECK_THAT(*text, ContainsSubstring(std::string{R"("type": "chamfer",
      "name": "Edge",
      "data": {
        "target": 6,
        )"} + std::string{kEdgeReference} + R"(,
        "mode": "equal_distance",
        "distance": 0.005,
        "distance_parameter": 4
      })"));
    CHECK_THAT(*text, ContainsSubstring(R"("name": "Bevel",
      "data": {
        "target": 7,
        "edges": [
          {
            "curve": "line",
            "point": [
              0.0,
              0.05,
              0.02
            ],
            "direction": [
              1.0,
              0.0,
              0.0
            ]
          }
        ],
        "mode": "two_distance",
        "distance": 0.004,
        "distance2": 0.002,
        "reference_side": [
          0.0,
          0.0,
          1.0
        ]
      })"));
    CHECK_THAT(*text, ContainsSubstring(R"("mode": "distance_angle",
        "distance": 0.003,
        "angle": 0.5235987755982988,
        "reference_side": [
          -0.0,
          -0.0,
          -1.0
        ]
      })"));

    ChamferedShaft shaft;
    const auto shaftText = io::documentToJson(shaft.doc);
    REQUIRE(shaftText.has_value());
    CHECK_THAT(*shaftText, ContainsSubstring(R"({
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
          })"));
}

TEST_CASE("ChamferFeature_MalformedDataIsRejectedWithTheJsonPath", "[chamfer][io]") {
    ChamferBlockModel m;
    const std::string good = io::documentToJson(m.doc).value();
    REQUIRE(io::documentFromJson(good).has_value());
    const auto loadError = [](const std::string& text) {
        const auto loaded = io::documentFromJson(text);
        REQUIRE_FALSE(loaded.has_value());
        UNSCOPED_INFO(loaded.error().message);
        return loaded.error();
    };
    const auto withEdges = [&](std::string_view edges) {
        return replaceOnce(good, kEdgeReference, std::string{"\"edges\": "} + std::string{edges});
    };
    const auto message = [&](const std::string& text) { return loadError(text).message; };

    // Structure: every problem is reported where it is.
    CHECK(message(replaceOnce(good, "\"mode\": \"equal_distance\"", "\"mode\": \"round\"")) ==
          "objects[2].data.mode: unknown value 'round'");
    CHECK(message(withEdges(R"([{"curve": "spline", "point": [0, 0, 0.02], "direction": [1, 0, 0]}])")) ==
          "objects[2].data.edges[0].curve: unknown value 'spline'");
    CHECK(message(withEdges(R"([{"curve": "line", "point": [0, 0, 0.02], "direction": [1, 0, 0], "radius": 1}])")) ==
          "objects[2].data.edges[0].radius: a line reference has no radius");
    CHECK(message(withEdges(R"([{"curve": "circle", "point": [0, 0, 0.02], "axis": [1, 0, 0], "radius": 1}])")) ==
          "objects[2].data.edges[0].point: a circle reference has no point");
    CHECK(message(withEdges(R"([{"curve": "circle", "center": [0, 0, 0.02], "axis": [1, 0, 0]}])")) ==
          "objects[2].data.edges[0].radius: missing required field");
    CHECK(message(withEdges(R"([{"curve": "line", "point": [0, 0.02], "direction": [1, 0, 0]}])")) ==
          "objects[2].data.edges[0].point: expected 3 numbers, got 2");
    CHECK(message(withEdges(R"([{"curve": "line", "point": [0, 0, 0.02], "direction": [2, 0, 0]}])")) ==
          "objects[2].data.edges[0].direction: expected a unit vector");
    CHECK(message(withEdges(R"([{"curve": "line", "point": [0, 0, 0.02], "direction": [1, 0, 0], "note": 1}])")) ==
          "objects[2].data.edges[0].note: unknown field");
    CHECK(message(withEdges(R"({"curve": "line"})")) == "objects[2].data.edges: expected an array");
    CHECK(message(replaceOnce(good, "\"target\": 6", "\"target\": \"Pad\"")) ==
          "objects[2].data.target: expected an ID (a non-negative integer)");

    // Content: the definition's own rules, reported at the chamfer.
    const auto invalid = [&](const std::string& text) {
        const Error error = loadError(text);
        CHECK(error.code == ErrorCode::InvalidArgument);
        return error.message;
    };
    CHECK(invalid(withEdges("[]")) == "objects[2].data: a chamfer needs at least one edge");
    CHECK(invalid(withEdges(R"([{"curve": "line", "point": [0, 0, 0.02], "direction": [1, 0, 0]},
                                {"curve": "line", "point": [0.05, 0, 0.02], "direction": [-1, 0, 0]}])")) ==
          "objects[2].data: edge references 1 and 2 refer to the same line through (50, 0, 20) mm along (-1, 0, 0)");
    CHECK(invalid(withEdges(R"([{"curve": "circle", "center": [0, 0, 0.02], "axis": [1, 0, 0], "radius": 0}])")) ==
          "objects[2].data.edges[0]: a circle edge reference needs a positive radius");
    CHECK(invalid(replaceOnce(good, "\"target\": 6", "\"target\": 0")) ==
          "objects[2].data: a chamfer needs a target feature");
    CHECK(invalid(replaceOnce(good, "\"distance\": 0.005,\n        \"distance_parameter\": 4", "\"distance\": -0.005")) ==
          "objects[2].data: the chamfer distance must be positive and finite, got -5 mm");
    CHECK(invalid(replaceOnce(good, "\"mode\": \"equal_distance\"", "\"mode\": \"equal_distance\", \"distance2\": 0.001")) ==
          "objects[2].data: only a chamfer by two distances takes a second distance");
    CHECK(invalid(replaceOnce(good, "\"mode\": \"equal_distance\"", "\"mode\": \"two_distance\", \"distance2\": 0.001")) ==
          "objects[2].data: a chamfer by two distances needs a reference side");
    CHECK_THAT(invalid(replaceOnce(good, "\"mode\": \"equal_distance\"",
                                   "\"mode\": \"distance_angle\", \"angle\": 1.6, \"reference_side\": [0, 0, 1]")),
               Catch::Matchers::StartsWith("objects[2].data: the chamfer angle must be in (0, 90) deg, got 91.67"));
}

TEST_CASE("ChamferFeature_ExportsToStepAndStl", "[chamfer][io][export][acceptance]") {
    ChamferBlockModel m;
    const double exact = ChamferBlockModel::expectedVolume(100, 50, 20, 5);
    TempDir dir;

    SECTION("STEP reads back as the same solid") {
        const auto path = dir.path() / "block.step";
        const auto summary = io::exportStep(m.doc, path);
        REQUIRE(summary.has_value());
        REQUIRE(summary->bodies.size() == 1);
        CHECK(summary->bodies[0].name == "Edge");
        CHECK_THAT(readFile(path), ContainsSubstring("PRODUCT('Edge','Edge'"));
        const auto contents = test::readStepFile(path);
        REQUIRE(contents.has_value());
        CHECK(contents->solids == 1);
        CHECK(contents->valid);
        CHECK_THAT(contents->volumeMm3, WithinRel(exact, kRelStep));
        CHECK_THAT(contents->areaMm2, WithinRel(16000.0 - 1000.0 - 25.0 + 500.0 * std::sqrt(2.0), kRelStep));
    }
    for (const io::StlFormat format : {io::StlFormat::Binary, io::StlFormat::Ascii}) {
        DYNAMIC_SECTION("STL (" << (format == io::StlFormat::Binary ? "binary" : "ASCII") << ") is closed and exact") {
            const auto path = dir.path() / "block.stl";
            const auto summary = io::exportStl(m.doc, path, {.format = format});
            REQUIRE(summary.has_value());
            REQUIRE(summary->bodies.size() == 1);
            const std::string bytes = readFile(path);
            const auto mesh = format == io::StlFormat::Binary ? test::parseBinaryStl(bytes) : test::parseAsciiStl(bytes);
            REQUIRE(mesh.has_value());
            const test::SurfaceCheck surface = test::checkSurface(mesh->triangles);
            CHECK(surface.watertight());
            // The box's eight corners, less the chamfered edge's two ends, plus
            // the four corners of the chamfer face.
            CHECK(surface.vertices == 10);
            // Every face is planar and every corner lies on a whole millimetre,
            // which 32-bit floats hold exactly, so the mesh is the solid itself.
            CHECK_THAT(test::enclosedVolume(mesh->triangles), WithinRel(exact, kRel));
            CHECK_THAT(test::surfaceArea(mesh->triangles),
                       WithinRel(16000.0 - 1000.0 - 25.0 + 500.0 * std::sqrt(2.0), kRel));
        }
    }
}
