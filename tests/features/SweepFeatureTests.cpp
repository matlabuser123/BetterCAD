#include "FeatureTestSupport.hpp"
#include "TestHelpers.hpp"
#include "core/geometry/GeometryTestSupport.hpp"
#include "support/BlockModel.hpp"
#include "support/BracketModel.hpp"
#include "support/SweepModels.hpp"

#include <bettercad/core/document/Commands.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/geometry/Edges.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/FeatureCommands.hpp>
#include <bettercad/features/Regeneration.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/features/ResultBodies.hpp>
#include <bettercad/features/RevolveFeature.hpp>
#include <bettercad/features/SweepFeature.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <utility>
#include <vector>

using namespace bettercad;
using namespace bettercad::features;
using namespace bettercad::literals;
using bettercad::test::ArcSweepModel;
using bettercad::test::BlockModel;
using bettercad::test::ChannelModel;
using bettercad::test::describe;
using bettercad::test::errorCode;
using bettercad::test::featureIdOf;
using bettercad::test::FixedPathSweepModel;
using bettercad::test::HandleModel;
using bettercad::test::kPositionToleranceMm;
using bettercad::test::requireReport;
using bettercad::test::sketchIdOf;
using bettercad::test::StraightSweepModel;
using bettercad::test::StraightTubeModel;
using bettercad::test::TorusSweepModel;
using bettercad::test::volumeMm3;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::StartsWith;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

constexpr double pi = std::numbers::pi;
// Sweeps along lines and arcs give planes, cylinders and tori, which the
// kernel integrates to rounding level (as for revolves).
constexpr double kRel = bettercad::test::kRelTight;
// Mitred corners are trimmed where the kernel intersects the pipes
// numerically, at its 1e-7 mm precision (measured: a few 1e-12, see
// examples/geometry_accuracy).
constexpr double kRelMitre = bettercad::test::kRelApproximatedIntersection;
// Body::boundingBox() bounds toroidal faces numerically and pads them by
// the kernel's confusion tolerance, 1e-7 mm (exact for planes and cylinders).
constexpr double kTorusBoundsPaddingMm = 1e-7;

using V = std::array<double, 3>;

std::uint64_t bits(double value) {
    return std::bit_cast<std::uint64_t>(value);
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

geometry::MassProperties propertiesOf(const Regenerator& regenerator, ObjectId feature) {
    return requireBody(regenerator, feature).massProperties().value();
}

void checkPoint(const Point3D& actual, const V& expected) {
    bettercad::test::checkPoint(actual, expected[0], expected[1], expected[2]);
}

/// Exact bounds (planes, cylinders).
void checkBox(const geometry::Body& body, const V& min, const V& max) {
    const BoundingBox3D box = body.boundingBox().value();
    checkPoint(box.min, min);
    checkPoint(box.max, max);
}

/// Bounds of a body with toroidal faces: the exact box, padded by at most
/// kTorusBoundsPaddingMm outwards.
void checkPaddedBox(const geometry::Body& body, const V& min, const V& max) {
    const BoundingBox3D box = body.boundingBox().value();
    const V lo{box.min.x.in(units::mm), box.min.y.in(units::mm), box.min.z.in(units::mm)};
    const V hi{box.max.x.in(units::mm), box.max.y.in(units::mm), box.max.z.in(units::mm)};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        CAPTURE(axis, lo[axis], hi[axis]);
        CHECK(lo[axis] <= min[axis] + kPositionToleranceMm);
        CHECK(lo[axis] >= min[axis] - kTorusBoundsPaddingMm - kPositionToleranceMm);
        CHECK(hi[axis] >= max[axis] - kPositionToleranceMm);
        CHECK(hi[axis] <= max[axis] + kTorusBoundsPaddingMm + kPositionToleranceMm);
    }
}

/// Total area of the body's faces on the plane through (x, y, z) mm facing (nx, ny, nz).
double areaOn(const Regenerator& regenerator, ObjectId feature, double x, double y, double z, double nx, double ny,
              double nz) {
    const auto found = geometry::findFaces(
        requireBody(regenerator, feature),
        geometry::planeSignature(Point3D{x * units::mm, y * units::mm, z * units::mm},
                                 *Direction3D::fromComponents(nx, ny, nz)));
    REQUIRE(found.has_value());
    double area = 0.0;
    for (const geometry::FaceInfo& info : *found) {
        area += info.area.in(units::mm2);
    }
    return area;
}

/// Edges on the circle of radius @p r mm around (x, y, z) mm with axis (ax, ay, az).
std::size_t circlesAt(const Regenerator& regenerator, ObjectId feature, V centre, double r, V axis) {
    const auto circle = geometry::circleSignature(
        Point3D{centre[0] * units::mm, centre[1] * units::mm, centre[2] * units::mm},
        *Direction3D::fromComponents(axis[0], axis[1], axis[2]), r * units::mm);
    REQUIRE(circle.has_value());
    return geometry::findEdges(requireBody(regenerator, feature), *circle).value().size();
}

Error refusal(const SweepDefinition& definition) {
    auto feature = SweepFeature::create("S", definition);
    REQUIRE_FALSE(feature.has_value());
    return feature.error();
}

/// Adds a sketch to @p doc and returns its ID.
ObjectId addSketch(Document& doc, std::unique_ptr<sketch::Sketch> sketch) {
    return doc.addObject(std::move(sketch)).value();
}

/// A path sketch in the XY plane made of fixed points joined by lines and
/// counter-clockwise arcs (centre, start, end); returns its edges in order.
struct PathBuilder {
    std::unique_ptr<sketch::Sketch> sketch;
    std::vector<EntityId> edges;

    explicit PathBuilder(const Frame3D& plane = Frame3D::xy(), const std::string& name = "Route")
        : sketch(std::make_unique<sketch::Sketch>(name, plane)) {}

    EntityId point(double x, double y) {
        const EntityId p = bettercad::test::require(sketch->addPoint(Point2D{x * units::mm, y * units::mm}));
        bettercad::test::require(sketch->addFixed(p));
        return p;
    }
    void line(EntityId a, EntityId b) { edges.push_back(bettercad::test::require(sketch->addLine(a, b))); }
    void arc(EntityId c, EntityId a, EntityId b) { edges.push_back(bettercad::test::require(sketch->addArc(c, a, b))); }
};

/// Points the model's sweep at a new path sketch.
ObjectId usePath(Document& doc, ObjectId sweep, PathBuilder builder) {
    const std::vector<EntityId> edges = builder.edges;
    const ObjectId path = addSketch(doc, std::move(builder.sketch));
    SweepDefinition d = doc.findObjectAs<SweepFeature>(sweep)->definition();
    d.path = {.sketch = sketchIdOf(path), .edges = edges};
    REQUIRE(doc.modifyObject<SweepFeature>(sweep, [&](SweepFeature& f) { return f.setDefinition(d); }).has_value());
    return path;
}

} // namespace

TEST_CASE("SweepFeature_DefinitionIsValidatedOnCreateAndEdit", "[sweep][features]") {
    const SweepDefinition good{.profile = SketchId::fromValue(4),
                               .path = {.sketch = SketchId::fromValue(5), .edges = {EntityId::fromValue(3)}}};
    REQUIRE(SweepFeature::create("S", good).has_value());
    SweepDefinition d = good;
    SECTION("profile and path sketches") {
        d.profile = SketchId{};
        CHECK(refusal(d).message == "a sweep needs a profile sketch");
        d = good;
        d.path.sketch = SketchId{};
        CHECK(refusal(d).message == "a sweep needs a path sketch");
        d.path.sketch = good.profile;
        CHECK(refusal(d).message ==
              "the path must be in another sketch than the profile: it leaves the profile's plane at right angles");
    }
    SECTION("path edges") {
        d.path.edges.clear();
        CHECK(refusal(d).message == "a sweep path needs at least one edge");
        d.path.edges = {EntityId::fromValue(3), EntityId{}};
        CHECK(refusal(d).message == "the path's edge IDs must be valid");
        d.path.edges = {EntityId::fromValue(3), EntityId::fromValue(7), EntityId::fromValue(3)};
        CHECK(refusal(d).message == "entity:3 is listed twice in the path");
        CHECK(refusal(d).code == ErrorCode::InvalidArgument);
    }
    SECTION("operation and target pairing") {
        d.operation = FeatureOperation::Cut;
        CHECK(errorCode(SweepFeature::create("S", d)) == ErrorCode::InvalidArgument);
        d.target = FeatureId::fromValue(2);
        CHECK(SweepFeature::create("S", d).has_value());
        d.operation = FeatureOperation::NewBody;
        CHECK(errorCode(SweepFeature::create("S", d)) == ErrorCode::InvalidArgument);
    }
    SECTION("an invalid edit leaves the feature unchanged") {
        auto feature = SweepFeature::create("S", good);
        REQUIRE(feature.has_value());
        d.path.edges.clear();
        CHECK(errorCode((*feature)->setDefinition(d)) == ErrorCode::InvalidArgument);
        CHECK((*feature)->definition() == good);
        const auto unchanged = (*feature)->setDefinition(good);
        REQUIRE(unchanged.has_value());
        CHECK_FALSE(*unchanged);
    }
    CHECK(toString(SweepOrientation::FollowPath) == "follow path");
}

TEST_CASE("SweepFeature_DependsOnProfilePathAndTarget", "[sweep][features]") {
    StraightSweepModel m;
    const auto* sweep = m.doc.findObjectAs<SweepFeature>(m.sweep);
    REQUIRE(sweep != nullptr);
    CHECK(sweep->dependencies() == std::vector<ObjectId>{m.profile, m.path});
    CHECK(sweep->typeName() == "sweep");
    CHECK_FALSE(sweep->target().has_value());
    CHECK(equivalent(*sweep->clone(), *sweep));
    ChannelModel channel;
    const auto* cut = channel.doc.findObjectAs<SweepFeature>(channel.channel);
    CHECK(cut->dependencies() == std::vector<ObjectId>{channel.channelProfile, channel.channelPath, channel.pad});
    CHECK(cut->target() == featureIdOf(channel.pad));

    // Each input dirties the sweep and nothing unrelated.
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).regenerated == std::vector<ObjectId>{m.profile, m.path, m.sweep});
    REQUIRE(m.doc.setParameterValue(m.length, 150_mm).has_value());
    CHECK(requireReport(regenerator, m.doc).regenerated == std::vector<ObjectId>{m.path, m.sweep});
    REQUIRE(m.doc.setParameterValue(m.width, 15_mm).has_value());
    CHECK(requireReport(regenerator, m.doc).regenerated == std::vector<ObjectId>{m.profile, m.sweep});
    CHECK(requireReport(regenerator, m.doc).regenerated.empty()); // nothing changed
    Regenerator channelRegenerator;
    REQUIRE(requireReport(channelRegenerator, channel.doc).succeeded());
    REQUIRE(channel.doc.setParameterValue(channel.height, 30_mm).has_value());
    CHECK(requireReport(channelRegenerator, channel.doc).regenerated ==
          std::vector<ObjectId>{channel.pad, channel.channel});
}

TEST_CASE("SweepFeature_StraightRectangleMatchesAnalyticVolume", "[sweep][features][acceptance]") {
    // The primary acceptance case: a 10 x 20 mm rectangle swept along a
    // straight 100 mm path. V = A L = 200 x 100 = 20000 mm^3.
    StraightSweepModel m;
    Regenerator regenerator;
    const RegenerationReport report = requireReport(regenerator, m.doc);
    INFO(describe(report));
    REQUIRE(report.succeeded());
    const geometry::Body& body = requireBody(regenerator, m.sweep);
    CHECK(body.isValid());
    CHECK(body.topology().solids == 1);
    const double expected = 10.0 * 20.0 * 100.0;
    const auto props = propertiesOf(regenerator, m.sweep);
    const double actual = props.volume.in(units::mm3);
    INFO("expected " << expected << " mm^3, actual " << actual << " mm^3, absolute error "
                     << std::abs(actual - expected) << " mm^3, relative " << std::abs(actual - expected) / expected);
    CHECK_THAT(actual, WithinRel(expected, kRel));
    CHECK_THAT(props.surfaceArea.in(units::mm2), WithinRel(2.0 * (200.0 + 1000.0 + 2000.0), kRel));
    checkPoint(props.centerOfMass, {5, 10, 50});
    checkBox(body, {0, 0, 0}, {10, 20, 100});
    CHECK(resultFeatures(m.doc) == std::vector<ObjectId>{m.sweep});
}

TEST_CASE("SweepFeature_StraightCircleMatchesAnalyticVolume", "[sweep][features][acceptance]") {
    // A circle r = 5 mm along 100 mm: V = pi r^2 L = 2500 pi.
    StraightTubeModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const geometry::Body& body = requireBody(regenerator, m.sweep);
    CHECK(body.isValid());
    CHECK(body.topology().solids == 1);
    const auto props = propertiesOf(regenerator, m.sweep);
    CHECK_THAT(props.volume.in(units::mm3), WithinRel(2500.0 * pi, kRel));
    CHECK_THAT(props.surfaceArea.in(units::mm2), WithinRel(2.0 * pi * 5.0 * 100.0 + 2.0 * pi * 25.0, kRel));
    checkPoint(props.centerOfMass, {0, 0, 50});
    checkBox(body, {-5, -5, 0}, {5, 5, 100});
}

TEST_CASE("SweepFeature_StraightPathMatchesExtrude", "[sweep][features][acceptance]") {
    // The same closed profile extruded by the same length: a verified
    // feature gives the same solid, and both follow the parameters.
    StraightSweepModel m;
    auto extrude = ExtrudeFeature::create("Extrude", {.profile = sketchIdOf(m.profile), .depthParameter = m.length});
    REQUIRE(extrude.has_value());
    const ObjectId pad = m.doc.addObject(std::move(*extrude)).value();
    Regenerator regenerator;
    const auto compare = [&](double expected) {
        const RegenerationReport report = requireReport(regenerator, m.doc);
        INFO(describe(report));
        REQUIRE(report.succeeded());
        const auto swept = propertiesOf(regenerator, m.sweep);
        const auto extruded = propertiesOf(regenerator, pad);
        INFO("sweep " << swept.volume.in(units::mm3) << " mm^3, extrude " << extruded.volume.in(units::mm3)
                      << " mm^3");
        CHECK_THAT(swept.volume.in(units::mm3), WithinRel(expected, kRel));
        CHECK_THAT(swept.volume.in(units::mm3), WithinRel(extruded.volume.in(units::mm3), kRel));
        CHECK_THAT(swept.surfaceArea.in(units::mm2), WithinRel(extruded.surfaceArea.in(units::mm2), kRel));
        CHECK_THAT((swept.centerOfMass.x - extruded.centerOfMass.x).in(units::mm), WithinAbs(0.0, kPositionToleranceMm));
        CHECK_THAT((swept.centerOfMass.y - extruded.centerOfMass.y).in(units::mm), WithinAbs(0.0, kPositionToleranceMm));
        CHECK_THAT((swept.centerOfMass.z - extruded.centerOfMass.z).in(units::mm), WithinAbs(0.0, kPositionToleranceMm));
        const BoundingBox3D a = requireBody(regenerator, m.sweep).boundingBox().value();
        const BoundingBox3D b = requireBody(regenerator, pad).boundingBox().value();
        checkPoint(a.min, {b.min.x.in(units::mm), b.min.y.in(units::mm), b.min.z.in(units::mm)});
        checkPoint(a.max, {b.max.x.in(units::mm), b.max.y.in(units::mm), b.max.z.in(units::mm)});
        CHECK(requireBody(regenerator, m.sweep).topology().solids == requireBody(regenerator, pad).topology().solids);
    };
    compare(20000.0);
    REQUIRE(m.doc.setParameterValue(m.width, 15_mm).has_value());
    REQUIRE(m.doc.setParameterValue(m.length, 60_mm).has_value());
    compare(15.0 * 20.0 * 60.0);
}

TEST_CASE("SweepFeature_ReversedPathSweepsTheOtherWay", "[sweep][features][acceptance]") {
    // The same rectangle along (0, 0, 0) -> (0, 0, -100): the solid lies below
    // the profile, z from -100 to 0; the original lies above.
    StraightSweepModel m;
    PathBuilder down{Frame3D::xz()};
    down.line(down.point(0, 0), down.point(0, -100));
    usePath(m.doc, m.sweep, std::move(down));
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK_THAT(volumeMm3(regenerator, m.sweep), WithinRel(20000.0, kRel));
    checkBox(requireBody(regenerator, m.sweep), {0, 0, -100}, {10, 20, 0});
    checkPoint(propertiesOf(regenerator, m.sweep).centerOfMass, {5, 10, -50});
}

TEST_CASE("SweepFeature_QuarterCircleMatchesAnalyticVolume", "[sweep][features][acceptance]") {
    // A circle r = 2 mm swept a quarter turn along an arc of radius R = 20 mm:
    // L = R theta = 10 pi, A = 4 pi, V = A L = 40 pi^2.
    ArcSweepModel m;
    Regenerator regenerator;
    const RegenerationReport report = requireReport(regenerator, m.doc);
    INFO(describe(report));
    REQUIRE(report.succeeded());
    const geometry::Body& body = requireBody(regenerator, m.sweep);
    CHECK(body.isValid());
    CHECK(body.topology().solids == 1);
    const double expected = 40.0 * pi * pi;
    CHECK_THAT(ArcSweepModel::expectedVolume(2, 20), WithinRel(expected, 1e-15));
    const auto props = propertiesOf(regenerator, m.sweep);
    const double actual = props.volume.in(units::mm3);
    INFO("expected " << expected << " mm^3, actual " << actual << " mm^3, absolute error "
                     << std::abs(actual - expected) << " mm^3, relative " << std::abs(actual - expected) / expected);
    CHECK_THAT(actual, WithinRel(expected, kRel));
    // The tube's wall is 2 pi r x R theta (Pappus), its ends two discs.
    CHECK_THAT(props.surfaceArea.in(units::mm2), WithinRel(40.0 * pi * pi + 8.0 * pi, kRel));
    // The centroid lies on the bisector of the arc, (2 sin(theta/2) / theta)
    // (R + r^2 / 4R) from its centre (R, 0, 0): here k = (2/pi)(R + r^2/4R)
    // along -X and -Z.
    const double k = 2.0 / pi * (20.0 + 4.0 / 80.0);
    checkPoint(props.centerOfMass, {20.0 - k, 0, -k});
    // It leaves the origin downwards and ends across the plane x = 20.
    checkPaddedBox(body, {-2, -2, -22}, {20, 2, 0});
    CHECK_THAT(areaOn(regenerator, m.sweep, 0, 0, 0, 0, 0, 1), WithinRel(4.0 * pi, kRel));  // the start
    CHECK_THAT(areaOn(regenerator, m.sweep, 20, 0, 0, 1, 0, 0), WithinRel(4.0 * pi, kRel)); // the end
    CHECK(circlesAt(regenerator, m.sweep, {20, 0, -20}, 2, {1, 0, 0}) == 1);
}

TEST_CASE("SweepFeature_ClosedCircularPathMatchesTorusVolume", "[sweep][features][acceptance]") {
    // A circle r = 2 mm around a closed circle R = 20 mm: V = 2 pi^2 R r^2 =
    // 160 pi^2, A = 4 pi^2 R r.
    TorusSweepModel m;
    Regenerator regenerator;
    const RegenerationReport report = requireReport(regenerator, m.doc);
    INFO(describe(report));
    REQUIRE(report.succeeded());
    const geometry::Body& body = requireBody(regenerator, m.sweep);
    CHECK(body.isValid());
    CHECK(body.topology().solids == 1);
    const auto props = propertiesOf(regenerator, m.sweep);
    const double expected = TorusSweepModel::expectedVolume(2, 20);
    CHECK_THAT(expected, WithinRel(160.0 * pi * pi, 1e-15));
    INFO("expected " << expected << " mm^3, actual " << props.volume.in(units::mm3) << " mm^3");
    CHECK_THAT(props.volume.in(units::mm3), WithinRel(expected, kRel));
    CHECK_THAT(props.surfaceArea.in(units::mm2), WithinRel(4.0 * pi * pi * 20.0 * 2.0, kRel));
    checkPoint(props.centerOfMass, {0, 0, 0});
    checkPaddedBox(body, {-22, -22, -2}, {22, 22, 2});
}

TEST_CASE("SweepFeature_TorusMatchesRevolve", "[sweep][features][revolve][acceptance]") {
    // The torus as a sweep of the circle around the circular path, and as a
    // revolve of the same profile sketch about the Z axis (its sketch Y axis):
    // both 2 pi^2 R r^2, and both follow R.
    TorusSweepModel m;
    auto revolve = RevolveFeature::create("Revolve", {.profile = sketchIdOf(m.profile), .axis = RevolveAxis::sketchY()});
    REQUIRE(revolve.has_value());
    const ObjectId turned = m.doc.addObject(std::move(*revolve)).value();
    Regenerator regenerator;
    for (const double bend : {20.0, 30.0}) {
        CAPTURE(bend);
        REQUIRE(m.doc.setParameterValue(m.bend, bend * units::mm).has_value());
        const RegenerationReport report = requireReport(regenerator, m.doc);
        INFO(describe(report));
        REQUIRE(report.succeeded());
        const auto swept = propertiesOf(regenerator, m.sweep);
        const auto revolved = propertiesOf(regenerator, turned);
        const double expected = TorusSweepModel::expectedVolume(2, bend);
        INFO("sweep " << swept.volume.in(units::mm3) << ", revolve " << revolved.volume.in(units::mm3) << ", formula "
                      << expected << " mm^3");
        CHECK_THAT(swept.volume.in(units::mm3), WithinRel(expected, kRel));
        CHECK_THAT(revolved.volume.in(units::mm3), WithinRel(expected, kRel));
        CHECK_THAT(swept.volume.in(units::mm3), WithinRel(revolved.volume.in(units::mm3), kRel));
        CHECK_THAT(swept.surfaceArea.in(units::mm2), WithinRel(revolved.surfaceArea.in(units::mm2), kRel));
        checkPoint(swept.centerOfMass, {0, 0, 0});
        checkPoint(revolved.centerOfMass, {0, 0, 0});
        checkPaddedBox(requireBody(regenerator, m.sweep), {-bend - 2, -bend - 2, -2}, {bend + 2, bend + 2, 2});
        checkPaddedBox(requireBody(regenerator, turned), {-bend - 2, -bend - 2, -2}, {bend + 2, bend + 2, 2});
    }
}

TEST_CASE("SweepFeature_PolylinePathProducesValidSolid", "[sweep][features][acceptance]") {
    // (0, 0, 0) -> (50, 0, 0) -> (50, 50, 0), a circle r = 2 mm: the corner is
    // mitred, which keeps V = A L (400 pi) for a profile centred on the path.
    FixedPathSweepModel m{FixedPathSweepModel::Route::Polyline};
    const auto path = resolveSweepPath(m.doc.findObjectAs<SweepFeature>(m.sweep)->definition().path, m.doc);
    REQUIRE(path.has_value());
    REQUIRE(path->segments.size() == 2);
    Regenerator regenerator;
    const RegenerationReport report = requireReport(regenerator, m.doc);
    INFO(describe(report));
    REQUIRE(report.succeeded());
    const geometry::Body& body = requireBody(regenerator, m.sweep);
    CHECK(body.isValid());
    CHECK(body.topology().solids == 1);
    const double volume = volumeMm3(regenerator, m.sweep);
    INFO("V " << volume << " mm^3, A L = " << 400.0 * pi);
    CHECK(volume > 0.0);
    CHECK_THAT(volume, WithinRel(400.0 * pi, kRelMitre));
    // The mitre's outer corner reaches x = 50 + 2 tan 45 deg.
    checkBox(body, {0, -2, -2}, {52, 50, 2});
    // The ends: a disc at the start facing -X and at the end (50, 50, 0) facing +Y.
    CHECK_THAT(areaOn(regenerator, m.sweep, 0, 0, 0, -1, 0, 0), WithinRel(4.0 * pi, kRel));
    CHECK_THAT(areaOn(regenerator, m.sweep, 0, 50, 0, 0, 1, 0), WithinRel(4.0 * pi, kRel));
    CHECK(circlesAt(regenerator, m.sweep, {50, 50, 0}, 2, {0, 1, 0}) == 1);
    // Regenerating again gives the same solid, bit for bit.
    Regenerator again;
    REQUIRE(requireReport(again, m.doc).succeeded());
    CHECK(bits(volumeMm3(again, m.sweep)) == bits(volume));
}

TEST_CASE("SweepFeature_MixedLineArcPathProducesValidSolid", "[sweep][features][acceptance]") {
    // 50 mm, a tangent quarter arc R = 20 mm, 50 mm: L = 100 + 10 pi and
    // V = 4 pi L exactly (tangent joints need no mitre).
    FixedPathSweepModel m{FixedPathSweepModel::Route::LineArcLine};
    Regenerator regenerator;
    const RegenerationReport report = requireReport(regenerator, m.doc);
    INFO(describe(report));
    REQUIRE(report.succeeded());
    const geometry::Body& body = requireBody(regenerator, m.sweep);
    CHECK(body.isValid());
    CHECK(body.topology().solids == 1);
    CHECK_THAT(volumeMm3(regenerator, m.sweep), WithinRel(4.0 * pi * (100.0 + 10.0 * pi), kRel));
    // Start at the origin facing -X, end at (70, 70, 0) facing +Y.
    CHECK_THAT(areaOn(regenerator, m.sweep, 0, 0, 0, -1, 0, 0), WithinRel(4.0 * pi, kRel));
    CHECK_THAT(areaOn(regenerator, m.sweep, 0, 70, 0, 0, 1, 0), WithinRel(4.0 * pi, kRel));
    CHECK(circlesAt(regenerator, m.sweep, {70, 70, 0}, 2, {0, 1, 0}) == 1);
    checkPaddedBox(body, {0, -2, -2}, {72, 70, 2});
    // The resolved path: the edges in order, head to tail, tangent.
    const auto path = resolveSweepPath(m.doc.findObjectAs<SweepFeature>(m.sweep)->definition().path, m.doc);
    REQUIRE(path.has_value());
    REQUIRE(path->segments.size() == 3);
    CHECK(std::holds_alternative<geometry::LineSegment2D>(path->segments[0]));
    CHECK(std::holds_alternative<geometry::ArcSegment2D>(path->segments[1]));
    CHECK(std::get<geometry::ArcSegment2D>(path->segments[1]).counterClockwise);
}

TEST_CASE("SweepFeature_NewBody", "[sweep][features][acceptance]") {
    // A new body is the swept tool alone: one valid solid, the result body.
    for (const bool curved : {false, true}) {
        CAPTURE(curved);
        if (!curved) {
            StraightSweepModel m;
            Regenerator regenerator;
            REQUIRE(requireReport(regenerator, m.doc).succeeded());
            CHECK(requireBody(regenerator, m.sweep).topology().solids == 1);
            CHECK_THAT(volumeMm3(regenerator, m.sweep), WithinRel(20000.0, kRel));
            checkBox(requireBody(regenerator, m.sweep), {0, 0, 0}, {10, 20, 100});
            checkPoint(propertiesOf(regenerator, m.sweep).centerOfMass, {5, 10, 50});
            CHECK(resultFeatures(m.doc) == std::vector<ObjectId>{m.sweep});
        } else {
            ArcSweepModel m;
            Regenerator regenerator;
            REQUIRE(requireReport(regenerator, m.doc).succeeded());
            CHECK(requireBody(regenerator, m.sweep).topology().solids == 1);
            CHECK_THAT(volumeMm3(regenerator, m.sweep), WithinRel(40.0 * pi * pi, kRel));
            CHECK(resultFeatures(m.doc) == std::vector<ObjectId>{m.sweep});
        }
    }
}

TEST_CASE("SweepFeature_Add", "[sweep][features][acceptance]") {
    SECTION("a curved handle joined to the block's top") {
        // A half-turn tube r = 2 mm, R = 20 mm standing on the top face:
        // V = 100000 + pi r^2 R pi = 100000 + 80 pi^2.
        HandleModel m;
        Regenerator regenerator;
        const RegenerationReport report = requireReport(regenerator, m.doc);
        INFO(describe(report));
        REQUIRE(report.succeeded());
        const geometry::Body& body = requireBody(regenerator, m.handle);
        CHECK(body.isValid());
        CHECK(body.topology().solids == 1);
        CHECK_THAT(volumeMm3(regenerator, m.handle), WithinRel(100000.0 + 80.0 * pi * pi, kRel));
        // The top of the handle: z = 20 + R + r.
        checkPaddedBox(body, {0, 0, 0}, {100, 50, 42});
        // The top face loses the two discs where the handle stands.
        CHECK_THAT(areaOn(regenerator, m.handle, 0, 0, 20, 0, 0, 1), WithinRel(5000.0 - 8.0 * pi, kRel));
        CHECK(resultFeatures(m.doc) == std::vector<ObjectId>{m.handle});
    }
    SECTION("a straight boss on the top face") {
        // A 10 x 20 mm rectangle on the top face swept 30 mm up: V = 100000 + 6000.
        BlockModel m{"spare", 1_mm};
        const auto top = Frame3D::create(Point3D{0_mm, 0_mm, 20_mm}, Direction3D::unitZ(), Direction3D::unitX()).value();
        auto rectangle = std::make_unique<sketch::Sketch>("BossProfile", top);
        const auto lines = bettercad::test::addRectangle(*rectangle, 40_mm, 15_mm, 10_mm, 20_mm);
        for (const EntityId line : lines) {
            bettercad::test::require(rectangle->addFixed(std::get<sketch::LineEntity>(rectangle->findEntity(line)->geometry).start));
        }
        const ObjectId profile = addSketch(m.doc, std::move(rectangle));
        const auto riser =
            Frame3D::create(Point3D{0_mm, 15_mm, 0_mm}, Direction3D::unitY().reversed(), Direction3D::unitX()).value();
        PathBuilder up{riser};
        up.line(up.point(40, 20), up.point(40, 50));
        const std::vector<EntityId> edges = up.edges;
        const ObjectId path = addSketch(m.doc, std::move(up.sketch));
        const ObjectId boss = m.add<SweepFeature>("Boss", {.profile = sketchIdOf(profile),
                                                           .path = {.sketch = sketchIdOf(path), .edges = edges},
                                                           .operation = FeatureOperation::Join,
                                                           .target = BlockModel::featureId(m.pad)});
        Regenerator regenerator;
        const RegenerationReport report = requireReport(regenerator, m.doc);
        INFO(describe(report));
        REQUIRE(report.succeeded());
        CHECK(requireBody(regenerator, boss).topology().solids == 1);
        CHECK_THAT(volumeMm3(regenerator, boss), WithinRel(106000.0, kRel));
        checkBox(requireBody(regenerator, boss), {0, 0, 0}, {100, 50, 50});
    }
}

TEST_CASE("SweepFeature_Remove", "[sweep][features][acceptance]") {
    SECTION("a straight channel through the block") {
        // r = 5 mm, 1 mm beyond both ends: V = 100000 - pi r^2 x 100.
        ChannelModel m;
        Regenerator regenerator;
        const RegenerationReport report = requireReport(regenerator, m.doc);
        INFO(describe(report));
        REQUIRE(report.succeeded());
        CHECK(requireBody(regenerator, m.channel).isValid());
        CHECK(requireBody(regenerator, m.channel).topology().solids == 1);
        CHECK_THAT(volumeMm3(regenerator, m.channel), WithinRel(ChannelModel::expectedVolume(20, 5), kRel));
        // The channel opens on both end faces.
        CHECK(circlesAt(regenerator, m.channel, {0, 25, 10}, 5, {1, 0, 0}) == 1);
        CHECK(circlesAt(regenerator, m.channel, {100, 25, 10}, 5, {1, 0, 0}) == 1);
        checkBox(requireBody(regenerator, m.channel), {0, 0, 0}, {100, 50, 20});
        CHECK(resultFeatures(m.doc) == std::vector<ObjectId>{m.channel});
    }
    SECTION("a curved blind channel inside the block") {
        // A circle r = 2 mm on the plane x = 40 about (y, z) = (25, 10), swept
        // a quarter turn R = 20 mm in the plane z = 10 to (60, 45, 10):
        // wholly inside, V = 100000 - 40 pi^2.
        BlockModel m{"spare", 1_mm};
        const auto across =
            Frame3D::create(Point3D{40_mm, 0_mm, 0_mm}, Direction3D::unitX(), Direction3D::unitY()).value();
        auto disc = std::make_unique<sketch::Sketch>("GrooveProfile", across);
        const EntityId circle = bettercad::test::require(disc->addCircle(Point2D{25_mm, 10_mm}, 2_mm));
        bettercad::test::require(disc->addFixed(std::get<sketch::CircleEntity>(disc->findEntity(circle)->geometry).center));
        bettercad::test::require(disc->addRadius(circle, 2_mm));
        const ObjectId profile = addSketch(m.doc, std::move(disc));
        const auto level = Frame3D::create(Point3D{0_mm, 0_mm, 10_mm}, Direction3D::unitZ(), Direction3D::unitX()).value();
        PathBuilder bend{level};
        const EntityId c = bend.point(40, 45);
        const EntityId start = bend.point(40, 25);
        const EntityId end = bettercad::test::require(bend.sketch->addPoint(Point2D{60_mm, 45_mm}));
        bettercad::test::require(bend.sketch->addHorizontal(c, end));
        bend.arc(c, start, end);
        const std::vector<EntityId> edges = bend.edges;
        const ObjectId path = addSketch(m.doc, std::move(bend.sketch));
        const ObjectId groove = m.add<SweepFeature>("Groove", {.profile = sketchIdOf(profile),
                                                               .path = {.sketch = sketchIdOf(path), .edges = edges},
                                                               .operation = FeatureOperation::Cut,
                                                               .target = BlockModel::featureId(m.pad)});
        Regenerator regenerator;
        const RegenerationReport report = requireReport(regenerator, m.doc);
        INFO(describe(report));
        REQUIRE(report.succeeded());
        CHECK(requireBody(regenerator, groove).isValid());
        CHECK_THAT(volumeMm3(regenerator, groove), WithinRel(100000.0 - 40.0 * pi * pi, kRel));
        // A cavity: the outer box and faces are the block's.
        checkBox(requireBody(regenerator, groove), {0, 0, 0}, {100, 50, 20});
    }
}

TEST_CASE("SweepFeature_Intersect", "[sweep][features][acceptance]") {
    // The channel's rod intersected with the block: the rod's 100 mm inside,
    // V = pi r^2 x 100 = 2500 pi.
    ChannelModel m{FeatureOperation::Intersect};
    Regenerator regenerator;
    const RegenerationReport report = requireReport(regenerator, m.doc);
    INFO(describe(report));
    REQUIRE(report.succeeded());
    CHECK(requireBody(regenerator, m.channel).isValid());
    CHECK(requireBody(regenerator, m.channel).topology().solids == 1);
    CHECK_THAT(volumeMm3(regenerator, m.channel), WithinRel(2500.0 * pi, kRel));
    checkBox(requireBody(regenerator, m.channel), {0, 20, 5}, {100, 30, 15});
}

TEST_CASE("SweepFeature_RegeneratesWhenProfileChanges", "[sweep][features][acceptance]") {
    SECTION("the arc sweep's radius 2 -> 3 mm: V grows with r^2") {
        ArcSweepModel m;
        Regenerator regenerator;
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        const double before = volumeMm3(regenerator, m.sweep);
        REQUIRE(m.doc.setParameterValue(m.radius, 3_mm).has_value());
        CHECK(requireReport(regenerator, m.doc).regenerated == std::vector<ObjectId>{m.profile, m.sweep});
        CHECK_THAT(volumeMm3(regenerator, m.sweep), WithinRel(ArcSweepModel::expectedVolume(3, 20), kRel));
        CHECK_THAT(volumeMm3(regenerator, m.sweep) / before, WithinRel(9.0 / 4.0, kRel));
        checkPaddedBox(requireBody(regenerator, m.sweep), {-3, -3, -23}, {20, 3, 0}); // no stale tube
    }
    SECTION("the rectangle's width 10 -> 15 mm") {
        StraightSweepModel m;
        Regenerator regenerator;
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        REQUIRE(m.doc.setParameterValue(m.width, 15_mm).has_value());
        CHECK(requireReport(regenerator, m.doc).regenerated == std::vector<ObjectId>{m.profile, m.sweep});
        CHECK_THAT(volumeMm3(regenerator, m.sweep), WithinRel(15.0 * 20.0 * 100.0, kRel));
        checkBox(requireBody(regenerator, m.sweep), {0, 0, 0}, {15, 20, 100});
    }
}

TEST_CASE("SweepFeature_RegeneratesWhenPathChanges", "[sweep][features][acceptance]") {
    SECTION("the straight path 100 -> 150 mm: V from A x 100 to A x 150") {
        StraightSweepModel m;
        Regenerator regenerator;
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        REQUIRE(m.doc.setParameterValue(m.length, 150_mm).has_value());
        CHECK(requireReport(regenerator, m.doc).regenerated == std::vector<ObjectId>{m.path, m.sweep});
        CHECK_THAT(volumeMm3(regenerator, m.sweep), WithinRel(200.0 * 150.0, kRel));
        checkBox(requireBody(regenerator, m.sweep), {0, 0, 0}, {10, 20, 150});
    }
    SECTION("the arc's radius R 20 -> 30 mm: V = pi r^2 R pi/2") {
        ArcSweepModel m;
        Regenerator regenerator;
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        REQUIRE(m.doc.setParameterValue(m.bend, 30_mm).has_value());
        CHECK(requireReport(regenerator, m.doc).regenerated == std::vector<ObjectId>{m.path, m.sweep});
        CHECK_THAT(volumeMm3(regenerator, m.sweep), WithinRel(ArcSweepModel::expectedVolume(2, 30), kRel));
        checkPaddedBox(requireBody(regenerator, m.sweep), {-2, -2, -32}, {30, 2, 0});
        CHECK(circlesAt(regenerator, m.sweep, {30, 0, -30}, 2, {1, 0, 0}) == 1);
        CHECK(circlesAt(regenerator, m.sweep, {20, 0, -20}, 2, {1, 0, 0}) == 0); // no stale end
    }
    SECTION("the torus' R 20 -> 30 mm drives the path and the profile together") {
        TorusSweepModel m;
        Regenerator regenerator;
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        REQUIRE(m.doc.setParameterValue(m.bend, 30_mm).has_value());
        CHECK(requireReport(regenerator, m.doc).regenerated == std::vector<ObjectId>{m.profile, m.path, m.sweep});
        CHECK_THAT(volumeMm3(regenerator, m.sweep), WithinRel(TorusSweepModel::expectedVolume(2, 30), kRel));
    }
}

TEST_CASE("SweepFeature_RegeneratesWhenTargetChanges", "[sweep][features][acceptance]") {
    // The block 20 -> 30 mm high: the channel (at z = 10) is cut from the new block.
    ChannelModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    REQUIRE(m.doc.setParameterValue(m.height, 30_mm).has_value());
    CHECK(requireReport(regenerator, m.doc).regenerated == std::vector<ObjectId>{m.pad, m.channel});
    CHECK_THAT(volumeMm3(regenerator, m.channel), WithinRel(ChannelModel::expectedVolume(30, 5), kRel));
    checkBox(requireBody(regenerator, m.channel), {0, 0, 0}, {100, 50, 30});
    // And the channel's radius: 5 -> 4 mm.
    REQUIRE(m.doc.setParameterValue(m.radius, 4_mm).has_value());
    CHECK(requireReport(regenerator, m.doc).regenerated == std::vector<ObjectId>{m.channelProfile, m.channel});
    CHECK_THAT(volumeMm3(regenerator, m.channel), WithinRel(ChannelModel::expectedVolume(30, 4), kRel));
}

TEST_CASE("SweepFeature_RejectsOpenProfile", "[sweep][features][acceptance]") {
    // Three sides of a rectangle: no closed profile, nothing is swept.
    StraightSweepModel m;
    auto open = std::make_unique<sketch::Sketch>("Open");
    const EntityId a = bettercad::test::require(open->addPoint(Point2D{}));
    const EntityId b = bettercad::test::require(open->addPoint(Point2D{10_mm, 0_mm}));
    const EntityId c = bettercad::test::require(open->addPoint(Point2D{10_mm, 20_mm}));
    const EntityId d = bettercad::test::require(open->addPoint(Point2D{0_mm, 20_mm}));
    for (const auto& [p, q] : {std::pair{a, b}, std::pair{b, c}, std::pair{c, d}}) {
        bettercad::test::require(open->addLine(p, q));
    }
    const ObjectId openId = addSketch(m.doc, std::move(open));
    SweepDefinition def = m.definitionOf(m.sweep);
    def.profile = sketchIdOf(openId);
    m.setDefinition(m.sweep, def);
    Regenerator regenerator;
    const Error error = requireFailure(regenerator, m.doc, m.sweep);
    CHECK(error.code == ErrorCode::FailedPrecondition);
    CHECK_THAT(error.message, StartsWith("Sweep: the profile is open: an edge ends at "));
}

TEST_CASE("SweepFeature_RejectsInvalidProfiles", "[sweep][features][acceptance]") {
    StraightSweepModel m;
    Regenerator regenerator;
    const auto sweepWith = [&](std::vector<Point2D> corners) {
        auto sketch = std::make_unique<sketch::Sketch>("Loop");
        std::vector<EntityId> points;
        for (const Point2D& p : corners) {
            points.push_back(bettercad::test::require(sketch->addPoint(p)));
        }
        for (std::size_t i = 0; i < points.size(); ++i) {
            bettercad::test::require(sketch->addLine(points[i], points[(i + 1) % points.size()]));
        }
        const ObjectId id = addSketch(m.doc, std::move(sketch));
        SweepDefinition def = m.definitionOf(m.sweep);
        def.profile = sketchIdOf(id);
        m.setDefinition(m.sweep, def);
        return requireFailure(regenerator, m.doc, m.sweep);
    };
    SECTION("a self-intersecting (bow-tie) loop") {
        const Error error = sweepWith({Point2D{}, Point2D{20_mm, 10_mm}, Point2D{20_mm, 0_mm}, Point2D{0_mm, 20_mm}});
        CHECK(error.code == ErrorCode::Internal);
        CHECK_THAT(error.message, StartsWith("Sweep: makeSweep: the profile face is invalid"));
    }
    SECTION("a loop enclosing no area") {
        const Error error = sweepWith({Point2D{}, Point2D{10_mm, 10_mm}, Point2D{10_mm, 0_mm}, Point2D{0_mm, 10_mm}});
        CHECK(error.code == ErrorCode::InvalidArgument);
        CHECK(error.message == "Sweep: a profile loop encloses no area");
    }
}

TEST_CASE("SweepFeature_RejectsMissingProfile", "[sweep][features][acceptance]") {
    StraightSweepModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    REQUIRE(m.doc.removeObject(m.profile).has_value());
    const Error error = requireFailure(regenerator, m.doc, m.sweep);
    CHECK(error.code == ErrorCode::NotFound);
    CHECK(error.message == "object:6 references object:4, which does not exist");
}

TEST_CASE("SweepFeature_RejectsMissingPath", "[sweep][features][acceptance]") {
    SECTION("the path sketch is deleted, then put back with its ID") {
        StraightSweepModel m;
        Regenerator regenerator;
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        const double swept = volumeMm3(regenerator, m.sweep);
        auto removed = m.doc.removeObject(m.path);
        REQUIRE(removed.has_value());
        const Error error = requireFailure(regenerator, m.doc, m.sweep);
        CHECK(error.code == ErrorCode::NotFound);
        CHECK(error.message == "object:6 references object:5, which does not exist");
        REQUIRE(m.doc.insertObject(std::move(*removed)).has_value());
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CHECK(volumeMm3(regenerator, m.sweep) == swept);
    }
    SECTION("a path edge that is not in the sketch: no other edge is taken") {
        StraightSweepModel m;
        SweepDefinition def = m.definitionOf(m.sweep);
        def.path.edges = {EntityId::fromValue(99)};
        m.setDefinition(m.sweep, def);
        Regenerator regenerator;
        const Error error = requireFailure(regenerator, m.doc, m.sweep);
        CHECK(error.code == ErrorCode::NotFound);
        CHECK(error.message == "Sweep: the path edge entity:99 does not exist in sketch 'Path'");
    }
    SECTION("a path sketch reference that is not a sketch") {
        StraightSweepModel m;
        const auto path = resolveSweepPath({.sketch = SketchId::fromValue(m.sweep.value()), .edges = {m.pathLine}}, m.doc);
        REQUIRE_FALSE(path.has_value());
        CHECK(path.error().code == ErrorCode::NotFound);
        CHECK(path.error().message == "path sketch:6 is not a sketch in this document");
        CHECK(errorCode(resolveSweepPath({.sketch = sketchIdOf(m.path), .edges = {}}, m.doc)) ==
              ErrorCode::InvalidArgument);
    }
    SECTION("a definition without path edges is refused before it reaches the document") {
        CHECK(refusal({.profile = SketchId::fromValue(1), .path = {.sketch = SketchId::fromValue(2), .edges = {}}})
                  .message == "a sweep path needs at least one edge");
    }
}

TEST_CASE("SweepFeature_RejectsDisconnectedPath", "[sweep][features][acceptance]") {
    FixedPathSweepModel m{FixedPathSweepModel::Route::Polyline};
    Regenerator regenerator;
    SECTION("A -> B and D -> E with B != D") {
        PathBuilder gap;
        gap.line(gap.point(0, 0), gap.point(50, 0));
        gap.line(gap.point(51, 0), gap.point(51, 50));
        const std::vector<EntityId> edges = gap.edges;
        usePath(m.doc, m.sweep, std::move(gap));
        const Error error = requireFailure(regenerator, m.doc, m.sweep);
        CHECK(error.code == ErrorCode::InvalidArgument);
        CHECK(error.message == std::format("Sweep: the path is not connected: the edges {} and {} do not meet (their "
                                           "nearest ends are 1 mm apart)",
                                           edges[0], edges[1]));
    }
    SECTION("a branch: the third edge leaves from the first edge's start") {
        PathBuilder branch;
        const EntityId o = branch.point(0, 0);
        const EntityId p = branch.point(50, 0);
        branch.line(o, p);
        branch.line(p, branch.point(50, 50));
        branch.line(o, branch.point(0, 50));
        const std::vector<EntityId> edges = branch.edges;
        usePath(m.doc, m.sweep, std::move(branch));
        const Error error = requireFailure(regenerator, m.doc, m.sweep);
        CHECK(error.message == std::format("Sweep: the path is not connected: the edges {} and {} do not meet (their "
                                           "nearest ends are 50 mm apart)",
                                           edges[1], edges[2]));
    }
    SECTION("a point, and a circle joined with a line") {
        PathBuilder odd;
        const EntityId o = odd.point(0, 0);
        odd.line(o, odd.point(50, 0));
        odd.edges.push_back(o);
        const EntityId point = odd.edges.back();
        usePath(m.doc, m.sweep, std::move(odd));
        Error error = requireFailure(regenerator, m.doc, m.sweep);
        CHECK(error.message == std::format("Sweep: the path edge {} is a point, not a line, arc or circle", point));
        PathBuilder ring{Frame3D::xy(), "Ring"};
        ring.line(ring.point(0, 0), ring.point(50, 0));
        ring.edges.push_back(bettercad::test::require(ring.sketch->addCircle(Point2D{60_mm, 0_mm}, 10_mm)));
        const EntityId circle = ring.edges.back();
        usePath(m.doc, m.sweep, std::move(ring));
        error = requireFailure(regenerator, m.doc, m.sweep);
        CHECK(error.message == std::format("Sweep: the path edge {} is a circle, which is a closed path on its own and "
                                           "cannot be joined with other edges",
                                           circle));
    }
}

TEST_CASE("SweepFeature_RejectsZeroLengthSegment", "[sweep][features][acceptance]") {
    // A line whose end is moved onto its start: the path refuses it, naming
    // the edge, before any kernel call.
    FixedPathSweepModel m{FixedPathSweepModel::Route::Polyline};
    auto sketch = std::make_unique<sketch::Sketch>("Stub");
    const EntityId line = bettercad::test::require(sketch->addLine(Point2D{}, Point2D{10_mm, 0_mm}));
    const EntityId end = std::get<sketch::LineEntity>(sketch->findEntity(line)->geometry).end;
    REQUIRE(sketch->setPointPosition(end, Point2D{}).has_value());
    const ObjectId stub = addSketch(m.doc, std::move(sketch));
    const auto path = resolveSweepPath({.sketch = sketchIdOf(stub), .edges = {line}}, m.doc);
    REQUIRE_FALSE(path.has_value());
    CHECK(path.error().code == ErrorCode::InvalidArgument);
    CHECK(path.error().message == std::format("the path edge {} has zero length", line));
    // Through the regenerator the sketch solver refuses the collapsed line
    // first, and the sweep is blocked: no body either way.
    SweepDefinition def = m.doc.findObjectAs<SweepFeature>(m.sweep)->definition();
    def.path = {.sketch = sketchIdOf(stub), .edges = {line}};
    REQUIRE(m.doc.modifyObject<SweepFeature>(m.sweep, [&](SweepFeature& f) { return f.setDefinition(def); })
                .has_value());
    Regenerator regenerator;
    const RegenerationReport report = requireReport(regenerator, m.doc);
    INFO(describe(report));
    CHECK(report.failed == std::vector<ObjectId>{stub});
    CHECK_THAT(report.errors.at(stub).message,
               StartsWith("sketch 'Stub' is SOLVER_FAILURE: the solution collapses"));
    CHECK(std::ranges::find(report.blocked, m.sweep) != report.blocked.end());
    CHECK(regenerator.body(m.sweep) == nullptr);
}

TEST_CASE("SweepFeature_RejectsNonFinitePath", "[sweep][features][acceptance]") {
    // Sketches cannot hold NaN or infinite coordinates, so a document's path
    // never does; the geometry refuses one given directly, before the kernel.
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    StraightSweepModel m;
    auto* path = m.doc.findObjectAs<sketch::Sketch>(m.path);
    const EntityId end = std::get<sketch::LineEntity>(path->findEntity(m.pathLine)->geometry).end;
    for (const double bad : {nan, inf, -inf}) {
        CHECK(errorCode(m.doc.modifyObject<sketch::Sketch>(m.path, [&](sketch::Sketch& s) {
                  return s.setPointPosition(end, Point2D{Length::fromSi(bad), 0_mm});
              })) == ErrorCode::InvalidArgument);
        const geometry::PlanarRegion square{
            .plane = Frame3D::xy(),
            .outer = {{geometry::LineSegment2D{Point2D{}, Point2D{10_mm, 0_mm}},
                       geometry::LineSegment2D{Point2D{10_mm, 0_mm}, Point2D{10_mm, 10_mm}},
                       geometry::LineSegment2D{Point2D{10_mm, 10_mm}, Point2D{}}}},
            .holes = {}};
        const auto swept = geometry::makeSweep(
            square, {.plane = Frame3D::xz(),
                     .segments = {geometry::LineSegment2D{Point2D{}, Point2D{0_mm, Length::fromSi(bad)}}}});
        REQUIRE_FALSE(swept.has_value());
        CHECK(swept.error().message == "makeSweep: path segment 1 is not finite");
    }
}

TEST_CASE("SweepFeature_FailsSafelyOnSelfIntersection", "[sweep][features][acceptance]") {
    SECTION("a profile reaching past the arc's centre: refused before the kernel") {
        ArcSweepModel m;
        Regenerator regenerator;
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        const double good = volumeMm3(regenerator, m.sweep);
        REQUIRE(m.doc.setParameterValue(m.radius, 25_mm).has_value());
        const Error error = requireFailure(regenerator, m.doc, m.sweep);
        CHECK(error.code == ErrorCode::InvalidArgument);
        CHECK(error.message == "Sweep: makeSweep: the profile reaches 25 mm from the path towards the centre of path "
                               "segment 1, an arc of radius 20 mm: the swept solid would fold over itself");
        CHECK(regenerator.state(m.profile) == NodeState::Regenerated); // the profile itself is fine
        CHECK(regenerator.state(m.path) == NodeState::UpToDate);
        REQUIRE(m.doc.setParameterValue(m.radius, 2_mm).has_value());
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        // The profile is re-solved from r = 25: the same circle to rounding.
        CHECK_THAT(volumeMm3(regenerator, m.sweep), WithinRel(good, kRel));
    }
    SECTION("a path crossing itself: found by the kernel's self-interference check") {
        FixedPathSweepModel m{FixedPathSweepModel::Route::Polyline};
        PathBuilder loop;
        const EntityId a = loop.point(0, 0);
        const EntityId b = loop.point(50, 0);
        const EntityId c = loop.point(50, 10);
        const EntityId d = loop.point(40, 10);
        loop.line(a, b);
        loop.arc(c, b, d); // 270 deg counter-clockwise, tangent at both ends
        loop.line(d, loop.point(40, -30));
        usePath(m.doc, m.sweep, std::move(loop));
        Regenerator regenerator;
        const Error error = requireFailure(regenerator, m.doc, m.sweep);
        CHECK(error.code == ErrorCode::InvalidArgument);
        CHECK(error.message == "Sweep: makeSweep: the swept solid would intersect itself: the path comes back within "
                               "the profile's reach of itself");
    }
}

TEST_CASE("SweepFeature_RejectsMisplacedProfile", "[sweep][features][acceptance]") {
    StraightSweepModel m;
    Regenerator regenerator;
    SECTION("the profile 5 mm above the path's start") {
        const auto raised = Frame3D::create(Point3D{0_mm, 0_mm, 5_mm}, Direction3D::unitZ(), Direction3D::unitX()).value();
        REQUIRE(m.doc.modifyObject<sketch::Sketch>(m.profile, [&](sketch::Sketch& s) { return s.setPlacement(raised); })
                    .has_value());
        const Error error = requireFailure(regenerator, m.doc, m.sweep);
        CHECK(error.code == ErrorCode::InvalidArgument);
        CHECK(error.message == "Sweep: makeSweep: the path must start on the profile's plane, but it starts 5 mm from it");
    }
    SECTION("a path leaving the profile at 45 deg") {
        PathBuilder slant{Frame3D::xz()};
        slant.line(slant.point(0, 0), slant.point(50, 50));
        usePath(m.doc, m.sweep, std::move(slant));
        const Error error = requireFailure(regenerator, m.doc, m.sweep);
        CHECK(error.message == "Sweep: makeSweep: the path must leave the profile's plane at right angles, but it leaves "
                               "at 45 deg to the plane's normal");
    }
}

TEST_CASE("SweepFeature_RejectsUnsupportedCorners", "[sweep][features][acceptance]") {
    FixedPathSweepModel m{FixedPathSweepModel::Route::Polyline};
    Regenerator regenerator;
    SECTION("a line meeting an arc at 90 deg") {
        // The arc entity runs counter-clockwise from (70, 20) to (50, 0); the
        // path takes it the other way, leaving (50, 0) along +Y.
        PathBuilder kink;
        const EntityId a = kink.point(0, 0);
        const EntityId b = kink.point(50, 0);
        kink.line(a, b);
        kink.arc(kink.point(70, 0), kink.point(70, 20), b);
        usePath(m.doc, m.sweep, std::move(kink));
        const Error error = requireFailure(regenerator, m.doc, m.sweep);
        CHECK(error.code == ErrorCode::InvalidArgument);
        CHECK(error.message == "Sweep: makeSweep: path segments 1 (a line) and 2 (an arc) meet at an angle of 90 deg; "
                               "only two straight segments may meet at a corner, and an arc must meet its neighbours "
                               "tangentially");
    }
    SECTION("a line turning back on itself") {
        PathBuilder hairpin;
        const EntityId b = hairpin.point(50, 0);
        hairpin.line(hairpin.point(0, 0), b);
        hairpin.line(b, hairpin.point(10, 0));
        usePath(m.doc, m.sweep, std::move(hairpin));
        const Error error = requireFailure(regenerator, m.doc, m.sweep);
        CHECK(error.message == "Sweep: makeSweep: the path turns back on itself where segments 1 and 2 meet");
    }
}

TEST_CASE("SweepFeature_FailuresLeaveTheDocumentAndUpstreamBodiesIntact", "[sweep][features][acceptance]") {
    ChannelModel m;
    CommandHistory history;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const Document before = m.doc.clone();
    const std::uint64_t revision = m.doc.revision();
    const double padVolume = volumeMm3(regenerator, m.pad);
    const double channelVolume = volumeMm3(regenerator, m.channel);

    // An invalid edit is refused before it touches the document.
    SweepDefinition invalid = m.definitionOf<SweepFeature>(m.channel);
    invalid.path.edges.clear();
    CHECK(errorCode(history.execute(m.doc, std::make_unique<ModifySweepCommand>(featureIdOf(m.channel), invalid))) ==
          ErrorCode::InvalidArgument);
    CHECK(equivalent(m.doc, before));
    CHECK(m.doc.revision() == revision);
    CHECK_FALSE(history.canUndo());

    // A valid edit the geometry cannot take fails at regeneration only: the
    // channel's profile moved off the path's start.
    SweepDefinition impossible = m.definitionOf<SweepFeature>(m.channel);
    impossible.profile = sketchIdOf(m.base);
    REQUIRE(history.execute(m.doc, std::make_unique<ModifySweepCommand>(featureIdOf(m.channel), impossible))
                .has_value());
    const Document edited = m.doc.clone();
    const Error error = requireFailure(regenerator, m.doc, m.channel);
    CHECK(error.code == ErrorCode::InvalidArgument);
    CHECK_THAT(error.message, StartsWith("Channel: makeSweep: the path must "));
    CHECK(equivalent(m.doc, edited)); // regeneration does not change the model
    CHECK(regenerator.state(m.pad) == NodeState::UpToDate);
    CHECK(volumeMm3(regenerator, m.pad) == padVolume);

    REQUIRE(history.undo(m.doc).has_value());
    CHECK(equivalent(m.doc, before));
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(bits(volumeMm3(regenerator, m.channel)) == bits(channelVolume));
}

TEST_CASE("SweepFeature_UndoRedoRestoresGeometry", "[sweep][features][undo][acceptance]") {
    // The straight model without its sweep; the sweep is created by command.
    StraightSweepModel m;
    REQUIRE(m.doc.removeObject(m.sweep).has_value());
    CommandHistory history;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const Document initial = m.doc.clone();

    struct State {
        SweepDefinition definition;
        Document document;
        Length length;
        Length width;
        double volume = 0.0;
        Point3D centre;
        BoundingBox3D box;
    };
    ObjectId sweep;
    const auto parameter = [&](ParameterId id) { return m.doc.parameters().find(id)->as<Length>().value(); };
    const auto record = [&] {
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        const geometry::Body& body = requireBody(regenerator, sweep);
        return State{m.definitionOf(sweep),
                     m.doc.clone(),
                     parameter(m.length),
                     parameter(m.width),
                     volumeMm3(regenerator, sweep),
                     body.massProperties()->centerOfMass,
                     body.boundingBox().value()};
    };
    const auto create = [&] {
        auto command = std::make_unique<CreateSweepCommand>("Sweep", m.definition());
        CreateSweepCommand* raw = command.get();
        CHECK(command->description() == "Create sweep 'Sweep'");
        REQUIRE(history.execute(m.doc, std::move(command)).has_value());
        sweep = ObjectId{raw->featureId()};
    };

    SECTION("commands on the sweep re-solve nothing: undo and redo restore everything bit for bit") {
        // 1. Create; 2. intersect it with a new 50 mm box (the definition's
        //    operation and target).
        create();
        const State created = record();
        CHECK_THAT(created.volume, WithinRel(20000.0, kRel));
        auto box = ExtrudeFeature::create("Box", {.profile = sketchIdOf(m.profile), .depth = 50_mm});
        REQUIRE(box.has_value());
        auto addBox = std::make_unique<AddObjectCommand>(std::move(*box));
        AddObjectCommand* addBoxRaw = addBox.get();
        REQUIRE(history.execute(m.doc, std::move(addBox)).has_value());
        const State boxed = record();
        SweepDefinition clipped = m.definitionOf(sweep);
        clipped.operation = FeatureOperation::Intersect;
        clipped.target = featureIdOf(addBoxRaw->objectId());
        REQUIRE(history.execute(m.doc, std::make_unique<ModifySweepCommand>(featureIdOf(sweep), clipped)).has_value());
        const State intersected = record();
        CHECK_THAT(intersected.volume, WithinRel(10.0 * 20.0 * 50.0, kRel)); // the sweep within the box

        const auto same = [&](const State& expected) {
            CHECK(equivalent(m.doc, expected.document));
            CHECK(m.definitionOf(sweep) == expected.definition);
            REQUIRE(requireReport(regenerator, m.doc).succeeded());
            CHECK(bits(volumeMm3(regenerator, sweep)) == bits(expected.volume));
            CHECK(requireBody(regenerator, sweep).massProperties()->centerOfMass == expected.centre);
            CHECK(requireBody(regenerator, sweep).boundingBox().value() == expected.box);
        };
        REQUIRE(history.undo(m.doc).has_value());
        same(boxed);
        REQUIRE(history.undo(m.doc).has_value());
        same(created);
        REQUIRE(history.undo(m.doc).has_value());
        CHECK(m.doc.findObject(sweep) == nullptr);
        CHECK(equivalent(m.doc, initial));
        REQUIRE(history.redo(m.doc).has_value()); // the same ID and geometry
        same(created);
        REQUIRE(history.redo(m.doc).has_value());
        REQUIRE(history.redo(m.doc).has_value());
        same(intersected);
        // Commands check the feature kind.
        CHECK(errorCode(history.execute(m.doc, std::make_unique<ModifySweepCommand>(featureIdOf(m.profile),
                                                                                     m.definition()))) ==
              ErrorCode::NotFound);
    }
    SECTION("parameter edits: path length 100 -> 150 mm, profile width 10 -> 15 mm") {
        // Undoing a parameter restores its value exactly and regeneration
        // re-solves its sketch from where the edit left it: the same geometry
        // to rounding, not bit for bit (the solver starts elsewhere).
        create();
        std::vector<State> states;
        states.push_back(record());
        REQUIRE(history.execute(m.doc, ModifyParameterCommand::setValue(m.length, 150_mm)).has_value());
        states.push_back(record());
        CHECK_THAT(states.back().volume, WithinRel(30000.0, kRel));
        REQUIRE(history.execute(m.doc, ModifyParameterCommand::setValue(m.width, 15_mm)).has_value());
        states.push_back(record());
        CHECK_THAT(states.back().volume, WithinRel(45000.0, kRel));
        const auto matches = [&](const State& expected) {
            CHECK(m.definitionOf(sweep) == expected.definition);
            CHECK(parameter(m.length) == expected.length);
            CHECK(parameter(m.width) == expected.width);
            REQUIRE(requireReport(regenerator, m.doc).succeeded());
            CHECK_THAT(volumeMm3(regenerator, sweep), WithinRel(expected.volume, kRel));
            const geometry::Body& body = requireBody(regenerator, sweep);
            const auto mmOf = [](const Point3D& p) {
                return V{p.x.in(units::mm), p.y.in(units::mm), p.z.in(units::mm)};
            };
            checkPoint(body.massProperties()->centerOfMass, mmOf(expected.centre));
            checkBox(body, mmOf(expected.box.min), mmOf(expected.box.max));
        };
        REQUIRE(history.undo(m.doc).has_value()); // width back to 10
        matches(states[1]);
        REQUIRE(history.undo(m.doc).has_value()); // length back to 100
        matches(states[0]);
        CHECK(parameter(m.length) == 100_mm);
        REQUIRE(history.redo(m.doc).has_value()); // length 150 again
        matches(states[1]);
        CHECK(parameter(m.length) == 150_mm);
        REQUIRE(history.redo(m.doc).has_value());
        matches(states[2]);
        checkBox(requireBody(regenerator, sweep), {0, 0, 0}, {15, 20, 150});
    }
}

TEST_CASE("SweepFeature_RegenerationIsDeterministic", "[sweep][features]") {
    for (const bool torus : {false, true}) {
        CAPTURE(torus);
        Document doc = torus ? TorusSweepModel{}.doc.clone() : FixedPathSweepModel{FixedPathSweepModel::Route::LineArcLine}.doc.clone();
        const ObjectId sweep = torus ? ObjectId::fromValue(5) : ObjectId::fromValue(3);
        Regenerator first;
        Regenerator second;
        const RegenerationReport a = requireReport(first, doc);
        const RegenerationReport b = requireReport(second, doc);
        REQUIRE(a.succeeded());
        CHECK(a.regenerated == b.regenerated);
        const auto pa = propertiesOf(first, sweep);
        const auto pb = propertiesOf(second, sweep);
        CHECK(bits(pa.volume.si()) == bits(pb.volume.si()));
        CHECK(bits(pa.surfaceArea.si()) == bits(pb.surfaceArea.si()));
        CHECK(pa.centerOfMass == pb.centerOfMass);
        CHECK(requireBody(first, sweep).boundingBox().value() == requireBody(second, sweep).boundingBox().value());
        CHECK(requireBody(first, sweep).topology() == requireBody(second, sweep).topology());
    }
}
