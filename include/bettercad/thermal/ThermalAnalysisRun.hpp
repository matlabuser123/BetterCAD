#pragma once

// P18-CLI-001 -- the production entry that runs one thermal analysis from canonical intent to a judged result.
//
// WHY THIS FILE EXISTS. Every earlier P18 milestone owns one link of a chain:
//
// ```text
//     requireThermalModel            P18-ARCH-001   the proof the inputs exist
//     buildThermalDofMap             P18-DOF-001    the numbering
//     prepareThermalLoads            P18-LOAD-001   the loads on this mesh
//     prepareThermalBoundary         P18-BC-001     the conditions on this mesh
//     assembleThermalSystem          P18-ASSEMBLY-001   K, C, Q
//     solveSteadyThermal             P18-SOLVE-001      T
//     solveTransientThermal          P18-TRANSIENT-001  T(t)
//     recoverSteadyThermal           P18-POST-001   grad(T), q, |q|
//     recoverSteadyBalance           P18-BALANCE-001    the heat accounting
//     validateThermalAnalysis        P18-VALID-001  the one decision
// ```
//
// and until this milestone the ORDER those links run in existed in exactly one place: the test support of the viewer
// (`tests/renderer/ThermalVizTestSupport.hpp`, `derive`). A headless command line that wanted a result would have had to
// write the chain again, and a chain written twice is two answers to "what does a thermal solve consist of". P17 has had its
// equivalent since P17-VALID-001 (`structural::solveStructuralAnalysis`); this is P18's.
//
// IT ADDS NO PHYSICS AND NO POLICY. It calls each link exactly as the chain above lists them, in that order, with the arguments
// the earlier milestones' own seams take, and it hands whatever exists to `validateThermalAnalysis`, which alone decides. There
// is no tolerance here, no threshold, no formula, no default, no retry and no fallback; a link that refuses stops the chain,
// and what had been built until then is kept so that the report can say which rule the refusal falls under.
//
// IT NEVER MESHES. Like `requireThermalModel` and `solveStructuralAnalysis` it reads the mesh the caller's `Mesher` holds and
// never calls `Mesher::generate`: meshing is an explicit request, and a solve that silently remeshed would hide both the cost
// and the intent change behind it. A stale mesh is therefore REPORTED (TV_MESH_CURRENT), never repaired.
//
// IT NEVER WRITES. Every argument is a const reference; nothing is added to the document, the regenerator or the mesher. A
// result is derived state (ADR-049) and lives exactly as long as the `ThermalAnalysisRun` that holds it.
//
// THE RUN IS A COMPLETE ACCOUNT, NOT A RESULT-OR-ERROR. A refusal at the third link leaves three links built and five unbuilt,
// and the validation report must be produced from precisely that: a caller that only got an error would have to guess which
// rule to blame. So the run carries the derived objects that exist (each an `std::optional`, so "not built" is not a
// default-constructed object), the stage that stopped it, the refusal as the link reported it, and the P18-VALID report.
//
// WHAT "ACCEPTED" MEANS IS P18-VALID'S AND NOT THIS FILE'S. `accepted()` below is `!stoppedAt && validation.accepted()`: the
// chain completed AND the validator, which re-judges the residual, the finiteness, the recovered fields and the heat balance
// independently of the links that produced them, accepts. A solver that returned success is not an input to it.

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/thermal/Export.hpp>
#include <bettercad/thermal/ThermalAnalysisObject.hpp>
#include <bettercad/thermal/ThermalBalance.hpp>
#include <bettercad/thermal/ThermalBoundary.hpp>
#include <bettercad/thermal/ThermalData.hpp>
#include <bettercad/thermal/ThermalDof.hpp>
#include <bettercad/thermal/ThermalLoadVector.hpp>
#include <bettercad/thermal/ThermalPost.hpp>
#include <bettercad/thermal/ThermalSolve.hpp>
#include <bettercad/thermal/ThermalSystem.hpp>
#include <bettercad/thermal/ThermalTransient.hpp>
#include <bettercad/thermal/ThermalValidation.hpp>

#include <cstdint>
#include <optional>
#include <string_view>

namespace bettercad {
class Document;
} // namespace bettercad

namespace bettercad::features {
class Regenerator;
} // namespace bettercad::features

namespace bettercad::thermal {

/// The link of the chain that refused. THE ENUMERATION ORDER IS THE EXECUTION ORDER.
enum class ThermalRunStage : std::uint8_t {
    /// The analysis does not exist, or `requireThermalModel` refused: the control, the geometry, the mesh or the conductivity.
    Model,
    DofMap,
    Loads,
    Boundary,
    /// Assembly, which is where a transient analysis on a material without a density or a specific heat is refused.
    System,
    /// The steady solve (including its anchoring check) or the transient integration.
    Solve,
    Post,
    Balance,
};

/// A stable name: `model`, `dof_map`, `loads`, `boundary`, `system`, `solve`, `post`, `balance`.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::string_view toString(ThermalRunStage stage) noexcept;

/// One analysis, run: what was built, where it stopped if it did, and the P18-VALID verdict on exactly that.
///
/// MOVE-ONLY BY CONTENT. The derived objects are values (no borrowed mesh view is kept: a `ThermalModel` is a snapshot that must
/// not outlive a remesh, so it lives only inside `runThermalAnalysis`), which is why the run stays valid for as long as its owner
/// keeps it, whatever the `Mesher` does afterwards. `validation` was computed before the run was returned and is not recomputed.
struct ThermalAnalysisRun {
    ThermalAnalysisId analysis{};
    ThermalAnalysisMode mode = ThermalAnalysisMode::Steady;

    /// The derived chain. A link that was not reached is nullopt, never an empty object.
    std::optional<ThermalDofMap> dofMap{};
    std::optional<PreparedThermalLoads> loads{};
    std::optional<PreparedThermalBoundary> boundary{};
    std::optional<ThermalSystem> system{};
    std::optional<SteadyThermalSolution> steady{};
    std::optional<TransientThermalSolution> transient{};
    std::optional<ThermalPostResult> post{};
    std::optional<ThermalBalanceResult> balance{};

    /// The first link that refused, and what it said. Both absent when the chain completed.
    std::optional<ThermalRunStage> stoppedAt{};
    std::optional<Error> failure{};

    /// P18-VALID's report on what exists, produced by `validateThermalAnalysis`. Always present: "this model is not ready, and
    /// here is why" is a report, not an absence of one.
    ThermalValidationReport validation{};

    /// The chain completed and the validator accepts the result. THE ONLY SUCCESS CRITERION: a warning-only report is accepted
    /// (`ThermalValidationReport::accepted`), a skipped rule is not.
    [[nodiscard]] bool accepted() const noexcept { return !stoppedAt.has_value() && validation.accepted(); }

    /// Views of what was built, in the shape `validateThermalAnalysis` takes. Valid for as long as this run is at its current
    /// address; moving the run invalidates a view taken before the move.
    [[nodiscard]] ThermalDerivedState derivedState() const noexcept {
        ThermalDerivedState state;
        state.dofMap = dofMap.has_value() ? &*dofMap : nullptr;
        state.preparedLoads = loads.has_value() ? &*loads : nullptr;
        state.preparedBoundary = boundary.has_value() ? &*boundary : nullptr;
        state.system = system.has_value() ? &*system : nullptr;
        state.steady = steady.has_value() ? &*steady : nullptr;
        state.transient = transient.has_value() ? &*transient : nullptr;
        state.post = post.has_value() ? &*post : nullptr;
        state.balance = balance.has_value() ? &*balance : nullptr;
        // The validator's `lastSolveFailure` is the failure of the SOLVE ATTEMPT. A refusal by the recovery or the balance is not one:
        // the solution exists, and the validator reports the missing field or balance under its own rule (TV_POST, TV_BALANCE) rather
        // than blaming the solver for what it did not do.
        state.lastSolveFailure =
            failure.has_value() && stoppedAt == ThermalRunStage::Solve ? &*failure : nullptr;
        return state;
    }
};

/// Runs the thermal analysis @p analysis against the mesh @p mesher holds for its control, and judges the outcome.
///
/// THE ORDER IS FROZEN (see the file comment) and each link is called with the canonical definition read from the document NOW:
/// the loads, conditions, initial temperature, transient settings and solver settings are the analysis's own, so there is no
/// parameter through which a caller could substitute a tolerance, an initial state or a time step. A steady analysis takes the
/// steady links, a transient one the transient links; the mode is the definition's.
///
/// ALWAYS RETURNS A RUN. It never throws for a refusal and never returns a half-built object as though it were a result:
/// `accepted()` is the question, `stoppedAt` and `failure` say why not.
///
/// NOT FOR A RESULT OF AN OLDER DOCUMENT. A run is made from the document it is given and is not "attached" to anything; the
/// currentness of a run held across an edit is `validateThermalAnalysis`'s (`isCurrent`), which compares the source the run was
/// computed from with the source of a result computed now.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT ThermalAnalysisRun
runThermalAnalysis(const Document& document, const features::Regenerator& regenerator,
                   const meshing::Mesher& mesher, ThermalAnalysisId analysis);

} // namespace bettercad::thermal
