#include "GeometryTestSupport.hpp"
#include "TestHelpers.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/geometry/Booleans.hpp>
#include <bettercad/core/geometry/Edges.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/core/geometry/Hole.hpp>
#include <bettercad/core/geometry/Primitives.hpp>
#include <bettercad/core/geometry/Transform.hpp>
#include <bettercad/core/math/RigidTransform.hpp>
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

TEST_CASE("Transform_RotatedBodyMovesExactly", "[geometry][transform]") {
    // The cube [45, 55] x [-5, 5] x [0, 10] turned 90° about Z: [-5, 5] x [45, 55] x [0, 10].
    const auto cube = makeBox(Point3D{45_mm, -5_mm, 0_mm}, 10_mm, 10_mm, 10_mm);
    REQUIRE(cube.has_value());
    const auto turn = RigidTransform3D::rotation(Axis3D{Point3D{}, Direction3D::unitZ()}, 90_deg);
    const auto turned = transformed(*cube, turn);
    REQUIRE(turned.has_value());
    CHECK(turned->isValid());
    CHECK(turned->topology() == cube->topology());
    const auto box = turned->boundingBox().value();
    checkPoint(box.min, -5, 45, 0);
    checkPoint(box.max, 5, 55, 10);
    const MassProperties properties = requireProperties(*turned);
    CHECK_THAT(properties.volume.in(units::mm3), WithinRel(1000.0, kRelTight));
    checkPoint(properties.centerOfMass, 0, 50, 5);
    CHECK(facesOn(*turned, planeSignature(Point3D{0_mm, 45_mm, 0_mm}, Direction3D::unitY().reversed())) == 1);
    CHECK(facesOn(*turned, planeSignature(Point3D{0_mm, 55_mm, 0_mm}, Direction3D::unitY())) == 1);
    // A pure translation takes the translation path, bit for bit.
    const auto shifted = transformed(*cube, RigidTransform3D::translation(mm(20, 0, 0)));
    const auto direct = translated(*cube, mm(20, 0, 0));
    REQUIRE(shifted.has_value());
    REQUIRE(direct.has_value());
    CHECK(shifted->boundingBox().value() == direct->boundingBox().value());
    CHECK(requireProperties(*shifted).volume.si() == requireProperties(*direct).volume.si());
    CHECK(errorCode(transformed(Body{}, turn)) == ErrorCode::FailedPrecondition);
}

TEST_CASE("Transform_RotatedReferencesDescribeTheMovedGeometry", "[geometry][transform]") {
    const auto turn = RigidTransform3D::rotation(Axis3D{Point3D{}, Direction3D::unitZ()}, 90_deg);
    SECTION("edges") {
        // A circle about Z at (40, 0, 10) turns to (0, 40, 10), axis still Z.
        const auto rim = circleSignature(Point3D{40_mm, 0_mm, 10_mm}, Direction3D::unitZ(), 5_mm);
        REQUIRE(rim.has_value());
        const EdgeSignature moved = transformed(*rim, turn);
        CHECK(moved.curve == EdgeCurve::Circle);
        checkPoint(moved.point, 0, 40, 10);
        CHECK_THAT(moved.direction.z(), WithinAbs(1.0, 1e-15));
        CHECK(moved.radius == 5_mm);
        // A line along X through (0, 5, 0) turns to a line along Y through (-5, 0, 0).
        const EdgeSignature line = transformed(lineSignature(Point3D{0_mm, 5_mm, 0_mm}, Direction3D::unitX()), turn);
        checkPoint(line.point, -5, 0, 0);
        CHECK_THAT(std::abs(line.direction.y()), WithinAbs(1.0, 1e-15));
    }
    SECTION("faces") {
        // A plane perpendicular to the axis keeps its reference; a side plane turns.
        const FaceSignature top = planeSignature(Point3D{0_mm, 0_mm, 10_mm}, Direction3D::unitZ());
        const FaceSignature turnedTop = transformed(top, turn);
        checkPoint(turnedTop.point, 0, 0, 10);
        CHECK_THAT(turnedTop.normal.z(), WithinAbs(1.0, 1e-15));
        const FaceSignature side = transformed(planeSignature(Point3D{55_mm, 0_mm, 0_mm}, Direction3D::unitX()), turn);
        checkPoint(side.point, 0, 55, 0);
        CHECK_THAT(side.normal.y(), WithinAbs(1.0, 1e-15));
    }
    SECTION("holes") {
        const HoleRequest bolt{.face = planeSignature(Point3D{}, Direction3D::unitZ().reversed()),
                               .center = Point2D{40_mm, 0_mm},
                               .diameter = 10_mm};
        const auto sixty = RigidTransform3D::rotation(Axis3D{Point3D{}, Direction3D::unitZ()}, 60_deg);
        const HoleRequest moved = transformed(bolt, sixty);
        CHECK(moved.face == bolt.face);
        CHECK_THAT(moved.center.x.in(units::mm), WithinAbs(20.0, kPositionToleranceMm));
        CHECK_THAT(moved.center.y.in(units::mm), WithinAbs(40.0 * std::sqrt(3.0) / 2.0, kPositionToleranceMm));
        CHECK(moved.diameter == bolt.diameter);
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
