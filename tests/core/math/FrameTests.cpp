#include "TestHelpers.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/math/BoundingBox.hpp>
#include <bettercad/core/math/Direction.hpp>
#include <bettercad/core/math/Frame.hpp>
#include <bettercad/core/math/Point.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <limits>

using namespace bettercad;
using namespace bettercad::literals;
using bettercad::test::errorCode;
using Catch::Matchers::WithinAbs;

namespace {

constexpr double kEps = 1e-15;

void checkDirection(const Direction3D& d, double x, double y, double z) {
    CHECK_THAT(d.x(), WithinAbs(x, kEps));
    CHECK_THAT(d.y(), WithinAbs(y, kEps));
    CHECK_THAT(d.z(), WithinAbs(z, kEps));
}

void checkPointMm(const Point3D& p, double x, double y, double z, double tolMm = 1e-12) {
    CHECK_THAT(p.x.in(units::mm), WithinAbs(x, tolMm));
    CHECK_THAT(p.y.in(units::mm), WithinAbs(y, tolMm));
    CHECK_THAT(p.z.in(units::mm), WithinAbs(z, tolMm));
}

// Orthonormal and right-handed: |X| = |Y| = |N| = 1, X.Y = 0, X x Y = N.
void checkOrthonormal(const Frame3D& frame) {
    for (const Direction3D& d : {frame.xAxis(), frame.yAxis(), frame.normal()}) {
        CHECK_THAT(std::hypot(d.x(), d.y(), d.z()), WithinAbs(1.0, kEps));
    }
    CHECK_THAT(frame.xAxis().dot(frame.yAxis()), WithinAbs(0.0, kEps));
    CHECK_THAT(frame.xAxis().dot(frame.normal()), WithinAbs(0.0, kEps));
    const auto z = frame.xAxis().cross(frame.yAxis());
    REQUIRE(z.has_value());
    checkDirection(*z, frame.normal().x(), frame.normal().y(), frame.normal().z());
}

} // namespace

TEST_CASE("The global XY plane maps sketch coordinates unchanged", "[math][frame]") {
    const Frame3D xy = Frame3D::xy();
    checkOrthonormal(xy);
    CHECK(xy.origin() == Point3D{});
    CHECK(xy.xAxis() == Direction3D::unitX());
    CHECK(xy.yAxis() == Direction3D::unitY());
    CHECK(xy.normal() == Direction3D::unitZ());

    const Point3D global = xy.toGlobal(Point2D{30_mm, 40_mm});
    CHECK(global == Point3D{30_mm, 40_mm, 0_mm});
    CHECK(xy.toLocal(Point3D{30_mm, 40_mm, 7_mm}) == Point2D{30_mm, 40_mm});
    CHECK(xy.signedDistance(Point3D{30_mm, 40_mm, 7_mm}) == 7_mm);
}

TEST_CASE("The XZ and YZ planes are right-handed", "[math][frame]") {
    SECTION("XZ: local (u, v) -> (u, 0, v), normal -Y") {
        const Frame3D xz = Frame3D::xz();
        checkOrthonormal(xz);
        checkDirection(xz.normal(), 0.0, -1.0, 0.0);
        CHECK(xz.toGlobal(Point2D{3_mm, 4_mm}) == Point3D{3_mm, 0_mm, 4_mm});
    }
    SECTION("YZ: local (u, v) -> (0, u, v), normal +X") {
        const Frame3D yz = Frame3D::yz();
        checkOrthonormal(yz);
        checkDirection(yz.normal(), 1.0, 0.0, 0.0);
        CHECK(yz.toGlobal(Point2D{3_mm, 4_mm}) == Point3D{0_mm, 3_mm, 4_mm});
    }
}

TEST_CASE("A general frame is orthonormal and round-trips points", "[math][frame]") {
    const auto normal = Direction3D::fromComponents(1.0, 1.0, 1.0);
    REQUIRE(normal.has_value());
    const auto frame = Frame3D::create(Point3D{10_mm, 20_mm, 30_mm}, *normal, Direction3D::unitX());
    REQUIRE(frame.has_value());
    checkOrthonormal(*frame);
    CHECK(frame->toGlobal(Point2D{}) == Point3D{10_mm, 20_mm, 30_mm});

    const auto [u, v] = GENERATE(table<double, double>({{0.0, 0.0}, {12.5, -3.0}, {-250.0, 1000.0}}));
    CAPTURE(u, v);
    const Point2D local{u * units::mm, v * units::mm};
    const Point3D global = frame->toGlobal(local);
    const Point2D back = frame->toLocal(global);
    CHECK_THAT(back.x.in(units::mm), WithinAbs(u, 1e-10));
    CHECK_THAT(back.y.in(units::mm), WithinAbs(v, 1e-10));
    CHECK_THAT(frame->signedDistance(global).in(units::mm), WithinAbs(0.0, 1e-10));
}

TEST_CASE("Frame creation projects the X direction into the plane", "[math][frame]") {
    const auto tilted = Direction3D::fromComponents(1.0, 0.0, 1.0);
    REQUIRE(tilted.has_value());
    const auto frame = Frame3D::create(Point3D{}, Direction3D::unitZ(), *tilted);
    REQUIRE(frame.has_value());
    checkDirection(frame->xAxis(), 1.0, 0.0, 0.0);
    checkDirection(frame->yAxis(), 0.0, 1.0, 0.0);
    // A point above the plane projects straight down and keeps its offset.
    checkPointMm(frame->toGlobal(frame->toLocal(Point3D{5_mm, 6_mm, 9_mm})), 5.0, 6.0, 0.0);
    CHECK(frame->signedDistance(Point3D{5_mm, 6_mm, 9_mm}) == 9_mm);
}

TEST_CASE("Invalid frames are rejected", "[math][frame]") {
    CHECK(errorCode(Frame3D::create(Point3D{}, Direction3D::unitZ(), Direction3D::unitZ())) ==
          ErrorCode::InvalidArgument);
    CHECK(errorCode(Frame3D::create(Point3D{}, Direction3D::unitZ(), Direction3D::unitZ().reversed())) ==
          ErrorCode::InvalidArgument);
    const Length nan = Length::fromSi(std::numeric_limits<double>::quiet_NaN());
    CHECK(errorCode(Frame3D::create(Point3D{nan, 0_mm, 0_mm}, Direction3D::unitZ(),
                                    Direction3D::unitX())) == ErrorCode::InvalidArgument);
}

TEST_CASE("Direction products", "[math][direction]") {
    const auto z = Direction3D::unitX().cross(Direction3D::unitY());
    REQUIRE(z.has_value());
    CHECK(*z == Direction3D::unitZ());
    CHECK_FALSE(Direction3D::unitX().cross(Direction3D::unitX().reversed()).has_value());
    CHECK(Direction3D::unitX().dot(Direction3D::unitY()) == 0.0);
    CHECK(Direction3D::unitZ().reversed().z() == -1.0);
}

TEST_CASE("Points and bounding boxes", "[math][point]") {
    CHECK_THAT(distance(Point2D{0_mm, 0_mm}, Point2D{3_mm, 4_mm}).in(units::mm),
               Catch::Matchers::WithinULP(5.0, 1));
    CHECK_THAT(distance(Point3D{0_mm, 0_mm, 0_mm}, Point3D{2_mm, 3_mm, 6_mm}).in(units::mm),
               Catch::Matchers::WithinULP(7.0, 1));
    CHECK(midpoint(Point2D{0_mm, 0_mm}, Point2D{10_mm, 4_mm}) == Point2D{5_mm, 2_mm});

    BoundingBox2D box = BoundingBox2D::around(Point2D{1_mm, 1_mm});
    box.include(Point2D{-2_mm, 5_mm});
    box.include(BoundingBox2D{{0_mm, -1_mm}, {3_mm, 0_mm}});
    CHECK(box == BoundingBox2D{{-2_mm, -1_mm}, {3_mm, 5_mm}});
    CHECK(box.width() == 5_mm);
    CHECK(box.height() == 6_mm);
    CHECK(box.contains(Point2D{0_mm, 0_mm}));
    CHECK_FALSE(box.contains(Point2D{4_mm, 0_mm}));
}
