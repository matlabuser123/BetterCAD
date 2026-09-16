#include "GeometryTestSupport.hpp"
#include "TestHelpers.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/geometry/Profile.hpp>
#include <bettercad/core/geometry/Sweeps.hpp>
#include <bettercad/core/math/BSpline.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <array>
#include <cmath>
#include <numbers>
#include <utility>
#include <vector>

using namespace bettercad;
using namespace bettercad::geometry;
using namespace bettercad::literals;
using bettercad::test::errorCode;
using bettercad::test::requireProperties;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// P12-SKETCH-002: ellipse and spline segments in profiles and in the
// kernel's solids. Expected areas, centroids and volumes are computed here
// by other means: pi a b for ellipses, Archimedes' quadrature of the
// parabola for quadratic splines, Boole's rule (exact for polynomials of
// degree 5) on Bernstein polynomials for the others, and Pappus for solids
// of revolution.

namespace {

constexpr double pi = std::numbers::pi;
// Green's theorem, exact to rounding.
constexpr double kRelArea = 1e-12;

Point2D mm(double u, double v) {
    return Point2D{u * units::mm, v * units::mm};
}

std::vector<Point2D> polygonMm(std::initializer_list<std::array<double, 2>> points) {
    std::vector<Point2D> result;
    for (const auto& p : points) {
        result.push_back(mm(p[0], p[1]));
    }
    return result;
}

PlanarRegion regionOf(ProfileLoop outer, std::vector<ProfileLoop> holes = {}, const Frame3D& plane = Frame3D::xy()) {
    return PlanarRegion{.plane = plane, .outer = std::move(outer), .holes = std::move(holes)};
}

ProfileLoop loopOf(ProfileSegment segment) {
    return ProfileLoop{{std::move(segment)}};
}

double areaMm2(const ProfileLoop& loop) {
    return signedArea(loop).in(units::mm2);
}

double binomial(int n, int k) {
    double result = 1.0;
    for (int i = 1; i <= k; ++i) {
        result = result * (n - k + i) / i;
    }
    return result;
}

/// Bézier point and derivative (mm) at t.
std::array<double, 4> bezier(const std::vector<std::array<double, 2>>& control, double t) {
    const int d = static_cast<int>(control.size()) - 1;
    std::array<double, 4> r{};
    for (int i = 0; i <= d; ++i) {
        const double b = binomial(d, i) * std::pow(t, i) * std::pow(1.0 - t, d - i);
        r[0] += b * control[static_cast<std::size_t>(i)][0];
        r[1] += b * control[static_cast<std::size_t>(i)][1];
    }
    for (int i = 0; i < d; ++i) {
        const double b = d * binomial(d - 1, i) * std::pow(t, i) * std::pow(1.0 - t, d - 1 - i);
        r[2] += b * (control[static_cast<std::size_t>(i) + 1][0] - control[static_cast<std::size_t>(i)][0]);
        r[3] += b * (control[static_cast<std::size_t>(i) + 1][1] - control[static_cast<std::size_t>(i)][1]);
    }
    return r;
}

/// Area and first moments enclosed by Bézier pieces (in mm), by composite
/// Boole's rule: 1/2 (x y' - y x'), x^2/2 y' and -y^2/2 x' integrated over
/// each piece. Boole's rule is exact for polynomials of degree 5.
std::array<double, 3> booleIntegrals(const std::vector<std::vector<std::array<double, 2>>>& pieces, int panels) {
    constexpr std::array<double, 5> weights{7.0, 32.0, 12.0, 32.0, 7.0};
    std::array<double, 3> sums{};
    for (const auto& control : pieces) {
        for (int panel = 0; panel < panels; ++panel) {
            for (int k = 0; k < 5; ++k) {
                const double t = (panel + k / 4.0) / panels;
                const auto p = bezier(control, t);
                const double w = weights[static_cast<std::size_t>(k)] / 90.0 / panels;
                sums[0] += w * 0.5 * (p[0] * p[3] - p[1] * p[2]);
                sums[1] += w * 0.5 * p[0] * p[0] * p[3];
                sums[2] -= w * 0.5 * p[1] * p[1] * p[2];
            }
        }
    }
    return sums;
}

/// The spline's Bézier pieces in millimetres.
std::vector<std::vector<std::array<double, 2>>> piecesMm(const SplineSegment2D& spline) {
    const auto curve = UniformBSpline::create(spline.poles, spline.degree, spline.periodic);
    REQUIRE(curve.has_value());
    auto pieces = curve->bezierPieces();
    for (auto& piece : pieces) {
        for (auto& q : piece) {
            q = {q[0] * 1e3, q[1] * 1e3};
        }
    }
    return pieces;
}

double volumeMm3(const Body& body) {
    return requireProperties(body).volume.in(units::mm3);
}

Body requireBody(const Result<Body>& body) {
    if (!body) {
        FAIL(body.error().message);
    }
    CHECK(body->isValid());
    CHECK(body->topology().solids == 1);
    return *body;
}

/// Signed area of a polygon (shoelace), in mm^2.
double shoelace(const std::vector<std::array<double, 2>>& p) {
    double sum = 0.0;
    for (std::size_t i = 0; i < p.size(); ++i) {
        const auto& a = p[i];
        const auto& b = p[(i + 1) % p.size()];
        sum += a[0] * b[1] - b[0] * a[1];
    }
    return 0.5 * sum;
}

} // namespace

// --- Areas and centroids ------------------------------------------------------------------------

TEST_CASE("CurvedProfile_EllipseAreaIsPiAB", "[core][geometry][profile][p12]") {
    // a = 30 along 30 deg, b = 12; and a shorter first axis (8 and 20).
    const double c = std::cos(pi / 6.0);
    const double s = std::sin(pi / 6.0);
    const EllipseSegment2D wide{mm(5, -7), mm(5 + 30 * c, -7 + 30 * s), 12_mm, true};
    const EllipseSegment2D tall{mm(-40, 10), mm(-32, 10), 20_mm, true};
    CHECK_THAT(areaMm2(loopOf(wide)), WithinRel(pi * 30 * 12, kRelArea));
    CHECK_THAT(areaMm2(loopOf(tall)), WithinRel(pi * 8 * 20, kRelArea));
    EllipseSegment2D clockwise = wide;
    clockwise.counterClockwise = false;
    CHECK_THAT(areaMm2(loopOf(clockwise)), WithinRel(-pi * 30 * 12, kRelArea));
    CHECK_THAT(areaMm2(reversed(loopOf(wide))), WithinRel(-pi * 30 * 12, kRelArea));

    const Point2D centre = regionCentroid(regionOf(loopOf(wide)));
    CHECK_THAT(centre.x.in(units::mm), WithinAbs(5.0, 1e-12));
    CHECK_THAT(centre.y.in(units::mm), WithinAbs(-7.0, 1e-12));

    // A 100 x 60 plate with the tall ellipse's hole moved inside it.
    const EllipseSegment2D hole{mm(70, 30), mm(78, 30), 20_mm, false};
    const ProfileLoop plate{{LineSegment2D{mm(0, 0), mm(100, 0)}, LineSegment2D{mm(100, 0), mm(100, 60)},
                             LineSegment2D{mm(100, 60), mm(0, 60)}, LineSegment2D{mm(0, 60), mm(0, 0)}}};
    const PlanarRegion withHole = regionOf(plate, {loopOf(hole)});
    const double holeArea = pi * 8 * 20;
    CHECK_THAT(regionArea(withHole).in(units::mm2), WithinRel(6000.0 - holeArea, kRelArea));
    const Point2D centroid = regionCentroid(withHole);
    CHECK_THAT(centroid.x.in(units::mm), WithinRel((6000.0 * 50 - holeArea * 70) / (6000.0 - holeArea), kRelArea));
    CHECK_THAT(centroid.y.in(units::mm), WithinRel(30.0, kRelArea));
}

TEST_CASE("CurvedProfile_QuadraticSplineAreaFollowsArchimedes", "[core][geometry][profile][p12]") {
    // A periodic quadratic spline's pieces are parabolas from edge midpoint to
    // edge midpoint with the corner as control point: the enclosed area is the
    // midpoint polygon's plus 2/3 of each corner triangle (Archimedes).
    SECTION("a square: 5/6 of its area") {
        const SplineSegment2D square{polygonMm({{0, 0}, {60, 0}, {60, 60}, {0, 60}}), 2, true};
        CHECK_THAT(areaMm2(loopOf(square)), WithinRel(3000.0, kRelArea));
        const Point2D centre = regionCentroid(regionOf(loopOf(square)));
        CHECK_THAT(centre.x.in(units::mm), WithinRel(30.0, kRelArea));
        CHECK_THAT(centre.y.in(units::mm), WithinRel(30.0, kRelArea));
    }
    SECTION("an irregular pentagon") {
        const std::vector<std::array<double, 2>> p{{0, 0}, {50, -10}, {70, 30}, {25, 55}, {-15, 25}};
        std::vector<Point2D> poles;
        for (const auto& q : p) {
            poles.push_back(mm(q[0], q[1]));
        }
        std::vector<std::array<double, 2>> mid;
        for (std::size_t i = 0; i < p.size(); ++i) {
            const auto& a = p[i];
            const auto& b = p[(i + 1) % p.size()];
            mid.push_back({(a[0] + b[0]) / 2, (a[1] + b[1]) / 2});
        }
        double expected = shoelace(mid);
        for (std::size_t i = 0; i < p.size(); ++i) {
            // The parabola from mid[i] to mid[i + 1] with control p[i + 1].
            expected += 2.0 / 3.0 * shoelace({mid[i], p[(i + 1) % p.size()], mid[(i + 1) % p.size()]});
        }
        const SplineSegment2D spline{poles, 2, true};
        CHECK_THAT(areaMm2(loopOf(spline)), WithinRel(expected, kRelArea));
    }
}

TEST_CASE("CurvedProfile_SplineAreaAndCentroidMatchBooleIntegration", "[core][geometry][profile][p12]") {
    const std::vector<Point2D> poles = polygonMm({{0, 0}, {50, -10}, {70, 30}, {60, 50}, {25, 55}, {-15, 25}, {-5, 10}});
    for (int degree = 2; degree <= 5; ++degree) {
        CAPTURE(degree);
        const SplineSegment2D spline{poles, degree, true};
        // Boole's error falls with the 6th power of the panel width: with 64
        // panels per piece the moments (degree 3 d - 1, up to 14) are exact
        // to rounding.
        const auto expected = booleIntegrals(piecesMm(spline), 64);
        // The area integrand (degree 2 d - 1) is exact with one panel up to
        // degree 3.
        if (degree <= 3) {
            CHECK_THAT(booleIntegrals(piecesMm(spline), 1)[0], WithinRel(expected[0], kRelArea));
        }
        CHECK_THAT(areaMm2(loopOf(spline)), WithinRel(expected[0], kRelArea));
        const Point2D centre = regionCentroid(regionOf(loopOf(spline)));
        CHECK_THAT(centre.x.in(units::mm), WithinRel(expected[1] / expected[0], 1e-11));
        CHECK_THAT(centre.y.in(units::mm), WithinRel(expected[2] / expected[0], 1e-11));
    }
}

TEST_CASE("CurvedProfile_OpenSplineClosesALoopWithLines", "[core][geometry][profile][p12]") {
    // A "D": a line from (0, 0) to (60, 0) and a cubic Bézier arch back.
    const SplineSegment2D arch{polygonMm({{60, 0}, {60, 45}, {0, 45}, {0, 0}}), 3, false};
    const ProfileLoop d{{LineSegment2D{mm(0, 0), mm(60, 0)}, arch}};
    // The line adds nothing to the integrals (y = 0 along it).
    const auto expected = booleIntegrals({{{60, 0}, {60, 45}, {0, 45}, {0, 0}}}, 64);
    CHECK_THAT(areaMm2(d), WithinRel(expected[0], kRelArea));
    // Closed form: a cubic Bezier with its control points at the corners of a
    // w x h rectangle encloses 3/5 w h with the side it spans.
    CHECK_THAT(areaMm2(d), WithinRel(3.0 / 5.0 * 60 * 45, kRelArea));
    const Point2D centre = regionCentroid(regionOf(d));
    CHECK_THAT(centre.x.in(units::mm), WithinRel(30.0, 1e-12));
    CHECK_THAT(centre.y.in(units::mm), WithinRel(expected[2] / expected[0], 1e-11));
    // Reversed, the spline's poles run the other way and the area flips.
    CHECK_THAT(areaMm2(reversed(d)), WithinRel(-expected[0], kRelArea));
    CHECK(std::get<SplineSegment2D>(reversed(d).segments.front()).poles.front() == mm(0, 0));
}

TEST_CASE("CurvedProfile_SegmentPointsFollowTheirCurves", "[core][geometry][profile][p12]") {
    const EllipseSegment2D ellipse{mm(10, 20), mm(40, 20), 12_mm, true};
    CHECK(isClosedSegment(ellipse));
    CHECK(firstPoint(ellipse) == mm(40, 20));
    CHECK(lastPoint(ellipse) == mm(40, 20));
    const Point2D quarter = pointAt(ellipse, 0.25);
    CHECK_THAT(quarter.x.in(units::mm), WithinAbs(10.0, 1e-12));
    CHECK_THAT(quarter.y.in(units::mm), WithinAbs(32.0, 1e-12));
    EllipseSegment2D clockwise = ellipse;
    clockwise.counterClockwise = false;
    CHECK_THAT(pointAt(clockwise, 0.25).y.in(units::mm), WithinAbs(8.0, 1e-12));

    const SplineSegment2D open{polygonMm({{0, 0}, {10, 40}, {35, -20}, {60, 30}}), 3, false};
    CHECK_FALSE(isClosedSegment(open));
    CHECK(firstPoint(open) == mm(0, 0));
    CHECK(lastPoint(open) == mm(60, 30));
    // A periodic cubic starts at (P0 + 4 P1 + P2) / 6 = (50, 10).
    const SplineSegment2D periodic{polygonMm({{0, 0}, {60, 0}, {60, 60}, {0, 60}}), 3, true};
    CHECK(isClosedSegment(periodic));
    CHECK_THAT(firstPoint(periodic).x.in(units::mm), WithinAbs(50.0, 1e-12));
    CHECK_THAT(firstPoint(periodic).y.in(units::mm), WithinAbs(10.0, 1e-12));
    CHECK(lastPoint(periodic) == firstPoint(periodic));

    CHECK(validate(open).has_value());
    SplineSegment2D bad = open;
    bad.degree = 7;
    CHECK(errorCode(validate(bad)) == ErrorCode::InvalidArgument);
}

// --- Solids ------------------------------------------------------------------------------------

TEST_CASE("CurvedProfile_ExtrudedEllipseHasVolumePiABH", "[core][geometry][profile][p12]") {
    const double c = std::cos(pi / 6.0);
    const double s = std::sin(pi / 6.0);
    for (const bool longFirstAxis : {true, false}) {
        CAPTURE(longFirstAxis);
        const double first = longFirstAxis ? 30.0 : 8.0;
        const double second = longFirstAxis ? 12.0 : 20.0;
        const EllipseSegment2D ellipse{mm(5, -7), mm(5 + first * c, -7 + first * s), second * units::mm, true};
        const Body body = requireBody(makePrism(regionOf(loopOf(ellipse)), 0_mm, 25_mm));
        CHECK_THAT(volumeMm3(body), WithinRel(pi * first * second * 25.0, bettercad::test::kRelTight));
        CHECK(body.topology().faces == 3);
    }
}

TEST_CASE("CurvedProfile_ExtrudedSplinesHaveTheirAreaTimesDepth", "[core][geometry][profile][p12]") {
    SECTION("a periodic quadratic spline on a square") {
        const SplineSegment2D square{polygonMm({{0, 0}, {60, 0}, {60, 60}, {0, 60}}), 2, true};
        const Body body = requireBody(makePrism(regionOf(loopOf(square)), 0_mm, 10_mm));
        CHECK_THAT(volumeMm3(body), WithinRel(30000.0, 1e-9));
    }
    SECTION("periodic splines of every degree") {
        const std::vector<Point2D> poles =
            polygonMm({{0, 0}, {50, -10}, {70, 30}, {60, 50}, {25, 55}, {-15, 25}, {-5, 10}});
        for (int degree = 2; degree <= 5; ++degree) {
            CAPTURE(degree);
            const SplineSegment2D spline{poles, degree, true};
            const double area = booleIntegrals(piecesMm(spline), 64)[0];
            const Body body = requireBody(makePrism(regionOf(loopOf(spline)), -5_mm, 15_mm));
            CHECK_THAT(volumeMm3(body), WithinRel(area * 20.0, 1e-9));
        }
    }
    SECTION("a line and an open spline, with an elliptic hole") {
        const SplineSegment2D arch{polygonMm({{60, 0}, {60, 45}, {0, 45}, {0, 0}}), 3, false};
        const ProfileLoop d{{LineSegment2D{mm(0, 0), mm(60, 0)}, arch}};
        const EllipseSegment2D hole{mm(30, 15), mm(40, 15), 5_mm, true};
        const Body body = requireBody(makePrism(regionOf(d, {loopOf(hole)}), 0_mm, 10_mm));
        CHECK_THAT(volumeMm3(body), WithinRel((1620.0 - pi * 50.0) * 10.0, 1e-9));
    }
}

TEST_CASE("CurvedProfile_RevolvedEllipseAndSplineFollowPappus", "[core][geometry][profile][p12]") {
    const Axis3D yAxis{Point3D{}, Direction3D::unitY()};
    SECTION("an ellipse 50 mm from the axis") {
        const EllipseSegment2D ellipse{mm(50, 0), mm(60, 0), 6_mm, true};
        const double full = 2.0 * pi * 50.0 * pi * 10.0 * 6.0;
        const Body ring = requireBody(makeRevolution(regionOf(loopOf(ellipse)), yAxis, 0_deg, 360_deg));
        CHECK_THAT(volumeMm3(ring), WithinRel(full, 1e-9));
        const Body quarter = requireBody(makeRevolution(regionOf(loopOf(ellipse)), yAxis, 0_deg, 90_deg));
        CHECK_THAT(volumeMm3(quarter), WithinRel(full / 4.0, 1e-9));
    }
    SECTION("a quadratic spline square centred 50 mm from the axis") {
        const SplineSegment2D square{polygonMm({{40, 20}, {60, 20}, {60, 40}, {40, 40}}), 2, true};
        const Body body = requireBody(makeRevolution(regionOf(loopOf(square)), yAxis, 0_deg, 360_deg));
        CHECK_THAT(volumeMm3(body), WithinRel(2.0 * pi * 50.0 * (5.0 * 400.0 / 6.0), 1e-9));
    }
    SECTION("a spline whose poles cross the axis is refused, although its curve does not") {
        // The corner pole at x = -1 pulls the parabola only to x = 2 (the
        // quadratic piece's middle is (5 - 2 + 5) / 4): the check uses the
        // poles, which bound the curve.
        const SplineSegment2D kite{polygonMm({{-1, 0}, {11, -10}, {23, 0}, {11, 10}}), 2, true};
        const auto body = makeRevolution(regionOf(loopOf(kite)), yAxis, 0_deg, 360_deg);
        REQUIRE_FALSE(body.has_value());
        CHECK(body.error().code == ErrorCode::InvalidArgument);
        CHECK_THAT(body.error().message, ContainsSubstring("the profile crosses the revolution axis"));
    }
}

TEST_CASE("CurvedProfile_SweptEllipseFollowsPappus", "[core][geometry][profile][p12]") {
    // Up the Z axis for 40 mm, then a quarter turn of radius 60 towards +X.
    const PlanarPath path{
        .plane = Frame3D::xz(),
        .segments = {LineSegment2D{mm(0, 0), mm(0, 40)}, ArcSegment2D{mm(60, 40), mm(0, 40), mm(60, 100), false}}};
    const EllipseSegment2D ellipse{mm(0, 0), mm(10, 0), 6_mm, true};
    const Body body = requireBody(makeSweep(regionOf(loopOf(ellipse)), path));
    // The centroid is on the path: V = A (40 + 60 pi / 2).
    CHECK_THAT(volumeMm3(body), WithinRel(pi * 60.0 * (40.0 + 30.0 * pi), 1e-9));
}

TEST_CASE("CurvedProfile_SweptCurveSidesHaveTheirAnalyticArea", "[core][geometry][profile][p12]") {
    // Perimeters: the Gauss-Kummer series for ellipses, and the closed-form
    // arc length of parabolas for the quadratic spline square.
    const auto kummer = [](double a, double b) {
        const double h = std::pow((a - b) / (a + b), 2.0);
        double sum = 0.0;
        double coefficient = 1.0;
        double power = 1.0;
        for (int n = 0; n < 80; ++n) {
            sum += coefficient * coefficient * power;
            coefficient *= (0.5 - n) / (n + 1);
            power *= h;
        }
        return pi * (a + b) * sum;
    };
    SECTION("an elliptic prism") {
        const EllipseSegment2D ellipse{mm(0, 0), mm(30, 0), 10_mm, true};
        const Body body = requireBody(makePrism(regionOf(loopOf(ellipse)), 0_mm, 20_mm));
        const MassProperties p = requireProperties(body);
        CHECK_THAT(p.surfaceArea.in(units::mm2), WithinRel(2.0 * pi * 300.0 + kummer(30.0, 10.0) * 20.0, 1e-12));
        CHECK(p.areaRelativeError < 1e-10);
        CHECK_THAT(p.centerOfMass.z.in(units::mm), WithinAbs(10.0, 1e-12));
    }
    SECTION("a spline prism") {
        // Each piece runs from an edge midpoint round a corner: B'(t) = 2 a t + b
        // with a = (-30, 30), b = (60, 0) (mm).
        const double A = 4.0 * (30.0 * 30.0 + 30.0 * 30.0);
        const double B = 4.0 * (-30.0 * 60.0);
        const double C = 60.0 * 60.0;
        const auto antiderivative = [&](double t) {
            const double q = std::sqrt(A * t * t + B * t + C);
            return (2.0 * A * t + B) / (4.0 * A) * q +
                   (4.0 * A * C - B * B) / (8.0 * std::pow(A, 1.5)) *
                       std::log(2.0 * std::sqrt(A) * q + 2.0 * A * t + B);
        };
        const double perimeter = 4.0 * (antiderivative(1.0) - antiderivative(0.0));
        const SplineSegment2D square{polygonMm({{0, 0}, {60, 0}, {60, 60}, {0, 60}}), 2, true};
        const Body body = requireBody(makePrism(regionOf(loopOf(square)), 0_mm, 10_mm));
        const MassProperties p = requireProperties(body);
        CHECK_THAT(p.surfaceArea.in(units::mm2), WithinRel(2.0 * 3000.0 + perimeter * 10.0, 1e-12));
        CHECK_THAT(p.volume.in(units::mm3), WithinRel(30000.0, 1e-13));
        CHECK_THAT(p.centerOfMass.x.in(units::mm), WithinAbs(30.0, 1e-12));
        CHECK_THAT(p.centerOfMass.y.in(units::mm), WithinAbs(30.0, 1e-12));
        CHECK_THAT(p.centerOfMass.z.in(units::mm), WithinAbs(5.0, 1e-12));
    }
    SECTION("a revolved ellipse: Pappus for the surface") {
        const EllipseSegment2D ellipse{mm(50, 0), mm(60, 0), 6_mm, true};
        const Body ring = requireBody(
            makeRevolution(regionOf(loopOf(ellipse)), Axis3D{Point3D{}, Direction3D::unitY()}, 0_deg, 360_deg));
        const MassProperties p = requireProperties(ring);
        CHECK_THAT(p.surfaceArea.in(units::mm2), WithinRel(2.0 * pi * 50.0 * kummer(10.0, 6.0), 1e-12));
        CHECK_THAT(p.centerOfMass.x.in(units::mm), WithinAbs(0.0, 1e-9));
        CHECK_THAT(p.centerOfMass.z.in(units::mm), WithinAbs(0.0, 1e-9));
    }
}

// --- Refusals ----------------------------------------------------------------------------------

TEST_CASE("CurvedProfile_MalformedCurvedLoopsAreRefused", "[core][geometry][profile][p12]") {
    const auto message = [](const Result<Body>& body) {
        REQUIRE_FALSE(body.has_value());
        CHECK(body.error().code == ErrorCode::InvalidArgument);
        return body.error().message;
    };
    const EllipseSegment2D ellipse{mm(0, 0), mm(10, 0), 6_mm, true};
    const LineSegment2D line{mm(10, 0), mm(20, 0)};
    CHECK(message(makePrism(regionOf(ProfileLoop{{ellipse, line}}), 0_mm, 1_mm)) ==
          "outer loop mixes a full ellipse with other segments");
    CHECK(message(makePrism(regionOf(loopOf(EllipseSegment2D{mm(0, 0), mm(10, 0), 0_mm, true})), 0_mm, 1_mm)) ==
          "outer loop has an invalid ellipse");
    const SplineSegment2D bad{polygonMm({{0, 0}, {10, 0}, {10, 10}}), 7, true};
    CHECK(message(makePrism(regionOf(loopOf(bad)), 0_mm, 1_mm)) ==
          "outer loop has an invalid spline: a spline's degree must be 2 to 5, got 7");
    const SplineSegment2D periodic{polygonMm({{0, 0}, {10, 0}, {10, 10}}), 2, true};
    const ProfileLoop square{{LineSegment2D{mm(-50, -50), mm(50, -50)}, LineSegment2D{mm(50, -50), mm(50, 50)},
                              LineSegment2D{mm(50, 50), mm(-50, 50)}, LineSegment2D{mm(-50, 50), mm(-50, -50)}}};
    CHECK(message(makePrism(regionOf(square, {ProfileLoop{{periodic, line}}}), 0_mm, 1_mm)) ==
          "hole loop mixes a periodic spline with other segments");
    // An open spline whose poles coincide has no curve.
    const SplineSegment2D point{std::vector<Point2D>(3, mm(0, 0)), 2, false};
    CHECK_THAT(message(makePrism(regionOf(loopOf(point)), 0_mm, 1_mm)),
               ContainsSubstring("outer loop has an invalid spline: a spline's poles all coincide"));
}

TEST_CASE("CurvedProfile_LoftsAndPathsRefuseEllipsesAndSplines", "[core][geometry][profile][p12]") {
    const EllipseSegment2D ellipse{mm(0, 0), mm(10, 0), 6_mm, true};
    const SplineSegment2D spline{polygonMm({{0, 0}, {10, 0}, {10, 10}, {0, 10}}), 2, true};
    const auto raised = Frame3D::create(Point3D{0_mm, 0_mm, 20_mm}, Direction3D::unitZ(), Direction3D::unitX());
    REQUIRE(raised.has_value());
    const PlanarRegion top{.plane = *raised, .outer = loopOf(ellipse), .holes = {}};
    const std::vector<PlanarRegion> sections{regionOf(loopOf(ellipse)), top};
    const auto loft = makeLoft(sections);
    REQUIRE_FALSE(loft.has_value());
    CHECK(loft.error().code == ErrorCode::InvalidArgument);
    CHECK(loft.error().message ==
          "makeLoft: section 1 has an ellipse; loft sections are made of lines, arcs and circles");
    const std::vector<PlanarRegion> splineSections{regionOf(loopOf(spline)), top};
    const auto splineLoft = makeLoft(splineSections);
    REQUIRE_FALSE(splineLoft.has_value());
    CHECK_THAT(splineLoft.error().message, ContainsSubstring("section 1 has a spline"));

    const PlanarRegion circle{.plane = Frame3D::xy(), .outer = loopOf(CircleSegment2D{mm(0, 0), 5_mm, true}), .holes = {}};
    const SplineSegment2D openPath{polygonMm({{0, 0}, {0, 20}, {10, 40}}), 2, false};
    const auto swept = makeSweep(circle, PlanarPath{.plane = Frame3D::xz(), .segments = {openPath}});
    REQUIRE_FALSE(swept.has_value());
    CHECK(swept.error().message == "makeSweep: path segment 1 is a spline; a path is made of lines, arcs and circles");
    const auto around = makeSweep(circle, PlanarPath{.plane = Frame3D::xz(), .segments = {ellipse}});
    REQUIRE_FALSE(around.has_value());
    CHECK(around.error().message ==
          "makeSweep: path segment 1 is an ellipse; a path is made of lines, arcs and circles");
}

TEST_CASE("CurvedProfile_SelfIntersectingSplineIsRefusedByTheKernelCheck", "[core][geometry][profile][p12]") {
    // The control polygon crosses itself, and so does the curve: the piece
    // from (60, 20) to (30, 25) passes under the start of the piece from
    // (30, 20) to (60, 20).
    const SplineSegment2D eight{polygonMm({{0, 0}, {60, 40}, {60, 0}, {0, 50}}), 2, true};
    const auto body = makePrism(regionOf(loopOf(eight)), 0_mm, 5_mm);
    REQUIRE_FALSE(body.has_value());
    CHECK_THAT(body.error().message, ContainsSubstring("the profile face is invalid"));
}
