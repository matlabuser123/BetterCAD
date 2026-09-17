#include "GeometryTestSupport.hpp"
#include "TestHelpers.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/geometry/Booleans.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/core/geometry/Primitives.hpp>
#include <bettercad/core/geometry/Profile.hpp>
#include <bettercad/core/geometry/Sweeps.hpp>
#include <bettercad/core/geometry/Transform.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <array>
#include <cmath>
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
PrismFaceNamer namer(ObjectId feature) {
    return [feature](const PrismFace& face) -> std::optional<FaceName> {
        switch (face.kind) {
        case PrismFace::Kind::First:
            return FaceName{feature, {FaceRole::StartCap, std::nullopt}};
        case PrismFace::Kind::Last:
            return FaceName{feature, {FaceRole::EndCap, std::nullopt}};
        case PrismFace::Kind::Side:
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
        for (const Body& body : {require(makePrism(region, 0_mm, 5_mm)), require(makeBox(10_mm, 10_mm, 10_mm)),
                                 require(translated(box(feature, 0, 0, 0, 10, 10, 10),
                                                    Translation3D{5_mm, 0_mm, 0_mm}))}) {
            const auto faces = listFaces(body);
            REQUIRE(faces.has_value());
            for (const FaceInfo& face : *faces) {
                CHECK(face.names.empty());
            }
        }
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
