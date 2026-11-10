#pragma once

// P19-TRANSIENT-001 -- the transient incompressible solve: the time grid, the
// march, the per-step convergence loop, and the atomic publication of a history
// whose last sample is the analysis's answer. ADR-078.
//
// ```text
//   rho (dU/dt + div(U U)) = -grad p + mu lap U + rho g      with div U = 0
//
//   backward Euler, one fixed step from t = 0 to endTime, the last step
//   shortened so the run lands on endTime exactly
// ```
//
// THIS MILESTONE ORCHESTRATES AND IT DOES NOT COMPUTE, and that is truer here
// than it was for the steady solve, because the transient TERM was qualified
// two milestones ago. `ADR-073` put `rho V / dt` on the momentum diagonal and
// `rho V / dt * U^n` in its source, with the unit assertions that make a term
// with a lost density fail to compile and a dedicated test for the one way a
// caller can get it wrong. The UNDERSTAND phase found a producer for most of
// the checklist:
//
// ```text
// Transient momentum term     CfdMomentumInput::timeStep + previousVelocity
//                                                          P19-MOM-001
// Pressure-velocity coupling  advanceCfdCoupling           P19-PVCOUPLE-001
// Per-step residual           advanceCfdCoupling step 7, ADR-058's CfdResiduals
// Per-step mass balance       advanceCfdCoupling step 6, evaluateCfdContinuity
//                                                          P19-CONT-001
// Reject invalid dt           validate(CfdTransientSettings)
//                                                          P19-DATA-001
// Reject time ordering        validate(CfdTransientSettings): an end time
//                             shorter than one step is refused there
// Reject NaN / Inf            applyCfdCouplingCorrection refuses a non-finite
//                             corrected field; stageOf maps it
//                                                          P19-STEADY-001
// Define initial fields       prepareCfdInitialField        P19-IC-001
// Define dt / end time /      CfdTransientSettings, and canonicalText already
//   output times              fingerprints all four fields  P19-DATA-001
// ```
//
// WHAT IS GENUINELY NEW IS THE GRID, THE MARCH AND THE SAMPLING. Everything
// else is a call.
//
// THE SETTINGS COME FROM THE DEFINITION AND NOT FROM A PARAMETER, for the
// reason `ADR-077` Decision 7 gave and this file inherits unchanged:
// `canonicalText` writes `transient.time_step`, `transient.end_time`,
// `transient.max_iterations_per_step` and `transient.sampling` into the
// definition fingerprint that `CfdResultSource::definition` carries, so a run
// driven by caller-supplied settings would publish provenance it does not have
// and `resultCurrency` would call it Current.
//
// NOTHING IN THE CANONICAL DATA VARIES WITH TIME, so the boundary and the body
// force are prepared ONCE and drive every step (ADR-078 Decision 7). A
// boundary condition is a velocity or a pressure, gravity is an acceleration
// and a reference height, the initial condition is a uniform state: there is
// no time profile to silently freeze. `CfdTransientMisuse.cpp` gates that,
// because a negative claim with no test is prose.
//
// ONLY THE VELOCITY HAS A PREVIOUS TIME LEVEL (ADR-078 Decision 8). The
// pressure of an incompressible flow carries no time derivative -- it is the
// constraint multiplier that makes the velocity divergence-free -- so it is
// carried across a step boundary as the next step's starting GUESS and appears
// in no term of any equation as `p^n`. The face flux is derived from the
// current velocity on every iteration and is never carried as state.
//
// AND THE TWO VELOCITY ARRAYS ARE THE DANGEROUS PART OF THIS FILE.
// `CfdMomentumInput` takes `currentVelocity` (the latest outer iterate, for
// diffusion's symmetry cross terms) and `previousVelocity` (`U^n`, frozen for
// the whole of a step's inner loop). They are spans of the same length in the
// same layout, so the type system cannot tell them apart, and `ADR-073`
// Decision 4 records what happens if the current field is passed as the
// previous one: the transient term's two halves cancel, the system becomes the
// steady one perturbed toward its own current state, and THE RUN CONVERGES TO
// THE CORRECT STEADY ANSWER with every steady assertion passing. The two tests
// that can see it are `CfdTransient_TheFirstStepsChangeScalesWithTheTimeStep`
// and `CfdTransient_AShortMarchWithASmallStepIsNotTheSteadySolution`; a
// fixed-point test could not, because the two arrays are equal at a fixed
// point -- and it is not reachable here anyway, since `CfdInitialCondition` is
// uniform and a developed profile cannot be put into a document. (An
// adversarial review found this paragraph naming the second test as
// `...AShortRunWithASmallStep...`, which does not exist. The header cites it as
// one of only two defences against the milestone's most dangerous defect, so a
// reader auditing that defence was being sent to a name that is not there.)
//
// WHAT THIS IS NOT.
//
// ```text
// no adaptive stepping   dt comes from the definition and never changes
// no retry               a step that fails fails the RUN
// no partial history     a failed march publishes nothing and keeps whatever
//                        result was held before it
// no interpolation       the samples are the integrator's own times; there is
//                        no list of requested output times in the canonical
//                        data, only the policy
// no restart             every run starts from the canonical initial condition
// no validation verdict  "CONVERGENCE IS RECORDED, NOT RE-JUDGED HERE"
//                        (CfdResult.hpp). P19-VALID-001 owns whether the
//                        numbers are good enough.
// ```

#include <bettercad/cfd/CfdAnalysisObject.hpp>
#include <bettercad/cfd/CfdCoupling.hpp>
#include <bettercad/cfd/CfdData.hpp>
#include <bettercad/cfd/CfdResult.hpp>
#include <bettercad/cfd/Export.hpp>
#include <bettercad/core/Error.hpp>
#include <bettercad/core/units/Units.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <vector>

namespace bettercad::features {
class Regenerator;
} // namespace bettercad::features

namespace bettercad::cfd {

// ---------------------------------------------------------------------------
// The time grid
// ---------------------------------------------------------------------------

/// The relative tolerance within which `endTime / timeStep` is taken to be a
/// whole number. The ONE place the time grid is given a tolerance; it is not a
/// solver tolerance and nothing else uses it.
///
/// `0.3 / 0.1` is `2.9999999999999996` in double precision, and without this
/// the grid would take three steps and then a fourth of `5.5e-17` s.
inline constexpr double kCfdTimeGridTolerance = 1.0e-9;

/// The most steps one run may take.
///
/// A RESOURCE guard, not an accuracy policy: a larger count is refused and the
/// step is NEVER enlarged to fit, because that would be solving a different
/// problem from the one asked for. `validate(CfdTransientSettings)` admits
/// `timeStep = 1e-300` with `endTime = 1e300` -- both finite, both positive,
/// the end time longer than one step -- so this guard is reachable through a
/// perfectly valid definition and is the only thing between it and an
/// unrepresentable step count.
inline constexpr std::size_t kMaxCfdTimeSteps = 1'000'000;

/// The most field values one run may store: samples times the values per
/// sample (`4 * cells + faces`, being p, the three components of U and phi).
///
/// The same shape of guard as `kMaxCfdTimeSteps` and the same rule: a history
/// that would exceed it is REFUSED, never silently reduced to fewer samples.
/// Quietly downgrading `EveryStep` to `FinalOnly` would publish a history the
/// definition did not ask for under a fingerprint that says it did.
inline constexpr std::size_t kMaxCfdStoredValues = 50'000'000;

/// The deterministic sequence `0 = t0 < t1 < ... < tN = endTime`, and the size
/// of each step.
///
/// ```text
/// t_k   = k * dt            for k < N      a multiplication, never a running sum
/// t_N   = endTime           EXACTLY
/// step  = dt                for every full step
/// last  = endTime - n*dt    the shortened remainder, only when endTime / dt is
///                           not a whole number to within kCfdTimeGridTolerance
/// ```
///
/// O(1) IN MEMORY however many steps it has: the times and the step sizes are
/// computed from the four numbers it holds, so a million-step grid costs
/// nothing until a step is taken. POSSESSION IS THE EVIDENCE (`ADR-036`) that
/// the step is finite and positive, the times strictly increasing, and the last
/// time exactly `endTime`: the default constructor is private and `create` is
/// the only way in.
///
/// TOTAL OVER FINITE POSITIVE INPUTS, AND DELIBERATELY SO. `dt > endTime` is
/// ONE shortened step of `endTime` rather than a refusal, because "you must be
/// able to take at least one full step" is an analysis policy that
/// `validate(CfdTransientSettings)` already holds -- "the end time {} s is
/// shorter than the time step {} s, so not one step could be taken". A grid
/// that refused it too would be a second spelling of one rule, and the two
/// could drift. `ADR-078` Decision 1.
///
/// THE SAME ALGORITHM AS `thermal::TimeGrid`, WHICH THIS CANNOT REUSE.
/// `ADR-059` put `cfd` at layer 42 and `thermal` at 45 so that a future
/// coupling can run out of CFD into thermal, and the layering test enforces the
/// mirror image: `cfd` may not include `thermal`. So the algorithm is stated
/// twice in the tree, in two modules that cannot share it, and no build check
/// can see a divergence between them. That is `ADR-078` `L1` and it is a real
/// cost, not a formality: a change to the final-step rule must be made in both
/// places by hand.
class BETTERCAD_CFD_EXPORT CfdTimeGrid {
public:
    /// Builds the grid, or says why not:
    ///
    /// ```text
    /// the step is finite and positive
    /// the end time is finite and positive
    /// the step count is within kMaxCfdTimeSteps  -- which also refuses an
    ///                                               overflowed quotient
    /// ```
    ///
    /// A ZERO END TIME IS REFUSED, which is where this differs from P18's grid.
    /// A thermal history can legitimately be the initial state alone; a CFD run
    /// cannot publish one, because `CfdResult::create` requires a transient
    /// sample to carry `a finite time > 0 s and a step >= 1`. A grid of no
    /// steps could therefore produce nothing to publish, and accepting it would
    /// move that refusal from the grid to a `.back()` on an empty vector.
    [[nodiscard]] static Result<CfdTimeGrid> create(Time timeStep, Time endTime);

    /// `N`: the number of steps. The number of TIMES is `N + 1`, because
    /// `t_0 = 0` is a time and not a step.
    [[nodiscard]] std::size_t stepCount() const noexcept { return steps_; }

    /// `t_k` for `0 <= k <= N`: zero at `k = 0` and `endTime` exactly at
    /// `k = N`. An index past `N` is the end time.
    [[nodiscard]] Time timeAt(std::size_t index) const noexcept;

    /// The size of step @p index (0-based, `index < N`): the nominal step for
    /// every full step, and the remainder for a shortened last one. An index
    /// past the last step is zero.
    [[nodiscard]] Time stepAt(std::size_t index) const noexcept;

    [[nodiscard]] Time endTime() const noexcept { return end_; }

    /// The nominal step it was built with, which is `stepAt` for every step but
    /// a shortened last one.
    [[nodiscard]] Time nominalStep() const noexcept { return nominal_; }

    /// Whether the last step is a shortened remainder.
    [[nodiscard]] bool hasPartialFinalStep() const noexcept { return partial_; }

    /// The time the steps actually add up to: `N * dt` for a whole number of
    /// steps and `(N - 1) * dt + remainder` for a shortened last one.
    ///
    /// READ THIS, NOT `endTime()`, to accumulate anything over the run. It is
    /// `endTime` to rounding in the second case and within
    /// `kCfdTimeGridTolerance * endTime` of it in the first -- where
    /// `endTime / dt` was a whole number only to that tolerance -- so the span
    /// integrated is never silently different from the span asked for by more
    /// than the documented tolerance.
    [[nodiscard]] Time integratedSpan() const noexcept;

    /// All `N + 1` times, for a caller that wants them materialised. O(N).
    [[nodiscard]] std::vector<Time> times() const;

    friend bool operator==(const CfdTimeGrid&, const CfdTimeGrid&) = default;

private:
    CfdTimeGrid() = default;

    Time nominal_{};
    Time end_{};
    /// The size of the shortened last step; zero when there is none.
    Time remainder_{};
    std::size_t steps_ = 0;
    bool partial_ = false;
};

// ---------------------------------------------------------------------------
// One step's evidence
// ---------------------------------------------------------------------------

/// What one accepted step measured. Derived; nothing here is canonical.
///
/// BOTH RESIDUAL RECORDS ARE KEPT, AND THAT IS `ADR-078` DECISION 5 MADE
/// READABLE. The divergence factor is relative to the FIRST iteration of a
/// run, and in a transient march the run is the STEP: each step is a fresh
/// nonlinear problem posed from a new `U^n`, so the scale its convergence is
/// relative to is its own first iteration and not the march's. That choice is
/// one `optional` reset inside the step loop rather than outside it, which is
/// invisible in a diff -- so both records are on the report, where a test can
/// assert the baseline moved.
struct CfdTransientStepRecord {
    /// 1-based. State 0 is the initial condition, which is not a step.
    std::uint32_t index = 0;
    /// The time at the END of this step. Exactly `grid.endTime()` for the last.
    Time time{};
    /// This step's own size: the nominal step, or the remainder for a shortened
    /// last one.
    Time stepSize{};
    /// Outer iterations this step took to converge; at least 1.
    std::uint32_t outerIterations = 0;
    /// This STEP's first-iteration residuals: the divergence baseline it was
    /// judged against.
    CfdResiduals firstResiduals{};
    /// Its last iteration's residuals, which are the ones the sample publishes
    /// when this step is sampled.
    CfdResiduals residuals{};
    /// `ADR-058`'s two measures, as the QUALIFIED evaluator produced them.
    CfdMassBalance balance{};
    /// Whether `balance` holds the evaluator's measures, which is false exactly
    /// in a closed domain -- there the two RELATIVE measures are undefined
    /// rather than small (`CfdContinuity.hpp`).
    bool massBalanceDefined = false;
    bool closedDomain = false;
    /// The scale the continuity residual is relative to, kg/s.
    double continuityScale = 0.0;
    /// The largest `|p'|` of this step's last iteration, Pa.
    double largestPressureCorrection = 0.0;
    /// Whether the momentum assembly reported the transient term applied, which
    /// is true exactly when a time step was supplied to it.
    ///
    /// RECORDED RATHER THAN GUARDED. The orchestrator always supplies the step,
    /// so a guard here could not fire for any input and this project removes
    /// those (`ADR-077` Decision 4). What a test can do is assert it, and
    /// `CfdTransient_EveryStepAppliesTheTransientTerm` does.
    bool transientTermApplied = false;
    /// `rho V / dt` over the cells, from this step's X-component assembly.
    ///
    /// COMPONENT-INDEPENDENT BY CONSTRUCTION -- `rho`, `V` and `dt` have no
    /// component -- so recording one component is exact rather than a sample of
    /// three. `CfdTransient_TheTransientCoefficientDoesNotDependOnTheComponent`
    /// asserts that rather than leaving it to this sentence.
    MassFlowRate smallestTransientCoefficient{};
    MassFlowRate largestTransientCoefficient{};

    friend bool operator==(const CfdTransientStepRecord&, const CfdTransientStepRecord&) = default;
};

// ---------------------------------------------------------------------------
// The history
// ---------------------------------------------------------------------------

/// The history of one accepted transient run.
///
/// THE SLOT HOLDS THE FINAL SAMPLE AND THIS HOLDS THE REST (`ADR-078`
/// Decision 2). `CfdResultSlot` holds at most one `CfdResult`, and a
/// `CfdResult` is the solution "at one time level"; the analysis's current
/// answer is its final state. So the last sample is published through the slot
/// and every requested sample is here, each a full `CfdResult` with its own
/// provenance, its own `time` and `step`, and fields that
/// `CfdResultFields::create` has already proved finite and of the layout's
/// lengths. The final sample therefore exists twice, which buys an invariant a
/// test can assert: the slot's result EQUALS `samples.back()`.
///
/// `EveryStep` IS `t_1 ... t_N`, NOT `t_0 ... t_N`, and this is where P19
/// differs from P18 deliberately (`ADR-078` Decision 3). A sample at `t = 0`
/// has no step index and no residuals because no system was solved to produce
/// it -- `CfdResultMetadata` says so ("t = 0 is the initial condition, not a
/// solve") and `CfdResult::create` refuses it. The initial field is below, as
/// a plain array, because that is what it is.
///
/// NO DEFAULT CONSTRUCTOR, AND THE COMPILER ENFORCES IT: `CfdTimeGrid`'s
/// default constructor is private, so this type has none either and
/// `solveCfdTransient` constructs its report WHOLE rather than filling an empty
/// one. The same device `CfdCouplingIteration` uses for the same reason.
struct CfdTransientReport {
    /// The grid that was marched. Its `stepCount()` is `steps.size()`.
    CfdTimeGrid grid;
    /// One record per step, in order, `grid.stepCount()` of them.
    std::vector<CfdTransientStepRecord> steps{};
    /// The requested samples, ascending in time. `FinalOnly`: one, at
    /// `endTime`. `EveryStep`: one per step.
    std::vector<CfdResult> samples{};
    /// The sample times, ascending, `samples.size()` of them. Each equals the
    /// `time` in the corresponding sample's metadata; kept here so a caller can
    /// find a time without unpacking a result.
    std::vector<Time> sampleTimes{};
    /// The resolved state at `t = 0`, as the initial condition expanded into the
    /// layout's lengths. A PLAIN ARRAY AND NOT A RESULT, for the reason above.
    std::vector<double> initialPressure{};
    std::vector<double> initialVelocity{};
    /// The policy that was read out of the definition.
    CfdTimeSampling sampling = CfdTimeSampling::FinalOnly;
    /// Outer iterations over the whole march, summed. This is also what a
    /// failure reports through `CfdSolveFailure::outerIterations`, where the
    /// step that failed is named in the diagnostic instead.
    std::uint64_t totalOuterIterations = 0;
};

/// Marches `rho dU/dt + ...` from `t = 0` to `endTime` and publishes the final
/// sample into @p slot.
///
/// ```text
///  1  find the analysis; its mode must be Transient
///  2  read ADR-058's settings and the transient settings out of the definition
///  3  build the time grid from the step and the end time
///  4  requireCfdModel -> currentResultSource -> requireCfdMesh
///                     -> requireCfdTopology -> requireCfdGeometry
///                     -> requireCfdFieldMap
///  5  prepareCfdBoundary, prepareCfdInitialField -- ONCE, for the whole march
///  6  expand p and U into this call's own arrays, and keep them as the t = 0
///     state
///  7  check the stored-sample budget BEFORE marching
///  8  for each step, with its own size from the grid:
///       a  freeze U^n: previousVelocity = the velocity at the start of the step
///       b  reset this step's divergence baseline
///       c  loop while the per-step budget allows:
///            cfdMassFlux from the current U
///            reconstructCfdPressureGradient from the current p
///            assembleCfdMomentum x3, WITH the step and U^n
///            advanceCfdCoupling
///            cfdCouplingVerdict against THIS STEP's first iteration
///       d  a step that diverges or exhausts its budget fails the RUN
///       e  record the step; sample it if the policy asks
///  9  slot.recordSuccess with the final sample, or slot.recordFailure on any
///     refusal above
/// ```
///
/// PUBLICATION IS ATOMIC OVER THE WHOLE MARCH, which is stronger than the
/// steady solve's atomicity and is the point of `CfdTransientSettings`'s own
/// rule that a step which does not converge fails the analysis. A march that
/// fails at step 40 of 50 publishes NOTHING -- not the 39 steps that converged,
/// not a shortened history -- and leaves whatever result was held before it
/// untouched. A partial history is not a worse history; it is a claim about a
/// time span that was never integrated.
///
/// THE MOMENTUM SYSTEMS ARE RE-ASSEMBLED EVERY ITERATION OF EVERY STEP, for
/// the reason `CfdSteady.hpp` gives -- `faceFlux`, `currentVelocity` and
/// `pressureGradient` all move with the fields, and freezing them would
/// converge to the wrong answer with falling residuals -- and for one more that
/// is this milestone's: the transient diagonal is `rho V / dt`, so a system
/// reused across a step boundary with a different `dt` would carry the previous
/// step's coefficient.
///
/// DETERMINISTIC. The times are formed by multiplication rather than by a
/// running sum, so `t_k` does not depend on the order the steps were taken in;
/// every loop runs in `CellIndex` or `FaceIndex` order with no unordered
/// container, no wall clock and no thread. The inherited hole is @p solve: a
/// supplied solve that is itself non-deterministic makes the run
/// non-deterministic and nothing here can detect that (`ADR-075` L4,
/// `ADR-076` L5, `ADR-077`, `ADR-078` L8) -- and a transient run multiplies
/// that hole's reach by the step count.
///
/// THE CONVECTION SCHEME AND THE GRADIENT METHOD ARE HARD-CODED HERE, exactly
/// as they are in `solveCfdSteady`, and that is `ADR-077` L12 inherited rather
/// than fixed: both sit outside the analysis definition and outside the
/// definition fingerprint, which contradicts `ADR-073` Decision 5. Moving them
/// means changing `CfdAnalysisDefinition` and `canonicalText`, both qualified
/// under `P19-DATA-001`, and no item on this checklist asks for a selector.
[[nodiscard]] BETTERCAD_CFD_EXPORT std::expected<CfdTransientReport, CfdSolveFailure>
solveCfdTransient(const Document& document, const features::Regenerator& regenerator, const meshing::Mesher& mesher,
                  CfdAnalysisId analysis, const CfdLinearSolve& solve, CfdResultSlot& slot);

} // namespace bettercad::cfd
