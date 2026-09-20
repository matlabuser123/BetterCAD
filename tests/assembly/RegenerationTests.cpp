#include "features/FeatureTestSupport.hpp"
#include "support/TestFiles.hpp"

#include <bettercad/assembly/Component.hpp>
#include <bettercad/assembly/Components.hpp>
#include <bettercad/assembly/Configurations.hpp>
#include <bettercad/assembly/Mate.hpp>
#include <bettercad/assembly/Mates.hpp>
#include <bettercad/assembly/Placement.hpp>
#include <bettercad/assembly/Resolution.hpp>
#include <bettercad/assembly/Solver.hpp>
#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/DependencyGraph.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/document/MateReference.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using assembly::AssemblyRegeneration;
using assembly::MateDefinition;
using assembly::MateType;
using assembly::SolveStatus;
using assembly::SolveTrigger;
using features::NodeState;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;

// P13-REGEN-001: the milestone that finally consumes the solver.
//
// The failure this file exists to catch is STALE DERIVED STATE THAT LOOKS
// CURRENT. A transform left over from before a mate changed is worse than no
// transform: the assembly renders, the positions are plausible, and they
// answer a question nobody asked any more. So it is never enough here to
// assert that a solve happened -- every case asserts WHICH transforms are in
// force afterwards.
//
// Its companion is the opposite failure, re-solving when nothing relevant
// moved, which is how a CAD system becomes unusable on a large assembly.
// That is why the trigger is observable and asserted in both directions.

namespace {

constexpr double kTolerance = 1e-9;

struct Rig {
    Document document{"Assembly"};
    features::Regenerator regenerator{};
    AssemblyRegeneration assembly{};
    ObjectId sketch{};
    ObjectId part{};
    ComponentId base{};
    ComponentId arm{};
    MateId ground{};
    MateId holds{};

    features::RegenerationReport regenerate() { return requireReport(regenerator, document); }
    [[nodiscard]] const RigidTransform3D* transform(ComponentId id) const {
        return regenerator.transform(id);
    }
};

/// A grounded base and an arm held to it, 50 mm up until the mate pulls it
/// down. Hand-derived throughout: the arm's XY plane is made coincident with
/// the base's, which sits at z = 0, so the arm solves to the origin.
std::unique_ptr<Rig> makeRig(Length depth = 10_mm) {
    auto rig = std::make_unique<Rig>();
    assembly::registerHandlers(rig->regenerator, nullptr, &rig->assembly);
    auto sketch = std::make_unique<sketch::Sketch>("BlockSketch", Frame3D::xy());
    addRectangle(*sketch, 0_mm, 0_mm, 40_mm, 30_mm);
    rig->sketch = require(rig->document.addObject(std::move(sketch)));
    auto extrude = features::ExtrudeFeature::create(
        "Block", {.profile = SketchId::fromValue(rig->sketch.value()), .depth = depth});
    REQUIRE(extrude.has_value());
    rig->part = require(rig->document.addObject(std::move(*extrude)));
    rig->base = require(assembly::createComponent(rig->document, "Base", {.part = rig->part}));
    rig->arm = require(assembly::createComponent(
        rig->document, "Arm", {.part = rig->part, .placement = {.translation = {0_mm, 0_mm, 50_mm}}}));
    rig->ground = require(assembly::createMate(rig->document, "Ground",
                                               {.type = MateType::Fixed, .component = rig->base}));
    rig->holds = require(assembly::createMate(
        rig->document, "Holds",
        {.type = MateType::Coincident,
         .a = planeTarget(rig->base, PlaneReference{}),
         .b = planeTarget(rig->arm, PlaneReference{})}));
    return rig;
}

void checkAt(const RigidTransform3D* transform, double x, double y, double z) {
    REQUIRE(transform != nullptr);
    const Point3D origin = transform->apply(Point3D{});
    CHECK_THAT(origin.x.si(), WithinAbs(x, kTolerance));
    CHECK_THAT(origin.y.si(), WithinAbs(y, kTolerance));
    CHECK_THAT(origin.z.si(), WithinAbs(z, kTolerance));
}

[[nodiscard]] bool contains(const std::vector<ObjectId>& list, ObjectId id) {
    return std::ranges::find(list, id) != list.end();
}

} // namespace

// --- The contract ---------------------------------------------------------------------------------

TEST_CASE("Regeneration_SolvesTheAssemblyAndPublishesItsTransforms", "[assembly][regen][p13]") {
    // The line four milestones carried forward -- "nothing consumes the
    // solved transforms yet" -- closing. Regenerating now solves, and the
    // transforms are available beside the bodies.
    auto rig = makeRig();
    const features::RegenerationReport report = rig->regenerate();
    CHECK(report.succeeded());

    CHECK(rig->assembly.trigger == SolveTrigger::First);
    REQUIRE(rig->assembly.status.has_value());
    CHECK(*rig->assembly.status == SolveStatus::UnderConstrained);
    CHECK(rig->assembly.transforms == 2);

    // Hand-derived: the arm started 50 mm up and the Coincident brings it to
    // the base's XY plane at z = 0.
    checkAt(rig->transform(rig->base), 0.0, 0.0, 0.0);
    checkAt(rig->transform(rig->arm), 0.0, 0.0, 0.0);
    // The canonical intent is untouched -- it still says 50 mm.
    CHECK(assembly::findComponent(rig->document, rig->arm)->definition().placement.translation[2] == 50_mm);
}

TEST_CASE("Regeneration_LeavesCanonicalIntentAlone", "[assembly][regen][p13]") {
    // The hard gate. Regeneration may write derived transforms and nothing
    // else.
    auto rig = makeRig();
    const ComponentPlacement intent{.translation = {0_mm, 0_mm, 50_mm}};
    const MateDefinition mate = assembly::findMate(rig->document, rig->holds)->definition();
    rig->regenerate();
    rig->regenerate();
    rig->regenerate();

    CHECK(assembly::findComponent(rig->document, rig->arm)->definition().placement == intent);
    CHECK(assembly::findMate(rig->document, rig->holds)->definition() == mate);
    CHECK_FALSE(rig->document.activeConfiguration().has_value());
}

// --- Solve triggering ------------------------------------------------------------------------------

TEST_CASE("Regeneration_DoesNotReSolveWhenNothingItReadsChanged", "[assembly][regen][p13]") {
    // The companion failure: re-solving for nothing is how a CAD system
    // becomes unusable on a large assembly.
    auto rig = makeRig();
    rig->regenerate();
    REQUIRE(rig->assembly.trigger == SolveTrigger::First);

    for (int pass = 0; pass < 5; ++pass) {
        INFO("pass " << pass);
        rig->regenerate();
        CHECK(rig->assembly.trigger == SolveTrigger::NotNeeded);
        // And the transforms it produced before still stand.
        checkAt(rig->transform(rig->arm), 0.0, 0.0, 0.0);
    }
}

TEST_CASE("Regeneration_ReSolvesWhenAMateValueChanges", "[assembly][regen][p13]") {
    auto rig = makeRig();
    rig->regenerate();
    checkAt(rig->transform(rig->arm), 0.0, 0.0, 0.0);

    // Replace the Coincident with a Distance of 30 mm: hand-derived, the arm
    // must end at z = 30 mm.
    REQUIRE(assembly::setMateDefinition(rig->document, rig->holds,
                                        {.type = MateType::Distance,
                                         .a = planeTarget(rig->base, PlaneReference{}),
                                         .b = planeTarget(rig->arm, PlaneReference{}),
                                         .distance = 30_mm})
                .has_value());
    rig->regenerate();

    CHECK(rig->assembly.trigger == SolveTrigger::ObjectChanged);
    checkAt(rig->transform(rig->arm), 0.0, 0.0, 0.030);
}

TEST_CASE("Regeneration_ReSolvesWhenPlacementIntentChanges", "[assembly][regen][p13]") {
    // A Concentric leaves the slide along the axis free, so moving the
    // placement moves where the arm settles -- which is exactly the case
    // where a stale transform would be invisible.
    auto rig = makeRig();
    REQUIRE(assembly::setMateDefinition(rig->document, rig->holds,
                                        {.type = MateType::Concentric,
                                         .a = axisTarget(rig->base, AxisReference{}),
                                         .b = axisTarget(rig->arm, AxisReference{})})
                .has_value());
    rig->regenerate();
    checkAt(rig->transform(rig->arm), 0.0, 0.0, 0.050);

    REQUIRE(assembly::setComponentDefinition(
                rig->document, rig->arm,
                {.part = rig->part, .placement = {.translation = {0_mm, 0_mm, 80_mm}}})
                .has_value());
    rig->regenerate();

    CHECK(rig->assembly.trigger == SolveTrigger::ObjectChanged);
    checkAt(rig->transform(rig->arm), 0.0, 0.0, 0.080);
}

TEST_CASE("Regeneration_ReSolvesWhenAConfigurationIsSwitched", "[assembly][regen][p13]") {
    auto rig = makeRig();
    rig->regenerate();
    CHECK(rig->assembly.transforms == 2);

    const ConfigurationId lean = require(rig->document.createConfiguration("Lean"));
    REQUIRE(assembly::suppressComponent(rig->document, lean, rig->arm, true).has_value());
    REQUIRE(rig->document.setActiveConfiguration(lean).has_value());
    rig->regenerate();

    CHECK(rig->assembly.trigger == SolveTrigger::ConfigurationChanged);
    // The arm is not in this build, so it has no transform at all -- not a
    // stale one from the build it was in.
    CHECK(rig->assembly.transforms == 1);
    CHECK(rig->transform(rig->arm) == nullptr);
    checkAt(rig->transform(rig->base), 0.0, 0.0, 0.0);

    REQUIRE(rig->document.setActiveConfiguration(std::nullopt).has_value());
    rig->regenerate();
    CHECK(rig->assembly.trigger == SolveTrigger::ConfigurationChanged);
    CHECK(rig->assembly.transforms == 2);
    checkAt(rig->transform(rig->arm), 0.0, 0.0, 0.0);
}

TEST_CASE("Regeneration_ReSolvesWhenSuppressionChangesWithoutSwitching", "[assembly][regen][p13]") {
    // Editing the active configuration's overrides changes what is in force
    // without changing which configuration is active.
    auto rig = makeRig();
    const ConfigurationId lean = require(rig->document.createConfiguration("Lean"));
    REQUIRE(rig->document.setActiveConfiguration(lean).has_value());
    rig->regenerate();
    CHECK(rig->assembly.transforms == 2);

    REQUIRE(assembly::suppressMate(rig->document, lean, rig->holds, true).has_value());
    rig->regenerate();

    CHECK(rig->assembly.trigger == SolveTrigger::MatesInForceChanged);
    // The mate no longer holds the arm down, so it stays where its intent
    // puts it: 50 mm up.
    checkAt(rig->transform(rig->arm), 0.0, 0.0, 0.050);
}

TEST_CASE("Regeneration_ReSolvesWhenTheGeometryBeneathAComponentMoves", "[assembly][regen][p13]") {
    // The upstream case: the part regenerates, which moves the face the mate
    // names, which must move the arm. A stale transform here would be a part
    // sitting where the old geometry used to be.
    auto rig = makeRig(10_mm);
    REQUIRE(assembly::setMateDefinition(
                rig->document, rig->holds,
                {.type = MateType::Coincident,
                 .a = faceTarget(rig->base, FaceName{.feature = rig->part, .face = {.role = FaceRole::EndCap}}),
                 .b = planeTarget(rig->arm, PlaneReference{})})
                .has_value());
    rig->regenerate();
    // Hand-derived: the base's end cap is at z = 10 mm, so the arm's XY plane
    // lands there.
    checkAt(rig->transform(rig->arm), 0.0, 0.0, 0.010);

    const auto* extrude = rig->document.findObjectAs<features::ExtrudeFeature>(rig->part);
    auto definition = extrude->definition();
    definition.depth = 25_mm;
    REQUIRE(rig->document
                .modifyObject<features::ExtrudeFeature>(
                    rig->part, [&](features::ExtrudeFeature& f) { return f.setDefinition(definition); })
                .has_value());
    rig->regenerate();

    CHECK(rig->assembly.trigger == SolveTrigger::ObjectChanged);
    checkAt(rig->transform(rig->arm), 0.0, 0.0, 0.025);
}

TEST_CASE("Regeneration_ReSolvesWhenAConfigurationOverridesAFreeParameter", "[assembly][regen][p13]") {
    // The case a revision-based trigger would miss entirely. A configuration
    // overriding a FREE parameter changes no object's revision -- the base
    // value is untouched and only the value in force differs -- so nothing
    // downstream looks dirty. The trigger compares resolved placements, which
    // is why it is caught.
    auto rig = makeRig();
    const ParameterId lift = require(rig->document.createParameter("lift", 50_mm, units::mm));
    REQUIRE(assembly::setMateDefinition(rig->document, rig->holds,
                                        {.type = MateType::Concentric,
                                         .a = axisTarget(rig->base, AxisReference{}),
                                         .b = axisTarget(rig->arm, AxisReference{})})
                .has_value());
    REQUIRE(assembly::setComponentDefinition(
                rig->document, rig->arm,
                {.part = rig->part,
                 .placement = {.translation = {0_mm, 0_mm, 0_mm},
                               .translationParameters = {std::nullopt, std::nullopt, lift}}})
                .has_value());
    rig->regenerate();
    checkAt(rig->transform(rig->arm), 0.0, 0.0, 0.050);

    const ConfigurationId tall = require(rig->document.createConfiguration("Tall"));
    REQUIRE(rig->document.setConfigurationOverride(tall, lift, 90_mm).has_value());
    REQUIRE(rig->document.setActiveConfiguration(tall).has_value());
    rig->regenerate();
    CHECK(rig->assembly.trigger == SolveTrigger::ConfigurationChanged);
    checkAt(rig->transform(rig->arm), 0.0, 0.0, 0.090);

    // And editing the override of the configuration already active, which
    // changes neither the active ID nor any object revision.
    REQUIRE(rig->document.setConfigurationOverride(tall, lift, 120_mm).has_value());
    rig->regenerate();
    CHECK(rig->assembly.trigger == SolveTrigger::PlacementChanged);
    checkAt(rig->transform(rig->arm), 0.0, 0.0, 0.120);
}

TEST_CASE("Regeneration_DoesNotReSolveForAnUnrelatedChange", "[assembly][regen][p13]") {
    // A second part, with no component placing it, regenerates without
    // touching the assembly.
    auto rig = makeRig();
    rig->regenerate();
    REQUIRE(rig->assembly.trigger == SolveTrigger::First);

    auto sketch = std::make_unique<sketch::Sketch>("OtherSketch", Frame3D::xy());
    addRectangle(*sketch, 0_mm, 0_mm, 10_mm, 10_mm);
    const ObjectId otherSketch = require(rig->document.addObject(std::move(sketch)));
    auto other = features::ExtrudeFeature::create(
        "Other", {.profile = SketchId::fromValue(otherSketch.value()), .depth = 5_mm});
    REQUIRE(other.has_value());
    const ObjectId otherPart = require(rig->document.addObject(std::move(*other)));

    const features::RegenerationReport report = rig->regenerate();
    // The new part was built...
    CHECK(contains(report.regenerated, otherPart));
    // ...and the assembly did not re-solve for it.
    CHECK(rig->assembly.trigger == SolveTrigger::NotNeeded);
    checkAt(rig->transform(rig->arm), 0.0, 0.0, 0.0);
}

// --- Affected-state propagation --------------------------------------------------------------------

TEST_CASE("Regeneration_RebuildsOnlyWhatDependsOnTheChange", "[assembly][regen][p13]") {
    // Two parts, one component each. Editing one part must not rebuild the
    // other, and the report says exactly which objects were touched.
    auto rig = makeRig();
    auto sketch = std::make_unique<sketch::Sketch>("OtherSketch", Frame3D::xy());
    addRectangle(*sketch, 0_mm, 0_mm, 10_mm, 10_mm);
    const ObjectId otherSketch = require(rig->document.addObject(std::move(sketch)));
    auto other = features::ExtrudeFeature::create(
        "Other", {.profile = SketchId::fromValue(otherSketch.value()), .depth = 5_mm});
    REQUIRE(other.has_value());
    const ObjectId otherPart = require(rig->document.addObject(std::move(*other)));
    rig->regenerate();

    const auto* extrude = rig->document.findObjectAs<features::ExtrudeFeature>(otherPart);
    auto definition = extrude->definition();
    definition.depth = 9_mm;
    REQUIRE(rig->document
                .modifyObject<features::ExtrudeFeature>(
                    otherPart, [&](features::ExtrudeFeature& f) { return f.setDefinition(definition); })
                .has_value());
    const features::RegenerationReport report = rig->regenerate();

    CHECK(contains(report.regenerated, otherPart));
    // The first part, its components and its mates were not rebuilt.
    CHECK_FALSE(contains(report.regenerated, rig->part));
    CHECK_FALSE(contains(report.regenerated, ObjectId{rig->base}));
    CHECK_FALSE(contains(report.regenerated, ObjectId{rig->arm}));
    CHECK_FALSE(contains(report.regenerated, ObjectId{rig->holds}));
    CHECK(rig->regenerator.state(rig->part) == NodeState::UpToDate);
}

TEST_CASE("Regeneration_OrdersUpstreamGeometryBeforeTheComponentsThatPlaceIt",
          "[assembly][regen][p13]") {
    // Ordering is what makes the solve correct: a component must not be
    // evaluated before the part it places, and a mate not before the
    // components it relates.
    auto rig = makeRig();
    const features::RegenerationReport report = rig->regenerate();

    const auto position = [&](ObjectId id) {
        const auto at = std::ranges::find(report.regenerated, id);
        REQUIRE(at != report.regenerated.end());
        return std::distance(report.regenerated.begin(), at);
    };
    CHECK(position(rig->sketch) < position(rig->part));
    CHECK(position(rig->part) < position(ObjectId{rig->base}));
    CHECK(position(rig->part) < position(ObjectId{rig->arm}));
    CHECK(position(ObjectId{rig->base}) < position(ObjectId{rig->holds}));
    CHECK(position(ObjectId{rig->arm}) < position(ObjectId{rig->holds}));
    // And the solve runs after all of them -- it is a final pass, so its
    // result exists once the objects are done.
    CHECK(rig->assembly.transforms == 2);
}

// --- Unresolved references and failure ---------------------------------------------------------------

TEST_CASE("Regeneration_FailsAMateWhoseTargetDoesNotResolve", "[assembly][regen][p13]") {
    // Mates had no handler before this milestone, so an unresolvable target
    // was silent during regeneration. Now it fails, and says so.
    auto rig = makeRig();
    REQUIRE(assembly::setMateDefinition(
                rig->document, rig->holds,
                {.type = MateType::Coincident,
                 .a = faceTarget(rig->base, FaceName{.feature = rig->part, .face = {.role = FaceRole::EndCap}}),
                 .b = planeTarget(rig->arm, PlaneReference{})})
                .has_value());
    rig->regenerate();
    checkAt(rig->transform(rig->arm), 0.0, 0.0, 0.010);

    // Point the face at a feature that makes no such face.
    REQUIRE(assembly::setMateDefinition(
                rig->document, rig->holds,
                {.type = MateType::Coincident,
                 .a = faceTarget(rig->base, FaceName{.feature = rig->sketch, .face = {.role = FaceRole::EndCap}}),
                 .b = planeTarget(rig->arm, PlaneReference{})})
                .has_value());
    const features::RegenerationReport report = rig->regenerate();

    CHECK_FALSE(report.succeeded());
    CHECK(contains(report.failed, ObjectId{rig->holds}));
    CHECK(rig->regenerator.state(ObjectId{rig->holds}) == NodeState::Failed);
    CHECK(rig->regenerator.error(ObjectId{rig->holds}) != nullptr);
    // And no transforms at all: not a partial set, and not the previous
    // pass's, which would render as if nothing were wrong.
    CHECK(rig->assembly.trigger == SolveTrigger::Broken);
    CHECK(rig->assembly.transforms == 0);
    CHECK(rig->transform(rig->arm) == nullptr);
    CHECK(rig->transform(rig->base) == nullptr);
}

TEST_CASE("Regeneration_RecoversWhenTheBrokenTargetIsRepaired", "[assembly][regen][p13]") {
    auto rig = makeRig();
    const MateDefinition good{.type = MateType::Coincident,
                              .a = planeTarget(rig->base, PlaneReference{}),
                              .b = planeTarget(rig->arm, PlaneReference{})};
    REQUIRE(assembly::setMateDefinition(
                rig->document, rig->holds,
                {.type = MateType::Coincident,
                 .a = faceTarget(rig->base, FaceName{.feature = rig->sketch, .face = {.role = FaceRole::EndCap}}),
                 .b = planeTarget(rig->arm, PlaneReference{})})
                .has_value());
    rig->regenerate();
    REQUIRE(rig->assembly.transforms == 0);
    // Canonical state survived the failure.
    CHECK(assembly::findComponent(rig->document, rig->arm)->definition().placement.translation[2] == 50_mm);

    REQUIRE(assembly::setMateDefinition(rig->document, rig->holds, good).has_value());
    const features::RegenerationReport report = rig->regenerate();

    CHECK(report.succeeded());
    CHECK(rig->assembly.transforms == 2);
    checkAt(rig->transform(rig->arm), 0.0, 0.0, 0.0);
}

TEST_CASE("Regeneration_DropsTransformsWhenAComponentsPartFails", "[assembly][regen][p13]") {
    // A component whose part is gone fails, and the assembly it was part of
    // publishes nothing rather than a set with a hole in it.
    auto rig = makeRig();
    rig->regenerate();
    REQUIRE(rig->assembly.transforms == 2);

    REQUIRE(rig->document.removeObject(rig->part).has_value());
    const features::RegenerationReport report = rig->regenerate();

    CHECK_FALSE(report.succeeded());
    CHECK(rig->assembly.transforms == 0);
    CHECK(rig->transform(rig->base) == nullptr);
    CHECK(rig->transform(rig->arm) == nullptr);
}

TEST_CASE("Regeneration_ReportsADependencyCycleWithoutHanging", "[assembly][regen][p13]") {
    // A cycle among parameters driving the assembly: reported explicitly,
    // no recursion, no partial derived state.
    auto rig = makeRig();
    const ParameterId a = require(rig->document.createParameter("a", 1_mm, units::mm));
    const ParameterId b = require(rig->document.createParameter("b", 1_mm, units::mm));
    REQUIRE(rig->document.setParameterExpression(a, "b + 1 mm").has_value());
    REQUIRE(rig->document.setParameterExpression(b, "a + 1 mm").has_value());

    const features::RegenerationReport report = rig->regenerate();
    CHECK_FALSE(report.succeeded());
    CHECK_FALSE(report.cycles.empty());
    CHECK(contains(report.failed, ObjectId{a}));
    CHECK(contains(report.failed, ObjectId{b}));
    // The cycle does not involve the assembly, so the assembly still solves.
    CHECK(rig->assembly.transforms == 2);
    checkAt(rig->transform(rig->arm), 0.0, 0.0, 0.0);
}

// --- Suppression -------------------------------------------------------------------------------------

TEST_CASE("Regeneration_ASuppressedMateDoesNotFailEvenIfItsTargetIsGone", "[assembly][regen][p13]") {
    // Inactive is not broken. A mate that is not in this build has nothing to
    // resolve, so it must not fail the regeneration of a build it is not in.
    auto rig = makeRig();
    const ConfigurationId lean = require(rig->document.createConfiguration("Lean"));
    REQUIRE(assembly::suppressMate(rig->document, lean, rig->holds, true).has_value());
    REQUIRE(rig->document.setActiveConfiguration(lean).has_value());
    REQUIRE(assembly::setMateDefinition(
                rig->document, rig->holds,
                {.type = MateType::Coincident,
                 .a = faceTarget(rig->base, FaceName{.feature = rig->sketch, .face = {.role = FaceRole::EndCap}}),
                 .b = planeTarget(rig->arm, PlaneReference{})})
                .has_value());

    const features::RegenerationReport report = rig->regenerate();
    CHECK(report.succeeded());
    CHECK_FALSE(contains(report.failed, ObjectId{rig->holds}));
    // The arm is free, so it sits at its intent.
    CHECK(rig->assembly.transforms == 2);
    checkAt(rig->transform(rig->arm), 0.0, 0.0, 0.050);
}

TEST_CASE("Regeneration_NoStaleSolveLeaksBetweenConfigurations", "[assembly][regen][p13][determinism]") {
    // Back and forth between two builds that solve to different answers. The
    // transforms must be the ones of the build in force, every time --
    // never the other build's, which is the stale-state failure exactly.
    auto rig = makeRig();
    const ConfigurationId loose = require(rig->document.createConfiguration("Loose"));
    REQUIRE(assembly::suppressMate(rig->document, loose, rig->holds, true).has_value());

    for (int pass = 0; pass < 6; ++pass) {
        INFO("pass " << pass);
        REQUIRE(rig->document.setActiveConfiguration(std::nullopt).has_value());
        rig->regenerate();
        // Held down.
        checkAt(rig->transform(rig->arm), 0.0, 0.0, 0.0);

        REQUIRE(rig->document.setActiveConfiguration(loose).has_value());
        rig->regenerate();
        // Free, so at its intent.
        checkAt(rig->transform(rig->arm), 0.0, 0.0, 0.050);
    }
}

// --- Determinism and persistence -----------------------------------------------------------------------

TEST_CASE("Regeneration_IsDeterministic", "[assembly][regen][p13][determinism]") {
    const auto build = [] {
        auto rig = makeRig();
        rig->regenerate();
        return rig;
    };
    auto first = build();
    auto second = build();

    CHECK(first->assembly.trigger == second->assembly.trigger);
    CHECK(first->assembly.status == second->assembly.status);
    CHECK(first->assembly.degreesOfFreedom == second->assembly.degreesOfFreedom);
    CHECK(first->assembly.transforms == second->assembly.transforms);
    for (const auto& [id, transform] : first->regenerator.transforms()) {
        REQUIRE(second->regenerator.transform(id) != nullptr);
        // Bit-identical: nothing is seeded and no base state moved.
        CHECK(*second->regenerator.transform(id) == transform);
    }
}

TEST_CASE("Regeneration_DoesNotDriftOverManyPasses", "[assembly][regen][p13][determinism]") {
    // Repeated regeneration with an edit each time, returning to the same
    // state, must return to the same transforms exactly.
    auto rig = makeRig();
    rig->regenerate();
    const RigidTransform3D settled = *rig->transform(rig->arm);

    for (int pass = 0; pass < 10; ++pass) {
        INFO("pass " << pass);
        REQUIRE(assembly::setComponentDefinition(
                    rig->document, rig->arm,
                    {.part = rig->part, .placement = {.translation = {0_mm, 0_mm, 70_mm}}})
                    .has_value());
        rig->regenerate();
        REQUIRE(assembly::setComponentDefinition(
                    rig->document, rig->arm,
                    {.part = rig->part, .placement = {.translation = {0_mm, 0_mm, 50_mm}}})
                    .has_value());
        rig->regenerate();
        CHECK(*rig->transform(rig->arm) == settled);
    }
}

TEST_CASE("Regeneration_AfterSaveAndLoadGivesTheSameTransforms", "[assembly][regen][p13][io]") {
    TempDir dir;
    auto rig = makeRig();
    rig->regenerate();
    const RigidTransform3D before = *rig->transform(rig->arm);

    const auto path = dir.path() / "assembly.bcad";
    REQUIRE(io::saveDocument(rig->document, path).has_value());
    // No derived transform reached the file: saving again after a solve gives
    // the same bytes.
    const auto again = dir.path() / "again.bcad";
    REQUIRE(io::saveDocument(rig->document, again).has_value());
    CHECK(readFile(again) == readFile(path));

    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());
    features::Regenerator regenerator;
    AssemblyRegeneration assemblyReport;
    assembly::registerHandlers(regenerator, nullptr, &assemblyReport);
    const auto report = regenerator.regenerate(*loaded);
    REQUIRE(report.has_value());
    CHECK(report->succeeded());

    CHECK(assemblyReport.trigger == SolveTrigger::First);
    CHECK(assemblyReport.transforms == 2);
    REQUIRE(regenerator.transform(rig->arm) != nullptr);
    CHECK(*regenerator.transform(rig->arm) == before);
}

TEST_CASE("Regeneration_LoadsABrokenAssemblyAndRecoversWhenItIsRepaired",
          "[assembly][regen][p13][io]") {
    TempDir dir;
    auto rig = makeRig();
    auto removed = rig->document.removeObject(rig->part);
    REQUIRE(removed.has_value());

    const auto path = dir.path() / "broken.bcad";
    REQUIRE(io::saveDocument(rig->document, path).has_value());
    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());

    features::Regenerator regenerator;
    AssemblyRegeneration assemblyReport;
    assembly::registerHandlers(regenerator, nullptr, &assemblyReport);
    auto broken = regenerator.regenerate(*loaded);
    REQUIRE(broken.has_value());
    CHECK_FALSE(broken->succeeded());
    CHECK(assemblyReport.transforms == 0);

    REQUIRE(loaded->insertObject(std::move(*removed)).has_value());
    auto repaired = regenerator.regenerate(*loaded);
    REQUIRE(repaired.has_value());
    CHECK(repaired->succeeded());
    CHECK(assemblyReport.transforms == 2);
    checkAt(regenerator.transform(rig->arm), 0.0, 0.0, 0.0);
}
