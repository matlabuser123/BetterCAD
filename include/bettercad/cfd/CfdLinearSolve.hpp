#pragma once

// P19-LINSOLVE-001 — CFD Linear Solver Infrastructure. ADR-076.
//
// THIS MODULE SUPPLIES NO NUMERICAL IDEA OF ITS OWN. It routes, it translates
// vocabulary, and it refuses. The factorisations, the conditioning gates and
// the independent residual are `BetterCAD::linalg` at layer 5 (`ADR-052`),
// linked PRIVATE, which `src/cfd/CMakeLists.txt` named as this milestone's plan
// before this milestone began: "the SOLVE -- still P19-LINSOLVE-001's decision,
// unchanged. The generic kernel it would reuse is BetterCAD::linalg (layer 5,
// ADR-052), linked PRIVATE, never an include of structural or thermal and never
// a hand copy."
//
// NO LINALG TYPE APPEARS BELOW, AND THAT IS WHAT PRIVATE LINKING MEANS. A
// consumer of `BetterCAD::cfd` does not inherit the link, so a linalg type in
// this header would not compile for them. Everything here is CFD's own
// vocabulary, exactly as `structural` and `thermal` wrap the same kernel in
// theirs.
//
// TWO SYSTEMS, TWO KERNELS, AND THE CHOICE IS NOT A MATTER OF OPINION.
//
// ```text
//   pressure correction   SYMMETRIC by construction, one face visit writes
//                         both off-diagonals (ADR-075 Q1)  -> the LDLT
//   momentum              ASYMMETRIC and correct: upwind convection never
//                         pairs into a symmetric entry (ADR-072 Decision 3)
//                         -> the general QR
// ```
//
// Handing a momentum system to the symmetric kernel is not silently wrong, and
// that was MEASURED rather than assumed: on a well-posed convection-diffusion
// chain with diffusion `1e-6`, an asymmetry of `1e-12` is already refused as
// `residual_too_large` and anything from `1e-7` up as `singular_system`. The
// kernel reads one triangle, the independent residual is computed from the
// ORIGINAL arrays, and the mismatch surfaces there. The refusal is nonetheless
// in the WRONG WORDS for a structural mistake, which is why the pressure entry
// point refuses asymmetry in CFD's own vocabulary first.
//
// THE PORT ROUTES ON EXACT SYMMETRY, NOT ON A TOLERANCE (ADR-076 Decision 7).
// `cfdLinearSolve` returns a `CfdLinearSolve` for `P19-PVCOUPLE-001`, which
// takes ONE port and uses it for both the three momentum components and the
// pressure correction, so the port must decide from the matrix alone -- its own
// header says "a port must accept both and a port that assumes symmetry
// everywhere is wrong". The decision is `largestSymmetryError() == 0.0`,
// exactly, because the pressure assembly guarantees that to the last bit and a
// test asserts it. An exact test has no threshold to tune and no ambiguity near
// one.
//
// THE CONSEQUENCE IS STATED RATHER THAN HIDDEN: the kernel is a function of the
// matrix, so a momentum system whose mass flux is EXACTLY zero is exactly
// symmetric and takes the LDLT. That is the right kernel for it -- at zero flux
// the operator is the diffusion operator and symmetric positive definite -- but
// it means the two paths' answers differ in the last bits, so nothing here or
// downstream may assert bitwise equality ACROSS the two kernels.

#include <bettercad/cfd/CfdCoupling.hpp>
#include <bettercad/cfd/CfdData.hpp>
#include <bettercad/cfd/CfdSparse.hpp>
#include <bettercad/cfd/Export.hpp>
#include <bettercad/core/Error.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace bettercad::cfd {

/// Why a CFD linear solve is refused.
///
/// IN THE ORDER THE PHASES CHECK THEM, and the order is asserted by a test, as
/// `CfdCouplingProblem`'s is.
enum class CfdLinearSolveProblem : std::uint8_t {
    /// `CfdLinearSolverSettings` is not usable.
    SettingsInvalid,
    /// The matrix is not square. A CFD system has one unknown per cell.
    MatrixIsNotSquare,
    /// The right-hand side does not have one entry per row. No implicit
    /// resizing.
    RightHandSideLengthWrong,
    /// A matrix entry or a right-hand side entry is not finite.
    ValueNotFinite,
    /// The pressure entry point was handed a matrix that is not symmetric.
    /// Refused HERE, in CFD's words, rather than left to surface as a large
    /// residual from a kernel that reads one triangle.
    MatrixIsNotSymmetric,
    /// The factorisation failed for a reason that is not a detected
    /// singularity.
    FactorizationFailed,
    /// The conditioning measure fell below its floor, or the factorisation
    /// found fewer independent columns than rows.
    SystemIsSingular,
    /// The pressure path's matrix is symmetric but NOT positive definite, so the
    /// LDLT cannot factorise it even though the system may be perfectly
    /// solvable.
    ///
    /// THIS ENUMERATOR EXISTS BECAUSE THE PORT ROUTES ON A CONDITION WEAKER THAN
    /// THE KERNEL'S PRECONDITION, and an adversarial review found it. Exact
    /// symmetry is strictly weaker than symmetric-positive-definite, which is
    /// what `CfdCoupling.hpp` names as the real requirement: "once the pressure
    /// level is fixed, positive definite -- which is exactly what
    /// `linalg::solveSymmetricSparse` requires". Two measured counter-examples,
    /// both exactly symmetric and both trivially solvable:
    ///
    /// ```text
    ///   [[-2]] x = [4]          x = -2       the LDLT refuses it
    ///   [[1,2],[2,1]] x = [3,3] x = [1,1]    the LDLT refuses it
    /// ```
    ///
    /// The LDLT's own refusal is `SingularSystem` with a message reporting a
    /// pivot ratio "below the floor of 1e-12" when the ratio is 1 and 0.333 --
    /// self-contradictory, because that message is formatted whenever the pivot
    /// gate fires for either of its two reasons. CFD cannot change a qualified
    /// kernel's wording, but it can tell the difference: the failure carries
    /// `allPivotsPositive`, so this path reports what actually happened instead
    /// of passing on a false singularity.
    MatrixIsNotPositiveDefinite,
    /// The solver returned a component that is not finite although it reported
    /// success.
    SolutionIsNotFinite,
    /// The independently computed residual is not finite.
    ResidualIsNotFinite,
    /// The independently computed normalised residual exceeds
    /// `CfdLinearSolverSettings::relativeResidualTolerance`.
    ResidualTooLarge,
};

/// Every enumerator exactly once, for exhaustiveness and ordering tests.
inline constexpr std::array<CfdLinearSolveProblem, 11> kCfdLinearSolveProblems{
    CfdLinearSolveProblem::SettingsInvalid,
    CfdLinearSolveProblem::MatrixIsNotSquare,
    CfdLinearSolveProblem::RightHandSideLengthWrong,
    CfdLinearSolveProblem::ValueNotFinite,
    CfdLinearSolveProblem::MatrixIsNotSymmetric,
    CfdLinearSolveProblem::FactorizationFailed,
    CfdLinearSolveProblem::SystemIsSingular,
    CfdLinearSolveProblem::MatrixIsNotPositiveDefinite,
    CfdLinearSolveProblem::SolutionIsNotFinite,
    CfdLinearSolveProblem::ResidualIsNotFinite,
    CfdLinearSolveProblem::ResidualTooLarge,
};

static_assert(kCfdLinearSolveProblems.size() == 11,
              "kCfdLinearSolveProblems must hold every CfdLinearSolveProblem exactly once");

[[nodiscard]] BETTERCAD_CFD_EXPORT std::string_view toString(CfdLinearSolveProblem problem) noexcept;

/// A refusal with its classification and the numbers behind it.
struct CfdLinearSolveFailure {
    CfdLinearSolveProblem problem = CfdLinearSolveProblem::FactorizationFailed;
    Error error{};
    /// The kernel's dimensionless conditioning measure: `min|D|/max|D|` of the
    /// LDLT's diagonal on the symmetric path, `min|R_ii|/max|R_ii|` of the QR's
    /// factor on the general one. Zero when the factorisation did not complete.
    double conditioningRatio = 0.0;
    /// The normalised residual, when one was measured.
    double normalizedResidual = 0.0;
    /// `max |A(i,j) - A(j,i)|`, for `MatrixIsNotSymmetric`.
    double largestSymmetryError = 0.0;
    /// The index of the offending entry, for the component-wise problems.
    std::size_t index = 0;
    /// The system size.
    std::size_t rows = 0;
    /// Which kernel ran, so a diagnostic can say which conditioning measure
    /// `conditioningRatio` is. False when no factorisation was attempted.
    bool symmetricKernel = false;
};

/// One solved CFD linear system.
struct CfdLinearSolution {
    /// `x`, one entry per row, in SI units of whatever the system was in.
    std::vector<double> values{};
    /// The normalised residual, computed by the kernel from the ORIGINAL arrays
    /// after the solve. `P19-PVCOUPLE-001` records as its `L4` that it cannot
    /// detect a port reporting a residual it did not measure; this one measures
    /// it.
    double normalizedResidual = 0.0;
    /// The kernel's dimensionless conditioning measure. See the failure's note
    /// for which quantity it is.
    double conditioningRatio = 0.0;
    /// The general kernel's reported independent-column count. Zero on the
    /// symmetric path, which has no such notion. REPORTED and never gated
    /// (`ADR-076` Decision 4).
    std::size_t reportedRank = 0;
    /// Which kernel ran.
    bool symmetricKernel = false;

    friend bool operator==(const CfdLinearSolution&, const CfdLinearSolution&) = default;
};

/// Solves a symmetric CFD system — the pressure correction — with the LDLT
/// kernel `structural` and `thermal` also use.
///
/// REQUIRES EXACT SYMMETRY and refuses anything else as
/// `MatrixIsNotSymmetric`. The assembly that produces this matrix writes one
/// face coefficient to both off-diagonals in a single visit for exactly this
/// reason (`ADR-075` Q1), so a matrix that fails here is a defective assembly
/// rather than a hard problem.
[[nodiscard]] BETTERCAD_CFD_EXPORT std::expected<CfdLinearSolution, CfdLinearSolveFailure>
solveCfdPressureSystem(const CfdSparseMatrix& matrix, std::span<const double> rightHandSide,
                       const CfdLinearSolverSettings& settings);

/// Solves a general CFD system — a momentum component — with the QR kernel.
///
/// ACCEPTS A SYMMETRIC MATRIX TOO, and does not route it elsewhere: at exactly
/// zero mass flux the momentum operator IS symmetric, and this entry point
/// still answers for it. Which kernel a given matrix should get is the port's
/// decision, not this function's.
[[nodiscard]] BETTERCAD_CFD_EXPORT std::expected<CfdLinearSolution, CfdLinearSolveFailure>
solveCfdMomentumSystem(const CfdSparseMatrix& matrix, std::span<const double> rightHandSide,
                       const CfdLinearSolverSettings& settings);

/// The problem the matching entry point would report, or none if it would
/// succeed. The same checks in the same order.
///
/// @param requireSymmetry true for the pressure entry point's rules.
[[nodiscard]] BETTERCAD_CFD_EXPORT std::optional<CfdLinearSolveProblem>
cfdLinearSolveProblem(const CfdSparseMatrix& matrix, std::span<const double> rightHandSide,
                      const CfdLinearSolverSettings& settings, bool requireSymmetry);

/// A `CfdLinearSolve` port for `P19-PVCOUPLE-001`, routing on exact symmetry.
///
/// `largestSymmetryError() == 0.0` takes the pressure path and anything else
/// takes the momentum path. See the header note for why the test is exact and
/// what follows from the kernel being a function of the matrix.
[[nodiscard]] BETTERCAD_CFD_EXPORT CfdLinearSolve cfdLinearSolve();

} // namespace bettercad::cfd
