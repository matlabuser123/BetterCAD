#pragma once

/// \file
/// The compressed-sparse-row value a CFD operator assembles into.
///
/// A CONTAINER AND A PRODUCT, AND NOTHING MORE. No factorisation, no solve, no
/// iteration, no preconditioner, no transpose. `src/cfd/CMakeLists.txt` records
/// that admitting a solve kernel is `P19-LINSOLVE-001`'s decision and that it
/// would reuse `BetterCAD::linalg`; this header is the amendment ADR-069
/// Decision 3 describes, which admits the *container* and leaves the solve
/// exactly where it was. **No `linalg` header is included here and no `linalg`
/// target is linked by `cfd`.**
///
/// WHY NOT `linalg::SparseIndex`. Naming that type would mean including that
/// header and linking that target, which is the decision being deferred.
/// `CfdSparseIndex` is the same width and signedness by construction, so a
/// later `linalg::solveSymmetricSparse(rows, rowStart, inner, values, rhs,
/// settings)` consumes these spans with no translation and no copy; the
/// `static_assert` that turns "same by construction" into "same, enforced"
/// belongs to the milestone that may legitimately include that header. That is
/// a weaker guarantee than the assert, on purpose, and ADR-069 says so.
///
/// WHY A THIRD COPY EXISTS AT ALL. `thermal::ThermalSparseMatrix` is layer 45
/// and `cfd` is 42, so it is unreachable -- not a judgement call, a build
/// failure. Extracting a shared container into `linalg` would modify a module
/// `P17` and `P18` are qualified against in order to serve one new consumer,
/// which is the shape of extraction ADR-063 Decision 7 refused for the same
/// reason.
///
/// POSSESSION IS THE EVIDENCE (ADR-036). The only way to obtain one is
/// `fromCsr`, which validates; a matrix whose arrays are inconsistent does not
/// exist, so a consumer handed one need not re-check.

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

/// The index type of a compressed-sparse-row array: 64-bit and unsigned.
///
/// 64-BIT DELIBERATELY. A 32-bit inner index is the classic sparse overflow,
/// and `thermal` and `structural` both chose 64 for that reason. It is also
/// the width `linalg::SparseIndex` uses, which is what makes a later solve a
/// span hand-off rather than a conversion.
using CfdSparseIndex = std::uint64_t;

/// Why a compressed-sparse-row layout is not a matrix.
///
/// EVERY VALUE IS REACHABLE from a hand-written layout and has a test. They are
/// checked in this order, and the order is asserted: a layout with two faults
/// reports the earlier one.
enum class CfdSparseProblem : std::uint8_t {
    /// `rowStart` is not `rows + 1` long, or `inner` and `values` differ in
    /// length.
    DimensionsInconsistent,
    /// `rowStart[0] != 0`, a decrease, or a last entry that is not `nonZeros()`.
    RowStartInvalid,
    /// A column index at or beyond `columns`.
    ColumnOutOfRange,
    /// Within a row, a column that does not exceed the one before it: a repeat
    /// -- a pattern holding one coefficient twice -- or a disorder. Either
    /// makes `coeffSi` ambiguous, so neither is tolerated.
    ColumnsNotStrictlyAscending,
    /// A stored value is NaN or infinite.
    NonFiniteEntry,
    /// Symmetry was required and `max |A(i,j) - A(j,i)|` exceeds the tolerance.
    NotSymmetric,
};

/// The six, in the order they are checked.
inline constexpr std::array<CfdSparseProblem, 6> kCfdSparseProblems{
    CfdSparseProblem::DimensionsInconsistent, CfdSparseProblem::RowStartInvalid,
    CfdSparseProblem::ColumnOutOfRange,       CfdSparseProblem::ColumnsNotStrictlyAscending,
    CfdSparseProblem::NonFiniteEntry,         CfdSparseProblem::NotSymmetric};

[[nodiscard]] BETTERCAD_CFD_EXPORT std::string_view toString(CfdSparseProblem problem) noexcept;

/// A refusal with its tag.
///
/// THE TAG IS THE POINT. P19-BC-001's review found eight refusals whose tags
/// could be swapped with every message assertion still passing, so a tagged
/// failure exists wherever a message would otherwise be the only evidence.
struct CfdSparseFailure {
    CfdSparseProblem problem{};
    Error error{};
};

/// A validated compressed-sparse-row matrix of plain doubles in SI.
///
/// `values()` is `double` and not a `Quantity`, so the buffer maps into any
/// sparse library without a reinterpret; the unit of an entry is the operator's
/// to document, and ADR-069 documents the diffusion operator's.
class BETTERCAD_CFD_EXPORT CfdSparseMatrix {
public:
    using Index = CfdSparseIndex;

    [[nodiscard]] std::size_t rows() const noexcept { return rows_; }
    [[nodiscard]] std::size_t columns() const noexcept { return columns_; }
    [[nodiscard]] std::size_t nonZeros() const noexcept { return values_.size(); }

    /// The three CSR arrays, in the form a solve takes.
    [[nodiscard]] std::span<const Index> rowStart() const noexcept { return rowStart_; }
    [[nodiscard]] std::span<const Index> inner() const noexcept { return inner_; }
    [[nodiscard]] std::span<const double> values() const noexcept { return values_; }

    /// `A(row, column)` in SI, or zero outside the pattern AND outside the
    /// matrix. A binary search within the row, so a caller may ask about any
    /// pair without bounds-checking first.
    [[nodiscard]] double coeffSi(std::size_t row, std::size_t column) const noexcept;

    /// `max |A(i,j) - A(j,i)|` over stored entries, an absent counterpart
    /// counted as zero. Infinity for a non-square matrix, so a caller cannot
    /// read a missing answer as a passing one.
    [[nodiscard]] double largestSymmetryError() const noexcept;

    /// `max |A(i,j)|`, zero for an empty pattern. The scale a relative bound
    /// divides by.
    [[nodiscard]] double largestMagnitude() const noexcept;

    /// The sum of row @p row, in SI, or zero for a row out of range.
    ///
    /// NAMED BECAUSE THE DIFFUSION OPERATOR'S CENTRAL NULL PROPERTY IS A ROW
    /// SUM: a row with no prescribed-value face sums to zero exactly, because
    /// its diagonal is the sum of the same coefficients its off-diagonals
    /// negate. A caller that had to write this loop itself would be writing the
    /// test's oracle and the production reader at once.
    [[nodiscard]] double rowSumSi(std::size_t row) const noexcept;

    /// `A x`. The only algebra in this class.
    ///
    /// A SERIAL SUM IN ROW ORDER AND ASCENDING COLUMN ORDER, so it is
    /// reproducible to the last bit and a determinism test means something. It
    /// is here because every invariant worth checking at assembly time -- `A 1`
    /// for the null mode, `x^T A x` for an energy -- is one, and it is the one
    /// operation a consumer and a test both need.
    [[nodiscard]] Result<std::vector<double>> multiply(std::span<const double> x) const;

    friend bool operator==(const CfdSparseMatrix&, const CfdSparseMatrix&) = default;

    /// The only door. Validates in `kCfdSparseProblems` order and takes
    /// ownership of the arrays.
    ///
    /// @param requiredSymmetryTolerance when present, `max |A(i,j) - A(j,i)|`
    ///        must not exceed it times `largestMagnitude()`. A RELATIVE bound,
    ///        and a REFUSAL rather than a repair: nothing here symmetrises,
    ///        because a `0.5 (A + A^T)` would hide a formula error rather than
    ///        fix a numerical one.
    [[nodiscard]] static std::expected<CfdSparseMatrix, CfdSparseFailure>
    fromCsr(std::size_t rows, std::size_t columns, std::vector<Index> rowStart, std::vector<Index> inner,
            std::vector<double> values, std::optional<double> requiredSymmetryTolerance = std::nullopt);

private:
    CfdSparseMatrix() = default;

    std::size_t rows_ = 0;
    std::size_t columns_ = 0;
    std::vector<Index> rowStart_{};
    std::vector<Index> inner_{};
    std::vector<double> values_{};
};

/// The problem `fromCsr` would report for these arrays, or none if it would
/// succeed. The same checks in the same order.
///
/// A PREDICTOR, so a test can assert a TAG without having to provoke the
/// refusal through the constructor and read a message.
[[nodiscard]] BETTERCAD_CFD_EXPORT std::optional<CfdSparseProblem>
cfdSparseProblem(std::size_t rows, std::size_t columns, std::span<const CfdSparseIndex> rowStart,
                 std::span<const CfdSparseIndex> inner, std::span<const double> values,
                 std::optional<double> requiredSymmetryTolerance = std::nullopt) noexcept;

} // namespace bettercad::cfd
