#include "FeatureTestSupport.hpp"
#include "TestHelpers.hpp"
#include "core/geometry/GeometryTestSupport.hpp"
#include "support/BracketModel.hpp"
#include "support/TurnedPartModel.hpp"

#include <bettercad/core/document/Commands.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/features/FeatureCommands.hpp>
#include <bettercad/features/Regeneration.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/features/ResultBodies.hpp>
#include <bettercad/features/RevolveFeature.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cmath>
#include <memory>
#include <numbers>
#include <vector>

using namespace bettercad;
using namespace bettercad::features;
using namespace bettercad::literals;
using namespace bettercad::sketch;
using bettercad::test::addRectangle;
using bettercad::test::describe;
using bettercad::test::errorCode;
using bettercad::test::require;
using bettercad::test::requireReport;
using bettercad::test::TurnedPartModel;
using bettercad::test::volumeMm3;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

constexpr double pi = std::numbers::pi;
// Revolutions of lines and arcs give planes, cylinders, cones, spheres and
// tori, whose properties the kernel computes to rounding level (measured in
// RevolutionTests); booleans with them intersect in circles and lines.
constexpr double kRel = 1e-12;

SketchId sketchOf(ObjectId id) {
    return SketchId::fromValue(id.value());
}

void setParameter(Document& doc, ParameterId id, Angle value) {
    REQUIRE(doc.setParameterValue(id, value).has_value());
}

RevolveDefinition definitionOf(const Document& doc, ObjectId id) {
    return doc.findObjectAs<RevolveFeature>(id)->definition();
}

void setDefinition(Document& doc, ObjectId id, const RevolveDefinition& definition) {
    REQUIRE(doc.modifyObject<RevolveFeature>(id, [&](RevolveFeature& f) { return f.setDefinition(definition); })
                .has_value());
}

/// A document with one revolve of a rectangle [r0, r1] x [0, h] (mm) drawn
/// on the XZ plane about the sketch Y axis (global Z).
struct SimpleRevolve {
    Document doc{"Simple"};
    ObjectId sketch, revolve;

    SimpleRevolve(double r0, double r1, double h, const RevolveDefinition& overrides = {}) {
        auto profile = std::make_unique<Sketch>("Profile", Frame3D::xz());
        addRectangle(*profile, r0 * units::mm, 0_mm, (r1 - r0) * units::mm, h * units::mm);
        sketch = doc.addObject(std::move(profile)).value();
        RevolveDefinition definition = overrides;
        definition.profile = sketchOf(sketch);
        auto feature = RevolveFeature::create("Revolve", definition);
        REQUIRE(feature.has_value());
        revolve = doc.addObject(std::move(*feature)).value();
    }
};

} // namespace

TEST_CASE("Revolve definitions are validated", "[revolve][features]") {
    const RevolveDefinition good{.profile = SketchId::fromValue(1)};
    REQUIRE(RevolveFeature::create("R", good).has_value());
    CHECK(good.angle == 360_deg);
    CHECK(good.axis == RevolveAxis::sketchY());

    const auto invalid = [](const RevolveDefinition& definition) {
        return errorCode(RevolveFeature::create("R", definition));
    };
    CHECK(invalid({}) == ErrorCode::InvalidArgument); // no profile
    RevolveDefinition d = good;
    SECTION("angles outside (0, 360] degrees") {
        for (const Angle angle : {0_deg, -(10_deg), 360.5_deg, Angle::fromSi(std::nan(""))}) {
            d.angle = angle;
            CHECK(invalid(d) == ErrorCode::InvalidArgument);
        }
        d.angle = 1e-3_deg;
        CHECK(invalid(d) == std::nullopt);
        d.angle = 360_deg;
        CHECK(invalid(d) == std::nullopt);
    }
    SECTION("a driving parameter replaces the literal angle") {
        d.angle = 0_deg;
        d.angleParameter = ParameterId::fromValue(3);
        CHECK(invalid(d) == std::nullopt);
        d.angleParameter = ParameterId{};
        CHECK(invalid(d) == ErrorCode::InvalidArgument);
    }
    SECTION("axes") {
        d.axis = RevolveAxis::alongLine(EntityId{});
        CHECK(invalid(d) == ErrorCode::InvalidArgument);
        d.axis = RevolveAxis{RevolveAxisKind::SketchX, EntityId::fromValue(4)};
        CHECK(invalid(d) == ErrorCode::InvalidArgument);
        d.axis = RevolveAxis::alongLine(EntityId::fromValue(4));
        CHECK(invalid(d) == std::nullopt);
    }
    SECTION("operation and target") {
        d.operation = FeatureOperation::Cut;
        CHECK(invalid(d) == ErrorCode::InvalidArgument);
        d.target = FeatureId::fromValue(2);
        CHECK(invalid(d) == std::nullopt);
        d.operation = FeatureOperation::NewBody;
        CHECK(invalid(d) == ErrorCode::InvalidArgument);
    }
    SECTION("an invalid edit leaves the feature unchanged") {
        auto feature = RevolveFeature::create("R", good);
        REQUIRE(feature.has_value());
        d.angle = 0_deg;
        CHECK(errorCode((*feature)->setDefinition(d)) == ErrorCode::InvalidArgument);
        CHECK((*feature)->definition() == good);
    }
    CHECK(toString(RevolveDirection::Negative) == "negative");
    CHECK(toString(RevolveAxisKind::SketchX) == "sketch X axis");
}

TEST_CASE("A revolve declares its profile, angle parameter and target as dependencies", "[revolve][features]") {
    TurnedPartModel m;
    const auto* turn = m.doc.findObjectAs<RevolveFeature>(m.turn);
    CHECK(turn->dependencies() == std::vector<ObjectId>{m.profile, ObjectId{m.sweep}});
    const auto* groove = m.doc.findObjectAs<RevolveFeature>(m.groove);
    CHECK(groove->dependencies() == std::vector<ObjectId>{m.grooveSketch, m.boreCut});
    CHECK(groove->operation() == FeatureOperation::Cut);
    CHECK(groove->target() == FeatureId::fromValue(m.boreCut.value()));
    const auto copy = turn->clone();
    CHECK(equivalent(*copy, *turn));
}

TEST_CASE("P11-FEAT-001 acceptance: a full revolve of a constrained profile matches the analytic solid",
          "[revolve][features][acceptance]") {
    TurnedPartModel m;
    Regenerator regenerator;
    const RegenerationReport report = requireReport(regenerator, m.doc);
    INFO(describe(report));
    REQUIRE(report.succeeded());
    CHECK(report.regenerated ==
          std::vector<ObjectId>{m.profile, m.turn, m.boreSketch, m.boreCut, m.grooveSketch, m.groove});

    // The revolve alone: a cylinder of radius 15 and height 40.
    const auto* turn = regenerator.body(m.turn);
    REQUIRE(turn != nullptr);
    CHECK(turn->isValid());
    CHECK(turn->topology().solids == 1);
    const auto props = turn->massProperties().value();
    CHECK_THAT(props.volume.in(units::mm3), WithinRel(pi * 15 * 15 * 40, kRel));
    CHECK_THAT(props.surfaceArea.in(units::mm2), WithinRel(2 * pi * 15 * 40 + 2 * pi * 15 * 15, kRel));
    CHECK_THAT(props.centerOfMass.z.in(units::mm), WithinRel(20.0, kRel));
    CHECK_THAT(props.centerOfMass.x.in(units::mm), WithinAbs(0.0, 1e-9));

    // The finished part: minus the bore and the groove.
    CHECK_THAT(volumeMm3(regenerator, m.groove), WithinRel(TurnedPartModel::expectedVolume(15, 40, 360, 5), kRel));
}

TEST_CASE("Partial revolves give proportional volumes in the chosen direction", "[revolve][features]") {
    const double degrees = GENERATE(30.0, 90.0, 180.0, 270.0);
    CAPTURE(degrees);
    const Angle angle = degrees * units::deg;
    const double expected = degrees / 360.0 * pi * (20 * 20 - 10 * 10) * 30;

    double centroidY[3] = {};
    for (const RevolveDirection direction :
         {RevolveDirection::Positive, RevolveDirection::Negative, RevolveDirection::Symmetric}) {
        CAPTURE(toString(direction));
        SimpleRevolve s(10, 20, 30, {.angle = angle, .direction = direction});
        Regenerator regenerator;
        REQUIRE(requireReport(regenerator, s.doc).succeeded());
        const auto props = regenerator.body(s.revolve)->massProperties().value();
        CHECK_THAT(props.volume.in(units::mm3), WithinRel(expected, kRel));
        centroidY[static_cast<int>(direction)] = props.centerOfMass.y.in(units::mm);
    }
    // Right-hand rule about +Z: the profile at +X turns towards +Y. Centroid
    // coordinates are positions, compared with the absolute position
    // tolerance (a relative one is ill-conditioned for small coordinates).
    CHECK(centroidY[0] > 0.0);
    CHECK_THAT(centroidY[1], WithinAbs(-centroidY[0], bettercad::test::kPositionToleranceMm));
    CHECK_THAT(centroidY[2], WithinAbs(0.0, bettercad::test::kPositionToleranceMm));
}

TEST_CASE("P11-FEAT-001 acceptance: changing the angle or the radius regenerates the revolve and its dependents",
          "[revolve][features][acceptance]") {
    TurnedPartModel m;
    CommandHistory history;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());

    SECTION("the angle rebuilds the revolve and what depends on it, not the sketch") {
        REQUIRE(history.execute(m.doc, ModifyParameterCommand::setValue(m.sweep, 90_deg)).has_value());
        const RegenerationReport report = requireReport(regenerator, m.doc);
        INFO(describe(report));
        CHECK(report.changed == std::vector<ObjectId>{ObjectId{m.sweep}});
        CHECK(report.regenerated == std::vector<ObjectId>{m.turn, m.boreCut, m.groove});
        CHECK_THAT(volumeMm3(regenerator, m.turn), WithinRel(pi * 15 * 15 * 40 / 4, kRel));
        CHECK_THAT(volumeMm3(regenerator, m.groove),
                   WithinRel(TurnedPartModel::expectedVolume(15, 40, 90, 5), kRel));

        // Undo restores the full revolution.
        REQUIRE(history.undo(m.doc).has_value());
        CHECK(requireReport(regenerator, m.doc).regenerated == std::vector<ObjectId>{m.turn, m.boreCut, m.groove});
        CHECK_THAT(volumeMm3(regenerator, m.groove),
                   WithinRel(TurnedPartModel::expectedVolume(15, 40, 360, 5), kRel));
    }
    SECTION("the radius rebuilds the profile sketch and everything downstream") {
        REQUIRE(history.execute(m.doc, ModifyParameterCommand::setValue(m.radius, 20_mm)).has_value());
        const RegenerationReport report = requireReport(regenerator, m.doc);
        INFO(describe(report));
        CHECK(report.regenerated == std::vector<ObjectId>{m.profile, m.turn, m.boreCut, m.groove});
        CHECK_THAT(volumeMm3(regenerator, m.turn), WithinRel(pi * 20 * 20 * 40, kRel));
        // The groove annulus [13, 16] now lies wholly inside the part.
        CHECK_THAT(volumeMm3(regenerator, m.groove),
                   WithinRel(TurnedPartModel::expectedVolume(20, 40, 360, 5), kRel));
    }
    SECTION("a profile dimension (the height) rebuilds the sketch and everything downstream") {
        REQUIRE(history.execute(m.doc, ModifyParameterCommand::setValue(m.height, 50_mm)).has_value());
        const RegenerationReport report = requireReport(regenerator, m.doc);
        CHECK(report.regenerated == std::vector<ObjectId>{m.profile, m.turn, m.boreCut, m.groove});
        CHECK_THAT(volumeMm3(regenerator, m.turn), WithinRel(pi * 15 * 15 * 50, kRel));
        CHECK_THAT(volumeMm3(regenerator, m.groove),
                   WithinRel(TurnedPartModel::expectedVolume(15, 50, 360, 5), kRel));
        const auto box = regenerator.body(m.turn)->boundingBox().value();
        CHECK_THAT(box.max.z.in(units::mm), WithinAbs(50.0, bettercad::test::kPositionToleranceMm));
    }
    SECTION("unrelated items are left alone") {
        REQUIRE(history.execute(m.doc, ModifyParameterCommand::setValue(m.bore, 6_mm)).has_value());
        const RegenerationReport report = requireReport(regenerator, m.doc);
        CHECK(report.regenerated == std::vector<ObjectId>{m.boreSketch, m.boreCut, m.groove});
        CHECK(regenerator.state(m.turn) == NodeState::UpToDate);
        CHECK_THAT(volumeMm3(regenerator, m.groove),
                   WithinRel(TurnedPartModel::expectedVolume(15, 40, 360, 6), kRel));
    }
}

TEST_CASE("Revolves join, cut and intersect with target bodies", "[revolve][features]") {
    // Base: the cylinder r = 15, h = 40 about Z.
    SimpleRevolve s(0, 15, 40);
    const FeatureId base = FeatureId::fromValue(s.revolve.value());
    const double cylinder = pi * 15 * 15 * 40;
    const auto addTool = [&](const char* name, std::initializer_list<std::pair<double, double>> corners,
                             FeatureOperation operation) {
        auto profile = std::make_unique<Sketch>(std::string{name} + "Sketch", Frame3D::xz());
        std::vector<EntityId> points;
        for (const auto& [u, v] : corners) {
            points.push_back(require(profile->addPoint(Point2D{u * units::mm, v * units::mm})));
        }
        for (std::size_t i = 0; i < points.size(); ++i) {
            require(profile->addLine(points[i], points[(i + 1) % points.size()]));
        }
        const ObjectId sketch = s.doc.addObject(std::move(profile)).value();
        auto tool = RevolveFeature::create(name, {.profile = sketchOf(sketch), .operation = operation, .target = base});
        REQUIRE(tool.has_value());
        return s.doc.addObject(std::move(*tool)).value();
    };

    // Every combined body is one valid solid, and the base stays a separate,
    // unchanged intermediate body.
    const auto checkResult = [&](const Regenerator& regenerator, ObjectId feature, double expected) {
        const geometry::Body* body = regenerator.body(feature);
        REQUIRE(body != nullptr);
        CHECK(body->isValid());
        CHECK(body->topology().solids == 1);
        CHECK_THAT(volumeMm3(regenerator, feature), WithinRel(expected, kRel));
        CHECK_THAT(volumeMm3(regenerator, s.revolve), WithinRel(cylinder, kRel));
        CHECK(resultFeatures(s.doc) == std::vector<ObjectId>{feature});
    };

    SECTION("join (add): a flange at the bottom") {
        const ObjectId flange = addTool("Flange", {{15, 0}, {25, 0}, {25, 5}, {15, 5}}, FeatureOperation::Join);
        Regenerator regenerator;
        REQUIRE(requireReport(regenerator, s.doc).succeeded());
        checkResult(regenerator, flange, cylinder + pi * (25 * 25 - 15 * 15) * 5);
        const auto box = regenerator.body(flange)->boundingBox().value();
        CHECK_THAT(box.max.x.in(units::mm), WithinAbs(25.0, bettercad::test::kPositionToleranceMm));
    }
    SECTION("cut (remove): a groove") {
        const ObjectId groove = addTool("Groove", {{13, 15}, {20, 15}, {20, 20}, {13, 20}}, FeatureOperation::Cut);
        Regenerator regenerator;
        REQUIRE(requireReport(regenerator, s.doc).succeeded());
        checkResult(regenerator, groove, cylinder - pi * (15 * 15 - 13 * 13) * 5);
    }
    SECTION("intersect: with a cone") {
        // Cone of base radius 30 and height 60: radius 30 - z/2, which drops
        // below 15 at z = 30. Inside the cylinder: 15^2 pi 30 plus the cone
        // frustum from radius 15 to 10 over z in [30, 40].
        const ObjectId tip = addTool("Tip", {{0, 0}, {30, 0}, {0, 60}}, FeatureOperation::Intersect);
        Regenerator regenerator;
        REQUIRE(requireReport(regenerator, s.doc).succeeded());
        const double frustum = 2.0 * pi * (15.0 * 15 * 15 - 10.0 * 10 * 10) / 3.0;
        checkResult(regenerator, tip, pi * 15 * 15 * 30 + frustum);
        const auto box = regenerator.body(tip)->boundingBox().value();
        CHECK_THAT(box.max.z.in(units::mm), WithinAbs(40.0, bettercad::test::kPositionToleranceMm));
    }
}

TEST_CASE("A revolve about a sketch line follows the line", "[revolve][features]") {
    // Rectangle 0..10 x 0..20 with a construction axis line at u = -10.
    Document doc("Tube");
    auto profile = std::make_unique<Sketch>("Profile", Frame3D::xz());
    addRectangle(*profile, 0_mm, 0_mm, 10_mm, 20_mm);
    const EntityId axis = require(profile->addLine(Point2D{-(10_mm), 0_mm}, Point2D{-(10_mm), 10_mm}));
    REQUIRE(profile->setConstruction(axis, true).has_value());
    const ObjectId sketch = doc.addObject(std::move(profile)).value();
    auto feature = RevolveFeature::create(
        "Tube", {.profile = sketchOf(sketch), .axis = RevolveAxis::alongLine(axis), .angle = 90_deg});
    REQUIRE(feature.has_value());
    const ObjectId tube = doc.addObject(std::move(*feature)).value();
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, doc).succeeded());
    CHECK_THAT(volumeMm3(regenerator, tube), WithinRel(pi * (20 * 20 - 10 * 10) * 20 / 4, kRel));
    const double turningTowards = regenerator.body(tube)->massProperties()->centerOfMass.y.in(units::mm);
    CHECK(turningTowards > 0.0); // the line points along +Z, like the sketch Y axis

    // Moving the axis line to u = -5 makes the tube wall run from radius 5 to 15.
    REQUIRE(doc.modifyObject<Sketch>(sketch, [&](Sketch& s) -> Result<bool> {
                   const auto line = std::get<LineEntity>(s.findEntity(axis)->geometry);
                   REQUIRE(s.setPointPosition(line.start, Point2D{-(5_mm), 0_mm}).has_value());
                   return s.setPointPosition(line.end, Point2D{-(5_mm), 10_mm});
               }).has_value());
    REQUIRE(requireReport(regenerator, doc).succeeded());
    CHECK_THAT(volumeMm3(regenerator, tube), WithinRel(pi * (15 * 15 - 5 * 5) * 20 / 4, kRel));

    // Reversing the line reverses the sense of rotation.
    REQUIRE(doc.modifyObject<Sketch>(sketch, [&](Sketch& s) -> Result<bool> {
                   const auto line = std::get<LineEntity>(s.findEntity(axis)->geometry);
                   REQUIRE(s.setPointPosition(line.start, Point2D{-(5_mm), 10_mm}).has_value());
                   return s.setPointPosition(line.end, Point2D{-(5_mm), 0_mm});
               }).has_value());
    REQUIRE(requireReport(regenerator, doc).succeeded());
    CHECK(regenerator.body(tube)->massProperties()->centerOfMass.y < Length{});
}

TEST_CASE("P11-FEAT-001 acceptance: invalid revolves fail with structured diagnostics",
          "[revolve][features][acceptance]") {
    TurnedPartModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());

    const auto failure = [&](ObjectId expected) {
        const RegenerationReport report = requireReport(regenerator, m.doc);
        INFO(describe(report));
        REQUIRE(report.failed == std::vector<ObjectId>{expected});
        CHECK(regenerator.body(expected) == nullptr);
        return report.errors.at(expected);
    };

    SECTION("a profile that crosses the axis") {
        REQUIRE(m.doc.modifyObject<Sketch>(m.grooveSketch, [](Sketch& s) -> Result<bool> {
                       // Move the groove rectangle's left side across the axis, to u = -2.
                       REQUIRE(s.setPointPosition(EntityId::fromValue(1), Point2D{-(2_mm), 15_mm}).has_value());
                       return s.setPointPosition(EntityId::fromValue(4), Point2D{-(2_mm), 20_mm});
                   }).has_value());
        const Error error = failure(m.groove);
        CHECK(error.code == ErrorCode::InvalidArgument);
        CHECK(error.message == "Groove: makeRevolution: the profile crosses the revolution axis "
                               "(it extends 16 mm and 2 mm to either side)");
    }
    SECTION("an axis line that does not exist") {
        RevolveDefinition d = definitionOf(m.doc, m.groove);
        d.axis = RevolveAxis::alongLine(EntityId::fromValue(99));
        setDefinition(m.doc, m.groove, d);
        const Error error = failure(m.groove);
        CHECK(error.code == ErrorCode::NotFound);
        CHECK(error.message == "Groove: the axis line entity:99 does not exist in sketch 'GrooveSketch'");
    }
    SECTION("an axis entity that is not a line") {
        RevolveDefinition d = definitionOf(m.doc, m.groove);
        d.axis = RevolveAxis::alongLine(EntityId::fromValue(1)); // a corner point
        setDefinition(m.doc, m.groove, d);
        const Error error = failure(m.groove);
        CHECK(error.code == ErrorCode::InvalidArgument);
        CHECK(error.message == "Groove: the axis entity:1 is a point, not a line");
    }
    SECTION("an axis line of zero length") {
        REQUIRE(m.doc.modifyObject<Sketch>(m.grooveSketch, [&](Sketch& s) -> Result<bool> {
                       const auto line = std::get<LineEntity>(s.findEntity(m.grooveAxis)->geometry);
                       return s.setPointPosition(line.end, Point2D{});
                   }).has_value());
        // The sketch solver already rejects the collapsed line, so the
        // revolve is blocked rather than built from it.
        const RegenerationReport report = requireReport(regenerator, m.doc);
        REQUIRE(report.failed == std::vector<ObjectId>{m.grooveSketch});
        CHECK_THAT(report.errors.at(m.grooveSketch).message, ContainsSubstring("SOLVER_FAILURE"));
        CHECK(report.blocked == std::vector<ObjectId>{m.groove});
        // Evaluated directly on such a sketch, the revolve refuses the axis too.
        const auto* sketch = m.doc.findObjectAs<Sketch>(m.grooveSketch);
        const auto axis = resolveAxis(RevolveAxis::alongLine(m.grooveAxis), *sketch);
        REQUIRE_FALSE(axis.has_value());
        CHECK(axis.error().code == ErrorCode::InvalidArgument);
        CHECK(axis.error().message == "the axis line entity:11 has zero length");
    }
    SECTION("an angle parameter outside (0, 360] degrees") {
        setParameter(m.doc, m.sweep, 400_deg);
        const RegenerationReport report = requireReport(regenerator, m.doc);
        REQUIRE(report.failed == std::vector<ObjectId>{m.turn});
        CHECK(report.errors.at(m.turn).code == ErrorCode::InvalidArgument);
        CHECK_THAT(report.errors.at(m.turn).message,
                   Catch::Matchers::StartsWith("Turn: revolve angle must be in (0, 360] deg, got 4"));
        // Everything built on the revolve is blocked, and keeps no stale body.
        CHECK(report.blocked == std::vector<ObjectId>{m.boreCut, m.groove});
        CHECK(regenerator.body(m.groove) == nullptr);
    }
    SECTION("an angle parameter that is a length") {
        RevolveDefinition d = definitionOf(m.doc, m.turn);
        d.angleParameter = m.radius;
        setDefinition(m.doc, m.turn, d);
        const RegenerationReport report = requireReport(regenerator, m.doc);
        REQUIRE(report.failed == std::vector<ObjectId>{m.turn});
        CHECK(report.errors.at(m.turn).code == ErrorCode::DimensionMismatch);
    }
    SECTION("an open profile") {
        REQUIRE(m.doc.modifyObject<Sketch>(m.grooveSketch, [](Sketch& s) -> Result<bool> {
                       return s.removeEntity(EntityId::fromValue(5)).transform([] { return true; });
                   }).has_value());
        const Error error = failure(m.groove);
        CHECK(error.code == ErrorCode::FailedPrecondition);
        CHECK_THAT(error.message, Catch::Matchers::StartsWith("Groove: "));
    }
}

TEST_CASE("Revolves with missing or unusable inputs fail with structured diagnostics", "[revolve][features]") {
    TurnedPartModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const auto failed = [&](ObjectId feature) {
        const RegenerationReport report = requireReport(regenerator, m.doc);
        INFO(describe(report));
        REQUIRE(report.failed == std::vector<ObjectId>{feature});
        CHECK(regenerator.body(feature) == nullptr);
        return report.errors.at(feature);
    };

    SECTION("an empty profile sketch") {
        auto empty = std::make_unique<Sketch>("Empty", Frame3D::xz());
        const ObjectId sketch = m.doc.addObject(std::move(empty)).value();
        const ObjectId revolve = m.addRevolve("Nothing", {.profile = sketchOf(sketch)});
        const Error error = failed(revolve);
        CHECK(error.code == ErrorCode::FailedPrecondition);
        CHECK_THAT(error.message, Catch::Matchers::StartsWith("Nothing: "));
    }
    SECTION("a missing profile sketch") {
        REQUIRE(m.doc.removeObject(m.grooveSketch).has_value());
        const Error error = failed(m.groove);
        CHECK(error.code == ErrorCode::NotFound);
        CHECK(error.message == "object:10 references object:9, which does not exist");
    }
    SECTION("a missing target feature") {
        REQUIRE(m.doc.removeObject(m.boreCut).has_value());
        const Error error = failed(m.groove);
        CHECK(error.code == ErrorCode::NotFound);
        CHECK(error.message == "object:10 references object:8, which does not exist");
    }
    SECTION("a target that has no body") {
        RevolveDefinition d = definitionOf(m.doc, m.groove);
        d.target = FeatureId::fromValue(m.profile.value()); // a sketch
        setDefinition(m.doc, m.groove, d);
        const Error error = failed(m.groove);
        CHECK(error.code == ErrorCode::FailedPrecondition);
        CHECK(error.message == "Groove: a cut feature needs the body of its target feature");
    }
}

TEST_CASE("A boolean with an empty target body fails instead of producing geometry", "[revolve][features]") {
    // Eraser cuts the whole base away (identical solids), leaving an empty
    // body; a join onto that body has nothing to combine with.
    SimpleRevolve s(0, 15, 40);
    const FeatureId base = FeatureId::fromValue(s.revolve.value());
    auto eraser =
        RevolveFeature::create("Eraser", {.profile = sketchOf(s.sketch), .operation = FeatureOperation::Cut, .target = base});
    REQUIRE(eraser.has_value());
    const ObjectId eraserId = s.doc.addObject(std::move(*eraser)).value();
    auto join = RevolveFeature::create("Rejoin", {.profile = sketchOf(s.sketch),
                                                  .operation = FeatureOperation::Join,
                                                  .target = FeatureId::fromValue(eraserId.value())});
    REQUIRE(join.has_value());
    const ObjectId joinId = s.doc.addObject(std::move(*join)).value();

    Regenerator regenerator;
    const RegenerationReport report = requireReport(regenerator, s.doc);
    INFO(describe(report));
    REQUIRE(regenerator.body(eraserId) != nullptr);
    CHECK(regenerator.body(eraserId)->isEmpty());
    REQUIRE(report.failed == std::vector<ObjectId>{joinId});
    CHECK(report.errors.at(joinId).code == ErrorCode::FailedPrecondition);
    CHECK(report.errors.at(joinId).message == "Rejoin: a join feature needs the body of its target feature");
    CHECK(regenerator.body(joinId) == nullptr);
}

TEST_CASE("Undo and redo of revolve commands restore identical geometry", "[revolve][features][undo]") {
    Document doc("Undo");
    auto profile = std::make_unique<Sketch>("Profile", Frame3D::xz());
    addRectangle(*profile, 10_mm, 0_mm, 10_mm, 30_mm);
    const SketchId sketch = sketchOf(doc.addObject(std::move(profile)).value());
    CommandHistory history;
    Regenerator regenerator;

    auto create = std::make_unique<CreateRevolveCommand>("Ring", RevolveDefinition{.profile = sketch});
    CreateRevolveCommand* createRaw = create.get();
    CHECK(create->description() == "Create revolve 'Ring'");
    REQUIRE(history.execute(doc, std::move(create)).has_value());
    const ObjectId ring{createRaw->featureId()};
    REQUIRE(requireReport(regenerator, doc).succeeded());
    const double full = volumeMm3(regenerator, ring);
    CHECK_THAT(full, WithinRel(pi * (400 - 100) * 30, kRel));
    const Document afterCreate = doc.clone();

    RevolveDefinition half = definitionOf(doc, ring);
    half.angle = 180_deg;
    half.direction = RevolveDirection::Symmetric;
    REQUIRE(history.execute(doc, std::make_unique<ModifyRevolveCommand>(FeatureId::fromValue(ring.value()), half))
                .has_value());
    REQUIRE(requireReport(regenerator, doc).succeeded());
    const double halfVolume = volumeMm3(regenerator, ring);
    CHECK_THAT(halfVolume, WithinRel(full / 2, kRel));
    const Document afterModify = doc.clone();

    REQUIRE(history.undo(doc).has_value());
    CHECK(equivalent(doc, afterCreate));
    REQUIRE(requireReport(regenerator, doc).succeeded());
    CHECK(volumeMm3(regenerator, ring) == full);

    REQUIRE(history.redo(doc).has_value());
    CHECK(equivalent(doc, afterModify));
    REQUIRE(requireReport(regenerator, doc).succeeded());
    CHECK(volumeMm3(regenerator, ring) == halfVolume);

    REQUIRE(history.undo(doc).has_value());
    REQUIRE(history.undo(doc).has_value());
    CHECK(doc.findObject(ring) == nullptr);
    REQUIRE(requireReport(regenerator, doc).succeeded());
    CHECK(regenerator.body(ring) == nullptr);

    REQUIRE(history.redo(doc).has_value());
    CHECK(equivalent(doc, afterCreate)); // recreated with the same ID
    REQUIRE(requireReport(regenerator, doc).succeeded());
    CHECK(volumeMm3(regenerator, ring) == full);

    // Commands check the feature kind.
    CHECK(errorCode(history.execute(doc, std::make_unique<ModifyRevolveCommand>(
                                             FeatureId::fromValue(ObjectId{sketch}.value()), half))) ==
          ErrorCode::NotFound);
}
