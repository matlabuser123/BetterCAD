#include "features/FeatureTestSupport.hpp"
#include "support/TestFiles.hpp"

#include <bettercad/assembly/Component.hpp>
#include <bettercad/assembly/Components.hpp>
#include <bettercad/assembly/Mates.hpp>
#include <bettercad/assembly/Solver.hpp>
#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/document/MateReference.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cmath>
#include <filesystem>
#include <memory>
#include <numbers>
#include <string>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using assembly::MateDefinition;
using assembly::MateType;
using assembly::SolveStatus;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;

// P13-MATE-002: the four mechanical mates, solved.
//
// A joint is named for the freedom it leaves, so counting the degrees of
// freedom is the weakest thing these tests could check -- a Slider and a
// Revolute both leave one, and confusing them would pass that check. What
// each case here checks instead is WHICH motion survives: the component is
// started displaced along a freedom the joint is supposed to keep and rotated
// out of a freedom it is supposed to remove, and the solve must leave the
// first alone and undo the second.
//
// Every expected value is derived by hand from the geometry.

namespace {

constexpr double kTolerance = 1e-8;

struct Rig {
    Document document{"Assembly"};
    ObjectId sketch{};
    ObjectId part{};
};

Rig makeRig() {
    Rig rig;
    auto sketch = std::make_unique<sketch::Sketch>("BlockSketch", Frame3D::xy());
    addRectangle(*sketch, 0_mm, 0_mm, 40_mm, 30_mm);
    rig.sketch = require(rig.document.addObject(std::move(sketch)));
    auto extrude = features::ExtrudeFeature::create(
        "Block", {.profile = SketchId::fromValue(rig.sketch.value()), .depth = 10_mm});
    REQUIRE(extrude.has_value());
    rig.part = require(rig.document.addObject(std::move(*extrude)));
    return rig;
}

ComponentId place(Rig& rig, const std::string& name, const ComponentPlacement& placement = {}) {
    return require(assembly::createComponent(rig.document, name, {.part = rig.part, .placement = placement}));
}

MateTarget plane(ComponentId component, PrincipalPlane which = PrincipalPlane::XY) {
    return planeTarget(component, PlaneReference{.plane = which});
}
MateTarget axis(ComponentId component, PrincipalAxis which = PrincipalAxis::Z) {
    return axisTarget(component, AxisReference{.axis = which});
}

void add(Rig& rig, const std::string& name, const MateDefinition& definition) {
    auto id = assembly::createMate(rig.document, name, definition);
    if (!id) {
        FAIL(id.error().message);
    }
}

assembly::AssemblySolveResult solved(const Rig& rig) {
    auto result = assembly::solve(rig.document);
    if (!result) {
        FAIL(result.error().message);
    }
    return *result;
}

void checkPosition(const RigidTransform3D& transform, double x, double y, double z) {
    const Point3D origin = transform.apply(Point3D{});
    CHECK_THAT(origin.x.si(), WithinAbs(x, kTolerance));
    CHECK_THAT(origin.y.si(), WithinAbs(y, kTolerance));
    CHECK_THAT(origin.z.si(), WithinAbs(z, kTolerance));
}

void checkDirection(const Direction3D& measured, double x, double y, double z) {
    CHECK_THAT(measured.x(), WithinAbs(x, kTolerance));
    CHECK_THAT(measured.y(), WithinAbs(y, kTolerance));
    CHECK_THAT(measured.z(), WithinAbs(z, kTolerance));
}

/// A component's own Z and X directions after the solve: the joint axis and
/// the reference that says how far it has turned about it.
Direction3D axisOf(const RigidTransform3D& transform) { return transform.apply(Direction3D::unitZ()); }
Direction3D rollOf(const RigidTransform3D& transform) { return transform.apply(Direction3D::unitX()); }

/// A grounded component at the origin, and a second one placed as given.
struct Pair {
    Rig rig;
    ComponentId a{};
    ComponentId b{};
};

Pair makePair(const ComponentPlacement& placement) {
    Pair pair{.rig = makeRig()};
    pair.a = place(pair.rig, "Block1");
    pair.b = place(pair.rig, "Block2", placement);
    add(pair.rig, "Ground", {.type = MateType::Fixed, .component = pair.a});
    return pair;
}

/// A slider's roll reference: each component's own X axis.
MateDefinition slider(ComponentId a, ComponentId b) {
    return {.type = MateType::Slider,
            .a = axis(a),
            .b = axis(b),
            .a2 = axis(a, PrincipalAxis::X),
            .b2 = axis(b, PrincipalAxis::X)};
}

constexpr double kCos25 = 0.90630778703664996;
constexpr double kSin25 = 0.42261826174069944;
constexpr double kCos30 = 0.86602540378443865;
constexpr double kCos35 = 0.81915204428899180;
constexpr double kSin35 = 0.57357643635104609;
constexpr double kCos28 = 0.88294759285892694;
constexpr double kSin28 = 0.46947156278589081;

} // namespace

// --- Revolute -----------------------------------------------------------------------------------

TEST_CASE("Revolute_KeepsTheTurnAboutItsAxisAndRemovesEverythingElse", "[assembly][mate][mechanical][p13]") {
    // Hand-derived. A is grounded at the origin, so its Z axis is the line
    // x = y = 0. A revolute holds b's axis on that line (2 rows), meeting it
    // (2 rows), and at a fixed position along it (1 row) -- 5 equations, so
    // one freedom is left, and it is the turn.
    //
    // b starts 15 mm off the axis, 20 mm along it, and turned 30 degrees
    // about it. The offsets must go; the turn must not.
    Pair p = makePair({.translation = {15_mm, 0_mm, 20_mm}, .rotation = {0_deg, 0_deg, 30_deg}});
    add(p.rig, "Hinge", {.type = MateType::Revolute, .a = axis(p.a), .b = axis(p.b)});

    const auto result = solved(p.rig);
    REQUIRE(result.solved());
    CHECK(result.status == SolveStatus::UnderConstrained);
    CHECK(result.equations == 5);
    CHECK(result.unknowns == 6);
    CHECK(result.degreesOfFreedom == 1);
    CHECK(result.redundant.empty());

    const RigidTransform3D& b = result.transforms.at(p.b);
    checkPosition(b, 0.0, 0.0, 0.0);
    checkDirection(axisOf(b), 0.0, 0.0, 1.0);
    // The freedom that survived is the turn, and it survived untouched:
    // 30 degrees about Z is (cos 30, sin 30, 0).
    checkDirection(rollOf(b), kCos30, 0.5, 0.0);
}

TEST_CASE("Revolute_PullsATiltedAxisBackOntoItsLine", "[assembly][mate][mechanical][p13]") {
    // The other half: a tilt is not a freedom a hinge has, so it is removed.
    Pair p = makePair({.translation = {0_mm, 0_mm, 5_mm}, .rotation = {20_deg, 0_deg, 0_deg}});
    add(p.rig, "Hinge", {.type = MateType::Revolute, .a = axis(p.a), .b = axis(p.b)});

    const auto result = solved(p.rig);
    REQUIRE(result.solved());
    CHECK(result.degreesOfFreedom == 1);
    const RigidTransform3D& b = result.transforms.at(p.b);
    checkDirection(axisOf(b), 0.0, 0.0, 1.0);
    checkPosition(b, 0.0, 0.0, 0.0);
}

// --- Slider -------------------------------------------------------------------------------------

TEST_CASE("Slider_KeepsTheSlideAlongItsAxisAndRemovesTheTurn", "[assembly][mate][mechanical][p13]") {
    // The mirror image of the revolute, and the case that shows the two are
    // not the same joint with the same DOF count. b starts 12 mm and -5 mm
    // off the axis, 30 mm along it, and turned 25 degrees about it. The
    // offsets and the turn must go; the 30 mm must not.
    Pair p = makePair({.translation = {12_mm, -5_mm, 30_mm}, .rotation = {0_deg, 0_deg, 25_deg}});
    add(p.rig, "Slide", slider(p.a, p.b));

    const auto result = solved(p.rig);
    REQUIRE(result.solved());
    CHECK(result.status == SolveStatus::UnderConstrained);
    CHECK(result.equations == 5);
    CHECK(result.degreesOfFreedom == 1);
    CHECK(result.redundant.empty());

    const RigidTransform3D& b = result.transforms.at(p.b);
    // Off-axis translation gone, the slide kept exactly.
    checkPosition(b, 0.0, 0.0, 0.030);
    checkDirection(axisOf(b), 0.0, 0.0, 1.0);
    // And the turn the roll reference exists to remove is gone: b's own X is
    // back on A's X.
    checkDirection(rollOf(b), 1.0, 0.0, 0.0);
}

TEST_CASE("Slider_WithoutItsRollReferenceWouldKeepATurnItMustNotKeep", "[assembly][mate][mechanical][p13]") {
    // Why the second target pair exists, measured rather than argued. The
    // same geometry under a cylindrical joint -- the slider's equations
    // without the roll row -- keeps the 25 degree turn and reports one more
    // degree of freedom. A slider that quietly behaved like this would pass
    // any test that only counted DOF against the wrong expectation.
    Pair loose = makePair({.translation = {12_mm, -5_mm, 30_mm}, .rotation = {0_deg, 0_deg, 25_deg}});
    add(loose.rig, "Sleeve", {.type = MateType::Cylindrical, .a = axis(loose.a), .b = axis(loose.b)});
    const auto without = solved(loose.rig);
    REQUIRE(without.solved());
    CHECK(without.equations == 4);
    CHECK(without.degreesOfFreedom == 2);
    checkDirection(rollOf(without.transforms.at(loose.b)), kCos25, kSin25, 0.0);

    Pair tight = makePair({.translation = {12_mm, -5_mm, 30_mm}, .rotation = {0_deg, 0_deg, 25_deg}});
    add(tight.rig, "Slide", slider(tight.a, tight.b));
    const auto with = solved(tight.rig);
    REQUIRE(with.solved());
    CHECK(with.equations == 5);
    CHECK(with.degreesOfFreedom == 1);
    checkDirection(rollOf(with.transforms.at(tight.b)), 1.0, 0.0, 0.0);
}

// --- Cylindrical --------------------------------------------------------------------------------

TEST_CASE("Cylindrical_KeepsBothTheSlideAndTheTurn", "[assembly][mate][mechanical][p13]") {
    // A shaft that turns and slides: 4 equations, and the two freedoms left
    // are exactly the two it started displaced in. b starts 10 mm and 6 mm
    // off the axis, 40 mm along it, turned 35 degrees about it.
    Pair p = makePair({.translation = {10_mm, 6_mm, 40_mm}, .rotation = {0_deg, 0_deg, 35_deg}});
    add(p.rig, "Sleeve", {.type = MateType::Cylindrical, .a = axis(p.a), .b = axis(p.b)});

    const auto result = solved(p.rig);
    REQUIRE(result.solved());
    CHECK(result.equations == 4);
    CHECK(result.degreesOfFreedom == 2);
    CHECK(result.redundant.empty());

    const RigidTransform3D& b = result.transforms.at(p.b);
    // Only the off-axis translation was removed.
    checkPosition(b, 0.0, 0.0, 0.040);
    checkDirection(axisOf(b), 0.0, 0.0, 1.0);
    checkDirection(rollOf(b), kCos35, kSin35, 0.0);
}

// --- Planar -------------------------------------------------------------------------------------

TEST_CASE("Planar_KeepsTheInPlaneMotionAndRemovesTheSeparation", "[assembly][mate][mechanical][p13]") {
    // Three equations, three freedoms: slide in two directions and spin
    // about the normal. b starts 12 mm and -8 mm across the plane, 45 mm off
    // it, and spun 28 degrees. Only the 45 mm is not a freedom.
    Pair p = makePair({.translation = {12_mm, -8_mm, 45_mm}, .rotation = {0_deg, 0_deg, 28_deg}});
    add(p.rig, "Face", {.type = MateType::Planar, .a = plane(p.a), .b = plane(p.b)});

    const auto result = solved(p.rig);
    REQUIRE(result.solved());
    CHECK(result.equations == 3);
    CHECK(result.degreesOfFreedom == 3);
    CHECK(result.redundant.empty());

    const RigidTransform3D& b = result.transforms.at(p.b);
    checkPosition(b, 0.012, -0.008, 0.0);
    checkDirection(axisOf(b), 0.0, 0.0, 1.0);
    checkDirection(rollOf(b), kCos28, kSin28, 0.0);
}

TEST_CASE("Planar_PullsATiltedFaceFlat", "[assembly][mate][mechanical][p13]") {
    Pair p = makePair({.translation = {0_mm, 0_mm, 20_mm}, .rotation = {15_deg, -10_deg, 0_deg}});
    add(p.rig, "Face", {.type = MateType::Planar, .a = plane(p.a), .b = plane(p.b)});

    const auto result = solved(p.rig);
    REQUIRE(result.solved());
    CHECK(result.degreesOfFreedom == 3);
    checkDirection(axisOf(result.transforms.at(p.b)), 0.0, 0.0, 1.0);
}

// --- Residuals ----------------------------------------------------------------------------------

TEST_CASE("MechanicalMates_AreAlreadySatisfiedWhenTheGeometryAlreadyFits", "[assembly][mate][mechanical][p13]") {
    // An exactly satisfied start: the residual is zero and the solve does no
    // work. Checked for each joint, because a residual that is not zero at a
    // state the engineer would call correct is a wrong equation, whatever it
    // converges to from elsewhere.
    const auto alreadyFits = [](MateType type, const ComponentPlacement& placement) {
        Pair p = makePair(placement);
        MateDefinition d{.type = type, .a = axis(p.a), .b = axis(p.b)};
        if (type == MateType::Planar) {
            d.a = plane(p.a);
            d.b = plane(p.b);
        } else if (type == MateType::Slider) {
            d = slider(p.a, p.b);
        }
        add(p.rig, "Joint", d);
        const auto result = solved(p.rig);
        REQUIRE(result.solved());
        INFO("mate: " << assembly::toString(type));
        CHECK_THAT(result.maxResidual.si(), WithinAbs(0.0, 1e-12));
        CHECK(result.iterations == 0);
    };
    // A revolute is satisfied at the origin, turned about its own axis.
    alreadyFits(MateType::Revolute, {.rotation = {0_deg, 0_deg, 40_deg}});
    // A slider is satisfied anywhere along its axis, not turned.
    alreadyFits(MateType::Slider, {.translation = {0_mm, 0_mm, 55_mm}});
    // A cylindrical joint is satisfied at both.
    alreadyFits(MateType::Cylindrical, {.translation = {0_mm, 0_mm, 55_mm}, .rotation = {0_deg, 0_deg, 40_deg}});
    // A planar joint is satisfied anywhere in its plane.
    alreadyFits(MateType::Planar, {.translation = {18_mm, -4_mm, 0_mm}, .rotation = {0_deg, 0_deg, 40_deg}});
}

// --- Combined with the basic constraints --------------------------------------------------------

TEST_CASE("Revolute_WithAnAngleMateHasNoFreedomLeft", "[assembly][mate][mechanical][p13]") {
    // The hinge leaves one turn; an Angle between the two X axes fixes it.
    // 5 + 1 = 6 independent equations against 6 unknowns.
    Pair p = makePair({.translation = {8_mm, 0_mm, 12_mm}, .rotation = {0_deg, 0_deg, 50_deg}});
    add(p.rig, "Hinge", {.type = MateType::Revolute, .a = axis(p.a), .b = axis(p.b)});
    add(p.rig, "Stop",
        {.type = MateType::Angle,
         .a = axis(p.a, PrincipalAxis::X),
         .b = axis(p.b, PrincipalAxis::X),
         .angle = 30_deg});

    const auto result = solved(p.rig);
    REQUIRE(result.solved());
    CHECK(result.status == SolveStatus::FullyConstrained);
    CHECK(result.equations == 6);
    CHECK(result.degreesOfFreedom == 0);
    CHECK(result.redundant.empty());

    const RigidTransform3D& b = result.transforms.at(p.b);
    checkPosition(b, 0.0, 0.0, 0.0);
    // The turn is now 30 degrees, not the 50 it started at.
    CHECK_THAT(rollOf(b).x(), WithinAbs(kCos30, kTolerance));
}

TEST_CASE("Slider_WithADistanceMateHasNoFreedomLeft", "[assembly][mate][mechanical][p13]") {
    // The slide leaves one translation; a Distance between the two XY planes
    // fixes where along the axis it sits. 5 + 1 = 6.
    Pair p = makePair({.translation = {7_mm, 3_mm, 80_mm}, .rotation = {0_deg, 0_deg, 18_deg}});
    add(p.rig, "Slide", slider(p.a, p.b));
    add(p.rig, "Stop", {.type = MateType::Distance, .a = plane(p.a), .b = plane(p.b), .distance = 35_mm});

    const auto result = solved(p.rig);
    REQUIRE(result.solved());
    CHECK(result.status == SolveStatus::FullyConstrained);
    CHECK(result.equations == 6);
    CHECK(result.degreesOfFreedom == 0);

    const RigidTransform3D& b = result.transforms.at(p.b);
    checkPosition(b, 0.0, 0.0, 0.035);
    checkDirection(rollOf(b), 1.0, 0.0, 0.0);
}

TEST_CASE("Cylindrical_WithADistanceMateKeepsOnlyItsTurn", "[assembly][mate][mechanical][p13]") {
    // 4 + 1 = 5 equations, so one freedom is left, and it is the turn --
    // the distance took the slide.
    Pair p = makePair({.translation = {5_mm, -5_mm, 90_mm}, .rotation = {0_deg, 0_deg, 35_deg}});
    add(p.rig, "Sleeve", {.type = MateType::Cylindrical, .a = axis(p.a), .b = axis(p.b)});
    add(p.rig, "Stop", {.type = MateType::Distance, .a = plane(p.a), .b = plane(p.b), .distance = 20_mm});

    const auto result = solved(p.rig);
    REQUIRE(result.solved());
    CHECK(result.equations == 5);
    CHECK(result.degreesOfFreedom == 1);
    const RigidTransform3D& b = result.transforms.at(p.b);
    checkPosition(b, 0.0, 0.0, 0.020);
    checkDirection(rollOf(b), kCos35, kSin35, 0.0);
}

TEST_CASE("Planar_WithTwoDistanceMatesHasNoFreedomLeft", "[assembly][mate][mechanical][p13]") {
    // The planar joint leaves 3; two Distance mates across the other two
    // principal planes take the two slides, and a Perpendicular takes the
    // spin. 3 + 1 + 1 + 1 = 6.
    Pair p = makePair({.translation = {30_mm, 30_mm, 25_mm}, .rotation = {0_deg, 0_deg, 20_deg}});
    add(p.rig, "Face", {.type = MateType::Planar, .a = plane(p.a), .b = plane(p.b)});
    add(p.rig, "AcrossX",
        {.type = MateType::Distance,
         .a = plane(p.a, PrincipalPlane::YZ),
         .b = plane(p.b, PrincipalPlane::YZ),
         .distance = 10_mm});
    add(p.rig, "AcrossY",
        {.type = MateType::Distance,
         .a = plane(p.a, PrincipalPlane::XZ),
         .b = plane(p.b, PrincipalPlane::XZ),
         .distance = 15_mm});
    // Hand-derived, and the sign matters: a Distance is measured along the
    // FIRST target's normal, and the XZ plane's frame is built right-handed
    // from -Y and X, so its normal is -Y. Fifteen millimetres along -Y is
    // y = -15 mm, not +15. The YZ plane's normal is +X by the same
    // construction, which is why the x offset above is positive.
    add(p.rig, "Square",
        {.type = MateType::Perpendicular, .a = plane(p.a, PrincipalPlane::YZ), .b = plane(p.b, PrincipalPlane::XZ)});

    const auto result = solved(p.rig);
    REQUIRE(result.solved());
    CHECK(result.status == SolveStatus::FullyConstrained);
    CHECK(result.equations == 6);
    CHECK(result.degreesOfFreedom == 0);
    checkPosition(result.transforms.at(p.b), 0.010, -0.015, 0.0);
}

// --- Contradiction, redundancy and failure ------------------------------------------------------

TEST_CASE("Revolute_ContradictedByAPerpendicularAxisIsInconsistent", "[assembly][mate][mechanical][p13]") {
    // A hinge puts b's Z axis on a's; a Perpendicular between the same two
    // directions says they are at a right angle. Both cannot hold.
    Pair p = makePair({});
    add(p.rig, "Hinge", {.type = MateType::Revolute, .a = axis(p.a), .b = axis(p.b)});
    add(p.rig, "Square", {.type = MateType::Perpendicular, .a = axis(p.a), .b = axis(p.b)});

    const auto result = solved(p.rig);
    CHECK(result.status == SolveStatus::Inconsistent);
    CHECK_FALSE(result.solved());
    // No partial answer: a failed solve returns no transforms at all.
    CHECK(result.transforms.empty());
    CHECK_FALSE(result.conflicting.empty());
    CHECK_THAT(result.message, ContainsSubstring("cannot all be satisfied"));
}

TEST_CASE("Slider_ContradictedByAnAngleAboutItsOwnAxisIsInconsistent", "[assembly][mate][mechanical][p13]") {
    // The slide's roll row holds the two X axes coplanar with the slide
    // axis -- 0 or 180 degrees apart. An Angle mate demanding 40 degrees
    // between them contradicts it.
    Pair p = makePair({.translation = {0_mm, 0_mm, 10_mm}});
    add(p.rig, "Slide", slider(p.a, p.b));
    add(p.rig, "Twist",
        {.type = MateType::Angle,
         .a = axis(p.a, PrincipalAxis::X),
         .b = axis(p.b, PrincipalAxis::X),
         .angle = 40_deg});

    const auto result = solved(p.rig);
    CHECK_FALSE(result.solved());
    CHECK(result.status == SolveStatus::Inconsistent);
    CHECK(result.transforms.empty());
}

TEST_CASE("Cylindrical_DuplicatedByAConcentricIsRedundantNotInconsistent", "[assembly][mate][mechanical][p13]") {
    // A cylindrical joint and a concentric mate over the same axes are the
    // same four equations. Every row of the second is dependent, so the
    // assembly is satisfiable and over-constrained -- not contradictory.
    Pair p = makePair({.translation = {9_mm, 0_mm, 25_mm}});
    add(p.rig, "Sleeve", {.type = MateType::Cylindrical, .a = axis(p.a), .b = axis(p.b)});
    add(p.rig, "Bore", {.type = MateType::Concentric, .a = axis(p.a), .b = axis(p.b)});

    const auto result = solved(p.rig);
    CHECK(result.status == SolveStatus::OverConstrained);
    CHECK(result.equations == 8);
    CHECK(result.degreesOfFreedom == 2);
    CHECK(result.maxResidual.si() < 1e-9);
    // The later mate is the one reported, which is the contract the sketch
    // solver states and this one inherits.
    REQUIRE(result.redundant.size() == 1);
}

TEST_CASE("MechanicalMates_RefuseToStartWhenATargetDoesNotResolve", "[assembly][mate][mechanical][p13]") {
    // An unresolved target is an error, not a status: a solve that never
    // started did not find the system inconsistent.
    Pair p = makePair({});
    add(p.rig, "Slide", slider(p.a, p.b));
    REQUIRE(assembly::removeComponent(p.rig.document, p.b).has_value());

    auto result = assembly::solve(p.rig.document);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == ErrorCode::NotFound);
}

TEST_CASE("MechanicalMates_LeaveTheDocumentUntouchedWhenTheSolveFails", "[assembly][mate][mechanical][p13]") {
    Pair p = makePair({.translation = {4_mm, 4_mm, 4_mm}});
    const ComponentPlacement intent{.translation = {4_mm, 4_mm, 4_mm}};
    add(p.rig, "Hinge", {.type = MateType::Revolute, .a = axis(p.a), .b = axis(p.b)});
    add(p.rig, "Square", {.type = MateType::Perpendicular, .a = axis(p.a), .b = axis(p.b)});
    const auto revisionBefore = p.rig.document.revision();

    const auto failed = solved(p.rig);
    REQUIRE_FALSE(failed.solved());
    CHECK(failed.transforms.empty());
    CHECK(assembly::findComponent(p.rig.document, p.b)->definition().placement == intent);
    CHECK(p.rig.document.revision() == revisionBefore);

    // Repairing it makes the assembly solve, which "nothing changed" alone
    // would not prove: the document is not merely untouched, it still works.
    for (const MateId id : assembly::mates(p.rig.document)) {
        if (assembly::findMate(p.rig.document, id)->name() == "Square") {
            REQUIRE(assembly::removeMate(p.rig.document, id).has_value());
        }
    }
    const auto repaired = solved(p.rig);
    REQUIRE(repaired.solved());
    CHECK(repaired.degreesOfFreedom == 1);
    checkPosition(repaired.transforms.at(p.b), 0.0, 0.0, 0.0);
}

// --- Determinism and persistence ----------------------------------------------------------------

TEST_CASE("MechanicalMates_SolveDeterministically", "[assembly][mate][mechanical][p13][determinism]") {
    const auto build = [] {
        Pair p = makePair({.translation = {11_mm, -6_mm, 33_mm}, .rotation = {7_deg, -4_deg, 19_deg}});
        add(p.rig, "Slide", slider(p.a, p.b));
        return p;
    };
    Pair first = build();
    Pair second = build();
    const auto a = solved(first.rig);
    const auto b = solved(second.rig);

    CHECK(a.status == b.status);
    CHECK(a.iterations == b.iterations);
    CHECK(a.degreesOfFreedom == b.degreesOfFreedom);
    CHECK(a.maxResidual == b.maxResidual);
    REQUIRE(a.transforms.size() == b.transforms.size());
    for (const auto& [id, transform] : a.transforms) {
        CHECK(transform == b.transforms.at(id));
    }
}

TEST_CASE("MechanicalMates_SurviveASaveAndReloadAndSolveIdentically", "[assembly][mate][mechanical][p13][io]") {
    TempDir dir;
    Rig rig = makeRig();
    const ComponentId a = place(rig, "Block1");
    const ComponentId b = place(rig, "Block2", {.translation = {14_mm, -9_mm, 26_mm}, .rotation = {0_deg, 0_deg, 22_deg}});
    add(rig, "Ground", {.type = MateType::Fixed, .component = a});
    add(rig, "Slide", slider(a, b));
    const auto before = solved(rig);
    REQUIRE(before.solved());

    const auto path = dir.path() / "joints.bcad";
    REQUIRE(io::saveDocument(rig.document, path).has_value());
    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());

    // The roll reference is intent and must come back intact, or the joint
    // that comes back is a cylindrical one wearing a slider's name.
    const assembly::Mate* slide = nullptr;
    for (const MateId id : assembly::mates(*loaded)) {
        if (assembly::findMate(*loaded, id)->name() == "Slide") {
            slide = assembly::findMate(*loaded, id);
        }
    }
    REQUIRE(slide != nullptr);
    CHECK(slide->definition().type == MateType::Slider);
    REQUIRE(slide->definition().a2.has_value());
    REQUIRE(slide->definition().b2.has_value());
    CHECK(slide->definition().a2->component == a);
    CHECK(slide->definition().b2->component == b);
    CHECK(slide->definition().a2->axis->axis == PrincipalAxis::X);

    auto after = assembly::solve(*loaded);
    REQUIRE(after.has_value());
    CHECK(after->equations == before.equations);
    CHECK(after->degreesOfFreedom == before.degreesOfFreedom);
    REQUIRE(after->transforms.size() == before.transforms.size());
    for (const auto& [id, transform] : before.transforms) {
        CHECK(after->transforms.at(id) == transform);
    }

    // And nothing derived reached the file: saving again after a solve gives
    // the same bytes.
    const auto again = dir.path() / "joints-again.bcad";
    REQUIRE(io::saveDocument(rig.document, again).has_value());
    CHECK(readFile(again) == readFile(path));
}

TEST_CASE("MechanicalMates_AreSuppressible", "[assembly][mate][mechanical][p13]") {
    // A suppressed joint contributes no equations, so the freedom it would
    // have removed is reported as still there.
    Pair p = makePair({.translation = {0_mm, 0_mm, 40_mm}});
    add(p.rig, "Slide", [&] {
        MateDefinition d = slider(p.a, p.b);
        d.suppressed = true;
        return d;
    }());

    const auto result = solved(p.rig);
    REQUIRE(result.solved());
    CHECK(result.equations == 0);
    CHECK(result.unknowns == 6);
    CHECK(result.degreesOfFreedom == 6);
}

// --- Adversarial (P13-MATE-002 section 22) ------------------------------------------------------

TEST_CASE("Slider_WithARollReferenceAlongItsOwnAxisReportsItRatherThanSilentlyBecomingASleeve",
          "[assembly][mate][mechanical][p13]") {
    // The failure this design could have hidden. The roll row is
    // cross(Ra, Rb) . D, so a roll reference pointing along the slide axis
    // makes it identically zero: the row says nothing, the slide keeps the
    // turn it exists to remove, and a caller who only counted equations
    // would see five and believe it.
    //
    // A component's XY plane normal is its Z direction -- the slide axis --
    // but it is a different target, so validation passes it. What catches it
    // is the rank analysis: a row that adds nothing to the span is a
    // dependent row, and the mate that produced it is named.
    Pair p = makePair({.translation = {0_mm, 0_mm, 20_mm}, .rotation = {0_deg, 0_deg, 25_deg}});
    MateDefinition degenerate = slider(p.a, p.b);
    degenerate.a2 = plane(p.a);
    degenerate.b2 = plane(p.b);
    add(p.rig, "Slide", degenerate);

    const auto result = solved(p.rig);
    CHECK(result.equations == 5);
    // Five equations, but only four of them independent.
    CHECK(result.degreesOfFreedom == 2);
    CHECK(result.status == SolveStatus::OverConstrained);
    REQUIRE(result.redundant.size() == 1);
    CHECK_THAT(result.message, ContainsSubstring("redundant"));
    // An over-constrained result carries no transforms -- P13-SOLVE-001's
    // contract, which this case inherits rather than changes. So the caller
    // gets a diagnostic naming the mate that contributed nothing, and no
    // positions, which is the outcome to want here: a slide that is not one
    // should not quietly hand back the sleeve's answer.
    CHECK(result.transforms.empty());
    CHECK_FALSE(result.solved());
}

TEST_CASE("Revolute_HingesJustAsWellWithItsAxesPointingOppositeWays", "[assembly][mate][mechanical][p13]") {
    // A hinge does not care which way round the pin goes, and the Parallel
    // rows are satisfied in either sense, so an axis reversed by a 180
    // degree flip must still solve rather than fight.
    Pair p = makePair({.translation = {14_mm, 0_mm, 18_mm}, .rotation = {180_deg, 0_deg, 0_deg}});
    add(p.rig, "Hinge", {.type = MateType::Revolute, .a = axis(p.a), .b = axis(p.b)});

    const auto result = solved(p.rig);
    REQUIRE(result.solved());
    CHECK(result.degreesOfFreedom == 1);
    const RigidTransform3D& b = result.transforms.at(p.b);
    checkPosition(b, 0.0, 0.0, 0.0);
    // Collinear, in whichever sense it settled: the axis is the Z line.
    const Direction3D settled = axisOf(b);
    CHECK_THAT(std::abs(settled.z()), WithinAbs(1.0, kTolerance));
    CHECK_THAT(settled.x(), WithinAbs(0.0, kTolerance));
    CHECK_THAT(settled.y(), WithinAbs(0.0, kTolerance));
}

TEST_CASE("Planar_AcceptsFacesWhoseNormalsOppose", "[assembly][mate][mechanical][p13]") {
    // Two faces in contact have opposing normals, and two coplanar faces of
    // one slab have aligned ones. The joint is built on Parallel rows, which
    // are satisfied either way, so it accepts both -- recorded here because
    // a reader may expect a contact semantics this mate does not claim.
    Pair p = makePair({.translation = {9_mm, 4_mm, 30_mm}, .rotation = {180_deg, 0_deg, 0_deg}});
    add(p.rig, "Face", {.type = MateType::Planar, .a = plane(p.a), .b = plane(p.b)});

    const auto result = solved(p.rig);
    REQUIRE(result.solved());
    CHECK(result.degreesOfFreedom == 3);
    const RigidTransform3D& b = result.transforms.at(p.b);
    // In the plane, and still facing the way it was put.
    CHECK_THAT(b.apply(Point3D{}).z.si(), WithinAbs(0.0, kTolerance));
    CHECK_THAT(std::abs(axisOf(b).z()), WithinAbs(1.0, kTolerance));
}

TEST_CASE("MechanicalMates_DoNotDependOnTheOrderTheyWereAdded", "[assembly][mate][mechanical][p13]") {
    // The transforms must not depend on the order the mates were written in.
    // Only which mate is named redundant does, and that follows ID order by
    // contract.
    const auto build = [](bool jointFirst) {
        Pair p = makePair({.translation = {6_mm, -6_mm, 44_mm}, .rotation = {0_deg, 0_deg, 12_deg}});
        const auto joint = [&] { add(p.rig, "Sleeve", {.type = MateType::Cylindrical, .a = axis(p.a), .b = axis(p.b)}); };
        const auto stop = [&] {
            add(p.rig, "Stop", {.type = MateType::Distance, .a = plane(p.a), .b = plane(p.b), .distance = 20_mm});
        };
        if (jointFirst) {
            joint();
            stop();
        } else {
            stop();
            joint();
        }
        return p;
    };
    Pair first = build(true);
    Pair second = build(false);
    const auto a = solved(first.rig);
    const auto b = solved(second.rig);

    REQUIRE(a.solved());
    REQUIRE(b.solved());
    CHECK(a.degreesOfFreedom == b.degreesOfFreedom);
    CHECK(a.equations == b.equations);
    checkPosition(a.transforms.at(first.b), 0.0, 0.0, 0.020);
    checkPosition(b.transforms.at(second.b), 0.0, 0.0, 0.020);
    checkDirection(rollOf(a.transforms.at(first.b)), rollOf(b.transforms.at(second.b)).x(),
                   rollOf(b.transforms.at(second.b)).y(), rollOf(b.transforms.at(second.b)).z());
}
