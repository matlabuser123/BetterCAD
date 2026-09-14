#include "GeometryTestSupport.hpp"
#include "TestHelpers.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/geometry/Booleans.hpp>
#include <bettercad/core/geometry/Edges.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/core/geometry/Hole.hpp>
#include <bettercad/core/geometry/Primitives.hpp>
#include <bettercad/core/geometry/Transform.hpp>
#include <bettercad/core/math/Vector.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <limits>

using namespace bettercad;
using namespace bettercad::geometry;
using namespace bettercad::literals;
using bettercad::test::checkPoint;
using bettercad::test::errorCode;
using bettercad::test::kPositionToleranceMm;
using bettercad::test::kRelTight;
using bettercad::test::requireProperties;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

Translation3D mm(double x, double y, double z) {
    return {x * units::mm, y * units::mm, z * units::mm};
}

std::size_t facesOn(const Body& body, const FaceSignature& face) {
    const auto found = findFaces(body, face);
    REQUIRE(found.has_value());
    return found->size();
}

} // namespace

TEST_CASE("Transform_TranslatedBodyMovesExactly", "[geometry][transform]") {
    const auto cube = makeBox(10_mm, 10_mm, 10_mm);
    REQUIRE(cube.has_value());
    const auto moved = translated(*cube, mm(20, -5, 2.5));
    REQUIRE(moved.has_value());
    CHECK(moved->isValid());
    CHECK(moved->topology() == cube->topology());
    const auto box = moved->boundingBox().value();
    checkPoint(box.min, 20, -5, 2.5);
    checkPoint(box.max, 30, 5, 12.5);
    const MassProperties properties = requireProperties(*moved);
    CHECK_THAT(properties.volume.in(units::mm3), WithinRel(1000.0, kRelTight));
    CHECK_THAT(properties.surfaceArea.in(units::mm2), WithinRel(600.0, kRelTight));
    checkPoint(properties.centerOfMass, 25, 0, 7.5);
    // The moved faces are exactly where the translation puts them.
    CHECK(facesOn(*moved, planeSignature(Point3D{20_mm, 0_mm, 0_mm}, Direction3D::unitX().reversed())) == 1);
    CHECK(facesOn(*moved, planeSignature(Point3D{30_mm, 0_mm, 0_mm}, Direction3D::unitX())) == 1);
    // The input is unchanged.
    checkPoint(cube->boundingBox()->min, 0, 0, 0);
    // No translation: the same geometry.
    const auto still = translated(*cube, Translation3D{});
    REQUIRE(still.has_value());
    CHECK(still->boundingBox().value() == cube->boundingBox().value());
}

TEST_CASE("Transform_RejectsNonFiniteTranslationsAndEmptyBodies", "[geometry][transform]") {
    const auto cube = makeBox(10_mm, 10_mm, 10_mm);
    REQUIRE(cube.has_value());
    for (const double bad : {std::nan(""), std::numeric_limits<double>::infinity()}) {
        CAPTURE(bad);
        CHECK(errorCode(translated(*cube, Translation3D{Length::fromSi(bad), 0_mm, 0_mm})) ==
              ErrorCode::InvalidArgument);
    }
    CHECK(errorCode(translated(Body{}, mm(1, 0, 0))) == ErrorCode::FailedPrecondition);
}

TEST_CASE("Transform_TranslationIsComputedOnceNotAccumulated", "[geometry][transform]") {
    // Translation3D::along multiplies once per component: k s d, exactly the
    // product, whatever k is.
    const Length spacing = 1.25_mm;
    const auto diagonal = Direction3D::fromComponents(1.0, 1.0, 0.0);
    REQUIRE(diagonal.has_value());
    for (const double k : {1.0, 7.0, 99.0, 999.0}) {
        CAPTURE(k);
        const Translation3D along = Translation3D::along(Direction3D::unitX(), spacing * k);
        CHECK(along.x.si() == spacing.si() * k);
        CHECK(along.y.si() == 0.0);
        const Translation3D slant = Translation3D::along(*diagonal, spacing * k);
        CHECK(slant.x.si() == (spacing.si() * k) * diagonal->x());
        CHECK_THAT(std::hypot(slant.x.in(units::mm), slant.y.in(units::mm)), WithinRel(1.25 * k, 1e-15));
    }
}

TEST_CASE("Transform_TranslatedReferencesDescribeTheMovedGeometry", "[geometry][transform]") {
    SECTION("edges") {
        const EdgeSignature topFront = lineSignature(Point3D{0_mm, 0_mm, 20_mm}, Direction3D::unitX());
        // Along its own line: the same line.
        CHECK(translated(topFront, mm(35, 0, 0)) == topFront);
        CHECK(translated(topFront, mm(0, 50, 0)) == lineSignature(Point3D{0_mm, 50_mm, 20_mm}, Direction3D::unitX()));
        const auto rim = circleSignature(Point3D{20_mm, 25_mm, 20_mm}, Direction3D::unitZ(), 5_mm);
        REQUIRE(rim.has_value());
        const EdgeSignature moved = translated(*rim, mm(20, 0, 0));
        CHECK(moved == *circleSignature(Point3D{40_mm, 25_mm, 20_mm}, Direction3D::unitZ(), 5_mm));
    }
    SECTION("faces") {
        const FaceSignature top = planeSignature(Point3D{0_mm, 0_mm, 20_mm}, Direction3D::unitZ());
        // Within the plane: the same reference; out of it: the moved plane.
        CHECK(translated(top, mm(20, 7, 0)) == top);
        CHECK(translated(top, mm(0, 0, 5)) == planeSignature(Point3D{0_mm, 0_mm, 25_mm}, Direction3D::unitZ()));
        const FaceSignature bottom = planeSignature(Point3D{}, Direction3D::unitZ().reversed());
        CHECK(translated(bottom, mm(0, 0, -3)).normal == Direction3D::unitZ().reversed());
    }
    SECTION("holes") {
        const HoleRequest drill{.face = planeSignature(Point3D{0_mm, 0_mm, 20_mm}, Direction3D::unitZ()),
                                .center = Point2D{20_mm, 25_mm},
                                .diameter = 10_mm};
        const HoleRequest along = translated(drill, mm(20, 0, 0));
        CHECK(along.face == drill.face);
        CHECK_THAT(along.center.x.in(units::mm), WithinAbs(40.0, kPositionToleranceMm));
        CHECK_THAT(along.center.y.in(units::mm), WithinAbs(25.0, kPositionToleranceMm));
        CHECK(along.diameter == drill.diameter);
        // Out of the face's plane, the face reference moves with the hole.
        const HoleRequest up = translated(drill, mm(0, 0, 5));
        CHECK(up.face == planeSignature(Point3D{0_mm, 0_mm, 25_mm}, Direction3D::unitZ()));
        CHECK_THAT(up.center.x.in(units::mm), WithinAbs(20.0, kPositionToleranceMm));
        // A hole moved off the block is refused by cutHole, as any other.
        const auto block = makeBox(100_mm, 50_mm, 20_mm);
        REQUIRE(block.has_value());
        CHECK(errorCode(cutHole(*block, translated(drill, mm(80, 0, 0)))) == ErrorCode::FailedPrecondition);
        CHECK(errorCode(cutHole(*block, up)) == ErrorCode::NotFound);
    }
}

TEST_CASE("Transform_TranslatedCopiesCombineWithAnalyticVolumes", "[geometry][transform]") {
    // Four 10 mm cubes 20 mm apart: disjoint, so 4000 mm^3 in 4 solids.
    const auto cube = makeBox(10_mm, 10_mm, 10_mm);
    REQUIRE(cube.has_value());
    Body row = *cube;
    for (int i = 1; i < 4; ++i) {
        const auto copy = translated(*cube, Translation3D::along(Direction3D::unitX(), 20_mm * static_cast<double>(i)));
        REQUIRE(copy.has_value());
        const auto united = booleanUnion(row, *copy);
        REQUIRE(united.has_value());
        row = *united;
    }
    CHECK(row.topology().solids == 4);
    CHECK_THAT(requireProperties(row).volume.in(units::mm3), WithinRel(4000.0, kRelTight));
    CHECK_THAT(row.boundingBox()->max.x.in(units::mm), WithinAbs(70.0, kPositionToleranceMm));
}
