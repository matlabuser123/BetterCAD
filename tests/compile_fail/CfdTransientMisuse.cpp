// P19-TRANSIENT-001 — misuse of the transient march, the time grid and the
// history that must not compile. ADR-078.
//
// THE GUARDS ARE `BETTERCAD_CF_<CASE>`, NOT `<CASE>`. `P19-PVCOUPLE-001` wrote
// them the short way, every translation unit came out EMPTY, every build
// succeeded, and all thirty-seven gates reported "exit code 0, expected 1" --
// caught only because the gates expect FAILURE. The convention is kept here.
//
// FOUR CLAIMS OF THIS MILESTONE ARE ENFORCED HERE RATHER THAN REVIEWED.
//
//   1. NOTHING IN THE CANONICAL DATA VARIES WITH TIME, which is both
//      "Time-dependent BC policy" and "Time-dependent load policy" on the
//      checklist. Those are NEGATIVE claims, and a negative claim with no test
//      is prose -- `ADR-078` Decision 7 says so and this group is what makes it
//      checkable. A boundary condition holds a velocity or a pressure, gravity
//      holds a vector and a height, the initial condition holds a uniform
//      state, and the transient settings hold two times, a count and a policy
//      enum. None of them accepts a callable and none of them has a profile, a
//      schedule or a table, so the solver has nothing to silently freeze. If a
//      later milestone adds a time profile, these gates stop compiling and the
//      decision gets revisited, which is the outcome the policy wants.
//
//   2. THE SCOPE LINE. There is no adaptive stepping, no retry, no step
//      rejection, no scheme selector and no interpolation. Backward Euler is
//      fixed by `ADR-058` and a failed step fails the run, so a `theta`, a
//      `minTimeStep`, a `retries` or a `sampleAt(t)` would each be a false
//      capability. Their ABSENCE from the types is the proof, and that is only
//      provable by a build that fails.
//
//   3. POSSESSION IS THE EVIDENCE (`ADR-036`) for the grid, and therefore for
//      the report. `CfdTimeGrid`'s default constructor is private, so holding
//      one proves the step is finite and positive, the times strictly
//      increasing and the last time exactly `endTime`. `CfdTransientReport`
//      inherits that: it has no default constructor either, so a history
//      cannot be conjured and then filled in.
//
//   4. THE TIMES ARE DIMENSIONED. `Quantity` has no implicit conversion from
//      `double`, so `create(0.1, 1.0)` -- the call that would silently treat a
//      millisecond as a second -- does not compile. "Units are part of
//      correctness" is usually kept by a typed accessor; here it is kept by the
//      absence of a conversion.

#include <bettercad/cfd/CfdAnalysisObject.hpp>
#include <bettercad/cfd/CfdData.hpp>
#include <bettercad/cfd/CfdResult.hpp>
#include <bettercad/cfd/CfdSteady.hpp>
#include <bettercad/cfd/CfdTransient.hpp>
#include <bettercad/core/units/Units.hpp>

#include <vector>

namespace bc = bettercad;
namespace cfd = bettercad::cfd;

// ---------------------------------------------------------------------------
// Possession is the evidence: neither the grid nor the history may be conjured
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_A_TIME_GRID_CAN_BE_DEFAULT_CONSTRUCTED)
[[maybe_unused]] static void misuse() {
    cfd::CfdTimeGrid grid;
    (void)grid;
}
#endif

#if defined(BETTERCAD_CF_A_TRANSIENT_REPORT_CAN_BE_DEFAULT_CONSTRUCTED)
// It holds a `CfdTimeGrid` by value, whose default constructor is private, so
// an empty history cannot be made and then filled in step by step.
[[maybe_unused]] static void misuse() {
    cfd::CfdTransientReport report;
    (void)report;
}
#endif

// ---------------------------------------------------------------------------
// The times are dimensioned
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_A_TIME_GRID_IS_BUILT_FROM_BARE_DOUBLES)
// The call that would read 0.1 as a second when a millisecond was meant.
[[maybe_unused]] static void misuse() {
    (void)cfd::CfdTimeGrid::create(0.1, 1.0);
}
#endif

#if defined(BETTERCAD_CF_A_BARE_DOUBLE_IS_NOT_A_TIME_STEP)
[[maybe_unused]] static void misuse() {
    cfd::CfdTransientSettings settings;
    settings.timeStep = 0.1;
}
#endif

#if defined(BETTERCAD_CF_A_LENGTH_IS_NOT_A_TIME)
[[maybe_unused]] static void misuse(bc::Length length) {
    cfd::CfdTransientSettings settings;
    settings.endTime = length;
}
#endif

#if defined(BETTERCAD_CF_A_TIME_GRIDS_STEP_COUNT_IS_WRITABLE)
// Possession is the evidence only while the evidence cannot be rewritten: a
// grid whose step count could be edited would claim a march it never made.
//
// THIS GATE NAMES A MEMBER AND NOT A CONSTRUCTOR, DELIBERATELY. `P19-STEADY-001`
// had an inherited access gate silently become an arity gate when a private
// constructor gained a parameter -- GCC resolves arity before access, so the
// case still failed to compile for the wrong reason and the claim stopped being
// tested. A private DATA MEMBER has no arity to resolve.
[[maybe_unused]] static void misuse(cfd::CfdTimeGrid& grid) {
    grid.steps_ = 5;
}
#endif

#if defined(BETTERCAD_CF_A_TIME_GRID_CAN_BE_RE_POINTED)
// There is no setter: a grid is built from a step and an end time, or not at
// all.
[[maybe_unused]] static void misuse(cfd::CfdTimeGrid& grid) {
    grid.setEndTime(bc::Time::fromSi(2.0));
}
#endif

// ---------------------------------------------------------------------------
// NOTHING IS TIME-DEPENDENT -- the two policy items, as build failures
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_A_VELOCITY_INLET_TAKES_A_TIME_FUNCTION)
// A prescribed inlet speed is a NUMBER. If it could be a function of time, a
// solver that evaluated it once at t = 0 would silently freeze the profile.
[[maybe_unused]] static void misuse() {
    cfd::VelocityInlet inlet;
    inlet.normalSpeed = [](double t) { return bc::Velocity::fromSi(t); };
}
#endif

#if defined(BETTERCAD_CF_A_VELOCITY_INLET_HAS_A_TIME_PROFILE)
[[maybe_unused]] static void misuse(const cfd::VelocityInlet& inlet) {
    (void)inlet.profile;
}
#endif

#if defined(BETTERCAD_CF_A_PRESSURE_OUTLET_HAS_A_SCHEDULE)
[[maybe_unused]] static void misuse(const cfd::PressureOutlet& outlet) {
    (void)outlet.schedule;
}
#endif

#if defined(BETTERCAD_CF_A_BOUNDARY_CONDITION_HAS_A_TIME_TABLE)
[[maybe_unused]] static void misuse(const cfd::CfdBoundaryCondition& condition) {
    (void)condition.timeTable;
}
#endif

#if defined(BETTERCAD_CF_GRAVITY_TAKES_A_TIME_FUNCTION)
// The body force is the other half of the "load policy" item: `rho g` is
// constant, so a run cannot be given a ramped body force that it would then
// apply only at the first step.
[[maybe_unused]] static void misuse() {
    cfd::CfdGravity gravity;
    gravity.acceleration = [](double t) { return t; };
}
#endif

#if defined(BETTERCAD_CF_GRAVITY_HAS_A_TIME_PROFILE)
[[maybe_unused]] static void misuse(const cfd::CfdGravity& gravity) {
    (void)gravity.profile;
}
#endif

#if defined(BETTERCAD_CF_AN_INITIAL_CONDITION_HAS_A_TIME)
// The initial condition IS the state at t = 0; a time on it would be a second
// authority for when the march starts.
[[maybe_unused]] static void misuse(const cfd::CfdInitialCondition& initial) {
    (void)initial.time;
}
#endif

#if defined(BETTERCAD_CF_THE_TRANSIENT_SETTINGS_HAVE_A_TIME_PROFILE)
[[maybe_unused]] static void misuse(const cfd::CfdTransientSettings& settings) {
    (void)settings.profile;
}
#endif

#if defined(BETTERCAD_CF_THE_SOLVE_TAKES_A_TIME)
// There is no "solve up to t" entry point: the span comes from the definition,
// which is also what the fingerprint records.
[[maybe_unused]] static void misuse(const bc::Document& document, const bc::features::Regenerator& regenerator,
                                    const bc::meshing::Mesher& mesher, bc::CfdAnalysisId analysis,
                                    const cfd::CfdLinearSolve& solve, cfd::CfdResultSlot& slot) {
    (void)cfd::solveCfdTransient(document, regenerator, mesher, analysis, solve, slot, bc::Time::fromSi(1.0));
}
#endif

// ---------------------------------------------------------------------------
// The scope line: no adaptive step, no retry, no selector, no interpolation
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_THE_TRANSIENT_SETTINGS_HAVE_A_MINIMUM_STEP)
[[maybe_unused]] static void misuse(const cfd::CfdTransientSettings& settings) {
    (void)settings.minimumTimeStep;
}
#endif

#if defined(BETTERCAD_CF_THE_TRANSIENT_SETTINGS_HAVE_A_RETRY_COUNT)
[[maybe_unused]] static void misuse(const cfd::CfdTransientSettings& settings) {
    (void)settings.retries;
}
#endif

#if defined(BETTERCAD_CF_THE_TRANSIENT_SETTINGS_HAVE_A_THETA)
// Backward Euler is fixed by ADR-058. A theta with one legal value would be a
// false capability, and a theta with two would be new numerics nothing has
// qualified.
[[maybe_unused]] static void misuse(const cfd::CfdTransientSettings& settings) {
    (void)settings.theta;
}
#endif

#if defined(BETTERCAD_CF_THE_TRANSIENT_SETTINGS_HAVE_A_SCHEME_SELECTOR)
[[maybe_unused]] static void misuse(const cfd::CfdTransientSettings& settings) {
    (void)settings.scheme;
}
#endif

#if defined(BETTERCAD_CF_THE_HISTORY_INTERPOLATES)
// The samples are the integrator's own times. There is no list of requested
// output times in the canonical data, so there is no off-grid request to
// answer and no interpolation to get wrong (ADR-078 L6).
[[maybe_unused]] static void misuse(const cfd::CfdTransientReport& report) {
    (void)report.sampleAt(bc::Time::fromSi(0.5));
}
#endif

#if defined(BETTERCAD_CF_THE_HISTORY_RESTARTS)
[[maybe_unused]] static void misuse(const cfd::CfdTransientReport& report) {
    (void)report.restartState();
}
#endif

// ---------------------------------------------------------------------------
// A history's samples are published results, and they are read-only
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_A_SAMPLED_FIELD_IS_WRITABLE)
[[maybe_unused]] static void misuse(const cfd::CfdTransientReport& report) {
    report.samples.at(0).fields().pressure()[0] = 1.0;
}
#endif

#if defined(BETTERCAD_CF_A_SAMPLES_SOURCE_IS_WRITABLE)
// A sample whose provenance could be edited would let a stale history be made
// to look current without re-marching anything.
[[maybe_unused]] static void misuse(const cfd::CfdTransientReport& report) {
    cfd::CfdResultSource& source = report.samples.at(0).source();
    (void)source;
}
#endif

// ---------------------------------------------------------------------------
// The steady and the transient runs are different things
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_A_TRANSIENT_REPORT_IS_A_STEADY_ONE)
[[maybe_unused]] static void misuse(const cfd::CfdTransientReport& report) {
    const cfd::CfdSteadyReport steady = report;
    (void)steady;
}
#endif

#if defined(BETTERCAD_CF_A_STEP_RECORD_IS_RESULT_METADATA)
[[maybe_unused]] static void misuse(const cfd::CfdTransientStepRecord& record) {
    const cfd::CfdResultMetadata metadata = record;
    (void)metadata;
}
#endif

#if defined(BETTERCAD_CF_THE_TRANSIENT_SOLVE_TAKES_SETTINGS)
// The settings come from the DEFINITION (ADR-077 Decision 7, inherited): a
// run driven by caller-supplied settings would publish a fingerprint naming
// settings it did not use, and `resultCurrency` would call it Current.
[[maybe_unused]] static void misuse(const bc::Document& document, const bc::features::Regenerator& regenerator,
                                    const bc::meshing::Mesher& mesher, bc::CfdAnalysisId analysis,
                                    const cfd::CfdLinearSolve& solve, cfd::CfdResultSlot& slot,
                                    const cfd::CfdTransientSettings& settings) {
    (void)cfd::solveCfdTransient(document, regenerator, mesher, analysis, solve, slot, settings);
}
#endif

#if defined(BETTERCAD_CF_THE_TRANSIENT_SOLVE_PUBLISHES_WITHOUT_A_SLOT)
// Every exit publishes, success or failure. A solve with nowhere to publish
// could fail silently and leave an older result looking like the answer.
[[maybe_unused]] static void misuse(const bc::Document& document, const bc::features::Regenerator& regenerator,
                                    const bc::meshing::Mesher& mesher, bc::CfdAnalysisId analysis,
                                    const cfd::CfdLinearSolve& solve) {
    (void)cfd::solveCfdTransient(document, regenerator, mesher, analysis, solve);
}
#endif
