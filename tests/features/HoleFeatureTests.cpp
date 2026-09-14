#include "FeatureTestSupport.hpp"
#include "TestHelpers.hpp"
#include "core/geometry/GeometryTestSupport.hpp"
#include "support/BracketModel.hpp"
#include "support/FilletModels.hpp"
#include "support/HoleModels.hpp"
#include "support/TurnedPartModel.hpp"

#include <bettercad/core/document/Commands.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/geometry/Edges.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/core/geometry/Hole.hpp>
#include <bettercad/features/ChamferFeature.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/FeatureCommands.hpp>
#include <bettercad/features/FilletFeature.hpp>
#include <bettercad/features/HoleFeature.hpp>
#include <bettercad/features/Regeneration.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/features/ResultBodies.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <numbers>
#include <utility>
#include <vector>

using namespace bettercad;
using namespace bettercad::features;
using namespace bettercad::literals;
using namespace bettercad::sketch;
using bettercad::geometry::HoleExtent;
using bettercad::geometry::HoleType;
using bettercad::test::BlockModel;
using bettercad::test::bottomFace;
using bettercad::test::BracketModel;
using bettercad::test::describe;
using bettercad::test::errorCode;
using bettercad::test::filletCorner;
using bettercad::test::HoleBlockModel;
using bettercad::test::HoleVariants;
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
// Holes in boxes and turned parts have planar, cylindrical and conical faces
// meeting in lines and circles, whose properties the kernel computes to
// rounding level, as for the geometry-level hole tests (kRelTight).
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

/// The circle of radius @p r mm around (x, y, z) mm with axis Z.
geometry::EdgeSignature rim(double x, double y, double z, double r) {
    const auto circle = geometry::circleSignature(Point3D{x * units::mm, y * units::mm, z * units::mm},
                                                  Direction3D::unitZ(), r * units::mm);
    REQUIRE(circle.has_value());
    return *circle;
}

std::size_t circlesOn(const Regenerator& regenerator, ObjectId feature, double x, double y, double z, double r) {
    const auto found = geometry::findEdges(requireBody(regenerator, feature), rim(x, y, z, r));
    REQUIRE(found.has_value());
    return found->size();
}

struct FacesOn {
    std::size_t count = 0;
    double areaMm2 = 0.0;
};

/// The faces of @p feature's body on @p face: how many, and their total area.
FacesOn facesOn(const Regenerator& regenerator, ObjectId feature, const geometry::FaceSignature& face) {
    const auto found = geometry::findFaces(requireBody(regenerator, feature), face);
    REQUIRE(found.has_value());
    FacesOn result{.count = found->size()};
    for (const geometry::FaceInfo& info : *found) {
        result.areaMm2 += info.area.in(units::mm2);
    }
    return result;
}

std::uint64_t bits(double value) {
    return std::bit_cast<std::uint64_t>(value);
}

/// A literal 10 mm through hole at (50, 25) in the block's top face.
HoleDefinition literalHole() {
    return {.target = FeatureId::fromValue(6), .face = topFace(), .center = Point2D{50_mm, 25_mm}, .diameter = 10_mm};
}

/// The error of creating a hole from @p definition, which must be refused.
Error refusal(const HoleDefinition& definition) {
    auto feature = HoleFeature::create("H", definition);
    REQUIRE_FALSE(feature.has_value());
    return feature.error();
}

} // namespace

TEST_CASE("HoleFeature_DefinitionIsValidatedOnCreateAndEdit", "[hole][features]") {
    const HoleDefinition good{.target = FeatureId::fromValue(2), .face = topFace(), .center = Point2D{50_mm, 25_mm},
                              .diameter = 10_mm};
    REQUIRE(HoleFeature::create("H", good).has_value());

    const auto message = [](const HoleDefinition& definition) -> std::string {
        auto feature = HoleFeature::create("H", definition);
        REQUIRE_FALSE(feature.has_value());
        CHECK(feature.error().code == ErrorCode::InvalidArgument);
        return feature.error().message;
    };
    const auto blind = [&](Length depth) {
        HoleDefinition b = good;
        b.extent = HoleExtent::Blind;
        b.depth = depth;
        return b;
    };
    HoleDefinition d = good;

    SECTION("a target, a planar face and a finite centre") {
        d.target = FeatureId{};
        CHECK(message(d) == "a hole needs a target feature");
        d = good;
        d.face = geometry::FaceSignature{.surface = geometry::FaceSurface::Cylinder};
        CHECK(message(d) == "placement face: only planar faces can be referenced, not a cylinder");
        d = good;
        d.center = Point2D{Length::fromSi(std::numeric_limits<double>::infinity()), 25_mm};
        CHECK(message(d) == "the hole centre must be finite");
    }
    SECTION("driving parameters replace the literal values") {
        d.diameter = 0_mm;
        d.diameterParameter = ParameterId::fromValue(4);
        d.center = Point2D{};
        d.centerUParameter = ParameterId::fromValue(7);
        d.centerVParameter = ParameterId::fromValue(8);
        CHECK(HoleFeature::create("H", d).has_value());
        HoleDefinition b = blind(0_mm);
        b.depthParameter = ParameterId::fromValue(5);
        CHECK(HoleFeature::create("H", b).has_value());
        d.centerVParameter = ParameterId{};
        CHECK(message(d) == "the hole's parameter IDs must be valid");
    }
    SECTION("head dimensions") {
        d.type = HoleType::Counterbore;
        d.counterboreDiameter = 8_mm;
        d.counterboreDepth = 4_mm;
        CHECK(message(d) == "the counterbore diameter must be larger than the hole diameter (10 mm), got 8 mm");
        // With a driven diameter the relation is checked at regeneration; the
        // literal head must still be positive.
        d.diameterParameter = ParameterId::fromValue(4);
        CHECK(HoleFeature::create("H", d).has_value());
        d.counterboreDiameter = -(8_mm);
        CHECK(message(d) == "the counterbore diameter must be positive and finite, got -8 mm");
        d = blind(4_mm);
        d.type = HoleType::Counterbore;
        d.counterboreDiameter = 16_mm;
        d.counterboreDepth = 4_mm;
        CHECK(message(d) == "the counterbore (4 mm deep) must be shallower than the blind hole (4 mm deep)");
        d = good;
        d.counterboreDepth = 4_mm;
        CHECK(message(d) == "only a counterbore hole takes counterbore dimensions");

        d = good;
        d.type = HoleType::Countersink;
        d.countersinkDiameter = 20_mm;
        d.countersinkAngle = 180_deg;
        CHECK_THAT(message(d), StartsWith("the countersink angle must be in (0, 180) deg, got "));
        d.countersinkAngle = 90_deg;
        d.extent = HoleExtent::Blind;
        d.depth = 5_mm; // the cone alone is 5 mm deep
        CHECK(message(d) == "the countersink (5 mm deep) must be shallower than the blind hole (5 mm deep)");
        d = good;
        d.countersinkAngle = 90_deg;
        CHECK(message(d) == "only a countersink hole takes countersink dimensions");
    }
    SECTION("an invalid edit leaves the feature unchanged") {
        auto feature = HoleFeature::create("H", good);
        REQUIRE(feature.has_value());
        d.diameter = -(10_mm);
        CHECK(errorCode((*feature)->setDefinition(d)) == ErrorCode::InvalidArgument);
        CHECK((*feature)->definition() == good);
        const auto unchanged = (*feature)->setDefinition(good);
        REQUIRE(unchanged.has_value());
        CHECK_FALSE(*unchanged);
    }
}

TEST_CASE("HoleFeature_DependsOnItsTargetAndParameters", "[hole][features]") {
    HoleBlockModel m;
    const auto* drill = m.doc.findObjectAs<HoleFeature>(m.drill);
    REQUIRE(drill != nullptr);
    CHECK(drill->dependencies() ==
          std::vector<ObjectId>{m.pad, ObjectId{m.diameter}, ObjectId{m.holeX}, ObjectId{m.holeY}});
    CHECK(drill->target() == featureId(m.pad));
    CHECK(drill->typeName() == "hole");
    CHECK(equivalent(*drill->clone(), *drill));

    // A blind hole's depth parameter; literal values depend on nothing.
    const ParameterId depth = m.doc.createParameter("hole_depth", 8_mm, units::mm).value();
    HoleDefinition d = m.definitionOf(m.drill);
    d.extent = HoleExtent::Blind;
    d.depthParameter = depth;
    d.diameterParameter.reset();
    d.centerUParameter.reset();
    d.centerVParameter.reset();
    m.setDefinition(m.drill, d);
    CHECK(m.doc.findObjectAs<HoleFeature>(m.drill)->dependencies() == std::vector<ObjectId>{m.pad, ObjectId{depth}});
}

TEST_CASE("HoleFeature_RejectsZeroDiameter", "[hole][features][acceptance]") {
    // As a literal, before it reaches the document...
    HoleDefinition d = literalHole();
    d.diameter = 0_mm;
    const Error refused = refusal(d);
    CHECK(refused.code == ErrorCode::InvalidArgument);
    CHECK(refused.message == "the hole diameter must be positive and finite, got 0 mm");

    // ...and as a driving parameter, at regeneration, leaving the pad intact.
    HoleBlockModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    REQUIRE(m.doc.setParameterValue(m.diameter, 0_mm).has_value());
    const Error error = requireFailure(regenerator, m.doc, m.drill);
    CHECK(error.code == ErrorCode::InvalidArgument);
    CHECK(error.message == "Drill: hole: the hole diameter must be positive and finite, got 0 mm");
    CHECK(regenerator.state(m.pad) == NodeState::UpToDate);
    CHECK_THAT(volumeMm3(regenerator, m.pad), WithinRel(100000.0, kRel));
}

TEST_CASE("HoleFeature_RejectsNegativeDiameter", "[hole][features][acceptance]") {
    HoleDefinition d = literalHole();
    d.diameter = -(10_mm);
    const Error refused = refusal(d);
    CHECK(refused.code == ErrorCode::InvalidArgument);
    CHECK(refused.message == "the hole diameter must be positive and finite, got -10 mm");

    HoleBlockModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    REQUIRE(m.doc.setParameterValue(m.diameter, -(2_mm)).has_value());
    const Error error = requireFailure(regenerator, m.doc, m.drill);
    CHECK(error.code == ErrorCode::InvalidArgument);
    CHECK(error.message == "Drill: hole: the hole diameter must be positive and finite, got -2 mm");
    CHECK(regenerator.state(m.pad) == NodeState::UpToDate);
}

TEST_CASE("HoleFeature_RejectsNonFiniteDiameter", "[hole][features][acceptance]") {
    for (const double mm : {std::nan(""), std::numeric_limits<double>::infinity(),
                            -std::numeric_limits<double>::infinity()}) {
        CAPTURE(mm);
        HoleDefinition d = literalHole();
        d.diameter = mm * units::mm;
        const Error refused = refusal(d);
        CHECK(refused.code == ErrorCode::InvalidArgument);
        CHECK_THAT(refused.message, StartsWith("the hole diameter must be positive and finite, got "));
    }
    // A driving parameter cannot become non-finite: the parameter refuses
    // the value and keeps its own, so the hole regenerates unchanged.
    HoleBlockModel m;
    for (const double bad : {std::nan(""), std::numeric_limits<double>::infinity()}) {
        CAPTURE(bad);
        CHECK(errorCode(m.doc.setParameterValue(m.diameter, Length::fromSi(bad))) == ErrorCode::InvalidArgument);
    }
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK_THAT(volumeMm3(regenerator, m.drill), WithinRel(HoleBlockModel::expectedVolume(100, 50, 20, 10), kRel));
}

TEST_CASE("HoleFeature_RejectsInvalidBlindDepth", "[hole][features][acceptance]") {
    HoleDefinition blind = literalHole();
    blind.extent = HoleExtent::Blind;
    for (const double mm : {0.0, -1.0, std::nan(""), std::numeric_limits<double>::infinity(),
                            -std::numeric_limits<double>::infinity()}) {
        CAPTURE(mm);
        blind.depth = mm * units::mm;
        const Error refused = refusal(blind);
        CHECK(refused.code == ErrorCode::InvalidArgument);
        CHECK_THAT(refused.message, StartsWith("the hole depth must be positive and finite, got "));
    }
    // A through hole takes no depth at all: it goes through all material.
    HoleDefinition through = literalHole();
    through.depth = 5_mm;
    CHECK(refusal(through).message == "a through hole takes no depth; it goes through all material");
    through.depth = 0_mm;
    through.depthParameter = ParameterId::fromValue(4);
    CHECK(refusal(through).message == "a through hole takes no depth parameter");

    // A driven blind hole from the bottom face, 10 mm deep.
    HoleBlockModel m;
    const ParameterId depth = m.doc.createParameter("hole_depth", 10_mm, units::mm).value();
    HoleDefinition d = m.definitionOf(m.drill);
    d.face = bottomFace();
    d.extent = HoleExtent::Blind;
    d.depthParameter = depth;
    m.setDefinition(m.drill, d);
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());

    SECTION("a depth parameter of zero or less") {
        for (const Length value : {0_mm, -(3_mm)}) {
            REQUIRE(m.doc.setParameterValue(depth, value).has_value());
            const Error error = requireFailure(regenerator, m.doc, m.drill);
            CHECK(error.code == ErrorCode::InvalidArgument);
            CHECK_THAT(error.message, StartsWith("Drill: hole: the hole depth must be positive and finite, got "));
        }
    }
    SECTION("a blind hole that would reach the far side is refused, not made through") {
        for (const Length tooDeep : {20_mm, 25_mm}) {
            REQUIRE(m.doc.setParameterValue(depth, tooDeep).has_value());
            const Error error = requireFailure(regenerator, m.doc, m.drill);
            CHECK(error.code == ErrorCode::FailedPrecondition);
            CHECK_THAT(error.message, StartsWith("Drill: hole: a blind hole "));
            CHECK_THAT(error.message, ContainsSubstring("would reach the far side: there are only 20 mm of material along "
                                                        "its axis; make it a through hole"));
            CHECK(regenerator.state(m.pad) == NodeState::UpToDate);
        }
        // In a 30 mm block, 20 mm is blind again.
        REQUIRE(m.doc.setParameterValue(depth, 20_mm).has_value());
        REQUIRE(m.doc.setParameterValue(m.height, 30_mm).has_value());
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CHECK_THAT(volumeMm3(regenerator, m.drill), WithinRel(150000.0 - pi * 25.0 * 20.0, kRel));
        CHECK_THAT(facesOn(regenerator, m.drill, topFace(30)).areaMm2, WithinRel(5000.0, kRel)); // not through
    }
}

TEST_CASE("HoleFeature_ThroughHoleMatchesAnalyticVolume", "[hole][features][acceptance]") {
    HoleBlockModel m;
    Regenerator regenerator;
    const RegenerationReport report = requireReport(regenerator, m.doc);
    INFO(describe(report));
    REQUIRE(report.succeeded());
    CHECK(report.regenerated == std::vector<ObjectId>{m.base, m.pad, m.drill});

    const geometry::Body& body = requireBody(regenerator, m.drill);
    CHECK(body.isValid());
    CHECK(body.topology().solids == 1);
    CHECK(body.topology().faces == 7);
    // V = L W H - pi r^2 H with 100 x 50 x 20 and d = 10.
    const double expected = HoleBlockModel::expectedVolume(100, 50, 20, 10);
    CHECK_THAT(expected, WithinRel(100000.0 - 500.0 * pi, 1e-15));
    const auto props = body.massProperties().value();
    CHECK_THAT(props.volume.in(units::mm3), WithinRel(expected, kRel));
    // Top and bottom lose a disc each; the bore adds 2 pi r H.
    CHECK_THAT(props.surfaceArea.in(units::mm2), WithinRel(16000.0 - 2.0 * 25.0 * pi + 2.0 * pi * 5.0 * 20.0, kRel));
    // Through: the bore opens into both the top and the bottom face.
    CHECK(circlesOn(regenerator, m.drill, 50, 25, 20, 5) == 1);
    CHECK(circlesOn(regenerator, m.drill, 50, 25, 0, 5) == 1);
    CHECK_THAT(facesOn(regenerator, m.drill, topFace()).areaMm2, WithinRel(5000.0 - 25.0 * pi, kRel));
    CHECK_THAT(facesOn(regenerator, m.drill, bottomFace()).areaMm2, WithinRel(5000.0 - 25.0 * pi, kRel));
    // The target's body is an untouched intermediate; the hole is the result.
    CHECK_THAT(volumeMm3(regenerator, m.pad), WithinRel(100000.0, kRel));
    CHECK(resultFeatures(m.doc) == std::vector<ObjectId>{m.drill});
}

TEST_CASE("HoleFeature_BlindHoleMatchesAnalyticVolume", "[hole][features][acceptance]") {
    HoleBlockModel m;
    HoleDefinition d = m.definitionOf(m.drill);
    d.extent = HoleExtent::Blind;
    d.depth = 8_mm;
    m.setDefinition(m.drill, d);
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());

    // V = V0 - pi r^2 h with h = 8: a flat floor 8 mm below the top face.
    const auto props = requireBody(regenerator, m.drill).massProperties().value();
    CHECK_THAT(props.volume.in(units::mm3), WithinRel(100000.0 - pi * 25.0 * 8.0, kRel));
    CHECK_THAT(props.surfaceArea.in(units::mm2), WithinRel(16000.0 + 2.0 * pi * 5.0 * 8.0, kRel));
    const FacesOn floor = facesOn(regenerator, m.drill, topFace(12));
    CHECK(floor.count == 1);
    CHECK_THAT(floor.areaMm2, WithinRel(25.0 * pi, kRel));
    CHECK(circlesOn(regenerator, m.drill, 50, 25, 0, 5) == 0); // not through
    CHECK_THAT(facesOn(regenerator, m.drill, bottomFace()).areaMm2, WithinRel(5000.0, kRel));

    SECTION("from the bottom face the same hole goes up, into the material") {
        d.face = bottomFace();
        m.setDefinition(m.drill, d);
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CHECK_THAT(volumeMm3(regenerator, m.drill), WithinRel(100000.0 - pi * 25.0 * 8.0, kRel));
        CHECK(facesOn(regenerator, m.drill, bottomFace(8)).count == 1); // its end faces down, 8 mm up
        CHECK(facesOn(regenerator, m.drill, topFace(12)).count == 0);
        CHECK_THAT(facesOn(regenerator, m.drill, topFace()).areaMm2, WithinRel(5000.0, kRel));
    }
}

TEST_CASE("HoleFeature_ThroughHoleRemainsThroughAfterThicknessChange", "[hole][features][undo][acceptance]") {
    // The hole starts on the bottom face, the pad's start plane, which stays
    // at z = 0 whatever the thickness; the pad's top face moves with it.
    HoleBlockModel m;
    HoleDefinition d = m.definitionOf(m.drill);
    d.face = bottomFace();
    m.setDefinition(m.drill, d);
    CommandHistory history;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const double original = volumeMm3(regenerator, m.drill);
    CHECK_THAT(original, WithinRel(HoleBlockModel::expectedVolume(100, 50, 20, 10), kRel));
    CHECK(circlesOn(regenerator, m.drill, 50, 25, 20, 5) == 1);

    // Thicker: the hole is rebuilt through all 40 mm, not left 20 mm deep.
    REQUIRE(history.execute(m.doc, ModifyParameterCommand::setValue(m.height, 40_mm)).has_value());
    const RegenerationReport report = requireReport(regenerator, m.doc);
    INFO(describe(report));
    CHECK(report.regenerated == std::vector<ObjectId>{m.pad, m.drill});
    CHECK(requireBody(regenerator, m.drill).isValid());
    CHECK(requireBody(regenerator, m.drill).topology().faces == 7);
    CHECK_THAT(volumeMm3(regenerator, m.drill), WithinRel(HoleBlockModel::expectedVolume(100, 50, 40, 10), kRel));
    CHECK(circlesOn(regenerator, m.drill, 50, 25, 0, 5) == 1);
    CHECK(circlesOn(regenerator, m.drill, 50, 25, 40, 5) == 1); // it leaves through the new top face
    CHECK_THAT(facesOn(regenerator, m.drill, topFace(40)).areaMm2, WithinRel(5000.0 - 25.0 * pi, kRel));
    CHECK(facesOn(regenerator, m.drill, topFace(20)).count == 0); // no floor where the old top was

    // Thinner: still through.
    REQUIRE(history.execute(m.doc, ModifyParameterCommand::setValue(m.height, 5_mm)).has_value());
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK_THAT(volumeMm3(regenerator, m.drill), WithinRel(HoleBlockModel::expectedVolume(100, 50, 5, 10), kRel));
    CHECK(circlesOn(regenerator, m.drill, 50, 25, 5, 5) == 1);

    // Undo returns through 40 mm to 20 mm, and the hole with it.
    REQUIRE(history.undo(m.doc).has_value());
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK_THAT(volumeMm3(regenerator, m.drill), WithinRel(HoleBlockModel::expectedVolume(100, 50, 40, 10), kRel));
    REQUIRE(history.undo(m.doc).has_value());
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(volumeMm3(regenerator, m.drill) == original);
}

TEST_CASE("HoleFeature_BlindHoleKeepsItsDepthWhenTheBodyChanges", "[hole][features][acceptance]") {
    HoleBlockModel m;
    const ParameterId depth = m.doc.createParameter("hole_depth", 10_mm, units::mm).value();
    HoleDefinition d = m.definitionOf(m.drill);
    d.face = bottomFace();
    d.extent = HoleExtent::Blind;
    d.depthParameter = depth;
    m.setDefinition(m.drill, d);
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK_THAT(volumeMm3(regenerator, m.drill), WithinRel(100000.0 - pi * 25.0 * 10.0, kRel));
    CHECK(facesOn(regenerator, m.drill, bottomFace(10)).count == 1);

    SECTION("a thicker block: still 10 mm deep") {
        REQUIRE(m.doc.setParameterValue(m.height, 30_mm).has_value());
        const RegenerationReport report = requireReport(regenerator, m.doc);
        INFO(describe(report));
        CHECK(report.regenerated == std::vector<ObjectId>{m.pad, m.drill});
        CHECK_THAT(volumeMm3(regenerator, m.drill), WithinRel(150000.0 - pi * 25.0 * 10.0, kRel));
        CHECK(facesOn(regenerator, m.drill, bottomFace(10)).count == 1);
        CHECK_THAT(facesOn(regenerator, m.drill, topFace(30)).areaMm2, WithinRel(5000.0, kRel)); // not through
    }
    SECTION("a deeper hole: only the hole is rebuilt") {
        REQUIRE(m.doc.setParameterValue(depth, 15_mm).has_value());
        const RegenerationReport report = requireReport(regenerator, m.doc);
        CHECK(report.regenerated == std::vector<ObjectId>{m.drill});
        CHECK_THAT(volumeMm3(regenerator, m.drill), WithinRel(100000.0 - pi * 25.0 * 15.0, kRel));
        CHECK(facesOn(regenerator, m.drill, bottomFace(15)).count == 1);
        CHECK(facesOn(regenerator, m.drill, bottomFace(10)).count == 0);
    }
}

TEST_CASE("HoleFeature_RegeneratesAfterDiameterChange", "[hole][features][undo][acceptance]") {
    HoleBlockModel m;
    CommandHistory history;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    std::vector<double> volumes{volumeMm3(regenerator, m.drill)}; // d = 10

    for (const double diameter : {5.0, 10.0, 15.0}) {
        CAPTURE(diameter);
        REQUIRE(history.execute(m.doc, ModifyParameterCommand::setValue(m.diameter, diameter * units::mm)).has_value());
        const RegenerationReport report = requireReport(regenerator, m.doc);
        CHECK(report.regenerated == std::vector<ObjectId>{m.drill}); // the pad is not rebuilt
        CHECK(regenerator.state(m.pad) == NodeState::UpToDate);
        volumes.push_back(volumeMm3(regenerator, m.drill));
        CHECK_THAT(volumes.back(), WithinRel(HoleBlockModel::expectedVolume(100, 50, 20, diameter), kRel));
        CHECK(circlesOn(regenerator, m.drill, 50, 25, 20, diameter / 2.0) == 1);
        CHECK(circlesOn(regenerator, m.drill, 50, 25, 0, diameter / 2.0) == 1);
    }
    CHECK(bits(volumes[2]) == bits(volumes[0])); // back to 10 mm: the same geometry

    // Undo and redo step through the same geometry, bit for bit.
    for (std::size_t step = 3; step > 0; --step) {
        REQUIRE(history.undo(m.doc).has_value());
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CHECK(bits(volumeMm3(regenerator, m.drill)) == bits(volumes[step - 1]));
    }
    for (std::size_t step = 1; step <= 3; ++step) {
        REQUIRE(history.redo(m.doc).has_value());
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CHECK(bits(volumeMm3(regenerator, m.drill)) == bits(volumes[step]));
    }
}

TEST_CASE("HoleFeature_RegeneratesAfterPositionChange", "[hole][features][undo][acceptance]") {
    HoleBlockModel m;
    CommandHistory history;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const double expected = HoleBlockModel::expectedVolume(100, 50, 20, 10);

    for (const double x : {25.0, 35.0}) {
        CAPTURE(x);
        REQUIRE(history.execute(m.doc, ModifyParameterCommand::setValue(m.holeX, x * units::mm)).has_value());
        const RegenerationReport report = requireReport(regenerator, m.doc);
        CHECK(report.regenerated == std::vector<ObjectId>{m.drill});
        CHECK_THAT(volumeMm3(regenerator, m.drill), WithinRel(expected, kRel));
        CHECK(circlesOn(regenerator, m.drill, x, 25, 20, 5) == 1);
        CHECK(circlesOn(regenerator, m.drill, x, 25, 0, 5) == 1);
        CHECK(circlesOn(regenerator, m.drill, 50, 25, 20, 5) == 0);
        const auto centre = requireBody(regenerator, m.drill).massProperties()->centerOfMass;
        // The hole moved left of the middle, so the centre of mass is right of it:
        // x_c = (V0 50 - V_hole x) / (V0 - V_hole).
        const double hole = 500.0 * pi;
        CHECK_THAT(centre.x.in(units::mm), WithinRel((100000.0 * 50.0 - hole * x) / (100000.0 - hole), kRel));
    }
    REQUIRE(history.execute(m.doc, ModifyParameterCommand::setValue(m.holeY, 10_mm)).has_value());
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(circlesOn(regenerator, m.drill, 35, 10, 20, 5) == 1);

    SECTION("near an edge: a thin wall is kept") {
        REQUIRE(m.doc.setParameterValue(m.holeX, 94.5_mm).has_value()); // 0.5 mm from x = 100
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CHECK_THAT(volumeMm3(regenerator, m.drill), WithinRel(expected, kRel));
        CHECK_THAT(requireBody(regenerator, m.drill).boundingBox()->max.x.in(units::mm),
                   WithinAbs(100.0, bettercad::test::kPositionToleranceMm));
    }
    SECTION("a hole that would break out of the face's side is refused") {
        REQUIRE(m.doc.setParameterValue(m.holeX, 97_mm).has_value());
        const Error error = requireFailure(regenerator, m.doc, m.drill);
        CHECK(error.code == ErrorCode::FailedPrecondition);
        CHECK(error.message == "Drill: hole: the hole does not fit on its face: its entry is 10 mm across, but the "
                               "centre (97, 10) mm is only 3 mm from the face's edge (a hole must stay at least 0.001 "
                               "mm inside its face)");
    }
    SECTION("a centre off the face is refused") {
        REQUIRE(m.doc.setParameterValue(m.holeX, 150_mm).has_value());
        const Error error = requireFailure(regenerator, m.doc, m.drill);
        CHECK(error.code == ErrorCode::FailedPrecondition);
        CHECK(error.message == "Drill: hole: the centre (150, 10) mm is not on a face of the body on the plane through "
                               "(0, 0, 20) mm facing (0, 0, 1) (1 face(s) lie on that plane elsewhere)");
    }
    SECTION("on a side face the coordinates are (x, z) and the hole goes through the block's length") {
        HoleDefinition d = m.definitionOf(m.drill);
        d.face = geometry::planeSignature(Point3D{}, Direction3D::unitY().reversed()); // the front face, y = 0
        d.centerUParameter.reset();
        d.centerVParameter.reset();
        d.center = Point2D{30_mm, 10_mm};
        m.setDefinition(m.drill, d);
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CHECK_THAT(volumeMm3(regenerator, m.drill), WithinRel(100000.0 - 25.0 * pi * 50.0, kRel));
        // Longer: still through, along +Y.
        REQUIRE(m.doc.setParameterValue(m.length, 80_mm).has_value());
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CHECK_THAT(volumeMm3(regenerator, m.drill), WithinRel(160000.0 - 25.0 * pi * 80.0, kRel));
    }
}

TEST_CASE("HoleFeature_DoesNotSubstituteFaceAfterTopologyChange", "[hole][features][acceptance]") {
    HoleBlockModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const double original = volumeMm3(regenerator, m.drill);

    SECTION("a thicker block moves the top face off the referenced plane") {
        REQUIRE(m.doc.setParameterValue(m.height, 30_mm).has_value());
        const Error error = requireFailure(regenerator, m.doc, m.drill);
        CHECK(error.code == ErrorCode::NotFound);
        CHECK(error.message == "Drill: hole: the placement face (plane through (0, 0, 20) mm facing (0, 0, 1)) matches "
                               "no face of the body");
        // The pad is rebuilt; its new top face (z = 30) is not taken instead.
        CHECK_THAT(volumeMm3(regenerator, m.pad), WithinRel(150000.0, kRel));
        CHECK(facesOn(regenerator, m.pad, topFace(30)).count == 1);
        REQUIRE(m.doc.setParameterValue(m.height, 20_mm).has_value());
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CHECK(volumeMm3(regenerator, m.drill) == original);
    }
    SECTION("a narrower block leaves the centre off the face, or too close to its edge") {
        REQUIRE(m.doc.setParameterValue(m.width, 40_mm).has_value());
        Error error = requireFailure(regenerator, m.doc, m.drill);
        CHECK(error.code == ErrorCode::FailedPrecondition);
        CHECK(error.message == "Drill: hole: the centre (50, 25) mm is not on a face of the body on the plane through "
                               "(0, 0, 20) mm facing (0, 0, 1) (1 face(s) lie on that plane elsewhere)");
        REQUIRE(m.doc.setParameterValue(m.width, 52_mm).has_value());
        error = requireFailure(regenerator, m.doc, m.drill);
        CHECK(error.code == ErrorCode::FailedPrecondition);
        CHECK_THAT(error.message, ContainsSubstring("is only 2 mm from the face's edge"));
        REQUIRE(m.doc.setParameterValue(m.width, 100_mm).has_value());
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CHECK_THAT(volumeMm3(regenerator, m.drill), WithinRel(original, kRel));
    }
    SECTION("a centre that an earlier hole has removed") {
        const ObjectId again = m.addHole("Again", {.target = featureId(m.drill), .face = topFace(),
                                                   .center = Point2D{50_mm, 25_mm}, .diameter = 6_mm});
        const Error error = requireFailure(regenerator, m.doc, again);
        CHECK(error.code == ErrorCode::FailedPrecondition);
        CHECK(error.message == "Again: hole: the centre (50, 25) mm is not on a face of the body on the plane through "
                               "(0, 0, 20) mm facing (0, 0, 1) (1 face(s) lie on that plane elsewhere)");
        CHECK(regenerator.body(m.drill) != nullptr);
    }
}

TEST_CASE("HoleFeature_RejectsMissingFace", "[hole][features][acceptance]") {
    HoleBlockModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    HoleDefinition d = m.definitionOf(m.drill);

    SECTION("a plane the body has no face on") {
        d.face = topFace(25);
        m.setDefinition(m.drill, d);
        const Error error = requireFailure(regenerator, m.doc, m.drill);
        CHECK(error.code == ErrorCode::NotFound);
        CHECK(error.message == "Drill: hole: the placement face (plane through (0, 0, 25) mm facing (0, 0, 1)) matches "
                               "no face of the body");
        CHECK(regenerator.state(m.pad) == NodeState::UpToDate);
    }
    SECTION("the top face's plane, facing into the material") {
        d.face = bottomFace(20);
        m.setDefinition(m.drill, d);
        const Error error = requireFailure(regenerator, m.doc, m.drill);
        CHECK(error.code == ErrorCode::NotFound);
        CHECK(error.message == "Drill: hole: the placement face (plane through (0, 0, 20) mm facing (0, 0, -1)) "
                               "matches no face of the body");
    }
    SECTION("a curved face cannot be referenced at all") {
        d.face = geometry::FaceSignature{.surface = geometry::FaceSurface::Cylinder};
        const Error refused = refusal(d);
        CHECK(refused.code == ErrorCode::InvalidArgument);
        CHECK(refused.message == "placement face: only planar faces can be referenced, not a cylinder");
    }
}

TEST_CASE("HoleFeature_RejectsAmbiguousFaceReference", "[hole][features][acceptance]") {
    // An outline with a redundant corner at (50, 0): the slab's front face
    // comes out as two coplanar faces that meet at x = 50.
    Document doc{"Split"};
    auto outline = std::make_unique<Sketch>("Outline");
    const std::array<std::pair<double, double>, 5> xy{{{0, 0}, {50, 0}, {100, 0}, {100, 50}, {0, 50}}};
    std::array<EntityId, 5> corners{};
    for (std::size_t i = 0; i < corners.size(); ++i) {
        corners[i] =
            bettercad::test::require(outline->addPoint(Point2D{xy[i].first * units::mm, xy[i].second * units::mm}));
    }
    for (std::size_t i = 0; i < corners.size(); ++i) {
        bettercad::test::require(outline->addLine(corners[i], corners[(i + 1) % corners.size()]));
    }
    const ObjectId outlineId = doc.addObject(std::move(outline)).value();
    auto slab = ExtrudeFeature::create("Slab", {.profile = SketchId::fromValue(outlineId.value()), .depth = 20_mm});
    REQUIRE(slab.has_value());
    const ObjectId slabId = doc.addObject(std::move(*slab)).value();
    const geometry::FaceSignature front = geometry::planeSignature(Point3D{}, Direction3D::unitY().reversed());
    auto probe = HoleFeature::create("Probe", {.target = featureId(slabId), .face = front,
                                               .center = Point2D{50_mm, 10_mm}, .diameter = 6_mm});
    REQUIRE(probe.has_value());
    const ObjectId probeId = doc.addObject(std::move(*probe)).value();

    Regenerator regenerator;
    const Error error = requireFailure(regenerator, doc, probeId);
    CHECK(error.code == ErrorCode::FailedPrecondition);
    CHECK(error.message == "Probe: hole: the placement is ambiguous: the centre (50, 10) mm lies on 2 faces on the "
                           "plane through (0, 0, 0) mm facing (0, -1, 0)");
    CHECK(facesOn(regenerator, slabId, front).count == 2);
    // Away from the split, the one face under the centre is used.
    REQUIRE(doc.modifyObject<HoleFeature>(probeId, [](HoleFeature& f) {
                   HoleDefinition moved = f.definition();
                   moved.center = Point2D{25_mm, 10_mm};
                   return f.setDefinition(moved);
               }).has_value());
    REQUIRE(requireReport(regenerator, doc).succeeded());
    CHECK_THAT(volumeMm3(regenerator, probeId), WithinRel(100000.0 - 9.0 * pi * 50.0, kRel));
}

TEST_CASE("HoleFeature_InvalidInputsFailWithStructuredDiagnostics", "[hole][features][acceptance]") {
    HoleBlockModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const double padVolume = volumeMm3(regenerator, m.pad);

    SECTION("a diameter parameter of zero or less") {
        for (const Length diameter : {0_mm, -(2_mm)}) {
            REQUIRE(m.doc.setParameterValue(m.diameter, diameter).has_value());
            const Error error = requireFailure(regenerator, m.doc, m.drill);
            CHECK(error.code == ErrorCode::InvalidArgument);
            CHECK_THAT(error.message, StartsWith("Drill: hole: the hole diameter must be positive and finite, got "));
        }
    }
    SECTION("a diameter larger than the face") {
        REQUIRE(m.doc.setParameterValue(m.diameter, 60_mm).has_value()); // the top face is 50 mm deep
        const Error error = requireFailure(regenerator, m.doc, m.drill);
        CHECK(error.code == ErrorCode::FailedPrecondition);
        CHECK(error.message == "Drill: hole: the hole does not fit on its face: its entry is 60 mm across, but the "
                               "centre (50, 25) mm is only 25 mm from the face's edge (a hole must stay at least 0.001 "
                               "mm inside its face)");
        CHECK(regenerator.state(m.pad) == NodeState::UpToDate);
        CHECK(volumeMm3(regenerator, m.pad) == padVolume);
    }
    SECTION("a driven diameter that reaches its counterbore") {
        HoleDefinition d = m.definitionOf(m.drill);
        d.type = HoleType::Counterbore;
        d.counterboreDiameter = 16_mm;
        d.counterboreDepth = 4_mm;
        m.setDefinition(m.drill, d);
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        REQUIRE(m.doc.setParameterValue(m.diameter, 16_mm).has_value());
        const Error error = requireFailure(regenerator, m.doc, m.drill);
        CHECK(error.code == ErrorCode::InvalidArgument);
        CHECK(error.message ==
              "Drill: hole: the counterbore diameter must be larger than the hole diameter (16 mm), got 16 mm");
    }
    SECTION("a diameter parameter that is an angle") {
        const ParameterId tilt = m.doc.createParameter("tilt", 5_deg, units::deg).value();
        HoleDefinition d = m.definitionOf(m.drill);
        d.diameterParameter = tilt;
        m.setDefinition(m.drill, d);
        const Error error = requireFailure(regenerator, m.doc, m.drill);
        CHECK(error.code == ErrorCode::DimensionMismatch);
        CHECK_THAT(error.message, StartsWith("Drill: "));
    }
    SECTION("a missing diameter parameter") {
        REQUIRE(m.doc.removeParameter(m.diameter).has_value());
        const Error error = requireFailure(regenerator, m.doc, m.drill);
        CHECK(error.code == ErrorCode::NotFound);
        CHECK(error.message == "object:9 references object:4, which does not exist");
    }
    SECTION("a missing target") {
        REQUIRE(m.doc.removeObject(m.pad).has_value());
        const Error error = requireFailure(regenerator, m.doc, m.drill);
        CHECK(error.code == ErrorCode::NotFound);
        CHECK(error.message == "object:9 references object:6, which does not exist");
    }
    SECTION("a target without a body") {
        HoleDefinition d = m.definitionOf(m.drill);
        d.target = featureId(m.base); // a sketch
        m.setDefinition(m.drill, d);
        const Error error = requireFailure(regenerator, m.doc, m.drill);
        CHECK(error.code == ErrorCode::FailedPrecondition);
        CHECK(error.message == "Drill: a hole needs the body of its target feature");
    }
    SECTION("a target that fails blocks the hole") {
        REQUIRE(m.doc.setParameterValue(m.height, 0_mm).has_value());
        const RegenerationReport report = requireReport(regenerator, m.doc);
        INFO(describe(report));
        CHECK(report.failed == std::vector<ObjectId>{m.pad});
        CHECK(report.blocked == std::vector<ObjectId>{m.drill});
        CHECK(regenerator.body(m.drill) == nullptr);
    }
}

TEST_CASE("HoleFeature_FailuresLeaveTheDocumentAndUpstreamBodiesIntact", "[hole][features][acceptance]") {
    HoleBlockModel m;
    CommandHistory history;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const Document before = m.doc.clone();
    const std::uint64_t revision = m.doc.revision();
    const double padVolume = volumeMm3(regenerator, m.pad);
    const double holeVolume = volumeMm3(regenerator, m.drill);

    // An invalid edit is refused before it touches the document.
    HoleDefinition invalid = m.definitionOf(m.drill);
    invalid.diameterParameter.reset();
    invalid.diameter = -(10_mm);
    CHECK(errorCode(history.execute(m.doc, std::make_unique<ModifyHoleCommand>(featureId(m.drill), invalid))) ==
          ErrorCode::InvalidArgument);
    CHECK(equivalent(m.doc, before));
    CHECK(m.doc.revision() == revision);
    CHECK_FALSE(history.canUndo());

    // A valid edit the geometry cannot take fails at regeneration only.
    HoleDefinition tooLarge = m.definitionOf(m.drill);
    tooLarge.diameterParameter.reset();
    tooLarge.diameter = 60_mm;
    REQUIRE(history.execute(m.doc, std::make_unique<ModifyHoleCommand>(featureId(m.drill), tooLarge)).has_value());
    const Document edited = m.doc.clone();
    const Error error = requireFailure(regenerator, m.doc, m.drill);
    CHECK(error.code == ErrorCode::FailedPrecondition);
    CHECK(equivalent(m.doc, edited)); // regeneration does not change the model
    CHECK(regenerator.state(m.pad) == NodeState::UpToDate);
    CHECK(volumeMm3(regenerator, m.pad) == padVolume);

    // Undo returns to the working model and its exact geometry.
    REQUIRE(history.undo(m.doc).has_value());
    CHECK(equivalent(m.doc, before));
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(volumeMm3(regenerator, m.drill) == holeVolume);
}

TEST_CASE("HoleFeature_UndoRedoRestoresGeometry", "[hole][features][undo][acceptance]") {
    HoleBlockModel m;
    CommandHistory history;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const Document initial = m.doc.clone();
    const double drilled = volumeMm3(regenerator, m.drill);

    // Create: a blind 6 mm hole, 8 mm deep, at (20, 25).
    const HoleDefinition pocket{.target = featureId(m.drill), .face = topFace(), .center = Point2D{20_mm, 25_mm},
                                .extent = HoleExtent::Blind, .diameter = 6_mm, .depth = 8_mm};
    auto create = std::make_unique<CreateHoleCommand>("Pocket", pocket);
    CreateHoleCommand* createRaw = create.get();
    CHECK(create->description() == "Create hole 'Pocket'");
    REQUIRE(history.execute(m.doc, std::move(create)).has_value());
    const ObjectId added{createRaw->featureId()};
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const double created = volumeMm3(regenerator, added);
    CHECK_THAT(created, WithinRel(drilled - pi * 9.0 * 8.0, kRel));
    CHECK(resultFeatures(m.doc) == std::vector<ObjectId>{added});
    const Document afterCreate = m.doc.clone();

    // Modify the diameter.
    HoleDefinition wider = pocket;
    wider.diameter = 8_mm;
    REQUIRE(history.execute(m.doc, std::make_unique<ModifyHoleCommand>(featureId(added), wider)).has_value());
    CHECK(requireReport(regenerator, m.doc).regenerated == std::vector<ObjectId>{added});
    const double widened = volumeMm3(regenerator, added);
    CHECK_THAT(widened, WithinRel(drilled - pi * 16.0 * 8.0, kRel));
    const Document afterDiameter = m.doc.clone();

    // Modify the depth.
    HoleDefinition deeper = wider;
    deeper.depth = 12_mm;
    REQUIRE(history.execute(m.doc, std::make_unique<ModifyHoleCommand>(featureId(added), deeper)).has_value());
    CHECK(requireReport(regenerator, m.doc).regenerated == std::vector<ObjectId>{added});
    const double deepened = volumeMm3(regenerator, added);
    CHECK_THAT(deepened, WithinRel(drilled - pi * 16.0 * 12.0, kRel));
    CHECK(facesOn(regenerator, added, topFace(8)).count == 1);
    const Document afterDepth = m.doc.clone();

    // Modify the placement.
    HoleDefinition moved = deeper;
    moved.center = Point2D{80_mm, 25_mm};
    REQUIRE(history.execute(m.doc, std::make_unique<ModifyHoleCommand>(featureId(added), moved)).has_value());
    CHECK(requireReport(regenerator, m.doc).regenerated == std::vector<ObjectId>{added});
    const double relocated = volumeMm3(regenerator, added);
    CHECK_THAT(relocated, WithinRel(drilled - pi * 16.0 * 12.0, kRel));
    CHECK(circlesOn(regenerator, added, 80, 25, 20, 4) == 1);
    CHECK(circlesOn(regenerator, added, 20, 25, 20, 4) == 0);
    const Document afterMove = m.doc.clone();

    // Undo steps back through each edit, to the same geometry bit for bit.
    REQUIRE(history.undo(m.doc).has_value());
    CHECK(equivalent(m.doc, afterDepth));
    CHECK(m.doc.findObjectAs<HoleFeature>(added)->definition() == deeper);
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(volumeMm3(regenerator, added) == deepened);
    CHECK(circlesOn(regenerator, added, 20, 25, 20, 4) == 1);

    REQUIRE(history.undo(m.doc).has_value());
    CHECK(equivalent(m.doc, afterDiameter));
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(volumeMm3(regenerator, added) == widened);

    REQUIRE(history.undo(m.doc).has_value());
    CHECK(equivalent(m.doc, afterCreate));
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(volumeMm3(regenerator, added) == created);

    for (int i = 0; i < 3; ++i) {
        REQUIRE(history.redo(m.doc).has_value());
    }
    CHECK(equivalent(m.doc, afterMove));
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(volumeMm3(regenerator, added) == relocated);

    for (int i = 0; i < 4; ++i) {
        REQUIRE(history.undo(m.doc).has_value());
    }
    CHECK(m.doc.findObject(added) == nullptr);
    CHECK(equivalent(m.doc, initial));
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(regenerator.body(added) == nullptr);
    CHECK(resultFeatures(m.doc) == std::vector<ObjectId>{m.drill});

    REQUIRE(history.redo(m.doc).has_value());
    CHECK(equivalent(m.doc, afterCreate)); // recreated with the same ID
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(volumeMm3(regenerator, added) == created);

    // Commands check the feature kind.
    CHECK(errorCode(history.execute(m.doc, std::make_unique<ModifyHoleCommand>(featureId(m.pad), wider))) ==
          ErrorCode::NotFound);
}

TEST_CASE("HoleFeature_CounterboreMatchesAnalyticVolume", "[hole][features][acceptance]") {
    SECTION("a through counterbore, with a driven hole diameter") {
        // A 10 mm hole with an 18 mm counterbore 5 mm deep:
        // V = V0 - pi r^2 H - pi (R^2 - r^2) h.
        HoleBlockModel m;
        HoleDefinition d = m.definitionOf(m.drill);
        d.type = HoleType::Counterbore;
        d.counterboreDiameter = 18_mm;
        d.counterboreDepth = 5_mm;
        m.setDefinition(m.drill, d);
        Regenerator regenerator;
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CHECK(requireBody(regenerator, m.drill).isValid());
        CHECK_THAT(volumeMm3(regenerator, m.drill), WithinRel(100000.0 - pi * 25.0 * 20.0 - pi * 56.0 * 5.0, kRel));
        const FacesOn shelf = facesOn(regenerator, m.drill, topFace(15));
        CHECK(shelf.count == 1);
        CHECK_THAT(shelf.areaMm2, WithinRel(56.0 * pi, kRel));
        CHECK(circlesOn(regenerator, m.drill, 50, 25, 20, 9) == 1);
        CHECK(circlesOn(regenerator, m.drill, 50, 25, 0, 5) == 1);
        // A 12 mm hole under the same counterbore.
        REQUIRE(m.doc.setParameterValue(m.diameter, 12_mm).has_value());
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CHECK_THAT(volumeMm3(regenerator, m.drill), WithinRel(100000.0 - pi * 36.0 * 20.0 - pi * 45.0 * 5.0, kRel));
    }
    SECTION("a blind counterbore in a chain of holes") {
        // Pocket: a 6 mm hole 12 mm deep under a 10 mm counterbore 4 mm deep:
        // pi 3^2 12 + pi (5^2 - 3^2) 4 = 172 pi.
        HoleVariants m;
        Regenerator regenerator;
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        // The difference of two volumes of ~1e5 mm^3, each good to kRel, is
        // good to ~1e-7 mm^3: ~1e-9 of the few hundred mm^3 removed.
        CHECK_THAT(volumeMm3(regenerator, m.drill) - volumeMm3(regenerator, m.pocket), WithinRel(172.0 * pi, 1e-9));
        CHECK_THAT(volumeMm3(regenerator, m.pocket),
                   WithinRel(HoleBlockModel::expectedVolume(100, 50, 20, 10) - 172.0 * pi, kRel));
        // A shelf 4 mm down (area pi (5^2 - 3^2)) and a floor 12 mm down (9 pi).
        const FacesOn shelf = facesOn(regenerator, m.pocket, topFace(16));
        CHECK(shelf.count == 1);
        CHECK_THAT(shelf.areaMm2, WithinRel(16.0 * pi, kRel));
        const FacesOn floor = facesOn(regenerator, m.pocket, topFace(8));
        CHECK(floor.count == 1);
        CHECK_THAT(floor.areaMm2, WithinRel(9.0 * pi, kRel));
        CHECK(circlesOn(regenerator, m.pocket, 20, 25, 0, 3) == 0); // blind
    }
}

TEST_CASE("HoleFeature_CountersinkMatchesAnalyticVolume", "[hole][features][acceptance]") {
    // The countersink cone two ways: the frustum formula (used by
    // HoleVariants), and the full cone to its apex less the cone below the
    // bore's radius. R = 6, r = 3, 90°: the apex is R / tan 45° below the face.
    const double h = 3.0;
    const double frustum = pi * h * (36.0 + 18.0 + 9.0) / 3.0;
    const double apex = 6.0 / std::tan(pi / 4.0);
    CHECK_THAT(frustum, WithinRel(pi / 3.0 * (36.0 * apex - 9.0 * (apex - h)), 1e-14));
    CHECK_THAT(frustum, WithinRel(63.0 * pi, 1e-15));

    SECTION("a through countersink at the end of a chain of holes") {
        HoleVariants m;
        Regenerator regenerator;
        const RegenerationReport report = requireReport(regenerator, m.doc);
        INFO(describe(report));
        REQUIRE(report.succeeded());
        CHECK(resultFeatures(m.doc) == std::vector<ObjectId>{m.sink});
        const geometry::Body& body = requireBody(regenerator, m.sink);
        CHECK(body.isValid());
        CHECK(body.topology().solids == 1);
        // Sink removes pi 3^2 20 and the cone beyond the bore, 63 pi - 27 pi.
        CHECK_THAT(volumeMm3(regenerator, m.pocket) - volumeMm3(regenerator, m.sink), WithinRel(216.0 * pi, 1e-9));
        CHECK_THAT(volumeMm3(regenerator, m.sink), WithinRel(HoleVariants::expectedVolume(10), kRel));
        CHECK_THAT(body.massProperties()->surfaceArea.in(units::mm2), WithinRel(HoleVariants::expectedArea(), kRel));
        // The cone meets the top face at R = 6 and the bore at r = 3, 3 mm down.
        CHECK(circlesOn(regenerator, m.sink, 80, 25, 20, 6) == 1);
        CHECK(circlesOn(regenerator, m.sink, 80, 25, 17, 3) == 1);
        CHECK(circlesOn(regenerator, m.sink, 80, 25, 0, 3) == 1);

        // Changing the first hole rebuilds the chain of holes on it.
        REQUIRE(m.doc.setParameterValue(m.diameter, 12_mm).has_value());
        const RegenerationReport changed = requireReport(regenerator, m.doc);
        CHECK(changed.regenerated == std::vector<ObjectId>{m.drill, m.pocket, m.sink});
        CHECK_THAT(volumeMm3(regenerator, m.sink), WithinRel(HoleVariants::expectedVolume(12), kRel));
    }
    SECTION("a blind countersink at 82 degrees") {
        // R = 10, r = 5, 12 mm deep: the cone is (R - r) / tan 41° deep.
        const double depth = 5.0 / std::tan(41.0 * pi / 180.0);
        const double beyondBore = pi * depth * (100.0 + 50.0 + 25.0) / 3.0 - pi * 25.0 * depth;
        HoleBlockModel m;
        HoleDefinition d = m.definitionOf(m.drill);
        d.type = HoleType::Countersink;
        d.extent = HoleExtent::Blind;
        d.depth = 12_mm;
        d.countersinkDiameter = 20_mm;
        d.countersinkAngle = 82_deg;
        m.setDefinition(m.drill, d);
        Regenerator regenerator;
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CHECK_THAT(volumeMm3(regenerator, m.drill), WithinRel(100000.0 - pi * 25.0 * 12.0 - beyondBore, kRel));
        CHECK(circlesOn(regenerator, m.drill, 50, 25, 20, 10) == 1);
        CHECK(circlesOn(regenerator, m.drill, 50, 25, 20.0 - depth, 5) == 1);
        CHECK(facesOn(regenerator, m.drill, topFace(8)).count == 1); // the floor
    }
}

TEST_CASE("HoleFeature_WorksOnRevolvedBody", "[hole][features][revolve][acceptance]") {
    // A 4 mm bolt hole through the turned part's top face at (-10, 0): its
    // wall spans radii 8..12, clear of the bore (5) and the groove (13..16).
    TurnedPartModel m;
    auto feature = HoleFeature::create("Bolt", {.target = featureId(m.groove), .face = topFace(40),
                                                .center = Point2D{-(10_mm), 0_mm}, .diameter = 4_mm});
    REQUIRE(feature.has_value());
    const ObjectId bolt = m.doc.addObject(std::move(*feature)).value();
    const auto expected = [](double sweepDeg) {
        return TurnedPartModel::expectedVolume(15, 40, sweepDeg, 5) - pi * 4.0 * 40.0;
    };

    Regenerator regenerator;
    const RegenerationReport report = requireReport(regenerator, m.doc);
    INFO(describe(report));
    REQUIRE(report.succeeded());
    CHECK(report.regenerated.back() == bolt);
    CHECK(requireBody(regenerator, bolt).isValid());
    CHECK_THAT(volumeMm3(regenerator, bolt), WithinRel(expected(360), kRel));
    CHECK(circlesOn(regenerator, bolt, -10, 0, 40, 2) == 1);
    CHECK(circlesOn(regenerator, bolt, -10, 0, 0, 2) == 1);
    CHECK(resultFeatures(m.doc) == std::vector<ObjectId>{bolt});

    SECTION("a partial sweep that still covers the hole") {
        REQUIRE(m.doc.setParameterValue(m.sweep, 270_deg).has_value());
        const RegenerationReport partial = requireReport(regenerator, m.doc);
        INFO(describe(partial));
        CHECK(partial.regenerated == std::vector<ObjectId>{m.turn, m.boreCut, m.groove, bolt});
        CHECK_THAT(volumeMm3(regenerator, bolt), WithinRel(expected(270), kRel));
    }
    SECTION("a wider bore reaches the hole: refused, not cut into the bore") {
        REQUIRE(m.doc.setParameterValue(m.bore, 9_mm).has_value());
        Error error = requireFailure(regenerator, m.doc, bolt);
        CHECK(error.code == ErrorCode::FailedPrecondition);
        CHECK_THAT(error.message, ContainsSubstring("the centre (-10, 0) mm is only 1 mm from the face's edge"));
        REQUIRE(m.doc.setParameterValue(m.bore, 11_mm).has_value());
        error = requireFailure(regenerator, m.doc, bolt);
        CHECK(error.message == "Bolt: hole: the centre (-10, 0) mm is not on a face of the body on the plane through "
                               "(0, 0, 40) mm facing (0, 0, 1) (1 face(s) lie on that plane elsewhere)");
    }
}

TEST_CASE("HoleFeature_WorksOnAnExtrudedChain", "[hole][features][acceptance]") {
    // The bracket: a plate with a sketched hole, then a 15 x 15 x 5 mm pocket
    // cut from its top face. Drill through the pocket's floor, then a blind
    // hole into the plate's top face.
    BracketModel m;
    const ObjectId anchor =
        m.doc.addObject(HoleFeature::create("Anchor", {.target = featureId(m.pocket), .face = topFace(15),
                                                       .center = Point2D{17.5_mm, 17.5_mm}, .diameter = 6_mm})
                            .value())
            .value();
    const ObjectId blind =
        m.doc.addObject(HoleFeature::create("Blind", {.target = featureId(anchor), .face = topFace(20),
                                                      .center = Point2D{80_mm, 30_mm}, .extent = HoleExtent::Blind,
                                                      .diameter = 10_mm, .depth = 8_mm})
                            .value())
            .value();
    Regenerator regenerator;
    const RegenerationReport report = requireReport(regenerator, m.doc);
    INFO(describe(report));
    REQUIRE(report.succeeded());
    // The floor hole goes through the 15 mm under the pocket; the blind one 8 mm.
    CHECK_THAT(volumeMm3(regenerator, anchor), WithinRel(BracketModel::kPocketVolume - pi * 9.0 * 15.0, kRel));
    CHECK_THAT(volumeMm3(regenerator, blind),
               WithinRel(BracketModel::kPocketVolume - pi * 9.0 * 15.0 - pi * 25.0 * 8.0, kRel));
    CHECK(circlesOn(regenerator, blind, 17.5, 17.5, 0, 3) == 1);
    CHECK(requireBody(regenerator, blind).isValid());

    // A larger sketched hole in the plate: the chain regenerates.
    REQUIRE(m.doc.setParameterValue(m.holeRadius, 10_mm).has_value());
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK_THAT(volumeMm3(regenerator, blind),
               WithinRel(BracketModel::kPocketVolume - pi * (100.0 - 64.0) * 20.0 - pi * 9.0 * 15.0 - pi * 25.0 * 8.0,
                         kRel));
}

TEST_CASE("HoleFeature_ComposesWithChamferAndFillet", "[hole][features][chamfer][fillet][acceptance]") {
    HoleBlockModel m;
    Regenerator regenerator;
    const double drilled = HoleBlockModel::expectedVolume(100, 50, 20, 10);

    SECTION("a hole in a chamfered block") {
        const ObjectId bevel = m.add<ChamferFeature>(
            "Bevel", {.target = featureId(m.pad), .edges = {BlockModel::alongX(0, 20)}, .distance = 2_mm});
        HoleDefinition d = m.definitionOf(m.drill);
        d.target = featureId(bevel);
        m.setDefinition(m.drill, d);
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CHECK_THAT(volumeMm3(regenerator, m.drill), WithinRel(drilled - 100.0 * 2.0 * 2.0 / 2.0, kRel));
        CHECK(resultFeatures(m.doc) == std::vector<ObjectId>{m.drill});
        // Next to the chamfer the hole would break into its face: refused.
        REQUIRE(m.doc.setParameterValue(m.holeY, 6_mm).has_value());
        const Error error = requireFailure(regenerator, m.doc, m.drill);
        CHECK(error.code == ErrorCode::FailedPrecondition);
        CHECK_THAT(error.message, ContainsSubstring("the centre (50, 6) mm is only 4 mm from the face's edge"));
    }
    SECTION("a hole in a filleted block") {
        const ObjectId round = m.add<FilletFeature>(
            "Round", {.target = featureId(m.pad), .edges = {BlockModel::alongX(0, 20)}, .radius = 5_mm});
        HoleDefinition d = m.definitionOf(m.drill);
        d.target = featureId(round);
        m.setDefinition(m.drill, d);
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CHECK_THAT(volumeMm3(regenerator, m.drill), WithinRel(drilled - 100.0 * filletCorner(5), kRel));
    }
    SECTION("a chamfer of both rims of the hole") {
        // Each rim loses a ring whose section is the triangle with legs c,
        // centroid c/3 outside the bore: pi c^2 (r + c/3) by Pappus.
        const ObjectId ease = m.add<ChamferFeature>(
            "Ease", {.target = featureId(m.drill), .edges = {rim(50, 25, 20, 5), rim(50, 25, 0, 5)}, .distance = 1_mm});
        const RegenerationReport report = requireReport(regenerator, m.doc);
        INFO(describe(report));
        REQUIRE(report.succeeded());
        CHECK_THAT(volumeMm3(regenerator, ease), WithinRel(drilled - 2.0 * pi * 1.0 * (5.0 + 1.0 / 3.0), kRel));
        CHECK(circlesOn(regenerator, ease, 50, 25, 20, 6) == 1);
        CHECK(resultFeatures(m.doc) == std::vector<ObjectId>{ease});
        // A larger hole moves the rims off the referenced circles: the
        // chamfer fails, the hole does not.
        REQUIRE(m.doc.setParameterValue(m.diameter, 12_mm).has_value());
        const Error error = requireFailure(regenerator, m.doc, ease);
        CHECK(error.code == ErrorCode::NotFound);
        CHECK(error.message == "Ease: chamfer: edge reference 1 (circle around (50, 25, 20) mm with axis (0, 0, 1) and "
                               "radius 5 mm) matches no edge of the body");
        CHECK(regenerator.body(m.drill) != nullptr);
    }
    SECTION("a fillet of the hole's rim") {
        // The corner area r_f^2 (1 - pi/4) turns about the hole's axis at its
        // centroid, r_f (10 - 3 pi) / (12 - 3 pi) outside the bore (Pappus).
        const double rf = 2.0;
        const double centroid = rf * (10.0 - 3.0 * pi) / (12.0 - 3.0 * pi);
        const ObjectId soften = m.add<FilletFeature>(
            "Soften", {.target = featureId(m.drill), .edges = {rim(50, 25, 20, 5)}, .radius = 2_mm});
        const RegenerationReport report = requireReport(regenerator, m.doc);
        INFO(describe(report));
        REQUIRE(report.succeeded());
        CHECK(requireBody(regenerator, soften).isValid());
        CHECK_THAT(volumeMm3(regenerator, soften),
                   WithinRel(drilled - 2.0 * pi * (5.0 + centroid) * filletCorner(rf), kRel));
        CHECK(circlesOn(regenerator, soften, 50, 25, 20, 7) == 1); // tangent to the top face 2 mm out
    }
}

TEST_CASE("HoleFeature_RegenerationIsDeterministic", "[hole][features]") {
    HoleVariants m;
    Regenerator first;
    Regenerator second;
    REQUIRE(requireReport(first, m.doc).succeeded());
    REQUIRE(requireReport(second, m.doc).succeeded());
    for (const ObjectId feature : {m.drill, m.pocket, m.sink}) {
        const geometry::Body& a = requireBody(first, feature);
        const geometry::Body& b = requireBody(second, feature);
        const auto pa = a.massProperties().value();
        const auto pb = b.massProperties().value();
        CHECK(bits(pa.volume.si()) == bits(pb.volume.si()));
        CHECK(bits(pa.surfaceArea.si()) == bits(pb.surfaceArea.si()));
        CHECK(a.boundingBox().value() == b.boundingBox().value());
        CHECK(a.topology() == b.topology());
    }
    CHECK_THAT(volumeMm3(first, m.sink), WithinRel(HoleVariants::expectedVolume(10), kRel));
}
