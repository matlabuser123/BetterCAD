// P18-SOLVE-001: the steady thermal solve, hand-checked.
//
// WHAT THESE TESTS ARE FOR. The solve is a REDUCTION, a SOLVE and a CHECK, so the
// claims are about which numbers enter the right-hand side, which systems are
// refused, and whether a wrong answer can pass:
//
//   - the prescribed temperatures enter as `beff = Qf - Kfc Tc` (a mutation that
//     drops the term solves with every prescribed node at 0 K);
//   - free and prescribed unknowns interleave: nothing assumes they are contiguous;
//   - a model with no absolute temperature anywhere is REFUSED, a model anchored by
//     convection alone is not, and a fully prescribed model is a success;
//   - a solver's "success" is not evidence: a perturbed finite answer is refused by
//     the independent residual;
//   - the answer does not move when the whole equation is scaled.
//
// EVERY EXPECTED VALUE IS TYPED IN FROM A HAND DERIVATION or comes from the
// test's own Gaussian elimination (ThermalSolveTestSupport.hpp). Nothing here asks
// the production solve for the answer it is judged against.
//
// The real-mesh and currentness claims are in ThermalSolveMappedTests.cpp.

#include "ThermalSolveTestSupport.hpp"

#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <bit>
#include <chrono>
#include <format>
#include <limits>
#include <numeric>

using namespace sotest;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

/// The five-unknown chain of the interleaved case: conductances 1, 2, 3, 4 between
/// neighbours, `Q0 = 5 W`, unknowns 1 and 4 prescribed at 300 K and 350 K.
///
/// ```text
/// K = [  1 -1  0  0  0 ]        free 0 2 3        prescribed 1 4
///     [ -1  3 -2  0  0 ]        Kff = [1 0 0; 0 5 -3; 0 -3 7]
///     [  0 -2  5 -3  0 ]        Kfc = [-1 0; -2 0; 0 -4]
///     [  0  0 -3  7 -4 ]        Qf = [5 0 0]   Tc = [300 350]
///     [  0  0  0 -4  4 ]        beff = Qf - Kfc Tc = [305 600 1400]
/// ```
[[nodiscard]] Dense2 chain5() {
    return {{1, -1, 0, 0, 0}, {-1, 3, -2, 0, 0}, {0, -2, 5, -3, 0}, {0, 0, -3, 7, -4}, {0, 0, 0, -4, 4}};
}
[[nodiscard]] std::vector<std::pair<std::size_t, double>> chain5Prescribed() { return {{1, 300.0}, {4, 350.0}}; }
[[nodiscard]] std::vector<double> chain5Heat() { return {5.0, 0.0, 0.0, 0.0, 0.0}; }

} // namespace

// ===========================================================================
// The reduction
// ===========================================================================

TEST_CASE("ThermalSolve_DirichletPartitionInterleavedConstraints", "[thermal][solve][partition]") {
    const Algebra a = makeAlgebra(chain5(), chain5Heat(), chain5Prescribed());
    auto reduced = thermal::reduceThermalSystem(a.k, a.q, a.equations, a.constraints);
    REQUIRE(reduced.has_value());

    // The free unknowns are 0, 2, 3 and the prescribed ones 1, 4: INTERLEAVED, and
    // the map says so.
    REQUIRE(a.equations.freeCount() == 3);
    CHECK(a.equations.freeDofs()[0].value() == 0);
    CHECK(a.equations.freeDofs()[1].value() == 2);
    CHECK(a.equations.freeDofs()[2].value() == 3);
    CHECK(a.equations.constrainedCount() == 2);

    REQUIRE(reduced->rows() == 3);
    REQUIRE(reduced->constrained() == 2);
    // Kff = rows/columns [0 2 3] of K.
    const double kff[3][3] = {{1, 0, 0}, {0, 5, -3}, {0, -3, 7}};
    for (std::size_t r = 0; r < 3; ++r) {
        for (std::size_t c = 0; c < 3; ++c) {
            CHECK(reduced->kffAt(r, c) == kff[r][c]);
        }
    }
    // Kfc = rows [0 2 3], columns [1 4] of K.
    const double kfc[3][2] = {{-1, 0}, {-2, 0}, {0, -4}};
    for (std::size_t r = 0; r < 3; ++r) {
        for (std::size_t c = 0; c < 2; ++c) {
            CHECK(reduced->kfcAt(r, c) == kfc[r][c]);
        }
    }
    CHECK(reduced->heat()[0] == 5.0);
    CHECK(reduced->heat()[1] == 0.0);
    CHECK(reduced->heat()[2] == 0.0);
    CHECK(reduced->prescribed()[0] == 300.0);
    CHECK(reduced->prescribed()[1] == 350.0);
    // beff = Qf - Kfc Tc: the prescribed temperatures are in the right-hand side.
    CHECK(reduced->effectiveHeat()[0] == 305.0);   //   5 - (-1*300)
    CHECK(reduced->effectiveHeat()[1] == 600.0);   //   0 - (-2*300)
    CHECK(reduced->effectiveHeat()[2] == 1400.0);  //   0 - (-4*350)
    CHECK(reduced->effectiveHeat()[0] != reduced->heat()[0]);
    WARN("interleaved partition: free {0,2,3}, prescribed {1,4}; Kff = [1 0 0; 0 5 -3; 0 -3 7]; Kfc = [-1 0; -2 0; 0 -4]; "
         "Qf = [5 0 0]; Tc = [300 350]; beff = [305 600 1400]");

    // And the CSR arrays are ascending with no contiguity assumed.
    for (std::size_t r = 0; r < reduced->rows(); ++r) {
        for (auto slot = reduced->rowStart()[r] + 1; slot < reduced->rowStart()[r + 1]; ++slot) {
            CHECK(reduced->innerIndices()[slot - 1] < reduced->innerIndices()[slot]);
        }
    }
}

TEST_CASE("ThermalSolve_InterleavedConstraintsSolve", "[thermal][solve][partition]") {
    const Algebra a = makeAlgebra(chain5(), chain5Heat(), chain5Prescribed());
    const ThermalAlgebraicSolution s = mustSolve(a);
    // T0 = 300 + 5/1 = 305; the 2-3 sub-chain: 5 T2 - 3 T3 = 600, -3 T2 + 7 T3 = 1400,
    // so T2 = 8400/26 and T3 = 8800/26.
    CHECK_THAT(tempAt(s, 0), WithinAbs(305.0, 1e-11));
    CHECK(tempAt(s, 1) == 300.0);
    CHECK_THAT(tempAt(s, 2), WithinAbs(8400.0 / 26.0, 1e-10));
    CHECK_THAT(tempAt(s, 3), WithinAbs(8800.0 / 26.0, 1e-10));
    CHECK(tempAt(s, 4) == 350.0);
    // The test's own elimination agrees.
    const std::vector<double> expected = denseExpected(chain5(), chain5Heat(), chain5Prescribed());
    for (std::size_t i = 0; i < 5; ++i) {
        CHECK_THAT(tempAt(s, i), WithinAbs(expected[i], 1e-10));
    }
    CHECK(s.freeEquations == 3);
    CHECK(s.kernelInvoked);
}

TEST_CASE("ThermalSolve_FreeResidualMatchesFullFreeComponents", "[thermal][solve][residual]") {
    const Algebra a = makeAlgebra(chain5(), chain5Heat(), chain5Prescribed());
    const ThermalAlgebraicSolution s = mustSolve(a);
    auto reduced = thermal::reduceThermalSystem(a.k, a.q, a.equations, a.constraints);
    REQUIRE(reduced.has_value());
    const std::vector<double> t = valuesOf(s);

    // The reduced residual r = Kff Tf - beff, written out in the test.
    std::vector<double> tf;
    for (const ThermalDofIndex dof : a.equations.freeDofs()) {
        tf.push_back(t[static_cast<std::size_t>(dof.value())]);
    }
    std::vector<double> reducedResidual(3, 0.0);
    for (std::size_t r = 0; r < 3; ++r) {
        for (std::size_t c = 0; c < 3; ++c) {
            reducedResidual[r] += reduced->kffAt(r, c) * tf[c];
        }
        reducedResidual[r] -= reduced->effectiveHeat()[r];
    }
    // The full residual K T - Q, written out in the test.
    std::vector<double> full = multiplyDense(chain5(), t);
    for (std::size_t i = 0; i < 5; ++i) {
        full[i] -= chain5Heat()[i];
    }
    // THE FREE COMPONENTS OF THE FULL RESIDUAL ARE THE REDUCED RESIDUAL.
    double worst = 0.0;
    for (std::size_t e = 0; e < 3; ++e) {
        const auto dof = static_cast<std::size_t>(a.equations.freeDofs()[e].value());
        worst = std::max(worst, std::abs(full[dof] - reducedResidual[e]));
        CHECK_THAT(full[dof], WithinAbs(0.0, 1e-10));
    }
    CHECK(worst <= 1e-12);
    // The solution's own retained full residual is the same vector.
    REQUIRE(s.fullResidual.size() == 5);
    for (std::size_t i = 0; i < 5; ++i) {
        CHECK_THAT(s.fullResidual[i], WithinAbs(full[i], 1e-12));
    }
    // AND THE PRESCRIBED COMPONENTS ARE NOT ZERO, which is correct: they are the heat
    // rates the boundary supplies, and they are not a solver failure.
    CHECK(std::abs(full[1]) > 1.0);
    CHECK(std::abs(full[4]) > 1.0);
    CHECK_THAT(full[1], WithinAbs(-305.0 + 900.0 - 2.0 * 8400.0 / 26.0, 1e-9));
    CHECK_THAT(full[4], WithinAbs(-4.0 * 8800.0 / 26.0 + 1400.0, 1e-9));
    // The rows of a conduction matrix sum to zero, so the full residual sums to -sum(Q).
    CHECK_THAT(std::accumulate(full.begin(), full.end(), 0.0), WithinAbs(-5.0, 1e-10));
    WARN(std::format("full K T - Q = [{:.6g} {:.6g} {:.6g} {:.6g} {:.6g}] W: free components ~0, prescribed components "
                     "{:.6g} and {:.6g} W are boundary heat rates; max |reduced - free full| = {:.2e}",
                     full[0], full[1], full[2], full[3], full[4], full[1], full[4], worst));
}

// ===========================================================================
// The one-free-node oracle and the nonzero Dirichlet term
// ===========================================================================

TEST_CASE("ThermalSolve_OneFreeNodeAnalytical", "[thermal][solve][analytical]") {
    // K = [2 -2; -2 2] W/K, Q = [0 100] W, T0 = 300 K held. The free node obeys
    // g (T1 - T0) = P, so T1 = 300 + 100/2 = 350 K.
    const Algebra a = makeAlgebra({{2, -2}, {-2, 2}}, {0.0, 100.0}, {{0, 300.0}});
    auto reduced = thermal::reduceThermalSystem(a.k, a.q, a.equations, a.constraints);
    REQUIRE(reduced.has_value());
    REQUIRE(reduced->rows() == 1);
    CHECK(reduced->kffAt(0, 0) == 2.0);
    CHECK(reduced->kfcAt(0, 0) == -2.0);
    CHECK(reduced->heat()[0] == 100.0);
    CHECK(reduced->prescribed()[0] == 300.0);
    CHECK(reduced->effectiveHeat()[0] == 700.0);  // 100 - (-2 * 300)

    const ThermalAlgebraicSolution s = mustSolve(a);
    CHECK(tempAt(s, 0) == 300.0);  // held exactly
    CHECK_THAT(tempAt(s, 1), WithinAbs(350.0, 1e-12));
    // Free residual 2 * 350 - 700 = 0; the prescribed row is a reaction: 2*300 - 2*350 - 0 = -100 W (node 0 is a sink: the boundary carries the 100 W away).
    CHECK_THAT(s.residual.infinityNorm.si(), WithinAbs(0.0, 1e-12));
    CHECK_THAT(s.fullResidual[0], WithinAbs(-100.0, 1e-10));
    CHECK_THAT(s.fullResidual[1], WithinAbs(0.0, 1e-10));
    WARN(std::format("one free node: Kff = [2], Kfc = [-2], beff = 700, expected T1 = 350 K, actual {:.15g}, error {:.2e}; "
                     "reaction at the prescribed node {:.6g} W",
                     tempAt(s, 1), std::abs(tempAt(s, 1) - 350.0), s.fullResidual[0]));
}

TEST_CASE("ThermalSolve_NonzeroDirichletRhsMutationTrap", "[thermal][solve][analytical]") {
    // THE ANSWER THAT ONLY THE KfcTc TERM PRODUCES. Dropping it gives beff = Qf = 100
    // and Tf = 50 K -- the temperature of a model whose prescribed node sits at 0 K.
    const Algebra a = makeAlgebra({{2, -2}, {-2, 2}}, {0.0, 100.0}, {{0, 300.0}});
    const ThermalAlgebraicSolution s = mustSolve(a);
    CHECK_THAT(tempAt(s, 1), WithinAbs(350.0, 1e-12));
    CHECK(std::abs(tempAt(s, 1) - 50.0) > 299.0);

    // And with the prescribed value at ZERO the two agree, so the term is the whole
    // difference between a model at 300 K and a model at 0 K.
    const Algebra zero = makeAlgebra({{2, -2}, {-2, 2}}, {0.0, 100.0}, {{0, 0.0}});
    CHECK_THAT(tempAt(mustSolve(zero), 1), WithinAbs(50.0, 1e-12));
    // Shifting every prescribed temperature by +c shifts the solution by +c for a
    // pure-conduction model (no absolute-temperature convection term).
    for (const double shift : {10.0, 100.0, 273.15}) {
        const Algebra shifted = makeAlgebra({{2, -2}, {-2, 2}}, {0.0, 100.0}, {{0, 300.0 + shift}});
        CHECK_THAT(tempAt(mustSolve(shifted), 1), WithinAbs(350.0 + shift, 1e-10));
    }
}

TEST_CASE("ThermalSolve_ThreeNodeChainMidpoint", "[thermal][solve][analytical]") {
    // g1 = g2 = 1, T0 = 400, T2 = 300, no load: the free node sits at the midpoint.
    const Algebra a = makeAlgebra({{1, -1, 0}, {-1, 2, -1}, {0, -1, 1}}, {0, 0, 0}, {{0, 400.0}, {2, 300.0}});
    const ThermalAlgebraicSolution s = mustSolve(a);
    CHECK_THAT(tempAt(s, 1), WithinAbs(350.0, 1e-12));
    CHECK(tempAt(s, 0) == 400.0);
    CHECK(tempAt(s, 2) == 300.0);
}

TEST_CASE("ThermalSolve_ThreeNodeChainAsymmetric", "[thermal][solve][analytical]") {
    // g1 = 2, g2 = 1: T1 = (2*400 + 1*300)/3 = 366.666... K. A symmetric chain hides a
    // swapped Kfc column; this one cannot.
    const Algebra a = makeAlgebra({{2, -2, 0}, {-2, 3, -1}, {0, -1, 1}}, {0, 0, 0}, {{0, 400.0}, {2, 300.0}});
    auto reduced = thermal::reduceThermalSystem(a.k, a.q, a.equations, a.constraints);
    REQUIRE(reduced.has_value());
    CHECK(reduced->effectiveHeat()[0] == 1100.0);  // 0 - (-2*400) - (-1*300)
    const ThermalAlgebraicSolution s = mustSolve(a);
    CHECK_THAT(tempAt(s, 1), WithinAbs(1100.0 / 3.0, 1e-11));
    CHECK_THAT(tempAt(s, 1), WithinAbs(366.6666666666667, 1e-9));
    // The same chain with the two prescribed values exchanged moves T1 to 333.33.
    const Algebra swapped = makeAlgebra({{2, -2, 0}, {-2, 3, -1}, {0, -1, 1}}, {0, 0, 0}, {{0, 300.0}, {2, 400.0}});
    CHECK_THAT(tempAt(mustSolve(swapped), 1), WithinAbs((2.0 * 300.0 + 1.0 * 400.0) / 3.0, 1e-11));
}

TEST_CASE("ThermalSolve_RobinOnlyNoDirichlet", "[thermal][solve][analytical]") {
    // One unknown tied to the ambient by g = h A = 4 W/K, 40 W applied, T_inf = 300 K:
    //     g T = g T_inf + P   =>   4 T = 1200 + 40   =>   T = 310 K.
    // NO prescribed temperature at all, and the model is well posed: the convection
    // coefficient is the datum.
    const Algebra a = makeAlgebra({{4}}, {4.0 * 300.0 + 40.0}, {});
    REQUIRE(a.constraints.isEmpty());
    const ThermalAlgebraicSolution s = mustSolve(a);
    CHECK_THAT(tempAt(s, 0), WithinAbs(310.0, 1e-11));
    CHECK(s.kernelInvoked);
    CHECK(s.anchoring.regions == 1);
    CHECK(s.anchoring.anchored == 1);
    CHECK(s.anchoring.unanchored == 0);
    WARN(std::format("Robin only, no Dirichlet: K = [4], Q = [1240], T_inf = 300 K, P = 40 W: expected 310 K, actual {:.15g}, "
                     "residual {:.2e} W",
                     tempAt(s, 0), s.residual.euclideanNorm.si()));

    // Linearity: doubling the applied heat doubles the rise above the ambient.
    const Algebra twice = makeAlgebra({{4}}, {4.0 * 300.0 + 80.0}, {});
    CHECK_THAT(tempAt(mustSolve(twice), 0) - 300.0, WithinAbs(2.0 * (tempAt(s, 0) - 300.0), 1e-10));
    // Shifting the ambient shifts the solution: T = T_inf + P/g. Shifting only the prescribed values of a
    // CONVECTION model would NOT (the film pulls toward T_inf), so the invariance asserted is the right one.
    for (const double ambient : {250.0, 400.0, 1000.0}) {
        const Algebra shifted = makeAlgebra({{4}}, {4.0 * ambient + 40.0}, {});
        CHECK_THAT(tempAt(mustSolve(shifted), 0), WithinAbs(ambient + 10.0, 1e-9));
    }
    // Kelvin is kept as given: 300 is not treated as degrees Celsius.
    CHECK(std::abs(tempAt(s, 0) - (300.0 - 273.15 + 10.0)) > 200.0);
}

TEST_CASE("ThermalSolve_RobinAnchoredChain", "[thermal][solve][analytical]") {
    // Two nodes, conductance 1 between them, node 1 tied to the ambient by 1 W/K:
    // K = [1 -1; -1 2], Q = [10, 300]. Heat 10 W into node 0 flows through the link, so
    // T0 - T1 = 10 and T1 = 300 + 10 = 310, T0 = 320.
    const Algebra a = makeAlgebra({{1, -1}, {-1, 2}}, {10.0, 300.0}, {});
    const ThermalAlgebraicSolution s = mustSolve(a);
    CHECK_THAT(tempAt(s, 1), WithinAbs(310.0, 1e-10));
    CHECK_THAT(tempAt(s, 0), WithinAbs(320.0, 1e-10));
}

// ===========================================================================
// A fully prescribed model, and the free residual of nothing
// ===========================================================================

TEST_CASE("ThermalSolve_FullyConstrainedSystem", "[thermal][solve][constrained]") {
    // Every unknown prescribed: Nfree = 0. The model is VALID, T = Tc, and nothing is
    // handed to the linear kernel.
    const Algebra a = makeAlgebra({{2, -2}, {-2, 2}}, {7.0, -3.0}, {{0, 300.0}, {1, 350.0}});
    REQUIRE(a.equations.freeCount() == 0);
    const ThermalAlgebraicSolution s = mustSolve(a);
    CHECK(s.freeEquations == 0);
    CHECK_FALSE(s.kernelInvoked);
    CHECK(tempAt(s, 0) == 300.0);
    CHECK(tempAt(s, 1) == 350.0);
    // The free residual of no equation is accepted trivially.
    CHECK(s.residual.normalized == 0.0);
    CHECK(s.residual.euclideanNorm.si() == 0.0);
    CHECK(s.pivotRatio == 1.0);
    // Q does not change T, and does change what the boundary supplies: K T - Q = [-100 - 7, 100 + 3].
    CHECK_THAT(s.fullResidual[0], WithinAbs(2.0 * 300.0 - 2.0 * 350.0 - 7.0, 1e-12));
    CHECK_THAT(s.fullResidual[1], WithinAbs(-2.0 * 300.0 + 2.0 * 350.0 + 3.0, 1e-12));
    WARN("fully constrained: Ndof 2, Nfree 0, Nconstrained 2, kernel invoked NO, T = [300 350] = Tc, status accepted");

    // Arbitrary finite Q gives the same T.
    const Algebra other = makeAlgebra({{2, -2}, {-2, 2}}, {1.0e6, -5.0e5}, {{0, 300.0}, {1, 350.0}});
    CHECK(valuesOf(mustSolve(other)) == valuesOf(s));
    // Repeats are identical.
    CHECK(mustSolve(a) == s);
}

// ===========================================================================
// Well-posedness: pure Neumann, unanchored regions, disconnected bodies
// ===========================================================================

TEST_CASE("ThermalSolve_PureNeumannSingular", "[thermal][solve][singular]") {
    const Dense2 k{{1, -1}, {-1, 1}};
    // A compatible zero-net load: infinitely many solutions, and the answer is a refusal.
    const Algebra a = makeAlgebra(k, {0.0, 0.0}, {});
    const auto solved = solveAlgebra(a);
    REQUIRE_FALSE(solved.has_value());
    CHECK(solved.error().problem == SteadySolveProblem::MissingTemperatureDatum);
    CHECK_THAT(solved.error().error.message, ContainsSubstring("datum"));
    // A net source: no solution at all. Still a refusal, and no arbitrary level is returned.
    const Algebra incompatible = makeAlgebra(k, {1.0, 0.0}, {});
    const auto bad = solveAlgebra(incompatible);
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().problem == SteadySolveProblem::MissingTemperatureDatum);
    // The same conduction with one temperature prescribed is well posed.
    const Algebra pinnedByData = makeAlgebra(k, {0.0, 0.0}, {{0, 310.0}});
    CHECK_THAT(tempAt(mustSolve(pinnedByData), 1), WithinAbs(310.0, 1e-12));
    WARN("pure Neumann [1 -1; -1 1], Q = 0, no constraints: missing_temperature_datum, accepted result NO");
}

TEST_CASE("ThermalSolve_PureNeumannChainOfManyNodes", "[thermal][solve][singular]") {
    // A longer conduction chain with nothing prescribed and no convection.
    Dense2 k(6, std::vector<double>(6, 0.0));
    for (std::size_t i = 0; i + 1 < 6; ++i) {
        k[i][i] += 2.0;
        k[i + 1][i + 1] += 2.0;
        k[i][i + 1] -= 2.0;
        k[i + 1][i] -= 2.0;
    }
    const Algebra a = makeAlgebra(k, std::vector<double>(6, 0.0), {});
    const auto solved = solveAlgebra(a);
    REQUIRE_FALSE(solved.has_value());
    CHECK(solved.error().problem == SteadySolveProblem::MissingTemperatureDatum);
    CHECK(problemOf(solved) == std::optional{SteadySolveProblem::MissingTemperatureDatum});
}

TEST_CASE("ThermalSolve_DisconnectedUnanchoredFails", "[thermal][solve][singular][disconnected]") {
    // Component A = unknowns 0..2 with unknown 0 prescribed at 300 K; component B =
    // unknowns 3, 4 with nothing holding it. A block-diagonal K.
    Dense2 k(5, std::vector<double>(5, 0.0));
    const Dense2 chain{{1, -1, 0}, {-1, 2, -1}, {0, -1, 1}};
    for (std::size_t r = 0; r < 3; ++r) {
        for (std::size_t c = 0; c < 3; ++c) {
            k[r][c] = chain[r][c];
        }
    }
    k[3][3] = 1;
    k[3][4] = -1;
    k[4][3] = -1;
    k[4][4] = 1;
    const Algebra a = makeAlgebra(k, std::vector<double>(5, 0.0), {{0, 300.0}});
    const auto solved = solveAlgebra(a);
    REQUIRE_FALSE(solved.has_value());
    CHECK(solved.error().problem == SteadySolveProblem::UnanchoredThermalRegion);
    CHECK_THAT(solved.error().error.message, ContainsSubstring("1 of the model's 2"));
    // The analysis, read directly.
    auto reduced = thermal::reduceThermalSystem(a.k, a.q, a.equations, a.constraints);
    REQUIRE(reduced.has_value());
    const thermal::AnchorAnalysis analysis = thermal::analyseAnchoring(*reduced);
    CHECK(analysis.regions == 2);
    CHECK(analysis.anchored == 1);
    CHECK(analysis.unanchored == 1);
    REQUIRE(analysis.firstUnanchoredEquation.has_value());
    CHECK(*analysis.firstUnanchoredEquation == 2);  // free equations: 0->1, 1->2, 2->3, 3->4; B starts at unknown 3 = equation 2
    WARN("disconnected: A = {0,1,2} anchored by T0 = 300 K, B = {3,4} unanchored: unanchored_thermal_region, accepted result NO");
}

TEST_CASE("ThermalSolve_DisconnectedAnchoredPasses", "[thermal][solve][disconnected]") {
    // Disconnected is not invalid. B is tied to a 300 K ambient by a 0.5 W/K convection
    // face on unknown 3, with 10 W applied there: 1.5 T3 - T4 = 0.5*300 + 10, T3 = T4.
    Dense2 k(5, std::vector<double>(5, 0.0));
    const Dense2 chain{{1, -1, 0}, {-1, 2, -1}, {0, -1, 1}};
    for (std::size_t r = 0; r < 3; ++r) {
        for (std::size_t c = 0; c < 3; ++c) {
            k[r][c] = chain[r][c];
        }
    }
    k[3][3] = 1.5;
    k[3][4] = -1;
    k[4][3] = -1;
    k[4][4] = 1;
    const Algebra a = makeAlgebra(k, {0, 0, 0, 0.5 * 300.0 + 10.0, 0}, {{0, 300.0}});
    const ThermalAlgebraicSolution s = mustSolve(a);
    CHECK(s.anchoring.regions == 2);
    CHECK(s.anchoring.anchored == 2);
    CHECK_THAT(tempAt(s, 1), WithinAbs(300.0, 1e-11));  // A: nothing drives it away from T0
    CHECK_THAT(tempAt(s, 2), WithinAbs(300.0, 1e-11));
    CHECK_THAT(tempAt(s, 3), WithinAbs(320.0, 1e-10));  // 0.5 (T3 - 300) = 10
    CHECK_THAT(tempAt(s, 4), WithinAbs(320.0, 1e-10));

    // Two components each held by a prescribed temperature: also fine.
    const Algebra both = makeAlgebra(k, std::vector<double>(5, 0.0), {{0, 300.0}, {4, 400.0}});
    const ThermalAlgebraicSolution b = mustSolve(both);
    CHECK(b.anchoring.regions == 2);
    CHECK(b.anchoring.unanchored == 0);
    const std::vector<double> expected = denseExpected(k, std::vector<double>(5, 0.0), {{0, 300.0}, {4, 400.0}});
    for (std::size_t i = 0; i < 5; ++i) {
        CHECK_THAT(tempAt(b, i), WithinAbs(expected[i], 1e-10));
    }
}

TEST_CASE("ThermalSolve_ExplicitZerosDoNotConnectRegions", "[thermal][solve][disconnected]") {
    // A pattern built from connectivity holds EXACT ZEROS. An entry that is stored and
    // zero couples nothing, so two blocks joined only by one are still two regions.
    std::vector<ThermalSparseMatrix::Index> rowStart{0, 3, 6, 8};
    std::vector<ThermalSparseMatrix::Index> inner{0, 1, 2, 0, 1, 2, 1, 2};
    // Rows: [2 -2 0] [-2 2 0] [0 0 1] stored with the zeros at (0,2), (1,2), (2,1).
    std::vector<double> values{2, -2, 0, -2, 2, 0, 0, 1};
    auto built = ThermalSparseMatrix::fromCsr(3, 3, rowStart, inner, values);
    REQUIRE(built.has_value());
    AsmHand hand = makeAsmHand(Nodes{{1, at(0, 0, 0)}, {2, at(1, 0, 0)}, {3, at(2, 0, 0)}}, {}, {});
    auto constraints = thermal::buildThermalConstraintMap(hand.dofs, std::vector<ThermalTemperatureConstraint>{});
    REQUIRE(constraints.has_value());
    auto equations = thermal::buildThermalFreeEquationMap(hand.dofs, *constraints);
    REQUIRE(equations.has_value());
    const ThermalHeatVector q = heatOf({0, 0, 300.0});
    auto reduced = thermal::reduceThermalSystem(*built, q, *equations, *constraints);
    REQUIRE(reduced.has_value());
    const thermal::AnchorAnalysis analysis = thermal::analyseAnchoring(*reduced);
    CHECK(analysis.regions == 2);   // {0,1} and {2}
    CHECK(analysis.anchored == 1);  // {2} has a nonzero row sum (1)
    CHECK(analysis.unanchored == 1);
    const auto solved = thermal::solveThermalAlgebra(*built, q, *equations, *constraints, ThermalSolverSettings{});
    REQUIRE_FALSE(solved.has_value());
    CHECK(solved.error().problem == SteadySolveProblem::UnanchoredThermalRegion);
}

TEST_CASE("ThermalSolve_AnchorToleranceSeparatesRoundingFromAnchoring", "[thermal][solve][singular]") {
    // A conduction row sums to zero to rounding; the weakest sensible anchor is nine
    // orders above it. The classifier's constant sits between the two.
    CHECK(thermal::kAnchorTolerance > 1.0e-14);
    CHECK(thermal::kAnchorTolerance < 1.0e-8);
    // A tiny but real anchor (1e-6 of the conduction) is recognised...
    const Algebra weak = makeAlgebra({{1.0 + 1e-6, -1}, {-1, 1}}, {1e-6 * 300.0, 0.0}, {});
    CHECK(mustSolve(weak).anchoring.unanchored == 0);
    // ...and rounding noise is not one.
    const double noise = 3.0e-16;
    const Algebra none = makeAlgebra({{1.0 + noise, -1}, {-1, 1}}, {0.0, 0.0}, {});
    CHECK(problemOf(solveAlgebra(none)) == std::optional{SteadySolveProblem::MissingTemperatureDatum});
}

TEST_CASE("ThermalSolve_FactorisationSingularityIsIndependentOfAnchoring", "[thermal][solve][singular]") {
    // Anchored in the row-sum sense, yet not a usable system: the kernel's own gate
    // must still refuse it, with its own classification.
    // Indefinite: pivots 1 and 1 - 4 = -3.
    CHECK(problemOf(solveAlgebra(makeAlgebra({{1, 2}, {2, 1}}, {1.0, 1.0}, {}))) ==
          std::optional{SteadySolveProblem::SingularSystem});
    // Exactly singular: pivots 1 and 0.
    CHECK(problemOf(solveAlgebra(makeAlgebra({{1, 1}, {1, 1}}, {2.0, 2.0}, {}))) ==
          std::optional{SteadySolveProblem::SingularSystem});
    // A pivot ratio below the floor.
    const Algebra tiny = makeAlgebra({{1, 0}, {0, 1e-14}}, {1.0, 1e-14}, {});
    CHECK(problemOf(solveAlgebra(tiny)) == std::optional{SteadySolveProblem::SingularSystem});
    // The floor is a setting: relaxed, the same system solves, and exactly.
    const ThermalAlgebraicSolution relaxed = mustSolve(tiny, ThermalSolverSettings{.relativeResidualTolerance = 1e-9, .pivotFloor = 1e-16});
    CHECK_THAT(tempAt(relaxed, 0), WithinAbs(1.0, 1e-12));
    CHECK_THAT(tempAt(relaxed, 1), WithinAbs(1.0, 1e-9));
    CHECK(relaxed.pivotRatio == 1e-14);
}

// ===========================================================================
// The independent residual, and the wrong answers it must refuse
// ===========================================================================

TEST_CASE("ThermalSolve_IndependentResidualAndAcceptance", "[thermal][solve][residual]") {
    const Algebra a = makeAlgebra(chain5(), chain5Heat(), chain5Prescribed());
    const ThermalAlgebraicSolution s = mustSolve(a);
    CHECK(s.residual.normalized <= 1e-9);
    // Recomputed in the test from the hand partition: r = Kff Tf - beff.
    const double kff[3][3] = {{1, 0, 0}, {0, 5, -3}, {0, -3, 7}};
    const double beff[3] = {305, 600, 1400};
    const double tf[3] = {tempAt(s, 0), tempAt(s, 2), tempAt(s, 3)};
    double squared = 0.0;
    double biggest = 0.0;
    double rowMax = 0.0;
    double tfSquared = 0.0;
    double bSquared = 0.0;
    for (int r = 0; r < 3; ++r) {
        double product = 0.0;
        double rowSum = 0.0;
        for (int c = 0; c < 3; ++c) {
            product += kff[r][c] * tf[c];
            rowSum += std::abs(kff[r][c]);
        }
        const double entry = product - beff[r];
        squared += entry * entry;
        biggest = std::max(biggest, std::abs(entry));
        rowMax = std::max(rowMax, rowSum);
        tfSquared += tf[r] * tf[r];
        bSquared += beff[r] * beff[r];
    }
    CHECK_THAT(s.residual.euclideanNorm.si(), WithinAbs(std::sqrt(squared), 1e-13));
    CHECK_THAT(s.residual.infinityNorm.si(), WithinAbs(biggest, 1e-13));
    CHECK_THAT(s.residual.conductanceInfinityNorm.si(), WithinRel(rowMax, 1e-15));
    CHECK_THAT(s.residual.temperatureNorm.si(), WithinRel(std::sqrt(tfSquared), 1e-15));
    CHECK_THAT(s.residual.heatNorm.si(), WithinRel(std::sqrt(bSquared), 1e-15));
    const double denominator = rowMax * std::sqrt(tfSquared) + std::sqrt(bSquared);
    CHECK_THAT(s.residual.normalized, WithinAbs(std::sqrt(squared) / denominator, 1e-18));
    // The acceptance helper agrees.
    const auto accepted = thermal::validateSteadySolution(a.k, a.q, a.equations, a.constraints, s.temperatures, ThermalSolverSettings{});
    REQUIRE(accepted.has_value());
    CHECK(accepted->normalized == s.residual.normalized);
    WARN(std::format("5-unknown interleaved case: ||r||_2 = {:.3e} W, ||r||_inf = {:.3e} W, normalized = {:.3e} against 1e-9",
                     s.residual.euclideanNorm.si(), s.residual.infinityNorm.si(), s.residual.normalized));
}

TEST_CASE("ThermalSolve_BadSolutionFailsResidual", "[thermal][solve][residual]") {
    // THE CLAIM: a solver's "success" is not evidence. The acceptance step is shown a
    // finite, plausible, WRONG answer, and refuses it on the residual alone.
    const Algebra a = makeAlgebra(chain5(), chain5Heat(), chain5Prescribed());
    const ThermalAlgebraicSolution good = mustSolve(a);
    ThermalSolverSettings settings{};
    REQUIRE(thermal::validateSteadySolution(a.k, a.q, a.equations, a.constraints, good.temperatures, settings).has_value());

    for (const std::size_t freeDof : {0u, 2u, 3u}) {
        std::vector<Temperature> bad = good.temperatures;
        bad[freeDof] = Temperature::fromSi(bad[freeDof].si() + 1.0);  // one kelvin off
        const auto refused = thermal::validateSteadySolution(a.k, a.q, a.equations, a.constraints, bad, settings);
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().problem == SteadySolveProblem::ResidualTooLarge);
        CHECK_THAT(refused.error().error.message, ContainsSubstring("The solver reported success; BetterCAD does not"));
    }
    // A perturbation far below the tolerance is accepted: the gate is scale-aware, not a coin flip.
    std::vector<Temperature> tiny = good.temperatures;
    tiny[2] = Temperature::fromSi(tiny[2].si() + 1.0e-12);
    CHECK(thermal::validateSteadySolution(a.k, a.q, a.equations, a.constraints, tiny, settings).has_value());
    // And the threshold is the SETTING: tighten it and the same tiny error is refused.
    ThermalSolverSettings strict{.relativeResidualTolerance = 1.0e-18, .pivotFloor = 1e-12};
    CHECK_FALSE(thermal::validateSteadySolution(a.k, a.q, a.equations, a.constraints, tiny, strict).has_value());
    WARN("bad-solution test: solver status Success (synthetic), one free temperature off by 1 K: residual_too_large, accepted NO");
}

TEST_CASE("ThermalSolve_RejectsNonFiniteSolutionAndResidual", "[thermal][solve][finite]") {
    const Algebra a = makeAlgebra(chain5(), chain5Heat(), chain5Prescribed());
    const ThermalAlgebraicSolution good = mustSolve(a);
    ThermalSolverSettings settings{};
    for (const double poison : {kNaN, kInf, -kInf}) {
        std::vector<Temperature> bad = good.temperatures;
        bad[2] = Temperature::fromSi(poison);
        const auto refused = thermal::validateSteadySolution(a.k, a.q, a.equations, a.constraints, bad, settings);
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().problem == SteadySolveProblem::NonFiniteSolution);
    }
    // A prescribed unknown that is not finite is also a non-finite solution, not a constraint report.
    std::vector<Temperature> badConstrained = good.temperatures;
    badConstrained[1] = Temperature::fromSi(kNaN);
    CHECK(problemOf(thermal::validateSteadySolution(a.k, a.q, a.equations, a.constraints, badConstrained, settings)) ==
          std::optional{SteadySolveProblem::NonFiniteSolution});

    // A residual that overflows is refused, never compared: huge temperatures against a huge matrix.
    const Algebra big = makeAlgebra({{1.0e300, -1.0e300}, {-1.0e300, 1.0e300}}, {0.0, 0.0}, {{0, 300.0}});
    std::vector<Temperature> overflow{Temperature::fromSi(300.0), Temperature::fromSi(1.0e300)};
    const auto refused = thermal::validateSteadySolution(big.k, big.q, big.equations, big.constraints, overflow, settings);
    REQUIRE_FALSE(refused.has_value());
    CHECK((refused.error().problem == SteadySolveProblem::NonFiniteResidual || refused.error().problem == SteadySolveProblem::NonFiniteInput));
}

TEST_CASE("ThermalSolve_ConstraintViolationIsAHardFailure", "[thermal][solve][constrained]") {
    const Algebra a = makeAlgebra(chain5(), chain5Heat(), chain5Prescribed());
    const ThermalAlgebraicSolution good = mustSolve(a);
    std::vector<Temperature> bad = good.temperatures;
    // One ulp off a prescribed value: a tolerance would admit it; exact equality does not.
    bad[4] = Temperature::fromSi(std::nextafter(350.0, 400.0));
    const auto refused = thermal::validateSteadySolution(a.k, a.q, a.equations, a.constraints, bad, ThermalSolverSettings{});
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().problem == SteadySolveProblem::ConstraintNotPreserved);
    CHECK_THAT(refused.error().error.message, ContainsSubstring("unknown 4"));
    // A candidate of the wrong length is a numbering error.
    const std::vector<Temperature> shortVector(3, Temperature::fromSi(300.0));
    CHECK(problemOf(thermal::validateSteadySolution(a.k, a.q, a.equations, a.constraints, shortVector, ThermalSolverSettings{})) ==
          std::optional{SteadySolveProblem::DofMapMismatch});
}

TEST_CASE("ThermalSolve_NonFiniteReducedRightHandSideIsRefused", "[thermal][solve][finite]") {
    // Kfc Tc overflows although every stored number is finite.
    const Algebra a = makeAlgebra({{1.0e308, -1.0e308}, {-1.0e308, 1.0e308}}, {0.0, 0.0}, {{0, 400.0}});
    const auto solved = solveAlgebra(a);
    REQUIRE_FALSE(solved.has_value());
    CHECK(solved.error().problem == SteadySolveProblem::NonFiniteInput);
    CHECK_THAT(solved.error().error.message, ContainsSubstring("not finite"));
    // The reduction refuses it ITSELF, before any kernel is asked: possession of a ReducedThermalSystem
    // proves its right-hand side is finite.
    const auto reduced = thermal::reduceThermalSystem(a.k, a.q, a.equations, a.constraints);
    REQUIRE_FALSE(reduced.has_value());
    CHECK(reduced.error().problem == SteadySolveProblem::NonFiniteInput);
    CHECK_THAT(reduced.error().error.message, ContainsSubstring("Qf - Kfc Tc overflowed"));
}

TEST_CASE("ThermalSolve_RejectsInvalidSettings", "[thermal][solve][invalid]") {
    const Algebra a = makeAlgebra({{4}}, {1240.0}, {});
    for (const ThermalSolverSettings& bad : {ThermalSolverSettings{.relativeResidualTolerance = 0.0, .pivotFloor = 1e-12},
                                              ThermalSolverSettings{.relativeResidualTolerance = -1.0, .pivotFloor = 1e-12},
                                              ThermalSolverSettings{.relativeResidualTolerance = kNaN, .pivotFloor = 1e-12},
                                              ThermalSolverSettings{.relativeResidualTolerance = 1e-9, .pivotFloor = 0.0},
                                              ThermalSolverSettings{.relativeResidualTolerance = 1e-9, .pivotFloor = kInf}}) {
        CHECK(problemOf(solveAlgebra(a, bad)) == std::optional{SteadySolveProblem::InvalidSettings});
    }
    CHECK(solveAlgebra(a, ThermalSolverSettings{}).has_value());
}

TEST_CASE("ThermalSolve_RejectsMismatchedSystemAndNumbering", "[thermal][solve][invalid]") {
    const Algebra a = makeAlgebra(chain5(), chain5Heat(), chain5Prescribed());
    const Algebra other = makeAlgebra({{2, -2}, {-2, 2}}, {0.0, 100.0}, {{0, 300.0}});
    // A 5-unknown system against a 2-unknown numbering.
    CHECK(problemOf(thermal::solveThermalAlgebra(a.k, a.q, other.equations, other.constraints, ThermalSolverSettings{})) ==
          std::optional{SteadySolveProblem::DofMapMismatch});
    // The right numbering with prescribed temperatures that are not its complement.
    const Algebra different = makeAlgebra(chain5(), chain5Heat(), {{1, 300.0}});
    CHECK(problemOf(thermal::solveThermalAlgebra(a.k, a.q, a.equations, different.constraints, ThermalSolverSettings{})) ==
          std::optional{SteadySolveProblem::ConstraintMismatch});
    // A heat vector of another length.
    const ThermalHeatVector shortQ = heatOf({1.0, 2.0});
    CHECK(problemOf(thermal::solveThermalAlgebra(a.k, shortQ, a.equations, a.constraints, ThermalSolverSettings{})) ==
          std::optional{SteadySolveProblem::DofMapMismatch});
}

// ===========================================================================
// Independent dense oracle
// ===========================================================================

TEST_CASE("ThermalSolve_MatchesIndependentDenseOracle", "[thermal][solve][dense]") {
    // Several deterministic SPD systems of increasing order with interleaved
    // prescribed unknowns, against the test's own elimination.
    double worstError = 0.0;
    for (const std::size_t n : {6u, 9u, 14u, 23u}) {
        const Dense2 k = spdMatrix(n, 0x9e3779b97f4a7c15ULL + n);
        std::vector<double> q(n);
        for (std::size_t i = 0; i < n; ++i) {
            q[i] = 10.0 * std::sin(0.7 * static_cast<double>(i)) + 3.0;
        }
        std::vector<std::pair<std::size_t, double>> prescribed;
        for (std::size_t i = 1; i < n; i += 3) {
            prescribed.emplace_back(i, 300.0 + 11.0 * static_cast<double>(i));
        }
        const Algebra a = makeAlgebra(k, q, prescribed);
        const ThermalAlgebraicSolution s = mustSolve(a);
        const std::vector<double> expected = denseExpected(k, q, prescribed);
        for (std::size_t i = 0; i < n; ++i) {
            worstError = std::max(worstError, std::abs(tempAt(s, i) - expected[i]) / std::max(1.0, std::abs(expected[i])));
        }
        for (const auto& [dof, kelvin] : prescribed) {
            CHECK(tempAt(s, dof) == kelvin);
        }
        CHECK(s.residual.normalized <= 1e-12);
    }
    CHECK(worstError <= 1e-11);
    WARN(std::format("dense oracle: SPD systems of order 6, 9, 14, 23 with every third unknown prescribed: max relative "
                     "temperature error {:.2e}",
                     worstError));
}

TEST_CASE("ThermalSolve_SystemScalingInvariant", "[thermal][solve][scaling]") {
    const Dense2 k = chain5();
    const std::vector<double> q = chain5Heat();
    const ThermalAlgebraicSolution reference = mustSolve(makeAlgebra(k, q, chain5Prescribed()));
    double worstT = 0.0;
    double lowest = 1.0;
    double highest = 0.0;
    for (const double c : {1.0e-6, 1.0e-3, 1.0, 1.0e3, 1.0e6}) {
        Dense2 scaledK = k;
        std::vector<double> scaledQ = q;
        for (auto& row : scaledK) {
            for (double& v : row) {
                v *= c;
            }
        }
        for (double& v : scaledQ) {
            v *= c;
        }
        const ThermalAlgebraicSolution s = mustSolve(makeAlgebra(scaledK, scaledQ, chain5Prescribed()));
        for (std::size_t i = 0; i < 5; ++i) {
            worstT = std::max(worstT, std::abs(tempAt(s, i) - tempAt(reference, i)));
        }
        CHECK(tempAt(s, 1) == 300.0);  // the prescribed values are not scaled
        CHECK(tempAt(s, 4) == 350.0);
        CHECK(s.residual.normalized <= 1e-9);
        lowest = std::min(lowest, s.residual.normalized);
        highest = std::max(highest, s.residual.normalized);
        // The absolute residual scales with c; the NORMALIZED one does not.
        CHECK(s.residual.heatNorm.si() > 0.0);
        CHECK_THAT(s.residual.heatNorm.si() / reference.residual.heatNorm.si(), WithinRel(c, 1e-12));
    }
    CHECK(worstT <= 1e-9);
    CHECK(highest <= 1e-12);
    WARN(std::format("K, Q scaled by 1e-6 ... 1e6: max |T - T_ref| = {:.2e} K; normalized residual in [{:.2e}, {:.2e}] "
                     "(an absolute-only tolerance would fail one end)",
                     worstT, lowest, highest));
}

TEST_CASE("ThermalSolve_ResidualToleranceIsScaleAware", "[thermal][solve][scaling]") {
    // The same RELATIVE error is judged the same at every scale: a perturbation of
    // 1e-6 relative to the solution is refused at c = 1e-6 and at c = 1e6 alike.
    for (const double c : {1.0e-6, 1.0, 1.0e6}) {
        Dense2 k = chain5();
        std::vector<double> q = chain5Heat();
        for (auto& row : k) {
            for (double& v : row) {
                v *= c;
            }
        }
        for (double& v : q) {
            v *= c;
        }
        const Algebra a = makeAlgebra(k, q, chain5Prescribed());
        const ThermalAlgebraicSolution s = mustSolve(a);
        std::vector<Temperature> off = s.temperatures;
        off[2] = Temperature::fromSi(off[2].si() * (1.0 + 1.0e-6));
        CHECK_FALSE(thermal::validateSteadySolution(a.k, a.q, a.equations, a.constraints, off, ThermalSolverSettings{}).has_value());
        std::vector<Temperature> close = s.temperatures;
        close[2] = Temperature::fromSi(close[2].si() * (1.0 + 1.0e-13));
        CHECK(thermal::validateSteadySolution(a.k, a.q, a.equations, a.constraints, close, ThermalSolverSettings{}).has_value());
    }
}

// ===========================================================================
// Purity, determinism
// ===========================================================================

TEST_CASE("ThermalSolve_SolveDoesNotMutateItsInputs", "[thermal][solve][purity]") {
    const Algebra a = makeAlgebra(chain5(), chain5Heat(), chain5Prescribed());
    const ThermalSparseMatrix kBefore = a.k;
    const ThermalHeatVector qBefore = a.q;
    const ThermalConstraintMap constraintsBefore = a.constraints;
    const ThermalFreeEquationMap equationsBefore = a.equations;
    for (int i = 0; i < 3; ++i) {
        (void)solveAlgebra(a);
    }
    (void)thermal::validateSteadySolution(a.k, a.q, a.equations, a.constraints, std::vector<Temperature>(5, Temperature::fromSi(300.0)),
                                           ThermalSolverSettings{});
    CHECK(a.k == kBefore);
    CHECK(a.q == qBefore);
    CHECK(a.constraints == constraintsBefore);
    CHECK(a.equations == equationsBefore);
}

TEST_CASE("ThermalSolve_DeterministicRepeat", "[thermal][solve][determinism]") {
    const Algebra a = makeAlgebra(chain5(), chain5Heat(), chain5Prescribed());
    const ThermalAlgebraicSolution first = mustSolve(a);
    int differences = 0;
    for (int i = 0; i < 40; ++i) {
        if (!(mustSolve(a) == first)) {
            ++differences;
        }
    }
    CHECK(differences == 0);

    // A failing model gives the same classification and message every time.
    const Algebra singular = makeAlgebra({{1, -1}, {-1, 1}}, {0.0, 0.0}, {});
    const auto one = solveAlgebra(singular);
    REQUIRE_FALSE(one.has_value());
    int diagnosticDifferences = 0;
    for (int i = 0; i < 20; ++i) {
        const auto again = solveAlgebra(singular);
        REQUIRE_FALSE(again.has_value());
        if (again.error().problem != one.error().problem || again.error().error.message != one.error().error.message) {
            ++diagnosticDifferences;
        }
    }
    CHECK(diagnosticDifferences == 0);

    // A larger sparse system, the same.
    const Dense2 k = spdMatrix(23, 7);
    std::vector<double> q(23, 1.0);
    const Algebra large = makeAlgebra(k, q, {{2, 310.0}, {11, 320.0}});
    const ThermalAlgebraicSolution lf = mustSolve(large);
    int largeDifferences = 0;
    for (int i = 0; i < 20; ++i) {
        if (!(mustSolve(large) == lf)) {
            ++largeDifferences;
        }
    }
    CHECK(largeDifferences == 0);
    WARN(std::format("determinism: 40 repeats of the interleaved case, 20 of a 23-unknown system and 20 of a singular model: "
                     "{} + {} bitwise differences in T, residual, full residual and pivot ratio; {} in the diagnostic",
                     differences, largeDifferences, diagnosticDifferences));
}

TEST_CASE("ThermalSolve_BitFingerprintForCrossPresetComparison", "[thermal][solve][determinism]") {
    const auto hashOf = [](const ThermalAlgebraicSolution& s) {
        std::uint64_t hash = 14695981039346656037ULL;
        const auto mix = [&hash](double value) {
            const auto bits = std::bit_cast<std::uint64_t>(value);
            for (int i = 0; i < 8; ++i) {
                hash ^= (bits >> (8 * i)) & 0xFFU;
                hash *= 1099511628211ULL;
            }
        };
        for (const Temperature t : s.temperatures) {
            mix(t.si());
        }
        for (const double r : s.fullResidual) {
            mix(r);
        }
        mix(s.residual.normalized);
        mix(s.pivotRatio);
        return hash;
    };
    const ThermalAlgebraicSolution chain = mustSolve(makeAlgebra(chain5(), chain5Heat(), chain5Prescribed()));
    const Dense2 k = spdMatrix(23, 7);
    const ThermalAlgebraicSolution large = mustSolve(makeAlgebra(k, std::vector<double>(23, 1.0), {{2, 310.0}, {11, 320.0}}));
    CHECK(hashOf(chain) == hashOf(mustSolve(makeAlgebra(chain5(), chain5Heat(), chain5Prescribed()))));
    WARN(std::format("bit fingerprint, interleaved 5-unknown case: {:016x}", hashOf(chain)));
    WARN(std::format("bit fingerprint, 23-unknown SPD case: {:016x}", hashOf(large)));
}

// ===========================================================================
// A finite-element model through the assembled system: hand mesh, no document
// ===========================================================================

namespace {

constexpr double kGridK = 45.0;

[[nodiscard]] ThermalSystem steadySystemOf(const AsmHand& hand, double k, const std::vector<Power>& heat,
                                           const std::vector<thermal::PreparedConvectionBoundary>& convections = {}) {
    return assemble(hand, k, std::nullopt, heat, convections);
}

[[nodiscard]] ThermalConstraintMap constraintsOn(const AsmHand& hand, const std::vector<std::pair<std::size_t, double>>& prescribed) {
    std::vector<ThermalTemperatureConstraint> list;
    for (const auto& [dof, kelvin] : prescribed) {
        list.push_back(ThermalTemperatureConstraint{dofIx(dof), Temperature::fromSi(kelvin)});
    }
    auto built = thermal::buildThermalConstraintMap(hand.dofs, list);
    REQUIRE(built.has_value());
    return std::move(*built);
}

} // namespace

TEST_CASE("ThermalSolve_SingleTetThroughAssembly", "[thermal][solve][fe]") {
    // The unit tetrahedron, k = 6: Ke = G^T G. Corner 0 at 400 K and corners 1, 2, 3 at 300 K
    // (three prescribed): the free corner... all but one prescribed. Prescribe 0, 1, 2:
    // free corner 3 has Kff = [1], Kfc = [-1 0 0] (column of node 0 is 0), beff = 0 + 0 +...
    // Hand: K(3,0) = -1, K(3,1) = 0, K(3,2) = 0, K(3,3) = 1, so T3 = T0 = 400 K.
    const AsmHand hand = makeAsmHand(unitTetNodes(), {}, {{0, 1, 2, 3}});
    const ThermalSystem system = steadySystemOf(hand, 6.0, noHeat(hand));
    const ThermalConstraintMap constraints = constraintsOn(hand, {{0, 400.0}, {1, 300.0}, {2, 300.0}});
    const auto solved = thermal::solveSteadyThermalSystem(system, hand.dofs, constraints, ThermalSolverSettings{});
    INFO((solved.has_value() ? std::string{} : solved.error().error.message));
    REQUIRE(solved.has_value());
    CHECK_THAT(solved->temperatures()[3].si(), WithinAbs(400.0, 1e-10));
    CHECK(solved->freeEquations() == 1);
    CHECK(solved->constrainedCount() == 3);
    CHECK(solved->kernelInvoked());
}

TEST_CASE("ThermalSolve_LinearProfileOnAStructuredGrid", "[thermal][solve][fe][affine]") {
    // 1-D conduction through a grid: T = 300 K on the x = 0 face, 400 K on the x = L face,
    // every other face adiabatic. Linear tetrahedra reproduce an affine field exactly, so
    // EVERY node's temperature is 300 + 100 x/L.
    constexpr std::size_t kSide = 6;
    const AsmHand hand = makeKuhnGrid(kSide, 0.01);
    const ThermalSystem system = steadySystemOf(hand, kGridK, noHeat(hand));
    std::vector<std::pair<std::size_t, double>> prescribed;
    for (std::size_t k = 0; k < kSide; ++k) {
        for (std::size_t j = 0; j < kSide; ++j) {
            prescribed.emplace_back((k * kSide + j) * kSide + 0, 300.0);
            prescribed.emplace_back((k * kSide + j) * kSide + (kSide - 1), 400.0);
        }
    }
    std::sort(prescribed.begin(), prescribed.end());
    const ThermalConstraintMap constraints = constraintsOn(hand, prescribed);
    const auto solved = thermal::solveSteadyThermalSystem(system, hand.dofs, constraints, ThermalSolverSettings{});
    INFO((solved.has_value() ? std::string{} : solved.error().error.message));
    REQUIRE(solved.has_value());
    double worst = 0.0;
    for (std::size_t k = 0; k < kSide; ++k) {
        for (std::size_t j = 0; j < kSide; ++j) {
            for (std::size_t i = 0; i < kSide; ++i) {
                const std::size_t dof = (k * kSide + j) * kSide + i;
                const double expected = 300.0 + 100.0 * static_cast<double>(i) / static_cast<double>(kSide - 1);
                worst = std::max(worst, std::abs(solved->temperatures()[dof].si() - expected));
            }
        }
    }
    CHECK(worst <= 1e-6);
    CHECK(solved->residual().normalized <= 1e-9);
    WARN(std::format("structured grid {}^3 ({} unknowns, {} free): linear profile 300 -> 400 K, max |T - exact| = {:.3e} K, "
                     "normalized residual {:.2e}",
                     kSide, hand.dofs.dofCount(), solved->freeEquations(), worst, solved->residual().normalized));
}

TEST_CASE("ThermalSolve_AffineFieldPatch", "[thermal][solve][fe][affine]") {
    // An affine field a + b x + c y + d z prescribed on EVERY boundary node of a small grid
    // is reproduced at every interior node: the patch test, through the solve.
    constexpr std::size_t kSide = 5;
    constexpr double h = 0.01;
    const double a = 310.0;
    const double bx = 2000.0;
    const double cy = -1500.0;
    const double dz = 800.0;
    const AsmHand hand = makeKuhnGrid(kSide, h);
    const ThermalSystem system = steadySystemOf(hand, kGridK, noHeat(hand));
    const auto exact = [&](std::size_t i, std::size_t j, std::size_t k) {
        return a + bx * h * static_cast<double>(i) + cy * h * static_cast<double>(j) + dz * h * static_cast<double>(k);
    };
    std::vector<std::pair<std::size_t, double>> prescribed;
    std::size_t interior = 0;
    for (std::size_t k = 0; k < kSide; ++k) {
        for (std::size_t j = 0; j < kSide; ++j) {
            for (std::size_t i = 0; i < kSide; ++i) {
                const bool boundary = i == 0 || j == 0 || k == 0 || i == kSide - 1 || j == kSide - 1 || k == kSide - 1;
                if (boundary) {
                    prescribed.emplace_back((k * kSide + j) * kSide + i, exact(i, j, k));
                } else {
                    ++interior;
                }
            }
        }
    }
    const ThermalConstraintMap constraints = constraintsOn(hand, prescribed);
    const auto solved = thermal::solveSteadyThermalSystem(system, hand.dofs, constraints, ThermalSolverSettings{});
    REQUIRE(solved.has_value());
    REQUIRE(solved->freeEquations() == interior);
    double worst = 0.0;
    for (std::size_t k = 1; k + 1 < kSide; ++k) {
        for (std::size_t j = 1; j + 1 < kSide; ++j) {
            for (std::size_t i = 1; i + 1 < kSide; ++i) {
                worst = std::max(worst, std::abs(solved->temperatures()[(k * kSide + j) * kSide + i].si() - exact(i, j, k)));
            }
        }
    }
    CHECK(worst <= 1e-6);
    WARN(std::format("affine patch {}^3: {} interior unknowns, max |T - (a + bx + cy + dz)| = {:.3e} K", kSide, interior, worst));
}

TEST_CASE("ThermalSolve_ConvectionAnchorsAFiniteElementModel", "[thermal][solve][fe][robin]") {
    // One tetrahedron, NOTHING prescribed, a convection face. K lifts the constant mode, so the
    // model is solvable with no Dirichlet datum. 12 W generation, h = 4 W/(m^2 K), T_inf = 300 K.
    const AsmHand hand = makeAsmHand(unitTetNodes(), {{0, 1, 2}}, {{0, 1, 2, 3}});
    const std::vector<thermal::PreparedConvectionBoundary> convection{convectionOver(hand, 1, hand.triangles, 4.0, 300.0)};
    const ThermalSystem system = steadySystemOf(hand, 6.0, generationHeat(hand, 24.0), convection);
    const ThermalConstraintMap none = constraintsOn(hand, {});
    const auto solved = thermal::solveSteadyThermalSystem(system, hand.dofs, none, ThermalSolverSettings{});
    INFO((solved.has_value() ? std::string{} : solved.error().error.message));
    REQUIRE(solved.has_value());
    CHECK(solved->anchoring().unanchored == 0);
    // The heat that generation supplies (q''' V = 24/6 = 4 W) leaves through the convection
    // face: sum over the face nodes of h-weighted (T - T_inf). The ENERGY check below is the
    // test's own arithmetic from the solved temperatures and the hand face area 1/2.
    const double faceArea = 0.5;
    // Consistent Kconv rows applied to T minus Qconv, summed over all three face nodes, equal
    // h A (T_avg - T_inf) with T_avg the mean of the three face temperatures.
    const double mean = (solved->temperatures()[0].si() + solved->temperatures()[1].si() + solved->temperatures()[2].si()) / 3.0;
    CHECK_THAT(4.0 * faceArea * (mean - 300.0), WithinAbs(4.0, 1e-9));

    // The same face with h = 0 anchors nothing: the model is singular and says so.
    const std::vector<thermal::PreparedConvectionBoundary> zero{convectionOver(hand, 1, hand.triangles, 0.0, 300.0)};
    const ThermalSystem floating = steadySystemOf(hand, 6.0, generationHeat(hand, 24.0), zero);
    const auto refused = thermal::solveSteadyThermalSystem(floating, hand.dofs, none, ThermalSolverSettings{});
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().problem == SteadySolveProblem::MissingTemperatureDatum);
}

TEST_CASE("ThermalSolve_SparseNodeIdsThroughTheSolve", "[thermal][solve][sparse]") {
    // Handles 7, 42, 104, 9001, 500000: the unknowns are their RANKS, the matrices are 5 x 5, and
    // the temperature of a node is found through the numbering, never by the handle.
    const Nodes sparse{{7, at(0, 0, 0)}, {42, at(1, 0, 0)}, {104, at(0, 1, 0)}, {9001, at(0, 0, 1)}, {500000, at(1, 1, 1)}};
    const AsmHand hand = makeAsmHand(sparse, {}, {{0, 1, 2, 3}, {1, 2, 3, 4}});
    const ThermalSystem system = steadySystemOf(hand, 6.0, noHeat(hand));
    const ThermalConstraintMap constraints = constraintsOn(hand, {{0, 300.0}, {4, 400.0}});
    const auto solved = thermal::solveSteadyThermalSystem(system, hand.dofs, constraints, ThermalSolverSettings{});
    REQUIRE(solved.has_value());
    REQUIRE(solved->dofCount() == 5);
    const Dense2 dense = [&] {
        Dense2 d(5, std::vector<double>(5, 0.0));
        for (std::size_t r = 0; r < 5; ++r) {
            for (std::size_t c = 0; c < 5; ++c) {
                d[r][c] = system.conductance().coeffSi(r, c);
            }
        }
        return d;
    }();
    const std::vector<double> expected = denseExpected(dense, std::vector<double>(5, 0.0), {{0, 300.0}, {4, 400.0}});
    const NodeId::ValueType handles[5] = {7, 42, 104, 9001, 500000};
    for (std::size_t i = 0; i < 5; ++i) {
        const auto t = solved->temperatureOf(hand.dofs, NodeId::fromValue(handles[i]));
        REQUIRE(t.has_value());
        CHECK_THAT(t->si(), WithinAbs(expected[i], 1e-9));
    }
    CHECK_FALSE(solved->temperatureOf(hand.dofs, NodeId::fromValue(3)).has_value());
    CHECK_FALSE(solved->temperatureOf(dofIx(5)).has_value());
}

TEST_CASE("ThermalSolve_SeamRefusals", "[thermal][solve][invalid]") {
    const AsmHand hand = makeTwoTet();
    const ThermalSystem steady = steadySystemOf(hand, 6.0, noHeat(hand));
    const ThermalConstraintMap constraints = constraintsOn(hand, {{0, 300.0}});
    // A transient system is not a steady one.
    const ThermalSystem transient = assemble(hand, 6.0, capacityOf(120.0, 1.0), noHeat(hand));
    const auto notSteady = thermal::solveSteadyThermalSystem(transient, hand.dofs, constraints, ThermalSolverSettings{});
    REQUIRE_FALSE(notSteady.has_value());
    CHECK(notSteady.error().problem == SteadySolveProblem::NotSteadySystem);
    // A numbering of another mesh.
    const AsmHand grid = makeKuhnGrid(3, 1.0);
    const ThermalConstraintMap foreignConstraints = constraintsOn(grid, {{0, 300.0}});
    const auto foreignMap = thermal::solveSteadyThermalSystem(steady, grid.dofs, foreignConstraints, ThermalSolverSettings{});
    REQUIRE_FALSE(foreignMap.has_value());
    CHECK(foreignMap.error().problem == SteadySolveProblem::DofMapMismatch);
    // Constraints of another mesh against the right numbering.
    const auto foreignConstraint = thermal::solveSteadyThermalSystem(steady, hand.dofs, foreignConstraints, ThermalSolverSettings{});
    REQUIRE_FALSE(foreignConstraint.has_value());
    CHECK(foreignConstraint.error().problem == SteadySolveProblem::ConstraintMismatch);
    // The correct trio solves.
    CHECK(thermal::solveSteadyThermalSystem(steady, hand.dofs, constraints, ThermalSolverSettings{}).has_value());
    // Invalid settings are refused first.
    CHECK(thermal::solveSteadyThermalSystem(steady, hand.dofs, constraints, ThermalSolverSettings{.relativeResidualTolerance = 0.0}).error().problem ==
          SteadySolveProblem::InvalidSettings);
}

TEST_CASE("ThermalSolve_EveryProblemHasAName", "[thermal][solve][invalid]") {
    for (const auto p : {SteadySolveProblem::InvalidSettings, SteadySolveProblem::NotSteadySystem, SteadySolveProblem::DofMapMismatch,
                         SteadySolveProblem::ConstraintMismatch, SteadySolveProblem::StaleSystem, SteadySolveProblem::StaleBoundary,
                         SteadySolveProblem::NonFiniteInput, SteadySolveProblem::MissingTemperatureDatum,
                         SteadySolveProblem::UnanchoredThermalRegion, SteadySolveProblem::SingularSystem,
                         SteadySolveProblem::FactorizationFailure, SteadySolveProblem::NonFiniteSolution,
                         SteadySolveProblem::NonFiniteResidual, SteadySolveProblem::ResidualTooLarge,
                         SteadySolveProblem::ConstraintNotPreserved}) {
        CHECK(thermal::toString(p) != "unknown");
    }
}

TEST_CASE("ThermalSolve_LargeSparseSolveSmoke", "[thermal][solve][sparse][smoke]") {
    // A grid large enough that an accidental dense reduced matrix would be visible: 18^3 =
    // 5832 unknowns, T = 300 K on x = 0, 400 K on x = L, a uniform 1e5 W/m^3 generation
    // is NOT used (the linear profile is the exact check). Nothing is benchmarked.
    constexpr std::size_t kSide = 18;
    const AsmHand hand = makeKuhnGrid(kSide, 0.01);
    const ThermalSystem system = steadySystemOf(hand, kGridK, noHeat(hand));
    std::vector<std::pair<std::size_t, double>> prescribed;
    for (std::size_t k = 0; k < kSide; ++k) {
        for (std::size_t j = 0; j < kSide; ++j) {
            prescribed.emplace_back((k * kSide + j) * kSide, 300.0);
            prescribed.emplace_back((k * kSide + j) * kSide + (kSide - 1), 400.0);
        }
    }
    std::sort(prescribed.begin(), prescribed.end());
    const ThermalConstraintMap constraints = constraintsOn(hand, prescribed);
    const auto begin = std::chrono::steady_clock::now();
    const auto solved = thermal::solveSteadyThermalSystem(system, hand.dofs, constraints, ThermalSolverSettings{});
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
    INFO((solved.has_value() ? std::string{} : solved.error().error.message));
    REQUIRE(solved.has_value());
    const std::size_t n = static_cast<std::size_t>(hand.dofs.dofCount());
    double worst = 0.0;
    bool allFinite = true;
    for (std::size_t k = 0; k < kSide; ++k) {
        for (std::size_t j = 0; j < kSide; ++j) {
            for (std::size_t i = 0; i < kSide; ++i) {
                const double t = solved->temperatures()[(k * kSide + j) * kSide + i].si();
                allFinite = allFinite && std::isfinite(t);
                worst = std::max(worst, std::abs(t - (300.0 + 100.0 * static_cast<double>(i) / static_cast<double>(kSide - 1))));
            }
        }
    }
    CHECK(allFinite);
    CHECK(worst <= 1e-5);
    CHECK(solved->residual().normalized <= 1e-9);
    CHECK(solved->freeSystemNonZeros() < solved->freeEquations() * solved->freeEquations() / 10);
    WARN(std::format("sparse solve smoke: Ndof {}, Nfree {}, Nconstrained {}, nnz(K) {}, nnz(Kff) {}, Nfree^2 {}, "
                     "pivot ratio {:.3e}, normalized residual {:.2e}, max |T - exact| {:.2e} K, {:.2f} s",
                     n, solved->freeEquations(), solved->constrainedCount(), system.conductance().nonZeros(),
                     solved->freeSystemNonZeros(), solved->freeEquations() * solved->freeEquations(), solved->pivotRatio(),
                     solved->residual().normalized, worst, seconds));
}

TEST_CASE("ThermalSolve_StaleReasonsNameEachFieldAlone", "[thermal][solve][currentness]") {
    using thermal::SolutionStaleReason;
    const FaceName face{ObjectId::fromValue(3), FaceSelector{.role = FaceRole::EndCap}};
    const thermal::ThermalBoundaryCondition held{ThermalBoundaryConditionId::fromValue(1),
                                                 thermal::PrescribedTemperature{.face = face, .value = Temperature::fromSi(350.0)}};
    const thermal::ThermalBoundaryCondition other{ThermalBoundaryConditionId::fromValue(1),
                                                  thermal::PrescribedTemperature{.face = face, .value = Temperature::fromSi(351.0)}};
    thermal::SteadySolutionSource base;
    base.system.body = ObjectId::fromValue(3);
    base.system.mesh = meshing::MeshStamp{meshing::MeshId::fromValue(7), 1};
    base.system.nodeCount = 9;
    base.system.geometry = meshing::GeometryRevision{4};
    base.system.mode = thermal::ThermalAnalysisMode::Steady;
    base.system.material = MaterialId::fromValue(5);
    base.system.materialRevision = 6;
    base.temperatures = {held};
    base.settings = ThermalSolverSettings{};

    CHECK(thermal::staleReasons(base, base).empty());
    const auto only = [&](const auto& edit) {
        thermal::SteadySolutionSource changed = base;
        edit(changed);
        return thermal::staleReasons(base, changed);
    };
    using Reasons = std::vector<SolutionStaleReason>;
    CHECK(only([&](auto& s) { s.temperatures = {other}; }) == Reasons{SolutionStaleReason::PrescribedTemperatures});
    CHECK(only([&](auto& s) { s.temperatures.clear(); }) == Reasons{SolutionStaleReason::PrescribedTemperatures});
    CHECK(only([](auto& s) { s.settings.relativeResidualTolerance = 1e-6; }) == Reasons{SolutionStaleReason::Settings});
    CHECK(only([](auto& s) { s.settings.pivotFloor = 1e-9; }) == Reasons{SolutionStaleReason::Settings});
    CHECK(only([](auto& s) { s.system.mesh.generation = 2; }) == Reasons{SolutionStaleReason::Mesh});
    CHECK(only([](auto& s) { s.system.nodeCount = 10; }) == Reasons{SolutionStaleReason::Mesh});
    CHECK(only([](auto& s) { s.system.body = ObjectId::fromValue(4); }) == Reasons{SolutionStaleReason::Body});
    CHECK(only([](auto& s) { s.system.geometry = meshing::GeometryRevision{5}; }) == Reasons{SolutionStaleReason::Geometry});
    CHECK(only([](auto& s) { s.system.materialRevision = 7; }) == Reasons{SolutionStaleReason::Material});
    CHECK(only([](auto& s) { s.system.mode = thermal::ThermalAnalysisMode::Transient; }) == Reasons{SolutionStaleReason::Mode});
}
