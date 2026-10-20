// The linear static solve (P17-SOLVE-001).
//
// THE FACTORISATION NOW LIVES IN `linalg` (ADR-052). It was here, using Eigen
// privately under ADR-039, and it moved BELOW layer 45 so that `thermal` can call
// the same qualified kernel: this file is now P17's reduction, reconstruction and
// wording around it. `StructuralPost.cpp` is still the module's one direct Eigen
// consumer (P17-POST-001, for symmetric 3x3 eigenvalues).
//
// WHAT THE KERNEL DOES, AND WHAT IT DOES NOT. It factorises and it
// back-substitutes. It does NOT decide whether the system was solvable and it
// does NOT compute the residual:
//
//     the pivot gate   measures min|D|/max|D| itself, because SimplicialLDLT
//                      reports NumericalIssue only on an EXACTLY zero pivot and
//                      a singular system almost never produces one
//     the residual     is a loop over BetterCAD's own CSR copy of Kff against
//                      BetterCAD's own Ff, after the solve returned.
//                      solver.error() is never consulted
//
// NO PENALTY, NO PINNING, NO REGULARISATION. Searched in the evidence: there
// is no 1e12, no 1e20, no epsilon added to a diagonal and no node fixed to
// make a free body solvable. An under-constrained model fails.
#include <bettercad/structural/StructuralSolve.hpp>

#include <bettercad/linalg/SymmetricSolve.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <utility>

namespace bettercad::structural {

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
//
// THE KERNEL MOVED BELOW LAYER 45 (ADR-052). The factorisation, the pivot gate
// and the independent residual are now `linalg::solveSymmetricSparse`, which
// `thermal` calls too. What stays HERE is exactly what is structural: P17's
// units on the numbers, and P17's wording of the diagnostics -- rebuilt below
// from the kernel's structured failure, byte for byte, so that the qualified
// messages and the qualified tests are unchanged.

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

[[nodiscard]] SolveProblem structuralProblem(linalg::SymmetricSolveProblem problem) noexcept {
    switch (problem) {
    case linalg::SymmetricSolveProblem::InvalidSettings:
        return SolveProblem::InvalidSettings;
    case linalg::SymmetricSolveProblem::DimensionMismatch:
        return SolveProblem::DimensionMismatch;
    case linalg::SymmetricSolveProblem::NonFiniteSystem:
        return SolveProblem::NonFiniteSystem;
    case linalg::SymmetricSolveProblem::FactorizationFailure:
        return SolveProblem::FactorizationFailure;
    case linalg::SymmetricSolveProblem::SingularSystem:
        return SolveProblem::SingularSystem;
    case linalg::SymmetricSolveProblem::NonFiniteSolution:
        return SolveProblem::NonFiniteSolution;
    case linalg::SymmetricSolveProblem::NonFiniteResidual:
        return SolveProblem::NonFiniteResidual;
    case linalg::SymmetricSolveProblem::ResidualTooLarge:
        return SolveProblem::ResidualTooLarge;
    }
    return SolveProblem::FactorizationFailure;
}

/// P17's diagnostics, in P17's words. Only the three that mention a structural
/// concept or a unit are rebuilt; the rest are the kernel's own text, which was
/// moved unchanged.
[[nodiscard]] Error structuralError(const linalg::SymmetricSolveFailure& failure, const SolverSettings& settings) {
    switch (failure.problem) {
    case linalg::SymmetricSolveProblem::SingularSystem:
        if (failure.exactlyZeroPivot) {
            return makeError(ErrorCode::FailedPrecondition,
                             std::format("the system is singular: the factorisation of a {} x {} system found an "
                                         "exactly zero pivot. The model may be under-constrained, disconnected or "
                                         "a mechanism -- this layer measures the conditioning and does not claim "
                                         "to know which",
                                         failure.rows, failure.rows))
                .error();
        }
        if (failure.nonFinitePivot) {
            return failure.error;
        }
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("the system is singular or too ill conditioned to solve: the factorisation's "
                                     "smallest pivot is {} of its largest, below the floor of {}, and {}. The model "
                                     "may be under-constrained, disconnected or a mechanism -- this layer measures "
                                     "the conditioning and does not claim to know which",
                                     failure.pivotRatio, settings.pivotFloor,
                                     failure.allPivotsPositive ? "every pivot is positive"
                                                               : "at least one pivot is not positive"))
            .error();
    case linalg::SymmetricSolveProblem::ResidualTooLarge:
        if (failure.residual.solution == 0.0 && failure.residual.rightHandSide == 0.0) {
            return makeError(ErrorCode::Internal,
                             std::format("the system has no scale -- the solution and the right-hand side are both "
                                         "zero -- and yet the residual is {} N",
                                         failure.residual.euclidean))
                .error();
        }
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("the independent residual is too large: ||r||_2 = {} N, ||r||_inf = {} N, "
                                     "normalized = {} against a threshold of {}. The solver reported success; "
                                     "BetterCAD does not",
                                     failure.residual.euclidean, failure.residual.infinity,
                                     failure.residual.normalized, settings.relativeResidualTolerance))
            .error();
    default:
        return failure.error;
    }
}

[[nodiscard]] Solved runKernel(std::size_t rows, std::span<const StiffnessMatrix::Index> rowStart,
                               std::span<const StiffnessMatrix::Index> inner, std::span<const double> values,
                               std::span<const double> rhs, const SolverSettings& settings) {
    if (Result<void> checked = validate(settings); !checked.has_value()) {
        return fail(SolveProblem::InvalidSettings, checked.error());
    }

    const linalg::SymmetricSolveSettings generic{.relativeResidualTolerance = settings.relativeResidualTolerance,
                                                 .pivotFloor = settings.pivotFloor};
    std::expected<linalg::SymmetricSolution, linalg::SymmetricSolveFailure> solved =
        linalg::solveSymmetricSparse(rows, rowStart, inner, values, rhs, generic);
    if (!solved.has_value()) {
        return fail(structuralProblem(solved.error().problem), structuralError(solved.error(), settings));
    }

    Solved out;
    out.solution.x = std::move(solved->x);
    out.solution.pivotRatio = solved->pivotRatio;
    const linalg::ResidualNorms& norms = solved->residual;
    out.solution.residual.euclideanNorm = Force::fromSi(norms.euclidean);
    out.solution.residual.infinityNorm = Force::fromSi(norms.infinity);
    out.solution.residual.stiffnessInfinityNorm = Stiffness::fromSi(norms.matrixInfinity);
    out.solution.residual.displacementNorm = Length::fromSi(norms.solution);
    out.solution.residual.forceNorm = Force::fromSi(norms.rightHandSide);
    out.solution.residual.normalized = norms.normalized;
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
