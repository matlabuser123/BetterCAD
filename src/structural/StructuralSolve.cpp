// The linear static solve (P17-SOLVE-001).
//
// EIGEN IS USED HERE AND NOWHERE ELSE IN THIS MODULE. ADR-039 admitted it,
// PRIVATE, as `bettercad_sketch` and `bettercad_assembly` already have it, and
// no public header includes it -- `StructuralSolve.hpp` exposes BetterCAD types
// only, so nothing above layer 50 sees Eigen.
//
// WHAT EIGEN IS USED FOR, AND WHAT IT IS NOT. It factorises and it
// back-substitutes. It does NOT decide whether the system was solvable and it
// does NOT compute the residual:
//
//     the pivot gate   reads vectorD() and measures min|D|/max|D| itself,
//                      because SimplicialLDLT reports NumericalIssue only on
//                      an EXACTLY zero pivot and a singular system almost
//                      never produces one
//     the residual     is a loop over BetterCAD's own CSR copy of Kff against
//                      BetterCAD's own Ff, after the solve returned.
//                      solver.error() is never consulted
//
// NO PENALTY, NO PINNING, NO REGULARISATION. Searched in the evidence: there
// is no 1e12, no 1e20, no epsilon added to a diagonal and no node fixed to
// make a free body solvable. An under-constrained model fails.

#include <bettercad/structural/StructuralSolve.hpp>

#include <Eigen/SparseCholesky>
#include <Eigen/SparseCore>

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <utility>

namespace bettercad::structural {

std::string_view toString(SolverAlgorithm algorithm) noexcept {
    switch (algorithm) {
    case SolverAlgorithm::SimplicialLdlt:
        return "simplicial_ldlt";
    }
    return "unknown";
}

std::string_view toString(SolveProblem problem) noexcept {
    switch (problem) {
    case SolveProblem::InvalidSettings:
        return "invalid_settings";
    case SolveProblem::DimensionMismatch:
        return "dimension_mismatch";
    case SolveProblem::NonFiniteSystem:
        return "non_finite_system";
    case SolveProblem::FactorizationFailure:
        return "factorization_failure";
    case SolveProblem::SingularSystem:
        return "singular_system";
    case SolveProblem::NonFiniteSolution:
        return "non_finite_solution";
    case SolveProblem::NonFiniteResidual:
        return "non_finite_residual";
    case SolveProblem::ResidualTooLarge:
        return "residual_too_large";
    case SolveProblem::SourceMismatch:
        return "source_mismatch";
    }
    return "unknown";
}

Result<void> validate(const SolverSettings& settings) {
    switch (settings.algorithm) {
    case SolverAlgorithm::SimplicialLdlt:
        break;
    default:
        return makeError(ErrorCode::InvalidArgument,
                         "the solver algorithm is not one this build recognises");
    }
    // A NaN threshold is refused here rather than left to decide something
    // inside a comparison: `r < NaN` is false, so it would reject every solve
    // for a reason no diagnostic could explain.
    if (!std::isfinite(settings.relativeResidualTolerance) ||
        settings.relativeResidualTolerance <= 0.0) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("the relative residual tolerance must be finite and "
                                     "positive, not {}",
                                     settings.relativeResidualTolerance));
    }
    if (!std::isfinite(settings.pivotFloor) || settings.pivotFloor <= 0.0) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("the pivot floor must be finite and positive, not {}",
                                     settings.pivotFloor));
    }
    return {};
}

// ---------------------------------------------------------------------------
// FreeSystem
// ---------------------------------------------------------------------------

namespace {

/// A binary search within one CSR row. Shared by every `coeff` here, so there
/// is one definition of what a CSR lookup is.
[[nodiscard]] std::optional<std::size_t>
slotOf(std::span<const StiffnessMatrix::Index> rowStart,
       std::span<const StiffnessMatrix::Index> inner, std::size_t rows, std::size_t row,
       std::size_t column) noexcept {
    if (row >= rows) {
        return std::nullopt;
    }
    const auto first = inner.begin() + static_cast<std::ptrdiff_t>(rowStart[row]);
    const auto last = inner.begin() + static_cast<std::ptrdiff_t>(rowStart[row + 1]);
    const auto found = std::lower_bound(first, last, static_cast<StiffnessMatrix::Index>(column));
    if (found == last || *found != static_cast<StiffnessMatrix::Index>(column)) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(found - inner.begin());
}

} // namespace

Stiffness FreeSystem::coeff(std::size_t row, std::size_t column) const noexcept {
    const std::optional<std::size_t> slot = slotOf(rowStart_, inner_, rows_, row, column);
    return slot.has_value() ? Stiffness::fromSi(values_[*slot]) : Stiffness{};
}

double FreeSystem::largestSymmetryError() const noexcept {
    double largest = 0.0;
    for (std::size_t row = 0; row < rows_; ++row) {
        for (Index slot = rowStart_[row]; slot < rowStart_[row + 1]; ++slot) {
            const auto column = static_cast<std::size_t>(inner_[slot]);
            largest = std::max(largest, std::abs(values_[slot] - coeff(column, row).si()));
        }
    }
    return largest;
}

Result<FreeSystem> extractFreeSystem(const GlobalStructuralSystem& system,
                                     const FreeEquationMap& equations) {
    // THE PAIR MUST BE THE SAME MESH'S. The stamp AND the degree-of-freedom
    // count, so an M1 stiffness cannot be reduced by M2 constraints because
    // the dimensions happen to agree.
    if (!(system.mesh().isValid() && equations.stamp() == system.mesh()) ||
        equations.dofCount() != static_cast<DofIndex::ValueType>(system.degreesOfFreedom())) {
        return makeError(ErrorCode::FailedPrecondition,
                         "the free-equation numbering and the assembled system were built for "
                         "different meshes, so the reduced rows would name unrelated material");
    }

    const StiffnessMatrix& stiffness = system.stiffness();
    const std::span<const DofIndex> freeDofs = equations.freeDofs();

    // THE GLOBAL ROW OF EACH FREE EQUATION, and the inverse. The global row of
    // a DofIndex is its own free-equation position in the EMPTY-constraint
    // numbering the assembly used, which is `value() - 1`; that single
    // conversion is confined here rather than repeated, and it is the only
    // place this milestone touches the 1-based identity at all.
    std::vector<std::size_t> globalOf(freeDofs.size(), 0);
    // `freeOf[globalRow]` is the free equation, or npos when constrained.
    std::vector<std::size_t> freeOf(system.degreesOfFreedom(),
                                    std::numeric_limits<std::size_t>::max());
    for (std::size_t equation = 0; equation < freeDofs.size(); ++equation) {
        const DofIndex dof = freeDofs[equation];
        if (!dof.isValid() || dof.value() > equations.dofCount()) {
            return makeError(ErrorCode::Internal,
                             std::format("free equation {} names a degree of freedom outside the "
                                         "numbering",
                                         equation));
        }
        const auto globalRow = static_cast<std::size_t>(dof.value() - 1);
        globalOf[equation] = globalRow;
        freeOf[globalRow] = equation;
    }

    FreeSystem out;
    out.rows_ = freeDofs.size();
    out.rowStart_.assign(out.rows_ + 1, 0);
    out.force_.assign(out.rows_, 0.0);

    // ONE PASS, IN FreeEquationIndex ORDER. `freeDofs()` is ascending, so the
    // global rows are visited in ascending order too and the inner indices of
    // each reduced row come out ascending without a sort -- which is what a
    // CSR needs and what makes the extraction deterministic by construction
    // rather than by a sorting step a future caller might skip.
    const std::span<const StiffnessMatrix::Index> globalRowStart = stiffness.rowStart();
    const std::span<const StiffnessMatrix::Index> globalInner = stiffness.innerIndices();
    const std::span<const double> globalValues = stiffness.values();
    for (std::size_t equation = 0; equation < out.rows_; ++equation) {
        const std::size_t globalRow = globalOf[equation];
        for (StiffnessMatrix::Index slot = globalRowStart[globalRow];
             slot < globalRowStart[globalRow + 1]; ++slot) {
            const auto globalColumn = static_cast<std::size_t>(globalInner[slot]);
            const std::size_t column = freeOf[globalColumn];
            if (column == std::numeric_limits<std::size_t>::max()) {
                // A constrained column. Its contribution would be `Kfc uc`,
                // and `uc = 0`, so it is dropped rather than subtracted. A
                // later prescribed-displacement milestone adds the one term
                // here; the structure is written so that it is one term.
                continue;
            }
            out.inner_.push_back(static_cast<FreeSystem::Index>(column));
            out.values_.push_back(globalValues[slot]);
        }
        out.rowStart_[equation + 1] = static_cast<FreeSystem::Index>(out.inner_.size());
        out.force_[equation] = system.force().values()[globalRow];
    }
    return out;
}

// ---------------------------------------------------------------------------
// The numerical kernel
// ---------------------------------------------------------------------------

namespace {

/// One pass, shared by `solveSymmetricSparse` and `symmetricSolveProblem`.
struct Solved {
    std::optional<SolveProblem> problem{};
    Error error{};
    SymmetricSolution solution{};
};

[[nodiscard]] Solved fail(SolveProblem problem, Error error) {
    return Solved{.problem = problem, .error = std::move(error)};
}

[[nodiscard]] Solved runKernel(std::size_t rows,
                               std::span<const StiffnessMatrix::Index> rowStart,
                               std::span<const StiffnessMatrix::Index> inner,
                               std::span<const double> values, std::span<const double> rhs,
                               const SolverSettings& settings) {
    if (Result<void> checked = validate(settings); !checked.has_value()) {
        return fail(SolveProblem::InvalidSettings, checked.error());
    }

    // DIMENSIONS, EXACTLY. No implicit resizing: a right-hand side of the
    // wrong length is a different problem, not a smaller one.
    if (rowStart.size() != rows + 1 || rhs.size() != rows || inner.size() != values.size() ||
        (rows > 0 && rowStart.front() != 0) ||
        rowStart.back() != static_cast<StiffnessMatrix::Index>(inner.size())) {
        return fail(SolveProblem::DimensionMismatch,
                    makeError(ErrorCode::InvalidArgument,
                              std::format("a {} x {} system needs {} row offsets and {} "
                                          "right-hand side entries, with the last offset equal "
                                          "to the entry count",
                                          rows, rows, rows + 1, rows))
                        .error());
    }
    for (const StiffnessMatrix::Index column : inner) {
        if (static_cast<std::size_t>(column) >= rows) {
            return fail(SolveProblem::DimensionMismatch,
                        makeError(ErrorCode::InvalidArgument,
                                  std::format("a column index {} is outside a {} x {} system",
                                              column, rows, rows))
                            .error());
        }
    }

    // THE KERNEL IS PUBLIC, SO IT VALIDATES ITS INPUT. P17-ASSEMBLY guarantees
    // a finite K and F, but this entry point can be called with anything and
    // the library must not become the validator.
    for (const double value : values) {
        if (!std::isfinite(value)) {
            return fail(SolveProblem::NonFiniteSystem,
                        makeError(ErrorCode::InvalidArgument,
                                  "a matrix entry is not finite")
                            .error());
        }
    }
    for (const double value : rhs) {
        if (!std::isfinite(value)) {
            return fail(SolveProblem::NonFiniteSystem,
                        makeError(ErrorCode::InvalidArgument,
                                  "a right-hand side entry is not finite")
                            .error());
        }
    }

    Solved out;

    // NO FREE EQUATIONS IS A SOLVED SYSTEM, NOT A FAILURE. There are no
    // unknowns, so `x` is empty and the residual is vacuously zero. Nothing is
    // handed to the library, so no 0x0 factorisation is attempted.
    if (rows == 0) {
        out.solution.pivotRatio = 1.0;
        return out;
    }

    // Build Eigen's matrix from the CSR. A Map would need Eigen's own signed
    // StorageIndex and BetterCAD's index is unsigned 64-bit, so this is a
    // conversion rather than a view -- stated plainly rather than claimed as
    // zero copy. It is O(nnz) once, against a factorisation that is
    // superlinear.
    using Sparse = Eigen::SparseMatrix<double>;
    using EigenIndex = Sparse::StorageIndex;
    Sparse matrix(static_cast<Eigen::Index>(rows), static_cast<Eigen::Index>(rows));
    {
        std::vector<Eigen::Triplet<double, EigenIndex>> triplets;
        triplets.reserve(values.size());
        for (std::size_t row = 0; row < rows; ++row) {
            for (StiffnessMatrix::Index slot = rowStart[row]; slot < rowStart[row + 1]; ++slot) {
                triplets.emplace_back(static_cast<EigenIndex>(row),
                                      static_cast<EigenIndex>(inner[slot]), values[slot]);
            }
        }
        // The CSR holds each (row, column) once, so there is no duplicate for
        // `setFromTriplets` to reduce and its summation order -- which Eigen
        // does not specify -- cannot affect anything. That is the property
        // ADR-038's symbolic pass bought, used here.
        matrix.setFromTriplets(triplets.begin(), triplets.end());
    }

    Eigen::VectorXd force(static_cast<Eigen::Index>(rows));
    for (std::size_t row = 0; row < rows; ++row) {
        force(static_cast<Eigen::Index>(row)) = rhs[row];
    }

    Eigen::SimplicialLDLT<Sparse> solver;
    solver.compute(matrix);
    if (solver.info() != Eigen::Success) {
        // NumericalIssue FROM compute() MEANS ONE THING AND IT IS WORTH SAYING
        // SO. `SimplicialCholesky_impl.h` assigns `m_info` in exactly one
        // place for the LDLT path -- `ok = false` when `d == RealScalar(0)` --
        // so a NumericalIssue here is an EXACTLY zero pivot, which is a
        // singular matrix and not a generic factorisation fault. Reporting it
        // as SingularSystem rather than FactorizationFailure was a measured
        // correction: `[1 1; 1 1]` produces exactly that, and calling it a
        // "factorisation failure" would have told a caller to look for memory
        // or a malformed matrix.
        //
        // Anything else -- InvalidInput, for a matrix the library will not
        // accept at all -- stays a FactorizationFailure, because it is not a
        // statement about the physics.
        const bool zeroPivot = solver.info() == Eigen::NumericalIssue;
        return fail(zeroPivot ? SolveProblem::SingularSystem
                              : SolveProblem::FactorizationFailure,
                    makeError(ErrorCode::FailedPrecondition,
                              zeroPivot
                                  ? std::format("the system is singular: the factorisation of a "
                                                "{} x {} system found an exactly zero pivot. The "
                                                "model may be under-constrained, disconnected or "
                                                "a mechanism -- this layer measures the "
                                                "conditioning and does not claim to know which",
                                                rows, rows)
                                  : std::format("the factorisation of a {} x {} system failed "
                                                "before it began: the library rejected the "
                                                "matrix",
                                                rows, rows))
                        .error());
    }

    // THE PIVOT GATE, WHICH THE LIBRARY DOES NOT HAVE. SimplicialLDLT reports
    // NumericalIssue only when a pivot is EXACTLY zero, so a singular system
    // reaches here with info() == Success. `vectorD()` is the factorisation's
    // own diagonal; for an SPD matrix every entry is positive, and the RATIO
    // min|D|/max|D| is dimensionless, so a soft material with small pivots is
    // not mistaken for a singular one.
    const Eigen::VectorXd diagonal = solver.vectorD();
    double smallest = std::numeric_limits<double>::infinity();
    double largest = 0.0;
    bool positive = true;
    for (Eigen::Index i = 0; i < diagonal.size(); ++i) {
        const double entry = diagonal(i);
        if (!std::isfinite(entry)) {
            return fail(SolveProblem::SingularSystem,
                        makeError(ErrorCode::FailedPrecondition,
                                  "the factorisation produced a pivot that is not finite, so the "
                                  "system is singular or too ill conditioned to solve")
                            .error());
        }
        if (entry <= 0.0) {
            positive = false;
        }
        smallest = std::min(smallest, std::abs(entry));
        largest = std::max(largest, std::abs(entry));
    }
    out.solution.pivotRatio = largest > 0.0 ? smallest / largest : 0.0;
    if (!positive || out.solution.pivotRatio < settings.pivotFloor) {
        return fail(SolveProblem::SingularSystem,
                    makeError(ErrorCode::FailedPrecondition,
                              std::format("the system is singular or too ill conditioned to "
                                          "solve: the factorisation's smallest pivot is {} of "
                                          "its largest, below the floor of {}, and {}. The model "
                                          "may be under-constrained, disconnected or a "
                                          "mechanism -- this layer measures the conditioning and "
                                          "does not claim to know which",
                                          out.solution.pivotRatio, settings.pivotFloor,
                                          positive ? "every pivot is positive"
                                                   : "at least one pivot is not positive"))
                        .error());
    }

    const Eigen::VectorXd solution = solver.solve(force);
    if (solver.info() != Eigen::Success) {
        return fail(SolveProblem::FactorizationFailure,
                    makeError(ErrorCode::FailedPrecondition,
                              "the back substitution failed after a successful factorisation")
                        .error());
    }

    out.solution.x.assign(static_cast<std::size_t>(solution.size()), 0.0);
    for (Eigen::Index i = 0; i < solution.size(); ++i) {
        const double entry = solution(i);
        if (!std::isfinite(entry)) {
            return fail(SolveProblem::NonFiniteSolution,
                        makeError(ErrorCode::FailedPrecondition,
                                  std::format("solution component {} is not finite although the "
                                              "solver reported success",
                                              i))
                            .error());
        }
        out.solution.x[static_cast<std::size_t>(i)] = entry;
    }

    // THE INDEPENDENT RESIDUAL. A loop over the ORIGINAL CSR and the ORIGINAL
    // right-hand side, computed here after the solve returned. Eigen is not
    // asked for it and `solver.error()` is never consulted -- a factorisation
    // object's idea of its own error is not evidence that the engineering
    // equations hold.
    double squared = 0.0;
    double infinity = 0.0;
    double stiffnessInfinity = 0.0;
    double displacementSquared = 0.0;
    double forceSquared = 0.0;
    for (std::size_t row = 0; row < rows; ++row) {
        double product = 0.0;
        double rowSum = 0.0;
        for (StiffnessMatrix::Index slot = rowStart[row]; slot < rowStart[row + 1]; ++slot) {
            product += values[slot] * out.solution.x[static_cast<std::size_t>(inner[slot])];
            rowSum += std::abs(values[slot]);
        }
        const double entry = product - rhs[row];
        if (!std::isfinite(entry)) {
            return fail(SolveProblem::NonFiniteResidual,
                        makeError(ErrorCode::FailedPrecondition,
                                  std::format("residual component {} is not finite", row))
                            .error());
        }
        squared += entry * entry;
        infinity = std::max(infinity, std::abs(entry));
        stiffnessInfinity = std::max(stiffnessInfinity, rowSum);
        displacementSquared += out.solution.x[row] * out.solution.x[row];
        forceSquared += rhs[row] * rhs[row];
    }
    const double euclidean = std::sqrt(squared);
    const double displacementNorm = std::sqrt(displacementSquared);
    const double forceNorm = std::sqrt(forceSquared);
    if (!std::isfinite(euclidean) || !std::isfinite(displacementNorm) ||
        !std::isfinite(forceNorm)) {
        return fail(SolveProblem::NonFiniteResidual,
                    makeError(ErrorCode::FailedPrecondition,
                              "a residual norm is not finite")
                        .error());
    }

    // THE NORMALISATION, AND ITS DEGENERATE CASE HANDLED EXACTLY RATHER THAN
    // WITH AN INVENTED FLOOR. The denominator vanishes only when `uf` and
    // `Ff` are both exactly zero, and then `r` is exactly zero too, so `0` is
    // the honest answer. If `r` were non-zero with a zero denominator that
    // would be arithmetically impossible, so it is refused rather than
    // flattered.
    const double denominator = stiffnessInfinity * displacementNorm + forceNorm;
    if (denominator == 0.0) {
        if (euclidean != 0.0) {
            return fail(SolveProblem::ResidualTooLarge,
                        makeError(ErrorCode::Internal,
                                  std::format("the system has no scale -- the solution and the "
                                              "right-hand side are both zero -- and yet the "
                                              "residual is {} N",
                                              euclidean))
                            .error());
        }
        out.solution.residual.normalized = 0.0;
    } else {
        out.solution.residual.normalized = euclidean / denominator;
    }
    out.solution.residual.euclideanNorm = Force::fromSi(euclidean);
    out.solution.residual.infinityNorm = Force::fromSi(infinity);
    out.solution.residual.stiffnessInfinityNorm = Stiffness::fromSi(stiffnessInfinity);
    out.solution.residual.displacementNorm = Length::fromSi(displacementNorm);
    out.solution.residual.forceNorm = Force::fromSi(forceNorm);

    if (!(out.solution.residual.normalized <= settings.relativeResidualTolerance)) {
        return fail(SolveProblem::ResidualTooLarge,
                    makeError(ErrorCode::FailedPrecondition,
                              std::format("the independent residual is too large: "
                                          "||r||_2 = {} N, ||r||_inf = {} N, normalized = {} "
                                          "against a threshold of {}. The solver reported "
                                          "success; BetterCAD does not",
                                          euclidean, infinity,
                                          out.solution.residual.normalized,
                                          settings.relativeResidualTolerance))
                        .error());
    }
    return out;
}

} // namespace

Result<SymmetricSolution> solveSymmetricSparse(std::size_t rows,
                                               std::span<const StiffnessMatrix::Index> rowStart,
                                               std::span<const StiffnessMatrix::Index> inner,
                                               std::span<const double> values,
                                               std::span<const double> rhs,
                                               const SolverSettings& settings) {
    Solved solved = runKernel(rows, rowStart, inner, values, rhs, settings);
    if (solved.problem.has_value()) {
        return std::unexpected(std::move(solved.error));
    }
    return std::move(solved.solution);
}

std::optional<SolveProblem> symmetricSolveProblem(std::size_t rows,
                                                  std::span<const StiffnessMatrix::Index> rowStart,
                                                  std::span<const StiffnessMatrix::Index> inner,
                                                  std::span<const double> values,
                                                  std::span<const double> rhs,
                                                  const SolverSettings& settings) {
    return runKernel(rows, rowStart, inner, values, rhs, settings).problem;
}

// ---------------------------------------------------------------------------
// The structural solve
// ---------------------------------------------------------------------------

Length SolvedSystem::displacementOf(DofIndex index) const noexcept {
    if (!index.isValid() || index.value() > displacement_.size()) {
        return Length{};
    }
    return Length::fromSi(displacement_[static_cast<std::size_t>(index.value() - 1)]);
}

namespace {

/// The working state of one structural solve. A `SolvedSystem` has one friend
/// and this helper is not it, so the parts are carried here and the public
/// entry point assembles them -- which also means nothing can hand out a
/// partially filled solution.
struct Prepared {
    std::optional<SolveProblem> problem{};
    Error error{};
    std::size_t freeEquations = 0;
    std::size_t freeNonZeros = 0;
    double pivotRatio = 0.0;
    Energy strainEnergy{};
    ResidualMetrics residual{};
    std::vector<double> displacement{};
    std::vector<double> fullResidual{};
};

[[nodiscard]] Prepared failStructural(SolveProblem problem, Error error) {
    return Prepared{.problem = problem, .error = std::move(error)};
}

[[nodiscard]] Prepared runStructural(const GlobalStructuralSystem& system,
                                     const ConstraintSet& constraints,
                                     const SolverSettings& settings) {
    if (Result<void> checked = validate(settings); !checked.has_value()) {
        return failStructural(SolveProblem::InvalidSettings, checked.error());
    }

    // THE FREE NUMBERING IS P17-DOF'S, NOT THIS MILESTONE'S. buildFreeEquationMap
    // refuses a numbering and a constraint set built against different meshes,
    // which is the check that makes an M1/M2 mix impossible -- and it cannot
    // be forgotten, because there is no other way to obtain a FreeEquationMap.
    Result<FreeEquationMap> equations = buildFreeEquationMap(system.numbering(), constraints);
    if (!equations.has_value()) {
        return failStructural(SolveProblem::SourceMismatch, equations.error());
    }

    Result<FreeSystem> reduced = extractFreeSystem(system, *equations);
    if (!reduced.has_value()) {
        return failStructural(SolveProblem::SourceMismatch, reduced.error());
    }

    Solved solved = runKernel(reduced->rows(), reduced->rowStart(), reduced->innerIndices(),
                              reduced->values(), reduced->force(), settings);
    if (solved.problem.has_value()) {
        return failStructural(*solved.problem, std::move(solved.error));
    }

    // RECONSTRUCTION. Free rows take their solved value; constrained rows stay
    // EXACTLY zero, because they were never unknowns and no factorisation
    // noise can reach them. The vector is in global DofIndex order, never in
    // the solver's internal AMD permutation.
    Prepared out;
    out.freeEquations = reduced->rows();
    out.freeNonZeros = reduced->nonZeros();
    out.pivotRatio = solved.solution.pivotRatio;
    out.residual = solved.solution.residual;
    out.displacement.assign(system.degreesOfFreedom(), 0.0);

    const std::span<const DofIndex> freeDofs = equations->freeDofs();
    for (std::size_t equation = 0; equation < freeDofs.size(); ++equation) {
        const auto row = static_cast<std::size_t>(freeDofs[equation].value() - 1);
        out.displacement[row] = solved.solution.x[equation];
    }

    // THE FULL RESIDUAL, `K u - F`. Free entries come out ~0; CONSTRAINED
    // ENTRIES ARE THE SUPPORT REACTIONS and are correctly non-zero. Retained
    // and not aggregated: P17-REACTION-001 owns reaction semantics, and
    // discarding this now would make it recompute what is already known.
    //
    // AND THE STRAIN ENERGY WITH IT, from the same pass: `U = (1/2) u^T K u`.
    const StiffnessMatrix& stiffness = system.stiffness();
    const std::span<const StiffnessMatrix::Index> rowStart = stiffness.rowStart();
    const std::span<const StiffnessMatrix::Index> inner = stiffness.innerIndices();
    const std::span<const double> values = stiffness.values();
    out.fullResidual.assign(system.degreesOfFreedom(), 0.0);
    double twiceEnergy = 0.0;
    for (std::size_t row = 0; row < system.degreesOfFreedom(); ++row) {
        double product = 0.0;
        for (StiffnessMatrix::Index slot = rowStart[row]; slot < rowStart[row + 1]; ++slot) {
            product += values[slot] * out.displacement[static_cast<std::size_t>(inner[slot])];
        }
        twiceEnergy += out.displacement[row] * product;
        const double entry = product - system.force().values()[row];
        if (!std::isfinite(entry)) {
            return failStructural(SolveProblem::NonFiniteResidual,
                                  makeError(ErrorCode::FailedPrecondition,
                                            std::format("full residual component {} is not "
                                                        "finite",
                                                        row))
                                      .error());
        }
        out.fullResidual[row] = entry;
    }
    if (!std::isfinite(twiceEnergy)) {
        return failStructural(SolveProblem::NonFiniteSolution,
                              makeError(ErrorCode::FailedPrecondition,
                                        "the strain energy is not finite")
                                  .error());
    }
    out.strainEnergy = Energy::fromSi(0.5 * twiceEnergy);
    return out;
}

} // namespace

Result<SolvedSystem> solveStructuralSystem(const GlobalStructuralSystem& system,
                                           const ConstraintSet& constraints,
                                           const SolverSettings& settings) {
    Prepared prepared = runStructural(system, constraints, settings);
    if (prepared.problem.has_value()) {
        return std::unexpected(std::move(prepared.error));
    }
    // Published only here, through the one private constructor: nothing above
    // has touched a SolvedSystem, so a refused solve cannot leave a partial
    // displacement field behind.
    return SolvedSystem{system.source(),          settings,
                        system.mesh(),            prepared.freeEquations,
                        prepared.freeNonZeros,    prepared.pivotRatio,
                        prepared.strainEnergy,    prepared.residual,
                        std::move(prepared.displacement),
                        std::move(prepared.fullResidual)};
}

std::optional<SolveProblem> structuralSolveProblem(const GlobalStructuralSystem& system,
                                                   const ConstraintSet& constraints,
                                                   const SolverSettings& settings) {
    return runStructural(system, constraints, settings).problem;
}

} // namespace bettercad::structural
