#include "features/FeatureTestSupport.hpp"

#include <bettercad/assembly/Component.hpp>
#include <bettercad/assembly/Components.hpp>
#include <bettercad/assembly/Placement.hpp>
#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/DependencyGraph.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <set>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;

// P13-XFORM-001: component placement, implementing ADR-005 -- the placement
// is intent and is persisted; the RigidTransform3D it means is derived and
// is not.
//
// Every expected value below is hand-derived from the stated convention
// (extrinsic X then Y then Z about the model's origin, right-handed, then a
// translation along the model's axes). None is produced by asking BetterCAD
// what it thinks, which would make the test agree with the code rather than
// with the mathematics.

namespace {

// Well-conditioned double-precision algebra: a handful of products and sums
// of values of order 1. 1e-12 is far above the ~1e-16 rounding of a 90
// degree sine and far below any error a wrong convention would produce.
constexpr double kTolerance = 1e-12;

struct PartDocument {
    Document document{"Assembly"};
    ObjectId sketch{};
    ObjectId part{};
};

PartDocument makePart() {
    PartDocument p;
    auto sketch = std::make_unique<sketch::Sketch>("BlockSketch", Frame3D::xy());
    addRectangle(*sketch, 0_mm, 0_mm, 40_mm, 30_mm);
    p.sketch = require(p.document.addObject(std::move(sketch)));
    auto extrude = features::ExtrudeFeature::create(
        "Block", {.profile = SketchId::fromValue(p.sketch.value()), .depth = 10_mm});
    REQUIRE(extrude.has_value());
    p.part = require(p.document.addObject(std::move(*extrude)));
    return p;
}

ComponentId addComponent(Document& document, std::string name, ObjectId part,
                         const ComponentPlacement& placement = {}) {
    auto id = assembly::createComponent(document, std::move(name), {.part = part, .placement = placement});
    if (!id) {
        FAIL(id.error().message);
    }
    return *id;
}

// The transform of a placement, in a document that has one component with
// it. Fails the test rather than returning an error, because these cases are
// all meant to resolve.
RigidTransform3D transformOf(const ComponentPlacement& placement) {
    PartDocument p = makePart();
    const ComponentId id = addComponent(p.document, "Block1", p.part, placement);
    auto motion = assembly::placementOf(p.document, id);
    if (!motion) {
        FAIL(motion.error().message);
    }
    return *motion;
}

void checkPoint(const Point3D& measured, double x, double y, double z) {
    CHECK_THAT(measured.x.si(), WithinAbs(x, kTolerance));
    CHECK_THAT(measured.y.si(), WithinAbs(y, kTolerance));
    CHECK_THAT(measured.z.si(), WithinAbs(z, kTolerance));
}

void checkDirection(const Direction3D& measured, double x, double y, double z) {
    CHECK_THAT(measured.x(), WithinAbs(x, kTolerance));
    CHECK_THAT(measured.y(), WithinAbs(y, kTolerance));
    CHECK_THAT(measured.z(), WithinAbs(z, kTolerance));
}

} // namespace

TEST_CASE("Placement_DefaultsToTheIdentityAndMovesNothing", "[assembly][placement][p13]") {
    // A component that has not been placed sits where its part is.
    const ComponentPlacement identity;
    CHECK(isIdentity(identity));
    CHECK(referencedParameters(identity).empty());

    const RigidTransform3D motion = transformOf(identity);
    CHECK(motion.isTranslation());
    CHECK_FALSE(motion.reversesOrientation());
    // Hand-derived: the identity leaves every point where it is.
    checkPoint(motion.apply(Point3D{1_mm, 2_mm, 3_mm}), 0.001, 0.002, 0.003);
    checkPoint(motion.apply(Point3D{}), 0.0, 0.0, 0.0);
    checkDirection(motion.apply(Direction3D::unitX()), 1.0, 0.0, 0.0);
}

TEST_CASE("Placement_TranslatesAlongTheModelAxes", "[assembly][placement][p13]") {
    // Hand-derived: p' = p + t, componentwise.
    const RigidTransform3D motion = transformOf({.translation = {10_mm, 20_mm, 30_mm}});

    CHECK(motion.isTranslation());
    checkPoint(motion.apply(Point3D{1_mm, 2_mm, 3_mm}), 0.011, 0.022, 0.033);
    checkPoint(motion.apply(Point3D{}), 0.010, 0.020, 0.030);
    // A translation does not turn directions.
    checkDirection(motion.apply(Direction3D::unitX()), 1.0, 0.0, 0.0);
}

TEST_CASE("Placement_RotatesRightHandedlyAboutEachModelAxis", "[assembly][placement][p13]") {
    // Right-handed, active rotations. Hand-derived by the right-hand rule:
    // curl the fingers of the right hand from the first axis to the second
    // and the thumb points along the rotation axis.
    SECTION("+90 degrees about X sends Y to Z and Z to -Y") {
        const RigidTransform3D motion = transformOf({.rotation = {90_deg, 0_deg, 0_deg}});
        checkDirection(motion.apply(Direction3D::unitY()), 0.0, 0.0, 1.0);
        checkDirection(motion.apply(Direction3D::unitZ()), 0.0, -1.0, 0.0);
        checkDirection(motion.apply(Direction3D::unitX()), 1.0, 0.0, 0.0);
    }
    SECTION("+90 degrees about Y sends Z to X and X to -Z") {
        const RigidTransform3D motion = transformOf({.rotation = {0_deg, 90_deg, 0_deg}});
        checkDirection(motion.apply(Direction3D::unitZ()), 1.0, 0.0, 0.0);
        checkDirection(motion.apply(Direction3D::unitX()), 0.0, 0.0, -1.0);
        checkDirection(motion.apply(Direction3D::unitY()), 0.0, 1.0, 0.0);
    }
    SECTION("+90 degrees about Z sends X to Y and Y to -X") {
        const RigidTransform3D motion = transformOf({.rotation = {0_deg, 0_deg, 90_deg}});
        checkDirection(motion.apply(Direction3D::unitX()), 0.0, 1.0, 0.0);
        checkDirection(motion.apply(Direction3D::unitY()), -1.0, 0.0, 0.0);
        checkDirection(motion.apply(Direction3D::unitZ()), 0.0, 0.0, 1.0);
    }
    SECTION("180 degrees about Z negates X and Y") {
        const RigidTransform3D motion = transformOf({.rotation = {0_deg, 0_deg, 180_deg}});
        checkPoint(motion.apply(Point3D{1_mm, 2_mm, 3_mm}), -0.001, -0.002, 0.003);
    }
    SECTION("zero rotation is the identity") {
        const RigidTransform3D motion = transformOf({.rotation = {0_deg, 0_deg, 0_deg}});
        CHECK(motion.isTranslation());
    }
    SECTION("a rotation never mirrors") {
        const RigidTransform3D motion = transformOf({.rotation = {30_deg, 40_deg, 50_deg}});
        CHECK_FALSE(motion.reversesOrientation());
    }
}

TEST_CASE("Placement_AppliesRotationsAboutTheModelsFixedAxesInXYZOrder", "[assembly][placement][p13]") {
    // The discriminating case. With rx = rz = 90 degrees, extrinsic X-then-Z
    // and extrinsic Z-then-X give different answers, so this fails if the
    // composition order is reversed.
    //
    // Hand-derived, taking the point (0, 1, 0):
    //   Rx(90): (0,1,0) -> (0,0,1)      Y goes to Z
    //   Rz(90): (0,0,1) -> (0,0,1)      a Z turn leaves Z alone
    //   so Rz * Rx gives (0, 0, 1).
    // The other order would give:
    //   Rz(90): (0,1,0) -> (-1,0,0)
    //   Rx(90): (-1,0,0) -> (-1,0,0)
    //   that is (-1, 0, 0), which is not the same point.
    const RigidTransform3D motion = transformOf({.rotation = {90_deg, 0_deg, 90_deg}});
    checkPoint(motion.apply(Point3D{0_mm, 1_mm, 0_mm}), 0.0, 0.0, 0.001);

    // And the full basis, hand-derived for Rz(90) * Rx(90):
    //   X -> Rx: (1,0,0) -> Rz: (0,1,0)
    //   Y -> Rx: (0,0,1) -> Rz: (0,0,1)
    //   Z -> Rx: (0,-1,0) -> Rz: (1,0,0)
    checkDirection(motion.apply(Direction3D::unitX()), 0.0, 1.0, 0.0);
    checkDirection(motion.apply(Direction3D::unitY()), 0.0, 0.0, 1.0);
    checkDirection(motion.apply(Direction3D::unitZ()), 1.0, 0.0, 0.0);
}

TEST_CASE("Placement_TurnsFirstAndThenTranslates", "[assembly][placement][p13]") {
    // The other discriminating case: the translation is along the model's
    // axes and is applied after the turn, so it is not itself rotated.
    //
    // Hand-derived for rz = 90 degrees, t = (10, 0, 0) mm, point (1, 0, 0) mm:
    //   turn:      (1,0,0) -> (0,1,0)
    //   translate: (0,1,0) + (10,0,0) = (10, 1, 0) mm
    // Translating first and then turning would give (0, 11, 0) mm instead.
    const RigidTransform3D motion =
        transformOf({.translation = {10_mm, 0_mm, 0_mm}, .rotation = {0_deg, 0_deg, 90_deg}});
    checkPoint(motion.apply(Point3D{1_mm, 0_mm, 0_mm}), 0.010, 0.001, 0.0);
    // The origin goes to the translation, because the turn fixes it.
    checkPoint(motion.apply(Point3D{}), 0.010, 0.0, 0.0);
}

TEST_CASE("Placement_MatchesAHandComputedMatrixAndTranslation", "[assembly][placement][p13]") {
    // A case with no zeros to hide behind: 30 degrees about Z, moved.
    // Hand-derived Rz(30) = [[cos30, -sin30, 0], [sin30, cos30, 0], [0, 0, 1]]
    // with cos30 = sqrt(3)/2 and sin30 = 1/2, stored row by row.
    const RigidTransform3D motion =
        transformOf({.translation = {5_mm, -7_mm, 2_mm}, .rotation = {0_deg, 0_deg, 30_deg}});
    const double c = std::sqrt(3.0) / 2.0;
    const double s = 0.5;
    const std::array<double, 9> expected{c, -s, 0.0, s, c, 0.0, 0.0, 0.0, 1.0};
    for (std::size_t i = 0; i < 9; ++i) {
        INFO("matrix entry " << i);
        CHECK_THAT(motion.matrix()[i], WithinAbs(expected[i], kTolerance));
    }
    CHECK_THAT(motion.translationPart().x.si(), WithinAbs(0.005, kTolerance));
    CHECK_THAT(motion.translationPart().y.si(), WithinAbs(-0.007, kTolerance));
    CHECK_THAT(motion.translationPart().z.si(), WithinAbs(0.002, kTolerance));

    // And the point it sends (1, 0, 0) mm to: (cos30, sin30, 0) mm + t.
    checkPoint(motion.apply(Point3D{1_mm, 0_mm, 0_mm}), 0.001 * c + 0.005, 0.001 * s - 0.007, 0.002);
}

TEST_CASE("Placement_ReadsAnglesAsAnglesAndLengthsAsLengths", "[assembly][placement][p13]") {
    // Units are part of correctness: the same rotation written in degrees and
    // in radians must give the same transform, and a length in inches must be
    // the millimetres it equals.
    const RigidTransform3D inDegrees = transformOf({.rotation = {0_deg, 0_deg, 90_deg}});
    const RigidTransform3D inRadians =
        transformOf({.rotation = {0_rad, 0_rad, Angle::fromSi(std::numbers::pi / 2.0)}});
    for (std::size_t i = 0; i < 9; ++i) {
        INFO("matrix entry " << i);
        CHECK_THAT(inDegrees.matrix()[i], WithinAbs(inRadians.matrix()[i], kTolerance));
    }

    const RigidTransform3D inInches = transformOf({.translation = {1_in, 0_mm, 0_mm}});
    CHECK_THAT(inInches.translationPart().x.si(), WithinAbs(0.0254, kTolerance));
}

TEST_CASE("Placement_IsPerInstanceSoMovingOneLeavesTheOthersWhereTheyWere", "[assembly][placement][p13]") {
    // The point of an instance: one part definition, independent positions.
    PartDocument p = makePart();
    const ComponentId a = addComponent(p.document, "Block1", p.part, {.translation = {10_mm, 0_mm, 0_mm}});
    const ComponentId b = addComponent(p.document, "Block2", p.part, {.translation = {0_mm, 20_mm, 0_mm}});
    const ComponentId c = addComponent(p.document, "Block3", p.part, {.rotation = {0_deg, 0_deg, 90_deg}});

    checkPoint(require(assembly::placementOf(p.document, a)).apply(Point3D{}), 0.010, 0.0, 0.0);
    checkPoint(require(assembly::placementOf(p.document, b)).apply(Point3D{}), 0.0, 0.020, 0.0);
    checkDirection(require(assembly::placementOf(p.document, c)).apply(Direction3D::unitX()), 0.0, 1.0, 0.0);

    // Moving one moves only that one.
    REQUIRE(assembly::setComponentPlacement(p.document, a, {.translation = {99_mm, 0_mm, 0_mm}}).has_value());

    checkPoint(require(assembly::placementOf(p.document, a)).apply(Point3D{}), 0.099, 0.0, 0.0);
    checkPoint(require(assembly::placementOf(p.document, b)).apply(Point3D{}), 0.0, 0.020, 0.0);
    checkDirection(require(assembly::placementOf(p.document, c)).apply(Direction3D::unitX()), 0.0, 1.0, 0.0);
    // And they still all place the one part, which was not duplicated.
    CHECK(assembly::componentsOf(p.document, p.part) == std::vector<ComponentId>{a, b, c});
    CHECK(p.document.objectCount() == 5);
}

TEST_CASE("Placement_FollowsTheParameterThatDrivesIt", "[assembly][placement][p13]") {
    PartDocument p = makePart();
    const ParameterId offset = require(p.document.createParameter("offset", 25_mm, units::mm));
    const ComponentId id = addComponent(
        p.document, "Block1", p.part,
        {.translation = {0_mm, 0_mm, 0_mm}, .translationParameters = {offset, std::nullopt, std::nullopt}});

    // The literal is ignored when a parameter drives the component.
    checkPoint(require(assembly::placementOf(p.document, id)).apply(Point3D{}), 0.025, 0.0, 0.0);

    // Editing the parameter moves the component, with nothing cached to go
    // stale: the transform is computed from the value in force each time.
    REQUIRE(p.document.setParameterValue(offset, 60_mm).has_value());
    checkPoint(require(assembly::placementOf(p.document, id)).apply(Point3D{}), 0.060, 0.0, 0.0);

    // And the component depends on the parameter, so the graph knows to
    // dirty it.
    const DocumentGraph graph = buildDependencyGraph(p.document);
    CHECK(graph.missing.empty());
    CHECK(graph.graph.dependenciesOf(id) == std::set<ObjectId>{p.part, ObjectId{offset}});
    CHECK(graph.graph.dependentsOf(ObjectId{offset}).contains(ObjectId{id}));
}

TEST_CASE("Placement_RefusesAParameterItCannotUse", "[assembly][placement][p13]") {
    PartDocument p = makePart();
    const ParameterId angle = require(p.document.createParameter("turn", 30_deg, units::deg));
    const ParameterId length = require(p.document.createParameter("offset", 25_mm, units::mm));

    SECTION("an angle cannot drive a translation") {
        const ComponentId id = addComponent(p.document, "Block1", p.part,
                                            {.translationParameters = {angle, std::nullopt, std::nullopt}});
        auto motion = assembly::placementOf(p.document, id);
        REQUIRE_FALSE(motion.has_value());
        CHECK(motion.error().code == ErrorCode::DimensionMismatch);
    }
    SECTION("a length cannot drive a rotation") {
        const ComponentId id = addComponent(p.document, "Block1", p.part,
                                            {.rotationParameters = {length, std::nullopt, std::nullopt}});
        auto motion = assembly::placementOf(p.document, id);
        REQUIRE_FALSE(motion.has_value());
        CHECK(motion.error().code == ErrorCode::DimensionMismatch);
    }
    SECTION("a parameter that does not exist") {
        const ComponentId id =
            addComponent(p.document, "Block1", p.part,
                         {.translationParameters = {ParameterId::fromValue(9999), std::nullopt, std::nullopt}});
        auto motion = assembly::placementOf(p.document, id);
        REQUIRE_FALSE(motion.has_value());
        CHECK(motion.error().code == ErrorCode::NotFound);
        CHECK_THAT(motion.error().message, ContainsSubstring("X translation parameter"));
    }
    SECTION("no component of that ID") {
        auto motion = assembly::placementOf(p.document, ComponentId::fromValue(9999));
        REQUIRE_FALSE(motion.has_value());
        CHECK(motion.error().code == ErrorCode::NotFound);
    }
}

TEST_CASE("Placement_RefusesNonFiniteValuesAndKeepsTheOneItHad", "[assembly][placement][p13]") {
    PartDocument p = makePart();
    const ComponentPlacement good{.translation = {10_mm, 0_mm, 0_mm}};
    const ComponentId id = addComponent(p.document, "Block1", p.part, good);
    const auto revisionBefore = p.document.revision();

    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    const std::array<ComponentPlacement, 4> bad{
        ComponentPlacement{.translation = {Length::fromSi(nan), 0_mm, 0_mm}},
        ComponentPlacement{.translation = {Length::fromSi(inf), 0_mm, 0_mm}},
        ComponentPlacement{.rotation = {Angle::fromSi(nan), 0_deg, 0_deg}},
        ComponentPlacement{.rotation = {Angle::fromSi(-inf), 0_deg, 0_deg}},
    };

    for (const ComponentPlacement& placement : bad) {
        auto changed = assembly::setComponentPlacement(p.document, id, placement);
        REQUIRE_FALSE(changed.has_value());
        CHECK(changed.error().code == ErrorCode::InvalidArgument);
        // Nothing moved, and nothing was half-applied.
        CHECK(assembly::findComponent(p.document, id)->definition().placement == good);
        CHECK(p.document.revision() == revisionBefore);
    }

    // And the component still works afterwards.
    REQUIRE(assembly::setComponentPlacement(p.document, id, {.translation = {1_mm, 2_mm, 3_mm}}).has_value());
    checkPoint(require(assembly::placementOf(p.document, id)).apply(Point3D{}), 0.001, 0.002, 0.003);
}

TEST_CASE("Placement_SettingTheSamePlacementChangesNothing", "[assembly][placement][p13]") {
    PartDocument p = makePart();
    const ComponentPlacement placement{.translation = {10_mm, 0_mm, 0_mm}};
    const ComponentId id = addComponent(p.document, "Block1", p.part, placement);
    const auto revision = p.document.revision();

    auto changed = assembly::setComponentPlacement(p.document, id, placement);
    REQUIRE(changed.has_value());
    CHECK_FALSE(*changed);
    CHECK(p.document.revision() == revision);
}

TEST_CASE("Placement_IsDeterministicForTheSameIntent", "[assembly][placement][p13][determinism]") {
    // Nothing is cached, so the only way the answer could move is if the
    // computation itself were not a function of the intent.
    const ComponentPlacement placement{.translation = {3_mm, -4_mm, 5_mm},
                                       .rotation = {10_deg, 20_deg, 30_deg}};
    const RigidTransform3D first = transformOf(placement);
    const RigidTransform3D second = transformOf(placement);
    // Bit-identical, not merely close: the same inputs through the same
    // arithmetic in the same order.
    CHECK(first == second);

    // Asking twice in one document gives the same answer too.
    PartDocument p = makePart();
    const ComponentId id = addComponent(p.document, "Block1", p.part, placement);
    CHECK(require(assembly::placementOf(p.document, id)) == require(assembly::placementOf(p.document, id)));
}

TEST_CASE("Placement_WhoseParameterIsDeletedFailsLikeAMissingPart", "[assembly][placement][p13][regeneration]") {
    // A placement parameter is an ordinary dependency edge, so removing it
    // must be reported the way removing the part is -- never silently
    // treated as zero, which would move the component without saying so.
    PartDocument p = makePart();
    const ParameterId offset = require(p.document.createParameter("offset", 25_mm, units::mm));
    const ComponentId id =
        addComponent(p.document, "Block1", p.part,
                     {.translationParameters = {offset, std::nullopt, std::nullopt}});
    features::Regenerator regenerator;
    REQUIRE(requireReport(regenerator, p.document).succeeded());

    REQUIRE(p.document.removeParameter(offset).has_value());

    const DocumentGraph graph = buildDependencyGraph(p.document);
    CHECK(graph.missing.size() == 1);
    const features::RegenerationReport report = requireReport(regenerator, p.document);
    CHECK_FALSE(report.succeeded());

    // And asking for the transform says which parameter is gone rather than
    // returning a position built from a default.
    auto motion = assembly::placementOf(p.document, id);
    REQUIRE_FALSE(motion.has_value());
    CHECK(motion.error().code == ErrorCode::NotFound);
    CHECK_THAT(motion.error().message, ContainsSubstring("X translation parameter"));
}

TEST_CASE("Placement_FollowsTheActiveConfiguration", "[assembly][placement][p13]") {
    // ADR-005 chose a parameter-driven intent partly because it makes a
    // placement configuration-aware for free: the value in force is
    // effectiveParameterValue(), so a component moves with the
    // configuration without knowing configurations exist.
    PartDocument p = makePart();
    const ParameterId offset = require(p.document.createParameter("offset", 25_mm, units::mm));
    const ComponentId id =
        addComponent(p.document, "Block1", p.part,
                     {.translationParameters = {offset, std::nullopt, std::nullopt}});
    const ConfigurationId wide = require(p.document.createConfiguration("Wide"));
    REQUIRE(p.document.setConfigurationOverride(wide, offset, 80_mm).has_value());

    // Base configuration: the parameter's own value.
    checkPoint(require(assembly::placementOf(p.document, id)).apply(Point3D{}), 0.025, 0.0, 0.0);

    REQUIRE(p.document.setActiveConfiguration(wide).has_value());
    checkPoint(require(assembly::placementOf(p.document, id)).apply(Point3D{}), 0.080, 0.0, 0.0);

    // And back, exactly: the override is applied to the base value, not
    // written into it.
    REQUIRE(p.document.setActiveConfiguration(std::nullopt).has_value());
    checkPoint(require(assembly::placementOf(p.document, id)).apply(Point3D{}), 0.025, 0.0, 0.0);
}
