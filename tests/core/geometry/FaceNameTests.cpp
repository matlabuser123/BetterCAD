#include "GeometryTestSupport.hpp"
#include "TestHelpers.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/geometry/Booleans.hpp>
#include <bettercad/core/geometry/Chamfer.hpp>
#include <bettercad/core/geometry/Edges.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/core/geometry/Fillet.hpp>
#include <bettercad/core/geometry/Hole.hpp>
#include <bettercad/core/geometry/Primitives.hpp>
#include <bettercad/core/geometry/Profile.hpp>
#include <bettercad/core/geometry/Sweeps.hpp>
#include <bettercad/core/geometry/Transform.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <optional>
#include <vector>

using namespace bettercad;
using namespace bettercad::geometry;
using namespace bettercad::literals;
using bettercad::test::errorCode;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// P12-STREF-001: face names on bodies. Prisms name the faces they generate;
// booleans carry the names through the kernel's history. Expected faces are
// located from the inputs' coordinates, not from the names.

namespace {

constexpr double pi = std::numbers::pi;
constexpr double kTolMm = 1e-9;

Body require(const Result<Body>& body) {
    if (!body) {
        FAIL(body.error().message);
    }
    return *body;
}

Point2D mm(double x, double y) {
    return Point2D{x * units::mm, y * units::mm};
}

/// The loop through @p corners (mm), in the order given.
ProfileLoop polygon(std::initializer_list<std::array<double, 2>> corners) {
    std::vector<Point2D> points;
    for (const auto& c : corners) {
        points.push_back(mm(c[0], c[1]));
    }
    ProfileLoop loop;
    for (std::size_t i = 0; i < points.size(); ++i) {
        loop.segments.emplace_back(LineSegment2D{points[i], points[(i + 1) % points.size()]});
    }
    return loop;
}

/// Names like a feature @p feature would: the caps by role, each side by an
/// entity numbered 1 + segment + 100 * loop.
SweptFaceNamer namer(ObjectId feature) {
    return [feature](const SweptFace& face) -> std::optional<FaceName> {
        switch (face.kind) {
        case SweptFace::Kind::First:
            return FaceName{feature, {FaceRole::StartCap, std::nullopt}};
        case SweptFace::Kind::Last:
            return FaceName{feature, {FaceRole::EndCap, std::nullopt}};
        case SweptFace::Kind::Side:
            break;
        }
        return FaceName{feature,
                        {FaceRole::Side, EntityId::fromValue(1 + face.segment + 100 * face.loop)}};
    };
}

FaceName cap(ObjectId feature, FaceRole role) {
    return {feature, {role, std::nullopt}};
}

FaceName side(ObjectId feature, std::uint64_t entity) {
    return {feature, {FaceRole::Side, EntityId::fromValue(entity)}};
}

/// A box [x0, x1] x [y0, y1] x [z0, z1] (mm) as a named prism.
Body box(ObjectId feature, double x0, double y0, double z0, double x1, double y1, double z1) {
    const PlanarRegion region{.plane = Frame3D::xy(),
                              .outer = polygon({{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}}),
                              .holes = {}};
    return require(makePrism(region, z0 * units::mm, z1 * units::mm, namer(feature)));
}

std::vector<FaceInfo> named(const Body& body, const FaceName& name) {
    auto faces = findNamedFaces(body, name);
    REQUIRE(faces.has_value());
    return *faces;
}

void checkPlane(const FaceInfo& face, std::array<double, 3> pointOnPlane, std::array<double, 3> normal) {
    REQUIRE(face.signature.has_value());
    const FaceSignature expected = planeSignature(
        Point3D{pointOnPlane[0] * units::mm, pointOnPlane[1] * units::mm, pointOnPlane[2] * units::mm},
        *Direction3D::fromComponents(normal[0], normal[1], normal[2]));
    bettercad::test::checkPoint(face.signature->point, expected.point.x.in(units::mm),
                                expected.point.y.in(units::mm), expected.point.z.in(units::mm));
    CHECK_THAT(face.signature->normal.x(), WithinAbs(expected.normal.x(), 1e-12));
    CHECK_THAT(face.signature->normal.y(), WithinAbs(expected.normal.y(), 1e-12));
    CHECK_THAT(face.signature->normal.z(), WithinAbs(expected.normal.z(), 1e-12));
}

double areaMm2(const FaceInfo& face) {
    return face.area.in(units::mm2);
}

} // namespace

TEST_CASE("FaceNames_PrismNamesItsCapsAndSides", "[core][geometry][names][p12]") {
    const ObjectId feature = ObjectId::fromValue(7);
    SECTION("an outer loop given clockwise and a square hole given counter-clockwise") {
        // Both loops are reversed for the kernel; the names follow the
        // segments as given.
        const PlanarRegion region{
            .plane = Frame3D::xy(),
            .outer = polygon({{0, 0}, {0, 60}, {100, 60}, {100, 0}}),
            .holes = {polygon({{20, 20}, {40, 20}, {40, 40}, {20, 40}})}};
        const Body body = require(makePrism(region, 5_mm, 25_mm, namer(feature)));
        const auto faces = listFaces(body);
        REQUIRE(faces.has_value());
        CHECK(faces->size() == 10);
        for (const FaceInfo& face : *faces) {
            CHECK(face.names.size() == 1);
        }

        const auto start = named(body, cap(feature, FaceRole::StartCap));
        REQUIRE(start.size() == 1);
        checkPlane(start[0], {0, 0, 5}, {0, 0, -1});
        CHECK_THAT(areaMm2(start[0]), WithinRel(6000.0 - 400.0, 1e-12));
        const auto end = named(body, cap(feature, FaceRole::EndCap));
        REQUIRE(end.size() == 1);
        checkPlane(end[0], {0, 0, 25}, {0, 0, 1});

        // Outer segments as given: x = 0 (1), y = 60 (2), x = 100 (3), y = 0 (4),
        // with outward normals.
        const std::array<std::pair<std::array<double, 3>, std::array<double, 3>>, 4> outer{{
            {{0, 30, 15}, {-1, 0, 0}},
            {{50, 60, 15}, {0, 1, 0}},
            {{100, 30, 15}, {1, 0, 0}},
            {{50, 0, 15}, {0, -1, 0}},
        }};
        for (std::size_t i = 0; i < outer.size(); ++i) {
            CAPTURE(i);
            const auto faceOf = named(body, side(feature, 1 + i));
            REQUIRE(faceOf.size() == 1);
            checkPlane(faceOf[0], outer[i].first, outer[i].second);
            bettercad::test::checkPoint(faceOf[0].centroid, outer[i].first[0], outer[i].first[1],
                                        outer[i].first[2]);
        }
        // Hole segments as given: y = 20 (101), x = 40 (102), y = 40 (103),
        // x = 20 (104); their faces face into the hole.
        const std::array<std::pair<std::array<double, 3>, std::array<double, 3>>, 4> hole{{
            {{30, 20, 15}, {0, 1, 0}},
            {{40, 30, 15}, {-1, 0, 0}},
            {{30, 40, 15}, {0, -1, 0}},
            {{20, 30, 15}, {1, 0, 0}},
        }};
        for (std::size_t i = 0; i < hole.size(); ++i) {
            CAPTURE(i);
            const auto faceOf = named(body, side(feature, 101 + i));
            REQUIRE(faceOf.size() == 1);
            checkPlane(faceOf[0], hole[i].first, hole[i].second);
            bettercad::test::checkPoint(faceOf[0].centroid, hole[i].first[0], hole[i].first[1], hole[i].first[2]);
        }
    }
    SECTION("a circle sweeps one cylindrical side") {
        const PlanarRegion region{.plane = Frame3D::xy(),
                                  .outer = ProfileLoop{{CircleSegment2D{mm(0, 0), 10_mm, true}}},
                                  .holes = {}};
        const Body body = require(makePrism(region, 0_mm, 30_mm, namer(feature)));
        const auto wall = named(body, side(feature, 1));
        REQUIRE(wall.size() == 1);
        CHECK(wall[0].surface == FaceSurface::Cylinder);
        CHECK_FALSE(wall[0].signature.has_value());
        CHECK_THAT(areaMm2(wall[0]), WithinRel(2.0 * pi * 10.0 * 30.0, 1e-12));
    }
    SECTION("without a namer, and from other operations, bodies carry no names") {
        const PlanarRegion region{.plane = Frame3D::xy(), .outer = polygon({{0, 0}, {10, 0}, {10, 10}}), .holes = {}};
        for (const Body& body : {require(makePrism(region, 0_mm, 5_mm)), require(makeBox(10_mm, 10_mm, 10_mm))}) {
            const auto faces = listFaces(body);
            REQUIRE(faces.has_value());
            for (const FaceInfo& face : *faces) {
                CHECK(face.names.empty());
            }
        }
    }
    // P12-SKETCH-003: a moved copy keeps its faces' names, on the moved faces,
    // until renameFaces() gives them the copy's names.
    SECTION("a moved copy keeps the names, where the faces went; renaming changes only the names") {
        const Body moved = require(translated(box(feature, 0, 0, 0, 10, 10, 10), Translation3D{5_mm, 0_mm, 0_mm}));
        const auto right = named(moved, side(feature, 2));
        REQUIRE(right.size() == 1);
        REQUIRE(right[0].signature.has_value());
        CHECK_THAT(right[0].signature->point.x.in(units::mm), WithinAbs(15.0, kTolMm));
        const auto top = named(moved, cap(feature, FaceRole::EndCap));
        REQUIRE(top.size() == 1);
        CHECK_THAT(top[0].signature->point.z.in(units::mm), WithinAbs(10.0, kTolMm));

        const ObjectId row = ObjectId::fromValue(20);
        const Body renamed = renameFaces(moved, [row](const FaceName& name) -> std::optional<FaceName> {
            if (name.face.role == FaceRole::StartCap) {
                return std::nullopt; // dropped
            }
            FaceName copy = name;
            copy.face.copies.push_back(FaceCopy{row, 3});
            return copy;
        });
        CHECK(named(renamed, side(feature, 2)).empty());
        CHECK(named(renamed, cap(feature, FaceRole::StartCap)).empty());
        FaceName copied = side(feature, 2);
        copied.face.copies.push_back(FaceCopy{row, 3});
        const auto after = named(renamed, copied);
        REQUIRE(after.size() == 1);
        CHECK(after[0].signature->point.x == right[0].signature->point.x);
        const auto faces = listFaces(renamed);
        REQUIRE(faces.has_value());
        // Only the bottom, whose name was dropped, is unnamed.
        CHECK(std::ranges::count_if(*faces, [](const FaceInfo& face) { return face.names.empty(); }) == 1);
        const auto properties = renamed.massProperties();
        REQUIRE(properties.has_value());
        CHECK_THAT(properties->volume.in(units::mm3), WithinRel(1000.0, 1e-12));
    }
}

TEST_CASE("FaceNames_BooleansCarryNamesThroughTheKernelHistory", "[core][geometry][names][p12]") {
    const ObjectId a = ObjectId::fromValue(1);
    const ObjectId b = ObjectId::fromValue(2);
    SECTION("side by side, equal heights: the merged top carries both names; the shared walls are gone") {
        const Body result = require(booleanUnion(box(a, 0, 0, 0, 100, 60, 20), box(b, 100, 0, 0, 150, 60, 20)));
        const auto topA = named(result, cap(a, FaceRole::EndCap));
        const auto topB = named(result, cap(b, FaceRole::EndCap));
        REQUIRE(topA.size() == 1);
        REQUIRE(topB.size() == 1);
        CHECK_THAT(areaMm2(topA[0]), WithinRel(9000.0, 1e-12));
        CHECK(topA[0].names == std::vector<FaceName>{cap(a, FaceRole::EndCap), cap(b, FaceRole::EndCap)});
        // a's x = 100 side (segment 2) and b's x = 100 side (segment 4) were inside.
        CHECK(named(result, side(a, 2)).empty());
        CHECK(named(result, side(b, 4)).empty());
        // The front walls merged too.
        const auto front = named(result, side(a, 1));
        REQUIRE(front.size() == 1);
        CHECK(front[0].names == std::vector<FaceName>{side(a, 1), side(b, 1)});
    }
    SECTION("a boss on a block: the boss's bottom is gone, the block's top has a hole") {
        const Body result = require(booleanUnion(box(a, 0, 0, 0, 100, 60, 20), box(b, 20, 20, 20, 40, 40, 30)));
        CHECK(named(result, cap(b, FaceRole::StartCap)).empty());
        const auto top = named(result, cap(a, FaceRole::EndCap));
        REQUIRE(top.size() == 1);
        CHECK_THAT(areaMm2(top[0]), WithinRel(6000.0 - 400.0, 1e-12));
        checkPlane(top[0], {0, 0, 20}, {0, 0, 1});
        const auto bossTop = named(result, cap(b, FaceRole::EndCap));
        REQUIRE(bossTop.size() == 1);
        checkPlane(bossTop[0], {0, 0, 30}, {0, 0, 1});
    }
    SECTION("a through slot splits the top in two, which both carry its name") {
        const Body result = require(booleanDifference(box(a, 0, 0, 0, 100, 60, 20), box(b, 40, -1, 10, 60, 61, 21)));
        const auto top = named(result, cap(a, FaceRole::EndCap));
        REQUIRE(top.size() == 2);
        for (const FaceInfo& part : top) {
            checkPlane(part, {0, 0, 20}, {0, 0, 1});
            CHECK_THAT(areaMm2(part), WithinRel(2400.0, 1e-12));
        }
        CHECK_THAT(top[0].centroid.x.in(units::mm) + top[1].centroid.x.in(units::mm), WithinAbs(100.0, kTolMm));
        // The slot's floor is the tool's start cap, now facing up out of the material.
        const auto floor = named(result, cap(b, FaceRole::StartCap));
        REQUIRE(floor.size() == 1);
        checkPlane(floor[0], {0, 0, 10}, {0, 0, 1});
        CHECK_THAT(areaMm2(floor[0]), WithinRel(1200.0, 1e-12));
        // The tool's top was outside the block; its ends at y = -1 and 61 too.
        CHECK(named(result, cap(b, FaceRole::EndCap)).empty());
        CHECK(named(result, side(b, 1)).empty());
        CHECK(named(result, side(b, 3)).empty());
    }
    SECTION("a pocket: the tool's top lay on the block's top and is gone; its bottom is the floor") {
        const Body result = require(booleanDifference(box(a, 0, 0, 0, 100, 60, 20), box(b, 20, 20, 12, 50, 40, 20)));
        CHECK(named(result, cap(b, FaceRole::EndCap)).empty());
        const auto floor = named(result, cap(b, FaceRole::StartCap));
        REQUIRE(floor.size() == 1);
        checkPlane(floor[0], {0, 0, 12}, {0, 0, 1});
        const auto top = named(result, cap(a, FaceRole::EndCap));
        REQUIRE(top.size() == 1);
        CHECK_THAT(areaMm2(top[0]), WithinRel(6000.0 - 600.0, 1e-12));
    }
    SECTION("an intersection keeps the names of both operands' remaining faces") {
        const Body result =
            require(booleanIntersection(box(a, 0, 0, 0, 100, 60, 20), box(b, 50, 30, 10, 150, 90, 30)));
        const auto top = named(result, cap(a, FaceRole::EndCap));
        REQUIRE(top.size() == 1);
        checkPlane(top[0], {0, 0, 20}, {0, 0, 1});
        const auto bottom = named(result, cap(b, FaceRole::StartCap));
        REQUIRE(bottom.size() == 1);
        checkPlane(bottom[0], {0, 0, 10}, {0, 0, -1});
        CHECK(named(result, cap(a, FaceRole::StartCap)).empty());
        CHECK(named(result, cap(b, FaceRole::EndCap)).empty());
    }
    SECTION("an empty body has no faces to look in") {
        CHECK(errorCode(findNamedFaces(Body{}, cap(a, FaceRole::EndCap))) == ErrorCode::FailedPrecondition);
    }
}

TEST_CASE("FaceNames_AreTheSameOnEveryBuild", "[core][geometry][names][p12]") {
    const ObjectId a = ObjectId::fromValue(1);
    const ObjectId b = ObjectId::fromValue(2);
    const auto build = [&] {
        const Body result = require(booleanDifference(box(a, 0, 0, 0, 100, 60, 20), box(b, 40, -1, 10, 60, 61, 21)));
        const auto faces = listFaces(result);
        REQUIRE(faces.has_value());
        std::vector<std::vector<FaceName>> names;
        for (const FaceInfo& face : *faces) {
            names.push_back(face.names);
        }
        return names;
    };
    const auto first = build();
    CHECK(first.size() == 10);
    for (int i = 0; i < 5; ++i) {
        CHECK(build() == first);
    }
}

TEST_CASE("FaceFrame_UsesTheFaceCoordinatesAndTheOutwardNormal", "[core][geometry][names][p12]") {
    const auto frameOf = [](std::array<double, 3> p, std::array<double, 3> n) {
        const auto frame = faceFrame(planeSignature(Point3D{p[0] * units::mm, p[1] * units::mm, p[2] * units::mm},
                                                    *Direction3D::fromComponents(n[0], n[1], n[2])));
        REQUIRE(frame.has_value());
        return *frame;
    };
    const auto checkAxes = [](const Frame3D& f, std::array<double, 3> x, std::array<double, 3> y) {
        CHECK(f.xAxis().x() == x[0]);
        CHECK(f.xAxis().y() == x[1]);
        CHECK(f.xAxis().z() == x[2]);
        CHECK(f.yAxis().x() == y[0]);
        CHECK(f.yAxis().y() == y[1]);
        CHECK(f.yAxis().z() == y[2]);
    };
    SECTION("a top face: (x, y) are the face's (u, v)") {
        const Frame3D f = frameOf({7, -3, 20}, {0, 0, 1});
        bettercad::test::checkPoint(f.origin(), 0, 0, 20);
        checkAxes(f, {1, 0, 0}, {0, 1, 0});
        const FaceSignature signature = planeSignature(Point3D{0_mm, 0_mm, 20_mm}, Direction3D::unitZ());
        const Point3D a = f.toGlobal(mm(12.5, -4));
        const Point3D b = facePoint(signature, mm(12.5, -4));
        bettercad::test::checkPoint(a, b.x.in(units::mm), b.y.in(units::mm), b.z.in(units::mm));
    }
    SECTION("a bottom face: y turns so that the frame faces out") {
        const Frame3D f = frameOf({0, 0, 5}, {0, 0, -1});
        checkAxes(f, {1, 0, 0}, {0, -1, 0});
        CHECK(f.normal().z() == -1.0);
    }
    SECTION("a front face (facing -Y): x along X, y along Z") {
        const Frame3D f = frameOf({3, 0, 9}, {0, -1, 0});
        bettercad::test::checkPoint(f.origin(), 0, 0, 0);
        checkAxes(f, {1, 0, 0}, {0, 0, 1});
    }
    SECTION("a right face (facing +X): x along Y, y along Z") {
        const Frame3D f = frameOf({100, 4, 4}, {1, 0, 0});
        bettercad::test::checkPoint(f.origin(), 100, 0, 0);
        checkAxes(f, {0, 1, 0}, {0, 0, 1});
    }
    SECTION("only planes have frames") {
        CHECK(errorCode(faceFrame(FaceSignature{.surface = FaceSurface::Cylinder})) == ErrorCode::InvalidArgument);
    }
}

// --- P12-SKETCH-003: revolutions, sweeps, lofts, holes, chamfers, fillets -----------------------

namespace {

/// Names like a sweep would: each side also by its path segment (1, 2, ...).
SweptFaceNamer pathNamer(ObjectId feature) {
    return [feature](const SweptFace& face) -> std::optional<FaceName> {
        if (face.kind != SweptFace::Kind::Side) {
            return namer(feature)(face);
        }
        return FaceName{feature, {.role = FaceRole::Side,
                                  .entity = EntityId::fromValue(1 + face.segment + 100 * face.loop),
                                  .along = EntityId::fromValue(1 + face.pathSegment)}};
    };
}

FaceName along(ObjectId feature, std::uint64_t entity, std::uint64_t pathSegment) {
    return {feature, {.role = FaceRole::Side,
                      .entity = EntityId::fromValue(entity),
                      .along = EntityId::fromValue(pathSegment)}};
}

std::size_t unnamedCount(const Body& body) {
    const auto faces = listFaces(body);
    REQUIRE(faces.has_value());
    return static_cast<std::size_t>(
        std::ranges::count_if(*faces, [](const FaceInfo& face) { return face.names.empty(); }));
}

void checkEveryFaceNamedOnce(const Body& body, std::size_t faceCount) {
    const auto faces = listFaces(body);
    REQUIRE(faces.has_value());
    CHECK(faces->size() == faceCount);
    for (const FaceInfo& face : *faces) {
        CHECK(face.names.size() == 1);
    }
}

} // namespace

TEST_CASE("FaceNames_RevolutionsNameTheirCapsAndSides", "[core][geometry][names][p12]") {
    const ObjectId feature = ObjectId::fromValue(3);
    const Axis3D yAxis{Point3D{}, Direction3D::unitY()};
    const double s = std::numbers::sqrt2 / 2.0;
    SECTION("a quarter turn, symmetric about the profile plane") {
        // x 20..40, y 0..10: bottom (1), outer (2), top (3), inner (4).
        const PlanarRegion region{.plane = Frame3D::xy(), .outer = polygon({{20, 0}, {40, 0}, {40, 10}, {20, 10}}),
                                  .holes = {}};
        const Body body = require(makeRevolution(region, yAxis, -45_deg, 45_deg, namer(feature)));
        checkEveryFaceNamedOnce(body, 6);
        // About +Y, angle a points along (cos a, 0, -sin a); the start (a =
        // -45) faces back along the turn, the end (a = 45) forward.
        const auto start = named(body, cap(feature, FaceRole::StartCap));
        REQUIRE(start.size() == 1);
        checkPlane(start[0], {0, 0, 0}, {-s, 0, s});
        CHECK_THAT(areaMm2(start[0]), WithinRel(200.0, 1e-12));
        const auto end = named(body, cap(feature, FaceRole::EndCap));
        REQUIRE(end.size() == 1);
        checkPlane(end[0], {0, 0, 0}, {-s, 0, -s});
        const auto bottom = named(body, side(feature, 1));
        REQUIRE(bottom.size() == 1);
        checkPlane(bottom[0], {0, 0, 0}, {0, -1, 0});
        const auto top = named(body, side(feature, 3));
        REQUIRE(top.size() == 1);
        checkPlane(top[0], {0, 10, 0}, {0, 1, 0});
        CHECK_THAT(areaMm2(top[0]), WithinRel(pi / 4.0 * (1600.0 - 400.0), 1e-12));
        for (const std::uint64_t curved : {2U, 4U}) {
            const auto wall = named(body, side(feature, curved));
            REQUIRE(wall.size() == 1);
            CHECK(wall[0].surface == FaceSurface::Cylinder);
        }
    }
    SECTION("a full turn has no caps, and a segment on the axis sweeps no face") {
        // x 0..30, y 0..10: the inner segment (4) lies on the axis.
        const PlanarRegion region{.plane = Frame3D::xy(), .outer = polygon({{0, 0}, {30, 0}, {30, 10}, {0, 10}}),
                                  .holes = {}};
        const Body body = require(makeRevolution(region, yAxis, 0_deg, 360_deg, namer(feature)));
        checkEveryFaceNamedOnce(body, 3);
        CHECK(named(body, cap(feature, FaceRole::StartCap)).empty());
        CHECK(named(body, cap(feature, FaceRole::EndCap)).empty());
        CHECK(named(body, side(feature, 4)).empty());
        const auto bottom = named(body, side(feature, 1));
        REQUIRE(bottom.size() == 1);
        checkPlane(bottom[0], {0, 0, 0}, {0, -1, 0});
        CHECK_THAT(areaMm2(bottom[0]), WithinRel(900.0 * pi, 1e-12));
        const auto wall = named(body, side(feature, 2));
        REQUIRE(wall.size() == 1);
        CHECK_THAT(areaMm2(wall[0]), WithinRel(2.0 * pi * 30.0 * 10.0, 1e-12));
    }
}

TEST_CASE("FaceNames_SweepsNameTheirSidesAlongEachPathSegment", "[core][geometry][names][p12]") {
    const ObjectId feature = ObjectId::fromValue(4);
    // A square x, y in -5..5 (sides y = -5 (1), x = 5 (2), y = 5 (3),
    // x = -5 (4)) swept up Z by 20 (path segment 1), round a quarter bend of
    // radius 30 towards -X (2) and 30 along -X (3). The bend turns the
    // profile X axis to +Z; its Y axis stays.
    const PlanarRegion region{.plane = Frame3D::xy(), .outer = polygon({{-5, -5}, {5, -5}, {5, 5}, {-5, 5}}),
                              .holes = {}};
    const PlanarPath path{.plane = Frame3D::xz(),
                          .segments = {LineSegment2D{mm(0, 0), mm(0, 20)},
                                       ArcSegment2D{mm(-30, 20), mm(0, 20), mm(-30, 50), true},
                                       LineSegment2D{mm(-30, 50), mm(-60, 50)}}};
    const Body body = require(makeSweep(region, path, pathNamer(feature)));
    checkEveryFaceNamedOnce(body, 2 + 4 * 3);
    const auto start = named(body, cap(feature, FaceRole::StartCap));
    REQUIRE(start.size() == 1);
    checkPlane(start[0], {0, 0, 0}, {0, 0, -1});
    const auto end = named(body, cap(feature, FaceRole::EndCap));
    REQUIRE(end.size() == 1);
    checkPlane(end[0], {-60, 0, 0}, {-1, 0, 0});

    const auto plane = [&](std::uint64_t entity, std::uint64_t segment) {
        const auto faces = named(body, along(feature, entity, segment));
        REQUIRE(faces.size() == 1);
        return faces[0];
    };
    checkPlane(plane(2, 1), {5, 0, 0}, {1, 0, 0});
    CHECK(plane(2, 2).surface == FaceSurface::Cylinder);
    checkPlane(plane(2, 3), {0, 0, 55}, {0, 0, 1});
    checkPlane(plane(4, 1), {-5, 0, 0}, {-1, 0, 0});
    CHECK(plane(4, 2).surface == FaceSurface::Cylinder);
    checkPlane(plane(4, 3), {0, 0, 45}, {0, 0, -1});
    for (std::uint64_t segment = 1; segment <= 3; ++segment) {
        checkPlane(plane(1, segment), {0, -5, 0}, {0, -1, 0});
        checkPlane(plane(3, segment), {0, 5, 0}, {0, 1, 0});
    }
    CHECK_THAT(areaMm2(plane(1, 1)), WithinRel(200.0, 1e-12));
    CHECK_THAT(areaMm2(plane(1, 2)), WithinRel(pi / 4.0 * (35.0 * 35.0 - 25.0 * 25.0), 1e-12));
    CHECK_THAT(areaMm2(plane(1, 3)), WithinRel(300.0, 1e-12));
    // A side is named by its entity and its path segment together.
    CHECK(named(body, side(feature, 2)).empty());

    SECTION("a profile given clockwise: the names follow its segments as given") {
        // x = -5 (1), y = 5 (2), x = 5 (3), y = -5 (4); the adapter reverses
        // the loop for the kernel.
        const PlanarRegion clockwise{.plane = Frame3D::xy(),
                                     .outer = polygon({{-5, -5}, {-5, 5}, {5, 5}, {5, -5}}),
                                     .holes = {}};
        const Body turned = require(makeSweep(clockwise, path, pathNamer(feature)));
        checkEveryFaceNamedOnce(turned, 2 + 4 * 3);
        const auto face = [&](std::uint64_t entity, std::uint64_t segment) {
            const auto faces = named(turned, along(feature, entity, segment));
            REQUIRE(faces.size() == 1);
            return faces[0];
        };
        checkPlane(face(1, 1), {-5, 0, 0}, {-1, 0, 0});
        checkPlane(face(3, 1), {5, 0, 0}, {1, 0, 0});
        checkPlane(face(3, 3), {0, 0, 55}, {0, 0, 1});
        checkPlane(face(1, 3), {0, 0, 45}, {0, 0, -1});
        checkPlane(face(4, 2), {0, -5, 0}, {0, -1, 0});
        checkPlane(face(2, 2), {0, 5, 0}, {0, 1, 0});
    }
    SECTION("a closed path has no caps") {
        const PlanarRegion ring{.plane = Frame3D::xz(), .outer = polygon({{25, -5}, {35, -5}, {35, 5}, {25, 5}}),
                                .holes = {}};
        const PlanarPath circle{.plane = Frame3D::xy(), .segments = {CircleSegment2D{mm(0, 0), 30_mm, true}}};
        const Body torus = require(makeSweep(ring, circle, pathNamer(feature)));
        CHECK(named(torus, cap(feature, FaceRole::StartCap)).empty());
        CHECK(named(torus, cap(feature, FaceRole::EndCap)).empty());
        CHECK(unnamedCount(torus) == 0);
    }
}

TEST_CASE("FaceNames_LoftsNameTheirEndsOnly", "[core][geometry][names][p12]") {
    const ObjectId feature = ObjectId::fromValue(5);
    const std::vector<PlanarRegion> sections{
        {.plane = Frame3D::xy(), .outer = polygon({{-20, -20}, {20, -20}, {20, 20}, {-20, 20}}), .holes = {}},
        {.plane = Frame3D::create(Point3D{0_mm, 0_mm, 30_mm}, Direction3D::unitZ(), Direction3D::unitX()).value(),
         .outer = polygon({{-10, -10}, {10, -10}, {10, 10}, {-10, 10}}),
         .holes = {}}};
    const Body body = require(makeLoft(sections, namer(feature)));
    const auto start = named(body, cap(feature, FaceRole::StartCap));
    REQUIRE(start.size() == 1);
    checkPlane(start[0], {0, 0, 0}, {0, 0, -1});
    CHECK_THAT(areaMm2(start[0]), WithinRel(1600.0, 1e-12));
    const auto end = named(body, cap(feature, FaceRole::EndCap));
    REQUIRE(end.size() == 1);
    checkPlane(end[0], {0, 0, 30}, {0, 0, 1});
    CHECK_THAT(areaMm2(end[0]), WithinRel(400.0, 1e-12));
    // The four ruled sides are not named.
    CHECK(unnamedCount(body) == 4);
}

TEST_CASE("FaceNames_HolesAndChamfersNameTheirFacesAndKeepTheirInputs", "[core][geometry][names][p12]") {
    const ObjectId block = ObjectId::fromValue(1);
    const ObjectId cutter = ObjectId::fromValue(2);
    const Body base = box(block, 0, 0, 0, 60, 40, 30);
    const FaceSignature top = planeSignature(Point3D{0_mm, 0_mm, 30_mm}, Direction3D::unitZ());
    const HoleFaceNamer holeNamer = [cutter](HoleFace face) -> std::optional<FaceName> {
        return FaceName{cutter, {.role = face == HoleFace::Bottom ? FaceRole::HoleBottom
                                                                  : FaceRole::CounterboreFloor}};
    };
    const auto holeFace = [&](const Body& body, FaceRole role) {
        return named(body, FaceName{cutter, {.role = role}});
    };

    SECTION("a blind counterbore: its bottom and its floor") {
        const Body body = require(cutHole(base,
                                          {.face = top,
                                           .center = mm(15, 20),
                                           .type = HoleType::Counterbore,
                                           .extent = HoleExtent::Blind,
                                           .diameter = 8_mm,
                                           .depth = 20_mm,
                                           .counterboreDiameter = 16_mm,
                                           .counterboreDepth = 5_mm},
                                          holeNamer));
        const auto bottom = holeFace(body, FaceRole::HoleBottom);
        REQUIRE(bottom.size() == 1);
        checkPlane(bottom[0], {0, 0, 10}, {0, 0, 1});
        CHECK_THAT(areaMm2(bottom[0]), WithinRel(16.0 * pi, 1e-12));
        const auto floor = holeFace(body, FaceRole::CounterboreFloor);
        REQUIRE(floor.size() == 1);
        checkPlane(floor[0], {0, 0, 25}, {0, 0, 1});
        CHECK_THAT(areaMm2(floor[0]), WithinRel(48.0 * pi, 1e-12));
        // The block top keeps its name, less the counterbore mouth.
        const auto blockTop = named(body, cap(block, FaceRole::EndCap));
        REQUIRE(blockTop.size() == 1);
        CHECK_THAT(areaMm2(blockTop[0]), WithinRel(2400.0 - 64.0 * pi, 1e-12));
        // The two walls are not named.
        CHECK(unnamedCount(body) == 2);
    }
    SECTION("a simple through hole names nothing of its own") {
        const Body body = require(cutHole(base, {.face = top, .center = mm(15, 20), .diameter = 8_mm}, holeNamer));
        CHECK(holeFace(body, FaceRole::HoleBottom).empty());
        CHECK(holeFace(body, FaceRole::CounterboreFloor).empty());
        CHECK(named(body, cap(block, FaceRole::StartCap)).size() == 1);
        CHECK(unnamedCount(body) == 1);
    }
    SECTION("a chamfer names the face each edge reference cuts") {
        const double s = std::numbers::sqrt2 / 2.0;
        const ChamferFaceNamer chamferNamer = [cutter](std::size_t reference) -> std::optional<FaceName> {
            return FaceName{cutter, {.role = FaceRole::Chamfer, .edge = static_cast<std::uint32_t>(reference + 1)}};
        };
        const Body body = require(chamferEdges(base,
                                               {.edges = {lineSignature(Point3D{0_mm, 0_mm, 30_mm},
                                                                        Direction3D::unitX()),
                                                          lineSignature(Point3D{0_mm, 40_mm, 30_mm},
                                                                        Direction3D::unitX())},
                                                .distance = 4_mm},
                                               chamferNamer));
        const auto face = [&](std::uint32_t reference) {
            const auto faces = named(body, FaceName{cutter, {.role = FaceRole::Chamfer, .edge = reference}});
            REQUIRE(faces.size() == 1);
            return faces[0];
        };
        checkPlane(face(1), {0, 0, 26}, {0, -s, s});
        checkPlane(face(2), {0, 40, 26}, {0, s, s});
        CHECK_THAT(areaMm2(face(1)), WithinRel(60.0 * 4.0 * std::numbers::sqrt2, 1e-12));
        // The block faces keep their names, trimmed: the top loses 4 mm on
        // each side, the front 4 mm at the top.
        const auto blockTop = named(body, cap(block, FaceRole::EndCap));
        REQUIRE(blockTop.size() == 1);
        CHECK_THAT(areaMm2(blockTop[0]), WithinRel(60.0 * 32.0, 1e-12));
        const auto front = named(body, side(block, 1));
        REQUIRE(front.size() == 1);
        CHECK_THAT(areaMm2(front[0]), WithinRel(60.0 * 26.0, 1e-12));
        checkEveryFaceNamedOnce(body, 8);
    }
    SECTION("a fillet keeps the names of its input and names nothing of its own") {
        const Body body = require(
            filletEdges(base, {.edges = {lineSignature(Point3D{}, Direction3D::unitX())}, .radius = 2_mm}));
        const auto bottom = named(body, cap(block, FaceRole::StartCap));
        REQUIRE(bottom.size() == 1);
        CHECK_THAT(areaMm2(bottom[0]), WithinRel(60.0 * 38.0, 1e-12));
        const auto front = named(body, side(block, 1));
        REQUIRE(front.size() == 1);
        CHECK_THAT(areaMm2(front[0]), WithinRel(60.0 * 28.0, 1e-12));
        CHECK(unnamedCount(body) == 1);
    }
}
