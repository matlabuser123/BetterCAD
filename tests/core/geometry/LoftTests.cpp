#include "GeometryTestSupport.hpp"
#include "TestHelpers.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/geometry/Edges.hpp>
#include <bettercad/core/geometry/Profile.hpp>
#include <bettercad/core/geometry/Sweeps.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

using namespace bettercad;
using namespace bettercad::geometry;
using namespace bettercad::literals;
using bettercad::test::checkPoint;
using bettercad::test::errorCode;
using bettercad::test::kPositionToleranceMm;
using bettercad::test::kRelTight;
using bettercad::test::requireProperties;
using Catch::Matchers::StartsWith;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

constexpr double pi = std::numbers::pi;
// Ruled faces between circles or arcs that are not coaxial, or are turned
// against each other, reproduce the analytic volume within 6.3e-10 (the
// kernel's B-spline ruled faces; measured, docs/verification/P11-FEAT-009);
// faces between lines and between coaxial circles and arcs to rounding.
constexpr double kRelSpline = bettercad::test::kRelApproximatedIntersection;
// The kernel's bounding box of a loft may exceed the exact box by its
// confusion tolerance, 1e-7 mm.
constexpr double kBoundsPaddingMm = 1e-7;

Point2D mm(double u, double v) {
    return Point2D{u * units::mm, v * units::mm};
}

ProfileLoop polygon(std::initializer_list<std::pair<double, double>> points) {
    const std::vector<std::pair<double, double>> p(points);
    ProfileLoop loop;
    for (std::size_t i = 0; i < p.size(); ++i) {
        const auto& [u0, v0] = p[i];
        const auto& [u1, v1] = p[(i + 1) % p.size()];
        loop.segments.emplace_back(LineSegment2D{mm(u0, v0), mm(u1, v1)});
    }
    return loop;
}

ProfileLoop circleLoop(double u, double v, double r) {
    return ProfileLoop{{CircleSegment2D{mm(u, v), r * units::mm, true}}};
}

/// A w x h rectangle centred at (u, v), counter-clockwise from its upper-right corner.
ProfileLoop rectangleLoop(double u, double v, double w, double h) {
    return polygon({{u + w / 2, v + h / 2}, {u - w / 2, v + h / 2}, {u - w / 2, v - h / 2}, {u + w / 2, v - h / 2}});
}

/// A regular n-gon of circumradius r about the origin, its first corner at angle a0 (rad).
ProfileLoop regular(int n, double r, double a0 = 0.0) {
    ProfileLoop loop;
    for (int i = 0; i < n; ++i) {
        const double a = a0 + 2.0 * pi * i / n;
        const double b = a0 + 2.0 * pi * (i + 1) / n;
        loop.segments.emplace_back(
            LineSegment2D{mm(r * std::cos(a), r * std::sin(a)), mm(r * std::cos(b), r * std::sin(b))});
    }
    return loop;
}

/// A slot about the origin: semicircles of radius r about (+-half, 0).
ProfileLoop slotLoop(double half, double r) {
    return ProfileLoop{{ArcSegment2D{mm(half, 0), mm(half, -r), mm(half, r), true},
                        LineSegment2D{mm(half, r), mm(-half, r)},
                        ArcSegment2D{mm(-half, 0), mm(-half, r), mm(-half, -r), true},
                        LineSegment2D{mm(-half, -r), mm(half, -r)}}};
}

/// The XY plane moved to z (mm), its X axis turned by @p turn (rad) about Z.
Frame3D levelAt(double z, double turn = 0.0) {
    return Frame3D::create(Point3D{0_mm, 0_mm, z * units::mm}, Direction3D::unitZ(),
                           *Direction3D::fromComponents(std::cos(turn), std::sin(turn), 0.0))
        .value();
}

PlanarRegion region(ProfileLoop outer, const Frame3D& plane, std::vector<ProfileLoop> holes = {}) {
    return PlanarRegion{.plane = plane, .outer = std::move(outer), .holes = std::move(holes)};
}

Body requireLoft(const std::vector<PlanarRegion>& sections) {
    auto body = makeLoft(sections);
    if (!body) {
        FAIL(body.error().message);
    }
    CHECK(body->isValid());
    CHECK(body->topology().solids == 1);
    return *body;
}

Error loftError(const std::vector<PlanarRegion>& sections) {
    auto body = makeLoft(sections);
    REQUIRE_FALSE(body.has_value());
    return body.error();
}

double volumeMm3(const Body& body) {
    return requireProperties(body).volume.in(units::mm3);
}

/// The kernel's box must contain the exact box and exceed it by at most kBoundsPaddingMm.
void checkBox(const Body& body, std::array<double, 3> min, std::array<double, 3> max) {
    const BoundingBox3D box = body.boundingBox().value();
    const std::array<double, 3> lo{box.min.x.in(units::mm), box.min.y.in(units::mm), box.min.z.in(units::mm)};
    const std::array<double, 3> hi{box.max.x.in(units::mm), box.max.y.in(units::mm), box.max.z.in(units::mm)};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        CAPTURE(axis, lo[axis], hi[axis]);
        CHECK(lo[axis] <= min[axis] + kPositionToleranceMm);
        CHECK(lo[axis] >= min[axis] - kBoundsPaddingMm - kPositionToleranceMm);
        CHECK(hi[axis] >= max[axis] - kPositionToleranceMm);
        CHECK(hi[axis] <= max[axis] + kBoundsPaddingMm + kPositionToleranceMm);
    }
}

/// pi h/3 (r1^2 + r1 r2 + r2^2)
double frustum(double r1, double r2, double h) {
    return pi * h / 3.0 * (r1 * r1 + r1 * r2 + r2 * r2);
}

/// Fraction of the height at which a circular frustum's centroid lies:
/// (r1^2 + 2 r1 r2 + 3 r2^2) / (4 (r1^2 + r1 r2 + r2^2)).
double frustumCentroid(double r1, double r2) {
    return (r1 * r1 + 2.0 * r1 * r2 + 3.0 * r2 * r2) / (4.0 * (r1 * r1 + r1 * r2 + r2 * r2));
}

std::uint64_t bits(double value) {
    return std::bit_cast<std::uint64_t>(value);
}

} // namespace

TEST_CASE("Loft_EqualSectionsMatchPrisms", "[geometry][loft]") {
    // Two equal sections 100 mm apart: the loft is the prism of either.
    for (const auto& [name, loop, area] : {std::tuple{"rectangle", rectangleLoop(0, 0, 10, 20), 200.0},
                                           std::tuple{"circle", circleLoop(0, 0, 5), 25.0 * pi}}) {
        CAPTURE(name);
        const Body lofted = requireLoft({region(loop, levelAt(0)), region(loop, levelAt(100))});
        const Body prism = makePrism(region(loop, levelAt(0)), 0_mm, 100_mm).value();
        const MassProperties l = requireProperties(lofted);
        const MassProperties p = requireProperties(prism);
        CHECK_THAT(l.volume.in(units::mm3), WithinRel(area * 100.0, kRelTight));
        CHECK_THAT(l.volume.in(units::mm3), WithinRel(p.volume.in(units::mm3), kRelTight));
        CHECK_THAT(l.surfaceArea.in(units::mm2), WithinRel(p.surfaceArea.in(units::mm2), kRelTight));
        checkPoint(l.centerOfMass, 0, 0, 50);
        const BoundingBox3D box = prism.boundingBox().value();
        checkBox(lofted, {box.min.x.in(units::mm), box.min.y.in(units::mm), 0},
                 {box.max.x.in(units::mm), box.max.y.in(units::mm), 100});
    }
}

TEST_CASE("Loft_CircularFrustumMatchesAnalyticVolumeAndRevolution", "[geometry][loft]") {
    // r1 = 10 at z = 0 to r2 = 5 at z = 30: V = pi h/3 (r1^2 + r1 r2 + r2^2),
    // and the revolution of the trapezoid (0, 0), (10, 0), (5, 30), (0, 30).
    const Body lofted = requireLoft({region(circleLoop(0, 0, 10), levelAt(0)), region(circleLoop(0, 0, 5), levelAt(30))});
    const Body revolved = makeRevolution(region(polygon({{0, 0}, {10, 0}, {5, 30}, {0, 30}}), Frame3D::xz()),
                                         Axis3D{Point3D{}, Direction3D::unitZ()}, 0_deg, 360_deg)
                              .value();
    const double expected = frustum(10, 5, 30);
    CHECK_THAT(expected, WithinRel(1750.0 * pi, 1e-15));
    const MassProperties l = requireProperties(lofted);
    const MassProperties r = requireProperties(revolved);
    CHECK_THAT(l.volume.in(units::mm3), WithinRel(expected, kRelTight));
    CHECK_THAT(l.volume.in(units::mm3), WithinRel(r.volume.in(units::mm3), kRelTight));
    CHECK_THAT(l.surfaceArea.in(units::mm2), WithinRel(r.surfaceArea.in(units::mm2), kRelTight));
    checkPoint(l.centerOfMass, 0, 0, 30.0 * frustumCentroid(10, 5));
    checkPoint(r.centerOfMass, 0, 0, 30.0 * frustumCentroid(10, 5));
    checkBox(lofted, {-10, -10, 0}, {10, 10, 30});
}

TEST_CASE("Loft_PolygonsMatchFrustumsAndPrismatoids", "[geometry][loft]") {
    SECTION("similar rectangles: h/3 (A1 + A2 + sqrt(A1 A2))") {
        const Body body = requireLoft({region(rectangleLoop(0, 0, 20, 10), levelAt(0)),
                                       region(rectangleLoop(0, 0, 10, 5), levelAt(30))});
        CHECK_THAT(volumeMm3(body), WithinRel(10.0 * (200.0 + 50.0 + 100.0), kRelTight));
        checkPoint(requireProperties(body).centerOfMass, 0, 0, 30.0 * frustumCentroid(1, 0.5));
        checkBox(body, {-10, -5, 0}, {10, 5, 30});
    }
    SECTION("a rectangle to its transpose: h/6 (A0 + 4 Am + A1), Am = 15 x 15") {
        const Body body = requireLoft({region(rectangleLoop(0, 0, 20, 10), levelAt(0)),
                                       region(rectangleLoop(0, 0, 10, 20), levelAt(30))});
        CHECK_THAT(volumeMm3(body), WithinRel(5.0 * (200.0 + 4.0 * 225.0 + 200.0), kRelTight));
        checkBox(body, {-10, -10, 0}, {10, 10, 30});
    }
    SECTION("regular hexagons R 10 -> R 5") {
        const Body body = requireLoft({region(regular(6, 10), levelAt(0)), region(regular(6, 5), levelAt(30))});
        const double a1 = 1.5 * std::sqrt(3.0) * 100.0;
        const double a2 = a1 / 4.0;
        CHECK_THAT(volumeMm3(body), WithinRel(10.0 * (a1 + a2 + std::sqrt(a1 * a2)), kRelTight));
        checkBox(body, {-10, -5.0 * std::sqrt(3.0), 0}, {10, 5.0 * std::sqrt(3.0), 30});
    }
}

TEST_CASE("Loft_MultipleSectionsArePiecewiseRuled", "[geometry][loft]") {
    SECTION("r 5, 10, 5 at z 0, 50, 100: two frustums, symmetric about z = 50") {
        const Body body = requireLoft({region(circleLoop(0, 0, 5), levelAt(0)), region(circleLoop(0, 0, 10), levelAt(50)),
                                       region(circleLoop(0, 0, 5), levelAt(100))});
        CHECK_THAT(volumeMm3(body), WithinRel(2.0 * frustum(5, 10, 50), kRelTight));
        checkPoint(requireProperties(body).centerOfMass, 0, 0, 50);
        checkBox(body, {-10, -10, 0}, {10, 10, 100});
    }
    SECTION("r 10, 6, 8, 4 at z 0, 20, 35, 60: three frustums") {
        const Body body = requireLoft({region(circleLoop(0, 0, 10), levelAt(0)), region(circleLoop(0, 0, 6), levelAt(20)),
                                       region(circleLoop(0, 0, 8), levelAt(35)), region(circleLoop(0, 0, 4), levelAt(60))});
        CHECK_THAT(volumeMm3(body), WithinRel(frustum(10, 6, 20) + frustum(6, 8, 15) + frustum(8, 4, 25), kRelTight));
        CHECK_THAT(volumeMm3(body), WithinRel(2980.0 * pi, kRelTight));
        checkBox(body, {-10, -10, 0}, {10, 10, 60});
    }
}

TEST_CASE("Loft_OffsetSectionsKeepTheirPositions", "[geometry][loft]") {
    // r 10 at the origin to r 5 centred at (10, 0, 30): an oblique frustum.
    // Cavalieri: the same volume as the right one; each cross-section is a
    // circle centred at (10 t, 0), so the centroid is at t = the right
    // frustum's height fraction.
    const Body body = requireLoft({region(circleLoop(0, 0, 10), levelAt(0)), region(circleLoop(10, 0, 5), levelAt(30))});
    CHECK_THAT(volumeMm3(body), WithinRel(frustum(10, 5, 30), kRelSpline));
    const double t = frustumCentroid(10, 5);
    checkPoint(requireProperties(body).centerOfMass, 10.0 * t, 0, 30.0 * t);
    checkBox(body, {-10, -10, 0}, {15, 10, 30});
    // Offset rectangles: the prism leans, with the same volume.
    const Body leaning = requireLoft({region(rectangleLoop(0, 0, 10, 20), levelAt(0)),
                                      region(rectangleLoop(7, -3, 10, 20), levelAt(50))});
    CHECK_THAT(volumeMm3(leaning), WithinRel(200.0 * 50.0, kRelTight));
    checkPoint(requireProperties(leaning).centerOfMass, 3.5, -1.5, 25);
    checkBox(leaning, {-5, -13, 0}, {12, 10, 50});
}

TEST_CASE("Loft_KeepsTheOrderAndDirectionOfItsSections", "[geometry][loft]") {
    const PlanarRegion bottom = region(circleLoop(0, 0, 10), levelAt(0));
    const PlanarRegion middle = region(circleLoop(0, 0, 8), levelAt(50));
    const PlanarRegion top = region(circleLoop(0, 0, 5), levelAt(20));
    SECTION("listed from the top down, the loft runs down") {
        const Body down = requireLoft({region(circleLoop(0, 0, 5), levelAt(30)), bottom});
        CHECK_THAT(volumeMm3(down), WithinRel(frustum(10, 5, 30), kRelTight));
        checkPoint(requireProperties(down).centerOfMass, 0, 0, 30.0 * frustumCentroid(10, 5));
    }
    SECTION("sections that go back are refused, not sorted") {
        const Error error = loftError({bottom, middle, top});
        CHECK(error.code == ErrorCode::InvalidArgument);
        CHECK(error.message == "makeLoft: section 3 lies 30 mm behind section 2 along the loft: the sections must be "
                               "listed in the order they follow one another");
        // The same sections in their order along the loft.
        const Body ordered = requireLoft({bottom, top, middle});
        CHECK_THAT(volumeMm3(ordered), WithinRel(frustum(10, 5, 20) + frustum(5, 8, 30), kRelTight));
    }
    SECTION("sections on one plane are refused") {
        const Error error = loftError({bottom, region(circleLoop(0, 0, 5), levelAt(0))});
        CHECK(error.code == ErrorCode::InvalidArgument);
        CHECK(error.message == "makeLoft: sections 1 and 2 lie on the same plane: a loft needs its sections apart");
    }
}

TEST_CASE("Loft_MatchesSectionsWithTheLeastTwist", "[geometry][loft]") {
    SECTION("a rectangle drawn from any corner, either way round, matches corner to corner") {
        // 20 x 10 to 10 x 20: matched corner to nearest corner the loft is
        // 6500 mm^3; a quarter-turn twist would give 4000.
        const std::array<std::pair<double, double>, 4> corners{{{5, 10}, {-5, 10}, {-5, -10}, {5, -10}}};
        for (std::size_t first = 0; first < 4; ++first) {
            for (const bool clockwise : {false, true}) {
                CAPTURE(first, clockwise);
                ProfileLoop top;
                for (std::size_t k = 0; k < 4; ++k) {
                    const std::size_t a = clockwise ? (first + 4 - k) % 4 : (first + k) % 4;
                    const std::size_t b = clockwise ? (first + 3 - k) % 4 : (first + k + 1) % 4;
                    top.segments.emplace_back(LineSegment2D{mm(corners[a].first, corners[a].second),
                                                            mm(corners[b].first, corners[b].second)});
                }
                const Body body =
                    requireLoft({region(rectangleLoop(0, 0, 20, 10), levelAt(0)), region(top, levelAt(30))});
                CHECK_THAT(volumeMm3(body), WithinRel(6500.0, kRelTight));
            }
        }
    }
    SECTION("a square turned by 30 deg twists by 30 deg, not 60") {
        // Am is the square of corners averaged: side 20 cos 15 deg.
        const double side = 20.0 * std::cos(pi / 12.0);
        const Body body = requireLoft({region(regular(4, 10.0 * std::sqrt(2.0), pi / 4.0), levelAt(0)),
                                       region(regular(4, 10.0 * std::sqrt(2.0), pi / 4.0 + pi / 6.0), levelAt(30))});
        CHECK_THAT(volumeMm3(body), WithinRel(5.0 * (400.0 + 4.0 * side * side + 400.0), kRelTight));
    }
    SECTION("circles match angle for angle whatever their sketch axes and normals") {
        // The top circle on a plane turned by 70 deg, and on one facing down:
        // the same frustum (a mismatched seam or winding would twist it).
        const PlanarRegion bottom = region(circleLoop(0, 0, 10), levelAt(0));
        const Frame3D facingDown =
            Frame3D::create(Point3D{0_mm, 0_mm, 30_mm}, Direction3D::unitZ().reversed(), Direction3D::unitX()).value();
        for (const Frame3D& plane : {levelAt(30, 70.0 * pi / 180.0), facingDown}) {
            const Body body = requireLoft({bottom, region(circleLoop(0, 0, 5), plane)});
            CHECK_THAT(volumeMm3(body), WithinRel(frustum(10, 5, 30), kRelTight));
        }
    }
}

TEST_CASE("Loft_BreaksTiesDeterministically", "[geometry][loft]") {
    // A square to the same square turned by 45 deg: twisting either way is
    // equally short. The tie goes to the top loop's first matching start,
    // whatever rounding noise says: turned by +-1e-12 rad the same side edge
    // exists. Corner (10, 10, 0) runs to (0, 14.14, 30) (+45 deg) or to
    // (14.14, 0, 30) (-45 deg). (A line reference does not find the
    // kernel's side edges, so they are found by their ends.)
    const double r = 10.0 * std::sqrt(2.0);
    const PlanarRegion bottom = region(regular(4, r, pi / 4.0), levelAt(0));
    const auto edgesBetween = [](const Body& body, const Point3D& p, const Point3D& q) {
        std::size_t found = 0;
        for (const EdgeInfo& edge : listEdges(body).value()) {
            const auto near = [](const Point3D& a, const Point3D& b) { return distance(a, b).in(units::mm) <= 1e-7; };
            if ((near(edge.start, p) && near(edge.end, q)) || (near(edge.start, q) && near(edge.end, p))) {
                ++found;
            }
        }
        return found;
    };
    const Point3D corner{10_mm, 10_mm, 0_mm};
    std::vector<double> volumes;
    for (const double noise : {0.0, 1e-12, -1e-12}) {
        CAPTURE(noise);
        const Body body = requireLoft({bottom, region(regular(4, r, pi / 2.0 + noise), levelAt(30))});
        volumes.push_back(volumeMm3(body));
        CHECK(edgesBetween(body, corner, Point3D{0_mm, r * units::mm, 30_mm}) == 1); // +45 deg
        CHECK(edgesBetween(body, corner, Point3D{r * units::mm, 0_mm, 30_mm}) == 0); // not -45 deg
    }
    const double side = 20.0 * std::cos(pi / 8.0);
    CHECK_THAT(volumes[0], WithinRel(5.0 * (400.0 + 4.0 * side * side + 400.0), kRelTight));
    // And the same inputs give the same bits.
    CHECK(bits(volumeMm3(requireLoft({bottom, region(regular(4, r, pi / 2.0), levelAt(30))}))) == bits(volumes[0]));
}

TEST_CASE("Loft_ArcsMatchArcsOfEqualSweep", "[geometry][loft]") {
    SECTION("a slot to a slot of half its size: h/3 (A1 + A2 + sqrt(A1 A2))") {
        const double a1 = 200.0 + 25.0 * pi;
        const Body body = requireLoft({region(slotLoop(10, 5), levelAt(0)), region(slotLoop(5, 2.5), levelAt(30))});
        CHECK_THAT(volumeMm3(body), WithinRel(10.0 * (a1 + a1 / 4.0 + a1 / 2.0), kRelSpline));
        checkBox(body, {-15, -5, 0}, {15, 5, 30});
    }
    SECTION("a pac-man (a 300 deg arc) to one of half its size about the arc's centre") {
        const auto pacman = [](double r) {
            const Point2D a = mm(r * std::cos(pi / 6.0), r * std::sin(pi / 6.0));
            const Point2D b = mm(r * std::cos(pi / 6.0), -r * std::sin(pi / 6.0));
            return ProfileLoop{{ArcSegment2D{mm(0, 0), a, b, true}, LineSegment2D{b, mm(0, 0)}, LineSegment2D{mm(0, 0), a}}};
        };
        const double a1 = 100.0 * pi * 300.0 / 360.0;
        const Body body = requireLoft({region(pacman(10), levelAt(0)), region(pacman(5), levelAt(30))});
        CHECK_THAT(volumeMm3(body), WithinRel(10.0 * (a1 + a1 / 4.0 + a1 / 2.0), kRelTight));
    }
}

TEST_CASE("Loft_RejectsSectionsItCannotLoft", "[geometry][loft]") {
    const PlanarRegion bottom = region(circleLoop(0, 0, 10), levelAt(0));
    const auto message = [](const std::vector<PlanarRegion>& sections) {
        const Error error = loftError(sections);
        CHECK(error.code == ErrorCode::InvalidArgument);
        return error.message;
    };
    CHECK(message({}) == "makeLoft: a loft needs at least two sections, got 0");
    CHECK(message({bottom}) == "makeLoft: a loft needs at least two sections, got 1");
    CHECK(message({region(circleLoop(0, 0, 10), levelAt(0), {circleLoop(0, 0, 3)}), region(circleLoop(0, 0, 5), levelAt(30))}) ==
          "makeLoft: section 1 has a hole: loft sections must be single closed loops");
    const double nan = std::numeric_limits<double>::quiet_NaN();
    CHECK(message({bottom, region(rectangleLoop(0, 0, nan, 5), levelAt(30))}) == "makeLoft: section 2 is not finite");
    const double tilt = 10.0 * pi / 180.0;
    const Frame3D tilted = Frame3D::create(Point3D{0_mm, 0_mm, 30_mm},
                                           *Direction3D::fromComponents(0.0, -std::sin(tilt), std::cos(tilt)),
                                           Direction3D::unitX())
                               .value();
    CHECK(message({bottom, region(circleLoop(0, 0, 5), tilted)}) ==
          "makeLoft: section 2 is not parallel to section 1: their planes are 10 deg apart; only sections on parallel "
          "planes can be lofted");
    // Sections of different shapes were refused here until
    // P12-LOFT-001, which matches them by arc length instead. Each of the
    // four kinds that used to be refused now builds one valid solid; the
    // volumes are checked against closed forms in LoftShapeTests.cpp.
    const auto builds = [](const std::vector<PlanarRegion>& sections) {
        auto body = makeLoft(sections);
        if (!body) {
            FAIL(body.error().message);
        }
        CHECK(body->isValid());
        return body->topology().solids;
    };
    CHECK(builds({bottom, region(rectangleLoop(0, 0, 10, 10), levelAt(30))}) == 1);        // a circle to 4 lines
    CHECK(builds({region(rectangleLoop(0, 0, 10, 10), levelAt(0)),
                  region(regular(6, 5), levelAt(30))}) == 1);                              // 4 lines to 6
    // A D (a line and a half circle) against a line and a 270 deg arc.
    const ProfileLoop half{{LineSegment2D{mm(0, -5), mm(0, 5)}, ArcSegment2D{mm(0, 0), mm(0, 5), mm(0, -5), true}}};
    const double s = 5.0 / std::sqrt(2.0);
    const ProfileLoop most{{LineSegment2D{mm(s, -s), mm(s, s)}, ArcSegment2D{mm(0, 0), mm(s, s), mm(s, -s), true}}};
    CHECK(builds({region(half, levelAt(0)), region(most, levelAt(30))}) == 1);              // arcs of unequal sweep
    // Lines and arcs in another order: a slot (arc, line, arc, line) against
    // line, line, arc, arc (two half circles on a square's corner).
    const ProfileLoop corner{{LineSegment2D{mm(0, 0), mm(10, 0)}, LineSegment2D{mm(10, 0), mm(10, 10)},
                              ArcSegment2D{mm(5, 10), mm(10, 10), mm(0, 10), true},
                              ArcSegment2D{mm(0, 5), mm(0, 10), mm(0, 0), true}}};
    CHECK(builds({region(slotLoop(10, 5), levelAt(0)), region(corner, levelAt(30))}) == 1); // another order
}

TEST_CASE("Loft_TwistedSectionsFollowTheTwistLaw", "[geometry][loft]") {
    // A section lofted to itself turned by theta about a point, every point
    // to its turned image: the section halfway has the area A (1 + cos theta)
    // / 2, so V = h A (2 + cos theta) / 3 (prismatoid formula).
    const auto law = [](double h, double area, double theta) { return h * area * (2.0 + std::cos(theta)) / 3.0; };
    SECTION("a square turned by 30 deg") {
        const double r = 10.0 * std::sqrt(2.0);
        const Body body = requireLoft({region(regular(4, r, pi / 4.0), levelAt(0)),
                                       region(regular(4, r, pi / 4.0 + pi / 6.0), levelAt(30))});
        CHECK_THAT(volumeMm3(body), WithinRel(law(30, 400, pi / 6.0), kRelTight));
    }
    SECTION("a half disc turned by 90 deg about its centre (coaxial arcs of other angles)") {
        // A line and a half circle can only be matched line to line.
        const auto halfDisc = [](double turn) {
            const auto at = [&](double a) { return mm(5.0 * std::cos(a + turn), 5.0 * std::sin(a + turn)); };
            return ProfileLoop{{LineSegment2D{at(-pi / 2.0), at(pi / 2.0)},
                                ArcSegment2D{mm(0, 0), at(pi / 2.0), at(-pi / 2.0), true}}};
        };
        const Body body = requireLoft({region(halfDisc(0.0), levelAt(0)), region(halfDisc(pi / 2.0), levelAt(30))});
        CHECK_THAT(volumeMm3(body), WithinRel(law(30, 12.5 * pi, pi / 2.0), kRelSpline));
    }
}

TEST_CASE("Loft_RejectsLoftsThatFoldOrIntersect", "[geometry][loft]") {
    SECTION("a half disc turned by 180 deg pinches to nothing halfway (checked before the kernel)") {
        const ProfileLoop left{{LineSegment2D{mm(0, -5), mm(0, 5)}, ArcSegment2D{mm(0, 0), mm(0, 5), mm(0, -5), true}}};
        const ProfileLoop right{{LineSegment2D{mm(0, 5), mm(0, -5)}, ArcSegment2D{mm(0, 0), mm(0, -5), mm(0, 5), true}}};
        const Error error = loftError({region(left, levelAt(0)), region(right, levelAt(30))});
        CHECK(error.code == ErrorCode::InvalidArgument);
        CHECK(error.message == "makeLoft: the loft between sections 1 and 2 folds over itself: its cross-section would "
                               "lose all its area 50% of the way from section 1 to section 2");
    }
    SECTION("an L turned by 90 deg: its sides pass through one another (the kernel's self-interference check)") {
        const auto ell = [](double turn) {
            ProfileLoop loop;
            const std::array<std::pair<double, double>, 6> p{{{0, 0}, {20, 0}, {20, 5}, {5, 5}, {5, 20}, {0, 20}}};
            for (std::size_t i = 0; i < p.size(); ++i) {
                const auto at = [&](std::pair<double, double> q) {
                    return mm(q.first * std::cos(turn) - q.second * std::sin(turn),
                              q.first * std::sin(turn) + q.second * std::cos(turn));
                };
                loop.segments.emplace_back(LineSegment2D{at(p[i]), at(p[(i + 1) % p.size()])});
            }
            return loop;
        };
        const Error error = loftError({region(ell(0.0), levelAt(0)), region(ell(pi / 2.0), levelAt(30))});
        CHECK(error.code == ErrorCode::InvalidArgument);
        CHECK(error.message == "makeLoft: the lofted solid would intersect itself: its sides pass through one another "
                               "between the sections");
        // Turned by 36 deg it stays clear.
        CHECK(requireLoft({region(ell(0.0), levelAt(0)), region(ell(pi / 5.0), levelAt(30))}).isValid());
    }
}
