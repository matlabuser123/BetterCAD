#pragma once

// Shared fixtures for the P18-SOLVE-001 tests.
//
// THE ORACLES ARE INDEPENDENT BY CONSTRUCTION. The expected temperatures of the
// small systems are typed in from a hand derivation, and the comparison solve
// (`denseSolve`) is a Gaussian elimination with partial pivoting written HERE, in
// the test, on a dense array. It calls no production solve, no production
// reduction and no `linalg` function: the production path is the thing under
// test, and a test that asked it for the answer would be a mirror.

#include "ThermalAssemblyTestSupport.hpp"

#include <bettercad/linalg/SymmetricSolve.hpp>
#include <bettercad/thermal/ThermalSolve.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace sotest {

using namespace actest;
using thermal::SteadyResidual;
using thermal::SteadySolveFailure;
using thermal::SteadySolveProblem;
using thermal::SteadyThermalSolution;
using thermal::ThermalAlgebraicSolution;
using thermal::ThermalConstraintMap;
using thermal::ThermalFreeEquationMap;
using thermal::ThermalHeatVector;
using thermal::ThermalSolverSettings;
using thermal::ThermalSparseMatrix;
using thermal::ThermalTemperatureConstraint;

using Dense2 = std::vector<std::vector<double>>;

/// A dense matrix as a `ThermalSparseMatrix`: every NONZERO entry, rows ascending.
[[nodiscard]] inline ThermalSparseMatrix sparseOf(const Dense2& dense) {
    const std::size_t n = dense.size();
    std::vector<ThermalSparseMatrix::Index> rowStart(n + 1, 0);
    std::vector<ThermalSparseMatrix::Index> inner;
    std::vector<double> values;
    for (std::size_t row = 0; row < n; ++row) {
        for (std::size_t column = 0; column < n; ++column) {
            if (dense[row][column] != 0.0) {
                inner.push_back(static_cast<ThermalSparseMatrix::Index>(column));
                values.push_back(dense[row][column]);
            }
        }
        rowStart[row + 1] = static_cast<ThermalSparseMatrix::Index>(inner.size());
    }
    auto built = ThermalSparseMatrix::fromCsr(n, n, std::move(rowStart), std::move(inner), std::move(values));
    INFO((built.has_value() ? std::string{} : built.error().error.message));
    REQUIRE(built.has_value());
    return std::move(*built);
}

[[nodiscard]] inline ThermalHeatVector heatOf(std::vector<double> q) {
    auto built = ThermalHeatVector::fromEntries(std::move(q));
    REQUIRE(built.has_value());
    return std::move(*built);
}

/// A system given directly as a matrix and a vector, with the numbering and the
/// prescribed temperatures the solve needs. The numbering comes from a hand mesh
/// of `n` nodes on a line (no elements: the matrix is the model).
struct Algebra {
    AsmHand hand;
    ThermalSparseMatrix k;
    ThermalHeatVector q;
    ThermalConstraintMap constraints;
    ThermalFreeEquationMap equations;
    Dense2 dense;
    std::vector<double> rhs;
};

[[nodiscard]] inline Algebra makeAlgebra(const Dense2& dense, const std::vector<double>& rhs,
                                         const std::vector<std::pair<std::size_t, double>>& prescribed) {
    const std::size_t n = dense.size();
    Nodes nodes;
    for (std::size_t i = 0; i < n; ++i) {
        nodes.emplace_back(static_cast<NodeId::ValueType>(i + 1), at(static_cast<double>(i), 0, 0));
    }
    AsmHand hand = makeAsmHand(nodes, {}, {});
    std::vector<ThermalTemperatureConstraint> list;
    for (const auto& [dof, kelvin] : prescribed) {
        list.push_back(ThermalTemperatureConstraint{dofIx(dof), Temperature::fromSi(kelvin)});
    }
    auto constraints = thermal::buildThermalConstraintMap(hand.dofs, list);
    INFO((constraints.has_value() ? std::string{} : constraints.error().message));
    REQUIRE(constraints.has_value());
    auto equations = thermal::buildThermalFreeEquationMap(hand.dofs, *constraints);
    REQUIRE(equations.has_value());
    return Algebra{std::move(hand), sparseOf(dense), heatOf(rhs), std::move(*constraints), std::move(*equations), dense, rhs};
}

[[nodiscard]] inline std::expected<ThermalAlgebraicSolution, SteadySolveFailure>
solveAlgebra(const Algebra& a, const ThermalSolverSettings& settings = {}) {
    return thermal::solveThermalAlgebra(a.k, a.q, a.equations, a.constraints, settings);
}

/// Requires success and returns the solution.
[[nodiscard]] inline ThermalAlgebraicSolution mustSolve(const Algebra& a, const ThermalSolverSettings& settings = {}) {
    auto solved = solveAlgebra(a, settings);
    INFO((solved.has_value() ? std::string{} : solved.error().error.message));
    REQUIRE(solved.has_value());
    return std::move(*solved);
}

template <typename T>
[[nodiscard]] std::optional<SteadySolveProblem> problemOf(const std::expected<T, SteadySolveFailure>& solved) {
    if (solved.has_value()) {
        return std::nullopt;
    }
    return solved.error().problem;
}
[[nodiscard]] inline double tempAt(const ThermalAlgebraicSolution& s, std::size_t dof) { return s.temperatures[dof].si(); }

/// Gaussian elimination with partial pivoting on a dense copy. THE TEST'S OWN
/// ORACLE: nothing production is called.
[[nodiscard]] inline std::vector<double> denseSolve(Dense2 a, std::vector<double> b) {
    const std::size_t n = b.size();
    for (std::size_t col = 0; col < n; ++col) {
        std::size_t pivot = col;
        for (std::size_t row = col + 1; row < n; ++row) {
            if (std::abs(a[row][col]) > std::abs(a[pivot][col])) {
                pivot = row;
            }
        }
        std::swap(a[col], a[pivot]);
        std::swap(b[col], b[pivot]);
        for (std::size_t row = col + 1; row < n; ++row) {
            const double factor = a[row][col] / a[col][col];
            for (std::size_t c = col; c < n; ++c) {
                a[row][c] -= factor * a[col][c];
            }
            b[row] -= factor * b[col];
        }
    }
    std::vector<double> x(n, 0.0);
    for (std::size_t i = n; i-- > 0;) {
        double sum = b[i];
        for (std::size_t c = i + 1; c < n; ++c) {
            sum -= a[i][c] * x[c];
        }
        x[i] = sum / a[i][i];
    }
    return x;
}

/// The expected full temperature vector of `dense T = rhs` with the unknowns in
/// @p prescribed held, by the partition written out in the test: gather the free
/// rows and columns, subtract the prescribed columns, eliminate densely.
[[nodiscard]] inline std::vector<double> denseExpected(const Dense2& dense, const std::vector<double>& rhs,
                                                       const std::vector<std::pair<std::size_t, double>>& prescribed) {
    const std::size_t n = dense.size();
    std::vector<bool> isPrescribed(n, false);
    std::vector<double> value(n, 0.0);
    for (const auto& [dof, kelvin] : prescribed) {
        isPrescribed[dof] = true;
        value[dof] = kelvin;
    }
    std::vector<std::size_t> freeList;
    for (std::size_t i = 0; i < n; ++i) {
        if (!isPrescribed[i]) {
            freeList.push_back(i);
        }
    }
    Dense2 a(freeList.size(), std::vector<double>(freeList.size(), 0.0));
    std::vector<double> b(freeList.size(), 0.0);
    for (std::size_t r = 0; r < freeList.size(); ++r) {
        double right = rhs[freeList[r]];
        for (std::size_t c = 0; c < n; ++c) {
            if (isPrescribed[c]) {
                right -= dense[freeList[r]][c] * value[c];
            }
        }
        b[r] = right;
        for (std::size_t c = 0; c < freeList.size(); ++c) {
            a[r][c] = dense[freeList[r]][freeList[c]];
        }
    }
    const std::vector<double> x = denseSolve(a, b);
    std::vector<double> full = value;
    for (std::size_t r = 0; r < freeList.size(); ++r) {
        full[freeList[r]] = x[r];
    }
    return full;
}

/// `dense x`, written out.
[[nodiscard]] inline std::vector<double> multiplyDense(const Dense2& a, const std::vector<double>& x) {
    std::vector<double> out(a.size(), 0.0);
    for (std::size_t i = 0; i < a.size(); ++i) {
        for (std::size_t j = 0; j < a.size(); ++j) {
            out[i] += a[i][j] * x[j];
        }
    }
    return out;
}

[[nodiscard]] inline std::vector<double> valuesOf(const ThermalAlgebraicSolution& s) {
    std::vector<double> out;
    for (const Temperature t : s.temperatures) {
        out.push_back(t.si());
    }
    return out;
}

/// A deterministic symmetric positive definite matrix of order @p n: diagonally
/// dominant with a pseudo-random sparse off-diagonal pattern. A linear
/// congruential generator, so the same matrix on every machine.
[[nodiscard]] inline Dense2 spdMatrix(std::size_t n, std::uint64_t seed) {
    Dense2 a(n, std::vector<double>(n, 0.0));
    std::uint64_t state = seed;
    const auto next = [&state]() {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<double>((state >> 33) % 1000) / 1000.0;
    };
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 1; j < n; ++j) {
            if (next() < 0.4) {
                const double g = 0.5 + 4.0 * next();
                a[i][j] = -g;
                a[j][i] = -g;
            }
        }
    }
    for (std::size_t i = 0; i < n; ++i) {
        double sum = 0.0;
        for (std::size_t j = 0; j < n; ++j) {
            sum += std::abs(a[i][j]);
        }
        a[i][i] = sum + 1.0 + next();  // strictly dominant, so SPD
    }
    return a;
}

/// A structured tetrahedral grid of `side` nodes per direction, spacing @p h: six
/// positively oriented tetrahedra per cell. Node `(i,j,k)` has handle
/// `(k*side + j)*side + i + 1` and therefore unknown `(k*side + j)*side + i`.
[[nodiscard]] inline AsmHand makeKuhnGrid(std::size_t side, double h) {
    const auto index = [side](std::size_t i, std::size_t j, std::size_t k) { return (k * side + j) * side + i; };
    Nodes nodes;
    nodes.reserve(side * side * side);
    for (std::size_t k = 0; k < side; ++k) {
        for (std::size_t j = 0; j < side; ++j) {
            for (std::size_t i = 0; i < side; ++i) {
                nodes.emplace_back(static_cast<NodeId::ValueType>(index(i, j, k) + 1),
                                   at(h * static_cast<double>(i), h * static_cast<double>(j), h * static_cast<double>(k)));
            }
        }
    }
    const auto determinant = [&](const std::array<std::size_t, 4>& t) {
        const Point3D a = nodes[t[0]].second;
        const Point3D b = nodes[t[1]].second;
        const Point3D c = nodes[t[2]].second;
        const Point3D d = nodes[t[3]].second;
        const double e1x = b.x.si() - a.x.si();
        const double e1y = b.y.si() - a.y.si();
        const double e1z = b.z.si() - a.z.si();
        const double e2x = c.x.si() - a.x.si();
        const double e2y = c.y.si() - a.y.si();
        const double e2z = c.z.si() - a.z.si();
        const double e3x = d.x.si() - a.x.si();
        const double e3y = d.y.si() - a.y.si();
        const double e3z = d.z.si() - a.z.si();
        return e1x * (e2y * e3z - e2z * e3y) + e1y * (e2z * e3x - e2x * e3z) + e1z * (e2x * e3y - e2y * e3x);
    };
    std::vector<std::array<std::size_t, 4>> tets;
    for (std::size_t k = 0; k + 1 < side; ++k) {
        for (std::size_t j = 0; j + 1 < side; ++j) {
            for (std::size_t i = 0; i + 1 < side; ++i) {
                const std::size_t n000 = index(i, j, k);
                const std::size_t n100 = index(i + 1, j, k);
                const std::size_t n010 = index(i, j + 1, k);
                const std::size_t n110 = index(i + 1, j + 1, k);
                const std::size_t n001 = index(i, j, k + 1);
                const std::size_t n101 = index(i + 1, j, k + 1);
                const std::size_t n011 = index(i, j + 1, k + 1);
                const std::size_t n111 = index(i + 1, j + 1, k + 1);
                const std::array<std::array<std::size_t, 4>, 6> cell{{{n000, n100, n110, n111}, {n000, n100, n101, n111},
                                                                      {n000, n010, n110, n111}, {n000, n010, n011, n111},
                                                                      {n000, n001, n101, n111}, {n000, n001, n011, n111}}};
                for (std::array<std::size_t, 4> t : cell) {
                    if (determinant(t) < 0.0) {
                        std::swap(t[2], t[3]);
                    }
                    tets.push_back(t);
                }
            }
        }
    }
    return makeAsmHand(nodes, {}, tets);
}

} // namespace sotest
