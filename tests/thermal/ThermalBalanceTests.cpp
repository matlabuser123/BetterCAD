// P18-BALANCE-001: the heat accounting, hand-checked.
//
// WHAT THESE TESTS ARE FOR. The accounting is a RESIDUAL (`K T - Q`, or its backward-Euler form), the
// reading of its prescribed components as REACTIONS, a SUM of loads taken from the qualified
// attribution, the heat each convection face EXCHANGES, and the energy a step STORES. The claims are
// about each of those and about the sign that ties them together:
//
//   - the reaction sign is derived from a conductance network whose heat flow is obvious (a hot
//     prescribed end supplies heat, a cold one removes it), not guessed;
//   - the reaction comes from the equation, never from "whatever makes the balance close";
//   - the convection is `Kconv T - Qconv`, so the ambient source alone is not a transfer;
//   - a transient reaction includes the storage term, which with a consistent capacity couples a
//     prescribed node to its free neighbours, and the stored energy is `1^T C dT` over EVERY unknown;
//   - the step size is the actual one; a shortened last step is not the nominal one.
//
// EVERY EXPECTED VALUE IS TYPED IN FROM A HAND DERIVATION or comes from the test's own dense
// arithmetic (ThermalBalanceTestSupport.hpp, ThermalTransientTestSupport.hpp). The real-mesh and
// currentness claims are in ThermalBalanceMappedTests.cpp.

#include "ThermalBalanceTestSupport.hpp"

#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <bit>
#include <format>
#include <limits>

using namespace batest;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

/// `g [1 -1; -1 1]`: two nodes joined by a conductance of @p g W/K.
[[nodiscard]] Dense2 pair(double g) { return {{g, -g}, {-g, g}}; }

/// `n` nodes in a chain, each neighbouring pair joined by @p g W/K.
[[nodiscard]] Dense2 chain(std::size_t n, double g) {
    Dense2 k(n, std::vector<double>(n, 0.0));
    for (std::size_t i = 0; i + 1 < n; ++i) {
        k[i][i] += g;
        k[i + 1][i + 1] += g;
        k[i][i + 1] -= g;
        k[i + 1][i] -= g;
    }
    return k;
}

[[nodiscard]] Dense2 scaledBy(const Dense2& a, double s) {
    Dense2 out = a;
    for (auto& row : out) {
        for (double& v : row) {
            v *= s;
        }
    }
    return out;
}

[[nodiscard]] Dense2 consistentTriangleCapacity(double total) {
    // The 3-node consistent mass pattern [2 1 1; 1 2 1; 1 1 2] scaled so the entries sum to @p total.
    return scaledBy({{2, 1, 1}, {1, 2, 1}, {1, 1, 2}}, total / 12.0);
}

} // namespace

// ===========================================================================
// The sign convention
// ===========================================================================

TEST_CASE("ThermalBalance_SignConvention", "[thermal][balance][sign]") {
    // g = 2 W/K between node 0 (prescribed 300 K) and node 1 (free). Node 1 receives +100 W through a surface
    // load and loses 20 W to a sink (negative generation), so Q1 = +80 W and T1 = 300 + 80/2 = 340 K.
    //   K T = [2*300 - 2*340, -2*300 + 2*340] = [-80, +80]    r = K T - Q = [-80, 0]
    // so node 0's constraint REMOVES 80 W: a reaction of -80 W into the body.
    const Net net = makeNet(pair(2.0), {0.0, 80.0}, {{0, 300.0}});
    const std::vector<ThermalLoadContribution> loads{surfaceContribution(1, 100.0), generationContribution(2, -20.0)};
    const ThermalAlgebraicSolution solved = mustSolve(net.base);
    CHECK_THAT(tempAt(solved, 1), WithinAbs(340.0, 1e-12));

    const ThermalHeatBalance b = mustBalance(thermal::steadyHeatBalance(algebraOf(net, loads), solved.temperatures));
    CHECK_THAT(b.terms.appliedSurfaceHeat.si(), WithinAbs(100.0, 1e-12));
    CHECK_THAT(b.terms.generatedHeat.si(), WithinAbs(-20.0, 1e-12));      // a sink stays negative: no abs
    CHECK_THAT(b.terms.convectionHeat.si(), WithinAbs(0.0, 1e-12));
    CHECK_THAT(b.terms.prescribedTemperatureReaction.si(), WithinAbs(-80.0, 1e-9));
    CHECK_FALSE(b.terms.storedEnergyRate.has_value());                     // steady: no such term, not zero
    CHECK_THAT(b.residual().si(), WithinAbs(0.0, 1e-9));
    CHECK(b.status == BalanceStatus::Balanced);

    // The one formula: the residual is P18-DATA-001's `netImbalance`, and the five terms are its own.
    CHECK(b.residual() == b.terms.netImbalance());
    CHECK(b.terms.netImbalance().si() ==
          (b.terms.appliedSurfaceHeat + b.terms.generatedHeat + b.terms.convectionHeat + b.terms.prescribedTemperatureReaction).si());
    WARN("sign convention: positive = heat INTO the body; q'' = +100 W, q''' = -20 W (sink), reaction = -80 W; residual 0");
}

TEST_CASE("ThermalBalance_ReactionSignAnalytical", "[thermal][balance][sign][reaction]") {
    // THE ORACLE THAT FREEZES THE REACTION SIGN. Two nodes, g = 2 W/K, both prescribed: 400 K and 300 K.
    //   K = [ 2 -2; -2  2]   Q = [0, 0]   T = [400, 300]
    //   K T = [2*400 - 2*300, -2*400 + 2*300] = [+200, -200] W     r = K T - Q = [+200, -200]
    // Heat flows from the hot node to the cold one at g (400 - 300) = 200 W. The hot constraint must SUPPLY it
    // (+200 into the body) and the cold constraint must ABSORB it (-200). R = +r reproduces that; R = -r would
    // put heat into the cold wall.
    const Net net = makeNet(pair(2.0), {0.0, 0.0}, {{0, 400.0}, {1, 300.0}});
    const std::vector<Temperature> t = kelvin({400.0, 300.0});

    const auto residual = thermal::steadyFullResidual(net.k, net.base.q, t);
    REQUIRE(residual.has_value());
    CHECK_THAT((*residual)[0], WithinAbs(200.0, 1e-12));
    CHECK_THAT((*residual)[1], WithinAbs(-200.0, 1e-12));

    const ThermalHeatBalance b = mustBalance(thermal::steadyHeatBalance(algebraOf(net), t));
    REQUIRE(b.reactions.size() == 2);
    CHECK(b.reactions[0].dof.value() == 0);
    CHECK(b.reactions[1].dof.value() == 1);
    CHECK_THAT(reactionOf(b, 0), WithinAbs(200.0, 1e-12));    // the hot end supplies heat
    CHECK_THAT(reactionOf(b, 1), WithinAbs(-200.0, 1e-12));   // the cold end removes it
    CHECK(reactionOf(b, 0) > 0.0);
    CHECK(reactionOf(b, 1) < 0.0);
    CHECK_THAT(b.terms.prescribedTemperatureReaction.si(), WithinAbs(0.0, 1e-12));
    CHECK_THAT(b.residual().si(), WithinAbs(0.0, 1e-12));
    CHECK(b.status == BalanceStatus::Balanced);
    // The flipped sign is a different, wrong, statement of the same numbers.
    CHECK(-(*residual)[0] < 0.0);
    WARN("reaction sign: g = 2 W/K, 400 K / 300 K: K T = [+200, -200] W; reaction INTO the body = +r = [+200, -200] W; total 0");
}

TEST_CASE("ThermalBalance_ConvectionSignAboveAmbient", "[thermal][balance][sign][convection]") {
    // h A = 8 * 0.5 = 4 W/K, T_inf = 300 K, a uniform face at 310 K: the body is hotter, so it LOSES h A (T - Tinf) = 40 W.
    const PreparedConvectionBoundary face = oneTriangle(1, 0.5, 8.0, 300.0);
    const auto exchanged = thermal::convectionHeatOf(face, kelvin({310.0, 310.0, 310.0}));
    REQUIRE(exchanged.has_value());
    CHECK_THAT(exchanged->heatOut.si(), WithinAbs(40.0, 1e-12));
    CHECK_THAT(exchanged->heatInto.si(), WithinAbs(-40.0, 1e-12));   // negative INTO the body
    CHECK_THAT(exchanged->area.si(), WithinAbs(0.5, 1e-15));
    CHECK(exchanged->heatInto.si() == -exchanged->heatOut.si());
}

TEST_CASE("ThermalBalance_ConvectionSignBelowAmbient", "[thermal][balance][sign][convection]") {
    const PreparedConvectionBoundary face = oneTriangle(1, 0.5, 8.0, 300.0);
    const auto exchanged = thermal::convectionHeatOf(face, kelvin({290.0, 290.0, 290.0}));
    REQUIRE(exchanged.has_value());
    CHECK_THAT(exchanged->heatOut.si(), WithinAbs(-40.0, 1e-12));
    CHECK_THAT(exchanged->heatInto.si(), WithinAbs(40.0, 1e-12));    // a colder body GAINS heat
    // At the ambient there is no exchange, although the ambient SOURCE (h Tinf A = 1200 W) is not zero.
    const auto none = thermal::convectionHeatOf(face, kelvin({300.0, 300.0, 300.0}));
    REQUIRE(none.has_value());
    CHECK_THAT(none->heatOut.si(), WithinAbs(0.0, 1e-9));
    CHECK_THAT(none->heatInto.si(), WithinAbs(0.0, 1e-9));
    CHECK_THAT(face.source().si(), WithinAbs(1200.0, 1e-9));
}

TEST_CASE("ThermalBalance_ConvectionHeatTransferIsKconvTMinusQconvAndNotTheSource", "[thermal][balance][convection]") {
    // Non-uniform face temperatures [310, 320, 330]: sum(Kconv T - Qconv) = (hA/3) sum(T) - hA Tinf = hA (mean(T) - Tinf) = 4 * 20 = 80 W.
    const PreparedConvectionBoundary face = oneTriangle(1, 0.5, 8.0, 300.0);
    const auto exchanged = thermal::convectionHeatOf(face, kelvin({310.0, 320.0, 330.0}));
    REQUIRE(exchanged.has_value());
    CHECK_THAT(exchanged->heatOut.si(), WithinAbs(80.0, 1e-10));
    // The ambient source alone (what the global Q contains) is 1200 W: it is not the heat transferred.
    CHECK(std::abs(face.source().si() - exchanged->heatOut.si()) > 1000.0);
    // The same number by the hand-written dense Kconv T - Qconv of the three nodes.
    const Dense2 kc = denseKconv(face);
    const std::vector<double> qc = denseQconv(face);
    const std::vector<double> t{310.0, 320.0, 330.0};
    double dense = 0.0;
    for (std::size_t i = 0; i < 3; ++i) {
        double row = 0.0;
        for (std::size_t j = 0; j < 3; ++j) {
            row += kc[i][j] * t[j];
        }
        dense += row - qc[i];
    }
    CHECK_THAT(dense, WithinAbs(80.0, 1e-10));
    CHECK_THAT(exchanged->heatOut.si(), WithinAbs(dense, 1e-10));
}

// ===========================================================================
// Steady reactions
// ===========================================================================

TEST_CASE("ThermalBalance_SteadyDirichletReaction", "[thermal][balance][reaction][steady]") {
    // Three nodes in a chain of g = 2 W/K, the ends prescribed (500 K, 380 K), the middle free. The heat that
    // flows end to end is the series conductance (1 W/K) times 120 K = 120 W.
    const Net net = makeNet(chain(3, 2.0), {0.0, 0.0, 0.0}, {{0, 500.0}, {2, 380.0}});
    const ThermalAlgebraicSolution solved = mustSolve(net.base);
    CHECK_THAT(tempAt(solved, 1), WithinAbs(440.0, 1e-9));
    const ThermalHeatBalance b = mustBalance(thermal::steadyHeatBalance(algebraOf(net), solved.temperatures));
    REQUIRE(b.reactions.size() == 2);
    CHECK_THAT(reactionOf(b, 0), WithinAbs(120.0, 1e-9));
    CHECK_THAT(reactionOf(b, 2), WithinAbs(-120.0, 1e-9));
    CHECK_THAT(b.terms.prescribedTemperatureReaction.si(), WithinAbs(0.0, 1e-9));
    CHECK(b.status == BalanceStatus::Balanced);
}

TEST_CASE("ThermalBalance_TwoTemperatureReactionsEqualOpposite", "[thermal][balance][reaction][multiple]") {
    // Two prescribed boundaries on a six-node bar of g = 5 W/K links: 400 K at one end, 300 K at the other. Series
    // conductance 1 W/K, so 100 W. The reactions are equal and opposite and the unique unknowns are the two ends.
    const Net net = makeNet(chain(6, 5.0), std::vector<double>(6, 0.0), {{0, 400.0}, {5, 300.0}});
    const ThermalAlgebraicSolution solved = mustSolve(net.base);
    const ThermalHeatBalance b = mustBalance(thermal::steadyHeatBalance(algebraOf(net), solved.temperatures));
    REQUIRE(b.reactions.size() == 2);
    CHECK_THAT(reactionOf(b, 0), WithinAbs(100.0, 1e-8));
    CHECK_THAT(reactionOf(b, 5), WithinAbs(-100.0, 1e-8));
    CHECK_THAT(reactionOf(b, 0) + reactionOf(b, 5), WithinAbs(0.0, 1e-8));
    // The hot side supplies, the cold side removes: the sign follows the energy, not an orientation.
    CHECK(reactionOf(b, 0) > 0.0);
    CHECK(reactionOf(b, 5) < 0.0);
    // Reversed temperatures reverse both signs.
    const Net reversed = makeNet(chain(6, 5.0), std::vector<double>(6, 0.0), {{0, 300.0}, {5, 400.0}});
    const ThermalHeatBalance r = mustBalance(thermal::steadyHeatBalance(algebraOf(reversed), mustSolve(reversed.base).temperatures));
    CHECK_THAT(reactionOf(r, 0), WithinAbs(-100.0, 1e-8));
    CHECK_THAT(reactionOf(r, 5), WithinAbs(100.0, 1e-8));
}

TEST_CASE("ThermalBalance_AppliedHeatBalancedByDirichletReaction", "[thermal][balance][reaction][applied]") {
    // g = 2 W/K, T0 = 300 K prescribed, P = 100 W applied at the free node: T1 = 300 + 100/2 = 350 K.
    //   K T = [2*300 - 2*350, -2*300 + 2*350] = [-100, +100]   Q = [0, 100]   r = [-100, 0]
    // The 300 K boundary must remove the 100 W the surface load puts in.
    const Net net = makeNet(pair(2.0), {0.0, 100.0}, {{0, 300.0}});
    const std::vector<ThermalLoadContribution> loads{surfaceContribution(1, 100.0)};
    const ThermalAlgebraicSolution solved = mustSolve(net.base);
    CHECK_THAT(tempAt(solved, 1), WithinAbs(350.0, 1e-12));
    const ThermalHeatBalance b = mustBalance(thermal::steadyHeatBalance(algebraOf(net, loads), solved.temperatures));
    CHECK_THAT(b.terms.appliedSurfaceHeat.si(), WithinAbs(100.0, 1e-12));
    CHECK_THAT(b.terms.prescribedTemperatureReaction.si(), WithinAbs(-100.0, 1e-9));
    CHECK_THAT(b.terms.generatedHeat.si(), WithinAbs(0.0, 1e-15));
    CHECK_THAT(b.terms.convectionHeat.si(), WithinAbs(0.0, 1e-15));
    CHECK_THAT(b.residual().si(), WithinAbs(0.0, 1e-9));
    REQUIRE(b.reactions.size() == 1);
    CHECK_THAT(b.reactions[0].heatInto.si(), WithinAbs(-100.0, 1e-9));
    WARN("applied 100 W + 300 K boundary: K T - Q = [-100, 0], reaction -100 W, T1 = 350 K, residual 0");
}

TEST_CASE("ThermalBalance_FreeResidualsAreNotReactions", "[thermal][balance][reaction][free]") {
    // Only node 0 is prescribed. A temperature field that does NOT satisfy the free rows has a free residual, and none of it
    // is a reaction: the reactions are the prescribed unknowns' components and nothing else.
    const Net net = makeNet(chain(3, 2.0), {0.0, 0.0, 0.0}, {{0, 300.0}});
    const std::vector<Temperature> wrong = kelvin({300.0, 310.0, 305.0});   // the true solution is uniform 300 K
    const auto residual = thermal::steadyFullResidual(net.k, net.base.q, wrong);
    REQUIRE(residual.has_value());
    CHECK(std::abs((*residual)[1]) > 1.0);
    CHECK(std::abs((*residual)[2]) > 1.0);
    const ThermalHeatBalance b = mustBalance(thermal::steadyHeatBalance(algebraOf(net), wrong));
    REQUIRE(b.reactions.size() == 1);                            // one prescribed unknown: one reaction
    CHECK(b.reactions[0].dof.value() == 0);
    CHECK_THAT(b.reactions[0].heatInto.si(), WithinAbs((*residual)[0], 1e-12));
    // The free residual is visible only as the balance's imbalance (it is a defect, not a heat), and the books do not close.
    CHECK_THAT(b.residual().si(), WithinAbs(-((*residual)[1] + (*residual)[2]), 1e-9));   // B = -(sum of the free residuals)
    CHECK(b.status == BalanceStatus::OutOfTolerance);
}

TEST_CASE("ThermalBalance_DuplicateIdenticalDirichletConstraintsAreOneReaction", "[thermal][balance][reaction][duplicates]") {
    // P18-BC-001 collapses two identical prescriptions of one unknown into ONE constraint; the reaction of that unknown
    // is counted once. Built here through the same constraint-map builder.
    Algebra base = makeAlgebra(pair(2.0), {0.0, 0.0}, {});
    const std::vector<ThermalTemperatureConstraint> doubled{{dofIx(0), Temperature::fromSi(400.0)},
                                                            {dofIx(0), Temperature::fromSi(400.0)},
                                                            {dofIx(1), Temperature::fromSi(300.0)}};
    auto constraints = thermal::buildThermalConstraintMap(base.hand.dofs, doubled);
    REQUIRE(constraints.has_value());
    REQUIRE(constraints->size() == 2);                           // unique unknowns
    const thermal::ConductanceMatrix k{base.k};
    const BalanceAlgebra input{.conductance = k, .capacity = nullptr, .heat = base.q, .constraints = *constraints,
                               .loads = {}, .convections = {}};
    const ThermalHeatBalance b = mustBalance(thermal::steadyHeatBalance(input, kelvin({400.0, 300.0})));
    REQUIRE(b.reactions.size() == 2);
    CHECK_THAT(reactionOf(b, 0), WithinAbs(200.0, 1e-12));       // not 400
    CHECK_THAT(reactionOf(b, 1), WithinAbs(-200.0, 1e-12));
    CHECK_THAT(b.terms.prescribedTemperatureReaction.si(), WithinAbs(0.0, 1e-12));
}

// ===========================================================================
// Categories
// ===========================================================================

TEST_CASE("ThermalBalance_AppliedSurfaceHeatTotal", "[thermal][balance][categories][surface]") {
    // Three surface loads, one of them an extraction, and a node-sharing generation load: the surface total is the signed sum.
    const Net net = makeNet(pair(2.0), {0.0, 0.0}, {{0, 300.0}});
    // Q must account for the loads: 30 - 5 + 12 = 37 W surface, + 3 generation = 40 W.
    const Net loaded = makeNet(pair(2.0), {0.0, 40.0}, {{0, 300.0}});
    const std::vector<ThermalLoadContribution> loads{surfaceContribution(1, 30.0), surfaceContribution(2, -5.0),
                                                     generationContribution(3, 3.0), surfaceContribution(4, 12.0)};
    const ThermalHeatBalance b = mustBalance(thermal::steadyHeatBalance(algebraOf(loaded, loads), mustSolve(loaded.base).temperatures));
    REQUIRE(b.surfaceLoads.size() == 3);
    CHECK(b.surfaceLoads[0].load == ThermalLoadId::fromValue(1));
    CHECK(b.surfaceLoads[1].load == ThermalLoadId::fromValue(2));
    CHECK(b.surfaceLoads[2].load == ThermalLoadId::fromValue(4));
    CHECK_THAT(b.surfaceLoads[1].heatInto.si(), WithinAbs(-5.0, 1e-15));
    CHECK_THAT(b.terms.appliedSurfaceHeat.si(), WithinAbs(37.0, 1e-12));
    (void)net;
}

TEST_CASE("ThermalBalance_VolumetricGenerationTotal", "[thermal][balance][categories][generation]") {
    const Net net = makeNet(pair(2.0), {0.0, 14.0}, {{0, 300.0}});
    const std::vector<ThermalLoadContribution> loads{generationContribution(1, 20.0), generationContribution(2, -6.0)};
    const ThermalHeatBalance b = mustBalance(thermal::steadyHeatBalance(algebraOf(net, loads), mustSolve(net.base).temperatures));
    REQUIRE(b.generationLoads.size() == 2);
    CHECK_THAT(b.terms.generatedHeat.si(), WithinAbs(14.0, 1e-12));    // 20 - 6: the sink is kept
    CHECK(b.surfaceLoads.empty());
}

TEST_CASE("ThermalBalance_MultipleSurfaceLoadsAreEachCounted", "[thermal][balance][categories][multiple]") {
    // Two loads with the SAME value (and so, in a model, possibly the same target) both count: nothing deduplicates by target.
    const Net net = makeNet(pair(2.0), {0.0, 20.0}, {{0, 300.0}});
    const std::vector<ThermalLoadContribution> loads{surfaceContribution(1, 10.0), surfaceContribution(2, 10.0)};
    const ThermalHeatBalance b = mustBalance(thermal::steadyHeatBalance(algebraOf(net, loads), mustSolve(net.base).temperatures));
    REQUIRE(b.surfaceLoads.size() == 2);
    CHECK_THAT(b.terms.appliedSurfaceHeat.si(), WithinAbs(20.0, 1e-12));
    CHECK(b.status == BalanceStatus::Balanced);
}

TEST_CASE("ThermalBalance_PerLoadTotalsMatchCategory", "[thermal][balance][aggregation][loads]") {
    const Net net = makeNet(pair(2.0), {0.0, 13.7}, {{0, 300.0}});
    const std::vector<ThermalLoadContribution> loads{surfaceContribution(1, 4.1), generationContribution(2, 2.2),
                                                     surfaceContribution(3, 0.3), generationContribution(4, 7.1)};
    const ThermalHeatBalance b = mustBalance(thermal::steadyHeatBalance(algebraOf(net, loads), mustSolve(net.base).temperatures));
    double surface = 0.0;
    for (const LoadHeat& load : b.surfaceLoads) {
        surface += load.heatInto.si();
    }
    double generation = 0.0;
    for (const LoadHeat& load : b.generationLoads) {
        generation += load.heatInto.si();
    }
    CHECK(surface == b.terms.appliedSurfaceHeat.si());           // bit for bit: the category IS the sum of the details
    CHECK(generation == b.terms.generatedHeat.si());
}

TEST_CASE("ThermalBalance_PerConvectionTotalsMatchCategory", "[thermal][balance][aggregation][convection]") {
    const PreparedConvectionBoundary a = oneTriangle(1, 0.5, 8.0, 300.0);
    const PreparedConvectionBoundary b2 = oneTriangle(2, 0.25, 4.0, 280.0);
    const Dense2 k = added(denseKconv(a), denseKconv(b2));
    std::vector<double> q = denseQconv(a);
    const std::vector<double> q2 = denseQconv(b2);
    for (std::size_t i = 0; i < 3; ++i) {
        q[i] += q2[i];
    }
    const Net net = makeNet(k, q);
    const std::vector<PreparedConvectionBoundary> convections{a, b2};
    const ThermalHeatBalance b = mustBalance(thermal::steadyHeatBalance(algebraOf(net, {}, convections), mustSolve(net.base).temperatures));
    REQUIRE(b.convections.size() == 2);
    CHECK(b.convections[0].condition == bid(1));
    CHECK(b.convections[1].condition == bid(2));
    CHECK((b.convections[0].heatInto.si() + b.convections[1].heatInto.si()) == b.terms.convectionHeat.si());
    CHECK_THAT(b.residual().si(), WithinAbs(0.0, 1e-9));
}

TEST_CASE("ThermalBalance_PerReactionTotalsMatchCategory", "[thermal][balance][aggregation][reactions]") {
    const Net net = makeNet(chain(6, 5.0), std::vector<double>(6, 0.0), {{0, 400.0}, {2, 340.0}, {5, 300.0}});
    const ThermalHeatBalance b = mustBalance(thermal::steadyHeatBalance(algebraOf(net), mustSolve(net.base).temperatures));
    REQUIRE(b.reactions.size() == 3);
    double sum = 0.0;
    for (const ReactionHeat& reaction : b.reactions) {
        sum += reaction.heatInto.si();
    }
    CHECK(sum == b.terms.prescribedTemperatureReaction.si());
    // Ascending by unknown: the constraint map's own order.
    CHECK(b.reactions[0].dof.value() < b.reactions[1].dof.value());
    CHECK(b.reactions[1].dof.value() < b.reactions[2].dof.value());
}

// ===========================================================================
// Steady balance: the reference suite
// ===========================================================================

TEST_CASE("ThermalBalance_SteadyPureConductionBalance", "[thermal][balance][steady][reference]") {
    // BAL-STEADY-01: two prescribed temperatures, pure conduction, no loads and no convection.
    const Net net = makeNet(pair(2.0), {0.0, 0.0}, {{0, 400.0}, {1, 300.0}});
    const ThermalHeatBalance b = mustBalance(thermal::steadyHeatBalance(algebraOf(net), kelvin({400.0, 300.0})));
    CHECK_THAT(b.terms.appliedSurfaceHeat.si(), WithinAbs(0.0, 1e-15));
    CHECK_THAT(b.terms.generatedHeat.si(), WithinAbs(0.0, 1e-15));
    CHECK_THAT(b.terms.convectionHeat.si(), WithinAbs(0.0, 1e-15));
    CHECK_THAT(b.terms.prescribedTemperatureReaction.si(), WithinAbs(0.0, 1e-12));
    CHECK_THAT(b.residual().si(), WithinAbs(0.0, 1e-12));
    CHECK(b.status == BalanceStatus::Balanced);
}

TEST_CASE("ThermalBalance_SteadyAppliedHeatAndReaction", "[thermal][balance][steady][reference]") {
    // BAL-STEADY-02: see AppliedHeatBalancedByDirichletReaction; here with a negative load (extraction) and a larger network.
    const Net net = makeNet(chain(4, 3.0), {0.0, 0.0, -9.0, 0.0}, {{0, 350.0}});
    const std::vector<ThermalLoadContribution> loads{surfaceContribution(1, -9.0)};
    const ThermalHeatBalance b = mustBalance(thermal::steadyHeatBalance(algebraOf(net, loads), mustSolve(net.base).temperatures));
    CHECK_THAT(b.terms.appliedSurfaceHeat.si(), WithinAbs(-9.0, 1e-12));
    CHECK_THAT(b.terms.prescribedTemperatureReaction.si(), WithinAbs(9.0, 1e-9));   // the boundary SUPPLIES what is extracted
    CHECK(b.status == BalanceStatus::Balanced);
}

TEST_CASE("ThermalBalance_SteadyGenerationAndConvection", "[thermal][balance][steady][reference][convection]") {
    // BAL-STEADY-03: one body of three nodes whose only exit is a convection face: h A = 4 W/K, Tinf = 300 K, 12 W generated.
    //   T = Tinf + P/(hA) = 303 K everywhere; the face loses 4 * 3 = 12 W; no prescribed unknown, no reaction.
    const PreparedConvectionBoundary face = oneTriangle(1, 0.5, 8.0, 300.0);
    std::vector<double> q = denseQconv(face);
    for (double& v : q) {
        v += 4.0;                                    // 12 W of generation, a third on each node
    }
    const Net net = makeNet(denseKconv(face), q);
    const std::vector<ThermalLoadContribution> loads{generationContribution(1, 12.0)};
    const std::vector<PreparedConvectionBoundary> convections{face};
    const ThermalAlgebraicSolution solved = mustSolve(net.base);
    for (std::size_t i = 0; i < 3; ++i) {
        CHECK_THAT(tempAt(solved, i), WithinAbs(303.0, 1e-9));
    }
    const ThermalHeatBalance b = mustBalance(thermal::steadyHeatBalance(algebraOf(net, loads, convections), solved.temperatures));
    CHECK_THAT(b.terms.generatedHeat.si(), WithinAbs(12.0, 1e-12));
    CHECK_THAT(b.terms.convectionHeat.si(), WithinAbs(-12.0, 1e-9));
    CHECK(b.reactions.empty());
    CHECK_THAT(b.terms.prescribedTemperatureReaction.si(), WithinAbs(0.0, 1e-15));
    CHECK_THAT(b.residual().si(), WithinAbs(0.0, 1e-9));
    CHECK(b.status == BalanceStatus::Balanced);
}

TEST_CASE("ThermalBalance_SteadyRobinOnlyOracle", "[thermal][balance][steady][reference][convection]") {
    // 40 W applied, h A = 4 W/K, Tinf = 300 K: T = 310 K, the face loses 40 W. No Dirichlet reaction.
    const PreparedConvectionBoundary face = oneTriangle(1, 0.5, 8.0, 300.0);
    std::vector<double> q = denseQconv(face);
    for (double& v : q) {
        v += 40.0 / 3.0;
    }
    const Net net = makeNet(denseKconv(face), q);
    const std::vector<ThermalLoadContribution> loads{surfaceContribution(1, 40.0)};
    const std::vector<PreparedConvectionBoundary> convections{face};
    const ThermalAlgebraicSolution solved = mustSolve(net.base);
    CHECK_THAT(tempAt(solved, 0), WithinAbs(310.0, 1e-9));
    const ThermalHeatBalance b = mustBalance(thermal::steadyHeatBalance(algebraOf(net, loads, convections), solved.temperatures));
    CHECK_THAT(b.terms.appliedSurfaceHeat.si(), WithinAbs(40.0, 1e-12));
    REQUIRE(b.convections.size() == 1);
    CHECK_THAT(b.convections[0].heatOut.si(), WithinAbs(40.0, 1e-9));
    CHECK_THAT(b.convections[0].heatInto.si(), WithinAbs(-40.0, 1e-9));
    CHECK_THAT(b.residual().si(), WithinAbs(0.0, 1e-9));
}

TEST_CASE("ThermalBalance_SteadyFixedTemperatureAndConvection", "[thermal][balance][steady][reference][convection][reaction]") {
    // BAL-STEADY-04: every node of a convecting face held at a temperature. Above the ambient the face loses heat and the
    // constraint SUPPLIES it; below the ambient the face gains heat and the constraint REMOVES it.
    const PreparedConvectionBoundary face = oneTriangle(1, 0.5, 8.0, 300.0);
    const std::vector<PreparedConvectionBoundary> convections{face};
    const Dense2 k = denseKconv(face);
    const std::vector<double> q = denseQconv(face);

    const Net hot = makeNet(k, q, {{0, 330.0}, {1, 330.0}, {2, 330.0}});
    const ThermalHeatBalance a = mustBalance(thermal::steadyHeatBalance(algebraOf(hot, {}, convections), kelvin({330.0, 330.0, 330.0})));
    CHECK_THAT(a.terms.convectionHeat.si(), WithinAbs(-120.0, 1e-9));              // hA (330 - 300) = 120 W out
    CHECK_THAT(a.terms.prescribedTemperatureReaction.si(), WithinAbs(120.0, 1e-9));
    CHECK_THAT(a.residual().si(), WithinAbs(0.0, 1e-9));
    for (const ReactionHeat& reaction : a.reactions) {
        CHECK_THAT(reaction.heatInto.si(), WithinAbs(40.0, 1e-9));                 // a third of it at each node
    }

    const Net cold = makeNet(k, q, {{0, 290.0}, {1, 290.0}, {2, 290.0}});
    const ThermalHeatBalance c = mustBalance(thermal::steadyHeatBalance(algebraOf(cold, {}, convections), kelvin({290.0, 290.0, 290.0})));
    CHECK_THAT(c.terms.convectionHeat.si(), WithinAbs(40.0, 1e-9));                // the body GAINS 40 W
    CHECK_THAT(c.terms.prescribedTemperatureReaction.si(), WithinAbs(-40.0, 1e-9));
    CHECK_THAT(c.residual().si(), WithinAbs(0.0, 1e-9));
    WARN("fixed T and convection: 330 K body in 300 K air: convection -120 W, reaction +120 W; 290 K: convection +40 W, reaction -40 W");
}

TEST_CASE("ThermalBalance_SteadyCombinedCategories", "[thermal][balance][steady][reference][multiple]") {
    // BAL-STEADY-05: all four categories at once on one body of three nodes: a convecting triangle, 30 W of surface heat,
    // 6 W of generation and a prescribed node. hA = 4 W/K, Tinf = 300 K. Chain conduction g = 1 W/K between the nodes.
    const PreparedConvectionBoundary face = oneTriangle(1, 0.5, 8.0, 300.0);
    const Dense2 k = added(denseKconv(face), chain(3, 1.0));
    std::vector<double> q = denseQconv(face);
    q[1] += 30.0;                                    // the surface heat enters at node 1
    q[2] += 6.0;                                     // the generation at node 2
    const Net net = makeNet(k, q, {{0, 320.0}});
    const std::vector<ThermalLoadContribution> loads{surfaceContribution(1, 30.0), generationContribution(2, 6.0)};
    const std::vector<PreparedConvectionBoundary> convections{face};
    const ThermalAlgebraicSolution solved = mustSolve(net.base);
    const ThermalHeatBalance b = mustBalance(thermal::steadyHeatBalance(algebraOf(net, loads, convections), solved.temperatures));
    CHECK_THAT(b.terms.appliedSurfaceHeat.si(), WithinAbs(30.0, 1e-12));
    CHECK_THAT(b.terms.generatedHeat.si(), WithinAbs(6.0, 1e-12));
    CHECK(b.terms.convectionHeat.si() < 0.0);
    CHECK(std::abs(b.terms.prescribedTemperatureReaction.si()) > 1e-3);
    // Independent of the production code: the convection by the hand formula, hA (mean T - Tinf).
    const double meanT = (tempAt(solved, 0) + tempAt(solved, 1) + tempAt(solved, 2)) / 3.0;
    CHECK_THAT(b.terms.convectionHeat.si(), WithinAbs(-4.0 * (meanT - 300.0), 1e-9));
    CHECK_THAT(b.residual().si(), WithinAbs(0.0, 1e-9));
    CHECK(b.status == BalanceStatus::Balanced);
}

TEST_CASE("ThermalBalance_MultipleConvectionBoundariesAreAdditive", "[thermal][balance][convection][multiple]") {
    // One body in contact with two ambients: h1 A1 = 4 W/K at 300 K and h2 A2 = 1 W/K at 280 K, plus 30 W applied.
    //   4 (T - 300) + 1 (T - 280) = 30   =>   T = 302 K;   out1 = 8 W, out2 = 22 W, total 30 W.
    const PreparedConvectionBoundary first = oneTriangle(1, 0.5, 8.0, 300.0);
    const PreparedConvectionBoundary second = oneTriangle(2, 0.5, 2.0, 280.0);
    const Dense2 k = added(denseKconv(first), denseKconv(second));
    std::vector<double> q = denseQconv(first);
    const std::vector<double> q2 = denseQconv(second);
    for (std::size_t i = 0; i < 3; ++i) {
        q[i] += q2[i] + 10.0;
    }
    const Net net = makeNet(k, q);
    const std::vector<ThermalLoadContribution> loads{surfaceContribution(1, 30.0)};
    const std::vector<PreparedConvectionBoundary> convections{first, second};
    const ThermalAlgebraicSolution solved = mustSolve(net.base);
    CHECK_THAT(tempAt(solved, 0), WithinAbs(302.0, 1e-9));
    const ThermalHeatBalance b = mustBalance(thermal::steadyHeatBalance(algebraOf(net, loads, convections), solved.temperatures));
    REQUIRE(b.convections.size() == 2);
    CHECK_THAT(b.convections[0].heatOut.si(), WithinAbs(8.0, 1e-9));
    CHECK_THAT(b.convections[1].heatOut.si(), WithinAbs(22.0, 1e-9));
    CHECK_THAT(b.terms.convectionHeat.si(), WithinAbs(-30.0, 1e-9));
    CHECK_THAT(b.residual().si(), WithinAbs(0.0, 1e-9));

    // The SAME physical face carried by two conditions (identical ambient) ADDS, it is not deduplicated.
    const PreparedConvectionBoundary twin = oneTriangle(3, 0.5, 8.0, 300.0);
    const std::vector<PreparedConvectionBoundary> twice{first, twin};
    const Dense2 k2 = added(denseKconv(first), denseKconv(twin));
    std::vector<double> q3 = denseQconv(first);
    const std::vector<double> q4 = denseQconv(twin);
    for (std::size_t i = 0; i < 3; ++i) {
        q3[i] += q4[i] + 10.0;
    }
    const Net netTwice = makeNet(k2, q3);
    const ThermalHeatBalance t = mustBalance(thermal::steadyHeatBalance(algebraOf(netTwice, loads, twice), mustSolve(netTwice.base).temperatures));
    CHECK_THAT(t.convections[0].heatOut.si(), WithinAbs(t.convections[1].heatOut.si(), 1e-12));
    CHECK_THAT(t.terms.convectionHeat.si(), WithinAbs(-30.0, 1e-9));    // both counted: 15 + 15
    CHECK_THAT(t.convections[0].heatOut.si(), WithinAbs(15.0, 1e-9));
}

TEST_CASE("ThermalBalance_MultipleTemperatureBoundariesAndLoadsTogether", "[thermal][balance][multiple]") {
    // Two prescribed ends and two loads between: a five-node bar, g = 4 W/K, ends 360 K and 300 K, 8 W in at node 1, 4 W out at node 3.
    const Net net = makeNet(chain(5, 4.0), {0.0, 8.0, 0.0, -4.0, 0.0}, {{0, 360.0}, {4, 300.0}});
    const std::vector<ThermalLoadContribution> loads{surfaceContribution(1, 8.0), surfaceContribution(2, -4.0)};
    const ThermalHeatBalance b = mustBalance(thermal::steadyHeatBalance(algebraOf(net, loads), mustSolve(net.base).temperatures));
    REQUIRE(b.reactions.size() == 2);
    CHECK_THAT(b.terms.appliedSurfaceHeat.si(), WithinAbs(4.0, 1e-12));
    CHECK_THAT(reactionOf(b, 0) + reactionOf(b, 4), WithinAbs(-4.0, 1e-8));   // the two boundaries together take the net 4 W
    CHECK_THAT(b.residual().si(), WithinAbs(0.0, 1e-8));
}

// ===========================================================================
// The traps
// ===========================================================================

TEST_CASE("ThermalBalance_AttributionMustAccountForTheHeatInput", "[thermal][balance][attribution]") {
    // Q carries 100 W. A balance that omits the load (or invents one) is refused: the attribution is never inferred from Q.
    const Net net = makeNet(pair(2.0), {0.0, 100.0}, {{0, 300.0}});
    const std::vector<Temperature> t = kelvin({300.0, 350.0});
    CHECK(balanceProblemOf(thermal::steadyHeatBalance(algebraOf(net), t)) == BalanceProblem::AttributionMissing);   // load omitted
    const std::vector<ThermalLoadContribution> extra{surfaceContribution(1, 100.0), generationContribution(2, 7.0)};
    CHECK(balanceProblemOf(thermal::steadyHeatBalance(algebraOf(net, extra), t)) == BalanceProblem::AttributionMissing);   // extra
    const std::vector<ThermalLoadContribution> right{surfaceContribution(1, 100.0)};
    CHECK_FALSE(balanceProblemOf(thermal::steadyHeatBalance(algebraOf(net, right), t)).has_value());
    // A convection condition omitted from the list: its ambient source is in Q and unaccounted for.
    const PreparedConvectionBoundary face = oneTriangle(1, 0.5, 8.0, 300.0);
    const Net withFace = makeNet(denseKconv(face), denseQconv(face));
    CHECK(balanceProblemOf(thermal::steadyHeatBalance(algebraOf(withFace), kelvin({300.0, 300.0, 300.0}))) ==
          BalanceProblem::AttributionMissing);
}

TEST_CASE("ThermalBalance_GlobalHeatInputIsNotAddedToTheCategories", "[thermal][balance][attribution][double-count]") {
    // The global Q already contains the loads and Qconv. A balance that added sum(Q) on top of the explicit categories would double
    // count them: here the correct residual is ~0 and the double-counted one would be the full 100 W.
    const Net net = makeNet(pair(2.0), {0.0, 100.0}, {{0, 300.0}});
    const std::vector<ThermalLoadContribution> loads{surfaceContribution(1, 100.0)};
    const ThermalHeatBalance b = mustBalance(thermal::steadyHeatBalance(algebraOf(net, loads), kelvin({300.0, 350.0})));
    const double doubleCounted = b.residual().si() + net.base.q.total().si();
    CHECK_THAT(b.residual().si(), WithinAbs(0.0, 1e-9));
    CHECK_THAT(doubleCounted, WithinAbs(100.0, 1e-9));
}

TEST_CASE("ThermalBalance_BalanceCannotRescueAWrongFreeField", "[thermal][balance][cancellation][solver-gate]") {
    // THE CANCELLATION TRAP. Two decoupled unknowns, K = I, Q = 0, and the field T = [+5, -5]: each free equation is violated by
    // 5 W, in opposite directions, so the TOTAL is zero. The balance (a total) is happy; the free residual is not. A balance PASS is
    // therefore never evidence of a solve: the accepted solver result is what gates it (a `SteadyThermalSolution` cannot be built
    // from this field), and the two gates are separate.
    const Net net = makeNet({{1.0, 0.0}, {0.0, 1.0}}, {0.0, 0.0});
    const std::vector<Temperature> wrong = kelvin({5.0, -5.0});
    const auto residual = thermal::steadyFullResidual(net.k, net.base.q, wrong);
    REQUIRE(residual.has_value());
    CHECK_THAT(std::max(std::abs((*residual)[0]), std::abs((*residual)[1])), WithinAbs(5.0, 1e-15));   // the local error
    const ThermalHeatBalance b = mustBalance(thermal::steadyHeatBalance(algebraOf(net), wrong));
    CHECK_THAT(b.residual().si(), WithinAbs(0.0, 1e-15));                                              // the global total
    CHECK(b.status == BalanceStatus::Balanced);
}

TEST_CASE("ThermalBalance_OutOfToleranceIsAResultNotAFailure", "[thermal][balance][tolerance][status]") {
    // A finite field one kelvin off: the books do not close, the terms are all there to be inspected, and nothing is thrown away.
    const Net net = makeNet(pair(2.0), {0.0, 100.0}, {{0, 300.0}});
    const std::vector<ThermalLoadContribution> loads{surfaceContribution(1, 100.0)};
    const auto off = thermal::steadyHeatBalance(algebraOf(net, loads), kelvin({300.0, 351.0}));
    REQUIRE(off.has_value());                                    // computed, not refused
    CHECK(off->status == BalanceStatus::OutOfTolerance);
    CHECK_THAT(off->terms.appliedSurfaceHeat.si(), WithinAbs(100.0, 1e-12));
    CHECK_THAT(off->terms.prescribedTemperatureReaction.si(), WithinAbs(-102.0, 1e-9));
    CHECK_THAT(off->residual().si(), WithinAbs(-2.0, 1e-9));
    CHECK(off->normalizedResidual() > 1e-3);
}

TEST_CASE("ThermalBalance_RefusesWhatItCannotCompute", "[thermal][balance][refusals]") {
    const Net net = makeNet(pair(2.0), {0.0, 0.0}, {{0, 300.0}});
    const std::vector<Temperature> good = kelvin({300.0, 300.0});
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    SECTION("a non-finite temperature") {
        CHECK(balanceProblemOf(thermal::steadyHeatBalance(algebraOf(net), kelvin({300.0, nan}))) == BalanceProblem::NonFiniteResidual);
        CHECK(balanceProblemOf(thermal::steadyHeatBalance(algebraOf(net), kelvin({inf, 300.0}))) == BalanceProblem::NonFiniteResidual);
    }
    SECTION("a non-finite load total") {
        const std::vector<ThermalLoadContribution> bad{surfaceContribution(1, nan)};
        CHECK(balanceProblemOf(thermal::steadyHeatBalance(algebraOf(net, bad), good)).has_value());
        const std::vector<ThermalLoadContribution> infinite{generationContribution(1, inf)};
        CHECK(balanceProblemOf(thermal::steadyHeatBalance(algebraOf(net, infinite), good)).has_value());
    }
    SECTION("sizes that do not agree") {
        CHECK(balanceProblemOf(thermal::steadyHeatBalance(algebraOf(net), kelvin({300.0}))) == BalanceProblem::NumberingMismatch);
        CHECK(balanceProblemOf(thermal::steadyHeatBalance(algebraOf(net), kelvin({300.0, 300.0, 300.0}))) ==
              BalanceProblem::NumberingMismatch);
    }
    SECTION("a steady balance of a transient algebra, and the reverse") {
        const Net transient = makeNet(pair(2.0), {0.0, 0.0}, {{0, 300.0}}, Dense2{{3, 2}, {2, 3}});
        CHECK(balanceProblemOf(thermal::steadyHeatBalance(algebraOf(transient), good)) == BalanceProblem::ModeMismatch);
        CHECK(balanceProblemOf(thermal::transientStepHeatBalance(algebraOf(net), good, good, 1.0, 1)) == BalanceProblem::ModeMismatch);
    }
    SECTION("a time step that is not finite and positive") {
        const Net transient = makeNet(pair(2.0), {0.0, 0.0}, {{0, 300.0}}, Dense2{{3, 2}, {2, 3}});
        for (const double dt : {0.0, -1.0, nan, inf}) {
            CAPTURE(dt);
            CHECK(balanceProblemOf(thermal::transientStepHeatBalance(algebraOf(transient), good, good, dt, 1)) ==
                  BalanceProblem::InvalidTimeStep);
        }
    }
    SECTION("the residual itself refuses a time step that is not finite and positive") {
        const Net transient = makeNet(pair(2.0), {0.0, 0.0}, {{0, 300.0}}, Dense2{{3, 2}, {2, 3}});
        for (const double dt : {0.0, -1.0, nan, inf}) {
            CAPTURE(dt);
            CHECK(balanceProblemOf(thermal::transientFullResidual(transient.k, *transient.c, transient.base.q, good, good, dt)) ==
                  BalanceProblem::InvalidTimeStep);
        }
    }
    SECTION("the failure names the step") {
        const Net transient = makeNet(pair(2.0), {0.0, 0.0}, {{0, 300.0}}, Dense2{{3, 2}, {2, 3}});
        const auto refused = thermal::transientStepHeatBalance(algebraOf(transient), good, kelvin({300.0, nan}), 1.0, 7);
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().step == 7);
    }
}

TEST_CASE("ThermalBalance_DiagnosticNamesAreStableAndDistinct", "[thermal][balance][diagnostics]") {
    const std::array<BalanceProblem, 11> all{BalanceProblem::SourceMismatch,    BalanceProblem::NumberingMismatch,
                                             BalanceProblem::ModeMismatch,      BalanceProblem::AttributionMissing,
                                             BalanceProblem::StepDataMissing,   BalanceProblem::InvalidTimeStep,
                                             BalanceProblem::NonFiniteResidual, BalanceProblem::NonFiniteReaction,
                                             BalanceProblem::NonFiniteCategory, BalanceProblem::NonFiniteStorage,
                                             BalanceProblem::NonFiniteBalance};
    std::vector<std::string> names;
    for (const BalanceProblem problem : all) {
        const std::string name{thermal::toString(problem)};
        CHECK_FALSE(name.empty());
        CHECK(name != "unknown");
        names.push_back(name);
    }
    std::ranges::sort(names);
    CHECK(std::ranges::adjacent_find(names) == names.end());
    CHECK(thermal::toString(static_cast<BalanceProblem>(200)) == "unknown");
    CHECK(thermal::toString(BalanceProblem::StepDataMissing) == "step_data_missing");
    CHECK(thermal::toString(BalanceProblem::AttributionMissing) == "attribution_missing");
    CHECK(thermal::toString(BalanceStatus::Balanced) == "balanced");
    CHECK(thermal::toString(BalanceStatus::OutOfTolerance) == "out_of_tolerance");
}

// ===========================================================================
// Transient
// ===========================================================================

TEST_CASE("ThermalBalance_TransientInsulatedNoLoad", "[thermal][balance][transient][reference]") {
    // BAL-TRANS-01: an insulated body at a uniform temperature, nothing applied: storage 0, every term 0, balance 0.
    const Net net = makeNet(pair(2.0), {0.0, 0.0}, {}, Dense2{{3, 2}, {2, 3}});
    const ThermalHeatBalance b =
        mustBalance(thermal::transientStepHeatBalance(algebraOf(net), kelvin({300.0, 300.0}), kelvin({300.0, 300.0}), 2.0, 1));
    REQUIRE(b.terms.storedEnergyRate.has_value());
    CHECK(b.terms.storedEnergyRate->si() == 0.0);
    CHECK(b.storedEnergyChange->si() == 0.0);
    CHECK(b.residual().si() == 0.0);
    CHECK(b.normalizedResidual() == 0.0);
    CHECK(b.status == BalanceStatus::Balanced);
    CHECK(b.step == 1);
    CHECK(b.stepSize->si() == 2.0);
}

TEST_CASE("ThermalBalance_TransientInsulatedGeneration", "[thermal][balance][transient][reference]") {
    // BAL-TRANS-02: C = [3 2; 2 3] (total 10 J/K), 20 W generated (10 W per node), dt = 2 s, insulated.
    //   uniform rise = Q dt / C_total = 20 * 2 / 10 = 4 K;  dE = 10 * 4 = 40 J;  storage rate = 40 / 2 = 20 W = the generation.
    const Dense2 dk = pair(2.0);
    const Dense2 dc{{3, 2}, {2, 3}};
    const std::vector<double> q{10.0, 10.0};
    const std::vector<double> next = denseBackwardEuler(dk, dc, q, {300.0, 300.0}, 2.0, {});
    CHECK_THAT(next[0], WithinAbs(304.0, 1e-12));
    CHECK_THAT(next[1], WithinAbs(304.0, 1e-12));
    const Net net = makeNet(dk, q, {}, dc);
    const std::vector<ThermalLoadContribution> loads{generationContribution(1, 20.0)};
    const ThermalHeatBalance b = mustBalance(thermal::transientStepHeatBalance(algebraOf(net, loads), kelvin({300.0, 300.0}), kelvin(next), 2.0, 1));
    CHECK_THAT(b.storedEnergyChange->si(), WithinAbs(40.0, 1e-10));
    CHECK_THAT(b.terms.storedEnergyRate->si(), WithinAbs(20.0, 1e-10));
    CHECK_THAT(b.terms.generatedHeat.si(), WithinAbs(20.0, 1e-12));
    CHECK_THAT(b.residual().si(), WithinAbs(0.0, 1e-10));
    CHECK(b.status == BalanceStatus::Balanced);
    WARN("insulated generation: C = 10 J/K, 20 W, dt = 2 s: dT = 4 K, dE = 40 J, storage rate 20 W = generation, residual 0");
}

TEST_CASE("ThermalBalance_TransientLumpedConvection", "[thermal][balance][transient][reference][convection]") {
    // BAL-TRANS-03: a lumped body (C_total = 10 J/K) cooling through h A = 2 W/K to 300 K air, T[n] = 400 K, dt = 1 s.
    //   backward Euler: 10 (T1 - 400)/1 + 2 (T1 - 300) = 0   =>   T1 = 4600/12 = 383.3333...
    //   storage rate = 10 (383.333 - 400) = -166.667 W;  convection INTO the body = -2 (83.333) = -166.667 W.
    const PreparedConvectionBoundary face = oneTriangle(1, 0.5, 4.0, 300.0);
    const Dense2 dk = denseKconv(face);
    const Dense2 dc = consistentTriangleCapacity(10.0);
    const std::vector<double> q = denseQconv(face);
    const std::vector<double> t0{400.0, 400.0, 400.0};
    const std::vector<double> next = denseBackwardEuler(dk, dc, q, t0, 1.0, {});
    for (const double t : next) {
        CHECK_THAT(t, WithinAbs(4600.0 / 12.0, 1e-9));
    }
    const Net net = makeNet(dk, q, {}, dc);
    const std::vector<PreparedConvectionBoundary> convections{face};
    const ThermalHeatBalance b = mustBalance(thermal::transientStepHeatBalance(algebraOf(net, {}, convections), kelvin(t0), kelvin(next), 1.0, 1));
    CHECK_THAT(b.terms.storedEnergyRate->si(), WithinAbs(-1000.0 / 6.0, 1e-8));
    CHECK_THAT(b.terms.convectionHeat.si(), WithinAbs(-1000.0 / 6.0, 1e-8));
    CHECK_THAT(b.residual().si(), WithinAbs(0.0, 1e-8));
    CHECK(b.status == BalanceStatus::Balanced);

    // The convection is evaluated at T[n+1]: at T[n] it would be -2 (400 - 300) = -200 W, which is not the storage rate.
    const auto atOld = thermal::convectionHeatOf(face, kelvin(t0));
    REQUIRE(atOld.has_value());
    CHECK_THAT(atOld->heatInto.si(), WithinAbs(-200.0, 1e-9));
    CHECK(std::abs(atOld->heatInto.si() - b.terms.storedEnergyRate->si()) > 30.0);

    // With 30 W applied (10 W per node) the three terms balance: P + Qconv_in = storage rate.
    std::vector<double> qLoaded = q;
    for (double& v : qLoaded) {
        v += 10.0;
    }
    const std::vector<double> nextLoaded = denseBackwardEuler(dk, dc, qLoaded, t0, 1.0, {});
    CHECK_THAT(nextLoaded[0], WithinAbs((4000.0 + 600.0 + 30.0) / 12.0, 1e-9));
    const Net loadedNet = makeNet(dk, qLoaded, {}, dc);
    const std::vector<ThermalLoadContribution> loads{surfaceContribution(1, 30.0)};
    const ThermalHeatBalance l =
        mustBalance(thermal::transientStepHeatBalance(algebraOf(loadedNet, loads, convections), kelvin(t0), kelvin(nextLoaded), 1.0, 1));
    CHECK_THAT(l.terms.appliedSurfaceHeat.si() + l.terms.convectionHeat.si(), WithinAbs(l.terms.storedEnergyRate->si(), 1e-8));
    CHECK_THAT(l.residual().si(), WithinAbs(0.0, 1e-8));
}

TEST_CASE("ThermalBalance_TransientDirichletReactionIncludesCapacity", "[thermal][balance][transient][reaction][capacity]") {
    // BAL-TRANS-04. C = [2 1; 1 2], K = [1 -1; -1 1], Q = 0, dt = 1. Node 0 is prescribed 300 K at both ends of the step;
    // node 1 goes from 330 K to T1'. Row 1 of backward Euler: (2/1 + 1) T1' + (1/1 - 1) 300 = 0 + (1*300 + 2*330)/1  =>  T1' = 320.
    //   dT = [0, -10]    C dT = [-10, -20]    K T' = [300 - 320, -300 + 320] = [-20, +20]
    //   r_0 = (C dT)_0 / dt + (K T')_0 - Q_0 = -10 - 20 = -30 W      the reaction INTO the body at node 0
    //   r_1 = -20 + 20 = 0 (free, as it must be)
    // The STEADY residual (K T' - Q)_0 would be -20 W: it omits the storage coupling of the free neighbour (-10 W).
    const Dense2 dk = pair(1.0);
    const Dense2 dc{{2, 1}, {1, 2}};
    const std::vector<double> prev{300.0, 330.0};
    const std::vector<double> next = denseBackwardEuler(dk, dc, {0.0, 0.0}, prev, 1.0, {{0, 300.0}});
    CHECK_THAT(next[0], WithinAbs(300.0, 1e-15));
    CHECK_THAT(next[1], WithinAbs(320.0, 1e-12));
    const Net net = makeNet(dk, {0.0, 0.0}, {{0, 300.0}}, dc);

    const auto transient = thermal::transientFullResidual(net.k, *net.c, net.base.q, kelvin(prev), kelvin(next), 1.0);
    REQUIRE(transient.has_value());
    CHECK_THAT((*transient)[0], WithinAbs(-30.0, 1e-12));
    CHECK_THAT((*transient)[1], WithinAbs(0.0, 1e-12));
    const auto steady = thermal::steadyFullResidual(net.k, net.base.q, kelvin(next));
    REQUIRE(steady.has_value());
    CHECK_THAT((*steady)[0], WithinAbs(-20.0, 1e-12));       // the steady formula: wrong for a transient step

    const ThermalHeatBalance b = mustBalance(thermal::transientStepHeatBalance(algebraOf(net), kelvin(prev), kelvin(next), 1.0, 1));
    REQUIRE(b.reactions.size() == 1);
    CHECK_THAT(b.reactions[0].heatInto.si(), WithinAbs(-30.0, 1e-12));
    // dE = 1^T C dT = -30 J over 1 s: the body loses 30 W of stored heat and the constraint removes 30 W.
    CHECK_THAT(b.storedEnergyChange->si(), WithinAbs(-30.0, 1e-12));
    CHECK_THAT(b.terms.storedEnergyRate->si(), WithinAbs(-30.0, 1e-12));
    CHECK_THAT(b.residual().si(), WithinAbs(0.0, 1e-12));
    CHECK(b.status == BalanceStatus::Balanced);

    // A LUMPED capacity (row sums on the diagonal) gives another reaction: (C dT)_0 = 3 * 0 = 0 and the balance would still
    // close for a lumped model, but it is not THIS model's. The consistent coupling is what -10 W is.
    const Dense2 lumped{{3, 0}, {0, 3}};
    const std::vector<double> nextLumped = denseBackwardEuler(dk, lumped, {0.0, 0.0}, prev, 1.0, {{0, 300.0}});
    const Net lumpedNet = makeNet(dk, {0.0, 0.0}, {{0, 300.0}}, lumped);
    const auto lumpedResidual = thermal::transientFullResidual(lumpedNet.k, *lumpedNet.c, lumpedNet.base.q, kelvin(prev), kelvin(nextLumped), 1.0);
    REQUIRE(lumpedResidual.has_value());
    CHECK(std::abs((*lumpedResidual)[0] - (*transient)[0]) > 1.0);
    WARN("transient reaction: C = [2 1;1 2], dT = [0, -10], K T' = [-20, +20]: r_0 = -10 (storage) - 20 (conduction) = -30 W; the steady formula gives -20");
}

TEST_CASE("ThermalBalance_TransientStoredEnergyUsesFullC", "[thermal][balance][transient][capacity][storage]") {
    // The constrained unknown is part of the stored energy. C = [2 1; 1 2], dT = [10, 0] (node 0 is the PRESCRIBED one that changes
    // by a prescribed ramp): 1^T C dT = (2 + 1) * 10 = 30 J.  C_ff alone (node 1) would see 0; diag(C) alone 20 J.
    const Net net = makeNet(pair(1.0), {0.0, 0.0}, {{0, 310.0}}, Dense2{{2, 1}, {1, 2}});
    const auto stored = thermal::storedEnergyChange(*net.c, kelvin({300.0, 330.0}), kelvin({310.0, 330.0}));
    REQUIRE(stored.has_value());
    CHECK_THAT(stored->si(), WithinAbs(30.0, 1e-15));
    CHECK(std::abs(stored->si() - 20.0) > 5.0);    // diag(C) only
    CHECK(std::abs(stored->si() - 0.0) > 5.0);     // the free unknown only
}

TEST_CASE("ThermalBalance_NonuniformStoredEnergyUsesConsistentCapacity", "[thermal][balance][transient][capacity][storage]") {
    // C = [2 1; 1 2] and a NONUNIFORM change dT = [10, -4]: 1^T C dT = [3, 3] . dT = 30 - 12 = 18 J. Diagonal only: 2*10 + 2*(-4) = 12 J.
    // (Row-sum lumping gives the same TOTAL energy; what it changes is each row, hence the reactions: see the reaction case.)
    const Dense2 dc{{2, 1}, {1, 2}};
    const std::vector<double> prev{300.0, 330.0};
    const std::vector<double> next{310.0, 326.0};
    const Net net = makeNet(pair(1.0), {0.0, 0.0}, {}, dc);
    const auto stored = thermal::storedEnergyChange(*net.c, kelvin(prev), kelvin(next));
    REQUIRE(stored.has_value());
    CHECK_THAT(stored->si(), WithinAbs(18.0, 1e-15));
    const double diagonalOnly = dc[0][0] * (next[0] - prev[0]) + dc[1][1] * (next[1] - prev[1]);
    CHECK_THAT(diagonalOnly, WithinAbs(12.0, 1e-15));
    CHECK(std::abs(stored->si() - diagonalOnly) > 5.0);
    // Computed independently by the test: 1^T (C dT) with the dense product written out.
    const std::vector<double> dT{10.0, -4.0};
    const std::vector<double> cdt = multiplyDense(dc, dT);
    CHECK_THAT(stored->si(), WithinAbs(cdt[0] + cdt[1], 1e-15));
    WARN("consistent capacity: C = [2 1;1 2], dT = [10, -4]: dE = 18 J (diag(C) alone would give 12 J)");
}

TEST_CASE("ThermalBalance_TransientFullyConstrainedStillBalances", "[thermal][balance][transient][constraints]") {
    // Both unknowns prescribed and constant: there is no free row to hide behind. K = [2 -2; -2 2], Q = [5, 3]: r = K T - Q = [-5, -3], the
    // reactions remove the 8 W applied, and nothing is stored.
    const Net net = makeNet(pair(2.0), {5.0, 3.0}, {{0, 300.0}, {1, 300.0}}, Dense2{{2, 1}, {1, 2}});
    const std::vector<ThermalLoadContribution> loads{surfaceContribution(1, 8.0)};
    const ThermalHeatBalance b =
        mustBalance(thermal::transientStepHeatBalance(algebraOf(net, loads), kelvin({300.0, 300.0}), kelvin({300.0, 300.0}), 0.5, 1));
    CHECK_THAT(reactionOf(b, 0), WithinAbs(-5.0, 1e-12));
    CHECK_THAT(reactionOf(b, 1), WithinAbs(-3.0, 1e-12));
    CHECK(b.terms.storedEnergyRate->si() == 0.0);
    CHECK_THAT(b.residual().si(), WithinAbs(0.0, 1e-12));
    CHECK(b.status == BalanceStatus::Balanced);
}

TEST_CASE("ThermalBalance_ConstantTemperatureUnderGenerationIsRemovedByTheReaction", "[thermal][balance][reaction][constraints]") {
    // A body held at 300 K with 20 W generated in it: the constraints must remove exactly 20 W, steady or transient.
    const Net steadyNet = makeNet(pair(2.0), {10.0, 10.0}, {{0, 300.0}, {1, 300.0}});
    const std::vector<ThermalLoadContribution> loads{generationContribution(1, 20.0)};
    const ThermalHeatBalance s = mustBalance(thermal::steadyHeatBalance(algebraOf(steadyNet, loads), kelvin({300.0, 300.0})));
    CHECK_THAT(s.terms.generatedHeat.si(), WithinAbs(20.0, 1e-12));
    CHECK_THAT(s.terms.prescribedTemperatureReaction.si(), WithinAbs(-20.0, 1e-12));
    CHECK_THAT(s.residual().si(), WithinAbs(0.0, 1e-12));
    const Net transientNet = makeNet(pair(2.0), {10.0, 10.0}, {{0, 300.0}, {1, 300.0}}, Dense2{{3, 2}, {2, 3}});
    const ThermalHeatBalance t =
        mustBalance(thermal::transientStepHeatBalance(algebraOf(transientNet, loads), kelvin({300.0, 300.0}), kelvin({300.0, 300.0}), 1.0, 1));
    CHECK_THAT(t.terms.prescribedTemperatureReaction.si(), WithinAbs(-20.0, 1e-12));
    CHECK(t.terms.storedEnergyRate->si() == 0.0);
    CHECK_THAT(t.residual().si(), WithinAbs(0.0, 1e-12));
}

// ===========================================================================
// Tolerance
// ===========================================================================

TEST_CASE("ThermalBalance_ZeroHeatCase", "[thermal][balance][tolerance][zero]") {
    // A body at a uniform 325.7 K with nothing applied: every term is zero up to the rounding of K T, the gross scale is ~0, and the
    // normalised residual neither divides by zero nor claims a defect. The arithmetic scale (|K||T|) is what judges it.
    const Net net = makeNet(chain(3, 0.1), {0.0, 0.0, 0.0}, {{0, 325.7}});
    // A second link of another conductance, so the row sums are formed from different products.
    Dense2 k = chain(3, 0.1);
    k[1][1] += 0.3;
    k[1][2] -= 0.3;
    k[2][2] += 0.3;
    k[2][1] -= 0.3;
    const Net varied = makeNet(k, {0.0, 0.0, 0.0}, {{0, 325.7}});
    const ThermalHeatBalance b = mustBalance(thermal::steadyHeatBalance(algebraOf(varied), kelvin({325.7, 325.7, 325.7})));
    CHECK(std::abs(b.residual().si()) < 1e-12);
    CHECK(std::isfinite(b.normalizedResidual()));
    CHECK(b.status == BalanceStatus::Balanced);
    CHECK(b.arithmeticScale.si() > 10.0);                       // |K||T| ~ 1e2: the scale that rounding has
    CHECK(b.grossScale().si() < 1e-9);                          // no heat flows
    (void)net;
    // Exactly zero everywhere: normalised residual is exactly zero.
    const Net zero = makeNet(pair(2.0), {0.0, 0.0}, {{0, 300.0}, {1, 300.0}});
    const ThermalHeatBalance z = mustBalance(thermal::steadyHeatBalance(algebraOf(zero), kelvin({300.0, 300.0})));
    CHECK(z.residual().si() == 0.0);
    CHECK(z.normalizedResidual() == 0.0);
    CHECK(z.status == BalanceStatus::Balanced);
}

TEST_CASE("ThermalBalance_LargeCancellationScale", "[thermal][balance][tolerance][cancellation]") {
    // +1e6 W in through one load, -1e6 W + 5 out through another, 5 W net: the scale is the gross flow, not the net. A defect of
    // 1e-4 W in a million is within the arithmetic's reach; a defect of 1 W is not.
    const Net net = makeNet(pair(2.0), {0.0, 5.0}, {{0, 300.0}});
    const std::vector<ThermalLoadContribution> loads{surfaceContribution(1, 1.0e6), surfaceContribution(2, -1.0e6 + 5.0)};
    const ThermalAlgebraicSolution solved = mustSolve(net.base);
    const ThermalHeatBalance b = mustBalance(thermal::steadyHeatBalance(algebraOf(net, loads), solved.temperatures));
    CHECK_THAT(b.terms.appliedSurfaceHeat.si(), WithinAbs(5.0, 1e-9));
    CHECK(b.grossScale().si() > 1.0e6);
    CHECK_THAT(b.residual().si(), WithinAbs(0.0, 1e-8));
    CHECK(b.status == BalanceStatus::Balanced);
    WARN(std::format("measure large cancellation +1e6 W / -1e6 W + 5 W: gross {:.6e} W, arithmetic {:.6e} W, |B| {:.3e} W, |B|/(gross+arithmetic) {:.3e}, "
                     "allowed {:.3e} W",
                     b.grossScale().si(), b.arithmeticScale.si(), std::abs(b.residual().si()),
                     std::abs(b.residual().si()) / (b.grossScale().si() + b.arithmeticScale.si()),
                     thermal::kBalanceAbsoluteToleranceW + thermal::kBalanceRelativeTolerance * (b.grossScale().si() + b.arithmeticScale.si())));
    // The same gross flows with a real defect of 1 W in one load total: the attribution refuses it (the heat input is 5 W)...
    const std::vector<ThermalLoadContribution> wrong{surfaceContribution(1, 1.0e6), surfaceContribution(2, -1.0e6 + 6.0)};
    CHECK(balanceProblemOf(thermal::steadyHeatBalance(algebraOf(net, wrong), solved.temperatures)) == BalanceProblem::AttributionMissing);
    // ... and a 1 W error in the solved field is out of tolerance although it is a millionth of the gross flow.
    const auto off = thermal::steadyHeatBalance(algebraOf(net, loads), kelvin({300.0, solved.temperatures[1].si() + 0.5}));
    REQUIRE(off.has_value());
    CHECK(off->status == BalanceStatus::OutOfTolerance);
}

TEST_CASE("ThermalBalance_MeasuredTolerancePolicy", "[thermal][balance][tolerance][policy]") {
    using thermal::balanceWithinTolerance;
    // The edge of the policy: |B| <= abs + rel * (gross + arithmetic), and nothing non-finite is ever within it.
    const double gross = 100.0;
    const double arithmetic = 1000.0;
    const double limit = thermal::kBalanceAbsoluteToleranceW + thermal::kBalanceRelativeTolerance * (gross + arithmetic);
    CHECK(balanceWithinTolerance(Power::fromSi(0.0), Power::fromSi(gross), Power::fromSi(arithmetic)));
    CHECK(balanceWithinTolerance(Power::fromSi(limit * 0.99), Power::fromSi(gross), Power::fromSi(arithmetic)));
    CHECK(balanceWithinTolerance(Power::fromSi(-limit * 0.99), Power::fromSi(gross), Power::fromSi(arithmetic)));
    CHECK_FALSE(balanceWithinTolerance(Power::fromSi(limit * 1.01), Power::fromSi(gross), Power::fromSi(arithmetic)));
    CHECK_FALSE(balanceWithinTolerance(Power::fromSi(-limit * 1.01), Power::fromSi(gross), Power::fromSi(arithmetic)));
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    CHECK_FALSE(balanceWithinTolerance(Power::fromSi(nan), Power::fromSi(gross), Power::fromSi(arithmetic)));
    CHECK_FALSE(balanceWithinTolerance(Power::fromSi(inf), Power::fromSi(gross), Power::fromSi(arithmetic)));
    CHECK_FALSE(balanceWithinTolerance(Power::fromSi(0.0), Power::fromSi(nan), Power::fromSi(arithmetic)));
    CHECK_FALSE(balanceWithinTolerance(Power::fromSi(0.0), Power::fromSi(gross), Power::fromSi(inf)));
    // The scale is the GROSS flow, not the net: a small net over a huge gross is within; the same residual over no gross is not.
    CHECK(balanceWithinTolerance(Power::fromSi(1.0e-7), Power::fromSi(2.0e6), Power{}));
    CHECK_FALSE(balanceWithinTolerance(Power::fromSi(1.0e-6), Power::fromSi(1.0), Power{}));
    // The floor keeps a zero scale from dividing.
    ThermalHeatBalance empty;
    CHECK(empty.normalizedResidual() == 0.0);
    CHECK(std::isfinite(empty.normalizedResidual()));
    CHECK(thermal::kBalanceScaleFloorW > 0.0);
}

TEST_CASE("ThermalBalance_RepeatedAggregationDeterministic", "[thermal][balance][determinism]") {
    const PreparedConvectionBoundary a = oneTriangle(1, 0.5, 8.0, 300.0);
    const PreparedConvectionBoundary c = oneTriangle(2, 0.3, 5.0, 285.0);
    Dense2 k = added(denseKconv(a), denseKconv(c));
    k = added(k, chain(3, 0.7));
    std::vector<double> q = denseQconv(a);
    const std::vector<double> qc = denseQconv(c);
    for (std::size_t i = 0; i < 3; ++i) {
        q[i] += qc[i];
    }
    q[1] += 11.3;
    q[2] += 2.7;
    const Net net = makeNet(k, q, {{0, 340.0}});
    const std::vector<ThermalLoadContribution> loads{surfaceContribution(1, 11.3), generationContribution(2, 2.7)};
    const std::vector<PreparedConvectionBoundary> convections{a, c};
    const ThermalAlgebraicSolution solved = mustSolve(net.base);
    const ThermalHeatBalance first = mustBalance(thermal::steadyHeatBalance(algebraOf(net, loads, convections), solved.temperatures));
    int differences = 0;
    for (int i = 0; i < 25; ++i) {
        if (!(mustBalance(thermal::steadyHeatBalance(algebraOf(net, loads, convections), solved.temperatures)) == first)) {
            ++differences;
        }
    }
    CHECK(differences == 0);
    std::uint64_t hash = 14695981039346656037ULL;
    const auto mix = [&hash](double value) {
        const auto bits = std::bit_cast<std::uint64_t>(value);
        for (int i = 0; i < 8; ++i) {
            hash ^= (bits >> (8 * i)) & 0xFFU;
            hash *= 1099511628211ULL;
        }
    };
    mix(first.terms.appliedSurfaceHeat.si());
    mix(first.terms.generatedHeat.si());
    mix(first.terms.convectionHeat.si());
    mix(first.terms.prescribedTemperatureReaction.si());
    mix(first.residual().si());
    for (const ConvectionHeat& c2 : first.convections) {
        mix(c2.heatOut.si());
    }
    for (const ReactionHeat& r : first.reactions) {
        mix(r.heatInto.si());
    }
    WARN(std::format("determinism: 25 repeated balances, {} bitwise differences", differences));
    WARN(std::format("bit fingerprint, steady combined network balance: {:016x}", hash));
}

TEST_CASE("ThermalBalance_EveryDiagnosticIsReachable", "[thermal][balance][diagnostics][reachability]") {
    const double huge = 1.0e308;
    SECTION("category: contributions that overflow their own category but cancel in the attribution") {
        const Net net = makeNet(pair(2.0), {0.0, 0.0}, {{0, 300.0}});
        const std::vector<ThermalLoadContribution> loads{surfaceContribution(1, huge), generationContribution(2, -huge),
                                                         surfaceContribution(3, huge), generationContribution(4, -huge)};
        CHECK(balanceProblemOf(thermal::steadyHeatBalance(algebraOf(net, loads), kelvin({300.0, 300.0}))) ==
              BalanceProblem::NonFiniteCategory);
    }
    SECTION("category: a convection face whose exchange overflows") {
        const PreparedConvectionBoundary face = oneTriangle(1, 1.0e150, 1.0e150, 300.0);
        CHECK(balanceProblemOf(thermal::convectionHeatOf(face, kelvin({1.0e300, 1.0e300, 1.0e300}))) == BalanceProblem::NonFiniteCategory);
    }
    SECTION("reaction: finite components whose total overflows") {
        // Two independent pairs, (0, 2) and (1, 3), each a conductance of 1e300 W/K across 1e8 K: every prescribed row is +-1e308 (finite),
        // and the two hot rows sum to 2e308.
        const double g = 1.0e300;
        const Dense2 k{{g, 0, -g, 0}, {0, g, 0, -g}, {-g, 0, g, 0}, {0, -g, 0, g}};
        const Net net = makeNet(k, {0.0, 0.0, 0.0, 0.0}, {{0, 1.0e8}, {1, 1.0e8}, {2, 0.0}, {3, 0.0}});
        CHECK(balanceProblemOf(thermal::steadyHeatBalance(algebraOf(net), kelvin({1.0e8, 1.0e8, 0.0, 0.0}))) ==
              BalanceProblem::NonFiniteReaction);
    }
    SECTION("reaction: a residual component that is not a number") {
        const Net net = makeNet(pair(2.0), {0.0, 0.0}, {{0, 300.0}});
        const std::vector<double> bad{std::numeric_limits<double>::quiet_NaN(), 0.0};
        CHECK(balanceProblemOf(thermal::reactionHeats(bad, net.base.constraints)) == BalanceProblem::NonFiniteReaction);
    }
    SECTION("storage: a capacity whose product with a change overflows") {
        const Net net = makeNet(pair(2.0), {0.0, 0.0}, {}, Dense2{{huge, 0.0}, {0.0, huge}});
        CHECK(balanceProblemOf(thermal::storedEnergyChange(*net.c, kelvin({300.0, 300.0}), kelvin({310.0, 310.0}))) ==
              BalanceProblem::NonFiniteStorage);
    }
    SECTION("balance: finite residual, finite terms, but a scale that overflows") {
        // Each row of K T is exactly zero (g T - g T), so the residual is finite; the sum of |K||T| over the system is not.
        const double g = 5.0e153;
        const Net net = makeNet(chain(3, g), {0.0, 0.0, 0.0}, {{0, 5.0e153}});
        CHECK(balanceProblemOf(thermal::steadyHeatBalance(algebraOf(net), kelvin({5.0e153, 5.0e153, 5.0e153}))) ==
              BalanceProblem::NonFiniteBalance);
    }
    SECTION("residual, mismatch, attribution, mode and time step are covered by RefusesWhatItCannotCompute") {
        SUCCEED();
    }
}

TEST_CASE("ThermalBalance_TransientFinalPartialStepUsesActualDt", "[thermal][balance][transient][partial-step][seam]") {
    // THE SEAM, on a hand tetrahedron: V = 1/6 m^3, 60 W/m^3 is 10 W, rho cp V = 1200 * 5 / 6 = 1000 J/K, so an insulated tetrahedron warms at
    // 0.01 K/s. A nominal step of 0.3 s to t_end = 1 s is 0.3, 0.3, 0.3 and 0.1 s. The last step stores 10 W * 0.1 s = 1 J: its rate is 10 W,
    // while the nominal 0.3 s would call it 3.33 W.
    const AsmHand hand = makeAsmHand(unitTetNodes(), {}, {{0, 1, 2, 3}});
    const ThermalSystem system = assemble(hand, 45.0, capacityOf(1200.0, 5.0), generationHeat(hand, 60.0));
    const std::vector<ThermalTemperatureConstraint> none;
    const auto constraints = thermal::buildThermalConstraintMap(hand.dofs, none);
    REQUIRE(constraints.has_value());
    const thermal::TransientSettings settings{.timeStep = Time::fromSi(0.3), .endTime = Time::fromSi(1.0),
                                              .output = thermal::TransientOutputPolicy::EveryStep};
    const auto solved = thermal::solveTransientThermalSystem(system, hand.dofs, *constraints,
                                                             thermal::UniformInitialTemperature{Temperature::fromSi(300.0)}, settings,
                                                             ThermalSolverSettings{});
    INFO((solved.has_value() ? std::string{} : solved.error().error.message));
    REQUIRE(solved.has_value());
    REQUIRE(solved->grid().stepCount() == 4);
    const std::vector<ThermalLoadContribution> loads{generationContribution(1, 10.0)};
    const auto balance = thermal::balanceTransientSolution(system, hand.dofs, *constraints, loads, {}, *solved);
    INFO((balance.has_value() ? std::string{} : balance.error().error.message));
    REQUIRE(balance.has_value());
    REQUIRE(balance->stateCount() == 4);
    const double sizes[4] = {0.3, 0.3, 0.3, 0.1};
    double totalSpan = 0.0;
    for (std::size_t k = 0; k < 4; ++k) {
        const ThermalHeatBalance& s = balance->states()[k];
        CAPTURE(k);
        CHECK_THAT(s.stepSize->si(), WithinAbs(sizes[k], 1e-15));
        CHECK_THAT(s.storedEnergyChange->si(), WithinRel(10.0 * sizes[k], 1e-9));
        CHECK_THAT(s.terms.storedEnergyRate->si(), WithinRel(10.0, 1e-9));
        CHECK_THAT(s.residual().si(), WithinAbs(0.0, 1e-9));
        totalSpan += s.stepSize->si();
    }
    CHECK_THAT(totalSpan, WithinAbs(1.0, 1e-12));
    CHECK(balance->states()[3].time->si() == 1.0);                              // t_end exactly
    const double nominalRate = balance->states()[3].storedEnergyChange->si() / 0.3;
    CHECK_THAT(nominalRate, WithinRel(10.0 / 3.0, 1e-9));
    CHECK(std::abs(nominalRate - balance->states()[3].terms.storedEnergyRate->si()) > 6.0);   // the nominal-dt rate is wrong by two thirds
    REQUIRE(balance->integrated().has_value());
    CHECK_THAT(balance->integrated()->external.si(), WithinRel(10.0, 1e-9));
    CHECK_THAT(balance->integrated()->stored.si(), WithinRel(10.0, 1e-9));
    CHECK_THAT(balance->integrated()->endpoints.si(), WithinRel(10.0, 1e-9));
    CHECK(balance->balanced());
    WARN("final partial step: dt_nominal 0.3, dt_actual 0.1: storage rate 10 W (dE = 1 J); the nominal-dt rate would be 3.333 W");
}

TEST_CASE("ThermalBalance_TransientSeamNeedsEveryStep", "[thermal][balance][transient][step-data][seam]") {
    const AsmHand hand = makeAsmHand(unitTetNodes(), {}, {{0, 1, 2, 3}});
    const ThermalSystem system = assemble(hand, 45.0, capacityOf(1200.0, 5.0), generationHeat(hand, 60.0));
    const std::vector<ThermalTemperatureConstraint> none;
    const auto constraints = thermal::buildThermalConstraintMap(hand.dofs, none);
    REQUIRE(constraints.has_value());
    const std::vector<ThermalLoadContribution> loads{generationContribution(1, 10.0)};
    const auto run = [&](double dt, double end, thermal::TransientOutputPolicy output) {
        const thermal::TransientSettings settings{.timeStep = Time::fromSi(dt), .endTime = Time::fromSi(end), .output = output};
        auto solved = thermal::solveTransientThermalSystem(system, hand.dofs, *constraints,
                                                           thermal::UniformInitialTemperature{Temperature::fromSi(300.0)}, settings,
                                                           ThermalSolverSettings{});
        REQUIRE(solved.has_value());
        return std::move(*solved);
    };
    // Four steps, only the last state kept: the interval from 0 to 1 s is not a backward-Euler step.
    const TransientThermalSolution sparse = run(0.25, 1.0, thermal::TransientOutputPolicy::FinalOnly);
    const auto refused = thermal::balanceTransientSolution(system, hand.dofs, *constraints, loads, {}, sparse);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().problem == BalanceProblem::StepDataMissing);
    CHECK_THAT(refused.error().error.message, ContainsSubstring("EveryStep"));
    CHECK_THAT(refused.error().error.message, ContainsSubstring("4 integration steps"));
    // One step needs no intermediate state: the initial and the final one are the whole step.
    const TransientThermalSolution one = run(1.0, 1.0, thermal::TransientOutputPolicy::FinalOnly);
    const auto balanced = thermal::balanceTransientSolution(system, hand.dofs, *constraints, loads, {}, one);
    REQUIRE(balanced.has_value());
    CHECK(balanced->stateCount() == 1);
    CHECK_THAT(balanced->states()[0].terms.storedEnergyRate->si(), WithinRel(10.0, 1e-9));
    // The same steps with every state kept balance step by step, and agree with the single step on the totals.
    const TransientThermalSolution every = run(0.25, 1.0, thermal::TransientOutputPolicy::EveryStep);
    const auto each = thermal::balanceTransientSolution(system, hand.dofs, *constraints, loads, {}, every);
    REQUIRE(each.has_value());
    CHECK(each->stateCount() == 4);
    CHECK_THAT(each->integrated()->stored.si(), WithinRel(10.0, 1e-9));
    // A mode mismatch: a transient balance of a steady system.
    const ThermalSystem steadySystem = assemble(hand, 45.0, std::nullopt, generationHeat(hand, 60.0));
    const auto wrongMode = thermal::balanceTransientSolution(steadySystem, hand.dofs, *constraints, loads, {}, every);
    REQUIRE_FALSE(wrongMode.has_value());
    CHECK(wrongMode.error().problem == BalanceProblem::ModeMismatch);
}

TEST_CASE("ThermalBalance_AnIncompleteConstraintMapIsVisibleAsAnImbalance", "[thermal][balance][status][seam]") {
    // A solution that holds node 0 at 300 K, balanced against a constraint map that omits node 0: the omitted reaction is not counted, the
    // books do not close, and the result says so (`OutOfTolerance`, `balanced() == false`) instead of failing or passing silently.
    const AsmHand hand = makeAsmHand(unitTetNodes(), {}, {{0, 1, 2, 3}});
    const ThermalSystem system = assemble(hand, 45.0, std::nullopt, generationHeat(hand, 60.0));
    const std::vector<ThermalTemperatureConstraint> held{{dofIx(0), Temperature::fromSi(300.0)}};
    const auto constraints = thermal::buildThermalConstraintMap(hand.dofs, held);
    REQUIRE(constraints.has_value());
    const auto solved = thermal::solveSteadyThermalSystem(system, hand.dofs, *constraints, ThermalSolverSettings{});
    REQUIRE(solved.has_value());
    const std::vector<ThermalLoadContribution> loads{generationContribution(1, 10.0)};

    const auto right = thermal::balanceSteadySolution(system, hand.dofs, *constraints, loads, {}, *solved);
    REQUIRE(right.has_value());
    CHECK(right->balanced());
    CHECK_THAT(right->finalState().terms.prescribedTemperatureReaction.si(), WithinAbs(-10.0, 1e-9));   // the one boundary removes all 10 W
    CHECK(right->worstNormalizedResidual() < 1e-9);

    const std::vector<ThermalTemperatureConstraint> none;
    const auto omitted = thermal::buildThermalConstraintMap(hand.dofs, none);
    REQUIRE(omitted.has_value());
    const auto wrong = thermal::balanceSteadySolution(system, hand.dofs, *omitted, loads, {}, *solved);
    REQUIRE(wrong.has_value());                                           // computed, not refused
    CHECK_FALSE(wrong->balanced());
    CHECK(wrong->finalState().status == BalanceStatus::OutOfTolerance);
    CHECK(wrong->finalState().reactions.empty());
    CHECK_THAT(wrong->finalState().residual().si(), WithinAbs(10.0, 1e-9));   // the 10 W the omitted boundary removes
    CHECK(wrong->worstNormalizedResidual() > 0.1);
}

TEST_CASE("ThermalBalance_TolerancePolicyConstantsArePinned", "[thermal][balance][tolerance][policy]") {
    // The policy is a measured constant, not an input: pinning its VALUES here means a change to it is a change to a test as well (the
    // tolerance fingerprint of the freeze covers the same three lines).
    CHECK(thermal::kBalanceAbsoluteToleranceW == 1.0e-12);
    CHECK(thermal::kBalanceRelativeTolerance == 1.0e-13);
    CHECK(thermal::kBalanceScaleFloorW == 1.0e-12);
    using thermal::balanceWithinTolerance;
    // The absolute term alone decides when there is no scale at all (a zero-flow body at 0 K).
    CHECK(balanceWithinTolerance(Power::fromSi(5.0e-13), Power{}, Power{}));
    CHECK_FALSE(balanceWithinTolerance(Power::fromSi(2.0e-12), Power{}, Power{}));
    // The relative term alone decides when the scale is large.
    CHECK(balanceWithinTolerance(Power::fromSi(5.0e-8), Power::fromSi(1.0e6), Power{}));
    CHECK_FALSE(balanceWithinTolerance(Power::fromSi(2.0e-6), Power::fromSi(1.0e6), Power{}));
    // The arithmetic scale counts as much as the gross one.
    CHECK(balanceWithinTolerance(Power::fromSi(5.0e-8), Power{}, Power::fromSi(1.0e6)));
    CHECK_FALSE(balanceWithinTolerance(Power::fromSi(2.0e-6), Power{}, Power::fromSi(1.0e6)));
}

TEST_CASE("ThermalBalance_SeamRefusesAMixedUpModeOrConstraintMap", "[thermal][balance][seam][refusals][constraints]") {
    const AsmHand hand = makeAsmHand(unitTetNodes(), {}, {{0, 1, 2, 3}});
    const ThermalSystem steadySystem = assemble(hand, 45.0, std::nullopt, generationHeat(hand, 60.0));
    const ThermalSystem transientSystem = assemble(hand, 45.0, capacityOf(1200.0, 5.0), generationHeat(hand, 60.0));
    const std::vector<ThermalTemperatureConstraint> held{{dofIx(0), Temperature::fromSi(300.0)}};
    const auto constraints = thermal::buildThermalConstraintMap(hand.dofs, held);
    REQUIRE(constraints.has_value());
    const auto steady = thermal::solveSteadyThermalSystem(steadySystem, hand.dofs, *constraints, ThermalSolverSettings{});
    REQUIRE(steady.has_value());
    const std::vector<ThermalLoadContribution> loads{generationContribution(1, 10.0)};

    // A transient system presented with a steady solution: the mode is named, not left to the source comparison.
    const auto wrongMode = thermal::balanceSteadySolution(transientSystem, hand.dofs, *constraints, loads, {}, *steady);
    REQUIRE_FALSE(wrongMode.has_value());
    CHECK(wrongMode.error().problem == BalanceProblem::ModeMismatch);

    // A constraint map that prescribes 310 K where the solution holds 300 K is not the problem that was solved.
    const std::vector<ThermalTemperatureConstraint> other{{dofIx(0), Temperature::fromSi(310.0)}};
    const auto otherMap = thermal::buildThermalConstraintMap(hand.dofs, other);
    REQUIRE(otherMap.has_value());
    const auto wrongTemperature = thermal::balanceSteadySolution(steadySystem, hand.dofs, *otherMap, loads, {}, *steady);
    REQUIRE_FALSE(wrongTemperature.has_value());
    CHECK(wrongTemperature.error().problem == BalanceProblem::SourceMismatch);
    CHECK_THAT(wrongTemperature.error().error.message, ContainsSubstring("does not hold the temperature"));

    // The same for every state of a history: a warming body does not hold 300 K at the first step.
    const std::vector<ThermalTemperatureConstraint> none;
    const auto noConstraints = thermal::buildThermalConstraintMap(hand.dofs, none);
    REQUIRE(noConstraints.has_value());
    const thermal::TransientSettings settings{.timeStep = Time::fromSi(0.5), .endTime = Time::fromSi(1.0),
                                              .output = thermal::TransientOutputPolicy::EveryStep};
    const auto history = thermal::solveTransientThermalSystem(transientSystem, hand.dofs, *noConstraints,
                                                              thermal::UniformInitialTemperature{Temperature::fromSi(300.0)}, settings,
                                                              ThermalSolverSettings{});
    REQUIRE(history.has_value());
    const auto wrongHistory = thermal::balanceTransientSolution(transientSystem, hand.dofs, *constraints, loads, {}, *history);
    REQUIRE_FALSE(wrongHistory.has_value());
    CHECK(wrongHistory.error().problem == BalanceProblem::SourceMismatch);
    CHECK(wrongHistory.error().step == 1);
}

TEST_CASE("ThermalBalance_SparseNodeIdsKeyReactionsByUnknownAndNotByHandle", "[thermal][balance][reaction][sparse-ids]") {
    // Node handles 7, 42, 104 and 9001 are unknowns 0..3 in ascending-handle order. The prescribed node is handle 104, unknown 2: its reaction is
    // keyed by the UNKNOWN, and the arrays have the size of the node count, never of the largest handle.
    const AsmHand hand = makeAsmHand({{7, at(0, 0, 0)}, {42, at(1, 0, 0)}, {104, at(0, 1, 0)}, {9001, at(0, 0, 1)}}, {}, {{0, 1, 2, 3}});
    REQUIRE(hand.dofs.dofCount() == 4);
    REQUIRE(hand.dofs.nodes()[2] == NodeId::fromValue(104));
    const ThermalSystem system = assemble(hand, 45.0, std::nullopt, generationHeat(hand, 60.0));
    const std::vector<ThermalTemperatureConstraint> held{{dofIx(2), Temperature::fromSi(300.0)}};
    const auto constraints = thermal::buildThermalConstraintMap(hand.dofs, held);
    REQUIRE(constraints.has_value());
    const auto solved = thermal::solveSteadyThermalSystem(system, hand.dofs, *constraints, ThermalSolverSettings{});
    REQUIRE(solved.has_value());
    const std::vector<ThermalLoadContribution> loads{generationContribution(1, 10.0)};
    const auto balance = thermal::balanceSteadySolution(system, hand.dofs, *constraints, loads, {}, *solved);
    INFO((balance.has_value() ? std::string{} : balance.error().error.message));
    REQUIRE(balance.has_value());
    const ThermalHeatBalance& b = balance->finalState();
    REQUIRE(b.reactions.size() == 1);
    CHECK(b.reactions[0].dof.value() == 2);                                     // not 104, not 103
    CHECK_THAT(b.reactions[0].heatInto.si(), WithinAbs(-10.0, 1e-9));
    CHECK_THAT(b.residual().si(), WithinAbs(0.0, 1e-9));
    CHECK(b.status == BalanceStatus::Balanced);
    CHECK(balance->describes(hand.dofs));
}
