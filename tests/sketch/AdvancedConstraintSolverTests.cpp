#include "TestHelpers.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/geometry/Profile.hpp>
#include <bettercad/features/Profiles.hpp>
#include <bettercad/sketch/Sketch.hpp>
#include <bettercad/sketch/Solver.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <cmath>
#include <numbers>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::sketch;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// P12-SKETCH-001: the new constraints in the solver. Every result is checked
// with geometry computed here from the solved points (atan2 angles,
// point-line distances, reflections), not with the solver's own residuals.

namespace {

constexpr double pi = std::numbers::pi;
// The solver iterates until residuals are below 1e-13 m (1e-10 mm); 1e-8 mm
// leaves two orders of margin, as in the P6 solver tests.
constexpr double kTolMm = 1e-8;
// Angles from solved points: 1e-8 mm over lever arms of 10 mm or more.
constexpr double kTolDeg = 1e-7;

template <typename Id>
Id require(const Result<Id>& id) {
    if (!id) {
        FAIL(id.error().message);
    }
    return *id;
}

Point2D at(const Sketch& sketch, EntityId point) {
    const auto position = sketch.position(point);
    REQUIRE(position.has_value());
    return *position;
}

double mm(Length value) {
    return value.in(units::mm);
}

EntityId centreOf(const Sketch& sketch, EntityId round) {
    const Entity* entity = sketch.findEntity(round);
    if (const auto* circle = std::get_if<CircleEntity>(&entity->geometry)) {
        return circle->center;
    }
    return std::get<ArcEntity>(entity->geometry).center;
}

LineEntity lineOf(const Sketch& sketch, EntityId line) {
    return std::get<LineEntity>(sketch.findEntity(line)->geometry);
}

void checkSolved(const SolveResult& result, SolveStatus expected, std::size_t dof) {
    INFO(result.message);
    CHECK(result.status == expected);
    CHECK(result.degreesOfFreedom == dof);
    CHECK(result.maxResidual <= SolverOptions{}.tolerance);
    CHECK(result.conflicting.empty());
    CHECK(result.redundant.empty());
}

/// Counter-clockwise angle of the direction a -> b, in degrees.
double directionDeg(const Point2D& a, const Point2D& b) {
    return std::atan2(mm(b.y - a.y), mm(b.x - a.x)) * 180.0 / pi;
}

/// Unsigned distance of p from the line through a and b, in mm.
double lineDistanceMm(const Point2D& p, const Point2D& a, const Point2D& b) {
    const double ux = mm(b.x - a.x);
    const double uy = mm(b.y - a.y);
    return std::abs(ux * mm(p.y - a.y) - uy * mm(p.x - a.x)) / std::hypot(ux, uy);
}

} // namespace

// --- Angle ------------------------------------------------------------------------------------

TEST_CASE("SketchSolver_AngleTurnsTheSecondLineCounterClockwiseFromTheFirst", "[solver][sketch][p12]") {
    for (const double degrees : {15.0, 30.0, 60.0, 90.0, 120.0, 150.0, 175.0}) {
        CAPTURE(degrees);
        Sketch s("Angle");
        const EntityId origin = require(s.addPoint(Point2D{0_mm, 0_mm}));
        const EntityId baseEnd = require(s.addPoint(Point2D{100_mm, 0_mm}));
        const EntityId base = require(s.addLine(origin, baseEnd));
        require(s.addFixed(origin));
        require(s.addFixed(baseEnd));
        // Drawn 20 degrees off, as a user would sketch it.
        const double start = (degrees - 20.0) * pi / 180.0;
        const EntityId tip = require(s.addPoint(Point2D{50.0 * std::cos(start) * units::mm,
                                                         50.0 * std::sin(start) * units::mm}));
        const EntityId arm = require(s.addLine(origin, tip));
        require(s.addDistance(arm, 50_mm));
        require(s.addAngle(base, arm, degrees * units::deg));

        checkSolved(solve(s), SolveStatus::FullyConstrained, 0);
        const Point2D p = at(s, tip);
        CHECK_THAT(directionDeg(Point2D{}, p), WithinAbs(degrees, kTolDeg));
        CHECK_THAT(std::hypot(mm(p.x), mm(p.y)), WithinAbs(50.0, kTolMm));
    }
}

TEST_CASE("SketchSolver_AngleIsMeasuredBetweenLinesNotTheirDirections", "[solver][sketch][p12]") {
    Sketch s("Angle");
    const EntityId origin = require(s.addPoint(Point2D{0_mm, 0_mm}));
    const EntityId baseEnd = require(s.addPoint(Point2D{100_mm, 0_mm}));
    const EntityId base = require(s.addLine(origin, baseEnd));
    require(s.addFixed(origin));
    require(s.addFixed(baseEnd));
    const EntityId tip = require(s.addPoint(Point2D{-40_mm, 25_mm}));

    SECTION("the order of the lines sets the sense") {
        // From the arm (near 150 deg) counter-clockwise to the base is 30 deg.
        const EntityId arm = require(s.addLine(origin, tip));
        require(s.addAngle(arm, base, 30_deg));
        require(s.addDistance(arm, 50_mm));
        checkSolved(solve(s), SolveStatus::FullyConstrained, 0);
        CHECK_THAT(directionDeg(Point2D{}, at(s, tip)), WithinAbs(150.0, kTolDeg));
    }
    SECTION("a line drawn the other way is the same line") {
        const EntityId arm = require(s.addLine(tip, origin));
        require(s.addAngle(base, arm, 150_deg));
        require(s.addDistance(arm, 50_mm));
        checkSolved(solve(s), SolveStatus::FullyConstrained, 0);
        CHECK_THAT(directionDeg(Point2D{}, at(s, tip)), WithinAbs(150.0, kTolDeg));
    }
    SECTION("without a length the arm keeps one freedom") {
        const EntityId arm = require(s.addLine(origin, tip));
        require(s.addAngle(base, arm, 150_deg));
        const SolveResult result = solve(s);
        checkSolved(result, SolveStatus::UnderConstrained, 1);
        CHECK_THAT(directionDeg(Point2D{}, at(s, tip)), WithinAbs(150.0, kTolDeg));
    }
}

// --- Tangent ----------------------------------------------------------------------------------

TEST_CASE("SketchSolver_TangentLineTouchesTheCircleOnItsOwnSide", "[solver][sketch][p12]") {
    for (const double side : {1.0, -1.0}) {
        CAPTURE(side);
        Sketch s("Tangent");
        const EntityId circle = require(s.addCircle(Point2D{0_mm, 0_mm}, 8_mm));
        require(s.addFixed(centreOf(s, circle)));
        require(s.addRadius(circle, 10_mm));
        const EntityId line = require(s.addLine(Point2D{-30_mm, side * 14_mm}, Point2D{30_mm, side * 16_mm}));
        require(s.addHorizontal(line));
        require(s.addTangent(line, circle));

        checkSolved(solve(s), SolveStatus::UnderConstrained, 2); // the line can slide along itself
        const Endpoints ends = s.endpoints(line).value();
        CHECK_THAT(mm(ends.start.y), WithinAbs(side * 10.0, kTolMm));
        CHECK_THAT(lineDistanceMm(Point2D{}, ends.start, ends.end), WithinAbs(10.0, kTolMm));
    }
}

TEST_CASE("SketchSolver_TangentArcAndLineJoinSmoothly", "[solver][sketch][p12]") {
    Sketch s("Joint");
    const EntityId arc = require(s.addArc(Point2D{0_mm, 0_mm}, 10_mm, 0_deg, 90_deg));
    const ArcEntity geometry = std::get<ArcEntity>(s.findEntity(arc)->geometry);
    require(s.addFixed(geometry.center));
    require(s.addFixed(geometry.start));
    // A line from the arc's end, drawn roughly along the tangent.
    const EntityId far = require(s.addPoint(Point2D{-30_mm, 14_mm}));
    const EntityId line = require(s.addLine(geometry.end, far));
    require(s.addTangent(arc, line)); // stored as (line, arc)
    CHECK(s.findConstraint(ConstraintId::fromValue(3))->entities == std::vector<EntityId>{line, arc});

    checkSolved(solve(s), SolveStatus::UnderConstrained, 2);
    // At the shared point the line is perpendicular to the radius.
    const Point2D c{};
    const Point2D joint = at(s, geometry.end);
    const Point2D other = at(s, far);
    const double rx = mm(joint.x - c.x);
    const double ry = mm(joint.y - c.y);
    const double lx = mm(other.x - joint.x);
    const double ly = mm(other.y - joint.y);
    CHECK_THAT((rx * lx + ry * ly) / (std::hypot(rx, ry) * std::hypot(lx, ly)), WithinAbs(0.0, 1e-10));
    CHECK_THAT(std::hypot(rx, ry), WithinAbs(10.0, kTolMm));
}

TEST_CASE("SketchSolver_TangentAtACoincidentJointIsExact", "[solver][sketch][p12]") {
    // Regression: at a joint, tangency is solved as the line perpendicular to
    // the radius. The centre-distance form placed the joint only to the
    // square root of the tolerance (4e-5 mm here). The joint may be one point
    // entity or two joined by a coincident constraint.
    Sketch s("Joint");
    const EntityId arc = require(s.addArc(Point2D{0_mm, 0_mm}, 10_mm, 0_deg, 90_deg));
    const ArcEntity geometry = std::get<ArcEntity>(s.findEntity(arc)->geometry);
    require(s.addFixed(geometry.center));
    require(s.addFixed(geometry.start));
    const EntityId start = require(s.addPoint(Point2D{0.5_mm, 10.3_mm}));
    const EntityId far = require(s.addPoint(Point2D{-30_mm, 14_mm}));
    const EntityId line = require(s.addLine(start, far));
    require(s.addCoincident(start, geometry.end));
    require(s.addTangent(line, arc));

    checkSolved(solve(s), SolveStatus::UnderConstrained, 2);
    const Point2D joint = at(s, geometry.end);
    CHECK_THAT(mm(distance(joint, at(s, start))), WithinAbs(0.0, kTolMm));
    const Point2D other = at(s, far);
    const double rx = mm(joint.x);
    const double ry = mm(joint.y);
    const double lx = mm(other.x - joint.x);
    const double ly = mm(other.y - joint.y);
    CHECK_THAT((rx * lx + ry * ly) / (std::hypot(rx, ry) * std::hypot(lx, ly)), WithinAbs(0.0, 1e-10));
    CHECK_THAT(lineDistanceMm(Point2D{}, joint, other), WithinAbs(10.0, kTolMm));
}

TEST_CASE("SketchSolver_TangentArcTouchesACircleWithoutAJoint", "[solver][sketch][p12]") {
    Sketch s("ArcCircle");
    const EntityId circle = require(s.addCircle(Point2D{0_mm, 0_mm}, 9_mm));
    const EntityId arc = require(s.addArc(Point2D{30_mm, 1_mm}, 4_mm, 90_deg, 270_deg));
    require(s.addFixed(centreOf(s, circle)));
    require(s.addRadius(circle, 10_mm));
    require(s.addRadius(arc, 5_mm));
    require(s.addHorizontal(centreOf(s, circle), centreOf(s, arc)));
    require(s.addTangent(arc, circle));

    // The arc keeps two freedoms: where it starts and how far it turns.
    checkSolved(solve(s), SolveStatus::UnderConstrained, 2);
    CHECK_THAT(mm(distance(at(s, centreOf(s, circle)), at(s, centreOf(s, arc)))), WithinAbs(15.0, kTolMm));
    CHECK_THAT(mm(s.radius(arc).value()), WithinAbs(5.0, kTolMm));
}

TEST_CASE("SketchSolver_TangentCirclesKeepTheirKindOfContact", "[solver][sketch][p12]") {
    struct Case {
        double startX;
        double expectedDistance;
    };
    // r1 = 20, r2 = 5: external contact at 25, internal at 15. Each start is
    // nearer to one kind, which is kept.
    for (const Case c : {Case{30.0, 25.0}, Case{21.0, 25.0}, Case{12.0, 15.0}, Case{17.0, 15.0}}) {
        CAPTURE(c.startX);
        Sketch s("Circles");
        const EntityId big = require(s.addCircle(Point2D{0_mm, 0_mm}, 19_mm));
        const EntityId small = require(s.addCircle(Point2D{c.startX * units::mm, 2_mm}, 6_mm));
        require(s.addFixed(centreOf(s, big)));
        require(s.addRadius(big, 20_mm));
        require(s.addRadius(small, 5_mm));
        require(s.addHorizontal(centreOf(s, big), centreOf(s, small)));
        require(s.addTangent(big, small));

        checkSolved(solve(s), SolveStatus::FullyConstrained, 0);
        CHECK_THAT(mm(distance(at(s, centreOf(s, big)), at(s, centreOf(s, small)))),
                   WithinAbs(c.expectedDistance, kTolMm));
        CHECK_THAT(mm(at(s, centreOf(s, small)).y), WithinAbs(0.0, kTolMm));
    }
}

TEST_CASE("SketchSolver_TangentArcsShareASmoothJoint", "[solver][sketch][p12]") {
    // Two arcs meeting at a point, curving the same way (an S would curve the
    // other way): internal contact of radii 30 and 10.
    Sketch s("Arcs");
    const EntityId big = require(s.addArc(Point2D{0_mm, 0_mm}, 30_mm, 0_deg, 60_deg));
    const ArcEntity a = std::get<ArcEntity>(s.findEntity(big)->geometry);
    const Point2D joint = at(s, a.end);
    // The small arc starts where the big one ends, around a centre drawn a
    // little off the line through the joint, and turns 90 deg.
    const Point2D centre{joint.x * (2.0 / 3.0), joint.y * (2.0 / 3.0) + 1_mm};
    const double r = mm(distance(centre, joint));
    const double t = std::atan2(mm(joint.y - centre.y), mm(joint.x - centre.x)) + pi / 2.0;
    const EntityId smallCentre = require(s.addPoint(centre));
    const EntityId smallEnd = require(
        s.addPoint(Point2D{centre.x + r * std::cos(t) * units::mm, centre.y + r * std::sin(t) * units::mm}));
    const EntityId small = require(s.addArc(smallCentre, a.end, smallEnd));
    require(s.addFixed(a.center));
    require(s.addFixed(a.start));
    require(s.addRadius(small, 10_mm));
    require(s.addTangent(big, small));

    const SolveResult result = solve(s);
    INFO(result.message);
    REQUIRE(result.solved());
    const Point2D c1 = at(s, a.center);
    const Point2D c2 = at(s, smallCentre);
    CHECK_THAT(mm(distance(c1, c2)), WithinAbs(20.0, kTolMm)); // 30 - 10
    // The joint lies on both circles, on the line through both centres.
    const Point2D p = at(s, a.end);
    CHECK_THAT(mm(distance(c1, p)), WithinAbs(30.0, kTolMm));
    CHECK_THAT(mm(distance(c2, p)), WithinAbs(10.0, kTolMm));
    CHECK_THAT(lineDistanceMm(p, c1, c2), WithinAbs(0.0, kTolMm));
}

// --- Concentric, midpoint, symmetric, diameter --------------------------------------------------

TEST_CASE("SketchSolver_ConcentricBringsTheCentresTogether", "[solver][sketch][p12]") {
    Sketch s("Concentric");
    const EntityId outer = require(s.addCircle(Point2D{10_mm, 20_mm}, 15_mm));
    const EntityId inner = require(s.addArc(Point2D{13_mm, 24_mm}, 5_mm, 0_deg, 180_deg));
    require(s.addFixed(centreOf(s, outer)));
    require(s.addRadius(outer, 15_mm));
    require(s.addConcentric(inner, outer));

    const SolveResult result = solve(s);
    // The constraint places the arc's centre; its radius, start and sweep stay
    // free.
    INFO(result.message);
    checkSolved(result, SolveStatus::UnderConstrained, 3);
    const Point2D c = at(s, centreOf(s, inner));
    CHECK_THAT(mm(c.x), WithinAbs(10.0, kTolMm));
    CHECK_THAT(mm(c.y), WithinAbs(20.0, kTolMm));
    const ArcEntity arc = std::get<ArcEntity>(s.findEntity(inner)->geometry);
    CHECK_THAT(mm(distance(c, at(s, arc.start))), WithinAbs(mm(distance(c, at(s, arc.end))), kTolMm));
}

TEST_CASE("SketchSolver_MidpointPlacesThePointHalfwayAlongTheLine", "[solver][sketch][p12]") {
    Sketch s("Midpoint");
    const EntityId line = require(s.addLine(Point2D{0_mm, 0_mm}, Point2D{80_mm, 40_mm}));
    require(s.addFixed(lineOf(s, line).start));
    require(s.addFixed(lineOf(s, line).end));
    const EntityId point = require(s.addPoint(Point2D{10_mm, 10_mm}));
    require(s.addMidpoint(line, point)); // stored as (point, line)

    checkSolved(solve(s), SolveStatus::FullyConstrained, 0);
    CHECK_THAT(mm(at(s, point).x), WithinAbs(40.0, kTolMm));
    CHECK_THAT(mm(at(s, point).y), WithinAbs(20.0, kTolMm));

    // A free line carries its midpoint along: the midpoint stays halfway.
    Sketch free("FreeMidpoint");
    const EntityId l = require(free.addLine(Point2D{0_mm, 0_mm}, Point2D{60_mm, 0_mm}));
    const EntityId m = require(free.addPoint(Point2D{20_mm, 5_mm}));
    require(free.addMidpoint(m, l));
    require(free.addFixed(m));
    checkSolved(solve(free), SolveStatus::UnderConstrained, 2);
    const Endpoints ends = free.endpoints(l).value();
    CHECK_THAT(mm(midpoint(ends.start, ends.end).x), WithinAbs(20.0, kTolMm));
    CHECK_THAT(mm(midpoint(ends.start, ends.end).y), WithinAbs(5.0, kTolMm));
}

TEST_CASE("SketchSolver_SymmetricMirrorsPointsAcrossTheLine", "[solver][sketch][p12]") {
    for (const double axisDeg : {0.0, 30.0, 90.0, 135.0}) {
        CAPTURE(axisDeg);
        const double ax = std::cos(axisDeg * pi / 180.0);
        const double ay = std::sin(axisDeg * pi / 180.0);
        Sketch s("Symmetric");
        const EntityId axis =
            require(s.addLine(Point2D{0_mm, 0_mm}, Point2D{100.0 * ax * units::mm, 100.0 * ay * units::mm}));
        require(s.addFixed(lineOf(s, axis).start));
        require(s.addFixed(lineOf(s, axis).end));
        const EntityId p = require(s.addPoint(Point2D{40_mm, 5_mm}));
        require(s.addFixed(p));
        // Reflection of (40, 5) about the axis through the origin along a:
        // q = 2 (p . a) a - p, written out here independently of the solver.
        const double dot = 40.0 * ax + 5.0 * ay;
        const double qx = 2.0 * dot * ax - 40.0;
        const double qy = 2.0 * dot * ay - 5.0;
        const EntityId q = require(s.addPoint(Point2D{(qx + 3.0) * units::mm, (qy - 2.0) * units::mm}));
        require(s.addSymmetric(p, q, axis));

        checkSolved(solve(s), SolveStatus::FullyConstrained, 0);
        CHECK_THAT(mm(at(s, q).x), WithinAbs(qx, kTolMm));
        CHECK_THAT(mm(at(s, q).y), WithinAbs(qy, kTolMm));
    }
}

TEST_CASE("SketchSolver_DiameterSetsTwiceTheRadius", "[solver][sketch][p12]") {
    Sketch s("Diameter");
    const EntityId circle = require(s.addCircle(Point2D{0_mm, 0_mm}, 3_mm));
    const EntityId arc = require(s.addArc(Point2D{50_mm, 0_mm}, 4_mm, 0_deg, 120_deg));
    require(s.addDiameter(circle, 25_mm));
    require(s.addDiameter(arc, 30_mm));

    const SolveResult result = solve(s);
    INFO(result.message);
    REQUIRE(result.solved());
    CHECK_THAT(mm(s.radius(circle).value()), WithinAbs(12.5, kTolMm));
    CHECK_THAT(mm(s.radius(arc).value()), WithinAbs(15.0, kTolMm));
    const ArcEntity a = std::get<ArcEntity>(s.findEntity(arc)->geometry);
    CHECK_THAT(mm(distance(at(s, a.center), at(s, a.end))), WithinAbs(15.0, kTolMm));
}

// --- Degrees of freedom, redundancy and conflicts ----------------------------------------------

TEST_CASE("SketchSolver_EachNewConstraintRemovesItsDegreesOfFreedom", "[solver][sketch][p12]") {
    // A base sketch with known freedom; each constraint removes exactly as
    // many degrees of freedom as it has equations.
    const auto freedom = [](const Sketch& sketch) {
        const SolveResult result = analyze(sketch);
        INFO(result.message);
        REQUIRE(result.solved());
        return result.degreesOfFreedom;
    };
    Sketch s("Freedom");
    const EntityId a = require(s.addLine(Point2D{0_mm, 0_mm}, Point2D{50_mm, 5_mm}));
    const EntityId b = require(s.addLine(Point2D{0_mm, 20_mm}, Point2D{40_mm, 50_mm}));
    const EntityId c1 = require(s.addCircle(Point2D{80_mm, 0_mm}, 10_mm));
    const EntityId c2 = require(s.addCircle(Point2D{82_mm, 3_mm}, 4_mm));
    const EntityId p = require(s.addPoint(Point2D{30_mm, 30_mm}));
    const EntityId q = require(s.addPoint(Point2D{-30_mm, 25_mm}));
    const std::size_t base = freedom(s);
    CHECK(base == 8 + 3 + 3 + 2 + 2);

    std::size_t expected = base;
    const auto add = [&](Result<ConstraintId> id, std::size_t equations) {
        require(id);
        expected -= equations;
        CHECK(freedom(s) == expected);
    };
    add(s.addAngle(a, b, 45_deg), 1);
    add(s.addDiameter(c1, 20_mm), 1);
    add(s.addConcentric(c1, c2), 2);
    add(s.addMidpoint(p, a), 2);
    add(s.addSymmetric(p, q, b), 2);
    add(s.addTangent(a, c2), 1);
}

TEST_CASE("SketchSolver_RedundantNewConstraintsAreOverConstrained", "[solver][sketch][p12]") {
    SECTION("an angle of 90 deg and a perpendicular") {
        Sketch s("Redundant");
        const EntityId a = require(s.addLine(Point2D{0_mm, 0_mm}, Point2D{50_mm, 0_mm}));
        const EntityId b = require(s.addLine(Point2D{0_mm, 10_mm}, Point2D{3_mm, 60_mm}));
        require(s.addAngle(a, b, 90_deg));
        const ConstraintId again = require(s.addPerpendicular(a, b));
        const Sketch before = s;
        const SolveResult result = solve(s);
        CHECK(result.status == SolveStatus::OverConstrained);
        CHECK(result.redundant == std::vector<ConstraintId>{again});
        CHECK(s.contentEquals(before));
    }
    SECTION("concentric circles whose centres are also coincident") {
        Sketch s("Redundant");
        const EntityId c1 = require(s.addCircle(Point2D{0_mm, 0_mm}, 10_mm));
        const EntityId c2 = require(s.addCircle(Point2D{1_mm, 1_mm}, 5_mm));
        require(s.addConcentric(c1, c2));
        const ConstraintId coincident = require(s.addCoincident(centreOf(s, c1), centreOf(s, c2)));
        const SolveResult result = solve(s);
        CHECK(result.status == SolveStatus::OverConstrained);
        CHECK(result.redundant == std::vector<ConstraintId>{coincident});
    }
    SECTION("a diameter and a matching radius") {
        Sketch s("Redundant");
        const EntityId circle = require(s.addCircle(Point2D{0_mm, 0_mm}, 10_mm));
        require(s.addDiameter(circle, 20_mm));
        const ConstraintId radius = require(s.addRadius(circle, 10_mm));
        const SolveResult result = solve(s);
        CHECK(result.status == SolveStatus::OverConstrained);
        CHECK(result.redundant == std::vector<ConstraintId>{radius});
    }
    SECTION("a tangent and the matching centre distance") {
        Sketch s("Redundant");
        const EntityId circle = require(s.addCircle(Point2D{0_mm, 0_mm}, 10_mm));
        require(s.addRadius(circle, 10_mm));
        const EntityId line = require(s.addLine(Point2D{-20_mm, 12_mm}, Point2D{20_mm, 12_mm}));
        require(s.addTangent(line, circle));
        const ConstraintId distance = require(s.addDistance(centreOf(s, circle), line, 10_mm));
        const SolveResult result = solve(s);
        CHECK(result.status == SolveStatus::OverConstrained);
        CHECK(result.redundant == std::vector<ConstraintId>{distance});
    }
}

TEST_CASE("SketchSolver_ConflictingNewConstraintsAreInconsistent", "[solver][sketch][p12]") {
    SECTION("an angle and a parallel") {
        // The first line is fixed: both residuals scale with its length, so a
        // free line could only satisfy them by collapsing.
        Sketch s("Conflict");
        const EntityId a = require(s.addLine(Point2D{0_mm, 0_mm}, Point2D{50_mm, 0_mm}));
        require(s.addFixed(lineOf(s, a).start));
        require(s.addFixed(lineOf(s, a).end));
        const EntityId b = require(s.addLine(Point2D{0_mm, 10_mm}, Point2D{50_mm, 30_mm}));
        const ConstraintId angle = require(s.addAngle(a, b, 30_deg));
        const ConstraintId parallel = require(s.addParallel(a, b));
        const Sketch before = s;
        const SolveResult result = solve(s);
        INFO(result.message);
        CHECK(result.status == SolveStatus::Inconsistent);
        CHECK(result.conflicting == std::vector<ConstraintId>{angle, parallel});
        CHECK_FALSE(result.geometryChanged);
        CHECK(s.contentEquals(before));
    }
    SECTION("a diameter and a radius that disagree") {
        Sketch s("Conflict");
        const EntityId circle = require(s.addCircle(Point2D{0_mm, 0_mm}, 10_mm));
        const ConstraintId diameter = require(s.addDiameter(circle, 20_mm));
        const ConstraintId radius = require(s.addRadius(circle, 5_mm));
        const SolveResult result = solve(s);
        CHECK(result.status == SolveStatus::Inconsistent);
        CHECK(result.conflicting == std::vector<ConstraintId>{diameter, radius});
    }
    SECTION("a midpoint off a fixed line") {
        Sketch s("Conflict");
        const EntityId line = require(s.addLine(Point2D{0_mm, 0_mm}, Point2D{80_mm, 0_mm}));
        require(s.addFixed(lineOf(s, line).start));
        require(s.addFixed(lineOf(s, line).end));
        const EntityId p = require(s.addPoint(Point2D{30_mm, 10_mm}));
        require(s.addFixed(p));
        const ConstraintId middle = require(s.addMidpoint(p, line));
        const SolveResult result = solve(s);
        CHECK(result.status == SolveStatus::Inconsistent);
        CHECK(result.conflicting == std::vector<ConstraintId>{middle});
    }
}

// --- Whole profiles, checked by their area -----------------------------------------------------

TEST_CASE("SketchSolver_RegularHexagonFromEqualSidesAndAngles", "[solver][sketch][p12][acceptance]") {
    Sketch s("Hexagon");
    std::array<EntityId, 6> corners{};
    for (std::size_t i = 0; i < 6; ++i) {
        // A rough hexagon, radius 18 to 24 mm.
        const double r = 18.0 + static_cast<double>(i % 3) * 3.0;
        const double t = static_cast<double>(i) * pi / 3.0 - pi / 3.0 + 0.1;
        corners[i] = require(s.addPoint(Point2D{r * std::cos(t) * units::mm, r * std::sin(t) * units::mm}));
    }
    std::array<EntityId, 6> sides{};
    for (std::size_t i = 0; i < 6; ++i) {
        sides[i] = require(s.addLine(corners[i], corners[(i + 1) % 6]));
    }
    require(s.addFixed(corners[0]));
    require(s.addHorizontal(sides[0]));
    require(s.addDistance(sides[0], 20_mm));
    for (std::size_t i = 1; i <= 4; ++i) {
        require(s.addEqual(sides[i], sides[0]));
        require(s.addAngle(sides[i - 1], sides[i], 60_deg));
    }

    checkSolved(solve(s), SolveStatus::FullyConstrained, 0);
    // Every side is 20 mm and turns 60 deg: the closing side too, although no
    // constraint names it.
    for (std::size_t i = 0; i < 6; ++i) {
        CAPTURE(i);
        const Endpoints e = s.endpoints(sides[i]).value();
        CHECK_THAT(mm(distance(e.start, e.end)), WithinAbs(20.0, kTolMm));
        const Endpoints next = s.endpoints(sides[(i + 1) % 6]).value();
        double turn = directionDeg(next.start, next.end) - directionDeg(e.start, e.end);
        turn = std::remainder(turn, 360.0);
        CHECK_THAT(turn, WithinAbs(60.0, kTolDeg));
    }
    // Its area by Green's theorem equals (3 sqrt 3 / 2) s^2.
    const auto regions = features::extractRegions(s);
    REQUIRE(regions.has_value());
    REQUIRE(regions->size() == 1);
    CHECK_THAT(geometry::regionArea(regions->front()).in(units::mm2),
               WithinRel(1.5 * std::sqrt(3.0) * 400.0, 1e-12));
}

TEST_CASE("SketchSolver_ObroundFromTangentArcsIsFullyConstrained", "[solver][sketch][p12][acceptance]") {
    // A slot: two arcs of diameter D, centres L apart, joined by two
    // tangent lines. Area L D + pi D^2 / 4.
    Sketch s("Slot");
    const EntityId cL = require(s.addPoint(Point2D{0_mm, 0_mm}));
    const EntityId cR = require(s.addPoint(Point2D{35_mm, 1_mm}));
    const EntityId b0 = require(s.addPoint(Point2D{0_mm, -6_mm}));
    const EntityId t1 = require(s.addPoint(Point2D{0_mm, 6_mm}));
    const EntityId b1 = require(s.addPoint(Point2D{35_mm, -5_mm}));
    const EntityId t0 = require(s.addPoint(Point2D{35_mm, 7_mm}));
    const EntityId right = require(s.addArc(cR, b1, t0));
    const EntityId left = require(s.addArc(cL, t1, b0));
    const EntityId bottom = require(s.addLine(b0, b1));
    const EntityId top = require(s.addLine(t0, t1));
    require(s.addFixed(cL));
    require(s.addHorizontal(cL, cR));
    require(s.addDistance(cL, cR, 40_mm));
    require(s.addDiameter(left, 16_mm));
    require(s.addEqual(right, left));
    require(s.addTangent(bottom, right));
    require(s.addTangent(top, right));
    require(s.addTangent(top, left));
    require(s.addTangent(bottom, left));

    checkSolved(solve(s), SolveStatus::FullyConstrained, 0);
    CHECK_THAT(mm(at(s, b0).y), WithinAbs(-8.0, kTolMm));
    CHECK_THAT(mm(at(s, t0).y), WithinAbs(8.0, kTolMm));
    CHECK_THAT(mm(at(s, b1).x), WithinAbs(40.0, kTolMm));
    CHECK_THAT(mm(at(s, t1).x), WithinAbs(0.0, kTolMm));
    const auto regions = features::extractRegions(s);
    REQUIRE(regions.has_value());
    REQUIRE(regions->size() == 1);
    CHECK_THAT(geometry::regionArea(regions->front()).in(units::mm2),
               WithinRel(40.0 * 16.0 + pi * 16.0 * 16.0 / 4.0, 1e-12));
}

TEST_CASE("SketchSolver_NewConstraintsSolveDeterministically", "[solver][sketch][p12]") {
    const auto build = [] {
        Sketch s("Twin");
        const EntityId a = require(s.addLine(Point2D{0_mm, 0_mm}, Point2D{50_mm, 5_mm}));
        const EntityId b = require(s.addLine(Point2D{0_mm, 20_mm}, Point2D{40_mm, 50_mm}));
        const EntityId circle = require(s.addCircle(Point2D{80_mm, 10_mm}, 6_mm));
        const EntityId p = require(s.addPoint(Point2D{30_mm, 30_mm}));
        require(s.addFixed(lineOf(s, a).start));
        require(s.addAngle(a, b, 70_deg));
        require(s.addDiameter(circle, 18_mm));
        require(s.addTangent(b, circle));
        require(s.addMidpoint(p, b));
        return s;
    };
    Sketch first = build();
    Sketch second = build();
    const SolveResult one = solve(first);
    const SolveResult two = solve(second);
    REQUIRE(one.solved());
    CHECK(one.iterations == two.iterations);
    CHECK(first.contentEquals(second)); // bit-identical
    const SolveResult again = solve(first);
    CHECK(again.solved());
    CHECK_FALSE(again.geometryChanged);
}
