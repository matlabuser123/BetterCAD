#include "TestHelpers.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <limits>
#include <numbers>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::sketch;
using bettercad::test::errorCode;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinULP;

namespace {

constexpr double pi = std::numbers::pi;
constexpr double kTolMm = 1e-12;

EntityId require(const Result<EntityId>& id) {
    REQUIRE(id.has_value());
    return *id;
}

void checkPoint(const Point2D& p, double xMm, double yMm) {
    CHECK_THAT(p.x.in(units::mm), WithinAbs(xMm, kTolMm));
    CHECK_THAT(p.y.in(units::mm), WithinAbs(yMm, kTolMm));
}

void checkBox(const Result<BoundingBox2D>& box, double minX, double minY, double maxX, double maxY) {
    REQUIRE(box.has_value());
    checkPoint(box->min, minX, minY);
    checkPoint(box->max, maxX, maxY);
}

} // namespace

// --- P4-001: sketch coordinate system ----------------------------------------------------

TEST_CASE("A new sketch lies on the global XY plane", "[sketch]") {
    const Sketch sketch("Sketch1");
    CHECK(sketch.typeName() == "sketch");
    CHECK(sketch.name() == "Sketch1");
    CHECK(sketch.placement() == Frame3D::xy());
    CHECK(sketch.placement().origin() == Point3D{});
    CHECK(sketch.placement().xAxis() == Direction3D::unitX());
    CHECK(sketch.placement().yAxis() == Direction3D::unitY());
    CHECK(sketch.placement().normal() == Direction3D::unitZ());
    CHECK(sketch.entityCount() == 0);
    CHECK_FALSE(sketch.boundingBox().has_value());
    CHECK(sketch.toGlobal(Point2D{10_mm, 20_mm}) == Point3D{10_mm, 20_mm, 0_mm});
}

TEST_CASE("A sketch on another plane places its geometry in model space", "[sketch]") {
    const auto plane = Frame3D::create(Point3D{0_mm, 50_mm, 0_mm}, Direction3D::unitY().reversed(),
                                       Direction3D::unitX());
    REQUIRE(plane.has_value());
    Sketch sketch("OnXZ", *plane);
    const EntityId line = require(sketch.addLine(Point2D{0_mm, 0_mm}, Point2D{100_mm, 20_mm}));

    const auto ends = sketch.endpoints(line);
    REQUIRE(ends.has_value());
    CHECK(sketch.toGlobal(ends->end) == Point3D{100_mm, 50_mm, 20_mm});
    CHECK(sketch.toLocal(Point3D{100_mm, 80_mm, 20_mm}) == Point2D{100_mm, 20_mm});

    REQUIRE(sketch.setPlacement(Frame3D::xy()).value());
    CHECK_FALSE(sketch.setPlacement(Frame3D::xy()).value());
}

// --- P4-002: entities -----------------------------------------------------------------------

TEST_CASE("Spec usage: sketch.addLine(Point2D{0_mm, 0_mm}, Point2D{100_mm, 0_mm})", "[sketch]") {
    Sketch sketch("Sketch1");
    auto line = sketch.addLine(Point2D{0_mm, 0_mm}, Point2D{100_mm, 0_mm});

    REQUIRE(line.has_value());
    CHECK(sketch.entityType(*line) == EntityType::Line);
    CHECK(sketch.length(*line).value() == 100_mm);
    // The line references two new point entities; IDs are deterministic.
    CHECK(line->value() == 3);
    CHECK(sketch.entityType(EntityId::fromValue(1)) == EntityType::Point);
    CHECK(sketch.entityType(EntityId::fromValue(2)) == EntityType::Point);
    CHECK(sketch.entityCount() == 3);
}

TEST_CASE("Points, lines, circles and arcs have stable IDs and types", "[sketch]") {
    Sketch sketch("Sketch1");
    const EntityId point = require(sketch.addPoint(Point2D{5_mm, 5_mm}));
    const EntityId line = require(sketch.addLine(Point2D{0_mm, 0_mm}, Point2D{10_mm, 0_mm}));
    const EntityId circle = require(sketch.addCircle(Point2D{0_mm, 0_mm}, 4_mm));
    const EntityId arc = require(sketch.addArc(Point2D{0_mm, 0_mm}, 10_mm, 0_deg, 90_deg));

    CHECK(sketch.entityType(point) == EntityType::Point);
    CHECK(sketch.entityType(line) == EntityType::Line);
    CHECK(sketch.entityType(circle) == EntityType::Circle);
    CHECK(sketch.entityType(arc) == EntityType::Arc);
    CHECK_FALSE(sketch.entityType(EntityId::fromValue(999)).has_value());

    const Entity* found = sketch.findEntity(circle);
    REQUIRE(found != nullptr);
    CHECK(found->id == circle);
    CHECK(std::get<CircleEntity>(found->geometry).radius == 4_mm);

    // Removing an entity leaves every other ID unchanged, and IDs are not reused.
    REQUIRE(sketch.removeEntity(point).has_value());
    CHECK(sketch.entityType(line) == EntityType::Line);
    CHECK(sketch.entityType(arc) == EntityType::Arc);
    const EntityId next = require(sketch.addPoint(Point2D{}));
    CHECK(next.value() == sketch.lastAllocatedEntityId());
    CHECK(next.value() > arc.value());
}

TEST_CASE("Lines can share end points", "[sketch]") {
    Sketch sketch("Rectangle");
    const EntityId p1 = require(sketch.addPoint(Point2D{0_mm, 0_mm}));
    const EntityId p2 = require(sketch.addPoint(Point2D{100_mm, 0_mm}));
    const EntityId p3 = require(sketch.addPoint(Point2D{100_mm, 50_mm}));
    const EntityId p4 = require(sketch.addPoint(Point2D{0_mm, 50_mm}));
    const EntityId bottom = require(sketch.addLine(p1, p2));
    const EntityId right = require(sketch.addLine(p2, p3));
    const EntityId top = require(sketch.addLine(p3, p4));
    const EntityId left = require(sketch.addLine(p4, p1));

    CHECK(sketch.dependentsOf(p2) == std::vector<EntityId>{bottom, right});
    CHECK(sketch.dependentsOf(p1) == std::vector<EntityId>{bottom, left});
    CHECK(sketch.length(top).value() == 100_mm);

    // Moving a shared corner moves both lines.
    REQUIRE(sketch.setPointPosition(p3, Point2D{120_mm, 50_mm}).value());
    CHECK(sketch.endpoints(right)->end == Point2D{120_mm, 50_mm});
    CHECK(sketch.endpoints(top)->start == Point2D{120_mm, 50_mm});
    CHECK(sketch.length(top).value() == 120_mm);
    checkBox(sketch.boundingBox(right), 100.0, 0.0, 120.0, 50.0);
}

// --- P4-003: geometric queries ----------------------------------------------------------------

TEST_CASE("Endpoints of lines and arcs", "[sketch][queries]") {
    Sketch sketch("Sketch1");
    const EntityId line = require(sketch.addLine(Point2D{1_mm, 2_mm}, Point2D{4_mm, 6_mm}));
    const EntityId arc = require(sketch.addArc(Point2D{0_mm, 0_mm}, 10_mm, 0_deg, 90_deg));
    const EntityId circle = require(sketch.addCircle(Point2D{}, 1_mm));

    CHECK(sketch.endpoints(line).value() == Endpoints{{1_mm, 2_mm}, {4_mm, 6_mm}});
    const Endpoints arcEnds = sketch.endpoints(arc).value();
    checkPoint(arcEnds.start, 10.0, 0.0);
    checkPoint(arcEnds.end, 0.0, 10.0);
    CHECK(errorCode(sketch.endpoints(circle)) == ErrorCode::InvalidArgument);
    CHECK(errorCode(sketch.endpoints(EntityId::fromValue(77))) == ErrorCode::NotFound);
}

TEST_CASE("Lengths of lines, arcs and circles", "[sketch][queries]") {
    Sketch sketch("Sketch1");
    const EntityId line = require(sketch.addLine(Point2D{0_mm, 0_mm}, Point2D{3_mm, 4_mm}));
    const EntityId circle = require(sketch.addCircle(Point2D{}, 10_mm));
    const EntityId quarter = require(sketch.addArc(Point2D{}, 10_mm, 0_deg, 90_deg));
    const EntityId major = require(sketch.addArc(Point2D{}, 10_mm, 90_deg, 0_deg));
    const EntityId point = require(sketch.addPoint(Point2D{}));

    CHECK_THAT(sketch.length(line)->in(units::mm), WithinULP(5.0, 1));
    CHECK_THAT(sketch.length(circle)->in(units::mm), WithinULP(20.0 * pi, 1));
    CHECK_THAT(sketch.length(quarter)->in(units::mm), WithinAbs(5.0 * pi, 1e-12));
    CHECK_THAT(sketch.length(major)->in(units::mm), WithinAbs(15.0 * pi, 1e-12));
    CHECK(errorCode(sketch.length(point)) == ErrorCode::InvalidArgument);
}

TEST_CASE("Radius, centre and sweep of circles and arcs", "[sketch][queries]") {
    Sketch sketch("Sketch1");
    const EntityId circle = require(sketch.addCircle(Point2D{5_mm, 6_mm}, 7_mm));
    const EntityId arc = require(sketch.addArc(Point2D{1_mm, 1_mm}, 2.5_mm, 30_deg, 120_deg));
    const EntityId wrapping = require(sketch.addArc(Point2D{}, 1_mm, 350_deg, 10_deg));
    const EntityId line = require(sketch.addLine(Point2D{}, Point2D{1_mm, 0_mm}));

    CHECK(sketch.radius(circle).value() == 7_mm);
    CHECK_THAT(sketch.radius(arc)->in(units::mm), WithinAbs(2.5, kTolMm));
    CHECK(sketch.center(circle).value() == Point2D{5_mm, 6_mm});
    CHECK(sketch.center(arc).value() == Point2D{1_mm, 1_mm});
    CHECK_THAT(sketch.sweep(arc)->in(units::deg), WithinAbs(90.0, 1e-10));
    CHECK_THAT(sketch.sweep(wrapping)->in(units::deg), WithinAbs(20.0, 1e-10));
    CHECK(errorCode(sketch.radius(line)) == ErrorCode::InvalidArgument);
    CHECK(errorCode(sketch.sweep(circle)) == ErrorCode::InvalidArgument);
    CHECK(errorCode(sketch.center(line)) == ErrorCode::InvalidArgument);
}

TEST_CASE("Bounding boxes of entities are exact", "[sketch][queries]") {
    Sketch sketch("Sketch1");
    SECTION("point and line") {
        const EntityId point = require(sketch.addPoint(Point2D{2_mm, 3_mm}));
        const EntityId line = require(sketch.addLine(Point2D{5_mm, 1_mm}, Point2D{-1_mm, 4_mm}));
        checkBox(sketch.boundingBox(point), 2.0, 3.0, 2.0, 3.0);
        checkBox(sketch.boundingBox(line), -1.0, 1.0, 5.0, 4.0);
    }
    SECTION("circle") {
        const EntityId circle = require(sketch.addCircle(Point2D{10_mm, -5_mm}, 3_mm));
        checkBox(sketch.boundingBox(circle), 7.0, -8.0, 13.0, -2.0);
    }
    SECTION("arc through the top (45 to 135 degrees)") {
        const EntityId arc = require(sketch.addArc(Point2D{}, 10_mm, 45_deg, 135_deg));
        const double c = 10.0 * std::sqrt(0.5);
        checkBox(sketch.boundingBox(arc), -c, c, c, 10.0);
    }
    SECTION("arc across the +X axis (350 to 10 degrees)") {
        const EntityId arc = require(sketch.addArc(Point2D{}, 10_mm, 350_deg, 10_deg));
        const double x = 10.0 * std::cos(10.0 * pi / 180.0);
        const double y = 10.0 * std::sin(10.0 * pi / 180.0);
        checkBox(sketch.boundingBox(arc), x, -y, 10.0, y);
    }
    SECTION("major arc (90 to 0 degrees counter-clockwise)") {
        const EntityId arc = require(sketch.addArc(Point2D{}, 10_mm, 90_deg, 0_deg));
        checkBox(sketch.boundingBox(arc), -10.0, -10.0, 10.0, 10.0);
    }
    SECTION("whole sketch") {
        require(sketch.addLine(Point2D{0_mm, 0_mm}, Point2D{100_mm, 50_mm}));
        require(sketch.addCircle(Point2D{100_mm, 50_mm}, 10_mm));
        const auto all = sketch.boundingBox();
        REQUIRE(all.has_value());
        checkPoint(all->min, 0.0, 0.0);
        checkPoint(all->max, 110.0, 60.0);
    }
}

// --- Validation and editing ------------------------------------------------------------------

TEST_CASE("Invalid entities are rejected and leave the sketch unchanged", "[sketch][validation]") {
    Sketch sketch("Sketch1");
    const EntityId a = require(sketch.addPoint(Point2D{0_mm, 0_mm}));
    const EntityId b = require(sketch.addPoint(Point2D{10_mm, 0_mm}));
    const EntityId c = require(sketch.addPoint(Point2D{0_mm, 20_mm}));
    const EntityId line = require(sketch.addLine(a, b));
    const auto count = sketch.entityCount();
    const Length nan = Length::fromSi(std::numeric_limits<double>::quiet_NaN());

    CHECK(errorCode(sketch.addLine(Point2D{1_mm, 1_mm}, Point2D{1_mm, 1_mm})) == ErrorCode::InvalidArgument);
    CHECK(errorCode(sketch.addLine(a, a)) == ErrorCode::InvalidArgument);
    CHECK(errorCode(sketch.addLine(a, line)) == ErrorCode::InvalidArgument); // not a point
    CHECK(errorCode(sketch.addLine(a, EntityId::fromValue(99))) == ErrorCode::NotFound);
    CHECK(errorCode(sketch.addPoint(Point2D{nan, 0_mm})) == ErrorCode::InvalidArgument);
    CHECK(errorCode(sketch.addCircle(Point2D{}, 0_mm)) == ErrorCode::InvalidArgument);
    CHECK(errorCode(sketch.addCircle(a, -(1_mm))) == ErrorCode::InvalidArgument);
    CHECK(errorCode(sketch.addArc(Point2D{}, 10_mm, 30_deg, 30_deg)) == ErrorCode::InvalidArgument);
    CHECK(errorCode(sketch.addArc(Point2D{}, 10_mm, 0_deg, 360_deg)) == ErrorCode::InvalidArgument);
    // b and c are at 10 mm and 20 mm from a: not an arc around a.
    CHECK(errorCode(sketch.addArc(a, b, c)) == ErrorCode::InvalidArgument);
    CHECK(errorCode(sketch.addArc(a, b, b)) == ErrorCode::InvalidArgument);
    CHECK(sketch.entityCount() == count);

    const EntityId d = require(sketch.addPoint(Point2D{0_mm, 10_mm}));
    const EntityId arc = require(sketch.addArc(a, b, d));
    CHECK_THAT(sketch.sweep(arc)->in(units::deg), WithinAbs(90.0, 1e-10));
}

TEST_CASE("Referenced points cannot be removed", "[sketch][validation]") {
    Sketch sketch("Sketch1");
    const EntityId line = require(sketch.addLine(Point2D{0_mm, 0_mm}, Point2D{10_mm, 0_mm}));
    const EntityId start = std::get<LineEntity>(sketch.findEntity(line)->geometry).start;

    CHECK(errorCode(sketch.removeEntity(start)) == ErrorCode::FailedPrecondition);
    REQUIRE(sketch.removeEntity(line).has_value());
    REQUIRE(sketch.removeEntity(start).has_value());
    CHECK(errorCode(sketch.removeEntity(start)) == ErrorCode::NotFound);
}

TEST_CASE("Editing reports whether anything changed", "[sketch][editing]") {
    Sketch sketch("Sketch1");
    const EntityId point = require(sketch.addPoint(Point2D{1_mm, 1_mm}));
    const EntityId circle = require(sketch.addCircle(Point2D{}, 5_mm));

    CHECK(sketch.setPointPosition(point, Point2D{2_mm, 2_mm}).value());
    CHECK_FALSE(sketch.setPointPosition(point, Point2D{2_mm, 2_mm}).value());
    CHECK(sketch.setCircleRadius(circle, 6_mm).value());
    CHECK(sketch.radius(circle).value() == 6_mm);
    CHECK_FALSE(sketch.setCircleRadius(circle, 6_mm).value());
    CHECK(sketch.setConstruction(circle, true).value());
    CHECK(sketch.findEntity(circle)->construction);
    CHECK_FALSE(sketch.setConstruction(circle, true).value());

    CHECK(errorCode(sketch.setPointPosition(circle, Point2D{})) == ErrorCode::InvalidArgument);
    CHECK(errorCode(sketch.setCircleRadius(point, 1_mm)) == ErrorCode::InvalidArgument);
    CHECK(errorCode(sketch.setCircleRadius(circle, 0_mm)) == ErrorCode::InvalidArgument);
}

TEST_CASE("The same construction steps produce identical sketches", "[sketch]") {
    const auto build = [] {
        Sketch sketch("Sketch1");
        require(sketch.addLine(Point2D{0_mm, 0_mm}, Point2D{100_mm, 0_mm}));
        require(sketch.addCircle(Point2D{50_mm, 25_mm}, 5_mm));
        require(sketch.addArc(Point2D{}, 10_mm, 0_deg, 45_deg));
        return sketch;
    };
    const Sketch a = build();
    const Sketch b = build();
    CHECK(a.contentEquals(b));
    CHECK(a.lastAllocatedEntityId() == b.lastAllocatedEntityId());

    const auto copy = a.clone();
    CHECK(a.contentEquals(*copy));
    Sketch changed = build();
    REQUIRE(changed.setPointPosition(EntityId::fromValue(1), Point2D{1_mm, 0_mm}).value());
    CHECK_FALSE(a.contentEquals(changed));
}
