#include "TestHelpers.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/drawing/Section.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <numbers>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using drawing::contains;
using drawing::crossings;
using drawing::twiceSignedArea;

// P14-VIEW-002: the 2D primitives hatch and cropping rest on.
//
// None of this existed before: core/math had Point2D and BoundingBox2D and
// nothing else 2D. Every expected value here is a closed form computed by
// hand -- the shoelace area of a square is its side squared, a regular
// polygon's area is (1/2) n R^2 sin(2 pi / n) -- never a value read back out
// of the code under test.
namespace {

std::vector<Point2D> rectangle(double x0, double y0, double x1, double y1) {
    return {{x0 * units::mm, y0 * units::mm},
            {x1 * units::mm, y0 * units::mm},
            {x1 * units::mm, y1 * units::mm},
            {x0 * units::mm, y1 * units::mm}};
}

/// A regular n-gon of circumradius r, counter-clockwise from angle 0.
std::vector<Point2D> regular(std::size_t n, double r) {
    std::vector<Point2D> loop;
    for (std::size_t i = 0; i < n; ++i) {
        const double a = 2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(n);
        loop.push_back({r * std::cos(a) * units::mm, r * std::sin(a) * units::mm});
    }
    return loop;
}

Point2D mm(double x, double y) { return {x * units::mm, y * units::mm}; }

} // namespace

// --- Area ----------------------------------------------------------------------------

TEST_CASE("Section2d_ShoelaceAreaMatchesClosedForms", "[drawing][section][p14]") {
    // A 40 x 25 rectangle has area 1000 mm^2, so twice the signed area is
    // 2000, positive because the points wind counter-clockwise.
    const double toMm2 = 1.0e6; // SI square metres to mm^2
    CHECK_THAT(twiceSignedArea(rectangle(0, 0, 40, 25)) * toMm2, WithinRel(2000.0, 1e-12));

    // Reversed, it is the same magnitude and the other sign. That sign is
    // what tells an outer loop from a void.
    std::vector<Point2D> backwards = rectangle(0, 0, 40, 25);
    std::ranges::reverse(backwards);
    CHECK_THAT(twiceSignedArea(backwards) * toMm2, WithinRel(-2000.0, 1e-12));

    // A regular n-gon: area = (1/2) n R^2 sin(2 pi / n).
    for (const std::size_t n : {3u, 5u, 8u, 64u}) {
        INFO(n << "-gon");
        const double r = 10.0;
        const double expected =
            0.5 * static_cast<double>(n) * r * r * std::sin(2.0 * std::numbers::pi / static_cast<double>(n));
        CHECK_THAT(0.5 * twiceSignedArea(regular(n, r)) * toMm2, WithinRel(expected, 1e-12));
    }

    // Degenerate loops have no area rather than an undefined one.
    CHECK(twiceSignedArea({}) == 0.0);
    CHECK(twiceSignedArea({mm(0, 0)}) == 0.0);
    CHECK(twiceSignedArea({mm(0, 0), mm(1, 1)}) == 0.0);
}

TEST_CASE("Section2d_AreaIsIndependentOfWhereTheLoopStarts", "[drawing][section][p14]") {
    // A rotation of the point list is the same polygon and must give the
    // same area, or a loop's area would depend on which vertex the kernel
    // happened to hand back first.
    const std::vector<Point2D> base = regular(7, 12.0);
    const double expected = twiceSignedArea(base);
    for (std::size_t shift = 1; shift < base.size(); ++shift) {
        std::vector<Point2D> rotated(base.begin() + static_cast<long>(shift), base.end());
        rotated.insert(rotated.end(), base.begin(), base.begin() + static_cast<long>(shift));
        INFO("starting at vertex " << shift);
        CHECK_THAT(twiceSignedArea(rotated), WithinRel(expected, 1e-12));
    }
}

// --- Point in polygon ----------------------------------------------------------------

TEST_CASE("Section2d_ContainsAgreesWithTheObviousAnswer", "[drawing][section][p14]") {
    const std::vector<Point2D> box = rectangle(0, 0, 40, 25);

    CHECK(contains(box, mm(20, 12)));   // middle
    CHECK(contains(box, mm(0.5, 0.5))); // just inside a corner
    CHECK(contains(box, mm(39.5, 24.5)));

    CHECK_FALSE(contains(box, mm(-1, 12)));  // left of it
    CHECK_FALSE(contains(box, mm(41, 12)));  // right of it
    CHECK_FALSE(contains(box, mm(20, -1)));  // below
    CHECK_FALSE(contains(box, mm(20, 26)));  // above
    CHECK_FALSE(contains(box, mm(-5, -5)));  // clear of it entirely

    // Winding direction must not change the answer: a void's loop runs the
    // other way and a point inside it is still inside it.
    std::vector<Point2D> backwards = box;
    std::ranges::reverse(backwards);
    CHECK(contains(backwards, mm(20, 12)));
    CHECK_FALSE(contains(backwards, mm(41, 12)));
}

TEST_CASE("Section2d_ContainsHandlesAConcaveLoop", "[drawing][section][p14]") {
    // An L. The notch is the part a convex test would get wrong.
    const std::vector<Point2D> ell{mm(0, 0),  mm(40, 0),  mm(40, 10),
                                   mm(10, 10), mm(10, 30), mm(0, 30)};
    CHECK(contains(ell, mm(5, 5)));    // in the foot
    CHECK(contains(ell, mm(30, 5)));   // in the arm
    CHECK(contains(ell, mm(5, 20)));   // in the upright
    CHECK_FALSE(contains(ell, mm(30, 20))); // in the notch -- outside the L
    CHECK_FALSE(contains(ell, mm(20, 25)));
}

TEST_CASE("Section2d_AVertexAtTheTestHeightIsCountedOnce", "[drawing][section][p14]") {
    // A ray leaving a point at exactly a vertex's height is the classic
    // double-count. The half-open rule on y is what prevents it.
    const std::vector<Point2D> diamond{mm(0, 10), mm(10, 0), mm(20, 10), mm(10, 20)};
    CHECK(contains(diamond, mm(10, 10)));    // centre, level with two vertices
    CHECK_FALSE(contains(diamond, mm(-5, 10)));
    CHECK_FALSE(contains(diamond, mm(25, 10)));
    // And level with the top and bottom vertices.
    CHECK_FALSE(contains(diamond, mm(-5, 0)));
    CHECK_FALSE(contains(diamond, mm(-5, 20)));
}

// --- Line crossings ------------------------------------------------------------------

TEST_CASE("Section2d_CrossingsFindsBothSidesOfALoop", "[drawing][section][p14]") {
    const std::vector<Point2D> box = rectangle(0, 0, 40, 25);
    // A horizontal line at y = 12, from x = -10 to x = 50: 60 mm long, so the
    // crossings at x = 0 and x = 40 are at t = 10/60 and t = 50/60.
    const auto found = crossings(box, mm(-10, 12), mm(50, 12));
    REQUIRE(found.size() == 2);
    CHECK_THAT(found[0], WithinAbs(10.0 / 60.0, 1e-12));
    CHECK_THAT(found[1], WithinAbs(50.0 / 60.0, 1e-12));
    // Sorted, so the pair brackets the material.
    CHECK(found[0] < found[1]);
}

TEST_CASE("Section2d_CrossingsOfAConcaveLoopComeInPairs", "[drawing][section][p14]") {
    // A line through both arms of an L crosses four edges, so the material is
    // between the first pair and the second -- which is exactly what hatch
    // clipping needs.
    const std::vector<Point2D> ell{mm(0, 0),  mm(40, 0),  mm(40, 10),
                                   mm(10, 10), mm(10, 30), mm(0, 30)};
    const auto low = crossings(ell, mm(-10, 5), mm(50, 5));
    CHECK(low.size() == 2); // through the foot and the arm as one span

    const auto high = crossings(ell, mm(-10, 20), mm(50, 20));
    REQUIRE(high.size() == 2); // only the upright
    // x = 0 and x = 10 on a 60 mm line from -10.
    CHECK_THAT(high[0], WithinAbs(10.0 / 60.0, 1e-12));
    CHECK_THAT(high[1], WithinAbs(20.0 / 60.0, 1e-12));
}

TEST_CASE("Section2d_CrossingsIgnoresALineThatMissesAndAParallelEdge",
          "[drawing][section][p14]") {
    const std::vector<Point2D> box = rectangle(0, 0, 40, 25);
    CHECK(crossings(box, mm(-10, 40), mm(50, 40)).empty()); // above it
    CHECK(crossings(box, mm(-10, -5), mm(50, -5)).empty()); // below it
    // A zero-length line has no direction and so no crossings.
    CHECK(crossings(box, mm(10, 10), mm(10, 10)).empty());
    // Degenerate loops cross nothing.
    CHECK(crossings({}, mm(-10, 5), mm(50, 5)).empty());
    CHECK(crossings({mm(0, 0), mm(1, 0)}, mm(-10, 0), mm(50, 0)).empty());
}

TEST_CASE("Section2d_CrossingsAreCountedOnceAtASharedVertex", "[drawing][section][p14]") {
    // A line through a corner where two edges meet must not count both. The
    // half-open rule on the edge parameter is what does it.
    const std::vector<Point2D> diamond{mm(0, 10), mm(10, 0), mm(20, 10), mm(10, 20)};
    const auto through = crossings(diamond, mm(-10, 10), mm(30, 10));
    // Exactly two: the left vertex and the right one, each once.
    CHECK(through.size() == 2);
}

// --- Validation ----------------------------------------------------------------------

TEST_CASE("Section2d_HatchSettingsAreValidated", "[drawing][section][p14]") {
    CHECK(drawing::validate(drawing::HatchSettings{}).has_value());

    drawing::HatchSettings bad;
    bad.spacing = 0_mm;
    CHECK(errorCode(drawing::validate(bad)) == ErrorCode::InvalidArgument);
    bad.spacing = -1_mm;
    CHECK(errorCode(drawing::validate(bad)) == ErrorCode::InvalidArgument);

    bad = drawing::HatchSettings{};
    bad.pattern = "crosshatch";
    CHECK(errorCode(drawing::validate(bad)) == ErrorCode::InvalidArgument);

    bad = drawing::HatchSettings{};
    bad.angle = Angle::fromSi(std::numeric_limits<double>::quiet_NaN());
    CHECK(errorCode(drawing::validate(bad)) == ErrorCode::InvalidArgument);
}

TEST_CASE("Section2d_CuttingPlanesAreValidated", "[drawing][section][p14]") {
    drawing::CuttingPlane plane;
    CHECK(drawing::validate(plane).has_value());
    CHECK(drawing::frameOf(plane).has_value());

    SECTION("a normal parallel to the reference has no frame") {
        plane.normal = Direction3D::unitX();
        plane.reference = Direction3D::unitX();
        CHECK_FALSE(drawing::validate(plane).has_value());
    }
    SECTION("a full section takes no legs and no split") {
        plane.legs.push_back({10_mm, 5_mm});
        CHECK(errorCode(drawing::validate(plane)) == ErrorCode::InvalidArgument);
        plane.legs.clear();
        plane.splitAt = 5_mm;
        CHECK(errorCode(drawing::validate(plane)) == ErrorCode::InvalidArgument);
    }
    SECTION("an offset section needs at least one leg") {
        plane.kind = drawing::SectionKind::Offset;
        CHECK(errorCode(drawing::validate(plane)) == ErrorCode::InvalidArgument);
        plane.legs.push_back({10_mm, 5_mm});
        CHECK(drawing::validate(plane).has_value());
    }
    SECTION("an offset section's legs must step forward") {
        plane.kind = drawing::SectionKind::Offset;
        plane.legs = {{10_mm, 5_mm}, {10_mm, 8_mm}};
        CHECK(errorCode(drawing::validate(plane)) == ErrorCode::InvalidArgument);
        plane.legs = {{10_mm, 5_mm}, {5_mm, 8_mm}};
        CHECK(errorCode(drawing::validate(plane)) == ErrorCode::InvalidArgument);
        plane.legs = {{10_mm, 5_mm}, {20_mm, 8_mm}};
        CHECK(drawing::validate(plane).has_value());
    }
    SECTION("a half section takes a split and no legs") {
        plane.kind = drawing::SectionKind::Half;
        plane.splitAt = 5_mm;
        CHECK(drawing::validate(plane).has_value());
        plane.legs.push_back({10_mm, 5_mm});
        CHECK(errorCode(drawing::validate(plane)) == ErrorCode::InvalidArgument);
    }
    SECTION("a non-finite origin is refused") {
        plane.origin = Point3D{Length::fromSi(std::numeric_limits<double>::infinity()), 0_mm, 0_mm};
        CHECK(errorCode(drawing::validate(plane)) == ErrorCode::InvalidArgument);
    }
}
