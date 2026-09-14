#include <bettercad/core/Units.hpp>
#include <bettercad/core/math/Direction.hpp>
#include <bettercad/core/math/RigidTransform.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <cmath>
#include <numbers>

using namespace bettercad;
using namespace bettercad::literals;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

constexpr double pi = std::numbers::pi;

// An independent reference: Rodrigues' rotation of p (mm) about the unit axis
// k through o by t (rad), written as p_par + cos(t) p_perp + sin(t) k x p_perp
// relative to o. No BetterCAD math is used.
using V = std::array<double, 3>;

V sub(const V& a, const V& b) {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}
V add(const V& a, const V& b) {
    return {a[0] + b[0], a[1] + b[1], a[2] + b[2]};
}
V scale(const V& a, double s) {
    return {a[0] * s, a[1] * s, a[2] * s};
}
double dot(const V& a, const V& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
V cross(const V& a, const V& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
V unit(const V& a) {
    return scale(a, 1.0 / std::sqrt(dot(a, a)));
}

V rodrigues(const V& p, const V& o, const V& kIn, double t) {
    const V k = unit(kIn);
    const V r = sub(p, o);
    const V parallel = scale(k, dot(r, k));
    const V perpendicular = sub(r, parallel);
    return add(o, add(parallel, add(scale(perpendicular, std::cos(t)), scale(cross(k, perpendicular), std::sin(t)))));
}

/// Distance of p from the line through o along k.
double distanceToAxis(const V& p, const V& o, const V& kIn) {
    const V k = unit(kIn);
    const V r = sub(p, o);
    const V perpendicular = sub(r, scale(k, dot(r, k)));
    return std::sqrt(dot(perpendicular, perpendicular));
}

V mm(const Point3D& p) {
    return {p.x.in(units::mm), p.y.in(units::mm), p.z.in(units::mm)};
}
Point3D point(const V& v) {
    return {v[0] * units::mm, v[1] * units::mm, v[2] * units::mm};
}
Axis3D axis(const V& o, const V& k) {
    return {point(o), *Direction3D::fromComponents(k[0], k[1], k[2])};
}

void checkClose(const V& actual, const V& expected, double toleranceMm) {
    CHECK_THAT(actual[0], WithinAbs(expected[0], toleranceMm));
    CHECK_THAT(actual[1], WithinAbs(expected[1], toleranceMm));
    CHECK_THAT(actual[2], WithinAbs(expected[2], toleranceMm));
}

} // namespace

TEST_CASE("RigidTransform_IdentityAndTranslation", "[math][transform]") {
    const Point3D p{12_mm, -3_mm, 7.5_mm};
    CHECK(RigidTransform3D{}.apply(p) == p);
    CHECK(RigidTransform3D{}.isTranslation());
    const Translation3D offset{20_mm, 0_mm, -5_mm};
    const auto move = RigidTransform3D::translation(offset);
    CHECK(move.isTranslation());
    CHECK(move.apply(p) == p + offset); // bit for bit the translation
    checkClose(mm(move.apply(p)), {32, -3, 2.5}, 1e-12);
    CHECK(move.apply(Direction3D::unitX()) == Direction3D::unitX()); // directions do not translate
    CHECK_FALSE(RigidTransform3D::rotation(Axis3D{}, 1_deg).isTranslation());
    // A zero rotation is the identity, exactly.
    CHECK(RigidTransform3D::rotation(Axis3D{Point3D{3_mm, 4_mm, 5_mm}, Direction3D::unitZ()}, 0_deg).isTranslation());
}

TEST_CASE("RigidTransform_QuadrantRotationsAboutZ", "[math][transform]") {
    // (50, 0, 0) turned about Z through the origin: counter-clockwise from +Z.
    const V p{50, 0, 0};
    const V expected[] = {{50, 0, 0}, {0, 50, 0}, {-50, 0, 0}, {0, -50, 0}};
    for (int i = 0; i < 4; ++i) {
        CAPTURE(i);
        const auto turn = RigidTransform3D::rotation(axis({0, 0, 0}, {0, 0, 1}), 90_deg * static_cast<double>(i));
        checkClose(mm(turn.apply(point(p))), expected[i], 1e-12);
    }
    // The other way round.
    checkClose(mm(RigidTransform3D::rotation(axis({0, 0, 0}, {0, 0, 1}), -(90_deg)).apply(point(p))), {0, -50, 0},
               1e-12);
    // About a displaced axis, the axis' points stay put.
    const auto turn = RigidTransform3D::rotation(axis({60, 25, 0}, {0, 0, 1}), 90_deg);
    checkClose(mm(turn.apply(point({60, 25, 7}))), {60, 25, 7}, 1e-12);
    checkClose(mm(turn.apply(point({80, 25, 0}))), {60, 45, 0}, 1e-12);
}

TEST_CASE("RigidTransform_RotationMatchesRodrigues", "[math][transform]") {
    const V axes[] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 1, 1}, {1, -2, 3}, {-0.3, 0.1, 0.9}};
    const V origins[] = {{0, 0, 0}, {12.5, -4, 30}};
    const V p{40, 10, 20};
    for (const V& k : axes) {
        for (const V& o : origins) {
            for (const double degrees : {0.0, 1.0, 30.0, 90.0, 137.0, 180.0, 270.0, 359.0, -45.0}) {
                CAPTURE(k[0], k[1], k[2], o[0], degrees);
                const auto turn = RigidTransform3D::rotation(axis(o, k), degrees * units::deg);
                const V expected = rodrigues(p, o, k, degrees * pi / 180.0);
                checkClose(mm(turn.apply(point(p))), expected, 1e-11);
                // Directions turn as vectors (no translation).
                const V d = unit({1, 2, 2});
                const V expectedDirection = sub(rodrigues(d, {0, 0, 0}, k, degrees * pi / 180.0), {0, 0, 0});
                const Direction3D turned = turn.apply(*Direction3D::fromComponents(d[0], d[1], d[2]));
                checkClose({turned.x(), turned.y(), turned.z()}, expectedDirection, 1e-14);
            }
        }
    }
}

TEST_CASE("RigidTransform_RotationPreservesDistanceToAxis", "[math][transform]") {
    // A full circle of 360 one-degree steps about (1, 1, 1), each rotation
    // computed from the source: the radius never changes and each instance
    // is where Rodrigues puts it.
    const V k{1, 1, 1};
    const V o{5, -2, 1};
    const V p{40, 10, 20};
    const double radius = distanceToAxis(p, o, k);
    double worstRadius = 0.0;
    double worstPosition = 0.0;
    for (int i = 0; i < 360; ++i) {
        const Angle angle = 1_deg * static_cast<double>(i);
        const V turned = mm(RigidTransform3D::rotation(axis(o, k), angle).apply(point(p)));
        worstRadius = std::max(worstRadius, std::abs(distanceToAxis(turned, o, k) - radius));
        const V expected = rodrigues(p, o, k, angle.si());
        worstPosition = std::max(worstPosition, std::sqrt(dot(sub(turned, expected), sub(turned, expected))));
    }
    // Coordinates of ~40 mm carry ~1e-14 mm of rounding; nothing accumulates.
    INFO("worst radius error " << worstRadius << " mm, worst position error " << worstPosition << " mm");
    CHECK(worstRadius < 1e-12);
    CHECK(worstPosition < 1e-11);
}
