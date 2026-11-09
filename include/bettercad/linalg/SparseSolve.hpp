#pragma once

// The generic NON-SYMMETRIC sparse solve (ADR-076), beside the symmetric one.
//
// WHAT THIS IS. A direct sparse QR factorisation of a general square system
// `A x = b` given in compressed sparse row form, behind THREE INDEPENDENT
// CONDITIONS, and the independent residual that proves the answer. Like its
// symmetric sibling it knows nothing about structures, heat, fluids, meshes,
// units, loads or constraints: its inputs are three index/value arrays, a
// right-hand side and two ratios.
//
// WHY A SECOND KERNEL EXISTS AT ALL. `solveSymmetricSparse` is an LDLT and
// reads one triangle. A convection operator is ASYMMETRIC and correct
// (`ADR-072` Decision 3), so a CFD momentum system cannot go through it.
// `ADR-052`'s own Rationale named this case when it created this module --
// "What would have made Option B right: if the thermal system needed a
// different algorithm (an iterative method, a non-symmetric factorisation). It
// does not" -- and CFD's momentum system does. The alternative, a private Eigen
// link inside the physics module, would duplicate the three-condition
// acceptance one level further out, which is the duplication `ADR-052` chose
// extraction to avoid.
//
// WHY QR AND NOT LU, WHICH IS FASTER (ADR-076 Decision 2). The gate needs a
// SCALE-FREE conditioning measure, because a library reporting success is never
// sufficient evidence, and `Eigen::SparseLU` was MEASURED reporting
// `info() == Success` with a normalized residual of exactly ZERO on a system
// singular to `1e-13`. Its conditioning analogue of the LDLT pivot ratio is
// `min|U_ii|/max|U_ii|`, and the pinned Eigen exposes no accessor for U's
// diagonal: recovering it means solving U against a dense identity, which is
// 272 MB at the 5832 unknowns of `P18-SOLVE-001`'s smoke test. `SparseQR`'s
// `matrixR()` is a real sparse matrix, so its diagonal is read in `O(nnz)`.
// The price is recorded rather than hidden: QR costs 1.65x LU at 4000 unknowns
// and its residual is looser and grows with size (`ADR-076` L2, L3).
//
// ```text
//   1  info() == Success, and min|R_ii|/max|R_ii| >= rankRatioFloor
//   2  every component of x is finite
//   3  the independent residual r = A x - b passes the normalized gate
// ```
//
// THE RESIDUAL IS NOT COMPUTED HERE. It is `evaluateResidual`, the symmetric
// kernel's own public acceptance step, which takes only arrays and knows no
// algorithm; this kernel calls it and translates its classification. There is
// ONE residual gate in BetterCAD and both kernels are behind it.
//
// EIGEN'S RANK IS REPORTED AND NEVER GATED (ADR-076 Decision 4). `rank()` is
// `m_nonzeropivots` under a threshold Eigen chooses, and Eigen's own
// documentation says the strategy "might fail for some rank deficient problems"
// and that one should "check the accuracy of the computed solution". A number
// that depends on a library's internal threshold is a fact for a diagnostic,
// not a condition to accept on.
//
// NO REGULARISATION. No `A += eps*I`, no pinned unknown, no penalty, no
// least-squares fallback for a square system, no second algorithm. A singular
// system is reported as one.
//
// PRIVATE EIGEN. No public header includes it (rule 7 of the layering check).

#include <bettercad/core/Error.hpp>
#include <bettercad/linalg/Export.hpp>
#include <bettercad/linalg/SymmetricSolve.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace bettercad::linalg {

/// The two ratios that judge a general solve.
struct SparseSolveSettings {
    /// The normalized residual above which a solve is refused. Finite, positive.
    double relativeResidualTolerance = 1.0e-9;
    /// The smallest `min|R_ii|/max|R_ii|` accepted before the system is declared
    /// singular. Finite, positive.
    ///
    /// NOT A PIVOT FLOOR, AND THE NAME IS DIFFERENT ON PURPOSE. The symmetric
    /// kernel's `pivotFloor` bounds `min|D|/max|D|` of an LDLT's diagonal; this
    /// bounds the diagonal of a QR's R factor. The two are both dimensionless
    /// ratios of the same shape and they are NOT the same quantity, so
    /// `ADR-076`'s Consequences require the names to stay apart.
    double rankRatioFloor = 1.0e-12;

    friend bool operator==(const SparseSolveSettings&, const SparseSolveSettings&) = default;
};

/// Both ratios finite and positive.
[[nodiscard]] BETTERCAD_LINALG_EXPORT Result<void> validate(const SparseSolveSettings& settings);

/// Why a general sparse system cannot be solved acceptably.
///
/// THE EIGHT MATCH `SymmetricSolveProblem` ONE FOR ONE, and a test asserts that
/// both kernels classify the same malformed system identically. The checks are
/// written twice because the two kernels are separate translation units and
/// neither may reach into the other's internals; the test is what makes the
/// duplication safe, since drift becomes a failure rather than a divergence.
enum class SparseSolveProblem : std::uint8_t {
    InvalidSettings,
    /// The arrays do not describe a square CSR of the stated size, or the
    /// right-hand side is the wrong length. No implicit resizing.
    DimensionMismatch,
    /// An input entry is not finite.
    NonFiniteSystem,
    /// The factorisation reported a failure that is not a detected singularity.
    FactorizationFailure,
    /// `min|R_ii|/max|R_ii|` is below the floor, or a diagonal entry of the
    /// factor is not finite. The kernel measures the conditioning; it does not
    /// claim to know why the system is singular.
    ///
    /// AND IT IS NOT REPORTED FROM A LIBRARY VERDICT. An earlier form of this
    /// comment said "a structurally singular matrix (the library said so)",
    /// which no code path produced: every non-`Success` from the factorisation
    /// becomes `FactorizationFailure`. A review found the divergence.
    SingularSystem,
    NonFiniteSolution,
    NonFiniteResidual,
    ResidualTooLarge,
};

/// Every enumerator exactly once, for exhaustiveness tests.
inline constexpr std::array<SparseSolveProblem, 8> kSparseSolveProblems{
    SparseSolveProblem::InvalidSettings,     SparseSolveProblem::DimensionMismatch,
    SparseSolveProblem::NonFiniteSystem,     SparseSolveProblem::FactorizationFailure,
    SparseSolveProblem::SingularSystem,      SparseSolveProblem::NonFiniteSolution,
    SparseSolveProblem::NonFiniteResidual,   SparseSolveProblem::ResidualTooLarge,
};

static_assert(kSparseSolveProblems.size() == 8,
              "kSparseSolveProblems must hold every SparseSolveProblem exactly once");

[[nodiscard]] BETTERCAD_LINALG_EXPORT std::string_view toString(SparseSolveProblem problem) noexcept;

/// A refusal with its classification and the numbers that justify it, so a
/// caller that owns the vocabulary says it in its own words without parsing a
/// message.
struct SparseSolveFailure {
    SparseSolveProblem problem = SparseSolveProblem::FactorizationFailure;
    Error error{};
    /// For `SingularSystem`: the factorisation ran and `min|R_ii|/max|R_ii|` is
    /// this. Zero when the factorisation did not run to completion.
    double rankRatio = 0.0;
    /// Eigen's own `rank()`. REPORTED, never gated: see the header note.
    std::size_t reportedRank = 0;
    /// For a `SingularSystem` refused by the ratio gate: Eigen's own `rank()`
    /// came out short of `rows`, so the factorisation found a dependent column
    /// as well as a small ratio.
    ///
    /// THE NAME IS ABOUT EIGEN'S THRESHOLD AND NOT ABOUT THE MATRIX, and an
    /// earlier name and comment claimed the library had declared the matrix
    /// structurally singular. It had not: this is `reportedRank < rows`, and
    /// `rank()` is `m_nonzeropivots` under a threshold Eigen chooses, which
    /// grows with the system size. A review found the divergence.
    bool rankCameOutShort = false;
    /// For `ResidualTooLarge`: the measured residual. Otherwise default.
    ResidualNorms residual{};
    /// Where the failure occurred, for the component-wise problems: the index
    /// of the first non-finite entry.
    std::size_t index = 0;
    /// The system size.
    std::size_t rows = 0;
};

/// One solved general sparse system.
struct SparseSolution {
    std::vector<double> x{};
    ResidualNorms residual{};
    /// `min|R_ii| / max|R_ii|`. `1.0` for an empty system, which has no factor.
    double rankRatio = 0.0;
    /// Eigen's own `rank()`, reported for a diagnostic and never gated.
    std::size_t reportedRank = 0;

    friend bool operator==(const SparseSolution&, const SparseSolution&) = default;
};

/// Solves `A x = b` for a general square `A` in compressed sparse row form.
///
/// Applies all three gates: the factorisation and its R-diagonal ratio,
/// finiteness, and the independent residual. ZERO ROWS IS A SOLVED SYSTEM, not
/// a failure: nothing is handed to the library.
///
/// A SYMMETRIC MATRIX IS ACCEPTED TOO, and is not routed elsewhere. This kernel
/// does not know what its caller's matrix means, so it cannot decide that a
/// symmetric system would be better served by the LDLT. That routing belongs to
/// the caller that has the information (`ADR-076` Decision 7).
[[nodiscard]] BETTERCAD_LINALG_EXPORT std::expected<SparseSolution, SparseSolveFailure>
solveSparse(std::size_t rows, std::span<const SparseIndex> rowStart, std::span<const SparseIndex> inner,
            std::span<const double> values, std::span<const double> rhs, const SparseSolveSettings& settings);

/// The problem `solveSparse` would report, or none if it would succeed. The same
/// checks in the same order.
[[nodiscard]] BETTERCAD_LINALG_EXPORT std::optional<SparseSolveProblem>
sparseSolveProblem(std::size_t rows, std::span<const SparseIndex> rowStart, std::span<const SparseIndex> inner,
                   std::span<const double> values, std::span<const double> rhs,
                   const SparseSolveSettings& settings);

} // namespace bettercad::linalg
