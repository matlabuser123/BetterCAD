#include "GeometryTestSupport.hpp"

#include <bettercad/core/geometry/Primitives.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <numbers>

using namespace bettercad;
using namespace bettercad::geometry;
using namespace bettercad::literals;
using bettercad::test::checkPoint;
using bettercad::test::kRelTight;
using bettercad::test::requireProperties;
using Catch::Matchers::WithinRel;

namespace {

constexpr double pi = std::numbers::pi;

void checkBounds(const Body& body, double minX, double minY, double minZ, double maxX,
                 double maxY, double maxZ) {
    const auto box = body.boundingBox();
    REQUIRE(box.has_value());
    checkPoint(box->min, minX, minY, minZ);
    checkPoint(box->max, maxX, maxY, maxZ);
}

} // namespace

// P3-003: volume, surface area, bounding box and centre of mass compared with
// analytic solutions over a range of sizes (millimetre to metre scale).

TEST_CASE("Box properties match V = LWH and A = 2(LW + LH + WH)", "[geometry][properties]") {
    const auto [l, w, h] = GENERATE(table<double, double, double>({
        {100.0, 50.0, 20.0},
        {1.0, 1.0, 1.0},
        {0.25, 3.5, 12.125},
        {2500.0, 1200.0, 8.0},
    }));
    CAPTURE(l, w, h);

    const auto box = makeBox(l * units::mm, w * units::mm, h * units::mm);
    REQUIRE(box.has_value());
    const MassProperties p = requireProperties(*box);

    CHECK_THAT(p.volume.in(units::mm3), WithinRel(l * w * h, kRelTight));
    CHECK_THAT(p.surfaceArea.in(units::mm2), WithinRel(2.0 * (l * w + l * h + w * h), kRelTight));
    checkPoint(p.centerOfMass, l / 2.0, w / 2.0, h / 2.0);
    checkBounds(*box, 0.0, 0.0, 0.0, l, w, h);
}

TEST_CASE("Spec example: 100 x 50 x 20 mm box has V = 100000 mm^3", "[geometry][properties]") {
    const auto box = makeBox(100_mm, 50_mm, 20_mm);
    REQUIRE(box.has_value());
    const MassProperties p = requireProperties(*box);
    CHECK_THAT(p.volume.in(units::mm3), WithinRel(100000.0, kRelTight));
    CHECK_THAT(p.volume.in(units::m3), WithinRel(1e-4, kRelTight));
}

TEST_CASE("Cylinder properties match V = pi r^2 h and A = 2 pi r (r + h)",
          "[geometry][properties]") {
    const auto [r, h] = GENERATE(table<double, double>({
        {10.0, 30.0},
        {0.5, 100.0},
        {250.0, 5.0},
    }));
    CAPTURE(r, h);

    const auto cylinder = makeCylinder(r * units::mm, h * units::mm);
    REQUIRE(cylinder.has_value());
    const MassProperties p = requireProperties(*cylinder);

    CHECK_THAT(p.volume.in(units::mm3), WithinRel(pi * r * r * h, kRelTight));
    CHECK_THAT(p.surfaceArea.in(units::mm2), WithinRel(2.0 * pi * r * (r + h), kRelTight));
    checkPoint(p.centerOfMass, 0.0, 0.0, h / 2.0);
    checkBounds(*cylinder, -r, -r, 0.0, r, r, h);
}

TEST_CASE("Sphere properties match V = 4/3 pi r^3 and A = 4 pi r^2", "[geometry][properties]") {
    const double r = GENERATE(1.0, 25.0, 500.0);
    CAPTURE(r);

    const auto sphere = makeSphere(r * units::mm);
    REQUIRE(sphere.has_value());
    const MassProperties p = requireProperties(*sphere);

    CHECK_THAT(p.volume.in(units::mm3), WithinRel(4.0 / 3.0 * pi * r * r * r, kRelTight));
    CHECK_THAT(p.surfaceArea.in(units::mm2), WithinRel(4.0 * pi * r * r, kRelTight));
    checkPoint(p.centerOfMass, 0.0, 0.0, 0.0);
    checkBounds(*sphere, -r, -r, -r, r, r, r);
}

TEST_CASE("Placement moves the centre of mass and bounds, not the volume",
          "[geometry][properties]") {
    SECTION("box with a corner at (10, 20, 30) mm") {
        const auto box = makeBox(Point3D{10_mm, 20_mm, 30_mm}, 100_mm, 50_mm, 20_mm);
        REQUIRE(box.has_value());
        const MassProperties p = requireProperties(*box);
        CHECK_THAT(p.volume.in(units::mm3), WithinRel(100000.0, kRelTight));
        checkPoint(p.centerOfMass, 60.0, 45.0, 40.0);
        checkBounds(*box, 10.0, 20.0, 30.0, 110.0, 70.0, 50.0);
    }
    SECTION("cylinder along +X from (-20, 0, 0) mm") {
        const auto cylinder =
            makeCylinder(Axis3D{Point3D{-20_mm, 0_mm, 0_mm}, Direction3D::unitX()}, 10_mm, 40_mm);
        REQUIRE(cylinder.has_value());
        const MassProperties p = requireProperties(*cylinder);
        CHECK_THAT(p.volume.in(units::mm3), WithinRel(pi * 100.0 * 40.0, kRelTight));
        checkPoint(p.centerOfMass, 0.0, 0.0, 0.0);
        checkBounds(*cylinder, -20.0, -10.0, -10.0, 20.0, 10.0, 10.0);
    }
    SECTION("sphere centred at (1, 2, 3) m") {
        const auto sphere = makeSphere(Point3D{1_m, 2_m, 3_m}, 0.5_m);
        REQUIRE(sphere.has_value());
        const MassProperties p = requireProperties(*sphere);
        CHECK_THAT(p.volume.in(units::m3), WithinRel(4.0 / 3.0 * pi * 0.125, kRelTight));
        checkPoint(p.centerOfMass, 1000.0, 2000.0, 3000.0);
        checkBounds(*sphere, 500.0, 1500.0, 2500.0, 1500.0, 2500.0, 3500.0);
    }
}

TEST_CASE("Integration error estimates are reported and small", "[geometry][properties]") {
    const auto sphere = makeSphere(25_mm);
    REQUIRE(sphere.has_value());
    const MassProperties p = requireProperties(*sphere);
    CHECK(p.volumeRelativeError >= 0.0);
    CHECK(p.volumeRelativeError < 1e-10);
    CHECK(p.areaRelativeError < 1e-10);
}
