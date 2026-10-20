// The generic symmetric sparse solve (ADR-052), tested on its own terms.
//
// The kernel was moved out of structural and is exercised there by P17's own
// suite, unchanged. These cases test what only the GENERIC layer can: that it
// knows no physics, that its failure carries structured facts, and that the
// acceptance step works without a solve -- so a wrong finite answer can be shown
// to it and refused. Every expected value is typed in from a hand derivation.

#include <bettercad/linalg/SymmetricSolve.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cmath>
#include <limits>
#include <vector>

using namespace bettercad;
using namespace bettercad::linalg;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;

namespace {

struct Csr {
    std::size_t rows = 0;
    std::vector<SparseIndex> rowStart;
    std::vector<SparseIndex> inner;
    std::vector<double> values;
};

[[nodiscard]] Csr csrOf(const std::vector<std::vector<double>>& dense) {
    Csr out;
    out.rows = dense.size();
    out.rowStart.assign(out.rows + 1, 0);
    for (std::size_t r = 0; r < out.rows; ++r) {
        for (std::size_t c = 0; c < out.rows; ++c) {
            if (dense[r][c] != 0.0) {
                out.inner.push_back(c);
                out.values.push_back(dense[r][c]);
            }
        }
        out.rowStart[r + 1] = out.inner.size();
    }
    return out;
}

[[nodiscard]] std::expected<SymmetricSolution, SymmetricSolveFailure>
solve(const Csr& a, const std::vector<double>& b, const SymmetricSolveSettings& settings = {}) {
    return solveSymmetricSparse(a.rows, a.rowStart, a.inner, a.values, b, settings);
}

} // namespace

TEST_CASE("Linalg_TwoByTwoExact", "[linalg]") {
    // [4 1; 1 3] x = [1; 2]  ->  x = [1/11, 7/11]: det = 11, x0 = (3*1 - 1*2)/11, x1 = (4*2 - 1*1)/11.
    const Csr a = csrOf({{4, 1}, {1, 3}});
    const auto solved = solve(a, {1.0, 2.0});
    REQUIRE(solved.has_value());
    CHECK_THAT(solved->x[0], WithinAbs(1.0 / 11.0, 1e-15));
    CHECK_THAT(solved->x[1], WithinAbs(7.0 / 11.0, 1e-15));
    CHECK(solved->residual.normalized <= 1e-15);
    CHECK(solved->pivotRatio > 0.0);
    // ||A||_inf = 5 (the larger absolute row sum), ||x||, ||b||, written out.
    CHECK(solved->residual.matrixInfinity == 5.0);
    CHECK_THAT(solved->residual.rightHandSide, WithinAbs(std::sqrt(5.0), 1e-15));
    CHECK_THAT(solved->residual.solution, WithinAbs(std::sqrt(1.0 + 49.0) / 11.0, 1e-15));
}

TEST_CASE("Linalg_EmptySystemIsSolved", "[linalg]") {
    const Csr a;
    Csr empty = a;
    empty.rowStart = {0};
    const auto solved = solve(empty, {});
    REQUIRE(solved.has_value());
    CHECK(solved->x.empty());
    CHECK(solved->pivotRatio == 1.0);
    CHECK(solved->residual.normalized == 0.0);
}

TEST_CASE("Linalg_SingularSystemCarriesStructuredFacts", "[linalg][singular]") {
    // Exactly singular: the library reports a zero pivot, and the failure says so.
    const auto exact = solve(csrOf({{1, 1}, {1, 1}}), {2.0, 2.0});
    REQUIRE_FALSE(exact.has_value());
    CHECK(exact.error().problem == SymmetricSolveProblem::SingularSystem);
    CHECK(exact.error().exactlyZeroPivot);
    CHECK(exact.error().rows == 2);
    CHECK_THAT(exact.error().error.message, ContainsSubstring("exactly zero pivot"));
    // Indefinite: a negative pivot, measured by the kernel and not by the library.
    const auto indefinite = solve(csrOf({{1, 2}, {2, 1}}), {1.0, 1.0});
    REQUIRE_FALSE(indefinite.has_value());
    CHECK(indefinite.error().problem == SymmetricSolveProblem::SingularSystem);
    CHECK_FALSE(indefinite.error().exactlyZeroPivot);
    CHECK_FALSE(indefinite.error().allPivotsPositive);
    // A pivot ratio below the floor: the ratio is reported.
    const auto tiny = solve(csrOf({{1, 0}, {0, 1e-14}}), {1.0, 1e-14});
    REQUIRE_FALSE(tiny.has_value());
    CHECK(tiny.error().problem == SymmetricSolveProblem::SingularSystem);
    CHECK(tiny.error().allPivotsPositive);
    CHECK(tiny.error().pivotRatio == 1e-14);
    // The floor is a setting.
    CHECK(solve(csrOf({{1, 0}, {0, 1e-14}}), {1.0, 1e-14}, {.relativeResidualTolerance = 1e-9, .pivotFloor = 1e-16}).has_value());
}

TEST_CASE("Linalg_ValidatesItsInput", "[linalg][invalid]") {
    const Csr a = csrOf({{2, -1}, {-1, 2}});
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    // The right-hand side.
    CHECK(solve(a, {1.0}).error().problem == SymmetricSolveProblem::DimensionMismatch);
    const auto badRhs = solve(a, {nan, 1.0});
    REQUIRE_FALSE(badRhs.has_value());
    CHECK(badRhs.error().problem == SymmetricSolveProblem::NonFiniteSystem);
    CHECK(badRhs.error().index == 0);
    // The matrix.
    Csr badMatrix = a;
    badMatrix.values[1] = inf;
    const auto refusedMatrix = solve(badMatrix, {1.0, 1.0});
    REQUIRE_FALSE(refusedMatrix.has_value());
    CHECK(refusedMatrix.error().problem == SymmetricSolveProblem::NonFiniteSystem);
    CHECK(refusedMatrix.error().index == 1);
    // The layout.
    Csr outOfRange = a;
    outOfRange.inner[1] = 7;
    CHECK(solve(outOfRange, {1.0, 1.0}).error().problem == SymmetricSolveProblem::DimensionMismatch);
    Csr shortStart = a;
    shortStart.rowStart.pop_back();
    CHECK(solve(shortStart, {1.0, 1.0}).error().problem == SymmetricSolveProblem::DimensionMismatch);
    // The settings.
    for (const SymmetricSolveSettings& bad : {SymmetricSolveSettings{0.0, 1e-12}, SymmetricSolveSettings{-1e-9, 1e-12},
                                              SymmetricSolveSettings{nan, 1e-12}, SymmetricSolveSettings{1e-9, 0.0},
                                              SymmetricSolveSettings{1e-9, inf}}) {
        CHECK(solve(a, {1.0, 1.0}, bad).error().problem == SymmetricSolveProblem::InvalidSettings);
    }
    CHECK(validate(SymmetricSolveSettings{}).has_value());
}

TEST_CASE("Linalg_ResidualIsEvaluableWithoutASolve", "[linalg][residual]") {
    // THE ACCEPTANCE STEP ON A CANDIDATE. [4 1; 1 3] x = [1; 2] has x = [1/11, 7/11].
    const Csr a = csrOf({{4, 1}, {1, 3}});
    const std::vector<double> b{1.0, 2.0};
    const std::vector<double> exact{1.0 / 11.0, 7.0 / 11.0};
    const auto accepted = evaluateResidual(a.rows, a.rowStart, a.inner, a.values, b, exact, {});
    REQUIRE(accepted.has_value());
    CHECK(accepted->normalized <= 1e-15);

    // A finite wrong answer is refused, with the measured residual attached.
    const std::vector<double> wrong{1.0 / 11.0, 7.0 / 11.0 + 0.1};
    const auto refused = evaluateResidual(a.rows, a.rowStart, a.inner, a.values, b, wrong, {});
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().problem == SymmetricSolveProblem::ResidualTooLarge);
    CHECK(refused.error().residual.normalized > 1e-3);
    CHECK_THAT(refused.error().residual.euclidean, WithinAbs(0.1 * std::sqrt(1.0 + 9.0), 1e-14));
    CHECK_THAT(refused.error().error.message, ContainsSubstring("The solver reported success; BetterCAD does not"));

    // Non-finite candidates.
    CHECK(evaluateResidual(a.rows, a.rowStart, a.inner, a.values, b, std::vector<double>{std::nan(""), 1.0}, {}).error().problem ==
          SymmetricSolveProblem::NonFiniteSolution);
    CHECK(evaluateResidual(a.rows, a.rowStart, a.inner, a.values, b, std::vector<double>{1.0}, {}).error().problem ==
          SymmetricSolveProblem::DimensionMismatch);
    // The threshold is the setting.
    const std::vector<double> close{1.0 / 11.0 + 1e-7, 7.0 / 11.0};
    CHECK_FALSE(evaluateResidual(a.rows, a.rowStart, a.inner, a.values, b, close, {.relativeResidualTolerance = 1e-12, .pivotFloor = 1e-12}).has_value());
    CHECK(evaluateResidual(a.rows, a.rowStart, a.inner, a.values, b, close, {.relativeResidualTolerance = 1e-3, .pivotFloor = 1e-12}).has_value());
}

TEST_CASE("Linalg_ResidualOverflowNamesTheRow", "[linalg][residual]") {
    // A candidate whose product overflows in row 1: refused as a non-finite residual, with the row
    // reported, and never compared with the tolerance.
    const Csr a = csrOf({{1, 0}, {0, 1e300}});
    const std::vector<double> b{1.0, 0.0};
    const std::vector<double> x{1.0, 1e300};
    const auto refused = evaluateResidual(a.rows, a.rowStart, a.inner, a.values, b, x, {});
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().problem == SymmetricSolveProblem::NonFiniteResidual);
    CHECK(refused.error().index == 1);
    CHECK_THAT(refused.error().error.message, ContainsSubstring("residual component 1"));
    // A non-finite candidate component is named too.
    const std::vector<double> poisoned{1.0, std::numeric_limits<double>::infinity()};
    const auto bad = evaluateResidual(a.rows, a.rowStart, a.inner, a.values, b, poisoned, {});
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().problem == SymmetricSolveProblem::NonFiniteSolution);
    CHECK(bad.error().index == 1);
}

TEST_CASE("Linalg_NoScaleSystemIsHandledExactly", "[linalg][residual]") {
    // x = 0 and b = 0: the denominator vanishes and so does the residual. The honest value is 0.
    const Csr a = csrOf({{2, -1}, {-1, 2}});
    const std::vector<double> zeros{0.0, 0.0};
    const auto zero = evaluateResidual(a.rows, a.rowStart, a.inner, a.values, zeros, zeros, {});
    REQUIRE(zero.has_value());
    CHECK(zero->normalized == 0.0);
    const auto solved = solve(a, {0.0, 0.0});
    REQUIRE(solved.has_value());
    CHECK(solved->x[0] == 0.0);
    CHECK(solved->x[1] == 0.0);
}

TEST_CASE("Linalg_ScalingInvariant", "[linalg][scaling]") {
    const std::vector<std::vector<double>> k{{3, -1, 0}, {-1, 3, -1}, {0, -1, 3}};
    const std::vector<double> b{1.0, 2.0, 3.0};
    const auto reference = solve(csrOf(k), b);
    REQUIRE(reference.has_value());
    for (const double c : {1e-6, 1e-3, 1e3, 1e6}) {
        auto scaled = k;
        for (auto& row : scaled) {
            for (double& v : row) {
                v *= c;
            }
        }
        std::vector<double> rhs = b;
        for (double& v : rhs) {
            v *= c;
        }
        const auto s = solve(csrOf(scaled), rhs);
        REQUIRE(s.has_value());
        for (std::size_t i = 0; i < 3; ++i) {
            CHECK_THAT(s->x[i], WithinAbs(reference->x[i], 1e-12));
        }
        CHECK(s->residual.normalized <= 1e-14);
        // The pivot RATIO is dimensionless, so it does not move.
        CHECK_THAT(s->pivotRatio, WithinAbs(reference->pivotRatio, 1e-12));
    }
}

TEST_CASE("Linalg_DeterministicRepeat", "[linalg][determinism]") {
    const Csr a = csrOf({{4, -1, 0, -1}, {-1, 4, -1, 0}, {0, -1, 4, -1}, {-1, 0, -1, 4}});
    const std::vector<double> b{1.0, 0.5, -0.25, 2.0};
    const auto first = solve(a, b);
    REQUIRE(first.has_value());
    int differences = 0;
    for (int i = 0; i < 40; ++i) {
        const auto again = solve(a, b);
        REQUIRE(again.has_value());
        if (!(*again == *first)) {
            ++differences;
        }
    }
    CHECK(differences == 0);
    // The diagnostic is deterministic too.
    const Csr singular = csrOf({{1, -1}, {-1, 1}});
    const auto bad = solve(singular, {1.0, 0.0});
    for (int i = 0; i < 10; ++i) {
        const auto again = solve(singular, {1.0, 0.0});
        REQUIRE_FALSE(again.has_value());
        CHECK(again.error().problem == bad.error().problem);
        CHECK(again.error().error.message == bad.error().error.message);
    }
}

TEST_CASE("Linalg_ProblemMatchesTheFailure", "[linalg]") {
    const Csr good = csrOf({{2, -1}, {-1, 2}});
    CHECK_FALSE(symmetricSolveProblem(good.rows, good.rowStart, good.inner, good.values, std::vector<double>{1.0, 1.0}, {}).has_value());
    const Csr bad = csrOf({{1, 1}, {1, 1}});
    CHECK(symmetricSolveProblem(bad.rows, bad.rowStart, bad.inner, bad.values, std::vector<double>{1.0, 1.0}, {}) ==
          std::optional{SymmetricSolveProblem::SingularSystem});
    for (const auto p : {SymmetricSolveProblem::InvalidSettings, SymmetricSolveProblem::DimensionMismatch, SymmetricSolveProblem::NonFiniteSystem,
                         SymmetricSolveProblem::FactorizationFailure, SymmetricSolveProblem::SingularSystem,
                         SymmetricSolveProblem::NonFiniteSolution, SymmetricSolveProblem::NonFiniteResidual,
                         SymmetricSolveProblem::ResidualTooLarge}) {
        CHECK(toString(p) != "unknown");
    }
}
