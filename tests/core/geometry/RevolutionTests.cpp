#include "GeometryTestSupport.hpp"
#include "TestHelpers.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/geometry/Profile.hpp>
#include <bettercad/core/geometry/Sweeps.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cmath>
#include <initializer_list>
#include <limits>
#include <numbers>
#include <utility>
#include <vector>

using namespace bettercad;
using namespace bettercad::geometry;
using namespace bettercad::literals;
using bettercad::test::errorCode;
using bettercad::test::kPositionToleranceMm;
using bettercad::test::kRelTight;
using bettercad::test::requireProperties;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

constexpr double pi = std::numbers::pi;

// Profiles are drawn in the XZ plane (local u = +X, local v = +Z) and
// revolved about the global Z axis, which lies in that plane: local
// coordinates are (radius, height).
const Axis3D kZAxis{Point3D{}, Direction3D::unitZ()};

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

PlanarRegion region(ProfileLoop outer, const Frame3D& plane = Frame3D::xz()) {
    return PlanarRegion{.plane = plane, .outer = std::move(outer), .holes = {}};
}

/// Rectangle [r0, r1] x [z0, z1] in (radius, height).
PlanarRegion rectangle(double r0, double r1, double z0, double z1) {
    return region(polygon({{r0, z0}, {r1, z0}, {r1, z1}, {r0, z1}}));
}

Body requireRevolution(const PlanarRegion& profile, Angle from, Angle to, const Axis3D& axis = kZAxis) {
    auto body = makeRevolution(profile, axis, from, to);
    if (!body) {
        FAIL(body.error().message);
    }
    CHECK(body->isValid());
    CHECK(body->topology().solids == 1);
    return *body;
}

Body fullTurn(const PlanarRegion& profile) {
    return requireRevolution(profile, 0_deg, 360_deg);
}

} // namespace

TEST_CASE("Full revolutions match analytic solids of revolution", "[geometry][revolve]") {
    SECTION("cylinder: a rectangle with one edge on the axis") {
        const auto props = requireProperties(fullTurn(rectangle(0, 10, 0, 20)));
        CHECK_THAT(props.volume.in(units::mm3), WithinRel(pi * 10 * 10 * 20, kRelTight));
        CHECK_THAT(props.surfaceArea.in(units::mm2), WithinRel(2 * pi * 10 * 20 + 2 * pi * 10 * 10, kRelTight));
    }
    SECTION("tube: a rectangle away from the axis") {
        // The P11-FEAT-001 reference case: r1 = 10, r2 = 20, h = 30 mm,
        // V = pi (r2^2 - r1^2) h.
        const Body tube = fullTurn(rectangle(10, 20, 0, 30));
        const auto props = requireProperties(tube);
        CHECK(std::isfinite(props.volume.si()));
        CHECK_THAT(props.volume.in(units::mm3), WithinRel(pi * (20 * 20 - 10 * 10) * 30, kRelTight));
        CHECK_THAT(props.surfaceArea.in(units::mm2),
                   WithinRel(2 * pi * 20 * 30 + 2 * pi * 10 * 30 + 2 * pi * (20 * 20 - 10 * 10), kRelTight));
        const auto box = tube.boundingBox().value();
        test::checkPoint(box.min, -20, -20, 0);
        test::checkPoint(box.max, 20, 20, 30);
        test::checkPoint(props.centerOfMass, 0, 0, 15);
    }
    SECTION("cone: a right triangle with a leg on the axis") {
        const auto props = requireProperties(fullTurn(region(polygon({{0, 0}, {10, 0}, {0, 20}}))));
        CHECK_THAT(props.volume.in(units::mm3), WithinRel(pi * 10 * 10 * 20 / 3.0, kRelTight));
        CHECK_THAT(props.surfaceArea.in(units::mm2),
                   WithinRel(pi * 10 * std::sqrt(10.0 * 10 + 20 * 20) + pi * 10 * 10, kRelTight));
    }
    SECTION("sphere: a half disc whose diameter is on the axis") {
        ProfileLoop half;
        half.segments.emplace_back(ArcSegment2D{mm(0, 0), mm(0, -10), mm(0, 10), /*counterClockwise=*/true});
        half.segments.emplace_back(LineSegment2D{mm(0, 10), mm(0, -10)});
        const auto props = requireProperties(fullTurn(region(half)));
        CHECK_THAT(props.volume.in(units::mm3), WithinRel(4.0 / 3.0 * pi * 1000, kRelTight));
        CHECK_THAT(props.surfaceArea.in(units::mm2), WithinRel(4 * pi * 100, kRelTight));
    }
    SECTION("torus: a circle away from the axis") {
        ProfileLoop circle;
        circle.segments.emplace_back(CircleSegment2D{mm(20, 0), 5_mm, true});
        const auto props = requireProperties(fullTurn(region(circle)));
        CHECK_THAT(props.volume.in(units::mm3), WithinRel(2 * pi * pi * 20 * 5 * 5, kRelTight));
        CHECK_THAT(props.surfaceArea.in(units::mm2), WithinRel(4 * pi * pi * 20 * 5, kRelTight));
    }
}

TEST_CASE("Partial revolutions scale with the sweep angle (Pappus)", "[geometry][revolve]") {
    const double degrees = GENERATE(1.0, 45.0, 90.0, 180.0, 270.0, 359.0);
    CAPTURE(degrees);
    const double fraction = degrees / 360.0;
    const Angle sweep = degrees * units::deg;

    // Tube wedge: volume and curved area scale with the angle; the two end
    // faces are copies of the 10 x 30 profile.
    const auto tube = requireProperties(requireRevolution(rectangle(10, 20, 0, 30), 0_deg, sweep));
    CHECK_THAT(tube.volume.in(units::mm3), WithinRel(fraction * pi * (400 - 100) * 30, kRelTight));
    CHECK_THAT(tube.surfaceArea.in(units::mm2),
               WithinRel(fraction * (2 * pi * 20 * 30 + 2 * pi * 10 * 30 + 2 * pi * (400 - 100)) + 2 * 10 * 30,
                         kRelTight));

    ProfileLoop circle;
    circle.segments.emplace_back(CircleSegment2D{mm(20, 0), 5_mm, true});
    const auto torus = requireProperties(requireRevolution(region(circle), 0_deg, sweep));
    CHECK_THAT(torus.volume.in(units::mm3), WithinRel(fraction * 2 * pi * pi * 20 * 25, kRelTight));
}

TEST_CASE("The sweep sense follows the right-hand rule about the axis", "[geometry][revolve]") {
    // The profile lies at +X; a positive quarter turn about +Z carries it to +Y.
    const PlanarRegion profile = rectangle(10, 20, 0, 30);
    const Body positive = requireRevolution(profile, 0_deg, 90_deg);
    const Body negative = requireRevolution(profile, -(90_deg), 0_deg);
    const Body symmetric = requireRevolution(profile, -(45_deg), 45_deg);

    const auto p = requireProperties(positive);
    const auto n = requireProperties(negative);
    const auto s = requireProperties(symmetric);
    CHECK_THAT(n.volume.in(units::mm3), WithinRel(p.volume.in(units::mm3), kRelTight));
    CHECK_THAT(s.volume.in(units::mm3), WithinRel(p.volume.in(units::mm3), kRelTight));
    CHECK(p.centerOfMass.y > 1_mm);
    CHECK(n.centerOfMass.y < -(1_mm));
    CHECK_THAT(s.centerOfMass.y.in(units::mm), WithinAbs(0.0, kPositionToleranceMm));
    // Mirror images: the positive and negative wedges have opposite centroids in y.
    CHECK_THAT(n.centerOfMass.y.in(units::mm), WithinAbs(-p.centerOfMass.y.in(units::mm), kPositionToleranceMm));

    const auto box = positive.boundingBox().value();
    CHECK_THAT(box.min.x.in(units::mm), WithinAbs(0.0, 1e-9));
    CHECK_THAT(box.max.x.in(units::mm), WithinAbs(20.0, 1e-9));
    CHECK_THAT(box.min.y.in(units::mm), WithinAbs(0.0, 1e-9));
    CHECK_THAT(box.max.y.in(units::mm), WithinAbs(20.0, 1e-9));
    const auto negativeBox = negative.boundingBox().value();
    CHECK_THAT(negativeBox.min.y.in(units::mm), WithinAbs(-20.0, 1e-9));
    CHECK_THAT(negativeBox.max.y.in(units::mm), WithinAbs(0.0, 1e-9));
}

TEST_CASE("Revolution works in any plane containing the axis", "[geometry][revolve]") {
    // A tilted plane through (100, 50, -30) mm; the axis is its local Y axis.
    const auto normal = Direction3D::fromComponents(1.0, 2.0, 3.0);
    REQUIRE(normal.has_value());
    const auto plane = Frame3D::create(Point3D{100_mm, 50_mm, -(30_mm)}, *normal, Direction3D::unitZ());
    REQUIRE(plane.has_value());
    const Axis3D axis{plane->origin(), plane->yAxis()};
    const PlanarRegion profile = region(polygon({{5, 0}, {10, 0}, {10, 20}, {5, 20}}), *plane);

    const auto props = requireProperties(requireRevolution(profile, 0_deg, 360_deg, axis));
    CHECK_THAT(props.volume.in(units::mm3), WithinRel(pi * (100 - 25) * 20, kRelTight));
    // The centroid is on the axis.
    const Point3D c = props.centerOfMass;
    const double t = ((c.x - axis.origin.x).in(units::mm) * axis.direction.x() +
                      (c.y - axis.origin.y).in(units::mm) * axis.direction.y() +
                      (c.z - axis.origin.z).in(units::mm) * axis.direction.z());
    CHECK_THAT(t, WithinRel(10.0, 1e-9)); // halfway up the 20 mm profile
    CHECK_THAT((c.x - axis.origin.x).in(units::mm), WithinAbs(t * axis.direction.x(), 1e-9));
    CHECK_THAT((c.y - axis.origin.y).in(units::mm), WithinAbs(t * axis.direction.y(), 1e-9));
    CHECK_THAT((c.z - axis.origin.z).in(units::mm), WithinAbs(t * axis.direction.z(), 1e-9));
}

TEST_CASE("Profiles that cross the axis are rejected", "[geometry][revolve]") {
    const auto crossing = [](const PlanarRegion& profile) {
        const auto body = makeRevolution(profile, kZAxis, 0_deg, 360_deg);
        REQUIRE_FALSE(body.has_value());
        CHECK(body.error().code == ErrorCode::InvalidArgument);
        CHECK_THAT(body.error().message, ContainsSubstring("crosses the revolution axis"));
    };
    SECTION("a polygon straddling the axis") {
        crossing(rectangle(-5, 5, 0, 10));
    }
    SECTION("a circle around the axis") {
        ProfileLoop circle;
        circle.segments.emplace_back(CircleSegment2D{mm(3, 0), 5_mm, true});
        crossing(region(circle));
    }
    SECTION("an arc that bulges across the axis while its ends do not") {
        // D-shape: the line x = 2 and a clockwise arc through (-2, 0).
        ProfileLoop d;
        d.segments.emplace_back(LineSegment2D{mm(2, -4), mm(2, 4)});
        d.segments.emplace_back(ArcSegment2D{mm(2, 0), mm(2, 4), mm(2, -4), /*counterClockwise=*/true});
        crossing(region(d));
    }
    SECTION("a hole is irrelevant as long as the outer loop is on one side") {
        PlanarRegion ring = rectangle(10, 30, 0, 20);
        ring.holes.push_back(polygon({{15, 5}, {25, 5}, {25, 15}, {15, 15}}));
        const auto props = requireProperties(fullTurn(ring));
        CHECK_THAT(props.volume.in(units::mm3),
                   WithinRel(pi * (900 - 100) * 20 - pi * (625 - 225) * 10, kRelTight));
    }
}

TEST_CASE("Self-intersecting profiles are rejected, not revolved", "[geometry][revolve]") {
    // A bow tie away from the axis: its two long edges cross at (15, 5).
    const auto body = makeRevolution(region(polygon({{10, 0}, {20, 10}, {20, 0}, {10, 10}})), kZAxis, 0_deg, 360_deg);
    REQUIRE_FALSE(body.has_value());
    CHECK(body.error().code == ErrorCode::Internal);
    CHECK_THAT(body.error().message, ContainsSubstring("makeRevolution: the profile face is invalid"));
}

TEST_CASE("Revolution axes must be finite, with a real direction", "[geometry][revolve]") {
    // A zero or non-finite direction cannot even be represented.
    CHECK_FALSE(Direction3D::fromComponents(0.0, 0.0, 0.0).has_value());
    CHECK_FALSE(Direction3D::fromComponents(std::nan(""), 0.0, 1.0).has_value());
    CHECK_FALSE(Direction3D::fromComponents(0.0, 0.0, std::numeric_limits<double>::infinity()).has_value());
    // A non-finite origin is rejected before anything reaches the kernel.
    const Axis3D badOrigin{Point3D{Length::fromSi(std::nan("")), 0_mm, 0_mm}, Direction3D::unitZ()};
    const auto body = makeRevolution(rectangle(10, 20, 0, 30), badOrigin, 0_deg, 360_deg);
    REQUIRE_FALSE(body.has_value());
    CHECK(body.error().code == ErrorCode::InvalidArgument);
    CHECK(body.error().message == "makeRevolution: the axis origin is not finite");
}

TEST_CASE("Revolution rejects invalid axes and sweeps", "[geometry][revolve]") {
    const PlanarRegion profile = rectangle(10, 20, 0, 30);
    SECTION("axis off the plane") {
        const auto body = makeRevolution(profile, Axis3D{Point3D{0_mm, 5_mm, 0_mm}, Direction3D::unitZ()},
                                         0_deg, 360_deg);
        REQUIRE_FALSE(body.has_value());
        CHECK(body.error().code == ErrorCode::InvalidArgument);
        CHECK_THAT(body.error().message, ContainsSubstring("does not lie in the profile plane (5 mm away)"));
    }
    SECTION("axis not parallel to the plane") {
        const auto body = makeRevolution(profile, Axis3D{Point3D{}, Direction3D::unitY()}, 0_deg, 360_deg);
        REQUIRE_FALSE(body.has_value());
        CHECK_THAT(body.error().message, ContainsSubstring("not parallel to the profile plane"));
    }
    SECTION("sweeps outside (0, 360] degrees") {
        CHECK(errorCode(makeRevolution(profile, kZAxis, 0_deg, 0_deg)) == ErrorCode::InvalidArgument);
        CHECK(errorCode(makeRevolution(profile, kZAxis, 10_deg, 5_deg)) == ErrorCode::InvalidArgument);
        CHECK(errorCode(makeRevolution(profile, kZAxis, 0_deg, 360.001_deg)) == ErrorCode::InvalidArgument);
        CHECK(errorCode(makeRevolution(profile, kZAxis, 0_deg, Angle::fromSi(std::nan("")))) ==
              ErrorCode::InvalidArgument);
    }
    SECTION("malformed loops") {
        PlanarRegion open = profile;
        open.outer.segments.pop_back();
        CHECK(errorCode(makeRevolution(open, kZAxis, 0_deg, 360_deg)) == ErrorCode::InvalidArgument);
    }
}
