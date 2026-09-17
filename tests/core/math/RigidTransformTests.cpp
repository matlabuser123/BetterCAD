#include <bettercad/core/Units.hpp>
#include <bettercad/core/math/Direction.hpp>
#include <bettercad/core/math/RigidTransform.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
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

/// An independent reference: p mirrored across the plane through p0 with
/// normal n (any length), p' = p - 2 ((p - p0) . n) n with n made unit.
V reflect(const V& p, const V& p0, const V& nIn) {
    const V n = unit(nIn);
    return sub(p, scale(n, 2.0 * dot(sub(p, p0), n)));
}

/// Signed distance of p from the plane through p0 with normal n.
double signedDistance(const V& p, const V& p0, const V& nIn) {
    return dot(sub(p, p0), unit(nIn));
}

Direction3D direction(const V& v) {
    return *Direction3D::fromComponents(v[0], v[1], v[2]);
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

TEST_CASE("RigidTransform_ReflectionMatchesPointFormula", "[math][transform][mirror]") {
    // p = (4, 1, 3) across the plane through (1, 0, 0) with normal (1, 1, 0)/sqrt 2:
    // (p - p0) . n = 4/sqrt 2, so p' = p - 4 (1, 1, 0) = (0, -3, 3).
    const auto mirror = RigidTransform3D::reflection(point({1, 0, 0}), direction({1, 1, 0}));
    const V image = mm(mirror.apply(point({4, 1, 3})));
    checkClose(image, {0, -3, 3}, 1e-14);
    checkClose(image, reflect({4, 1, 3}, {1, 0, 0}, {1, 1, 0}), 1e-14);
    // The axis planes through the origin: one coordinate changes sign,
    // exactly (q is p as stored).
    const V p{12.5, -4, 7};
    const V q = mm(point(p));
    CHECK(mm(RigidTransform3D::reflection(Point3D{}, Direction3D::unitX()).apply(point(p))) == V{-q[0], q[1], q[2]});
    CHECK(mm(RigidTransform3D::reflection(Point3D{}, Direction3D::unitY()).apply(point(p))) == V{q[0], -q[1], q[2]});
    CHECK(mm(RigidTransform3D::reflection(Point3D{}, Direction3D::unitZ()).apply(point(p))) == V{q[0], q[1], -q[2]});
    // An offset plane: x = 10 takes x = 30 to 2 x 10 - 30 = -10.
    checkClose(mm(RigidTransform3D::reflection(point({10, 0, 0}), Direction3D::unitX()).apply(point({30, 2, 3}))),
               {-10, 2, 3}, 1e-12);
    // Arbitrary planes and points against the formula; directions mirror as
    // vectors, v' = v - 2 (v . n) n, whatever the plane's position.
    const V normals[] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 1, 0}, {1, 1, 1}, {1, -2, 3}, {-0.3, 0.1, 0.9}};
    const V origins[] = {{0, 0, 0}, {12.5, -4, 30}};
    const V points[] = {{40, 10, 20}, {-3, 0.5, 11}, {0, 0, 0}};
    for (const V& n : normals) {
        for (const V& o : origins) {
            CAPTURE(n[0], n[1], n[2], o[0]);
            const auto reflection = RigidTransform3D::reflection(point(o), direction(n));
            CHECK(reflection.reversesOrientation());
            CHECK_FALSE(reflection.isTranslation());
            for (const V& r : points) {
                checkClose(mm(reflection.apply(point(r))), reflect(r, o, n), 1e-12);
            }
            const V d = unit({1, 2, 2});
            const Direction3D mirrored = reflection.apply(direction(d));
            checkClose({mirrored.x(), mirrored.y(), mirrored.z()}, reflect(d, {0, 0, 0}, n), 1e-15);
        }
    }
    // Rotations and translations keep orientation.
    CHECK_FALSE(RigidTransform3D::rotation(axis({0, 0, 0}, {1, 1, 1}), 137_deg).reversesOrientation());
    CHECK_FALSE(RigidTransform3D::translation({1_mm, 2_mm, 3_mm}).reversesOrientation());
    CHECK_FALSE(RigidTransform3D{}.reversesOrientation());
}

TEST_CASE("RigidTransform_ReflectionIsAnInvolution", "[math][transform][mirror]") {
    // M(M(p)) = p, and the matrix is its own inverse: A A = I, det A = -1.
    const V n{1, -2, 3};
    const V o{12.5, -4, 30};
    const auto mirror = RigidTransform3D::reflection(point(o), direction(n));
    const auto& a = mirror.matrix();
    double worstIdentity = 0.0;
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
            double sum = 0.0;
            for (std::size_t k = 0; k < 3; ++k) {
                sum += a[3 * i + k] * a[3 * k + j];
            }
            worstIdentity = std::max(worstIdentity, std::abs(sum - (i == j ? 1.0 : 0.0)));
        }
    }
    const double det = a[0] * (a[4] * a[8] - a[5] * a[7]) - a[1] * (a[3] * a[8] - a[5] * a[6]) +
                       a[2] * (a[3] * a[7] - a[4] * a[6]);
    INFO("worst entry of A A - I: " << worstIdentity << ", det A = " << det);
    CHECK(worstIdentity < 1e-15);
    CHECK_THAT(det, WithinAbs(-1.0, 1e-15));
    double worst = 0.0;
    for (const V& p : {V{40, 10, 20}, V{-3, 0.5, 11}, V{0, 0, 0}, V{1e3, -2e3, 5e2}}) {
        const V twice = mm(mirror.apply(mirror.apply(point(p))));
        worst = std::max(worst, std::sqrt(dot(sub(twice, p), sub(twice, p))));
    }
    INFO("worst |M(M(p)) - p|: " << worst << " mm");
    CHECK(worst < 1e-11);
}

TEST_CASE("RigidTransform_ReflectionNegatesSignedDistanceToThePlane", "[math][transform][mirror]") {
    // A mirror image is as far from the plane as the point, on the other
    // side; points on the plane stay put.
    const V n{1, 1, 1};
    const V o{5, -2, 1};
    const auto mirror = RigidTransform3D::reflection(point(o), direction(n));
    double worst = 0.0;
    for (const V& p : {V{40, 10, 20}, V{-3, 0.5, 11}, V{0, 0, 0}, V{7, 7, -30}}) {
        const V image = mm(mirror.apply(point(p)));
        worst = std::max(worst, std::abs(signedDistance(image, o, n) + signedDistance(p, o, n)));
        // The segment from p to its image is perpendicular to the plane.
        const V chord = sub(image, p);
        const V across = cross(chord, unit(n));
        worst = std::max(worst, std::sqrt(dot(across, across)));
    }
    INFO("worst distance or direction error: " << worst << " mm");
    CHECK(worst < 1e-12);
    const V onPlane = add(o, V{3, -2, -1}); // (3, -2, -1) . (1, 1, 1) = 0
    checkClose(mm(mirror.apply(point(onPlane))), onPlane, 1e-13);
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

TEST_CASE("RigidTransform_AfterAppliesTheInnerMotionFirst", "[math][transform][p12]") {
    // P12-PATTERN-001: a pattern of a pattern moves an instance of the inner
    // pattern by an instance of the outer one. after() must be exactly the
    // two motions in that order, for every point and direction.
    const std::array<RigidTransform3D, 5> motions{
        RigidTransform3D{},
        RigidTransform3D::translation(Translation3D{20_mm, -5_mm, 3_mm}),
        RigidTransform3D::rotation(axis({0, 0, 0}, {0, 0, 1}), 90_deg),
        RigidTransform3D::rotation(axis({60, 25, 0}, {1, 2, 3}), 37_deg),
        RigidTransform3D::reflection(point({10, 0, 0}), Direction3D::unitX()),
    };
    const V points[] = {{0, 0, 0}, {12, -3, 7.5}, {-40, 55, 2}};
    for (const RigidTransform3D& outer : motions) {
        for (const RigidTransform3D& inner : motions) {
            const RigidTransform3D combined = outer.after(inner);
            for (const V& p : points) {
                CAPTURE(p[0], p[1], p[2]);
                // The reference: apply the inner motion, then the outer one.
                checkClose(mm(combined.apply(point(p))), mm(outer.apply(inner.apply(point(p)))), 1e-9);
            }
            for (const Direction3D& d : {Direction3D::unitX(), Direction3D::unitY(), Direction3D::unitZ()}) {
                const Direction3D expected = outer.apply(inner.apply(d));
                const Direction3D actual = combined.apply(d);
                CHECK_THAT(actual.x(), WithinAbs(expected.x(), 1e-12));
                CHECK_THAT(actual.y(), WithinAbs(expected.y(), 1e-12));
                CHECK_THAT(actual.z(), WithinAbs(expected.z(), 1e-12));
            }
            // Composing two rigid motions is rigid: a reflection either side
            // reverses orientation, both or neither keeps it.
            CHECK(combined.reversesOrientation() ==
                  (outer.reversesOrientation() != inner.reversesOrientation()));
        }
    }
    // The order matters, and after() takes the inner motion as its argument.
    const auto turn = RigidTransform3D::rotation(axis({0, 0, 0}, {0, 0, 1}), 90_deg);
    const auto move = RigidTransform3D::translation(Translation3D{10_mm, 0_mm, 0_mm});
    checkClose(mm(turn.after(move).apply(point({0, 0, 0}))), {0, 10, 0}, 1e-12);
    checkClose(mm(move.after(turn).apply(point({0, 0, 0}))), {10, 0, 0}, 1e-12);
    // The identity on either side changes nothing.
    checkClose(mm(turn.after(RigidTransform3D{}).apply(point({50, 0, 0}))), {0, 50, 0}, 1e-12);
    checkClose(mm(RigidTransform3D{}.after(turn).apply(point({50, 0, 0}))), {0, 50, 0}, 1e-12);
}
