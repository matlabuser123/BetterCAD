// P19-LINSOLVE-001 measurements. Scratch program, OUTSIDE the frozen surface.
//
// Q1  What does linalg::solveSymmetricSparse actually DO with an asymmetric
//     input? It performs no symmetry check, so the answer is a property of
//     Eigen's SimplicialLDLT plus the independent residual gate. Predicted:
//     refused as ResidualTooLarge. Predictions about libraries get measured.
// Q2  Does Eigen's SparseLU expose a conditioning measure analogous to the
//     LDLT pivot ratio min|D|/max|D|, and what does it report on a singular
//     non-symmetric system?
// Q3  Is the pressure-correction matrix's SHAPE accepted by the LDLT gate,
//     which demands every pivot positive? Diagonal +sum a_f, off-diagonal
//     -a_f, one row pinned.
// Q4  Is a momentum-shaped matrix (upwind convection + diffusion) refused by
//     the symmetric kernel, and solved by SparseLU?

#include <bettercad/linalg/SymmetricSolve.hpp>

#include <Eigen/SparseCore>
#include <Eigen/SparseLU>

#include <cmath>
#include <cstdio>
#include <string>
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

/// A dense-built CSR so the test matrices stay readable.
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

const char* verdict(const Csr& m, const std::vector<double>& rhs, const bl::SymmetricSolveSettings& s,
                    double* normalized, double* pivotRatio, bool* positive) {
    std::expected<bl::SymmetricSolution, bl::SymmetricSolveFailure> r =
        bl::solveSymmetricSparse(m.rows, m.rowStart, m.inner, m.values, rhs, s);
    if (r.has_value()) {
        *normalized = r->residual.normalized;
        *pivotRatio = r->pivotRatio;
        *positive = true;
        return "SOLVED";
    }
    *normalized = r.error().residual.normalized;
    *pivotRatio = r.error().pivotRatio;
    *positive = r.error().allPivotsPositive;
    return bl::toString(r.error().problem).data();
}

/// Eigen's general LU, with every conditioning handle the version exposes.
void luReport(const Csr& m, const std::vector<double>& rhs, const char* label) {
    using Sparse = Eigen::SparseMatrix<double>;
    using EigenIndex = Sparse::StorageIndex;
    Sparse matrix(static_cast<Eigen::Index>(m.rows), static_cast<Eigen::Index>(m.rows));
    std::vector<Eigen::Triplet<double, EigenIndex>> triplets;
    for (std::size_t row = 0; row < m.rows; ++row) {
        for (Index slot = m.rowStart[row]; slot < m.rowStart[row + 1]; ++slot) {
            triplets.emplace_back(static_cast<EigenIndex>(row), static_cast<EigenIndex>(m.inner[slot]),
                                  m.values[slot]);
        }
    }
    matrix.setFromTriplets(triplets.begin(), triplets.end());

    Eigen::VectorXd b(static_cast<Eigen::Index>(m.rows));
    for (std::size_t i = 0; i < m.rows; ++i) {
        b(static_cast<Eigen::Index>(i)) = rhs[i];
    }

    Eigen::SparseLU<Sparse, Eigen::COLAMDOrdering<EigenIndex>> lu;
    lu.analyzePattern(matrix);
    lu.factorize(matrix);
    std::printf("  %-26s info=%-14s", label,
                lu.info() == Eigen::Success ? "Success"
                    : lu.info() == Eigen::NumericalIssue ? "NumericalIssue"
                    : lu.info() == Eigen::InvalidInput ? "InvalidInput" : "NoConvergence");
    if (lu.info() != Eigen::Success) {
        std::printf(" lastError=\"%s\"\n", lu.lastErrorMessage().c_str());
        return;
    }
    const double logAbsDet = lu.logAbsDeterminant();
    const Eigen::VectorXd x = lu.solve(b);

    // The independent residual, computed here from the ORIGINAL CSR exactly as
    // linalg::acceptSolution does, so the two paths are judged alike.
    double squared = 0.0;
    double matrixInfinity = 0.0;
    double solutionSquared = 0.0;
    double rhsSquared = 0.0;
    for (std::size_t row = 0; row < m.rows; ++row) {
        double product = 0.0;
        double rowSum = 0.0;
        for (Index slot = m.rowStart[row]; slot < m.rowStart[row + 1]; ++slot) {
            product += m.values[slot] * x(static_cast<Eigen::Index>(m.inner[slot]));
            rowSum += std::abs(m.values[slot]);
        }
        const double e = product - rhs[row];
        squared += e * e;
        matrixInfinity = std::max(matrixInfinity, rowSum);
        solutionSquared += x(static_cast<Eigen::Index>(row)) * x(static_cast<Eigen::Index>(row));
        rhsSquared += rhs[row] * rhs[row];
    }
    const double denominator = matrixInfinity * std::sqrt(solutionSquared) + std::sqrt(rhsSquared);
    const double normalized = denominator > 0.0 ? std::sqrt(squared) / denominator : std::sqrt(squared);
    std::printf(" logAbsDet=%+.6e normalized=%.6e nnzL=%lld nnzU=%lld\n", logAbsDet, normalized,
                static_cast<long long>(lu.nnzL()), static_cast<long long>(lu.nnzU()));
}

} // namespace

int main() {
    const bl::SymmetricSolveSettings settings{};
    double normalized = 0.0;
    double pivotRatio = 0.0;
    bool positive = false;

    std::printf("Q1  AN ASYMMETRIC MATRIX HANDED TO THE SYMMETRIC KERNEL\n");
    std::printf("    base is symmetric; A(0,1) is perturbed by `skew` and A(1,0) is not.\n");
    std::printf("    %-14s %-18s %-14s %-10s %s\n", "skew", "verdict", "normalized", "pivotRatio", "allPivotsPositive");
    for (double skew : {0.0, 1.0e-16, 1.0e-14, 1.0e-12, 1.0e-9, 1.0e-6, 1.0e-3, 1.0e-1, 1.0, 10.0}) {
        std::vector<std::vector<double>> a = {{4.0, -1.0, 0.0}, {-1.0, 4.0, -1.0}, {0.0, -1.0, 4.0}};
        a[0][1] += skew;
        const Csr m = fromDense(a);
        const std::vector<double> rhs = {1.0, 2.0, 3.0};
        const char* v = verdict(m, rhs, settings, &normalized, &pivotRatio, &positive);
        std::printf("    %-14.3e %-18s %-14.6e %-10.4f %s\n", skew, v, normalized, pivotRatio,
                    positive ? "yes" : "NO");
    }

    std::printf("\nQ3  THE PRESSURE-CORRECTION SHAPE: diagonal +sum a_f, off-diagonal -a_f\n");
    for (std::size_t n : {4u, 16u, 64u}) {
        std::vector<std::vector<double>> a(n, std::vector<double>(n, 0.0));
        const double af = 2.5e-7; // a representative coefficient magnitude
        for (std::size_t i = 0; i + 1 < n; ++i) {
            a[i][i] += af;
            a[i + 1][i + 1] += af;
            a[i][i + 1] -= af;
            a[i + 1][i] -= af;
        }
        // Cell 0 pinned, exactly as the coupling pins one cell per region.
        for (std::size_t j = 0; j < n; ++j) {
            a[0][j] = 0.0;
            a[j][0] = 0.0;
        }
        a[0][0] = 1.0;
        const Csr m = fromDense(a);
        std::vector<double> rhs(n, 1.0e-9);
        rhs[0] = 0.0;
        const char* v = verdict(m, rhs, settings, &normalized, &pivotRatio, &positive);
        std::printf("    n=%-5zu %-18s normalized=%-14.6e pivotRatio=%-12.6e allPivotsPositive=%s\n", n, v,
                    normalized, pivotRatio, positive ? "yes" : "NO");
    }

    std::printf("\nQ4  A MOMENTUM-SHAPED MATRIX: upwind convection + diffusion\n");
    std::printf("    upwind writes A(P,P)=+q and A(N,P)=-q*w, which never pairs symmetrically.\n");
    for (double q : {0.0, 1.0e-9, 1.0e-6, 1.0e-4}) {
        const std::size_t n = 8;
        std::vector<std::vector<double>> a(n, std::vector<double>(n, 0.0));
        const double d = 1.0e-6; // diffusion coefficient, symmetric
        for (std::size_t i = 0; i + 1 < n; ++i) {
            a[i][i] += d;
            a[i + 1][i + 1] += d;
            a[i][i + 1] -= d;
            a[i + 1][i] -= d;
            // upwind convection, flow from i to i+1
            a[i][i] += q;
            a[i + 1][i] -= q;
        }
        a[0][0] += d + q; // an inlet anchoring the system
        const Csr m = fromDense(a);
        std::vector<double> rhs(n, 0.0);
        rhs[0] = (d + q) * 1.0e-3;
        const char* v = verdict(m, rhs, settings, &normalized, &pivotRatio, &positive);
        std::printf("    q=%-10.3e symmetric kernel: %-18s normalized=%-14.6e\n", q, v, normalized);
        luReport(m, rhs, "general LU");
    }

    std::printf("\nQ2  SPARSE LU ON A SINGULAR NON-SYMMETRIC SYSTEM\n");
    {
        // Two identical rows: exactly singular.
        const Csr m = fromDense({{1.0, 2.0, 0.0}, {1.0, 2.0, 0.0}, {0.0, 1.0, 3.0}});
        const std::vector<double> rhs = {1.0, 1.0, 1.0};
        luReport(m, rhs, "singular (duplicate row)");
        const Csr m2 = fromDense({{1.0, 2.0, 0.0}, {1.0, 2.0 + 1.0e-13, 0.0}, {0.0, 1.0, 3.0}});
        luReport(m2, rhs, "near-singular 1e-13");
        const Csr m3 = fromDense({{1.0, 2.0, 0.0}, {3.0, 1.0, 0.0}, {0.0, 1.0, 3.0}});
        luReport(m3, rhs, "well conditioned");
    }

    std::printf("\ndone\n");
    return 0;
}
