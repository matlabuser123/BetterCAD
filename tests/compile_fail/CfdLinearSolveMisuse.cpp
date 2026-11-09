// P19-LINSOLVE-001 — misuse of the CFD solve that must not compile. ADR-076.
//
// THE GUARDS ARE `BETTERCAD_CF_<CASE>`, NOT `<CASE>` (P19-PVCOUPLE-001's `r22`).
//
// ONE CASE HERE IS AN ARCHITECTURAL GUARD RATHER THAN A TYPE GUARD.
// `BetterCAD::linalg` is linked PRIVATE, so a consumer of `BetterCAD::cfd` does
// not inherit it and no linalg type may appear in a cfd PUBLIC header. That is
// a claim about this header, and
// `THE_CFD_HEADER_DOES_NOT_EXPOSE_LINALG` is what makes it falsifiable: if a
// linalg type ever leaks into `CfdLinearSolve.hpp`, naming one here starts to
// COMPILE and the gate fails. The layering check enforces containment of the
// include; this enforces containment of the vocabulary.

#include <bettercad/cfd/CfdLinearSolve.hpp>

#include <span>
#include <vector>

namespace bc = bettercad::cfd;

// ---------------------------------------------------------------------------
// The architectural guard.
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_THE_CFD_HEADER_DOES_NOT_EXPOSE_LINALG)
[[maybe_unused]] static void misuse() {
    // Nothing above declares this, and nothing above may.
    const bettercad::linalg::SparseSolveSettings settings{};
    (void)settings;
}
#endif

// ---------------------------------------------------------------------------
// The conditioning measure has ONE name in CFD, and it is not a kernel's.
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_A_CFD_SOLUTION_HAS_A_PIVOT_RATIO)
[[maybe_unused]] static void misuse() {
    bc::CfdLinearSolution solution;
    const double ratio = solution.pivotRatio;
    (void)ratio;
}
#endif

#if defined(BETTERCAD_CF_A_CFD_SOLUTION_HAS_A_RANK_RATIO)
[[maybe_unused]] static void misuse() {
    bc::CfdLinearSolution solution;
    const double ratio = solution.rankRatio;
    (void)ratio;
}
#endif

#if defined(BETTERCAD_CF_A_CFD_FAILURE_HAS_A_PIVOT_FLOOR)
[[maybe_unused]] static void misuse() {
    bc::CfdLinearSolveFailure failure;
    const double floor = failure.pivotFloor;
    (void)floor;
}
#endif

#if defined(BETTERCAD_CF_A_CFD_SOLUTION_HAS_A_RESIDUAL_NORMS)
[[maybe_unused]] static void misuse() {
    bc::CfdLinearSolution solution;
    const auto norms = solution.residual;
    (void)norms;
}
#endif

// ---------------------------------------------------------------------------
// CFD owns ONE solver ratio (ADR-058), and no field was added for the floors.
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_THE_CFD_SETTINGS_HAVE_A_PIVOT_FLOOR)
[[maybe_unused]] static void misuse() {
    bc::CfdLinearSolverSettings settings;
    settings.pivotFloor = 1.0e-12;
}
#endif

#if defined(BETTERCAD_CF_THE_CFD_SETTINGS_HAVE_A_RANK_RATIO_FLOOR)
[[maybe_unused]] static void misuse() {
    bc::CfdLinearSolverSettings settings;
    settings.rankRatioFloor = 1.0e-12;
}
#endif

// ---------------------------------------------------------------------------
// The refusal vocabularies do not substitute for one another.
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_A_LINEAR_SOLVE_PROBLEM_IS_A_COUPLING_PROBLEM)
[[maybe_unused]] static void misuse() {
    const bc::CfdCouplingProblem problem = bc::CfdLinearSolveProblem::SystemIsSingular;
    (void)problem;
}
#endif

#if defined(BETTERCAD_CF_A_COUPLING_PROBLEM_IS_A_LINEAR_SOLVE_PROBLEM)
[[maybe_unused]] static void misuse() {
    const bc::CfdLinearSolveProblem problem = bc::CfdCouplingProblem::LinearSolveRefused;
    (void)problem;
}
#endif

#if defined(BETTERCAD_CF_A_LINEAR_SOLVE_FAILURE_IS_A_COUPLING_FAILURE)
[[maybe_unused]] static void misuse() {
    const bc::CfdCouplingFailure failure = bc::CfdLinearSolveFailure{};
    (void)failure;
}
#endif

#if defined(BETTERCAD_CF_A_LINEAR_SOLUTION_IS_A_SOLVE_RESULT)
[[maybe_unused]] static void misuse() {
    const bc::CfdLinearSolveResult result = bc::CfdLinearSolution{};
    (void)result;
}
#endif

// ---------------------------------------------------------------------------
// The enumerator table is a constant.
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_THE_CFD_LINEAR_SOLVE_PROBLEM_TABLE_IS_ASSIGNABLE)
[[maybe_unused]] static void misuse() {
    bc::kCfdLinearSolveProblems[0] = bc::CfdLinearSolveProblem::ResidualTooLarge;
}
#endif

// ---------------------------------------------------------------------------
// The entry points take a matrix, not raw arrays: no back door around the
// container's validating gate.
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_THE_PRESSURE_SOLVE_TAKES_RAW_ARRAYS)
[[maybe_unused]] static void misuse() {
    const std::vector<double> values{1.0, 1.0};
    const std::vector<double> rhs{1.0, 1.0};
    (void)bc::solveCfdPressureSystem(values, rhs, bc::CfdLinearSolverSettings{});
}
#endif

#if defined(BETTERCAD_CF_THE_MOMENTUM_SOLVE_TAKES_A_BARE_TOLERANCE)
[[maybe_unused]] static void misuse() {
    const std::vector<double> rhs{1.0, 1.0};
    const bc::CfdSparseMatrix* matrix = nullptr;
    (void)bc::solveCfdMomentumSystem(*matrix, rhs, 1.0e-9);
}
#endif
