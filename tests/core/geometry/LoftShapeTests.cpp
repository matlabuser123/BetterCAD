#include "GeometryTestSupport.hpp"
#include "TestHelpers.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/core/geometry/Profile.hpp>
#include <bettercad/core/geometry/Split.hpp>
#include <bettercad/core/geometry/Sweeps.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <array>
#include <cmath>
#include <initializer_list>
#include <numbers>
#include <utility>
#include <vector>

using namespace bettercad;
using namespace bettercad::geometry;
using namespace bettercad::literals;
using bettercad::test::checkPoint;
using bettercad::test::kPositionToleranceMm;
using bettercad::test::kRelTight;
using bettercad::test::requireProperties;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// P12-LOFT-001: lofts between sections of different shapes, matched by arc
// length. The expected volumes here are closed forms derived by hand, not
// read back from the loft.
//
// Between matched sections whose points move in straight lines, the
// cross-section at t has area
//
//     A(t) = (1 - t)^2 A0 + t (1 - t) M + t^2 A1,
//
// with M the mixed area, so the volume is h/6 (A0 + 4 Am + A1) with
// Am = (A0 + M + A1) / 4 -- the prismatoid formula, exact.
//
// For a regular n-gon of circumradius R lofted to a circle of radius r,
// matched corner to equal arc, the mixed area works out in closed form:
//
//     M = (2 n^2 r R / pi) sin^2(pi / n)
//
// (each of the n sides contributes (r R / phi) 4 sin^2(phi/2) with
// phi = 2 pi / n). For n = 4, R = 10, r = 5 that is 800/pi = 254.6479 mm^2,
// and Am = (200 + 254.6479 + 78.5398)/4 = 133.2969 mm^2 -- the value an
// independent 4096-point sampling of the averaged curve gives in the kernel
// probe (133.296911).

namespace {

constexpr double pi = std::numbers::pi;
// The kernel's ruled faces between a line and an arc are B-splines, as they
// are between turned circles: P11-FEAT-009 measured those within 6.3e-10 of
// the closed form.
constexpr double kRelSpline = bettercad::test::kRelApproximatedIntersection;
constexpr double kBoundsPaddingMm = 1e-7;
// A smooth loft passes through its INTERMEDIATE sections only as closely as
// the kernel's one-surface fit through the section curves allows. Measured
// on the three-circle spool by cutting the solid on the middle section's
// plane: 78.54028940 mm^2 against an exact 78.53981634, i.e. 6.0e-6 relative
// in area and 1.5e-5 mm in radius. The end sections are the caps and are
// much closer (3.8e-11). This bound is that measurement, rounded up once;
// it is not used for anything else.
constexpr double kSectionThroughSmooth = 1e-5;

Point2D mm(double u, double v) {
    return Point2D{u * units::mm, v * units::mm};
}

/// A regular n-gon of circumradius @p r, its first corner at angle @p start,
/// counter-clockwise.
ProfileLoop regular(int n, double r, double start = 0.0) {
    std::vector<Point2D> corners;
    for (int i = 0; i < n; ++i) {
        const double a = start + 2.0 * pi * static_cast<double>(i) / static_cast<double>(n);
        corners.push_back(mm(r * std::cos(a), r * std::sin(a)));
    }
    ProfileLoop loop;
    for (int i = 0; i < n; ++i) {
        loop.segments.emplace_back(LineSegment2D{corners[static_cast<std::size_t>(i)],
                                                 corners[static_cast<std::size_t>((i + 1) % n)]});
    }
    return loop;
}

ProfileLoop circleLoop(double r) {
    return ProfileLoop{{CircleSegment2D{mm(0, 0), r * units::mm, true}}};
}

Frame3D levelAt(double z) {
    auto frame = Frame3D::create(Point3D{0_mm, 0_mm, z * units::mm}, Direction3D::unitZ(), Direction3D::unitX());
    REQUIRE(frame.has_value());
    return *frame;
}

PlanarRegion section(ProfileLoop loop, double z) {
    return PlanarRegion{.plane = levelAt(z), .outer = std::move(loop), .holes = {}};
}

Body requireBody(const Result<Body>& body) {
    if (!body) {
        FAIL(body.error().message);
    }
    return *body;
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

/// The mixed area of a regular n-gon of circumradius R and a circle of
/// radius r, matched corner to equal arc: (2 n^2 r R / pi) sin^2(pi / n).
double mixedPolygonCircle(int n, double radius, double circumradius) {
    const double k = static_cast<double>(n);
    return 2.0 * k * k * radius * circumradius / pi * std::pow(std::sin(pi / k), 2.0);
}

double polygonArea(int n, double r) {
    return 0.5 * static_cast<double>(n) * r * r * std::sin(2.0 * pi / static_cast<double>(n));
}

/// h/6 (A0 + 4 Am + A1) with Am = (A0 + M + A1)/4.
double prismatoid(double h, double a0, double a1, double m) {
    return h / 6.0 * (a0 + (a0 + m + a1) + a1);
}

void checkPaddedBox(const Body& body, std::array<double, 3> min, std::array<double, 3> max) {
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

} // namespace

TEST_CASE("LoftShapes_SquareToCircleMatchesTheClosedForm", "[geometry][loft][p12][acceptance]") {
    // A square of circumradius 10 at z = 0 to a circle of radius 5 at
    // z = 30. By hand: A0 = 2 R^2 = 200, A1 = pi r^2, M = 16 r R / pi.
    const double h = 30.0;
    const double a0 = polygonArea(4, 10.0);
    const double a1 = pi * 25.0;
    const double m = mixedPolygonCircle(4, 5.0, 10.0);
    CHECK_THAT(a0, WithinRel(200.0, 1e-15));
    CHECK_THAT(m, WithinRel(800.0 / pi, 1e-15));
    // The halfway area an independent sampling of the averaged curve gives.
    // The probe's figure is a 4096-point shoelace of a curve, which
    // under-estimates by order N^-2; the closed form here is exact, and the
    // two agree to 1.5e-7 relative, which is that sampling error.
    CHECK_THAT(0.25 * (a0 + m + a1), WithinRel(133.296911, 1e-6));
    CHECK_THAT(0.25 * (a0 + m + a1), WithinRel(133.29693132169436, 1e-14));
    const double expected = prismatoid(h, a0, a1, m);
    CHECK_THAT(expected, WithinRel(4058.6373, 1e-6));

    const Body body = requireLoft({section(regular(4, 10.0), 0.0), section(circleLoop(5.0), h)});
    INFO("expected " << expected << " mm^3, actual " << volumeMm3(body) << " mm^3");
    CHECK_THAT(volumeMm3(body), WithinRel(expected, kRelSpline));
    checkPaddedBox(body, {-10, -10, 0}, {10, 10, 30});
    // The caps are the sections themselves.
    const auto bottom = findFaces(body, planeSignature(Point3D{}, Direction3D::unitZ().reversed()));
    REQUIRE(bottom.has_value());
    REQUIRE(bottom->size() == 1);
    CHECK_THAT(bottom->front().area.in(units::mm2), WithinRel(a0, kRelTight));
    const auto top = findFaces(body, planeSignature(Point3D{0_mm, 0_mm, h * units::mm}, Direction3D::unitZ()));
    REQUIRE(top.has_value());
    REQUIRE(top->size() == 1);
    CHECK_THAT(top->front().area.in(units::mm2), WithinRel(a1, kRelTight));
}

TEST_CASE("LoftShapes_CircleToSquareIsTheSameSolidTheOtherWayUp", "[geometry][loft][p12]") {
    // The same two sections in the other order enclose the same volume.
    const double h = 30.0;
    const double expected = prismatoid(h, polygonArea(4, 10.0), pi * 25.0, mixedPolygonCircle(4, 5.0, 10.0));
    const Body up = requireLoft({section(regular(4, 10.0), 0.0), section(circleLoop(5.0), h)});
    const Body down = requireLoft({section(circleLoop(5.0), 0.0), section(regular(4, 10.0), h)});
    CHECK_THAT(volumeMm3(up), WithinRel(expected, kRelSpline));
    CHECK_THAT(volumeMm3(down), WithinRel(expected, kRelSpline));
    CHECK_THAT(volumeMm3(down), WithinRel(volumeMm3(up), kRelSpline));
}

TEST_CASE("LoftShapes_TriangleAndHexagonToCircles", "[geometry][loft][p12][acceptance]") {
    const double h = 30.0;
    SECTION("a triangle to a circle") {
        const double a0 = polygonArea(3, 10.0);
        const double a1 = pi * 25.0;
        const double m = mixedPolygonCircle(3, 5.0, 10.0);
        const double expected = prismatoid(h, a0, a1, m);
        const Body body = requireLoft({section(regular(3, 10.0), 0.0), section(circleLoop(5.0), h)});
        INFO("expected " << expected << " mm^3, actual " << volumeMm3(body) << " mm^3");
        CHECK_THAT(volumeMm3(body), WithinRel(expected, kRelSpline));
    }
    SECTION("a hexagon to a circle") {
        const double a0 = polygonArea(6, 10.0);
        const double a1 = pi * 25.0;
        const double m = mixedPolygonCircle(6, 5.0, 10.0);
        const double expected = prismatoid(h, a0, a1, m);
        const Body body = requireLoft({section(regular(6, 10.0), 0.0), section(circleLoop(5.0), h)});
        CHECK_THAT(volumeMm3(body), WithinRel(expected, kRelSpline));
    }
}

TEST_CASE("LoftShapes_PolygonsOfDifferentCornerCounts", "[geometry][loft][p12][acceptance]") {
    // A square to a hexagon: matched by arc length, so the solid is the one
    // whose cross-section is the average of the two at every height. Its
    // volume is the prismatoid of the matched sections, which the loft's own
    // planner computes; here it is checked against bounds that need no
    // matching at all -- it must lie strictly between the prism of the
    // smaller section and the prism of the larger.
    const double h = 30.0;
    const Body body = requireLoft({section(regular(4, 10.0), 0.0), section(regular(6, 5.0), h)});
    const double small = polygonArea(6, 5.0) * h;
    const double large = polygonArea(4, 10.0) * h;
    INFO("V " << volumeMm3(body) << " mm^3, between " << small << " and " << large);
    CHECK(volumeMm3(body) > small);
    CHECK(volumeMm3(body) < large);
    checkPaddedBox(body, {-10, -10, 0}, {10, 10, 30});
}

TEST_CASE("LoftShapes_AreUnchangedByWhereASketchStarts", "[geometry][loft][p12][acceptance]") {
    // The same square drawn from another corner, and the same square wound
    // the other way, must give the same solid: the matching is by geometry,
    // not by the order the profile happens to be written in.
    const double h = 30.0;
    const Body base = requireLoft({section(regular(4, 10.0), 0.0), section(circleLoop(5.0), h)});
    const double expected = volumeMm3(base);

    const Body turned = requireLoft({section(regular(4, 10.0, pi / 2.0), 0.0), section(circleLoop(5.0), h)});
    CHECK_THAT(volumeMm3(turned), WithinRel(expected, kRelSpline));

    ProfileLoop reversedSquare = reversed(regular(4, 10.0));
    const Body flipped = requireLoft({section(reversedSquare, 0.0), section(circleLoop(5.0), h)});
    CHECK_THAT(volumeMm3(flipped), WithinRel(expected, kRelSpline));
}

TEST_CASE("LoftShapes_ASideSplitInTwoStillMatches", "[geometry][loft][p12]") {
    // A square whose top side is drawn as two lines has five segments
    // against the other's four. P11-FEAT-009 refused that; matching by arc
    // length takes it, and the solid is the one the unsplit square gives.
    const double h = 30.0;
    ProfileLoop split;
    split.segments.emplace_back(LineSegment2D{mm(-10, -10), mm(10, -10)});
    split.segments.emplace_back(LineSegment2D{mm(10, -10), mm(10, 10)});
    split.segments.emplace_back(LineSegment2D{mm(10, 10), mm(0, 10)});
    split.segments.emplace_back(LineSegment2D{mm(0, 10), mm(-10, 10)});
    split.segments.emplace_back(LineSegment2D{mm(-10, 10), mm(-10, -10)});
    ProfileLoop plain;
    plain.segments.emplace_back(LineSegment2D{mm(-5, -5), mm(5, -5)});
    plain.segments.emplace_back(LineSegment2D{mm(5, -5), mm(5, 5)});
    plain.segments.emplace_back(LineSegment2D{mm(5, 5), mm(-5, 5)});
    plain.segments.emplace_back(LineSegment2D{mm(-5, 5), mm(-5, -5)});

    const Body body = requireLoft({section(split, 0.0), section(plain, h)});
    // Both are squares centred on the axis: a pyramidal frustum,
    // V = h/3 (A1 + A2 + sqrt(A1 A2)) = 10 (400 + 100 + 200) = 7000.
    CHECK_THAT(volumeMm3(body), WithinRel(7000.0, kRelSpline));
}

TEST_CASE("LoftShapes_StillRefusesWhatItCannotMatch", "[geometry][loft][p12]") {
    SECTION("a section of no area") {
        ProfileLoop flat;
        flat.segments.emplace_back(LineSegment2D{mm(0, 0), mm(10, 0)});
        flat.segments.emplace_back(LineSegment2D{mm(10, 0), mm(0, 0)});
        const Error error = loftError({section(flat, 0.0), section(circleLoop(5.0), 30.0)});
        CHECK(error.code == ErrorCode::InvalidArgument);
        CHECK_THAT(error.message, ContainsSubstring("encloses no area"));
    }
    SECTION("sections on the same plane") {
        const Error error = loftError({section(regular(4, 10.0), 0.0), section(circleLoop(5.0), 0.0)});
        CHECK_THAT(error.message, ContainsSubstring("lie on the same plane"));
    }
    SECTION("a section with a hole") {
        PlanarRegion holed = section(regular(4, 10.0), 0.0);
        holed.holes.push_back(circleLoop(2.0));
        const Error error = loftError({holed, section(circleLoop(5.0), 30.0)});
        CHECK_THAT(error.message, ContainsSubstring("has a hole"));
    }
    SECTION("a loft that folds") {
        // A square to a circle of no size: the cross-section vanishes.
        const Error error = loftError({section(regular(4, 10.0), 0.0), section(circleLoop(1e-7), 30.0)});
        CHECK(error.code == ErrorCode::InvalidArgument);
    }
}

TEST_CASE("LoftShapes_EqualShapesAreMatchedAsBefore", "[geometry][loft][p12]") {
    // The P11-FEAT-009 path must be untouched: equal shapes keep their own
    // matching, and their volumes are the exact prismatoid values.
    const double h = 30.0;
    const Body squares = requireLoft({section(regular(4, 10.0), 0.0), section(regular(4, 5.0), h)});
    // Similar squares centred on the axis: h/3 (A1 + A2 + sqrt(A1 A2)).
    const double a0 = polygonArea(4, 10.0);
    const double a1 = polygonArea(4, 5.0);
    CHECK_THAT(volumeMm3(squares), WithinRel(h / 3.0 * (a0 + a1 + std::sqrt(a0 * a1)), kRelTight));

    const Body circles = requireLoft({section(circleLoop(10.0), 0.0), section(circleLoop(5.0), h)});
    CHECK_THAT(volumeMm3(circles), WithinRel(pi * h / 3.0 * (100.0 + 50.0 + 25.0), kRelTight));
}

// ---------------------------------------------------------------------------
// Smooth interpolation
// ---------------------------------------------------------------------------

TEST_CASE("LoftShapes_ChainsOfThreeDifferentShapesStayMatched", "[geometry][loft][p12][acceptance]") {
    // Matching is pairwise, but a section in the middle of a chain belongs to
    // two pairs at once, and both must split it the same way -- otherwise the
    // kernel is handed sections with different numbers of edges and the
    // correspondence between the first pair is silently lost.
    //
    // Square (R 10) -> circle (r 5) -> hexagon (R 8), 25 mm apart. Nothing
    // here is symmetric, so a segmentation chosen for one pair does not
    // happen to suit the other.
    const std::vector<PlanarRegion> sections{section(regular(4, 10.0), 0.0), section(circleLoop(5.0), 25.0),
                                             section(regular(6, 8.0), 50.0)};
    const Body body = requireLoft(sections);

    // Each interval is its own prismatoid, exactly as a single pair is.
    const double square = polygonArea(4, 10.0);
    const double circle = pi * 25.0;
    const double hexagon = polygonArea(6, 8.0);
    const double lower = prismatoid(25.0, square, circle, mixedPolygonCircle(4, 5.0, 10.0));
    const double upper = prismatoid(25.0, circle, hexagon, mixedPolygonCircle(6, 5.0, 8.0));
    INFO("lower " << lower << " + upper " << upper << " = " << lower + upper << " mm^3");
    CHECK_THAT(volumeMm3(body), WithinRel(lower + upper, kRelSpline));
    checkPaddedBox(body, {-10.0, -10.0, 0.0}, {10.0, 10.0, 50.0});
}

TEST_CASE("LoftShapes_ChainsAreIndependentOfTheirDirection", "[geometry][loft][p12]") {
    // The same chain read upside down is the same solid mirrored, so it
    // encloses the same volume. A segmentation that depended on the order
    // the pairs happen to be visited in would not.
    const std::vector<PlanarRegion> up{section(regular(4, 10.0), 0.0), section(circleLoop(5.0), 25.0),
                                       section(regular(6, 8.0), 50.0)};
    const std::vector<PlanarRegion> down{section(regular(6, 8.0), 0.0), section(circleLoop(5.0), 25.0),
                                         section(regular(4, 10.0), 50.0)};
    CHECK_THAT(volumeMm3(requireLoft(up)), WithinRel(volumeMm3(requireLoft(down)), kRelSpline));
}

TEST_CASE("LoftSmooth_WithTwoSectionsIsTheRuledLoft", "[geometry][loft][p12][acceptance]") {
    // With two sections there is nothing to run across, so a smooth loft is
    // the ruled one. Measured in the kernel probe as identical; asserted
    // here to rounding, against the frustum's own closed form.
    const double h = 30.0;
    const std::vector<PlanarRegion> sections{section(circleLoop(10.0), 0.0), section(circleLoop(5.0), h)};
    auto ruled = makeLoft(sections, LoftStyle::Ruled, SweptFaceNamer{});
    REQUIRE(ruled.has_value());
    auto smooth = makeLoft(sections, LoftStyle::Smooth, SweptFaceNamer{});
    REQUIRE(smooth.has_value());
    const double frustum = pi * h / 3.0 * (100.0 + 50.0 + 25.0);
    CHECK_THAT(volumeMm3(*ruled), WithinRel(frustum, kRelTight));
    CHECK_THAT(volumeMm3(*smooth), WithinRel(frustum, kRelTight));
    CHECK(smooth->topology() == ruled->topology());
    CHECK(toString(LoftStyle::Ruled) == "ruled");
    CHECK(toString(LoftStyle::Smooth) == "smooth");
}

TEST_CASE("LoftSmooth_ThroughThreeSectionsFollowsTheQuadratic", "[geometry][loft][p12][acceptance]") {
    // Three circles r 10, 5, 10 equally spaced over 50 mm. The smooth
    // surface is the quadratic through the three radii:
    //   r(t) = 10 - 20 t (1 - t),  integral of r^2 over [0,1] = 140/3,
    // so V = pi h (140/3) = 7000 pi / 3. The ruled loft is two frustums,
    // 8750 pi / 3. Both are closed forms; neither comes from the loft.
    const double h = 50.0;
    const std::vector<PlanarRegion> sections{section(circleLoop(10.0), 0.0), section(circleLoop(5.0), 25.0),
                                             section(circleLoop(10.0), h)};
    auto ruled = makeLoft(sections, LoftStyle::Ruled, SweptFaceNamer{});
    REQUIRE(ruled.has_value());
    auto smooth = makeLoft(sections, LoftStyle::Smooth, SweptFaceNamer{});
    REQUIRE(smooth.has_value());

    const double ruledVolume = 2.0 * pi * 25.0 / 3.0 * (100.0 + 50.0 + 25.0);
    CHECK_THAT(ruledVolume, WithinRel(8750.0 * pi / 3.0, 1e-15));
    CHECK_THAT(volumeMm3(*ruled), WithinRel(ruledVolume, kRelTight));

    const double smoothVolume = 7000.0 * pi / 3.0;
    INFO("smooth " << volumeMm3(*smooth) << " mm^3, quadratic law " << smoothVolume);
    CHECK_THAT(volumeMm3(*smooth), WithinRel(smoothVolume, kRelSpline));
    // It is genuinely a different solid: four fifths of the ruled one.
    CHECK_THAT(volumeMm3(*smooth) / volumeMm3(*ruled), WithinRel(0.8, 1e-8));

    // The signature of smoothness: the ruled loft's sides are split into a
    // band per interval, the smooth loft's run across the middle section.
    CHECK(ruled->topology().faces == 4);  // two caps and two bands
    CHECK(smooth->topology().faces == 3); // two caps and one continuous side
    CHECK(smooth->isValid());
    CHECK(smooth->topology().solids == 1);
}

TEST_CASE("LoftSmooth_PassesThroughEverySection", "[geometry][loft][p12][acceptance]") {
    // Interpolation means the surface goes through the sections, not near
    // them. A thin slab cut at the middle section must have its radius.
    const double h = 50.0;
    const std::vector<PlanarRegion> sections{section(circleLoop(10.0), 0.0), section(circleLoop(5.0), 25.0),
                                             section(circleLoop(10.0), h)};
    auto smooth = makeLoft(sections, LoftStyle::Smooth, SweptFaceNamer{});
    REQUIRE(smooth.has_value());

    // The caps lie on the end sections' planes and carry their area. A
    // smooth loft's caps are bounded by the curve the kernel approximated
    // for the side, not by the section's own circle, so their area matches
    // to 3.8e-11 rather than to rounding; a ruled loft's caps are exact
    // (LoftShapes_SquareToCircleMatchesTheClosedForm checks those at 1e-12).
    const auto bottom = findFaces(*smooth, planeSignature(Point3D{}, Direction3D::unitZ().reversed()));
    REQUIRE(bottom.has_value());
    REQUIRE(bottom->size() == 1);
    CHECK_THAT(bottom->front().area.in(units::mm2), WithinRel(pi * 100.0, kRelSpline));
    const auto top = findFaces(*smooth, planeSignature(Point3D{0_mm, 0_mm, h * units::mm}, Direction3D::unitZ()));
    REQUIRE(top.has_value());
    REQUIRE(top->size() == 1);
    CHECK_THAT(top->front().area.in(units::mm2), WithinRel(pi * 100.0, kRelSpline));

    checkPaddedBox(*smooth, {-10, -10, 0}, {10, 10, 50});

    // The intermediate section is the whole claim, and the end caps do not
    // test it: cut the solid on that section's plane and measure the face
    // the surface actually makes there.
    const auto sectionAt = [](const Body& body) {
        const Body lower = requireBody(splitBody(body, levelAt(25.0), SplitKeep::Back));
        const auto face = findFaces(lower, planeSignature(Point3D{0_mm, 0_mm, 25.0 * units::mm},
                                                          Direction3D::unitZ()));
        REQUIRE(face.has_value());
        REQUIRE(face->size() == 1);
        return face->front().area.in(units::mm2);
    };

    // The control. A ruled loft's waist is the middle section itself, on the
    // crease between two exact cones, so this measures the cut rather than
    // the loft: exact to rounding. Whatever the smooth loft's figure differs
    // by is therefore the surface's, not the cut's.
    auto ruled = makeLoft(sections, LoftStyle::Ruled, SweptFaceNamer{});
    REQUIRE(ruled.has_value());
    CHECK_THAT(sectionAt(*ruled), WithinRel(pi * 25.0, 1e-15));

    // The smooth loft, measured against the same exact 25 pi. It passes
    // through the section closely but NOT exactly: measured 78.54028940 mm^2
    // against 78.53981634, which is 6.0e-6 relative in area, a radius of
    // 5.0000150 mm against 5 -- 1.5e-5 mm out. The kernel fits one B-spline
    // through the section curves to its own approximation tolerance, and
    // that is the size of the miss in the interior.
    //
    // This is why BetterCAD says a smooth loft passes through its end
    // sections exactly (they are its caps) and through the intermediate ones
    // to within the kernel's approximation, rather than saying it
    // interpolates them. The bound is the measurement, not a wish.
    const double waist = sectionAt(*smooth);
    INFO("waist " << waist << " mm^2, section " << pi * 25.0 << " mm^2");
    CHECK_THAT(waist, WithinRel(pi * 25.0, kSectionThroughSmooth));
    // ... and it is a genuine bound, not a loose one: the miss is real and
    // an order tighter would fail.
    CHECK_FALSE(std::abs(waist - pi * 25.0) <= 1e-7 * pi * 25.0);

    // Yet the volume matches the exact quadratic integral to 3.8e-11
    // (LoftSmooth_ThroughThreeSectionsFollowsTheQuadratic), so the miss is
    // local and cancels when integrated -- an approximating spline
    // oscillating about the curve it was fitted through.

    // The solid really is narrowest there: the half below the waist reaches
    // r 10 at its own end and no further.
    checkPaddedBox(requireBody(splitBody(*smooth, levelAt(25.0), SplitKeep::Back)),
                   {-10, -10, 0}, {10, 10, 25});
}

TEST_CASE("LoftSmooth_WorksBetweenDifferingShapes", "[geometry][loft][p12]") {
    // Smooth interpolation and arc-length matching together: a square, a
    // circle and a square. The correspondence is guarded by the ruled loft
    // of the same sections, which meets its prismatoid volume exactly.
    const double h = 50.0;
    const std::vector<PlanarRegion> sections{section(regular(4, 10.0), 0.0), section(circleLoop(5.0), 25.0),
                                             section(regular(4, 10.0), h)};
    auto ruled = makeLoft(sections, LoftStyle::Ruled, SweptFaceNamer{});
    REQUIRE(ruled.has_value());
    auto smooth = makeLoft(sections, LoftStyle::Smooth, SweptFaceNamer{});
    REQUIRE(smooth.has_value());
    CHECK(smooth->isValid());
    CHECK(smooth->topology().solids == 1);
    // The ruled loft is two prismatoids, each h/6 (A0 + 4 Am + A1).
    const double a0 = polygonArea(4, 10.0);
    const double a1 = pi * 25.0;
    const double m = mixedPolygonCircle(4, 5.0, 10.0);
    CHECK_THAT(volumeMm3(*ruled), WithinRel(2.0 * prismatoid(25.0, a0, a1, m), kRelSpline));
    // The smooth solid is thinner at the waist, as it is for circles.
    CHECK(volumeMm3(*smooth) < volumeMm3(*ruled));
    CHECK(volumeMm3(*smooth) > 0.5 * volumeMm3(*ruled));

    // The same for a chain in which nothing is symmetric: square (R 10) ->
    // circle (r 5) -> hexagon (R 8). The ruled guard still meets its
    // prismatoid volume exactly, which is what proves the correspondence.
    const std::vector<PlanarRegion> uneven{section(regular(4, 10.0), 0.0), section(circleLoop(5.0), 25.0),
                                           section(regular(6, 8.0), 50.0)};
    auto unevenRuled = makeLoft(uneven, LoftStyle::Ruled, SweptFaceNamer{});
    REQUIRE(unevenRuled.has_value());
    auto unevenSmooth = makeLoft(uneven, LoftStyle::Smooth, SweptFaceNamer{});
    REQUIRE(unevenSmooth.has_value());
    CHECK(unevenSmooth->isValid());
    CHECK(unevenSmooth->topology().solids == 1);
    const double lower = prismatoid(25.0, polygonArea(4, 10.0), pi * 25.0, mixedPolygonCircle(4, 5.0, 10.0));
    const double upper = prismatoid(25.0, pi * 25.0, polygonArea(6, 8.0), mixedPolygonCircle(6, 5.0, 8.0));
    CHECK_THAT(volumeMm3(*unevenRuled), WithinRel(lower + upper, kRelSpline));
    CHECK(volumeMm3(*unevenSmooth) < volumeMm3(*unevenRuled));
    CHECK(volumeMm3(*unevenSmooth) > 0.5 * volumeMm3(*unevenRuled));
}
