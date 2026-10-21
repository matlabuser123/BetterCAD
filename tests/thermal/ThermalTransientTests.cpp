// P18-TRANSIENT-001: the transient thermal solve, hand-checked.
//
// WHAT THESE TESTS ARE FOR. A backward-Euler step is `(C/dt + K) T1 = Q + (C/dt) T0`
// with the prescribed unknowns eliminated exactly, and every claim worth making is
// about WHICH numbers enter it:
//
//   - the CONSISTENT capacity is used, not a lumped one (the non-uniform two-unknown
//     oracle gives [310, 320]; a lumped C would give [306, 324]);
//   - the history term is the PREVIOUS state (T0 on the right, T1 on the left), and the
//     prescribed temperatures enter it as already resolved (prescribed dominates at t0);
//   - the time grid lands exactly on t_end, and a shortened last step uses ITS dt;
//   - a pure-Neumann or a disconnected transient model SUCCEEDS where the steady one is
//     refused, because C/dt makes the matrix positive definite and the initial state fixes
//     the level; what IS refused is a region with no capacity at all;
//   - every step is accepted by two independent residuals, so a wrongly BUILT system or a
//     wrongly stepped candidate is refused even when a solver would have called it a success.
//
// EVERY EXPECTED VALUE IS TYPED IN FROM A HAND DERIVATION, comes from a closed form, or
// from the test's own dense elimination (ThermalTransientTestSupport.hpp). Nothing here
// asks the production integrator for the answer it is judged against.
//
// The real-mesh, currentness and lifecycle claims are in ThermalTransientMappedTests.cpp.

#include "ThermalTransientTestSupport.hpp"

#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <bit>
#include <format>
#include <limits>

using namespace trtest;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

/// The two-unknown conduction matrix `[1 -1; -1 1]` and the consistent capacity
/// `[2 1; 1 2]` of the milestone's hard oracle.
[[nodiscard]] Dense2 k2() { return {{1, -1}, {-1, 1}}; }
[[nodiscard]] Dense2 c2() { return {{2, 1}, {1, 2}}; }

[[nodiscard]] Dense2 denseRows(const thermal::ThermalSparseMatrix& m) {
    Dense2 out(m.rows(), std::vector<double>(m.columns(), 0.0));
    for (std::size_t r = 0; r < m.rows(); ++r) {
        for (std::size_t c = 0; c < m.columns(); ++c) {
            out[r][c] = m.coeffSi(r, c);
        }
    }
    return out;
}

/// A three-unknown chain with a heat input in the middle, conductances 2 and 3 and
/// the consistent capacity of two segments of `cell/6 = 1`.
struct Chain3 {
    Dense2 k = chainConductance({2.0, 3.0});
    Dense2 c = chainCapacity(3, 6.0);
    std::vector<double> q{0.0, 1.0, 0.0};
};

[[nodiscard]] std::uint64_t fingerprint(const ThermalTransientAlgebra& run) {
    std::uint64_t hash = 14695981039346656037ULL;
    const auto mix = [&hash](double value) {
        const auto bits = std::bit_cast<std::uint64_t>(value);
        for (int i = 0; i < 8; ++i) {
            hash ^= (bits >> (8 * i)) & 0xFFU;
            hash *= 1099511628211ULL;
        }
    };
    for (const Temperature t : run.samples) {
        mix(t.si());
    }
    for (const thermal::TransientStepRecord& record : run.steps) {
        mix(record.reducedResidual);
        mix(record.physicalResidual);
        mix(record.pivotRatio);
    }
    return hash;
}

} // namespace

// ===========================================================================
// The time grid
// ===========================================================================

TEST_CASE("ThermalTransient_TimeGridExactDivision", "[thermal][transient][grid]") {
    const TimeGrid grid = gridOf(0.25, 1.0);
    REQUIRE(grid.stepCount() == 4);
    CHECK_FALSE(grid.hasPartialFinalStep());
    const double expected[5] = {0.0, 0.25, 0.5, 0.75, 1.0};
    for (std::size_t i = 0; i <= 4; ++i) {
        CHECK(grid.timeAt(i) == expected[i]);
    }
    for (std::size_t i = 0; i < 4; ++i) {
        CHECK(grid.stepAt(i) == 0.25);
    }
    CHECK(grid.endTime() == 1.0);
    CHECK(grid.nominalStep() == 0.25);
    CHECK(grid.times() == std::vector<double>{0.0, 0.25, 0.5, 0.75, 1.0});
}

TEST_CASE("ThermalTransient_TimeGridPartialFinalStep", "[thermal][transient][grid]") {
    // dt = 0.3, t_end = 1.0 -> 0.3, 0.3, 0.3, 0.1: the last step is the shortened remainder and the grid lands on
    // t_end EXACTLY.
    const TimeGrid grid = gridOf(0.3, 1.0);
    REQUIRE(grid.stepCount() == 4);
    CHECK(grid.hasPartialFinalStep());
    CHECK(grid.stepAt(0) == 0.3);
    CHECK(grid.stepAt(1) == 0.3);
    CHECK(grid.stepAt(2) == 0.3);
    CHECK_THAT(grid.stepAt(3), WithinAbs(0.1, 1e-15));
    CHECK(grid.timeAt(4) == 1.0);
    CHECK(grid.endTime() == 1.0);
    CHECK(grid.timeAt(5) == 1.0);  // out of range is the end time
    CHECK(grid.stepAt(4) == 0.0);  // out of range is zero
    double sum = 0.0;
    for (std::size_t i = 0; i < grid.stepCount(); ++i) {
        sum += grid.stepAt(i);
    }
    CHECK_THAT(sum, WithinAbs(1.0, 1e-15));
    // Strictly increasing, never overshooting.
    for (std::size_t i = 0; i < grid.stepCount(); ++i) {
        CHECK(grid.timeAt(i) < grid.timeAt(i + 1));
    }
    WARN("time grid dt = 0.3, t_end = 1.0: 4 steps 0.3, 0.3, 0.3, 0.1; t_N == t_end exactly, final step shortened, none overshot");
}

TEST_CASE("ThermalTransient_TimeGridFloatingPointRatioIsAnInteger", "[thermal][transient][grid]") {
    // 0.3 / 0.1 = 2.9999999999999996 in double precision. Without the tolerance the grid would take two full steps and a
    // 5.5e-17 s remainder.
    REQUIRE(0.3 / 0.1 != 3.0);
    const TimeGrid grid = gridOf(0.1, 0.3);
    CHECK(grid.stepCount() == 3);
    CHECK_FALSE(grid.hasPartialFinalStep());
    for (std::size_t i = 0; i < 3; ++i) {
        CHECK(grid.stepAt(i) == 0.1);
    }
    CHECK(grid.endTime() == 0.3);
    CHECK(grid.timeAt(3) == 0.3);

    // Another classic: 0.7 / 0.1 = 6.999999999999999.
    const TimeGrid seven = gridOf(0.1, 0.7);
    CHECK(seven.stepCount() == 7);
    CHECK_FALSE(seven.hasPartialFinalStep());
    CHECK(gridOf(0.1, 1.0).stepCount() == 10);
    CHECK(gridOf(0.2, 0.6).stepCount() == 3);
}

TEST_CASE("ThermalTransient_TimeGridToleranceBoundary", "[thermal][transient][grid]") {
    // The tolerance is relative, 1e-9 of the step count. Inside it the ratio is an integer; outside it is a remainder.
    const TimeGrid inside = gridOf(1.0, 1000.0 * (1.0 + 0.5e-9));
    CHECK(inside.stepCount() == 1000);
    CHECK_FALSE(inside.hasPartialFinalStep());
    CHECK(inside.endTime() == 1000.0 * (1.0 + 0.5e-9));  // the user's t_end is what is reported

    const TimeGrid outside = gridOf(1.0, 1000.0 * (1.0 + 2.0e-9));
    CHECK(outside.stepCount() == 1001);
    CHECK(outside.hasPartialFinalStep());
    CHECK(outside.stepAt(999) == 1.0);
    CHECK_THAT(outside.stepAt(1000), WithinAbs(2.0e-6, 1e-9));
    CHECK(outside.timeAt(1001) == 1000.0 * (1.0 + 2.0e-9));
}

TEST_CASE("ThermalTransient_TimeGridSaysWhichSpanItIntegrates", "[thermal][transient][grid]") {
    // A shortened last step makes the steps add up to t_end to rounding.
    CHECK_THAT(gridOf(0.3, 1.0).integratedSpan(), WithinAbs(1.0, 1e-15));
    CHECK_THAT(gridOf(0.7, 2.0).integratedSpan(), WithinAbs(2.0, 1e-15));
    // A whole number of steps integrates N dt, which is t_end only to the grid tolerance: reported, not hidden.
    const TimeGrid inside = gridOf(1.0, 1000.0 * (1.0 + 0.5e-9));
    CHECK(inside.integratedSpan() == 1000.0);
    CHECK(inside.endTime() != inside.integratedSpan());
    CHECK(std::abs(inside.endTime() - inside.integratedSpan()) <= thermal::kTimeGridTolerance * inside.endTime());
    CHECK_THAT(gridOf(0.1, 0.3).integratedSpan(), WithinAbs(0.3, 1e-15));
    CHECK(gridOf(1.0, 0.0).integratedSpan() == 0.0);
    CHECK(gridOf(2.0, 1.0).integratedSpan() == 1.0);
}

TEST_CASE("ThermalTransient_TimeGridMultipliesAndNeverAccumulates", "[thermal][transient][grid]") {
    // t_k = k * dt, exactly the product, so a long grid carries no accumulated rounding. A running sum of 0.1 would reach
    // 9999.999999999 after 1e5 additions.
    const TimeGrid grid = gridOf(0.1, 10000.0);
    REQUIRE(grid.stepCount() == 100000);
    for (std::size_t k : {std::size_t{1}, std::size_t{3}, std::size_t{7}, std::size_t{12345}, std::size_t{99999}}) {
        CHECK(grid.timeAt(k) == static_cast<double>(k) * 0.1);
    }
    CHECK(grid.timeAt(100000) == 10000.0);
    double previous = -1.0;
    for (std::size_t k = 0; k <= grid.stepCount(); ++k) {
        const double t = grid.timeAt(k);
        REQUIRE(t > previous);
        previous = t;
    }
    double running = 0.0;
    for (std::size_t k = 0; k < 100000; ++k) {
        running += 0.1;
    }
    CHECK(running != 10000.0);  // what a running sum would have produced
}

TEST_CASE("ThermalTransient_TimeGridStepLongerThanTheSpanAndZeroSpan", "[thermal][transient][grid]") {
    // dt > t_end: one step of t_end (the canonical settings forbid it, the grid is more permissive so it can be tested).
    const TimeGrid one = gridOf(2.0, 1.0);
    CHECK(one.stepCount() == 1);
    CHECK(one.hasPartialFinalStep());
    CHECK(one.stepAt(0) == 1.0);
    CHECK(one.timeAt(1) == 1.0);
    // dt just over t_end, inside the tolerance: one full step.
    const TimeGrid near = gridOf(1.0 + 1e-12, 1.0);
    CHECK(near.stepCount() == 1);
    CHECK_FALSE(near.hasPartialFinalStep());
    // t_end = 0: one time and no step.
    const TimeGrid zero = gridOf(1.0, 0.0);
    CHECK(zero.stepCount() == 0);
    CHECK(zero.times() == std::vector<double>{0.0});
    CHECK(zero.endTime() == 0.0);
    CHECK(zero.timeAt(0) == 0.0);
}

TEST_CASE("ThermalTransient_TimeGridRefusesInvalidInput", "[thermal][transient][grid]") {
    const double bad[] = {0.0, -1.0, kNaN, kInf, -kInf};
    for (const double dt : bad) {
        const auto grid = TimeGrid::create(dt, 1.0);
        REQUIRE_FALSE(grid.has_value());
        CHECK(grid.error().problem == TransientProblem::InvalidTimeStep);
        CHECK(grid.error().step == 0);
    }
    for (const double end : {-1.0, kNaN, kInf, -kInf}) {
        const auto grid = TimeGrid::create(1.0, end);
        REQUIRE_FALSE(grid.has_value());
        CHECK(grid.error().problem == TransientProblem::InvalidEndTime);
    }
}

TEST_CASE("ThermalTransient_TimeGridResourceGuardNeverEnlargesTheStep", "[thermal][transient][grid]") {
    const auto tooMany = TimeGrid::create(1.0, 2.0e6);
    REQUIRE_FALSE(tooMany.has_value());
    CHECK(tooMany.error().problem == TransientProblem::TooManyTimeSteps);
    CHECK_THAT(tooMany.error().error.message, ContainsSubstring("not enlarged"));
    // The quotient overflowing to infinity is refused the same way.
    const auto overflow = TimeGrid::create(1.0e-300, 1.0e300);
    REQUIRE_FALSE(overflow.has_value());
    CHECK(overflow.error().problem == TransientProblem::TooManyTimeSteps);
    // Just over the limit, with a remainder.
    CHECK_FALSE(TimeGrid::create(1.0, static_cast<double>(thermal::kMaxTimeSteps) + 0.5).has_value());
    // At the limit it is fine, and costs nothing until a step is taken.
    const auto atLimit = TimeGrid::create(1.0, static_cast<double>(thermal::kMaxTimeSteps));
    REQUIRE(atLimit.has_value());
    CHECK(atLimit->stepCount() == thermal::kMaxTimeSteps);
    CHECK(atLimit->endTime() == static_cast<double>(thermal::kMaxTimeSteps));
}

TEST_CASE("ThermalTransient_TimeGridIsDeterministic", "[thermal][transient][grid][determinism]") {
    for (int i = 0; i < 20; ++i) {
        CHECK(gridOf(0.3, 1.0) == gridOf(0.3, 1.0));
    }
    CHECK_FALSE(gridOf(0.3, 1.0) == gridOf(0.25, 1.0));
    CHECK_FALSE(gridOf(0.3, 1.0) == gridOf(0.3, 1.1));
    const TimeGrid a = gridOf(0.3, 1.0);
    const TimeGrid copy = a;
    CHECK(copy == a);
    CHECK(copy.times() == a.times());
}

// ===========================================================================
// The effective matrix  A = C/dt + K
// ===========================================================================

TEST_CASE("ThermalTransient_EffectiveMatrixIsTheUnionOfThePatterns", "[thermal][transient][effective]") {
    // K has (1,2) and (2,1) that C lacks; C has (0,2) and (2,0) that K lacks.
    const Dense2 dk{{2, -1, 0}, {-1, 2, -1}, {0, -1, 1}};
    const Dense2 dc{{1, 0.5, 0.25}, {0.5, 1, 0}, {0.25, 0, 1}};
    const thermal::ConductanceMatrix k{sparseOf(dk)};
    const thermal::CapacityMatrix c{sparseOf(dc)};
    const thermal::ConductanceMatrix kBefore = k;
    const thermal::CapacityMatrix cBefore = c;

    const auto effective = thermal::effectiveMatrix(k, c, 0.5);
    REQUIRE(effective.has_value());
    for (std::size_t r = 0; r < 3; ++r) {
        for (std::size_t col = 0; col < 3; ++col) {
            CHECK(effective->coeffSi(r, col) == dc[r][col] / 0.5 + dk[r][col]);
        }
    }
    CHECK(effective->nonZeros() == 9);
    // Ascending columns within each row: the CSR invariant.
    for (std::size_t r = 0; r < 3; ++r) {
        for (auto slot = effective->rowStart()[r] + 1; slot < effective->rowStart()[r + 1]; ++slot) {
            CHECK(effective->innerIndices()[slot - 1] < effective->innerIndices()[slot]);
        }
    }
    // The inputs are READ and never written.
    CHECK(k == kBefore);
    CHECK(c == cBefore);
}

TEST_CASE("ThermalTransient_EffectiveMatrixIsExactlyCOverDtPlusK", "[thermal][transient][effective]") {
    // The one-entry case is the formula: 10 / 4 + 2 = 4.5. A C * dt would give 42.
    const auto effective = thermal::effectiveMatrix(thermal::ConductanceMatrix{sparseOf({{2.0}})}, thermal::CapacityMatrix{sparseOf({{10.0}})}, 4.0);
    REQUIRE(effective.has_value());
    CHECK(effective->coeffSi(0, 0) == 4.5);
    // A pattern with no overlap at all.
    const auto disjoint = thermal::effectiveMatrix(thermal::ConductanceMatrix{sparseOf({{1, 0}, {0, 0}})}, thermal::CapacityMatrix{sparseOf({{0, 0}, {0, 3}})}, 1.5);
    REQUIRE(disjoint.has_value());
    CHECK(disjoint->coeffSi(0, 0) == 1.0);
    CHECK(disjoint->coeffSi(1, 1) == 2.0);
}

TEST_CASE("ThermalTransient_EffectiveMatrixRefusals", "[thermal][transient][effective]") {
    const thermal::ConductanceMatrix k{sparseOf(k2())};
    const thermal::CapacityMatrix c{sparseOf(c2())};
    for (const double dt : {0.0, -1.0, kNaN, kInf}) {
        const auto refused = thermal::effectiveMatrix(k, c, dt);
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().problem == TransientProblem::InvalidTimeStep);
    }
    // Two matrices of different sizes.
    const auto mismatch = thermal::effectiveMatrix(k, thermal::CapacityMatrix{sparseOf({{1, 0, 0}, {0, 1, 0}, {0, 0, 1}})}, 1.0);
    REQUIRE_FALSE(mismatch.has_value());
    CHECK(mismatch.error().problem == TransientProblem::DofMapMismatch);
    // C / dt overflowing is refused, not clamped to infinity.
    const auto overflow = thermal::effectiveMatrix(k, thermal::CapacityMatrix{sparseOf({{1.0e308, 0}, {0, 1.0e308}})}, 1.0e-10);
    REQUIRE_FALSE(overflow.has_value());
    CHECK(overflow.error().problem == TransientProblem::NonFiniteInput);
    // An asymmetric capacity makes an asymmetric effective matrix; it is refused, not symmetrised.
    const auto asymmetric = thermal::effectiveMatrix(k, thermal::CapacityMatrix{sparseOf({{1, 2}, {0, 1}})}, 1.0);
    REQUIRE_FALSE(asymmetric.has_value());
    CHECK(asymmetric.error().problem == TransientProblem::InconsistentSystem);
}

// ===========================================================================
// The hard oracles
// ===========================================================================

TEST_CASE("ThermalTransient_OneDofHandOracle", "[thermal][transient][oracle]") {
    // C = 10 J/K, G = 2 W/K to an ambient at 300 K, T0 = 400 K, dt = 1 s:
    //   (10/1 + 2) T1 = 2*300 + (10/1)*400   ->   T1 = 4600/12 = 383.3333...
    const TrAlgebra a = makeTr({{2.0}}, {{10.0}}, {600.0});
    const auto init = stateOf({400.0});
    const ThermalTransientAlgebra run = mustRunTr(a, init, 1.0, 1.0);
    REQUIRE(run.samples.size() == 2);
    CHECK_THAT(run.samples[0].si(), WithinAbs(400.0, 0.0));
    CHECK_THAT(run.samples[1].si(), WithinAbs(4600.0 / 12.0, 1e-12));
    CHECK_THAT(run.samples[1].si(), WithinAbs(383.33333333333333, 1e-10));

    // And n steps: T_n - 300 = 100 (10/12)^n, the discrete geometric decay.
    const ThermalTransientAlgebra five = mustRunTr(a, init, 1.0, 5.0);
    double expected = 100.0;
    for (std::size_t n = 1; n <= 5; ++n) {
        expected *= 10.0 / 12.0;
        CHECK_THAT(five.samples[n].si(), WithinAbs(300.0 + expected, 1e-10));
    }
    WARN("one-DOF oracle: C = 10, G = 2, T_inf = 300, T0 = 400, dt = 1 -> T1 = 4600/12 = 383.333333333333 K");
}

TEST_CASE("ThermalTransient_NonUniformTwoUnknownConsistentCapacityOracle", "[thermal][transient][oracle][consistent]") {
    // C = [2 1; 1 2], K = [1 -1; -1 1], dt = 1, Q = 0, T0 = [300, 330]:
    //   A = C/dt + K = [3 0; 0 3]        b = C T0 = [930, 960]        T1 = [310, 320]
    const TrAlgebra a = makeTr(k2(), c2(), {0.0, 0.0});
    const ThermalTransientAlgebra run = mustRunTr(a, stateOf({300.0, 330.0}), 1.0, 1.0);
    const std::vector<double> t1 = sampleOf(run, 1);
    CHECK_THAT(t1[0], WithinAbs(310.0, 1e-11));
    CHECK_THAT(t1[1], WithinAbs(320.0, 1e-11));

    // A LUMPED capacity (row sums 3, 3) gives [306, 324] instead -- computed here by hand from
    // [4 -1; -1 4] T1 = [900, 990] -- so the oracle distinguishes the two by 4 K.
    CHECK(std::abs(t1[0] - 306.0) > 3.9);
    CHECK(std::abs(t1[1] - 324.0) > 3.9);
    // The mean is conserved (K 1 = 0, Q = 0), the antisymmetric part decays by 1/(1 + 2 dt) = 1/3 per step.
    CHECK_THAT(t1[0] + t1[1], WithinAbs(630.0, 1e-10));
    const ThermalTransientAlgebra three = mustRunTr(a, stateOf({300.0, 330.0}), 1.0, 3.0);
    for (std::size_t n = 0; n <= 3; ++n) {
        const double anti = 15.0 / std::pow(3.0, static_cast<double>(n));
        const std::vector<double> t = sampleOf(three, n);
        CHECK_THAT(t[0], WithinAbs(315.0 - anti, 1e-10));
        CHECK_THAT(t[1], WithinAbs(315.0 + anti, 1e-10));
    }
    WARN("non-uniform 2x2 oracle: C = [2 1; 1 2], K = [1 -1; -1 1], dt = 1, T0 = [300, 330] -> T1 = [310, 320] (a lumped C gives [306, 324])");
}

// ===========================================================================
// Singularity rules: steady is not transient
// ===========================================================================

TEST_CASE("ThermalTransient_PureNeumannTransientSucceedsWhereSteadyIsRefused", "[thermal][transient][neumann][singularity]") {
    const TrAlgebra a = makeTr(k2(), c2(), {10.0, 0.0});
    // The STEADY solve of the same K is refused: the constant temperature is in its null space.
    const auto steady = solveAlgebra(a.base);
    REQUIRE_FALSE(steady.has_value());
    CHECK(steady.error().problem == SteadySolveProblem::MissingTemperatureDatum);

    // The TRANSIENT solve is not: C/dt makes the matrix positive definite and T0 fixes the level.
    auto run = runTr(a, stateOf({300.0, 330.0}), 1.0, 4.0);
    INFO((run.has_value() ? std::string{} : run.error().error.message));
    REQUIRE(run.has_value());
    CHECK(run->steps.size() == 4);
    // The stored energy grows by dt * sum(Q) = 10 J every second, exactly: 1^T K = 0, so the conduction cancels.
    for (std::size_t n = 1; n <= 4; ++n) {
        const double before = storedEnergy(c2(), sampleOf(*run, n - 1));
        const double after = storedEnergy(c2(), sampleOf(*run, n));
        CHECK_THAT(after - before, WithinAbs(10.0, 1e-8));
    }
    // And it agrees with the dense oracle.
    const auto history = denseHistory(k2(), c2(), {10.0, 0.0}, {300.0, 330.0}, {1.0, 1.0, 1.0, 1.0}, {});
    for (std::size_t n = 0; n <= 4; ++n) {
        CHECK(worstDifference(sampleOf(*run, n), history[n]) <= 1e-9);
    }
    WARN("pure-Neumann: steady solve refused (missing_temperature_datum); transient solve accepted, energy +10 J per 1 s step");
}

TEST_CASE("ThermalTransient_DisconnectedPositiveCapacityRegionsSucceed", "[thermal][transient][neumann][singularity]") {
    // Two unconnected bodies: {0,1} with K = [1 -1; -1 1], C = [2 1; 1 2]; {2,3} with k = 3 and a diagonal capacity of 1.
    const Dense2 dk{{1, -1, 0, 0}, {-1, 1, 0, 0}, {0, 0, 3, -3}, {0, 0, -3, 3}};
    const Dense2 dc{{2, 1, 0, 0}, {1, 2, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
    const std::vector<double> q{0.0, 0.0, 5.0, 0.0};
    const std::vector<double> t0{300.0, 330.0, 350.0, 350.0};

    SECTION("nothing prescribed: every region floats, and the transient solve does not care") {
        const TrAlgebra a = makeTr(dk, dc, q);
        const auto steady = solveAlgebra(a.base);
        REQUIRE_FALSE(steady.has_value());
        CHECK(steady.error().problem == SteadySolveProblem::MissingTemperatureDatum);
        const ThermalTransientAlgebra run = mustRunTr(a, stateOf(t0), 0.5, 3.0);
        const auto history = denseHistory(dk, dc, q, t0, std::vector<double>(6, 0.5), {});
        for (std::size_t n = 0; n <= 6; ++n) {
            CHECK(worstDifference(sampleOf(run, n), history[n]) <= 1e-9);
        }
        // Region 1 is untouched by region 2's heat: its mean is exactly constant.
        for (std::size_t n = 0; n <= 6; ++n) {
            const std::vector<double> t = sampleOf(run, n);
            CHECK_THAT(t[0] + t[1], WithinAbs(630.0, 1e-8));
        }
        // Region 2 heats at 5 W into a capacity of 2 J/K: +2.5 K/s of mean temperature, +7.5 K over 3 s.
        const std::vector<double> last = sampleOf(run, 6);
        CHECK_THAT((last[2] + last[3]) / 2.0, WithinAbs(350.0 + 5.0 * 3.0 / 2.0, 1e-8));
    }
    SECTION("one region anchored, one floating: steady is refused as an unanchored region, transient is not") {
        const TrAlgebra a = makeTr(dk, dc, q, {{0, 300.0}});
        const auto steady = solveAlgebra(a.base);
        REQUIRE_FALSE(steady.has_value());
        CHECK(steady.error().problem == SteadySolveProblem::UnanchoredThermalRegion);
        const ThermalTransientAlgebra run = mustRunTr(a, stateOf(t0), 0.5, 3.0);
        const auto history = denseHistory(dk, dc, q, t0, std::vector<double>(6, 0.5), {{0, 300.0}});
        for (std::size_t n = 0; n <= 6; ++n) {
            CHECK(worstDifference(sampleOf(run, n), history[n]) <= 1e-9);
            CHECK(sampleOf(run, n)[0] == 300.0);
        }
    }
}

TEST_CASE("ThermalTransient_RegionWithNoCapacityAtAllIsRefused", "[thermal][transient][singularity][capacity]") {
    // Unknown 2 has no conduction and no capacity: its row of A = C/dt + K is zero. That is a real singularity, found by
    // the factorisation and refused; nothing is added to the diagonal.
    const Dense2 dk{{1, -1, 0}, {-1, 1, 0}, {0, 0, 0}};
    const Dense2 dc{{1, 0, 0}, {0, 1, 0}, {0, 0, 0}};
    const TrAlgebra a = makeTr(dk, dc, {0.0, 0.0, 0.0});
    const auto run = runTr(a, uniformState(3, 300.0), 1.0, 2.0);
    REQUIRE_FALSE(run.has_value());
    CHECK(run.error().problem == TransientProblem::SingularSystem);
    CHECK(run.error().step == 1);
    CHECK(run.error().time == 1.0);
    CHECK_THAT(run.error().error.message, ContainsSubstring("capacity"));

    // A capacity of 1e-30 is no better: the pivot ratio is far below the floor.
    const Dense2 tiny{{1, 0, 0}, {0, 1, 0}, {0, 0, 1e-30}};
    const auto tinyRun = runTr(makeTr(dk, tiny, {0.0, 0.0, 0.0}), uniformState(3, 300.0), 1.0, 2.0);
    REQUIRE_FALSE(tinyRun.has_value());
    CHECK(tinyRun.error().problem == TransientProblem::SingularSystem);
}

TEST_CASE("ThermalTransient_ZeroCapacityConnectedByConductionSolves", "[thermal][transient][capacity]") {
    // Unknown 1 has no capacity but conducts to unknown 0: A = [2 -1; -1 1] is positive definite, the node simply follows
    // its neighbour (a quasi-steady node). Zero capacity is not an error by itself; an isolated one is.
    const TrAlgebra a = makeTr(k2(), {{1, 0}, {0, 0}}, {0.0, 0.0});
    const ThermalTransientAlgebra run = mustRunTr(a, stateOf({300.0, 500.0}), 1.0, 1.0);
    const std::vector<double> t1 = sampleOf(run, 1);
    // [2 -1; -1 1]^-1 = [1 1; 1 2];  b = [C00*300, 0] = [300, 0]  ->  T1 = [300, 300].
    CHECK_THAT(t1[0], WithinAbs(300.0, 1e-10));
    CHECK_THAT(t1[1], WithinAbs(300.0, 1e-10));
}

// ===========================================================================
// Prescribed temperatures
// ===========================================================================

TEST_CASE("ThermalTransient_NonzeroDirichletMatchesDenseOracle", "[thermal][transient][dirichlet]") {
    const Chain3 chain;
    const std::vector<std::vector<std::pair<std::size_t, double>>> sets{
        {{0, 400.0}}, {{1, 350.0}}, {{2, 250.0}}, {{0, 400.0}, {2, 250.0}}, {{1, 350.0}, {2, 250.0}}};
    for (const auto& prescribed : sets) {
        CAPTURE(prescribed.size(), prescribed.front().first);
        const TrAlgebra a = makeTr(chain.k, chain.c, chain.q, prescribed);
        const ThermalTransientAlgebra run = mustRunTr(a, uniformState(3, 300.0), 0.5, 3.0);
        const auto history = denseHistory(chain.k, chain.c, chain.q, {300.0, 300.0, 300.0}, std::vector<double>(6, 0.5), prescribed);
        REQUIRE(run.samples.size() == 7 * 3);
        for (std::size_t n = 0; n <= 6; ++n) {
            CHECK(worstDifference(sampleOf(run, n), history[n]) <= 1e-9);
            // The prescribed unknowns hold their value EXACTLY at every state, from the first.
            for (const auto& [dof, kelvin] : prescribed) {
                CHECK(sampleOf(run, n)[dof] == kelvin);
            }
        }
        CHECK(run.initialOverrides == prescribed.size());
        CHECK(run.freeEquations == 3 - prescribed.size());
    }
}

TEST_CASE("ThermalTransient_PrescribedDominatesTheInitialTemperatureAtTimeZero", "[thermal][transient][dirichlet][initial]") {
    // K = [1 -1; -1 1], C = [2 1; 1 2], unknown 0 prescribed at 400, initial 300 everywhere, dt = 1, one step.
    //   A_ff = 3,  A_fc = C10/dt + K10 = 1 - 1 = 0,  b_f = Q1 + C10*T0_0 + C11*T0_1 = 1*400 + 2*300 = 1000
    //   T1_1 = 1000/3.
    // History taken from the UNRESOLVED initial state (T0_0 = 300) would give 900/3 = 300.
    const TrAlgebra a = makeTr(k2(), c2(), {0.0, 0.0}, {{0, 400.0}});
    const ThermalTransientAlgebra run = mustRunTr(a, uniformState(2, 300.0), 1.0, 1.0);
    CHECK(run.initial[0].si() == 400.0);
    CHECK(run.initial[1].si() == 300.0);
    CHECK(run.initialOverrides == 1);
    CHECK(run.samples[0].si() == 400.0);  // the first stored state already holds the prescription
    CHECK_THAT(run.samples[3].si(), WithinAbs(1000.0 / 3.0, 1e-10));
    CHECK(std::abs(run.samples[3].si() - 300.0) > 30.0);

    // An initial temperature that already equals the prescription overrides nothing.
    const ThermalTransientAlgebra same = mustRunTr(a, stateOf({400.0, 300.0}), 1.0, 1.0);
    CHECK(same.initialOverrides == 0);
    CHECK(same.samples == run.samples);
    WARN("prescribed dominates at t0: initial 300 K, prescribed 400 K -> first state holds 400 K, 1 override counted, T1(free) = 1000/3");
}

TEST_CASE("ThermalTransient_FullyPrescribedModelSucceedsWithoutASolve", "[thermal][transient][dirichlet]") {
    const TrAlgebra a = makeTr(k2(), c2(), {7.0, -3.0}, {{0, 300.0}, {1, 350.0}});
    const ThermalTransientAlgebra run = mustRunTr(a, uniformState(2, 100.0), 0.5, 1.5);
    CHECK(run.freeEquations == 0);
    CHECK(run.kernelSolves == 0);
    CHECK(run.initialOverrides == 2);
    REQUIRE(run.steps.size() == 3);
    for (const thermal::TransientStepRecord& record : run.steps) {
        CHECK_FALSE(record.kernelInvoked);
        CHECK(record.reducedResidual == 0.0);
        CHECK(record.physicalResidual == 0.0);
        CHECK(record.pivotRatio == 1.0);
    }
    for (std::size_t n = 0; n <= 3; ++n) {
        CHECK(sampleOf(run, n) == std::vector<double>{300.0, 350.0});
    }
}

// ===========================================================================
// Time stepping
// ===========================================================================

TEST_CASE("ThermalTransient_LastPartialStepUsesItsOwnTimeStep", "[thermal][transient][grid][oracle]") {
    // C = 10, G = 2, Q = 600, T0 = 400; dt = 0.3, t_end = 1.0 -> steps 0.3, 0.3, 0.3, 0.1.
    const TrAlgebra a = makeTr({{2.0}}, {{10.0}}, {600.0});
    const ThermalTransientAlgebra run = mustRunTr(a, stateOf({400.0}), 0.3, 1.0);
    const TimeGrid grid = gridOf(0.3, 1.0);
    REQUIRE(run.samples.size() == 5);
    double t = 400.0;
    for (std::size_t i = 0; i < 4; ++i) {
        const double dt = grid.stepAt(i);
        t = (600.0 + 10.0 / dt * t) / (10.0 / dt + 2.0);
        CHECK_THAT(run.samples[i + 1].si(), WithinAbs(t, 1e-10));
    }
    // The same four steps with the nominal 0.3 for the last one would land elsewhere.
    double wrong = 400.0;
    for (std::size_t i = 0; i < 4; ++i) {
        wrong = (600.0 + 10.0 / 0.3 * wrong) / (10.0 / 0.3 + 2.0);
    }
    CHECK(std::abs(wrong - run.samples[4].si()) > 0.5);
    CHECK(run.sampleTimes.back().si() == 1.0);
    CHECK(run.sampleTimes[3].si() == 0.3 * 3.0);
}

TEST_CASE("ThermalTransient_OutputPoliciesStoreWhatTheyPromise", "[thermal][transient][output]") {
    const Chain3 chain;
    const TrAlgebra a = makeTr(chain.k, chain.c, chain.q, {{0, 400.0}});
    const auto init = uniformState(3, 300.0);

    const ThermalTransientAlgebra every = mustRunTr(a, init, 0.5, 2.0, TransientOutputPolicy::EveryStep);
    REQUIRE(every.sampleTimes.size() == 5);
    CHECK(every.samples.size() == 5 * 3);
    for (std::size_t i = 0; i < 5; ++i) {
        CHECK(every.sampleTimes[i].si() == static_cast<double>(i) * 0.5);
    }
    CHECK(every.sampleTimes.back().si() == 2.0);
    CHECK(sampleOf(every, 0) == std::vector<double>{400.0, 300.0, 300.0});  // the RESOLVED initial state
    CHECK(every.steps.size() == 4);

    const ThermalTransientAlgebra final = mustRunTr(a, init, 0.5, 2.0, TransientOutputPolicy::FinalOnly);
    REQUIRE(final.sampleTimes.size() == 1);
    CHECK(final.sampleTimes[0].si() == 2.0);
    CHECK(final.samples.size() == 3);
    // The final state is the same to the last bit, whatever was kept in between.
    CHECK(sampleOf(final, 0) == sampleOf(every, 4));
    CHECK(final.steps.size() == 4);  // the evidence is kept for every step either way

    // A grid of no steps: the only state is the initial one.
    const ThermalTransientAlgebra none = mustRunTr(a, init, 1.0, 0.0, TransientOutputPolicy::FinalOnly);
    REQUIRE(none.sampleTimes.size() == 1);
    CHECK(none.sampleTimes[0].si() == 0.0);
    CHECK(sampleOf(none, 0) == std::vector<double>{400.0, 300.0, 300.0});
    CHECK(none.steps.empty());
    CHECK(none.kernelSolves == 0);
    const ThermalTransientAlgebra noneEvery = mustRunTr(a, init, 1.0, 0.0, TransientOutputPolicy::EveryStep);
    CHECK(noneEvery.sampleTimes.size() == 1);
}

TEST_CASE("ThermalTransient_EveryStepEvidenceIsRecordedPerStep", "[thermal][transient][evidence]") {
    const Chain3 chain;
    const TrAlgebra a = makeTr(chain.k, chain.c, chain.q, {{0, 400.0}});
    const ThermalTransientAlgebra run = mustRunTr(a, uniformState(3, 300.0), 0.5, 2.0);
    REQUIRE(run.steps.size() == 4);
    CHECK(run.kernelSolves == 4);
    for (std::size_t i = 0; i < run.steps.size(); ++i) {
        const thermal::TransientStepRecord& record = run.steps[i];
        CHECK(record.index == i + 1);
        CHECK(record.kernelInvoked);
        CHECK(record.reducedResidual >= 0.0);
        CHECK(record.reducedResidual <= 1e-9);
        CHECK(record.physicalResidual <= 1e-9);
        CHECK(record.pivotRatio > 0.0);
        CHECK(record.pivotRatio <= 1.0);
    }
    // The recorded residuals ARE the acceptance step's: recomputed here from the stored states, they agree to the last bit, and a
    // run that skipped the acceptance (or recorded zeros) could not.
    for (std::size_t i = 0; i < run.steps.size(); ++i) {
        const auto again = thermal::validateTransientStep(
            a.k, a.c, a.base.q, a.base.equations, a.base.constraints,
            std::span<const Temperature>(run.samples).subspan(i * 3, 3), std::span<const Temperature>(run.samples).subspan((i + 1) * 3, 3),
            0.5, ThermalSolverSettings{});
        REQUIRE(again.has_value());
        CHECK(run.steps[i].reducedResidual == again->reduced);
        CHECK(run.steps[i].physicalResidual == again->physical);
        CHECK(run.steps[i].reducedResidual > 0.0);   // an honest residual is rounding noise, not an exact zero
        CHECK(run.steps[i].physicalResidual > 0.0);
    }
}

// ===========================================================================
// Refusals
// ===========================================================================

TEST_CASE("ThermalTransient_AlgebraRefusals", "[thermal][transient][refusals]") {
    const Chain3 chain;
    const TrAlgebra a = makeTr(chain.k, chain.c, chain.q, {{0, 400.0}});
    const auto init = uniformState(3, 300.0);

    SECTION("invalid solver settings") {
        for (const ThermalSolverSettings& bad :
             {ThermalSolverSettings{.relativeResidualTolerance = 0.0}, ThermalSolverSettings{.relativeResidualTolerance = kNaN},
              ThermalSolverSettings{.pivotFloor = -1.0}, ThermalSolverSettings{.pivotFloor = kInf}}) {
            CHECK(trProblemOf(runTr(a, init, 1.0, 1.0, TransientOutputPolicy::EveryStep, bad)) == TransientProblem::InvalidSettings);
        }
    }
    SECTION("an output policy this build does not know") {
        CHECK(trProblemOf(runTr(a, init, 1.0, 1.0, static_cast<TransientOutputPolicy>(7))) == TransientProblem::InvalidOutputPolicy);
    }
    SECTION("an initial temperature that is not a finite absolute temperature") {
        for (const double bad : {kNaN, kInf, -kInf, -1.0, -0.001}) {
            std::vector<Temperature> start = init;
            start[1] = Temperature::fromSi(bad);
            const auto run = runTr(a, start, 1.0, 1.0);
            REQUIRE_FALSE(run.has_value());
            CHECK(run.error().problem == TransientProblem::InvalidInitialTemperature);
            CHECK_THAT(run.error().error.message, ContainsSubstring("unknown 1"));
        }
        // 0 K is an absolute temperature.
        std::vector<Temperature> zero = init;
        zero[1] = Temperature::fromSi(0.0);
        CHECK(runTr(a, zero, 1.0, 1.0).has_value());
    }
    SECTION("a vector or matrix of the wrong size") {
        CHECK(trProblemOf(runTr(a, uniformState(2, 300.0), 1.0, 1.0)) == TransientProblem::DofMapMismatch);
        const TrAlgebra badK = makeTr(k2(), chain.c, {0.0, 0.0});
        CHECK(trProblemOf(thermal::solveThermalTransientAlgebra(badK.k, badK.c, a.base.q, a.base.equations,
                                                                a.base.constraints, init, gridOf(1.0, 1.0),
                                                                TransientOutputPolicy::EveryStep, {})) == TransientProblem::DofMapMismatch);
        CHECK(trProblemOf(thermal::solveThermalTransientAlgebra(a.k, thermal::CapacityMatrix{sparseOf(c2())}, a.base.q, a.base.equations,
                                                                a.base.constraints, init, gridOf(1.0, 1.0),
                                                                TransientOutputPolicy::EveryStep, {})) == TransientProblem::DofMapMismatch);
        CHECK(trProblemOf(thermal::solveThermalTransientAlgebra(a.k, a.c, heatOf({0.0, 1.0}), a.base.equations,
                                                                a.base.constraints, init, gridOf(1.0, 1.0),
                                                                TransientOutputPolicy::EveryStep, {})) == TransientProblem::DofMapMismatch);
    }
    SECTION("constraints that are not the complement of the free numbering") {
        const TrAlgebra other = makeTr(chain.k, chain.c, chain.q, {{2, 250.0}});
        CHECK(trProblemOf(thermal::solveThermalTransientAlgebra(a.k, a.c, a.base.q, a.base.equations,
                                                                other.base.constraints, init, gridOf(1.0, 1.0),
                                                                TransientOutputPolicy::EveryStep, {})) == TransientProblem::ConstraintMismatch);
    }
    SECTION("a history that would not fit in memory is refused before the first step") {
        // 51 unknowns over a million steps is 51 million stored values, over the limit of 50 million.
        const Dense2 identity = [] {
            Dense2 d(51, std::vector<double>(51, 0.0));
            for (std::size_t i = 0; i < 51; ++i) {
                d[i][i] = 1.0;
            }
            return d;
        }();
        const TrAlgebra big = makeTr(identity, identity, std::vector<double>(51, 0.0));
        const auto refused = runTr(big, uniformState(51, 300.0), 1.0, static_cast<double>(thermal::kMaxTimeSteps));
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().problem == TransientProblem::HistoryTooLarge);
        CHECK(refused.error().step == 0);
        CHECK_THAT(refused.error().error.message, ContainsSubstring("final-only"));
    }
}

TEST_CASE("ThermalTransient_TheUsersSolverSettingsReachTheFactorisation", "[thermal][transient][settings]") {
    // The pivot floor and the residual tolerance are the USER'S canonical numbers (ThermalSolverSettings), and the integrator hands
    // both to the kernel. A floor of 0.999999 refuses a chain whose pivots differ by far more than that; the defaults accept it.
    const Chain3 chain;
    const TrAlgebra a = makeTr(chain.k, chain.c, chain.q, {{0, 400.0}});
    const auto init = uniformState(3, 300.0);
    const ThermalTransientAlgebra ok = mustRunTr(a, init, 0.5, 1.0);
    REQUIRE_FALSE(ok.steps.empty());
    CHECK(ok.steps[0].pivotRatio < 0.9);   // the chain's real pivot spread
    const auto strict = runTr(a, init, 0.5, 1.0, TransientOutputPolicy::EveryStep, ThermalSolverSettings{.pivotFloor = 0.999999});
    REQUIRE_FALSE(strict.has_value());
    CHECK(strict.error().problem == TransientProblem::SingularSystem);
    CHECK(strict.error().step == 1);
    CHECK_THAT(strict.error().error.message, ContainsSubstring("pivot floor"));
    // A tolerance no double-precision solve can meet is refused at the first step as a residual failure, not accepted.
    const auto impossible =
        runTr(a, init, 0.5, 1.0, TransientOutputPolicy::EveryStep, ThermalSolverSettings{.relativeResidualTolerance = 1.0e-30});
    REQUIRE_FALSE(impossible.has_value());
    CHECK(impossible.error().problem == TransientProblem::ResidualTooLarge);
    CHECK(impossible.error().step == 1);
    // And a tolerance loosened to 1 accepts it.
    CHECK(runTr(a, init, 0.5, 1.0, TransientOutputPolicy::EveryStep, ThermalSolverSettings{.relativeResidualTolerance = 1.0}).has_value());
}

TEST_CASE("ThermalTransient_AStepThatCannotBeTakenFailsTheRunWithTheStepAndTime", "[thermal][transient][refusals][atomic]") {
    // Capacity so large that C/dt overflows in step 1: the failure names step 1 and the time at its end, and the run
    // returns no history at all (no partial result).
    const TrAlgebra a = makeTr({{1.0}}, {{1.0e308}}, {0.0});
    const auto run = runTr(a, uniformState(1, 300.0), 1.0e-5, 1.0e-5);
    REQUIRE_FALSE(run.has_value());
    CHECK(run.error().problem == TransientProblem::NonFiniteInput);
    CHECK(run.error().step == 1);
    CHECK(run.error().time == 1.0e-5);
}

// ===========================================================================
// The step acceptance
// ===========================================================================

namespace {

struct Candidate {
    Chain3 chain;
    TrAlgebra a;
    std::vector<Temperature> previous;
    std::vector<Temperature> next;
    double dt = 0.5;

    Candidate()
        : a(makeTr(chain.k, chain.c, chain.q, {{0, 400.0}})) {
        const ThermalTransientAlgebra run = mustRunTr(a, uniformState(3, 300.0), dt, dt, TransientOutputPolicy::EveryStep);
        previous.assign(run.samples.begin(), run.samples.begin() + 3);
        next.assign(run.samples.begin() + 3, run.samples.begin() + 6);
    }

    [[nodiscard]] std::expected<thermal::TransientStepResidual, TransientFailure>
    judge(std::span<const Temperature> candidate, double step) const {
        return thermal::validateTransientStep(a.k, a.c, a.base.q, a.base.equations, a.base.constraints, previous,
                                              candidate, step, ThermalSolverSettings{});
    }
};

} // namespace

TEST_CASE("ThermalTransient_AcceptanceAcceptsTheRealStepAndRefusesWrongFiniteOnes", "[thermal][transient][acceptance]") {
    const Candidate c;
    const auto good = c.judge(c.next, c.dt);
    INFO((good.has_value() ? std::string{} : good.error().error.message));
    REQUIRE(good.has_value());
    CHECK(good->reduced <= 1e-12);
    CHECK(good->physical <= 1e-12);

    SECTION("a perturbed free temperature") {
        std::vector<Temperature> wrong = c.next;
        wrong[1] = Temperature::fromSi(wrong[1].si() + 1.0e-3);
        CHECK(trProblemOf(c.judge(wrong, c.dt)) == TransientProblem::ResidualTooLarge);
    }
    SECTION("no change at all (a stagnant candidate)") {
        CHECK(trProblemOf(c.judge(c.previous, c.dt)) == TransientProblem::ResidualTooLarge);
    }
    SECTION("the right candidate judged against another time step") {
        CHECK(trProblemOf(c.judge(c.next, c.dt * 1.2)) == TransientProblem::ResidualTooLarge);
    }
    SECTION("a non-finite temperature") {
        std::vector<Temperature> wrong = c.next;
        wrong[2] = Temperature::fromSi(kNaN);
        const auto refused = c.judge(wrong, c.dt);
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().problem == TransientProblem::NonFiniteSolution);
        // It is the acceptance step's own check that speaks (the unknown), not a later one's (a free equation).
        CHECK_THAT(refused.error().error.message, ContainsSubstring("unknown 2"));
        wrong[2] = Temperature::fromSi(kInf);
        CHECK(trProblemOf(c.judge(wrong, c.dt)) == TransientProblem::NonFiniteSolution);
        // A NaN at a PRESCRIBED unknown is a non-finite temperature, not a moved constraint: finiteness is checked first.
        std::vector<Temperature> held = c.next;
        held[0] = Temperature::fromSi(kNaN);
        CHECK(trProblemOf(c.judge(held, c.dt)) == TransientProblem::NonFiniteSolution);
    }
    SECTION("a prescribed unknown that moved") {
        std::vector<Temperature> wrong = c.next;
        wrong[0] = Temperature::fromSi(400.0 + 1.0e-9);
        const auto refused = c.judge(wrong, c.dt);
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().problem == TransientProblem::ConstraintNotPreserved);
        CHECK_THAT(refused.error().error.message, ContainsSubstring("unknown 0"));
    }
    SECTION("a state of the wrong size, a previous state that is not finite, a bad step") {
        CHECK(trProblemOf(c.judge(std::span<const Temperature>(c.next).first(2), c.dt)) == TransientProblem::DofMapMismatch);
        for (const double dt : {0.0, -1.0, kNaN, kInf}) {
            CHECK(trProblemOf(c.judge(c.next, dt)) == TransientProblem::InvalidTimeStep);
        }
        std::vector<Temperature> previous = c.previous;
        previous[1] = Temperature::fromSi(kNaN);
        const auto fromNaN = thermal::validateTransientStep(c.a.k, c.a.c, c.a.base.q, c.a.base.equations, c.a.base.constraints, previous,
                                                            c.next, c.dt, {});
        REQUIRE_FALSE(fromNaN.has_value());
        CHECK(fromNaN.error().problem == TransientProblem::NonFiniteInput);
        // Named as the PREVIOUS temperature, not as the right-hand side that the NaN would otherwise have spoiled later.
        CHECK_THAT(fromNaN.error().error.message, ContainsSubstring("previous temperature of unknown 1"));
        CHECK(trProblemOf(thermal::validateTransientStep(c.a.k, c.a.c, c.a.base.q, c.a.base.equations,
                                                         c.a.base.constraints, c.previous, c.next, c.dt,
                                                         ThermalSolverSettings{.relativeResidualTolerance = 0.0})) ==
              TransientProblem::InvalidSettings);
    }
    SECTION("a tolerance that is loosened accepts what the default refuses (the knob is real, and is the user's)") {
        std::vector<Temperature> wrong = c.next;
        wrong[1] = Temperature::fromSi(wrong[1].si() + 1.0e-5);
        CHECK(trProblemOf(c.judge(wrong, c.dt)) == TransientProblem::ResidualTooLarge);
        CHECK(thermal::validateTransientStep(c.a.k, c.a.c, c.a.base.q, c.a.base.equations, c.a.base.constraints, c.previous,
                                             wrong, c.dt, ThermalSolverSettings{.relativeResidualTolerance = 1.0e-3})
                  .has_value());
    }
}

TEST_CASE("ThermalTransient_AcceptanceRefusesTheWrongSchemesThatAreFinite", "[thermal][transient][acceptance][schemes]") {
    // Candidates produced by schemes OTHER than backward Euler, computed with the test's dense elimination, are refused
    // by the independent residual -- the acceptance step knows which equation it is judging.
    const Dense2 dk = chainConductance({2.0, 3.0});
    const Dense2 dc = chainCapacity(3, 6.0);
    const std::vector<double> q{0.0, 1.0, 0.0};
    const std::vector<double> t0{300.0, 320.0, 310.0};
    const double dt = 0.5;
    const TrAlgebra a = makeTr(dk, dc, q);  // nothing prescribed
    const auto judge = [&](const std::vector<double>& candidate) {
        return thermal::validateTransientStep(a.k, a.c, a.base.q, a.base.equations, a.base.constraints, stateOf(t0),
                                              stateOf(candidate), dt, ThermalSolverSettings{});
    };

    // The correct backward-Euler step is accepted.
    CHECK(judge(denseBackwardEuler(dk, dc, q, t0, dt, {})).has_value());

    // EXPLICIT Euler:  T1 = T0 + dt C^-1 (Q - K T0).
    const std::vector<double> kt0 = multiplyDense(dk, t0);
    std::vector<double> residualAtT0(3);
    for (std::size_t i = 0; i < 3; ++i) {
        residualAtT0[i] = q[i] - kt0[i];
    }
    const std::vector<double> rate = denseSolve(dc, residualAtT0);
    std::vector<double> explicitEuler(3);
    for (std::size_t i = 0; i < 3; ++i) {
        explicitEuler[i] = t0[i] + dt * rate[i];
    }
    CHECK(trProblemOf(judge(explicitEuler)) == TransientProblem::ResidualTooLarge);

    // A system built with C * dt instead of C / dt:  (C dt + K) T1 = Q + C dt T0.
    Dense2 wrongA(3, std::vector<double>(3, 0.0));
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
            wrongA[i][j] = dc[i][j] * dt + dk[i][j];
        }
    }
    std::vector<double> wrongB = multiplyDense(dc, t0);
    for (std::size_t i = 0; i < 3; ++i) {
        wrongB[i] = q[i] + wrongB[i] * dt;
    }
    CHECK(trProblemOf(judge(denseSolve(wrongA, wrongB))) == TransientProblem::ResidualTooLarge);

    // The history taken from the NEW state instead of the old one: (C/dt + K) T1 = Q + (C/dt) T1 is just K T1 = Q, the
    // steady state, which a pure-Neumann model does not even have -- use the unknown-0-prescribed model instead.
    const TrAlgebra anchored = makeTr(dk, dc, q, {{0, 400.0}});
    const std::vector<double> steady = denseExpected(dk, q, {{0, 400.0}});
    const auto steadyJudged = thermal::validateTransientStep(anchored.k, anchored.c, anchored.base.q, anchored.base.equations,
                                                             anchored.base.constraints, stateOf({400.0, 320.0, 310.0}),
                                                             stateOf(steady), dt, ThermalSolverSettings{});
    CHECK(trProblemOf(steadyJudged) == TransientProblem::ResidualTooLarge);

    // The CRANK-NICOLSON step is second order and a different scheme: refused as well.
    Dense2 lhs(3, std::vector<double>(3, 0.0));
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
            lhs[i][j] = dc[i][j] / dt + 0.5 * dk[i][j];
        }
    }
    std::vector<double> rhs = multiplyDense(dc, t0);
    for (std::size_t i = 0; i < 3; ++i) {
        rhs[i] = q[i] + rhs[i] / dt - 0.5 * kt0[i];
    }
    CHECK(trProblemOf(judge(denseSolve(lhs, rhs))) == TransientProblem::ResidualTooLarge);
}

TEST_CASE("ThermalTransient_PhysicalResidualHasTheStatedValue", "[thermal][transient][acceptance][physical]") {
    // One unknown: C = 10, K = 2, Q = 600, T0 = 400, candidate T1 = 380, dt = 1.
    //   r = C (T1 - T0)/dt + K T1 - Q = 10 (-20) + 2 (380) - 600 = -40
    //   scale = ||C|| |T1 - T0| / dt + ||K|| |T1| + |Q| = 10 (20) + 2 (380) + 600 = 1560
    //   -> 40 / 1560
    const TrAlgebra a = makeTr({{2.0}}, {{10.0}}, {600.0});
    const auto value = thermal::physicalStepResidual(a.k, a.c, a.base.q, a.base.equations, stateOf({400.0}), stateOf({380.0}), 1.0);
    REQUIRE(value.has_value());
    CHECK_THAT(*value, WithinAbs(40.0 / 1560.0, 1e-15));
    CHECK_THAT(*value, WithinAbs(0.02564102564102564, 1e-15));

    // The real step has none of it: 4600/12 solves the equation to rounding.
    const auto exact = thermal::physicalStepResidual(a.k, a.c, a.base.q, a.base.equations, stateOf({400.0}), stateOf({4600.0 / 12.0}), 1.0);
    REQUIRE(exact.has_value());
    CHECK(*exact <= 1e-15);

    // Scaling K, C and Q together leaves the number unchanged; scaling only the step does not.
    const TrAlgebra scaled = makeTr({{2.0e6}}, {{1.0e7}}, {6.0e8});
    const auto same = thermal::physicalStepResidual(scaled.k, scaled.c, scaled.base.q, scaled.base.equations, stateOf({400.0}), stateOf({380.0}), 1.0);
    REQUIRE(same.has_value());
    CHECK_THAT(*same, WithinRel(*value, 1e-12));
    const auto otherStep = thermal::physicalStepResidual(a.k, a.c, a.base.q, a.base.equations, stateOf({400.0}), stateOf({380.0}), 2.0);
    REQUIRE(otherStep.has_value());
    CHECK(std::abs(*otherStep - *value) > 0.01);

    // Refusals.
    for (const double dt : {0.0, -1.0, kNaN, kInf}) {
        const auto refused = thermal::physicalStepResidual(a.k, a.c, a.base.q, a.base.equations, stateOf({400.0}), stateOf({380.0}), dt);
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().problem == TransientProblem::InvalidTimeStep);
    }
    CHECK(trProblemOf(thermal::physicalStepResidual(a.k, a.c, a.base.q, a.base.equations, stateOf({400.0, 1.0}), stateOf({380.0}), 1.0)) ==
          TransientProblem::DofMapMismatch);
}

TEST_CASE("ThermalTransient_PhysicalResidualRefusesANonFiniteValueAndSaysWhere", "[thermal][transient][acceptance][physical]") {
    // A row that overflows is named; a row that is finite but whose square overflows is caught by the norm.
    const TrAlgebra overflowRow = makeTr({{1.0e200}}, {{1.0}}, {0.0});
    const auto row = thermal::physicalStepResidual(overflowRow.k, overflowRow.c, overflowRow.base.q, overflowRow.base.equations,
                                                   uniformState(1, 0.0), stateOf({1.0e200}), 1.0);
    REQUIRE_FALSE(row.has_value());
    CHECK(row.error().problem == TransientProblem::NonFiniteResidual);
    CHECK_THAT(row.error().error.message, ContainsSubstring("unknown 0"));

    // r = 1e200 is finite; r * r = 1e400 is not.
    const TrAlgebra overflowNorm = makeTr({{1.0}}, {{1.0}}, {0.0});
    const auto norm = thermal::physicalStepResidual(overflowNorm.k, overflowNorm.c, overflowNorm.base.q, overflowNorm.base.equations,
                                                    uniformState(1, 0.0), stateOf({1.0e200}), 1.0);
    REQUIRE_FALSE(norm.has_value());
    CHECK(norm.error().problem == TransientProblem::NonFiniteResidual);
    CHECK_THAT(norm.error().error.message, ContainsSubstring("norm"));

    // Everything exactly zero: the residual is exactly zero, not a division of zero by zero.
    const TrAlgebra zero = makeTr({{1.0}}, {{1.0}}, {0.0});
    const auto none = thermal::physicalStepResidual(zero.k, zero.c, zero.base.q, zero.base.equations, uniformState(1, 0.0), uniformState(1, 0.0), 1.0);
    REQUIRE(none.has_value());
    CHECK(*none == 0.0);
}

TEST_CASE("ThermalTransient_PhysicalResidualSeesAWronglyBuiltSystemTheReducedOneCannot", "[thermal][transient][acceptance][physical]") {
    // THE DEFENCE IS TESTED BY SHOWING IT A WRONG BUILD. A system built with C * dt instead of C / dt is solved perfectly well --
    // the residual of ITS OWN equation is rounding noise, computed here from the test's own arrays -- and the answer is not the
    // temperature field of the heat equation. Only a residual taken from the ORIGINAL K, C and Q can say so.
    const Dense2 dk = chainConductance({2.0, 3.0});
    const Dense2 dc = chainCapacity(3, 6.0);
    const std::vector<double> q{0.0, 1.0, 0.0};
    const std::vector<double> t0{300.0, 320.0, 310.0};
    const double dt = 0.5;
    const TrAlgebra a = makeTr(dk, dc, q);

    Dense2 wrongA(3, std::vector<double>(3, 0.0));
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
            wrongA[i][j] = dc[i][j] * dt + dk[i][j];
        }
    }
    std::vector<double> wrongB = multiplyDense(dc, t0);
    for (std::size_t i = 0; i < 3; ++i) {
        wrongB[i] = q[i] + wrongB[i] * dt;
    }
    const std::vector<double> wrong = denseSolve(wrongA, wrongB);
    // Against the wrong system's own equation: noise.
    const std::vector<double> own = multiplyDense(wrongA, wrong);
    double ownResidual = 0.0;
    for (std::size_t i = 0; i < 3; ++i) {
        ownResidual = std::max(ownResidual, std::abs(own[i] - wrongB[i]));
    }
    CHECK(ownResidual <= 1e-10);

    const auto physical = thermal::physicalStepResidual(a.k, a.c, a.base.q, a.base.equations, stateOf(t0), stateOf(wrong), dt);
    REQUIRE(physical.has_value());
    CHECK(*physical > 1e-4);  // seven orders above the tolerance's 1e-9 floor of honesty
    // The correct step is clean.
    const auto good = thermal::physicalStepResidual(a.k, a.c, a.base.q, a.base.equations, stateOf(t0),
                                                    stateOf(denseBackwardEuler(dk, dc, q, t0, dt, {})), dt);
    REQUIRE(good.has_value());
    CHECK(*good <= 1e-13);

    // A wrong HISTORY SIGN: (C/dt + K) T1 = Q - (C/dt) T0.
    Dense2 effective(3, std::vector<double>(3, 0.0));
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
            effective[i][j] = dc[i][j] / dt + dk[i][j];
        }
    }
    std::vector<double> signFlipped = multiplyDense(dc, t0);
    for (std::size_t i = 0; i < 3; ++i) {
        signFlipped[i] = q[i] - signFlipped[i] / dt;
    }
    const auto flipped = thermal::physicalStepResidual(a.k, a.c, a.base.q, a.base.equations, stateOf(t0),
                                                       stateOf(denseSolve(effective, signFlipped)), dt);
    REQUIRE(flipped.has_value());
    CHECK(*flipped > 1e-4);
}

TEST_CASE("ThermalTransient_PhysicalResidualExcludesTheBoundaryHeatAtPrescribedRows", "[thermal][transient][acceptance][physical]") {
    // At a prescribed unknown the residual C dT/dt + K T - Q is the heat the boundary supplies and is correctly non-zero.
    // Counting it would fail every correct solve with a Dirichlet condition. Only the FREE rows are the heat equation.
    const Chain3 chain;
    const TrAlgebra a = makeTr(chain.k, chain.c, chain.q, {{0, 400.0}});
    const std::vector<double> t0{400.0, 300.0, 300.0};
    const std::vector<double> t1 = denseBackwardEuler(chain.k, chain.c, chain.q, t0, 0.5, {{0, 400.0}});
    const auto physical = thermal::physicalStepResidual(a.k, a.c, a.base.q, a.base.equations, stateOf(t0), stateOf(t1), 0.5);
    REQUIRE(physical.has_value());
    CHECK(*physical <= 1e-13);
    // The residual of row 0, which is prescribed, is the boundary heat: not small.
    std::vector<double> delta(3);
    for (std::size_t i = 0; i < 3; ++i) {
        delta[i] = t1[i] - t0[i];
    }
    const double boundaryHeat = multiplyDense(chain.c, delta)[0] / 0.5 + multiplyDense(chain.k, t1)[0] - chain.q[0];
    CHECK(std::abs(boundaryHeat) > 1.0);
}

// ===========================================================================
// Scaling, limits and conservation
// ===========================================================================

TEST_CASE("ThermalTransient_PhysicalResidualIsStricterThanTheReducedOneForASmallStep", "[thermal][transient][acceptance][physical]") {
    // C = 1, K = 1, Q = 0, T0 = 300 K, dt = 1e-3:  A = 1/dt + 1 = 1001,  b = 300/dt = 3e5,  T1 = 300/1.001.
    // A candidate that is off by 1e-7 K passes the REDUCED residual, which is scaled by ||A|| ||x|| + ||b|| and so by the whole
    // C T/dt (6e5 here), and fails the PHYSICAL one, which is scaled by the temperature CHANGE C dT/dt (300) and K T (300):
    //   reduced  = 1001 * 1e-7 / (1001 * 300 + 3e5)  = 1.7e-10  <= 1e-9
    //   physical = 1001 * 1e-7 / (299.7 + 299.7)     = 1.7e-7   >  1e-9
    // The physical residual is therefore the stricter test of whether the state moved by the right amount, and the one that
    // keeps a small step honest.
    const TrAlgebra a = makeTr({{1.0}}, {{1.0}}, {0.0});
    const double dt = 1.0e-3;
    const double exact = 300.0 / (1.0 + dt);
    const double delta = 1.0e-7;
    const double reducedByHand = 1001.0 * delta / (1001.0 * (exact + delta) + 300.0 / dt);
    CHECK(reducedByHand < 1.0e-9);   // the reduced residual would have accepted it
    CHECK(reducedByHand > 1.0e-10);

    const auto refused = thermal::validateTransientStep(a.k, a.c, a.base.q, a.base.equations, a.base.constraints, stateOf({300.0}),
                                                        stateOf({exact + delta}), dt, ThermalSolverSettings{});
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().problem == TransientProblem::ResidualTooLarge);
    CHECK_THAT(refused.error().error.message, ContainsSubstring("physical residual"));
    const auto physical = thermal::physicalStepResidual(a.k, a.c, a.base.q, a.base.equations, stateOf({300.0}), stateOf({exact + delta}), dt);
    REQUIRE(physical.has_value());
    CHECK(*physical > 1.0e-7);
    CHECK(*physical < 3.0e-7);
    // The exact step has neither.
    CHECK(thermal::validateTransientStep(a.k, a.c, a.base.q, a.base.equations, a.base.constraints, stateOf({300.0}), stateOf({exact}), dt,
                                         ThermalSolverSettings{})
              .has_value());
}

TEST_CASE("ThermalTransient_AStepBelowTheResolutionOfTheTemperatureIsRefusedAndSaysSo", "[thermal][transient][refusals][resolution]") {
    // When dt/tau is tiny the temperature CHANGE of a step is smaller than the rounding of the temperature: T1 = 300/(1 + dt)
    // differs from 300 by 3e-10 at dt = 1e-12, and a double near 300 carries an error of 5.7e-14, so the increment (and with it
    // C dT/dt) is wrong in its fourth digit. The step satisfies the system that was solved and not the heat equation, and the
    // physical residual says so. A limitation of fixed-precision backward Euler stated in the open and not hidden by a loose test.
    const TrAlgebra a = makeTr({{1.0}}, {{1.0}}, {0.0});
    const auto init = uniformState(1, 300.0);
    CHECK(runTr(a, init, 1.0e-6, 1.0e-6).has_value());          // dt/tau = 1e-6: the increment is 3e-4 K, resolved
    CHECK(runTr(a, init, 1.0e-3, 1.0e-3).has_value());
    const auto tiny = runTr(a, init, 1.0e-12, 1.0e-12);          // dt/tau = 1e-12: the increment is 3e-10 K, not resolved
    REQUIRE_FALSE(tiny.has_value());
    CHECK(tiny.error().problem == TransientProblem::ResidualTooLarge);
    CHECK(tiny.error().step == 1);
    CHECK_THAT(tiny.error().error.message, ContainsSubstring("physical residual"));
    CHECK_THAT(tiny.error().error.message, ContainsSubstring("rounding of the temperature"));
    // The user's tolerance is the control: accepted when they say the increment need not be better than 10 %.
    CHECK(runTr(a, init, 1.0e-12, 1.0e-12, TransientOutputPolicy::EveryStep, ThermalSolverSettings{.relativeResidualTolerance = 1.0}).has_value());
}

TEST_CASE("ThermalTransient_ConstantTemperatureIsASolution", "[thermal][transient][constant]") {
    // T = T* everywhere solves C dT/dt + K T = Q whenever K T* = Q, because then dT/dt = 0: a uniform field in equilibrium with
    // its surroundings must not move, whatever the capacity, whatever the step.
    SECTION("insulated, no heat, uniform start: nothing moves") {
        const Chain3 chain;
        const TrAlgebra a = makeTr(chain.k, chain.c, {0.0, 0.0, 0.0});
        const ThermalTransientAlgebra run = mustRunTr(a, uniformState(3, 350.0), 0.7, 7.0);
        REQUIRE(run.sampleTimes.size() == 11);
        for (std::size_t n = 0; n < run.sampleTimes.size(); ++n) {
            CHECK(worstDifference(sampleOf(run, n), {350.0, 350.0, 350.0}) <= 1e-10);
        }
    }
    SECTION("held at the temperature it starts at, no heat: nothing moves, and nothing is overridden") {
        const Chain3 chain;
        const TrAlgebra a = makeTr(chain.k, chain.c, {0.0, 0.0, 0.0}, {{0, 350.0}, {2, 350.0}});
        const ThermalTransientAlgebra run = mustRunTr(a, uniformState(3, 350.0), 0.5, 3.0);
        CHECK(run.initialOverrides == 0);
        for (std::size_t n = 0; n < run.sampleTimes.size(); ++n) {
            CHECK(worstDifference(sampleOf(run, n), {350.0, 350.0, 350.0}) <= 1e-10);
        }
    }
    SECTION("a body at its ambient: the film supplies exactly what it would lose") {
        // G = 2 W/K to an ambient of 350 K: K = 2, Q = 700. T = 350 gives K T - Q = 0.
        const TrAlgebra a = makeTr({{2.0}}, {{10.0}}, {700.0});
        const ThermalTransientAlgebra run = mustRunTr(a, uniformState(1, 350.0), 1.0, 5.0);
        for (std::size_t n = 0; n <= 5; ++n) {
            CHECK_THAT(sampleOf(run, n)[0], WithinAbs(350.0, 1e-11));
        }
    }
    SECTION("a mesh: the 64-unknown insulated Kuhn body with no generation") {
        const AsmHand hand = makeKuhnGrid(4, 0.01);
        const ThermalSystem system = assemble(hand, 45.0, capacityOf(7850.0, 460.0), noHeat(hand));
        const std::vector<thermal::ThermalTemperatureConstraint> none;
        const auto constraints = thermal::buildThermalConstraintMap(hand.dofs, none);
        REQUIRE(constraints.has_value());
        const auto equations = thermal::buildThermalFreeEquationMap(hand.dofs, *constraints);
        REQUIRE(equations.has_value());
        const auto run = thermal::solveThermalTransientAlgebra(system.conductance(), *system.capacity(), system.heat(), *equations, *constraints,
                                                               uniformState(64, 310.0), gridOf(2.0, 20.0), TransientOutputPolicy::FinalOnly, {});
        REQUIRE(run.has_value());
        CHECK(worstDifference(sampleOf(*run, 0), std::vector<double>(64, 310.0)) <= 1e-10);
    }
}

TEST_CASE("ThermalTransient_AnswerIsInvariantUnderScalingTheWholeEquation", "[thermal][transient][scaling]") {
    const Chain3 chain;
    const std::vector<std::pair<std::size_t, double>> prescribed{{0, 400.0}};
    const TrAlgebra reference = makeTr(chain.k, chain.c, chain.q, prescribed);
    const ThermalTransientAlgebra base = mustRunTr(reference, uniformState(3, 300.0), 0.5, 2.0);

    const auto scaled = [&](double factor) {
        Dense2 k = chain.k;
        Dense2 c = chain.c;
        std::vector<double> q = chain.q;
        for (std::size_t i = 0; i < 3; ++i) {
            for (std::size_t j = 0; j < 3; ++j) {
                k[i][j] *= factor;
                c[i][j] *= factor;
            }
            q[i] *= factor;
        }
        return makeTr(k, c, q, prescribed);
    };
    for (const double factor : {1.0e-6, 1.0e6}) {
        CAPTURE(factor);
        const ThermalTransientAlgebra other = mustRunTr(scaled(factor), uniformState(3, 300.0), 0.5, 2.0);
        for (std::size_t n = 0; n <= 4; ++n) {
            CHECK(worstDifference(sampleOf(other, n), sampleOf(base, n)) <= 1e-7);
        }
        // The normalized residuals are scale free.
        for (const thermal::TransientStepRecord& record : other.steps) {
            CHECK(record.reducedResidual <= 1e-9);
            CHECK(record.physicalResidual <= 1e-9);
        }
    }

    // Doubling C and dt together leaves C/dt, and the answer, unchanged.
    Dense2 c2x = chain.c;
    for (auto& row : c2x) {
        for (double& v : row) {
            v *= 2.0;
        }
    }
    const ThermalTransientAlgebra slow = mustRunTr(makeTr(chain.k, c2x, chain.q, prescribed), uniformState(3, 300.0), 1.0, 4.0);
    // Q is a rate, so the step is twice as long at twice the capacity: the states at t = 1, 2, 3, 4 s are the states of
    // the reference at t = 0.5, 1, 1.5, 2 s.
    for (std::size_t n = 0; n <= 4; ++n) {
        CHECK(worstDifference(sampleOf(slow, n), sampleOf(base, n)) <= 1e-9);
    }
}

TEST_CASE("ThermalTransient_SteadyLimitMatchesTheSteadySolve", "[thermal][transient][steady-limit]") {
    const Chain3 chain;
    const std::vector<std::pair<std::size_t, double>> prescribed{{0, 400.0}};
    const TrAlgebra a = makeTr(chain.k, chain.c, chain.q, prescribed);
    const ThermalAlgebraicSolution steady = mustSolve(a.base);

    // Long enough that (1/(1 + lambda dt))^N is below machine precision: the slowest generalised eigenvalue of this chain
    // is lambda = 0.227 /s (7 l^2 - 28 l + 6 = 0), so a step of 10 s multiplies the error by 0.31 and 80 steps by 1e-41.
    const ThermalTransientAlgebra run = mustRunTr(a, uniformState(3, 300.0), 10.0, 800.0, TransientOutputPolicy::FinalOnly);
    const std::vector<double> last = sampleOf(run, 0);
    const std::vector<double> expected = valuesOf(steady);
    CHECK(worstDifference(last, expected) <= 1e-9);

    // The steady state is a FIXED POINT of the backward-Euler step for every dt.
    for (const double dt : {0.01, 1.0, 100.0, 1.0e6}) {
        CAPTURE(dt);
        const ThermalTransientAlgebra stay = mustRunTr(a, stateOf(expected), dt, 3.0 * dt, TransientOutputPolicy::EveryStep);
        for (std::size_t n = 0; n <= 3; ++n) {
            CHECK(worstDifference(sampleOf(stay, n), expected) <= 1e-8);
        }
    }
    // One enormous step lands on the steady state: C/dt vanishes against K.
    const ThermalTransientAlgebra huge = mustRunTr(a, uniformState(3, 300.0), 1.0e12, 1.0e12, TransientOutputPolicy::FinalOnly);
    CHECK(worstDifference(sampleOf(huge, 0), expected) <= 1e-6);
}

TEST_CASE("ThermalTransient_TimeStepRefinementIsFirstOrder", "[thermal][transient][refinement]") {
    // C dT/dt = -K T with C = [2 1; 1 2], K = [1 -1; -1 1], T0 = [300, 330]:
    //   the mean (1,1) is constant (K 1 = 0), the antisymmetric mode (1,-1) has C v = v and K v = 2 v, so it decays as
    //   e^{-2t}:     T_0(t) = 315 - 15 e^{-2t}      T_1(t) = 315 + 15 e^{-2t}
    // and backward Euler multiplies it by 1/(1 + 2 dt) per step, so after n steps  315 -/+ 15 (1 + 2 dt)^{-n}  exactly.
    const TrAlgebra a = makeTr(k2(), c2(), {0.0, 0.0});
    const auto init = stateOf({300.0, 330.0});
    const double exact = 315.0 - 15.0 * std::exp(-2.0);

    std::vector<double> errors;
    for (const double dt : {0.2, 0.1, 0.05, 0.025}) {
        CAPTURE(dt);
        const ThermalTransientAlgebra run = mustRunTr(a, init, dt, 1.0, TransientOutputPolicy::FinalOnly);
        const double node0 = sampleOf(run, 0)[0];
        // The integrator reproduces the DISCRETE closed form to rounding...
        const double steps = std::round(1.0 / dt);
        CHECK_THAT(node0, WithinAbs(315.0 - 15.0 * std::pow(1.0 + 2.0 * dt, -steps), 1e-10));
        // ...and its distance from the CONTINUOUS solution is the time-discretisation error.
        errors.push_back(std::abs(node0 - exact));
    }
    REQUIRE(errors.size() == 4);
    for (std::size_t i = 0; i + 1 < errors.size(); ++i) {
        const double order = std::log2(errors[i] / errors[i + 1]);
        CAPTURE(i, errors[i], errors[i + 1], order);
        CHECK(errors[i + 1] < errors[i]);
        CHECK(order > 0.9);
        CHECK(order < 1.1);
    }
    WARN(std::format("time-step refinement, non-uniform 2x2 (exact node-0 temperature at t = 1: {:.10f} K): errors for dt = 0.2, 0.1, "
                     "0.05, 0.025 are {:.4e}, {:.4e}, {:.4e}, {:.4e}; observed orders {:.3f}, {:.3f}, {:.3f}",
                     exact, errors[0], errors[1], errors[2], errors[3], std::log2(errors[0] / errors[1]),
                     std::log2(errors[1] / errors[2]), std::log2(errors[2] / errors[3])));
}

TEST_CASE("ThermalTransient_LumpedBodyMatchesTheExponentialAtFirstOrder", "[thermal][transient][refinement][lumped]") {
    // One lumped body: C = 10 J/K cooling through G = 2 W/K to 300 K from 400 K. T(t) = 300 + 100 e^{-t/5}.
    const TrAlgebra a = makeTr({{2.0}}, {{10.0}}, {600.0});
    std::vector<double> errors;
    for (const double dt : {1.0, 0.5, 0.25, 0.125}) {
        const ThermalTransientAlgebra run = mustRunTr(a, stateOf({400.0}), dt, 5.0, TransientOutputPolicy::FinalOnly);
        errors.push_back(std::abs(run.samples[0].si() - (300.0 + 100.0 * std::exp(-1.0))));
    }
    for (std::size_t i = 0; i + 1 < errors.size(); ++i) {
        const double order = std::log2(errors[i] / errors[i + 1]);
        CHECK(order > 0.9);
        CHECK(order < 1.1);
    }
    CHECK(errors.back() < 0.5);
    WARN(std::format("lumped body, tau = C/G = 5 s, T(5 s) = {:.9f} K; errors {:.4e}, {:.4e}, {:.4e}, {:.4e} for dt = 1, 0.5, 0.25, 0.125",
                     300.0 + 100.0 * std::exp(-1.0), errors[0], errors[1], errors[2], errors[3]));
}

TEST_CASE("ThermalTransient_EnergyIsConservedOnAnInsulatedMeshedBody", "[thermal][transient][energy]") {
    // A structured Kuhn grid, insulated everywhere, with a uniform generation: 1^T K = 0, so the stored energy 1^T C T
    // grows by EXACTLY dt * sum(Q) every step, whatever the temperature field does.
    const AsmHand hand = makeKuhnGrid(4, 0.01);
    const std::vector<Power> heat = generationHeat(hand, 2.0e6);
    const ThermalSystem system = assemble(hand, 45.0, capacityOf(7850.0, 460.0), heat);
    REQUIRE(system.capacity().has_value());
    const Dense2 dc = denseRows(*system.capacity());
    const std::size_t n = dc.size();
    double totalQ = 0.0;
    for (const double q : system.heat().values()) {
        totalQ += q;
    }
    REQUIRE(totalQ > 0.0);

    // A non-uniform initial field.
    std::vector<double> t0(n);
    for (std::size_t i = 0; i < n; ++i) {
        t0[i] = 300.0 + 10.0 * static_cast<double>(i % 7);
    }
    const std::vector<thermal::ThermalTemperatureConstraint> none;
    const auto constraints = thermal::buildThermalConstraintMap(hand.dofs, none);
    REQUIRE(constraints.has_value());
    const auto equations = thermal::buildThermalFreeEquationMap(hand.dofs, *constraints);
    REQUIRE(equations.has_value());
    const double dt = 2.0;
    const auto grid = TimeGrid::create(dt, 20.0);
    REQUIRE(grid.has_value());
    auto run = thermal::solveThermalTransientAlgebra(system.conductance(), *system.capacity(), system.heat(), *equations,
                                                     *constraints, stateOf(t0), *grid, TransientOutputPolicy::EveryStep, {});
    INFO((run.has_value() ? std::string{} : run.error().error.message));
    REQUIRE(run.has_value());
    REQUIRE(run->steps.size() == 10);
    const double e0 = storedEnergy(dc, sampleOf(*run, 0));
    for (std::size_t step = 1; step <= 10; ++step) {
        const double gained = storedEnergy(dc, sampleOf(*run, step)) - e0;
        CHECK_THAT(gained, WithinRel(dt * static_cast<double>(step) * totalQ, 1e-9));
    }
    // And the field is not trivial: it moved.
    CHECK(worstDifference(sampleOf(*run, 10), t0) > 1.0);
    WARN(std::format("insulated Kuhn body, {} unknowns, generation {:.6g} W: stored energy +{:.6g} J over 20 s vs dt*N*sum(Q) = {:.6g} J",
                     n, totalQ, storedEnergy(dc, sampleOf(*run, 10)) - e0, 20.0 * totalQ));
}

TEST_CASE("ThermalTransient_LargeSparseSystemMatchesTheDenseOracle", "[thermal][transient][sparse][smoke]") {
    // 6^3 = 216 unknowns of a structured tetrahedral grid (1080 tetrahedra), one face held at 400 K, uniform generation. The
    // dense oracle eliminates the 216-unknown system itself.
    const AsmHand hand = makeKuhnGrid(6, 0.01);
    const std::vector<Power> heat = generationHeat(hand, 5.0e5);
    const ThermalSystem system = assemble(hand, 45.0, capacityOf(7850.0, 460.0), heat);
    const std::size_t n = static_cast<std::size_t>(hand.dofs.dofCount());
    std::vector<thermal::ThermalTemperatureConstraint> list;
    std::vector<std::pair<std::size_t, double>> prescribed;
    for (std::size_t j = 0; j < 6; ++j) {
        for (std::size_t i = 0; i < 6; ++i) {
            const std::size_t dof = (0 * 6 + j) * 6 + i;  // the z = 0 face
            list.push_back(thermal::ThermalTemperatureConstraint{dofIx(dof), Temperature::fromSi(400.0)});
            prescribed.emplace_back(dof, 400.0);
        }
    }
    const auto constraints = thermal::buildThermalConstraintMap(hand.dofs, list);
    REQUIRE(constraints.has_value());
    const auto equations = thermal::buildThermalFreeEquationMap(hand.dofs, *constraints);
    REQUIRE(equations.has_value());

    const auto run = thermal::solveThermalTransientAlgebra(system.conductance(), *system.capacity(), system.heat(), *equations,
                                                           *constraints, uniformState(n, 300.0), gridOf(1.0, 3.0),
                                                           TransientOutputPolicy::EveryStep, {});
    INFO((run.has_value() ? std::string{} : run.error().error.message));
    REQUIRE(run.has_value());
    const auto history = denseHistory(denseRows(system.conductance()), denseRows(*system.capacity()),
                                      std::vector<double>(system.heat().values().begin(), system.heat().values().end()),
                                      std::vector<double>(n, 300.0), {1.0, 1.0, 1.0}, prescribed);
    for (std::size_t step = 0; step <= 3; ++step) {
        CHECK(worstDifference(sampleOf(*run, step), history[step]) <= 1e-7);
    }
    CHECK(run->freeEquations == n - 36);
    CHECK(system.conductance().nonZeros() < n * n / 5);
    WARN(std::format("sparse smoke: {} unknowns, {} free, nnz(K) {} of {}, three steps agree with the dense oracle to {:.2e} K",
                     n, n - 36, system.conductance().nonZeros(), n * n,
                     worstDifference(sampleOf(*run, 3), history[3])));
}

// ===========================================================================
// Determinism
// ===========================================================================

TEST_CASE("ThermalTransient_RepeatedRunsAreBitwiseIdentical", "[thermal][transient][determinism]") {
    const Chain3 chain;
    const TrAlgebra a = makeTr(chain.k, chain.c, chain.q, {{0, 400.0}});
    const ThermalTransientAlgebra first = mustRunTr(a, uniformState(3, 300.0), 0.3, 3.0);
    int differences = 0;
    for (int i = 0; i < 30; ++i) {
        if (!(mustRunTr(a, uniformState(3, 300.0), 0.3, 3.0) == first)) {
            ++differences;
        }
    }
    CHECK(differences == 0);
    CHECK(fingerprint(first) == fingerprint(mustRunTr(a, uniformState(3, 300.0), 0.3, 3.0)));
    WARN(std::format("determinism: 30 repeated algebraic runs, {} bitwise differences; fingerprint {:016x}", differences, fingerprint(first)));
}

TEST_CASE("ThermalTransient_TheInputsAreNeverModified", "[thermal][transient][purity]") {
    const Chain3 chain;
    const TrAlgebra a = makeTr(chain.k, chain.c, chain.q, {{0, 400.0}});
    const thermal::ConductanceMatrix kBefore = a.k;
    const thermal::CapacityMatrix cBefore = a.c;
    const thermal::ThermalHeatVector qBefore = a.base.q;
    const thermal::ThermalConstraintMap constraintsBefore = a.base.constraints;
    const std::vector<Temperature> init = uniformState(3, 300.0);
    const std::vector<Temperature> initBefore = init;
    (void)mustRunTr(a, init, 0.5, 2.0);
    CHECK(a.k == kBefore);
    CHECK(a.c == cBefore);
    CHECK(a.base.q == qBefore);
    CHECK(a.base.constraints == constraintsBefore);
    CHECK(init == initBefore);
}

// ===========================================================================
// Failures in the middle of a run
// ===========================================================================

TEST_CASE("ThermalTransient_AFailureAtALaterStepNamesThatStepAndPublishesNothing", "[thermal][transient][refusals][atomic]") {
    // C = 1e307 J/K from 0 K with no heat: step 1 (dt = 1) is fine -- A = 1e307 + 1, b = 0, T = 0. The grid for t_end = 1.01
    // ends with a SHORTENED step of 0.01 s, and C / 0.01 = 1e309 overflows. The failure is step 2, at t = 1.01 s, and the run
    // returns no history at all: step 1's accepted state is not published as a result.
    const TrAlgebra a = makeTr({{1.0}}, {{1.0e307}}, {0.0});
    const auto run = runTr(a, uniformState(1, 0.0), 1.0, 1.01);
    REQUIRE_FALSE(run.has_value());
    CHECK(run.error().problem == TransientProblem::NonFiniteInput);
    CHECK(run.error().step == 2);
    CHECK(run.error().time == 1.01);
    // The same system on a grid whose last step is full is fine, so the shortened step is what failed.
    CHECK(runTr(a, uniformState(1, 0.0), 1.0, 2.0).has_value());
}

TEST_CASE("ThermalTransient_AnOverflowingHistoryTermIsRefusedNotPropagated", "[thermal][transient][refusals]") {
    // C T / dt = 1e200 * 1e200 overflows although A = C/dt + K is finite: the right-hand side is checked, not assumed.
    const TrAlgebra a = makeTr({{1.0}}, {{1.0e200}}, {0.0});
    const auto run = runTr(a, uniformState(1, 1.0e200), 1.0, 1.0);
    REQUIRE_FALSE(run.has_value());
    CHECK(run.error().problem == TransientProblem::NonFiniteInput);
    CHECK(run.error().step == 1);
    CHECK_THAT(run.error().error.message, ContainsSubstring("overflowed"));
}

TEST_CASE("ThermalTransient_AnOverflowingResidualIsRefusedByTheAcceptanceStep", "[thermal][transient][acceptance]") {
    // A candidate so large that A x overflows: the residual is not finite, which is a refusal and not a pass.
    const TrAlgebra a = makeTr({{1.0e200}}, {{1.0}}, {0.0});
    const auto refused = thermal::validateTransientStep(a.k, a.c, a.base.q, a.base.equations, a.base.constraints, uniformState(1, 0.0),
                                                        stateOf({1.0e200}), 1.0, ThermalSolverSettings{});
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().problem == TransientProblem::NonFiniteResidual);
}

// ===========================================================================
// The assembled-system seam, on a hand mesh
// ===========================================================================

namespace {

[[nodiscard]] ThermalSystem assembleIn(const AsmHand& hand, thermal::ThermalAnalysisMode mode,
                                       std::optional<ThermalCapacity> capacity, const std::vector<Power>& heat) {
    const ThermalAssemblyData data{.mesh = hand.mesh,
                                   .map = hand.dofs,
                                   .conductivity = ThermalConductivity::fromSi(45.0),
                                   .capacity = capacity,
                                   .heatInput = heat,
                                   .convections = {}};
    auto built = thermal::assembleThermalSystemFrom(data, ThermalSystemSource{.mode = mode});
    INFO((built.has_value() ? std::string{} : built.error().error.message));
    REQUIRE(built.has_value());
    return std::move(*built);
}

[[nodiscard]] thermal::ThermalConstraintMap constraintsOf(const AsmHand& hand,
                                                          const std::vector<std::pair<std::size_t, double>>& prescribed) {
    std::vector<thermal::ThermalTemperatureConstraint> list;
    for (const auto& [dof, kelvin] : prescribed) {
        list.push_back(thermal::ThermalTemperatureConstraint{dofIx(dof), Temperature::fromSi(kelvin)});
    }
    auto built = thermal::buildThermalConstraintMap(hand.dofs, list);
    REQUIRE(built.has_value());
    return std::move(*built);
}

[[nodiscard]] TransientSettings timingOf(double dt, double end, TransientOutputPolicy output = TransientOutputPolicy::EveryStep) {
    return TransientSettings{.timeStep = Time::fromSi(dt), .endTime = Time::fromSi(end), .output = output};
}

} // namespace

TEST_CASE("ThermalTransient_SeamMatchesTheAlgebraAndRecordsTheSystemSource", "[thermal][transient][seam]") {
    const AsmHand hand = makeTwoTet();
    const ThermalSystem system = assembleIn(hand, thermal::ThermalAnalysisMode::Transient, capacityOf(7850.0, 460.0), generationHeat(hand, 1.0e6));
    REQUIRE(system.capacity().has_value());
    const thermal::ThermalConstraintMap constraints = constraintsOf(hand, {{0, 400.0}});
    const auto equations = thermal::buildThermalFreeEquationMap(hand.dofs, constraints);
    REQUIRE(equations.has_value());
    const TransientSettings transient = timingOf(1.0e4, 5.0e4);

    const auto solved = thermal::solveTransientThermalSystem(system, hand.dofs, constraints, UniformInitialTemperature{Temperature::fromSi(300.0)},
                                                             transient, {});
    INFO((solved.has_value() ? std::string{} : solved.error().error.message));
    REQUIRE(solved.has_value());
    const auto run = thermal::solveThermalTransientAlgebra(system.conductance(), *system.capacity(), system.heat(), *equations, constraints,
                                                           uniformState(5, 300.0), gridOf(1.0e4, 5.0e4), TransientOutputPolicy::EveryStep, {});
    REQUIRE(run.has_value());
    REQUIRE(solved->sampleCount() == run->sampleTimes.size());
    for (std::size_t i = 0; i < solved->sampleCount(); ++i) {
        CHECK(std::equal(solved->temperaturesAt(i).begin(), solved->temperaturesAt(i).end(), run->samples.begin() + static_cast<std::ptrdiff_t>(i * 5)));
    }
    CHECK(solved->source().system == system.source());
    CHECK(solved->source().system.mode == thermal::ThermalAnalysisMode::Transient);
    CHECK(solved->source().temperatures.empty());  // a hand-built constraint map has no canonical conditions
    CHECK(solved->source().initial.value.si() == 300.0);
    CHECK(solved->source().transient == transient);
    CHECK(solved->source().settings == ThermalSolverSettings{});
    CHECK(solved->describes(hand.dofs));
    CHECK(solved->constrainedCount() == 1);
    CHECK(solved->freeEquations() == 4);
    CHECK(solved->steps().size() == 5);
    // The held node stays exactly 400 K in every state.
    for (std::size_t i = 0; i < solved->sampleCount(); ++i) {
        CHECK(solved->temperaturesAt(i)[0].si() == 400.0);
    }
}

TEST_CASE("ThermalTransient_SeamRefusals", "[thermal][transient][seam][refusals]") {
    const AsmHand hand = makeTwoTet();
    const std::vector<Power> heat = generationHeat(hand, 1.0e6);
    const ThermalSystem transientSystem = assembleIn(hand, thermal::ThermalAnalysisMode::Transient, capacityOf(7850.0, 460.0), heat);
    const thermal::ThermalConstraintMap constraints = constraintsOf(hand, {{0, 400.0}});
    const UniformInitialTemperature initial{Temperature::fromSi(300.0)};
    const TransientSettings transient = timingOf(10.0, 100.0);
    const auto problemOf2 = [&](const ThermalSystem& system, const thermal::ThermalDofMap& map, const thermal::ThermalConstraintMap& c,
                                const UniformInitialTemperature& i, const TransientSettings& t, const ThermalSolverSettings& s) {
        return trProblemOf(thermal::solveTransientThermalSystem(system, map, c, i, t, s));
    };
    CHECK_FALSE(problemOf2(transientSystem, hand.dofs, constraints, initial, transient, {}).has_value());

    SECTION("a steady system") {
        const ThermalSystem steadyPlain = assembleIn(hand, thermal::ThermalAnalysisMode::Steady, std::nullopt, heat);
        CHECK(problemOf2(steadyPlain, hand.dofs, constraints, initial, transient, {}) == TransientProblem::NotTransientSystem);
        // The mode of an assembled system is DERIVED from whether it carries a capacity, whatever mode the caller asked for, so a
        // system cannot claim transient mode and lack a C, or the reverse.
        const ThermalSystem derivedTransient = assembleIn(hand, thermal::ThermalAnalysisMode::Steady, capacityOf(7850.0, 460.0), heat);
        CHECK(derivedTransient.source().mode == thermal::ThermalAnalysisMode::Transient);
        const ThermalSystem derivedSteady = assembleIn(hand, thermal::ThermalAnalysisMode::Transient, std::nullopt, heat);
        CHECK(derivedSteady.source().mode == thermal::ThermalAnalysisMode::Steady);
        CHECK(problemOf2(derivedSteady, hand.dofs, constraints, initial, transient, {}) == TransientProblem::NotTransientSystem);
    }
    SECTION("a numbering or constraint map of another mesh") {
        const AsmHand other = makeTwoTet();
        CHECK(problemOf2(transientSystem, other.dofs, constraints, initial, transient, {}) == TransientProblem::DofMapMismatch);
        const thermal::ThermalConstraintMap otherConstraints = constraintsOf(other, {{0, 400.0}});
        CHECK(problemOf2(transientSystem, hand.dofs, otherConstraints, initial, transient, {}) == TransientProblem::ConstraintMismatch);
    }
    SECTION("settings") {
        for (const double dt : {0.0, -1.0, kNaN, kInf}) {
            CHECK(problemOf2(transientSystem, hand.dofs, constraints, initial, timingOf(dt, 100.0), {}) == TransientProblem::InvalidTimeStep);
        }
        for (const double end : {0.0, -1.0, kNaN, kInf, 5.0}) {  // 5 s < the 10 s step
            CHECK(problemOf2(transientSystem, hand.dofs, constraints, initial, timingOf(10.0, end), {}) == TransientProblem::InvalidEndTime);
        }
        CHECK(problemOf2(transientSystem, hand.dofs, constraints, initial,
                         TransientSettings{.timeStep = Time::fromSi(10.0), .endTime = Time::fromSi(100.0), .output = static_cast<TransientOutputPolicy>(9)},
                         {}) == TransientProblem::InvalidOutputPolicy);
        for (const double bad : {kNaN, kInf, -5.0}) {
            CHECK(problemOf2(transientSystem, hand.dofs, constraints, UniformInitialTemperature{Temperature::fromSi(bad)}, transient, {}) ==
                  TransientProblem::InvalidInitialTemperature);
        }
        CHECK(problemOf2(transientSystem, hand.dofs, constraints, initial, transient, ThermalSolverSettings{.relativeResidualTolerance = -1.0}) ==
              TransientProblem::InvalidSettings);
        // The grid's own resource guard, through the seam.
        CHECK(problemOf2(transientSystem, hand.dofs, constraints, initial, timingOf(1.0, 2.0e6), {}) == TransientProblem::TooManyTimeSteps);
    }
}

TEST_CASE("ThermalTransient_SparseNodeIdsAreLookedUpThroughTheNumbering", "[thermal][transient][seam][sparse-ids]") {
    // Node handles 10..13: the unknowns are 0..3, never the handles, and a lookup by node goes through the map.
    const AsmHand hand = makeAsmHand(unitTetNodes(10), {}, {{0, 1, 2, 3}});
    const ThermalSystem system = assembleIn(hand, thermal::ThermalAnalysisMode::Transient, capacityOf(7850.0, 460.0), generationHeat(hand, 1.0e6));
    const auto nodeHot = NodeId::fromValue(11);
    const thermal::ThermalDofIndex hotDof = *hand.dofs.dofOf(nodeHot);
    CHECK(hotDof.value() == 1);
    const thermal::ThermalConstraintMap constraints = constraintsOf(hand, {{static_cast<std::size_t>(hotDof.value()), 500.0}});
    const auto solved = thermal::solveTransientThermalSystem(system, hand.dofs, constraints, UniformInitialTemperature{Temperature::fromSi(300.0)},
                                                             timingOf(100.0, 300.0), {});
    INFO((solved.has_value() ? std::string{} : solved.error().error.message));
    REQUIRE(solved.has_value());
    const auto hot = solved->temperatureOf(3, hand.dofs, nodeHot);
    REQUIRE(hot.has_value());
    CHECK(hot->si() == 500.0);
    const auto cold = solved->temperatureOf(3, hand.dofs, NodeId::fromValue(10));
    REQUIRE(cold.has_value());
    CHECK(cold->si() > 300.0);
    // A node the mesh does not have is an error, never an index into the array.
    CHECK_FALSE(solved->temperatureOf(3, hand.dofs, NodeId::fromValue(9999)).has_value());
}

// ===========================================================================
// The vocabulary is stable
// ===========================================================================

TEST_CASE("ThermalTransient_DiagnosticNamesAreStableAndDistinct", "[thermal][transient][diagnostics]") {
    std::vector<std::string_view> names;
    for (std::uint8_t v = 0; v <= static_cast<std::uint8_t>(TransientProblem::ConstraintNotPreserved); ++v) {
        const std::string_view name = thermal::toString(static_cast<TransientProblem>(v));
        CHECK_FALSE(name.empty());
        CHECK(name != "unknown");
        names.push_back(name);
    }
    CHECK(names.size() == 21);
    std::sort(names.begin(), names.end());
    CHECK(std::adjacent_find(names.begin(), names.end()) == names.end());
    CHECK(thermal::toString(static_cast<TransientProblem>(200)) == "unknown");
    CHECK(thermal::toString(TransientProblem::SingularSystem) == "singular_system");
    CHECK(thermal::toString(TransientProblem::NotTransientSystem) == "not_transient_system");
    CHECK(thermal::toString(TransientProblem::TooManyTimeSteps) == "too_many_time_steps");

    std::vector<std::string_view> reasons;
    for (std::uint8_t v = 0; v <= static_cast<std::uint8_t>(TransientStaleReason::Settings); ++v) {
        const std::string_view name = thermal::toString(static_cast<TransientStaleReason>(v));
        CHECK(name != "unknown");
        reasons.push_back(name);
    }
    CHECK(reasons.size() == 13);
    std::sort(reasons.begin(), reasons.end());
    CHECK(std::adjacent_find(reasons.begin(), reasons.end()) == reasons.end());
    CHECK(thermal::toString(static_cast<TransientStaleReason>(200)) == "unknown");
    CHECK(thermal::toString(TransientStaleReason::InitialCondition) == "initial_condition");
}

TEST_CASE("ThermalTransient_StaleReasonsNameEachFieldAlone", "[thermal][transient][currentness]") {
    // Every field of the source compared on its own, on a source built by hand: the comparison, not a model, is under test.
    thermal::TransientSolutionSource base;
    base.initial = UniformInitialTemperature{Temperature::fromSi(300.0)};
    base.transient = timingOf(1.0, 10.0);
    using Reasons = std::vector<TransientStaleReason>;
    CHECK(thermal::staleReasons(base, base).empty());

    thermal::TransientSolutionSource edited = base;
    edited.initial.value = Temperature::fromSi(301.0);
    CHECK(thermal::staleReasons(base, edited) == Reasons{TransientStaleReason::InitialCondition});
    edited = base;
    edited.transient.timeStep = Time::fromSi(2.0);
    CHECK(thermal::staleReasons(base, edited) == Reasons{TransientStaleReason::TimeStep});
    edited = base;
    edited.transient.endTime = Time::fromSi(20.0);
    CHECK(thermal::staleReasons(base, edited) == Reasons{TransientStaleReason::EndTime});
    edited = base;
    edited.transient.output = TransientOutputPolicy::FinalOnly;
    CHECK(thermal::staleReasons(base, edited) == Reasons{TransientStaleReason::OutputPolicy});
    edited = base;
    edited.settings.pivotFloor = 1.0e-9;
    CHECK(thermal::staleReasons(base, edited) == Reasons{TransientStaleReason::Settings});
    edited = base;
    edited.system.mode = thermal::ThermalAnalysisMode::Transient;
    CHECK(thermal::staleReasons(base, edited) == Reasons{TransientStaleReason::Mode});
    // The assembled system's own fields, each mapped to its own reason.
    edited = base;
    edited.system.body = ObjectId::fromValue(7);
    CHECK(thermal::staleReasons(base, edited) == Reasons{TransientStaleReason::Body});
    edited = base;
    edited.system.mesh.generation = 3;
    CHECK(thermal::staleReasons(base, edited) == Reasons{TransientStaleReason::Mesh});
    edited = base;
    edited.system.geometry = meshing::GeometryRevision{5};
    CHECK(thermal::staleReasons(base, edited) == Reasons{TransientStaleReason::Geometry});
    edited = base;
    edited.system.material = MaterialId::fromValue(2);
    CHECK(thermal::staleReasons(base, edited) == Reasons{TransientStaleReason::Material});
    edited = base;
    edited.system.materialRevision = 9;
    CHECK(thermal::staleReasons(base, edited) == Reasons{TransientStaleReason::Material});
    edited = base;
    edited.system.loads.push_back(
        thermal::ThermalLoad{ThermalLoadId::fromValue(1), thermal::VolumetricHeatSourceLoad{.generation = VolumetricHeatSource::fromSi(1.0)}});
    CHECK(thermal::staleReasons(base, edited) == Reasons{TransientStaleReason::Loads});
    const FaceName face{ObjectId::fromValue(1), FaceSelector{.role = FaceRole::EndCap}};
    edited = base;
    edited.system.convections.push_back(convectionBc(1, face, 25.0, 293.15));
    CHECK(thermal::staleReasons(base, edited) == Reasons{TransientStaleReason::Convections});
    edited = base;
    edited.temperatures.push_back(temperatureBc(2, face, 400.0));
    CHECK(thermal::staleReasons(base, edited) == Reasons{TransientStaleReason::PrescribedTemperatures});
    // Several at once, in the enum's order.
    edited = base;
    edited.settings.pivotFloor = 1.0e-9;
    edited.initial.value = Temperature::fromSi(1.0);
    edited.transient.endTime = Time::fromSi(11.0);
    CHECK(thermal::staleReasons(base, edited) ==
          Reasons{TransientStaleReason::InitialCondition, TransientStaleReason::EndTime, TransientStaleReason::Settings});
}
