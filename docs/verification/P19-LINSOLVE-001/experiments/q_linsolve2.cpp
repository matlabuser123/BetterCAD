// P19-LINSOLVE-001 measurements, round two. Scratch, OUTSIDE the frozen surface.
//
// Round one found three things that change the design:
//   Q1  a mis-routed asymmetric system is REFUSED, not silently solved, and the
//       normalized residual tracks the asymmetry linearly
//   Q3  the pressure shape is accepted, but pivotRatio came out 1.25e-07
//       INDEPENDENT OF n -- which says the PINNED diagonal of 1.0 sets max|D|
//       while the physical coefficients set min|D|
//   Q2  SparseLU reports Success with a ZERO residual on a system that is
//       singular to 1e-13, so info() + residual is NOT a sufficient gate, and
//       logAbsDeterminant is not scale-free enough to be one either
//
// This round answers:
//   Q5  where does the pressure system's pivot ratio cross pivotFloor = 1e-12
//       as the physical coefficient shrinks? That is a REFUSAL OF A CORRECT
//       SYSTEM, so the boundary must be known before a solver depends on it.
//   Q6  is Eigen SparseLU's U diagonal reachable, and does min|U_ii|/max|U_ii|
//       separate near-singular from well conditioned?
//   C   THE CONTROL. A deliberately wrong x must produce a large normalized
//       residual. Without it, "normalized = 0" proves nothing about the loop.

#include <bettercad/linalg/SymmetricSolve.hpp>

#include <Eigen/SparseCore>
#include <Eigen/SparseLU>

#include <cmath>
#include <limits>
#include <cstdio>
#include <vector>

namespace bl = bettercad::linalg;
using Index = bl::SparseIndex;

namespace {

struct Csr {
    std::size_t rows = 0;
    std::vector<Index> rowStart{};
    std::vector<Index> inner{};
    std::vector<double> values{};
};

Csr fromDense(const std::vector<std::vector<double>>& a) {
    Csr out;
    out.rows = a.size();
    out.rowStart.push_back(0);
    for (const std::vector<double>& row : a) {
        for (std::size_t j = 0; j < row.size(); ++j) {
            if (row[j] != 0.0) {
                out.inner.push_back(static_cast<Index>(j));
                out.values.push_back(row[j]);
            }
        }
        out.rowStart.push_back(static_cast<Index>(out.values.size()));
    }
    return out;
}

/// The pressure-correction shape: diagonal +sum a_f, off-diagonal -a_f, cell 0
/// pinned with a UNIT diagonal, which is what the qualified coupling does.
Csr pressureShape(std::size_t n, double af, double pinnedDiagonal) {
    std::vector<std::vector<double>> a(n, std::vector<double>(n, 0.0));
    for (std::size_t i = 0; i + 1 < n; ++i) {
        a[i][i] += af;
        a[i + 1][i + 1] += af;
        a[i][i + 1] -= af;
        a[i + 1][i] -= af;
    }
    for (std::size_t j = 0; j < n; ++j) {
        a[0][j] = 0.0;
        a[j][0] = 0.0;
    }
    a[0][0] = pinnedDiagonal;
    return fromDense(a);
}

using Sparse = Eigen::SparseMatrix<double>;
using EigenIndex = Sparse::StorageIndex;

Sparse toEigen(const Csr& m) {
    Sparse matrix(static_cast<Eigen::Index>(m.rows), static_cast<Eigen::Index>(m.rows));
    std::vector<Eigen::Triplet<double, EigenIndex>> triplets;
    for (std::size_t row = 0; row < m.rows; ++row) {
        for (Index slot = m.rowStart[row]; slot < m.rowStart[row + 1]; ++slot) {
            triplets.emplace_back(static_cast<EigenIndex>(row), static_cast<EigenIndex>(m.inner[slot]),
                                  m.values[slot]);
        }
    }
    matrix.setFromTriplets(triplets.begin(), triplets.end());
    return matrix;
}

/// The independent residual, from the ORIGINAL CSR, for ANY candidate x.
double normalizedResidual(const Csr& m, const std::vector<double>& rhs, const std::vector<double>& x) {
    double squared = 0.0;
    double matrixInfinity = 0.0;
    double solutionSquared = 0.0;
    double rhsSquared = 0.0;
    for (std::size_t row = 0; row < m.rows; ++row) {
        double product = 0.0;
        double rowSum = 0.0;
        for (Index slot = m.rowStart[row]; slot < m.rowStart[row + 1]; ++slot) {
            product += m.values[slot] * x[static_cast<std::size_t>(m.inner[slot])];
            rowSum += std::abs(m.values[slot]);
        }
        const double e = product - rhs[row];
        squared += e * e;
        matrixInfinity = std::max(matrixInfinity, rowSum);
        solutionSquared += x[row] * x[row];
        rhsSquared += rhs[row] * rhs[row];
    }
    const double denominator = matrixInfinity * std::sqrt(solutionSquared) + std::sqrt(rhsSquared);
    return denominator > 0.0 ? std::sqrt(squared) / denominator : std::sqrt(squared);
}

} // namespace

int main() {
    std::printf("Q5  THE PRESSURE SYSTEM'S PIVOT RATIO AGAINST THE PHYSICAL COEFFICIENT\n");
    std::printf("    pinned diagonal fixed at 1.0, as the qualified coupling writes it.\n");
    std::printf("    pivotFloor = 1e-12. A refusal here REFUSES A CORRECT SYSTEM.\n");
    std::printf("    %-12s %-8s %-18s %-14s %s\n", "a_f", "n", "verdict", "pivotRatio", "ratio/a_f");
    for (double af : {1.0e-3, 1.0e-6, 1.0e-7, 1.0e-9, 1.0e-12, 1.0e-13, 1.0e-15}) {
        for (std::size_t n : {8u, 64u}) {
            const Csr m = pressureShape(n, af, 1.0);
            std::vector<double> rhs(n, af * 1.0e-3);
            rhs[0] = 0.0;
            const bl::SymmetricSolveSettings s{};
            auto r = bl::solveSymmetricSparse(m.rows, m.rowStart, m.inner, m.values, rhs, s);
            const char* v = r.has_value() ? "SOLVED" : bl::toString(r.error().problem).data();
            const double pr = r.has_value() ? r->pivotRatio : r.error().pivotRatio;
            std::printf("    %-12.3e %-8zu %-18s %-14.6e %.4f\n", af, n, v, pr, af > 0.0 ? pr / af : 0.0);
        }
    }

    std::printf("\n    The same systems with the pinned diagonal set to a_f instead of 1.0:\n");
    for (double af : {1.0e-9, 1.0e-12, 1.0e-13, 1.0e-15}) {
        const Csr m = pressureShape(64, af, af);
        std::vector<double> rhs(64, af * 1.0e-3);
        rhs[0] = 0.0;
        const bl::SymmetricSolveSettings s{};
        auto r = bl::solveSymmetricSparse(m.rows, m.rowStart, m.inner, m.values, rhs, s);
        const char* v = r.has_value() ? "SOLVED" : bl::toString(r.error().problem).data();
        const double pr = r.has_value() ? r->pivotRatio : r.error().pivotRatio;
        std::printf("    a_f=%-12.3e n=64   %-18s pivotRatio=%.6e\n", af, v, pr);
    }

    std::printf("\nQ6  IS SPARSE LU'S U DIAGONAL REACHABLE, AND DOES ITS RATIO SEPARATE?\n");
    struct Case {
        const char* label;
        std::vector<std::vector<double>> a;
    };
    const std::vector<Case> cases = {
        {"well conditioned", {{1.0, 2.0, 0.0}, {3.0, 1.0, 0.0}, {0.0, 1.0, 3.0}}},
        {"near-singular 1e-13", {{1.0, 2.0, 0.0}, {1.0, 2.0 + 1.0e-13, 0.0}, {0.0, 1.0, 3.0}}},
        {"near-singular 1e-9", {{1.0, 2.0, 0.0}, {1.0, 2.0 + 1.0e-9, 0.0}, {0.0, 1.0, 3.0}}},
        {"tiny but well cond.", {{1.0e-9, 2.0e-9, 0.0}, {3.0e-9, 1.0e-9, 0.0}, {0.0, 1.0e-9, 3.0e-9}}},
    };
    for (const Case& c : cases) {
        const Csr m = fromDense(c.a);
        const Sparse matrix = toEigen(m);
        Eigen::SparseLU<Sparse, Eigen::COLAMDOrdering<EigenIndex>> lu;
        lu.analyzePattern(matrix);
        lu.factorize(matrix);
        if (lu.info() != Eigen::Success) {
            std::printf("    %-22s info != Success: %s\n", c.label, lu.lastErrorMessage().c_str());
            continue;
        }
        // THE QUESTION: can U's diagonal be read? Reconstruct U densely by
        // applying the U solve to the identity -- the only handle the public
        // API gives -- and read its diagonal from that.
        const Eigen::Index n = matrix.rows();
        Eigen::MatrixXd identity = Eigen::MatrixXd::Identity(n, n);
        Eigen::MatrixXd uInverse = identity;
        lu.matrixU().solveInPlace(uInverse);
        // U^-1 is upper triangular with diagonal 1/U_ii, so U_ii = 1/(U^-1)_ii.
        double smallest = std::numeric_limits<double>::infinity();
        double largest = 0.0;
        for (Eigen::Index i = 0; i < n; ++i) {
            const double uii = 1.0 / uInverse(i, i);
            smallest = std::min(smallest, std::abs(uii));
            largest = std::max(largest, std::abs(uii));
        }
        const double ratio = largest > 0.0 ? smallest / largest : 0.0;
        std::printf("    %-22s U-diagonal ratio = %-14.6e logAbsDet = %+.4e\n", c.label, ratio,
                    lu.logAbsDeterminant());
    }

    std::printf("\nC   THE CONTROL: a deliberately WRONG x must score large\n");
    {
        const Csr m = fromDense({{4.0, -1.0, 0.0}, {-1.0, 4.0, -1.0}, {0.0, -1.0, 4.0}});
        const std::vector<double> rhs = {1.0, 2.0, 3.0};
        const bl::SymmetricSolveSettings s{};
        auto r = bl::solveSymmetricSparse(m.rows, m.rowStart, m.inner, m.values, rhs, s);
        std::printf("    the kernel's own x          normalized = %.6e\n",
                    normalizedResidual(m, rhs, r->x));
        std::vector<double> wrong = r->x;
        wrong[1] += 1.0;
        std::printf("    x with 1.0 added to x[1]    normalized = %.6e\n", normalizedResidual(m, rhs, wrong));
        std::vector<double> zero(3, 0.0);
        std::printf("    x = 0                       normalized = %.6e\n", normalizedResidual(m, rhs, zero));
    }

    std::printf("\ndone\n");
    return 0;
}
