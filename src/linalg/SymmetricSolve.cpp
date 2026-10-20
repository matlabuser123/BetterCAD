// The generic symmetric sparse solve (ADR-052), extracted from P17-SOLVE-001.
//
// EIGEN IS USED HERE, PRIVATE (ADR-039). No public header includes it.
//
// THIS IS P17'S KERNEL, MOVED AND NOT REWRITTEN. The order of the checks, the
// pivot gate, the independent residual and the normalisation are those of
// `structural::runKernel` as P17-SOLVE-001 qualified them; what changed is that
// the results are plain numbers and a structured failure rather than Newtons and
// P17's wording, so that both `structural` and `thermal` can say them in their
// own words. `structural::solveSymmetricSparse` rebuilds P17's messages from the
// structured fields byte for byte.
//
// WHAT EIGEN IS USED FOR, AND WHAT IT IS NOT. It factorises and it
// back-substitutes. It does NOT decide whether the system was solvable and it
// does NOT compute the residual:
//
//     the pivot gate   reads vectorD() and measures min|D|/max|D| itself,
//                      because SimplicialLDLT reports NumericalIssue only on
//                      an EXACTLY zero pivot and a singular system almost
//                      never produces one
//     the residual     is a loop over BetterCAD's own CSR arrays against
//                      BetterCAD's own right-hand side, after the solve
//                      returned. solver.error() is never consulted
//
// NO PENALTY, NO PINNING, NO REGULARISATION, NO FALLBACK.

#include <bettercad/linalg/SymmetricSolve.hpp>

#include <Eigen/SparseCholesky>
#include <Eigen/SparseCore>

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <utility>

namespace bettercad::linalg {

std::string_view toString(SymmetricSolveProblem problem) noexcept {
    switch (problem) {
    case SymmetricSolveProblem::InvalidSettings:
        return "invalid_settings";
    case SymmetricSolveProblem::DimensionMismatch:
        return "dimension_mismatch";
    case SymmetricSolveProblem::NonFiniteSystem:
        return "non_finite_system";
    case SymmetricSolveProblem::FactorizationFailure:
        return "factorization_failure";
    case SymmetricSolveProblem::SingularSystem:
        return "singular_system";
    case SymmetricSolveProblem::NonFiniteSolution:
        return "non_finite_solution";
    case SymmetricSolveProblem::NonFiniteResidual:
        return "non_finite_residual";
    case SymmetricSolveProblem::ResidualTooLarge:
        return "residual_too_large";
    }
    return "unknown";
}

Result<void> validate(const SymmetricSolveSettings& settings) {
    // A NaN threshold is refused here rather than left to decide something
    // inside a comparison: `r < NaN` is false, so it would reject every solve
    // for a reason no diagnostic could explain.
    if (!std::isfinite(settings.relativeResidualTolerance) || settings.relativeResidualTolerance <= 0.0) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("the relative residual tolerance must be finite and positive, not {}",
                                     settings.relativeResidualTolerance));
    }
    if (!std::isfinite(settings.pivotFloor) || settings.pivotFloor <= 0.0) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("the pivot floor must be finite and positive, not {}", settings.pivotFloor));
    }
    return {};
}

namespace {

using Failure = SymmetricSolveFailure;

[[nodiscard]] Failure fail(SymmetricSolveProblem problem, Error error, std::size_t rows = 0) {
    Failure out;
    out.problem = problem;
    out.error = std::move(error);
    out.rows = rows;
    return out;
}

/// The dimension and finiteness checks of the system, in P17's order.
[[nodiscard]] std::optional<Failure> checkSystem(std::size_t rows, std::span<const SparseIndex> rowStart,
                                                 std::span<const SparseIndex> inner, std::span<const double> values,
                                                 std::span<const double> rhs) {
    // DIMENSIONS, EXACTLY. No implicit resizing: a right-hand side of the wrong
    // length is a different problem, not a smaller one.
    if (rowStart.size() != rows + 1 || rhs.size() != rows || inner.size() != values.size() ||
        (rows > 0 && rowStart.front() != 0) || rowStart.back() != static_cast<SparseIndex>(inner.size())) {
        return fail(SymmetricSolveProblem::DimensionMismatch,
                    makeError(ErrorCode::InvalidArgument,
                              std::format("a {} x {} system needs {} row offsets and {} right-hand side entries, "
                                          "with the last offset equal to the entry count",
                                          rows, rows, rows + 1, rows))
                        .error(),
                    rows);
    }
    for (const SparseIndex column : inner) {
        if (static_cast<std::size_t>(column) >= rows) {
            return fail(SymmetricSolveProblem::DimensionMismatch,
                        makeError(ErrorCode::InvalidArgument,
                                  std::format("a column index {} is outside a {} x {} system", column, rows, rows))
                            .error(),
                        rows);
        }
    }

    // THE KERNEL IS PUBLIC, SO IT VALIDATES ITS INPUT: the library must not
    // become the validator.
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (!std::isfinite(values[i])) {
            Failure out = fail(SymmetricSolveProblem::NonFiniteSystem,
                               makeError(ErrorCode::InvalidArgument, "a matrix entry is not finite").error(), rows);
            out.index = i;
            return out;
        }
    }
    for (std::size_t i = 0; i < rhs.size(); ++i) {
        if (!std::isfinite(rhs[i])) {
            Failure out = fail(SymmetricSolveProblem::NonFiniteSystem,
                               makeError(ErrorCode::InvalidArgument, "a right-hand side entry is not finite").error(),
                               rows);
            out.index = i;
            return out;
        }
    }
    return std::nullopt;
}

/// THE INDEPENDENT RESIDUAL, AND THE GATE ON IT. A loop over the ORIGINAL CSR
/// and the ORIGINAL right-hand side. Eigen is not asked for it.
[[nodiscard]] std::expected<ResidualNorms, Failure>
acceptSolution(std::size_t rows, std::span<const SparseIndex> rowStart, std::span<const SparseIndex> inner,
               std::span<const double> values, std::span<const double> rhs, std::span<const double> x,
               const SymmetricSolveSettings& settings) {
    for (std::size_t i = 0; i < x.size(); ++i) {
        if (!std::isfinite(x[i])) {
            Failure out = fail(SymmetricSolveProblem::NonFiniteSolution,
                               makeError(ErrorCode::FailedPrecondition,
                                         std::format("solution component {} is not finite although the solver "
                                                     "reported success",
                                                     i))
                                   .error(),
                               rows);
            out.index = i;
            return std::unexpected(std::move(out));
        }
    }

    double squared = 0.0;
    double infinity = 0.0;
    double matrixInfinity = 0.0;
    double solutionSquared = 0.0;
    double rhsSquared = 0.0;
    for (std::size_t row = 0; row < rows; ++row) {
        double product = 0.0;
        double rowSum = 0.0;
        for (SparseIndex slot = rowStart[row]; slot < rowStart[row + 1]; ++slot) {
            product += values[slot] * x[static_cast<std::size_t>(inner[slot])];
            rowSum += std::abs(values[slot]);
        }
        const double entry = product - rhs[row];
        if (!std::isfinite(entry)) {
            Failure out = fail(SymmetricSolveProblem::NonFiniteResidual,
                               makeError(ErrorCode::FailedPrecondition,
                                         std::format("residual component {} is not finite", row))
                                   .error(),
                               rows);
            out.index = row;
            return std::unexpected(std::move(out));
        }
        squared += entry * entry;
        infinity = std::max(infinity, std::abs(entry));
        matrixInfinity = std::max(matrixInfinity, rowSum);
        solutionSquared += x[row] * x[row];
        rhsSquared += rhs[row] * rhs[row];
    }
    const double euclidean = std::sqrt(squared);
    const double solutionNorm = std::sqrt(solutionSquared);
    const double rhsNorm = std::sqrt(rhsSquared);
    if (!std::isfinite(euclidean) || !std::isfinite(solutionNorm) || !std::isfinite(rhsNorm)) {
        return std::unexpected(fail(SymmetricSolveProblem::NonFiniteResidual,
                                    makeError(ErrorCode::FailedPrecondition, "a residual norm is not finite").error(),
                                    rows));
    }

    ResidualNorms norms;
    norms.euclidean = euclidean;
    norms.infinity = infinity;
    norms.matrixInfinity = matrixInfinity;
    norms.solution = solutionNorm;
    norms.rightHandSide = rhsNorm;

    // THE NORMALISATION, AND ITS DEGENERATE CASE HANDLED EXACTLY RATHER THAN
    // WITH AN INVENTED FLOOR.
    const double denominator = matrixInfinity * solutionNorm + rhsNorm;
    if (denominator == 0.0) {
        if (euclidean != 0.0) {
            Failure out = fail(SymmetricSolveProblem::ResidualTooLarge,
                               makeError(ErrorCode::Internal,
                                         std::format("the system has no scale -- the solution and the right-hand "
                                                     "side are both zero -- and yet the residual is {}",
                                                     euclidean))
                                   .error(),
                               rows);
            out.residual = norms;
            return std::unexpected(std::move(out));
        }
        norms.normalized = 0.0;
    } else {
        norms.normalized = euclidean / denominator;
    }

    if (!(norms.normalized <= settings.relativeResidualTolerance)) {
        Failure out = fail(SymmetricSolveProblem::ResidualTooLarge,
                           makeError(ErrorCode::FailedPrecondition,
                                     std::format("the independent residual is too large: ||r||_2 = {}, ||r||_inf = "
                                                 "{}, normalized = {} against a threshold of {}. The solver "
                                                 "reported success; BetterCAD does not",
                                                 euclidean, infinity, norms.normalized,
                                                 settings.relativeResidualTolerance))
                               .error(),
                           rows);
        out.residual = norms;
        return std::unexpected(std::move(out));
    }
    return norms;
}

struct Solved {
    std::optional<Failure> failure{};
    SymmetricSolution solution{};
};

[[nodiscard]] Solved runKernel(std::size_t rows, std::span<const SparseIndex> rowStart,
                               std::span<const SparseIndex> inner, std::span<const double> values,
                               std::span<const double> rhs, const SymmetricSolveSettings& settings) {
    Solved out;
    if (Result<void> checked = validate(settings); !checked.has_value()) {
        out.failure = fail(SymmetricSolveProblem::InvalidSettings, checked.error(), rows);
        return out;
    }
    if (std::optional<Failure> bad = checkSystem(rows, rowStart, inner, values, rhs); bad.has_value()) {
        out.failure = std::move(bad);
        return out;
    }

    // NO FREE EQUATIONS IS A SOLVED SYSTEM, NOT A FAILURE. There are no
    // unknowns, so `x` is empty and the residual is vacuously zero. Nothing is
    // handed to the library, so no 0x0 factorisation is attempted.
    if (rows == 0) {
        out.solution.pivotRatio = 1.0;
        return out;
    }

    // Build Eigen's matrix from the CSR. A Map would need Eigen's own signed
    // StorageIndex and BetterCAD's index is unsigned 64-bit, so this is a
    // conversion rather than a view. It is O(nnz) once, against a factorisation
    // that is superlinear.
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

    Eigen::VectorXd force(static_cast<Eigen::Index>(rows));
    for (std::size_t row = 0; row < rows; ++row) {
        force(static_cast<Eigen::Index>(row)) = rhs[row];
    }

    Eigen::SimplicialLDLT<Sparse> solver;
    solver.compute(matrix);
    if (solver.info() != Eigen::Success) {
        // NumericalIssue FROM compute() MEANS ONE THING: `SimplicialCholesky_impl.h`
        // assigns `m_info` in exactly one place for the LDLT path -- `ok = false`
        // when `d == RealScalar(0)` -- so it is an EXACTLY zero pivot, which is a
        // singular matrix and not a generic factorisation fault. Anything else
        // (InvalidInput) stays a FactorizationFailure.
        const bool zeroPivot = solver.info() == Eigen::NumericalIssue;
        Failure failure = fail(zeroPivot ? SymmetricSolveProblem::SingularSystem
                                         : SymmetricSolveProblem::FactorizationFailure,
                               makeError(ErrorCode::FailedPrecondition,
                                         zeroPivot ? std::format("the system is singular: the factorisation of a "
                                                                 "{} x {} system found an exactly zero pivot",
                                                                 rows, rows)
                                                   : std::format("the factorisation of a {} x {} system failed "
                                                                 "before it began: the library rejected the matrix",
                                                                 rows, rows))
                                   .error(),
                               rows);
        failure.exactlyZeroPivot = zeroPivot;
        out.failure = std::move(failure);
        return out;
    }

    // THE PIVOT GATE, WHICH THE LIBRARY DOES NOT HAVE. For an SPD matrix every
    // entry of D is positive, and the RATIO min|D|/max|D| is dimensionless, so a
    // soft system with small pivots is not mistaken for a singular one.
    const Eigen::VectorXd diagonal = solver.vectorD();
    double smallest = std::numeric_limits<double>::infinity();
    double largest = 0.0;
    bool positive = true;
    for (Eigen::Index i = 0; i < diagonal.size(); ++i) {
        const double entry = diagonal(i);
        if (!std::isfinite(entry)) {
            Failure failure = fail(SymmetricSolveProblem::SingularSystem,
                                   makeError(ErrorCode::FailedPrecondition,
                                             "the factorisation produced a pivot that is not finite, so the system "
                                             "is singular or too ill conditioned to solve")
                                       .error(),
                                   rows);
            failure.index = static_cast<std::size_t>(i);
            failure.nonFinitePivot = true;
            out.failure = std::move(failure);
            return out;
        }
        if (entry <= 0.0) {
            positive = false;
        }
        smallest = std::min(smallest, std::abs(entry));
        largest = std::max(largest, std::abs(entry));
    }
    out.solution.pivotRatio = largest > 0.0 ? smallest / largest : 0.0;
    if (!positive || out.solution.pivotRatio < settings.pivotFloor) {
        Failure failure = fail(SymmetricSolveProblem::SingularSystem,
                               makeError(ErrorCode::FailedPrecondition,
                                         std::format("the system is singular or too ill conditioned to solve: the "
                                                     "factorisation's smallest pivot is {} of its largest, below the "
                                                     "floor of {}, and {}",
                                                     out.solution.pivotRatio, settings.pivotFloor,
                                                     positive ? "every pivot is positive"
                                                              : "at least one pivot is not positive"))
                                   .error(),
                               rows);
        failure.pivotRatio = out.solution.pivotRatio;
        failure.allPivotsPositive = positive;
        out.failure = std::move(failure);
        return out;
    }

    const Eigen::VectorXd solution = solver.solve(force);
    if (solver.info() != Eigen::Success) {
        out.failure = fail(SymmetricSolveProblem::FactorizationFailure,
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

    // The same gate `evaluateResidual` offers: finiteness, then the independent
    // residual. The solver's own idea of its error is not consulted.
    std::expected<ResidualNorms, Failure> accepted =
        acceptSolution(rows, rowStart, inner, values, rhs, out.solution.x, settings);
    if (!accepted.has_value()) {
        out.failure = std::move(accepted.error());
        out.solution.x.clear();
        return out;
    }
    out.solution.residual = *accepted;
    return out;
}

} // namespace

std::expected<SymmetricSolution, SymmetricSolveFailure>
solveSymmetricSparse(std::size_t rows, std::span<const SparseIndex> rowStart, std::span<const SparseIndex> inner,
                     std::span<const double> values, std::span<const double> rhs,
                     const SymmetricSolveSettings& settings) {
    Solved solved = runKernel(rows, rowStart, inner, values, rhs, settings);
    if (solved.failure.has_value()) {
        return std::unexpected(std::move(*solved.failure));
    }
    return std::move(solved.solution);
}

std::optional<SymmetricSolveProblem> symmetricSolveProblem(std::size_t rows, std::span<const SparseIndex> rowStart,
                                                           std::span<const SparseIndex> inner,
                                                           std::span<const double> values,
                                                           std::span<const double> rhs,
                                                           const SymmetricSolveSettings& settings) {
    const Solved solved = runKernel(rows, rowStart, inner, values, rhs, settings);
    if (!solved.failure.has_value()) {
        return std::nullopt;
    }
    return solved.failure->problem;
}

std::expected<ResidualNorms, SymmetricSolveFailure>
evaluateResidual(std::size_t rows, std::span<const SparseIndex> rowStart, std::span<const SparseIndex> inner,
                 std::span<const double> values, std::span<const double> rhs, std::span<const double> x,
                 const SymmetricSolveSettings& settings) {
    if (Result<void> checked = validate(settings); !checked.has_value()) {
        return std::unexpected(fail(SymmetricSolveProblem::InvalidSettings, checked.error(), rows));
    }
    if (std::optional<Failure> bad = checkSystem(rows, rowStart, inner, values, rhs); bad.has_value()) {
        return std::unexpected(std::move(*bad));
    }
    if (x.size() != rows) {
        return std::unexpected(fail(SymmetricSolveProblem::DimensionMismatch,
                                    makeError(ErrorCode::InvalidArgument,
                                              std::format("a candidate solution of {} entries cannot satisfy a {} x "
                                                          "{} system",
                                                          x.size(), rows, rows))
                                        .error(),
                                    rows));
    }
    return acceptSolution(rows, rowStart, inner, values, rhs, x, settings);
}

} // namespace bettercad::linalg
