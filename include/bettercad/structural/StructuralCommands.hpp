#pragma once

// Undoable structural-analysis edits (P17-CMD-001, ADR-047).
//
// THE HISTORY HOLDS CANONICAL ENGINEERING INTENT AND NOTHING ELSE. No assembled
// stiffness matrix, no load vector, no factorisation, no displacement array, no
// strain, no stress, no von Mises, no reaction, no constrained-DOF list, no
// prepared nodal force field, no render buffer. Every one of those is derived,
// and recomputes from the intent an undo restores -- which is why undoing a
// load edit gives the old load back without the old solve ever having been
// stored.
//
//     edit a load       ->  history stores a StructuralLoad
//     NOT               ->  history stores 850 nodal force vectors and a K
//
// THE CREATE COMMAND IS A THIN WRAPPER, AND DELIBERATELY SO. A
// StructuralAnalysis IS a DocumentObject, so AddObjectCommand already creates
// one correctly, including restoring the same ObjectId on redo and the whole
// object on undo. P16-CMD-001 established that and verified it by probe; this
// milestone reuses it rather than proving it again.
//
// EVERY EDIT CARRIES A BEFORE AND AN AFTER OF ONE DEFINITION. The alternative
// -- inverting each field on undo -- is a list of things to forget, and it
// grows every time the definition gains a field. It also invites drift: a
// magnitude undone by `-= delta` and redone by `+= delta` is not exactly
// reversible in floating point, while ASSIGNING A STORED DEFINITION is. A
// StructuralAnalysisDefinition is a control handle, a mode, two short vectors
// of canonical records and the solver settings, so a pair of them is
// proportional to the number of LOADS AND RESTRAINTS and not to the size of
// any mesh or any result computed from them.
//
// WHAT A COMMAND MAY NAME. AnalysisId, LoadId, RestraintId, FaceName,
// StructuralLoad, StructuralRestraint, RestraintComponents, SolverSettings.
// What it may not: DofIndex, FreeEquationIndex, a boundary facet handle, a
// ConstraintSet, a PreparedStructuralLoads, a GlobalStructuralSystem, a
// StructuralSolution, a StructuralPostResult or a StructuralReactionResult.
// The compile-fail group proves those cannot be constructed rather than merely
// are not (tests/compile_fail/StructuralCommandMisuse.cpp).
//
// ONE DECLARED EXCEPTION, AND IT IS QUALIFIED INTENT RATHER THAN A LEAK. A
// `NodalForceLoad` names a `NodeId`. P17-LOAD-001 made that choice explicitly:
// the payload carries the `MeshStamp` the handle belongs to, is refused against
// any other mesh "including one with the same node count and the same numeric
// handles", and exists for fixtures -- an element test, a solver check, a
// headless numerical case. A command that adds one therefore carries that
// NodeId, as part of a canonical record that declares its own mesh binding.
// Forbidding it would leave a qualified canonical load form with no undoable
// editing path. A restraint has no such form: it is a RestraintId, a FaceName
// and a component mask, and a NodeId cannot target one.
//
// RESULT INVALIDATION IS NOT IMPLEMENTED HERE, because it already exists. An
// edit moves the analysis object's revision, and a result computed under the
// old revision is already not current through the one `analysisRevision` that
// `StructuralResultSource` carries. There is no flag, no notification and no
// second invalidation path in this file. A NO-OP edit succeeds, moves no
// revision and so invalidates nothing -- the contract
// `StructuralAnalysis::setDefinition` already states.

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/Command.hpp>
#include <bettercad/core/document/Commands.hpp>
#include <bettercad/structural/Export.hpp>
#include <bettercad/structural/StructuralAnalysisObject.hpp>

#include <optional>
#include <string>

namespace bettercad::structural {

/// Creates a structural analysis; redo recreates it with the same AnalysisId.
///
/// The ID is part of the engineering intent rather than an implementation
/// detail: a result records which analysis it came from, and a redo that
/// minted a new one would leave every reference to the original dangling.
class BETTERCAD_STRUCTURAL_EXPORT CreateStructuralAnalysisCommand final : public Command {
public:
    /// @param name        the object name: a valid identifier, unique in the
    ///                    document. A duplicate is refused rather than
    ///                    adjusted.
    /// @param definition  the canonical intent, validated on execute.
    CreateStructuralAnalysisCommand(std::string name, StructuralAnalysisDefinition definition);

    [[nodiscard]] std::string description() const override;
    [[nodiscard]] Result<void> execute(Document& document) override;
    [[nodiscard]] Result<void> undo(Document& document) override;
    [[nodiscard]] Result<void> redo(Document& document) override;

    /// The created analysis's ID; invalid before a successful execute().
    [[nodiscard]] AnalysisId analysisId() const noexcept;

private:
    std::string name_;
    StructuralAnalysisDefinition definition_;
    std::optional<AddObjectCommand> add_{};
};

/// Deletes a structural analysis; undo restores it with the same ID and name.
///
/// Its loads, restraints and settings go with it, because the definition owns
/// them by value -- so there is no orphaned load to leave behind, and undo
/// restores the whole analysis in one step.
class BETTERCAD_STRUCTURAL_EXPORT DeleteStructuralAnalysisCommand final : public Command {
public:
    explicit DeleteStructuralAnalysisCommand(AnalysisId analysis) noexcept;

    [[nodiscard]] std::string description() const override;
    [[nodiscard]] Result<void> execute(Document& document) override;
    [[nodiscard]] Result<void> undo(Document& document) override;
    [[nodiscard]] Result<void> redo(Document& document) override;

private:
    AnalysisId analysis_;
    std::optional<DeleteObjectCommand> remove_{};
};

/// The shape every edit of one analysis's definition takes.
///
/// WHY A BASE AND NOT SEVEN COPIES. Each edit differs only in how it derives
/// the new definition from the old one and in what it is called; capturing the
/// before-state, applying the after-state, undoing exactly and redoing exactly
/// are the same four lines every time, and the same four lines are where an
/// atomicity mistake would hide. Written once, they are testable once. The
/// same decision P16-CMD-001 made, for the same reason.
///
/// FAILURE IS ATOMIC BY CONSTRUCTION. `apply` goes through
/// `Document::modifyObject`, which validates through
/// `StructuralAnalysis::setDefinition` before assigning anything, and the
/// before-state is recorded only after a successful apply -- so a refused edit
/// leaves the document untouched and the command unexecuted, and
/// `CommandHistory` records only a command whose execute() succeeded.
///
/// UNDO AND REDO ASSIGN A STORED DEFINITION. Neither computes an inverse, so
/// repeating the pair cannot accumulate floating-point drift however many
/// times it is repeated.
class BETTERCAD_STRUCTURAL_EXPORT StructuralAnalysisEditCommand : public Command {
public:
    [[nodiscard]] Result<void> execute(Document& document) override;
    [[nodiscard]] Result<void> undo(Document& document) override;
    [[nodiscard]] Result<void> redo(Document& document) override;

    /// Whether the edit changed anything. Valid after a successful execute().
    ///
    /// A no-op edit SUCCEEDS and changes nothing: no revision bump, and so no
    /// result invalidation. It is still recorded by `CommandHistory`, which
    /// records every command whose execute() succeeded -- the same behaviour
    /// P15's material edit and P16's mesh-control edit have, and not something
    /// to special-case in one module.
    [[nodiscard]] bool changedAnything() const noexcept { return changed_; }

protected:
    explicit StructuralAnalysisEditCommand(AnalysisId analysis) noexcept : analysis_(analysis) {}

    /// The new definition, derived from @p before, or a refusal.
    ///
    /// Refusals here are the command's own preconditions -- "there is already a
    /// load with that id", "there is no restraint with that id". Whether the
    /// RESULT is valid is not checked here: `StructuralAnalysis::setDefinition`
    /// delegates that to `validate`, which owns the rules.
    [[nodiscard]] virtual Result<StructuralAnalysisDefinition> edit(
        const StructuralAnalysisDefinition& before) const = 0;

    AnalysisId analysis_;

private:
    std::optional<StructuralAnalysisDefinition> before_{};
    std::optional<StructuralAnalysisDefinition> after_{};
    bool changed_ = false;
};

/// Replaces an analysis's whole definition.
///
/// One user-visible action, one undo step, with the before and after of one
/// value. Used where a dialog edits several fields at once, which the framework
/// has no compound-command type for.
class BETTERCAD_STRUCTURAL_EXPORT SetStructuralAnalysisDefinitionCommand final
    : public StructuralAnalysisEditCommand {
public:
    SetStructuralAnalysisDefinitionCommand(AnalysisId analysis,
                                           StructuralAnalysisDefinition definition);

    [[nodiscard]] std::string description() const override;

protected:
    [[nodiscard]] Result<StructuralAnalysisDefinition> edit(
        const StructuralAnalysisDefinition& before) const override;

private:
    StructuralAnalysisDefinition definition_;
};

// ---------------------------------------------------------------------------
// Loads
// ---------------------------------------------------------------------------

/// Appends a load.
///
/// APPENDED, BECAUSE THE ORDER IS THE USER'S. The definition keeps its loads in
/// the order they were given -- it does not affect the physical result, since
/// linear statics superposes and the nodal field accumulates into an ascending
/// map, but reordering a user's list is not this layer's to do.
///
/// Refuses a load whose `LoadId` the analysis already holds, and refuses a
/// malformed record through `validate(StructuralLoad)`, which checks the
/// identity and the finiteness of the value without consulting a mesh.
class BETTERCAD_STRUCTURAL_EXPORT AddStructuralLoadCommand final
    : public StructuralAnalysisEditCommand {
public:
    AddStructuralLoadCommand(AnalysisId analysis, StructuralLoad load);

    [[nodiscard]] std::string description() const override;

protected:
    [[nodiscard]] Result<StructuralAnalysisDefinition> edit(
        const StructuralAnalysisDefinition& before) const override;

private:
    StructuralLoad load_;
};

/// Replaces the load with @p replacement's id, in place.
///
/// IN PLACE, SO THE USER'S ORDER SURVIVES AN EDIT. The replacement keeps the
/// position the original held, which is what makes an edit distinguishable from
/// a remove followed by an add.
///
/// THE WHOLE RECORD IS REPLACED, not field by field. A multi-field edit that
/// validated the last field after assigning the first could leave one field
/// changed and the rest not; replacing a validated whole cannot.
class BETTERCAD_STRUCTURAL_EXPORT EditStructuralLoadCommand final
    : public StructuralAnalysisEditCommand {
public:
    EditStructuralLoadCommand(AnalysisId analysis, StructuralLoad replacement);

    [[nodiscard]] std::string description() const override;

protected:
    [[nodiscard]] Result<StructuralAnalysisDefinition> edit(
        const StructuralAnalysisDefinition& before) const override;

private:
    StructuralLoad replacement_;
};

/// Removes the load with @p load.
///
/// Undo restores it at the position it held, with the same `LoadId` and the
/// same canonical values -- the whole definition is assigned back, so position
/// and identity come with it rather than being reconstructed.
class BETTERCAD_STRUCTURAL_EXPORT RemoveStructuralLoadCommand final
    : public StructuralAnalysisEditCommand {
public:
    RemoveStructuralLoadCommand(AnalysisId analysis, LoadId load) noexcept;

    [[nodiscard]] std::string description() const override;

protected:
    [[nodiscard]] Result<StructuralAnalysisDefinition> edit(
        const StructuralAnalysisDefinition& before) const override;

private:
    LoadId load_;
};

// ---------------------------------------------------------------------------
// Restraints
// ---------------------------------------------------------------------------

/// Appends a restraint.
///
/// Refuses a duplicate `RestraintId`, and refuses a malformed record through
/// `validate(StructuralRestraint)` -- which refuses an empty component mask,
/// among other things, and deliberately does not consult a mesh.
class BETTERCAD_STRUCTURAL_EXPORT AddStructuralRestraintCommand final
    : public StructuralAnalysisEditCommand {
public:
    AddStructuralRestraintCommand(AnalysisId analysis, StructuralRestraint restraint);

    [[nodiscard]] std::string description() const override;

protected:
    [[nodiscard]] Result<StructuralAnalysisDefinition> edit(
        const StructuralAnalysisDefinition& before) const override;

private:
    StructuralRestraint restraint_;
};

/// Replaces the restraint with @p replacement's id, in place.
///
/// Both the target face and the component mask are canonical, so both are
/// edited through this one command and both survive undo and redo exactly.
/// There is no geometry rebinding and no nearby-face fallback: a `FaceName` is
/// restored as it was stored.
class BETTERCAD_STRUCTURAL_EXPORT EditStructuralRestraintCommand final
    : public StructuralAnalysisEditCommand {
public:
    EditStructuralRestraintCommand(AnalysisId analysis, StructuralRestraint replacement);

    [[nodiscard]] std::string description() const override;

protected:
    [[nodiscard]] Result<StructuralAnalysisDefinition> edit(
        const StructuralAnalysisDefinition& before) const override;

private:
    StructuralRestraint replacement_;
};

/// Removes the restraint with @p restraint.
class BETTERCAD_STRUCTURAL_EXPORT RemoveStructuralRestraintCommand final
    : public StructuralAnalysisEditCommand {
public:
    RemoveStructuralRestraintCommand(AnalysisId analysis, RestraintId restraint) noexcept;

    [[nodiscard]] std::string description() const override;

protected:
    [[nodiscard]] Result<StructuralAnalysisDefinition> edit(
        const StructuralAnalysisDefinition& before) const override;

private:
    RestraintId restraint_;
};

// ---------------------------------------------------------------------------
// Solver settings
// ---------------------------------------------------------------------------

/// Sets the analysis's solver settings.
///
/// CANONICAL SINCE ADR-047. Before that the settings were an argument to the
/// solve, and settings that live outside the document cannot be undone, redone
/// or persisted -- so this command would have had nothing to mutate.
///
/// Refused through `validate(SolverSettings)`, which owns what a recognised
/// algorithm and a usable tolerance are: a NaN or non-positive tolerance is
/// rejected there rather than left to decide something inside a comparison.
///
/// STALES THE RESULT AND NOT THE MESH. A tolerance is not a meshing question.
class BETTERCAD_STRUCTURAL_EXPORT SetSolverSettingsCommand final
    : public StructuralAnalysisEditCommand {
public:
    SetSolverSettingsCommand(AnalysisId analysis, SolverSettings settings) noexcept;

    [[nodiscard]] std::string description() const override;

protected:
    [[nodiscard]] Result<StructuralAnalysisDefinition> edit(
        const StructuralAnalysisDefinition& before) const override;

private:
    SolverSettings settings_;
};

} // namespace bettercad::structural
