// P19-LINSOLVE-001 measurements, round three. Scratch, OUTSIDE the frozen surface.
//
// ROUND TWO'S Q5 HAD A FALSE PREMISE, AND THE HEADER IT WAS ABOUT SAYS SO.
// It pinned the reference cell with a diagonal of 1.0 and called that "what the
// qualified coupling writes". CfdCoupling.hpp actually says the opposite: the
// rule is "the diagonal keeps its own value", and a dimensionless 1.0 is a
// FALLBACK for one degenerate case -- a reference cell with no internal face and
// no fixed-pressure face -- reported through `pinnedDiagonalIsUnitValued`.
// So round two measured the exception as if it were the rule.
//
// Q5' re-measures BOTH paths, correctly labelled, because the exception is a
//     real if narrow hazard and the law behind it is worth having.
// Q7  is the open design question: the non-symmetric kernel needs a SCALE-FREE
//     conditioning gate, because Q2 measured SparseLU reporting Success with a
//     ZERO residual on a system singular to 1e-13. Q6 found SparseLU's U
//     diagonal reachable only by inverting U into a DENSE identity -- 272 MB at
//     P18's smoke size of 5832 unknowns, so unusable. Does SparseQR expose its
//     R factor as a real sparse matrix whose diagonal can be read directly, and
//     does min|R_ii|/max|R_ii| separate the same cases?

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

/// The pressure-correction shape. `pinKeepsItsOwnValue` is the RULE; the unit
/// fallback is the exception `pinnedDiagonalIsUnitValued` reports.
Csr pressureShape(std::size_t n, double af, bool pinKeepsItsOwnValue) {
    std::vector<std::vector<double>> a(n, std::vector<double>(n, 0.0));
    for (std::size_t i = 0; i + 1 < n; ++i) {
        a[i][i] += af;
        a[i + 1][i + 1] += af;
        a[i][i + 1] -= af;
        a[i + 1][i] -= af;
    }
    const double kept = a[0][0];
    for (std::size_t j = 0; j < n; ++j) {
        a[0][j] = 0.0;
        a[j][0] = 0.0;
    }
    a[0][0] = pinKeepsItsOwnValue ? kept : 1.0;
    return fromDense(a);
}

/// A 1D convection-diffusion chain: the momentum shape, asymmetric for q != 0.
Csr momentumShape(std::size_t n, double d, double q) {
    std::vector<std::vector<double>> a(n, std::vector<double>(n, 0.0));
    for (std::size_t i = 0; i + 1 < n; ++i) {
        a[i][i] += d;
        a[i + 1][i + 1] += d;
        a[i][i + 1] -= d;
        a[i + 1][i] -= d;
        a[i][i] += q;
        a[i + 1][i] -= q;
    }
    a[0][0] += d + q;
    return fromDense(a);
}

double ratioOfDiagonal(const Eigen::VectorXd& diagonal) {
    double smallest = std::numeric_limits<double>::infinity();
    double largest = 0.0;
    for (Eigen::Index i = 0; i < diagonal.size(); ++i) {
        const double v = std::abs(diagonal(i));
        smallest = std::min(smallest, v);
        largest = std::max(largest, v);
    }
    return largest > 0.0 ? smallest / largest : 0.0;
}

} // namespace

int main() {
    std::printf("Q5' THE PRESSURE SHAPE, BOTH PIN PATHS, CORRECTLY LABELLED\n");
    std::printf("    pivotFloor = 1e-12. n = 64 throughout.\n");
    std::printf("    %-12s %-28s %-18s %s\n", "a_f", "pin keeps its own value", "verdict", "pivotRatio");
    for (double af : {1.0e-3, 1.0e-9, 1.0e-12, 1.0e-15, 1.0e-20}) {
        for (bool keeps : {true, false}) {
            const Csr m = pressureShape(64, af, keeps);
            std::vector<double> rhs(64, af * 1.0e-3);
            rhs[0] = 0.0;
            const bl::SymmetricSolveSettings s{};
            auto r = bl::solveSymmetricSparse(m.rows, m.rowStart, m.inner, m.values, rhs, s);
            const char* v = r.has_value() ? "SOLVED" : bl::toString(r.error().problem).data();
            const double pr = r.has_value() ? r->pivotRatio : r.error().pivotRatio;
            std::printf("    %-12.3e %-28s %-18s %.6e\n", af, keeps ? "yes (THE RULE)" : "no (1.0 fallback)", v, pr);
        }
    }

    std::printf("\nQ7  SPARSE QR: IS THE R DIAGONAL DIRECTLY READABLE, AND DOES IT SEPARATE?\n");
    struct Case {
        const char* label;
        std::vector<std::vector<double>> a;
    };
    const std::vector<Case> cases = {
        {"well conditioned", {{1.0, 2.0, 0.0}, {3.0, 1.0, 0.0}, {0.0, 1.0, 3.0}}},
        {"near-singular 1e-13", {{1.0, 2.0, 0.0}, {1.0, 2.0 + 1.0e-13, 0.0}, {0.0, 1.0, 3.0}}},
        {"near-singular 1e-9", {{1.0, 2.0, 0.0}, {1.0, 2.0 + 1.0e-9, 0.0}, {0.0, 1.0, 3.0}}},
        {"tiny but well cond.", {{1.0e-9, 2.0e-9, 0.0}, {3.0e-9, 1.0e-9, 0.0}, {0.0, 1.0e-9, 3.0e-9}}},
        {"exactly singular", {{1.0, 2.0, 0.0}, {1.0, 2.0, 0.0}, {0.0, 1.0, 3.0}}},
    };
    for (const Case& c : cases) {
        const Sparse matrix = toEigen(fromDense(c.a));
        Eigen::SparseQR<Sparse, Eigen::COLAMDOrdering<EigenIndex>> qr;
        qr.compute(matrix);
        if (qr.info() != Eigen::Success) {
            std::printf("    %-22s info != Success\n", c.label);
            continue;
        }
        // THE QUESTION: matrixR() is a real sparse matrix, so its diagonal is
        // readable in O(nnz) with no dense intermediate at all.
        const Sparse r = qr.matrixR();
        Eigen::VectorXd diagonal(r.rows());
        for (Eigen::Index i = 0; i < r.rows(); ++i) {
            diagonal(i) = r.coeff(i, i);
        }
        std::printf("    %-22s R-diagonal ratio = %-14.6e rank = %lld of %lld\n", c.label, ratioOfDiagonal(diagonal),
                    static_cast<long long>(qr.rank()), static_cast<long long>(r.rows()));
    }

    std::printf("\n    AND THE COST, against SparseLU, on the momentum shape:\n");
    std::printf("    %-8s %-14s %-14s %-12s %-12s %s\n", "n", "LU seconds", "QR seconds", "QR/LU", "LU nnzU", "QR nnzR");
    for (std::size_t n : {200u, 1000u, 4000u}) {
        const Csr m = momentumShape(n, 1.0e-6, 1.0e-6);
        const Sparse matrix = toEigen(m);
        Eigen::VectorXd b = Eigen::VectorXd::Zero(static_cast<Eigen::Index>(n));
        b(0) = 1.0e-9;

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
        std::printf("    %-8zu %-14.6f %-14.6f %-12.2f %-12lld %lld\n", n, luS, qrS, luS > 0.0 ? qrS / luS : 0.0,
                    static_cast<long long>(lu.nnzU()), static_cast<long long>(r.nonZeros()));
        // both must actually solve it
        double worst = 0.0;
        for (Eigen::Index i = 0; i < xlu.size(); ++i) {
            worst = std::max(worst, std::abs(xlu(i) - xqr(i)));
        }
        std::printf("             LU vs QR largest component gap %.6e\n", worst);
    }

    std::printf("\ndone\n");
    return 0;
}
