#include "FeatureTestSupport.hpp"
#include "TestHelpers.hpp"
#include "core/geometry/GeometryTestSupport.hpp"
#include "support/BracketModel.hpp"
#include "support/FilletModels.hpp"
#include "support/TurnedPartModel.hpp"

#include <bettercad/core/document/Commands.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/geometry/Edges.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/FeatureCommands.hpp>
#include <bettercad/features/FilletFeature.hpp>
#include <bettercad/features/Regeneration.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/features/ResultBodies.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

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
using namespace bettercad::sketch;
using bettercad::geometry::EdgeCurve;
using bettercad::geometry::EdgeSignature;
using bettercad::test::addRectangle;
using bettercad::test::BlockModel;
using bettercad::test::describe;
using bettercad::test::errorCode;
using bettercad::test::FilletBlockModel;
using bettercad::test::filletCorner;
using bettercad::test::FilletedShaft;
using bettercad::test::FilletVariants;
using bettercad::test::kPositionToleranceMm;
using bettercad::test::requireReport;
using bettercad::test::TurnedPartModel;
using bettercad::test::volumeMm3;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::StartsWith;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

constexpr double pi = std::numbers::pi;
// Fillets of boxes and cylinders have planar, cylindrical, toroidal and
// spherical faces, whose properties the kernel computes to rounding level
// (measured in the geometry-level fillet tests, within 1e-13).
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

std::size_t edgesOn(const Regenerator& regenerator, ObjectId feature, const EdgeSignature& signature) {
    const geometry::Body* body = regenerator.body(feature);
    REQUIRE(body != nullptr);
    const auto found = geometry::findEdges(*body, signature);
    REQUIRE(found.has_value());
    return found->size();
}

std::uint64_t bits(double value) {
    return std::bit_cast<std::uint64_t>(value);
}

} // namespace

TEST_CASE("FilletFeature_DefinitionIsValidatedOnCreateAndEdit", "[fillet][features]") {
    const FilletDefinition good{.target = FeatureId::fromValue(2), .edges = {BlockModel::alongX(0, 20)}, .radius = 5_mm};
    REQUIRE(FilletFeature::create("F", good).has_value());

    const auto message = [](const FilletDefinition& definition) -> std::string {
        auto feature = FilletFeature::create("F", definition);
        REQUIRE_FALSE(feature.has_value());
        CHECK(feature.error().code == ErrorCode::InvalidArgument);
        return feature.error().message;
    };
    FilletDefinition d = good;

    SECTION("a target and at least one edge") {
        d.target = FeatureId{};
        CHECK(message(d) == "a fillet needs a target feature");
        d = good;
        d.edges.clear();
        CHECK(message(d) == "a fillet needs at least one edge");
    }
    SECTION("radii that are zero, negative, NaN or infinite") {
        for (const double mm : {0.0, -1.0, std::nan(""), std::numeric_limits<double>::infinity(),
                                -std::numeric_limits<double>::infinity()}) {
            CAPTURE(mm);
            d.radius = mm * units::mm;
            CHECK_THAT(message(d), StartsWith("the fillet radius must be positive and finite, got "));
        }
    }
    SECTION("a driving parameter replaces the literal radius") {
        d.radius = 0_mm;
        d.radiusParameter = ParameterId::fromValue(4);
        CHECK(FilletFeature::create("F", d).has_value());
        d.radiusParameter = ParameterId{};
        CHECK(message(d) == "the radius parameter ID must be valid");
    }
    SECTION("edge references") {
        d.edges.push_back(geometry::lineSignature(Point3D{42_mm, 0_mm, 20_mm}, Direction3D::unitX().reversed()));
        CHECK(message(d) == "edge references 1 and 2 refer to the same line through (0, 0, 20) mm along (1, 0, 0)");
        d.edges = {EdgeSignature{.curve = EdgeCurve::Other}};
        CHECK(message(d) == "edge reference 1: only line and circle edges can be referenced");
        d.edges = {EdgeSignature{.point = Point3D{Length::fromSi(std::nan("")), 0_mm, 0_mm}}};
        CHECK(message(d) == "edge reference 1: an edge reference needs a finite point");
    }
    SECTION("an invalid edit leaves the feature unchanged") {
        auto feature = FilletFeature::create("F", good);
        REQUIRE(feature.has_value());
        d.radius = -(5_mm);
        CHECK(errorCode((*feature)->setDefinition(d)) == ErrorCode::InvalidArgument);
        CHECK((*feature)->definition() == good);
        const auto unchanged = (*feature)->setDefinition(good);
        REQUIRE(unchanged.has_value());
        CHECK_FALSE(*unchanged);
    }
}

TEST_CASE("FilletFeature_DependsOnItsTargetAndRadiusParameter", "[fillet][features]") {
    FilletBlockModel m;
    const auto* round = m.doc.findObjectAs<FilletFeature>(m.round);
    REQUIRE(round != nullptr);
    CHECK(round->dependencies() == std::vector<ObjectId>{m.pad, ObjectId{m.radius}});
    CHECK(round->target() == featureId(m.pad));
    CHECK(round->typeName() == "fillet");
    CHECK(equivalent(*round->clone(), *round));

    FilletDefinition literal = m.definitionOf(m.round);
    literal.radiusParameter.reset();
    m.setDefinition(m.round, literal);
    CHECK(m.doc.findObjectAs<FilletFeature>(m.round)->dependencies() == std::vector<ObjectId>{m.pad});
}

TEST_CASE("FilletFeature_SingleStraightEdgeMatchesAnalyticVolume", "[fillet][features][acceptance]") {
    FilletBlockModel m;
    Regenerator regenerator;
    const RegenerationReport report = requireReport(regenerator, m.doc);
    INFO(describe(report));
    REQUIRE(report.succeeded());
    CHECK(report.regenerated == std::vector<ObjectId>{m.base, m.pad, m.round});

    const geometry::Body* body = regenerator.body(m.round);
    REQUIRE(body != nullptr);
    CHECK(body->isValid());
    CHECK(body->topology().solids == 1);
    CHECK(body->topology().faces == 7);
    // V = V0 - L r^2 (1 - pi/4) with V0 = 100 x 50 x 20, L = 100, r = 5.
    const double expected = FilletBlockModel::expectedVolume(100, 50, 20, 5);
    CHECK_THAT(expected, WithinRel(100000.0 - 2500.0 * (1.0 - pi / 4.0), 1e-15));
    const auto props = body->massProperties().value();
    CHECK_THAT(props.volume.in(units::mm3), WithinRel(expected, kRel));
    CHECK_THAT(props.surfaceArea.in(units::mm2),
               WithinRel(16000.0 - 1000.0 - 2.0 * filletCorner(5) + pi * 5.0 / 2.0 * 100.0, kRel));
    const auto box = body->boundingBox().value();
    CHECK_THAT(box.max.x.in(units::mm), WithinAbs(100.0, kPositionToleranceMm));
    CHECK_THAT(box.max.z.in(units::mm), WithinAbs(20.0, kPositionToleranceMm));

    // The edge is replaced by a cylinder tangent to the top and front faces
    // 5 mm from it.
    CHECK(edgesOn(regenerator, m.round, BlockModel::alongX(0, 20)) == 0);
    for (const EdgeSignature& seam : {BlockModel::alongX(5, 20), BlockModel::alongX(0, 15)}) {
        const auto found = geometry::findEdges(*body, seam);
        REQUIRE(found.has_value());
        REQUIRE(found->size() == 1);
        CHECK(found->front().faceAngle->si() < 1e-9);
    }
    // The target's body is an untouched intermediate; the fillet is the result.
    CHECK_THAT(volumeMm3(regenerator, m.pad), WithinRel(100000.0, kRel));
    CHECK(resultFeatures(m.doc) == std::vector<ObjectId>{m.round});
}

TEST_CASE("FilletFeature_MultipleAndAdjacentEdgesMatchAnalyticVolume", "[fillet][features][acceptance]") {
    FilletBlockModel m;
    FilletDefinition d = m.definitionOf(m.round);
    Regenerator regenerator;
    const double r = 5.0;
    const double overlap = r * r * r * (5.0 / 3.0 - pi / 2.0); // two corner regions meeting at a vertex

    SECTION("two edges that do not touch") {
        d.edges.push_back(BlockModel::alongX(50, 0));
        m.setDefinition(m.round, d);
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CHECK_THAT(volumeMm3(regenerator, m.round), WithinRel(100000.0 - 2 * 100.0 * filletCorner(r), kRel));
    }
    SECTION("two edges that meet at a corner") {
        d.edges.push_back(BlockModel::alongY(0, 20));
        m.setDefinition(m.round, d);
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CHECK_THAT(volumeMm3(regenerator, m.round), WithinRel(100000.0 - filletCorner(r) * (100 + 50) + overlap, kRel));
    }
    SECTION("three edges that meet at a vertex") {
        d.edges.push_back(BlockModel::alongY(0, 20));
        d.edges.push_back(BlockModel::alongZ(0, 0));
        m.setDefinition(m.round, d);
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CHECK_THAT(volumeMm3(regenerator, m.round),
                   WithinRel(100000.0 - filletCorner(r) * (170 - 3 * r) - r * r * r * (1.0 - pi / 6.0), kRel));
    }
    SECTION("the four edges around the top face") {
        d.edges.push_back(BlockModel::alongY(100, 20));
        d.edges.push_back(BlockModel::alongX(50, 20));
        d.edges.push_back(BlockModel::alongY(0, 20));
        m.setDefinition(m.round, d);
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CHECK_THAT(volumeMm3(regenerator, m.round),
                   WithinRel(100000.0 - filletCorner(r) * (2 * 100 + 2 * 50) + 4 * overlap, kRel));
        CHECK(regenerator.body(m.round)->isValid());
    }
}

TEST_CASE("FilletFeature_WorksOnRevolvedBody", "[fillet][features][revolve][acceptance]") {
    FilletedShaft m;
    Regenerator regenerator;
    const RegenerationReport report = requireReport(regenerator, m.doc);
    INFO(describe(report));
    REQUIRE(report.succeeded());
    CHECK(report.regenerated.back() == m.rims);
    CHECK(regenerator.body(m.rims)->isValid());
    CHECK_THAT(volumeMm3(regenerator, m.rims), WithinRel(FilletedShaft::expectedVolume(360), kRel));
    CHECK(resultFeatures(m.doc) == std::vector<ObjectId>{m.rims});
    // The rims now meet the top face tangentially, at radii 13 and 7.
    for (const Length radius : {13_mm, 7_mm}) {
        const auto circle = geometry::circleSignature(Point3D{0_mm, 0_mm, 40_mm}, Direction3D::unitZ(), radius);
        REQUIRE(circle.has_value());
        const auto found = geometry::findEdges(*regenerator.body(m.rims), *circle);
        REQUIRE(found.has_value());
        REQUIRE(found->size() == 1);
        CHECK(found->front().faceAngle->si() < 1e-9);
    }

    SECTION("a partial sweep keeps the rims on the same circles") {
        REQUIRE(m.doc.setParameterValue(m.sweep, 270_deg).has_value());
        const RegenerationReport partial = requireReport(regenerator, m.doc);
        INFO(describe(partial));
        CHECK(partial.regenerated == std::vector<ObjectId>{m.turn, m.boreCut, m.groove, m.rims});
        CHECK_THAT(volumeMm3(regenerator, m.rims), WithinRel(FilletedShaft::expectedVolume(270), kRel));
    }
    SECTION("a larger radius moves the outer rim off the referenced circle") {
        REQUIRE(m.doc.setParameterValue(m.radius, 20_mm).has_value());
        const Error error = requireFailure(regenerator, m.doc, m.rims);
        CHECK(error.code == ErrorCode::NotFound);
        CHECK(error.message == "Rims: fillet: edge reference 1 (circle around (0, 0, 40) mm with axis (0, 0, 1) and "
                               "radius 15 mm) matches no edge of the body");
    }
}

TEST_CASE("FilletFeature_RegeneratesAfterUpstreamDimensionChange", "[fillet][features][acceptance]") {
    FilletBlockModel m;
    CommandHistory history;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const double original = volumeMm3(regenerator, m.round);

    REQUIRE(history.execute(m.doc, ModifyParameterCommand::setValue(m.width, 120_mm)).has_value());
    const RegenerationReport report = requireReport(regenerator, m.doc);
    INFO(describe(report));
    CHECK(report.changed == std::vector<ObjectId>{ObjectId{m.width}});
    CHECK(report.regenerated == std::vector<ObjectId>{m.base, m.pad, m.round});
    // The top front edge is longer but still on the line y = 0, z = 20.
    CHECK_THAT(volumeMm3(regenerator, m.round), WithinRel(FilletBlockModel::expectedVolume(120, 50, 20, 5), kRel));
    CHECK_THAT(regenerator.body(m.round)->boundingBox()->max.x.in(units::mm), WithinAbs(120.0, kPositionToleranceMm));

    // Undo restores the width; the sketch is solved again from the 120 mm
    // rectangle, which reaches the original to rounding level, not bit for
    // bit (as for chamfers), so the volume matches to rounding level.
    REQUIRE(history.undo(m.doc).has_value());
    CHECK(requireReport(regenerator, m.doc).regenerated == std::vector<ObjectId>{m.base, m.pad, m.round});
    CHECK_THAT(volumeMm3(regenerator, m.round), WithinRel(original, kRel));
}

TEST_CASE("FilletFeature_RadiusParameterDrivesTheFillet", "[fillet][features][undo][acceptance]") {
    FilletBlockModel m;
    CommandHistory history;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    std::vector<double> volumes{volumeMm3(regenerator, m.round)}; // r = 5

    for (const double r : {2.0, 5.0, 8.0}) {
        CAPTURE(r);
        REQUIRE(history.execute(m.doc, ModifyParameterCommand::setValue(m.radius, r * units::mm)).has_value());
        const RegenerationReport report = requireReport(regenerator, m.doc);
        CHECK(report.regenerated == std::vector<ObjectId>{m.round}); // the pad is not rebuilt
        CHECK(regenerator.state(m.pad) == NodeState::UpToDate);
        volumes.push_back(volumeMm3(regenerator, m.round));
        CHECK_THAT(volumes.back(), WithinRel(FilletBlockModel::expectedVolume(100, 50, 20, r), kRel));
    }
    CHECK(bits(volumes[2]) == bits(volumes[0])); // back to 5 mm: the same geometry

    // Undo and redo step through the same geometry, bit for bit.
    for (std::size_t step = 3; step > 0; --step) {
        REQUIRE(history.undo(m.doc).has_value());
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CHECK(bits(volumeMm3(regenerator, m.round)) == bits(volumes[step - 1]));
    }
    for (std::size_t step = 1; step <= 3; ++step) {
        REQUIRE(history.redo(m.doc).has_value());
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CHECK(bits(volumeMm3(regenerator, m.round)) == bits(volumes[step]));
    }
}

TEST_CASE("FilletFeature_RefusesSubstitutionAfterTopologyChange", "[fillet][features][acceptance]") {
    FilletBlockModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());

    SECTION("a taller block moves the edge off the referenced line") {
        REQUIRE(m.doc.setParameterValue(m.height, 30_mm).has_value());
        const Error error = requireFailure(regenerator, m.doc, m.round);
        CHECK(error.code == ErrorCode::NotFound);
        CHECK(error.message ==
              "Round: fillet: edge reference 1 (line through (0, 0, 20) mm along (1, 0, 0)) matches no edge of the body");
        // The pad is rebuilt; its new top edge (z = 30) is not taken instead.
        CHECK_THAT(volumeMm3(regenerator, m.pad), WithinRel(150000.0, kRel));
        CHECK(edgesOn(regenerator, m.pad, BlockModel::alongX(0, 30)) == 1);
        REQUIRE(m.doc.setParameterValue(m.height, 20_mm).has_value());
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CHECK_THAT(volumeMm3(regenerator, m.round), WithinRel(FilletBlockModel::expectedVolume(100, 50, 20, 5), kRel));
    }
    SECTION("a notch that splits the edge makes the reference ambiguous; merged again, it resolves") {
        // A 10 x 10 mm notch at x = 45..55 in the front, cut upwards by notch_depth.
        const ParameterId notchDepth = m.doc.createParameter("notch_depth", 10_mm, units::mm).value();
        auto slot = std::make_unique<Sketch>("NotchSketch");
        addRectangle(*slot, 45_mm, -(1_mm), 10_mm, 11_mm);
        const ObjectId notchSketch = m.doc.addObject(std::move(slot)).value();
        const ObjectId notch = m.add<ExtrudeFeature>("Notch", {.profile = BlockModel::sketchId(notchSketch),
                                                                .depthParameter = notchDepth,
                                                                .operation = FeatureOperation::Cut,
                                                                .target = featureId(m.pad)});
        FilletDefinition d = m.definitionOf(m.round);
        d.target = featureId(notch);
        m.setDefinition(m.round, d);
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        const double whole = 100000.0 - 10.0 * 10.0 * 10.0 - 100.0 * filletCorner(5);
        CHECK_THAT(volumeMm3(regenerator, m.round), WithinRel(whole, kRel));

        REQUIRE(m.doc.setParameterValue(notchDepth, 25_mm).has_value());
        const Error error = requireFailure(regenerator, m.doc, m.round);
        CHECK(error.code == ErrorCode::FailedPrecondition);
        CHECK(error.message == "Round: fillet: edge reference 1 (line through (0, 0, 20) mm along (1, 0, 0)) is "
                               "ambiguous: 2 edges of the body lie on it");
        CHECK(edgesOn(regenerator, notch, BlockModel::alongX(0, 20)) == 2);

        REQUIRE(m.doc.setParameterValue(notchDepth, 10_mm).has_value());
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CHECK(edgesOn(regenerator, notch, BlockModel::alongX(0, 20)) == 1);
        CHECK_THAT(volumeMm3(regenerator, m.round), WithinRel(whole, kRel));
    }
    SECTION("an edge that an earlier fillet removed") {
        const ObjectId again =
            m.addFillet("Again", {.target = featureId(m.round), .edges = {BlockModel::alongX(0, 20)}, .radius = 1_mm});
        const Error error = requireFailure(regenerator, m.doc, again);
        CHECK(error.code == ErrorCode::NotFound);
        CHECK_THAT(error.message, StartsWith("Again: fillet: edge reference 1 "));
        CHECK(regenerator.body(m.round) != nullptr);
    }
}

TEST_CASE("FilletFeature_InvalidInputsFailWithStructuredDiagnostics", "[fillet][features][acceptance]") {
    FilletBlockModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const double padVolume = volumeMm3(regenerator, m.pad);

    SECTION("a radius parameter of zero or less") {
        for (const Length radius : {0_mm, -(2_mm)}) {
            REQUIRE(m.doc.setParameterValue(m.radius, radius).has_value());
            const Error error = requireFailure(regenerator, m.doc, m.round);
            CHECK(error.code == ErrorCode::InvalidArgument);
            CHECK_THAT(error.message, StartsWith("Round: fillet: the fillet radius must be positive and finite, got "));
        }
    }
    SECTION("a radius larger than the faces allow") {
        REQUIRE(m.doc.setParameterValue(m.radius, 25_mm).has_value()); // the block is 20 mm high
        const Error error = requireFailure(regenerator, m.doc, m.round);
        CHECK(error.code == ErrorCode::FailedPrecondition);
        CHECK(error.message == "Round: fillet: edge reference 1 (line through (0, 0, 20) mm along (1, 0, 0)) does not "
                               "fit: its fillet needs 25 mm on a face next to the edge, which leaves only 20 mm (a "
                               "fillet must leave at least 0.001 mm)");
        CHECK(regenerator.state(m.pad) == NodeState::UpToDate);
        CHECK(volumeMm3(regenerator, m.pad) == padVolume);
    }
    SECTION("a radius parameter that is an angle") {
        const ParameterId tilt = m.doc.createParameter("tilt", 5_deg, units::deg).value();
        FilletDefinition d = m.definitionOf(m.round);
        d.radiusParameter = tilt;
        m.setDefinition(m.round, d);
        const Error error = requireFailure(regenerator, m.doc, m.round);
        CHECK(error.code == ErrorCode::DimensionMismatch);
        CHECK_THAT(error.message, StartsWith("Round: "));
    }
    SECTION("a missing radius parameter") {
        REQUIRE(m.doc.removeParameter(m.radius).has_value());
        const Error error = requireFailure(regenerator, m.doc, m.round);
        CHECK(error.code == ErrorCode::NotFound);
        CHECK(error.message == "object:7 references object:4, which does not exist");
    }
    SECTION("a missing target") {
        REQUIRE(m.doc.removeObject(m.pad).has_value());
        const Error error = requireFailure(regenerator, m.doc, m.round);
        CHECK(error.code == ErrorCode::NotFound);
        CHECK(error.message == "object:7 references object:6, which does not exist");
    }
    SECTION("a target without a body") {
        FilletDefinition d = m.definitionOf(m.round);
        d.target = featureId(m.base); // a sketch
        m.setDefinition(m.round, d);
        const Error error = requireFailure(regenerator, m.doc, m.round);
        CHECK(error.code == ErrorCode::FailedPrecondition);
        CHECK(error.message == "Round: a fillet needs the body of its target feature");
    }
    SECTION("a target with an empty body") {
        const ObjectId eraser = m.add<ExtrudeFeature>("Eraser", {.profile = BlockModel::sketchId(m.base),
                                                                  .depthParameter = m.height,
                                                                  .operation = FeatureOperation::Cut,
                                                                  .target = featureId(m.pad)});
        FilletDefinition d = m.definitionOf(m.round);
        d.target = featureId(eraser);
        m.setDefinition(m.round, d);
        const Error error = requireFailure(regenerator, m.doc, m.round);
        REQUIRE(regenerator.body(eraser) != nullptr);
        CHECK(regenerator.body(eraser)->isEmpty());
        CHECK(error.message == "Round: a fillet needs the body of its target feature");
    }
    SECTION("a target that fails blocks the fillet") {
        REQUIRE(m.doc.setParameterValue(m.height, 0_mm).has_value());
        const RegenerationReport report = requireReport(regenerator, m.doc);
        INFO(describe(report));
        CHECK(report.failed == std::vector<ObjectId>{m.pad});
        CHECK(report.blocked == std::vector<ObjectId>{m.round});
        CHECK(regenerator.body(m.round) == nullptr);
    }
    SECTION("an edge that is not on the body") {
        FilletDefinition d = m.definitionOf(m.round);
        d.edges.push_back(BlockModel::alongX(0, 21));
        m.setDefinition(m.round, d);
        const Error error = requireFailure(regenerator, m.doc, m.round);
        CHECK(error.code == ErrorCode::NotFound);
        CHECK(error.message ==
              "Round: fillet: edge reference 2 (line through (0, 0, 21) mm along (1, 0, 0)) matches no edge of the body");
    }
    SECTION("a seam, which does not lie between two faces") {
        TurnedPartModel turned;
        auto feature = FilletFeature::create(
            "Seam", {.target = featureId(turned.turn),
                     .edges = {geometry::lineSignature(Point3D{15_mm, 0_mm, 0_mm}, Direction3D::unitZ())},
                     .radius = 1_mm});
        REQUIRE(feature.has_value());
        const ObjectId seam = turned.doc.addObject(std::move(*feature)).value();
        Regenerator turnedRegenerator;
        const RegenerationReport report = requireReport(turnedRegenerator, turned.doc);
        REQUIRE(report.failed == std::vector<ObjectId>{seam});
        CHECK(report.errors.at(seam).code == ErrorCode::FailedPrecondition);
        CHECK_THAT(report.errors.at(seam).message, ContainsSubstring("a fillet needs an edge between two faces"));
    }
}

TEST_CASE("FilletFeature_FailuresLeaveTheDocumentAndUpstreamBodiesIntact", "[fillet][features][acceptance]") {
    FilletBlockModel m;
    CommandHistory history;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const Document before = m.doc.clone();
    const std::uint64_t revision = m.doc.revision();
    const double padVolume = volumeMm3(regenerator, m.pad);
    const double filletVolume = volumeMm3(regenerator, m.round);

    // An invalid edit is refused before it touches the document.
    FilletDefinition invalid = m.definitionOf(m.round);
    invalid.edges.clear();
    CHECK(errorCode(history.execute(m.doc, std::make_unique<ModifyFilletCommand>(featureId(m.round), invalid))) ==
          ErrorCode::InvalidArgument);
    CHECK(equivalent(m.doc, before));
    CHECK(m.doc.revision() == revision);
    CHECK_FALSE(history.canUndo());

    // A valid edit the geometry cannot take fails at regeneration only.
    FilletDefinition tooLarge = m.definitionOf(m.round);
    tooLarge.radiusParameter.reset();
    tooLarge.radius = 25_mm;
    REQUIRE(history.execute(m.doc, std::make_unique<ModifyFilletCommand>(featureId(m.round), tooLarge)).has_value());
    const Document edited = m.doc.clone();
    const Error error = requireFailure(regenerator, m.doc, m.round);
    CHECK(error.code == ErrorCode::FailedPrecondition);
    CHECK(equivalent(m.doc, edited)); // regeneration does not change the model
    CHECK(regenerator.state(m.pad) == NodeState::UpToDate);
    CHECK(volumeMm3(regenerator, m.pad) == padVolume);

    // Undo returns to the working model and its exact geometry.
    REQUIRE(history.undo(m.doc).has_value());
    CHECK(equivalent(m.doc, before));
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(volumeMm3(regenerator, m.round) == filletVolume);
}

TEST_CASE("FilletFeature_UndoRedoRestoresGeometry", "[fillet][features][undo][acceptance]") {
    FilletBlockModel m;
    CommandHistory history;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const Document initial = m.doc.clone();
    const double roundVolume = volumeMm3(regenerator, m.round);

    // Create: the three edges meeting at the bottom back right vertex, 3 mm.
    const FilletDefinition three{.target = featureId(m.round),
                                 .edges = {BlockModel::alongX(50, 0), BlockModel::alongY(100, 0),
                                           BlockModel::alongZ(100, 50)},
                                 .radius = 3_mm};
    auto create = std::make_unique<CreateFilletCommand>("Corner", three);
    CreateFilletCommand* createRaw = create.get();
    CHECK(create->description() == "Create fillet 'Corner'");
    REQUIRE(history.execute(m.doc, std::move(create)).has_value());
    const ObjectId corner{createRaw->featureId()};
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const double created = volumeMm3(regenerator, corner);
    CHECK_THAT(created, WithinRel(FilletVariants::expectedVolume(5), kRel));
    CHECK(resultFeatures(m.doc) == std::vector<ObjectId>{corner});
    const Document afterCreate = m.doc.clone();

    // Modify the radius.
    FilletDefinition smaller = three;
    smaller.radius = 2_mm;
    REQUIRE(history.execute(m.doc, std::make_unique<ModifyFilletCommand>(featureId(corner), smaller)).has_value());
    CHECK(requireReport(regenerator, m.doc).regenerated == std::vector<ObjectId>{corner});
    const double r = 2.0;
    const double resized = volumeMm3(regenerator, corner);
    CHECK_THAT(resized, WithinRel(roundVolume - filletCorner(r) * (170 - 3 * r) - r * r * r * (1.0 - pi / 6.0), kRel));
    const Document afterResize = m.doc.clone();

    // Modify the edge set: drop the vertical edge, leaving two edges at the vertex.
    FilletDefinition two = smaller;
    two.edges.pop_back();
    REQUIRE(history.execute(m.doc, std::make_unique<ModifyFilletCommand>(featureId(corner), two)).has_value());
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const double reselected = volumeMm3(regenerator, corner);
    CHECK_THAT(reselected, WithinRel(roundVolume - filletCorner(r) * (100 + 50) + r * r * r * (5.0 / 3.0 - pi / 2.0), kRel));
    const Document afterReselect = m.doc.clone();

    REQUIRE(history.undo(m.doc).has_value());
    CHECK(equivalent(m.doc, afterResize));
    CHECK(m.doc.findObjectAs<FilletFeature>(corner)->definition() == smaller);
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(volumeMm3(regenerator, corner) == resized);

    REQUIRE(history.undo(m.doc).has_value());
    CHECK(equivalent(m.doc, afterCreate));
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(volumeMm3(regenerator, corner) == created);

    REQUIRE(history.redo(m.doc).has_value());
    REQUIRE(history.redo(m.doc).has_value());
    CHECK(equivalent(m.doc, afterReselect));
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(volumeMm3(regenerator, corner) == reselected);

    for (int i = 0; i < 3; ++i) {
        REQUIRE(history.undo(m.doc).has_value());
    }
    CHECK(m.doc.findObject(corner) == nullptr);
    CHECK(equivalent(m.doc, initial));
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(regenerator.body(corner) == nullptr);
    CHECK(resultFeatures(m.doc) == std::vector<ObjectId>{m.round});

    REQUIRE(history.redo(m.doc).has_value());
    CHECK(equivalent(m.doc, afterCreate)); // recreated with the same ID
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(volumeMm3(regenerator, corner) == created);

    // Commands check the feature kind.
    CHECK(errorCode(history.execute(m.doc, std::make_unique<ModifyFilletCommand>(featureId(m.pad), smaller))) ==
          ErrorCode::NotFound);
}

TEST_CASE("FilletFeature_RegenerationIsDeterministic", "[fillet][features]") {
    FilletVariants m;
    Regenerator first;
    Regenerator second;
    REQUIRE(requireReport(first, m.doc).succeeded());
    REQUIRE(requireReport(second, m.doc).succeeded());
    for (const ObjectId feature : {m.round, m.corner}) {
        const geometry::Body* a = first.body(feature);
        const geometry::Body* b = second.body(feature);
        REQUIRE(a != nullptr);
        REQUIRE(b != nullptr);
        const auto pa = a->massProperties().value();
        const auto pb = b->massProperties().value();
        CHECK(bits(pa.volume.si()) == bits(pb.volume.si()));
        CHECK(bits(pa.surfaceArea.si()) == bits(pb.surfaceArea.si()));
        CHECK(a->boundingBox().value() == b->boundingBox().value());
        CHECK(a->topology() == b->topology());
    }
    CHECK_THAT(volumeMm3(first, m.corner), WithinRel(FilletVariants::expectedVolume(5), kRel));
}
