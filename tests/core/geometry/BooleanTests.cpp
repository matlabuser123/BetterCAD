#include "GeometryTestSupport.hpp"
#include "TestHelpers.hpp"

#include <bettercad/core/geometry/Booleans.hpp>
#include <bettercad/core/geometry/Primitives.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <bit>
#include <cstdint>
#include <numbers>

using namespace bettercad;
using namespace bettercad::geometry;
using namespace bettercad::literals;
using bettercad::test::checkPoint;
using bettercad::test::errorCode;
using bettercad::test::kRelApproximatedIntersection;
using bettercad::test::kRelCurvedIntersection;
using bettercad::test::kRelTight;
using bettercad::test::requireProperties;
using Catch::Matchers::WithinRel;

namespace {

constexpr double pi = std::numbers::pi;

Body require(const Result<Body>& body) {
    REQUIRE(body.has_value());
    return *body;
}

double volumeMm3(const Body& body) {
    return requireProperties(body).volume.in(units::mm3);
}

} // namespace

// --- P3-004: acceptance example -----------------------------------------------------

TEST_CASE("Box minus a through cylinder is a valid solid with V = LWH - pi r^2 H",
          "[geometry][booleans]") {
    const Body box = require(makeBox(100_mm, 50_mm, 20_mm));
    // The cylinder overshoots both faces so that no coplanar faces remain.
    const Body tool = require(makeCylinder(Axis3D{Point3D{50_mm, 25_mm, -1_mm}}, 10_mm, 22_mm));

    const Body result = require(booleanDifference(box, tool));

    CHECK(result.isValid());
    CHECK(result.topology() ==
          TopologySummary{.solids = 1, .shells = 1, .faces = 7, .edges = 15, .vertices = 10});
    const MassProperties p = requireProperties(result);
    CHECK_THAT(p.volume.in(units::mm3), WithinRel(100.0 * 50.0 * 20.0 - pi * 10.0 * 10.0 * 20.0, kRelTight));
    // Outer area plus the hole's wall, minus the two hole openings.
    const double area = 2.0 * (100.0 * 50.0 + 100.0 * 20.0 + 50.0 * 20.0) - 2.0 * pi * 100.0 +
                        2.0 * pi * 10.0 * 20.0;
    CHECK_THAT(p.surfaceArea.in(units::mm2), WithinRel(area, kRelTight));
    checkPoint(p.centerOfMass, 50.0, 25.0, 10.0); // symmetric hole
    const auto bounds = result.boundingBox();
    REQUIRE(bounds.has_value());
    checkPoint(bounds->min, 0.0, 0.0, 0.0);
    checkPoint(bounds->max, 100.0, 50.0, 20.0);
}

TEST_CASE("An off-centre hole moves the centre of mass as predicted", "[geometry][booleans]") {
    const Body box = require(makeBox(100_mm, 50_mm, 20_mm));
    const Body tool = require(makeCylinder(Axis3D{Point3D{25_mm, 25_mm, -1_mm}}, 10_mm, 22_mm));
    const Body result = require(booleanDifference(box, tool));

    const double boxVolume = 100000.0;
    const double holeVolume = pi * 100.0 * 20.0;
    const double expectedX = (boxVolume * 50.0 - holeVolume * 25.0) / (boxVolume - holeVolume);
    checkPoint(requireProperties(result).centerOfMass, expectedX, 25.0, 10.0);
}

// --- Union, difference, intersection -------------------------------------------------

TEST_CASE("Overlapping boxes: union, difference and intersection volumes", "[geometry][booleans]") {
    const Body a = require(makeBox(10_mm, 10_mm, 10_mm));
    const Body b = require(makeBox(Point3D{5_mm, 0_mm, 0_mm}, 10_mm, 10_mm, 10_mm));

    const Body united = require(booleanUnion(a, b));
    const Body difference = require(booleanDifference(a, b));
    const Body intersection = require(booleanIntersection(a, b));

    CHECK_THAT(volumeMm3(united), WithinRel(1500.0, kRelTight));
    CHECK_THAT(volumeMm3(difference), WithinRel(500.0, kRelTight));
    CHECK_THAT(volumeMm3(intersection), WithinRel(500.0, kRelTight));
    // Coplanar faces are merged: each result is a plain box.
    for (const Body* body : {&united, &difference, &intersection}) {
        CHECK(body->isValid());
        CHECK(body->topology() ==
              TopologySummary{.solids = 1, .shells = 1, .faces = 6, .edges = 12, .vertices = 8});
    }
    checkPoint(requireProperties(united).centerOfMass, 7.5, 5.0, 5.0);
    checkPoint(requireProperties(difference).centerOfMass, 2.5, 5.0, 5.0);
    checkPoint(requireProperties(intersection).centerOfMass, 7.5, 5.0, 5.0);
}

TEST_CASE("Inclusion-exclusion holds: V(A u B) = V(A) + V(B) - V(A n B)", "[geometry][booleans]") {
    // An off-axis sphere and cylinder meet in a general quartic curve, which
    // the kernel approximates; see kRelApproximatedIntersection.
    const Body sphere = require(makeSphere(Point3D{5_mm, 0_mm, 0_mm}, 8_mm));
    const Body cylinder = require(makeCylinder(Axis3D{Point3D{0_mm, 0_mm, -10_mm}}, 6_mm, 20_mm));

    const Body unitedBody = require(booleanUnion(sphere, cylinder));
    const Body commonBody = require(booleanIntersection(sphere, cylinder));
    CHECK(unitedBody.isValid());
    CHECK(commonBody.isValid());
    const double united = volumeMm3(unitedBody);
    const double common = volumeMm3(commonBody);
    const double sum = volumeMm3(sphere) + volumeMm3(cylinder);
    CHECK_THAT(united, WithinRel(sum - common, kRelApproximatedIntersection));
    // A - B and B - A partition the union together with A n B.
    const double onlySphere = volumeMm3(require(booleanDifference(sphere, cylinder)));
    const double onlyCylinder = volumeMm3(require(booleanDifference(cylinder, sphere)));
    CHECK_THAT(onlySphere + onlyCylinder + common, WithinRel(united, kRelApproximatedIntersection));
}

TEST_CASE("Sphere intersected with an octant box is one eighth of the sphere",
          "[geometry][booleans]") {
    const Body sphere = require(makeSphere(10_mm));
    const Body octant = require(makeBox(20_mm, 20_mm, 20_mm));
    const Body result = require(booleanIntersection(sphere, octant));

    CHECK(result.isValid());
    const MassProperties p = requireProperties(result);
    CHECK_THAT(p.volume.in(units::mm3), WithinRel(4.0 / 3.0 * pi * 1000.0 / 8.0, kRelTight));
    // Centroid of a solid octant: 3r/8 along each axis.
    checkPoint(p.centerOfMass, 3.75, 3.75, 3.75);
}

TEST_CASE("Two perpendicular cylinders intersect in a Steinmetz solid, V = 16 r^3 / 3",
          "[geometry][booleans]") {
    const Body alongX =
        require(makeCylinder(Axis3D{Point3D{-20_mm, 0_mm, 0_mm}, Direction3D::unitX()}, 10_mm, 40_mm));
    const Body alongY =
        require(makeCylinder(Axis3D{Point3D{0_mm, -20_mm, 0_mm}, Direction3D::unitY()}, 10_mm, 40_mm));
    const Body result = require(booleanIntersection(alongX, alongY));

    CHECK(result.isValid());
    CHECK_THAT(volumeMm3(result), WithinRel(16.0 / 3.0 * 1000.0, kRelCurvedIntersection));
    checkPoint(requireProperties(result).centerOfMass, 0.0, 0.0, 0.0);
}

TEST_CASE("Disjoint bodies: union keeps both solids, intersection is empty", "[geometry][booleans]") {
    const Body a = require(makeBox(10_mm, 10_mm, 10_mm));
    const Body b = require(makeBox(Point3D{20_mm, 0_mm, 0_mm}, 5_mm, 5_mm, 5_mm));

    const Body united = require(booleanUnion(a, b));
    CHECK(united.isValid());
    CHECK(united.topology().solids == 2);
    CHECK_THAT(volumeMm3(united), WithinRel(1125.0, kRelTight));

    const Body intersection = require(booleanIntersection(a, b));
    CHECK(intersection.isEmpty());
    CHECK(errorCode(intersection.massProperties()) == ErrorCode::FailedPrecondition);

    const Body difference = require(booleanDifference(a, b));
    CHECK_THAT(volumeMm3(difference), WithinRel(1000.0, kRelTight));
}

TEST_CASE("Subtracting an enclosing body leaves nothing", "[geometry][booleans]") {
    const Body small = require(makeBox(Point3D{1_mm, 1_mm, 1_mm}, 2_mm, 2_mm, 2_mm));
    const Body large = require(makeBox(10_mm, 10_mm, 10_mm));
    CHECK(require(booleanDifference(small, large)).isEmpty());
}

TEST_CASE("Boolean operations are deterministic and leave their inputs unchanged",
          "[geometry][booleans]") {
    const Body box = require(makeBox(100_mm, 50_mm, 20_mm));
    const Body tool = require(makeCylinder(Axis3D{Point3D{50_mm, 25_mm, -1_mm}}, 10_mm, 22_mm));
    const double boxVolumeBefore = volumeMm3(box);

    const Body first = require(booleanDifference(box, tool));
    const Body second = require(booleanDifference(box, tool));

    CHECK(std::bit_cast<std::uint64_t>(volumeMm3(first)) ==
          std::bit_cast<std::uint64_t>(volumeMm3(second)));
    CHECK(first.topology() == second.topology());
    CHECK(volumeMm3(box) == boxVolumeBefore);
    CHECK(box.topology().faces == 6);
}

TEST_CASE("Boolean operands must not be empty", "[geometry][booleans]") {
    const Body box = require(makeBox(1_mm, 1_mm, 1_mm));
    CHECK(errorCode(booleanUnion(box, Body{})) == ErrorCode::InvalidArgument);
    CHECK(errorCode(booleanDifference(Body{}, box)) == ErrorCode::InvalidArgument);
    CHECK(errorCode(booleanIntersection(Body{}, Body{})) == ErrorCode::InvalidArgument);
}
