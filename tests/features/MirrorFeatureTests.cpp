#include "FeatureTestSupport.hpp"
#include "TestHelpers.hpp"
#include "core/geometry/GeometryTestSupport.hpp"
#include "support/BracketModel.hpp"
#include "support/FilletModels.hpp"
#include "support/HoleModels.hpp"
#include "support/MirrorModels.hpp"
#include "support/PatternModels.hpp"
#include "support/TurnedPartModel.hpp"

#include <bettercad/core/document/Commands.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/geometry/Edges.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/features/ChamferFeature.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/FeatureCommands.hpp>
#include <bettercad/features/FilletFeature.hpp>
#include <bettercad/features/HoleFeature.hpp>
#include <bettercad/features/LinearPatternFeature.hpp>
#include <bettercad/features/MirrorFeature.hpp>
#include <bettercad/features/Regeneration.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/features/ResultBodies.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <numbers>
#include <vector>

using namespace bettercad;
using namespace bettercad::features;
using namespace bettercad::literals;
using bettercad::test::BlockModel;
using bettercad::test::bottomFace;
using bettercad::test::BossMirrorModel;
using bettercad::test::CubeMirrorModel;
using bettercad::test::describe;
using bettercad::test::errorCode;
using bettercad::test::filletCorner;
using bettercad::test::HoleBlockModel;
using bettercad::test::HoleMirrorModel;
using bettercad::test::HoleRowModel;
using bettercad::test::kPositionToleranceMm;
using bettercad::test::PegModel;
using bettercad::test::requireReport;
using bettercad::test::topFace;
using bettercad::test::TurnedPartModel;
using bettercad::test::volumeMm3;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::StartsWith;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

constexpr double pi = std::numbers::pi;
// Mirrored boxes, holes, bosses and turned parts have planar, cylindrical
// and conical faces meeting in lines and circles, which the kernel computes
// to rounding level (kRelTight, as for the pattern tests).
constexpr double kRel = bettercad::test::kRelTight;

FeatureId featureId(ObjectId id) {
    return FeatureId::fromValue(id.value());
}

/// Regenerates; exactly @p feature must fail, keeping no body. Returns its error.
Error requireFailure(Regenerator& regenerator, Document& doc, ObjectId feature) {
    const RegenerationReport report = requireReport(regenerator, doc);
    INFO(describe(report));
    REQUIRE(report.failed == std::vector<ObjectId>{feature});
    CHECK(regenerator.state(feature) == NodeState::Failed);
    CHECK(regenerator.body(feature) == nullptr);
    return report.errors.at(feature);
}

const geometry::Body& requireBody(const Regenerator& regenerator, ObjectId feature) {
    const geometry::Body* body = regenerator.body(feature);
    REQUIRE(body != nullptr);
    return *body;
}

std::size_t facesOn(const Regenerator& regenerator, ObjectId feature, const geometry::FaceSignature& face) {
    const auto found = geometry::findFaces(requireBody(regenerator, feature), face);
    REQUIRE(found.has_value());
    return found->size();
}

double areaOn(const Regenerator& regenerator, ObjectId feature, const geometry::FaceSignature& face) {
    const auto found = geometry::findFaces(requireBody(regenerator, feature), face);
    REQUIRE(found.has_value());
    double area = 0.0;
    for (const geometry::FaceInfo& info : *found) {
        area += info.area.in(units::mm2);
    }
    return area;
}

/// The plane through (x, y, z) mm with outward normal (nx, ny, nz).
geometry::FaceSignature plane(double x, double y, double z, double nx, double ny, double nz) {
    return geometry::planeSignature(Point3D{x * units::mm, y * units::mm, z * units::mm},
                                    *Direction3D::fromComponents(nx, ny, nz));
}

/// Edges on the circle of radius @p r mm around (x, y, z) mm with axis (ax, ay, az).
std::vector<geometry::EdgeInfo> circles(const Regenerator& regenerator, ObjectId feature, double x, double y, double z,
                                        double r, double ax = 0.0, double ay = 0.0, double az = 1.0) {
    const auto circle = geometry::circleSignature(Point3D{x * units::mm, y * units::mm, z * units::mm},
                                                  *Direction3D::fromComponents(ax, ay, az), r * units::mm);
    REQUIRE(circle.has_value());
    const auto found = geometry::findEdges(requireBody(regenerator, feature), *circle);
    REQUIRE(found.has_value());
    return *found;
}

/// Hole rims (circles about Z) of radius @p r at (x, y) on z = @p z.
std::size_t holeAt(const Regenerator& regenerator, ObjectId feature, double x, double y, double z, double r = 5.0) {
    return circles(regenerator, feature, x, y, z, r).size();
}

std::uint64_t bits(double value) {
    return std::bit_cast<std::uint64_t>(value);
}

// The independent reference: p mirrored across the plane through p0 with
// normal n (any length), p' = p - 2 ((p - p0) . n) n with n made unit.
using V = std::array<double, 3>;

V unit(const V& n) {
    const double length = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    return {n[0] / length, n[1] / length, n[2] / length};
}

double signedDistance(const V& p, const V& p0, const V& nIn) {
    const V n = unit(nIn);
    return (p[0] - p0[0]) * n[0] + (p[1] - p0[1]) * n[1] + (p[2] - p0[2]) * n[2];
}

V reflect(const V& p, const V& p0, const V& nIn) {
    const V n = unit(nIn);
    const double d = signedDistance(p, p0, n);
    return {p[0] - 2.0 * d * n[0], p[1] - 2.0 * d * n[1], p[2] - 2.0 * d * n[2]};
}

/// A direction mirrored: v' = v - 2 (v . n) n.
V reflectVector(const V& v, const V& nIn) {
    return reflect(v, {0, 0, 0}, nIn);
}

V mm(const Point3D& p) {
    return {p.x.in(units::mm), p.y.in(units::mm), p.z.in(units::mm)};
}

void checkPoint(const Point3D& actual, const V& expected) {
    bettercad::test::checkPoint(actual, expected[0], expected[1], expected[2]);
}

/// A mirror of @p source across the plane through @p p0 (mm) with normal @p n.
MirrorDefinition across(FeatureId source, const V& p0, const V& n, MirrorScope scope = MirrorScope::Feature,
                        bool keepOriginal = true) {
    return {.source = source,
            .plane = {.origin = Point3D{p0[0] * units::mm, p0[1] * units::mm, p0[2] * units::mm},
                      .normal = {n[0], n[1], n[2]}},
            .scope = scope,
            .keepOriginal = keepOriginal};
}

Error refusal(const MirrorDefinition& definition) {
    auto feature = MirrorFeature::create("M", definition);
    REQUIRE_FALSE(feature.has_value());
    return feature.error();
}

} // namespace

TEST_CASE("MirrorFeature_DefinitionIsValidatedOnCreateAndEdit", "[mirror][features]") {
    const MirrorDefinition good = across(FeatureId::fromValue(3), {0, 0, 0}, {1, 0, 0});
    REQUIRE(MirrorFeature::create("M", good).has_value());
    MirrorDefinition d = good;

    SECTION("a source and valid parameter IDs") {
        d.source = FeatureId{};
        CHECK(refusal(d).message == "a mirror needs a source feature");
        d = good;
        d.plane.offsetParameter = ParameterId{};
        CHECK(refusal(d).message == "the mirror's parameter IDs must be valid");
        d.plane.offsetParameter = ParameterId::fromValue(2);
        CHECK(MirrorFeature::create("M", d).has_value());
    }
    SECTION("only a body mirror can leave the original out") {
        d.keepOriginal = false;
        const Error refused = refusal(d);
        CHECK(refused.code == ErrorCode::InvalidArgument);
        CHECK(refused.message == "a feature mirror keeps its source: the mirrored operation is added to the body the "
                                 "source made (mirror the body to keep only the mirror image)");
        d.scope = MirrorScope::Body;
        CHECK(MirrorFeature::create("M", d).has_value());
        d.keepOriginal = true;
        CHECK(MirrorFeature::create("M", d).has_value());
    }
    SECTION("an invalid edit leaves the feature unchanged") {
        auto feature = MirrorFeature::create("M", good);
        REQUIRE(feature.has_value());
        d.plane.normal = {0.0, 0.0, 0.0};
        CHECK(errorCode((*feature)->setDefinition(d)) == ErrorCode::InvalidArgument);
        CHECK((*feature)->definition() == good);
        const auto unchanged = (*feature)->setDefinition(good);
        REQUIRE(unchanged.has_value());
        CHECK_FALSE(*unchanged);
    }
    CHECK(toString(MirrorScope::Feature) == "feature");
    CHECK(toString(MirrorScope::Body) == "body");
}

TEST_CASE("MirrorFeature_DependsOnItsSourceAndOffsetParameter", "[mirror][features]") {
    HoleMirrorModel m;
    const auto* mirror = m.doc.findObjectAs<MirrorFeature>(m.mirror);
    REQUIRE(mirror != nullptr);
    CHECK(mirror->dependencies() == std::vector<ObjectId>{m.drill, ObjectId{m.mid}});
    CHECK(mirror->target() == featureId(m.drill)); // the mirror consumes its source
    CHECK(mirror->typeName() == "mirror");
    CHECK(equivalent(*mirror->clone(), *mirror));
    CubeMirrorModel cubes;
    CHECK(cubes.doc.findObjectAs<MirrorFeature>(cubes.mirror)->dependencies() == std::vector<ObjectId>{cubes.cube});
}

TEST_CASE("MirrorFeature_ReflectsCubeAcrossYZPlane", "[mirror][features][acceptance]") {
    // The 10 mm cube centred at (20, 0, 0) across x = 0: its mirror image is
    // centred at (-20, 0, 0).
    CubeMirrorModel m;
    const auto reflection = resolveMirrorReflection(m.definitionOf(m.mirror), m.doc);
    REQUIRE(reflection.has_value());
    bettercad::test::checkPoint(reflection->point, 0, 0, 0);
    CHECK(reflection->normal == Direction3D::unitX());
    CHECK(reflection->motion.reversesOrientation());
    const V image = reflect({20, 0, 0}, {0, 0, 0}, {1, 0, 0});
    CHECK(image == V{-20, 0, 0});
    checkPoint(reflection->motion.apply(Point3D{20_mm, 0_mm, 0_mm}), image);

    Regenerator regenerator;
    const RegenerationReport report = requireReport(regenerator, m.doc);
    INFO(describe(report));
    REQUIRE(report.succeeded());
    CHECK(report.regenerated == std::vector<ObjectId>{m.sketch, m.cube, m.mirror});
    const geometry::Body& body = requireBody(regenerator, m.mirror);
    CHECK(body.isValid());
    CHECK(body.topology().solids == 2);
    const auto props = body.massProperties().value();
    CHECK_THAT(props.volume.in(units::mm3), WithinRel(2000.0, kRel)); // V + V
    CHECK_THAT(props.surfaceArea.in(units::mm2), WithinRel(1200.0, kRel));
    bettercad::test::checkPoint(props.centerOfMass, 0, 0, 0);
    // X bounds [-xmax, -xmin] U [xmin, xmax] = [-25, 25].
    bettercad::test::checkPoint(body.boundingBox()->min, -25, -5, -5);
    bettercad::test::checkPoint(body.boundingBox()->max, 25, 5, 5);
    // The source's sides at x = 15 (facing -X) and 25 (+X); the image's at
    // x = -25 (facing -X) and -15 (+X): the mirror turns each face around.
    CHECK(facesOn(regenerator, m.mirror, plane(15, 0, 0, -1, 0, 0)) == 1);
    CHECK(facesOn(regenerator, m.mirror, plane(25, 0, 0, 1, 0, 0)) == 1);
    CHECK(facesOn(regenerator, m.mirror, plane(-25, 0, 0, -1, 0, 0)) == 1);
    CHECK(facesOn(regenerator, m.mirror, plane(-15, 0, 0, 1, 0, 0)) == 1);
    CHECK(facesOn(regenerator, m.mirror, plane(-25, 0, 0, 1, 0, 0)) == 0);
    CHECK(resultFeatures(m.doc) == std::vector<ObjectId>{m.mirror});
    CHECK_THAT(volumeMm3(regenerator, m.cube), WithinRel(1000.0, kRel)); // the source, untouched

    // The mirror image alone (a body mirror without the original).
    MirrorDefinition only = m.definitionOf(m.mirror);
    only.scope = MirrorScope::Body;
    only.keepOriginal = false;
    m.setDefinition(m.mirror, only);
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const geometry::Body& alone = requireBody(regenerator, m.mirror);
    CHECK(alone.isValid());
    CHECK(alone.topology().solids == 1);
    CHECK_THAT(volumeMm3(regenerator, m.mirror), WithinRel(1000.0, kRel));
    checkPoint(alone.massProperties()->centerOfMass, image);
    bettercad::test::checkPoint(alone.boundingBox()->min, -25, -5, -5);
    bettercad::test::checkPoint(alone.boundingBox()->max, -15, 5, 5);
}

TEST_CASE("MirrorFeature_ReflectsAcrossOffsetPlane", "[mirror][features][acceptance]") {
    // The cube centred at x = 30 across the plane x = 10: 20 mm on one side,
    // 20 mm on the other, so its image is centred at x' = 2a - x = -10.
    CubeMirrorModel m{25_mm};
    Regenerator regenerator;
    const auto checkImage = [&] {
        const RegenerationReport report = requireReport(regenerator, m.doc);
        INFO(describe(report));
        REQUIRE(report.succeeded());
        const geometry::Body& body = requireBody(regenerator, m.mirror);
        CHECK(body.topology().solids == 1);
        CHECK_THAT(volumeMm3(regenerator, m.mirror), WithinRel(1000.0, kRel));
        checkPoint(body.massProperties()->centerOfMass, reflect({30, 0, 0}, {10, 0, 0}, {1, 0, 0}));
        bettercad::test::checkPoint(body.boundingBox()->min, -15, -5, -5); // [2a - 35, 2a - 25]
        bettercad::test::checkPoint(body.boundingBox()->max, -5, 5, 5);
    };
    SECTION("the plane through (10, 0, 0)") {
        m.setDefinition(m.mirror, across(featureId(m.cube), {10, 0, 0}, {1, 0, 0}, MirrorScope::Body, false));
        checkImage();
        // With the original: 2000 mm^3 from -15 to 35.
        m.setDefinition(m.mirror, across(featureId(m.cube), {10, 0, 0}, {1, 0, 0}, MirrorScope::Body, true));
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CHECK_THAT(volumeMm3(regenerator, m.mirror), WithinRel(2000.0, kRel));
        bettercad::test::checkPoint(requireBody(regenerator, m.mirror).boundingBox()->min, -15, -5, -5);
        bettercad::test::checkPoint(requireBody(regenerator, m.mirror).boundingBox()->max, 35, 5, 5);
    }
    SECTION("the plane through the origin, offset 10 mm along its normal") {
        MirrorDefinition d = across(featureId(m.cube), {0, 0, 0}, {1, 0, 0}, MirrorScope::Body, false);
        d.plane.offset = 10_mm;
        m.setDefinition(m.mirror, d);
        checkImage();
        // The normal's sense does not change the plane's position rule:
        // (-2, 0, 0) with offset -10 mm is the same plane.
        d.plane.normal = {-2.0, 0.0, 0.0};
        d.plane.offset = -(10_mm);
        m.setDefinition(m.mirror, d);
        checkImage();
    }
    SECTION("the offset driven by a parameter") {
        const ParameterId offset = m.doc.createParameter("plane_x", 10_mm, units::mm).value();
        MirrorDefinition d = across(featureId(m.cube), {0, 0, 0}, {1, 0, 0}, MirrorScope::Body, false);
        d.plane.offsetParameter = offset;
        m.setDefinition(m.mirror, d);
        checkImage();
        const auto resolved = resolveMirrorReflection(d, m.doc);
        REQUIRE(resolved.has_value());
        bettercad::test::checkPoint(resolved->point, 10, 0, 0);
        // x = 20: the image is centred at 2 x 20 - 30 = 10.
        REQUIRE(m.doc.setParameterValue(offset, 20_mm).has_value());
        CHECK(requireReport(regenerator, m.doc).regenerated == std::vector<ObjectId>{m.mirror});
        checkPoint(requireBody(regenerator, m.mirror).massProperties()->centerOfMass,
                   reflect({30, 0, 0}, {20, 0, 0}, {1, 0, 0}));
    }
}

TEST_CASE("MirrorFeature_ArbitraryPlaneMatchesAnalyticPointReflection", "[mirror][features][acceptance]") {
    SECTION("the reference point: (4, 1, 3) across (1, 0, 0), normal (1, 1, 0)") {
        CubeMirrorModel m;
        const auto reflection = resolveMirrorReflection(across(featureId(m.cube), {1, 0, 0}, {1, 1, 0}), m.doc);
        REQUIRE(reflection.has_value());
        const V expected = reflect({4, 1, 3}, {1, 0, 0}, {1, 1, 0});
        CHECK_THAT(expected[0], WithinAbs(0.0, 1e-14));
        CHECK_THAT(expected[1], WithinAbs(-3.0, 1e-14));
        CHECK_THAT(expected[2], WithinAbs(3.0, 1e-14));
        const V actual = mm(reflection->motion.apply(Point3D{4_mm, 1_mm, 3_mm}));
        const double error =
            std::hypot(actual[0] - expected[0], actual[1] - expected[1], actual[2] - expected[2]);
        INFO("error " << error << " mm");
        CHECK(error < 1e-12);
    }
    SECTION("pegs across the axis planes and skew planes") {
        // The peg (r = 3 mm, z = 17 to 23 about x = 40, y = 10) mirrored on its
        // own: each rim is found where the formula puts it, with its axis
        // mirrored, on the other side of the plane at the same distance; the
        // centre of mass is the reflected centre (40, 10, 20).
        const std::array<std::pair<V, V>, 6> planes{{{{0, 0, 0}, {1, 0, 0}},
                                                     {{0, 0, 0}, {0, 1, 0}},
                                                     {{0, 0, 0}, {0, 0, 1}},
                                                     {{0, 0, 0}, {1, 1, 0}},
                                                     {{1, 0, 0}, {1, 1, 0}},
                                                     {{5, -2, 1}, {1, 1, 1}}}};
        for (const auto& [p0, n] : planes) {
            CAPTURE(p0[0], p0[1], p0[2], n[0], n[1], n[2]);
            PegModel m;
            auto feature = MirrorFeature::create("Image", across(FeatureId::fromValue(m.peg.value()), p0, n,
                                                                  MirrorScope::Body, false));
            REQUIRE(feature.has_value());
            const ObjectId image = m.doc.addObject(std::move(*feature)).value();
            Regenerator regenerator;
            REQUIRE(requireReport(regenerator, m.doc).succeeded());
            const geometry::Body& body = requireBody(regenerator, image);
            CHECK(body.isValid());
            CHECK(body.topology().solids == 1);
            CHECK_THAT(volumeMm3(regenerator, image), WithinRel(pi * 9.0 * 6.0, kRel));
            const V axis = reflectVector({0, 0, 1}, n);
            double worst = 0.0;
            for (const double z : {17.0, 23.0}) {
                const V rim{40, 10, z};
                const V expected = reflect(rim, p0, n);
                const auto found = circles(regenerator, image, expected[0], expected[1], expected[2], 3, axis[0],
                                           axis[1], axis[2]);
                REQUIRE(found.size() == 1);
                const V actual = mm(found.front().signature->point);
                worst = std::max(worst, std::abs(signedDistance(actual, p0, n) + signedDistance(rim, p0, n)));
                worst = std::max(worst, std::hypot(actual[0] - expected[0], actual[1] - expected[1],
                                                   actual[2] - expected[2]));
            }
            INFO("largest distance or position error of a rim: " << worst << " mm");
            CHECK(worst < 1e-9);
            checkPoint(body.massProperties()->centerOfMass, reflect({40, 10, 20}, p0, n));
        }
    }
    SECTION("face normals mirror as vectors") {
        // The cube across the plane through the origin with normal (1, 1, 0):
        // its +X face (x = 25) becomes the face through (0, -25, 0) facing
        // (0, -1, 0), and its +Y face (y = 5) the face x = -5 facing -X.
        CubeMirrorModel m;
        m.setDefinition(m.mirror, across(featureId(m.cube), {0, 0, 0}, {1, 1, 0}, MirrorScope::Body, false));
        Regenerator regenerator;
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        const V xFace = reflect({25, 0, 0}, {0, 0, 0}, {1, 1, 0});
        const V xNormal = reflectVector({1, 0, 0}, {1, 1, 0});
        CHECK_THAT(xNormal[1], WithinAbs(-1.0, 1e-15));
        CHECK(facesOn(regenerator, m.mirror, plane(xFace[0], xFace[1], xFace[2], xNormal[0], xNormal[1], xNormal[2])) ==
              1);
        const V yFace = reflect({20, 5, 0}, {0, 0, 0}, {1, 1, 0});
        const V yNormal = reflectVector({0, 1, 0}, {1, 1, 0});
        CHECK(facesOn(regenerator, m.mirror, plane(yFace[0], yFace[1], yFace[2], yNormal[0], yNormal[1], yNormal[2])) ==
              1);
        CHECK(facesOn(regenerator, m.mirror, plane(yFace[0], yFace[1], yFace[2], -yNormal[0], -yNormal[1],
                                                   -yNormal[2])) == 0);
    }
}

TEST_CASE("MirrorFeature_PreservesVolume", "[mirror][features][acceptance]") {
    // A mirror image on its own has the source's volume and area.
    SECTION("an extruded cube across a skew plane") {
        CubeMirrorModel m;
        m.setDefinition(m.mirror, across(featureId(m.cube), {3, -2, 1}, {1, 2, 2}, MirrorScope::Body, false));
        Regenerator regenerator;
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        const auto source = requireBody(regenerator, m.cube).massProperties().value();
        const auto image = requireBody(regenerator, m.mirror).massProperties().value();
        CHECK(requireBody(regenerator, m.mirror).isValid());
        CHECK_THAT(image.volume.in(units::mm3), WithinRel(1000.0, kRel));
        CHECK_THAT(image.volume.in(units::mm3), WithinRel(source.volume.in(units::mm3), kRel));
        CHECK_THAT(image.surfaceArea.in(units::mm2), WithinRel(source.surfaceArea.in(units::mm2), kRel));
        checkPoint(image.centerOfMass, reflect({20, 0, 0}, {3, -2, 1}, {1, 2, 2}));
    }
    SECTION("a drilled block") {
        HoleBlockModel m;
        const ObjectId image = m.add<MirrorFeature>(
            "Image", across(featureId(m.drill), {5, -2, 1}, {1, 1, 1}, MirrorScope::Body, false));
        Regenerator regenerator;
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CHECK(requireBody(regenerator, image).isValid());
        CHECK_THAT(volumeMm3(regenerator, image), WithinRel(HoleBlockModel::expectedVolume(100, 50, 20, 10), kRel));
        CHECK_THAT(requireBody(regenerator, image).massProperties()->surfaceArea.in(units::mm2),
                   WithinRel(requireBody(regenerator, m.drill).massProperties()->surfaceArea.in(units::mm2), kRel));
    }
}

TEST_CASE("MirrorFeature_DoubleMirrorReturnsOriginal", "[mirror][features][acceptance]") {
    // M(M(p)) = p: the cube mirrored across a skew plane, and that image
    // mirrored again across the same plane, is the cube again.
    CubeMirrorModel m;
    const V p0{3, -2, 1};
    const V n{1, 2, 2};
    m.setDefinition(m.mirror, across(featureId(m.cube), p0, n, MirrorScope::Body, false));
    const ObjectId back = m.addMirror("Back", across(featureId(m.mirror), p0, n, MirrorScope::Body, false));
    Regenerator regenerator;
    const RegenerationReport report = requireReport(regenerator, m.doc);
    INFO(describe(report));
    REQUIRE(report.succeeded());
    const auto source = requireBody(regenerator, m.cube).massProperties().value();
    const auto once = requireBody(regenerator, m.mirror).massProperties().value();
    const auto twice = requireBody(regenerator, back).massProperties().value();
    checkPoint(once.centerOfMass, reflect({20, 0, 0}, p0, n));
    CHECK(requireBody(regenerator, back).isValid());
    CHECK_THAT(twice.volume.in(units::mm3), WithinRel(source.volume.in(units::mm3), kRel));
    CHECK_THAT(twice.surfaceArea.in(units::mm2), WithinRel(source.surfaceArea.in(units::mm2), kRel));
    bettercad::test::checkPoint(twice.centerOfMass, 20, 0, 0);
    const auto box = requireBody(regenerator, back).boundingBox().value();
    bettercad::test::checkPoint(box.min, 15, -5, -5);
    bettercad::test::checkPoint(box.max, 25, 5, 5);
    CHECK(facesOn(regenerator, back, plane(15, 0, 0, -1, 0, 0)) == 1);
    CHECK(facesOn(regenerator, back, plane(25, 0, 0, 1, 0, 0)) == 1);
}

TEST_CASE("MirrorFeature_KeepOriginalDoublesNonOverlappingBodyVolume", "[mirror][features][acceptance]") {
    CubeMirrorModel m;
    Regenerator regenerator;
    const auto volumeWith = [&](MirrorScope scope, bool keep) {
        m.setDefinition(m.mirror, across(featureId(m.cube), {0, 0, 0}, {1, 0, 0}, scope, keep));
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        return volumeMm3(regenerator, m.mirror);
    };
    // Disjoint: V_total = 2 V with the original, V without.
    CHECK_THAT(volumeWith(MirrorScope::Feature, true), WithinRel(2000.0, kRel));
    CHECK(requireBody(regenerator, m.mirror).topology().solids == 2);
    CHECK_THAT(volumeWith(MirrorScope::Body, true), WithinRel(2000.0, kRel));
    CHECK(requireBody(regenerator, m.mirror).topology().solids == 2);
    CHECK_THAT(volumeWith(MirrorScope::Body, false), WithinRel(1000.0, kRel));
    CHECK(requireBody(regenerator, m.mirror).topology().solids == 1);
    CHECK_THAT(volumeWith(MirrorScope::Body, true), WithinRel(2000.0, kRel));
}

TEST_CASE("MirrorFeature_HoleMirrorMatchesAnalyticVolume", "[mirror][features][hole][acceptance]") {
    // The flagship case: a 10 mm through hole at x = 30 in a 100 x 50 x 20 mm
    // block, mirrored across x = 50 (the plane through the origin moved by
    // `mid`): the second hole is at x = 70, and V = L W H - 2 pi (d/2)^2 H.
    HoleMirrorModel m;
    const auto reflection = resolveMirrorReflection(m.mirrorOf(m.mirror), m.doc);
    REQUIRE(reflection.has_value());
    bettercad::test::checkPoint(reflection->point, 50, 0, 0);
    const V expectedCentre = reflect({30, 25, 0}, {50, 0, 0}, {1, 0, 0});
    CHECK(expectedCentre == V{70, 25, 0});
    checkPoint(reflection->motion.apply(Point3D{30_mm, 25_mm, 0_mm}), expectedCentre);

    Regenerator regenerator;
    const RegenerationReport report = requireReport(regenerator, m.doc);
    INFO(describe(report));
    REQUIRE(report.succeeded());
    CHECK(report.regenerated == std::vector<ObjectId>{m.base, m.pad, m.drill, m.mirror});
    const geometry::Body& body = requireBody(regenerator, m.mirror);
    CHECK(body.isValid());
    CHECK(body.topology().solids == 1);
    const double expected = HoleMirrorModel::expectedVolume(100, 20, 10);
    CHECK_THAT(expected, WithinRel(100000.0 - 1000.0 * pi, 1e-15));
    const auto props = body.massProperties().value();
    CHECK_THAT(props.volume.in(units::mm3), WithinRel(expected, kRel));
    // The faces lose 4 discs pi r^2; the bores add 2 x 2 pi r H.
    CHECK_THAT(props.surfaceArea.in(units::mm2), WithinRel(16000.0 + 300.0 * pi, kRel));
    // Both holes, through both faces, where the plane puts them.
    for (const double x : {30.0, 70.0}) {
        CAPTURE(x);
        CHECK(holeAt(regenerator, m.mirror, x, 25, 0) == 1);
        CHECK(holeAt(regenerator, m.mirror, x, 25, 20) == 1);
    }
    CHECK(holeAt(regenerator, m.mirror, 50, 25, 20) == 0);
    CHECK(holeAt(regenerator, m.mirror, 10, 25, 20) == 0);
    // The source hole's body is an unchanged intermediate.
    CHECK_THAT(volumeMm3(regenerator, m.drill), WithinRel(HoleMirrorModel::expectedVolume(100, 20, 10, 1), kRel));
    CHECK(resultFeatures(m.doc) == std::vector<ObjectId>{m.mirror});
}

TEST_CASE("MirrorFeature_MirrorsHoleTypesAndExtents", "[mirror][features][hole]") {
    // The drill remade as other kinds of hole at (30, 25) in the top face of
    // the 100 x 50 x 20 mm block; its mirror image is the same hole reflected,
    // so V = V0 - 2 V_removed.
    HoleMirrorModel m;
    Regenerator regenerator;
    HoleDefinition d = m.definitionOf(m.drill);
    d.face = topFace();
    const auto build = [&] {
        m.setDefinition(m.drill, d);
        const RegenerationReport report = requireReport(regenerator, m.doc);
        INFO(describe(report));
        REQUIRE(report.succeeded());
        CHECK(requireBody(regenerator, m.mirror).isValid());
        CHECK(requireBody(regenerator, m.mirror).topology().solids == 1);
        return volumeMm3(regenerator, m.mirror);
    };
    SECTION("a blind counterbore, across x = 50") {
        // The bore r = 5 mm 12 mm deep and the counterbore R = 8 mm 4 mm deep
        // remove pi r^2 12 + pi (R^2 - r^2) 4 = 456 pi each.
        d.type = geometry::HoleType::Counterbore;
        d.extent = geometry::HoleExtent::Blind;
        d.depth = 12_mm;
        d.counterboreDiameter = 16_mm;
        d.counterboreDepth = 4_mm;
        CHECK_THAT(build(), WithinRel(100000.0 - 2.0 * 456.0 * pi, kRel));
        for (const double x : {30.0, 70.0}) {
            CAPTURE(x);
            CHECK(holeAt(regenerator, m.mirror, x, 25, 20, 8) == 1); // the counterbore's rim
            CHECK(holeAt(regenerator, m.mirror, x, 25, 16, 8) == 1); // its shelf
            CHECK(holeAt(regenerator, m.mirror, x, 25, 16, 5) == 1); // the bore's top
            CHECK(holeAt(regenerator, m.mirror, x, 25, 8, 5) == 1);  // its floor
            CHECK(holeAt(regenerator, m.mirror, x, 25, 0, 5) == 0);  // blind
        }
    }
    SECTION("a through countersink, across x = 50") {
        // The 90 deg cone from R = 10 to r = 5 mm is h = 5 mm deep: it removes
        // the frustum pi h (R^2 + R r + r^2) / 3 less pi r^2 h beyond the bore,
        // 500 pi / 3, and the bore pi r^2 20 = 500 pi.
        d.type = geometry::HoleType::Countersink;
        d.countersinkDiameter = 20_mm;
        d.countersinkAngle = 90_deg;
        CHECK_THAT(build(), WithinRel(100000.0 - 2.0 * (500.0 + 500.0 / 3.0) * pi, kRel));
        for (const double x : {30.0, 70.0}) {
            CAPTURE(x);
            CHECK(holeAt(regenerator, m.mirror, x, 25, 20, 10) == 1);
            CHECK(holeAt(regenerator, m.mirror, x, 25, 15, 5) == 1);
            CHECK(holeAt(regenerator, m.mirror, x, 25, 0, 5) == 1);
        }
    }
    SECTION("a blind hole across the mid-plane z = 10 enters the opposite face") {
        // Drilled 6 mm down from the top, its mirror image is drilled 6 mm up
        // from the bottom: the face and the direction are reflected.
        d.extent = geometry::HoleExtent::Blind;
        d.depth = 6_mm;
        m.setMirror(m.mirror, across(featureId(m.drill), {0, 0, 10}, {0, 0, 1}));
        CHECK_THAT(build(), WithinRel(100000.0 - 2.0 * 150.0 * pi, kRel));
        for (const double z : {20.0, 14.0, 6.0, 0.0}) {
            CAPTURE(z);
            CHECK(holeAt(regenerator, m.mirror, 30, 25, z) == 1);
        }
        CHECK(holeAt(regenerator, m.mirror, 70, 25, 20) == 0);
    }
}

TEST_CASE("MirrorFeature_AdditiveBossMirrorMatchesAnalyticVolume", "[mirror][features][acceptance]") {
    // A 10 x 10 x 5 mm boss centred at x = 30 on a 100 x 50 x 10 mm plate,
    // mirrored across x = 50: V = V_plate + 2 V_boss.
    BossMirrorModel m;
    Regenerator regenerator;
    SECTION("a boss and its mirror image") {
        const RegenerationReport report = requireReport(regenerator, m.doc);
        INFO(describe(report));
        REQUIRE(report.succeeded());
        const geometry::Body& body = requireBody(regenerator, m.mirror);
        CHECK(body.isValid());
        CHECK(body.topology().solids == 1);
        CHECK_THAT(volumeMm3(regenerator, m.mirror), WithinRel(50000.0 + 2.0 * 500.0, kRel));
        CHECK(facesOn(regenerator, m.mirror, topFace(15)) == 2);
        CHECK_THAT(areaOn(regenerator, m.mirror, topFace(15)), WithinRel(200.0, kRel));
        CHECK_THAT(areaOn(regenerator, m.mirror, topFace(10)), WithinRel(5000.0 - 200.0, kRel));
        // The boss's sides at x = 25 and 35; the image's at 65 and 75 (2 x 50 - x).
        CHECK(facesOn(regenerator, m.mirror, plane(25, 0, 0, -1, 0, 0)) == 1);
        CHECK(facesOn(regenerator, m.mirror, plane(35, 0, 0, 1, 0, 0)) == 1);
        CHECK(facesOn(regenerator, m.mirror, plane(65, 0, 0, -1, 0, 0)) == 1);
        CHECK(facesOn(regenerator, m.mirror, plane(75, 0, 0, 1, 0, 0)) == 1);
        CHECK_THAT(body.boundingBox()->max.z.in(units::mm), WithinAbs(15.0, kPositionToleranceMm));
        CHECK(resultFeatures(m.doc) == std::vector<ObjectId>{m.mirror});
    }
    SECTION("the boss as a pocket (cut 5 mm down): V = V_plate - 2 V_pocket") {
        m.setDefinition<ExtrudeFeature>(m.bossFeature, {.profile = BlockModel::sketchId(m.bossSketch),
                                                         .depth = 5_mm,
                                                         .direction = ExtrudeDirection::Reversed,
                                                         .operation = FeatureOperation::Cut,
                                                         .target = BlockModel::featureId(m.pad)});
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CHECK(requireBody(regenerator, m.mirror).isValid());
        CHECK_THAT(volumeMm3(regenerator, m.mirror), WithinRel(50000.0 - 2.0 * 500.0, kRel));
        CHECK(facesOn(regenerator, m.mirror, topFace(5)) == 2); // two pocket floors
        CHECK_THAT(areaOn(regenerator, m.mirror, topFace(5)), WithinRel(200.0, kRel));
    }
}

TEST_CASE("MirrorFeature_RegeneratesWhenSourceChanges", "[mirror][features][hole][acceptance]") {
    SECTION("a larger hole diameter changes both holes") {
        HoleMirrorModel m;
        Regenerator regenerator;
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        REQUIRE(m.doc.setParameterValue(m.diameter, 12_mm).has_value());
        CHECK(requireReport(regenerator, m.doc).regenerated == std::vector<ObjectId>{m.drill, m.mirror});
        CHECK_THAT(volumeMm3(regenerator, m.mirror), WithinRel(HoleMirrorModel::expectedVolume(100, 20, 12), kRel));
        for (const double x : {30.0, 70.0}) {
            CAPTURE(x);
            CHECK(holeAt(regenerator, m.mirror, x, 25, 20, 6) == 1);
            CHECK(holeAt(regenerator, m.mirror, x, 25, 20, 5) == 0);
        }
    }
    SECTION("a thicker block: both through holes stay through") {
        HoleMirrorModel m;
        Regenerator regenerator;
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        REQUIRE(m.doc.setParameterValue(m.height, 40_mm).has_value());
        CHECK(requireReport(regenerator, m.doc).regenerated == std::vector<ObjectId>{m.pad, m.drill, m.mirror});
        CHECK_THAT(volumeMm3(regenerator, m.mirror), WithinRel(HoleMirrorModel::expectedVolume(100, 40, 10), kRel));
        for (const double x : {30.0, 70.0}) {
            CAPTURE(x);
            CHECK(holeAt(regenerator, m.mirror, x, 25, 40) == 1);
            CHECK(holeAt(regenerator, m.mirror, x, 25, 0) == 1);
        }
    }
    SECTION("moving the hole moves its mirror image: 30 -> 20 gives 70 -> 80") {
        HoleMirrorModel m;
        Regenerator regenerator;
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        REQUIRE(m.doc.setParameterValue(m.holeX, 20_mm).has_value());
        CHECK(requireReport(regenerator, m.doc).regenerated == std::vector<ObjectId>{m.drill, m.mirror});
        CHECK(holeAt(regenerator, m.mirror, 20, 25, 20) == 1);
        CHECK(holeAt(regenerator, m.mirror, 80, 25, 20) == 1);
        CHECK(holeAt(regenerator, m.mirror, 30, 25, 20) == 0); // no stale holes
        CHECK(holeAt(regenerator, m.mirror, 70, 25, 20) == 0);
        CHECK_THAT(volumeMm3(regenerator, m.mirror), WithinRel(HoleMirrorModel::expectedVolume(100, 20, 10), kRel));
    }
    SECTION("a wider boss: both bosses grow") {
        // boss 14: the source spans x = 25..39, its image 61..75.
        BossMirrorModel m;
        Regenerator regenerator;
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        REQUIRE(m.doc.setParameterValue(m.boss, 14_mm).has_value());
        CHECK(requireReport(regenerator, m.doc).regenerated ==
              std::vector<ObjectId>{m.bossSketch, m.bossFeature, m.mirror});
        CHECK_THAT(volumeMm3(regenerator, m.mirror), WithinRel(50000.0 + 2.0 * 14.0 * 14.0 * 5.0, kRel));
        CHECK(facesOn(regenerator, m.mirror, plane(39, 0, 0, 1, 0, 0)) == 1);
        CHECK(facesOn(regenerator, m.mirror, plane(61, 0, 0, -1, 0, 0)) == 1);
        CHECK(facesOn(regenerator, m.mirror, plane(65, 0, 0, -1, 0, 0)) == 0);
    }
}

TEST_CASE("MirrorFeature_RegeneratesWhenPlaneMoves", "[mirror][features][hole][acceptance]") {
    HoleMirrorModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    SECTION("the driving parameter: x = 50 -> 60 takes the image from 70 to 90") {
        REQUIRE(m.doc.setParameterValue(m.mid, 60_mm).has_value());
        CHECK(requireReport(regenerator, m.doc).regenerated == std::vector<ObjectId>{m.mirror});
        CHECK(holeAt(regenerator, m.mirror, 30, 25, 20) == 1);
        CHECK(holeAt(regenerator, m.mirror, 90, 25, 20) == 1);
        CHECK(holeAt(regenerator, m.mirror, 70, 25, 20) == 0);
        CHECK_THAT(volumeMm3(regenerator, m.mirror), WithinRel(HoleMirrorModel::expectedVolume(100, 20, 10), kRel));
    }
    SECTION("the plane's origin: through (60, 0, 0) with no offset") {
        MirrorDefinition d = m.mirrorOf(m.mirror);
        d.plane.offsetParameter.reset();
        d.plane.origin = Point3D{60_mm, 0_mm, 0_mm};
        m.setMirror(m.mirror, d);
        CHECK(requireReport(regenerator, m.doc).regenerated == std::vector<ObjectId>{m.mirror});
        CHECK(holeAt(regenerator, m.mirror, 90, 25, 20) == 1);
    }
    SECTION("the plane's normal: across y = 16 the image is at (30, 7)") {
        m.setMirror(m.mirror, across(featureId(m.drill), {0, 16, 0}, {0, 1, 0}));
        CHECK(requireReport(regenerator, m.doc).regenerated == std::vector<ObjectId>{m.mirror});
        const V image = reflect({30, 25, 20}, {0, 16, 0}, {0, 1, 0});
        CHECK(holeAt(regenerator, m.mirror, image[0], image[1], image[2]) == 1);
        CHECK(holeAt(regenerator, m.mirror, 70, 25, 20) == 0);
        CHECK_THAT(volumeMm3(regenerator, m.mirror), WithinRel(HoleMirrorModel::expectedVolume(100, 20, 10), kRel));
    }
}

TEST_CASE("MirrorFeature_RejectsZeroNormal", "[mirror][features][acceptance]") {
    for (const V& zero : {V{0, 0, 0}, V{-0.0, 0, 0}}) {
        const Error refused = refusal(across(FeatureId::fromValue(3), {0, 0, 0}, zero));
        CHECK(refused.code == ErrorCode::InvalidArgument);
        CHECK(refused.message == "the plane's normal must be a finite, non-zero vector, got (0, 0, 0)");
    }
    CHECK(MirrorFeature::create("M", across(FeatureId::fromValue(3), {0, 0, 0}, {1e-300, 0, 0})).has_value());
}

TEST_CASE("MirrorFeature_RejectsNonFinitePlane", "[mirror][features][acceptance]") {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    for (const V& bad : {V{nan, 0, 0}, V{0, inf, 0}, V{1, 0, -inf}}) {
        CAPTURE(bad[0], bad[1], bad[2]);
        const Error refused = refusal(across(FeatureId::fromValue(3), {0, 0, 0}, bad));
        CHECK(refused.code == ErrorCode::InvalidArgument);
        CHECK_THAT(refused.message, StartsWith("the plane's normal must be a finite, non-zero vector, got "));
        CHECK(refusal(across(FeatureId::fromValue(3), bad, {1, 0, 0})).message == "the plane's origin must be finite");
    }
    MirrorDefinition d = across(FeatureId::fromValue(3), {0, 0, 0}, {1, 0, 0});
    d.plane.offset = Length::fromSi(inf);
    CHECK(refusal(d).message == "the plane's offset must be finite, got inf mm");
    // A driving parameter cannot hold NaN or infinity at all.
    HoleMirrorModel m;
    CHECK(errorCode(m.doc.setParameterValue(m.mid, Length::fromSi(nan))) == ErrorCode::InvalidArgument);
}

TEST_CASE("MirrorFeature_FailsAtomicallyWhenMirroredFeatureInvalid", "[mirror][features][hole][acceptance]") {
    HoleMirrorModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const double drilled = volumeMm3(regenerator, m.drill);
    const double mirrored = volumeMm3(regenerator, m.mirror);

    SECTION("the mirror image of the hole lands 2 mm from the block's end") {
        // Across x = 64 the hole at x = 30 goes to 98; a 10 mm hole needs 5 mm.
        REQUIRE(m.doc.setParameterValue(m.mid, 64_mm).has_value());
        const Document edited = m.doc.clone();
        const Error error = requireFailure(regenerator, m.doc, m.mirror);
        CHECK(error.code == ErrorCode::FailedPrecondition);
        CHECK_THAT(error.message,
                   StartsWith("Mirror: mirror: the mirror image across the plane through (64, 0, 0) mm facing (1, 0, "
                              "0): hole: the hole does not fit on its face: its entry is 10 mm across, but the centre "
                              "(98, 25) mm is only "));
        CHECK_THAT(error.message, ContainsSubstring("2 mm from the face's edge"));
        // No partial result: no body at all; the source and the model are untouched.
        CHECK(equivalent(m.doc, edited));
        CHECK(regenerator.state(m.drill) == NodeState::UpToDate);
        CHECK(volumeMm3(regenerator, m.drill) == drilled);
        // Back inside the block, the mirror builds again.
        REQUIRE(m.doc.setParameterValue(m.mid, 50_mm).has_value());
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CHECK(volumeMm3(regenerator, m.mirror) == mirrored);
    }
    SECTION("the mirror image of the hole lands beyond the block") {
        REQUIRE(m.doc.setParameterValue(m.mid, 80_mm).has_value());
        const Error error = requireFailure(regenerator, m.doc, m.mirror);
        CHECK(error.code == ErrorCode::FailedPrecondition);
        CHECK_THAT(error.message, StartsWith("Mirror: mirror: the mirror image across the plane through (80, 0, 0) mm "
                                             "facing (1, 0, 0): hole: the centre (130, 25) mm is not on a face of the "
                                             "body"));
    }
}

TEST_CASE("MirrorFeature_UndoRedoRestoresGeometry", "[mirror][features][hole][undo][acceptance]") {
    // The drilled block without its mirror; the mirror is created by command.
    HoleMirrorModel m;
    REQUIRE(m.doc.removeObject(m.mirror).has_value());
    CommandHistory history;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const Document initial = m.doc.clone();

    struct State {
        MirrorDefinition definition;
        Document document;
        double volume;
        Point3D centre;
        BoundingBox3D box;
    };
    std::vector<State> states;
    ObjectId mirror;
    const auto record = [&] {
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        const geometry::Body& body = requireBody(regenerator, mirror);
        states.push_back({m.mirrorOf(mirror), m.doc.clone(), volumeMm3(regenerator, mirror),
                          body.massProperties()->centerOfMass, body.boundingBox().value()});
    };
    const auto modify = [&](const MirrorDefinition& d) {
        REQUIRE(history.execute(m.doc, std::make_unique<ModifyMirrorCommand>(featureId(mirror), d)).has_value());
        record();
    };

    // 1. Create: the hole mirrored across x = 50.
    auto create = std::make_unique<CreateMirrorCommand>("Mirror", across(featureId(m.drill), {50, 0, 0}, {1, 0, 0}));
    CreateMirrorCommand* createRaw = create.get();
    CHECK(create->description() == "Create mirror 'Mirror'");
    REQUIRE(history.execute(m.doc, std::move(create)).has_value());
    mirror = ObjectId{createRaw->featureId()};
    record();
    CHECK_THAT(states.back().volume, WithinRel(HoleMirrorModel::expectedVolume(100, 20, 10), kRel));
    CHECK(holeAt(regenerator, mirror, 70, 25, 20) == 1);
    // 2. The plane's position: x = 60 puts the image at 90.
    modify(across(featureId(m.drill), {60, 0, 0}, {1, 0, 0}));
    CHECK(holeAt(regenerator, mirror, 90, 25, 20) == 1);
    // 3. The plane's normal: across y = 16 the image is at (30, 7).
    modify(across(featureId(m.drill), {0, 16, 0}, {0, 1, 0}));
    CHECK(holeAt(regenerator, mirror, 30, 7, 20) == 1);
    // 4. The mirror image of the whole block alone: y from -18 to 32.
    modify(across(featureId(m.drill), {0, 16, 0}, {0, 1, 0}, MirrorScope::Body, false));
    CHECK_THAT(states.back().volume, WithinRel(HoleMirrorModel::expectedVolume(100, 20, 10, 1), kRel));
    bettercad::test::checkPoint(states.back().box.min, 0, -18, 0);
    // 5. Keep the original too: the two blocks overlap, and each fills the
    //    other's hole, so the union is a plain 100 x 68 x 20 block.
    modify(across(featureId(m.drill), {0, 16, 0}, {0, 1, 0}, MirrorScope::Body, true));
    CHECK_THAT(states.back().volume, WithinRel(100.0 * 68.0 * 20.0, kRel));

    // Undo steps back through each state, to the same geometry bit for bit.
    for (std::size_t i = states.size() - 1; i > 0; --i) {
        CAPTURE(i);
        REQUIRE(history.undo(m.doc).has_value());
        const State& expected = states[i - 1];
        CHECK(equivalent(m.doc, expected.document));
        CHECK(m.mirrorOf(mirror) == expected.definition);
        CHECK(m.mirrorOf(mirror).source == featureId(m.drill));
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CHECK(bits(volumeMm3(regenerator, mirror)) == bits(expected.volume));
        CHECK(requireBody(regenerator, mirror).massProperties()->centerOfMass == expected.centre);
        CHECK(requireBody(regenerator, mirror).boundingBox().value() == expected.box);
    }
    for (std::size_t i = 1; i < states.size(); ++i) {
        CAPTURE(i);
        REQUIRE(history.redo(m.doc).has_value());
        CHECK(equivalent(m.doc, states[i].document));
        CHECK(m.mirrorOf(mirror) == states[i].definition);
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CHECK(bits(volumeMm3(regenerator, mirror)) == bits(states[i].volume));
        CHECK(requireBody(regenerator, mirror).massProperties()->centerOfMass == states[i].centre);
        CHECK(requireBody(regenerator, mirror).boundingBox().value() == states[i].box);
    }
    // Undo everything, then redo the creation: the same ID and geometry.
    for (std::size_t i = 0; i < states.size(); ++i) {
        REQUIRE(history.undo(m.doc).has_value());
    }
    CHECK(m.doc.findObject(mirror) == nullptr);
    CHECK(equivalent(m.doc, initial));
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(resultFeatures(m.doc) == std::vector<ObjectId>{m.drill});
    REQUIRE(history.redo(m.doc).has_value());
    CHECK(equivalent(m.doc, states.front().document));
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(bits(volumeMm3(regenerator, mirror)) == bits(states.front().volume));
    // Commands check the feature kind.
    CHECK(errorCode(history.execute(m.doc, std::make_unique<ModifyMirrorCommand>(
                                               featureId(m.drill), across(featureId(m.drill), {0, 0, 0}, {1, 0, 0})))) ==
          ErrorCode::NotFound);
}

TEST_CASE("MirrorFeature_WorksWithRevolvedBody", "[mirror][features][revolve][acceptance]") {
    // The turned part's revolve (R = 15, h = 40 about Z) across the offset
    // plane x = 30: a second cylinder about x = 60.
    TurnedPartModel m;
    auto feature = MirrorFeature::create("Pair", across(FeatureId::fromValue(m.turn.value()), {30, 0, 0}, {1, 0, 0}));
    REQUIRE(feature.has_value());
    const ObjectId pair = m.doc.addObject(std::move(*feature)).value();
    Regenerator regenerator;
    const RegenerationReport report = requireReport(regenerator, m.doc);
    INFO(describe(report));
    REQUIRE(report.succeeded());
    const geometry::Body& body = requireBody(regenerator, pair);
    CHECK(body.isValid());
    CHECK(body.topology().solids == 2);
    CHECK_THAT(volumeMm3(regenerator, pair), WithinRel(2.0 * pi * 225.0 * 40.0, kRel));
    CHECK_THAT(body.boundingBox()->min.x.in(units::mm), WithinAbs(-15.0, kPositionToleranceMm));
    CHECK_THAT(body.boundingBox()->max.x.in(units::mm), WithinAbs(75.0, kPositionToleranceMm));
    CHECK(circles(regenerator, pair, 60, 0, 40, 15).size() == 1); // the image's top rim
    // The revolve's sweep drives both.
    REQUIRE(m.doc.setParameterValue(m.sweep, 270_deg).has_value());
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK_THAT(volumeMm3(regenerator, pair), WithinRel(2.0 * 0.75 * pi * 225.0 * 40.0, kRel));

    // The whole turned part (bored and grooved) mirrored on its own.
    REQUIRE(m.doc.setParameterValue(m.sweep, 360_deg).has_value());
    auto whole = MirrorFeature::create(
        "Image", across(FeatureId::fromValue(m.groove.value()), {30, 0, 0}, {1, 0, 0}, MirrorScope::Body, false));
    REQUIRE(whole.has_value());
    const ObjectId image = m.doc.addObject(std::move(*whole)).value();
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(requireBody(regenerator, image).isValid());
    CHECK_THAT(volumeMm3(regenerator, image), WithinRel(TurnedPartModel::expectedVolume(15, 40, 360, 5), kRel));
    CHECK_THAT(requireBody(regenerator, image).boundingBox()->min.x.in(units::mm), WithinAbs(45.0, kPositionToleranceMm));
    CHECK(circles(regenerator, image, 60, 0, 40, 5).size() == 1); // the bore's rim

    // A revolved cut (the groove, r 13..16 mm at z 15..20) mirrored across
    // z = 20: a second groove at z 20..25, so the band 15..25 is cut, and
    // V = pi ((R^2 - r^2) h - (15^2 - 13^2) 10) = 7440 pi.
    TurnedPartModel cut;
    auto grooves = MirrorFeature::create(
        "Grooves", across(FeatureId::fromValue(cut.groove.value()), {0, 0, 20}, {0, 0, 1}));
    REQUIRE(grooves.has_value());
    const ObjectId both = cut.doc.addObject(std::move(*grooves)).value();
    Regenerator cutRegenerator;
    REQUIRE(requireReport(cutRegenerator, cut.doc).succeeded());
    CHECK(requireBody(cutRegenerator, both).isValid());
    CHECK_THAT(volumeMm3(cutRegenerator, both), WithinRel(7440.0 * pi, kRel));
    CHECK(circles(cutRegenerator, both, 0, 0, 15, 13).size() == 1); // the groove's floor, from z = 15
    CHECK(circles(cutRegenerator, both, 0, 0, 25, 13).size() == 1); // to z = 25
    CHECK(circles(cutRegenerator, both, 0, 0, 25, 15).size() == 1);
    CHECK(circles(cutRegenerator, both, 0, 0, 20, 15).empty()); // no wall left at z = 20
}

TEST_CASE("MirrorFeature_WorksWithExtrudedBody", "[mirror][features][acceptance]") {
    // Sketch -> extrude -> mirror: the cube's size drives both cubes.
    CubeMirrorModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    for (const double size : {6.0, 14.0}) {
        CAPTURE(size);
        REQUIRE(m.doc.setParameterValue(m.size, size * units::mm).has_value());
        CHECK(requireReport(regenerator, m.doc).regenerated == std::vector<ObjectId>{m.sketch, m.cube, m.mirror});
        CHECK_THAT(volumeMm3(regenerator, m.mirror), WithinRel(2.0 * size * size * size, kRel));
        // The source spans x = 15..15 + s, its image -(15 + s)..-15.
        const auto box = requireBody(regenerator, m.mirror).boundingBox().value();
        bettercad::test::checkPoint(box.min, -(15.0 + size), -5, -5);
        bettercad::test::checkPoint(box.max, 15.0 + size, size - 5.0, size - 5.0);
        CHECK(facesOn(regenerator, m.mirror, plane(-(15.0 + size), 0, 0, -1, 0, 0)) == 1);
    }
}

TEST_CASE("MirrorFeature_PlaneThroughSourceGeometry", "[mirror][features][hole][acceptance]") {
    SECTION("a plane through the cube: the source and its image fuse") {
        for (const MirrorScope scope : {MirrorScope::Feature, MirrorScope::Body}) {
            CAPTURE(toString(scope));
            CubeMirrorModel m;
            Regenerator regenerator;
            const auto build = [&](double a) {
                m.setDefinition(m.mirror, across(featureId(m.cube), {a, 0, 0}, {1, 0, 0}, scope, true));
                REQUIRE(requireReport(regenerator, m.doc).succeeded());
                CHECK(requireBody(regenerator, m.mirror).isValid());
                CHECK(requireBody(regenerator, m.mirror).topology().solids == 1);
                return volumeMm3(regenerator, m.mirror);
            };
            // Straddling (x = 18): [15, 25] and [11, 21] make [11, 25].
            CHECK_THAT(build(18), WithinRel(14.0 * 10.0 * 10.0, kRel));
            CHECK_THAT(requireBody(regenerator, m.mirror).boundingBox()->min.x.in(units::mm),
                       WithinAbs(11.0, kPositionToleranceMm));
            // Touching (x = 15): [5, 15] and [15, 25] make one 20 mm bar.
            CHECK_THAT(build(15), WithinRel(2000.0, kRel));
            CHECK(facesOn(regenerator, m.mirror, plane(15, 0, 0, 1, 0, 0)) == 0);
            CHECK(facesOn(regenerator, m.mirror, plane(15, 0, 0, -1, 0, 0)) == 0);
            // Centred (x = 20): the image coincides with the source.
            CHECK_THAT(build(20), WithinRel(1000.0, kRel));
            bettercad::test::checkPoint(requireBody(regenerator, m.mirror).boundingBox()->min, 15, -5, -5);
        }
    }
    SECTION("a boss symmetric about the plane is its own image: the body is unchanged") {
        BossMirrorModel m;
        m.setMirror(m.mirror, across(featureId(m.bossFeature), {30, 0, 0}, {1, 0, 0}));
        Regenerator regenerator;
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CHECK(requireBody(regenerator, m.mirror).isValid());
        CHECK_THAT(volumeMm3(regenerator, m.mirror), WithinRel(50000.0 + 500.0, kRel));
        CHECK(facesOn(regenerator, m.mirror, topFace(15)) == 1);
    }
    SECTION("a hole the plane maps onto itself is refused, not drilled twice") {
        HoleMirrorModel m;
        Regenerator regenerator;
        // The plane x = 30 contains the hole's axis.
        REQUIRE(m.doc.setParameterValue(m.mid, 30_mm).has_value());
        Error error = requireFailure(regenerator, m.doc, m.mirror);
        CHECK(error.code == ErrorCode::FailedPrecondition);
        CHECK(error.message == "Mirror: mirror: the mirror image across the plane through (30, 0, 0) mm facing (1, 0, "
                               "0) is the hole 'Drill' itself: the plane maps the hole centred at (30, 25, 0) mm onto "
                               "itself, so there is nothing to mirror");
        // The plane z = 10 halves the through hole: its image is the same
        // hole drilled from the top.
        m.setMirror(m.mirror, across(featureId(m.drill), {0, 0, 10}, {0, 0, 1}));
        error = requireFailure(regenerator, m.doc, m.mirror);
        CHECK_THAT(error.message, StartsWith("Mirror: mirror: the mirror image across the plane through (0, 0, 10) mm "
                                             "facing (0, 0, 1) is the hole 'Drill' itself"));
        CHECK(regenerator.body(m.drill) != nullptr);
        // The plane x = 32 through the hole: its image at x = 34 overlaps it
        // (the centre lies in the first hole) and is refused by the hole's own
        // rules.
        m.setMirror(m.mirror, across(featureId(m.drill), {32, 0, 0}, {1, 0, 0}));
        error = requireFailure(regenerator, m.doc, m.mirror);
        CHECK(error.code == ErrorCode::FailedPrecondition);
        CHECK(error.message == "Mirror: mirror: the mirror image across the plane through (32, 0, 0) mm facing (1, 0, "
                               "0): hole: the centre (34, 25) mm is not on a face of the body on the plane through (0, "
                               "0, 0) mm facing (0, 0, -1) (1 face(s) lie on that plane elsewhere)");
        CHECK(regenerator.body(m.drill) != nullptr);
    }
}

TEST_CASE("MirrorFeature_MirrorsChamferAndFilletByMirroredReferences", "[mirror][features][chamfer][fillet]") {
    SECTION("an asymmetric chamfer keeps its handedness") {
        // The block's top-left edge (x = 0, z = 20) chamfered 3 mm on the side
        // face (the reference side, -X) and 5 mm on the top, mirrored across
        // x = 50: the top-right edge gets 3 mm on its side (+X) and 5 mm on
        // top. So the top face is 100 - 5 - 5 wide (a mirror that kept -X
        // as the reference side would leave 100 - 5 - 3).
        BlockModel m{"spare", 1_mm};
        const ObjectId bevel = m.add<ChamferFeature>("Bevel", {.target = featureId(m.pad),
                                                               .edges = { ChamferEdge{BlockModel::alongY(0, 20)}},
                                                               .mode = geometry::ChamferMode::TwoDistance,
                                                               .distance = 3_mm,
                                                               .distance2 = 5_mm,
                                                               .referenceSide = Direction3D::unitX().reversed()});
        const ObjectId mirror = m.add<MirrorFeature>("Mirror", across(featureId(bevel), {50, 0, 0}, {1, 0, 0}));
        Regenerator regenerator;
        const RegenerationReport report = requireReport(regenerator, m.doc);
        INFO(describe(report));
        REQUIRE(report.succeeded());
        CHECK(requireBody(regenerator, mirror).isValid());
        CHECK_THAT(volumeMm3(regenerator, mirror), WithinRel(100000.0 - 2.0 * 0.5 * 3.0 * 5.0 * 50.0, kRel));
        CHECK_THAT(areaOn(regenerator, mirror, topFace(20)), WithinRel((100.0 - 5.0 - 5.0) * 50.0, kRel));
        CHECK_THAT(areaOn(regenerator, mirror, plane(0, 0, 0, -1, 0, 0)), WithinRel((20.0 - 3.0) * 50.0, kRel));
        CHECK_THAT(areaOn(regenerator, mirror, plane(100, 0, 0, 1, 0, 0)), WithinRel((20.0 - 3.0) * 50.0, kRel));
    }
    SECTION("a chamfered hole rim: pi c^2 (r + c/3) per rim, by Pappus") {
        HoleMirrorModel m;
        const double holes = HoleMirrorModel::expectedVolume(100, 20, 10);
        const auto rim = geometry::circleSignature(Point3D{30_mm, 25_mm, 20_mm}, Direction3D::unitZ(), 5_mm);
        REQUIRE(rim.has_value());
        const ObjectId ease = m.add<ChamferFeature>("Ease", {.target = featureId(m.mirror), .edges = { ChamferEdge{*rim}}, .distance = 1_mm});
        const ObjectId eases = m.add<MirrorFeature>("Eases", across(featureId(ease), {50, 0, 0}, {1, 0, 0}));
        Regenerator regenerator;
        const RegenerationReport report = requireReport(regenerator, m.doc);
        INFO(describe(report));
        REQUIRE(report.succeeded());
        CHECK_THAT(volumeMm3(regenerator, eases), WithinRel(holes - 2.0 * pi * (5.0 + 1.0 / 3.0), kRel));
        CHECK(holeAt(regenerator, eases, 30, 25, 20, 6) == 1);
        CHECK(holeAt(regenerator, eases, 70, 25, 20, 6) == 1);
        // About a plane whose image rim does not exist, the reference matches
        // no edge; no other edge is taken instead.
        m.setMirror(eases, across(featureId(ease), {45, 0, 0}, {1, 0, 0}));
        const Error error = requireFailure(regenerator, m.doc, eases);
        CHECK(error.code == ErrorCode::NotFound);
        CHECK(error.message == "Eases: mirror: the mirror image across the plane through (45, 0, 0) mm facing (1, 0, "
                               "0): chamfer: edge reference 1 (circle around (60, 25, 20) mm with axis (0, 0, 1) and "
                               "radius 5 mm) matches no edge of the body");
    }
    SECTION("a filleted hole rim: 2 pi (r + u) r_f^2 (1 - pi/4) per rim") {
        HoleMirrorModel m;
        const double holes = HoleMirrorModel::expectedVolume(100, 20, 10);
        const double rf = 2.0;
        const double centroid = rf * (10.0 - 3.0 * pi) / (12.0 - 3.0 * pi);
        const auto rim = geometry::circleSignature(Point3D{30_mm, 25_mm, 20_mm}, Direction3D::unitZ(), 5_mm);
        REQUIRE(rim.has_value());
        const ObjectId soften = m.add<FilletFeature>("Soften", {.target = featureId(m.mirror), .edges = {*rim}, .radius = 2_mm});
        const ObjectId rounds = m.add<MirrorFeature>("Rounds", across(featureId(soften), {50, 0, 0}, {1, 0, 0}));
        Regenerator regenerator;
        const RegenerationReport report = requireReport(regenerator, m.doc);
        INFO(describe(report));
        REQUIRE(report.succeeded());
        CHECK(requireBody(regenerator, rounds).isValid());
        CHECK_THAT(volumeMm3(regenerator, rounds),
                   WithinRel(holes - 2.0 * 2.0 * pi * (5.0 + centroid) * filletCorner(rf), kRel));
        CHECK(holeAt(regenerator, rounds, 30, 25, 20, 7) == 1);
        CHECK(holeAt(regenerator, rounds, 70, 25, 20, 7) == 1);

        // The fillet's radius drives both rims: 2 -> 1.5 mm rebuilds the
        // fillet and its mirror, and both rims meet the top face at 6.5 mm.
        m.BlockModel::setDefinition<FilletFeature>(soften,
                                                   {.target = featureId(m.mirror), .edges = {*rim}, .radius = 1.5_mm});
        CHECK(requireReport(regenerator, m.doc).regenerated == std::vector<ObjectId>{soften, rounds});
        const double smaller = 1.5 * (10.0 - 3.0 * pi) / (12.0 - 3.0 * pi);
        CHECK_THAT(volumeMm3(regenerator, rounds),
                   WithinRel(holes - 2.0 * 2.0 * pi * (5.0 + smaller) * filletCorner(1.5), kRel));
        for (const double x : {30.0, 70.0}) {
            CAPTURE(x);
            CHECK(holeAt(regenerator, rounds, x, 25, 20, 6.5) == 1);
            CHECK(holeAt(regenerator, rounds, x, 25, 20, 7) == 0); // no stale rim
        }
    }
    SECTION("an edge the plane maps onto the feature's own edges is refused, not blended again") {
        // One chamfer of both top edges along Y: across x = 50 each is the
        // other's mirror image, and both are already chamfered.
        BlockModel m{"spare", 1_mm};
        const ObjectId bevel = m.add<ChamferFeature>(
            "Bevel", {.target = featureId(m.pad),
                      .edges = { ChamferEdge{BlockModel::alongY(0, 20)}, ChamferEdge{BlockModel::alongY(100, 20)}},
                      .distance = 2_mm});
        const ObjectId mirror = m.add<MirrorFeature>("Mirror", across(featureId(bevel), {50, 0, 0}, {1, 0, 0}));
        Regenerator regenerator;
        Error error = requireFailure(regenerator, m.doc, mirror);
        CHECK(error.code == ErrorCode::FailedPrecondition);
        CHECK(error.message == "Mirror: mirror: the mirror image across the plane through (50, 0, 0) mm facing (1, 0, "
                               "0) of edge reference 1 (line through (0, 0, 20) mm along (0, 1, 0)) of the chamfer "
                               "'Bevel' is its edge reference 2 (line through (100, 0, 20) mm along (0, 1, 0)), which "
                               "is already chamfered, so there is nothing to mirror onto");
        CHECK(regenerator.body(bevel) != nullptr); // the chamfer itself is intact
        // Across y = 25 each edge along Y is its own mirror image.
        m.setDefinition<MirrorFeature>(mirror, across(featureId(bevel), {0, 25, 0}, {0, 1, 0}));
        error = requireFailure(regenerator, m.doc, mirror);
        CHECK_THAT(error.message, ContainsSubstring("of edge reference 1 (line through (0, 0, 20) mm along (0, 1, 0)) of "
                                                    "the chamfer 'Bevel' is that edge itself, which is already "
                                                    "chamfered"));
        // Across z = 10 the images are the bottom edges, new ones: four
        // chamfers of 1/2 d^2 L each.
        m.setDefinition<MirrorFeature>(mirror, across(featureId(bevel), {0, 0, 10}, {0, 0, 1}));
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CHECK(requireBody(regenerator, mirror).isValid());
        CHECK_THAT(volumeMm3(regenerator, mirror), WithinRel(100000.0 - 4.0 * 0.5 * 4.0 * 50.0, kRel));
        CHECK_THAT(areaOn(regenerator, mirror, plane(0, 0, 0, 0, 0, -1)), WithinRel((100.0 - 4.0) * 50.0, kRel));

        // A fillet of a hole rim across a plane through the hole's axis.
        HoleMirrorModel holes;
        const auto rim = geometry::circleSignature(Point3D{30_mm, 25_mm, 20_mm}, Direction3D::unitZ(), 5_mm);
        REQUIRE(rim.has_value());
        const ObjectId soften =
            holes.add<FilletFeature>("Soften", {.target = featureId(holes.mirror), .edges = {*rim}, .radius = 2_mm});
        const ObjectId rounds = holes.add<MirrorFeature>("Rounds", across(featureId(soften), {30, 0, 0}, {1, 0, 0}));
        Regenerator holeRegenerator;
        error = requireFailure(holeRegenerator, holes.doc, rounds);
        CHECK(error.code == ErrorCode::FailedPrecondition);
        CHECK(error.message == "Rounds: mirror: the mirror image across the plane through (30, 0, 0) mm facing (1, 0, "
                               "0) of edge reference 1 (circle around (30, 25, 20) mm with axis (0, 0, 1) and radius 5 "
                               "mm) of the fillet 'Soften' is that edge itself, which is already filleted, so there is "
                               "nothing to mirror onto");
    }
}

TEST_CASE("MirrorFeature_BodyScopeMirrorsTheWholeBody", "[mirror][features][hole][acceptance]") {
    SECTION("the drilled block across its end face") {
        // HoleBlockModel: the hole at (50, 25); across x = 100 its image is
        // at 150, and the two blocks fuse into one 200 mm block.
        HoleBlockModel m;
        const ObjectId pair = m.add<MirrorFeature>(
            "Pair", across(featureId(m.drill), {100, 0, 0}, {1, 0, 0}, MirrorScope::Body, true));
        Regenerator regenerator;
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        const geometry::Body& body = requireBody(regenerator, pair);
        CHECK(body.isValid());
        CHECK(body.topology().solids == 1);
        CHECK_THAT(volumeMm3(regenerator, pair), WithinRel(2.0 * HoleBlockModel::expectedVolume(100, 50, 20, 10), kRel));
        bettercad::test::checkPoint(body.boundingBox()->max, 200, 50, 20);
        CHECK(holeAt(regenerator, pair, 50, 25, 20) == 1);
        CHECK(holeAt(regenerator, pair, 150, 25, 20) == 1);
        CHECK(facesOn(regenerator, pair, plane(100, 0, 0, 1, 0, 0)) == 0);
        // Without the original: the image alone, from 100 to 200.
        m.BlockModel::setDefinition<MirrorFeature>(
            pair, across(featureId(m.drill), {100, 0, 0}, {1, 0, 0}, MirrorScope::Body, false));
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CHECK_THAT(volumeMm3(regenerator, pair), WithinRel(HoleBlockModel::expectedVolume(100, 50, 20, 10), kRel));
        bettercad::test::checkPoint(requireBody(regenerator, pair).boundingBox()->min, 100, 0, 0);
        CHECK(holeAt(regenerator, pair, 150, 25, 20) == 1);
        // Later features work on the mirror image's faces: a second hole in
        // its top face.
        const ObjectId extra = m.addHole("Extra", {.target = featureId(pair),
                                                   .face = topFace(),
                                                   .center = Point2D{180_mm, 25_mm},
                                                   .diameter = 10_mm});
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CHECK_THAT(volumeMm3(regenerator, extra), WithinRel(100000.0 - 1000.0 * pi, kRel));
    }
    SECTION("a linear pattern's body (a pattern cannot be repeated, but its body can be mirrored)") {
        // Five holes 20 mm apart in a 120 mm block, mirrored across its end x = 120.
        HoleRowModel m;
        const ObjectId pair = m.add<MirrorFeature>(
            "Pair", across(featureId(m.holes), {120, 0, 0}, {1, 0, 0}, MirrorScope::Body, true));
        Regenerator regenerator;
        const RegenerationReport report = requireReport(regenerator, m.doc);
        INFO(describe(report));
        REQUIRE(report.succeeded());
        CHECK(requireBody(regenerator, pair).isValid());
        CHECK(requireBody(regenerator, pair).topology().solids == 1);
        CHECK_THAT(volumeMm3(regenerator, pair), WithinRel(2.0 * HoleRowModel::expectedVolume(120, 20, 10, 5), kRel));
        for (const double x : {20.0, 100.0, 140.0, 220.0}) {
            CAPTURE(x);
            CHECK(holeAt(regenerator, pair, x, 25, 20) == 1);
        }
    }
}

TEST_CASE("MirrorFeature_RefusesUnsupportedSources", "[mirror][features]") {
    SECTION("a feature mirror of a pattern or a mirror (their bodies can be mirrored)") {
        HoleRowModel rows;
        const ObjectId mirror = rows.add<MirrorFeature>("Mirror", across(featureId(rows.holes), {60, 0, 0}, {0, 1, 0}));
        Regenerator regenerator;
        Error error = requireFailure(regenerator, rows.doc, mirror);
        CHECK(error.code == ErrorCode::FailedPrecondition);
        CHECK(error.message == "Mirror: mirror: a feature mirror cannot repeat a linear_pattern; mirror its body instead");

        CubeMirrorModel cubes;
        const ObjectId again = cubes.addMirror("Again", across(featureId(cubes.mirror), {0, 0, 0}, {0, 1, 0}));
        Regenerator cubeRegenerator;
        CHECK(requireFailure(cubeRegenerator, cubes.doc, again).message ==
              "Again: mirror: a feature mirror cannot repeat a mirror; mirror its body instead");
    }
    SECTION("a pattern of a mirror") {
        CubeMirrorModel m;
        auto row = LinearPatternFeature::create(
            "Row", {.source = featureId(m.mirror), .first = {.direction = {0.0, 1.0, 0.0}, .count = 2, .spacing = 20_mm}});
        REQUIRE(row.has_value());
        const ObjectId rowId = m.doc.addObject(std::move(*row)).value();
        Regenerator regenerator;
        const Error error = requireFailure(regenerator, m.doc, rowId);
        CHECK(error.message == "Row: linear pattern: a linear pattern cannot repeat a mirror");
    }
    SECTION("an intersect extrude and a sketch") {
        CubeMirrorModel m;
        auto clip = ExtrudeFeature::create("Clip", {.profile = SketchId::fromValue(m.sketch.value()),
                                                    .depth = 5_mm,
                                                    .operation = FeatureOperation::Intersect,
                                                    .target = featureId(m.cube)});
        REQUIRE(clip.has_value());
        const ObjectId clipId = m.doc.addObject(std::move(*clip)).value();
        m.setDefinition(m.mirror, across(featureId(clipId), {0, 0, 0}, {1, 0, 0}));
        Regenerator regenerator;
        Error error = requireFailure(regenerator, m.doc, m.mirror);
        CHECK(error.code == ErrorCode::FailedPrecondition);
        CHECK(error.message == "Mirror: mirror: repeating an intersect extrude is not supported: its instances would "
                               "only intersect each other");
        m.setDefinition(m.mirror, across(FeatureId::fromValue(m.sketch.value()), {0, 0, 0}, {1, 0, 0}));
        error = requireFailure(regenerator, m.doc, m.mirror);
        CHECK(error.code == ErrorCode::FailedPrecondition);
        CHECK(error.message == "Mirror: a mirror needs the body of its source feature");
    }
}

TEST_CASE("MirrorFeature_InvalidInputsFailWithStructuredDiagnostics", "[mirror][features][acceptance]") {
    HoleMirrorModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());

    SECTION("a missing source") {
        REQUIRE(m.doc.removeObject(m.drill).has_value());
        const Error error = requireFailure(regenerator, m.doc, m.mirror);
        CHECK(error.code == ErrorCode::NotFound);
        CHECK(error.message == "object:11 references object:9, which does not exist");
    }
    SECTION("a missing offset parameter") {
        REQUIRE(m.doc.removeParameter(m.mid).has_value());
        const Error error = requireFailure(regenerator, m.doc, m.mirror);
        CHECK(error.code == ErrorCode::NotFound);
        CHECK(error.message == "object:11 references object:10, which does not exist");
    }
    SECTION("an offset parameter that is an angle") {
        const ParameterId tilt = m.doc.createParameter("tilt", 5_deg, units::deg).value();
        MirrorDefinition d = m.mirrorOf(m.mirror);
        d.plane.offsetParameter = tilt;
        m.setMirror(m.mirror, d);
        const Error error = requireFailure(regenerator, m.doc, m.mirror);
        CHECK(error.code == ErrorCode::DimensionMismatch);
        CHECK_THAT(error.message, StartsWith("Mirror: mirror: "));
    }
    SECTION("a source that fails blocks the mirror") {
        REQUIRE(m.doc.setParameterValue(m.diameter, 0_mm).has_value());
        const RegenerationReport report = requireReport(regenerator, m.doc);
        INFO(describe(report));
        CHECK(report.failed == std::vector<ObjectId>{m.drill});
        CHECK(std::ranges::find(report.blocked, m.mirror) != report.blocked.end());
        CHECK(regenerator.body(m.mirror) == nullptr);
    }
}

TEST_CASE("MirrorFeature_FailuresLeaveTheDocumentAndUpstreamBodiesIntact", "[mirror][features][acceptance]") {
    HoleMirrorModel m;
    CommandHistory history;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const Document before = m.doc.clone();
    const std::uint64_t revision = m.doc.revision();
    const double drilled = volumeMm3(regenerator, m.drill);
    const double mirrored = volumeMm3(regenerator, m.mirror);

    // An invalid edit is refused before it touches the document.
    MirrorDefinition invalid = m.mirrorOf(m.mirror);
    invalid.plane.normal = {0.0, 0.0, 0.0};
    CHECK(errorCode(history.execute(m.doc, std::make_unique<ModifyMirrorCommand>(featureId(m.mirror), invalid))) ==
          ErrorCode::InvalidArgument);
    CHECK(equivalent(m.doc, before));
    CHECK(m.doc.revision() == revision);
    CHECK_FALSE(history.canUndo());

    // A valid edit the geometry cannot take fails at regeneration only.
    REQUIRE(history
                .execute(m.doc, std::make_unique<ModifyMirrorCommand>(featureId(m.mirror),
                                                                       across(featureId(m.drill), {64, 0, 0}, {1, 0, 0})))
                .has_value());
    const Document edited = m.doc.clone();
    const Error error = requireFailure(regenerator, m.doc, m.mirror);
    CHECK(error.code == ErrorCode::FailedPrecondition);
    CHECK(equivalent(m.doc, edited)); // regeneration does not change the model
    CHECK(regenerator.state(m.drill) == NodeState::UpToDate);
    CHECK(volumeMm3(regenerator, m.drill) == drilled);

    REQUIRE(history.undo(m.doc).has_value());
    CHECK(equivalent(m.doc, before));
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(volumeMm3(regenerator, m.mirror) == mirrored);
}

TEST_CASE("MirrorFeature_ReusesStableSourceReference", "[mirror][features][acceptance]") {
    HoleMirrorModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const double mirrored = volumeMm3(regenerator, m.mirror);
    REQUIRE(m.doc.rename(m.drill, "Bore").has_value());
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(m.mirrorOf(m.mirror).source == featureId(m.drill));
    CHECK(volumeMm3(regenerator, m.mirror) == mirrored);
    auto removed = m.doc.removeObject(m.drill);
    REQUIRE(removed.has_value());
    const Error error = requireFailure(regenerator, m.doc, m.mirror);
    CHECK(error.code == ErrorCode::NotFound);
    CHECK(error.message == "object:11 references object:9, which does not exist");
    REQUIRE(m.doc.insertObject(std::move(*removed)).has_value());
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(volumeMm3(regenerator, m.mirror) == mirrored);
}

TEST_CASE("MirrorFeature_RegenerationIsDeterministic", "[mirror][features]") {
    HoleMirrorModel m;
    Regenerator first;
    Regenerator second;
    REQUIRE(requireReport(first, m.doc).succeeded());
    REQUIRE(requireReport(second, m.doc).succeeded());
    const geometry::Body& a = requireBody(first, m.mirror);
    const geometry::Body& b = requireBody(second, m.mirror);
    const auto pa = a.massProperties().value();
    const auto pb = b.massProperties().value();
    CHECK(bits(pa.volume.si()) == bits(pb.volume.si()));
    CHECK(bits(pa.surfaceArea.si()) == bits(pb.surfaceArea.si()));
    CHECK(pa.centerOfMass == pb.centerOfMass);
    CHECK(a.boundingBox().value() == b.boundingBox().value());
    CHECK(a.topology() == b.topology());
    CHECK(resolveMirrorReflection(m.mirrorOf(m.mirror), m.doc).value() ==
          resolveMirrorReflection(m.mirrorOf(m.mirror), m.doc).value());
}
