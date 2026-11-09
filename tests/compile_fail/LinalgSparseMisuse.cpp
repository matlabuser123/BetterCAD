// P19-LINSOLVE-001 — misuse of the two kernels that must not compile. ADR-076.
//
// THE GUARDS ARE `BETTERCAD_CF_<CASE>`, NOT `<CASE>`. P19-PVCOUPLE-001 wrote
// them the short way, every translation unit came out EMPTY, every build
// succeeded and all thirty-seven gates reported "exit code 0, expected 1" --
// which is only a caught defect because the gates expect FAILURE. Recorded as
// that milestone's `r22`.
//
// WHAT THIS GROUP IS FOR. `ADR-076`'s Consequences name one hazard above the
// others: `linalg` now carries TWO conditioning notions of the same shape and
// they are not the same quantity. `pivotFloor` bounds `min|D|/max|D|` of an
// LDLT's diagonal; `rankRatioFloor` bounds `min|R_ii|/max|R_ii|` of a QR's
// factor. A rename or a copy-paste that confused them would be a numerically
// meaningful change that compiled silently, so the type system is made to
// refuse it.

#include <bettercad/linalg/SparseSolve.hpp>
#include <bettercad/linalg/SymmetricSolve.hpp>

#include <span>
#include <vector>

namespace bl = bettercad::linalg;

namespace {

[[maybe_unused]] std::vector<bl::SparseIndex> rowStart() { return {0, 1, 2}; }
[[maybe_unused]] std::vector<bl::SparseIndex> inner() { return {0, 1}; }
[[maybe_unused]] std::vector<double> values() { return {1.0, 1.0}; }
[[maybe_unused]] std::vector<double> rhs() { return {1.0, 1.0}; }

} // namespace

// ---------------------------------------------------------------------------
// The two conditioning floors are NOT interchangeable.
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_SPARSE_SETTINGS_HAVE_A_PIVOT_FLOOR)
[[maybe_unused]] static void misuse() {
    bl::SparseSolveSettings settings;
    settings.pivotFloor = 1.0e-12;
}
#endif

#if defined(BETTERCAD_CF_SYMMETRIC_SETTINGS_HAVE_A_RANK_RATIO_FLOOR)
[[maybe_unused]] static void misuse() {
    bl::SymmetricSolveSettings settings;
    settings.rankRatioFloor = 1.0e-12;
}
#endif

#if defined(BETTERCAD_CF_A_SPARSE_SOLUTION_HAS_A_PIVOT_RATIO)
[[maybe_unused]] static void misuse() {
    bl::SparseSolution solution;
    const double ratio = solution.pivotRatio;
    (void)ratio;
}
#endif

#if defined(BETTERCAD_CF_A_SYMMETRIC_SOLUTION_HAS_A_RANK_RATIO)
[[maybe_unused]] static void misuse() {
    bl::SymmetricSolution solution;
    const double ratio = solution.rankRatio;
    (void)ratio;
}
#endif

#if defined(BETTERCAD_CF_A_SPARSE_FAILURE_HAS_AN_EXACTLY_ZERO_PIVOT)
[[maybe_unused]] static void misuse() {
    bl::SparseSolveFailure failure;
    const bool zero = failure.exactlyZeroPivot;
    (void)zero;
}
#endif

#if defined(BETTERCAD_CF_A_SYMMETRIC_FAILURE_HAS_A_REPORTED_RANK)
[[maybe_unused]] static void misuse() {
    bl::SymmetricSolveFailure failure;
    const std::size_t rank = failure.reportedRank;
    (void)rank;
}
#endif

// ---------------------------------------------------------------------------
// Neither settings type substitutes for the other.
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_SOLVE_SPARSE_TAKES_SYMMETRIC_SETTINGS)
[[maybe_unused]] static void misuse() {
    const std::vector<bl::SparseIndex> starts = rowStart();
    const std::vector<bl::SparseIndex> columns = inner();
    const std::vector<double> entries = values();
    const std::vector<double> right = rhs();
    (void)bl::solveSparse(2, starts, columns, entries, right, bl::SymmetricSolveSettings{});
}
#endif

#if defined(BETTERCAD_CF_SOLVE_SYMMETRIC_TAKES_SPARSE_SETTINGS)
[[maybe_unused]] static void misuse() {
    const std::vector<bl::SparseIndex> starts = rowStart();
    const std::vector<bl::SparseIndex> columns = inner();
    const std::vector<double> entries = values();
    const std::vector<double> right = rhs();
    (void)bl::solveSymmetricSparse(2, starts, columns, entries, right, bl::SparseSolveSettings{});
}
#endif

#if defined(BETTERCAD_CF_SPARSE_SETTINGS_ARE_SYMMETRIC_SETTINGS)
[[maybe_unused]] static void misuse() {
    const bl::SymmetricSolveSettings settings = bl::SparseSolveSettings{};
    (void)settings;
}
#endif

#if defined(BETTERCAD_CF_SYMMETRIC_SETTINGS_ARE_SPARSE_SETTINGS)
[[maybe_unused]] static void misuse() {
    const bl::SparseSolveSettings settings = bl::SymmetricSolveSettings{};
    (void)settings;
}
#endif

// ---------------------------------------------------------------------------
// Neither result type substitutes for the other.
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_A_SPARSE_SOLUTION_IS_A_SYMMETRIC_SOLUTION)
[[maybe_unused]] static void misuse() {
    const bl::SymmetricSolution solution = bl::SparseSolution{};
    (void)solution;
}
#endif

#if defined(BETTERCAD_CF_A_SPARSE_FAILURE_IS_A_SYMMETRIC_FAILURE)
[[maybe_unused]] static void misuse() {
    const bl::SymmetricSolveFailure failure = bl::SparseSolveFailure{};
    (void)failure;
}
#endif

#if defined(BETTERCAD_CF_A_SPARSE_PROBLEM_IS_A_SYMMETRIC_PROBLEM)
[[maybe_unused]] static void misuse() {
    const bl::SymmetricSolveProblem problem = bl::SparseSolveProblem::SingularSystem;
    (void)problem;
}
#endif

// ---------------------------------------------------------------------------
// The enumerator table is a constant.
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_THE_SPARSE_PROBLEM_TABLE_IS_ASSIGNABLE)
[[maybe_unused]] static void misuse() {
    bl::kSparseSolveProblems[0] = bl::SparseSolveProblem::ResidualTooLarge;
}
#endif
