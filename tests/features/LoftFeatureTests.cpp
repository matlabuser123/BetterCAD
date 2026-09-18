#include "FeatureTestSupport.hpp"
#include "TestHelpers.hpp"
#include "core/geometry/GeometryTestSupport.hpp"
#include "support/BlockModel.hpp"
#include "support/BracketModel.hpp"
#include "support/LoftModels.hpp"

#include <bettercad/core/document/Commands.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/geometry/Edges.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/FeatureCommands.hpp>
#include <bettercad/features/LoftFeature.hpp>
#include <bettercad/features/Regeneration.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/features/ResultBodies.hpp>
#include <bettercad/features/RevolveFeature.hpp>
#include <bettercad/features/Validation.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace bettercad;
using namespace bettercad::features;
using namespace bettercad::literals;
using bettercad::test::describe;
using bettercad::test::errorCode;
using bettercad::test::featureIdOf;
using bettercad::test::fixedPolygon;
using bettercad::test::FrustumLoftModel;
using bettercad::test::frustumCentroid;
using bettercad::test::frustumVolume;
using bettercad::test::kPositionToleranceMm;
using bettercad::test::levelPlane;
using bettercad::test::LoftBossModel;
using bettercad::test::OffsetLoftModel;
using bettercad::test::RectangleLoftModel;
using bettercad::test::RectangularFrustumModel;
using bettercad::test::requireReport;
using bettercad::test::sketchIdOf;
using bettercad::test::TaperedHoleModel;
using bettercad::test::ThreeSectionLoftModel;
using bettercad::test::volumeMm3;
using Catch::Matchers::StartsWith;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

constexpr double pi = std::numbers::pi;
// Lofts between lines and coaxial circles give planes, bilinear patches,
// cylinders and cones, which the kernel integrates to rounding level.
constexpr double kRel = bettercad::test::kRelTight;
// Circles or arcs that are not coaxial, or are turned against each other,
// give B-spline ruled faces, within 6.3e-10 of the analytic volume
// (measured, docs/verification/P11-FEAT-009).
constexpr double kRelSpline = bettercad::test::kRelApproximatedIntersection;
// The kernel's box of a loft may exceed the exact box by its confusion
// tolerance, 1e-7 mm.
constexpr double kBoundsPaddingMm = 1e-7;

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

/// The kernel's box must contain the exact box and exceed it by at most kBoundsPaddingMm.
void checkBox(const geometry::Body& body, const V& min, const V& max) {
    const BoundingBox3D box = body.boundingBox().value();
    const V lo{box.min.x.in(units::mm), box.min.y.in(units::mm), box.min.z.in(units::mm)};
    const V hi{box.max.x.in(units::mm), box.max.y.in(units::mm), box.max.z.in(units::mm)};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        CAPTURE(axis, lo[axis], hi[axis]);
        CHECK(lo[axis] <= min[axis] + kPositionToleranceMm);
        CHECK(lo[axis] >= min[axis] - kBoundsPaddingMm - kPositionToleranceMm);
        CHECK(hi[axis] >= max[axis] - kPositionToleranceMm);
        CHECK(hi[axis] <= max[axis] + kBoundsPaddingMm + kPositionToleranceMm);
    }
}

/// Total area of the body's faces on the plane through (x, y, z) mm facing (nx, ny, nz).
double areaOn(const geometry::Body& body, V point, V normal) {
    const auto found = geometry::findFaces(
        body, geometry::planeSignature(Point3D{point[0] * units::mm, point[1] * units::mm, point[2] * units::mm},
                                       *Direction3D::fromComponents(normal[0], normal[1], normal[2])));
    REQUIRE(found.has_value());
    double area = 0.0;
    for (const geometry::FaceInfo& info : *found) {
        area += info.area.in(units::mm2);
    }
    return area;
}

/// Edges on the circle of radius @p r mm about @p centre mm with axis Z.
std::size_t circlesAt(const geometry::Body& body, V centre, double r) {
    const auto circle = geometry::circleSignature(
        Point3D{centre[0] * units::mm, centre[1] * units::mm, centre[2] * units::mm}, Direction3D::unitZ(),
        r * units::mm);
    REQUIRE(circle.has_value());
    return geometry::findEdges(body, *circle).value().size();
}

Error refusal(const LoftDefinition& definition) {
    auto feature = LoftFeature::create("L", definition);
    REQUIRE_FALSE(feature.has_value());
    return feature.error();
}

/// A sketch on @p plane with a circle of radius r mm about the fixed point (u, v) mm.
std::unique_ptr<sketch::Sketch> fixedCircle(const std::string& name, const Frame3D& plane, double u, double v,
                                            double r) {
    auto sketch = std::make_unique<sketch::Sketch>(name, plane);
    const EntityId circle =
        bettercad::test::require(sketch->addCircle(Point2D{u * units::mm, v * units::mm}, r * units::mm));
    bettercad::test::require(
        sketch->addFixed(std::get<sketch::CircleEntity>(sketch->findEntity(circle)->geometry).center));
    bettercad::test::require(sketch->addRadius(circle, r * units::mm));
    return sketch;
}

/// A half disc of radius 5 mm about the origin on @p plane: the line from
/// the angle a - 90 deg to a + 90 deg, closed by the arc round through a + 180 deg.
std::unique_ptr<sketch::Sketch> halfDisc(const std::string& name, const Frame3D& plane, double a) {
    auto sketch = std::make_unique<sketch::Sketch>(name, plane);
    const auto at = [&](double t) { return Point2D{5.0 * std::cos(t) * units::mm, 5.0 * std::sin(t) * units::mm}; };
    const EntityId c = bettercad::test::require(sketch->addPoint(Point2D{}));
    const EntityId p = bettercad::test::require(sketch->addPoint(at(a - pi / 2.0)));
    const EntityId q = bettercad::test::require(sketch->addPoint(at(a + pi / 2.0)));
    for (const EntityId point : {c, p, q}) {
        bettercad::test::require(sketch->addFixed(point));
    }
    bettercad::test::require(sketch->addLine(p, q));
    bettercad::test::require(sketch->addArc(c, q, p));
    return sketch;
}

/// Points @p loft's section @p index at a new sketch (with no offset).
ObjectId useSection(Document& doc, ObjectId loft, std::size_t index, std::unique_ptr<sketch::Sketch> sketch) {
    const ObjectId id = doc.addObject(std::move(sketch)).value();
    LoftDefinition d = doc.findObjectAs<LoftFeature>(loft)->definition();
    d.sections[index] = {.sketch = sketchIdOf(id)};
    REQUIRE(doc.modifyObject<LoftFeature>(loft, [&](LoftFeature& f) { return f.setDefinition(d); }).has_value());
    return id;
}

} // namespace

TEST_CASE("LoftFeature_DefinitionIsValidatedOnCreateAndEdit", "[loft][features]") {
    const LoftDefinition good{.sections = {{.sketch = SketchId::fromValue(4)},
                                           {.sketch = SketchId::fromValue(5), .offset = 30_mm}}};
    REQUIRE(LoftFeature::create("L", good).has_value());
    LoftDefinition d = good;
    SECTION("at least two sections") {
        d.sections.clear();
        CHECK(refusal(d).message == "a loft needs at least two sections, got 0");
        d.sections = {{.sketch = SketchId::fromValue(4)}};
        CHECK(refusal(d).message == "a loft needs at least two sections, got 1");
        CHECK(refusal(d).code == ErrorCode::InvalidArgument);
    }
    SECTION("each section's sketch and offset") {
        d.sections[1].sketch = SketchId{};
        CHECK(refusal(d).message == "section 2 needs a sketch");
        d = good;
        d.sections[1].offset = Length::fromSi(std::numeric_limits<double>::infinity());
        CHECK(refusal(d).message == "section 2's offset must be finite, got inf mm");
        d = good;
        d.sections[0].offsetParameter = ParameterId{};
        CHECK(refusal(d).message == "section 1's offset parameter ID must be valid");
    }
    SECTION("a repeated section: the same sketch at the same offset") {
        d.sections.push_back({.sketch = SketchId::fromValue(4)});
        CHECK(refusal(d).message == "section 3 repeats section 1: the same sketch at the same offset");
        // The same sketch at another offset is another section.
        d.sections.back().offset = 60_mm;
        CHECK(LoftFeature::create("L", d).has_value());
    }
    SECTION("operation and target pairing") {
        d.operation = FeatureOperation::Cut;
        CHECK(errorCode(LoftFeature::create("L", d)) == ErrorCode::InvalidArgument);
        d.target = FeatureId::fromValue(2);
        CHECK(LoftFeature::create("L", d).has_value());
        d.operation = FeatureOperation::NewBody;
        CHECK(errorCode(LoftFeature::create("L", d)) == ErrorCode::InvalidArgument);
    }
    SECTION("an invalid edit leaves the feature unchanged") {
        auto feature = LoftFeature::create("L", good);
        REQUIRE(feature.has_value());
        d.sections.pop_back();
        CHECK(errorCode((*feature)->setDefinition(d)) == ErrorCode::InvalidArgument);
        CHECK((*feature)->definition() == good);
        const auto unchanged = (*feature)->setDefinition(good);
        REQUIRE(unchanged.has_value());
        CHECK_FALSE(*unchanged);
    }
    CHECK(toString(LoftInterpolation::Ruled) == "ruled");
}

TEST_CASE("LoftFeature_DependsOnEverySectionAndTarget", "[loft][features]") {
    ThreeSectionLoftModel m;
    const auto* loft = m.doc.findObjectAs<LoftFeature>(m.loft);
    REQUIRE(loft != nullptr);
    // Every section's sketch in order, then the offset parameters.
    CHECK(loft->dependencies() ==
          std::vector<ObjectId>{m.bottom, m.middle, m.top, ObjectId{m.mid}, ObjectId{m.length}});
    CHECK(loft->typeName() == "loft");
    CHECK_FALSE(loft->target().has_value());
    CHECK(equivalent(*loft->clone(), *loft));
    TaperedHoleModel taper;
    const auto* cut = taper.doc.findObjectAs<LoftFeature>(taper.taper);
    CHECK(cut->dependencies() == std::vector<ObjectId>{taper.mouth, taper.tip, ObjectId{taper.extra}, taper.pad});
    CHECK(cut->target() == featureIdOf(taper.pad));

    // Each input dirties the loft and nothing unrelated.
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).regenerated ==
            std::vector<ObjectId>{m.bottom, m.middle, m.top, m.loft});
    REQUIRE(m.doc.setParameterValue(m.rMid, 12_mm).has_value());
    CHECK(requireReport(regenerator, m.doc).regenerated == std::vector<ObjectId>{m.middle, m.loft});
    REQUIRE(m.doc.setParameterValue(m.rEnd, 6_mm).has_value());
    CHECK(requireReport(regenerator, m.doc).regenerated == std::vector<ObjectId>{m.bottom, m.top, m.loft});
    REQUIRE(m.doc.setParameterValue(m.mid, 40_mm).has_value());
    CHECK(requireReport(regenerator, m.doc).regenerated == std::vector<ObjectId>{m.loft});
    CHECK(requireReport(regenerator, m.doc).regenerated.empty()); // nothing changed
    Regenerator taperRegenerator;
    REQUIRE(requireReport(taperRegenerator, taper.doc).succeeded());
    REQUIRE(taper.doc.setParameterValue(taper.height, 30_mm).has_value());
    CHECK(requireReport(taperRegenerator, taper.doc).regenerated == std::vector<ObjectId>{taper.pad, taper.taper});
}

TEST_CASE("LoftFeature_EqualRectanglesMatchPrismVolume", "[loft][features][acceptance]") {
    // The primary acceptance case: 10 x 20 mm at z = 0 and z = 100.
    // V = A h = 200 x 100 = 20000 mm^3.
    RectangleLoftModel m;
    Regenerator regenerator;
    const RegenerationReport report = requireReport(regenerator, m.doc);
    INFO(describe(report));
    REQUIRE(report.succeeded());
    const geometry::Body& body = requireBody(regenerator, m.loft);
    CHECK(body.isValid());
    CHECK(body.topology().solids == 1);
    const double expected = 10.0 * 20.0 * 100.0;
    const auto props = propertiesOf(regenerator, m.loft);
    const double actual = props.volume.in(units::mm3);
    INFO("expected " << expected << " mm^3, actual " << actual << " mm^3, absolute error "
                     << std::abs(actual - expected) << " mm^3, relative " << std::abs(actual - expected) / expected);
    CHECK_THAT(actual, WithinRel(expected, kRel));
    CHECK_THAT(props.surfaceArea.in(units::mm2), WithinRel(2.0 * (200.0 + 1000.0 + 2000.0), kRel));
    checkPoint(props.centerOfMass, {5, 10, 50});
    checkBox(body, {0, 0, 0}, {10, 20, 100});
    CHECK(resultFeatures(m.doc) == std::vector<ObjectId>{m.loft});
}

TEST_CASE("LoftFeature_EqualCirclesMatchCylinderVolume", "[loft][features][acceptance]") {
    // r1 = r2 = 5 mm, h = 100 mm: V = pi r^2 h = 2500 pi; and the extrude of
    // the bottom circle by the same height.
    FrustumLoftModel m;
    REQUIRE(m.doc.setParameterValue(m.r1, 5_mm).has_value());
    REQUIRE(m.doc.setParameterValue(m.height, 100_mm).has_value());
    auto extrude = ExtrudeFeature::create("Extrude", {.profile = sketchIdOf(m.bottom), .depthParameter = m.height});
    REQUIRE(extrude.has_value());
    const ObjectId pad = m.doc.addObject(std::move(*extrude)).value();
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const geometry::Body& body = requireBody(regenerator, m.loft);
    CHECK(body.isValid());
    CHECK(body.topology().solids == 1);
    const auto props = propertiesOf(regenerator, m.loft);
    const auto extruded = propertiesOf(regenerator, pad);
    INFO("loft " << props.volume.in(units::mm3) << " mm^3, extrude " << extruded.volume.in(units::mm3) << " mm^3");
    CHECK_THAT(props.volume.in(units::mm3), WithinRel(2500.0 * pi, kRel));
    CHECK_THAT(props.volume.in(units::mm3), WithinRel(extruded.volume.in(units::mm3), kRel));
    CHECK_THAT(props.surfaceArea.in(units::mm2), WithinRel(2.0 * pi * 5.0 * 100.0 + 2.0 * pi * 25.0, kRel));
    checkPoint(props.centerOfMass, {0, 0, 50});
    checkPoint(extruded.centerOfMass, {0, 0, 50});
    checkBox(body, {-5, -5, 0}, {5, 5, 100});
}

TEST_CASE("LoftFeature_CircularFrustumMatchesAnalyticVolume", "[loft][features][acceptance]") {
    // r1 = 10, r2 = 5, h = 30: V = pi h/3 (r1^2 + r1 r2 + r2^2) = 1750 pi.
    FrustumLoftModel m;
    Regenerator regenerator;
    const RegenerationReport report = requireReport(regenerator, m.doc);
    INFO(describe(report));
    REQUIRE(report.succeeded());
    const geometry::Body& body = requireBody(regenerator, m.loft);
    CHECK(body.isValid());
    CHECK(body.topology().solids == 1);
    const double expected = pi * 30.0 / 3.0 * (100.0 + 50.0 + 25.0);
    CHECK_THAT(expected, WithinRel(1750.0 * pi, 1e-15));
    const auto props = propertiesOf(regenerator, m.loft);
    const double actual = props.volume.in(units::mm3);
    INFO("r1 10 mm, r2 5 mm, h 30 mm: expected " << expected << " mm^3, actual " << actual
                                                   << " mm^3, absolute error " << std::abs(actual - expected)
                                                   << " mm^3, relative " << std::abs(actual - expected) / expected);
    CHECK_THAT(actual, WithinRel(expected, kRel));
    // The side is a cone, pi (r1 + r2) s with s = sqrt(h^2 + (r1 - r2)^2); the ends are discs.
    const double slant = std::sqrt(900.0 + 25.0);
    CHECK_THAT(props.surfaceArea.in(units::mm2), WithinRel(pi * 15.0 * slant + pi * 125.0, kRel));
    checkPoint(props.centerOfMass, {0, 0, 30.0 * frustumCentroid(10, 5)});
    checkBox(body, {-10, -10, 0}, {10, 10, 30});
    CHECK_THAT(areaOn(body, {0, 0, 0}, {0, 0, -1}), WithinRel(100.0 * pi, kRel)); // the bottom disc
    CHECK_THAT(areaOn(body, {0, 0, 30}, {0, 0, 1}), WithinRel(25.0 * pi, kRel));  // the top disc
}

TEST_CASE("LoftFeature_RectangularFrustumMatchesAnalyticVolume", "[loft][features][acceptance]") {
    // 20 x 10 to 10 x 5 over 30 mm, similar and centred: a pyramidal frustum,
    // V = h/3 (A1 + A2 + sqrt(A1 A2)) = 10 (200 + 50 + 100) = 3500.
    RectangularFrustumModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const geometry::Body& body = requireBody(regenerator, m.loft);
    CHECK(body.isValid());
    CHECK(body.topology().solids == 1);
    const double expected = 30.0 / 3.0 * (200.0 + 50.0 + std::sqrt(200.0 * 50.0));
    CHECK_THAT(volumeMm3(regenerator, m.loft), WithinRel(expected, kRel));
    // The sides are trapezoids: the +-Y sides lean in by 2.5 mm over 30 with
    // parallel sides 20 and 10, the +-X sides by 5 mm with 10 and 5. (The
    // kernel represents them as B-spline surfaces, so they are measured by
    // the total area rather than found as planes.)
    const double sides = 2.0 * 15.0 * std::sqrt(900.0 + 6.25) + 2.0 * 7.5 * std::sqrt(900.0 + 25.0);
    CHECK_THAT(propertiesOf(regenerator, m.loft).surfaceArea.in(units::mm2), WithinRel(200.0 + 50.0 + sides, kRel));
    CHECK_THAT(areaOn(body, {0, 0, 0}, {0, 0, -1}), WithinRel(200.0, kRel)); // the ends are planes
    CHECK_THAT(areaOn(body, {0, 0, 30}, {0, 0, 1}), WithinRel(50.0, kRel));
    checkPoint(propertiesOf(regenerator, m.loft).centerOfMass, {0, 0, 30.0 * frustumCentroid(1, 0.5)});
    checkBox(body, {-10, -5, 0}, {10, 5, 30});
    // A hexagon to a smaller hexagon (similar): the same law.
    Document doc{"Hex"};
    const auto hexagon = [](double r) {
        std::vector<std::pair<double, double>> corners;
        for (int i = 0; i < 6; ++i) {
            corners.emplace_back(r * std::cos(pi * i / 3.0), r * std::sin(pi * i / 3.0));
        }
        return corners;
    };
    const ObjectId big = doc.addObject(fixedPolygon("Big", Frame3D::xy(), hexagon(10))).value();
    const ObjectId small = doc.addObject(fixedPolygon("Small", Frame3D::xy(), hexagon(5))).value();
    auto hex = LoftFeature::create("Hex", {.sections = {{.sketch = sketchIdOf(big)},
                                                        {.sketch = sketchIdOf(small), .offset = 30_mm}}});
    REQUIRE(hex.has_value());
    const ObjectId hexLoft = doc.addObject(std::move(*hex)).value();
    Regenerator hexRegenerator;
    REQUIRE(requireReport(hexRegenerator, doc).succeeded());
    const double a1 = 1.5 * std::sqrt(3.0) * 100.0;
    const double a2 = a1 / 4.0;
    CHECK_THAT(volumeMm3(hexRegenerator, hexLoft), WithinRel(10.0 * (a1 + a2 + std::sqrt(a1 * a2)), kRel));
}

TEST_CASE("LoftFeature_ThreeCircularSectionsMatchPiecewiseFrustums", "[loft][features][acceptance]") {
    SECTION("r 5, 10, 5 at z 0, 50, 100: two frustums, symmetric about z = 50") {
        ThreeSectionLoftModel m;
        Regenerator regenerator;
        const RegenerationReport report = requireReport(regenerator, m.doc);
        INFO(describe(report));
        REQUIRE(report.succeeded());
        const geometry::Body& body = requireBody(regenerator, m.loft);
        CHECK(body.isValid());
        CHECK(body.topology().solids == 1);
        const double expected = frustumVolume(5, 10, 50) + frustumVolume(10, 5, 50);
        CHECK_THAT(expected, WithinRel(17500.0 * pi / 3.0, 1e-15));
        CHECK_THAT(volumeMm3(regenerator, m.loft), WithinRel(expected, kRel));
        checkPoint(propertiesOf(regenerator, m.loft).centerOfMass, {0, 0, 50});
        checkBox(body, {-10, -10, 0}, {10, 10, 100});
        // The widest section is at z = 50, where the definition put it.
        CHECK(circlesAt(body, {0, 0, 50}, 10) == 1);
        CHECK(circlesAt(body, {0, 0, 0}, 5) == 1);
        CHECK(circlesAt(body, {0, 0, 100}, 5) == 1);
    }
    SECTION("r 10, 6, 8, 4 at z 0, 20, 35, 60 (literal offsets): three frustums") {
        Document doc{"Stack"};
        const std::array<std::pair<double, double>, 4> rings{{{10, 0}, {6, 20}, {8, 35}, {4, 60}}};
        LoftDefinition d;
        for (std::size_t i = 0; i < rings.size(); ++i) {
            const ObjectId id =
                doc.addObject(fixedCircle(std::format("Ring{}", i + 1), Frame3D::xy(), 0, 0, rings[i].first)).value();
            d.sections.push_back({.sketch = sketchIdOf(id), .offset = rings[i].second * units::mm});
        }
        auto feature = LoftFeature::create("Stack", d);
        REQUIRE(feature.has_value());
        const ObjectId loft = doc.addObject(std::move(*feature)).value();
        Regenerator regenerator;
        REQUIRE(requireReport(regenerator, doc).succeeded());
        const double expected = frustumVolume(10, 6, 20) + frustumVolume(6, 8, 15) + frustumVolume(8, 4, 25);
        CHECK_THAT(expected, WithinRel(2980.0 * pi, 1e-15));
        CHECK_THAT(volumeMm3(regenerator, loft), WithinRel(expected, kRel));
        checkBox(requireBody(regenerator, loft), {-10, -10, 0}, {10, 10, 60});
    }
}

TEST_CASE("LoftFeature_EqualSectionsMatchExtrude", "[loft][features][acceptance]") {
    // The equal-rectangle loft against the extrude of its bottom sketch by
    // the same length: the same solid, and both follow the parameters.
    RectangleLoftModel m;
    auto extrude = ExtrudeFeature::create("Extrude", {.profile = sketchIdOf(m.bottom), .depthParameter = m.length});
    REQUIRE(extrude.has_value());
    const ObjectId pad = m.doc.addObject(std::move(*extrude)).value();
    Regenerator regenerator;
    const auto compare = [&](double expected) {
        const RegenerationReport report = requireReport(regenerator, m.doc);
        INFO(describe(report));
        REQUIRE(report.succeeded());
        const auto lofted = propertiesOf(regenerator, m.loft);
        const auto extruded = propertiesOf(regenerator, pad);
        INFO("loft " << lofted.volume.in(units::mm3) << " mm^3, extrude " << extruded.volume.in(units::mm3)
                     << " mm^3");
        CHECK_THAT(lofted.volume.in(units::mm3), WithinRel(expected, kRel));
        CHECK_THAT(lofted.volume.in(units::mm3), WithinRel(extruded.volume.in(units::mm3), kRel));
        CHECK_THAT(lofted.surfaceArea.in(units::mm2), WithinRel(extruded.surfaceArea.in(units::mm2), kRel));
        CHECK_THAT((lofted.centerOfMass.x - extruded.centerOfMass.x).in(units::mm), WithinAbs(0.0, kPositionToleranceMm));
        CHECK_THAT((lofted.centerOfMass.y - extruded.centerOfMass.y).in(units::mm), WithinAbs(0.0, kPositionToleranceMm));
        CHECK_THAT((lofted.centerOfMass.z - extruded.centerOfMass.z).in(units::mm), WithinAbs(0.0, kPositionToleranceMm));
        const BoundingBox3D b = requireBody(regenerator, pad).boundingBox().value();
        checkBox(requireBody(regenerator, m.loft), {b.min.x.in(units::mm), b.min.y.in(units::mm), b.min.z.in(units::mm)},
                 {b.max.x.in(units::mm), b.max.y.in(units::mm), b.max.z.in(units::mm)});
        CHECK(requireBody(regenerator, m.loft).topology().solids == requireBody(regenerator, pad).topology().solids);
    };
    compare(20000.0);
    REQUIRE(m.doc.setParameterValue(m.width, 15_mm).has_value());
    REQUIRE(m.doc.setParameterValue(m.length, 60_mm).has_value());
    compare(15.0 * 20.0 * 60.0);
}

TEST_CASE("LoftFeature_CircularFrustumMatchesRevolve", "[loft][features][revolve][acceptance]") {
    // The frustum as a loft of two circles and as a revolve of the trapezoid
    // (0, 0), (r1, 0), (r2, h), (0, h) about the Z axis, both driven by r1,
    // r2 and height: analytic, loft and revolve agree, and follow the parameters.
    FrustumLoftModel m;
    auto trapezoid = std::make_unique<sketch::Sketch>("Trapezoid", Frame3D::xz());
    const EntityId o = bettercad::test::require(trapezoid->addPoint(Point2D{}));
    const EntityId a = bettercad::test::require(trapezoid->addPoint(Point2D{10_mm, 0_mm}));
    const EntityId b = bettercad::test::require(trapezoid->addPoint(Point2D{5_mm, 30_mm}));
    const EntityId c = bettercad::test::require(trapezoid->addPoint(Point2D{0_mm, 30_mm}));
    bettercad::test::require(trapezoid->addFixed(o));
    bettercad::test::require(trapezoid->addHorizontal(o, a));
    bettercad::test::require(trapezoid->addVertical(o, c));
    bettercad::test::require(trapezoid->addHorizontal(c, b));
    const std::array<std::tuple<EntityId, EntityId, ParameterId>, 3> drives{
        {{o, a, m.r1}, {c, b, m.r2}, {o, c, m.height}}};
    for (const auto& [p, q, parameter] : drives) {
        const ConstraintId d = bettercad::test::require(trapezoid->addDistance(p, q, 1_mm));
        REQUIRE(trapezoid->setConstraintParameter(d, parameter).has_value());
    }
    for (const auto& [p, q] : {std::pair{o, a}, std::pair{a, b}, std::pair{b, c}, std::pair{c, o}}) {
        bettercad::test::require(trapezoid->addLine(p, q));
    }
    const ObjectId section = m.doc.addObject(std::move(trapezoid)).value();
    auto revolve = RevolveFeature::create("Revolve", {.profile = sketchIdOf(section), .axis = RevolveAxis::sketchY()});
    REQUIRE(revolve.has_value());
    const ObjectId turned = m.doc.addObject(std::move(*revolve)).value();
    Regenerator regenerator;
    for (const auto& [r2, h] : {std::pair{5.0, 30.0}, std::pair{8.0, 60.0}}) {
        CAPTURE(r2, h);
        REQUIRE(m.doc.setParameterValue(m.r2, r2 * units::mm).has_value());
        REQUIRE(m.doc.setParameterValue(m.height, h * units::mm).has_value());
        const RegenerationReport report = requireReport(regenerator, m.doc);
        INFO(describe(report));
        REQUIRE(report.succeeded());
        const auto lofted = propertiesOf(regenerator, m.loft);
        const auto revolved = propertiesOf(regenerator, turned);
        const double expected = frustumVolume(10, r2, h);
        INFO("loft " << lofted.volume.in(units::mm3) << ", revolve " << revolved.volume.in(units::mm3) << ", formula "
                     << expected << " mm^3");
        CHECK_THAT(lofted.volume.in(units::mm3), WithinRel(expected, kRel));
        CHECK_THAT(revolved.volume.in(units::mm3), WithinRel(expected, kRel));
        CHECK_THAT(lofted.volume.in(units::mm3), WithinRel(revolved.volume.in(units::mm3), kRel));
        CHECK_THAT(lofted.surfaceArea.in(units::mm2), WithinRel(revolved.surfaceArea.in(units::mm2), kRel));
        checkPoint(lofted.centerOfMass, {0, 0, h * frustumCentroid(10, r2)});
        checkPoint(revolved.centerOfMass, {0, 0, h * frustumCentroid(10, r2)});
        checkBox(requireBody(regenerator, m.loft), {-10, -10, 0}, {10, 10, h});
        checkBox(requireBody(regenerator, turned), {-10, -10, 0}, {10, 10, h});
    }
}

TEST_CASE("LoftFeature_OffsetSectionsProduceValidSolid", "[loft][features][acceptance]") {
    // The top circle's centre moves from over the origin to x = 10: the
    // frustum leans, keeps its volume (Cavalieri), and its centroid moves to
    // (10 t, 0, 30 t), t the right frustum's height fraction.
    OffsetLoftModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const double expected = frustumVolume(10, 5, 30);
    const double t = frustumCentroid(10, 5);
    CHECK_THAT(volumeMm3(regenerator, m.loft), WithinRel(expected, kRel)); // coaxial: a cone
    checkPoint(propertiesOf(regenerator, m.loft).centerOfMass, {0, 0, 30.0 * t});
    REQUIRE(m.doc.setParameterValue(m.shift, 20_mm).has_value());
    CHECK(requireReport(regenerator, m.doc).regenerated == std::vector<ObjectId>{m.top, m.loft});
    const geometry::Body& body = requireBody(regenerator, m.loft);
    CHECK(body.isValid());
    CHECK(body.topology().solids == 1);
    CHECK_THAT(volumeMm3(regenerator, m.loft), WithinRel(expected, kRelSpline));
    checkPoint(propertiesOf(regenerator, m.loft).centerOfMass, {10.0 * t, 0, 30.0 * t});
    checkBox(body, {-10, -10, 0}, {15, 10, 30});
    // No recentring: the top disc is where its sketch puts it.
    CHECK(circlesAt(body, {10, 0, 30}, 5) == 1);
    CHECK(circlesAt(body, {0, 0, 30}, 5) == 0);
    CHECK_THAT(areaOn(body, {10, 0, 30}, {0, 0, 1}), WithinRel(25.0 * pi, kRel));
}

TEST_CASE("LoftFeature_PreservesSectionOrder", "[loft][features][acceptance]") {
    ThreeSectionLoftModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const double forwards = volumeMm3(regenerator, m.loft);
    // The sections resolve in the definition's order.
    auto sections = resolveLoftSections(m.definitionOf(m.loft), m.doc);
    REQUIRE(sections.has_value());
    REQUIRE(sections->size() == 3);
    CHECK((*sections)[0].plane.origin().z.in(units::mm) == 0.0);
    CHECK((*sections)[1].plane.origin().z.in(units::mm) == 50.0);
    CHECK((*sections)[2].plane.origin().z.in(units::mm) == 100.0);
    SECTION("listed out of order: refused, never sorted") {
        LoftDefinition d = m.definitionOf(m.loft);
        std::swap(d.sections[1], d.sections[2]);
        m.setDefinition(m.loft, d);
        CHECK(m.definitionOf(m.loft) == d); // stored as given
        const Error error = requireFailure(regenerator, m.doc, m.loft);
        CHECK(error.code == ErrorCode::InvalidArgument);
        CHECK(error.message == "Loft: makeLoft: section 3 lies 50 mm behind section 2 along the loft: the sections must "
                               "be listed in the order they follow one another");
    }
    SECTION("listed top down: the loft runs down, through the same sections") {
        LoftDefinition d = m.definitionOf(m.loft);
        std::swap(d.sections[0], d.sections[2]);
        m.setDefinition(m.loft, d);
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CHECK_THAT(volumeMm3(regenerator, m.loft), WithinRel(forwards, kRel));
        checkPoint(propertiesOf(regenerator, m.loft).centerOfMass, {0, 0, 50});
    }
}

TEST_CASE("LoftFeature_NewBody", "[loft][features][acceptance]") {
    // A new body is the lofted tool alone: one valid solid, the result body.
    FrustumLoftModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(requireBody(regenerator, m.loft).topology().solids == 1);
    CHECK(requireBody(regenerator, m.loft).isValid());
    CHECK_THAT(volumeMm3(regenerator, m.loft), WithinRel(1750.0 * pi, kRel));
    CHECK(resultFeatures(m.doc) == std::vector<ObjectId>{m.loft});
}

TEST_CASE("LoftFeature_Add", "[loft][features][acceptance]") {
    // A tapered boss on the block's top face: 40 x 20 at z = 20 to 20 x 10
    // at z = 50. V = 100000 + h/3 (800 + 200 + sqrt(800 x 200)) = 114000.
    LoftBossModel m;
    Regenerator regenerator;
    const RegenerationReport report = requireReport(regenerator, m.doc);
    INFO(describe(report));
    REQUIRE(report.succeeded());
    const geometry::Body& body = requireBody(regenerator, m.boss);
    CHECK(body.isValid());
    CHECK(body.topology().solids == 1);
    CHECK_THAT(volumeMm3(regenerator, m.boss), WithinRel(100000.0 + 10.0 * 1400.0, kRel));
    checkBox(body, {0, 0, 0}, {100, 50, 50});
    // The boss's top, and the block's top less the boss's foot.
    CHECK_THAT(areaOn(body, {0, 0, 50}, {0, 0, 1}), WithinRel(200.0, kRel));
    CHECK_THAT(areaOn(body, {0, 0, 20}, {0, 0, 1}), WithinRel(5000.0 - 800.0, kRel));
    CHECK(resultFeatures(m.doc) == std::vector<ObjectId>{m.boss});
}

TEST_CASE("LoftFeature_Remove", "[loft][features][acceptance]") {
    // A tapered blind hole: r 8 on the top face to r 4 at z = 5, wholly in
    // the block. V = 100000 - pi 15/3 (64 + 32 + 16) = 100000 - 560 pi.
    TaperedHoleModel m;
    Regenerator regenerator;
    const RegenerationReport report = requireReport(regenerator, m.doc);
    INFO(describe(report));
    REQUIRE(report.succeeded());
    const geometry::Body& body = requireBody(regenerator, m.taper);
    CHECK(body.isValid());
    CHECK(body.topology().solids == 1);
    CHECK_THAT(volumeMm3(regenerator, m.taper), WithinRel(TaperedHoleModel::expectedVolume(20, 15), kRel));
    CHECK_THAT(volumeMm3(regenerator, m.taper), WithinRel(100000.0 - 560.0 * pi, kRel));
    // The mouth on the top face, and the flat bottom 15 mm down.
    CHECK(circlesAt(body, {50, 25, 20}, 8) == 1);
    CHECK_THAT(areaOn(body, {0, 0, 5}, {0, 0, 1}), WithinRel(16.0 * pi, kRel));
    CHECK_THAT(areaOn(body, {0, 0, 20}, {0, 0, 1}), WithinRel(5000.0 - 64.0 * pi, kRel));
    checkBox(body, {0, 0, 0}, {100, 50, 20});
    CHECK(resultFeatures(m.doc) == std::vector<ObjectId>{m.taper});
}

TEST_CASE("LoftFeature_Intersect", "[loft][features][acceptance]") {
    // The tapered hole's tool intersected with the block: the frustum itself, 560 pi.
    TaperedHoleModel m{FeatureOperation::Intersect};
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const geometry::Body& body = requireBody(regenerator, m.taper);
    CHECK(body.isValid());
    CHECK(body.topology().solids == 1);
    CHECK_THAT(volumeMm3(regenerator, m.taper), WithinRel(560.0 * pi, kRel));
    checkBox(body, {42, 17, 5}, {58, 33, 20});
}

TEST_CASE("LoftFeature_OperationsWithoutOverlapFollowTheSharedPolicy", "[loft][features]") {
    // No loft-specific boolean rules: combineWithTarget() as for extrudes and
    // revolves. A join that misses keeps the tool as another solid; an
    // intersection that misses leaves an empty body, which validation reports
    // and which no later feature can use.
    SECTION("an intersection that misses: an empty body, reported") {
        TaperedHoleModel m{FeatureOperation::Intersect};
        LoftDefinition d = m.definitionOf<LoftFeature>(m.taper);
        d.sections[0] = {.sketch = sketchIdOf(m.mouth), .offset = 10_mm};   // z = 30
        d.sections[1] = {.sketch = sketchIdOf(m.tip), .offset = -(30_mm)};  // faces down: z = 50
        m.setDefinition<LoftFeature>(m.taper, d);
        auto join = ExtrudeFeature::create("Rejoin", {.profile = sketchIdOf(m.base), .depth = 5_mm,
                                                      .operation = FeatureOperation::Join,
                                                      .target = featureIdOf(m.taper)});
        REQUIRE(join.has_value());
        const ObjectId rejoin = m.doc.addObject(std::move(*join)).value();
        Regenerator regenerator;
        const RegenerationReport report = requireReport(regenerator, m.doc);
        INFO(describe(report));
        REQUIRE(regenerator.body(m.taper) != nullptr);
        CHECK(regenerator.body(m.taper)->isEmpty());
        REQUIRE(report.failed == std::vector<ObjectId>{rejoin});
        CHECK(report.errors.at(rejoin).code == ErrorCode::FailedPrecondition);
        CHECK(report.errors.at(rejoin).message == "Rejoin: a join feature needs the body of its target feature");
        REQUIRE(m.doc.removeObject(rejoin).has_value());
        const ValidationReport validation = validateDocument(m.doc);
        CHECK_FALSE(validation.valid());
        bool reported = false;
        for (const ValidationIssue& issue : validation.issues) {
            reported = reported || (issue.check == ValidationCheck::Geometry &&
                                    issue.message == "Taper (object:9) produced an empty body");
        }
        CHECK(reported);
    }
    SECTION("a join that misses: the tool stays a separate solid") {
        LoftBossModel m;
        LoftDefinition d = m.definitionOf<LoftFeature>(m.boss);
        d.sections[0] = {.sketch = sketchIdOf(m.bossBottom), .offset = 10_mm};                         // z = 30
        d.sections[1] = {.sketch = sketchIdOf(m.bossTop), .offset = 40_mm};                            // z = 60
        m.setDefinition<LoftFeature>(m.boss, d);
        Regenerator regenerator;
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        const geometry::Body& body = requireBody(regenerator, m.boss);
        CHECK(body.isValid());
        CHECK(body.topology().solids == 2);
        CHECK_THAT(volumeMm3(regenerator, m.boss), WithinRel(100000.0 + 14000.0, kRel));
    }
}

TEST_CASE("LoftFeature_RegeneratesWhenSectionChanges", "[loft][features][acceptance]") {
    FrustumLoftModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    SECTION("the top radius 5 -> 8 mm") {
        REQUIRE(m.doc.setParameterValue(m.r2, 8_mm).has_value());
        CHECK(requireReport(regenerator, m.doc).regenerated == std::vector<ObjectId>{m.top, m.loft});
        CHECK_THAT(volumeMm3(regenerator, m.loft), WithinRel(frustumVolume(10, 8, 30), kRel));
        const geometry::Body& body = requireBody(regenerator, m.loft);
        CHECK(circlesAt(body, {0, 0, 30}, 8) == 1);
        CHECK(circlesAt(body, {0, 0, 30}, 5) == 0); // no stale section
    }
    SECTION("the bottom radius 10 -> 12 mm") {
        REQUIRE(m.doc.setParameterValue(m.r1, 12_mm).has_value());
        CHECK(requireReport(regenerator, m.doc).regenerated == std::vector<ObjectId>{m.bottom, m.loft});
        CHECK_THAT(volumeMm3(regenerator, m.loft), WithinRel(frustumVolume(12, 5, 30), kRel));
        checkBox(requireBody(regenerator, m.loft), {-12, -12, 0}, {12, 12, 30});
    }
}

TEST_CASE("LoftFeature_RegeneratesWhenSpacingChanges", "[loft][features][acceptance]") {
    // height 30 -> 60 mm: the end sections stay, so V doubles (V is linear in h).
    FrustumLoftModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const double before = volumeMm3(regenerator, m.loft);
    REQUIRE(m.doc.setParameterValue(m.height, 60_mm).has_value());
    CHECK(requireReport(regenerator, m.doc).regenerated == std::vector<ObjectId>{m.loft});
    CHECK_THAT(volumeMm3(regenerator, m.loft), WithinRel(frustumVolume(10, 5, 60), kRel));
    CHECK_THAT(volumeMm3(regenerator, m.loft) / before, WithinRel(2.0, kRel));
    checkBox(requireBody(regenerator, m.loft), {-10, -10, 0}, {10, 10, 60});
    // A middle section's offset: the three-section loft's `mid` 50 -> 40 mm.
    ThreeSectionLoftModel three;
    Regenerator threeRegenerator;
    REQUIRE(requireReport(threeRegenerator, three.doc).succeeded());
    REQUIRE(three.doc.setParameterValue(three.mid, 40_mm).has_value());
    CHECK(requireReport(threeRegenerator, three.doc).regenerated == std::vector<ObjectId>{three.loft});
    CHECK_THAT(volumeMm3(threeRegenerator, three.loft),
               WithinRel(frustumVolume(5, 10, 40) + frustumVolume(10, 5, 60), kRel));
    CHECK(circlesAt(requireBody(threeRegenerator, three.loft), {0, 0, 40}, 10) == 1);
}

TEST_CASE("LoftFeature_RegeneratesWhenTargetChanges", "[loft][features][acceptance]") {
    // The block 20 -> 30 mm high: the taper (from z = 20 down to 5) is now a
    // closed cavity in the new block; then its depth 15 -> 10 mm.
    TaperedHoleModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    REQUIRE(m.doc.setParameterValue(m.height, 30_mm).has_value());
    CHECK(requireReport(regenerator, m.doc).regenerated == std::vector<ObjectId>{m.pad, m.taper});
    CHECK_THAT(volumeMm3(regenerator, m.taper), WithinRel(TaperedHoleModel::expectedVolume(30, 15), kRel));
    checkBox(requireBody(regenerator, m.taper), {0, 0, 0}, {100, 50, 30});
    REQUIRE(m.doc.setParameterValue(m.extra, 10_mm).has_value());
    CHECK(requireReport(regenerator, m.doc).regenerated == std::vector<ObjectId>{m.taper});
    CHECK_THAT(volumeMm3(regenerator, m.taper), WithinRel(TaperedHoleModel::expectedVolume(30, 10), kRel));
}

TEST_CASE("LoftFeature_RejectsTooFewProfiles", "[loft][features][acceptance]") {
    // A definition with one section never reaches the document, and the
    // geometry refuses one on its own.
    CHECK(refusal({.sections = {{.sketch = SketchId::fromValue(4)}}}).message ==
          "a loft needs at least two sections, got 1");
    FrustumLoftModel m;
    const auto sections = resolveLoftSections(m.definitionOf(m.loft), m.doc);
    REQUIRE(sections.has_value());
    const auto body = geometry::makeLoft(std::span{sections->data(), 1});
    REQUIRE_FALSE(body.has_value());
    CHECK(body.error().code == ErrorCode::InvalidArgument);
    CHECK(body.error().message == "makeLoft: a loft needs at least two sections, got 1");
}

TEST_CASE("LoftFeature_RejectsOpenProfile", "[loft][features][acceptance]") {
    // Three sides of a square as the top section: no closed profile.
    FrustumLoftModel m;
    auto open = std::make_unique<sketch::Sketch>("Open");
    const EntityId a = bettercad::test::require(open->addPoint(Point2D{}));
    const EntityId b = bettercad::test::require(open->addPoint(Point2D{10_mm, 0_mm}));
    const EntityId c = bettercad::test::require(open->addPoint(Point2D{10_mm, 10_mm}));
    const EntityId d = bettercad::test::require(open->addPoint(Point2D{0_mm, 10_mm}));
    for (const auto& [p, q] : {std::pair{a, b}, std::pair{b, c}, std::pair{c, d}}) {
        bettercad::test::require(open->addLine(p, q));
    }
    useSection(m.doc, m.loft, 1, std::move(open));
    Regenerator regenerator;
    const Error error = requireFailure(regenerator, m.doc, m.loft);
    CHECK(error.code == ErrorCode::FailedPrecondition);
    CHECK_THAT(error.message, StartsWith("Loft: section 2 (sketch 'Open'): the profile is open: an edge ends at "));
}

TEST_CASE("LoftFeature_RejectsMissingProfile", "[loft][features][acceptance]") {
    SECTION("a section's sketch is deleted, then put back with its ID") {
        FrustumLoftModel m;
        Regenerator regenerator;
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        const double lofted = volumeMm3(regenerator, m.loft);
        auto removed = m.doc.removeObject(m.top);
        REQUIRE(removed.has_value());
        const Error error = requireFailure(regenerator, m.doc, m.loft);
        CHECK(error.code == ErrorCode::NotFound);
        CHECK(error.message == "object:6 references object:5, which does not exist");
        REQUIRE(m.doc.insertObject(std::move(*removed)).has_value());
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CHECK(bits(volumeMm3(regenerator, m.loft)) == bits(lofted)); // nothing was substituted
    }
    SECTION("a section reference that is not a sketch") {
        FrustumLoftModel m;
        LoftDefinition d = m.definitionOf(m.loft);
        d.sections[1].sketch = SketchId::fromValue(m.loft.value());
        const auto sections = resolveLoftSections(d, m.doc);
        REQUIRE_FALSE(sections.has_value());
        CHECK(sections.error().code == ErrorCode::NotFound);
        CHECK(sections.error().message == "section 2: sketch:6 is not a sketch in this document");
    }
    SECTION("a section's offset parameter is deleted") {
        FrustumLoftModel m;
        Regenerator regenerator;
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        LoftDefinition d = m.definitionOf(m.loft);
        d.sections[1].offsetParameter = ParameterId::fromValue(99);
        m.setDefinition(m.loft, d);
        const Error error = requireFailure(regenerator, m.doc, m.loft);
        CHECK(error.code == ErrorCode::NotFound);
        CHECK(error.message == "object:6 references object:99, which does not exist");
    }
}

TEST_CASE("LoftFeature_RejectsCoincidentSections", "[loft][features][acceptance]") {
    SECTION("the spacing parameter at 0") {
        FrustumLoftModel m;
        REQUIRE(m.doc.setParameterValue(m.height, 0_mm).has_value());
        Regenerator regenerator;
        const Error error = requireFailure(regenerator, m.doc, m.loft);
        CHECK(error.code == ErrorCode::InvalidArgument);
        CHECK(error.message == "Loft: makeLoft: sections 1 and 2 lie on the same plane: a loft needs its sections "
                               "apart");
    }
    SECTION("two sketches on one plane, without offsets") {
        FrustumLoftModel m;
        LoftDefinition d = m.definitionOf(m.loft);
        d.sections[1].offsetParameter.reset();
        m.setDefinition(m.loft, d);
        Regenerator regenerator;
        CHECK(requireFailure(regenerator, m.doc, m.loft).message ==
              "Loft: makeLoft: sections 1 and 2 lie on the same plane: a loft needs its sections apart");
    }
}

TEST_CASE("LoftFeature_RejectsInvalidProfile", "[loft][features][acceptance]") {
    FrustumLoftModel m;
    Regenerator regenerator;
    SECTION("two closed profiles in one section") {
        auto pair = std::make_unique<sketch::Sketch>("Pair");
        bettercad::test::require(pair->addCircle(Point2D{-(10_mm), 0_mm}, 3_mm));
        bettercad::test::require(pair->addCircle(Point2D{10_mm, 0_mm}, 3_mm));
        useSection(m.doc, m.loft, 1, std::move(pair));
        const Error error = requireFailure(regenerator, m.doc, m.loft);
        CHECK(error.code == ErrorCode::FailedPrecondition);
        CHECK(error.message ==
              "Loft: section 2 (sketch 'Pair'): it has 2 closed profiles; a loft section must be exactly one");
    }
    SECTION("a profile with a hole") {
        auto ring = std::make_unique<sketch::Sketch>("Ring");
        bettercad::test::require(ring->addCircle(Point2D{}, 5_mm));
        bettercad::test::require(ring->addCircle(Point2D{}, 2_mm));
        useSection(m.doc, m.loft, 1, std::move(ring));
        const Error error = requireFailure(regenerator, m.doc, m.loft);
        CHECK(error.code == ErrorCode::FailedPrecondition);
        CHECK(error.message ==
              "Loft: section 2 (sketch 'Ring'): its profile has a hole; loft sections must be single closed profiles");
    }
    SECTION("a loop enclosing no area") {
        useSection(m.doc, m.loft, 1, fixedPolygon("Flat", Frame3D::xy(), {{0, 0}, {10, 10}, {10, 0}, {0, 10}}));
        const Error error = requireFailure(regenerator, m.doc, m.loft);
        CHECK(error.code == ErrorCode::InvalidArgument);
        CHECK(error.message == "Loft: section 2 (sketch 'Flat'): a profile loop encloses no area");
    }
    SECTION("a self-intersecting (bow-tie) loop against a square") {
        useSection(m.doc, m.loft, 0, fixedPolygon("Square", Frame3D::xy(), {{-5, -5}, {5, -5}, {5, 5}, {-5, 5}}));
        LoftDefinition d = m.definitionOf(m.loft);
        const ObjectId bow =
            m.doc.addObject(fixedPolygon("Bow", Frame3D::xy(), {{-5, -5}, {5, 5}, {5, -5}, {-5, 10}})).value();
        d.sections[1] = {.sketch = sketchIdOf(bow), .offsetParameter = m.height};
        m.setDefinition(m.loft, d);
        const Error error = requireFailure(regenerator, m.doc, m.loft);
        CHECK(error.code == ErrorCode::Internal);
        CHECK(error.message ==
              "Loft: makeLoft: section 2: the profile face is invalid (self-intersecting or overlapping loops?)");
    }
}

TEST_CASE("LoftFeature_RejectsIncompatibleSections", "[loft][features][acceptance]") {
    FrustumLoftModel m;
    Regenerator regenerator;
    SECTION("a circle to a rectangle now lofts (P12-LOFT-001)") {
        // Until this milestone a circle against a rectangle was refused as
        // unmatchable. It is matched by arc length now, so it builds; its
        // volume is checked against the closed form in LoftShapeTests.cpp.
        useSection(m.doc, m.loft, 1, fixedPolygon("Box", levelPlane(30), {{-5, -5}, {5, -5}, {5, 5}, {-5, 5}}));
        const RegenerationReport report = requireReport(regenerator, m.doc);
        INFO(describe(report));
        REQUIRE(report.succeeded());
        const geometry::Body* body = regenerator.body(m.loft);
        REQUIRE(body != nullptr);
        CHECK(body->isValid());
        CHECK(body->topology().solids == 1);
    }
    SECTION("a section on a tilted plane") {
        const double tilt = 10.0 * pi / 180.0;
        const Frame3D tilted = Frame3D::create(Point3D{0_mm, 0_mm, 30_mm},
                                               *Direction3D::fromComponents(0.0, -std::sin(tilt), std::cos(tilt)),
                                               Direction3D::unitX())
                                   .value();
        useSection(m.doc, m.loft, 1, fixedCircle("Tilted", tilted, 0, 0, 5));
        const Error error = requireFailure(regenerator, m.doc, m.loft);
        CHECK(error.code == ErrorCode::InvalidArgument);
        CHECK(error.message == "Loft: makeLoft: section 2 is not parallel to section 1: their planes are 10 deg apart; "
                               "only sections on parallel planes can be lofted");
    }
    SECTION("an offset driven by an angle") {
        const ParameterId tilt = m.doc.createParameter("tilt", 30_deg, units::deg).value();
        LoftDefinition d = m.definitionOf(m.loft);
        d.sections[1].offsetParameter = tilt;
        m.setDefinition(m.loft, d);
        const Error error = requireFailure(regenerator, m.doc, m.loft);
        CHECK(error.code == ErrorCode::DimensionMismatch);
        CHECK_THAT(error.message, StartsWith("Loft: section 2 (sketch 'Top'): "));
    }
}

TEST_CASE("LoftFeature_FailsSafelyOnSelfIntersection", "[loft][features][acceptance]") {
    SECTION("a half disc turned by 180 deg pinches to nothing halfway: refused before the kernel") {
        Document doc{"Pinch"};
        const ObjectId left = doc.addObject(halfDisc("Left", Frame3D::xy(), pi)).value();
        const ObjectId right = doc.addObject(halfDisc("Right", Frame3D::xy(), 0.0)).value();
        auto feature = LoftFeature::create("Pinch", {.sections = {{.sketch = sketchIdOf(left)},
                                                                  {.sketch = sketchIdOf(right), .offset = 30_mm}}});
        REQUIRE(feature.has_value());
        const ObjectId loft = doc.addObject(std::move(*feature)).value();
        Regenerator regenerator;
        const Error error = requireFailure(regenerator, doc, loft);
        CHECK(error.code == ErrorCode::InvalidArgument);
        CHECK(error.message == "Pinch: makeLoft: the loft between sections 1 and 2 folds over itself: its cross-section "
                               "would lose all its area 50% of the way from section 1 to section 2");
        // Turned by 90 deg instead, it lofts: V = h A (2 + cos 90 deg) / 3.
        const ObjectId up = doc.addObject(halfDisc("Up", Frame3D::xy(), pi / 2.0)).value();
        LoftDefinition d = doc.findObjectAs<LoftFeature>(loft)->definition();
        d.sections[1].sketch = sketchIdOf(up);
        REQUIRE(doc.modifyObject<LoftFeature>(loft, [&](LoftFeature& f) { return f.setDefinition(d); }).has_value());
        REQUIRE(requireReport(regenerator, doc).succeeded());
        CHECK_THAT(volumeMm3(regenerator, loft), WithinRel(30.0 * 12.5 * pi * 2.0 / 3.0, kRelSpline));
    }
    SECTION("an L turned by 90 deg: its sides pass through one another (the kernel's check)") {
        Document doc{"Twist"};
        const ObjectId ell = doc.addObject(fixedPolygon("L", Frame3D::xy(),
                                                        {{0, 0}, {20, 0}, {20, 5}, {5, 5}, {5, 20}, {0, 20}}))
                                 .value();
        const ObjectId turned =
            doc.addObject(fixedPolygon("Turned", Frame3D::xy(), {{0, 0}, {0, 20}, {-5, 20}, {-5, 5}, {-20, 5}, {-20, 0}}))
                .value();
        auto feature = LoftFeature::create("Twist", {.sections = {{.sketch = sketchIdOf(ell)},
                                                                  {.sketch = sketchIdOf(turned), .offset = 30_mm}}});
        REQUIRE(feature.has_value());
        const ObjectId loft = doc.addObject(std::move(*feature)).value();
        Regenerator regenerator;
        const Error error = requireFailure(regenerator, doc, loft);
        CHECK(error.code == ErrorCode::InvalidArgument);
        CHECK(error.message == "Twist: makeLoft: the lofted solid would intersect itself: its sides pass through one "
                               "another between the sections");
    }
}

TEST_CASE("LoftFeature_FailuresLeaveTheDocumentAndUpstreamBodiesIntact", "[loft][features][acceptance]") {
    TaperedHoleModel m;
    CommandHistory history;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const Document before = m.doc.clone();
    const std::uint64_t revision = m.doc.revision();
    const double padVolume = volumeMm3(regenerator, m.pad);
    const double taperVolume = volumeMm3(regenerator, m.taper);

    // An invalid edit is refused before it touches the document.
    LoftDefinition invalid = m.definitionOf<LoftFeature>(m.taper);
    invalid.sections.pop_back();
    CHECK(errorCode(history.execute(m.doc, std::make_unique<ModifyLoftCommand>(featureIdOf(m.taper), invalid))) ==
          ErrorCode::InvalidArgument);
    CHECK(equivalent(m.doc, before));
    CHECK(m.doc.revision() == revision);
    CHECK_FALSE(history.canUndo());

    // A valid edit the geometry cannot take fails at regeneration only: the
    // tip moved onto the mouth's own plane, where there is no loft to make.
    // (A rectangle against a circle was this case until P12-LOFT-001, which
    // matches different shapes by arc length and builds it.)
    LoftDefinition impossible = m.definitionOf<LoftFeature>(m.taper);
    impossible.sections[1] = {.sketch = sketchIdOf(m.tip)};
    REQUIRE(history.execute(m.doc, std::make_unique<ModifyLoftCommand>(featureIdOf(m.taper), impossible)).has_value());
    const Document edited = m.doc.clone();
    const Error error = requireFailure(regenerator, m.doc, m.taper);
    CHECK(error.code == ErrorCode::InvalidArgument);
    CHECK(error.message == "Taper: makeLoft: sections 1 and 2 lie on the same plane: a loft needs its sections "
                           "apart");
    CHECK(equivalent(m.doc, edited)); // regeneration does not change the model
    CHECK(regenerator.state(m.pad) == NodeState::UpToDate);
    CHECK(bits(volumeMm3(regenerator, m.pad)) == bits(padVolume));

    REQUIRE(history.undo(m.doc).has_value());
    CHECK(equivalent(m.doc, before));
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(bits(volumeMm3(regenerator, m.taper)) == bits(taperVolume));
}

TEST_CASE("LoftFeature_UndoRedoRestoresGeometry", "[loft][features][undo][acceptance]") {
    // The frustum model without its loft; the loft is created by command.
    FrustumLoftModel m;
    REQUIRE(m.doc.removeObject(m.loft).has_value());
    CommandHistory history;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const Document initial = m.doc.clone();

    struct State {
        LoftDefinition definition;
        Document document;
        Length r2;
        double volume = 0.0;
        Point3D centre;
        BoundingBox3D box;
    };
    ObjectId loft;
    const auto record = [&] {
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        const geometry::Body& body = requireBody(regenerator, loft);
        return State{m.definitionOf(loft),
                     m.doc.clone(),
                     m.doc.parameters().find(m.r2)->as<Length>().value(),
                     volumeMm3(regenerator, loft),
                     body.massProperties()->centerOfMass,
                     body.boundingBox().value()};
    };
    const auto create = [&] {
        auto command = std::make_unique<CreateLoftCommand>("Loft", m.definition());
        CreateLoftCommand* raw = command.get();
        CHECK(command->description() == "Create loft 'Loft'");
        REQUIRE(history.execute(m.doc, std::move(command)).has_value());
        loft = ObjectId{raw->featureId()};
    };

    SECTION("commands on the loft re-solve nothing: undo and redo restore everything bit for bit") {
        // 1. Create; 2. add a crown section (r 3, 15 mm above the top).
        create();
        const State created = record();
        CHECK_THAT(created.volume, WithinRel(1750.0 * pi, kRel));
        auto crown = std::make_unique<AddObjectCommand>(fixedCircle("Crown", levelPlane(45), 0, 0, 3));
        AddObjectCommand* crownRaw = crown.get();
        REQUIRE(history.execute(m.doc, std::move(crown)).has_value());
        const State crowned = record();
        LoftDefinition three = m.definitionOf(loft);
        three.sections.push_back({.sketch = sketchIdOf(crownRaw->objectId())});
        REQUIRE(history.execute(m.doc, std::make_unique<ModifyLoftCommand>(featureIdOf(loft), three)).has_value());
        const State extended = record();
        CHECK_THAT(extended.volume, WithinRel(frustumVolume(10, 5, 30) + frustumVolume(5, 3, 15), kRel));
        CHECK(m.definitionOf(loft).sections.size() == 3);

        const auto same = [&](const State& expected) {
            CHECK(equivalent(m.doc, expected.document));
            CHECK(m.definitionOf(loft) == expected.definition);
            REQUIRE(requireReport(regenerator, m.doc).succeeded());
            CHECK(bits(volumeMm3(regenerator, loft)) == bits(expected.volume));
            CHECK(requireBody(regenerator, loft).massProperties()->centerOfMass == expected.centre);
            CHECK(requireBody(regenerator, loft).boundingBox().value() == expected.box);
        };
        REQUIRE(history.undo(m.doc).has_value());
        same(crowned);
        REQUIRE(history.undo(m.doc).has_value());
        same(created);
        REQUIRE(history.undo(m.doc).has_value());
        CHECK(m.doc.findObject(loft) == nullptr);
        CHECK(equivalent(m.doc, initial));
        REQUIRE(history.redo(m.doc).has_value()); // the same ID and geometry
        same(created);
        REQUIRE(history.redo(m.doc).has_value());
        REQUIRE(history.redo(m.doc).has_value());
        same(extended);
        // Commands check the feature kind.
        CHECK(errorCode(history.execute(m.doc, std::make_unique<ModifyLoftCommand>(featureIdOf(m.top), three))) ==
              ErrorCode::NotFound);
    }
    SECTION("parameter edits: the top radius 5 -> 8 mm, the height 30 -> 60 mm") {
        // Undoing a parameter restores its value exactly and regeneration
        // re-solves its sketch from where the edit left it: the same geometry
        // to rounding, not bit for bit (the solver starts elsewhere).
        create();
        std::vector<State> states;
        states.push_back(record());
        REQUIRE(history.execute(m.doc, ModifyParameterCommand::setValue(m.r2, 8_mm)).has_value());
        states.push_back(record());
        CHECK_THAT(states.back().volume, WithinRel(frustumVolume(10, 8, 30), kRel));
        REQUIRE(history.execute(m.doc, ModifyParameterCommand::setValue(m.height, 60_mm)).has_value());
        states.push_back(record());
        CHECK_THAT(states.back().volume, WithinRel(frustumVolume(10, 8, 60), kRel));
        const auto matches = [&](const State& expected, double analytic) {
            CHECK(m.definitionOf(loft) == expected.definition);
            CHECK(m.doc.parameters().find(m.r2)->as<Length>().value() == expected.r2);
            REQUIRE(requireReport(regenerator, m.doc).succeeded());
            CHECK_THAT(volumeMm3(regenerator, loft), WithinRel(expected.volume, kRel));
            CHECK_THAT(volumeMm3(regenerator, loft), WithinRel(analytic, kRel));
            const geometry::Body& body = requireBody(regenerator, loft);
            const auto mmOf = [](const Point3D& p) {
                return V{p.x.in(units::mm), p.y.in(units::mm), p.z.in(units::mm)};
            };
            checkPoint(body.massProperties()->centerOfMass, mmOf(expected.centre));
        };
        REQUIRE(history.undo(m.doc).has_value()); // height back to 30
        matches(states[1], frustumVolume(10, 8, 30));
        REQUIRE(history.undo(m.doc).has_value()); // r2 back to 5
        matches(states[0], frustumVolume(10, 5, 30));
        CHECK(m.doc.parameters().find(m.r2)->as<Length>().value() == 5_mm);
        REQUIRE(history.redo(m.doc).has_value()); // r2 8 again
        matches(states[1], frustumVolume(10, 8, 30));
        REQUIRE(history.redo(m.doc).has_value());
        matches(states[2], frustumVolume(10, 8, 60));
        checkBox(requireBody(regenerator, loft), {-10, -10, 0}, {10, 10, 60});
    }
}

TEST_CASE("LoftFeature_RegenerationIsDeterministic", "[loft][features]") {
    for (const bool oblique : {false, true}) {
        CAPTURE(oblique);
        Document doc = ThreeSectionLoftModel{}.doc.clone();
        ObjectId loft = ObjectId::fromValue(8);
        if (oblique) {
            OffsetLoftModel m;
            REQUIRE(m.doc.setParameterValue(m.shift, 20_mm).has_value());
            doc = m.doc.clone();
            loft = m.loft;
        }
        Regenerator first;
        Regenerator second;
        const RegenerationReport a = requireReport(first, doc);
        const RegenerationReport b = requireReport(second, doc);
        REQUIRE(a.succeeded());
        CHECK(a.regenerated == b.regenerated);
        const auto pa = propertiesOf(first, loft);
        const auto pb = propertiesOf(second, loft);
        CHECK(bits(pa.volume.si()) == bits(pb.volume.si()));
        CHECK(bits(pa.surfaceArea.si()) == bits(pb.surfaceArea.si()));
        CHECK(pa.centerOfMass == pb.centerOfMass);
        CHECK(requireBody(first, loft).boundingBox().value() == requireBody(second, loft).boundingBox().value());
        CHECK(requireBody(first, loft).topology() == requireBody(second, loft).topology());
        CHECK(doc.findObjectAs<LoftFeature>(loft)->definition().sections.size() == (oblique ? 2U : 3U));
    }
}
