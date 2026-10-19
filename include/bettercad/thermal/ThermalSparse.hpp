#pragma once

// P18-ASSEMBLY-001 -- the sparse containers a global thermal system is held in.
//
// BETTERCAD'S OWN COMPRESSED SPARSE ROW, FOR THE REASON ADR-038 GAVE P17. No
// linear-algebra library is linked into `bettercad_thermal` (its CMake file says
// P18-SOLVE-001 decides that), so the matrix is three vectors laid out so a
// solver can wrap them in `Eigen::Map<const Eigen::SparseMatrix<double,
// Eigen::RowMajor, std::int64_t>>`, SuiteSparse or anything else at zero copy.
// It is the same representation as P17's `StiffnessMatrix`, deliberately, so the
// two solvers can share what they share.
//
// ```text
// rowStart   size rows() + 1, monotone non-decreasing, rowStart[0] == 0,
//            rowStart[rows()] == nonZeros()
// inner      size nonZeros(), STRICTLY ASCENDING within each row, each < columns()
// values     size nonZeros(), parallel to inner, SI
// ```
//
// 64-BIT INDICES, because a 32-bit inner index is the classic sparse overflow, and
// the bound is proved by a `static_assert` in ThermalSystem.hpp rather than hoped
// for.
//
// AN ABSENT ENTRY IS ZERO. `coeffSi()` returns zero for a pair the pattern does not
// hold, which is what a sparse matrix means, and the symmetry measure counts a
// missing transpose as zero: `K(i,j)` stored with `K(j,i)` absent is an asymmetry of
// `|K(i,j)|`, not a pass.
//
// POSSESSION IS THE EVIDENCE (ADR-036). The only constructor is `fromCsr`, which
// validates the layout, the range of every column, the finiteness of every value
// and, if asked, the symmetry. A matrix whose arrays are inconsistent does not
// exist, so a solver handed one need not re-check.
//
// NOTHING HERE IS DENSE. There is no `toDense`: a large mesh must never be made
// dense to be inspected, and the small-system oracles in the tests build their own
// dense copies from `coeffSi`.
//
// NO ALGEBRA BEYOND `multiply`. A matrix-vector product is here because every
// invariant worth checking at assembly time (`K 1 = 0`, `1^T C 1`, `T^T K T`) is
// one, and it is the one operation a solver and a test both need. There is no
// factorisation, no solve and no transpose.

#include <bettercad/core/Error.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/thermal/Export.hpp>
#include <bettercad/thermal/ThermalElement.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace bettercad::thermal {

/// Why a CSR layout cannot be a `ThermalSparseMatrix`.
///
/// EVERY VALUE IS REACHABLE from a hand-written layout and has a test.
enum class MatrixProblem : std::uint8_t {
    /// `rowStart` is not `rows + 1` long, or `inner` and `values` differ in length.
    DimensionsInconsistent,
    /// `rowStart[0] != 0`, a decrease, or a last entry that is not `nonZeros()`.
    RowStartInvalid,
    /// A column index at or beyond `columns`.
    ColumnOutOfRange,
    /// Within a row, a column that does not exceed the one before it: a repeat (a
    /// pattern that holds one coefficient twice) or a disorder.
    ColumnsNotStrictlyAscending,
    /// A stored value is NaN or infinite.
    NonFiniteEntry,
    /// Symmetry was required and `max |A(i,j) - A(j,i)|` exceeds the tolerance.
    NotSymmetric,
};

[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::string_view toString(MatrixProblem problem) noexcept;

/// A square or rectangular sparse matrix of doubles, in SI of the unit its owner
/// states.
class BETTERCAD_THERMAL_EXPORT ThermalSparseMatrix {
public:
    using Index = std::uint64_t;

    [[nodiscard]] std::size_t rows() const noexcept { return rows_; }
    [[nodiscard]] std::size_t columns() const noexcept { return columns_; }
    [[nodiscard]] std::size_t nonZeros() const noexcept { return values_.size(); }

    /// The CSR arrays, for a solver that maps rather than copies.
    [[nodiscard]] std::span<const Index> rowStart() const noexcept { return rowStart_; }
    [[nodiscard]] std::span<const Index> innerIndices() const noexcept { return inner_; }
    [[nodiscard]] std::span<const double> values() const noexcept { return values_; }

    /// `A(row, column)` in SI, or zero if the pattern does not hold that pair or the
    /// pair is outside the matrix. A binary search within the row.
    [[nodiscard]] double coeffSi(std::size_t row, std::size_t column) const noexcept;

    /// `max |A(i,j) - A(j,i)|` over the stored pattern, in SI, with an absent
    /// transpose counted as zero. Never forms a transpose and never densifies.
    /// A non-square matrix has no symmetry and reports infinity.
    [[nodiscard]] double largestSymmetryError() const noexcept;

    /// The largest `|A(i,j)|`, in SI. Zero for an empty pattern.
    [[nodiscard]] double largestMagnitude() const noexcept;

    /// `A x`. Fails with InvalidArgument if @p x is not `columns()` long. The one
    /// operation the assembly invariants and a solver share; a serial row-order sum,
    /// so the answer is reproducible.
    [[nodiscard]] Result<std::vector<double>> multiply(std::span<const double> x) const;

    friend bool operator==(const ThermalSparseMatrix&, const ThermalSparseMatrix&) = default;

    /// Validates a CSR layout and takes ownership of it, or says why not.
    ///
    /// @p requiredSymmetryTolerance, if present, is a RELATIVE bound: the layout is
    /// refused unless `max |A(i,j) - A(j,i)| <= tolerance * largestMagnitude()`. An
    /// all-zero matrix passes. It is a gate and never a repair: nothing is
    /// symmetrised.
    struct Failure {
        MatrixProblem problem = MatrixProblem::DimensionsInconsistent;
        Error error{};
    };
    [[nodiscard]] static std::expected<ThermalSparseMatrix, Failure>
    fromCsr(std::size_t rows, std::size_t columns, std::vector<Index> rowStart, std::vector<Index> inner,
            std::vector<double> values, std::optional<double> requiredSymmetryTolerance = std::nullopt);

private:
    ThermalSparseMatrix() = default;

    std::size_t rows_ = 0;
    std::size_t columns_ = 0;
    std::vector<Index> rowStart_{};
    std::vector<Index> inner_{};
    std::vector<double> values_{};
};

/// A `ThermalSparseMatrix` whose entries are @p Entry, a dimensioned quantity.
///
/// A THIN, HEADER-ONLY WRAPPER, so that `K` and `C` are different TYPES: a
/// conductance (W/K) cannot be handed where a capacity (J/K) is wanted, and the
/// compile-fail group says so. It adds a typed accessor and nothing else; the
/// storage is the base's and so is its validation.
template <typename Entry>
class TypedThermalMatrix : public ThermalSparseMatrix {
public:
    /// Wrapping a validated matrix is safe by construction: possession of the base
    /// is already the evidence.
    explicit TypedThermalMatrix(ThermalSparseMatrix validated) : ThermalSparseMatrix(std::move(validated)) {}

    /// `A(row, column)` with its unit. Zero outside the pattern.
    [[nodiscard]] Entry coeff(std::size_t row, std::size_t column) const noexcept {
        return Entry::fromSi(coeffSi(row, column));
    }

    friend bool operator==(const TypedThermalMatrix&, const TypedThermalMatrix&) = default;
};

/// The global conduction matrix `K`, in W/K.
using ConductanceMatrix = TypedThermalMatrix<ThermalConductance>;
/// The global capacity matrix `C`, in J/K.
using CapacityMatrix = TypedThermalMatrix<HeatCapacity>;

/// The global heat-input vector `Q`, in W.
///
/// DENSE, length `Ndof`, and every entry finite: a vector holding a NaN or an
/// infinity does not exist. Zero-initialised by whoever builds it, because an
/// uninitialised right-hand side is the kind of defect that produces a plausible
/// answer.
class BETTERCAD_THERMAL_EXPORT ThermalHeatVector {
public:
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

    /// Entry @p row in W, or zero outside the vector.
    [[nodiscard]] Power operator[](std::size_t row) const noexcept {
        return row < entries_.size() ? Power::fromSi(entries_[row]) : Power{};
    }

    /// SI, for a solver that maps rather than copies.
    [[nodiscard]] std::span<const double> values() const noexcept { return entries_; }

    /// The sum over the vector in row order, so the answer is reproducible. This is
    /// the total heat the right-hand side carries, NOT a heat balance: the convective
    /// term is its ambient part only (`h T_inf A`), and the heat actually exchanged
    /// is `Kconv T - Qconv`, which needs a solution.
    [[nodiscard]] Power total() const noexcept;

    friend bool operator==(const ThermalHeatVector&, const ThermalHeatVector&) = default;

    /// Takes ownership of @p entries, or refuses a non-finite one.
    [[nodiscard]] static Result<ThermalHeatVector> fromEntries(std::vector<double> entries);

private:
    ThermalHeatVector() = default;

    std::vector<double> entries_{};
};

} // namespace bettercad::thermal
