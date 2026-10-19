// The sparse containers of a global thermal system (P18-ASSEMBLY-001).
//
// A CSR WHOSE LAYOUT IS CHECKED, ONCE, AT THE ONE DOOR. `fromCsr` is the only way
// to obtain a `ThermalSparseMatrix`, so every other function in the module may
// assume strictly ascending columns (a binary search) and finite values.
//
// NOTHING IS REPAIRED. A matrix that is asymmetric is refused, not symmetrised; an
// entry that is not finite is refused, not replaced; a small entry is kept, not
// pruned. The only transformation anywhere in this file is none.

#include <bettercad/thermal/ThermalSparse.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>

namespace bettercad::thermal {

std::string_view toString(MatrixProblem problem) noexcept {
    switch (problem) {
    case MatrixProblem::DimensionsInconsistent:
        return "dimensions_inconsistent";
    case MatrixProblem::RowStartInvalid:
        return "row_start_invalid";
    case MatrixProblem::ColumnOutOfRange:
        return "column_out_of_range";
    case MatrixProblem::ColumnsNotStrictlyAscending:
        return "columns_not_strictly_ascending";
    case MatrixProblem::NonFiniteEntry:
        return "non_finite_entry";
    case MatrixProblem::NotSymmetric:
        return "not_symmetric";
    }
    return "unknown";
}

double ThermalSparseMatrix::coeffSi(std::size_t row, std::size_t column) const noexcept {
    if (row >= rows_ || column >= columns_) {
        return 0.0;
    }
    const auto first = inner_.begin() + static_cast<std::ptrdiff_t>(rowStart_[row]);
    const auto last = inner_.begin() + static_cast<std::ptrdiff_t>(rowStart_[row + 1]);
    // Strictly ascending within a row, which is what makes this a search.
    const auto found = std::lower_bound(first, last, static_cast<Index>(column));
    if (found == last || *found != static_cast<Index>(column)) {
        return 0.0;
    }
    return values_[static_cast<std::size_t>(found - inner_.begin())];
}

double ThermalSparseMatrix::largestSymmetryError() const noexcept {
    if (rows_ != columns_) {
        return std::numeric_limits<double>::infinity();
    }
    double largest = 0.0;
    // Over STORED entries; the counterpart is looked up, and an ABSENT counterpart
    // is zero, so a stored (i,j) with no (j,i) is an asymmetry of |A(i,j)|. Entries
    // that are absent on BOTH sides contribute nothing, correctly.
    for (std::size_t row = 0; row < rows_; ++row) {
        for (Index slot = rowStart_[row]; slot < rowStart_[row + 1]; ++slot) {
            const auto column = static_cast<std::size_t>(inner_[slot]);
            largest = std::max(largest, std::abs(values_[slot] - coeffSi(column, row)));
        }
    }
    return largest;
}

double ThermalSparseMatrix::largestMagnitude() const noexcept {
    double largest = 0.0;
    for (const double value : values_) {
        largest = std::max(largest, std::abs(value));
    }
    return largest;
}

Result<std::vector<double>> ThermalSparseMatrix::multiply(std::span<const double> x) const {
    if (x.size() != columns_) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("a {} x {} matrix cannot multiply a vector of {} entries", rows_,
                                     columns_, x.size()));
    }
    std::vector<double> out(rows_, 0.0);
    // A serial sum in row order and ascending column order: reproducible.
    for (std::size_t row = 0; row < rows_; ++row) {
        double sum = 0.0;
        for (Index slot = rowStart_[row]; slot < rowStart_[row + 1]; ++slot) {
            sum = sum + values_[slot] * x[static_cast<std::size_t>(inner_[slot])];
        }
        out[row] = sum;
    }
    return out;
}

std::expected<ThermalSparseMatrix, ThermalSparseMatrix::Failure>
ThermalSparseMatrix::fromCsr(std::size_t rows, std::size_t columns, std::vector<Index> rowStart,
                             std::vector<Index> inner, std::vector<double> values,
                             std::optional<double> requiredSymmetryTolerance) {
    const auto refuse = [](MatrixProblem problem, std::string message) {
        return std::unexpected(Failure{.problem = problem,
                                       .error = makeError(ErrorCode::InvalidArgument, std::move(message)).error()});
    };

    if (rowStart.size() != rows + 1 || inner.size() != values.size()) {
        return refuse(MatrixProblem::DimensionsInconsistent,
                      std::format("a {} x {} CSR needs {} row starts and equal inner and value counts; it has "
                                  "{} row starts, {} inner indices and {} values",
                                  rows, columns, rows + 1, rowStart.size(), inner.size(), values.size()));
    }
    if (rowStart.front() != 0 || rowStart.back() != static_cast<Index>(values.size()) ||
        !std::ranges::is_sorted(rowStart)) {
        return refuse(MatrixProblem::RowStartInvalid,
                      "the row starts must begin at 0, never decrease and end at the number of stored entries");
    }
    for (std::size_t row = 0; row < rows; ++row) {
        for (Index slot = rowStart[row]; slot < rowStart[row + 1]; ++slot) {
            if (inner[slot] >= static_cast<Index>(columns)) {
                return refuse(MatrixProblem::ColumnOutOfRange,
                              std::format("row {} stores column {} of a matrix with {} columns", row,
                                          inner[slot], columns));
            }
            if (slot > rowStart[row] && !(inner[slot - 1] < inner[slot])) {
                return refuse(MatrixProblem::ColumnsNotStrictlyAscending,
                              std::format("row {} stores column {} after column {}: a repeat holds one "
                                          "coefficient twice and a disorder defeats the search",
                                          row, inner[slot], inner[slot - 1]));
            }
        }
    }
    for (std::size_t slot = 0; slot < values.size(); ++slot) {
        if (!std::isfinite(values[slot])) {
            return refuse(MatrixProblem::NonFiniteEntry,
                          std::format("stored entry {} is not finite", slot));
        }
    }

    ThermalSparseMatrix out;
    out.rows_ = rows;
    out.columns_ = columns;
    out.rowStart_ = std::move(rowStart);
    out.inner_ = std::move(inner);
    out.values_ = std::move(values);

    if (requiredSymmetryTolerance.has_value()) {
        const double error = out.largestSymmetryError();
        const double bound = *requiredSymmetryTolerance * out.largestMagnitude();
        // `!(error <= bound)` also refuses a NaN error and a non-square matrix's
        // infinite one. A zero matrix has error zero and bound zero and passes.
        if (!(error <= bound)) {
            return refuse(MatrixProblem::NotSymmetric,
                          std::format("max |A(i,j) - A(j,i)| = {} exceeds {} x the largest entry {}", error,
                                      *requiredSymmetryTolerance, out.largestMagnitude()));
        }
    }
    return out;
}

Power ThermalHeatVector::total() const noexcept {
    double sum = 0.0;
    for (const double value : entries_) {
        sum = sum + value;
    }
    return Power::fromSi(sum);
}

Result<ThermalHeatVector> ThermalHeatVector::fromEntries(std::vector<double> entries) {
    for (std::size_t row = 0; row < entries.size(); ++row) {
        if (!std::isfinite(entries[row])) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("heat-input entry {} is not finite", row));
        }
    }
    ThermalHeatVector out;
    out.entries_ = std::move(entries);
    return out;
}

} // namespace bettercad::thermal
