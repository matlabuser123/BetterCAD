#include "GeometryTestSupport.hpp"
#include "TestHelpers.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/core/geometry/Profile.hpp>
#include <bettercad/core/geometry/Sweeps.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <array>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <numbers>
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
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::StartsWith;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

constexpr double pi = std::numbers::pi;
// Mitred corners are trimmed where the kernel intersects the pipes
// numerically, at its 1e-7 mm precision (measured: a few 1e-12, see
// examples/geometry_accuracy).
constexpr double kRelMitre = bettercad::test::kRelApproximatedIntersection;

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

/// A w x h rectangle centred at (u, v).
ProfileLoop rectangleLoop(double u, double v, double w, double h) {
    return polygon({{u - w / 2, v - h / 2}, {u + w / 2, v - h / 2}, {u + w / 2, v + h / 2}, {u - w / 2, v + h / 2}});
}

PlanarRegion region(ProfileLoop outer, const Frame3D& plane, std::vector<ProfileLoop> holes = {}) {
    return PlanarRegion{.plane = plane, .outer = std::move(outer), .holes = std::move(holes)};
}

LineSegment2D line(double u0, double v0, double u1, double v1) {
    return LineSegment2D{mm(u0, v0), mm(u1, v1)};
}

ArcSegment2D arc(double cu, double cv, double u0, double v0, double u1, double v1, bool ccw = true) {
    return ArcSegment2D{mm(cu, cv), mm(u0, v0), mm(u1, v1), ccw};
}

PlanarPath pathIn(const Frame3D& plane, std::vector<ProfileSegment> segments) {
    return PlanarPath{.plane = plane, .segments = std::move(segments)};
}

Body requireSweep(const PlanarRegion& profile, const PlanarPath& path) {
    auto body = makeSweep(profile, path);
    if (!body) {
        FAIL(body.error().message);
    }
    CHECK(body->isValid());
    CHECK(body->topology().solids == 1);
    return *body;
}

Error sweepError(const PlanarRegion& profile, const PlanarPath& path) {
    auto body = makeSweep(profile, path);
    REQUIRE_FALSE(body.has_value());
    return body.error();
}

double volumeMm3(const Body& body) {
    return requireProperties(body).volume.in(units::mm3);
}

// Body::boundingBox() is exact for planes and cylinders, but the kernel
// bounds toroidal faces numerically (BRepBndLib::AddOptimal) and pads them by
// its confusion tolerance, 1e-7 mm. Such a box must contain the exact box and
// exceed it by at most that.
constexpr double kTorusBoundsPaddingMm = 1e-7;

void checkPaddedBox(const Body& body, std::array<double, 3> min, std::array<double, 3> max) {
    const BoundingBox3D box = body.boundingBox().value();
    const std::array<double, 3> lo{box.min.x.in(units::mm), box.min.y.in(units::mm), box.min.z.in(units::mm)};
    const std::array<double, 3> hi{box.max.x.in(units::mm), box.max.y.in(units::mm), box.max.z.in(units::mm)};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        CAPTURE(axis, lo[axis], hi[axis]);
        CHECK(lo[axis] <= min[axis] + kPositionToleranceMm);
        CHECK(lo[axis] >= min[axis] - kTorusBoundsPaddingMm - kPositionToleranceMm);
        CHECK(hi[axis] >= max[axis] - kPositionToleranceMm);
        CHECK(hi[axis] <= max[axis] + kTorusBoundsPaddingMm + kPositionToleranceMm);
    }
}

// A straight path up the Z axis from the origin, drawn in the XZ plane
// (local u = +X, v = +Z).
PlanarPath upZ(double length) {
    return pathIn(Frame3D::xz(), {line(0, 0, 0, length)});
}

// A quarter arc of radius R in the XY plane about the origin, from (R, 0, 0)
// (leaving along +Y) to (0, R, 0); profiles sit in the XZ plane at x = R.
PlanarPath quarter(double R) {
    return pathIn(Frame3D::xy(), {arc(0, 0, R, 0, 0, R)});
}

} // namespace

TEST_CASE("Sweep_RegionCentroidIsExact", "[geometry][sweep][profile]") {
    // Rectangle, circle, half disc (centroid 4r/(3 pi) from the diameter)
    // and an off-centre hole, each with either orientation.
    const auto centroid = [](const PlanarRegion& r) { return regionCentroid(r); };
    Point2D c = centroid(region(rectangleLoop(30, -20, 10, 4), Frame3D::xy()));
    CHECK_THAT(c.x.in(units::mm), WithinAbs(30.0, 1e-12));
    CHECK_THAT(c.y.in(units::mm), WithinAbs(-20.0, 1e-12));
    c = centroid(region(reversed(rectangleLoop(30, -20, 10, 4)), Frame3D::xy()));
    CHECK_THAT(c.x.in(units::mm), WithinAbs(30.0, 1e-12));
    c = centroid(region(circleLoop(7, 3, 2), Frame3D::xy()));
    CHECK_THAT(c.x.in(units::mm), WithinAbs(7.0, 1e-12));
    CHECK_THAT(c.y.in(units::mm), WithinAbs(3.0, 1e-12));
    // The half disc of radius 6 above the diameter from (-6, 1) to (6, 1).
    const ProfileLoop half{{line(6, 1, -6, 1), arc(0, 1, -6, 1, 6, 1, true)}};
    const ProfileLoop halfUp{{line(-6, 1, 6, 1), arc(0, 1, 6, 1, -6, 1, true)}};
    c = centroid(region(halfUp, Frame3D::xy()));
    CHECK_THAT(c.x.in(units::mm), WithinAbs(0.0, 1e-12));
    CHECK_THAT(c.y.in(units::mm), WithinAbs(1.0 + 4.0 * 6.0 / (3.0 * pi), 1e-12));
    c = centroid(region(half, Frame3D::xy())); // the lower half disc
    CHECK_THAT(c.y.in(units::mm), WithinAbs(1.0 - 4.0 * 6.0 / (3.0 * pi), 1e-12));
    // A 20 x 20 square at the origin less a disc r 5 at (4, 0):
    // x_c = (400 x 0 - 25 pi x 4) / (400 - 25 pi).
    c = centroid(region(rectangleLoop(0, 0, 20, 20), Frame3D::xy(), {reversed(circleLoop(4, 0, 5))}));
    CHECK_THAT(c.x.in(units::mm), WithinAbs(-100.0 * pi / (400.0 - 25.0 * pi), 1e-12));
    CHECK_THAT(c.y.in(units::mm), WithinAbs(0.0, 1e-12));
    // Far from the origin the terms are taken relative to the region.
    c = centroid(region(circleLoop(5000, -7000, 0.5), Frame3D::xy()));
    CHECK_THAT(c.x.in(units::mm), WithinAbs(5000.0, 1e-9));
    CHECK_THAT(c.y.in(units::mm), WithinAbs(-7000.0, 1e-9));
}

TEST_CASE("Sweep_StraightPathsMatchPrisms", "[geometry][sweep]") {
    // A 10 x 20 mm rectangle and a circle r = 5 mm on the XY plane, swept
    // 100 mm up Z: V = A L, and the same solid as a prism of the region.
    for (const auto& [name, loop, area] : {std::tuple{"rectangle", rectangleLoop(0, 0, 10, 20), 200.0},
                                           std::tuple{"circle", circleLoop(0, 0, 5), 25.0 * pi}}) {
        CAPTURE(name);
        const PlanarRegion profile = region(loop, Frame3D::xy());
        const Body swept = requireSweep(profile, upZ(100));
        const Body prism = makePrism(profile, 0_mm, 100_mm).value();
        const MassProperties s = requireProperties(swept);
        const MassProperties p = requireProperties(prism);
        CHECK_THAT(s.volume.in(units::mm3), WithinRel(area * 100.0, kRelTight));
        CHECK_THAT(s.volume.in(units::mm3), WithinRel(p.volume.in(units::mm3), kRelTight));
        CHECK_THAT(s.surfaceArea.in(units::mm2), WithinRel(p.surfaceArea.in(units::mm2), kRelTight));
        checkPoint(s.centerOfMass, 0, 0, 50);
        const BoundingBox3D box = swept.boundingBox().value();
        const BoundingBox3D prismBox = prism.boundingBox().value();
        checkPoint(box.min, prismBox.min.x.in(units::mm), prismBox.min.y.in(units::mm), 0);
        checkPoint(box.max, prismBox.max.x.in(units::mm), prismBox.max.y.in(units::mm), 100);
    }
    // A path that leaves the profile against its normal sweeps the other way.
    const Body down = requireSweep(region(rectangleLoop(0, 0, 10, 20), Frame3D::xy()),
                                   pathIn(Frame3D::xz(), {line(0, 0, 0, -100)}));
    CHECK_THAT(volumeMm3(down), WithinRel(20000.0, kRelTight));
    checkPoint(down.boundingBox()->min, -5, -10, -100);
    checkPoint(down.boundingBox()->max, 5, 10, 0);
}

TEST_CASE("Sweep_ArcsAndCirclesMatchPappus", "[geometry][sweep]") {
    // A circle r = 2 at (20, 0, 0) in the XZ plane, swept along arcs of the
    // circle R = 20 about the Z axis: V = pi r^2 R theta.
    const PlanarRegion tube = region(circleLoop(20, 0, 2), Frame3D::xz());
    SECTION("a quarter turn: 40 pi^2") {
        const Body body = requireSweep(tube, quarter(20));
        CHECK_THAT(volumeMm3(body), WithinRel(40.0 * pi * pi, kRelTight));
        // The quarter torus: x and y from 0 to 22, z within +-2.
        checkPaddedBox(body, {0, 0, -2}, {22, 22, 2});
        // Its centroid lies on the bisector at the Pappus distance.
        const Point3D g = requireProperties(body).centerOfMass;
        CHECK_THAT((g.x - g.y).in(units::mm), WithinAbs(0.0, kPositionToleranceMm));
    }
    SECTION("three quarters, clockwise: 60 pi^2") {
        // From (20, 0) clockwise to (0, 20) the long way round.
        const Body body = requireSweep(tube, pathIn(Frame3D::xy(), {arc(0, 0, 20, 0, 0, 20, false)}));
        CHECK_THAT(volumeMm3(body), WithinRel(4.0 * pi * 20.0 * 1.5 * pi, kRelTight));
        checkPaddedBox(body, {-22, -22, -2}, {22, 22, 2});
    }
    SECTION("a full circle: the torus 2 pi^2 R r^2, as the revolution of the same circle") {
        const Body torus =
            requireSweep(tube, pathIn(Frame3D::xy(), {CircleSegment2D{mm(0, 0), 20_mm, true}}));
        const Body revolved = makeRevolution(tube, Axis3D{Point3D{}, Direction3D::unitZ()}, 0_deg, 360_deg).value();
        CHECK_THAT(volumeMm3(torus), WithinRel(2.0 * pi * pi * 20.0 * 4.0, kRelTight));
        CHECK_THAT(volumeMm3(torus), WithinRel(volumeMm3(revolved), kRelTight));
        CHECK_THAT(requireProperties(torus).surfaceArea.in(units::mm2),
                   WithinRel(4.0 * pi * pi * 20.0 * 2.0, kRelTight));
        checkPoint(requireProperties(torus).centerOfMass, 0, 0, 0);
        checkPaddedBox(torus, {-22, -22, -2}, {22, 22, 2});
    }
    SECTION("an off-path profile keeps its offset: pi r^2 (R + 5) theta") {
        const Body body = requireSweep(region(circleLoop(25, 0, 2), Frame3D::xz()), quarter(20));
        CHECK_THAT(volumeMm3(body), WithinRel(4.0 * pi * 25.0 * pi / 2.0, kRelTight));
        checkPaddedBox(body, {0, 0, -2}, {27, 27, 2});
    }
    SECTION("a tube (a region with a hole): pi (3^2 - 2^2) R theta") {
        const Body body = requireSweep(region(circleLoop(20, 0, 3), Frame3D::xz(), {reversed(circleLoop(20, 0, 2))}),
                                       quarter(20));
        CHECK_THAT(volumeMm3(body), WithinRel(5.0 * pi * 20.0 * pi / 2.0, kRelTight));
    }
}

TEST_CASE("Sweep_KeepsTheProfilesOrientation", "[geometry][sweep]") {
    // A 4 x 2 rectangle (4 radial, 2 along Z) swept a quarter turn along
    // R = 50 is a quarter of a revolved rectangle: V = A R theta (Pappus).
    // At the end the 4 mm side lies along Y, still radial: no twist.
    const PlanarRegion bar = region(rectangleLoop(50, 0, 4, 2), Frame3D::xz());
    const Body body = requireSweep(bar, quarter(50));
    CHECK_THAT(volumeMm3(body), WithinRel(8.0 * 50.0 * pi / 2.0, kRelTight));
    checkPoint(body.boundingBox()->min, 0, 0, -1);
    checkPoint(body.boundingBox()->max, 52, 52, 1);
    // The end faces: the start in the plane y = 0 facing -Y, the end in the
    // plane x = 0 facing -X, each 4 x 2 mm.
    const auto faceArea = [&](const FaceSignature& face) {
        double area = 0.0;
        for (const FaceInfo& info : findFaces(body, face).value()) {
            area += info.area.in(units::mm2);
        }
        return area;
    };
    CHECK_THAT(faceArea(planeSignature(Point3D{}, Direction3D::unitY().reversed())), WithinRel(8.0, kRelTight));
    CHECK_THAT(faceArea(planeSignature(Point3D{}, Direction3D::unitX().reversed())), WithinRel(8.0, kRelTight));
    // The flat sides stay flat: z = +-1, each a quarter annulus 48..52.
    CHECK_THAT(faceArea(planeSignature(Point3D{0_mm, 0_mm, 1_mm}, Direction3D::unitZ())),
               WithinRel(pi / 4.0 * (52.0 * 52.0 - 48.0 * 48.0), kRelTight));
}

TEST_CASE("Sweep_CornersAreMitred", "[geometry][sweep]") {
    // A centred profile around a mitred corner: what the inside of the corner
    // loses the outside gains, so V = A L exactly (L to the corner points).
    const PlanarRegion tube = region(circleLoop(0, 0, 2), Frame3D::yz()); // on x = 0, facing +X
    SECTION("an L: (0, 0, 0) -> (50, 0, 0) -> (50, 50, 0)") {
        const Body body = requireSweep(tube, pathIn(Frame3D::xy(), {line(0, 0, 50, 0), line(50, 0, 50, 50)}));
        CHECK_THAT(volumeMm3(body), WithinRel(4.0 * pi * 100.0, kRelMitre));
        checkPoint(body.boundingBox()->min, 0, -2, -2);
        checkPoint(body.boundingBox()->max, 52, 50, 2);
    }
    SECTION("an obtuse corner of 135 deg") {
        const double s = 50.0 / std::sqrt(2.0);
        const Body body = requireSweep(tube, pathIn(Frame3D::xy(), {line(0, 0, 50, 0), line(50, 0, 50 - s, s)}));
        CHECK_THAT(volumeMm3(body), WithinRel(4.0 * pi * 100.0, kRelMitre));
    }
    SECTION("a closed square, starting at a corner") {
        const Body body = requireSweep(tube, pathIn(Frame3D::xy(), {line(0, 0, 50, 0), line(50, 0, 50, 50),
                                                                    line(50, 50, 0, 50), line(0, 50, 0, 0)}));
        CHECK_THAT(volumeMm3(body), WithinRel(4.0 * pi * 200.0, kRelMitre));
        checkPoint(body.boundingBox()->min, -2, -2, -2);
        checkPoint(body.boundingBox()->max, 52, 52, 2);
        CHECK(body.topology().solids == 1);
    }
    SECTION("tangent joints need no mitre: line, quarter arc R 20, line") {
        const Body body = requireSweep(
            tube, pathIn(Frame3D::xy(), {line(0, 0, 50, 0), arc(50, 20, 50, 0, 70, 20), line(70, 20, 70, 70)}));
        CHECK_THAT(volumeMm3(body), WithinRel(4.0 * pi * (100.0 + 10.0 * pi), kRelTight));
        checkPaddedBox(body, {0, -2, -2}, {72, 70, 2});
    }
}

TEST_CASE("Sweep_RejectsMalformedPaths", "[geometry][sweep]") {
    const PlanarRegion tube = region(circleLoop(0, 0, 2), Frame3D::yz());
    const auto message = [&](std::vector<ProfileSegment> segments) {
        const Error error = sweepError(tube, pathIn(Frame3D::xy(), std::move(segments)));
        CHECK(error.code == ErrorCode::InvalidArgument);
        return error.message;
    };
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    CHECK(message({}) == "makeSweep: the path is empty");
    CHECK(message({line(0, 0, nan, 0)}) == "makeSweep: path segment 1 is not finite");
    CHECK(message({line(0, 0, 50, 0), line(50, 0, 50, inf)}) == "makeSweep: path segment 2 is not finite");
    CHECK(message({line(0, 0, 50, 0), line(50, 0, 50, -inf)}) == "makeSweep: path segment 2 is not finite");
    CHECK(message({line(0, 0, 0, 0)}) == "makeSweep: path segment 1 has zero length");
    CHECK(message({line(0, 0, 50, 0), line(50, 0, 50, 0)}) == "makeSweep: path segment 2 has zero length");
    CHECK(message({arc(0, 0, 0, 0, 0, 0)}) == "makeSweep: path segment 1 is an arc of zero radius");
    CHECK(message({arc(0, 10, 0, 0, 10, 10.5)}) == "makeSweep: path segment 1 is an arc whose ends are not on one circle");
    CHECK(message({arc(0, 10, 0, 0, 0, 0)}) ==
          "makeSweep: path segment 1 is an arc whose ends coincide (use a circle for a full turn)");
    CHECK(message({CircleSegment2D{mm(0, 0), 0_mm, true}}) == "makeSweep: path segment 1 is a circle of zero radius");
    CHECK(message({line(0, 0, 50, 0), CircleSegment2D{mm(60, 0), 10_mm, true}}) ==
          "makeSweep: path segment 2 is a full circle, which is a closed path on its own and cannot be joined with "
          "other segments");
    // Disconnected: A -> B and D -> E with B != D; nothing is repaired.
    CHECK(message({line(0, 0, 50, 0), line(51, 0, 51, 50)}) ==
          "makeSweep: the path is not connected: segment 1 ends at (50, 0) mm, but segment 2 starts at (51, 0) mm, "
          "1 mm away");
    CHECK(message({line(0, 0, 50, 0), line(50, 50, 50, 0)}) ==
          "makeSweep: the path is not connected: segment 1 ends at (50, 0) mm, but segment 2 starts at (50, 50) mm, "
          "50 mm away");
}

TEST_CASE("Sweep_RejectsCornersItCannotMitre", "[geometry][sweep]") {
    const PlanarRegion tube = region(circleLoop(0, 0, 2), Frame3D::yz());
    const auto message = [&](std::vector<ProfileSegment> segments, const PlanarRegion& profile) {
        const Error error = sweepError(profile, pathIn(Frame3D::xy(), std::move(segments)));
        CHECK(error.code == ErrorCode::InvalidArgument);
        return error.message;
    };
    // A line meeting an arc at 90 deg: the kernel builds an invalid solid there.
    CHECK(message({line(0, 0, 50, 0), arc(70, 0, 50, 0, 70, 20, false)}, tube) ==
          "makeSweep: path segments 1 (a line) and 2 (an arc) meet at an angle of 90 deg; only two straight segments "
          "may meet at a corner, and an arc must meet its neighbours tangentially");
    // Turning back by 180 deg (the kernel throws).
    CHECK(message({line(0, 0, 50, 0), line(50, 0, 0, 0)}, tube) ==
          "makeSweep: the path turns back on itself where segments 1 and 2 meet");
    // A profile r = 10 around a corner with 5 mm legs: the inside of the mitre
    // needs 10 tan 45 deg = 10 mm of each leg.
    CHECK(message({line(0, 0, 5, 0), line(5, 0, 5, 5)}, region(circleLoop(0, 0, 10), Frame3D::yz())) ==
          "makeSweep: path segment 1 is too short for the mitred corners at its ends: it is 5 mm long, but the "
          "profile reaches 10 mm from the path, which uses 10 mm of it");
    // Two corners on one leg add up: r = 2 at two right-angle corners needs
    // 2 + 2 mm of the 3 mm middle leg.
    CHECK_THAT(message({line(0, 0, 50, 0), line(50, 0, 50, 3), line(50, 3, 0, 3)}, tube),
               StartsWith("makeSweep: path segment 2 is too short for the mitred corners at its ends: it is 3 mm long"));
}

TEST_CASE("Sweep_RejectsMisplacedProfiles", "[geometry][sweep]") {
    // The path must start on the profile's plane and leave it at right angles.
    const PlanarRegion bar = region(rectangleLoop(0, 0, 10, 20), Frame3D::xy());
    Error error = sweepError(bar, pathIn(Frame3D::xz(), {line(0, 5, 0, 105)}));
    CHECK(error.code == ErrorCode::InvalidArgument);
    CHECK(error.message == "makeSweep: the path must start on the profile's plane, but it starts 5 mm from it");
    error = sweepError(bar, pathIn(Frame3D::xz(), {line(0, 0, 100, 100)}));
    CHECK(error.code == ErrorCode::InvalidArgument);
    CHECK(error.message == "makeSweep: the path must leave the profile's plane at right angles, but it leaves at 45 deg "
                           "to the plane's normal");
    // A path in the profile's own plane.
    error = sweepError(bar, pathIn(Frame3D::xy(), {line(0, 0, 100, 0)}));
    CHECK(error.message == "makeSweep: the path must leave the profile's plane at right angles, but it leaves at 90 deg "
                           "to the plane's normal");
    // Malformed, empty or self-intersecting profiles are refused as for prisms.
    CHECK(errorCode(makeSweep(region(ProfileLoop{}, Frame3D::xy()), upZ(100))) == ErrorCode::InvalidArgument);
    error = sweepError(region(polygon({{0, 0}, {10, 10}, {10, 0}, {0, 10}}), Frame3D::xy()), upZ(100));
    CHECK(error.message == "makeSweep: the profile encloses no area"); // a symmetric bow tie
    error = sweepError(region(polygon({{0, 0}, {20, 10}, {20, 0}, {0, 20}}), Frame3D::xy()), upZ(100));
    CHECK(error.code == ErrorCode::Internal);
    CHECK_THAT(error.message, StartsWith("makeSweep: the profile face is invalid"));
}

TEST_CASE("Sweep_RejectsSolidsThatFoldOrIntersect", "[geometry][sweep]") {
    SECTION("a profile reaching past the centre of an arc (checked before the kernel)") {
        // r = 25 around an arc of radius 20: the kernel alone would call the
        // folded solid valid.
        const Error error = sweepError(region(circleLoop(20, 0, 25), Frame3D::xz()), quarter(20));
        CHECK(error.code == ErrorCode::InvalidArgument);
        CHECK(error.message == "makeSweep: the profile reaches 25 mm from the path towards the centre of path segment "
                               "1, an arc of radius 20 mm: the swept solid would fold over itself");
        // Just inside the centre is fine.
        CHECK(makeSweep(region(circleLoop(20, 0, 19.9), Frame3D::xz()), quarter(20)).has_value());
    }
    SECTION("a path crossing itself (found by the kernel's self-interference check)") {
        // A line, a 270 deg tangent arc and a line back across the first line.
        const Error error = sweepError(
            region(circleLoop(0, 0, 2), Frame3D::yz()),
            pathIn(Frame3D::xy(), {line(0, 0, 50, 0), arc(50, 10, 50, 0, 40, 10), line(40, 10, 40, -30)}));
        CHECK(error.code == ErrorCode::InvalidArgument);
        CHECK(error.message == "makeSweep: the swept solid would intersect itself: the path comes back within the "
                               "profile's reach of itself");
    }
    SECTION("the same path with a smaller loop that stays clear") {
        // Legs 20 mm apart around a half circle R 10, profile r 2.
        const Body body =
            requireSweep(region(circleLoop(0, 0, 2), Frame3D::yz()),
                         pathIn(Frame3D::xy(), {line(0, 0, 50, 0), arc(50, 10, 50, 0, 50, 20), line(50, 20, 0, 20)}));
        CHECK_THAT(volumeMm3(body), WithinRel(4.0 * pi * (100.0 + 10.0 * pi), kRelTight));
    }
}
