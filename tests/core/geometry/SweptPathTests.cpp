#include "GeometryTestSupport.hpp"
#include "TestHelpers.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/geometry/Booleans.hpp>
#include <bettercad/core/geometry/Faces.hpp>
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
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// P12-SWEEP-001: paths of several runs (spatial paths), twist and guide
// curves, at the geometry layer. Every expected number here is worked out
// from the definition by hand:
//   - the volume is the profile's area times the path's length (Pappus,
//     which holds however the section turns, since its centroid rides on
//     the path);
//   - a 2a x 2b section turned by phi about the tangent reaches
//     a |cos phi| + b |sin phi| across the frame's first axis, and
//     a |sin phi| + b |cos phi| across the second, so the solid's bounds
//     say what the twist did;
//   - the twist law is theta(u) = u theta_total, so a slab cut at u tells
//     the angle there without trusting the sweep.

namespace {

constexpr double pi = std::numbers::pi;
// A guided sweep follows a curve fitted through samples of the path, so it
// meets the analytic volume less closely than an exact frame does. Measured
// worst case in the kernel probe: 1.9e-6.
constexpr double kRelGuided = 1e-5;
// Positions on a fitted guide, in millimetres: the same measurement in
// length rather than volume.
constexpr double kGuidedPositionMm = 2e-3;

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

/// A w x h rectangle centred at the origin, so its centroid rides on a path
/// that starts there.
ProfileLoop bar(double w, double h) {
    return polygon({{-w / 2, -h / 2}, {w / 2, -h / 2}, {w / 2, h / 2}, {-w / 2, h / 2}});
}

ProfileLoop circleLoop(double u, double v, double r) {
    return ProfileLoop{{CircleSegment2D{mm(u, v), r * units::mm, true}}};
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

Frame3D planeAt(const Point3D& origin, const Direction3D& normal, const Direction3D& xAxis) {
    auto frame = Frame3D::create(origin, normal, xAxis);
    REQUIRE(frame.has_value());
    return *frame;
}

Body requireSweep(const PlanarRegion& profile, const SweptPath& path) {
    auto body = makeSweep(profile, path);
    if (!body) {
        FAIL(body.error().message);
    }
    CHECK(body->isValid());
    CHECK(body->topology().solids == 1);
    return *body;
}

Error sweepError(const PlanarRegion& profile, const SweptPath& path) {
    auto body = makeSweep(profile, path);
    REQUIRE_FALSE(body.has_value());
    return body.error();
}

double volumeMm3(const Body& body) {
    return requireProperties(body).volume.in(units::mm3);
}

std::array<double, 6> boxMm(const Body& body) {
    const BoundingBox3D box = body.boundingBox().value();
    return {box.min.x.in(units::mm), box.min.y.in(units::mm), box.min.z.in(units::mm),
            box.max.x.in(units::mm), box.max.y.in(units::mm), box.max.z.in(units::mm)};
}

/// The bounding box of the slice of @p body between z = @p at ± @p half,
/// which shows where the section points there without asking the sweep.
std::array<double, 6> sliceAt(const Body& body, double at, double half, double reach) {
    const auto slab = makeSweep(region(bar(4.0 * reach, 4.0 * reach),
                                       planeAt(Point3D{0_mm, 0_mm, (at - half) * units::mm}, Direction3D::unitZ(),
                                               Direction3D::unitX())),
                                asSweptPath(pathIn(Frame3D::xz(), {line(0, at - half, 0, at + half)})));
    REQUIRE(slab.has_value());
    auto cut = booleanIntersection(body, *slab);
    REQUIRE(cut.has_value());
    return boxMm(*cut);
}

std::uint64_t bits(double value) {
    return std::bit_cast<std::uint64_t>(value);
}

// A straight path up the Z axis from the origin, drawn in the XZ plane.
PlanarPath upZ(double length) {
    return pathIn(Frame3D::xz(), {line(0, 0, 0, length)});
}

} // namespace

// ---------------------------------------------------------------------------
// One run: exactly the sweep of P11-FEAT-008
// ---------------------------------------------------------------------------

TEST_CASE("SweptPath_OneRunIsThePlanarSweepItself", "[geometry][sweep][p12]") {
    // A path of one run, without a twist or a guide, must give the planar
    // sweep's own solid: the same code builds it, so the same bits.
    const PlanarRegion profile = region(bar(10, 20), Frame3D::xy());
    const PlanarPath planar = upZ(100);
    auto direct = makeSweep(profile, planar);
    REQUIRE(direct.has_value());
    auto viaSwept = makeSweep(profile, asSweptPath(planar));
    REQUIRE(viaSwept.has_value());
    CHECK(bits(volumeMm3(*direct)) == bits(volumeMm3(*viaSwept)));
    CHECK(bits(requireProperties(*direct).surfaceArea.si()) == bits(requireProperties(*viaSwept).surfaceArea.si()));
    CHECK(direct->boundingBox().value() == viaSwept->boundingBox().value());
    CHECK(direct->topology() == viaSwept->topology());
    CHECK_THAT(volumeMm3(*viaSwept), WithinRel(10.0 * 20.0 * 100.0, kRelTight));
    CHECK(frameOf(asSweptPath(planar)) == SweepFrame::PlanarBinormal);
    CHECK(toString(SweepFrame::PlanarBinormal) == "planar binormal");
    CHECK(toString(SweepFrame::RotationMinimizing) == "rotation minimizing");
    CHECK(toString(SweepFrame::Guided) == "guided");
}

// ---------------------------------------------------------------------------
// Spatial paths
// ---------------------------------------------------------------------------

TEST_CASE("SweptPath_RunsOnDifferentPlanesMakeASpatialPath", "[geometry][sweep][p12][acceptance]") {
    // Up Z, then along X, then along Y: three straight runs, each drawn in
    // its own plane, meeting at two right angles. A path no single sketch
    // can hold.
    const PlanarRegion profile = region(bar(4, 4), Frame3D::xy());
    SweptPath path;
    path.runs.push_back(pathIn(Frame3D::xz(), {line(0, 0, 0, 40)}));         // (0,0,0) -> (0,0,40)
    path.runs.push_back(pathIn(planeAt(Point3D{0_mm, 0_mm, 40_mm}, Direction3D::unitY(), Direction3D::unitX()),
                               {line(0, 0, 40, 0)}));                        // -> (40,0,40)
    path.runs.push_back(pathIn(planeAt(Point3D{40_mm, 0_mm, 40_mm}, Direction3D::unitZ(), Direction3D::unitY()),
                               {line(0, 0, 40, 0)}));                        // -> (40,40,40)
    CHECK(frameOf(path) == SweepFrame::RotationMinimizing);

    const Body body = requireSweep(profile, path);
    // Pappus: 16 mm^2 of section along 120 mm of path.
    CHECK_THAT(volumeMm3(body), WithinRel(16.0 * 120.0, kRelTight));
    // The bar is 4 mm square about the path, so it reaches 2 mm either side
    // of every run; at the two ends it stops at its caps, z = 0 and y = 40.
    const auto box = boxMm(body);
    CHECK_THAT(box[0], WithinAbs(-2.0, kPositionToleranceMm));  // run 1 across X
    CHECK_THAT(box[1], WithinAbs(-2.0, kPositionToleranceMm));  // runs 1 and 2 across Y
    CHECK_THAT(box[2], WithinAbs(0.0, kPositionToleranceMm));   // the start cap
    CHECK_THAT(box[3], WithinAbs(42.0, kPositionToleranceMm));  // run 3 across X
    CHECK_THAT(box[4], WithinAbs(40.0, kPositionToleranceMm));  // the end cap
    CHECK_THAT(box[5], WithinAbs(42.0, kPositionToleranceMm));  // runs 2 and 3 across Z
}

TEST_CASE("SweptPath_ASpatialPathMayBendThroughAnArc", "[geometry][sweep][p12][acceptance]") {
    // A quarter arc in the XZ plane, then a straight run along +X: the arc
    // ends travelling along +X, so they meet tangentially.
    const PlanarRegion profile = region(circleLoop(0, 0, 2), Frame3D::xy());
    SweptPath path;
    // In XZ (u = +X, v = +Z), the XZ plane's normal is -Y, so a clockwise
    // arc turns about +Y. About (20, 0) from (0, 0) it leaves the origin
    // along +Z and arrives at (20, 20) travelling +X.
    path.runs.push_back(pathIn(Frame3D::xz(), {arc(20, 0, 0, 0, 20, 20, false)}));
    path.runs.push_back(pathIn(planeAt(Point3D{20_mm, 0_mm, 20_mm}, Direction3D::unitY(), Direction3D::unitX()),
                               {line(0, 0, 30, 0)}));
    const Body body = requireSweep(profile, path);
    const double length = 20.0 * pi / 2.0 + 30.0;
    CHECK_THAT(volumeMm3(body), WithinRel(4.0 * pi * length, kRelTight));
}

TEST_CASE("SweptPath_RejectsMalformedSpatialPaths", "[geometry][sweep][p12]") {
    const PlanarRegion profile = region(bar(4, 4), Frame3D::xy());
    const auto twoRuns = [](PlanarPath second) {
        SweptPath path;
        path.runs.push_back(pathIn(Frame3D::xz(), {line(0, 0, 0, 40)}));
        path.runs.push_back(std::move(second));
        return path;
    };

    SECTION("a gap between the runs") {
        const Error error = sweepError(
            profile, twoRuns(pathIn(planeAt(Point3D{0_mm, 0_mm, 45_mm}, Direction3D::unitY(), Direction3D::unitX()),
                                    {line(0, 0, 40, 0)})));
        CHECK(error.code == ErrorCode::InvalidArgument);
        CHECK_THAT(error.message, ContainsSubstring("the path is not connected"));
        CHECK_THAT(error.message, ContainsSubstring("5 mm away"));
    }
    SECTION("a run that turns back on itself") {
        // That plane's second local axis is (+Y) x (+X) = -Z, so a positive
        // v runs downwards, back along the first run.
        const Error error = sweepError(
            profile, twoRuns(pathIn(planeAt(Point3D{0_mm, 0_mm, 40_mm}, Direction3D::unitY(), Direction3D::unitX()),
                                    {line(0, 0, 0, 40)})));
        CHECK(error.code == ErrorCode::InvalidArgument);
        CHECK_THAT(error.message, ContainsSubstring("turns back on itself"));
    }
    SECTION("an empty run") {
        SweptPath path;
        path.runs.push_back(pathIn(Frame3D::xz(), {line(0, 0, 0, 40)}));
        path.runs.push_back(pathIn(Frame3D::xy(), {}));
        CHECK_THAT(sweepError(profile, path).message, ContainsSubstring("the path has an empty run 2"));
    }
    SECTION("no runs at all") {
        SweptPath path;
        path.twist = 90_deg;
        CHECK_THAT(sweepError(profile, path).message, ContainsSubstring("the path is empty"));
    }
    SECTION("a profile whose centroid is off the path") {
        // The bar is centred at (10, 0), so it does not ride on the path.
        const PlanarRegion offset = region(polygon({{8, -2}, {12, -2}, {12, 2}, {8, 2}}), Frame3D::xy());
        SweptPath path;
        path.runs.push_back(upZ(40));
        path.twist = 90_deg;
        const Error error = sweepError(offset, path);
        CHECK(error.code == ErrorCode::InvalidArgument);
        CHECK_THAT(error.message, ContainsSubstring("the profile's centroid must lie on the path"));
        CHECK_THAT(error.message, ContainsSubstring("10 mm"));
    }
}

// ---------------------------------------------------------------------------
// Twist
// ---------------------------------------------------------------------------

TEST_CASE("SweptPath_TwistTurnsTheSectionAboutTheTangent", "[geometry][sweep][p12][acceptance]") {
    // An 8 x 2 bar up 100 mm. Turned by phi, its half-extents are
    // 4|cos phi| + |sin phi| across X and 4|sin phi| + |cos phi| across Y.
    // The solid's bounds are the largest of those over the whole turn.
    constexpr double a = 4.0;
    constexpr double b = 1.0;
    const PlanarRegion profile = region(bar(2.0 * a, 2.0 * b), Frame3D::xy());
    const double radius = std::hypot(a, b); // the section's own reach

    const auto sweptWith = [&](Angle twist) {
        SweptPath path;
        path.runs.push_back(upZ(100));
        path.twist = twist;
        CHECK(frameOf(path) == (twist == Angle{} ? SweepFrame::PlanarBinormal : SweepFrame::Guided));
        return requireSweep(profile, path);
    };

    SECTION("no twist is the sweep it always was") {
        const Body body = sweptWith(0_deg);
        CHECK_THAT(volumeMm3(body), WithinRel(2.0 * a * 2.0 * b * 100.0, kRelTight));
        const auto box = boxMm(body);
        CHECK_THAT(box[0], WithinAbs(-a, kPositionToleranceMm));
        CHECK_THAT(box[4], WithinAbs(b, kPositionToleranceMm));
        // Its four sides are still flat.
        const auto side = findFaces(body, planeSignature(Point3D{a * units::mm, 0_mm, 0_mm}, Direction3D::unitX()));
        REQUIRE(side.has_value());
        CHECK(side->size() == 1);
    }

    SECTION("a quarter turn reaches the section's own radius both ways") {
        const Body body = sweptWith(90_deg);
        // The volume does not change: Pappus does not care how it turns.
        CHECK_THAT(volumeMm3(body), WithinRel(2.0 * a * 2.0 * b * 100.0, kRelGuided));
        // phi runs over [0, 90 deg], so both extents reach sqrt(a^2 + b^2).
        const auto box = boxMm(body);
        CHECK_THAT(box[0], WithinAbs(-radius, kGuidedPositionMm));
        CHECK_THAT(box[3], WithinAbs(radius, kGuidedPositionMm));
        CHECK_THAT(box[1], WithinAbs(-radius, kGuidedPositionMm));
        CHECK_THAT(box[4], WithinAbs(radius, kGuidedPositionMm));
        CHECK_THAT(box[2], WithinAbs(0.0, kGuidedPositionMm));
        CHECK_THAT(box[5], WithinAbs(100.0, kGuidedPositionMm));
    }

    SECTION("an eighth turn reaches it one way only") {
        // phi over [0, 45 deg]: the X extent peaks at atan(b/a) = 14.04 deg,
        // which is inside, so it reaches the radius; the Y extent is largest
        // at the end, 4 sin 45 + cos 45 = 3.5355 mm.
        const Body body = sweptWith(45_deg);
        const auto box = boxMm(body);
        CHECK_THAT(box[3], WithinAbs(radius, kGuidedPositionMm));
        CHECK_THAT(box[4], WithinAbs((a + b) / std::sqrt(2.0), kGuidedPositionMm));
    }

    SECTION("a half turn brings the section back onto itself") {
        const Body body = sweptWith(180_deg);
        CHECK_THAT(volumeMm3(body), WithinRel(2.0 * a * 2.0 * b * 100.0, kRelGuided));
        const auto box = boxMm(body);
        CHECK_THAT(box[3], WithinAbs(radius, kGuidedPositionMm));
        CHECK_THAT(box[4], WithinAbs(radius, kGuidedPositionMm));
    }

    SECTION("a full turn is a full turn, not no turn") {
        const Body body = sweptWith(360_deg);
        CHECK_THAT(volumeMm3(body), WithinRel(2.0 * a * 2.0 * b * 100.0, kRelGuided));
        const auto box = boxMm(body);
        CHECK_THAT(box[3], WithinAbs(radius, kGuidedPositionMm));
        CHECK_THAT(box[4], WithinAbs(radius, kGuidedPositionMm));
        // A full turn is not the untwisted bar: that one is only 1 mm thick.
        CHECK(box[4] > 2.0);
    }

    SECTION("the other way round") {
        const Body body = sweptWith(-90_deg);
        CHECK_THAT(volumeMm3(body), WithinRel(2.0 * a * 2.0 * b * 100.0, kRelGuided));
        const auto box = boxMm(body);
        CHECK_THAT(box[1], WithinAbs(-radius, kGuidedPositionMm));
        CHECK_THAT(box[4], WithinAbs(radius, kGuidedPositionMm));
    }
}

TEST_CASE("SweptPath_TwistIsProportionalToTheDistanceAlongThePath", "[geometry][sweep][p12][acceptance]") {
    // theta(u) = u theta_total, checked where the sweep cannot answer for
    // itself: a thin slab cut from the solid at u shows the section there.
    constexpr double a = 4.0;
    constexpr double b = 1.0;
    const PlanarRegion profile = region(bar(2.0 * a, 2.0 * b), Frame3D::xy());
    SweptPath path;
    path.runs.push_back(upZ(100));
    path.twist = 180_deg;
    const Body body = requireSweep(profile, path);

    // At u the section has turned u x 180 deg, so it reaches
    // a |cos| + b |sin| across X and a |sin| + b |cos| across Y.
    for (const double u : {0.0, 0.25, 0.5, 0.75, 1.0}) {
        CAPTURE(u);
        const double phi = u * pi;
        const auto box = sliceAt(body, u * 100.0, 0.05, std::hypot(a, b));
        const double acrossX = a * std::abs(std::cos(phi)) + b * std::abs(std::sin(phi));
        const double acrossY = a * std::abs(std::sin(phi)) + b * std::abs(std::cos(phi));
        // The slab is 0.1 mm tall, over which the section turns 0.18 deg, so
        // the measured extents exceed the exact ones by at most that.
        CHECK_THAT(box[3], WithinAbs(acrossX, 0.02));
        CHECK_THAT(box[4], WithinAbs(acrossY, 0.02));
    }
}

TEST_CASE("SweptPath_TwistsAlongACurvedPath", "[geometry][sweep][p12][acceptance]") {
    // A quarter bend of radius 40 with a quarter turn: the volume is still
    // the area times the path, and the section reaches its own radius.
    constexpr double a = 4.0;
    constexpr double b = 1.0;
    const PlanarRegion profile = region(bar(2.0 * a, 2.0 * b), Frame3D::xz());
    SweptPath path;
    // In XY about the origin, from (40, 0) leaving along +Y to (0, 40).
    path.runs.push_back(pathIn(Frame3D::xy(), {arc(0, 0, 40, 0, 0, 40)}));
    // The profile must sit at the path's start, across it.
    PlanarRegion placed = profile;
    placed.plane = planeAt(Point3D{40_mm, 0_mm, 0_mm}, Direction3D::unitY(), Direction3D::unitX());
    path.twist = 90_deg;
    const Body body = requireSweep(placed, path);
    CHECK_THAT(volumeMm3(body), WithinRel(2.0 * a * 2.0 * b * 40.0 * pi / 2.0, kRelGuided));
    // It stays within the section's radius of the 40 mm circle it follows.
    const auto box = boxMm(body);
    const double radius = std::hypot(a, b);
    CHECK(box[5] <= radius + kGuidedPositionMm);
    CHECK(box[2] >= -radius - kGuidedPositionMm);
    CHECK(box[3] <= 40.0 + radius + kGuidedPositionMm);
}

TEST_CASE("SweptPath_RejectsANonFiniteTwist", "[geometry][sweep][p12]") {
    const PlanarRegion profile = region(bar(8, 2), Frame3D::xy());
    SweptPath path;
    path.runs.push_back(upZ(100));
    path.twist = Angle::fromSi(std::numeric_limits<double>::quiet_NaN());
    const Error error = sweepError(profile, path);
    CHECK(error.code == ErrorCode::InvalidArgument);
    CHECK_THAT(error.message, ContainsSubstring("the twist must be finite"));
}

// ---------------------------------------------------------------------------
// Guide curves
// ---------------------------------------------------------------------------

TEST_CASE("SweptPath_AGuideCurveCarriesTheSection", "[geometry][sweep][p12][acceptance]") {
    // The path runs up Z; the guide runs beside it and rises across, so the
    // section turns to follow it. The guide leans 45 degrees over the
    // length, so the section ends 45 degrees round.
    constexpr double a = 4.0;
    constexpr double b = 1.0;
    const PlanarRegion profile = region(bar(2.0 * a, 2.0 * b), Frame3D::xy());
    SweptPath path;
    path.runs.push_back(upZ(100));
    // The guide runs from (10, 0, 0) to (0, 10, 100). Its offset from the
    // path turns from +X to +Y: a quarter turn, by the guide's own
    // definition, so the section must end a quarter turn round.
    const auto along = Direction3D::fromComponents(-10.0, 10.0, 100.0);
    REQUIRE(along.has_value());
    const double length = std::sqrt(10.0 * 10.0 + 10.0 * 10.0 + 100.0 * 100.0);
    const auto across = along->cross(Direction3D::unitX());
    REQUIRE(across.has_value());
    path.guide.push_back(pathIn(planeAt(Point3D{10_mm, 0_mm, 0_mm}, *across, *along), {line(0, 0, length, 0)}));
    CHECK(frameOf(path) == SweepFrame::Guided);
    const Body body = requireSweep(profile, path);
    CHECK_THAT(volumeMm3(body), WithinRel(2.0 * a * 2.0 * b * 100.0, kRelGuided));
    // phi runs over [0, 90 deg], so both extents reach the section's radius.
    const auto box = boxMm(body);
    CHECK_THAT(box[3], WithinAbs(std::hypot(a, b), kGuidedPositionMm));
    CHECK_THAT(box[4], WithinAbs(std::hypot(a, b), kGuidedPositionMm));
    // Not the untwisted bar, which is only b thick across Y.
    CHECK(box[4] > 2.0 * b);
}

TEST_CASE("SweptPath_RejectsAnUnusableGuide", "[geometry][sweep][p12]") {
    const PlanarRegion profile = region(bar(8, 2), Frame3D::xy());
    SweptPath path;
    path.runs.push_back(upZ(100));

    SECTION("a guide whose runs do not meet") {
        path.guide.push_back(pathIn(Frame3D::xz(), {line(10, 0, 10, 40)}));
        path.guide.push_back(pathIn(Frame3D::xz(), {line(10, 50, 10, 100)}));
        const Error error = sweepError(profile, path);
        CHECK(error.code == ErrorCode::InvalidArgument);
        CHECK_THAT(error.message, ContainsSubstring("the guide is not connected"));
        CHECK_THAT(error.message, ContainsSubstring("10 mm away"));
    }
    SECTION("a guide of no length") {
        path.guide.push_back(pathIn(Frame3D::xz(), {line(10, 0, 10, 0)}));
        const Error error = sweepError(profile, path);
        CHECK(error.code == ErrorCode::InvalidArgument);
        CHECK_THAT(error.message, ContainsSubstring("the guide segment 1 is a point"));
    }
}
