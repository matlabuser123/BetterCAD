#pragma once

// Undoable thermal-analysis edits (P18-CMD-001, ADR-055).
//
// THE HISTORY HOLDS CANONICAL THERMAL INTENT AND NOTHING ELSE. No conductivity
// matrix K, no capacity matrix C, no heat vector Q, no factorisation, no DOF map,
// no temperature array, no transient history, no gradient, no heat flux, no
// reaction, no heat balance, no validation report, no prepared load or boundary
// mapping, no render buffer. Every one of those is derived and recomputes from the
// intent an undo restores -- which is why undoing a load edit gives the old load
// back without the old solve ever having been stored.
//
//     edit a load       ->  history stores a ThermalLoad (an id, a value, a FaceName)
//     NOT               ->  history stores 850 nodal heat contributions and a K
//
// THIS MILESTONE ADDS NO COMMAND FRAMEWORK, NO INVALIDATION MECHANISM AND NO
// IDENTITY TYPE. It adds ten commands on the base P15, P16 and P17 already settled
// on, because the audit (docs/verification/P18-CMD-001/COMMAND_MODEL.md) found the
// rest already there: a `ThermalAnalysis` IS a `DocumentObject`, so
// `AddObjectCommand` creates one and restores the same `ObjectId` on redo;
// `Document::modifyObject` validates through `ThermalAnalysis::setDefinition`
// BEFORE assigning; `setDefinition` returns false for an effective no-op and moves
// no revision; and `CommandHistory` records only a command whose execute()
// succeeded and clears redo only then.
//
// EVERY EDIT CARRIES A BEFORE AND AN AFTER OF ONE DEFINITION. The alternative --
// inverting each field on undo -- is a list of things to forget, and it invites
// drift: a flux undone by `-= delta` and redone by `+= delta` is not exactly
// reversible in floating point, while ASSIGNING A STORED DEFINITION is. A
// `ThermalAnalysisDefinition` is a control handle, a mode, two short vectors of
// canonical records, at most one initial temperature, two solver ratios and at
// most one transient setting, so a pair of them is proportional to the number of
// LOADS AND CONDITIONS and not to the size of any mesh or any result.
//
// WHAT A COMMAND MAY NAME. ThermalAnalysisId, ThermalLoadId,
// ThermalBoundaryConditionId, FaceName (the canonical GeometryReference of a
// thermal load or condition), ThermalLoad, ThermalBoundaryCondition,
// UniformInitialTemperature, ThermalSolverSettings, TransientSettings. What it
// may not: a NodeId, an ElementId, a ThermalDofIndex, a facet handle, a
// PreparedThermalLoads, a ThermalSystem, a SteadyThermalSolution, a
// TransientThermalSolution, a ThermalPostResult, a ThermalBalanceResult or a
// ThermalValidationReport. The compile-fail group proves those cannot be
// constructed into a command rather than merely are not
// (tests/compile_fail/ThermalCommandMisuse.cpp), and the architecture gate
// (CheckThermalCommandOwnership.cmake) proves this module's own source reaches none
// of them.
//
// A CANONICAL TARGET IS A FACENAME, NEVER A MESH HANDLE. The face is stored as it
// was given and is resolved afresh against whatever mesh is current. A remesh
// leaves every command's payload valid; a face that no longer resolves is
// reported by P18-LOAD / P18-BC / P18-VALID, not repaired here. There is no
// nearest-face fallback and no resolved set in any payload.
//
// RESULT INVALIDATION IS NOT IMPLEMENTED HERE, because it already exists. An edit
// goes through `Document::modifyObject`, which moves the analysis object's
// revision when something actually changed, and every derived P18 object records
// the canonical intent it was computed from and is compared with the intent the
// document holds now. There is no flag, no notification and no second
// invalidation path in this file, and no command solves, remeshes, assembles,
// recovers, balances or validates.

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/Command.hpp>
#include <bettercad/core/document/Commands.hpp>
#include <bettercad/thermal/Export.hpp>
#include <bettercad/thermal/ThermalAnalysisObject.hpp>

#include <optional>
#include <string>

namespace bettercad::thermal {

/// Creates a thermal analysis; redo recreates it with the same ThermalAnalysisId.
///
/// The ID is part of the engineering intent rather than an implementation detail:
/// a result records which analysis it came from, and a redo that minted a new one
/// would leave every reference to the original dangling. The command establishes
/// INTENT ONLY -- no mesh is generated, no numbering built, nothing solved.
class BETTERCAD_THERMAL_EXPORT CreateThermalAnalysisCommand final : public Command {
public:
    /// @param name        the object name: a valid identifier, unique in the
    ///                    document. A duplicate is refused rather than adjusted.
    /// @param definition  the canonical intent, validated on execute. Nothing is
    ///                    defaulted here: the defaults are the data model's.
    CreateThermalAnalysisCommand(std::string name, ThermalAnalysisDefinition definition);

    [[nodiscard]] std::string description() const override;
    [[nodiscard]] Result<void> execute(Document& document) override;
    [[nodiscard]] Result<void> undo(Document& document) override;
    [[nodiscard]] Result<void> redo(Document& document) override;

    /// The created analysis's ID; invalid before a successful execute().
    [[nodiscard]] ThermalAnalysisId analysisId() const noexcept;

private:
    std::string name_;
    ThermalAnalysisDefinition definition_;
    std::optional<AddObjectCommand> add_{};
};

/// The shape every edit of one analysis's definition takes.
///
/// WHY A BASE AND NOT NINE COPIES. Each edit differs only in how it derives the new
/// definition from the old one and in what it is called; capturing the
/// before-state, applying the after-state, undoing exactly and redoing exactly are
/// the same few lines every time, and those lines are where an atomicity mistake
/// would hide. Written once, they are testable once -- the decision P16-CMD-001
/// and P17-CMD-001 made, for the same reason.
///
/// FAILURE IS ATOMIC BY CONSTRUCTION. `apply` goes through `Document::modifyObject`,
/// which validates through `ThermalAnalysis::setDefinition` before assigning
/// anything, and the before-state is recorded only after a successful apply -- so a
/// refused edit leaves the document untouched and the command unexecuted, and
/// `CommandHistory` records only a command whose execute() succeeded. A refused
/// edit therefore moves no revision, stales no result, pushes no history entry and
/// clears no redo.
///
/// UNDO AND REDO ASSIGN A STORED DEFINITION. Neither computes an inverse, so
/// repeating the pair cannot accumulate floating-point drift however many times it
/// is repeated, and no value is ever round-tripped through text.
///
/// THE STACK DISCIPLINE IS CHECKED, NOT ASSUMED. `CommandHistory` undoes in reverse
/// order, so when `undo` runs the analysis holds exactly the definition this command
/// installed (and exactly the one it replaced when `redo` runs). If it does not --
/// something edited the analysis outside the history -- assigning the stored
/// definition would silently discard that change, so the command REFUSES
/// (`FailedPrecondition`), leaving the document, its own state and the stacks as
/// they were. A missing analysis is `NotFound` on the same terms.
class BETTERCAD_THERMAL_EXPORT ThermalAnalysisEditCommand : public Command {
public:
    [[nodiscard]] Result<void> execute(Document& document) override;
    [[nodiscard]] Result<void> undo(Document& document) override;
    [[nodiscard]] Result<void> redo(Document& document) override;

    /// Whether the edit changed anything. Valid after a successful execute().
    ///
    /// A no-op edit SUCCEEDS and changes nothing: no revision bump, and so no
    /// result invalidation. It is still recorded by `CommandHistory`, which
    /// records every command whose execute() succeeded -- the same behaviour
    /// P15's material edit, P16's mesh-control edit and P17's analysis edit have,
    /// and not something to special-case in one module.
    [[nodiscard]] bool changedAnything() const noexcept { return changed_; }

    /// The canonical definition this command replaced, and the one it installed.
    /// Empty until a successful execute(). These ARE the command's whole payload:
    /// two canonical definitions and nothing derived, which is what the history
    /// size evidence reads.
    [[nodiscard]] const std::optional<ThermalAnalysisDefinition>& before() const noexcept {
        return before_;
    }
    [[nodiscard]] const std::optional<ThermalAnalysisDefinition>& after() const noexcept {
        return after_;
    }

    /// The analysis this command edits.
    [[nodiscard]] ThermalAnalysisId analysis() const noexcept { return analysis_; }

protected:
    explicit ThermalAnalysisEditCommand(ThermalAnalysisId analysis) noexcept : analysis_(analysis) {}

    /// The new definition, derived from @p before, or a refusal.
    ///
    /// Refusals here are the command's own preconditions -- "there is already a
    /// load with that id", "there is no condition with that id", "a steady
    /// analysis has no initial temperature". Whether the RESULT is a valid
    /// definition is not decided here: `ThermalAnalysis::setDefinition` delegates
    /// that to `validate`, which owns the rules.
    [[nodiscard]] virtual Result<ThermalAnalysisDefinition>
    edit(const ThermalAnalysisDefinition& before) const = 0;

    ThermalAnalysisId analysis_;

private:
    std::optional<ThermalAnalysisDefinition> before_{};
    std::optional<ThermalAnalysisDefinition> after_{};
    bool changed_ = false;
};

// ---------------------------------------------------------------------------
// Loads
// ---------------------------------------------------------------------------

/// Appends a load.
///
/// APPENDED, BECAUSE THE ORDER IS THE USER'S. The definition keeps its loads in the
/// order they were given and the order is canonical state (compared by
/// `operator==`, persisted as given); the loads superpose, so it does not affect
/// the physical result, but reordering a user's list is not this layer's to do.
///
/// Refuses a load whose `ThermalLoadId` the analysis already holds, and refuses a
/// malformed record through `validate(ThermalLoad)`, which checks the identity, the
/// face selector and the finiteness of the value WITHOUT consulting a mesh. It also
/// refuses a `FaceName` that names no feature (the invalid handle), which the data
/// model's validator lets through: such a face could only ever be reported unresolved,
/// and is not a canonical target representation. Whether
/// the face resolves is a question about a particular mesh and belongs to
/// preparation: a user may legitimately define a load before a mesh exists. A
/// NEGATIVE flux or generation is a sink and is accepted.
class BETTERCAD_THERMAL_EXPORT AddThermalLoadCommand final : public ThermalAnalysisEditCommand {
public:
    AddThermalLoadCommand(ThermalAnalysisId analysis, ThermalLoad load);

    [[nodiscard]] std::string description() const override;

protected:
    [[nodiscard]] Result<ThermalAnalysisDefinition>
    edit(const ThermalAnalysisDefinition& before) const override;

private:
    ThermalLoad load_;
};

/// Replaces the load with @p replacement's id, in place.
///
/// IN PLACE, SO THE USER'S ORDER SURVIVES AN EDIT, and by the SAME ID: an edit is
/// not a remove followed by an add. The WHOLE record is replaced, not field by
/// field, so a multi-field edit (a new flux and a new face) cannot leave one field
/// changed and the rest not -- and may change the kind of load, because the
/// payload is a validated whole.
class BETTERCAD_THERMAL_EXPORT EditThermalLoadCommand final : public ThermalAnalysisEditCommand {
public:
    EditThermalLoadCommand(ThermalAnalysisId analysis, ThermalLoad replacement);

    [[nodiscard]] std::string description() const override;

protected:
    [[nodiscard]] Result<ThermalAnalysisDefinition>
    edit(const ThermalAnalysisDefinition& before) const override;

private:
    ThermalLoad replacement_;
};

/// Removes the load with @p load.
///
/// Undo restores it at the position it held, with the same `ThermalLoadId` and the
/// same canonical values -- the whole definition is assigned back, so position and
/// identity come with it rather than being reconstructed.
class BETTERCAD_THERMAL_EXPORT RemoveThermalLoadCommand final : public ThermalAnalysisEditCommand {
public:
    RemoveThermalLoadCommand(ThermalAnalysisId analysis, ThermalLoadId load) noexcept;

    [[nodiscard]] std::string description() const override;

protected:
    [[nodiscard]] Result<ThermalAnalysisDefinition>
    edit(const ThermalAnalysisDefinition& before) const override;

private:
    ThermalLoadId load_;
};

// ---------------------------------------------------------------------------
// Boundary conditions
// ---------------------------------------------------------------------------

/// Appends a boundary condition (a prescribed temperature or a convection).
///
/// Refuses a duplicate `ThermalBoundaryConditionId` and a malformed record through
/// `validate(ThermalBoundaryCondition)`: a finite absolute temperature, and for
/// convection a finite POSITIVE coefficient. It does NOT invent a conflict policy:
/// two prescribed temperatures on one face are canonical intent this layer permits,
/// and P18-BC / P18-VALID own what an identical duplicate and a conflicting pair
/// mean. Nothing is silently overwritten.
class BETTERCAD_THERMAL_EXPORT AddThermalBoundaryConditionCommand final
    : public ThermalAnalysisEditCommand {
public:
    AddThermalBoundaryConditionCommand(ThermalAnalysisId analysis, ThermalBoundaryCondition condition);

    [[nodiscard]] std::string description() const override;

protected:
    [[nodiscard]] Result<ThermalAnalysisDefinition>
    edit(const ThermalAnalysisDefinition& before) const override;

private:
    ThermalBoundaryCondition condition_;
};

/// Replaces the condition with @p replacement's id, in place.
///
/// The face and every value are canonical, so all are edited through this one
/// command and all survive undo and redo exactly. There is no geometry rebinding
/// and no nearby-face fallback: a `FaceName` is restored as it was stored.
class BETTERCAD_THERMAL_EXPORT EditThermalBoundaryConditionCommand final
    : public ThermalAnalysisEditCommand {
public:
    EditThermalBoundaryConditionCommand(ThermalAnalysisId analysis, ThermalBoundaryCondition replacement);

    [[nodiscard]] std::string description() const override;

protected:
    [[nodiscard]] Result<ThermalAnalysisDefinition>
    edit(const ThermalAnalysisDefinition& before) const override;

private:
    ThermalBoundaryCondition replacement_;
};

/// Removes the condition with @p condition.
class BETTERCAD_THERMAL_EXPORT RemoveThermalBoundaryConditionCommand final
    : public ThermalAnalysisEditCommand {
public:
    RemoveThermalBoundaryConditionCommand(ThermalAnalysisId analysis,
                                          ThermalBoundaryConditionId condition) noexcept;

    [[nodiscard]] std::string description() const override;

protected:
    [[nodiscard]] Result<ThermalAnalysisDefinition>
    edit(const ThermalAnalysisDefinition& before) const override;

private:
    ThermalBoundaryConditionId condition_;
};

// ---------------------------------------------------------------------------
// Initial temperature, solver settings, transient settings
// ---------------------------------------------------------------------------

/// Sets the uniform initial temperature of a TRANSIENT analysis.
///
/// ONE VALUE IN KELVIN, and not a nodal vector: a per-node initial field would be
/// mesh-local state inside canonical intent, and a field from a previous result
/// would be derived state promoted to authority (ADR-049). The derived initial
/// vector is rebuilt by P18-TRANSIENT from this value and the current numbering.
///
/// REFUSED FOR A STEADY ANALYSIS, because the data model is mode-complete: a steady
/// definition carries no initial condition, since an inert field in canonical
/// intent is one whose edit would stale a result for nothing. Switching mode is a
/// whole-definition edit that this milestone does not add (it is not on the
/// checklist, and the data model already makes it one edit).
class BETTERCAD_THERMAL_EXPORT SetInitialTemperatureCommand final : public ThermalAnalysisEditCommand {
public:
    SetInitialTemperatureCommand(ThermalAnalysisId analysis, UniformInitialTemperature initial) noexcept;

    [[nodiscard]] std::string description() const override;

protected:
    [[nodiscard]] Result<ThermalAnalysisDefinition>
    edit(const ThermalAnalysisDefinition& before) const override;

private:
    UniformInitialTemperature initial_;
};

/// Sets the analysis's solver settings (two ratios; steady and transient alike).
///
/// Refused through `validate(ThermalSolverSettings)`, which owns what a usable
/// tolerance is: a NaN or non-positive ratio is rejected there rather than left to
/// decide something inside a comparison. STALES THE RESULT AND NOT THE MESH: a
/// tolerance is not a meshing question.
class BETTERCAD_THERMAL_EXPORT SetThermalSolverSettingsCommand final
    : public ThermalAnalysisEditCommand {
public:
    SetThermalSolverSettingsCommand(ThermalAnalysisId analysis, ThermalSolverSettings settings) noexcept;

    [[nodiscard]] std::string description() const override;

protected:
    [[nodiscard]] Result<ThermalAnalysisDefinition>
    edit(const ThermalAnalysisDefinition& before) const override;

private:
    ThermalSolverSettings settings_;
};

/// Sets the time discretisation of a TRANSIENT analysis: step, end time and output
/// policy, as one validated record.
///
/// THE CANONICAL INTENT IS THE THREE FIELDS, NOT THE GRID. The step sequence a run
/// actually takes (including its shortened last step) is derived by P18-TRANSIENT
/// and is never stored here, and there is no output-time vector in the data model
/// to store. All three fields apply together or not at all.
///
/// REFUSED FOR A STEADY ANALYSIS, for the reason `SetInitialTemperatureCommand`
/// gives.
class BETTERCAD_THERMAL_EXPORT SetTransientSettingsCommand final : public ThermalAnalysisEditCommand {
public:
    SetTransientSettingsCommand(ThermalAnalysisId analysis, TransientSettings settings) noexcept;

    [[nodiscard]] std::string description() const override;

protected:
    [[nodiscard]] Result<ThermalAnalysisDefinition>
    edit(const ThermalAnalysisDefinition& before) const override;

private:
    TransientSettings settings_;
};

} // namespace bettercad::thermal
