#include "TestHelpers.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/math/BSpline.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <cmath>
#include <limits>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using bettercad::test::errorCode;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// P12-SKETCH-002: the B-spline curve of sketch splines. Every value is
// compared with formulas written out here: Bernstein polynomials for
// clamped splines with degree + 1 poles (Bézier curves), and the textbook
// matrix of the uniform cubic B-spline for periodic ones.

namespace {

// Metres; coordinates are tens of millimetres, so 1e-15 m is about 1e-13
// relative: rounding of a few dozen operations.
constexpr double kTolM = 1e-15;

std::vector<Point2D> points(std::initializer_list<std::array<double, 2>> mm) {
    std::vector<Point2D> result;
    for (const auto& p : mm) {
        result.push_back(Point2D{p[0] * units::mm, p[1] * units::mm});
    }
    return result;
}

UniformBSpline requireSpline(const std::vector<Point2D>& poles, int degree, bool periodic) {
    auto spline = UniformBSpline::create(poles, degree, periodic);
    if (!spline) {
        FAIL(spline.error().message);
    }
    return *spline;
}

double binomial(int n, int k) {
    double result = 1.0;
    for (int i = 1; i <= k; ++i) {
        result = result * (n - k + i) / i;
    }
    return result;
}

/// Bézier curve and derivative at t in [0, 1], in metres.
std::array<double, 4> bezier(const std::vector<Point2D>& poles, double t) {
    const int d = static_cast<int>(poles.size()) - 1;
    std::array<double, 4> r{};
    for (int i = 0; i <= d; ++i) {
        const double b = binomial(d, i) * std::pow(t, i) * std::pow(1.0 - t, d - i);
        r[0] += b * poles[static_cast<std::size_t>(i)].x.si();
        r[1] += b * poles[static_cast<std::size_t>(i)].y.si();
    }
    for (int i = 0; i < d; ++i) {
        const double b = d * binomial(d - 1, i) * std::pow(t, i) * std::pow(1.0 - t, d - 1 - i);
        r[2] += b * (poles[static_cast<std::size_t>(i) + 1].x - poles[static_cast<std::size_t>(i)].x).si();
        r[3] += b * (poles[static_cast<std::size_t>(i) + 1].y - poles[static_cast<std::size_t>(i)].y).si();
    }
    return r;
}

/// Uniform cubic B-spline span on four poles at t in [0, 1]:
/// (1/6) [(1-t)^3, 3t^3 - 6t^2 + 4, -3t^3 + 3t^2 + 3t + 1, t^3].
std::array<double, 2> cubicSpan(const std::array<Point2D, 4>& p, double t) {
    const std::array<double, 4> b{(1.0 - t) * (1.0 - t) * (1.0 - t) / 6.0,
                                  (3.0 * t * t * t - 6.0 * t * t + 4.0) / 6.0,
                                  (-3.0 * t * t * t + 3.0 * t * t + 3.0 * t + 1.0) / 6.0, t * t * t / 6.0};
    std::array<double, 2> r{};
    for (std::size_t i = 0; i < 4; ++i) {
        r[0] += b[i] * p[i].x.si();
        r[1] += b[i] * p[i].y.si();
    }
    return r;
}

} // namespace

TEST_CASE("BSpline_ClampedSplineWithDegreePlusOnePolesIsABezierCurve", "[core][math][bspline][p12]") {
    const std::vector<Point2D> poles = points({{0, 0}, {10, 40}, {35, -20}, {60, 30}, {80, 5}, {90, 50}});
    for (int degree = 2; degree <= 5; ++degree) {
        CAPTURE(degree);
        const std::vector<Point2D> used(poles.begin(), poles.begin() + degree + 1);
        const UniformBSpline spline = requireSpline(used, degree, false);
        CHECK(spline.first() == 0.0);
        CHECK(spline.last() == 1.0);
        for (const double t : {0.0, 0.1, 0.25, 0.5, 0.7, 0.9, 1.0}) {
            CAPTURE(t);
            const auto expected = bezier(used, t);
            const UniformBSpline::Sample s = spline.evaluate(t);
            CHECK_THAT(s.x, WithinAbs(expected[0], kTolM));
            CHECK_THAT(s.y, WithinAbs(expected[1], kTolM));
            CHECK_THAT(s.dx, WithinAbs(expected[2], 1e-13));
            CHECK_THAT(s.dy, WithinAbs(expected[3], 1e-13));
        }
    }
}

TEST_CASE("BSpline_PeriodicCubicMatchesTheUniformBasisMatrix", "[core][math][bspline][p12]") {
    const std::vector<Point2D> poles = points({{0, 0}, {50, -10}, {60, 40}, {20, 55}, {-15, 30}});
    const UniformBSpline spline = requireSpline(poles, 3, true);
    CHECK(spline.first() == 3.0);
    CHECK(spline.last() == 8.0);
    // Span i runs over poles i, i+1, i+2, i+3 (cyclically).
    for (std::size_t i = 0; i < poles.size(); ++i) {
        const std::array<Point2D, 4> span{poles[i], poles[(i + 1) % 5], poles[(i + 2) % 5], poles[(i + 3) % 5]};
        for (const double t : {0.0, 0.2, 0.5, 0.8}) {
            CAPTURE(i, t);
            const auto expected = cubicSpan(span, t);
            const UniformBSpline::Sample s = spline.evaluate(3.0 + static_cast<double>(i) + t);
            CHECK_THAT(s.x, WithinAbs(expected[0], kTolM));
            CHECK_THAT(s.y, WithinAbs(expected[1], kTolM));
        }
    }
    // Closed: the end of the domain is its start.
    CHECK_THAT(spline.evaluate(8.0).x, WithinAbs(spline.evaluate(3.0).x, kTolM));
    CHECK_THAT(spline.evaluate(8.0).y, WithinAbs(spline.evaluate(3.0).y, kTolM));
}

TEST_CASE("BSpline_ClampedSplineStartsAndEndsAtItsEndPoles", "[core][math][bspline][p12]") {
    const std::vector<Point2D> poles = points({{0, 0}, {10, 40}, {35, -20}, {60, 30}, {80, 5}, {90, 50}, {120, 0}});
    for (int degree = 2; degree <= 5; ++degree) {
        CAPTURE(degree);
        const UniformBSpline spline = requireSpline(poles, degree, false);
        CHECK(spline.last() == static_cast<double>(poles.size()) - degree);
        const auto start = spline.evaluate(spline.first());
        const auto end = spline.evaluate(spline.last());
        CHECK_THAT(start.x, WithinAbs(0.0, kTolM));
        CHECK_THAT(start.y, WithinAbs(0.0, kTolM));
        CHECK_THAT(end.x, WithinAbs(0.120, kTolM));
        CHECK_THAT(end.y, WithinAbs(0.0, kTolM));
        // The end tangents are degree * (P1 - P0) and degree * (Pn - Pn-1)
        // for spans of unit length.
        CHECK_THAT(start.dx, WithinAbs(degree * 0.010, 1e-13));
        CHECK_THAT(start.dy, WithinAbs(degree * 0.040, 1e-13));
        CHECK_THAT(end.dx, WithinAbs(degree * 0.030, 1e-13));
        CHECK_THAT(end.dy, WithinAbs(degree * -0.050, 1e-13));
        // Outside the domain the parameter is clamped.
        CHECK(spline.evaluate(-1.0).x == start.x);
        CHECK(spline.evaluate(spline.last() + 1.0).y == end.y);
    }
}

TEST_CASE("BSpline_BezierPiecesReproduceTheCurve", "[core][math][bspline][p12]") {
    const std::vector<Point2D> poles = points({{0, 0}, {10, 40}, {35, -20}, {60, 30}, {80, 5}, {90, 50}, {120, 0}});
    for (int degree = 2; degree <= 5; ++degree) {
        for (const bool periodic : {false, true}) {
            CAPTURE(degree, periodic);
            const UniformBSpline spline = requireSpline(poles, degree, periodic);
            const auto pieces = spline.bezierPieces();
            REQUIRE(pieces.size() == static_cast<std::size_t>(spline.last() - spline.first()));
            for (std::size_t k = 0; k < pieces.size(); ++k) {
                REQUIRE(pieces[k].size() == static_cast<std::size_t>(degree) + 1);
                std::vector<Point2D> control;
                for (const auto& q : pieces[k]) {
                    control.push_back(Point2D{Length::fromSi(q[0]), Length::fromSi(q[1])});
                }
                for (const double t : {0.0, 0.3, 0.5, 0.9, 1.0}) {
                    CAPTURE(k, t);
                    const auto expected = spline.evaluate(spline.first() + static_cast<double>(k) + t);
                    const auto actual = bezier(control, t);
                    CHECK_THAT(actual[0], WithinAbs(expected.x, kTolM));
                    CHECK_THAT(actual[1], WithinAbs(expected.y, kTolM));
                }
            }
        }
    }
}

TEST_CASE("BSpline_KnotsInKernelFormHaveTheRightMultiplicities", "[core][math][bspline][p12]") {
    const std::vector<Point2D> poles = points({{0, 0}, {10, 40}, {35, -20}, {60, 30}, {80, 5}, {90, 50}});
    const UniformBSpline open = requireSpline(poles, 3, false);
    CHECK(open.knotValues() == std::vector<double>{0.0, 1.0, 2.0, 3.0});
    CHECK(open.knotMultiplicities() == std::vector<int>{4, 1, 1, 4});
    const UniformBSpline periodic = requireSpline(poles, 3, true);
    CHECK(periodic.knotValues() == std::vector<double>{0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0});
    CHECK(periodic.knotMultiplicities() == std::vector<int>{1, 1, 1, 1, 1, 1, 1});
}

TEST_CASE("BSpline_InvalidSplinesAreRefused", "[core][math][bspline][p12]") {
    const std::vector<Point2D> poles = points({{0, 0}, {10, 40}, {35, -20}, {60, 30}});
    const auto message = [](const Result<UniformBSpline>& result) {
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == ErrorCode::InvalidArgument);
        return result.error().message;
    };
    CHECK(message(UniformBSpline::create(poles, 1, false)) == "a spline's degree must be 2 to 5, got 1");
    CHECK(message(UniformBSpline::create(poles, 6, false)) == "a spline's degree must be 2 to 5, got 6");
    CHECK(message(UniformBSpline::create(poles, 4, true)) == "a spline of degree 4 needs at least 5 poles, got 4");
    std::vector<Point2D> infinite = poles;
    infinite[2].y = Length::fromSi(std::numeric_limits<double>::infinity());
    CHECK(message(UniformBSpline::create(infinite, 3, false)) == "pole 3 of a spline is not finite");
    const std::vector<Point2D> same(3, Point2D{5_mm, 5_mm});
    CHECK(message(UniformBSpline::create(same, 2, true)) == "a spline's poles all coincide");
    CHECK(errorCode(UniformBSpline::checkStructure(2, 2)) == ErrorCode::InvalidArgument);
    CHECK(UniformBSpline::checkStructure(3, 2).has_value());
}

TEST_CASE("BSpline_GaussRuleIntegratesPolynomialsUpToDegree15", "[core][math][bspline][p12]") {
    const GaussLegendreRule& rule = gaussLegendreRule();
    for (int k = 0; k <= 15; ++k) {
        CAPTURE(k);
        double sum = 0.0;
        for (std::size_t i = 0; i < GaussLegendreRule::kPoints; ++i) {
            CHECK(rule.nodes[i] > 0.0);
            CHECK(rule.nodes[i] < 1.0);
            sum += rule.weights[i] * std::pow(rule.nodes[i], k);
        }
        // integral of t^k over [0, 1]
        CHECK_THAT(sum, WithinRel(1.0 / (k + 1), 1e-14));
    }
}
