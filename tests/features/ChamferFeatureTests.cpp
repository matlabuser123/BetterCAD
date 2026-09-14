#include "FeatureTestSupport.hpp"
#include "TestHelpers.hpp"
#include "core/geometry/GeometryTestSupport.hpp"
#include "support/BracketModel.hpp"
#include "support/ChamferBlockModel.hpp"
#include "support/TurnedPartModel.hpp"

#include <bettercad/core/document/Commands.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/geometry/Edges.hpp>
#include <bettercad/features/ChamferFeature.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/FeatureCommands.hpp>
#include <bettercad/features/Regeneration.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/features/ResultBodies.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <numbers>
#include <vector>

using namespace bettercad;
using namespace bettercad::features;
using namespace bettercad::literals;
using namespace bettercad::sketch;
using bettercad::geometry::ChamferMode;
using bettercad::geometry::EdgeCurve;
using bettercad::geometry::EdgeSignature;
using bettercad::test::addRectangle;
using bettercad::test::ChamferBlockModel;
using bettercad::test::describe;
using bettercad::test::errorCode;
using bettercad::test::kPositionToleranceMm;
using bettercad::test::requireReport;
using bettercad::test::TurnedPartModel;
using bettercad::test::volumeMm3;
using Catch::Matchers::StartsWith;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

constexpr double pi = std::numbers::pi;
// Chamfers of boxes and cylinders have planar, cylindrical and conical faces,
// whose properties the kernel computes to rounding level (measured in the
// geometry-level chamfer tests).
constexpr double kRel = 1e-12;

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

/// Number of edges of the feature's body on the referenced curve.
std::size_t edgesOn(const Regenerator& regenerator, ObjectId feature, const EdgeSignature& signature) {
    const geometry::Body* body = regenerator.body(feature);
    REQUIRE(body != nullptr);
    const auto found = geometry::findEdges(*body, signature);
    REQUIRE(found.has_value());
    return found->size();
}

/// The block's chamfer as a literal-distance chamfer of another mode.
ChamferDefinition withMode(ChamferDefinition d, ChamferMode mode, Length distance, Length second, Angle angle,
                           const Direction3D& side) {
    d.mode = mode;
    d.distanceParameter.reset();
    d.distance = distance;
    d.distance2 = second;
    d.angle = angle;
    d.referenceSide = side;
    return d;
}

} // namespace

TEST_CASE("ChamferFeature_DefinitionIsValidatedOnCreateAndEdit", "[chamfer][features]") {
    const ChamferDefinition good{
        .target = FeatureId::fromValue(2), .edges = {ChamferBlockModel::alongX(0, 20)}, .distance = 5_mm};
    REQUIRE(ChamferFeature::create("C", good).has_value());
    CHECK(good.mode == ChamferMode::EqualDistance);

    const auto message = [](const ChamferDefinition& definition) -> std::string {
        auto feature = ChamferFeature::create("C", definition);
        REQUIRE_FALSE(feature.has_value());
        CHECK(feature.error().code == ErrorCode::InvalidArgument);
        return feature.error().message;
    };
    const auto accepted = [](const ChamferDefinition& definition) {
        return ChamferFeature::create("C", definition).has_value();
    };
    ChamferDefinition d = good;

    SECTION("a target and at least one edge") {
        d.target = FeatureId{};
        CHECK(message(d) == "a chamfer needs a target feature");
        d = good;
        d.edges.clear();
        CHECK(message(d) == "a chamfer needs at least one edge");
    }
    SECTION("distances that are zero, negative, NaN or infinite") {
        for (const double mm : {0.0, -1.0, std::nan(""), std::numeric_limits<double>::infinity()}) {
            CAPTURE(mm);
            d.distance = mm * units::mm;
            CHECK_THAT(message(d), StartsWith("the chamfer distance must be positive and finite, got "));
        }
    }
    SECTION("a driving parameter replaces the literal distance") {
        d.distance = 0_mm;
        d.distanceParameter = ParameterId::fromValue(4);
        CHECK(accepted(d));
        d.distanceParameter = ParameterId{};
        CHECK(message(d) == "the distance parameter ID must be valid");
    }
    SECTION("edge references") {
        // The same line again, described from another point and the other way round.
        d.edges.push_back(geometry::lineSignature(Point3D{42_mm, 0_mm, 20_mm}, Direction3D::unitX().reversed()));
        CHECK(message(d) == "edge references 1 and 2 refer to the same line through (0, 0, 20) mm along (1, 0, 0)");
        d.edges = {EdgeSignature{.curve = EdgeCurve::Circle, .radius = 0_mm}};
        CHECK(message(d) == "edge reference 1: a circle edge reference needs a positive radius");
        d.edges = {EdgeSignature{.curve = EdgeCurve::Other}};
        CHECK(message(d) == "edge reference 1: only line and circle edges can be referenced");
        d.edges = {EdgeSignature{.point = Point3D{Length::fromSi(std::nan("")), 0_mm, 0_mm}}};
        CHECK(message(d) == "edge reference 1: an edge reference needs a finite point");
        // Parallel lines are different edges.
        d.edges = {ChamferBlockModel::alongX(0, 20), ChamferBlockModel::alongX(50, 20)};
        CHECK(accepted(d));
    }
    SECTION("each mode takes exactly the values it uses") {
        d.distance2 = 3_mm;
        CHECK(message(d) == "only a chamfer by two distances takes a second distance");
        d.mode = ChamferMode::TwoDistance;
        CHECK(message(d) == "a chamfer by two distances needs a reference side");
        d.referenceSide = Direction3D::unitZ();
        CHECK(accepted(d));
        d.distance2 = 0_mm;
        CHECK_THAT(message(d), StartsWith("the second chamfer distance must be positive and finite, got "));
        d.distance2 = 3_mm;
        d.angle = 30_deg;
        CHECK(message(d) == "only a chamfer by distance and angle takes an angle");

        d.mode = ChamferMode::DistanceAngle;
        d.distance2 = 0_mm;
        CHECK(accepted(d));
        for (const Angle angle : {0_deg, 90_deg, -(10_deg), Angle::fromSi(std::nan(""))}) {
            d.angle = angle;
            CHECK_THAT(message(d), StartsWith("the chamfer angle must be in (0, 90) deg, got "));
        }
        d.mode = ChamferMode::EqualDistance;
        d.angle = 0_deg;
        CHECK(message(d) == "an equal-distance chamfer takes no reference side");
    }
    SECTION("an invalid edit leaves the feature unchanged") {
        auto feature = ChamferFeature::create("C", good);
        REQUIRE(feature.has_value());
        d.distance = -(5_mm);
        CHECK(errorCode((*feature)->setDefinition(d)) == ErrorCode::InvalidArgument);
        CHECK((*feature)->definition() == good);
        const auto unchanged = (*feature)->setDefinition(good);
        REQUIRE(unchanged.has_value());
        CHECK_FALSE(*unchanged);
    }
    CHECK(geometry::toString(ChamferMode::DistanceAngle) == "distance and angle");
}

TEST_CASE("ChamferFeature_DependsOnItsTargetAndDistanceParameter", "[chamfer][features]") {
    ChamferBlockModel m;
    const auto* edge = m.doc.findObjectAs<ChamferFeature>(m.edge);
    REQUIRE(edge != nullptr);
    CHECK(edge->dependencies() == std::vector<ObjectId>{m.pad, ObjectId{m.size}});
    CHECK(edge->target() == featureId(m.pad));
    CHECK(edge->typeName() == "chamfer");
    const auto copy = edge->clone();
    CHECK(equivalent(*copy, *edge));

    ChamferDefinition literal = m.definitionOf(m.edge);
    literal.distanceParameter.reset();
    literal.distance = 5_mm;
    m.setDefinition(m.edge, literal);
    CHECK(m.doc.findObjectAs<ChamferFeature>(m.edge)->dependencies() == std::vector<ObjectId>{m.pad});
}

TEST_CASE("ChamferFeature_SingleEdgeMatchesAnalyticVolume", "[chamfer][features][acceptance]") {
    ChamferBlockModel m;
    Regenerator regenerator;
    const RegenerationReport report = requireReport(regenerator, m.doc);
    INFO(describe(report));
    REQUIRE(report.succeeded());
    CHECK(report.regenerated == std::vector<ObjectId>{m.base, m.pad, m.edge});

    const geometry::Body* body = regenerator.body(m.edge);
    REQUIRE(body != nullptr);
    CHECK(body->isValid());
    const geometry::TopologySummary topology = body->topology();
    CHECK(topology.solids == 1);
    CHECK(topology.faces == 7);  // the box's six plus the chamfer face
    CHECK(topology.edges == 15); // one edge replaced by two, plus the two short end edges

    // V = V0 - (d^2 / 2) L with V0 = 100 x 50 x 20, d = 5, L = 100.
    const double expected = ChamferBlockModel::expectedVolume(100, 50, 20, 5);
    CHECK(expected == 98750.0);
    const auto props = body->massProperties().value();
    CHECK_THAT(props.volume.in(units::mm3), WithinRel(expected, kRel));
    // The top and front faces each lose a 5 x 100 strip and the end faces a
    // 5 x 5 / 2 triangle each; the chamfer face is 100 x 5 sqrt(2).
    CHECK_THAT(props.surfaceArea.in(units::mm2), WithinRel(16000.0 - 1000.0 - 25.0 + 500.0 * std::sqrt(2.0), kRel));
    const auto box = body->boundingBox().value();
    CHECK_THAT(box.max.x.in(units::mm), WithinAbs(100.0, kPositionToleranceMm));
    CHECK_THAT(box.max.y.in(units::mm), WithinAbs(50.0, kPositionToleranceMm));
    CHECK_THAT(box.max.z.in(units::mm), WithinAbs(20.0, kPositionToleranceMm));

    // The edge is gone; the chamfer face meets the top and front faces 5 mm from it.
    CHECK(edgesOn(regenerator, m.edge, ChamferBlockModel::alongX(0, 20)) == 0);
    CHECK(edgesOn(regenerator, m.edge, ChamferBlockModel::alongX(5, 20)) == 1);
    CHECK(edgesOn(regenerator, m.edge, ChamferBlockModel::alongX(0, 15)) == 1);

    // The target's body is an untouched intermediate; the chamfer is the result.
    CHECK_THAT(volumeMm3(regenerator, m.pad), WithinRel(100000.0, kRel));
    CHECK(edgesOn(regenerator, m.pad, ChamferBlockModel::alongX(0, 20)) == 1);
    CHECK(resultFeatures(m.doc) == std::vector<ObjectId>{m.edge});
}

TEST_CASE("ChamferFeature_MultipleEdgesMatchAnalyticVolume", "[chamfer][features][acceptance]") {
    ChamferBlockModel m;
    ChamferDefinition d = m.definitionOf(m.edge);
    Regenerator regenerator;
    const double d2 = 5.0 * 5.0;
    const double corner = 5.0 * 5.0 * 5.0 / 3.0; // where two chamfers meet at a right-angled corner

    SECTION("two edges that do not touch") {
        d.edges.push_back(ChamferBlockModel::alongX(50, 0)); // the bottom back edge
        m.setDefinition(m.edge, d);
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CHECK_THAT(volumeMm3(regenerator, m.edge), WithinRel(100000.0 - 2 * (d2 / 2) * 100, kRel));
        CHECK(regenerator.body(m.edge)->topology().faces == 8);
    }
    SECTION("two edges that meet at a corner") {
        d.edges.push_back(ChamferBlockModel::alongY(0, 20)); // the top left edge
        m.setDefinition(m.edge, d);
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        // Two prisms of cross-section d^2 / 2 that overlap in a corner of volume d^3 / 3.
        CHECK_THAT(volumeMm3(regenerator, m.edge), WithinRel(100000.0 - (d2 / 2) * (100 + 50) + corner, kRel));
        CHECK(regenerator.body(m.edge)->isValid());
    }
    SECTION("the four edges around the top face") {
        d.edges.push_back(ChamferBlockModel::alongY(100, 20));
        d.edges.push_back(ChamferBlockModel::alongX(50, 20));
        d.edges.push_back(ChamferBlockModel::alongY(0, 20));
        m.setDefinition(m.edge, d);
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CHECK_THAT(volumeMm3(regenerator, m.edge),
                   WithinRel(100000.0 - (d2 / 2) * (2 * 100 + 2 * 50) + 4 * corner, kRel));
        CHECK(regenerator.body(m.edge)->topology().faces == 10);
    }
}

TEST_CASE("ChamferFeature_TwoDistancesPutTheFirstDistanceOnTheReferenceFace", "[chamfer][features][acceptance]") {
    ChamferBlockModel m;
    Regenerator regenerator;
    // 5 mm on the top face (outward normal +Z), 3 mm down the front face.
    m.setDefinition(m.edge, withMode(m.definitionOf(m.edge), ChamferMode::TwoDistance, 5_mm, 3_mm, {},
                                     Direction3D::unitZ()));
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const double expected = 100000.0 - 0.5 * 5.0 * 3.0 * 100.0; // V0 - d1 d2 L / 2
    CHECK_THAT(volumeMm3(regenerator, m.edge), WithinRel(expected, kRel));
    CHECK(edgesOn(regenerator, m.edge, ChamferBlockModel::alongX(5, 20)) == 1);
    CHECK(edgesOn(regenerator, m.edge, ChamferBlockModel::alongX(0, 17)) == 1);

    // With the front face (outward normal -Y) as the reference, the distances swap faces.
    m.setDefinition(m.edge, withMode(m.definitionOf(m.edge), ChamferMode::TwoDistance, 5_mm, 3_mm, {},
                                     Direction3D::unitY().reversed()));
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK_THAT(volumeMm3(regenerator, m.edge), WithinRel(expected, kRel));
    CHECK(edgesOn(regenerator, m.edge, ChamferBlockModel::alongX(3, 20)) == 1);
    CHECK(edgesOn(regenerator, m.edge, ChamferBlockModel::alongX(0, 15)) == 1);
}

TEST_CASE("ChamferFeature_DistanceAngleMeasuresTheAngleFromTheReferenceFace", "[chamfer][features][acceptance]") {
    ChamferBlockModel m;
    Regenerator regenerator;
    // 5 mm on the top face; the chamfer face makes 30 degrees with it, so it
    // runs 5 tan(30) mm down the front face.
    m.setDefinition(m.edge, withMode(m.definitionOf(m.edge), ChamferMode::DistanceAngle, 5_mm, {}, 30_deg,
                                     Direction3D::unitZ()));
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const double leg = 5.0 * std::tan(pi / 6.0);
    CHECK_THAT(volumeMm3(regenerator, m.edge), WithinRel(100000.0 - 0.5 * 5.0 * leg * 100.0, kRel));
    CHECK(edgesOn(regenerator, m.edge, ChamferBlockModel::alongX(5, 20)) == 1);
    CHECK(edgesOn(regenerator, m.edge, ChamferBlockModel::alongX(0, 20 - leg)) == 1);
}

TEST_CASE("ChamferFeature_OnRevolvedBodyMatchesPappusAndFollowsTheSweep", "[chamfer][features][revolve][acceptance]") {
    // Both rims of the turned part's top face: the outer circle (radius 15)
    // and the edge of the bore (radius 5), 2 mm each.
    TurnedPartModel m;
    const auto outer = geometry::circleSignature(Point3D{0_mm, 0_mm, 40_mm}, Direction3D::unitZ(), 15_mm);
    const auto inner = geometry::circleSignature(Point3D{0_mm, 0_mm, 40_mm}, Direction3D::unitZ(), 5_mm);
    REQUIRE(outer.has_value());
    REQUIRE(inner.has_value());
    auto feature = ChamferFeature::create(
        "Rims", {.target = featureId(m.groove), .edges = {*outer, *inner}, .distance = 2_mm});
    REQUIRE(feature.has_value());
    const ObjectId rims = m.doc.addObject(std::move(*feature)).value();

    // Each chamfer removes a ring whose cross-section is the triangle with
    // legs d; its centroid lies d / 3 inside the edge, so by Pappus the outer
    // ring is pi d^2 (r - d/3) and the bore's pi d^2 (b + d/3): pi d^2 (r + b)
    // together, per full turn.
    const auto expected = [](double sweepDeg) {
        return TurnedPartModel::expectedVolume(15, 40, sweepDeg, 5) - sweepDeg / 360.0 * pi * 2.0 * 2.0 * (15 + 5);
    };
    Regenerator regenerator;
    const RegenerationReport report = requireReport(regenerator, m.doc);
    INFO(describe(report));
    REQUIRE(report.succeeded());
    CHECK(report.regenerated.back() == rims);
    CHECK(regenerator.body(rims)->isValid());
    CHECK_THAT(volumeMm3(regenerator, rims), WithinRel(expected(360), kRel));
    CHECK(resultFeatures(m.doc) == std::vector<ObjectId>{rims});

    SECTION("a partial sweep keeps the rims on the same circles") {
        REQUIRE(m.doc.setParameterValue(m.sweep, 270_deg).has_value());
        const RegenerationReport partial = requireReport(regenerator, m.doc);
        INFO(describe(partial));
        CHECK(partial.regenerated == std::vector<ObjectId>{m.turn, m.boreCut, m.groove, rims});
        CHECK_THAT(volumeMm3(regenerator, rims), WithinRel(expected(270), kRel));
    }
    SECTION("a larger radius moves the outer rim off the referenced circle") {
        REQUIRE(m.doc.setParameterValue(m.radius, 20_mm).has_value());
        const Error error = requireFailure(regenerator, m.doc, rims);
        CHECK(error.code == ErrorCode::NotFound);
        CHECK(error.message == "Rims: chamfer: edge reference 1 (circle around (0, 0, 40) mm with axis (0, 0, 1) and "
                               "radius 15 mm) matches no edge of the body");
    }
}

TEST_CASE("ChamferFeature_RegeneratesWhenTheUpstreamWidthChanges", "[chamfer][features][acceptance]") {
    ChamferBlockModel m;
    CommandHistory history;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const double original = volumeMm3(regenerator, m.edge);

    SECTION("the width rebuilds the sketch, the pad and the chamfer") {
        REQUIRE(history.execute(m.doc, ModifyParameterCommand::setValue(m.width, 120_mm)).has_value());
        const RegenerationReport report = requireReport(regenerator, m.doc);
        INFO(describe(report));
        CHECK(report.changed == std::vector<ObjectId>{ObjectId{m.width}});
        CHECK(report.regenerated == std::vector<ObjectId>{m.base, m.pad, m.edge});
        // The top front edge is longer but still on the line y = 0, z = 20.
        CHECK_THAT(volumeMm3(regenerator, m.edge), WithinRel(ChamferBlockModel::expectedVolume(120, 50, 20, 5), kRel));
        CHECK_THAT(regenerator.body(m.edge)->boundingBox()->max.x.in(units::mm),
                   WithinAbs(120.0, kPositionToleranceMm));

        // Undo restores the width; the sketch is then solved again from the
        // 120 mm rectangle, which reaches the original one to rounding level
        // (corners within ~1e-14 mm), not bit for bit. So the chamfer matches
        // its original volume to rounding level too.
        REQUIRE(history.undo(m.doc).has_value());
        CHECK(requireReport(regenerator, m.doc).regenerated == std::vector<ObjectId>{m.base, m.pad, m.edge});
        CHECK_THAT(volumeMm3(regenerator, m.edge), WithinRel(original, kRel));
    }
    SECTION("the distance rebuilds only the chamfer") {
        REQUIRE(history.execute(m.doc, ModifyParameterCommand::setValue(m.size, 3_mm)).has_value());
        const RegenerationReport report = requireReport(regenerator, m.doc);
        CHECK(report.changed == std::vector<ObjectId>{ObjectId{m.size}});
        CHECK(report.regenerated == std::vector<ObjectId>{m.edge});
        CHECK(regenerator.state(m.pad) == NodeState::UpToDate);
        CHECK_THAT(volumeMm3(regenerator, m.edge), WithinRel(ChamferBlockModel::expectedVolume(100, 50, 20, 3), kRel));

        REQUIRE(history.undo(m.doc).has_value());
        CHECK(requireReport(regenerator, m.doc).regenerated == std::vector<ObjectId>{m.edge});
        CHECK(volumeMm3(regenerator, m.edge) == original);
    }
}

TEST_CASE("ChamferFeature_EdgeChangedUpstreamFailsInsteadOfSubstituting", "[chamfer][features][acceptance]") {
    ChamferBlockModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());

    SECTION("a taller block moves the top edge off the referenced line") {
        REQUIRE(m.doc.setParameterValue(m.height, 30_mm).has_value());
        const Error error = requireFailure(regenerator, m.doc, m.edge);
        CHECK(error.code == ErrorCode::NotFound);
        CHECK(error.message ==
              "Edge: chamfer: edge reference 1 (line through (0, 0, 20) mm along (1, 0, 0)) matches no edge of the body");
        // The pad is rebuilt; the new top edge (z = 30) is not taken instead.
        CHECK_THAT(volumeMm3(regenerator, m.pad), WithinRel(150000.0, kRel));
        CHECK(edgesOn(regenerator, m.pad, ChamferBlockModel::alongX(0, 30)) == 1);
        CHECK(resultFeatures(m.doc) == std::vector<ObjectId>{m.edge});

        // The definition is untouched, so the original height restores the chamfer.
        REQUIRE(m.doc.setParameterValue(m.height, 20_mm).has_value());
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CHECK_THAT(volumeMm3(regenerator, m.edge), WithinRel(98750.0, kRel));
    }
    SECTION("a notch that splits the edge makes the reference ambiguous") {
        // A 10 x 10 mm notch at x = 45..55 in the front, cut upwards from the
        // bottom by notch_depth; the chamfer now applies to the notched block.
        const ParameterId notchDepth = m.doc.createParameter("notch_depth", 10_mm, units::mm).value();
        auto slot = std::make_unique<Sketch>("NotchSketch");
        addRectangle(*slot, 45_mm, -(1_mm), 10_mm, 11_mm);
        const ObjectId notchSketch = m.doc.addObject(std::move(slot)).value();
        auto cut = ExtrudeFeature::create("Notch", {.profile = ChamferBlockModel::sketchId(notchSketch),
                                                    .depthParameter = notchDepth,
                                                    .operation = FeatureOperation::Cut,
                                                    .target = featureId(m.pad)});
        REQUIRE(cut.has_value());
        const ObjectId notch = m.doc.addObject(std::move(*cut)).value();
        ChamferDefinition d = m.definitionOf(m.edge);
        d.target = featureId(notch);
        m.setDefinition(m.edge, d);

        // 10 mm deep, the notch stays below the chamfer.
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CHECK_THAT(volumeMm3(regenerator, m.edge), WithinRel(100000.0 - 10.0 * 10.0 * 10.0 - 1250.0, kRel));

        // 25 mm deep, it cuts through the top edge, leaving two edges on the line.
        REQUIRE(m.doc.setParameterValue(notchDepth, 25_mm).has_value());
        const Error error = requireFailure(regenerator, m.doc, m.edge);
        CHECK(error.code == ErrorCode::FailedPrecondition);
        CHECK(error.message == "Edge: chamfer: edge reference 1 (line through (0, 0, 20) mm along (1, 0, 0)) is "
                               "ambiguous: 2 edges of the body lie on it");
        CHECK(edgesOn(regenerator, notch, ChamferBlockModel::alongX(0, 20)) == 2);
        CHECK_THAT(volumeMm3(regenerator, notch), WithinRel(100000.0 - 10.0 * 10.0 * 20.0, kRel));
    }
}

TEST_CASE("ChamferFeature_InvalidInputsFailWithStructuredDiagnostics", "[chamfer][features][acceptance]") {
    ChamferBlockModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const double padVolume = volumeMm3(regenerator, m.pad);

    SECTION("a distance parameter of zero or less") {
        for (const Length size : {0_mm, -(2_mm)}) {
            REQUIRE(m.doc.setParameterValue(m.size, size).has_value());
            const Error error = requireFailure(regenerator, m.doc, m.edge);
            CHECK(error.code == ErrorCode::InvalidArgument);
            CHECK_THAT(error.message, StartsWith("Edge: chamfer: the chamfer distance must be positive and finite, got "));
        }
    }
    SECTION("a distance larger than the faces allow") {
        REQUIRE(m.doc.setParameterValue(m.size, 25_mm).has_value()); // the block is 20 mm high
        const Error error = requireFailure(regenerator, m.doc, m.edge);
        CHECK(error.code == ErrorCode::FailedPrecondition);
        CHECK(error.message == "Edge: chamfer: edge reference 1 (line through (0, 0, 20) mm along (1, 0, 0)) does not "
                               "fit: its chamfer needs 25 mm on a face next to the edge, which leaves only 20 mm (a "
                               "chamfer must leave at least 0.001 mm)");
        CHECK(regenerator.state(m.pad) == NodeState::UpToDate);
        CHECK(volumeMm3(regenerator, m.pad) == padVolume);
    }
    SECTION("a distance parameter that is an angle") {
        const ParameterId tilt = m.doc.createParameter("tilt", 5_deg, units::deg).value();
        ChamferDefinition d = m.definitionOf(m.edge);
        d.distanceParameter = tilt;
        m.setDefinition(m.edge, d);
        const Error error = requireFailure(regenerator, m.doc, m.edge);
        CHECK(error.code == ErrorCode::DimensionMismatch);
        CHECK_THAT(error.message, StartsWith("Edge: "));
    }
    SECTION("a missing distance parameter") {
        REQUIRE(m.doc.removeParameter(m.size).has_value());
        const Error error = requireFailure(regenerator, m.doc, m.edge);
        CHECK(error.code == ErrorCode::NotFound);
        CHECK(error.message == "object:7 references object:4, which does not exist");
    }
    SECTION("a missing target") {
        REQUIRE(m.doc.removeObject(m.pad).has_value());
        const Error error = requireFailure(regenerator, m.doc, m.edge);
        CHECK(error.code == ErrorCode::NotFound);
        CHECK(error.message == "object:7 references object:6, which does not exist");
    }
    SECTION("a target without a body") {
        ChamferDefinition d = m.definitionOf(m.edge);
        d.target = featureId(m.base); // a sketch
        m.setDefinition(m.edge, d);
        const Error error = requireFailure(regenerator, m.doc, m.edge);
        CHECK(error.code == ErrorCode::FailedPrecondition);
        CHECK(error.message == "Edge: a chamfer needs the body of its target feature");
    }
    SECTION("a target with an empty body") {
        // Eraser cuts the whole pad away (identical solids).
        auto eraser = ExtrudeFeature::create("Eraser", {.profile = ChamferBlockModel::sketchId(m.base),
                                                        .depthParameter = m.height,
                                                        .operation = FeatureOperation::Cut,
                                                        .target = featureId(m.pad)});
        REQUIRE(eraser.has_value());
        const ObjectId eraserId = m.doc.addObject(std::move(*eraser)).value();
        ChamferDefinition d = m.definitionOf(m.edge);
        d.target = featureId(eraserId);
        m.setDefinition(m.edge, d);
        const Error error = requireFailure(regenerator, m.doc, m.edge);
        REQUIRE(regenerator.body(eraserId) != nullptr);
        CHECK(regenerator.body(eraserId)->isEmpty());
        CHECK(error.code == ErrorCode::FailedPrecondition);
        CHECK(error.message == "Edge: a chamfer needs the body of its target feature");
    }
    SECTION("a target that fails blocks the chamfer") {
        REQUIRE(m.doc.setParameterValue(m.height, 0_mm).has_value());
        const RegenerationReport report = requireReport(regenerator, m.doc);
        INFO(describe(report));
        CHECK(report.failed == std::vector<ObjectId>{m.pad});
        CHECK(report.blocked == std::vector<ObjectId>{m.edge});
        CHECK(regenerator.state(m.edge) == NodeState::Blocked);
        CHECK(regenerator.body(m.edge) == nullptr);
    }
    SECTION("an edge that is not on the body") {
        ChamferDefinition d = m.definitionOf(m.edge);
        d.edges.push_back(ChamferBlockModel::alongX(0, 21));
        m.setDefinition(m.edge, d);
        const Error error = requireFailure(regenerator, m.doc, m.edge);
        CHECK(error.code == ErrorCode::NotFound);
        CHECK(error.message ==
              "Edge: chamfer: edge reference 2 (line through (0, 0, 21) mm along (1, 0, 0)) matches no edge of the body");
    }
    SECTION("an edge that an earlier chamfer removed") {
        const ObjectId again = m.addChamfer("Again", {.target = featureId(m.edge),
                                                      .edges = {ChamferBlockModel::alongX(0, 20)},
                                                      .distance = 1_mm});
        const Error error = requireFailure(regenerator, m.doc, again);
        CHECK(error.code == ErrorCode::NotFound);
        CHECK_THAT(error.message, StartsWith("Again: chamfer: edge reference 1 "));
        CHECK(regenerator.body(m.edge) != nullptr); // its target is intact
    }
    SECTION("a seam edge, which does not lie between two faces") {
        // The turned part's full revolution has a seam line on its cylinder, at x = 15.
        TurnedPartModel turned;
        auto feature = ChamferFeature::create(
            "Seam", {.target = featureId(turned.turn),
                     .edges = {geometry::lineSignature(Point3D{15_mm, 0_mm, 0_mm}, Direction3D::unitZ())},
                     .distance = 1_mm});
        REQUIRE(feature.has_value());
        const ObjectId seam = turned.doc.addObject(std::move(*feature)).value();
        Regenerator turnedRegenerator;
        const RegenerationReport report = requireReport(turnedRegenerator, turned.doc);
        REQUIRE(report.failed == std::vector<ObjectId>{seam});
        CHECK(report.errors.at(seam).code == ErrorCode::FailedPrecondition);
        CHECK_THAT(report.errors.at(seam).message, Catch::Matchers::ContainsSubstring("a chamfer needs an edge between two faces"));
    }
}

TEST_CASE("ChamferFeature_FailuresLeaveTheDocumentAndUpstreamBodiesIntact", "[chamfer][features][acceptance]") {
    ChamferBlockModel m;
    CommandHistory history;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const Document before = m.doc.clone();
    const std::uint64_t revision = m.doc.revision();
    const double padVolume = volumeMm3(regenerator, m.pad);
    const double chamferVolume = volumeMm3(regenerator, m.edge);

    // An invalid edit is refused before it touches the document.
    ChamferDefinition invalid = m.definitionOf(m.edge);
    invalid.edges.clear();
    CHECK(errorCode(history.execute(m.doc, std::make_unique<ModifyChamferCommand>(featureId(m.edge), invalid))) ==
          ErrorCode::InvalidArgument);
    CHECK(equivalent(m.doc, before));
    CHECK(m.doc.revision() == revision);
    CHECK_FALSE(history.canUndo());

    // A valid edit the kernel cannot build fails at regeneration only.
    ChamferDefinition tooLarge = m.definitionOf(m.edge);
    tooLarge.distanceParameter.reset();
    tooLarge.distance = 25_mm;
    REQUIRE(history.execute(m.doc, std::make_unique<ModifyChamferCommand>(featureId(m.edge), tooLarge)).has_value());
    const Document edited = m.doc.clone();
    const Error error = requireFailure(regenerator, m.doc, m.edge);
    CHECK(error.code == ErrorCode::FailedPrecondition);
    CHECK(equivalent(m.doc, edited)); // regeneration does not change the model
    CHECK(regenerator.state(m.pad) == NodeState::UpToDate);
    CHECK(volumeMm3(regenerator, m.pad) == padVolume);

    // Undo returns to the working model and its exact geometry.
    REQUIRE(history.undo(m.doc).has_value());
    CHECK(equivalent(m.doc, before));
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(volumeMm3(regenerator, m.edge) == chamferVolume);
}

TEST_CASE("ChamferFeature_UndoRedoRestoresIdenticalGeometry", "[chamfer][features][undo][acceptance]") {
    ChamferBlockModel m;
    CommandHistory history;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const Document initial = m.doc.clone();

    // A second chamfer, on the bottom back edge of the first one's result.
    auto create = std::make_unique<CreateChamferCommand>(
        "Bevel",
        ChamferDefinition{.target = featureId(m.edge), .edges = {ChamferBlockModel::alongX(50, 0)}, .distance = 4_mm});
    CreateChamferCommand* createRaw = create.get();
    CHECK(create->description() == "Create chamfer 'Bevel'");
    REQUIRE(history.execute(m.doc, std::move(create)).has_value());
    const ObjectId bevel{createRaw->featureId()};
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const double created = volumeMm3(regenerator, bevel);
    CHECK_THAT(created, WithinRel(98750.0 - 0.5 * 4.0 * 4.0 * 100.0, kRel));
    CHECK(resultFeatures(m.doc) == std::vector<ObjectId>{bevel});
    const Document afterCreate = m.doc.clone();

    ChamferDefinition smaller = m.definitionOf(bevel);
    smaller.distance = 2_mm;
    REQUIRE(history.execute(m.doc, std::make_unique<ModifyChamferCommand>(featureId(bevel), smaller)).has_value());
    CHECK(requireReport(regenerator, m.doc).regenerated == std::vector<ObjectId>{bevel});
    const double modified = volumeMm3(regenerator, bevel);
    CHECK_THAT(modified, WithinRel(98750.0 - 0.5 * 2.0 * 2.0 * 100.0, kRel));
    const Document afterModify = m.doc.clone();

    REQUIRE(history.undo(m.doc).has_value());
    CHECK(equivalent(m.doc, afterCreate));
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(volumeMm3(regenerator, bevel) == created);

    REQUIRE(history.redo(m.doc).has_value());
    CHECK(equivalent(m.doc, afterModify));
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(volumeMm3(regenerator, bevel) == modified);

    REQUIRE(history.undo(m.doc).has_value());
    REQUIRE(history.undo(m.doc).has_value());
    CHECK(m.doc.findObject(bevel) == nullptr);
    CHECK(equivalent(m.doc, initial));
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(regenerator.body(bevel) == nullptr);
    CHECK(resultFeatures(m.doc) == std::vector<ObjectId>{m.edge});

    REQUIRE(history.redo(m.doc).has_value());
    CHECK(equivalent(m.doc, afterCreate)); // recreated with the same ID
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(volumeMm3(regenerator, bevel) == created);

    // Commands check the feature kind.
    CHECK(errorCode(history.execute(m.doc, std::make_unique<ModifyChamferCommand>(featureId(m.pad), smaller))) ==
          ErrorCode::NotFound);
}
