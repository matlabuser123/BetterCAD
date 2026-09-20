#include "features/FeatureTestSupport.hpp"
#include "support/TestFiles.hpp"

#include <bettercad/assembly/Component.hpp>
#include <bettercad/assembly/Commands.hpp>
#include <bettercad/assembly/Components.hpp>
#include <bettercad/assembly/Configurations.hpp>
#include <bettercad/assembly/Mate.hpp>
#include <bettercad/assembly/Mates.hpp>
#include <bettercad/assembly/Placement.hpp>
#include <bettercad/assembly/Resolution.hpp>
#include <bettercad/assembly/Solver.hpp>
#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Command.hpp>
#include <bettercad/core/document/Commands.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/document/MateReference.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/Regenerator.hpp>
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
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;

// P13-CMD-001: every assembly edit becomes an edit that can be taken back.
//
// The failure this file exists to catch is an undo that leaves the document
// SUBTLY different from before. Not visibly broken -- an exception would be a
// kindness -- but almost restored, with one field nothing thought to check.
// The engineer carries on from a state they believe they recognise, and the
// divergence surfaces much later as geometry that cannot be explained.
//
// So the tests here compare whole canonical state across a round trip rather
// than spot-checking whatever the command obviously touched.

namespace {

struct Rig {
    Document document{"Assembly"};
    CommandHistory history{};
    ObjectId sketch{};
    ObjectId part{};
    ComponentId base{};
    ComponentId arm{};
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
    rig.base = require(assembly::createComponent(rig.document, "Base", {.part = rig.part}));
    rig.arm = require(assembly::createComponent(
        rig.document, "Arm", {.part = rig.part, .placement = {.translation = {0_mm, 0_mm, 50_mm}}}));
    return rig;
}

} // namespace

// --- The defect this milestone found before building on it -------------------------------------

TEST_CASE("Command_UndoingADeleteRestoresTheConfigurationOverridesItCleared",
          "[assembly][command][p13]") {
    // A deletion clears the configuration overrides that named the deleted
    // object -- rightly, because a configuration must never name something
    // that is gone. Undo must put them back, or the canonical intent after
    // undo is not the canonical intent before the delete.
    //
    // This is the exact shape of the failure this file is about: the
    // component returns, the document looks restored, and the configuration
    // has quietly forgotten that it suppressed it.
    Rig rig = makeRig();
    const ConfigurationId lean = require(rig.document.createConfiguration("Lean"));
    REQUIRE(assembly::suppressComponent(rig.document, lean, rig.arm, true).has_value());
    REQUIRE(rig.document.configurations().find(lean)->suppressionFor(rig.arm) ==
            std::optional<bool>{true});

    REQUIRE(rig.history.execute(rig.document, std::make_unique<DeleteObjectCommand>(ObjectId{rig.arm}))
                .has_value());
    CHECK(assembly::findComponent(rig.document, rig.arm) == nullptr);
    CHECK(rig.document.configurations().find(lean)->componentSuppression().empty());

    REQUIRE(rig.history.undo(rig.document).has_value());

    // The component is back...
    REQUIRE(assembly::findComponent(rig.document, rig.arm) != nullptr);
    // ...and so is the intent that it is not in the Lean build.
    CHECK(rig.document.configurations().find(lean)->suppressionFor(rig.arm) ==
          std::optional<bool>{true});
    CHECK(assembly::isComponentSuppressed(rig.document, rig.arm) == false); // base is active
    REQUIRE(rig.document.setActiveConfiguration(lean).has_value());
    CHECK(assembly::isComponentSuppressed(rig.document, rig.arm));
}

TEST_CASE("Command_UndoingAParameterDeleteRestoresItsOverridesToo", "[assembly][command][p13]") {
    // The same hole, on the side that predates P13 entirely: removeParameter()
    // has called forgetParameter() since P12-PARAM-002.
    Rig rig = makeRig();
    const ParameterId width = require(rig.document.createParameter("width", 40_mm, units::mm));
    const ConfigurationId big = require(rig.document.createConfiguration("Big"));
    REQUIRE(rig.document.setConfigurationOverride(big, width, 90_mm).has_value());
    REQUIRE(rig.document.configurations().find(big)->overrideFor(width).has_value());

    REQUIRE(rig.history.execute(rig.document, std::make_unique<DeleteObjectCommand>(ObjectId{width}))
                .has_value());
    CHECK(rig.document.configurations().find(big)->overrides().empty());

    REQUIRE(rig.history.undo(rig.document).has_value());

    REQUIRE(rig.document.parameters().find(width) != nullptr);
    auto restored = rig.document.configurations().find(big)->overrideFor(width);
    REQUIRE(restored.has_value());
    CHECK_THAT(restored->siValue, WithinAbs(0.090, 1e-12));
}

// --- The command contract -------------------------------------------------------------------------

TEST_CASE("Command_CreateComponentValidatesWhereARawAddWouldNot", "[assembly][command][p13]") {
    // The reason assembly commands exist at all: AddObjectCommand would take
    // any DocumentObject, including one naming a part that is not a part.
    Rig rig = makeRig();
    const std::size_t before = rig.document.objectCount();

    auto bad = rig.history.execute(
        rig.document, std::make_unique<assembly::CreateComponentCommand>(
                          "Bad", assembly::ComponentDefinition{.part = rig.sketch}));
    REQUIRE_FALSE(bad.has_value());
    // Nothing added, and nothing recorded to undo.
    CHECK(rig.document.objectCount() == before);
    CHECK(rig.history.undoCount() == 0);
    CHECK_FALSE(rig.history.canUndo());
}

TEST_CASE("Command_CreateComponentUndoAndRedoKeepTheSameIdentity", "[assembly][command][p13]") {
    // Redo must restore the same ComponentId, or a mate that named it before
    // the undo would name nothing after the redo.
    Rig rig = makeRig();
    auto create = std::make_unique<assembly::CreateComponentCommand>(
        "Third", assembly::ComponentDefinition{.part = rig.part,
                                               .placement = {.translation = {5_mm, 0_mm, 0_mm}}});
    auto* raw = create.get();
    REQUIRE(rig.history.execute(rig.document, std::move(create)).has_value());
    const ComponentId id = raw->componentId();
    REQUIRE(id.isValid());
    const assembly::ComponentDefinition definition = assembly::findComponent(rig.document, id)->definition();

    REQUIRE(rig.history.undo(rig.document).has_value());
    CHECK(assembly::findComponent(rig.document, id) == nullptr);

    REQUIRE(rig.history.redo(rig.document).has_value());
    const assembly::Component* back = assembly::findComponent(rig.document, id);
    REQUIRE(back != nullptr);
    CHECK(back->definition() == definition);
    CHECK(back->name() == "Third");
}

// --- Placement --------------------------------------------------------------------------------------

TEST_CASE("Command_PlacementEditUndoesAndRedoesExactly", "[assembly][command][p13]") {
    Rig rig = makeRig();
    const ComponentPlacement a{.translation = {0_mm, 0_mm, 50_mm}};
    const ComponentPlacement b{.translation = {12_mm, -7_mm, 90_mm}, .rotation = {0_deg, 0_deg, 35_deg}};
    REQUIRE(assembly::findComponent(rig.document, rig.arm)->definition().placement == a);

    REQUIRE(rig.history
                .execute(rig.document, std::make_unique<assembly::SetComponentPlacementCommand>(rig.arm, b))
                .has_value());
    CHECK(assembly::findComponent(rig.document, rig.arm)->definition().placement == b);

    REQUIRE(rig.history.undo(rig.document).has_value());
    CHECK(assembly::findComponent(rig.document, rig.arm)->definition().placement == a);

    REQUIRE(rig.history.redo(rig.document).has_value());
    CHECK(assembly::findComponent(rig.document, rig.arm)->definition().placement == b);

    // Several cycles, with no drift in either direction.
    for (int pass = 0; pass < 5; ++pass) {
        INFO("pass " << pass);
        REQUIRE(rig.history.undo(rig.document).has_value());
        CHECK(assembly::findComponent(rig.document, rig.arm)->definition().placement == a);
        REQUIRE(rig.history.redo(rig.document).has_value());
        CHECK(assembly::findComponent(rig.document, rig.arm)->definition().placement == b);
    }
}

TEST_CASE("Command_PlacementEditStoresIntentAndNotTheSolvedPosition", "[assembly][command][p13]") {
    // ADR-005's separation, through a command. The mate pulls the arm to the
    // origin, but the undo state is the 90 mm the engineer asked for.
    Rig rig = makeRig();
    features::Regenerator regenerator;
    assembly::AssemblyRegeneration report;
    assembly::registerHandlers(regenerator, nullptr, &report);
    REQUIRE(assembly::createMate(rig.document, "Ground",
                                 {.type = MateType::Fixed, .component = rig.base})
                .has_value());
    REQUIRE(assembly::createMate(rig.document, "Touch",
                                 {.type = MateType::Coincident,
                                  .a = planeTarget(rig.base, PlaneReference{}),
                                  .b = planeTarget(rig.arm, PlaneReference{})})
                .has_value());

    REQUIRE(rig.history
                .execute(rig.document, std::make_unique<assembly::SetComponentPlacementCommand>(
                                           rig.arm, ComponentPlacement{.translation = {0_mm, 0_mm, 90_mm}}))
                .has_value());
    requireReport(regenerator, rig.document);
    // Solved to the origin...
    REQUIRE(regenerator.transform(rig.arm) != nullptr);
    CHECK_THAT(regenerator.transform(rig.arm)->apply(Point3D{}).z.si(), WithinAbs(0.0, 1e-9));
    // ...while the intent says 90 mm.
    CHECK(assembly::findComponent(rig.document, rig.arm)->definition().placement.translation[2] == 90_mm);

    REQUIRE(rig.history.undo(rig.document).has_value());
    // Undo restores the intent, not the solved position.
    CHECK(assembly::findComponent(rig.document, rig.arm)->definition().placement.translation[2] == 50_mm);
}

// --- Mates ------------------------------------------------------------------------------------------

TEST_CASE("Command_MateCreateEditAndUndoPreserveEverythingAboutIt", "[assembly][command][p13]") {
    Rig rig = makeRig();
    auto create = std::make_unique<assembly::CreateMateCommand>(
        "Gap", MateDefinition{.type = MateType::Distance,
                              .a = planeTarget(rig.base, PlaneReference{}),
                              .b = planeTarget(rig.arm, PlaneReference{}),
                              .distance = 25_mm});
    auto* raw = create.get();
    REQUIRE(rig.history.execute(rig.document, std::move(create)).has_value());
    const MateId id = raw->mateId();
    const MateDefinition created = assembly::findMate(rig.document, id)->definition();

    // Edit it to a different kind entirely, with different targets.
    const MateDefinition edited{.type = MateType::Concentric,
                                .a = axisTarget(rig.base, AxisReference{}),
                                .b = axisTarget(rig.arm, AxisReference{})};
    REQUIRE(rig.history
                .execute(rig.document, std::make_unique<assembly::SetMateDefinitionCommand>(id, edited))
                .has_value());
    CHECK(assembly::findMate(rig.document, id)->definition() == edited);

    REQUIRE(rig.history.undo(rig.document).has_value());
    // Everything about it comes back: kind, targets, value.
    const MateDefinition restored = assembly::findMate(rig.document, id)->definition();
    CHECK(restored == created);
    CHECK(restored.type == MateType::Distance);
    CHECK(restored.distance == 25_mm);
    CHECK(restored.a->component == rig.base);

    REQUIRE(rig.history.redo(rig.document).has_value());
    CHECK(assembly::findMate(rig.document, id)->definition() == edited);

    // Undo the edit and then the creation: the mate is gone entirely.
    REQUIRE(rig.history.undo(rig.document).has_value());
    REQUIRE(rig.history.undo(rig.document).has_value());
    CHECK(assembly::findMate(rig.document, id) == nullptr);
    REQUIRE(rig.history.redo(rig.document).has_value());
    CHECK(assembly::findMate(rig.document, id)->definition() == created);
}

TEST_CASE("Command_AMechanicalMateSurvivesUndoAndRedoWithItsRollReference",
          "[assembly][command][p13]") {
    // A slider carries a second target pair (P13-MATE-002). An undo that
    // dropped it would bring back a sleeve wearing a slider's name.
    Rig rig = makeRig();
    const MateDefinition slider{.type = MateType::Slider,
                                .a = axisTarget(rig.base, AxisReference{}),
                                .b = axisTarget(rig.arm, AxisReference{}),
                                .a2 = axisTarget(rig.base, AxisReference{.axis = PrincipalAxis::X}),
                                .b2 = axisTarget(rig.arm, AxisReference{.axis = PrincipalAxis::X})};
    auto create = std::make_unique<assembly::CreateMateCommand>("Slide", slider);
    auto* raw = create.get();
    REQUIRE(rig.history.execute(rig.document, std::move(create)).has_value());
    const MateId id = raw->mateId();

    REQUIRE(rig.history.undo(rig.document).has_value());
    REQUIRE(rig.history.redo(rig.document).has_value());

    const MateDefinition back = assembly::findMate(rig.document, id)->definition();
    CHECK(back == slider);
    REQUIRE(back.a2.has_value());
    REQUIRE(back.b2.has_value());
    CHECK(back.a2->axis->axis == PrincipalAxis::X);
}

// --- Configuration and suppression --------------------------------------------------------------------

TEST_CASE("Command_SuppressionUndoesToTheExactPriorState", "[assembly][command][p13]") {
    // Three states, not two: suppressed, present, and "this configuration
    // says nothing". Undo must distinguish the third from the second.
    Rig rig = makeRig();
    const ConfigurationId lean = require(rig.document.createConfiguration("Lean"));
    REQUIRE(rig.document.configurations().find(lean)->suppressionFor(rig.arm) == std::nullopt);

    REQUIRE(rig.history
                .execute(rig.document,
                         std::make_unique<assembly::SuppressComponentCommand>(lean, rig.arm, true))
                .has_value());
    CHECK(rig.document.configurations().find(lean)->suppressionFor(rig.arm) == std::optional<bool>{true});

    REQUIRE(rig.history.undo(rig.document).has_value());
    // Back to saying nothing -- not to saying "present".
    CHECK(rig.document.configurations().find(lean)->suppressionFor(rig.arm) == std::nullopt);
    CHECK(rig.document.configurations().find(lean)->componentSuppression().empty());

    REQUIRE(rig.history.redo(rig.document).has_value());
    CHECK(rig.document.configurations().find(lean)->suppressionFor(rig.arm) == std::optional<bool>{true});

    // And an explicit false is a third state again.
    REQUIRE(rig.history
                .execute(rig.document,
                         std::make_unique<assembly::SuppressComponentCommand>(lean, rig.arm, false))
                .has_value());
    CHECK(rig.document.configurations().find(lean)->suppressionFor(rig.arm) == std::optional<bool>{false});
    REQUIRE(rig.history.undo(rig.document).has_value());
    CHECK(rig.document.configurations().find(lean)->suppressionFor(rig.arm) == std::optional<bool>{true});
}

TEST_CASE("Command_SuppressionUndoChangesSolverParticipation", "[assembly][command][p13]") {
    // The point of suppression, through the history: undo must put the
    // component back into the solve, not merely back into a map.
    Rig rig = makeRig();
    features::Regenerator regenerator;
    assembly::AssemblyRegeneration report;
    assembly::registerHandlers(regenerator, nullptr, &report);
    REQUIRE(assembly::createMate(rig.document, "Ground", {.type = MateType::Fixed, .component = rig.base})
                .has_value());
    const ConfigurationId lean = require(rig.document.createConfiguration("Lean"));
    REQUIRE(rig.document.setActiveConfiguration(lean).has_value());
    requireReport(regenerator, rig.document);
    CHECK(report.transforms == 2);

    REQUIRE(rig.history
                .execute(rig.document,
                         std::make_unique<assembly::SuppressComponentCommand>(lean, rig.arm, true))
                .has_value());
    requireReport(regenerator, rig.document);
    CHECK(report.transforms == 1);
    CHECK(regenerator.transform(rig.arm) == nullptr);

    REQUIRE(rig.history.undo(rig.document).has_value());
    requireReport(regenerator, rig.document);
    CHECK(report.transforms == 2);
    CHECK(regenerator.transform(rig.arm) != nullptr);
}

TEST_CASE("Command_SuppressionRefusesAnObjectThatIsNotThere", "[assembly][command][p13]") {
    Rig rig = makeRig();
    const ConfigurationId lean = require(rig.document.createConfiguration("Lean"));
    auto bad = rig.history.execute(
        rig.document, std::make_unique<assembly::SuppressMateCommand>(lean, MateId::fromValue(9999), true));
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().code == ErrorCode::NotFound);
    CHECK(rig.history.undoCount() == 0);

    auto noConfiguration = rig.history.execute(
        rig.document, std::make_unique<assembly::SuppressComponentCommand>(
                          ConfigurationId::fromValue(9999), rig.arm, true));
    REQUIRE_FALSE(noConfiguration.has_value());
    CHECK(rig.history.undoCount() == 0);
}

// --- History semantics ----------------------------------------------------------------------------------

TEST_CASE("Command_AFailedCommandNeverEntersHistory", "[assembly][command][p13]") {
    Rig rig = makeRig();
    REQUIRE(rig.history
                .execute(rig.document, std::make_unique<assembly::SetComponentPlacementCommand>(
                                           rig.arm, ComponentPlacement{.translation = {1_mm, 0_mm, 0_mm}}))
                .has_value());
    const std::size_t depth = rig.history.undoCount();
    const auto revision = rig.document.revision();

    // A mate relating a component to itself, which validation refuses.
    auto bad = rig.history.execute(
        rig.document,
        std::make_unique<assembly::CreateMateCommand>(
            "Impossible",
            MateDefinition{.type = MateType::Coincident,
                           .a = planeTarget(rig.arm, PlaneReference{}),
                           .b = planeTarget(rig.arm, PlaneReference{.plane = PrincipalPlane::YZ})}));
    REQUIRE_FALSE(bad.has_value());
    CHECK(rig.history.undoCount() == depth);
    CHECK(rig.document.revision() == revision);

    // A placement edit on a component that is not there.
    auto missing = rig.history.execute(
        rig.document, std::make_unique<assembly::SetComponentPlacementCommand>(
                          ComponentId::fromValue(9999), ComponentPlacement{}));
    REQUIRE_FALSE(missing.has_value());
    CHECK(rig.history.undoCount() == depth);

    // And the history still works afterwards.
    REQUIRE(rig.history.undo(rig.document).has_value());
    CHECK(assembly::findComponent(rig.document, rig.arm)->definition().placement.translation[2] == 50_mm);
}

TEST_CASE("Command_ADivergentEditClearsTheRedoStack", "[assembly][command][p13]") {
    // A -> B -> C, undo to B, then D. C must be unreachable.
    Rig rig = makeRig();
    const auto place = [&](Length z) {
        return std::make_unique<assembly::SetComponentPlacementCommand>(
            rig.arm, ComponentPlacement{.translation = {0_mm, 0_mm, z}});
    };
    REQUIRE(rig.history.execute(rig.document, place(60_mm)).has_value()); // B
    REQUIRE(rig.history.execute(rig.document, place(70_mm)).has_value()); // C
    REQUIRE(rig.history.undo(rig.document).has_value());                  // back to B
    CHECK(rig.history.canRedo());
    CHECK(rig.history.redoCount() == 1);

    REQUIRE(rig.history.execute(rig.document, place(80_mm)).has_value()); // D
    CHECK_FALSE(rig.history.canRedo());
    CHECK(rig.history.redoCount() == 0);
    CHECK(assembly::findComponent(rig.document, rig.arm)->definition().placement.translation[2] == 80_mm);

    // The history is now A -> B -> D.
    REQUIRE(rig.history.undo(rig.document).has_value());
    CHECK(assembly::findComponent(rig.document, rig.arm)->definition().placement.translation[2] == 60_mm);
    REQUIRE(rig.history.undo(rig.document).has_value());
    CHECK(assembly::findComponent(rig.document, rig.arm)->definition().placement.translation[2] == 50_mm);
    CHECK_FALSE(rig.history.canUndo());
}

TEST_CASE("Command_AMultiStepHistoryRewindsAndReplaysExactly", "[assembly][command][p13]") {
    // The realistic sequence the brief asks for, rewound to baseline and
    // replayed, checking canonical state at every position.
    Rig rig = makeRig();
    const ConfigurationId lean = require(rig.document.createConfiguration("Lean"));

    auto createArm = std::make_unique<assembly::CreateComponentCommand>(
        "Third", assembly::ComponentDefinition{.part = rig.part});
    auto* createRaw = createArm.get();
    REQUIRE(rig.history.execute(rig.document, std::move(createArm)).has_value());
    const ComponentId third = createRaw->componentId();

    REQUIRE(rig.history
                .execute(rig.document, std::make_unique<assembly::SetComponentPlacementCommand>(
                                           third, ComponentPlacement{.translation = {20_mm, 0_mm, 0_mm}}))
                .has_value());

    auto createGround = std::make_unique<assembly::CreateMateCommand>(
        "Ground", MateDefinition{.type = MateType::Fixed, .component = rig.base});
    auto* mateRaw = createGround.get();
    REQUIRE(rig.history.execute(rig.document, std::move(createGround)).has_value());
    const MateId ground = mateRaw->mateId();

    REQUIRE(rig.history
                .execute(rig.document,
                         std::make_unique<assembly::SuppressComponentCommand>(lean, third, true))
                .has_value());

    REQUIRE(rig.history.undoCount() == 4);
    const std::size_t objectsAtTop = rig.document.objectCount();

    // Rewind to baseline, one at a time.
    REQUIRE(rig.history.undo(rig.document).has_value());
    CHECK(rig.document.configurations().find(lean)->componentSuppression().empty());
    REQUIRE(rig.history.undo(rig.document).has_value());
    CHECK(assembly::findMate(rig.document, ground) == nullptr);
    REQUIRE(rig.history.undo(rig.document).has_value());
    CHECK(assembly::findComponent(rig.document, third)->definition().placement == ComponentPlacement{});
    REQUIRE(rig.history.undo(rig.document).has_value());
    CHECK(assembly::findComponent(rig.document, third) == nullptr);
    CHECK_FALSE(rig.history.canUndo());
    CHECK(rig.history.redoCount() == 4);

    // Replay to the top.
    for (int step = 0; step < 4; ++step) {
        REQUIRE(rig.history.redo(rig.document).has_value());
    }
    CHECK(rig.document.objectCount() == objectsAtTop);
    CHECK(assembly::findComponent(rig.document, third) != nullptr);
    CHECK(assembly::findComponent(rig.document, third)->definition().placement.translation[0] == 20_mm);
    CHECK(assembly::findMate(rig.document, ground) != nullptr);
    CHECK(rig.document.configurations().find(lean)->suppressionFor(third) == std::optional<bool>{true});
}

// --- Stable references ------------------------------------------------------------------------------------

TEST_CASE("Command_AMateStillNamesItsComponentAfterTheComponentIsUndoneAndRedone",
          "[assembly][command][p13]") {
    // The identity question. Delete a component a mate names, undo the
    // deletion, and the mate must still name the same component -- not a
    // lookalike, and not nothing.
    Rig rig = makeRig();
    const MateId holds = require(assembly::createMate(
        rig.document, "Holds",
        {.type = MateType::Coincident,
         .a = planeTarget(rig.base, PlaneReference{}),
         .b = planeTarget(rig.arm, PlaneReference{})}));
    const MateDefinition before = assembly::findMate(rig.document, holds)->definition();

    REQUIRE(rig.history.execute(rig.document, std::make_unique<DeleteObjectCommand>(ObjectId{rig.arm}))
                .has_value());
    CHECK(assembly::findComponent(rig.document, rig.arm) == nullptr);
    // The mate is untouched and still names the component that is gone --
    // unresolved, never rebound (P13-STREF-001).
    CHECK(assembly::findMate(rig.document, holds)->definition() == before);

    REQUIRE(rig.history.undo(rig.document).has_value());
    REQUIRE(assembly::findComponent(rig.document, rig.arm) != nullptr);
    CHECK(assembly::findMate(rig.document, holds)->definition() == before);
    CHECK(assembly::findMate(rig.document, holds)->definition().b->component == rig.arm);
}

// --- Determinism and persistence ------------------------------------------------------------------------------

TEST_CASE("Command_TheSameSequenceGivesTheSameDocument", "[assembly][command][p13][determinism]") {
    const auto build = [] {
        auto rig = std::make_unique<Rig>(makeRig());
        REQUIRE(rig->history
                    .execute(rig->document,
                             std::make_unique<assembly::SetComponentPlacementCommand>(
                                 rig->arm, ComponentPlacement{.translation = {3_mm, 4_mm, 5_mm}}))
                    .has_value());
        REQUIRE(rig->history
                    .execute(rig->document,
                             std::make_unique<assembly::CreateMateCommand>(
                                 "Ground", MateDefinition{.type = MateType::Fixed, .component = rig->base}))
                    .has_value());
        REQUIRE(rig->history.undo(rig->document).has_value());
        REQUIRE(rig->history.redo(rig->document).has_value());
        return rig;
    };
    auto first = build();
    auto second = build();

    CHECK(first->history.undoCount() == second->history.undoCount());
    CHECK(first->document.objectCount() == second->document.objectCount());
    CHECK(assembly::components(first->document) == assembly::components(second->document));
    CHECK(assembly::mates(first->document) == assembly::mates(second->document));
    CHECK(assembly::findComponent(first->document, first->arm)->definition() ==
          assembly::findComponent(second->document, second->arm)->definition());
}

TEST_CASE("Command_HistoryIsNotPersisted", "[assembly][command][p13][io]") {
    // The architecture keeps history transient: nothing in the file format
    // writes it, so a loaded document opens with an empty history and the
    // canonical state the commands produced.
    TempDir dir;
    Rig rig = makeRig();
    REQUIRE(rig.history
                .execute(rig.document, std::make_unique<assembly::SetComponentPlacementCommand>(
                                           rig.arm, ComponentPlacement{.translation = {0_mm, 0_mm, 33_mm}}))
                .has_value());
    REQUIRE(rig.history.undoCount() == 1);

    const auto path = dir.path() / "edited.bcad";
    REQUIRE(io::saveDocument(rig.document, path).has_value());
    const std::string text = readFile(path);
    CHECK_THAT(text, !ContainsSubstring("undo"));
    CHECK_THAT(text, !ContainsSubstring("history"));

    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());
    CommandHistory fresh;
    CHECK_FALSE(fresh.canUndo());
    CHECK_FALSE(fresh.canRedo());
    // And the canonical state the commands produced is what came back.
    CHECK(assembly::findComponent(*loaded, rig.arm)->definition().placement.translation[2] == 33_mm);
}
