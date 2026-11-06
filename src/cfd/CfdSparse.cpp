#include <bettercad/cfd/CfdSparse.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <iterator>
#include <limits>
#include <string>
#include <utility>

// ONE DECISION PATH, TWO ENTRY POINTS. `fromCsr` and `cfdSparseProblem` both
// call `problemOf` below, so the predictor cannot disagree with the path it
// predicts. P19-DOF-001's review found a staged gate whose predictor was a
// second implementation; this is the shape that followed from it.
//
// EVERY CHECK IS ON SPANS, so the predictor needs no matrix and the constructor
// needs no second copy.

namespace bettercad::cfd {
namespace {

/// `A(row, column)` from raw CSR, or zero outside the pattern. A binary search
/// within the row, which is why the columns must be strictly ascending.
[[nodiscard]] double coeffOf(std::span<const CfdSparseIndex> rowStart, std::span<const CfdSparseIndex> inner,
                             std::span<const double> values, std::size_t rows, std::size_t columns,
                             std::size_t row, std::size_t column) noexcept {
    if (row >= rows || column >= columns) {
        return 0.0;
    }
    const auto first = static_cast<std::ptrdiff_t>(rowStart[row]);
    const auto last = static_cast<std::ptrdiff_t>(rowStart[row + 1]);
    const auto begin = std::next(inner.begin(), first);
    const auto end = std::next(inner.begin(), last);
    const auto found = std::lower_bound(begin, end, static_cast<CfdSparseIndex>(column));
    if (found == end || *found != static_cast<CfdSparseIndex>(column)) {
        return 0.0;
    }
    return values[static_cast<std::size_t>(std::distance(inner.begin(), found))];
}

[[nodiscard]] double largestMagnitudeOf(std::span<const double> values) noexcept {
    double largest = 0.0;
    for (const double value : values) {
        largest = std::max(largest, std::abs(value));
    }
    return largest;
}

/// `max |A(i,j) - A(j,i)|` over stored entries. Requires a layout that has
/// already passed the structural checks, because it reads through `coeffOf`.
[[nodiscard]] double symmetryErrorOf(std::span<const CfdSparseIndex> rowStart,
                                     std::span<const CfdSparseIndex> inner, std::span<const double> values,
                                     std::size_t rows, std::size_t columns) noexcept {
    if (rows != columns) {
        return std::numeric_limits<double>::infinity();
    }
    double largest = 0.0;
    for (std::size_t row = 0; row < rows; ++row) {
        for (CfdSparseIndex slot = rowStart[row]; slot < rowStart[row + 1]; ++slot) {
            const auto index = static_cast<std::size_t>(slot);
            const auto column = static_cast<std::size_t>(inner[index]);
            const double mirrored = coeffOf(rowStart, inner, values, rows, columns, column, row);
            largest = std::max(largest, std::abs(values[index] - mirrored));
        }
    }
    return largest;
}

/// Records @p problem, and builds its message only if a caller asked for one.
template <typename Describe>
[[nodiscard]] std::optional<CfdSparseProblem> refuse(CfdSparseProblem problem, std::string* message,
                                                    Describe describe) {
    if (message != nullptr) {
        *message = describe();
    }
    return problem;
}

/// THE decision path, shared by both entry points so a predictor cannot
/// disagree with the path it predicts.
///
/// THE MESSAGE IS OPTIONAL, AND THAT IS WHAT MAKES THE PREDICTOR'S `noexcept`
/// HONEST. Formatting allocates, and a throwing allocation inside a `noexcept`
/// function terminates the process rather than propagating. So a message is
/// built only when a caller passes somewhere to put it: `fromCsr` does,
/// `cfdSparseProblem` does not, and with `nullptr` this function allocates on no
/// path. An earlier draft formatted unconditionally and defended the `noexcept`
/// in a comment, which is the kind of claim this project's rules treat as a
/// defect rather than a wording choice.
[[nodiscard]] std::optional<CfdSparseProblem> problemOf(std::size_t rows, std::size_t columns,
                                                        std::span<const CfdSparseIndex> rowStart,
                                                        std::span<const CfdSparseIndex> inner,
                                                        std::span<const double> values,
                                                        std::optional<double> requiredSymmetryTolerance,
                                                        std::string* message) {
    // PHASE 1 -- the three arrays have to agree before any of them is indexed.
    if (rowStart.size() != rows + 1 || inner.size() != values.size()) {
        return refuse(CfdSparseProblem::DimensionsInconsistent, message, [&] {
            return std::format("a {} by {} sparse matrix needs a row-start array of {} entries and equal "
                               "inner and value arrays; got {}, {} and {}",
                               rows, columns, rows + 1, rowStart.size(), inner.size(), values.size());
        });
    }

    // PHASE 2 -- the row offsets, which every later phase indexes through.
    if (rowStart.front() != 0) {
        return refuse(CfdSparseProblem::RowStartInvalid, message, [&] {
            return std::format("the row-start array begins at {} rather than 0", rowStart.front());
        });
    }
    for (std::size_t row = 0; row < rows; ++row) {
        if (rowStart[row + 1] < rowStart[row]) {
            return refuse(CfdSparseProblem::RowStartInvalid, message, [&] {
                return std::format("the row-start array decreases at row {}: {} then {}", row,
                                   rowStart[row], rowStart[row + 1]);
            });
        }
    }
    if (rowStart.back() != static_cast<CfdSparseIndex>(values.size())) {
        return refuse(CfdSparseProblem::RowStartInvalid, message, [&] {
            return std::format("the row-start array ends at {} rather than at the {} stored values",
                               rowStart.back(), values.size());
        });
    }

    // PHASE 3 -- the columns, in range.
    for (std::size_t slot = 0; slot < inner.size(); ++slot) {
        if (inner[slot] >= static_cast<CfdSparseIndex>(columns)) {
            return refuse(CfdSparseProblem::ColumnOutOfRange, message, [&] {
                return std::format("slot {} names column {} in a matrix of {} columns", slot, inner[slot],
                                   columns);
            });
        }
    }

    // PHASE 4 -- and strictly ascending within a row, so `coeffSi` is a binary
    // search and a repeat cannot hold one coefficient twice.
    for (std::size_t row = 0; row < rows; ++row) {
        for (CfdSparseIndex slot = rowStart[row] + 1; slot < rowStart[row + 1]; ++slot) {
            const auto index = static_cast<std::size_t>(slot);
            const CfdSparseIndex previous = inner[index - 1];
            const CfdSparseIndex current = inner[index];
            if (current <= previous) {
                return refuse(CfdSparseProblem::ColumnsNotStrictlyAscending, message, [&] {
                    return std::format("row {} names column {} after column {}, which is not an increase",
                                       row, current, previous);
                });
            }
        }
    }

    // PHASE 5 -- finiteness, before any arithmetic reads a value.
    for (std::size_t slot = 0; slot < values.size(); ++slot) {
        if (!std::isfinite(values[slot])) {
            return refuse(CfdSparseProblem::NonFiniteEntry, message, [&] {
                return std::format("slot {} holds {}, which is not finite", slot, values[slot]);
            });
        }
    }

    // PHASE 6 -- symmetry, last because it is the only check that reads the
    // layout through `coeffOf` and so needs every phase above to have passed.
    if (requiredSymmetryTolerance.has_value()) {
        const double error = symmetryErrorOf(rowStart, inner, values, rows, columns);
        const double magnitude = largestMagnitudeOf(values);
        const double bound = *requiredSymmetryTolerance * magnitude;
        // `!(error <= bound)` also refuses a NaN error and a non-square
        // matrix's infinite one. A zero matrix has error zero and bound zero
        // and passes.
        if (!(error <= bound)) {
            return refuse(CfdSparseProblem::NotSymmetric, message, [&] {
                return std::format("the largest asymmetry is {} and the bound is {} ({} times the largest "
                                   "magnitude {})",
                                   error, bound, *requiredSymmetryTolerance, magnitude);
            });
        }
    }

    return std::nullopt;
}

} // namespace

std::string_view toString(CfdSparseProblem problem) noexcept {
    switch (problem) {
    case CfdSparseProblem::DimensionsInconsistent:
        return "dimensions_inconsistent";
    case CfdSparseProblem::RowStartInvalid:
        return "row_start_invalid";
    case CfdSparseProblem::ColumnOutOfRange:
        return "column_out_of_range";
    case CfdSparseProblem::ColumnsNotStrictlyAscending:
        return "columns_not_strictly_ascending";
    case CfdSparseProblem::NonFiniteEntry:
        return "non_finite_entry";
    case CfdSparseProblem::NotSymmetric:
        return "not_symmetric";
    }
    // Not `default:`, so a seventh enumerator is a -Wswitch error rather than a
    // string that silently names the wrong fault.
    return "unknown";
}

double CfdSparseMatrix::coeffSi(std::size_t row, std::size_t column) const noexcept {
    return coeffOf(rowStart_, inner_, values_, rows_, columns_, row, column);
}

double CfdSparseMatrix::largestSymmetryError() const noexcept {
    return symmetryErrorOf(rowStart_, inner_, values_, rows_, columns_);
}

double CfdSparseMatrix::largestMagnitude() const noexcept { return largestMagnitudeOf(values_); }

double CfdSparseMatrix::rowSumSi(std::size_t row) const noexcept {
    if (row >= rows_) {
        return 0.0;
    }
    // Ascending column order, serially, so the sum is reproducible.
    double sum = 0.0;
    for (Index slot = rowStart_[row]; slot < rowStart_[row + 1]; ++slot) {
        sum = sum + values_[static_cast<std::size_t>(slot)];
    }
    return sum;
}

Result<std::vector<double>> CfdSparseMatrix::multiply(std::span<const double> x) const {
    if (x.size() != columns_) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("a {} by {} matrix multiplies a vector of {} entries, not {}", rows_,
                                     columns_, columns_, x.size()));
    }
    std::vector<double> out(rows_, 0.0);
    // A serial sum in row order and ascending column order: reproducible.
    for (std::size_t row = 0; row < rows_; ++row) {
        double sum = 0.0;
        for (Index slot = rowStart_[row]; slot < rowStart_[row + 1]; ++slot) {
            const auto index = static_cast<std::size_t>(slot);
            sum = sum + values_[index] * x[static_cast<std::size_t>(inner_[index])];
        }
        out[row] = sum;
    }
    return out;
}

std::expected<CfdSparseMatrix, CfdSparseFailure>
CfdSparseMatrix::fromCsr(std::size_t rows, std::size_t columns, std::vector<Index> rowStart,
                         std::vector<Index> inner, std::vector<double> values,
                         std::optional<double> requiredSymmetryTolerance) {
    std::string message;
    const std::optional<CfdSparseProblem> problem =
        problemOf(rows, columns, rowStart, inner, values, requiredSymmetryTolerance, &message);
    if (problem.has_value()) {
        return std::unexpected(
            CfdSparseFailure{*problem, Error{ErrorCode::InvalidArgument, std::move(message)}});
    }
    CfdSparseMatrix out;
    out.rows_ = rows;
    out.columns_ = columns;
    out.rowStart_ = std::move(rowStart);
    out.inner_ = std::move(inner);
    out.values_ = std::move(values);
    return out;
}

std::optional<CfdSparseProblem> cfdSparseProblem(std::size_t rows, std::size_t columns,
                                                 std::span<const CfdSparseIndex> rowStart,
                                                 std::span<const CfdSparseIndex> inner,
                                                 std::span<const double> values,
                                                 std::optional<double> requiredSymmetryTolerance) noexcept {
    // `nullptr` for the message, so this path allocates nothing and the
    // `noexcept` is a fact rather than a hope.
    return problemOf(rows, columns, rowStart, inner, values, requiredSymmetryTolerance, nullptr);
}

} // namespace bettercad::cfd
