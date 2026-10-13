// P17-CMD-001 — undoable structural-analysis edits (ADR-047).
//
// WHAT THESE TESTS HOLD, and it is two separate properties that a single
// equality would blur:
//
//   1. CANONICAL CONTENT is restored exactly by undo and by redo.
//   2. THE DERIVED RESULT is invalidated when the intent changes, and is NOT
//      invalidated when a command fails or changes nothing.
//
// A test that compared only (1) would pass while a staleness bug shipped, and
// one that compared only (2) would pass while undo quietly renumbered a load.
// So there is a canonical projection for (1) and a currentness check for (2),
// and no test uses one to stand in for the other.
//
// THE PROJECTION DELIBERATELY EXCLUDES THE REVISION. BetterCAD's revisions are
// monotonic: an edit moves them and an undo moves them again rather than
// rolling back, which is what stops an old result becoming current again just
// because the values returned to a previous state. "Undo exact" is therefore a
// statement about CONTENT, and the revision is checked separately and in the
// opposite direction -- it must NOT return to its old value.

#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Command.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/meshing/MeshControl.hpp>
#include <bettercad/meshing/Mesher.hpp>
#include <bettercad/sketch/Sketch.hpp>
#include <bettercad/structural/StructuralAnalysisObject.hpp>
#include <bettercad/structural/StructuralCommands.hpp>
#include <bettercad/structural/StructuralResult.hpp>
#include <bettercad/structural/StructuralValidation.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <array>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::ContainsSubstring;
using structural::AddStructuralLoadCommand;
using structural::AddStructuralRestraintCommand;
using structural::CreateStructuralAnalysisCommand;
using structural::DeleteStructuralAnalysisCommand;
using structural::EditStructuralLoadCommand;
using structural::EditStructuralRestraintCommand;
using structural::RemoveStructuralLoadCommand;
using structural::RemoveStructuralRestraintCommand;
using structural::SetSolverSettingsCommand;
using structural::SolverSettings;
using structural::StructuralAnalysis;
using structural::StructuralAnalysisDefinition;
using structural::StructuralLoad;
using structural::StructuralRestraint;

namespace {

/// A document with a body, a meshing control and one analysis.
///
/// NO MESH AND NO SOLVE. Every canonical, undo, redo and atomicity property
/// below is about the document, and generating a mesh for each of them would
/// make the suite slow for no added coverage. The result-invalidation cases
/// that genuinely need a solve say so and pay for it.
struct CommandPart {
    Document document{"Part"};
    features::Regenerator regenerator;
    CommandHistory history;
    ObjectId feature{};
    MeshControlId control{};
    AnalysisId analysis{};
    std::array<EntityId, 4> lines{};

    CommandPart() {
        auto profile = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
        lines = addRectangle(*profile, 0_mm, 0_mm, 40_mm, 30_mm);
        const ObjectId sketchId = require(document.addObject(std::move(profile)));
        auto extrude = features::ExtrudeFeature::create(
            "Solid", {.profile = SketchId::fromValue(sketchId.value()), .depth = 20_mm});
        REQUIRE(extrude.has_value());
        feature = require(document.addObject(std::move(*extrude)));

        meshing::MeshControlDefinition controls;
        controls.body = feature;
        controls.mesh.sizing.globalTargetSize = 12_mm;
        auto meshControl = meshing::MeshControl::create("Mesh", controls);
        REQUIRE(meshControl.has_value());
        control = MeshControlId::fromValue(
            require(document.addObject(std::move(*meshControl))).value());

        auto study = StructuralAnalysis::create("Study",
                                                StructuralAnalysisDefinition{.mesh = control});
        REQUIRE(study.has_value());
        analysis = AnalysisId::fromValue(require(document.addObject(std::move(*study))).value());
        requireReport(regenerator, document);
    }

    [[nodiscard]] FaceName startCap() const {
        return FaceName{feature, FaceSelector{.role = FaceRole::StartCap}};
    }
    [[nodiscard]] FaceName endCap() const {
        return FaceName{feature, FaceSelector{.role = FaceRole::EndCap}};
    }
    [[nodiscard]] FaceName side() const {
        return FaceName{feature, FaceSelector{.role = FaceRole::Side, .entity = lines.at(0)}};
    }

    [[nodiscard]] const StructuralAnalysisDefinition& definition() const {
        const StructuralAnalysis* object = structural::findStructuralAnalysis(document, analysis);
        REQUIRE(object != nullptr);
        return object->definition();
    }

    [[nodiscard]] std::uint64_t revision() const {
        const StructuralAnalysis* object = structural::findStructuralAnalysis(document, analysis);
        REQUIRE(object != nullptr);
        return object->revision();
    }

    /// Runs @p command through the history, so the undo/redo stacks are the
    /// real ones and not a hand-driven imitation.
    template <typename C, typename... Args>
    [[nodiscard]] Result<void> run(Args&&... args) {
        return history.execute(document, std::make_unique<C>(std::forward<Args>(args)...));
    }
};

/// An independent projection of the canonical structural intent.
///
/// BUILT BY HAND RATHER THAN BY COMPARING DEFINITIONS, so that it names every
/// field it checks and a new field added to the definition does not silently
/// fall outside the comparison -- the same instrument `MeshFingerprint` is for
/// P16. It carries the IDs, the targets, the values and the ORDER, and it
/// carries no revision and no derived state.
struct CanonicalIntent {
    MeshControlId mesh{};
    structural::StructuralAnalysisMode mode{};
    std::vector<StructuralLoad> loads{};
    std::vector<StructuralRestraint> restraints{};
    SolverSettings solver{};

    explicit CanonicalIntent(const StructuralAnalysisDefinition& definition)
        : mesh(definition.mesh), mode(definition.mode), loads(definition.loads),
          restraints(definition.restraints), solver(definition.solver) {}

    friend bool operator==(const CanonicalIntent&, const CanonicalIntent&) = default;
};

[[nodiscard]] CanonicalIntent intentOf(const CommandPart& part) {
    return CanonicalIntent{part.definition()};
}

[[nodiscard]] StructuralLoad pressureOn(const FaceName& face, LoadId id, double pascals) {
    return StructuralLoad{id, structural::PressureLoad{.face = face,
                                                       .magnitude = Pressure::fromSi(pascals)}};
}

[[nodiscard]] StructuralRestraint fixedOn(const FaceName& face, RestraintId id) {
    return StructuralRestraint::fixedSupport(id, face);
}

} // namespace

// ---------------------------------------------------------------------------
// Create and delete
// ---------------------------------------------------------------------------

TEST_CASE("StructuralCommand_CreateAnalysisUndoRedoKeepsTheSameAnalysisId",
          "[structural][command]") {
    // THE ID IS ENGINEERING INTENT, NOT AN IMPLEMENTATION DETAIL. A result
    // records which analysis it came from, so a redo that minted a new
    // AnalysisId would orphan every reference to the original.
    CommandPart part;
    const std::size_t before = part.document.objectCount();

    auto create = std::make_unique<CreateStructuralAnalysisCommand>(
        "Second", StructuralAnalysisDefinition{.mesh = part.control});
    const CreateStructuralAnalysisCommand* raw = create.get();
    REQUIRE(part.history.execute(part.document, std::move(create)).has_value());

    const AnalysisId created = raw->analysisId();
    REQUIRE(created.isValid());
    REQUIRE(part.document.objectCount() == before + 1U);
    REQUIRE(structural::findStructuralAnalysis(part.document, created) != nullptr);

    SECTION("undo removes it and redo brings back the SAME id") {
        REQUIRE(part.history.undo(part.document).has_value());
        CHECK(structural::findStructuralAnalysis(part.document, created) == nullptr);
        CHECK(part.document.objectCount() == before);

        REQUIRE(part.history.redo(part.document).has_value());
        CHECK(raw->analysisId() == created);
        const StructuralAnalysis* again =
            structural::findStructuralAnalysis(part.document, created);
        REQUIRE(again != nullptr);
        CHECK(again->definition().mesh == part.control);
    }
}

TEST_CASE("StructuralCommand_CreateAnalysisWithUnusableSettingsFailsAtomically",
          "[structural][command]") {
    // THE SETTINGS ARE CANONICAL NOW (ADR-047), so an unusable tolerance is a
    // reason to refuse the whole creation -- and the document must be untouched
    // and the history empty, because CommandHistory records only a command
    // whose execute() succeeded.
    CommandPart part;
    const std::size_t objects = part.document.objectCount();
    const std::size_t depth = part.history.undoCount();

    StructuralAnalysisDefinition bad{.mesh = part.control};
    bad.solver.relativeResidualTolerance = -1.0;

    const Result<void> refused = part.history.execute(
        part.document, std::make_unique<CreateStructuralAnalysisCommand>("Bad", bad));
    REQUIRE_FALSE(refused.has_value());

    CHECK(part.document.objectCount() == objects);
    CHECK(part.history.undoCount() == depth);
    CHECK_FALSE(part.history.canUndo());
}

TEST_CASE("StructuralCommand_DeleteAnalysisUndoRestoresTheWholeAnalysis",
          "[structural][command]") {
    // THE LOADS AND RESTRAINTS GO WITH IT AND COME BACK WITH IT, because the
    // definition owns them by value. There is no orphaned load to re-attach,
    // which is the scope question the checklist asks about.
    CommandPart part;
    REQUIRE(part.run<AddStructuralLoadCommand>(part.analysis,
                                               pressureOn(part.endCap(), LoadId::fromValue(1), 500.0))
                .has_value());
    REQUIRE(part.run<AddStructuralRestraintCommand>(
                    part.analysis, fixedOn(part.startCap(), RestraintId::fromValue(1)))
                .has_value());
    const CanonicalIntent full = intentOf(part);
    REQUIRE(full.loads.size() == 1U);
    REQUIRE(full.restraints.size() == 1U);

    REQUIRE(part.run<DeleteStructuralAnalysisCommand>(part.analysis).has_value());
    CHECK(structural::findStructuralAnalysis(part.document, part.analysis) == nullptr);

    REQUIRE(part.history.undo(part.document).has_value());
    REQUIRE(structural::findStructuralAnalysis(part.document, part.analysis) != nullptr);
    CHECK(intentOf(part) == full);
}

// ---------------------------------------------------------------------------
// Loads
// ---------------------------------------------------------------------------

TEST_CASE("StructuralCommand_AddLoadUndoRedoIsExact", "[structural][command]") {
    CommandPart part;
    const CanonicalIntent before = intentOf(part);

    REQUIRE(part.run<AddStructuralLoadCommand>(
                    part.analysis, pressureOn(part.endCap(), LoadId::fromValue(7), 1234.5))
                .has_value());
    const CanonicalIntent after = intentOf(part);
    REQUIRE(after.loads.size() == 1U);
    REQUIRE(after.loads.front().id() == LoadId::fromValue(7));
    REQUIRE(after != before);

    REQUIRE(part.history.undo(part.document).has_value());
    CHECK(intentOf(part) == before);
    REQUIRE(part.history.redo(part.document).has_value());
    CHECK(intentOf(part) == after);
}

TEST_CASE("StructuralCommand_ADuplicateLoadIdFailsAtomically", "[structural][command]") {
    CommandPart part;
    REQUIRE(part.run<AddStructuralLoadCommand>(
                    part.analysis, pressureOn(part.endCap(), LoadId::fromValue(1), 100.0))
                .has_value());
    const CanonicalIntent before = intentOf(part);
    const std::size_t depth = part.history.undoCount();

    const Result<void> refused = part.run<AddStructuralLoadCommand>(
        part.analysis, pressureOn(part.side(), LoadId::fromValue(1), 999.0));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().code == ErrorCode::AlreadyExists);

    // NOTHING PARTIALLY INSERTED, and no history entry for the attempt.
    CHECK(intentOf(part) == before);
    CHECK(part.history.undoCount() == depth);
}

TEST_CASE("StructuralCommand_ANonFiniteLoadValueFailsAtomically", "[structural][command]") {
    // REFUSED WITHOUT A MESH, which is the division `validate(StructuralLoad)`
    // exists to draw: whether the value is finite is a question about the
    // record, and whether the target resolves is a question about a mesh.
    CommandPart part;
    const CanonicalIntent before = intentOf(part);

    const Result<void> refused = part.run<AddStructuralLoadCommand>(
        part.analysis,
        pressureOn(part.endCap(), LoadId::fromValue(2),
                   std::numeric_limits<double>::quiet_NaN()));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().code == ErrorCode::InvalidArgument);
    CHECK_THAT(refused.error().message, ContainsSubstring("not finite"));
    CHECK(intentOf(part) == before);
    CHECK_FALSE(part.history.canUndo());
}

TEST_CASE("StructuralCommand_EditLoadRestoresEveryFieldExactly", "[structural][command]") {
    // EVERY CANONICAL FIELD, and the target among them. A FaceName is restored
    // as it was stored: no rebinding, no nearby-face fallback.
    CommandPart part;
    REQUIRE(part.run<AddStructuralLoadCommand>(
                    part.analysis, pressureOn(part.endCap(), LoadId::fromValue(3), 250.0))
                .has_value());
    const CanonicalIntent before = intentOf(part);

    REQUIRE(part.run<EditStructuralLoadCommand>(
                    part.analysis, pressureOn(part.side(), LoadId::fromValue(3), 777.25))
                .has_value());
    const CanonicalIntent after = intentOf(part);

    // The target AND the magnitude moved, asked of the record itself.
    REQUIRE(after.loads.size() == 1U);
    const structural::PressureLoad* edited = after.loads.front().pressure();
    REQUIRE(edited != nullptr);
    CHECK(edited->face == part.side());
    CHECK(edited->magnitude.si() == 777.25);

    REQUIRE(part.history.undo(part.document).has_value());
    CHECK(intentOf(part) == before);
    const structural::PressureLoad* restored = part.definition().loads.front().pressure();
    REQUIRE(restored != nullptr);
    // THE EXACT STORED VALUE, bit for bit -- not a tolerance. Undo assigns a
    // stored definition rather than inverting an arithmetic change.
    CHECK(restored->face == part.endCap());
    CHECK(restored->magnitude.si() == 250.0);

    REQUIRE(part.history.redo(part.document).has_value());
    CHECK(intentOf(part) == after);
}

TEST_CASE("StructuralCommand_EditingAMissingLoadFailsAndCreatesNothing",
          "[structural][command]") {
    CommandPart part;
    const CanonicalIntent before = intentOf(part);

    const Result<void> refused = part.run<EditStructuralLoadCommand>(
        part.analysis, pressureOn(part.endCap(), LoadId::fromValue(42), 10.0));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().code == ErrorCode::NotFound);
    // AND IT DID NOT SILENTLY ADD THE LOAD, which is what an edit-or-insert
    // would have done with a stale dialog.
    CHECK(intentOf(part) == before);
    CHECK(part.definition().loads.empty());
}

TEST_CASE("StructuralCommand_RemoveLoadUndoRestoresItAtItsOriginalPosition",
          "[structural][command]") {
    // ORDER IS THE USER'S AND IS PRESERVED, which the definition's own header
    // states. Undo restores the whole definition, so the position comes back
    // with the record rather than being reconstructed -- and a restored load
    // appended at the end would be a different canonical state.
    CommandPart part;
    for (std::uint64_t id : {1U, 2U, 3U}) {
        REQUIRE(part.run<AddStructuralLoadCommand>(
                        part.analysis,
                        pressureOn(part.endCap(), LoadId::fromValue(id),
                                   100.0 * static_cast<double>(id)))
                    .has_value());
    }
    const CanonicalIntent before = intentOf(part);
    REQUIRE(before.loads.size() == 3U);

    REQUIRE(part.run<RemoveStructuralLoadCommand>(part.analysis, LoadId::fromValue(2))
                .has_value());
    const CanonicalIntent after = intentOf(part);
    REQUIRE(after.loads.size() == 2U);
    CHECK(after.loads[0].id() == LoadId::fromValue(1));
    CHECK(after.loads[1].id() == LoadId::fromValue(3));

    REQUIRE(part.history.undo(part.document).has_value());
    CHECK(intentOf(part) == before);
    // THE MIDDLE, not the end.
    REQUIRE(part.definition().loads.size() == 3U);
    CHECK(part.definition().loads[1].id() == LoadId::fromValue(2));

    REQUIRE(part.history.redo(part.document).has_value());
    CHECK(intentOf(part) == after);
}

TEST_CASE("StructuralCommand_RemovingAMissingLoadFailsAtomically", "[structural][command]") {
    CommandPart part;
    const CanonicalIntent before = intentOf(part);
    const std::size_t depth = part.history.undoCount();

    const Result<void> refused =
        part.run<RemoveStructuralLoadCommand>(part.analysis, LoadId::fromValue(9));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().code == ErrorCode::NotFound);
    CHECK(intentOf(part) == before);
    CHECK(part.history.undoCount() == depth);
}

// ---------------------------------------------------------------------------
// Restraints
// ---------------------------------------------------------------------------

TEST_CASE("StructuralCommand_RestraintComponentsSurviveUndoAndRedoExactly",
          "[structural][command]") {
    // EVERY COMPONENT COMBINATION P17-BC-001 DEFINES, not just fixed. A mask
    // that round-tripped as "fixed" whatever it was would satisfy a test that
    // only used fixed supports.
    CommandPart part;
    using structural::DofComponent;
    using structural::RestraintComponents;

    const std::array<RestraintComponents, 7> masks{
        RestraintComponents::along(DofComponent::Ux),
        RestraintComponents::along(DofComponent::Uy),
        RestraintComponents::along(DofComponent::Uz),
        RestraintComponents::along(DofComponent::Ux).add(DofComponent::Uy),
        RestraintComponents::along(DofComponent::Ux).add(DofComponent::Uz),
        RestraintComponents::along(DofComponent::Uy).add(DofComponent::Uz),
        RestraintComponents::fixed()};

    std::uint64_t next = 1;
    for (const RestraintComponents& mask : masks) {
        const RestraintId id = RestraintId::fromValue(next++);
        const CanonicalIntent before = intentOf(part);
        REQUIRE(part.run<AddStructuralRestraintCommand>(
                        part.analysis, StructuralRestraint{id, part.startCap(), mask})
                    .has_value());
        const CanonicalIntent after = intentOf(part);
        INFO("mask " << toString(mask));
        REQUIRE(after.restraints.back().components() == mask);

        REQUIRE(part.history.undo(part.document).has_value());
        CHECK(intentOf(part) == before);
        REQUIRE(part.history.redo(part.document).has_value());
        CHECK(intentOf(part) == after);
        CHECK(part.definition().restraints.back().components() == mask);
    }
}

TEST_CASE("StructuralCommand_EditRestraintChangesTargetAndComponentsTogether",
          "[structural][command]") {
    // The checklist's worked example: Face A / Ux becomes Face B / Ux+Uz, and
    // undo must recover exactly Face A / Ux.
    CommandPart part;
    using structural::DofComponent;
    using structural::RestraintComponents;

    const RestraintId id = RestraintId::fromValue(1);
    REQUIRE(part.run<AddStructuralRestraintCommand>(
                    part.analysis,
                    StructuralRestraint{id, part.startCap(),
                                        RestraintComponents::along(DofComponent::Ux)})
                .has_value());
    const CanonicalIntent before = intentOf(part);

    REQUIRE(part.run<EditStructuralRestraintCommand>(
                    part.analysis,
                    StructuralRestraint{
                        id, part.side(),
                        RestraintComponents::along(DofComponent::Ux).add(DofComponent::Uz)})
                .has_value());
    CHECK(part.definition().restraints.front().face() == part.side());
    CHECK(part.definition().restraints.front().components().holds(DofComponent::Uz));

    REQUIRE(part.history.undo(part.document).has_value());
    CHECK(intentOf(part) == before);
    CHECK(part.definition().restraints.front().face() == part.startCap());
    CHECK(part.definition().restraints.front().components() ==
          RestraintComponents::along(DofComponent::Ux));
    CHECK_FALSE(part.definition().restraints.front().components().holds(DofComponent::Uz));
}

TEST_CASE("StructuralCommand_AnEmptyRestraintMaskFailsAtomically", "[structural][command]") {
    // `validate(StructuralRestraint)` owns this rule and refuses a mask that
    // holds nothing. The command reuses it rather than restating it.
    CommandPart part;
    const CanonicalIntent before = intentOf(part);

    const Result<void> refused = part.run<AddStructuralRestraintCommand>(
        part.analysis,
        StructuralRestraint{RestraintId::fromValue(1), part.startCap(),
                            structural::RestraintComponents{}});
    REQUIRE_FALSE(refused.has_value());
    CHECK(intentOf(part) == before);
    CHECK_FALSE(part.history.canUndo());
}

TEST_CASE("StructuralCommand_RemoveRestraintUndoRestoresIdFaceAndComponents",
          "[structural][command]") {
    CommandPart part;
    using structural::DofComponent;
    using structural::RestraintComponents;

    REQUIRE(part.run<AddStructuralRestraintCommand>(
                    part.analysis, fixedOn(part.startCap(), RestraintId::fromValue(1)))
                .has_value());
    REQUIRE(part.run<AddStructuralRestraintCommand>(
                    part.analysis,
                    StructuralRestraint{RestraintId::fromValue(2), part.side(),
                                        RestraintComponents::along(DofComponent::Uz)})
                .has_value());
    const CanonicalIntent before = intentOf(part);

    REQUIRE(part.run<RemoveStructuralRestraintCommand>(part.analysis, RestraintId::fromValue(1))
                .has_value());
    REQUIRE(part.definition().restraints.size() == 1U);
    CHECK(part.definition().restraints.front().id() == RestraintId::fromValue(2));

    REQUIRE(part.history.undo(part.document).has_value());
    CHECK(intentOf(part) == before);
    // THE SAME RestraintId, at the same position, with the same face and mask.
    REQUIRE(part.definition().restraints.size() == 2U);
    CHECK(part.definition().restraints[0].id() == RestraintId::fromValue(1));
    CHECK(part.definition().restraints[0].face() == part.startCap());
    CHECK(part.definition().restraints[0].components().isFixed());
}

// ---------------------------------------------------------------------------
// Solver settings
// ---------------------------------------------------------------------------

TEST_CASE("StructuralCommand_SolverSettingsUndoRedoIsExact", "[structural][command]") {
    CommandPart part;
    const SolverSettings original = part.definition().solver;

    SolverSettings tightened;
    tightened.relativeResidualTolerance = 1.0e-11;
    tightened.pivotFloor = 1.0e-14;
    REQUIRE(part.run<SetSolverSettingsCommand>(part.analysis, tightened).has_value());
    CHECK(part.definition().solver == tightened);

    REQUIRE(part.history.undo(part.document).has_value());
    // THE EXACT PREVIOUS VALUES, not a library default substituted during
    // replay.
    CHECK(part.definition().solver == original);
    CHECK(part.definition().solver.relativeResidualTolerance ==
          original.relativeResidualTolerance);

    REQUIRE(part.history.redo(part.document).has_value());
    CHECK(part.definition().solver == tightened);
}

TEST_CASE("StructuralCommand_InvalidSolverSettingsFailAtomically", "[structural][command]") {
    CommandPart part;
    const SolverSettings original = part.definition().solver;
    const std::size_t depth = part.history.undoCount();

    for (const double bad : {0.0, -1.0, std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::infinity()}) {
        INFO("tolerance " << bad);
        SolverSettings broken;
        broken.relativeResidualTolerance = bad;
        const Result<void> refused = part.run<SetSolverSettingsCommand>(part.analysis, broken);
        REQUIRE_FALSE(refused.has_value());
        // THE OLD SETTINGS REMAIN EXACT, and no history entry was made.
        CHECK(part.definition().solver == original);
        CHECK(part.history.undoCount() == depth);
    }

    SolverSettings badPivot;
    badPivot.pivotFloor = -1.0;
    REQUIRE_FALSE(part.run<SetSolverSettingsCommand>(part.analysis, badPivot).has_value());
    CHECK(part.definition().solver == original);
    CHECK(part.history.undoCount() == depth);
}

// ---------------------------------------------------------------------------
// The history's own semantics
// ---------------------------------------------------------------------------

TEST_CASE("StructuralCommand_ANoOpEditSucceedsAndMovesNoRevision", "[structural][command]") {
    // THE PROJECT'S EXISTING POLICY, not a P17 special case: a no-op edit
    // succeeds, changes nothing, moves no revision -- and therefore invalidates
    // no result -- and is still recorded by CommandHistory, which records every
    // command whose execute() succeeded. The same behaviour P15's material edit
    // and P16's mesh-control edit have.
    CommandPart part;
    const SolverSettings same = part.definition().solver;
    const std::uint64_t revision = part.revision();
    const std::size_t depth = part.history.undoCount();

    auto command = std::make_unique<SetSolverSettingsCommand>(part.analysis, same);
    const SetSolverSettingsCommand* raw = command.get();
    REQUIRE(part.history.execute(part.document, std::move(command)).has_value());

    CHECK_FALSE(raw->changedAnything());
    CHECK(part.revision() == revision);
    // Recorded all the same.
    CHECK(part.history.undoCount() == depth + 1U);
}

TEST_CASE("StructuralCommand_UndoMovesTheRevisionForwardRatherThanBack",
          "[structural][command]") {
    // THE DISTINCTION THE CHECKLIST ASKS FOR, and it is the reason "undo
    // exact" is a statement about CONTENT. BetterCAD's revisions are
    // monotonic: an undo restores the values and takes a NEW revision, which is
    // exactly what stops a previously computed result becoming current again
    // merely because the numbers returned to a previous state.
    CommandPart part;
    const std::uint64_t start = part.revision();

    REQUIRE(part.run<AddStructuralLoadCommand>(
                    part.analysis, pressureOn(part.endCap(), LoadId::fromValue(1), 10.0))
                .has_value());
    const std::uint64_t edited = part.revision();
    REQUIRE(edited > start);

    REQUIRE(part.history.undo(part.document).has_value());
    const std::uint64_t undone = part.revision();

    // CONTENT is back.
    CHECK(part.definition().loads.empty());
    // IDENTITY IS NOT. The revision advanced again rather than rolling back.
    CHECK(undone > edited);
    CHECK(undone != start);
}

TEST_CASE("StructuralCommand_ASuccessfulNewCommandInvalidatesTheRedoBranch",
          "[structural][command]") {
    CommandPart part;
    REQUIRE(part.run<AddStructuralLoadCommand>(
                    part.analysis, pressureOn(part.endCap(), LoadId::fromValue(1), 10.0))
                .has_value());
    REQUIRE(part.run<AddStructuralLoadCommand>(
                    part.analysis, pressureOn(part.side(), LoadId::fromValue(2), 20.0))
                .has_value());

    REQUIRE(part.history.undo(part.document).has_value());
    REQUIRE(part.history.canRedo());

    // A NEW BRANCH. The abandoned one must not remain redoable.
    REQUIRE(part.run<AddStructuralRestraintCommand>(
                    part.analysis, fixedOn(part.startCap(), RestraintId::fromValue(1)))
                .has_value());
    CHECK_FALSE(part.history.canRedo());
    CHECK(part.history.redoCount() == 0U);
}

TEST_CASE("StructuralCommand_AFailedNewCommandLeavesTheRedoBranchIntact",
          "[structural][command]") {
    // THE EDGE CASE THE CHECKLIST SINGLES OUT, and the answer comes from the
    // existing framework rather than from a P17 opinion: CommandHistory clears
    // the redo stack inside execute() only AFTER the command succeeds, so a
    // refused attempt commits no new branch and the redo stays available.
    CommandPart part;
    REQUIRE(part.run<AddStructuralLoadCommand>(
                    part.analysis, pressureOn(part.endCap(), LoadId::fromValue(1), 10.0))
                .has_value());
    REQUIRE(part.history.undo(part.document).has_value());
    REQUIRE(part.history.canRedo());
    const CanonicalIntent before = intentOf(part);

    // An invalid command: there is no load 99 to edit.
    REQUIRE_FALSE(part.run<EditStructuralLoadCommand>(
                          part.analysis, pressureOn(part.endCap(), LoadId::fromValue(99), 1.0))
                      .has_value());

    CHECK(intentOf(part) == before);
    CHECK(part.history.canRedo());
    REQUIRE(part.history.redo(part.document).has_value());
    CHECK(part.definition().loads.size() == 1U);
}

TEST_CASE("StructuralCommand_UndoAllThenRedoAllIsExactAtEveryStep",
          "[structural][command]") {
    // THE WHOLE SEQUENCE THE CHECKLIST NAMES, with a canonical snapshot after
    // every command, undone to the start comparing each step, then redone
    // comparing each step again.
    CommandPart part;
    using structural::DofComponent;
    using structural::RestraintComponents;

    std::vector<CanonicalIntent> states;
    states.emplace_back(part.definition());

    const auto step = [&](Result<void> outcome) {
        INFO((outcome.has_value() ? std::string{} : outcome.error().message));
        REQUIRE(outcome.has_value());
        states.emplace_back(part.definition());
    };

    step(part.run<AddStructuralLoadCommand>(
        part.analysis, pressureOn(part.endCap(), LoadId::fromValue(1), 100.0)));
    step(part.run<AddStructuralLoadCommand>(
        part.analysis, pressureOn(part.side(), LoadId::fromValue(2), 200.0)));
    step(part.run<AddStructuralRestraintCommand>(
        part.analysis, fixedOn(part.startCap(), RestraintId::fromValue(1))));
    step(part.run<AddStructuralRestraintCommand>(
        part.analysis, StructuralRestraint{RestraintId::fromValue(2), part.side(),
                                           RestraintComponents::along(DofComponent::Uz)}));
    step(part.run<EditStructuralLoadCommand>(
        part.analysis, pressureOn(part.endCap(), LoadId::fromValue(1), 111.5)));
    step(part.run<EditStructuralRestraintCommand>(
        part.analysis, StructuralRestraint{RestraintId::fromValue(2), part.endCap(),
                                           RestraintComponents::fixed()}));
    SolverSettings tuned;
    tuned.relativeResidualTolerance = 5.0e-10;
    step(part.run<SetSolverSettingsCommand>(part.analysis, tuned));
    step(part.run<RemoveStructuralLoadCommand>(part.analysis, LoadId::fromValue(2)));
    step(part.run<RemoveStructuralRestraintCommand>(part.analysis, RestraintId::fromValue(1)));

    REQUIRE(states.size() == 10U);
    // NON-VACUITY: every step really changed the canonical state, so the
    // comparisons below are not nine copies of one value.
    for (std::size_t i = 1; i < states.size(); ++i) {
        INFO("step " << i);
        REQUIRE_FALSE(states[i] == states[i - 1U]);
    }

    for (std::size_t i = states.size() - 1U; i > 0U; --i) {
        REQUIRE(part.history.undo(part.document).has_value());
        INFO("after undoing to step " << (i - 1U));
        REQUIRE(intentOf(part) == states[i - 1U]);
    }
    CHECK_FALSE(part.history.canUndo());

    for (std::size_t i = 1; i < states.size(); ++i) {
        REQUIRE(part.history.redo(part.document).has_value());
        INFO("after redoing to step " << i);
        REQUIRE(intentOf(part) == states[i]);
    }
    CHECK_FALSE(part.history.canRedo());
}

TEST_CASE("StructuralCommand_RepeatedUndoRedoDoesNotDriftTheStoredValues",
          "[structural][command]") {
    // UNDO ASSIGNS A STORED DEFINITION RATHER THAN INVERTING A DELTA, so the
    // pair is exactly reversible however many times it is repeated. A
    // `magnitude -= delta` / `+= delta` implementation would accumulate error
    // here and this is the case that would catch it.
    CommandPart part;
    const double exact = 1234.56789;
    REQUIRE(part.run<AddStructuralLoadCommand>(
                    part.analysis, pressureOn(part.endCap(), LoadId::fromValue(1), exact))
                .has_value());
    const CanonicalIntent added = intentOf(part);
    const CanonicalIntent empty = CanonicalIntent{StructuralAnalysisDefinition{
        .mesh = part.control, .mode = part.definition().mode}};

    for (int cycle = 0; cycle < 50; ++cycle) {
        INFO("cycle " << cycle);
        REQUIRE(part.history.undo(part.document).has_value());
        REQUIRE(part.definition().loads.empty());
        REQUIRE(part.history.redo(part.document).has_value());
        REQUIRE(intentOf(part) == added);
        // BIT FOR BIT, not within a tolerance.
        REQUIRE(part.definition().loads.front().pressure()->magnitude.si() == exact);
    }
    (void)empty;
}

TEST_CASE("StructuralCommand_UndoAndRedoRefuseBeforeExecution", "[structural][command]") {
    // A command that was never executed has no before-state to restore, and
    // says so rather than assigning a default-constructed definition.
    CommandPart part;
    SetSolverSettingsCommand command{part.analysis, SolverSettings{}};
    const Result<void> undone = command.undo(part.document);
    REQUIRE_FALSE(undone.has_value());
    CHECK(undone.error().code == ErrorCode::FailedPrecondition);
    const Result<void> redone = command.redo(part.document);
    REQUIRE_FALSE(redone.has_value());
    CHECK(redone.error().code == ErrorCode::FailedPrecondition);
}

TEST_CASE("StructuralCommand_CommandsOnAMissingAnalysisFailAtomically",
          "[structural][command]") {
    CommandPart part;
    const AnalysisId absent = AnalysisId::fromValue(987654);
    const CanonicalIntent before = intentOf(part);

    const Result<void> load = part.run<AddStructuralLoadCommand>(
        absent, pressureOn(part.endCap(), LoadId::fromValue(1), 10.0));
    REQUIRE_FALSE(load.has_value());
    CHECK(load.error().code == ErrorCode::NotFound);

    const Result<void> settings = part.run<SetSolverSettingsCommand>(absent, SolverSettings{});
    REQUIRE_FALSE(settings.has_value());
    CHECK(settings.error().code == ErrorCode::NotFound);

    CHECK(intentOf(part) == before);
    CHECK_FALSE(part.history.canUndo());
}

TEST_CASE("StructuralCommand_TwoAnalysesDoNotConfuseTheirLoadIds",
          "[structural][command]") {
    // THE SAME LoadId IN TWO ANALYSES IS TWO DIFFERENT LOADS, and a command
    // names its analysis, so editing one cannot reach the other.
    CommandPart part;
    auto create = std::make_unique<CreateStructuralAnalysisCommand>(
        "Second", StructuralAnalysisDefinition{.mesh = part.control});
    const CreateStructuralAnalysisCommand* raw = create.get();
    REQUIRE(part.history.execute(part.document, std::move(create)).has_value());
    const AnalysisId second = raw->analysisId();

    REQUIRE(part.run<AddStructuralLoadCommand>(
                    part.analysis, pressureOn(part.endCap(), LoadId::fromValue(1), 100.0))
                .has_value());
    REQUIRE(part.run<AddStructuralLoadCommand>(
                    second, pressureOn(part.side(), LoadId::fromValue(1), 900.0))
                .has_value());

    // Editing the FIRST analysis's load 1 leaves the second's alone.
    REQUIRE(part.run<EditStructuralLoadCommand>(
                    part.analysis, pressureOn(part.endCap(), LoadId::fromValue(1), 101.0))
                .has_value());

    CHECK(part.definition().loads.front().pressure()->magnitude.si() == 101.0);
    const StructuralAnalysis* other = structural::findStructuralAnalysis(part.document, second);
    REQUIRE(other != nullptr);
    CHECK(other->definition().loads.front().pressure()->magnitude.si() == 900.0);
    CHECK(other->definition().loads.front().pressure()->face == part.side());
}


TEST_CASE("StructuralCommand_AnEditKeepsTheLoadAtItsOwnPosition",
          "[structural][command]") {
    // THE GAP PROBE C8 FOUND. `EditLoadRestoresEveryFieldExactly` uses ONE
    // load, so an implementation that erased the record and pushed the
    // replacement onto the end left it in the same place and passed -- and
    // every other case was equally blind, because none of them edited a load
    // that had neighbours.
    //
    // ORDER IS THE USER'S AND IS PRESERVED, which the definition's own header
    // states. An edit is not a remove followed by an add, and the difference is
    // only visible with a load on each side.
    CommandPart part;
    for (std::uint64_t id : {1U, 2U, 3U}) {
        REQUIRE(part.run<AddStructuralLoadCommand>(
                        part.analysis,
                        pressureOn(part.endCap(), LoadId::fromValue(id),
                                   100.0 * static_cast<double>(id)))
                    .has_value());
    }
    REQUIRE(part.definition().loads.size() == 3U);
    const CanonicalIntent before = intentOf(part);

    // EDIT THE MIDDLE ONE, which is the only position that can detect either
    // an append or a prepend.
    REQUIRE(part.run<EditStructuralLoadCommand>(
                    part.analysis, pressureOn(part.side(), LoadId::fromValue(2), 999.0))
                .has_value());

    const std::vector<StructuralLoad>& loads = part.definition().loads;
    REQUIRE(loads.size() == 3U);
    // THE ORDER IS UNCHANGED, asked id by id rather than as a whole.
    CHECK(loads[0].id() == LoadId::fromValue(1));
    CHECK(loads[1].id() == LoadId::fromValue(2));
    CHECK(loads[2].id() == LoadId::fromValue(3));
    // AND IT IS THE EDITED RECORD THAT SITS THERE, so the position check is
    // not satisfied by the edit having been dropped.
    const structural::PressureLoad* edited = loads[1].pressure();
    REQUIRE(edited != nullptr);
    CHECK(edited->magnitude.si() == 999.0);
    CHECK(edited->face == part.side());
    // THE NEIGHBOURS ARE UNTOUCHED.
    CHECK(loads[0].pressure()->magnitude.si() == 100.0);
    CHECK(loads[2].pressure()->magnitude.si() == 300.0);

    SECTION("and undo puts the original back in the same position") {
        REQUIRE(part.history.undo(part.document).has_value());
        CHECK(intentOf(part) == before);
        const std::vector<StructuralLoad>& restored = part.definition().loads;
        REQUIRE(restored.size() == 3U);
        CHECK(restored[1].id() == LoadId::fromValue(2));
        CHECK(restored[1].pressure()->magnitude.si() == 200.0);
        CHECK(restored[1].pressure()->face == part.endCap());
    }
}

TEST_CASE("StructuralCommand_AnEditKeepsTheRestraintAtItsOwnPosition",
          "[structural][command]") {
    // THE SAME GAP ON THE RESTRAINT SIDE. C8 probed the load path; this is the
    // one that would have survived the equivalent mutation of the restraint
    // path, which no probe in the batch made.
    CommandPart part;
    using structural::DofComponent;
    using structural::RestraintComponents;

    REQUIRE(part.run<AddStructuralRestraintCommand>(
                    part.analysis, fixedOn(part.startCap(), RestraintId::fromValue(1)))
                .has_value());
    REQUIRE(part.run<AddStructuralRestraintCommand>(
                    part.analysis,
                    StructuralRestraint{RestraintId::fromValue(2), part.side(),
                                        RestraintComponents::along(DofComponent::Ux)})
                .has_value());
    REQUIRE(part.run<AddStructuralRestraintCommand>(
                    part.analysis,
                    StructuralRestraint{RestraintId::fromValue(3), part.endCap(),
                                        RestraintComponents::along(DofComponent::Uz)})
                .has_value());

    REQUIRE(part.run<EditStructuralRestraintCommand>(
                    part.analysis,
                    StructuralRestraint{RestraintId::fromValue(2), part.endCap(),
                                        RestraintComponents::fixed()})
                .has_value());

    const std::vector<StructuralRestraint>& held = part.definition().restraints;
    REQUIRE(held.size() == 3U);
    CHECK(held[0].id() == RestraintId::fromValue(1));
    CHECK(held[1].id() == RestraintId::fromValue(2));
    CHECK(held[2].id() == RestraintId::fromValue(3));
    CHECK(held[1].components().isFixed());
    CHECK(held[1].face() == part.endCap());
    // The neighbours kept their own masks.
    CHECK(held[0].components().isFixed());
    CHECK(held[2].components() == RestraintComponents::along(DofComponent::Uz));
}



TEST_CASE("StructuralCommand_ACommandWhoseApplyFailedCannotBeUndone",
          "[structural][command]") {
    // THE GAP PROBE C3 FOUND, and it took two attempts to even reach: the
    // first anchor was wrong, and when the probe finally ran it SURVIVED.
    //
    // C3 moves `before_ = before` ABOVE the check on apply()'s result, so a
    // command whose apply FAILED still records a before-state and becomes
    // undoable. Every other case missed it because, for the add, edit and
    // remove commands, APPLY CANNOT FAIL AFTER EDIT SUCCEEDED: they validate
    // their own record, and `validate(definition)` only checks the mesh handle
    // and the solver settings, neither of which those commands touch.
    //
    // SetStructuralAnalysisDefinitionCommand IS THE ONE THAT CAN. It replaces
    // the whole definition and its `edit` validates nothing -- `setDefinition`
    // does -- so a definition carrying an invalid mesh handle reaches apply()
    // and is refused there. That is the only path through this base where the
    // ordering C3 mutates is observable.
    //
    // CONSTRUCTED DIRECTLY, NOT THROUGH THE HISTORY, because CommandHistory
    // discards a command whose execute() failed -- so a test driven through the
    // history could never ask the command anything afterwards.
    CommandPart part;
    const CanonicalIntent before = intentOf(part);

    // A default-constructed definition: its MeshControlId is the invalid
    // handle, which is exactly what `validate(StructuralAnalysisDefinition)`
    // refuses.
    const StructuralAnalysisDefinition broken{};
    REQUIRE_FALSE(broken.mesh.isValid());

    structural::SetStructuralAnalysisDefinitionCommand command{part.analysis, broken};
    const Result<void> executed = command.execute(part.document);
    REQUIRE_FALSE(executed.has_value());
    CHECK(executed.error().code == ErrorCode::InvalidArgument);

    // ATOMIC: the document is untouched.
    CHECK(intentOf(part) == before);

    // AND THE COMMAND IS NOT UNDOABLE, because its execute() did not succeed.
    // With `before_` recorded before the apply was checked, this undo would
    // instead succeed and assign a state that was never current.
    const Result<void> undone = command.undo(part.document);
    REQUIRE_FALSE(undone.has_value());
    CHECK(undone.error().code == ErrorCode::FailedPrecondition);
    CHECK(intentOf(part) == before);

    // Nor redoable, for the same reason.
    const Result<void> redone = command.redo(part.document);
    REQUIRE_FALSE(redone.has_value());
    CHECK(redone.error().code == ErrorCode::FailedPrecondition);

    SECTION("and the same definition with a VALID mesh is accepted, so the refusal is specific") {
        // NON-VACUITY. Without this the test would also pass against a command
        // that refused every whole-definition edit.
        StructuralAnalysisDefinition usable{};
        usable.mesh = part.control;
        structural::SetStructuralAnalysisDefinitionCommand good{part.analysis, usable};
        REQUIRE(good.execute(part.document).has_value());
        REQUIRE(good.undo(part.document).has_value());
        CHECK(intentOf(part) == before);
    }
}
// ---------------------------------------------------------------------------
// Result invalidation, which needs a real solve
// ---------------------------------------------------------------------------

namespace {

/// A solvable analysis: a block with a material, a mesh, a load and a restraint.
///
/// EXPENSIVE, AND ONLY THE INVALIDATION CASES PAY FOR IT. Every canonical,
/// undo, redo and atomicity property above is about the document and needs no
/// mesh; these cases are about what a command does to a GENERATED result, so
/// they need one that exists.
struct SolvablePart {
    Document document{"Part"};
    features::Regenerator regenerator;
    meshing::Mesher mesher;
    CommandHistory history;
    ObjectId feature{};
    MeshControlId control{};
    AnalysisId analysis{};
    MaterialId material{};
    std::array<EntityId, 4> lines{};

    SolvablePart() {
        auto profile = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
        lines = addRectangle(*profile, 0_mm, 0_mm, 40_mm, 30_mm);
        const ObjectId sketchId = require(document.addObject(std::move(profile)));
        auto extrude = features::ExtrudeFeature::create(
            "Solid", {.profile = SketchId::fromValue(sketchId.value()), .depth = 20_mm});
        REQUIRE(extrude.has_value());
        feature = require(document.addObject(std::move(*extrude)));

        // NAMED `steel`, NOT `definition`: a local called `definition` shadows
        // this fixture's own definition() accessor and the compiler reports it
        // as an "invalid use of member" four lines later.
        features::MaterialDefinition steel;
        steel.designation = "Steel";
        steel.mechanical.youngsModulus =
            materials::MaterialProperty<ElasticModulus>::known(ElasticModulus::fromSi(2.1e11));
        steel.mechanical.poissonRatio =
            materials::MaterialProperty<bettercad::PoissonRatio>::known(PoissonRatio::of(0.3));
        const Result<MaterialId> id = features::createMaterial(document, "Steel", steel);
        REQUIRE(id.has_value());
        material = *id;
        REQUIRE(features::assignMaterial(document, material).has_value());

        meshing::MeshControlDefinition controls;
        controls.body = feature;
        controls.mesh.sizing.globalTargetSize = 12_mm;
        auto meshControl = meshing::MeshControl::create("Mesh", controls);
        REQUIRE(meshControl.has_value());
        control = MeshControlId::fromValue(
            require(document.addObject(std::move(*meshControl))).value());

        StructuralAnalysisDefinition analysisDefinition{.mesh = control};
        analysisDefinition.restraints.push_back(
            StructuralRestraint::fixedSupport(RestraintId::fromValue(1), startCap()));
        analysisDefinition.loads.push_back(StructuralLoad{
            LoadId::fromValue(1),
            structural::PressureLoad{.face = endCap(), .magnitude = Pressure::fromSi(-1.0e6)}});
        auto study = StructuralAnalysis::create("Study", analysisDefinition);
        REQUIRE(study.has_value());
        analysis = AnalysisId::fromValue(require(document.addObject(std::move(*study))).value());

        requireReport(regenerator, document);
        REQUIRE(mesher.generate(document, regenerator, control).has_value());
    }

    [[nodiscard]] FaceName startCap() const {
        return FaceName{feature, FaceSelector{.role = FaceRole::StartCap}};
    }
    [[nodiscard]] FaceName endCap() const {
        return FaceName{feature, FaceSelector{.role = FaceRole::EndCap}};
    }

    [[nodiscard]] const StructuralAnalysisDefinition& definition() const {
        const StructuralAnalysis* object = structural::findStructuralAnalysis(document, analysis);
        REQUIRE(object != nullptr);
        return object->definition();
    }

    [[nodiscard]] structural::StructuralSolveOutcome solve() {
        Result<structural::StructuralSolveOutcome> outcome =
            structural::solveStructuralAnalysis(document, regenerator, mesher, analysis);
        INFO((outcome.has_value() ? std::string{} : outcome.error().message));
        REQUIRE(outcome.has_value());
        return std::move(*outcome);
    }

    /// Where @p result stands against what a result computed NOW would carry.
    ///
    /// THROUGH P17-DATA'S OWN TWO FUNCTIONS, not a flag of this milestone's:
    /// `currentResultSource` reads the document and `resultCurrency` compares
    /// the provenance. P17-CMD adds no third opinion about staleness.
    [[nodiscard]] structural::ResultCurrency
    currencyOf(const structural::StructuralResult& result) const {
        const Result<structural::StructuralResultSource> source =
            structural::currentResultSource(document, regenerator, mesher, analysis);
        INFO((source.has_value() ? std::string{} : source.error().message));
        REQUIRE(source.has_value());
        return structural::resultCurrency(&result, *source);
    }

    [[nodiscard]] meshing::MeshCurrency meshCurrency() const {
        return mesher.currency(document, control);
    }

    template <typename C, typename... Args>
    [[nodiscard]] Result<void> run(Args&&... args) {
        return history.execute(document, std::make_unique<C>(std::forward<Args>(args)...));
    }
};

[[nodiscard]] StructuralLoad pressureAt(const FaceName& face, std::uint64_t id, double pascals) {
    return StructuralLoad{
        LoadId::fromValue(id),
        structural::PressureLoad{.face = face, .magnitude = Pressure::fromSi(pascals)}};
}

} // namespace

TEST_CASE("StructuralCommand_EveryPhysicsCommandStalesTheResultAndNotTheMesh",
          "[structural][command][solve]") {
    // THE CENTRAL INVALIDATION RULE, and the asymmetry is the point: a load,
    // restraint or settings edit changes what the ANSWER should be, and changes
    // nothing about the mesh it would be computed on. Over-invalidating the
    // mesh would force a remesh for a tolerance change.
    //
    // NO NEW MECHANISM IS BEING TESTED HERE. An edit moves the analysis
    // object's revision, and the result's recorded source already carries that
    // revision -- so staleness follows from the model P17-DATA-001 established
    // rather than from anything P17-CMD added.
    SolvablePart part;
    const structural::StructuralSolveOutcome first = part.solve();

    // THE PRECONDITION: the result really is current before each command, so a
    // "no longer current" below is a transition and not a standing state.
    REQUIRE(part.currencyOf(first.result) == structural::ResultCurrency::Current);
    REQUIRE(describesTheModel(part.meshCurrency()));

    SECTION("a load edit") {
        REQUIRE(part.run<EditStructuralLoadCommand>(part.analysis,
                                                    pressureAt(part.endCap(), 1U, -1.1e6))
                    .has_value());
        CHECK(part.currencyOf(first.result) == structural::ResultCurrency::Stale);
        CHECK(describesTheModel(part.meshCurrency()));
    }

    SECTION("a load edit of one part in a million still stales it") {
        // SIZE IS NOT THE TEST. Any change to the intent makes the old answer
        // an answer to a different question.
        REQUIRE(part.run<EditStructuralLoadCommand>(part.analysis,
                                                    pressureAt(part.endCap(), 1U, -1.000001e6))
                    .has_value());
        CHECK(part.currencyOf(first.result) == structural::ResultCurrency::Stale);
        CHECK(describesTheModel(part.meshCurrency()));
    }

    SECTION("adding a load") {
        REQUIRE(part.run<AddStructuralLoadCommand>(part.analysis,
                                                   pressureAt(part.startCap(), 2U, -5.0e5))
                    .has_value());
        CHECK(part.currencyOf(first.result) == structural::ResultCurrency::Stale);
        CHECK(describesTheModel(part.meshCurrency()));
    }

    SECTION("removing a load") {
        REQUIRE(part.run<RemoveStructuralLoadCommand>(part.analysis, LoadId::fromValue(1))
                    .has_value());
        CHECK(part.currencyOf(first.result) == structural::ResultCurrency::Stale);
        CHECK(describesTheModel(part.meshCurrency()));
    }

    SECTION("a restraint edit") {
        REQUIRE(part.run<EditStructuralRestraintCommand>(
                        part.analysis,
                        StructuralRestraint{RestraintId::fromValue(1), part.startCap(),
                                            structural::RestraintComponents::along(
                                                structural::DofComponent::Uz)})
                    .has_value());
        CHECK(part.currencyOf(first.result) == structural::ResultCurrency::Stale);
        CHECK(describesTheModel(part.meshCurrency()));
    }

    SECTION("removing a restraint") {
        REQUIRE(part.run<RemoveStructuralRestraintCommand>(part.analysis,
                                                           RestraintId::fromValue(1))
                    .has_value());
        CHECK(part.currencyOf(first.result) == structural::ResultCurrency::Stale);
        CHECK(describesTheModel(part.meshCurrency()));
    }

    SECTION("a solver-settings edit") {
        // A TOLERANCE IS NOT A MESHING QUESTION, and it is not a geometry
        // question either -- but it IS a question about the published numbers,
        // so the old result is no longer the current answer.
        SolverSettings tightened;
        tightened.relativeResidualTolerance = 1.0e-11;
        REQUIRE(part.run<SetSolverSettingsCommand>(part.analysis, tightened).has_value());
        CHECK(part.currencyOf(first.result) == structural::ResultCurrency::Stale);
        CHECK(describesTheModel(part.meshCurrency()));
    }
}

TEST_CASE("StructuralCommand_AFailedCommandLeavesACurrentResultCurrent",
          "[structural][command][solve]") {
    // MANDATORY, AND THE OPPOSITE DIRECTION FROM EVERY CASE ABOVE. An invalid
    // edit must not cost the user a solve: nothing changed, so nothing is
    // stale. A "stale on attempt" implementation would pass every invalidation
    // case above and fail this one.
    SolvablePart part;
    const structural::StructuralSolveOutcome first = part.solve();
    REQUIRE(part.currencyOf(first.result) == structural::ResultCurrency::Current);
    const std::size_t depth = part.history.undoCount();

    // One of each shape of refusal: a duplicate id, a missing target, a
    // non-finite value and unusable settings.
    REQUIRE_FALSE(
        part.run<AddStructuralLoadCommand>(part.analysis, pressureAt(part.startCap(), 1U, -1.0))
            .has_value());
    REQUIRE_FALSE(part.run<RemoveStructuralLoadCommand>(part.analysis, LoadId::fromValue(99))
                      .has_value());
    REQUIRE_FALSE(part.run<AddStructuralLoadCommand>(
                          part.analysis,
                          pressureAt(part.endCap(), 3U,
                                     std::numeric_limits<double>::quiet_NaN()))
                      .has_value());
    SolverSettings broken;
    broken.pivotFloor = -1.0;
    REQUIRE_FALSE(part.run<SetSolverSettingsCommand>(part.analysis, broken).has_value());

    // STILL CURRENT, and no history entry for any of the four.
    CHECK(part.currencyOf(first.result) == structural::ResultCurrency::Current);
    CHECK(describesTheModel(part.meshCurrency()));
    CHECK(part.history.undoCount() == depth);
}

TEST_CASE("StructuralCommand_UndoDoesNotResurrectTheOldResult",
          "[structural][command][solve]") {
    // THE ADVERSARIAL SCENARIO THE CHECKLIST SINGLES OUT. Solve, edit, undo --
    // and the first result must NOT become current again merely because the
    // canonical values returned to what they were.
    //
    // WHY IT CANNOT: revisions are monotonic. The undo restores the content and
    // takes a NEW revision, so the result's recorded source no longer matches
    // however the numbers happen to compare. That is a property of the existing
    // model, and this case is what proves P17-CMD did not undermine it.
    SolvablePart part;
    const structural::StructuralSolveOutcome first = part.solve();
    REQUIRE(part.currencyOf(first.result) == structural::ResultCurrency::Current);

    REQUIRE(part.run<EditStructuralLoadCommand>(part.analysis,
                                                pressureAt(part.endCap(), 1U, -2.0e6))
                .has_value());
    REQUIRE(part.currencyOf(first.result) == structural::ResultCurrency::Stale);

    REQUIRE(part.history.undo(part.document).has_value());

    // THE CONTENT IS BACK, bit for bit...
    const structural::PressureLoad* restored = part.definition().loads.front().pressure();
    REQUIRE(restored != nullptr);
    CHECK(restored->magnitude.si() == -1.0e6);
    // ...AND THE OLD RESULT IS STILL NOT CURRENT.
    CHECK(part.currencyOf(first.result) == structural::ResultCurrency::Stale);

    SECTION("and a fresh solve is current, computed from the intent rather than from history") {
        const structural::StructuralSolveOutcome again = part.solve();
        CHECK(part.currencyOf(again.result) == structural::ResultCurrency::Current);
        // THE FIRST RESULT IS STILL STALE. Nothing in the history restored its
        // arrays, and the new solve did not revive them -- it recomputed.
        CHECK(part.currencyOf(first.result) == structural::ResultCurrency::Stale);
    }
}

TEST_CASE("StructuralCommand_ANoOpEditDoesNotStaleACurrentResult",
          "[structural][command][solve]") {
    // THE OTHER HALF OF THE NO-OP POLICY. A no-op edit moves no revision, so a
    // current result stays current -- the canonical physics did not change, and
    // charging the user a re-solve for assigning a value its existing value
    // would be wrong.
    SolvablePart part;
    const structural::StructuralSolveOutcome first = part.solve();
    REQUIRE(part.currencyOf(first.result) == structural::ResultCurrency::Current);

    // Assign the settings their current value, and the load its current value.
    auto settings = std::make_unique<SetSolverSettingsCommand>(part.analysis,
                                                               part.definition().solver);
    const SetSolverSettingsCommand* raw = settings.get();
    REQUIRE(part.history.execute(part.document, std::move(settings)).has_value());
    CHECK_FALSE(raw->changedAnything());

    REQUIRE(part.run<EditStructuralLoadCommand>(part.analysis,
                                                pressureAt(part.endCap(), 1U, -1.0e6))
                .has_value());

    CHECK(part.currencyOf(first.result) == structural::ResultCurrency::Current);
    CHECK(describesTheModel(part.meshCurrency()));
}
