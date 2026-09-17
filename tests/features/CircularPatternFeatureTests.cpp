#include "FeatureTestSupport.hpp"
#include "TestHelpers.hpp"
#include "core/geometry/GeometryTestSupport.hpp"
#include "support/BracketModel.hpp"
#include "support/FilletModels.hpp"
#include "support/HoleModels.hpp"
#include "support/PatternModels.hpp"
#include "support/TurnedPartModel.hpp"

#include <bettercad/core/document/Commands.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/geometry/Edges.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/features/ChamferFeature.hpp>
#include <bettercad/features/CircularPatternFeature.hpp>
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
using bettercad::test::BoltCircleModel;
using bettercad::test::countOf;
using bettercad::test::CubeRingModel;
using bettercad::test::describe;
using bettercad::test::errorCode;
using bettercad::test::filletCorner;
using bettercad::test::HoleBlockModel;
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
// Patterns of boxes, holes and turned parts have planar, cylindrical and
// conical faces meeting in lines and circles, which the kernel computes to
// rounding level (kRelTight, as for the hole and boolean tests).
constexpr double kRel = bettercad::test::kRelTight;

FeatureId featureId(ObjectId id) {
    return FeatureId::fromValue(id.value());
}

double radians(double degrees) {
    return degrees * pi / 180.0;
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

/// The plane through (x, y, z) mm with normal (nx, ny, nz).
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

/// Holes of radius @p r on the bolt circle of radius @p orbit about (cx, cy), at @p degrees, found on z = @p z.
std::size_t holeAt(const Regenerator& regenerator, ObjectId feature, double degrees, double z = 0.0,
                   double r = 5.0, double orbit = 40.0, double cx = 0.0, double cy = 0.0) {
    return circles(regenerator, feature, cx + orbit * std::cos(radians(degrees)),
                   cy + orbit * std::sin(radians(degrees)), z, r)
        .size();
}

std::uint64_t bits(double value) {
    return std::bit_cast<std::uint64_t>(value);
}

/// Instance angles of a resolved pattern, in degrees.
std::vector<double> anglesOf(const CircularPatternDefinition& definition, const Document& doc) {
    const auto instances = resolveCircularPatternInstances(definition, doc);
    REQUIRE(instances.has_value());
    std::vector<double> angles;
    for (const CircularPatternInstance& instance : *instances) {
        angles.push_back(instance.angle.in(units::deg));
    }
    return angles;
}

void checkAngles(const std::vector<double>& actual, const std::vector<double>& expected) {
    REQUIRE(actual.size() == expected.size());
    for (std::size_t i = 0; i < actual.size(); ++i) {
        CAPTURE(i);
        CHECK_THAT(actual[i], WithinAbs(expected[i], 1e-12));
    }
}

// Independent rotation (Rodrigues) for expected positions: p turned by t
// (degrees) about the unit axis k through the origin.
using V = std::array<double, 3>;

V rodrigues(const V& p, const V& kIn, double degrees) {
    const double n = std::sqrt(kIn[0] * kIn[0] + kIn[1] * kIn[1] + kIn[2] * kIn[2]);
    const V k{kIn[0] / n, kIn[1] / n, kIn[2] / n};
    const double along = p[0] * k[0] + p[1] * k[1] + p[2] * k[2];
    const V par{k[0] * along, k[1] * along, k[2] * along};
    const V perp{p[0] - par[0], p[1] - par[1], p[2] - par[2]};
    const V kxp{k[1] * perp[2] - k[2] * perp[1], k[2] * perp[0] - k[0] * perp[2], k[0] * perp[1] - k[1] * perp[0]};
    const double c = std::cos(radians(degrees));
    const double s = std::sin(radians(degrees));
    return {par[0] + c * perp[0] + s * kxp[0], par[1] + c * perp[1] + s * kxp[1], par[2] + c * perp[2] + s * kxp[2]};
}

double distanceToAxis(const V& p, const V& kIn) {
    const double n = std::sqrt(kIn[0] * kIn[0] + kIn[1] * kIn[1] + kIn[2] * kIn[2]);
    const V k{kIn[0] / n, kIn[1] / n, kIn[2] / n};
    const double along = p[0] * k[0] + p[1] * k[1] + p[2] * k[2];
    const V perp{p[0] - k[0] * along, p[1] - k[1] * along, p[2] - k[2] * along};
    return std::sqrt(perp[0] * perp[0] + perp[1] * perp[1] + perp[2] * perp[2]);
}

CircularPatternDefinition literalRing() {
    return {.source = FeatureId::fromValue(4), .axis = {.origin = Point3D{}, .direction = {0.0, 0.0, 1.0}}, .count = 4};
}

Error refusal(const CircularPatternDefinition& definition) {
    auto feature = CircularPatternFeature::create("P", definition);
    REQUIRE_FALSE(feature.has_value());
    return feature.error();
}

/// Makes the bolt circle's pattern an included-angle one of @p count holes over @p degrees.
void includedAngle(BoltCircleModel& m, std::uint32_t count, double degrees,
                   RotationDirection direction = RotationDirection::Positive) {
    CircularPatternDefinition d = m.definitionOf(m.bolts);
    d.countParameter.reset();
    d.count = count;
    d.spacing = CircularSpacing::IncludedAngle;
    d.angle = degrees * units::deg;
    d.direction = direction;
    m.setDefinition(m.bolts, d);
}

} // namespace

TEST_CASE("CircularPattern_DefinitionIsValidatedOnCreateAndEdit", "[pattern][circular][features]") {
    const CircularPatternDefinition good = literalRing();
    REQUIRE(CircularPatternFeature::create("P", good).has_value());
    CircularPatternDefinition d = good;

    SECTION("a source and valid parameter IDs") {
        d.source = FeatureId{};
        CHECK(refusal(d).message == "a circular pattern needs a source feature");
        d = good;
        d.countParameter = ParameterId{};
        CHECK(refusal(d).message == "the pattern's parameter IDs must be valid");
    }
    SECTION("a full circle takes no angle") {
        d.angle = 30_deg;
        CHECK(refusal(d).message == "a full-circle pattern takes no angle: its instances are 360 deg / count apart");
        d.angle = 0_deg;
        d.angleParameter = ParameterId::fromValue(3);
        CHECK(refusal(d).message == "a full-circle pattern takes no angle: its instances are 360 deg / count apart");
    }
    SECTION("driving parameters replace the literal count and angle") {
        d.count = 0;
        d.countParameter = ParameterId::fromValue(2);
        d.spacing = CircularSpacing::IncludedAngle;
        d.angle = 0_deg;
        d.angleParameter = ParameterId::fromValue(3);
        CHECK(CircularPatternFeature::create("P", d).has_value());
    }
    SECTION("an invalid edit leaves the feature unchanged") {
        auto feature = CircularPatternFeature::create("P", good);
        REQUIRE(feature.has_value());
        d.axis.direction = {0.0, 0.0, 0.0};
        CHECK(errorCode((*feature)->setDefinition(d)) == ErrorCode::InvalidArgument);
        CHECK((*feature)->definition() == good);
        const auto unchanged = (*feature)->setDefinition(good);
        REQUIRE(unchanged.has_value());
        CHECK_FALSE(*unchanged);
    }
}

TEST_CASE("CircularPattern_DependsOnItsSourceAndParameters", "[pattern][circular][features]") {
    CubeRingModel m;
    const auto* ring = m.doc.findObjectAs<CircularPatternFeature>(m.ring);
    REQUIRE(ring != nullptr);
    CHECK(ring->dependencies() == std::vector<ObjectId>{m.cube, ObjectId{m.count}});
    CHECK(ring->target() == featureId(m.cube)); // the pattern consumes its source
    CHECK(ring->typeName() == "circular_pattern");
    CHECK(equivalent(*ring->clone(), *ring));
    const ParameterId span = m.doc.createParameter("span", 90_deg, units::deg).value();
    CircularPatternDefinition d = m.definitionOf(m.ring);
    d.spacing = CircularSpacing::IncludedAngle;
    d.angleParameter = span;
    m.setDefinition(m.ring, d);
    CHECK(m.doc.findObjectAs<CircularPatternFeature>(m.ring)->dependencies() ==
          std::vector<ObjectId>{m.cube, ObjectId{m.count}, ObjectId{span}});
}

TEST_CASE("CircularPattern_CountIncludesOriginal", "[pattern][circular][features][acceptance]") {
    // count = N: the source and N - 1 turned copies.
    const Axis3D z{Point3D{}, Direction3D::unitZ()};
    const auto one = circularPatternInstances(z, 1, circularPatternStep(CircularSpacing::FullCircle, 1, {}, {}));
    REQUIRE(one.size() == 1);
    CHECK(one[0].index == 0);
    CHECK(one[0].angle == Angle{});
    CHECK(one[0].motion.isTranslation()); // the source stays where it is
    const auto two = circularPatternInstances(z, 2, circularPatternStep(CircularSpacing::FullCircle, 2, {}, {}));
    REQUIRE(two.size() == 2);
    CHECK_THAT(two[1].angle.in(units::deg), WithinAbs(180.0, 1e-12));

    CubeRingModel m;
    Regenerator regenerator;
    for (const double count : {1.0, 2.0, 4.0}) {
        CAPTURE(count);
        REQUIRE(m.doc.setParameterValue(m.count, count, kUnitless).has_value());
        const RegenerationReport report = requireReport(regenerator, m.doc);
        INFO(describe(report));
        REQUIRE(report.succeeded());
        CHECK(requireBody(regenerator, m.ring).topology().solids == static_cast<std::size_t>(count));
        CHECK_THAT(volumeMm3(regenerator, m.ring), WithinRel(1000.0 * count, kRel));
    }
    // Count 1 is the source alone.
    REQUIRE(m.doc.setParameterValue(m.count, 1.0, kUnitless).has_value());
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(requireBody(regenerator, m.ring).boundingBox().value() ==
          requireBody(regenerator, m.cube).boundingBox().value());
}

TEST_CASE("CircularPattern_FullCircleDoesNotDuplicate360Degrees", "[pattern][circular][features][acceptance]") {
    // N instances 360/N apart: the last at (N - 1) 360/N, never at 360.
    const Axis3D z{Point3D{}, Direction3D::unitZ()};
    for (const std::size_t n : {1U, 2U, 3U, 4U, 6U, 7U, 12U}) {
        CAPTURE(n);
        const auto instances =
            circularPatternInstances(z, n, circularPatternStep(CircularSpacing::FullCircle, n, {}, {}));
        REQUIRE(instances.size() == n);
        CHECK_THAT(instances.back().angle.in(units::deg),
                   WithinAbs(360.0 * static_cast<double>(n - 1) / static_cast<double>(n), 1e-12));
        CHECK(instances.back().angle.in(units::deg) < 360.0 - 1e-9);
    }
    // Four cubes, not five: the fourth is at 270 degrees.
    CubeRingModel m;
    checkAngles(anglesOf(m.definitionOf(m.ring), m.doc), {0.0, 90.0, 180.0, 270.0});
    // A duplicate hole at 360 degrees would be a hole in a hole and fail; the
    // six-hole bolt circle builds.
    BoltCircleModel bolts;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, bolts.doc).succeeded());
    CHECK_THAT(volumeMm3(regenerator, bolts.bolts),
               WithinRel(BoltCircleModel::expectedVolume(60, 10, 10, 6), kRel));
    // An included angle may not reach 360 degrees: its ends would coincide.
    CircularPatternDefinition d = literalRing();
    d.spacing = CircularSpacing::IncludedAngle;
    d.angle = 360_deg;
    CHECK(refusal(d).message == "the included angle must be less than 360 deg, got 360 deg: the last instance would "
                                "land on the source (use a full circle)");
}

TEST_CASE("CircularPattern_FourInstancesAreAtQuadrants", "[pattern][circular][features][acceptance]") {
    // The cube centred at (50, 0, 5) about the Z axis: centres (50, 0),
    // (0, 50), (-50, 0), (0, -50).
    CubeRingModel m;
    const auto instances = resolveCircularPatternInstances(m.definitionOf(m.ring), m.doc);
    REQUIRE(instances.has_value());
    REQUIRE(instances->size() == 4);
    for (std::size_t i = 0; i < 4; ++i) {
        CAPTURE(i);
        CHECK((*instances)[i].index == i);
        CHECK_THAT((*instances)[i].angle.in(units::deg), WithinAbs(90.0 * static_cast<double>(i), 1e-12));
        const Point3D centre = (*instances)[i].motion.apply(Point3D{50_mm, 0_mm, 5_mm});
        bettercad::test::checkPoint(centre, 50.0 * std::cos(radians(90.0 * static_cast<double>(i))),
                                    50.0 * std::sin(radians(90.0 * static_cast<double>(i))), 5.0);
    }

    Regenerator regenerator;
    const RegenerationReport report = requireReport(regenerator, m.doc);
    INFO(describe(report));
    REQUIRE(report.succeeded());
    CHECK(report.regenerated == std::vector<ObjectId>{m.sketch, m.cube, m.ring});
    const geometry::Body& body = requireBody(regenerator, m.ring);
    CHECK(body.isValid());
    CHECK(body.topology().solids == 4);
    const auto box = body.boundingBox().value();
    bettercad::test::checkPoint(box.min, -55, -55, 0);
    bettercad::test::checkPoint(box.max, 55, 55, 10);
    bettercad::test::checkPoint(body.massProperties()->centerOfMass, 0, 0, 5);
    // Each cube's outer (x = 55) and inner (x = 45) side, turned by its
    // angle: one face each, where cos and sin put it.
    for (const double degrees : {0.0, 90.0, 180.0, 270.0}) {
        CAPTURE(degrees);
        const double c = std::cos(radians(degrees));
        const double s = std::sin(radians(degrees));
        CHECK(facesOn(regenerator, m.ring, plane(55.0 * c, 55.0 * s, 0, c, s, 0)) == 1);
        CHECK(facesOn(regenerator, m.ring, plane(45.0 * c, 45.0 * s, 0, -c, -s, 0)) == 1);
    }
    CHECK(facesOn(regenerator, m.ring, plane(55.0 * std::cos(radians(45.0)), 55.0 * std::sin(radians(45.0)), 0,
                                             std::cos(radians(45.0)), std::sin(radians(45.0)), 0)) == 0);
}

TEST_CASE("CircularPattern_BodyPatternMatchesAnalyticVolume", "[pattern][circular][features][acceptance]") {
    CubeRingModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    // V = count x V_source = 4 x 10^3.
    const auto props = requireBody(regenerator, m.ring).massProperties().value();
    CHECK_THAT(props.volume.in(units::mm3), WithinRel(4000.0, kRel));
    CHECK_THAT(props.surfaceArea.in(units::mm2), WithinRel(4.0 * 600.0, kRel));
    CHECK_THAT(volumeMm3(regenerator, m.cube), WithinRel(1000.0, kRel)); // the source, untouched
    CHECK(resultFeatures(m.doc) == std::vector<ObjectId>{m.ring});
}

TEST_CASE("CircularPattern_TouchingAndOverlappingInstancesFuse", "[pattern][circular][features][acceptance]") {
    // New-body instances are united, as for linear patterns: separate where
    // they do not meet (the quadrant test), fused where they touch or overlap.
    CubeRingModel m;
    Regenerator regenerator;
    CircularPatternDefinition d = m.definitionOf(m.ring);
    d.countParameter.reset();
    SECTION("touching: the cube and its half turn about x = 45 form one 20 mm bar") {
        d.axis.origin = Point3D{45_mm, 0_mm, 0_mm};
        d.count = 2;
        m.setDefinition(m.ring, d);
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        const geometry::Body& body = requireBody(regenerator, m.ring);
        CHECK(body.isValid());
        CHECK(body.topology().solids == 1);
        CHECK_THAT(volumeMm3(regenerator, m.ring), WithinRel(2000.0, kRel));
        bettercad::test::checkPoint(body.boundingBox()->min, 35, -5, 0);
        bettercad::test::checkPoint(body.boundingBox()->max, 55, 5, 10);
        CHECK(facesOn(regenerator, m.ring, plane(45, 0, 0, 1, 0, 0)) == 0); // no inner faces
        CHECK(facesOn(regenerator, m.ring, plane(45, 0, 0, -1, 0, 0)) == 0);
    }
    SECTION("overlapping: a half turn about (50, 2.5) covers half the cube again") {
        // [45, 55] x [-5, 5] and [45, 55] x [0, 10]: the union is 10 x 15 x 10.
        d.axis.origin = Point3D{50_mm, 2.5_mm, 0_mm};
        d.count = 2;
        m.setDefinition(m.ring, d);
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        const geometry::Body& body = requireBody(regenerator, m.ring);
        CHECK(body.isValid());
        CHECK(body.topology().solids == 1);
        CHECK_THAT(volumeMm3(regenerator, m.ring), WithinRel(1500.0, kRel));
        bettercad::test::checkPoint(body.boundingBox()->min, 45, -5, 0);
        bettercad::test::checkPoint(body.boundingBox()->max, 55, 10, 10);
    }
    SECTION("overlapping: 36 cubes 10 degrees apart fuse into one ring") {
        // Each square [45, 55] x [-5, 5] covers the sector within 5 degrees
        // of its own axis between the lines 45 and 55 mm from the centre, so
        // the union is the band between two regular 36-gons of apothems 45
        // and 55 mm, n a^2 tan(pi/n) each, plus, where neighbours cross, the
        // tip of each square's outer corners beyond its neighbour's outer
        // side: 72 right triangles with legs 5 - 55 tan 5 deg and
        // 55 - (55 - 5 sin 10 deg) / cos 10 deg.
        d.count = 36;
        m.setDefinition(m.ring, d);
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        const geometry::Body& body = requireBody(regenerator, m.ring);
        CHECK(body.isValid());
        CHECK(body.topology().solids == 1);
        const double band = 36.0 * std::tan(radians(5.0)) * (55.0 * 55.0 - 45.0 * 45.0);
        const double tipY = 5.0 - 55.0 * std::tan(radians(5.0));
        const double tipX = 55.0 - (55.0 - 5.0 * std::sin(radians(10.0))) / std::cos(radians(10.0));
        const double expected = 10.0 * (band + 72.0 * tipX * tipY / 2.0);
        CHECK_THAT(expected, WithinAbs(31498.16, 0.01));
        CHECK_THAT(volumeMm3(regenerator, m.ring), WithinRel(expected, kRel));
    }
}

TEST_CASE("CircularPattern_SourceOnTheAxis", "[pattern][circular][features][hole][acceptance]") {
    // A source the axis passes through turns onto itself. Policy: nothing
    // special is done. New-body instances coincide and fuse into the source;
    // a hole is refused by the hole's own placement check, because the second
    // instance would be drilled where the first already is.
    SECTION("a body: every instance coincides with the source") {
        CubeRingModel m;
        CircularPatternDefinition d = m.definitionOf(m.ring);
        d.axis.origin = Point3D{50_mm, 0_mm, 0_mm}; // through the cube's centre
        m.setDefinition(m.ring, d);
        Regenerator regenerator;
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        const geometry::Body& body = requireBody(regenerator, m.ring);
        CHECK(body.isValid());
        CHECK(body.topology().solids == 1);
        CHECK_THAT(volumeMm3(regenerator, m.ring), WithinRel(1000.0, kRel));
        bettercad::test::checkPoint(body.boundingBox()->min, 45, -5, 0);
        bettercad::test::checkPoint(body.boundingBox()->max, 55, 5, 10);
    }
    SECTION("a hole: the second instance is refused") {
        BoltCircleModel m;
        CircularPatternDefinition d = m.definitionOf(m.bolts);
        d.axis.origin = Point3D{40_mm, 0_mm, 0_mm}; // through the hole's centre
        m.setDefinition(m.bolts, d);
        Regenerator regenerator;
        const Error error = requireFailure(regenerator, m.doc, m.bolts);
        CHECK(error.code == ErrorCode::FailedPrecondition);
        CHECK_THAT(error.message, StartsWith("Bolts: circular pattern: instance 1 at 60 deg: hole: the centre "));
        CHECK_THAT(error.message, ContainsSubstring("is not on a face of the body"));
        CHECK(regenerator.state(m.bolt) == NodeState::Regenerated); // built in the same pass, and kept
        CHECK(regenerator.body(m.bolt) != nullptr);
    }
}

TEST_CASE("CircularPattern_HoleBoltCircleMatchesAnalyticVolume", "[pattern][circular][features][hole][acceptance]") {
    // Six 10 mm through holes on a 40 mm bolt circle in a flange R = 60,
    // H = 10: V = pi R^2 H - 6 pi (d/2)^2 H.
    BoltCircleModel m;
    Regenerator regenerator;
    const RegenerationReport report = requireReport(regenerator, m.doc);
    INFO(describe(report));
    REQUIRE(report.succeeded());
    const double expected = BoltCircleModel::expectedVolume(60, 10, 10, 6);
    CHECK_THAT(expected, WithinRel(34500.0 * pi, 1e-15));
    const geometry::Body& body = requireBody(regenerator, m.bolts);
    CHECK(body.isValid());
    CHECK(body.topology().solids == 1);
    const auto props = body.massProperties().value();
    CHECK_THAT(props.volume.in(units::mm3), WithinRel(expected, kRel));
    // Faces: 2 pi R^2 + 2 pi R H, less 6 x 2 discs, plus 6 bores 2 pi r H.
    CHECK_THAT(props.surfaceArea.in(units::mm2), WithinRel(8700.0 * pi, kRel));
    // Each hole at 40 (cos 60i, sin 60i), through both faces, and none between.
    for (int i = 0; i < 6; ++i) {
        CAPTURE(i);
        CHECK(holeAt(regenerator, m.bolts, 60.0 * i, 0.0) == 1);
        CHECK(holeAt(regenerator, m.bolts, 60.0 * i, 10.0) == 1);
    }
    CHECK(holeAt(regenerator, m.bolts, 30.0) == 0);
    checkAngles(anglesOf(m.definitionOf(m.bolts), m.doc), {0.0, 60.0, 120.0, 180.0, 240.0, 300.0});
    CHECK_THAT(volumeMm3(regenerator, m.bolt), WithinRel(BoltCircleModel::expectedVolume(60, 10, 10, 1), kRel));
    CHECK(resultFeatures(m.doc) == std::vector<ObjectId>{m.bolts});
}

TEST_CASE("CircularPattern_PartialAngleIncludesExpectedEndpoints", "[pattern][circular][features][hole][acceptance]") {
    // Four holes over an included 90 degrees: 0, 30, 60 and 90.
    BoltCircleModel m;
    includedAngle(m, 4, 90.0);
    checkAngles(anglesOf(m.definitionOf(m.bolts), m.doc), {0.0, 30.0, 60.0, 90.0});
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK_THAT(volumeMm3(regenerator, m.bolts), WithinRel(BoltCircleModel::expectedVolume(60, 10, 10, 4), kRel));
    for (const double degrees : {0.0, 30.0, 60.0, 90.0}) {
        CAPTURE(degrees);
        CHECK(holeAt(regenerator, m.bolts, degrees) == 1);
    }
    CHECK(holeAt(regenerator, m.bolts, 120.0) == 0);
    // Two holes: both ends of the angle; one: the source alone.
    includedAngle(m, 2, 90.0);
    checkAngles(anglesOf(m.definitionOf(m.bolts), m.doc), {0.0, 90.0});
    includedAngle(m, 1, 90.0);
    checkAngles(anglesOf(m.definitionOf(m.bolts), m.doc), {0.0});
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK_THAT(volumeMm3(regenerator, m.bolts), WithinRel(BoltCircleModel::expectedVolume(60, 10, 10, 1), kRel));
}

TEST_CASE("CircularPattern_AngleStepSpacing", "[pattern][circular][features][hole][acceptance]") {
    // Four holes 45 degrees apart: 0, 45, 90, 135.
    BoltCircleModel m;
    CircularPatternDefinition d = m.definitionOf(m.bolts);
    d.countParameter.reset();
    d.count = 4;
    d.spacing = CircularSpacing::AngleStep;
    d.angle = 45_deg;
    m.setDefinition(m.bolts, d);
    checkAngles(anglesOf(d, m.doc), {0.0, 45.0, 90.0, 135.0});
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    for (const double degrees : {0.0, 45.0, 90.0, 135.0}) {
        CAPTURE(degrees);
        CHECK(holeAt(regenerator, m.bolts, degrees) == 1);
    }
    CHECK_THAT(volumeMm3(regenerator, m.bolts), WithinRel(BoltCircleModel::expectedVolume(60, 10, 10, 4), kRel));
    // Steps that would come round to the source again are refused.
    d.angle = 120_deg;
    CHECK(refusal(d).message == "the instances would go all the way around: 4 instances 120 deg apart span 360 deg, "
                                "which must stay below 360 deg");
    d.count = 3;
    CHECK(CircularPatternFeature::create("P", d).has_value()); // 0, 120, 240
}

TEST_CASE("CircularPattern_NegativeDirectionReversesOrder", "[pattern][circular][features][hole][acceptance]") {
    BoltCircleModel m;
    includedAngle(m, 4, 90.0, RotationDirection::Negative);
    checkAngles(anglesOf(m.definitionOf(m.bolts), m.doc), {0.0, -30.0, -60.0, -90.0});
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    for (const double degrees : {0.0, -30.0, -60.0, -90.0}) {
        CAPTURE(degrees);
        CHECK(holeAt(regenerator, m.bolts, degrees) == 1);
    }
    CHECK(holeAt(regenerator, m.bolts, 30.0) == 0);
    CHECK_THAT(volumeMm3(regenerator, m.bolts), WithinRel(BoltCircleModel::expectedVolume(60, 10, 10, 4), kRel));
}

TEST_CASE("CircularPattern_AdditiveBossesMatchAnalyticVolume", "[pattern][circular][features][acceptance]") {
    // A 10 x 10 x 5 mm boss joined to the drilled flange's top face at
    // (15..25, -5..5), between the bolt holes, six times around:
    // V = V_flange - 6 holes + 6 x 500.
    BoltCircleModel m;
    const auto top = Frame3D::create(Point3D{0_mm, 0_mm, 10_mm}, Direction3D::unitZ(), Direction3D::unitX());
    REQUIRE(top.has_value());
    auto square = std::make_unique<sketch::Sketch>("BossSketch", *top);
    bettercad::test::addRectangle(*square, 15_mm, -(5_mm), 10_mm, 10_mm);
    const ObjectId bossSketch = m.doc.addObject(std::move(square)).value();
    auto boss = ExtrudeFeature::create("Boss", {.profile = SketchId::fromValue(bossSketch.value()),
                                                .depth = 5_mm,
                                                .operation = FeatureOperation::Join,
                                                .target = featureId(m.bolts)});
    REQUIRE(boss.has_value());
    const ObjectId bossId = m.doc.addObject(std::move(*boss)).value();
    const ObjectId bosses = m.addPattern("Bosses", {.source = featureId(bossId),
                                                    .axis = {.origin = Point3D{}, .direction = {0.0, 0.0, 1.0}},
                                                    .count = 6});
    Regenerator regenerator;
    const RegenerationReport report = requireReport(regenerator, m.doc);
    INFO(describe(report));
    REQUIRE(report.succeeded());
    const geometry::Body& body = requireBody(regenerator, bosses);
    CHECK(body.isValid());
    CHECK(body.topology().solids == 1);
    CHECK_THAT(volumeMm3(regenerator, bosses),
               WithinRel(BoltCircleModel::expectedVolume(60, 10, 10, 6) + 6.0 * 500.0, kRel));
    CHECK(facesOn(regenerator, bosses, topFace(15)) == 6); // six boss tops
    // Each boss's outer side, 25 mm out, turned by its angle.
    for (int i = 0; i < 6; ++i) {
        CAPTURE(i);
        const double c = std::cos(radians(60.0 * i));
        const double s = std::sin(radians(60.0 * i));
        CHECK(facesOn(regenerator, bosses, plane(25.0 * c, 25.0 * s, 0, c, s, 0)) == 1);
    }
    CHECK_THAT(body.boundingBox()->max.z.in(units::mm), WithinAbs(15.0, kPositionToleranceMm));
    CHECK(resultFeatures(m.doc) == std::vector<ObjectId>{bosses});
}

TEST_CASE("CircularPattern_SubtractiveExtrudePatternMatchesAnalyticVolume", "[pattern][circular][features][acceptance]") {
    // The boss turned into a pocket: the same square cut 5 mm down from the
    // drilled flange's top face, six times around: V = V_flange - 6 holes
    // - 6 x 500.
    BoltCircleModel m;
    const auto top = Frame3D::create(Point3D{0_mm, 0_mm, 10_mm}, Direction3D::unitZ(), Direction3D::unitX());
    REQUIRE(top.has_value());
    auto square = std::make_unique<sketch::Sketch>("PocketSketch", *top);
    bettercad::test::addRectangle(*square, 15_mm, -(5_mm), 10_mm, 10_mm);
    const ObjectId pocketSketch = m.doc.addObject(std::move(square)).value();
    auto pocket = ExtrudeFeature::create("Pocket", {.profile = SketchId::fromValue(pocketSketch.value()),
                                                    .depth = 5_mm,
                                                    .direction = ExtrudeDirection::Reversed,
                                                    .operation = FeatureOperation::Cut,
                                                    .target = featureId(m.bolts)});
    REQUIRE(pocket.has_value());
    const ObjectId pocketId = m.doc.addObject(std::move(*pocket)).value();
    const ObjectId pockets = m.addPattern("Pockets", {.source = featureId(pocketId),
                                                      .axis = {.origin = Point3D{}, .direction = {0.0, 0.0, 1.0}},
                                                      .count = 6});
    Regenerator regenerator;
    const RegenerationReport report = requireReport(regenerator, m.doc);
    INFO(describe(report));
    REQUIRE(report.succeeded());
    const geometry::Body& body = requireBody(regenerator, pockets);
    CHECK(body.isValid());
    CHECK(body.topology().solids == 1);
    CHECK_THAT(volumeMm3(regenerator, pockets),
               WithinRel(BoltCircleModel::expectedVolume(60, 10, 10, 6) - 6.0 * 500.0, kRel));
    CHECK(facesOn(regenerator, pockets, topFace(5)) == 6); // six pocket floors
    for (int i = 0; i < 6; ++i) {
        CAPTURE(i);
        const double c = std::cos(radians(60.0 * i));
        const double s = std::sin(radians(60.0 * i));
        // Each pocket's outer wall, 25 mm out, faces inwards.
        CHECK(facesOn(regenerator, pockets, plane(25.0 * c, 25.0 * s, 0, -c, -s, 0)) == 1);
    }
}

TEST_CASE("CircularPattern_WorksOnRevolvedSource", "[pattern][circular][features][revolve][acceptance]") {
    // The turned part's revolve (R = 15, h = 40 about Z) four times about the
    // axis through (60, 0) along Z: at (0, 0), (60, -60), (120, 0), (60, 60).
    TurnedPartModel m;
    auto feature = CircularPatternFeature::create(
        "Shafts", {.source = FeatureId::fromValue(m.turn.value()),
                   .axis = {.origin = Point3D{60_mm, 0_mm, 0_mm}, .direction = {0.0, 0.0, 1.0}},
                   .count = 4});
    REQUIRE(feature.has_value());
    const ObjectId shafts = m.doc.addObject(std::move(*feature)).value();
    Regenerator regenerator;
    const RegenerationReport report = requireReport(regenerator, m.doc);
    INFO(describe(report));
    REQUIRE(report.succeeded());
    const geometry::Body& body = requireBody(regenerator, shafts);
    CHECK(body.isValid());
    CHECK(body.topology().solids == 4);
    CHECK_THAT(volumeMm3(regenerator, shafts), WithinRel(4.0 * pi * 225.0 * 40.0, kRel));
    const auto box = body.boundingBox().value();
    CHECK_THAT(box.min.x.in(units::mm), WithinAbs(-15.0, kPositionToleranceMm));
    CHECK_THAT(box.min.y.in(units::mm), WithinAbs(-75.0, kPositionToleranceMm));
    CHECK_THAT(box.max.x.in(units::mm), WithinAbs(135.0, kPositionToleranceMm));
    CHECK_THAT(box.max.y.in(units::mm), WithinAbs(75.0, kPositionToleranceMm));
    // The revolve's sweep drives every instance.
    REQUIRE(m.doc.setParameterValue(m.sweep, 270_deg).has_value());
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK_THAT(volumeMm3(regenerator, shafts), WithinRel(4.0 * 0.75 * pi * 225.0 * 40.0, kRel));
}

TEST_CASE("CircularPattern_ArbitraryAxisPreservesRadius", "[pattern][circular][features][acceptance]") {
    // A peg centred at (40, 10, 20) four times about axes through the origin.
    // Each instance's rims are where Rodrigues' formula puts them, and each
    // rim centre found on the body keeps the source's distance from the axis.
    const V axes[] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 1, 1}};
    for (const V& k : axes) {
        CAPTURE(k[0], k[1], k[2]);
        PegModel m;
        auto feature = CircularPatternFeature::create(
            "Pegs", {.source = FeatureId::fromValue(m.peg.value()),
                     .axis = {.origin = Point3D{}, .direction = {k[0], k[1], k[2]}},
                     .count = 4});
        REQUIRE(feature.has_value());
        const ObjectId pegs = m.doc.addObject(std::move(*feature)).value();
        Regenerator regenerator;
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        const geometry::Body& body = requireBody(regenerator, pegs);
        CHECK(body.isValid());
        CHECK(body.topology().solids == 4);
        CHECK_THAT(volumeMm3(regenerator, pegs), WithinRel(4.0 * pi * 9.0 * 6.0, kRel));

        double worst = 0.0;
        for (const double z : {17.0, 23.0}) {
            const V rimCentre{40, 10, z};
            const double radius = distanceToAxis(rimCentre, k);
            for (int i = 0; i < 4; ++i) {
                CAPTURE(z, i);
                const V centre = rodrigues(rimCentre, k, 90.0 * i);
                const V axis = rodrigues({0, 0, 1}, k, 90.0 * i);
                const auto found = circles(regenerator, pegs, centre[0], centre[1], centre[2], 3, axis[0], axis[1],
                                           axis[2]);
                REQUIRE(found.size() == 1);
                const Point3D& actual = found.front().signature->point;
                const V p{actual.x.in(units::mm), actual.y.in(units::mm), actual.z.in(units::mm)};
                worst = std::max(worst, std::abs(distanceToAxis(p, k) - radius));
            }
        }
        INFO("largest change of a rim's distance from the axis: " << worst << " mm");
        CHECK(worst < 1e-9);
        // By symmetry the four pegs balance on the axis, at the foot of the
        // source's centre.
        const V c{40, 10, 20};
        const double n = std::sqrt(k[0] * k[0] + k[1] * k[1] + k[2] * k[2]);
        const double along = (c[0] * k[0] + c[1] * k[1] + c[2] * k[2]) / n;
        bettercad::test::checkPoint(body.massProperties()->centerOfMass, k[0] / n * along, k[1] / n * along,
                                    k[2] / n * along);
    }
}

TEST_CASE("CircularPattern_DoesNotAccumulateAngularDrift", "[pattern][circular][features][acceptance]") {
    // Each instance's angle is i x step for its own i, bit for bit.
    const Axis3D z{Point3D{}, Direction3D::unitZ()};
    const Angle step = circularPatternStep(CircularSpacing::FullCircle, 100, {}, {});
    const auto hundred = circularPatternInstances(z, 100, step);
    REQUIRE(hundred.size() == 100);
    for (const CircularPatternInstance& instance : hundred) {
        CHECK(instance.angle.si() == step.si() * static_cast<double>(instance.index));
    }
    CHECK_THAT(hundred.back().angle.in(units::deg), WithinAbs(356.4, 1e-12));
    const Point3D last = hundred.back().motion.apply(Point3D{50_mm, 0_mm, 0_mm});
    bettercad::test::checkPoint(last, 50.0 * std::cos(radians(356.4)), 50.0 * std::sin(radians(356.4)), 0.0);
    // 360 one-degree instances: every one at radius 50 and at i degrees.
    const auto degrees =
        circularPatternInstances(z, 360, circularPatternStep(CircularSpacing::FullCircle, 360, {}, {}));
    double worstRadius = 0.0;
    double worstAngle = 0.0;
    for (const CircularPatternInstance& instance : degrees) {
        const Point3D p = instance.motion.apply(Point3D{50_mm, 0_mm, 0_mm});
        worstRadius = std::max(worstRadius, std::abs(std::hypot(p.x.in(units::mm), p.y.in(units::mm)) - 50.0));
        const double expected = radians(static_cast<double>(instance.index));
        const double actual = std::atan2(p.y.si(), p.x.si());
        worstAngle = std::max(worstAngle, std::abs(std::remainder(actual - expected, 2.0 * pi)));
    }
    INFO("worst radius error " << worstRadius << " mm, worst angle error " << worstAngle << " rad");
    CHECK(worstRadius < 1e-12);
    CHECK(worstAngle < 1e-14);

    // On the body: 36 small cubes, the last (350 degrees) where it belongs.
    CubeRingModel m;
    REQUIRE(m.doc.setParameterValue(m.size, 2_mm).has_value());
    REQUIRE(m.doc.setParameterValue(m.count, 36.0, kUnitless).has_value());
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(requireBody(regenerator, m.ring).topology().solids == 36);
    CHECK_THAT(volumeMm3(regenerator, m.ring), WithinRel(36.0 * 8.0, kRel));
    const double c = std::cos(radians(350.0));
    const double s = std::sin(radians(350.0));
    CHECK(facesOn(regenerator, m.ring, plane(47.0 * c, 47.0 * s, 0, c, s, 0)) == 1);
}

TEST_CASE("CircularPattern_RegeneratesWhenSourceChanges", "[pattern][circular][features][hole][acceptance]") {
    BoltCircleModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());

    SECTION("a larger hole diameter changes every hole") {
        REQUIRE(m.doc.setParameterValue(m.diameter, 12_mm).has_value());
        CHECK(requireReport(regenerator, m.doc).regenerated == std::vector<ObjectId>{m.bolt, m.bolts});
        CHECK_THAT(volumeMm3(regenerator, m.bolts), WithinRel(BoltCircleModel::expectedVolume(60, 10, 12, 6), kRel));
        for (int i = 0; i < 6; ++i) {
            CAPTURE(i);
            CHECK(holeAt(regenerator, m.bolts, 60.0 * i, 0.0, 6.0) == 1);
            CHECK(holeAt(regenerator, m.bolts, 60.0 * i, 0.0, 5.0) == 0);
        }
    }
    SECTION("a thicker flange: every through hole stays through") {
        REQUIRE(m.doc.setParameterValue(m.thickness, 20_mm).has_value());
        CHECK(requireReport(regenerator, m.doc).regenerated == std::vector<ObjectId>{m.flange, m.bolt, m.bolts});
        CHECK_THAT(volumeMm3(regenerator, m.bolts), WithinRel(BoltCircleModel::expectedVolume(60, 20, 10, 6), kRel));
        for (int i = 0; i < 6; ++i) {
            CAPTURE(i);
            CHECK(holeAt(regenerator, m.bolts, 60.0 * i, 20.0) == 1);
        }
    }
    SECTION("a larger flange keeps the bolt circle") {
        REQUIRE(m.doc.setParameterValue(m.radius, 70_mm).has_value());
        CHECK(requireReport(regenerator, m.doc).regenerated == std::vector<ObjectId>{m.disc, m.flange, m.bolt, m.bolts});
        CHECK_THAT(volumeMm3(regenerator, m.bolts), WithinRel(BoltCircleModel::expectedVolume(70, 10, 10, 6), kRel));
    }
}

TEST_CASE("CircularPattern_RegeneratesWhenCountChanges", "[pattern][circular][features][hole][undo][acceptance]") {
    BoltCircleModel m;
    CommandHistory history;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    std::vector<double> volumes{volumeMm3(regenerator, m.bolts)}; // 6 holes

    for (const int count : {3, 6, 8}) {
        CAPTURE(count);
        REQUIRE(history.execute(m.doc, ModifyParameterCommand::setValue(m.count, countOf(count))).has_value());
        const RegenerationReport report = requireReport(regenerator, m.doc);
        CHECK(report.regenerated == std::vector<ObjectId>{m.bolts}); // the source is not rebuilt
        CHECK(regenerator.state(m.bolt) == NodeState::UpToDate);
        volumes.push_back(volumeMm3(regenerator, m.bolts));
        CHECK_THAT(volumes.back(), WithinRel(BoltCircleModel::expectedVolume(60, 10, 10, count), kRel));
        const double step = 360.0 / count;
        for (int i = 0; i < count; ++i) {
            CAPTURE(i);
            CHECK(holeAt(regenerator, m.bolts, step * i) == 1);
        }
        CHECK(holeAt(regenerator, m.bolts, step / 2.0) == 0);
    }
    CHECK(bits(volumes[2]) == bits(volumes[0])); // back to 6: the same geometry
    for (std::size_t step = 3; step > 0; --step) {
        REQUIRE(history.undo(m.doc).has_value());
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CHECK(bits(volumeMm3(regenerator, m.bolts)) == bits(volumes[step - 1]));
    }
    for (std::size_t step = 1; step <= 3; ++step) {
        REQUIRE(history.redo(m.doc).has_value());
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CHECK(bits(volumeMm3(regenerator, m.bolts)) == bits(volumes[step]));
    }
}

TEST_CASE("CircularPattern_RegeneratesWhenAngleChanges", "[pattern][circular][features][hole][acceptance]") {
    // Four holes over an included angle driven by `span`: 90 -> 180 degrees.
    BoltCircleModel m;
    const ParameterId span = m.doc.createParameter("span", 90_deg, units::deg).value();
    CircularPatternDefinition d = m.definitionOf(m.bolts);
    d.countParameter.reset();
    d.count = 4;
    d.spacing = CircularSpacing::IncludedAngle;
    d.angleParameter = span;
    m.setDefinition(m.bolts, d);
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    for (const double degrees : {0.0, 30.0, 60.0, 90.0}) {
        CHECK(holeAt(regenerator, m.bolts, degrees) == 1);
    }
    REQUIRE(m.doc.setParameterValue(span, 180_deg).has_value());
    CHECK(requireReport(regenerator, m.doc).regenerated == std::vector<ObjectId>{m.bolts});
    checkAngles(anglesOf(d, m.doc), {0.0, 60.0, 120.0, 180.0});
    for (const double degrees : {0.0, 60.0, 120.0, 180.0}) {
        CAPTURE(degrees);
        CHECK(holeAt(regenerator, m.bolts, degrees) == 1);
    }
    CHECK(holeAt(regenerator, m.bolts, 30.0) == 0);
    CHECK_THAT(volumeMm3(regenerator, m.bolts), WithinRel(BoltCircleModel::expectedVolume(60, 10, 10, 4), kRel));
}

TEST_CASE("CircularPattern_RegeneratesWhenDirectionChanges", "[pattern][circular][features][hole][acceptance]") {
    BoltCircleModel m;
    includedAngle(m, 4, 90.0);
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const double positive = volumeMm3(regenerator, m.bolts);
    CHECK(holeAt(regenerator, m.bolts, 90.0) == 1);
    CHECK(holeAt(regenerator, m.bolts, -90.0) == 0);
    CircularPatternDefinition d = m.definitionOf(m.bolts);
    d.direction = RotationDirection::Negative;
    m.setDefinition(m.bolts, d);
    CHECK(requireReport(regenerator, m.doc).regenerated == std::vector<ObjectId>{m.bolts});
    for (const double degrees : {0.0, -30.0, -60.0, -90.0}) {
        CAPTURE(degrees);
        CHECK(holeAt(regenerator, m.bolts, degrees) == 1);
    }
    CHECK(holeAt(regenerator, m.bolts, 90.0) == 0);
    CHECK_THAT(volumeMm3(regenerator, m.bolts), WithinRel(positive, kRel));
}

TEST_CASE("CircularPattern_RegeneratesWhenAxisChanges", "[pattern][circular][features][hole][acceptance]") {
    BoltCircleModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    SECTION("an axis moved to (5, 0): the holes orbit it at 35 mm") {
        CircularPatternDefinition d = m.definitionOf(m.bolts);
        d.axis.origin = Point3D{5_mm, 0_mm, 0_mm};
        m.setDefinition(m.bolts, d);
        CHECK(requireReport(regenerator, m.doc).regenerated == std::vector<ObjectId>{m.bolts});
        for (int i = 0; i < 6; ++i) {
            CAPTURE(i);
            CHECK(holeAt(regenerator, m.bolts, 60.0 * i, 0.0, 5.0, 35.0, 5.0, 0.0) == 1);
        }
        CHECK_THAT(volumeMm3(regenerator, m.bolts), WithinRel(BoltCircleModel::expectedVolume(60, 10, 10, 6), kRel));
    }
    SECTION("the axis reversed: instance 1 turns the other way") {
        CircularPatternDefinition d = m.definitionOf(m.bolts);
        d.axis.direction = {0.0, 0.0, -1.0};
        m.setDefinition(m.bolts, d);
        const auto instances = resolveCircularPatternInstances(d, m.doc);
        REQUIRE(instances.has_value());
        const Point3D second = (*instances)[1].motion.apply(Point3D{40_mm, 0_mm, 0_mm});
        bettercad::test::checkPoint(second, 20.0, -40.0 * std::sqrt(3.0) / 2.0, 0.0);
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CHECK_THAT(volumeMm3(regenerator, m.bolts), WithinRel(BoltCircleModel::expectedVolume(60, 10, 10, 6), kRel));
    }
}

TEST_CASE("CircularPattern_RejectsZeroAxis", "[pattern][circular][features][acceptance]") {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    for (const Vector3D bad : {Vector3D{0.0, 0.0, 0.0}, Vector3D{nan, 0.0, 1.0}, Vector3D{0.0, inf, 0.0}}) {
        CircularPatternDefinition d = literalRing();
        d.axis.direction = bad;
        const Error refused = refusal(d);
        CHECK(refused.code == ErrorCode::InvalidArgument);
        CHECK_THAT(refused.message, StartsWith("the axis direction must be a finite, non-zero vector, got "));
    }
    CircularPatternDefinition d = literalRing();
    d.axis.direction = {0.0, -0.0, 0.0};
    CHECK(refusal(d).message == "the axis direction must be a finite, non-zero vector, got (0, 0, 0)");
    d = literalRing();
    d.axis.origin = Point3D{Length::fromSi(nan), 0_mm, 0_mm};
    CHECK(refusal(d).message == "the axis origin must be finite");
}

TEST_CASE("CircularPattern_RejectsInvalidAngle", "[pattern][circular][features][acceptance]") {
    CircularPatternDefinition d = literalRing();
    d.spacing = CircularSpacing::IncludedAngle;
    for (const double degrees : {0.0, -30.0, std::nan(""), std::numeric_limits<double>::infinity()}) {
        CAPTURE(degrees);
        d.angle = degrees * units::deg;
        const Error refused = refusal(d);
        CHECK(refused.code == ErrorCode::InvalidArgument);
        CHECK_THAT(refused.message, StartsWith("the included angle must be positive and finite, got "));
    }
    for (const double degrees : {360.0, 400.0, 720.0}) {
        CAPTURE(degrees);
        d.angle = degrees * units::deg;
        CHECK_THAT(refusal(d).message, StartsWith("the included angle must be less than 360 deg, got "));
    }
    d.spacing = CircularSpacing::AngleStep;
    d.angle = 0_deg;
    CHECK(refusal(d).message == "the angle step must be positive and finite, got 0 deg");
    d.angle = 360_deg;
    CHECK(refusal(d).message == "the angle step must be less than 360 deg, got 360 deg");

    // A driving parameter is checked with its value, at regeneration.
    BoltCircleModel m;
    const ParameterId span = m.doc.createParameter("span", 90_deg, units::deg).value();
    CircularPatternDefinition driven = m.definitionOf(m.bolts);
    driven.countParameter.reset();
    driven.count = 4;
    driven.spacing = CircularSpacing::IncludedAngle;
    driven.angleParameter = span;
    m.setDefinition(m.bolts, driven);
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    REQUIRE(m.doc.setParameterValue(span, 0_deg).has_value());
    Error error = requireFailure(regenerator, m.doc, m.bolts);
    CHECK(error.code == ErrorCode::InvalidArgument);
    CHECK(error.message == "Bolts: circular pattern: the included angle must be positive and finite, got 0 deg");
    REQUIRE(m.doc.setParameterValue(span, 360_deg).has_value());
    error = requireFailure(regenerator, m.doc, m.bolts);
    CHECK_THAT(error.message, StartsWith("Bolts: circular pattern: the included angle must be less than 360 deg"));
    CHECK(regenerator.state(m.bolt) == NodeState::UpToDate);
}

TEST_CASE("CircularPattern_RejectsZeroCount", "[pattern][circular][features][acceptance]") {
    CircularPatternDefinition d = literalRing();
    d.count = 0;
    const Error refused = refusal(d);
    CHECK(refused.code == ErrorCode::InvalidArgument);
    CHECK(refused.message == "the count must be at least 1, got 0");
    d.count = 501; // the linear pattern's limit, shared
    CHECK(refusal(d).message == "a circular pattern may have at most 500 instances, got 501");
    d.count = 500;
    CHECK(CircularPatternFeature::create("P", d).has_value());

    CubeRingModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    for (const double count : {0.0, -1.0, 2.5, 501.0}) {
        CAPTURE(count);
        REQUIRE(m.doc.setParameterValue(m.count, count, kUnitless).has_value());
        const Error error = requireFailure(regenerator, m.doc, m.ring);
        CHECK(error.code == ErrorCode::InvalidArgument);
        CHECK_THAT(error.message,
                   StartsWith("Ring: circular pattern: the count must be a whole number from 1 to 500, got "));
    }
    CHECK_THAT(requireFailure(regenerator, m.doc, m.ring).message, ContainsSubstring("got 501"));
}

TEST_CASE("CircularPattern_FailsAtomicallyWhenInstanceInvalid", "[pattern][circular][features][hole][acceptance]") {
    // The drilled block (100 x 50 x 20) with its hole at (80, 25), turned
    // about the axis through (60, 25) over 90 degrees: at 0, 30 and 60
    // degrees the holes fit (10.35 mm apart); at 90 degrees the hole's
    // centre (60, 45) is 5 mm from the block's side, which a 10 mm hole
    // cannot leave room inside.
    HoleBlockModel m;
    REQUIRE(m.doc.setParameterValue(m.holeX, 80_mm).has_value());
    auto feature = CircularPatternFeature::create(
        "Arc", {.source = featureId(m.drill),
                .axis = {.origin = Point3D{60_mm, 25_mm, 0_mm}, .direction = {0.0, 0.0, 1.0}},
                .count = 4,
                .spacing = CircularSpacing::IncludedAngle,
                .angle = 90_deg});
    REQUIRE(feature.has_value());
    const ObjectId arc = m.doc.addObject(std::move(*feature)).value();
    Regenerator regenerator;
    const Error error = requireFailure(regenerator, m.doc, arc);
    CHECK(error.code == ErrorCode::FailedPrecondition);
    CHECK_THAT(error.message,
               StartsWith("Arc: circular pattern: instance 3 at 90 deg: hole: the hole does not fit on its face: its "
                          "entry is 10 mm across, but the centre (60, 45) mm is only "));
    CHECK_THAT(error.message, ContainsSubstring("is only 5 mm from the face's edge"));
    // No partial pattern: no body at all, and the source (built in the same
    // pass) is untouched.
    CHECK(regenerator.state(m.drill) == NodeState::Regenerated);
    const double drilled = volumeMm3(regenerator, m.drill);
    CHECK_THAT(drilled, WithinRel(HoleBlockModel::expectedVolume(100, 50, 20, 10), kRel));
    // Three holes over 60 degrees (0, 30, 60) all fit.
    CircularPatternDefinition d = m.doc.findObjectAs<CircularPatternFeature>(arc)->definition();
    d.count = 3;
    d.angle = 60_deg;
    REQUIRE(m.doc.modifyObject<CircularPatternFeature>(arc, [&](CircularPatternFeature& f) {
                   return f.setDefinition(d);
               }).has_value());
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK_THAT(volumeMm3(regenerator, arc), WithinRel(100000.0 - 3.0 * 500.0 * pi, kRel));
    CHECK(holeAt(regenerator, arc, 60.0, 20.0, 5.0, 20.0, 60.0, 25.0) == 1);
    CHECK(volumeMm3(regenerator, m.drill) == drilled);
}

TEST_CASE("CircularPattern_PatternsChamferAndFilletByRotatedReferences",
          "[pattern][circular][features][chamfer][fillet]") {
    // Six holes; the first hole's top rim is eased, and that edge operation
    // is patterned: each instance's rim reference is the first one turned,
    // and must match exactly one edge.
    BoltCircleModel m;
    const double holes = BoltCircleModel::expectedVolume(60, 10, 10, 6);
    const auto rim = geometry::circleSignature(Point3D{40_mm, 0_mm, 10_mm}, Direction3D::unitZ(), 5_mm);
    REQUIRE(rim.has_value());
    Regenerator regenerator;

    SECTION("chamfers: pi c^2 (r + c/3) per rim, by Pappus") {
        const ParameterId ease = m.doc.createParameter("ease", 1_mm, units::mm).value();
        auto chamfer = ChamferFeature::create("Ease", {.target = featureId(m.bolts), .edges = {*rim},
                                                       .distanceParameter = ease});
        REQUIRE(chamfer.has_value());
        const ObjectId easeId = m.doc.addObject(std::move(*chamfer)).value();
        const ObjectId eases = m.addPattern("Eases", {.source = featureId(easeId),
                                                      .axis = {.origin = Point3D{}, .direction = {0.0, 0.0, 1.0}},
                                                      .count = 6});
        const RegenerationReport report = requireReport(regenerator, m.doc);
        INFO(describe(report));
        REQUIRE(report.succeeded());
        CHECK_THAT(volumeMm3(regenerator, eases), WithinRel(holes - 6.0 * pi * (5.0 + 1.0 / 3.0), kRel));
        for (int i = 0; i < 6; ++i) {
            CAPTURE(i);
            CHECK(holeAt(regenerator, eases, 60.0 * i, 10.0, 6.0) == 1);
        }
        REQUIRE(m.doc.setParameterValue(ease, 2_mm).has_value());
        CHECK(requireReport(regenerator, m.doc).regenerated == std::vector<ObjectId>{easeId, eases});
        CHECK_THAT(volumeMm3(regenerator, eases), WithinRel(holes - 6.0 * pi * 4.0 * (5.0 + 2.0 / 3.0), kRel));
        // About another axis the turned rims do not exist: the reference
        // matches no edge, and no other edge is taken instead.
        CircularPatternDefinition d = m.definitionOf(eases);
        d.axis.origin = Point3D{5_mm, 0_mm, 0_mm};
        m.setDefinition(eases, d);
        const Error error = requireFailure(regenerator, m.doc, eases);
        CHECK(error.code == ErrorCode::NotFound);
        CHECK_THAT(error.message, StartsWith("Eases: circular pattern: instance 1 at 60 deg: chamfer: edge reference 1 "
                                             "(circle around "));
        CHECK_THAT(error.message, ContainsSubstring("matches no edge of the body"));
    }
    SECTION("fillets: 2 pi (r + u) r_f^2 (1 - pi/4) per rim") {
        const double rf = 2.0;
        const double centroid = rf * (10.0 - 3.0 * pi) / (12.0 - 3.0 * pi);
        auto fillet = FilletFeature::create("Soften", {.target = featureId(m.bolts), .edges = {*rim}, .radius = 2_mm});
        REQUIRE(fillet.has_value());
        const ObjectId softenId = m.doc.addObject(std::move(*fillet)).value();
        const ObjectId rounds = m.addPattern("Rounds", {.source = featureId(softenId),
                                                        .axis = {.origin = Point3D{}, .direction = {0.0, 0.0, 1.0}},
                                                        .count = 6});
        const RegenerationReport report = requireReport(regenerator, m.doc);
        INFO(describe(report));
        REQUIRE(report.succeeded());
        CHECK(requireBody(regenerator, rounds).isValid());
        CHECK_THAT(volumeMm3(regenerator, rounds),
                   WithinRel(holes - 6.0 * 2.0 * pi * (5.0 + centroid) * filletCorner(rf), kRel));
        for (int i = 0; i < 6; ++i) {
            CAPTURE(i);
            CHECK(holeAt(regenerator, rounds, 60.0 * i, 10.0, 7.0) == 1);
        }
    }
}

TEST_CASE("CircularPattern_RefusesUnsupportedSources", "[pattern][circular][features]") {
    // A pattern of a pattern is supported since P12-PATTERN-001; see
     // PatternNestingTests.cpp. A mirror is still not: its image is not a
     // motion of the source's operation, so it has no instance to repeat.
    SECTION("a mirror") {
        CubeRingModel m;
        auto mirror = MirrorFeature::create(
            "Across", {.source = featureId(m.cube), .plane = {.origin = Point3D{}, .normal = {0.0, 1.0, 0.0}}});
        REQUIRE(mirror.has_value());
        const ObjectId across = m.doc.addObject(std::move(*mirror)).value();
        const ObjectId twice = m.addPattern("Twice", {.source = featureId(across),
                                                      .axis = {.origin = Point3D{}, .direction = {0.0, 0.0, 1.0}},
                                                      .count = 2});
        Regenerator regenerator;
        const Error error = requireFailure(regenerator, m.doc, twice);
        CHECK(error.code == ErrorCode::FailedPrecondition);
        CHECK(error.message == "Twice: circular pattern: a circular pattern cannot repeat a mirror");
    }
    SECTION("a sketch, which has no body") {
        CubeRingModel m;
        CircularPatternDefinition d = m.definitionOf(m.ring);
        d.source = FeatureId::fromValue(m.sketch.value());
        m.setDefinition(m.ring, d);
        Regenerator regenerator;
        const Error error = requireFailure(regenerator, m.doc, m.ring);
        CHECK(error.code == ErrorCode::FailedPrecondition);
        CHECK(error.message == "Ring: a circular pattern needs the body of its source feature");
    }
}

TEST_CASE("CircularPattern_InvalidInputsFailWithStructuredDiagnostics", "[pattern][circular][features][acceptance]") {
    CubeRingModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());

    SECTION("a missing source") {
        REQUIRE(m.doc.removeObject(m.cube).has_value());
        const Error error = requireFailure(regenerator, m.doc, m.ring);
        CHECK(error.code == ErrorCode::NotFound);
        CHECK(error.message == "object:5 references object:4, which does not exist");
    }
    SECTION("a missing count parameter") {
        REQUIRE(m.doc.removeParameter(m.count).has_value());
        const Error error = requireFailure(regenerator, m.doc, m.ring);
        CHECK(error.code == ErrorCode::NotFound);
        CHECK(error.message == "object:5 references object:2, which does not exist");
    }
    SECTION("a count parameter that is a length") {
        CircularPatternDefinition d = m.definitionOf(m.ring);
        d.countParameter = m.size;
        m.setDefinition(m.ring, d);
        const Error error = requireFailure(regenerator, m.doc, m.ring);
        CHECK(error.code == ErrorCode::DimensionMismatch);
        CHECK_THAT(error.message, StartsWith("Ring: circular pattern: "));
    }
    SECTION("an angle parameter that is a length") {
        CircularPatternDefinition d = m.definitionOf(m.ring);
        d.spacing = CircularSpacing::IncludedAngle;
        d.angleParameter = m.size;
        m.setDefinition(m.ring, d);
        const Error error = requireFailure(regenerator, m.doc, m.ring);
        CHECK(error.code == ErrorCode::DimensionMismatch);
        CHECK_THAT(error.message, StartsWith("Ring: circular pattern: "));
    }
    SECTION("a source that fails blocks the pattern") {
        REQUIRE(m.doc.setParameterValue(m.size, 0_mm).has_value());
        const RegenerationReport report = requireReport(regenerator, m.doc);
        INFO(describe(report));
        CHECK_FALSE(report.failed.empty());
        CHECK(std::ranges::find(report.blocked, m.ring) != report.blocked.end());
        CHECK(regenerator.body(m.ring) == nullptr);
    }
}

TEST_CASE("CircularPattern_FailuresLeaveTheDocumentAndUpstreamBodiesIntact", "[pattern][circular][features][acceptance]") {
    BoltCircleModel m;
    CommandHistory history;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const Document before = m.doc.clone();
    const std::uint64_t revision = m.doc.revision();
    const double drilled = volumeMm3(regenerator, m.bolt);
    const double patterned = volumeMm3(regenerator, m.bolts);

    // An invalid edit is refused before it touches the document.
    CircularPatternDefinition invalid = m.definitionOf(m.bolts);
    invalid.axis.direction = {0.0, 0.0, 0.0};
    CHECK(errorCode(history.execute(m.doc, std::make_unique<ModifyCircularPatternCommand>(featureId(m.bolts),
                                                                                           invalid))) ==
          ErrorCode::InvalidArgument);
    CHECK(equivalent(m.doc, before));
    CHECK(m.doc.revision() == revision);
    CHECK_FALSE(history.canUndo());

    // A valid edit the geometry cannot take (40 holes overlap on the bolt
    // circle) fails at regeneration only.
    CircularPatternDefinition crowded = m.definitionOf(m.bolts);
    crowded.countParameter.reset();
    crowded.count = 40;
    REQUIRE(history.execute(m.doc, std::make_unique<ModifyCircularPatternCommand>(featureId(m.bolts), crowded))
                .has_value());
    const Document edited = m.doc.clone();
    const Error error = requireFailure(regenerator, m.doc, m.bolts);
    CHECK(error.code == ErrorCode::FailedPrecondition);
    CHECK_THAT(error.message, StartsWith("Bolts: circular pattern: instance 1 at 9 deg: hole: "));
    CHECK(equivalent(m.doc, edited)); // regeneration does not change the model
    CHECK(regenerator.state(m.bolt) == NodeState::UpToDate);
    CHECK(volumeMm3(regenerator, m.bolt) == drilled);

    REQUIRE(history.undo(m.doc).has_value());
    CHECK(equivalent(m.doc, before));
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(volumeMm3(regenerator, m.bolts) == patterned);
}

TEST_CASE("CircularPattern_UndoRedoRestoresGeometry", "[pattern][circular][features][undo][acceptance]") {
    // The flange with its one hole; the pattern is created by command.
    BoltCircleModel m;
    REQUIRE(m.doc.removeObject(m.bolts).has_value());
    CommandHistory history;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const Document initial = m.doc.clone();

    const CircularPatternDefinition six{.source = featureId(m.bolt),
                                        .axis = {.origin = Point3D{}, .direction = {0.0, 0.0, 1.0}},
                                        .count = 6};
    auto create = std::make_unique<CreateCircularPatternCommand>("Holes", six);
    CreateCircularPatternCommand* createRaw = create.get();
    CHECK(create->description() == "Create circular_pattern 'Holes'");
    REQUIRE(history.execute(m.doc, std::move(create)).has_value());
    const ObjectId holes{createRaw->featureId()};
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const double created = volumeMm3(regenerator, holes);
    CHECK_THAT(created, WithinRel(BoltCircleModel::expectedVolume(60, 10, 10, 6), kRel));
    CHECK(resultFeatures(m.doc) == std::vector<ObjectId>{holes});
    const Document afterCreate = m.doc.clone();

    // Modify the count.
    CircularPatternDefinition four = six;
    four.count = 4;
    REQUIRE(history.execute(m.doc, std::make_unique<ModifyCircularPatternCommand>(featureId(holes), four)).has_value());
    CHECK(requireReport(regenerator, m.doc).regenerated == std::vector<ObjectId>{holes});
    const double counted = volumeMm3(regenerator, holes);
    CHECK_THAT(counted, WithinRel(BoltCircleModel::expectedVolume(60, 10, 10, 4), kRel));
    CHECK(holeAt(regenerator, holes, 90.0) == 1);
    const Document afterCount = m.doc.clone();

    // Modify the angle: over an included 90 degrees.
    CircularPatternDefinition narrow = four;
    narrow.spacing = CircularSpacing::IncludedAngle;
    narrow.angle = 90_deg;
    REQUIRE(history.execute(m.doc, std::make_unique<ModifyCircularPatternCommand>(featureId(holes), narrow)).has_value());
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const double angled = volumeMm3(regenerator, holes);
    CHECK(holeAt(regenerator, holes, 30.0) == 1);
    const Document afterAngle = m.doc.clone();

    // Modify the direction.
    CircularPatternDefinition reversed = narrow;
    reversed.direction = RotationDirection::Negative;
    REQUIRE(history.execute(m.doc, std::make_unique<ModifyCircularPatternCommand>(featureId(holes), reversed))
                .has_value());
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const double directed = volumeMm3(regenerator, holes);
    CHECK(holeAt(regenerator, holes, -30.0) == 1);
    CHECK(holeAt(regenerator, holes, 30.0) == 0);
    const Document afterDirection = m.doc.clone();

    // Undo steps back through each edit, to the same geometry bit for bit.
    REQUIRE(history.undo(m.doc).has_value());
    CHECK(equivalent(m.doc, afterAngle));
    CHECK(m.doc.findObjectAs<CircularPatternFeature>(holes)->definition() == narrow);
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(volumeMm3(regenerator, holes) == angled);
    CHECK(holeAt(regenerator, holes, 30.0) == 1);

    REQUIRE(history.undo(m.doc).has_value());
    CHECK(equivalent(m.doc, afterCount));
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(volumeMm3(regenerator, holes) == counted);

    REQUIRE(history.undo(m.doc).has_value());
    CHECK(equivalent(m.doc, afterCreate));
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(volumeMm3(regenerator, holes) == created);

    for (int i = 0; i < 3; ++i) {
        REQUIRE(history.redo(m.doc).has_value());
    }
    CHECK(equivalent(m.doc, afterDirection));
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(volumeMm3(regenerator, holes) == directed);

    for (int i = 0; i < 4; ++i) {
        REQUIRE(history.undo(m.doc).has_value());
    }
    CHECK(m.doc.findObject(holes) == nullptr);
    CHECK(equivalent(m.doc, initial));
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(regenerator.body(holes) == nullptr);
    CHECK(resultFeatures(m.doc) == std::vector<ObjectId>{m.bolt});

    REQUIRE(history.redo(m.doc).has_value());
    CHECK(equivalent(m.doc, afterCreate)); // recreated with the same ID
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(volumeMm3(regenerator, holes) == created);

    CHECK(errorCode(history.execute(m.doc, std::make_unique<ModifyCircularPatternCommand>(featureId(m.bolt), four))) ==
          ErrorCode::NotFound);
}

TEST_CASE("CircularPattern_ReusesStableSourceReference", "[pattern][circular][features][acceptance]") {
    BoltCircleModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const double patterned = volumeMm3(regenerator, m.bolts);
    REQUIRE(m.doc.rename(m.bolt, "Anchor").has_value());
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(m.definitionOf(m.bolts).source == featureId(m.bolt));
    CHECK(volumeMm3(regenerator, m.bolts) == patterned);
    auto removed = m.doc.removeObject(m.bolt);
    REQUIRE(removed.has_value());
    const Error error = requireFailure(regenerator, m.doc, m.bolts);
    CHECK(error.code == ErrorCode::NotFound);
    CHECK(error.message == "object:8 references object:7, which does not exist");
    REQUIRE(m.doc.insertObject(std::move(*removed)).has_value());
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(volumeMm3(regenerator, m.bolts) == patterned);
}

TEST_CASE("CircularPattern_RegenerationIsDeterministic", "[pattern][circular][features]") {
    BoltCircleModel m;
    Regenerator first;
    Regenerator second;
    REQUIRE(requireReport(first, m.doc).succeeded());
    REQUIRE(requireReport(second, m.doc).succeeded());
    const geometry::Body& a = requireBody(first, m.bolts);
    const geometry::Body& b = requireBody(second, m.bolts);
    const auto pa = a.massProperties().value();
    const auto pb = b.massProperties().value();
    CHECK(bits(pa.volume.si()) == bits(pb.volume.si()));
    CHECK(bits(pa.surfaceArea.si()) == bits(pb.surfaceArea.si()));
    CHECK(bits(pa.centerOfMass.x.si()) == bits(pb.centerOfMass.x.si()));
    CHECK(a.boundingBox().value() == b.boundingBox().value());
    CHECK(a.topology() == b.topology());
    CHECK(resolveCircularPatternInstances(m.definitionOf(m.bolts), m.doc).value() ==
          resolveCircularPatternInstances(m.definitionOf(m.bolts), m.doc).value());
}
