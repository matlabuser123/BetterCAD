/// \file
/// P19-PVCOUPLE-001 — what the type system refuses about the coupling.
/// ADR-075.
///
/// WHAT THIS GROUP CAN AND CANNOT HOLD. These cases pin the places where a
/// misuse is a COMPILE error rather than a runtime refusal. Two things this
/// milestone's reviews should not mistake for coverage:
///
/// ```text
/// the unit types DO hold        a density, a pressure and a velocity are
///                              distinct types, so handing one where another
///                              belongs does not compile
/// the SPANS DO NOT             every field is `std::span<const double>` of SI
///                              values (ADR-066), so a velocity span and a
///                              pressure span are the SAME TYPE. Swapping them
///                              is a runtime length or finiteness refusal at
///                              best and a silent wrong answer at worst, and
///                              NO compile-fail case can hold it. That is
///                              stated here rather than implied by the group's
///                              size.
/// ```
///
/// Each case is selected by a macro the CMake registration defines one at a
/// time, and each carries the diagnostic regexes GCC can emit for it -- every
/// one it CAN emit, not the one a reader expects. `P19-GRAD-001` had three
/// gates fail because its regex omitted `invalid initialization`.

#include <bettercad/cfd/CfdContinuity.hpp>
#include <bettercad/cfd/CfdCoupling.hpp>

#include <span>
#include <vector>

using namespace bettercad;
using namespace bettercad::cfd;

// ---------------------------------------------------------------------------
// A summary does not hold another summary's members
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_A_CORRECTION_SUMMARY_HOLDS_A_CLOSURE_BOUND)
double f() {
    CfdPressureCorrectionSummary summary;
    return summary.closureBound;
}
#endif

#if defined(BETTERCAD_CF_AN_ITERATION_SUMMARY_HOLDS_A_COMPATIBILITY_RESIDUAL)
double f() {
    CfdCouplingIterationSummary summary;
    return summary.compatibilityResidual;
}
#endif

#if defined(BETTERCAD_CF_A_CORRECTION_SUMMARY_HOLDS_A_CONTINUITY_CLOSED_FLAG)
bool f() {
    CfdPressureCorrectionSummary summary;
    return summary.continuityClosed;
}
#endif

#if defined(BETTERCAD_CF_AN_ITERATION_SUMMARY_HOLDS_A_PINNED_CELL)
bool f() {
    CfdCouplingIterationSummary summary;
    return summary.pinnedCell.has_value();
}
#endif

// ---------------------------------------------------------------------------
// The two summaries are not each other
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_A_CORRECTION_SUMMARY_IS_AN_ITERATION_SUMMARY)
CfdCouplingIterationSummary f() {
    CfdPressureCorrectionSummary summary;
    return summary;
}
#endif

#if defined(BETTERCAD_CF_AN_ITERATION_SUMMARY_IS_A_CORRECTION_SUMMARY)
CfdPressureCorrectionSummary f() {
    CfdCouplingIterationSummary summary;
    return summary;
}
#endif

// ---------------------------------------------------------------------------
// Possession is the evidence: a system cannot be conjured
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_DEFAULT_CONSTRUCT_A_PRESSURE_CORRECTION_SYSTEM)
CfdPressureCorrectionSystem f() {
    // `CfdSparseMatrix`'s default constructor is private (ADR-036), so a
    // pressure-correction system with no matrix does not exist.
    return CfdPressureCorrectionSystem{};
}
#endif

#if defined(BETTERCAD_CF_DEFAULT_CONSTRUCT_A_COUPLING_ITERATION)
CfdCouplingIteration f() {
    // And an iteration holds a system, so it inherits the same impossibility.
    return CfdCouplingIteration{};
}
#endif

// ---------------------------------------------------------------------------
// The engineering types are not bare doubles
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_THE_DENSITY_TAKES_A_BARE_DOUBLE)
CfdPressureCorrectionInput f() {
    CfdPressureCorrectionInput input;
    input.density = 998.2;
    return input;
}
#endif

#if defined(BETTERCAD_CF_THE_DENSITY_IS_A_PRESSURE)
CfdPressureCorrectionInput f() {
    CfdPressureCorrectionInput input;
    input.density = Pressure::fromSi(998.2);
    return input;
}
#endif

#if defined(BETTERCAD_CF_THE_DENSITY_IS_A_VELOCITY)
CfdPressureCorrectionInput f() {
    CfdPressureCorrectionInput input;
    input.density = Velocity::fromSi(1.0);
    return input;
}
#endif

// ---------------------------------------------------------------------------
// The index types do not convert into one another
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_A_PINNED_CELL_IS_A_FACE)
CfdPressureCorrectionSummary f() {
    CfdPressureCorrectionSummary summary;
    summary.pinnedCell = FaceIndex::fromValue(0);
    return summary;
}
#endif

#if defined(BETTERCAD_CF_A_PINNED_CELL_IS_A_BOUNDARY_FACE)
CfdPressureCorrectionSummary f() {
    CfdPressureCorrectionSummary summary;
    summary.pinnedCell = BoundaryFaceIndex::fromValue(0);
    return summary;
}
#endif

#if defined(BETTERCAD_CF_A_PINNED_CELL_IS_A_PATCH)
CfdPressureCorrectionSummary f() {
    CfdPressureCorrectionSummary summary;
    summary.pinnedCell = PatchIndex::fromValue(0);
    return summary;
}
#endif

#if defined(BETTERCAD_CF_A_PINNED_CELL_IS_A_BARE_SIZE)
CfdPressureCorrectionSummary f() {
    CfdPressureCorrectionSummary summary;
    summary.pinnedCell = std::size_t{0};
    return summary;
}
#endif

// ---------------------------------------------------------------------------
// A problem is not an integer and not another operator's problem
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_A_COUPLING_PROBLEM_IS_AN_INTEGER)
CfdCouplingProblem f() { return 3; }
#endif

#if defined(BETTERCAD_CF_A_COUPLING_PROBLEM_IS_A_CONTINUITY_PROBLEM)
CfdCouplingProblem f() { return CfdContinuityProblem::ValueNotFinite; }
#endif

#if defined(BETTERCAD_CF_A_COUPLING_PROBLEM_IS_A_SPARSE_PROBLEM)
CfdCouplingProblem f() { return CfdSparseProblem::NonFiniteEntry; }
#endif

#if defined(BETTERCAD_CF_A_VERDICT_IS_A_PROBLEM)
CfdCouplingVerdict f() { return CfdCouplingProblem::ResultIsNotFinite; }
#endif

#if defined(BETTERCAD_CF_A_VERDICT_IS_A_BOOL)
CfdCouplingVerdict f() { return true; }
#endif

// ---------------------------------------------------------------------------
// The inputs are not each other
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_A_CORRECTION_INPUT_IS_A_PRESSURE_CORRECTION_INPUT)
CfdPressureCorrectionInput f() {
    CfdCouplingCorrectionInput input;
    return input;
}
#endif

#if defined(BETTERCAD_CF_A_PREDICTOR_INPUT_IS_AN_ITERATION_INPUT)
CfdCouplingIterationInput f() {
    CfdCouplingPredictorInput input;
    return input;
}
#endif

#if defined(BETTERCAD_CF_AN_ITERATION_INPUT_IS_A_PREDICTOR_INPUT)
CfdCouplingPredictorInput f() {
    CfdCouplingIterationInput input;
    return input;
}
#endif

// ---------------------------------------------------------------------------
// The settings records are distinct, and ADR-058 owns all of them
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_THE_ITERATION_SETTINGS_ARE_CONVERGENCE_SETTINGS)
CfdCouplingPredictorInput f() {
    CfdCouplingPredictorInput input;
    input.iteration = CfdConvergenceSettings{};
    return input;
}
#endif

#if defined(BETTERCAD_CF_THE_LINEAR_SETTINGS_ARE_STEADY_SETTINGS)
CfdCouplingPredictorInput f() {
    CfdCouplingPredictorInput input;
    input.linearSolver = CfdSteadySettings{};
    return input;
}
#endif

#if defined(BETTERCAD_CF_THE_BUDGET_TAKES_CONVERGENCE_SETTINGS)
bool f() { return cfdCouplingBudgetExhausted(0, CfdConvergenceSettings{}); }
#endif

#if defined(BETTERCAD_CF_THE_VERDICT_TAKES_ITERATION_SETTINGS)
CfdCouplingVerdict f() {
    return cfdCouplingVerdict(CfdResiduals{}, CfdResiduals{}, CfdIterationSettings{});
}
#endif

// ---------------------------------------------------------------------------
// A momentum system is not a pressure-correction system
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_A_MOMENTUM_SYSTEM_IS_A_PRESSURE_SYSTEM)
CfdPressureCorrectionSystem f(CfdMomentumSystem system) { return system; }
#endif

#if defined(BETTERCAD_CF_A_PRESSURE_SYSTEM_IS_A_MOMENTUM_SYSTEM)
CfdMomentumSystem f(CfdPressureCorrectionSystem system) { return system; }
#endif

// ---------------------------------------------------------------------------
// The solve port's shape is fixed
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_THE_SOLVE_RETURNS_A_BARE_VECTOR)
CfdLinearSolve f() {
    return [](const CfdSparseMatrix&, std::span<const double>,
              const CfdLinearSolverSettings&) -> std::vector<double> { return {}; };
}
#endif

#if defined(BETTERCAD_CF_THE_SOLVE_TAKES_A_MOMENTUM_SYSTEM)
CfdLinearSolve f() {
    return [](const CfdMomentumSystem&, std::span<const double>, const CfdLinearSolverSettings&)
               -> std::expected<CfdLinearSolveResult, Error> { return CfdLinearSolveResult{}; };
}
#endif

#if defined(BETTERCAD_CF_A_SOLVE_RESULT_IS_A_BARE_VECTOR)
CfdLinearSolveResult f() { return std::vector<double>{1.0}; }
#endif

// ---------------------------------------------------------------------------
// The optional members are optional, and the non-optional ones are not
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_THE_PINNED_CELL_IS_NOT_OPTIONAL)
CellIndex f() {
    CfdPressureCorrectionSummary summary;
    return summary.pinnedCell;
}
#endif

#if defined(BETTERCAD_CF_THE_SOLVE_RESIDUAL_IS_NOT_AN_ARRAY)
double f() {
    CfdCouplingPredictor predictor;
    return predictor.solveResidual;
}
#endif

// ---------------------------------------------------------------------------
// The measured bounds are constants, not variables
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_THE_SYMMETRY_TOLERANCE_IS_ASSIGNABLE)
void f() { kCfdCouplingSymmetryTolerance = 1.0; }
#endif

#if defined(BETTERCAD_CF_THE_CLOSURE_FLOOR_IS_ASSIGNABLE)
void f() { kCfdCouplingClosureFloor = 1.0; }
#endif
