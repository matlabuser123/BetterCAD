// P19-LINSOLVE-001 — the CFD linear solve. ADR-076.
//
// NO FACTORISATION IS WRITTEN IN THIS FILE, and no conditioning gate and no
// residual. `linalg::solveSymmetricSparse` is P17's qualified kernel, moved
// below layer 45 by `ADR-052` and used by `structural` and `thermal` already;
// `linalg::solveSparse` is this milestone's general kernel beside it. What is
// here is CFD's half: the routing, the symmetry precondition in CFD's own
// words, and the translation of a structured refusal into CFD's vocabulary.
// That is the same division `src/structural/StructuralSolve.cpp` draws -- "What
// stays HERE is exactly what is structural: P17's units on the numbers, and
// P17's wording of the diagnostics".

#include <bettercad/cfd/CfdLinearSolve.hpp>

#include <bettercad/linalg/SparseSolve.hpp>
#include <bettercad/linalg/SymmetricSolve.hpp>

#include <cmath>
#include <format>
#include <type_traits>
#include <utility>

namespace bettercad::cfd {

// THE ASSERT THIS MILESTONE WAS OWED BY NAME. `src/cfd/CMakeLists.txt` recorded
// it under P19-DIFF-001: "CfdSparseIndex is cfd's own uint64_t alias rather than
// linalg::SparseIndex, same width and signedness by construction so a later
// solve consumes these spans without translation, and the static_assert that
// turns 'same by construction' into 'same, enforced' belongs to the milestone
// that may legitimately include that header." This is that milestone, and this
// is the first translation unit in `cfd` that may include it.
//
// It sits in the .cpp rather than a public header because `BetterCAD::linalg`
// is linked PRIVATE: a consumer of `BetterCAD::cfd` does not inherit the link,
// so a linalg type named in a cfd public header would not compile for them.
static_assert(std::is_same_v<CfdSparseIndex, linalg::SparseIndex>,
              "CfdSparseIndex and linalg::SparseIndex must be the same type, so that a CFD matrix's spans reach "
              "the kernel without translation or copy");
static_assert(sizeof(CfdSparseIndex) == sizeof(linalg::SparseIndex));
static_assert(std::is_unsigned_v<CfdSparseIndex> && std::is_unsigned_v<linalg::SparseIndex>);

namespace {

using Failure = CfdLinearSolveFailure;

[[nodiscard]] Failure fail(CfdLinearSolveProblem problem, Error error, std::size_t rows = 0) {
    Failure out;
    out.problem = problem;
    out.error = std::move(error);
    out.rows = rows;
    return out;
}

/// The checks that belong to CFD, before either kernel is reached.
///
/// THE MATRIX'S ENTRIES ARE NOT CHECKED FOR FINITENESS HERE, and their absence
/// is deliberate. `CfdSparseMatrix::fromCsr` is the only producer of the type
/// and it already refuses `CfdSparseProblem::NonFiniteEntry`, so a loop over
/// the values could not fire for any input a caller can construct.
/// `CfdBoundary.hpp` states this project's rule for the same shape of problem:
/// "An enumerator no input can produce is worse than no enumerator", and a
/// guard no input can fire is that rule's other face. `ValueNotFinite` remains
/// reachable, and the right-hand side is what reaches it: it arrives as a raw
/// span with no gate of its own.
[[nodiscard]] std::optional<Failure> checkSystem(const CfdSparseMatrix& matrix, std::span<const double> rightHandSide,
                                                 const CfdLinearSolverSettings& settings, bool requireSymmetry) {
    if (Result<void> valid = validate(settings); !valid.has_value()) {
        return fail(CfdLinearSolveProblem::SettingsInvalid, valid.error(), matrix.rows());
    }
    if (matrix.rows() != matrix.columns()) {
        return fail(CfdLinearSolveProblem::MatrixIsNotSquare,
                    makeError(ErrorCode::InvalidArgument,
                              std::format("a CFD system has one unknown per cell, so its matrix must be square, "
                                          "not {} by {}",
                                          matrix.rows(), matrix.columns()))
                        .error(),
                    matrix.rows());
    }
    if (rightHandSide.size() != matrix.rows()) {
        return fail(CfdLinearSolveProblem::RightHandSideLengthWrong,
                    makeError(ErrorCode::InvalidArgument,
                              std::format("a {} row system needs {} right-hand side entries, not {}", matrix.rows(),
                                          matrix.rows(), rightHandSide.size()))
                        .error(),
                    matrix.rows());
    }
    for (std::size_t i = 0; i < rightHandSide.size(); ++i) {
        if (!std::isfinite(rightHandSide[i])) {
            Failure out = fail(CfdLinearSolveProblem::ValueNotFinite,
                               makeError(ErrorCode::InvalidArgument,
                                         std::format("right-hand side entry {} is not finite", i))
                                   .error(),
                               matrix.rows());
            out.index = i;
            return out;
        }
    }
    if (requireSymmetry) {
        const double asymmetry = matrix.largestSymmetryError();
        if (asymmetry != 0.0) {
            Failure out =
                fail(CfdLinearSolveProblem::MatrixIsNotSymmetric,
                     makeError(ErrorCode::InvalidArgument,
                               std::format("the pressure system must be symmetric to the last bit and its largest "
                                           "departure is {}; the assembly writes one face coefficient to both "
                                           "off-diagonals in a single visit, so this is a defective assembly "
                                           "rather than a hard problem",
                                           asymmetry))
                         .error(),
                     matrix.rows());
            out.largestSymmetryError = asymmetry;
            return out;
        }
    }
    return std::nullopt;
}

/// The LDLT kernel's classification, said in CFD's words.
[[nodiscard]] CfdLinearSolveProblem translate(linalg::SymmetricSolveProblem problem) noexcept {
    switch (problem) {
    case linalg::SymmetricSolveProblem::InvalidSettings:
        return CfdLinearSolveProblem::SettingsInvalid;
    case linalg::SymmetricSolveProblem::DimensionMismatch:
        return CfdLinearSolveProblem::MatrixIsNotSquare;
    case linalg::SymmetricSolveProblem::NonFiniteSystem:
        return CfdLinearSolveProblem::ValueNotFinite;
    case linalg::SymmetricSolveProblem::SingularSystem:
        return CfdLinearSolveProblem::SystemIsSingular;
    case linalg::SymmetricSolveProblem::NonFiniteSolution:
        return CfdLinearSolveProblem::SolutionIsNotFinite;
    case linalg::SymmetricSolveProblem::NonFiniteResidual:
        return CfdLinearSolveProblem::ResidualIsNotFinite;
    case linalg::SymmetricSolveProblem::ResidualTooLarge:
        return CfdLinearSolveProblem::ResidualTooLarge;
    case linalg::SymmetricSolveProblem::FactorizationFailure:
        break;
    }
    return CfdLinearSolveProblem::FactorizationFailed;
}

/// The QR kernel's classification, said in CFD's words.
[[nodiscard]] CfdLinearSolveProblem translate(linalg::SparseSolveProblem problem) noexcept {
    switch (problem) {
    case linalg::SparseSolveProblem::InvalidSettings:
        return CfdLinearSolveProblem::SettingsInvalid;
    case linalg::SparseSolveProblem::DimensionMismatch:
        return CfdLinearSolveProblem::MatrixIsNotSquare;
    case linalg::SparseSolveProblem::NonFiniteSystem:
        return CfdLinearSolveProblem::ValueNotFinite;
    case linalg::SparseSolveProblem::SingularSystem:
        return CfdLinearSolveProblem::SystemIsSingular;
    case linalg::SparseSolveProblem::NonFiniteSolution:
        return CfdLinearSolveProblem::SolutionIsNotFinite;
    case linalg::SparseSolveProblem::NonFiniteResidual:
        return CfdLinearSolveProblem::ResidualIsNotFinite;
    case linalg::SparseSolveProblem::ResidualTooLarge:
        return CfdLinearSolveProblem::ResidualTooLarge;
    case linalg::SparseSolveProblem::FactorizationFailure:
        break;
    }
    return CfdLinearSolveProblem::FactorizationFailed;
}

/// ONE RATIO CROSSES THE BOUNDARY, AND THE FLOORS DO NOT (ADR-076 Decision 8).
/// `ADR-058` says of `CfdLinearSolverSettings`: "ONE RATIO: the normalised
/// residual of the solved system, computed independently of the solver ... Which
/// solver is used is P19-LINSOLVE-001's decision and not a field." So the
/// conditioning floors come from the kernels' own defaults, and CFD does not
/// grow a field for them. `ADR-076` Q5' is what makes that safe on the pressure
/// path: the LDLT's pivot ratio is `0.25` for the pressure shape across
/// seventeen orders of magnitude of face coefficient, because the pin keeps its
/// own diagonal.
[[nodiscard]] linalg::SymmetricSolveSettings symmetricSettings(const CfdLinearSolverSettings& settings) {
    return linalg::SymmetricSolveSettings{.relativeResidualTolerance = settings.relativeResidualTolerance,
                                          .pivotFloor = linalg::SymmetricSolveSettings{}.pivotFloor};
}

[[nodiscard]] linalg::SparseSolveSettings generalSettings(const CfdLinearSolverSettings& settings) {
    return linalg::SparseSolveSettings{.relativeResidualTolerance = settings.relativeResidualTolerance,
                                       .rankRatioFloor = linalg::SparseSolveSettings{}.rankRatioFloor};
}

[[nodiscard]] std::expected<CfdLinearSolution, Failure> runSymmetric(const CfdSparseMatrix& matrix,
                                                                     std::span<const double> rightHandSide,
                                                                     const CfdLinearSolverSettings& settings) {
    std::expected<linalg::SymmetricSolution, linalg::SymmetricSolveFailure> solved =
        linalg::solveSymmetricSparse(matrix.rows(), matrix.rowStart(), matrix.inner(), matrix.values(), rightHandSide,
                                     symmetricSettings(settings));
    if (!solved.has_value()) {
        // A SINGULARITY THE LDLT BLAMED ON A NON-POSITIVE PIVOT IS NOT A
        // SINGULARITY, and saying so is the one place this layer improves on the
        // kernel's own wording. `allPivotsPositive` is a structured fact the
        // kernel carries precisely so a caller can say it in its own words.
        const bool indefinite = solved.error().problem == linalg::SymmetricSolveProblem::SingularSystem &&
                                !solved.error().allPivotsPositive && !solved.error().exactlyZeroPivot;
        Failure out = fail(indefinite ? CfdLinearSolveProblem::MatrixIsNotPositiveDefinite
                                      : translate(solved.error().problem),
                           indefinite
                               ? makeError(ErrorCode::FailedPrecondition,
                                           std::format("the matrix is symmetric but not positive definite: the "
                                                       "factorisation of a {} x {} system produced a pivot that is "
                                                       "not positive, so this kernel cannot take it. The system may "
                                                       "still be solvable -- the general kernel accepts it -- and "
                                                       "the pressure correction is positive definite once its level "
                                                       "is fixed, so this is a defective assembly or a matrix that "
                                                       "does not belong on this path",
                                                       matrix.rows(), matrix.rows()))
                                     .error()
                               : solved.error().error,
                           matrix.rows());
        out.conditioningRatio = solved.error().pivotRatio;
        out.normalizedResidual = solved.error().residual.normalized;
        out.index = solved.error().index;
        out.symmetricKernel = true;
        return std::unexpected(std::move(out));
    }
    CfdLinearSolution out;
    out.values = std::move(solved->x);
    out.normalizedResidual = solved->residual.normalized;
    out.conditioningRatio = solved->pivotRatio;
    out.reportedRank = 0;
    out.symmetricKernel = true;
    return out;
}

[[nodiscard]] std::expected<CfdLinearSolution, Failure> runGeneral(const CfdSparseMatrix& matrix,
                                                                   std::span<const double> rightHandSide,
                                                                   const CfdLinearSolverSettings& settings) {
    std::expected<linalg::SparseSolution, linalg::SparseSolveFailure> solved =
        linalg::solveSparse(matrix.rows(), matrix.rowStart(), matrix.inner(), matrix.values(), rightHandSide,
                            generalSettings(settings));
    if (!solved.has_value()) {
        Failure out = fail(translate(solved.error().problem), solved.error().error, matrix.rows());
        out.conditioningRatio = solved.error().rankRatio;
        out.normalizedResidual = solved.error().residual.normalized;
        out.index = solved.error().index;
        out.symmetricKernel = false;
        return std::unexpected(std::move(out));
    }
    CfdLinearSolution out;
    out.values = std::move(solved->x);
    out.normalizedResidual = solved->residual.normalized;
    out.conditioningRatio = solved->rankRatio;
    out.reportedRank = solved->reportedRank;
    out.symmetricKernel = false;
    return out;
}

} // namespace

std::string_view toString(CfdLinearSolveProblem problem) noexcept {
    switch (problem) {
    case CfdLinearSolveProblem::SettingsInvalid:
        return "settings_invalid";
    case CfdLinearSolveProblem::MatrixIsNotSquare:
        return "matrix_is_not_square";
    case CfdLinearSolveProblem::RightHandSideLengthWrong:
        return "right_hand_side_length_wrong";
    case CfdLinearSolveProblem::ValueNotFinite:
        return "value_not_finite";
    case CfdLinearSolveProblem::MatrixIsNotSymmetric:
        return "matrix_is_not_symmetric";
    case CfdLinearSolveProblem::FactorizationFailed:
        return "factorization_failed";
    case CfdLinearSolveProblem::SystemIsSingular:
        return "system_is_singular";
    case CfdLinearSolveProblem::MatrixIsNotPositiveDefinite:
        return "matrix_is_not_positive_definite";
    case CfdLinearSolveProblem::SolutionIsNotFinite:
        return "solution_is_not_finite";
    case CfdLinearSolveProblem::ResidualIsNotFinite:
        return "residual_is_not_finite";
    case CfdLinearSolveProblem::ResidualTooLarge:
        return "residual_too_large";
    }
    return "unknown";
}

std::expected<CfdLinearSolution, CfdLinearSolveFailure>
solveCfdPressureSystem(const CfdSparseMatrix& matrix, std::span<const double> rightHandSide,
                       const CfdLinearSolverSettings& settings) {
    if (std::optional<Failure> bad = checkSystem(matrix, rightHandSide, settings, true); bad.has_value()) {
        return std::unexpected(std::move(*bad));
    }
    return runSymmetric(matrix, rightHandSide, settings);
}

std::expected<CfdLinearSolution, CfdLinearSolveFailure>
solveCfdMomentumSystem(const CfdSparseMatrix& matrix, std::span<const double> rightHandSide,
                       const CfdLinearSolverSettings& settings) {
    if (std::optional<Failure> bad = checkSystem(matrix, rightHandSide, settings, false); bad.has_value()) {
        return std::unexpected(std::move(*bad));
    }
    return runGeneral(matrix, rightHandSide, settings);
}

std::optional<CfdLinearSolveProblem> cfdLinearSolveProblem(const CfdSparseMatrix& matrix,
                                                           std::span<const double> rightHandSide,
                                                           const CfdLinearSolverSettings& settings,
                                                           bool requireSymmetry) {
    if (std::optional<Failure> bad = checkSystem(matrix, rightHandSide, settings, requireSymmetry); bad.has_value()) {
        return bad->problem;
    }
    if (requireSymmetry) {
        std::expected<CfdLinearSolution, Failure> solved = runSymmetric(matrix, rightHandSide, settings);
        if (!solved.has_value()) {
            return solved.error().problem;
        }
        return std::nullopt;
    }
    std::expected<CfdLinearSolution, Failure> solved = runGeneral(matrix, rightHandSide, settings);
    if (!solved.has_value()) {
        return solved.error().problem;
    }
    return std::nullopt;
}

CfdLinearSolve cfdLinearSolve() {
    return [](const CfdSparseMatrix& matrix, std::span<const double> rightHandSide,
              const CfdLinearSolverSettings& settings) -> std::expected<CfdLinearSolveResult, Error> {
        // EXACTLY SYMMETRIC TAKES THE LDLT, ANYTHING ELSE TAKES THE QR. The
        // test is exact rather than toleranced: the pressure assembly
        // guarantees `largestSymmetryError() == 0.0` to the last bit and a test
        // asserts it, so there is no threshold to tune and no ambiguity near
        // one. The consequence is stated in the header: the kernel is a
        // function of the matrix.
        const bool symmetric = matrix.rows() == matrix.columns() && matrix.largestSymmetryError() == 0.0;
        std::expected<CfdLinearSolution, CfdLinearSolveFailure> solved =
            symmetric ? solveCfdPressureSystem(matrix, rightHandSide, settings)
                      : solveCfdMomentumSystem(matrix, rightHandSide, settings);
        if (!solved.has_value()) {
            return std::unexpected(solved.error().error);
        }
        return CfdLinearSolveResult{.solution = std::move(solved->values),
                                    .normalizedResidual = solved->normalizedResidual};
    };
}

} // namespace bettercad::cfd
