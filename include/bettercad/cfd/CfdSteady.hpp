#pragma once

// P19-STEADY-001 -- the steady incompressible solve: the outer loop, the
// convergence verdict, the mapping of the coupling's refusals onto the result's
// failure stages, and the atomic publication of a result. ADR-077.
//
// THIS MILESTONE ORCHESTRATES AND IT DOES NOT COMPUTE. Ten of its nineteen
// checklist items name work that was already qualified, and the UNDERSTAND phase
// found each one with a producer:
//
// ```text
// Momentum predictor      advanceCfdCoupling step 1       P19-PVCOUPLE-001
// Pressure correction     advanceCfdCoupling steps 3-4
// Velocity correction     advanceCfdCoupling step 5
// Flux correction         advanceCfdCoupling step 5
// Residual computation    advanceCfdCoupling step 7, ADR-058's CfdResiduals
// Mass-balance            advanceCfdCoupling step 6, evaluateCfdContinuity
//                                                         P19-CONT-001
// Convergence loop        ours, consulting cfdCouplingVerdict
// Maximum-iteration       ours, consulting cfdCouplingBudgetExhausted
// Divergence detection    cfdCouplingVerdict reports Diverged
// Validate inputs         requireCfdModel .. prepareCfdInitialField
// ```
//
// `CfdCoupling.hpp` named the first six as not its own and these last three as
// ours, by milestone id: "NO LOOP. One iteration, and the criteria a loop would
// consult are `cfdCouplingVerdict`. 'Convergence loop' is `P19-STEADY-001`'s
// item." Re-deriving any of them here would be the duplication that three ADRs
// chose this split to avoid, and `cfdCouplingVerdict` is documented as a pure
// function precisely so that this loop and the coupling's own tests agree by
// construction rather than by inspection.
//
// WHAT IS GENUINELY NEW IS THE LOOP, THE MAPPING AND THE PUBLICATION, plus
// one thing no earlier milestone could do: `CfdResult` now carries its fields,
// because `CfdResultSlot` had no producer in the tree until this one. ADR-077
// Decision 2 has the candidates and the argument.
//
// THE SOLVE IS SUPPLIED, NOT CHOSEN HERE. `cfdLinearSolve()` is the production
// port and `ADR-076` qualified it, but the parameter stays open so that the
// coupling suite's own dense Gaussian elimination -- the independent reference
// this milestone validates against -- is reachable from outside. A supplied
// solve that is itself non-deterministic makes the run non-deterministic, and
// nothing here can detect that (`ADR-075` L4, `ADR-076` L5, carried forward).
//
// IT DOES NOT JUDGE ITS OWN NUMBERS. `CfdResult.hpp`: "CONVERGENCE IS RECORDED,
// NOT RE-JUDGED HERE. Whether these numbers are good enough is validation's
// question (P19-VALID-001)." A run that converges here is not a validated one,
// and `P19-TRANSIENT-001` owns time stepping.

#include <bettercad/cfd/CfdAnalysisObject.hpp>
#include <bettercad/cfd/CfdCoupling.hpp>
#include <bettercad/cfd/CfdData.hpp>
#include <bettercad/cfd/CfdResult.hpp>
#include <bettercad/cfd/Export.hpp>
#include <bettercad/core/Error.hpp>

#include <cstdint>
#include <expected>

namespace bettercad::features {
class Regenerator;
} // namespace bettercad::features

namespace bettercad::cfd {

// THERE IS NO SETTINGS PARAMETER, AND THAT IS A CORRECTNESS REQUIREMENT RATHER
// THAN A SIMPLIFICATION (ADR-077 Decision 7).
//
// `CfdAnalysisDefinition` already holds `iteration`, `convergence`,
// `linearSolver` and `steady`, and `canonicalText` writes every one of them into
// the definition fingerprint: `iteration.velocity_relaxation`,
// `iteration.pressure_relaxation`,
// `convergence.momentum_residual_tolerance`,
// `convergence.continuity_residual_tolerance`,
// `convergence.divergence_factor`,
// `linear_solver.relative_residual_tolerance`, `steady.max_iterations`.
//
// `CfdResultSource::definition` IS that fingerprint. So a solve driven by
// caller-supplied settings would publish a result whose provenance names one set
// of settings while the numbers came from another, and `resultCurrency` would
// call it Current. The settings are read from the definition, which is where the
// Document owns them (ARCHITECTURE.md: "The Document owns canonical engineering
// state"), and a caller that wants different ones edits the analysis -- which
// moves the revision and the fingerprint, as it should.
//
// An earlier draft of this header took a `CfdSteadySolveInput` carrying all four.
// It was the "second parameter system" ARCHITECTURE.md rejects, and this file's
// own scope note had warned against exactly that one milestone earlier.

/// What an accepted run measured about itself, beyond what the published
/// `CfdResult` carries.
///
/// THE RESULT IS THE ANSWER AND THIS IS THE ACCOUNT OF GETTING THERE. The
/// fields, the provenance and `ADR-058`'s residual and mass-balance records are
/// on the result, where a reader looks for them. What is here is the run's own
/// history: the first iteration's residuals, which is the only thing the
/// divergence factor is relative to and which the result has no field for, and
/// the three measures that `CfdCouplingIterationSummary` reports but
/// `CfdResultMetadata` does not carry.
struct CfdSteadyReport {
    /// Completed outer iterations, at least 1 on an accepted run.
    std::uint32_t outerIterations = 0;
    /// The FIRST iteration's residuals, the divergence baseline.
    CfdResiduals firstResiduals{};
    /// The LAST iteration's residuals, the same record the result carries.
    CfdResiduals residuals{};
    /// Whether `CfdCouplingIteration::balance` held the evaluator's measures,
    /// which is false exactly in a closed domain -- there `ADR-058`'s two
    /// RELATIVE measures are undefined rather than small, and
    /// `CfdContinuity.hpp` says why.
    bool massBalanceDefined = false;
    /// Whether every boundary face is a wall, as the qualified continuity
    /// evaluator reports it.
    bool closedDomain = false;
    /// The scale the continuity residual is relative to, kg/s.
    double continuityScale = 0.0;
    /// The largest `|p'|` of the LAST iteration, Pa -- how far from converged
    /// the run still was, in a unit a reader can interpret.
    double largestPressureCorrection = 0.0;

    friend bool operator==(const CfdSteadyReport&, const CfdSteadyReport&) = default;
};

/// Which failure stage a coupling problem is, in this milestone's vocabulary.
///
/// `ADR-075` Decision 9 assigned this mapping here: "a caller owning the
/// vocabulary -- `P19-STEADY-001` maps these onto `CfdFailureStage` -- can say
/// it in its own words without parsing a message."
///
/// EXPOSED SO THAT THE TABLE IS TESTABLE DIRECTLY, and written as one `switch`
/// with no `default`, so a seventeenth `CfdCouplingProblem` upstream fails to
/// compile here rather than landing silently in a catch-all. Sixteen problems
/// onto nine stages is not injective and is not meant to be.
[[nodiscard]] BETTERCAD_CFD_EXPORT CfdFailureStage stageOf(CfdCouplingProblem problem) noexcept;

/// Runs SIMPLE to convergence and publishes the outcome into @p slot.
///
/// ```text
///  1  find the analysis, check its mode, read ADR-058's four settings out of
///     its definition and validate each one
///  2  requireCfdModel -> requireCfdMesh -> requireCfdTopology
///                     -> requireCfdGeometry -> requireCfdFieldMap
///  3  prepareCfdBoundary, prepareCfdInitialField
///  4  expand p and U into this call's own arrays        (ADR-068)
///  5  loop, while the budget allows:
///       a  cfdMassFlux from the current U               (P19-CONV-001)
///       b  reconstructCfdPressureGradient from the current p
///       c  assembleCfdMomentum x3, with that flux and that gradient
///       d  advanceCfdCoupling                           (P19-PVCOUPLE-001)
///       e  cfdCouplingVerdict: Diverged -> stop, Converged -> publish
///       f  cfdCouplingBudgetExhausted  -> IterationLimitReached
///  6  currentResultSource, CfdResultFields::create, CfdResult::create
///  7  slot.recordSuccess, or slot.recordFailure on any refusal above
/// ```
///
/// THE MOMENTUM SYSTEMS ARE RE-ASSEMBLED EVERY ITERATION, and that is required
/// rather than cautious: `CfdMomentumInput` takes `faceFlux`,
/// `currentVelocity`, `faceGradient` and `pressureGradient`, and all of them
/// move with the fields. Caching them would freeze the convection coefficients
/// at the initial field, and the run would converge to the wrong answer with
/// falling residuals all the way.
///
/// THE PRESSURE GRADIENT IS PASSED, AND A REVIEW OF `P19-PVCOUPLE-001` IS WHY.
/// That review found every fixture in the coupling suite omitting it, and the
/// consequence is recorded in that test file: "WITHOUT THE PRESSURE GRADIENT
/// THE MOMENTUM EQUATION IS MISSING ONE OF ITS TWO DRIVING FORCES ... so the
/// pressure was a pure OUTPUT and nothing could converge in the usual sense." A
/// steady solver that omitted it would iterate, report falling residuals, and
/// solve a different problem.
///
/// "NaN / INF REJECTION" IS DISCHARGED BY THE MAPPING AND NOT BY A LOOP HERE.
/// A check over the corrected fields was written first and then found
/// UNREACHABLE: `applyCfdCouplingCorrection` already ends with
/// `if (!allFinite(out.faceFlux) || !allFinite(out.velocity) ||
/// !allFinite(out.pressure))` and refuses as `ResultIsNotFinite`, so
/// `advanceCfdCoupling` never returns a success whose fields hold one. A guard
/// no input can fire is what this project's rule rejects, so it was removed —
/// `P19-LINSOLVE-001` had a mutation probe find the same shape of defect in two
/// kernels, and here the layer below was read instead. What remains is
/// reachable: `stageOf` answers `NonFiniteField` for both `ResultIsNotFinite`
/// and `ValueNotFinite`, and `CfdResultFields::create` refuses a non-finite
/// value for every caller that is not this one.
///
/// PUBLICATION IS ATOMIC AND HAS ONE PATH. Every refusal reaches
/// `CfdResultSlot::recordFailure`, which keeps any previous result untouched;
/// only a converged run reaches `recordSuccess`, with a complete field set.
///
/// The returned failure and the recorded one are EQUAL BY VALUE and built from
/// one initializer, recorded first and then returned. They are two objects: an
/// earlier version of this paragraph said "the same object", which an
/// adversarial review found to be false -- `recordFailure` takes its argument by
/// value, so it copy-constructs, and the original is then moved out. The
/// property that matters holds, which is that neither can drift from the other,
/// but it holds because they share an initializer and not because they share
/// an address.
///
/// DETERMINISTIC, with one inherited hole. Every loop here runs in `CellIndex`
/// or `FaceIndex` order with no unordered container, no wall clock and no
/// thread; the operators it calls each carry their own determinism evidence.
/// The hole is @p solve, as noted at the top of this file.
[[nodiscard]] BETTERCAD_CFD_EXPORT std::expected<CfdSteadyReport, CfdSolveFailure>
solveCfdSteady(const Document& document, const features::Regenerator& regenerator, const meshing::Mesher& mesher,
               CfdAnalysisId analysis, const CfdLinearSolve& solve, CfdResultSlot& slot);

} // namespace bettercad::cfd
