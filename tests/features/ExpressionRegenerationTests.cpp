#include "FeatureTestSupport.hpp"
#include "TestHelpers.hpp"
#include "support/DrivenPlateModel.hpp"

#include <bettercad/core/document/Commands.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/features/ResultBodies.hpp>
#include <bettercad/features/Validation.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <format>
#include <memory>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::features;
using namespace bettercad::literals;
using bettercad::test::DrivenPlateModel;
using bettercad::test::requireReport;
using bettercad::test::volumeMm3;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// P12-PARAM-001: parameter expressions drive geometry through regeneration.

namespace {

// Volumes of planar-faced prisms with cylindrical holes agree with the
// analytic value to rounding (P11 measured 1.2e-13 for such parts).
constexpr double kVolumeRel = 1e-12;

std::uint64_t bits(double value) {
    return std::bit_cast<std::uint64_t>(value);
}

/// Errors of a regeneration report, for failure messages.
std::string describeFailures(const RegenerationReport& report) {
    std::string text;
    for (const auto& [id, error] : report.errors) {
        text += std::format("{}: {}\n", id, error.message);
    }
    return text;
}

/// Checks the plate against its hand-written definition for @p widthMm.
void checkPlate(const DrivenPlateModel& m, const Regenerator& regenerator, double widthMm) {
    CAPTURE(widthMm);
    CHECK_THAT(m.si(m.width), WithinRel(widthMm / 1000.0, 1e-15));
    CHECK_THAT(m.si(m.height), WithinRel(widthMm / 2.0 / 1000.0, 1e-15));
    CHECK_THAT(m.si(m.thickness), WithinRel(0.1 * widthMm / 1000.0, 1e-15));
    CHECK_THAT(m.si(m.holeSpacing), WithinRel((widthMm - 30.0) / 1000.0, 1e-15));
    CHECK_THAT(m.si(m.holeY), WithinRel(widthMm / 4.0 / 1000.0, 1e-15));
    CHECK_THAT(volumeMm3(regenerator, m.plate),
               WithinRel(DrivenPlateModel::expectedVolumeMm3(widthMm), kVolumeRel));

    const geometry::Body* body = regenerator.body(m.plate);
    REQUIRE(body != nullptr);
    CHECK(body->isValid());
    CHECK(body->topology().solids == 1);
    const auto box = body->boundingBox();
    REQUIRE(box.has_value());
    CHECK_THAT(box->max.x.in(units::mm), WithinAbs(widthMm, 1e-7));
    CHECK_THAT(box->max.y.in(units::mm), WithinAbs(widthMm / 2.0, 1e-7));
    CHECK_THAT(box->max.z.in(units::mm), WithinAbs(widthMm / 10.0, 1e-7));
    CHECK_THAT(box->min.x.in(units::mm), WithinAbs(0.0, 1e-7));

    // The holes sit where the driven dimensions put them: edge_distance from
    // the left edge, hole_spacing apart, at hole_y.
    const auto* sketch = m.doc.findObjectAs<sketch::Sketch>(m.sketch);
    REQUIRE(sketch != nullptr);
    std::vector<Point2D> centres;
    for (const sketch::Entity& entity : sketch->entities()) {
        if (const auto* circle = std::get_if<sketch::CircleEntity>(&entity.geometry)) {
            centres.push_back(*sketch->position(circle->center));
        }
    }
    REQUIRE(centres.size() == 2);
    CHECK_THAT(centres[0].x.in(units::mm), WithinAbs(15.0, 1e-9));
    CHECK_THAT(centres[1].x.in(units::mm), WithinAbs(widthMm - 15.0, 1e-9));
    CHECK_THAT(centres[0].y.in(units::mm), WithinAbs(widthMm / 4.0, 1e-9));
    CHECK_THAT(centres[1].y.in(units::mm), WithinAbs(widthMm / 4.0, 1e-9));
}

} // namespace

TEST_CASE("ParameterExpressions_DrivePlateAndReevaluateWhenWidthChanges",
          "[regeneration][expressions][p12][acceptance]") {
    DrivenPlateModel m;
    Regenerator regenerator;

    const RegenerationReport first = requireReport(regenerator, m.doc);
    INFO(describeFailures(first));
    REQUIRE(first.succeeded());
    // Evaluation order follows the dependencies, not the creation order:
    // hole_y (created first) comes after height, which it uses.
    CHECK(first.updatedParameters ==
          std::vector<ParameterId>{m.height, m.holeY, m.thickness, m.holeSpacing});
    CHECK(first.regenerated == std::vector<ObjectId>{m.sketch, m.plate});
    checkPlate(m, regenerator, 100.0);

    // Changing width re-evaluates every expression that depends on it and
    // rebuilds the geometry from the new values.
    m.set(m.width, 160_mm);
    const RegenerationReport wider = requireReport(regenerator, m.doc);
    REQUIRE(wider.succeeded());
    CHECK(wider.updatedParameters == std::vector<ParameterId>{m.height, m.holeY, m.thickness, m.holeSpacing});
    CHECK(wider.changed == std::vector<ObjectId>{m.holeY, m.height, m.thickness, m.holeSpacing, m.width});
    CHECK(wider.regenerated == std::vector<ObjectId>{m.sketch, m.plate});
    checkPlate(m, regenerator, 160.0);

    // edge_distance changes hole_spacing only.
    m.set(m.edgeDistance, 20_mm);
    const RegenerationReport edge = requireReport(regenerator, m.doc);
    REQUIRE(edge.succeeded());
    CHECK(edge.updatedParameters == std::vector<ParameterId>{m.holeSpacing});
    CHECK_THAT(m.si(m.holeSpacing), WithinRel(0.120, 1e-15));
    CHECK_THAT(volumeMm3(regenerator, m.plate), WithinRel(DrivenPlateModel::expectedVolumeMm3(160.0), kVolumeRel));

    // Undo restores the previous inputs, and regeneration the previous values.
    REQUIRE(m.history.undo(m.doc).has_value());
    REQUIRE(m.history.undo(m.doc).has_value());
    const RegenerationReport undone = requireReport(regenerator, m.doc);
    REQUIRE(undone.succeeded());
    checkPlate(m, regenerator, 100.0);
    CHECK(bits(m.si(m.height)) == bits(0.1 / 2.0));
    CHECK(bits(m.si(m.thickness)) == bits(0.1 * 0.1));
    CHECK(bits(m.si(m.holeSpacing)) == bits(0.1 - 2.0 * 0.015));

    // Redo reapplies them.
    REQUIRE(m.history.redo(m.doc).has_value());
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    checkPlate(m, regenerator, 160.0);
}

TEST_CASE("ParameterExpressions_WithoutChangesNothingIsRegenerated", "[regeneration][expressions][p12]") {
    DrivenPlateModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const std::uint64_t revision = m.doc.revision();

    const RegenerationReport second = requireReport(regenerator, m.doc);
    CHECK(second.succeeded());
    CHECK(second.changed.empty());
    CHECK(second.updatedParameters.empty());
    CHECK(second.regenerated.empty());
    CHECK(m.doc.revision() == revision);
    CHECK(regenerator.state(ObjectId{m.height}) == NodeState::UpToDate);
}

TEST_CASE("ParameterExpressions_FailedExpressionBlocksGeometryAndKeepsLastValue",
          "[regeneration][expressions][p12]") {
    DrivenPlateModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const double spacing = m.si(m.holeSpacing);

    // Deleting edge_distance leaves hole_spacing with an unknown name.
    REQUIRE(m.history.execute(m.doc, std::make_unique<DeleteObjectCommand>(m.edgeDistance)).has_value());
    const RegenerationReport report = requireReport(regenerator, m.doc);
    CHECK(report.failed == std::vector<ObjectId>{m.holeSpacing});
    const Error& error = report.errors.at(m.holeSpacing);
    CHECK(error.code == ErrorCode::NotFound);
    CHECK(error.message ==
          "parameter 'hole_spacing' = width - 2 * edge_distance: unknown parameter 'edge_distance' at offset 12");
    CHECK(report.blocked == std::vector<ObjectId>{m.sketch, m.plate});
    CHECK(regenerator.state(ObjectId{m.holeSpacing}) == NodeState::Failed);
    CHECK(regenerator.error(ObjectId{m.holeSpacing}) != nullptr);
    CHECK(regenerator.body(m.plate) == nullptr); // no stale body
    CHECK(m.si(m.holeSpacing) == spacing);      // the last value is kept
    CHECK(resultFeatures(m.doc).size() == 1);

    // It stays failed until fixed.
    const RegenerationReport again = requireReport(regenerator, m.doc);
    CHECK(again.failed == std::vector<ObjectId>{m.holeSpacing});
    CHECK(again.blocked == std::vector<ObjectId>{m.sketch, m.plate});

    // Undo brings the parameter back and everything rebuilds.
    REQUIRE(m.history.undo(m.doc).has_value());
    const RegenerationReport fixed = requireReport(regenerator, m.doc);
    CHECK(fixed.succeeded());
    CHECK(fixed.regenerated == std::vector<ObjectId>{m.sketch, m.plate});
    CHECK(regenerator.state(ObjectId{m.holeSpacing}) == NodeState::UpToDate);
    checkPlate(m, regenerator, 100.0);
}

TEST_CASE("ParameterExpressions_DimensionErrorFailsWithoutCoercion", "[regeneration][expressions][p12]") {
    DrivenPlateModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());

    // thickness = 0.1 * width * width is an area, not a length.
    REQUIRE(m.history
                .execute(m.doc, std::make_unique<ModifyParameterCommand>(
                                    m.thickness,
                                    ParameterChanges{.expression = std::optional<std::string>{"0.1 * width * width"}}))
                .has_value());
    const RegenerationReport report = requireReport(regenerator, m.doc);
    CHECK(report.failed == std::vector<ObjectId>{m.thickness});
    CHECK(report.errors.at(m.thickness).code == ErrorCode::DimensionMismatch);
    CHECK_THAT(report.errors.at(m.thickness).message,
               ContainsSubstring("the result has dimension area, but the parameter has dimension length"));
    CHECK(report.blocked == std::vector<ObjectId>{m.plate});
    CHECK(regenerator.body(m.plate) == nullptr);
    CHECK_THAT(m.si(m.thickness), WithinRel(0.01, 1e-15));

    const ValidationReport validation = validateDocument(m.doc);
    CHECK_FALSE(validation.valid());
    REQUIRE(validation.issues.size() == 2);
    CHECK(validation.count(ValidationCheck::FeatureRegeneration, Severity::Error) == 2);
    CHECK_THAT(validation.issues[0].message,
               ContainsSubstring("thickness (object:3) failed to evaluate: parameter 'thickness' = "));
    CHECK(validation.issues[1].message ==
          "Plate (object:9) was not regenerated because an item it depends on failed");

    REQUIRE(m.history.undo(m.doc).has_value());
    CHECK(requireReport(regenerator, m.doc).succeeded());
    checkPlate(m, regenerator, 100.0);
}

TEST_CASE("ParameterExpressions_CycleFailsParametersAndBlocksGeometry", "[regeneration][expressions][p12]") {
    DrivenPlateModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());

    // width = hole_spacing + 30 mm closes width -> hole_spacing -> width.
    REQUIRE(m.history
                .execute(m.doc, std::make_unique<ModifyParameterCommand>(
                                    m.width, ParameterChanges{.expression = std::optional<std::string>{
                                                                  "hole_spacing + 2 * edge_distance"}}))
                .has_value());
    const RegenerationReport report = requireReport(regenerator, m.doc);
    REQUIRE(report.cycles.size() == 1);
    CHECK(report.cycles.front() == std::vector<ObjectId>{m.holeSpacing, m.width});
    CHECK(report.failed == std::vector<ObjectId>{m.holeSpacing, m.width});
    CHECK_THAT(report.errors.at(m.width).message, ContainsSubstring("dependency cycle: hole_spacing, width"));
    CHECK(report.blocked == std::vector<ObjectId>{m.holeY, m.height, m.thickness, m.sketch, m.plate});
    CHECK(regenerator.body(m.plate) == nullptr);
    CHECK_THAT(m.si(m.width), WithinRel(0.1, 1e-15));

    const ValidationReport validation = validateDocument(m.doc);
    REQUIRE(validation.count(ValidationCheck::DependencyCycles, Severity::Error) == 1);
    CHECK_THAT(validation.issues[0].message,
               ContainsSubstring("dependency cycle: hole_spacing (object:4), width (object:5)"));

    REQUIRE(m.history.undo(m.doc).has_value());
    CHECK(requireReport(regenerator, m.doc).succeeded());
    checkPlate(m, regenerator, 100.0);
}

TEST_CASE("ParameterExpressions_ValidationReportsEachExpressionProblemOnce", "[validation][expressions][p12]") {
    DrivenPlateModel m;

    SECTION("a valid model") {
        const ValidationReport report = validateDocument(m.doc);
        CHECK(report.valid());
        CHECK(report.issues.empty()); // the sketch is fully constrained with evaluated values
        REQUIRE(report.bodies.size() == 1);
        REQUIRE(report.bodies[0].properties.has_value());
        CHECK_THAT(report.bodies[0].properties->volume.in(units::mm3),
                   WithinRel(DrivenPlateModel::expectedVolumeMm3(100.0), kVolumeRel));
        // Validation works on a copy: the document still has its placeholders.
        CHECK(m.si(m.height) == 0.001);
    }
    SECTION("an unknown name") {
        REQUIRE(m.doc.setParameterExpression(m.height, "wdth / 2").has_value());
        const ValidationReport report = validateDocument(m.doc);
        CHECK_FALSE(report.valid());
        REQUIRE(report.issues.size() == 4);
        CHECK(report.issues[0].check == ValidationCheck::MissingReferences);
        CHECK(report.issues[0].item == ObjectId{m.height});
        CHECK(report.issues[0].message ==
              "height (object:2): expression 'wdth / 2': unknown parameter 'wdth' at offset 0");
        // What depends on it is reported as not rebuilt, and the sketch is not
        // solved with an unknown value.
        CHECK(report.count(ValidationCheck::SketchConstraints, Severity::Error) == 0);
        REQUIRE(report.count(ValidationCheck::FeatureRegeneration, Severity::Error) == 3);
        CHECK(report.issues[1].message == "hole_y (object:1) was not evaluated because an item it depends on failed");
        CHECK(report.issues[2].message ==
              "PlateSketch (object:8) was not regenerated because an item it depends on failed");
        CHECK(report.issues[3].message == "Plate (object:9) was not regenerated because an item it depends on failed");
    }
    SECTION("the name of an object") {
        REQUIRE(m.doc.setParameterExpression(m.height, "PlateSketch / 2").has_value());
        const ValidationReport report = validateDocument(m.doc);
        REQUIRE(report.count(ValidationCheck::DocumentConsistency, Severity::Error) == 1);
        CHECK(report.issues[0].message ==
              "height (object:2): expression 'PlateSketch / 2': 'PlateSketch' is not a parameter but a "
              "document object of type 'sketch' at offset 0");
        CHECK(report.count(ValidationCheck::MissingReferences, Severity::Error) == 0);
    }
    SECTION("a division by zero") {
        REQUIRE(m.doc.setParameterValue(m.edgeDistance, 50_mm).has_value());
        REQUIRE(m.doc.setParameterExpression(m.holeY, "height / (width - 2 * edge_distance)").has_value());
        const ValidationReport report = validateDocument(m.doc);
        CHECK(report.count(ValidationCheck::SketchConstraints, Severity::Error) == 0);
        REQUIRE(report.count(ValidationCheck::FeatureRegeneration, Severity::Error) == 3);
        CHECK(report.issues[0].message ==
              "hole_y (object:1) failed to evaluate: parameter 'hole_y' = height / (width - 2 * "
              "edge_distance): division by zero at offset 7: '(width - 2 * edge_distance)' is zero");
    }
}

TEST_CASE("ParameterExpressions_RegenerationIsDeterministic", "[regeneration][expressions][p12]") {
    DrivenPlateModel first;
    DrivenPlateModel second;
    Regenerator a;
    Regenerator b;
    REQUIRE(requireReport(a, first.doc).succeeded());
    REQUIRE(requireReport(b, second.doc).succeeded());
    first.set(first.width, 123.4_mm);
    second.set(second.width, 123.4_mm);
    REQUIRE(requireReport(a, first.doc).succeeded());
    REQUIRE(requireReport(b, second.doc).succeeded());

    CHECK(equivalent(first.doc, second.doc));
    for (const ParameterId id : {first.height, first.thickness, first.holeSpacing, first.holeY}) {
        CHECK(bits(first.si(id)) == bits(second.si(id)));
    }
    CHECK(bits(volumeMm3(a, first.plate)) == bits(volumeMm3(b, second.plate)));

    // Rebuilding everything from scratch gives the same values and body.
    const double volume = volumeMm3(a, first.plate);
    const auto all = a.regenerateAll(first.doc);
    REQUIRE(all.has_value());
    CHECK(all->succeeded());
    CHECK(all->updatedParameters.empty());
    CHECK(bits(volumeMm3(a, first.plate)) == bits(volume));
}
