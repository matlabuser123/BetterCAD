#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"
#include "support/BracketModel.hpp"
#include "support/MeshAnalysis.hpp"
#include "support/TestFiles.hpp"
#include "support/TurnedPartModel.hpp"
#include "support/occt/StepReadBack.hpp"

#include <bettercad/core/document/DependencyGraph.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/features/RevolveFeature.hpp>
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
using bettercad::test::describe;
using bettercad::test::readFile;
using bettercad::test::require;
using bettercad::test::requireReport;
using bettercad::test::TempDir;
using bettercad::test::TurnedPartModel;
using bettercad::test::volumeMm3;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinRel;

namespace {

constexpr double pi = std::numbers::pi;

std::uint64_t bits(double value) {
    return std::bit_cast<std::uint64_t>(value);
}

/// The turned part plus a second, independent revolve that uses the other
/// options: the sketch X axis, a literal partial angle and a symmetric sweep.
struct RevolveVariants : TurnedPartModel {
    ObjectId wingSketch, wing;

    RevolveVariants() {
        using namespace bettercad::sketch;
        // A 10 x 5 rectangle at y = 20..25 on the XY plane, turned about the
        // sketch X axis (global X) through 120 degrees, half each way.
        auto sketch = std::make_unique<Sketch>("WingSketch");
        test::addRectangle(*sketch, 100_mm, 20_mm, 10_mm, 5_mm);
        wingSketch = doc.addObject(std::move(sketch)).value();
        wing = addRevolve("Wing", {.profile = sketchId(wingSketch), .axis = RevolveAxis::sketchX(), .angle = 120_deg,
                                   .direction = RevolveDirection::Symmetric});
    }

    static constexpr double kWingVolume = 120.0 / 360.0 * std::numbers::pi * (25.0 * 25 - 20.0 * 20) * 10;
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

std::string replaceOnce(std::string text, std::string_view from, std::string_view to) {
    const auto pos = text.find(from);
    REQUIRE(pos != std::string::npos);
    text.replace(pos, from.size(), to);
    return text;
}

} // namespace

TEST_CASE("P11-FEAT-001 acceptance: revolves survive save, destroy, load and regenerate",
          "[revolve][io][acceptance]") {
    TempDir dir;
    const auto path = dir.path() / "shaft.bcad";
    RevolveVariants m;
    Regenerator before;
    const RegenerationReport built = requireReport(before, m.doc);
    INFO(describe(built));
    REQUIRE(built.succeeded());
    CHECK_THAT(volumeMm3(before, m.groove), WithinRel(TurnedPartModel::expectedVolume(15, 40, 360, 5), 1e-12));
    CHECK_THAT(volumeMm3(before, m.wing), WithinRel(RevolveVariants::kWingVolume, 1e-12));

    const Document expected = m.doc.clone();
    REQUIRE(io::saveDocument(m.doc, path).has_value());
    m.doc = Document("Closed"); // destroy the original; keep only the IDs

    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());
    CHECK(equivalent(expected, *loaded));
    CHECK(loaded->itemIds() == expected.itemIds());
    CHECK(loaded->findObjectAs<RevolveFeature>(m.groove)->definition() ==
          expected.findObjectAs<RevolveFeature>(m.groove)->definition());
    const DocumentGraph a = buildDependencyGraph(expected);
    const DocumentGraph b = buildDependencyGraph(*loaded);
    REQUIRE(a.graph.nodes() == b.graph.nodes());
    for (const ObjectId node : a.graph.nodes()) {
        CHECK(a.graph.dependenciesOf(node) == b.graph.dependenciesOf(node));
    }

    Regenerator after;
    const RegenerationReport rebuilt = requireReport(after, *loaded);
    CHECK(rebuilt.succeeded());
    CHECK(rebuilt.regenerated == built.regenerated);
    for (const ObjectId feature : {m.turn, m.boreCut, m.groove, m.wing}) {
        checkSameGeometry(before, after, feature);
    }
    CHECK(equivalent(expected, *loaded)); // the solved state was saved

    // The loaded model is still parametric.
    REQUIRE(loaded->setParameterValue(m.sweep, 180_deg).has_value());
    REQUIRE(requireReport(after, *loaded).succeeded());
    CHECK_THAT(volumeMm3(after, m.groove), WithinRel(TurnedPartModel::expectedVolume(15, 40, 180, 5), 1e-12));
}

TEST_CASE("Revolve data is stored as transparent JSON", "[revolve][io]") {
    TurnedPartModel m;
    const auto text = io::documentToJson(m.doc);
    REQUIRE(text.has_value());
    CHECK_THAT(*text, ContainsSubstring(R"("type": "revolve",
      "name": "Turn",
      "data": {
        "profile": 5,
        "axis": {
          "type": "sketch_y"
        },
        "angle": 6.283185307179586,
        "angle_parameter": 3,
        "direction": "positive",
        "operation": "new_body"
      })"));
    CHECK_THAT(*text, ContainsSubstring(R"("type": "revolve",
      "name": "Groove",
      "data": {
        "profile": 9,
        "axis": {
          "type": "line",
          "line": 11
        },
        "angle": 6.283185307179586,
        "direction": "positive",
        "operation": "cut",
        "target": 8
      })"));
}

TEST_CASE("Malformed revolve data is rejected with the JSON path", "[revolve][io]") {
    TurnedPartModel m;
    const std::string good = io::documentToJson(m.doc).value();
    REQUIRE(io::documentFromJson(good).has_value());
    const auto loadError = [](const std::string& text) {
        const auto loaded = io::documentFromJson(text);
        REQUIRE_FALSE(loaded.has_value());
        UNSCOPED_INFO(loaded.error().message);
        return loaded.error();
    };
    const std::string grooveAxis = "\"axis\": {\n          \"type\": \"line\",\n          \"line\": 11\n        }";
    const std::string turnAxis = "\"axis\": {\n          \"type\": \"sketch_y\"\n        }";

    CHECK(loadError(replaceOnce(good, turnAxis, R"("axis": {"type": "diagonal"})")).message ==
          "objects[1].data.axis.type: unknown value 'diagonal'");
    CHECK(loadError(replaceOnce(good, turnAxis, R"("axis": {"type": "sketch_y", "line": 4})")).message ==
          "objects[1].data.axis.line: only a line axis refers to a line");
    CHECK(loadError(replaceOnce(good, grooveAxis, R"("axis": {"type": "line"})")).message ==
          "objects[5].data.axis.line: missing required field");
    CHECK(loadError(replaceOnce(good, turnAxis, R"("axis": "sketch_y")")).message ==
          "objects[1].data.axis: expected an object");
    CHECK(loadError(replaceOnce(good, "\"direction\": \"positive\"", "\"direction\": \"clockwise\"")).message ==
          "objects[1].data.direction: unknown value 'clockwise'");
    // A literal angle outside (0, 360] degrees (the Groove has no parameter).
    const Error angle = loadError(replaceOnce(good, "\"line\": 11\n        },\n        \"angle\": 6.283185307179586",
                                              "\"line\": 11\n        },\n        \"angle\": 7.0"));
    CHECK(angle.code == ErrorCode::InvalidArgument);
    CHECK_THAT(angle.message, ContainsSubstring("objects[5].data: revolve angle must be in (0, 360] deg"));
}

TEST_CASE("Revolved bodies export to STEP and STL", "[revolve][io][export]") {
    TurnedPartModel m;
    REQUIRE(m.doc.setParameterValue(m.sweep, 270_deg).has_value()); // an open wedge, not just a full turn
    const double exact = TurnedPartModel::expectedVolume(15, 40, 270, 5);
    TempDir dir;

    SECTION("STEP reads back as the same solid") {
        const auto path = dir.path() / "shaft.step";
        const auto summary = io::exportStep(m.doc, path);
        REQUIRE(summary.has_value());
        REQUIRE(summary->bodies.size() == 1);
        CHECK(summary->bodies[0].name == "Groove");
        CHECK_THAT(readFile(path), ContainsSubstring("PRODUCT('Groove','Groove'"));
        const auto contents = test::readStepFile(path);
        REQUIRE(contents.has_value());
        CHECK(contents->solids == 1);
        CHECK(contents->valid);
        CHECK_THAT(contents->volumeMm3, WithinRel(exact, 1e-9));
    }
    SECTION("STL is a closed surface within the deflection of the solid") {
        const auto path = dir.path() / "shaft.stl";
        const auto summary = io::exportStl(m.doc, path, {.mesh = {.linearDeflection = 0.01_mm}});
        REQUIRE(summary.has_value());
        const auto mesh = test::parseBinaryStl(readFile(path));
        REQUIRE(mesh.has_value());
        CHECK(test::checkSurface(mesh->triangles).watertight());
        Regenerator regenerator;
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        const auto props = regenerator.body(m.groove)->massProperties().value();
        const double meshed = test::enclosedVolume(mesh->triangles);
        CHECK(meshed < exact);
        CHECK(exact - meshed <= 0.01 * props.surfaceArea.in(units::mm2));
    }
}
