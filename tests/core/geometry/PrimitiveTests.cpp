#include "GeometryTestSupport.hpp"
#include "TestHelpers.hpp"

#include <bettercad/core/geometry/Kernel.hpp>
#include <bettercad/core/geometry/Primitives.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <limits>
#include <string>

// The geometry API must not expose Open CASCADE: consumers of the public
// headers compile without OCCT include paths (OCCT is a private dependency).
#if __has_include(<TopoDS_Shape.hxx>)
#error "Open CASCADE headers must not be visible to users of the geometry API"
#endif

using namespace bettercad;
using namespace bettercad::geometry;
using namespace bettercad::literals;
using bettercad::test::errorCode;

// --- P3-001: kernel integration ---------------------------------------------------

TEST_CASE("The geometry kernel is Open CASCADE 8.0", "[geometry][kernel]") {
    const KernelInfo kernel = geometryKernel();
    CHECK(kernel.name == "Open CASCADE Technology");
    CHECK_THAT(std::string{kernel.version}, Catch::Matchers::StartsWith("8.0."));
}

TEST_CASE("A default body is empty", "[geometry][body]") {
    const Body body;
    CHECK(body.isEmpty());
    CHECK_FALSE(body.isValid());
    CHECK(body.topology() == TopologySummary{});
    CHECK(errorCode(body.massProperties()) == ErrorCode::FailedPrecondition);
    CHECK(errorCode(body.boundingBox()) == ErrorCode::FailedPrecondition);
}

// --- P3-002: primitive solids -----------------------------------------------------

TEST_CASE("Spec usage: makeBox(100_mm, 50_mm, 20_mm)", "[geometry][primitives]") {
    const auto body = makeBox(100_mm, 50_mm, 20_mm);
    REQUIRE(body.has_value());
    CHECK_FALSE(body->isEmpty());
    CHECK(body->isValid());
}

TEST_CASE("Primitives have the expected topology", "[geometry][primitives]") {
    SECTION("box") {
        const auto box = makeBox(100_mm, 50_mm, 20_mm);
        REQUIRE(box.has_value());
        CHECK(box->topology() == TopologySummary{.solids = 1, .shells = 1, .faces = 6, .edges = 12, .vertices = 8});
    }
    SECTION("cylinder: lateral, top and bottom faces; two circles and a seam") {
        const auto cylinder = makeCylinder(10_mm, 30_mm);
        REQUIRE(cylinder.has_value());
        CHECK(cylinder->isValid());
        CHECK(cylinder->topology() == TopologySummary{.solids = 1, .shells = 1, .faces = 3, .edges = 3, .vertices = 2});
    }
    SECTION("sphere: one face; a seam and two degenerate pole edges") {
        const auto sphere = makeSphere(25_mm);
        REQUIRE(sphere.has_value());
        CHECK(sphere->isValid());
        CHECK(sphere->topology() == TopologySummary{.solids = 1, .shells = 1, .faces = 1, .edges = 3, .vertices = 2});
    }
}

TEST_CASE("Primitive sizes must be positive, finite and above kernel precision",
          "[geometry][primitives]") {
    const Length nan = Length::fromSi(std::numeric_limits<double>::quiet_NaN());
    const Length infinity = Length::fromSi(std::numeric_limits<double>::infinity());

    CHECK(errorCode(makeBox(0_mm, 50_mm, 20_mm)) == ErrorCode::InvalidArgument);
    CHECK(errorCode(makeBox(-(1_mm), 50_mm, 20_mm)) == ErrorCode::InvalidArgument);
    CHECK(errorCode(makeBox(1_mm, nan, 20_mm)) == ErrorCode::InvalidArgument);
    CHECK(errorCode(makeBox(1_mm, 1_mm, infinity)) == ErrorCode::InvalidArgument);
    CHECK(errorCode(makeBox(Length::fromSi(1e-12), 1_mm, 1_mm)) == ErrorCode::InvalidArgument);
    CHECK(errorCode(makeBox(Point3D{nan, 0_mm, 0_mm}, 1_mm, 1_mm, 1_mm)) == ErrorCode::InvalidArgument);
    CHECK(errorCode(makeCylinder(0_mm, 10_mm)) == ErrorCode::InvalidArgument);
    CHECK(errorCode(makeCylinder(10_mm, -(5_mm))) == ErrorCode::InvalidArgument);
    CHECK(errorCode(makeSphere(0_mm)) == ErrorCode::InvalidArgument);
    CHECK(errorCode(makeSphere(Point3D{0_mm, infinity, 0_mm}, 1_mm)) == ErrorCode::InvalidArgument);

    const auto small = makeBox(1_um, 1_um, 1_um);
    REQUIRE(small.has_value());
    CHECK(small->isValid());
}

TEST_CASE("Directions are normalized and reject zero vectors", "[geometry][types]") {
    const auto d = Direction3D::fromComponents(3.0, 0.0, 4.0);
    REQUIRE(d.has_value());
    CHECK_THAT(d->x(), Catch::Matchers::WithinULP(0.6, 1));
    CHECK_THAT(d->z(), Catch::Matchers::WithinULP(0.8, 1));
    CHECK_FALSE(Direction3D::fromComponents(0.0, 0.0, 0.0).has_value());
    CHECK_FALSE(
        Direction3D::fromComponents(std::numeric_limits<double>::quiet_NaN(), 1.0, 0.0).has_value());
    CHECK(Direction3D::unitZ() == *Direction3D::fromComponents(0.0, 0.0, 2.0));
}

TEST_CASE("Bodies are immutable values that share their shape", "[geometry][body]") {
    const auto box = makeBox(10_mm, 20_mm, 30_mm);
    REQUIRE(box.has_value());
    const Body copy = *box; // cheap copy
    CHECK(copy.topology() == box->topology());
    CHECK(bettercad::test::requireProperties(copy).volume ==
          bettercad::test::requireProperties(*box).volume);
}
