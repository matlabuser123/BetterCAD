// P19-LINSOLVE-001 measurements, round four. Scratch, OUTSIDE the frozen surface.
//
// ROUND THREE'S MOMENTUM FIXTURE WAS SINGULAR, AND ITS OWN OUTPUT SAID SO.
// "LU vs QR largest component gap 1.574122e+12" came out IDENTICAL at n = 200,
// 1000 and 4000. One number for three different systems cannot be a property of
// the systems; it is a property of the fixture. A singular system has a family
// of solutions, LU and QR pick different members, and both report a tiny
// residual -- which is why the gap was huge while every residual looked perfect.
// The chain was anchored at one end only.
//
// This round anchors BOTH ends the way a finite-volume boundary does, asserts
// rank == n BEFORE believing any other number, and only then re-measures:
//   A  does the symmetric kernel refuse a WELL-POSED asymmetric system?
//   B  do LU and QR agree on it, as two solvers of a non-singular system must?
//   C  what does each cost, and what does each expose for conditioning?

#include <bettercad/linalg/SymmetricSolve.hpp>

#include <Eigen/SparseCore>
#include <Eigen/SparseLU>
#include <Eigen/SparseQR>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

namespace bl = bettercad::linalg;
using Index = bl::SparseIndex;
using Sparse = Eigen::SparseMatrix<double>;
using EigenIndex = Sparse::StorageIndex;

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

/// A WELL-POSED 1D convection-diffusion chain, Dirichlet at both ends.
/// Interior faces couple i and i+1; the two boundary faces add to the end
/// diagonals without an off-diagonal, exactly as a FixedValue facet does.
/// q >= 0 is the mass flux, flowing from 0 toward n-1, upwinded.
Csr chain(std::size_t n, double d, double q) {
    std::vector<std::vector<double>> a(n, std::vector<double>(n, 0.0));
    for (std::size_t i = 0; i + 1 < n; ++i) {
        a[i][i] += d;
        a[i + 1][i + 1] += d;
        a[i][i + 1] -= d;
        a[i + 1][i] -= d;
        // upwind: the face takes its value from the upstream cell i
        a[i][i] += q;      // outflow from i
        a[i + 1][i] -= q;  // inflow to i+1 from i
    }
    // the two Dirichlet boundary faces
    a[0][0] += d + q;          // inlet: diffusion plus incoming convection
    a[n - 1][n - 1] += d;      // outlet: diffusion; convection leaves, see below
    a[n - 1][n - 1] += q;      // outflow through the outlet face
    return fromDense(a);
}

double ratioOf(const Eigen::VectorXd& v) {
    double smallest = std::numeric_limits<double>::infinity();
    double largest = 0.0;
    for (Eigen::Index i = 0; i < v.size(); ++i) {
        const double x = std::abs(v(i));
        smallest = std::min(smallest, x);
        largest = std::max(largest, x);
    }
    return largest > 0.0 ? smallest / largest : 0.0;
}

double normalizedResidual(const Csr& m, const std::vector<double>& rhs, const Eigen::VectorXd& x) {
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
    const double den = matrixInfinity * std::sqrt(solutionSquared) + std::sqrt(rhsSquared);
    return den > 0.0 ? std::sqrt(squared) / den : std::sqrt(squared);
}

} // namespace

int main() {
    std::printf("THE FIXTURE IS CHECKED FIRST: rank must be n, or nothing below means anything\n");
    std::printf("    %-8s %-12s %-10s %-12s %s\n", "n", "q", "rank", "n", "well posed");
    bool allFullRank = true;
    for (std::size_t n : {8u, 200u, 1000u}) {
        for (double q : {0.0, 1.0e-6, 1.0e-3}) {
            const Sparse matrix = toEigen(chain(n, 1.0e-6, q));
            Eigen::SparseQR<Sparse, Eigen::COLAMDOrdering<EigenIndex>> qr;
            qr.compute(matrix);
            const long long rank = static_cast<long long>(qr.rank());
            const bool ok = rank == static_cast<long long>(n);
            allFullRank = allFullRank && ok;
            std::printf("    %-8zu %-12.3e %-10lld %-12zu %s\n", n, q, rank, n, ok ? "yes" : "*** NO ***");
        }
    }
    if (!allFullRank) {
        std::printf("\n    THE FIXTURE IS STILL SINGULAR. Stopping rather than publishing numbers.\n");
        return 1;
    }

    std::printf("\nA   DOES THE SYMMETRIC KERNEL REFUSE A WELL-POSED ASYMMETRIC SYSTEM?\n");
    std::printf("    %-12s %-22s %-20s %s\n", "q", "largest asymmetry", "symmetric kernel", "normalized");
    for (double q : {0.0, 1.0e-12, 1.0e-9, 1.0e-7, 1.0e-6, 1.0e-3}) {
        const std::size_t n = 64;
        const Csr m = chain(n, 1.0e-6, q);
        // the largest |A(i,j) - A(j,i)|, measured rather than predicted
        const Sparse matrix = toEigen(m);
        double asym = 0.0;
        for (Eigen::Index i = 0; i < matrix.rows(); ++i) {
            for (Eigen::Index j = 0; j < matrix.rows(); ++j) {
                asym = std::max(asym, std::abs(matrix.coeff(i, j) - matrix.coeff(j, i)));
            }
        }
        std::vector<double> rhs(n, 0.0);
        rhs[0] = (1.0e-6 + q) * 1.0e-3;
        const bl::SymmetricSolveSettings s{};
        auto r = bl::solveSymmetricSparse(m.rows, m.rowStart, m.inner, m.values, rhs, s);
        const char* v = r.has_value() ? "SOLVED" : bl::toString(r.error().problem).data();
        const double nr = r.has_value() ? r->residual.normalized : r.error().residual.normalized;
        std::printf("    %-12.3e %-22.6e %-20s %.6e\n", q, asym, v, nr);
    }

    std::printf("\nB+C LU AND QR ON THE SAME WELL-POSED SYSTEMS: agreement, cost, conditioning\n");
    std::printf("    %-7s %-11s %-11s %-8s %-13s %-13s %-13s %s\n", "n", "LU s", "QR s", "QR/LU", "LU normalized",
                "QR normalized", "R-diag ratio", "largest gap");
    for (std::size_t n : {200u, 1000u, 4000u}) {
        const double q = 1.0e-6;
        const Csr m = chain(n, 1.0e-6, q);
        const Sparse matrix = toEigen(m);
        std::vector<double> rhs(n, 0.0);
        rhs[0] = (1.0e-6 + q) * 1.0e-3;
        Eigen::VectorXd b = Eigen::VectorXd::Zero(static_cast<Eigen::Index>(n));
        b(0) = rhs[0];

        auto t0 = std::chrono::steady_clock::now();
        Eigen::SparseLU<Sparse, Eigen::COLAMDOrdering<EigenIndex>> lu;
        lu.analyzePattern(matrix);
        lu.factorize(matrix);
        const Eigen::VectorXd xlu = lu.solve(b);
        auto t1 = std::chrono::steady_clock::now();

        Eigen::SparseQR<Sparse, Eigen::COLAMDOrdering<EigenIndex>> qr;
        qr.compute(matrix);
        const Eigen::VectorXd xqr = qr.solve(b);
        auto t2 = std::chrono::steady_clock::now();

        const double luS = std::chrono::duration<double>(t1 - t0).count();
        const double qrS = std::chrono::duration<double>(t2 - t1).count();
        const Sparse r = qr.matrixR();
        Eigen::VectorXd diagonal(r.rows());
        for (Eigen::Index i = 0; i < r.rows(); ++i) {
            diagonal(i) = r.coeff(i, i);
        }
        double gap = 0.0;
        double scale = 0.0;
        for (Eigen::Index i = 0; i < xlu.size(); ++i) {
            gap = std::max(gap, std::abs(xlu(i) - xqr(i)));
            scale = std::max(scale, std::abs(xlu(i)));
        }
        std::printf("    %-7zu %-11.6f %-11.6f %-8.2f %-13.6e %-13.6e %-13.6e %.6e (rel %.3e)\n", n, luS, qrS,
                    luS > 0.0 ? qrS / luS : 0.0, normalizedResidual(m, rhs, xlu), normalizedResidual(m, rhs, xqr),
                    ratioOf(diagonal), gap, scale > 0.0 ? gap / scale : 0.0);
    }

    std::printf("\ndone\n");
    return 0;
}
