#include "features/FeatureTestSupport.hpp"
#include "support/TestFiles.hpp"

#include <bettercad/assembly/Component.hpp>
#include <bettercad/assembly/Components.hpp>
#include <bettercad/assembly/Configurations.hpp>
#include <bettercad/assembly/Mate.hpp>
#include <bettercad/assembly/Mates.hpp>
#include <bettercad/assembly/Solver.hpp>
#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/DependencyGraph.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/document/MateReference.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using assembly::MateDefinition;
using assembly::MateType;
using assembly::SolveStatus;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;

// P13-CONF-001: one assembly, several builds.
//
// The semantics are ADR-007's, and they are P12-PARAM-002's applied to a
// second kind of state:
//
//     base state -> the active configuration's override -> what is in force
//
// An object's own `suppressed` flag is its base state and switching never
// edits it. That is the whole reason A -> B -> A restores A exactly, and it
// is what these tests measure rather than assume.

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

MateId add(Rig& rig, const std::string& name, const MateDefinition& definition) {
    return require(assembly::createMate(rig.document, name, definition));
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

/// Three components of one part: a grounded one and two held to it. Twelve
/// unknowns, six equations, six degrees of freedom in the base
/// configuration -- all hand-derived, and the starting point for most of the
/// cases below.
struct Trio {
    Rig rig;
    ComponentId a{};
    ComponentId b{};
    ComponentId c{};
    MateId ground{};
    MateId holdsB{};
    MateId holdsC{};
};

Trio makeTrio() {
    Trio t{.rig = makeRig()};
    t.a = place(t.rig, "Base");
    t.b = place(t.rig, "Arm", {.translation = {0_mm, 0_mm, 50_mm}});
    t.c = place(t.rig, "Bracket", {.translation = {0_mm, 0_mm, 80_mm}});
    t.ground = add(t.rig, "Ground", {.type = MateType::Fixed, .component = t.a});
    t.holdsB = add(t.rig, "HoldsArm", {.type = MateType::Coincident, .a = plane(t.a), .b = plane(t.b)});
    t.holdsC = add(t.rig, "HoldsBracket", {.type = MateType::Coincident, .a = plane(t.a), .b = plane(t.c)});
    return t;
}

} // namespace

// --- Base / default behaviour -------------------------------------------------------------------

TEST_CASE("Configuration_AnAssemblyWithNoConfigurationsBehavesExactlyAsBefore",
          "[assembly][configuration][p13]") {
    // The compatibility gate. Nothing about this document mentions a
    // configuration, and it must solve exactly as it did before this
    // milestone: three components, two mates, six degrees of freedom.
    Trio t = makeTrio();
    CHECK(t.rig.document.configurations().empty());
    CHECK_FALSE(t.rig.document.activeConfiguration().has_value());

    const auto result = solved(t.rig);
    REQUIRE(result.solved());
    CHECK(result.unknowns == 12);
    CHECK(result.equations == 6);
    CHECK(result.degreesOfFreedom == 6);
    CHECK(assembly::activeComponents(t.rig.document) == std::vector<ComponentId>{t.a, t.b, t.c});
    CHECK(assembly::activeMates(t.rig.document) == std::vector<MateId>{t.ground, t.holdsB, t.holdsC});
}

TEST_CASE("Configuration_AnEmptyConfigurationChangesNothing", "[assembly][configuration][p13]") {
    // The base configuration is the absence of overrides, so a configuration
    // that overrides nothing must be indistinguishable from none at all.
    Trio t = makeTrio();
    const auto base = solved(t.rig);

    const ConfigurationId empty = require(t.rig.document.createConfiguration("Empty"));
    REQUIRE(t.rig.document.setActiveConfiguration(empty).has_value());
    const auto withEmpty = solved(t.rig);

    CHECK(withEmpty.unknowns == base.unknowns);
    CHECK(withEmpty.equations == base.equations);
    CHECK(withEmpty.degreesOfFreedom == base.degreesOfFreedom);
    for (const auto& [id, transform] : base.transforms) {
        CHECK(withEmpty.transforms.at(id) == transform);
    }
}

// --- Component suppression ----------------------------------------------------------------------

TEST_CASE("Configuration_ASuppressedComponentContributesNoUnknowns", "[assembly][configuration][p13]") {
    // Hand-derived. Suppressing the bracket removes its six unknowns, and
    // takes the mate that holds it out with it -- that mate constrains a part
    // which is not in this build. 12 unknowns and 6 equations become 6 and 3.
    Trio t = makeTrio();
    const ConfigurationId lean = require(t.rig.document.createConfiguration("Lean"));
    REQUIRE(assembly::suppressComponent(t.rig.document, lean, t.c, true).has_value());
    REQUIRE(t.rig.document.setActiveConfiguration(lean).has_value());

    CHECK(assembly::isComponentSuppressed(t.rig.document, t.c));
    CHECK_FALSE(assembly::isComponentSuppressed(t.rig.document, t.b));
    CHECK(assembly::activeComponents(t.rig.document) == std::vector<ComponentId>{t.a, t.b});
    CHECK(assembly::activeMates(t.rig.document) == std::vector<MateId>{t.ground, t.holdsB});

    const auto result = solved(t.rig);
    REQUIRE(result.solved());
    CHECK(result.unknowns == 6);
    CHECK(result.equations == 3);
    CHECK(result.degreesOfFreedom == 3);
    // And the suppressed component has no transform at all: it is not in this
    // build, so there is no position for it to be at.
    CHECK_FALSE(result.transforms.contains(t.c));
    CHECK(result.transforms.contains(t.b));
}

TEST_CASE("Configuration_SuppressionDeletesNothing", "[assembly][configuration][p13]") {
    // Suppression is engineering intent, not deletion. Everything the
    // component is stays exactly where it was.
    Trio t = makeTrio();
    const ComponentPlacement intent{.translation = {0_mm, 0_mm, 80_mm}};
    const ConfigurationId lean = require(t.rig.document.createConfiguration("Lean"));
    REQUIRE(assembly::suppressComponent(t.rig.document, lean, t.c, true).has_value());
    REQUIRE(t.rig.document.setActiveConfiguration(lean).has_value());

    const assembly::Component* component = assembly::findComponent(t.rig.document, t.c);
    REQUIRE(component != nullptr);
    CHECK(component->definition().placement == intent);
    CHECK(component->definition().part == assembly::findComponent(t.rig.document, t.b)->definition().part);
    // Its base state is untouched -- suppression lives in the configuration.
    CHECK_FALSE(component->definition().suppressed);
    // The mate that holds it is still in the document with its targets.
    const assembly::Mate* mate = assembly::findMate(t.rig.document, t.holdsC);
    REQUIRE(mate != nullptr);
    CHECK(mate->definition().b->component == t.c);
    CHECK_FALSE(mate->definition().suppressed);
    // It is merely inactive, which is a different thing and says so.
    CHECK_FALSE(assembly::isMateActive(t.rig.document, t.holdsC));
    CHECK_FALSE(assembly::isMateSuppressed(t.rig.document, t.holdsC));
}

TEST_CASE("Configuration_CanUnsuppressAComponentThatIsSuppressedByDefault",
          "[assembly][configuration][p13]") {
    // An override is a value, not a toggle -- which is why a component whose
    // BASE state is suppressed can be turned back on by a configuration. A
    // set of suppressed objects could not express this.
    Trio t = makeTrio();
    REQUIRE(assembly::setComponentDefinition(
                t.rig.document, t.c,
                {.part = t.rig.part, .suppressed = true, .placement = {.translation = {0_mm, 0_mm, 80_mm}}})
                .has_value());
    // Suppressed at the base: six unknowns, not twelve.
    CHECK(assembly::isComponentSuppressed(t.rig.document, t.c));
    CHECK(solved(t.rig).unknowns == 6);

    const ConfigurationId full = require(t.rig.document.createConfiguration("Full"));
    REQUIRE(assembly::suppressComponent(t.rig.document, full, t.c, false).has_value());
    REQUIRE(t.rig.document.setActiveConfiguration(full).has_value());

    CHECK_FALSE(assembly::isComponentSuppressed(t.rig.document, t.c));
    const auto result = solved(t.rig);
    CHECK(result.unknowns == 12);
    CHECK(result.equations == 6);
    CHECK(result.transforms.contains(t.c));
}

// --- Mate suppression ---------------------------------------------------------------------------

TEST_CASE("Configuration_ASuppressedMateContributesNoEquations", "[assembly][configuration][p13]") {
    // Hand-derived: the components all stay, so the unknowns do not move;
    // only the three equations of the suppressed mate go.
    Trio t = makeTrio();
    const ConfigurationId loose = require(t.rig.document.createConfiguration("Loose"));
    REQUIRE(assembly::suppressMate(t.rig.document, loose, t.holdsC, true).has_value());
    REQUIRE(t.rig.document.setActiveConfiguration(loose).has_value());

    CHECK(assembly::isMateSuppressed(t.rig.document, t.holdsC));
    CHECK_FALSE(assembly::isMateActive(t.rig.document, t.holdsC));
    CHECK(assembly::activeComponents(t.rig.document) == std::vector<ComponentId>{t.a, t.b, t.c});

    const auto result = solved(t.rig);
    REQUIRE(result.solved());
    CHECK(result.unknowns == 12);
    CHECK(result.equations == 3);
    CHECK(result.degreesOfFreedom == 9);
    // The component is still in the build, so it still has a transform --
    // it is simply free now.
    CHECK(result.transforms.contains(t.c));
    checkPosition(result.transforms.at(t.c), 0.0, 0.0, 0.080);
}

TEST_CASE("Configuration_ASuppressedGroundingMateFreesTheAssembly", "[assembly][configuration][p13]") {
    // Grounding is a Fixed mate, so suppressing it gives the assembly back
    // its six rigid-body modes: 12 unknowns, rank 6, DOF 6 -> 12 unknowns
    // with a grounded base becomes 18 without one.
    Trio t = makeTrio();
    const ConfigurationId floating = require(t.rig.document.createConfiguration("Floating"));
    REQUIRE(assembly::suppressMate(t.rig.document, floating, t.ground, true).has_value());
    REQUIRE(t.rig.document.setActiveConfiguration(floating).has_value());

    const auto result = solved(t.rig);
    REQUIRE(result.solved());
    CHECK(result.status == SolveStatus::UnderConstrained);
    CHECK(result.unknowns == 18);
    CHECK(result.equations == 6);
    CHECK(result.degreesOfFreedom == 12);
}

// --- Solve participation, analytically -----------------------------------------------------------

TEST_CASE("Configuration_TurnsAFullyConstrainedAssemblyUnderConstrained",
          "[assembly][configuration][p13]") {
    // Six independent equations against six unknowns is P13-SOLVE-001's
    // fully-constrained recipe. Suppressing the Perpendicular takes exactly
    // one row, so the same assembly has one degree of freedom in the other
    // configuration -- derived, then measured.
    Rig rig = makeRig();
    const ComponentId a = place(rig, "Base");
    const ComponentId b = place(rig, "Arm", {.translation = {12_mm, -8_mm, 40_mm}});
    add(rig, "Ground", {.type = MateType::Fixed, .component = a});
    add(rig, "Touch", {.type = MateType::Coincident, .a = plane(a), .b = plane(b)});
    add(rig, "AcrossX",
        {.type = MateType::Distance,
         .a = plane(a, PrincipalPlane::YZ),
         .b = plane(b, PrincipalPlane::YZ),
         .distance = 0_mm});
    add(rig, "AcrossY",
        {.type = MateType::Distance,
         .a = plane(a, PrincipalPlane::XZ),
         .b = plane(b, PrincipalPlane::XZ),
         .distance = 0_mm});
    const MateId square = add(
        rig, "Square",
        {.type = MateType::Perpendicular, .a = plane(a, PrincipalPlane::YZ), .b = plane(b, PrincipalPlane::XZ)});

    const auto tight = solved(rig);
    REQUIRE(tight.solved());
    CHECK(tight.status == SolveStatus::FullyConstrained);
    CHECK(tight.equations == 6);
    CHECK(tight.degreesOfFreedom == 0);

    const ConfigurationId loose = require(rig.document.createConfiguration("Loose"));
    REQUIRE(assembly::suppressMate(rig.document, loose, square, true).has_value());
    REQUIRE(rig.document.setActiveConfiguration(loose).has_value());

    const auto slack = solved(rig);
    REQUIRE(slack.solved());
    CHECK(slack.status == SolveStatus::UnderConstrained);
    CHECK(slack.equations == 5);
    CHECK(slack.degreesOfFreedom == 1);
}

TEST_CASE("Configuration_AlternateConfigurationsSelectDifferentMates", "[assembly][configuration][p13]") {
    // Two builds of one assembly: the arm sits 25 mm up in one and 60 mm up
    // in the other, chosen by which Distance mate is in force. Neither
    // configuration is the base, and each must give exactly its own answer.
    Rig rig = makeRig();
    const ComponentId a = place(rig, "Base");
    const ComponentId b = place(rig, "Arm", {.translation = {0_mm, 0_mm, 100_mm}});
    add(rig, "Ground", {.type = MateType::Fixed, .component = a});
    const MateId low = add(rig, "Low", {.type = MateType::Distance, .a = plane(a), .b = plane(b), .distance = 25_mm});
    const MateId high =
        add(rig, "High", {.type = MateType::Distance, .a = plane(a), .b = plane(b), .distance = 60_mm});

    // Both at once contradict each other -- which is the point of putting
    // them in different configurations.
    CHECK(solved(rig).status == SolveStatus::Inconsistent);

    const ConfigurationId lowered = require(rig.document.createConfiguration("Lowered"));
    const ConfigurationId raised = require(rig.document.createConfiguration("Raised"));
    REQUIRE(assembly::suppressMate(rig.document, lowered, high, true).has_value());
    REQUIRE(assembly::suppressMate(rig.document, raised, low, true).has_value());

    REQUIRE(rig.document.setActiveConfiguration(lowered).has_value());
    const auto down = solved(rig);
    REQUIRE(down.solved());
    CHECK(down.equations == 1);
    checkPosition(down.transforms.at(b), 0.0, 0.0, 0.025);

    REQUIRE(rig.document.setActiveConfiguration(raised).has_value());
    const auto up = solved(rig);
    REQUIRE(up.solved());
    CHECK(up.equations == 1);
    checkPosition(up.transforms.at(b), 0.0, 0.0, 0.060);
}

// --- Switching ------------------------------------------------------------------------------------

TEST_CASE("Configuration_SwitchingThereAndBackRestoresTheSameStateExactly",
          "[assembly][configuration][p13][determinism]") {
    // The property ADR-007 inherits from P12-PARAM-002: because switching
    // never edits a base state, there is nothing to drift. Ten round trips,
    // and the transforms must be bit-identical to the first solve every time.
    Trio t = makeTrio();
    const ConfigurationId lean = require(t.rig.document.createConfiguration("Lean"));
    REQUIRE(assembly::suppressComponent(t.rig.document, lean, t.c, true).has_value());

    const auto first = solved(t.rig);
    REQUIRE(first.solved());
    REQUIRE(first.transforms.size() == 3);

    for (int pass = 0; pass < 10; ++pass) {
        INFO("pass " << pass);
        REQUIRE(t.rig.document.setActiveConfiguration(lean).has_value());
        const auto lean_ = solved(t.rig);
        CHECK(lean_.unknowns == 6);
        CHECK(lean_.transforms.size() == 2);

        REQUIRE(t.rig.document.setActiveConfiguration(std::nullopt).has_value());
        const auto back = solved(t.rig);
        CHECK(back.unknowns == first.unknowns);
        CHECK(back.equations == first.equations);
        CHECK(back.degreesOfFreedom == first.degreesOfFreedom);
        CHECK(back.iterations == first.iterations);
        REQUIRE(back.transforms.size() == first.transforms.size());
        for (const auto& [id, transform] : first.transforms) {
            // Bit-identical, not merely close: nothing was seeded and no base
            // state moved, so the arithmetic is the same arithmetic.
            CHECK(back.transforms.at(id) == transform);
        }
    }
}

TEST_CASE("Configuration_SwitchingNeverTouchesCanonicalState", "[assembly][configuration][p13]") {
    // What switching must not do. The placement intent and the base
    // suppression flags are canonical; a configuration overrides what is in
    // force and edits none of it.
    Trio t = makeTrio();
    const ComponentPlacement intent{.translation = {0_mm, 0_mm, 80_mm}};
    const ConfigurationId lean = require(t.rig.document.createConfiguration("Lean"));
    REQUIRE(assembly::suppressComponent(t.rig.document, lean, t.c, true).has_value());
    REQUIRE(assembly::suppressMate(t.rig.document, lean, t.holdsB, true).has_value());

    for (int pass = 0; pass < 5; ++pass) {
        REQUIRE(t.rig.document.setActiveConfiguration(lean).has_value());
        (void)solved(t.rig);
        REQUIRE(t.rig.document.setActiveConfiguration(std::nullopt).has_value());
        (void)solved(t.rig);
    }

    CHECK(assembly::findComponent(t.rig.document, t.c)->definition().placement == intent);
    CHECK_FALSE(assembly::findComponent(t.rig.document, t.c)->definition().suppressed);
    CHECK_FALSE(assembly::findMate(t.rig.document, t.holdsB)->definition().suppressed);
}

// --- Dependencies ---------------------------------------------------------------------------------

TEST_CASE("Configuration_DependenciesSurviveSuppressionUnchanged", "[assembly][configuration][p13]") {
    // Suppression is not deletion, so the dependency graph must not move: a
    // suppressed component still depends on its part, and a mate still
    // depends on the components it names. Nothing stale, nothing duplicated.
    Trio t = makeTrio();
    const DocumentGraph before = buildDependencyGraph(t.rig.document);
    REQUIRE(before.missing.empty());

    const ConfigurationId lean = require(t.rig.document.createConfiguration("Lean"));
    REQUIRE(assembly::suppressComponent(t.rig.document, lean, t.c, true).has_value());
    REQUIRE(assembly::suppressMate(t.rig.document, lean, t.holdsB, true).has_value());
    REQUIRE(t.rig.document.setActiveConfiguration(lean).has_value());

    const DocumentGraph suppressed = buildDependencyGraph(t.rig.document);
    CHECK(suppressed.missing.empty());
    CHECK(suppressed.graph.dependenciesOf(ObjectId{t.c}) == before.graph.dependenciesOf(ObjectId{t.c}));
    CHECK(suppressed.graph.dependenciesOf(ObjectId{t.holdsC}) ==
          before.graph.dependenciesOf(ObjectId{t.holdsC}));
    CHECK(suppressed.graph.dependentsOf(ObjectId{t.c}) == before.graph.dependentsOf(ObjectId{t.c}));

    // And back again.
    REQUIRE(t.rig.document.setActiveConfiguration(std::nullopt).has_value());
    const DocumentGraph after = buildDependencyGraph(t.rig.document);
    CHECK(after.missing.empty());
    CHECK(after.graph.dependenciesOf(ObjectId{t.holdsC}) == before.graph.dependenciesOf(ObjectId{t.holdsC}));
}

TEST_CASE("Configuration_ForgetsAnObjectThatIsDeleted", "[assembly][configuration][p13]") {
    // A configuration must never name an object that is gone -- the rule
    // parameter overrides already follow when a parameter is deleted.
    Trio t = makeTrio();
    const ConfigurationId lean = require(t.rig.document.createConfiguration("Lean"));
    REQUIRE(assembly::suppressComponent(t.rig.document, lean, t.c, true).has_value());
    REQUIRE(assembly::suppressMate(t.rig.document, lean, t.holdsC, true).has_value());
    REQUIRE(t.rig.document.configurations().find(lean)->componentSuppression().size() == 1);
    REQUIRE(t.rig.document.configurations().find(lean)->mateSuppression().size() == 1);

    REQUIRE(assembly::removeMate(t.rig.document, t.holdsC).has_value());
    REQUIRE(assembly::removeComponent(t.rig.document, t.c).has_value());

    const Configuration* configuration = t.rig.document.configurations().find(lean);
    REQUIRE(configuration != nullptr);
    CHECK(configuration->componentSuppression().empty());
    CHECK(configuration->mateSuppression().empty());
    CHECK(configuration->empty());
}

// --- Failure paths --------------------------------------------------------------------------------

TEST_CASE("Configuration_RefusesToSuppressWhatItCannotName", "[assembly][configuration][p13]") {
    Trio t = makeTrio();
    const ConfigurationId lean = require(t.rig.document.createConfiguration("Lean"));

    // An unknown configuration.
    auto unknownConfiguration =
        assembly::suppressComponent(t.rig.document, ConfigurationId::fromValue(9999), t.c, true);
    REQUIRE_FALSE(unknownConfiguration.has_value());
    CHECK(unknownConfiguration.error().code == ErrorCode::NotFound);

    // An unknown component, and an unknown mate.
    auto unknownComponent =
        assembly::suppressComponent(t.rig.document, lean, ComponentId::fromValue(9999), true);
    REQUIRE_FALSE(unknownComponent.has_value());
    CHECK(unknownComponent.error().code == ErrorCode::NotFound);
    auto unknownMate = assembly::suppressMate(t.rig.document, lean, MateId::fromValue(9999), true);
    REQUIRE_FALSE(unknownMate.has_value());
    CHECK(unknownMate.error().code == ErrorCode::NotFound);

    // An ID that names an object of the wrong kind: a mate is not a component.
    auto wrongKind =
        assembly::suppressComponent(t.rig.document, lean, ComponentId::fromValue(t.holdsC.value()), true);
    REQUIRE_FALSE(wrongKind.has_value());
    CHECK(wrongKind.error().code == ErrorCode::NotFound);

    // None of it changed anything.
    CHECK(t.rig.document.configurations().find(lean)->empty());
}

TEST_CASE("Configuration_AFailedSwitchLeavesThePreviousOneInForce", "[assembly][configuration][p13]") {
    Trio t = makeTrio();
    const ConfigurationId lean = require(t.rig.document.createConfiguration("Lean"));
    REQUIRE(assembly::suppressComponent(t.rig.document, lean, t.c, true).has_value());
    REQUIRE(t.rig.document.setActiveConfiguration(lean).has_value());
    const auto before = solved(t.rig);

    auto failed = t.rig.document.setActiveConfiguration(ConfigurationId::fromValue(9999));
    REQUIRE_FALSE(failed.has_value());
    CHECK(failed.error().code == ErrorCode::NotFound);

    // The previous configuration is still in force and the document still
    // works -- not merely untouched, but usable.
    CHECK(t.rig.document.activeConfiguration() == lean);
    const auto after = solved(t.rig);
    CHECK(after.unknowns == before.unknowns);
    for (const auto& [id, transform] : before.transforms) {
        CHECK(after.transforms.at(id) == transform);
    }
}

TEST_CASE("Configuration_SuppressingEveryComponentIsAnEmptyButValidSolve",
          "[assembly][configuration][p13]") {
    // The degenerate build. Nothing is in force, so there is nothing to
    // solve and nothing to report as free -- and no failure, because an
    // assembly of no parts is not a contradiction.
    Trio t = makeTrio();
    const ConfigurationId none = require(t.rig.document.createConfiguration("None"));
    for (const ComponentId id : {t.a, t.b, t.c}) {
        REQUIRE(assembly::suppressComponent(t.rig.document, none, id, true).has_value());
    }
    REQUIRE(t.rig.document.setActiveConfiguration(none).has_value());

    CHECK(assembly::activeComponents(t.rig.document).empty());
    CHECK(assembly::activeMates(t.rig.document).empty());
    const auto result = solved(t.rig);
    CHECK(result.status == SolveStatus::FullyConstrained);
    CHECK(result.unknowns == 0);
    CHECK(result.equations == 0);
    CHECK(result.degreesOfFreedom == 0);
    CHECK(result.transforms.empty());
}

// --- Persistence ------------------------------------------------------------------------------------

TEST_CASE("ConfigurationFile_PreservesConfigurationsAndSuppression", "[assembly][configuration][p13][io]") {
    TempDir dir;
    Trio t = makeTrio();
    const ConfigurationId lean = require(t.rig.document.createConfiguration("Lean"));
    const ConfigurationId full = require(t.rig.document.createConfiguration("Full"));
    REQUIRE(assembly::suppressComponent(t.rig.document, lean, t.c, true).has_value());
    REQUIRE(assembly::suppressMate(t.rig.document, lean, t.holdsC, true).has_value());
    REQUIRE(assembly::suppressMate(t.rig.document, full, t.holdsB, false).has_value());
    REQUIRE(t.rig.document.setActiveConfiguration(lean).has_value());
    const auto before = solved(t.rig);

    const auto path = dir.path() / "configured.bcad";
    REQUIRE(io::saveDocument(t.rig.document, path).has_value());
    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());

    // Identity, names, and which one is active.
    REQUIRE(loaded->configurations().size() == 2);
    CHECK(loaded->activeConfiguration() == lean);
    const Configuration* reloadedLean = loaded->configurations().find(lean);
    const Configuration* reloadedFull = loaded->configurations().find(full);
    REQUIRE(reloadedLean != nullptr);
    REQUIRE(reloadedFull != nullptr);
    CHECK(reloadedLean->name() == "Lean");
    CHECK(reloadedFull->name() == "Full");

    // The suppression states themselves, including the false one -- an
    // override that turns something ON must survive as readily as one that
    // turns it off.
    CHECK(reloadedLean->suppressionFor(t.c) == std::optional<bool>{true});
    CHECK(reloadedLean->suppressionFor(t.holdsC) == std::optional<bool>{true});
    CHECK(reloadedFull->suppressionFor(t.holdsB) == std::optional<bool>{false});
    CHECK_FALSE(reloadedFull->suppressionFor(t.c).has_value());

    // And the reloaded document solves to the same answer.
    auto after = assembly::solve(*loaded);
    REQUIRE(after.has_value());
    CHECK(after->unknowns == before.unknowns);
    CHECK(after->equations == before.equations);
    CHECK(after->degreesOfFreedom == before.degreesOfFreedom);
    REQUIRE(after->transforms.size() == before.transforms.size());
    for (const auto& [id, transform] : before.transforms) {
        CHECK(after->transforms.at(id) == transform);
    }

    // Writing what was read gives the same bytes.
    const auto again = dir.path() / "again.bcad";
    REQUIRE(io::saveDocument(*loaded, again).has_value());
    CHECK(readFile(again) == readFile(path));
}

TEST_CASE("ConfigurationFile_WritesNoSuppressionKeysWhenThereAreNone",
          "[assembly][configuration][p13][io]") {
    // A document whose configurations change only parameters must write
    // exactly the file it wrote before this milestone.
    TempDir dir;
    Trio t = makeTrio();
    require(t.rig.document.createConfiguration("Plain"));
    const auto path = dir.path() / "plain.bcad";
    REQUIRE(io::saveDocument(t.rig.document, path).has_value());

    const std::string text = readFile(path);
    CHECK_THAT(text, ContainsSubstring("\"Plain\""));
    CHECK_THAT(text, !ContainsSubstring("\"components\""));
    CHECK_THAT(text, !ContainsSubstring("\"mates\""));
}

TEST_CASE("ConfigurationFile_RefusesAnOverrideNamingAnObjectThatIsNotThere",
          "[assembly][configuration][p13][io]") {
    // A hand-edited file must not load a suppression override that can never
    // apply; it is refused rather than silently dropped.
    TempDir dir;
    Trio t = makeTrio();
    const ConfigurationId lean = require(t.rig.document.createConfiguration("Lean"));
    REQUIRE(assembly::suppressComponent(t.rig.document, lean, t.c, true).has_value());
    const auto path = dir.path() / "configured.bcad";
    REQUIRE(io::saveDocument(t.rig.document, path).has_value());

    std::string text = readFile(path);
    const std::string needle = std::format("\"object\": {}", t.c.value());
    const std::size_t at = text.find(needle);
    REQUIRE(at != std::string::npos);
    text.replace(at, needle.size(), "\"object\": 987654");
    const auto edited = dir.path() / "edited.bcad";
    writeFile(edited, text);

    auto loaded = io::loadDocument(edited);
    REQUIRE_FALSE(loaded.has_value());
    CHECK_THAT(loaded.error().message, ContainsSubstring("987654"));
}

// --- Determinism -------------------------------------------------------------------------------------

TEST_CASE("Configuration_IsDeterministic", "[assembly][configuration][p13][determinism]") {
    const auto build = [] {
        Trio t = makeTrio();
        const ConfigurationId lean = require(t.rig.document.createConfiguration("Lean"));
        REQUIRE(assembly::suppressComponent(t.rig.document, lean, t.c, true).has_value());
        REQUIRE(t.rig.document.setActiveConfiguration(lean).has_value());
        return t;
    };
    Trio first = build();
    Trio second = build();
    const auto a = solved(first.rig);
    const auto b = solved(second.rig);

    CHECK(a.status == b.status);
    CHECK(a.unknowns == b.unknowns);
    CHECK(a.equations == b.equations);
    CHECK(a.degreesOfFreedom == b.degreesOfFreedom);
    CHECK(a.iterations == b.iterations);
    CHECK(a.maxResidual == b.maxResidual);
    CHECK(assembly::activeComponents(first.rig.document) == assembly::activeComponents(second.rig.document));
    CHECK(assembly::activeMates(first.rig.document) == assembly::activeMates(second.rig.document));
    REQUIRE(a.transforms.size() == b.transforms.size());
    for (const auto& [id, transform] : a.transforms) {
        CHECK(transform == b.transforms.at(id));
    }
}

TEST_CASE("Configuration_DoesNotDependOnTheOrderSuppressionWasSet", "[assembly][configuration][p13]") {
    const auto build = [](bool componentFirst) {
        Trio t = makeTrio();
        const ConfigurationId lean = require(t.rig.document.createConfiguration("Lean"));
        if (componentFirst) {
            REQUIRE(assembly::suppressComponent(t.rig.document, lean, t.c, true).has_value());
            REQUIRE(assembly::suppressMate(t.rig.document, lean, t.holdsB, true).has_value());
        } else {
            REQUIRE(assembly::suppressMate(t.rig.document, lean, t.holdsB, true).has_value());
            REQUIRE(assembly::suppressComponent(t.rig.document, lean, t.c, true).has_value());
        }
        REQUIRE(t.rig.document.setActiveConfiguration(lean).has_value());
        return t;
    };
    Trio first = build(true);
    Trio second = build(false);
    const auto a = solved(first.rig);
    const auto b = solved(second.rig);

    CHECK(a.unknowns == b.unknowns);
    CHECK(a.equations == b.equations);
    CHECK(a.degreesOfFreedom == b.degreesOfFreedom);
    for (const auto& [id, transform] : a.transforms) {
        CHECK(transform == b.transforms.at(id));
    }
}

// --- Adversarial (P13-CONF-001) -------------------------------------------------------------------

TEST_CASE("Configuration_ConfigurationsDoNotShareSuppressionState", "[assembly][configuration][p13]") {
    // Two configurations holding one map between them would make every build
    // the last one edited. Each must own its overrides outright.
    Trio t = makeTrio();
    const ConfigurationId lean = require(t.rig.document.createConfiguration("Lean"));
    const ConfigurationId full = require(t.rig.document.createConfiguration("Full"));
    REQUIRE(assembly::suppressComponent(t.rig.document, lean, t.c, true).has_value());

    // Setting one says nothing about the other.
    CHECK(t.rig.document.configurations().find(lean)->suppressionFor(t.c) == std::optional<bool>{true});
    CHECK_FALSE(t.rig.document.configurations().find(full)->suppressionFor(t.c).has_value());
    CHECK(t.rig.document.configurations().find(full)->empty());

    // And they solve differently, which is the observable form of the same
    // claim: 6 unknowns against 12.
    REQUIRE(t.rig.document.setActiveConfiguration(lean).has_value());
    CHECK(solved(t.rig).unknowns == 6);
    REQUIRE(t.rig.document.setActiveConfiguration(full).has_value());
    CHECK(solved(t.rig).unknowns == 12);

    // Clearing one leaves the other alone.
    REQUIRE(assembly::suppressComponent(t.rig.document, full, t.c, true).has_value());
    REQUIRE(assembly::clearComponentSuppression(t.rig.document, full, t.c).has_value());
    CHECK(t.rig.document.configurations().find(lean)->suppressionFor(t.c) == std::optional<bool>{true});
    CHECK_FALSE(t.rig.document.configurations().find(full)->suppressionFor(t.c).has_value());
}

TEST_CASE("Configuration_ADeletedMateLeavesTheSolveImmediately", "[assembly][configuration][p13]") {
    // A deleted mate must not go on contributing equations through a
    // configuration that still remembered it.
    Trio t = makeTrio();
    const ConfigurationId lean = require(t.rig.document.createConfiguration("Lean"));
    REQUIRE(assembly::suppressMate(t.rig.document, lean, t.holdsC, true).has_value());
    REQUIRE(t.rig.document.setActiveConfiguration(lean).has_value());
    CHECK(solved(t.rig).equations == 3);

    REQUIRE(assembly::removeMate(t.rig.document, t.holdsC).has_value());
    const auto result = solved(t.rig);
    REQUIRE(result.solved());
    CHECK(result.equations == 3);
    CHECK(assembly::activeMates(t.rig.document) == std::vector<MateId>{t.ground, t.holdsB});
    // And the override went with it, so re-saving cannot carry a ghost.
    CHECK(t.rig.document.configurations().find(lean)->mateSuppression().empty());
}

TEST_CASE("Configuration_DeletingTheActiveConfigurationFallsBackToTheBase",
          "[assembly][configuration][p13]") {
    Trio t = makeTrio();
    const ConfigurationId lean = require(t.rig.document.createConfiguration("Lean"));
    REQUIRE(assembly::suppressComponent(t.rig.document, lean, t.c, true).has_value());
    REQUIRE(t.rig.document.setActiveConfiguration(lean).has_value());
    CHECK(solved(t.rig).unknowns == 6);

    REQUIRE(t.rig.document.removeConfiguration(lean).has_value());
    CHECK_FALSE(t.rig.document.activeConfiguration().has_value());
    // Back to the base build, with nothing suppressed.
    const auto result = solved(t.rig);
    CHECK(result.unknowns == 12);
    CHECK(result.equations == 6);
    CHECK_FALSE(assembly::isComponentSuppressed(t.rig.document, t.c));
}

TEST_CASE("ConfigurationFile_PreservesTheBaseConfigurationAsActive", "[assembly][configuration][p13][io]") {
    // The active configuration is written by name and absent means the base.
    // A document sitting on the base with configurations defined must come
    // back on the base, not on the first one it finds.
    TempDir dir;
    Trio t = makeTrio();
    const ConfigurationId lean = require(t.rig.document.createConfiguration("Lean"));
    REQUIRE(assembly::suppressComponent(t.rig.document, lean, t.c, true).has_value());
    REQUIRE(t.rig.document.setActiveConfiguration(std::nullopt).has_value());

    const auto path = dir.path() / "base-active.bcad";
    REQUIRE(io::saveDocument(t.rig.document, path).has_value());
    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());

    CHECK_FALSE(loaded->activeConfiguration().has_value());
    CHECK(loaded->configurations().size() == 1);
    // The configuration is still there with its override, simply not in force.
    CHECK(loaded->configurations().find(lean)->suppressionFor(t.c) == std::optional<bool>{true});
    auto result = assembly::solve(*loaded);
    REQUIRE(result.has_value());
    CHECK(result->unknowns == 12);
}
