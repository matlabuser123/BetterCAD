// P19-LINSOLVE-001 — the generic non-symmetric sparse solve. ADR-076.
//
// These cases test what only the GENERIC layer can: that it knows no physics,
// that its failure carries structured facts, that its conditioning measure is
// SCALE-FREE, and that it classifies a malformed system exactly as its
// symmetric sibling does. Every expected value is typed in from a hand
// derivation or constructed so that the answer is known before the solve.
//
// TWO CASES HERE ARE ABOUT THE OTHER KERNEL, and they are this milestone's
// audit finding rather than its implementation. `solveSymmetricSparse` performs
// NO symmetry check -- it builds the full CSR into Eigen and hands it to an
// LDLT that reads one triangle -- and before this milestone no test anywhere
// had ever shown it an asymmetric matrix. Both callers guarded the precondition
// upstream and pointed at it; nothing qualified it. `ADR-076` Decision 6 keeps
// the kernel's behaviour and pins it with tests instead of changing it, because
// a system with asymmetry 1e-12 solves today and changing that would reach
// `structural` and `thermal` for no benefit to either.

#include <bettercad/linalg/SparseSolve.hpp>
#include <bettercad/linalg/SymmetricSolve.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <optional>
#include <set>
#include <string_view>
#include <vector>

using namespace bettercad;
using linalg::SparseIndex;
using linalg::SparseSolveProblem;
using linalg::SparseSolveSettings;
using linalg::SparseSolution;

namespace {

struct Csr {
    std::size_t rows = 0;
    std::vector<SparseIndex> rowStart{};
    std::vector<SparseIndex> inner{};
    std::vector<double> values{};
};

/// Built from a dense picture so every test matrix is readable in the source.
[[nodiscard]] Csr csrOf(const std::vector<std::vector<double>>& dense) {
    Csr out;
    out.rows = dense.size();
    out.rowStart.push_back(0);
    for (const std::vector<double>& row : dense) {
        for (std::size_t j = 0; j < row.size(); ++j) {
            if (row[j] != 0.0) {
                out.inner.push_back(static_cast<SparseIndex>(j));
                out.values.push_back(row[j]);
            }
        }
        out.rowStart.push_back(static_cast<SparseIndex>(out.values.size()));
    }
    return out;
}

[[nodiscard]] auto solve(const Csr& m, const std::vector<double>& rhs, const SparseSolveSettings& settings = {}) {
    return linalg::solveSparse(m.rows, m.rowStart, m.inner, m.values, rhs, settings);
}

/// `||A x - b||_2 / (||A||_inf ||x||_2 + ||b||_2)`, recomputed HERE so the
/// number the kernel reports can be checked against one this file derived.
[[nodiscard]] double independentNormalizedResidual(const Csr& m, const std::vector<double>& rhs,
                                                   const std::vector<double>& x) {
    double squared = 0.0;
    double matrixInfinity = 0.0;
    double solutionSquared = 0.0;
    double rhsSquared = 0.0;
    for (std::size_t row = 0; row < m.rows; ++row) {
        double product = 0.0;
        double rowSum = 0.0;
        for (SparseIndex slot = m.rowStart[row]; slot < m.rowStart[row + 1]; ++slot) {
            product += m.values[slot] * x[static_cast<std::size_t>(m.inner[slot])];
            rowSum += std::abs(m.values[slot]);
        }
        const double entry = product - rhs[row];
        squared += entry * entry;
        matrixInfinity = std::max(matrixInfinity, rowSum);
        solutionSquared += x[row] * x[row];
        rhsSquared += rhs[row] * rhs[row];
    }
    const double denominator = matrixInfinity * std::sqrt(solutionSquared) + std::sqrt(rhsSquared);
    return denominator > 0.0 ? std::sqrt(squared) / denominator : std::sqrt(squared);
}

/// A 1D convection-diffusion chain, Dirichlet at both ends. Asymmetric exactly
/// when `flux` is non-zero: upwind writes `+q` on the diagonal and `-q` on the
/// downstream row's upstream column, which never pairs symmetrically.
[[nodiscard]] Csr chain(std::size_t cells, double diffusion, double flux) {
    std::vector<std::vector<double>> dense(cells, std::vector<double>(cells, 0.0));
    for (std::size_t i = 0; i + 1 < cells; ++i) {
        dense[i][i] += diffusion;
        dense[i + 1][i + 1] += diffusion;
        dense[i][i + 1] -= diffusion;
        dense[i + 1][i] -= diffusion;
        dense[i][i] += flux;
        dense[i + 1][i] -= flux;
    }
    dense[0][0] += diffusion + flux;
    dense[cells - 1][cells - 1] += diffusion + flux;
    return csrOf(dense);
}

/// `A x` for a chosen `x`, so a large system's answer is known before it is
/// solved. NOT an analytic solution of a differential equation: it is an exact
/// reference for the ALGEBRA, which is what this kernel is responsible for.
[[nodiscard]] std::vector<double> multiply(const Csr& m, const std::vector<double>& x) {
    std::vector<double> out(m.rows, 0.0);
    for (std::size_t row = 0; row < m.rows; ++row) {
        double product = 0.0;
        for (SparseIndex slot = m.rowStart[row]; slot < m.rowStart[row + 1]; ++slot) {
            product += m.values[slot] * x[static_cast<std::size_t>(m.inner[slot])];
        }
        out[row] = product;
    }
    return out;
}

/// A 64-bit FNV-1a over a solution's bits, the vehicle `P18-SOLVE-001` used to
/// compare presets: the test asserts only that it is reproducible in one
/// process, and the cross-preset identity is established by diffing the printed
/// values from the three presets' logs.
[[nodiscard]] std::uint64_t fingerprint(const SparseSolution& solution) {
    std::uint64_t hash = 14695981039346656037ULL;
    const auto feed = [&hash](double value) {
        const std::uint64_t bits = std::bit_cast<std::uint64_t>(value);
        for (int byte = 0; byte < 8; ++byte) {
            hash ^= (bits >> (byte * 8)) & 0xffULL;
            hash *= 1099511628211ULL;
        }
    };
    for (const double value : solution.x) {
        feed(value);
    }
    feed(solution.residual.normalized);
    feed(solution.rankRatio);
    return hash;
}

} // namespace

TEST_CASE("Linalg_SparseSolvesAnAsymmetricSystemExactly", "[linalg][sparse]") {
    // A = {{1, 2}, {3, 1}}, b = {1, 1}. det = 1 - 6 = -5.
    // x1 = (1*1 - 2*1) / -5 = 0.2,  x2 = (1*1 - 3*1) / -5 = 0.4.
    const Csr m = csrOf({{1.0, 2.0}, {3.0, 1.0}});
    const std::vector<double> rhs = {1.0, 1.0};
    const auto solved = solve(m, rhs);
    REQUIRE(solved.has_value());
    REQUIRE(solved->x.size() == 2);
    CHECK(std::abs(solved->x[0] - 0.2) <= 1.0e-15);
    CHECK(std::abs(solved->x[1] - 0.4) <= 1.0e-15);
    CHECK(solved->residual.normalized <= 1.0e-15);
    CHECK(solved->rankRatio > 0.0);
    CHECK(solved->reportedRank == 2);

    SECTION("and the matrix really is asymmetric, so the case is not vacuous") {
        CHECK(m.values[1] != m.values[2]);
    }

    SECTION("the residual it reports is the one this file computes independently") {
        const double independent = independentNormalizedResidual(m, rhs, solved->x);
        // EXACT EQUALITY, AND AN ABSOLUTE BOUND WAS THE WRONG TEST. The first
        // form allowed `1.0e-18` against a residual of magnitude `4e-17` -- a
        // 2.4% relative tolerance, which a defect scaling the reported residual
        // by 1.001 passes comfortably. The two loops are the same arithmetic
        // over the same arrays in the same order, so the honest assertion is
        // equality, and the premise is asserted so a pair of zeros cannot make
        // it vacuous.
        REQUIRE(independent > 0.0);
        CHECK(solved->residual.normalized == independent);
    }
}

TEST_CASE("Linalg_SparseEmptySystemIsSolved", "[linalg][sparse]") {
    // ZERO ROWS IS A SOLVED SYSTEM: nothing is handed to the library, so there
    // is no factor and the ratio is the honest 1.0 rather than 0.
    const Csr m = csrOf({});
    const auto solved = solve(m, {});
    REQUIRE(solved.has_value());
    CHECK(solved->x.empty());
    CHECK(solved->rankRatio == 1.0);
    CHECK(solved->reportedRank == 0);
    CHECK(solved->residual.normalized == 0.0);
}

TEST_CASE("Linalg_SparseSymmetricMatrixIsAcceptedToo", "[linalg][sparse]") {
    // The general kernel does not route a symmetric system elsewhere: it knows
    // nothing about what its caller's matrix means. A momentum system at
    // exactly zero mass flux IS symmetric and this kernel still answers for it.
    const Csr m = chain(6, 1.0e-6, 0.0);
    std::vector<double> rhs(6, 0.0);
    rhs[0] = 1.0e-9;
    const auto solved = solve(m, rhs);
    REQUIRE(solved.has_value());
    CHECK(solved->residual.normalized <= 1.0e-9);

    SECTION("and the fixture is exactly symmetric, so the claim is not vacuous") {
        double worst = 0.0;
        for (std::size_t row = 0; row < m.rows; ++row) {
            for (SparseIndex slot = m.rowStart[row]; slot < m.rowStart[row + 1]; ++slot) {
                const std::size_t column = static_cast<std::size_t>(m.inner[slot]);
                double mirrored = 0.0;
                for (SparseIndex other = m.rowStart[column]; other < m.rowStart[column + 1]; ++other) {
                    if (static_cast<std::size_t>(m.inner[other]) == row) {
                        mirrored = m.values[other];
                    }
                }
                worst = std::max(worst, std::abs(m.values[slot] - mirrored));
            }
        }
        CHECK(worst == 0.0);
    }
}

TEST_CASE("Linalg_SparseSingularSystemCarriesStructuredFacts", "[linalg][sparse][singular]") {
    SECTION("two identical rows are exactly singular") {
        const Csr m = csrOf({{1.0, 2.0, 0.0}, {1.0, 2.0, 0.0}, {0.0, 1.0, 3.0}});
        const auto solved = solve(m, {1.0, 1.0, 1.0});
        REQUIRE_FALSE(solved.has_value());
        CHECK(solved.error().problem == SparseSolveProblem::SingularSystem);
        CHECK(solved.error().rankRatio == 0.0);
        CHECK(solved.error().rankCameOutShort);
        CHECK(solved.error().reportedRank < 3);
        CHECK(solved.error().rows == 3);
        CHECK_THAT(solved.error().error.message, Catch::Matchers::ContainsSubstring("singular"));
    }

    SECTION("a system singular to 1e-13 is refused, which is the whole reason QR was chosen") {
        // MEASURED BEFORE THE DESIGN: Eigen's SparseLU reports info() == Success
        // on this matrix with a normalized residual of exactly ZERO, so a gate
        // built from the library's own verdict plus the residual would have
        // accepted it. The R-diagonal ratio is what refuses it.
        const Csr m = csrOf({{1.0, 2.0, 0.0}, {1.0, 2.0 + 1.0e-13, 0.0}, {0.0, 1.0, 3.0}});
        const auto solved = solve(m, {1.0, 1.0, 1.0});
        REQUIRE_FALSE(solved.has_value());
        CHECK(solved.error().problem == SparseSolveProblem::SingularSystem);
        CHECK(solved.error().rankRatio < 1.0e-12);
    }

    SECTION("the floor is a setting, and RAISING it is what shows that") {
        // THE FIRST FORM OF THIS SECTION WAS WRONG, AND ADR-076's OWN Q7 HAD
        // SAID SO. It copied the symmetric suite's shape -- lower the floor and
        // the system is admitted -- which works there because
        // `{{1, 0}, {0, 1e-14}}` has a pivot ratio of exactly 1e-14. A column
        // QR pivots away does not behave that way, so the demonstration has to
        // run the other direction: a system the DEFAULT floor admits is refused
        // once the floor is raised above its measured ratio.
        const Csr m = csrOf({{1.0, 2.0, 0.0}, {1.0, 2.0 + 1.0e-9, 0.0}, {0.0, 1.0, 3.0}});
        const std::vector<double> rhs = {1.0, 1.0, 1.0};

        const auto admitted = solve(m, rhs);
        REQUIRE(admitted.has_value());
        CHECK(std::abs(admitted->rankRatio - 2.357023e-10) <= 1.0e-16);
        CHECK(admitted->reportedRank == 3);

        const auto refused =
            solve(m, rhs,
                  SparseSolveSettings{.relativeResidualTolerance = 1.0e-9, .rankRatioFloor = 1.0e-9});
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().problem == SparseSolveProblem::SingularSystem);
        // The SAME ratio in both: only the floor moved.
        CHECK(std::abs(refused.error().rankRatio - admitted->rankRatio) == 0.0);
        // And the factorisation found full rank, so the refusal is the floor's
        // doing and not a detected deficiency.
        CHECK(refused.error().reportedRank == 3);
        CHECK_FALSE(refused.error().rankCameOutShort);
    }

    SECTION("a column QR pivots away reads exactly zero, and NO floor admits it") {
        // THIS IS A REAL DIFFERENCE BETWEEN THE TWO KERNELS, measured rather
        // than assumed, and it is why the section above had to be rewritten.
        // Column pivoting treats a numerically dependent column as zero and
        // moves it to the end, so the ratio is EXACTLY 0.0 -- not a small
        // number -- and a system singular to 1e-13 is indistinguishable here
        // from one that is exactly singular. The LDLT does not do this: its
        // pivot ratio for `{{1, 0}, {0, 1e-14}}` is exactly 1e-14 and a lower
        // floor admits it, which the symmetric suite asserts.
        const Csr m = csrOf({{1.0, 2.0, 0.0}, {1.0, 2.0 + 1.0e-13, 0.0}, {0.0, 1.0, 3.0}});
        const std::vector<double> rhs = {1.0, 1.0, 1.0};
        for (const double floor : {1.0e-9, 1.0e-12, 1.0e-16, 1.0e-300}) {
            const auto solved =
                solve(m, rhs,
                      SparseSolveSettings{.relativeResidualTolerance = 1.0e-9, .rankRatioFloor = floor});
            REQUIRE_FALSE(solved.has_value());
            CHECK(solved.error().problem == SparseSolveProblem::SingularSystem);
            CHECK(solved.error().rankRatio == 0.0);
            CHECK(solved.error().reportedRank == 2);
            CHECK(solved.error().rankCameOutShort);
        }

        // And the LDLT's contrasting behaviour, asserted here so the difference
        // is pinned in one place rather than inferred from two suites.
        const Csr tiny = csrOf({{1.0, 0.0}, {0.0, 1.0e-14}});
        const std::vector<double> small = {1.0, 1.0e-14};
        const auto refusedByDefault = linalg::solveSymmetricSparse(
            tiny.rows, tiny.rowStart, tiny.inner, tiny.values, small, linalg::SymmetricSolveSettings{});
        REQUIRE_FALSE(refusedByDefault.has_value());
        CHECK(refusedByDefault.error().pivotRatio == 1.0e-14);
        const auto admittedByALowerFloor = linalg::solveSymmetricSparse(
            tiny.rows, tiny.rowStart, tiny.inner, tiny.values, small,
            linalg::SymmetricSolveSettings{.relativeResidualTolerance = 1.0e-9, .pivotFloor = 1.0e-16});
        CHECK(admittedByALowerFloor.has_value());
    }
}

TEST_CASE("Linalg_SparseValidatesItsInput", "[linalg][sparse][invalid]") {
    const Csr good = csrOf({{1.0, 2.0}, {3.0, 1.0}});

    SECTION("a short right-hand side is a dimension mismatch, never a smaller system") {
        const auto solved = solve(good, {1.0});
        REQUIRE_FALSE(solved.has_value());
        CHECK(solved.error().problem == SparseSolveProblem::DimensionMismatch);
    }

    SECTION("a column index outside the system") {
        Csr bad = good;
        bad.inner[1] = 7;
        const auto solved = solve(bad, {1.0, 1.0});
        REQUIRE_FALSE(solved.has_value());
        CHECK(solved.error().problem == SparseSolveProblem::DimensionMismatch);
    }

    SECTION("a truncated row-offset array") {
        Csr bad = good;
        bad.rowStart.pop_back();
        const auto solved = solve(bad, {1.0, 1.0});
        REQUIRE_FALSE(solved.has_value());
        CHECK(solved.error().problem == SparseSolveProblem::DimensionMismatch);
    }

    SECTION("a non-finite matrix entry names its index") {
        Csr bad = good;
        bad.values[1] = std::numeric_limits<double>::infinity();
        const auto solved = solve(bad, {1.0, 1.0});
        REQUIRE_FALSE(solved.has_value());
        CHECK(solved.error().problem == SparseSolveProblem::NonFiniteSystem);
        CHECK(solved.error().index == 1);
    }

    SECTION("a non-finite right-hand side entry names its index") {
        const auto solved = solve(good, {1.0, std::numeric_limits<double>::quiet_NaN()});
        REQUIRE_FALSE(solved.has_value());
        CHECK(solved.error().problem == SparseSolveProblem::NonFiniteSystem);
        CHECK(solved.error().index == 1);
    }

    SECTION("every unusable setting is refused") {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double inf = std::numeric_limits<double>::infinity();
        for (const SparseSolveSettings& bad :
             {SparseSolveSettings{.relativeResidualTolerance = 0.0, .rankRatioFloor = 1.0e-12},
              SparseSolveSettings{.relativeResidualTolerance = -1.0e-9, .rankRatioFloor = 1.0e-12},
              SparseSolveSettings{.relativeResidualTolerance = nan, .rankRatioFloor = 1.0e-12},
              SparseSolveSettings{.relativeResidualTolerance = 1.0e-9, .rankRatioFloor = 0.0},
              SparseSolveSettings{.relativeResidualTolerance = 1.0e-9, .rankRatioFloor = -1.0},
              SparseSolveSettings{.relativeResidualTolerance = 1.0e-9, .rankRatioFloor = inf}}) {
            const auto solved = solve(good, {1.0, 1.0}, bad);
            REQUIRE_FALSE(solved.has_value());
            CHECK(solved.error().problem == SparseSolveProblem::InvalidSettings);
        }
        CHECK(linalg::validate(SparseSolveSettings{}).has_value());
    }
}

TEST_CASE("Linalg_SparseResidualGateRefusesAnAnswerItCannotJustify", "[linalg][sparse][residual]") {
    // A GATE THAT CANNOT BE SHOWN A WRONG ANSWER HAS NOT BEEN TESTED, and this
    // kernel never hands out its candidate, so the gate is exercised from the
    // other side: a tolerance BELOW what double precision can deliver for this
    // system must refuse the kernel's own best answer.
    const Csr m = chain(32, 1.0e-6, 1.0e-6);
    std::vector<double> rhs(32, 0.0);
    rhs[0] = 1.0e-9;

    const auto achievable = solve(m, rhs);
    REQUIRE(achievable.has_value());
    const double achieved = achievable->residual.normalized;
    CHECK(achieved <= 1.0e-9);

    const auto refused =
        solve(m, rhs, SparseSolveSettings{.relativeResidualTolerance = achieved / 1.0e3, .rankRatioFloor = 1.0e-12});
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().problem == SparseSolveProblem::ResidualTooLarge);
    CHECK(refused.error().residual.normalized > 0.0);
    // The refusal carries the conditioning it measured, so a reader can see the
    // system was fine and the TOLERANCE was the problem.
    CHECK(refused.error().rankRatio > 1.0e-12);

    SECTION("and the premise is not vacuous: the achieved residual is above zero") {
        CHECK(achieved > 0.0);
    }
}

TEST_CASE("Linalg_SparseConditioningIsScaleFree", "[linalg][sparse][scaling]") {
    // MEASURED BEFORE THE DESIGN (ADR-076 Q7): a matrix whose every entry is
    // 1e-9 and which is perfectly well conditioned reads the SAME ratio as its
    // unit-scaled twin, while `logAbsDeterminant` reads -59 for one and +2.7
    // for the other. That is why the ratio is the gate and the determinant is
    // not.
    double reference = 0.0;
    for (const double scale : {1.0e-9, 1.0e-3, 1.0, 1.0e3, 1.0e9}) {
        const Csr m = csrOf({{1.0 * scale, 2.0 * scale, 0.0},
                             {3.0 * scale, 1.0 * scale, 0.0},
                             {0.0, 1.0 * scale, 3.0 * scale}});
        const std::vector<double> rhs = {1.0 * scale, 1.0 * scale, 1.0 * scale};
        const auto solved = solve(m, rhs);
        REQUIRE(solved.has_value());
        if (reference == 0.0) {
            reference = solved->rankRatio;
            // PINNED TO THE MEASURED VALUE, NOT MERELY TO BEING LARGE.
            // `ADR-076` Q7 measured `5.000000e-01` for this matrix, and a
            // one-sided `> 1e-3` admitted a hard-coded constant -- which is
            // also perfectly scale-free, so the sweep below could not tell the
            // difference.
            CHECK(std::abs(reference - 0.5) <= 1.0e-12);
        } else {
            // Scaling A and b by c scales the factor by c, so the RATIO must not
            // move at all beyond the rounding of the scaling itself.
            CHECK(std::abs(solved->rankRatio - reference) <= 1.0e-12);
        }
        CHECK(solved->residual.normalized <= 1.0e-12);
    }
}

TEST_CASE("Linalg_SparseDeterministicRepeat", "[linalg][sparse][determinism]") {
    // NO TOLERANCE, because there is nothing to tolerance: the same operations
    // in the same order on the same data. This is `P17-SOLVE-001`'s form and
    // `P18-SOLVE-001`'s counting.
    const Csr m = chain(24, 1.0e-6, 1.0e-6);
    std::vector<double> rhs(24, 0.0);
    rhs[0] = 1.0e-9;

    const auto first = solve(m, rhs);
    REQUIRE(first.has_value());

    std::size_t differences = 0;
    for (int repeat = 0; repeat < 40; ++repeat) {
        const auto again = solve(m, rhs);
        REQUIRE(again.has_value());
        if (!(*again == *first)) {
            ++differences;
        }
    }
    CHECK(differences == 0);

    SECTION("a refusal is deterministic too, diagnostic text included") {
        const Csr singular = csrOf({{1.0, 2.0, 0.0}, {1.0, 2.0, 0.0}, {0.0, 1.0, 3.0}});
        const auto bad = solve(singular, {1.0, 1.0, 1.0});
        REQUIRE_FALSE(bad.has_value());
        std::size_t diagnosticDifferences = 0;
        for (int repeat = 0; repeat < 20; ++repeat) {
            const auto again = solve(singular, {1.0, 1.0, 1.0});
            REQUIRE_FALSE(again.has_value());
            if (again.error().problem != bad.error().problem ||
                again.error().error.message != bad.error().error.message) {
                ++diagnosticDifferences;
            }
        }
        CHECK(diagnosticDifferences == 0);
    }

    SECTION("a bit fingerprint, for comparing presets outside the test") {
        // The test asserts only that it is reproducible IN ONE PROCESS. The
        // cross-preset claim is made by diffing the three presets' logs, which
        // is what `P18-SOLVE-001` did and why it could claim bitwise identity
        // across presets while its tests promised only this.
        const std::uint64_t once = fingerprint(*first);
        const auto repeated = solve(m, rhs);
        REQUIRE(repeated.has_value());
        CHECK(fingerprint(*repeated) == once);
        WARN(std::format("bit fingerprint, 24-cell asymmetric chain: {:016x}", once));
    }
}

TEST_CASE("Linalg_SparseProblemMatchesTheFailure", "[linalg][sparse]") {
    const Csr singular = csrOf({{1.0, 2.0, 0.0}, {1.0, 2.0, 0.0}, {0.0, 1.0, 3.0}});
    const std::vector<double> threeOnes = {1.0, 1.0, 1.0};
    CHECK(linalg::sparseSolveProblem(singular.rows, singular.rowStart, singular.inner, singular.values,
                                     threeOnes, SparseSolveSettings{}) ==
          std::optional{SparseSolveProblem::SingularSystem});

    const Csr good = csrOf({{1.0, 2.0}, {3.0, 1.0}});
    const std::vector<double> twoOnes = {1.0, 1.0};
    CHECK_FALSE(linalg::sparseSolveProblem(good.rows, good.rowStart, good.inner, good.values, twoOnes,
                                           SparseSolveSettings{})
                    .has_value());

    SECTION("every enumerator has a name, and the table holds each exactly once") {
        std::set<std::string_view> names;
        for (const SparseSolveProblem problem : linalg::kSparseSolveProblems) {
            const std::string_view name = linalg::toString(problem);
            CHECK(name != "unknown");
            names.insert(name);
        }
        CHECK(names.size() == linalg::kSparseSolveProblems.size());
    }
}

TEST_CASE("Linalg_BothKernelsClassifyAMalformedSystemAlike", "[linalg][sparse]") {
    // THE DRIFT GUARD FOR A DELIBERATE DUPLICATION. The two kernels are
    // separate translation units and each validates its own input, so the
    // forty-line system check is written twice. This case is what makes that
    // safe: it feeds the same malformed systems to both and requires the same
    // classification and the same index, so a divergence is a FAILURE here
    // rather than a silent difference in the field.
    const linalg::SymmetricSolveSettings symmetric{};
    const SparseSolveSettings general{};

    struct Malformed {
        const char* what;
        Csr matrix;
        std::vector<double> rhs;
        linalg::SymmetricSolveProblem expectedSymmetric;
        SparseSolveProblem expectedGeneral;
    };

    Csr shortRhsMatrix = csrOf({{2.0, -1.0}, {-1.0, 2.0}});
    Csr outOfRange = csrOf({{2.0, -1.0}, {-1.0, 2.0}});
    outOfRange.inner[1] = 9;
    Csr truncated = csrOf({{2.0, -1.0}, {-1.0, 2.0}});
    truncated.rowStart.pop_back();
    Csr nonFiniteValue = csrOf({{2.0, -1.0}, {-1.0, 2.0}});
    nonFiniteValue.values[1] = std::numeric_limits<double>::infinity();

    const std::vector<Malformed> cases = {
        {"a short right-hand side", shortRhsMatrix, {1.0}, linalg::SymmetricSolveProblem::DimensionMismatch,
         SparseSolveProblem::DimensionMismatch},
        {"a column outside the system", outOfRange, {1.0, 1.0},
         linalg::SymmetricSolveProblem::DimensionMismatch, SparseSolveProblem::DimensionMismatch},
        {"a truncated row-offset array", truncated, {1.0, 1.0},
         linalg::SymmetricSolveProblem::DimensionMismatch, SparseSolveProblem::DimensionMismatch},
        {"a non-finite matrix entry", nonFiniteValue, {1.0, 1.0},
         linalg::SymmetricSolveProblem::NonFiniteSystem, SparseSolveProblem::NonFiniteSystem},
        {"a non-finite right-hand side", shortRhsMatrix,
         {1.0, std::numeric_limits<double>::quiet_NaN()}, linalg::SymmetricSolveProblem::NonFiniteSystem,
         SparseSolveProblem::NonFiniteSystem},
    };

    for (const Malformed& bad : cases) {
        const auto symmetricResult = linalg::solveSymmetricSparse(bad.matrix.rows, bad.matrix.rowStart,
                                                                  bad.matrix.inner, bad.matrix.values, bad.rhs,
                                                                  symmetric);
        const auto generalResult = linalg::solveSparse(bad.matrix.rows, bad.matrix.rowStart, bad.matrix.inner,
                                                       bad.matrix.values, bad.rhs, general);
        REQUIRE_FALSE(symmetricResult.has_value());
        REQUIRE_FALSE(generalResult.has_value());
        CHECK(symmetricResult.error().problem == bad.expectedSymmetric);
        CHECK(generalResult.error().problem == bad.expectedGeneral);
        // THE INDEX TOO, not only the classification: an off-by-one in one
        // kernel's loop and not the other's would otherwise pass.
        CHECK(symmetricResult.error().index == generalResult.error().index);
        CHECK(symmetricResult.error().rows == generalResult.error().rows);
    }

    SECTION("and both accept the same well-posed symmetric system") {
        const Csr good = csrOf({{2.0, -1.0}, {-1.0, 2.0}});
        const std::vector<double> rhs = {1.0, 0.0};
        const auto bySymmetric = linalg::solveSymmetricSparse(good.rows, good.rowStart, good.inner, good.values,
                                                              rhs, symmetric);
        const auto byGeneral = linalg::solveSparse(good.rows, good.rowStart, good.inner, good.values, rhs, general);
        REQUIRE(bySymmetric.has_value());
        REQUIRE(byGeneral.has_value());
        // THE ANSWERS AGREE, AND NOTHING ASSERTS THEY ARE BITWISE EQUAL. Two
        // different factorisations of the same system agree to their accuracy
        // and not to the last bit, and `ADR-076` forbids a bitwise claim across
        // the two kernels anywhere.
        for (std::size_t i = 0; i < good.rows; ++i) {
            CHECK(std::abs(bySymmetric->x[i] - byGeneral->x[i]) <= 1.0e-14);
        }
        // AND BOTH TO A HAND VALUE, because anchoring each only to the other
        // leaves a defect in the SHARED residual gate -- the one piece of code
        // both paths run -- invisible. `{{2, -1}, {-1, 2}} x = {1, 0}` has
        // determinant 3, so `x = {2/3, 1/3}`.
        CHECK(std::abs(bySymmetric->x[0] - 2.0 / 3.0) <= 1.0e-15);
        CHECK(std::abs(bySymmetric->x[1] - 1.0 / 3.0) <= 1.0e-15);
        CHECK(std::abs(byGeneral->x[0] - 2.0 / 3.0) <= 1.0e-15);
        CHECK(std::abs(byGeneral->x[1] - 1.0 / 3.0) <= 1.0e-15);
    }
}

TEST_CASE("Linalg_TheSymmetricKernelRefusesAnAsymmetricSystem", "[linalg][sparse][audit]") {
    // THIS MILESTONE'S AUDIT FINDING, PINNED. `solveSymmetricSparse` performs no
    // symmetry check and no test had ever shown it an asymmetric matrix. Its
    // LDLT reads one triangle, so it factorises a DIFFERENT matrix from the one
    // supplied -- and the independent residual, computed from the ORIGINAL
    // arrays, is what refuses the result. These are the measured boundaries,
    // and the point of recording them is that `ADR-076` Decision 6 chose to
    // keep this behaviour rather than change a kernel two physics depend on.
    const linalg::SymmetricSolveSettings settings{};
    const double diffusion = 1.0e-6;

    SECTION("exactly symmetric is solved") {
        const Csr m = chain(64, diffusion, 0.0);
        std::vector<double> rhs(64, 0.0);
        rhs[0] = diffusion * 1.0e-3;
        const auto solved =
            linalg::solveSymmetricSparse(m.rows, m.rowStart, m.inner, m.values, rhs, settings);
        REQUIRE(solved.has_value());
        CHECK(solved->residual.normalized <= 1.0e-9);
    }

    SECTION("and every asymmetry large enough to matter is REFUSED, never solved quietly") {
        for (const double flux : {1.0e-12, 1.0e-9, 1.0e-7, 1.0e-6, 1.0e-3}) {
            const Csr m = chain(64, diffusion, flux);
            std::vector<double> rhs(64, 0.0);
            rhs[0] = (diffusion + flux) * 1.0e-3;
            const auto solved =
                linalg::solveSymmetricSparse(m.rows, m.rowStart, m.inner, m.values, rhs, settings);
            REQUIRE_FALSE(solved.has_value());
            // Either gate may fire first depending on how far the triangle is
            // from the matrix; what matters is that NEITHER accepts it.
            CHECK((solved.error().problem == linalg::SymmetricSolveProblem::ResidualTooLarge ||
                   solved.error().problem == linalg::SymmetricSolveProblem::SingularSystem));
        }
    }

    SECTION("the general kernel solves every one of those systems") {
        for (const double flux : {1.0e-12, 1.0e-9, 1.0e-7, 1.0e-6, 1.0e-3}) {
            const Csr m = chain(64, diffusion, flux);
            std::vector<double> rhs(64, 0.0);
            rhs[0] = (diffusion + flux) * 1.0e-3;
            const auto solved = solve(m, rhs);
            REQUIRE(solved.has_value());
            CHECK(solved->residual.normalized <= 1.0e-9);
        }
    }
}

TEST_CASE("Linalg_SparseLargeSystemSmoke", "[linalg][sparse][smoke]") {
    // NOT A PERFORMANCE TEST. No wall-clock bound is asserted and no timing is
    // a gate; the seconds are printed so a reader can see them and the ctest
    // log records the real figure.
    //
    // The reference is EXACT BY CONSTRUCTION: a solution is chosen, the
    // right-hand side is formed as `A x*`, and the solve must return `x*`. That
    // is an exact reference for the ALGEBRA, which is this kernel's
    // responsibility; it is not an analytic solution of a differential
    // equation and is not claimed as one.
    constexpr std::size_t kCells = 2000;
    const Csr m = chain(kCells, 1.0e-6, 1.0e-6);

    std::vector<double> expected(kCells, 0.0);
    for (std::size_t i = 0; i < kCells; ++i) {
        // A smooth, strictly varying field, so a solver that returned a
        // constant or a zero vector could not pass.
        expected[i] = 1.0e-3 * (1.0 + static_cast<double>(i) / static_cast<double>(kCells));
    }
    const std::vector<double> rhs = multiply(m, expected);

    const auto start = std::chrono::steady_clock::now();
    const auto solved = solve(m, rhs);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    REQUIRE(solved.has_value());

    SECTION("the fixture is large enough for the claim to mean anything") {
        CHECK(m.rows > 1000);
        CHECK(solved->x.size() > 1000);
    }

    SECTION("every component is finite and matches the constructed solution") {
        double worst = 0.0;
        for (std::size_t i = 0; i < kCells; ++i) {
            REQUIRE(std::isfinite(solved->x[i]));
            worst = std::max(worst, std::abs(solved->x[i] - expected[i]));
        }
        // Relative to the field's own magnitude, which is 1e-3.
        CHECK(worst <= 1.0e-3 * 1.0e-6);
        WARN(std::format("large sparse smoke: {} unknowns, {} stored entries, worst component departure {:.6e}, "
                         "normalized residual {:.6e}, rank ratio {:.6e}, {:.2f} s",
                         kCells, m.values.size(), worst, solved->residual.normalized, solved->rankRatio, seconds));
    }

    SECTION("the gates hold at size") {
        CHECK(solved->residual.normalized <= 1.0e-9);
        CHECK(solved->rankRatio > 1.0e-12);
        CHECK(solved->reportedRank == kCells);
    }

    SECTION("and nothing densified") {
        // The anti-densification assertion `P18-SOLVE-001` used on its own
        // smoke test: a stored-entry count anywhere near n^2 would mean the
        // sparse path had been abandoned.
        CHECK(m.values.size() < kCells * kCells / 10);
    }
}

TEST_CASE("Linalg_SparseSolvesTheSmallestSystems", "[linalg][sparse]") {
    // A 1x1 SYSTEM, BECAUSE `if (rows == 0)` -> `if (rows <= 1)` IS THE
    // COMMONEST DEFECT SHAPE IN AN EARLY-OUT and no fixture anywhere was
    // smaller than 2x2. The mutation returns an empty solution, a zero
    // residual, a ratio of 1.0 and `has_value() == true`, and nothing noticed.
    const Csr one = csrOf({{4.0}});
    const std::vector<double> rhs = {2.0};
    const auto solved = solve(one, rhs);
    REQUIRE(solved.has_value());
    REQUIRE(solved->x.size() == 1);
    CHECK(solved->x[0] == 0.5);
    CHECK(solved->reportedRank == 1);
    CHECK(solved->rankRatio > 0.0);

    SECTION("and a 1x1 singular system is refused rather than divided by zero") {
        const Csr zero = csrOf({{0.0}});
        const std::vector<double> right = {1.0};
        const auto refused = solve(zero, right);
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().problem == SparseSolveProblem::SingularSystem);
    }
}

TEST_CASE("Linalg_SparseRefusesAnAllZeroMatrix", "[linalg][sparse][singular]") {
    // THE `largest > 0.0 ? ... : 0.0` FALSE ARM, which nothing reached. An
    // all-zero matrix is a valid CSR with empty `inner` and `values`: there is
    // no diagonal to divide by, and the honest ratio is 0.0 rather than a NaN.
    const Csr zero = csrOf({{0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}});
    REQUIRE(zero.values.empty());
    const std::vector<double> rhs = {1.0, 1.0, 1.0};
    const auto refused = solve(zero, rhs);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().problem == SparseSolveProblem::SingularSystem);
    CHECK(refused.error().rankRatio == 0.0);
    CHECK_FALSE(std::isnan(refused.error().rankRatio));
}

TEST_CASE("Linalg_SparseGateRefusesEveryMalformedRowOffsetArray", "[linalg][sparse][invalid]") {
    // THE AUDIT'S MOST SERIOUS FINDING, AS A REGRESSION TEST. Before this
    // milestone neither kernel checked that `rowStart` is non-decreasing or
    // that an interior offset is within the stored entries, so this input was
    // ACCEPTED and the build loop read past the spans -- reproduced as an abort
    // inside `std::span::operator[]`. Every case below must be refused by BOTH
    // kernels, and the clauses that already existed are included so that
    // deleting any one of them fails here.
    const linalg::SymmetricSolveSettings symmetric{};
    const SparseSolveSettings general{};

    struct Case {
        const char* what;
        std::size_t rows;
        std::vector<SparseIndex> rowStart;
    };
    // Two stored entries throughout: `inner = {0, 1}`, `values = {2.0, 2.0}`.
    //
    // EACH CASE IS WRONG IN EXACTLY ONE WAY, and the last one is here because a
    // mutation probe that removed ONLY the monotonicity half of the new guard
    // SURVIVED the first version of this case. All five of its inputs were
    // caught by a DIFFERENT clause -- two by the interior bound, three by the
    // pre-existing front and back checks -- so the decrease itself was never
    // exercised.
    //
    // `{0, 2, 1, 2}` over three rows is the isolating input: the first offset
    // is 0, the last is the entry count, every offset lies within the stored
    // entries, and only the 2 -> 1 step is wrong.
    //
    // AND A SECOND PROBE THEN SHOWED THE OTHER HALF OF THAT GUARD COULD NOT
    // FIRE AT ALL. An interior offset can only exceed `inner.size()` if the
    // offsets also decrease somewhere, because the last one is already required
    // to equal `inner.size()` -- so the bounding clause was removed as
    // unfireable and monotonicity carries the whole weight. That is why the
    // first input is labelled "also a decrease": it is caught, but not by a
    // clause of its own.
    const std::vector<Case> cases = {
        {"an interior offset beyond the stored entries (also a decrease)", 2, {0, 5, 2}},
        {"a decreasing pair whose last offset is also wrong", 2, {0, 2, 1}},
        {"a first offset that is not zero", 2, {1, 2, 2}},
        {"a last offset that is not the entry count", 2, {0, 1, 1}},
        {"an interior offset far beyond, with a correct last", 2, {0, 1000, 2}},
        {"ONLY a decrease: every offset in bounds, first 0, last correct", 3, {0, 2, 1, 2}},
    };
    for (const Case& bad : cases) {
        INFO(bad.what);
        const std::vector<SparseIndex> inner = {0, 1};
        const std::vector<double> values = {2.0, 2.0};
        const std::vector<double> rhs(bad.rows, 1.0);
        const auto byGeneral = linalg::solveSparse(bad.rows, bad.rowStart, inner, values, rhs, general);
        const auto bySymmetric =
            linalg::solveSymmetricSparse(bad.rows, bad.rowStart, inner, values, rhs, symmetric);
        REQUIRE_FALSE(byGeneral.has_value());
        REQUIRE_FALSE(bySymmetric.has_value());
        CHECK(byGeneral.error().problem == SparseSolveProblem::DimensionMismatch);
        CHECK(bySymmetric.error().problem == linalg::SymmetricSolveProblem::DimensionMismatch);
    }

    SECTION("and a well-formed offset array of the same shape is accepted") {
        // So the cases above are refused for their own reason and not because
        // this fixture shape is refused outright.
        const std::vector<SparseIndex> rowStart = {0, 1, 2};
        const std::vector<SparseIndex> inner = {0, 1};
        const std::vector<double> values = {2.0, 2.0};
        const std::vector<double> rhs = {1.0, 1.0};
        CHECK(linalg::solveSparse(2, rowStart, inner, values, rhs, general).has_value());
        CHECK(linalg::solveSymmetricSparse(2, rowStart, inner, values, rhs, symmetric).has_value());
    }
}

TEST_CASE("Linalg_SparseRefusesEveryShapeOfBadLengthAndNonFiniteValue", "[linalg][sparse][invalid]") {
    // THREE NARROW PREDICATES, EACH PREVIOUSLY PROBED FROM ONE SIDE ONLY: the
    // length test saw only SHORT right-hand sides, the matrix test only
    // infinities, the right-hand-side test only NaNs, and every non-finite
    // index assertion expected 1.
    const Csr good = csrOf({{1.0, 2.0, 0.0}, {3.0, 1.0, 0.0}, {0.0, 1.0, 3.0}});
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();

    SECTION("an OVER-LONG right-hand side is a mismatch, not a truncation") {
        const std::vector<double> tooLong = {1.0, 1.0, 1.0, 1.0};
        const auto refused = solve(good, tooLong);
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().problem == SparseSolveProblem::DimensionMismatch);
    }

    SECTION("a NaN matrix entry, at index 0, not only an infinity at index 1") {
        Csr bad = good;
        bad.values[0] = nan;
        const auto refused = solve(bad, {1.0, 1.0, 1.0});
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().problem == SparseSolveProblem::NonFiniteSystem);
        CHECK(refused.error().index == 0);
    }

    SECTION("a negative infinity matrix entry, at the last index") {
        Csr bad = good;
        bad.values.back() = -inf;
        const auto refused = solve(bad, {1.0, 1.0, 1.0});
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().problem == SparseSolveProblem::NonFiniteSystem);
        CHECK(refused.error().index == bad.values.size() - 1);
    }

    SECTION("an INFINITE right-hand side entry, at index 0, not only a NaN") {
        const auto refused = solve(good, {inf, 1.0, 1.0});
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().problem == SparseSolveProblem::NonFiniteSystem);
        CHECK(refused.error().index == 0);
    }

    SECTION("a doubly malformed system reports the EARLIER fault") {
        // The order of the phases is a claim, and a claim with no test is
        // prose. A column out of range AND a non-finite value must report the
        // dimension fault, because that phase runs first.
        Csr bad = good;
        bad.inner[1] = 9;
        bad.values[1] = nan;
        const auto byGeneral = solve(bad, {1.0, 1.0, 1.0});
        const auto bySymmetric = linalg::solveSymmetricSparse(bad.rows, bad.rowStart, bad.inner, bad.values,
                                                              std::vector<double>{1.0, 1.0, 1.0},
                                                              linalg::SymmetricSolveSettings{});
        REQUIRE_FALSE(byGeneral.has_value());
        REQUIRE_FALSE(bySymmetric.has_value());
        CHECK(byGeneral.error().problem == SparseSolveProblem::DimensionMismatch);
        CHECK(bySymmetric.error().problem == linalg::SymmetricSolveProblem::DimensionMismatch);
    }
}

TEST_CASE("Linalg_SparsePredictorAgreesWithTheSolveOnEveryShape", "[linalg][sparse]") {
    // THE PREDICTOR WAS SHOWN ONE FAILING INPUT, so `return solved.failure->problem;`
    // could be replaced by a literal `SingularSystem` and nothing noticed. The
    // header's promise is "the same checks in the same order", which is a claim
    // about EVERY input and is tested here over every malformed shape the suite
    // builds.
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const Csr good = csrOf({{1.0, 2.0, 0.0}, {3.0, 1.0, 0.0}, {0.0, 1.0, 3.0}});
    const Csr singular = csrOf({{1.0, 2.0, 0.0}, {1.0, 2.0, 0.0}, {0.0, 1.0, 3.0}});
    Csr outOfRange = good;
    outOfRange.inner[1] = 9;
    Csr nonFinite = good;
    nonFinite.values[0] = nan;

    struct Shape {
        const char* what;
        Csr matrix;
        std::vector<double> rhs;
        SparseSolveSettings settings;
    };
    const std::vector<Shape> shapes = {
        {"well posed", good, {1.0, 1.0, 1.0}, SparseSolveSettings{}},
        {"singular", singular, {1.0, 1.0, 1.0}, SparseSolveSettings{}},
        {"column out of range", outOfRange, {1.0, 1.0, 1.0}, SparseSolveSettings{}},
        {"non-finite entry", nonFinite, {1.0, 1.0, 1.0}, SparseSolveSettings{}},
        {"short right-hand side", good, {1.0}, SparseSolveSettings{}},
        {"over-long right-hand side", good, {1.0, 1.0, 1.0, 1.0}, SparseSolveSettings{}},
        {"unusable settings", good, {1.0, 1.0, 1.0},
         SparseSolveSettings{.relativeResidualTolerance = 0.0, .rankRatioFloor = 1.0e-12}},
        {"a tolerance nothing can meet", good, {1.0, 1.0, 1.0},
         SparseSolveSettings{.relativeResidualTolerance = 1.0e-300, .rankRatioFloor = 1.0e-12}},
    };

    for (const Shape& shape : shapes) {
        INFO(shape.what);
        const auto solved = linalg::solveSparse(shape.matrix.rows, shape.matrix.rowStart, shape.matrix.inner,
                                                shape.matrix.values, shape.rhs, shape.settings);
        const std::optional<SparseSolveProblem> predicted =
            linalg::sparseSolveProblem(shape.matrix.rows, shape.matrix.rowStart, shape.matrix.inner,
                                       shape.matrix.values, shape.rhs, shape.settings);
        CHECK(solved.has_value() == !predicted.has_value());
        if (!solved.has_value()) {
            REQUIRE(predicted.has_value());
            CHECK(*predicted == solved.error().problem);
        }
    }
}

TEST_CASE("Linalg_SparseRatioExactlyAtTheFloorIsAccepted", "[linalg][sparse]") {
    // THE COMPARISON IS STRICT, and no fixture sat at the boundary, so
    // `<` -> `<=` survived. A ratio EQUAL to the floor must be accepted: the
    // floor is the smallest ratio still admitted, which is what "below the
    // floor" means in the refusal's own words.
    const Csr m = csrOf({{1.0, 2.0, 0.0}, {1.0, 2.0 + 1.0e-9, 0.0}, {0.0, 1.0, 3.0}});
    const std::vector<double> rhs = {1.0, 1.0, 1.0};
    const auto measured = solve(m, rhs);
    REQUIRE(measured.has_value());
    const double ratio = measured->rankRatio;
    REQUIRE(ratio > 0.0);

    const auto atTheFloor =
        solve(m, rhs, SparseSolveSettings{.relativeResidualTolerance = 1.0e-9, .rankRatioFloor = ratio});
    CHECK(atTheFloor.has_value());

    const auto justAbove = solve(
        m, rhs,
        SparseSolveSettings{.relativeResidualTolerance = 1.0e-9, .rankRatioFloor = std::nextafter(ratio, 1.0)});
    REQUIRE_FALSE(justAbove.has_value());
    CHECK(justAbove.error().problem == SparseSolveProblem::SingularSystem);
}

TEST_CASE("Linalg_SparseResidualFailureCarriesEveryMeasurementItMade", "[linalg][sparse][residual]") {
    // ON THE RESIDUAL PATH the failure carried `rankRatio` but nothing asserted
    // `reportedRank`, so it could be zeroed undetectably -- and a caller
    // reading it would conclude the factorisation found no independent columns.
    const Csr m = chain(16, 1.0e-6, 1.0e-6);
    std::vector<double> rhs(16, 0.0);
    rhs[0] = 1.0e-9;
    const auto best = solve(m, rhs);
    REQUIRE(best.has_value());
    REQUIRE(best->residual.normalized > 0.0);
    const auto refused =
        solve(m, rhs,
              SparseSolveSettings{.relativeResidualTolerance = best->residual.normalized / 1.0e3,
                                  .rankRatioFloor = 1.0e-12});
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().problem == SparseSolveProblem::ResidualTooLarge);
    // THE SAME SYSTEM AND THE SAME SOLVE, so every measurement must match the
    // one the successful call reported.
    CHECK(refused.error().rankRatio == best->rankRatio);
    CHECK(refused.error().reportedRank == best->reportedRank);
    CHECK(refused.error().residual.normalized == best->residual.normalized);
    CHECK(refused.error().rows == 16);
}
