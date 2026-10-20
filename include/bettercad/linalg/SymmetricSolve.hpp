#pragma once

// The generic symmetric sparse solve (ADR-052), extracted from P17-SOLVE-001.
//
// WHAT THIS IS. A direct sparse factorisation of a symmetric system `A x = b`
// given in compressed sparse row form, behind THREE INDEPENDENT CONDITIONS, and
// the independent residual that proves the answer. It knows nothing about
// structures, heat, meshes, units, loads or constraints: its inputs are three
// index/value arrays, a right-hand side and two ratios, and its outputs are
// numbers and a classification.
//
// WHY IT LIVES HERE (ADR-050, ADR-052). `structural` (50) and `thermal` (45) both
// need exactly this kernel, and `thermal` may not include `structural`. P17
// built and qualified it inside `structural`; the extraction moves that code to a
// module BELOW both, MECHANICALLY -- the same algorithm, the same gates, the same
// order of checks -- and `structural::solveSymmetricSparse` is now a thin wrapper
// that adds P17's units and P17's wording. Nothing was copied: there is one
// kernel, and both physics call it.
//
// A LIBRARY SAYING "Success" IS NEVER SUFFICIENT EVIDENCE. `SimplicialLDLT`
// reports `NumericalIssue` only when a pivot is EXACTLY zero, and a singular
// matrix in floating point almost never produces one, so the kernel applies:
//
// ```text
//   1  info() == Success, every D(k,k) > 0, and min|D|/max|D| >= pivotFloor
//   2  every component of x is finite
//   3  the independent residual r = A x - b passes the normalized gate
// ```
//
// THE RESIDUAL IS COMPUTED BY BETTERCAD, from the ORIGINAL arrays, after the
// solve has returned. The library's own error estimate is never consulted. It
// is also available WITHOUT a solve (`evaluateResidual`), so a candidate
// solution -- from any source, including a deliberately wrong one in a test --
// can be accepted or refused on the same terms.
//
// NO REGULARISATION. No `A += eps*I`, no pinned unknown, no penalty, no
// pseudo-inverse, no fallback to a second algorithm. A singular system is
// reported as one.
//
// PRIVATE EIGEN. No public header includes it (rule 7 of the layering check).

#include <bettercad/core/Error.hpp>
#include <bettercad/linalg/Export.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace bettercad::linalg {

/// The index type of a compressed-sparse-row array: 64-bit and unsigned, as
/// BetterCAD's own sparse containers use.
using SparseIndex = std::uint64_t;

/// The two ratios that judge a solve.
struct SymmetricSolveSettings {
    /// The normalized residual above which a solve is refused. Finite, positive.
    double relativeResidualTolerance = 1.0e-9;
    /// The smallest `min|D|/max|D|` accepted before the system is declared
    /// singular. Finite, positive.
    double pivotFloor = 1.0e-12;

    friend bool operator==(const SymmetricSolveSettings&, const SymmetricSolveSettings&) = default;
};

/// Both ratios finite and positive.
[[nodiscard]] BETTERCAD_LINALG_EXPORT Result<void> validate(const SymmetricSolveSettings& settings);

/// The independently computed residual `r = A x - b`, as dimensionless numbers
/// of whatever units the caller's system is in.
struct ResidualNorms {
    /// `||r||_2`.
    double euclidean = 0.0;
    /// `||r||_inf`.
    double infinity = 0.0;
    /// `||A||_inf`, the largest absolute row sum.
    double matrixInfinity = 0.0;
    /// `||x||_2`.
    double solution = 0.0;
    /// `||b||_2`.
    double rightHandSide = 0.0;

    /// ```text
    ///                       ||r||_2
    ///     normalized = ------------------------
    ///                  ||A||_inf ||x||_2 + ||b||_2
    /// ```
    ///
    /// NO SCALE FLOOR, AND ITS ABSENCE IS THE POINT. The denominator vanishes
    /// only when `x` and `b` are both exactly zero, and then `r = A 0 - 0` is
    /// exactly zero too, so the honest value is `0` rather than `0 / floor`. If
    /// the denominator is zero while `r` is not, the check fails rather than
    /// reporting a flattering ratio.
    ///
    /// Scale-aware by construction: scaling `A` and `b` by `c` scales numerator
    /// and denominator alike.
    double normalized = 0.0;

    friend bool operator==(const ResidualNorms&, const ResidualNorms&) = default;
};

/// Why a symmetric sparse system cannot be solved acceptably.
enum class SymmetricSolveProblem : std::uint8_t {
    InvalidSettings,
    /// The arrays do not describe a square CSR of the stated size, or the
    /// right-hand side is the wrong length. No implicit resizing.
    DimensionMismatch,
    /// An input entry is not finite.
    NonFiniteSystem,
    /// The factorisation reported a failure other than a zero pivot.
    FactorizationFailure,
    /// An exactly zero pivot, a non-positive pivot, a non-finite pivot, or
    /// `min|D|/max|D|` below the floor. The kernel measures the conditioning;
    /// it does not claim to know why the system is singular.
    SingularSystem,
    NonFiniteSolution,
    NonFiniteResidual,
    ResidualTooLarge,
};

[[nodiscard]] BETTERCAD_LINALG_EXPORT std::string_view toString(SymmetricSolveProblem problem) noexcept;

/// A refusal with its classification and the numbers that justify it, so a
/// caller that owns the vocabulary (units, wording) can say it in its own words
/// without parsing a message.
struct SymmetricSolveFailure {
    SymmetricSolveProblem problem = SymmetricSolveProblem::FactorizationFailure;
    Error error{};
    /// For `SingularSystem`: the factorisation ran and `min|D|/max|D|` is this.
    /// Zero when the factorisation did not run to completion.
    double pivotRatio = 0.0;
    /// For `SingularSystem`: whether every pivot was positive.
    bool allPivotsPositive = true;
    /// For `SingularSystem`: an exactly zero pivot (the library said so)
    /// rather than a measured ratio.
    bool exactlyZeroPivot = false;
    /// For `SingularSystem`: the factorisation produced a pivot that is not finite.
    bool nonFinitePivot = false;
    /// For `ResidualTooLarge`: the measured residual. Otherwise default.
    ResidualNorms residual{};
    /// Where the failure occurred, for the component-wise problems: the index
    /// of the first non-finite entry.
    std::size_t index = 0;
    /// The system size.
    std::size_t rows = 0;
};

/// One solved symmetric sparse system.
struct SymmetricSolution {
    std::vector<double> x{};
    ResidualNorms residual{};
    /// `min|D| / max|D|`. `1.0` for an empty system, which has no pivots.
    double pivotRatio = 0.0;

    friend bool operator==(const SymmetricSolution&, const SymmetricSolution&) = default;
};

/// Solves `A x = b` for a symmetric `A` in compressed sparse row form.
///
/// Applies all three gates: the factorisation and its pivot ratio, finiteness,
/// and the independent residual. ZERO ROWS IS A SOLVED SYSTEM, not a failure:
/// nothing is handed to the library.
[[nodiscard]] BETTERCAD_LINALG_EXPORT std::expected<SymmetricSolution, SymmetricSolveFailure>
solveSymmetricSparse(std::size_t rows, std::span<const SparseIndex> rowStart, std::span<const SparseIndex> inner,
                     std::span<const double> values, std::span<const double> rhs,
                     const SymmetricSolveSettings& settings);

/// The problem `solveSymmetricSparse` would report, or none if it would
/// succeed. The same checks in the same order.
[[nodiscard]] BETTERCAD_LINALG_EXPORT std::optional<SymmetricSolveProblem>
symmetricSolveProblem(std::size_t rows, std::span<const SparseIndex> rowStart, std::span<const SparseIndex> inner,
                      std::span<const double> values, std::span<const double> rhs,
                      const SymmetricSolveSettings& settings);

/// Accepts or refuses a CANDIDATE solution `x` of `A x = b` on the kernel's own
/// terms, without solving: every component finite, the residual computed
/// independently, finite, and within the tolerance.
///
/// The acceptance step of `solveSymmetricSparse`, public because a solve is only
/// as trustworthy as the gate behind it, and a gate that cannot be shown a wrong
/// answer has not been tested. It performs the dimension and finiteness checks of
/// the system as well.
[[nodiscard]] BETTERCAD_LINALG_EXPORT std::expected<ResidualNorms, SymmetricSolveFailure>
evaluateResidual(std::size_t rows, std::span<const SparseIndex> rowStart, std::span<const SparseIndex> inner,
                 std::span<const double> values, std::span<const double> rhs, std::span<const double> x,
                 const SymmetricSolveSettings& settings);

} // namespace bettercad::linalg
