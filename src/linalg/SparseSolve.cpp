// The generic non-symmetric sparse solve (ADR-076).
//
// The three gates, in order: the factorisation and its R-diagonal ratio,
// finiteness, and the independent residual. The residual is NOT computed here:
// `evaluateResidual` is the symmetric kernel's own public acceptance step, it
// takes only arrays and knows no algorithm, and this file calls it and
// translates its classification. There is one residual gate in BetterCAD.
//
// THE SYSTEM CHECK IS WRITTEN TWICE, IN THE SAME ORDER, ON PURPOSE. The two
// kernels are separate translation units and neither may reach into the other's
// anonymous namespace, and extracting the check would edit a file qualified
// across two physics for the sake of forty lines. What makes the duplication
// safe is `Linalg_BothKernelsClassifyAMalformedSystemAlike`, which feeds the
// same malformed systems to both and asserts the same problem and the same
// index: drift is then a test failure rather than a silent divergence.

#include <bettercad/linalg/SparseSolve.hpp>

#include <Eigen/SparseCore>
#include <Eigen/SparseQR>

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <utility>

namespace bettercad::linalg {

namespace {

using Failure = SparseSolveFailure;

[[nodiscard]] Failure fail(SparseSolveProblem problem, Error error, std::size_t rows = 0) {
    Failure out;
    out.problem = problem;
    out.error = std::move(error);
    out.rows = rows;
    return out;
}

/// The dimension and finiteness checks of the system, in the symmetric kernel's
/// order so that the two classify alike.
[[nodiscard]] std::optional<Failure> checkSystem(std::size_t rows, std::span<const SparseIndex> rowStart,
                                                 std::span<const SparseIndex> inner, std::span<const double> values,
                                                 std::span<const double> rhs) {
    if (rowStart.size() != rows + 1 || rhs.size() != rows || inner.size() != values.size() ||
        (rows > 0 && rowStart.front() != 0) || rowStart.back() != static_cast<SparseIndex>(inner.size())) {
        return fail(SparseSolveProblem::DimensionMismatch,
                    makeError(ErrorCode::InvalidArgument,
                              std::format("a {} x {} system needs {} row offsets and {} right-hand side entries, "
                                          "with the last offset equal to the entry count",
                                          rows, rows, rows + 1, rows))
                        .error(),
                    rows);
    }
    // ROW OFFSETS MUST BE NON-DECREASING, AND THEIR ABSENCE WAS A
    // MEMORY-SAFETY DEFECT RATHER THAN A MISSING DIAGNOSTIC.
    // P19-LINSOLVE-001's audit found it: the checks above accept
    // `rowStart = {0, 5, 2}` for a 2 x 2 system with two stored entries --
    // the size is right, the first offset is 0 and the last equals
    // `inner.size()` -- and the build loop below then reads `inner[2..4]`
    // and `values[2..4]` past the end of their spans. `std::span`'s
    // `operator[]` is unchecked, so a hardened build aborts and an
    // unhardened one reads adjacent memory, narrows the garbage to Eigen's
    // index type and hands it to `setFromTriplets`, which guards the result
    // with `eigen_assert` alone: Debug aborts, Release writes out of
    // bounds. It was reproduced before this guard was written.
    //
    // MONOTONICITY ALONE IS SUFFICIENT, AND A SECOND CLAUSE BOUNDING EACH
    // INTERIOR OFFSET WAS REMOVED AFTER A MUTATION PROBE SHOWED IT COULD NOT
    // FIRE. The proof is one line: the clause above has already required
    // `rowStart.back() == inner.size()`, so if the offsets never decrease then
    // `rowStart[i] <= rowStart[rows] == inner.size()` for every `i`, and every
    // `slot` the loop below visits lies inside both spans. A guard no input can
    // fire is exactly what this project's own rule rejects -- `CfdBoundary.hpp`:
    // "An enumerator no input can produce is worse than no enumerator" -- and
    // the rule was being invoked elsewhere in this same milestone to justify
    // omitting other checks, so it applies here too.
    //
    // Both surviving clauses are pinned by probes: removing this one, and
    // removing the `rowStart.back()` clause it leans on, are each caught.
    //
    // Every in-tree caller validates monotonicity in its own container
    // first -- `CfdSparseMatrix::fromCsr` reports `RowStartInvalid`, and
    // thermal's and structural's do the same -- so no caller's behaviour
    // changes. The kernels are PUBLIC, and this file already claims they
    // validate their input: "the library must not become the validator".
    for (std::size_t row = 0; row < rows; ++row) {
        if (rowStart[row] > rowStart[row + 1]) {
            return fail(SparseSolveProblem::DimensionMismatch,
                        makeError(ErrorCode::InvalidArgument,
                                  std::format("row offset {} is {} and row offset {} is {}: the offsets "
                                              "must not decrease",
                                              row, rowStart[row], row + 1, rowStart[row + 1]))
                            .error(),
                        rows);
        }
    }

    // AND THE SYSTEM MUST FIT EIGEN'S INDEX TYPE. `Eigen::SparseMatrix`'s
    // StorageIndex is `int`, and the build loop narrows both the row and
    // the column to it. A system with more than `INT_MAX` stored entries
    // wraps silently to a negative index and lands in the same
    // `eigen_assert`-only path as the case above. Refused here rather than
    // left to wrap.
    {
        constexpr std::size_t limit = static_cast<std::size_t>(std::numeric_limits<int>::max());
        if (rows > limit || values.size() > limit) {
            return fail(SparseSolveProblem::DimensionMismatch,
                        makeError(ErrorCode::InvalidArgument,
                                  std::format("a {} row system with {} stored entries exceeds the index "
                                              "range the sparse factorisation can address ({})",
                                              rows, values.size(), limit))
                            .error(),
                        rows);
        }
    }

    for (const SparseIndex column : inner) {
        if (static_cast<std::size_t>(column) >= rows) {
            return fail(SparseSolveProblem::DimensionMismatch,
                        makeError(ErrorCode::InvalidArgument,
                                  std::format("a column index {} is outside a {} x {} system", column, rows, rows))
                            .error(),
                        rows);
        }
    }
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (!std::isfinite(values[i])) {
            Failure out = fail(SparseSolveProblem::NonFiniteSystem,
                               makeError(ErrorCode::InvalidArgument, "a matrix entry is not finite").error(), rows);
            out.index = i;
            return out;
        }
    }
    for (std::size_t i = 0; i < rhs.size(); ++i) {
        if (!std::isfinite(rhs[i])) {
            Failure out = fail(SparseSolveProblem::NonFiniteSystem,
                               makeError(ErrorCode::InvalidArgument, "a right-hand side entry is not finite").error(),
                               rows);
            out.index = i;
            return out;
        }
    }
    return std::nullopt;
}

/// The symmetric kernel's residual classification, said in this kernel's words.
[[nodiscard]] SparseSolveProblem translate(SymmetricSolveProblem problem) noexcept {
    switch (problem) {
    case SymmetricSolveProblem::InvalidSettings:
        return SparseSolveProblem::InvalidSettings;
    case SymmetricSolveProblem::DimensionMismatch:
        return SparseSolveProblem::DimensionMismatch;
    case SymmetricSolveProblem::NonFiniteSystem:
        return SparseSolveProblem::NonFiniteSystem;
    case SymmetricSolveProblem::SingularSystem:
        return SparseSolveProblem::SingularSystem;
    case SymmetricSolveProblem::NonFiniteSolution:
        return SparseSolveProblem::NonFiniteSolution;
    case SymmetricSolveProblem::NonFiniteResidual:
        return SparseSolveProblem::NonFiniteResidual;
    case SymmetricSolveProblem::ResidualTooLarge:
        return SparseSolveProblem::ResidualTooLarge;
    case SymmetricSolveProblem::FactorizationFailure:
        break;
    }
    return SparseSolveProblem::FactorizationFailure;
}

struct Solved {
    std::optional<Failure> failure{};
    SparseSolution solution{};
};

[[nodiscard]] Solved runKernel(std::size_t rows, std::span<const SparseIndex> rowStart,
                               std::span<const SparseIndex> inner, std::span<const double> values,
                               std::span<const double> rhs, const SparseSolveSettings& settings) {
    Solved out;
    if (Result<void> valid = validate(settings); !valid.has_value()) {
        out.failure = fail(SparseSolveProblem::InvalidSettings, valid.error(), rows);
        return out;
    }
    if (std::optional<Failure> bad = checkSystem(rows, rowStart, inner, values, rhs); bad.has_value()) {
        out.failure = std::move(bad);
        return out;
    }

    // ZERO ROWS IS A SOLVED SYSTEM. There is no factor, so the ratio is 1.0 and
    // nothing is handed to the library, exactly as the symmetric kernel does.
    if (rows == 0) {
        out.solution.rankRatio = 1.0;
        out.solution.reportedRank = 0;
        return out;
    }

    using Sparse = Eigen::SparseMatrix<double>;
    using EigenIndex = Sparse::StorageIndex;
    Sparse matrix(static_cast<Eigen::Index>(rows), static_cast<Eigen::Index>(rows));
    {
        std::vector<Eigen::Triplet<double, EigenIndex>> triplets;
        triplets.reserve(values.size());
        for (std::size_t row = 0; row < rows; ++row) {
            for (SparseIndex slot = rowStart[row]; slot < rowStart[row + 1]; ++slot) {
                triplets.emplace_back(static_cast<EigenIndex>(row), static_cast<EigenIndex>(inner[slot]),
                                      values[slot]);
            }
        }
        // The CSR holds each (row, column) once, so there is no duplicate for
        // `setFromTriplets` to reduce and its summation order -- which Eigen
        // does not specify -- cannot affect anything.
        matrix.setFromTriplets(triplets.begin(), triplets.end());
    }
    // SparseQR REQUIRES COMPRESSED MODE, and says so in its own documentation.
    // `setFromTriplets` leaves the matrix compressed, and this makes that a
    // guarantee of this file rather than an assumption about Eigen's internals.
    matrix.makeCompressed();

    Eigen::VectorXd right(static_cast<Eigen::Index>(rows));
    for (std::size_t row = 0; row < rows; ++row) {
        right(static_cast<Eigen::Index>(row)) = rhs[row];
    }

    Eigen::SparseQR<Sparse, Eigen::COLAMDOrdering<EigenIndex>> solver;
    solver.compute(matrix);
    if (solver.info() != Eigen::Success) {
        out.failure = fail(SparseSolveProblem::FactorizationFailure,
                           makeError(ErrorCode::FailedPrecondition,
                                     std::format("the factorisation of a {} x {} system failed before it began: "
                                                 "the library rejected the matrix",
                                                 rows, rows))
                               .error(),
                           rows);
        return out;
    }

    // THE CONDITIONING GATE, WHICH THE LIBRARY DOES NOT HAVE. `matrixR()` is a
    // real sparse matrix, so its diagonal is read in O(nnz) with no dense
    // intermediate -- the property that chose QR over the faster LU, whose U
    // diagonal the pinned Eigen does not expose at all (ADR-076 Decision 2).
    //
    // READ BY ITERATION AND NEVER BY `coeff()`, AND EIGEN'S OWN DOCUMENTATION IS
    // WHY. `SparseQR::matrixR()` warns: "The entries of the returned matrix are
    // not sorted. This means that using it in algorithms expecting sorted
    // entries will fail. This include random coefficient accesses
    // (SpaseMatrix::coeff())". The first form of this function did exactly that
    // -- a column-major copy and `coeff(i, i)`, which is the counter-example
    // Eigen's own comment prints -- and an adversarial review caught it. It
    // APPEARED to work, and a measurement could not make it return a wrong
    // number, because `CompressedStorage::atInRange` happens to test the last
    // entry of a column before it binary-searches and the diagonal happens to be
    // inserted last. Relying on that is relying on an undocumented shortcut.
    //
    // A single pass over the stored entries needs no ordering at all, is O(nnz)
    // either way, and binds a REFERENCE instead of copying the whole factor --
    // which the previous form did, since `matrixR()`'s return type is the same
    // type as this copy's.
    const Sparse& factor = solver.matrixR();
    std::vector<double> diagonal(rows, 0.0);
    for (Eigen::Index column = 0; column < factor.outerSize(); ++column) {
        for (Sparse::InnerIterator entry(factor, column); entry; ++entry) {
            if (entry.row() == entry.col()) {
                const std::size_t index = static_cast<std::size_t>(entry.row());
                if (index < rows) {
                    diagonal[index] = entry.value();
                }
            }
        }
    }

    double smallest = std::numeric_limits<double>::infinity();
    double largest = 0.0;
    for (std::size_t i = 0; i < rows; ++i) {
        const double entry = diagonal[i];
        if (!std::isfinite(entry)) {
            Failure failure = fail(SparseSolveProblem::SingularSystem,
                                   makeError(ErrorCode::FailedPrecondition,
                                             "the factorisation produced a diagonal entry that is not finite, so "
                                             "the system is singular or too ill conditioned to solve")
                                       .error(),
                                   rows);
            failure.index = i;
            failure.reportedRank = static_cast<std::size_t>(solver.rank());
            out.failure = std::move(failure);
            return out;
        }
        smallest = std::min(smallest, std::abs(entry));
        largest = std::max(largest, std::abs(entry));
    }
    out.solution.rankRatio = largest > 0.0 ? smallest / largest : 0.0;
    out.solution.reportedRank = static_cast<std::size_t>(solver.rank());

    if (out.solution.rankRatio < settings.rankRatioFloor) {
        // `rank()` is REPORTED here and does not decide anything (ADR-076
        // Decision 4): the ratio is the gate, and the rank is a fact that helps
        // a reader see WHY.
        const bool short_ = out.solution.reportedRank < rows;
        Failure failure =
            fail(SparseSolveProblem::SingularSystem,
                 makeError(ErrorCode::FailedPrecondition,
                           std::format("the system is singular or too ill conditioned to solve: the "
                                       "factorisation's smallest diagonal entry is {} of its largest, below the "
                                       "floor of {}, and the factorisation found {} independent columns of {}",
                                       out.solution.rankRatio, settings.rankRatioFloor, out.solution.reportedRank,
                                       rows))
                     .error(),
                 rows);
        failure.rankRatio = out.solution.rankRatio;
        failure.reportedRank = out.solution.reportedRank;
        failure.rankCameOutShort = short_;
        out.failure = std::move(failure);
        return out;
    }

    const Eigen::VectorXd solution = solver.solve(right);
    // NOT ONE OF THE THREE GATES, AND KEPT ANYWAY. `SparseQR::_solve_impl` sets
    // `m_info = Success` unconditionally on its last line in the pinned Eigen,
    // so this branch is unreachable today and is defence against a future
    // version rather than a gate this kernel can claim. Recorded as such
    // because a review asked which of the three gates it was, and the honest
    // answer is none.
    if (solver.info() != Eigen::Success) {
        out.failure = fail(SparseSolveProblem::FactorizationFailure,
                           makeError(ErrorCode::FailedPrecondition,
                                     "the back substitution failed after a successful factorisation")
                               .error(),
                           rows);
        return out;
    }

    out.solution.x.assign(static_cast<std::size_t>(solution.size()), 0.0);
    for (Eigen::Index i = 0; i < solution.size(); ++i) {
        out.solution.x[static_cast<std::size_t>(i)] = solution(i);
    }

    // THE ONE RESIDUAL GATE. `evaluateResidual` takes the ORIGINAL arrays and
    // this kernel's candidate, and knows nothing about which factorisation
    // produced it.
    //
    // ITS `pivotFloor` IS READ, AND AN EARLIER FORM OF THIS COMMENT SAID IT WAS
    // NOT. `evaluateResidual`'s first statement is `validate(settings)`, and
    // `validate` requires `pivotFloor` to be finite and positive. What is true
    // is narrower: no LDLT runs on this path, so the floor is never COMPARED
    // against a pivot, and the default is passed because it must pass that
    // validation. A review caught the overstatement.
    const SymmetricSolveSettings shared{.relativeResidualTolerance = settings.relativeResidualTolerance,
                                        .pivotFloor = SymmetricSolveSettings{}.pivotFloor};
    std::expected<ResidualNorms, SymmetricSolveFailure> accepted =
        evaluateResidual(rows, rowStart, inner, values, rhs, out.solution.x, shared);
    if (!accepted.has_value()) {
        Failure failure = fail(translate(accepted.error().problem), accepted.error().error, rows);
        failure.residual = accepted.error().residual;
        failure.index = accepted.error().index;
        failure.rankRatio = out.solution.rankRatio;
        failure.reportedRank = out.solution.reportedRank;
        out.solution.x.clear();
        out.failure = std::move(failure);
        return out;
    }
    out.solution.residual = *accepted;
    return out;
}

} // namespace

Result<void> validate(const SparseSolveSettings& settings) {
    if (!std::isfinite(settings.relativeResidualTolerance) || settings.relativeResidualTolerance <= 0.0) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("the relative residual tolerance must be finite and positive, not {}",
                                     settings.relativeResidualTolerance));
    }
    if (!std::isfinite(settings.rankRatioFloor) || settings.rankRatioFloor <= 0.0) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("the rank ratio floor must be finite and positive, not {}",
                                     settings.rankRatioFloor));
    }
    return {};
}

std::string_view toString(SparseSolveProblem problem) noexcept {
    switch (problem) {
    case SparseSolveProblem::InvalidSettings:
        return "invalid_settings";
    case SparseSolveProblem::DimensionMismatch:
        return "dimension_mismatch";
    case SparseSolveProblem::NonFiniteSystem:
        return "non_finite_system";
    case SparseSolveProblem::FactorizationFailure:
        return "factorization_failure";
    case SparseSolveProblem::SingularSystem:
        return "singular_system";
    case SparseSolveProblem::NonFiniteSolution:
        return "non_finite_solution";
    case SparseSolveProblem::NonFiniteResidual:
        return "non_finite_residual";
    case SparseSolveProblem::ResidualTooLarge:
        return "residual_too_large";
    }
    return "unknown";
}

std::expected<SparseSolution, SparseSolveFailure>
solveSparse(std::size_t rows, std::span<const SparseIndex> rowStart, std::span<const SparseIndex> inner,
            std::span<const double> values, std::span<const double> rhs, const SparseSolveSettings& settings) {
    Solved solved = runKernel(rows, rowStart, inner, values, rhs, settings);
    if (solved.failure.has_value()) {
        return std::unexpected(std::move(*solved.failure));
    }
    return std::move(solved.solution);
}

std::optional<SparseSolveProblem> sparseSolveProblem(std::size_t rows, std::span<const SparseIndex> rowStart,
                                                     std::span<const SparseIndex> inner,
                                                     std::span<const double> values, std::span<const double> rhs,
                                                     const SparseSolveSettings& settings) {
    Solved solved = runKernel(rows, rowStart, inner, values, rhs, settings);
    if (solved.failure.has_value()) {
        return solved.failure->problem;
    }
    return std::nullopt;
}

} // namespace bettercad::linalg
