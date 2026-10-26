// RM-THERM-08 -- the transient reference models (P18-REFMOD-001).
//
//   THE LUMPED BODY. A 40 mm cube of a copper-like solid convecting from all six faces: with Bi = h Lc / k far below 0.1 its
//   temperature is uniform in time, and C dT/dt = -h A (T - Tinf) gives T(t) = Tinf + (T0 - Tinf) exp(-t/tau), tau = rho cp V/(hA).
//   The Biot number is COMPUTED FROM THE MESH'S OWN VOLUME AND AREA and asserted small, never assumed: a lumped closed form on a body
//   that is not lumped is not an oracle.
//
//   THE INSULATED BODY UNDER GENERATION. rho cp dT/dt = q''' is LINEAR in time, and backward Euler reproduces a constant source on a
//   uniform field EXACTLY, so every stored state is known to rounding.
//
// The bulk temperature is the CAPACITY-WEIGHTED mean, Tbar = (1^T C T)/(1^T C 1): for a uniform rho cp the consistent capacity's row
// sums are V_i/4 per node, so Tbar is the nodal mean weighted by the volume each node owns (the oracle's own weights). An
// unweighted nodal mean would depend on how the mesh happens to place its nodes.

#include "reference/ThermalReferenceSupport.hpp"

#include <algorithm>

namespace {

using namespace bettercad;
using namespace bettercad::test::thermalref;

/// The volume each node owns: sum over its tetrahedra of V/4 (the row sums of a uniform consistent capacity matrix, divided by rho cp).
[[nodiscard]] std::vector<double> nodalWeights(const meshing::Mesh& mesh) {
    std::unordered_map<std::uint64_t, std::size_t> index;
    for (std::size_t i = 0; i < mesh.nodes().size(); ++i) {
        index.emplace(mesh.nodes()[i].id.value(), i);
    }
    std::vector<double> w(mesh.nodes().size(), 0.0);
    for (const meshing::Tetrahedron& tet : mesh.tetrahedra()) {
        const double v = std::abs(oracleTet(mesh, tet).signedVolume());
        for (const meshing::NodeId id : tet.nodes) {
            w[index.at(id.value())] += v / 4.0;
        }
    }
    return w;
}

[[nodiscard]] double bulk(const std::vector<double>& weights, const thermal::ThermalPostState& state) {
    double sum = 0.0;
    double total = 0.0;
    for (std::size_t i = 0; i < weights.size(); ++i) {
        sum += weights[i] * state.temperatures[i].si();
        total += weights[i];
    }
    return sum / total;
}

[[nodiscard]] double boundaryArea(const meshing::Mesh& mesh) {
    double sum = 0.0;
    for (const meshing::Triangle& tri : mesh.triangles()) {
        std::array<Vec3, 3> p{};
        for (std::size_t c = 0; c < 3; ++c) {
            p[c] = positionOf(mesh.findNode(tri.nodes[c])->position);
        }
        sum += 0.5 * heat::norm(heat::cross(heat::minus(p[1], p[0]), heat::minus(p[2], p[0])));
    }
    return sum;
}

/// The lumped oracle for the MEASURED geometry of a run.
[[nodiscard]] heat::Lumped lumpedFor(Run& run) {
    const meshing::Mesh& mesh = run.mesh();
    return heat::Lumped{.conductivity = reference::kLumpedMaterial.conductivity,
                        .density = reference::kLumpedMaterial.density,
                        .specificHeat = reference::kLumpedMaterial.specificHeat,
                        .volume = meshVolume(mesh),
                        .area = boundaryArea(mesh),
                        .coefficient = reference::kLumpedCoefficient,
                        .initial = reference::kLumpedInitialKelvin,
                        .ambient = reference::kLumpedAmbientKelvin};
}

} // namespace

TEST_CASE("ThermalReference_RM08_TheLumpedAssumptionHolds", "[refmod][thermal][rm08]") {
    // V = 0.04^3 = 6.4e-5 m^3, A = 6 x 0.04^2 = 9.6e-3 m^2 (BY HAND), then from the mesh's own triangles and tetrahedra.
    //   Lc = V/A = 6.667 mm      Bi = h Lc / k = 25 x 0.006667 / 400 = 4.167e-4        (the criterion is Bi < 0.1)
    //   rho cp V = 8960 x 385 x 6.4e-5 = 220.7744 J/K       tau = 220.7744 / (25 x 9.6e-3) = 919.8933 s
    auto run = runOf(reference::buildThermalLumpedReferenceModel(), /*runNow=*/false);
    const heat::Lumped lumped = lumpedFor(*run);
    requireClose(lumped.volume, 6.4e-5, Tolerance{0.0, 1e-12}, "V measured from the mesh");
    requireClose(lumped.area, 9.6e-3, Tolerance{0.0, 1e-12}, "A measured from the mesh");
    requireClose(lumped.characteristicLength(), 6.4e-5 / 9.6e-3, kClassA, "Lc = V/A");
    requireClose(lumped.biot(), 25.0 * (6.4e-5 / 9.6e-3) / 400.0, kClassA, "Bi by hand");
    CHECK(lumped.biot() < 1.0e-3);
    CHECK(lumped.biot() < 0.1 / 100.0); // more than two orders of margin on the criterion
    requireClose(lumped.capacity(), 220.7744, Tolerance{0.0, 1e-12}, "rho cp V by hand");
    requireClose(lumped.timeConstant(), 220.7744 / 0.24, kClassA, "tau by hand");
    requireClose(lumped.timeConstant(), 919.89333333, Tolerance{1e-5, 0.0}, "tau = 919.893 s");
    evidence("RM-THERM-08 Bi " + format(lumped.biot()) + " tau " + format(lumped.timeConstant()) + " Lc_mm " + format(lumped.characteristicLength() * 1e3));
}

TEST_CASE("ThermalReference_RM08_LumpedTemperatureFollowsTheAnalyticalDecay", "[refmod][thermal][rm08]") {
    auto run = runOf(reference::buildThermalLumpedReferenceModel(100.0, 4600.0));
    const heat::Lumped lumped = lumpedFor(*run);
    const thermal::ThermalPostResult& post = run->post();
    const std::vector<double> w = nodalWeights(run->mesh());
    REQUIRE(post.stateCount() == 47); // t = 0, 100, ..., 4600

    const double rise = lumped.initial - lumped.ambient;
    const double r = 100.0 / lumped.timeConstant();
    double worstDiscrete = 0.0;
    double worstContinuous = 0.0;
    double worstSpread = 0.0;
    for (std::size_t i = 0; i < post.stateCount(); ++i) {
        const thermal::ThermalPostState& state = post.states()[i];
        REQUIRE(state.time.has_value());
        requireClose(state.time->si(), 100.0 * static_cast<double>(i), kClassA, "the stored time");
        const double tbar = bulk(w, state);
        worstDiscrete = std::max(worstDiscrete, std::abs(tbar - lumped.backwardEuler(i, 100.0)));
        worstContinuous = std::max(worstContinuous, std::abs(tbar - lumped.temperature(state.time->si())));
        double lo = 1e300;
        double hi = -1e300;
        for (const Temperature& t : state.temperatures) {
            lo = std::min(lo, t.si());
            hi = std::max(hi, t.si());
        }
        worstSpread = std::max(worstSpread, hi - lo);
    }
    evidence("RM-THERM-08 lumped: worst |Tbar - T_BE| " + format(worstDiscrete) + " worst |Tbar - T_exp| " + format(worstContinuous) +
             " worst nodal spread " + format(worstSpread) + " final Tbar " + format(bulk(w, post.finalState())) + " exp " +
             format(lumped.temperature(4600.0)) + " BE " + format(lumped.backwardEuler(46, 100.0)));

    // 1. THE DISCRETE ORACLE. The same ODE advanced by the same backward-Euler steps: what is left is the body's departure from
    //    uniformity, which is O(Bi) of the temperature difference (the lumped model's own error).
    CHECK(worstDiscrete <= lumped.biot() * rise);
    // 2. THE CONTINUOUS ONE. Backward Euler is first order: the error against exp(-t/tau) is at most (r/2) (x e^-x) rise with
    //    x = t/tau, whose maximum is (r/2)/e; allow 10 percent over it plus the Bi term.
    CHECK(worstContinuous <= 1.10 * (r / 2.0) / std::exp(1.0) * rise + lumped.biot() * rise);
    // 3. THE FIELD IS UNIFORM TO O(Bi). The bound is DERIVED, not fitted: the convective loss h dTs A is, to the interior, a uniform
    //    sink of h dTs / Lc per unit volume; no point lies farther than d from the surface (d = half the cube's side, from the mesh);
    //    under a uniform sink s the internal drop is at most s d^2 / (2k) (a slab of half-thickness d). So the nodal spread is at
    //    most  Bi dTs d^2 / (2 Lc^2), with dTs <= the initial difference. For this cube d = 3 Lc and the factor is 4.5.
    const Extent cube = extentOf(run->mesh());
    const double d = 0.5 * cube.along(0);
    const double Lc = lumped.characteristicLength();
    CHECK(worstSpread <= lumped.biot() * rise * d * d / (2.0 * Lc * Lc));
    // 4. AND IT IS A COOLING: the bulk temperature falls monotonically toward the ambient and never crosses it.
    double previous = 1e300;
    for (const thermal::ThermalPostState& state : post.states()) {
        const double tbar = bulk(w, state);
        CHECK(tbar < previous);
        CHECK(tbar > lumped.ambient);
        previous = tbar;
    }
    // the initial state is the initial condition, at every node
    for (const Temperature& t : post.states().front().temperatures) {
        requireClose(t.si(), 400.0, kClassA, "the initial temperature");
    }
}

TEST_CASE("ThermalReference_RM08_TimeStepRefinement", "[refmod][thermal][rm08]") {
    // dt = 200, 100, 50 s (r = dt/tau = 0.217, 0.109, 0.054). The error against exp(-t/tau) at the TIMES ALL THREE RUNS STORE (every
    // 200 s) must fall by close to 2 per halving -- backward Euler is first order. (The gate is order >= 0.8, a ratio of 1.74: the
    // theory says 1, and the Bi-sized departure from uniformity is two orders below the temporal error here.)
    std::vector<double> errors;
    for (const double dt : {200.0, 100.0, 50.0}) {
        INFO("dt " << dt);
        auto run = runOf(reference::buildThermalLumpedReferenceModel(dt, 4600.0));
        const heat::Lumped lumped = lumpedFor(*run);
        const thermal::ThermalPostResult& post = run->post();
        const std::vector<double> w = nodalWeights(run->mesh());
        double worst = 0.0;
        for (const thermal::ThermalPostState& state : post.states()) {
            const double t = state.time->si();
            if (std::fmod(t, 200.0) == 0.0) {
                worst = std::max(worst, std::abs(bulk(w, state) - lumped.temperature(t)));
            }
        }
        errors.push_back(worst);
        evidence("RM-THERM-08 refinement dt " + format(dt) + " steps " + std::to_string(post.stateCount() - 1) + " worst_error_K " + format(worst));
    }
    REQUIRE(errors.size() == 3);
    CHECK(errors[1] < errors[0] / 1.74);
    CHECK(errors[2] < errors[1] / 1.74);
    evidence("RM-THERM-08 observed order " + format(std::log2(errors[0] / errors[1])) + " then " + format(std::log2(errors[1] / errors[2])));
    // and it IS first order, not better: second order would mean the error is not the integrator's
    CHECK(errors[1] > errors[0] / 2.5);
    CHECK(errors[2] > errors[1] / 2.5);
}

TEST_CASE("ThermalReference_RM08_EnergyBalance", "[refmod][thermal][rm08]") {
    auto run = runOf(reference::buildThermalLumpedReferenceModel(100.0, 4600.0));
    const heat::Lumped lumped = lumpedFor(*run);
    const thermal::ThermalPostResult& post = run->post();
    const thermal::ThermalBalanceResult& balance = run->balance();
    const std::vector<double> w = nodalWeights(run->mesh());
    REQUIRE(balance.stateCount() == 46); // one entry per integration step

    // EVERY STEP: the heat convected into the body equals the storage rate, both negative (the body is cooling), and the two
    // are one number computed two ways.
    double external = 0.0;
    for (std::size_t j = 0; j < balance.stateCount(); ++j) {
        const thermal::ThermalHeatBalance& step = balance.states()[j];
        REQUIRE(step.stepSize.has_value());
        requireClose(step.stepSize->si(), 100.0, kClassA, "the actual step size");
        REQUIRE(step.terms.storedEnergyRate.has_value());
        const double storage = step.terms.storedEnergyRate->si();
        const double convection = step.terms.convectionHeat.si();
        CHECK(convection < 0.0);
        CHECK(storage < 0.0);
        requireClose(convection, storage, Tolerance{1e-9, 1e-9}, "convection into the body = rate of stored energy");
        external += 100.0 * convection;
        // INDEPENDENT: the storage rate is the capacity times the bulk temperature change over the step
        const double independent = lumped.capacity() * (bulk(w, post.states()[j + 1]) - bulk(w, post.states()[j])) / 100.0;
        requireClose(storage, independent, Tolerance{1e-9, 1e-9}, "stored energy rate against rho cp V dTbar/dt");
        CHECK(step.normalizedResidual() <= 1.0e-9);
    }
    // THE WHOLE RUN: the energy the convection removed equals the energy the body lost, computed from the endpoints alone.
    const double lost = lumped.capacity() * (bulk(w, post.finalState()) - bulk(w, post.states().front()));
    requireClose(external, lost, Tolerance{1e-9, 1e-9}, "sum of dt x convection against rho cp V (Tbar_N - T0)");
    REQUIRE(balance.integrated().has_value());
    requireClose(balance.integrated()->stored.si(), lost, Tolerance{1e-9, 1e-9}, "the balance's own stored energy");
    CHECK(std::abs(balance.integrated()->residual.si()) <= balance.integrated()->allowed.si());
    CHECK(balance.balanced());
    evidence("RM-THERM-08 energy lost " + format(lost) + " J, external " + format(external) + " J, worst normalized residual " +
             format(balance.worstNormalizedResidual()));
}

TEST_CASE("ThermalReference_RM08_SteadyLimit", "[refmod][thermal][rm08]") {
    // 20 time constants (18400 s), 200 s steps: after that long the body is at the ambient to a few microkelvin (measured 1.4e-6 K). The run is
    // long enough that the test is not made before the physics has had time (5 tau still leaves 0.7 K).
    auto run = runOf(reference::buildThermalLumpedReferenceModel(200.0, 18400.0));
    const heat::Lumped lumped = lumpedFor(*run);
    const std::vector<double> w = nodalWeights(run->mesh());
    const double tbar = bulk(w, run->post().finalState());
    evidence("RM-THERM-08 steady limit: 20 tau, Tbar - Tinf = " + format(tbar - lumped.ambient) + " K; BE closed form " +
             format(lumped.backwardEuler(92, 200.0) - lumped.ambient));
    CHECK(18400.0 / lumped.timeConstant() > 19.9);
    CHECK(tbar - lumped.ambient > 0.0);
    CHECK(tbar - lumped.ambient < 1.0e-5);
    // the discrete decay's value: measured difference 5.5e-9 K (the body's O(Bi) departure from uniformity at 20 tau), gate 1e-7 K, so a
    // time constant wrong by 1 percent (which moves it by about 2.8e-7 K at 20 tau) fails
    requireClose(tbar, lumped.backwardEuler(92, 200.0), Tolerance{1e-7, 0.0}, "and it is the discrete decay's value");
}

TEST_CASE("ThermalReference_RM08_FinalPartialStep", "[refmod][thermal][rm08]") {
    // dt = 0.3 s, t_end = 1.0 s: three whole steps and a remainder. The LAST stored time is 1.0, not 1.2, and the last step's size is
    // 0.1 -- so a balance that used the nominal 0.3 would see a storage rate three times too small in that step.
    auto run = runOf(reference::buildThermalInsulatedGenerationReferenceModel(0.3, 1.0));
    const thermal::ThermalPostResult& post = run->post();
    REQUIRE(post.stateCount() == 5);
    const std::array<double, 5> times{0.0, 0.3, 0.6, 0.9, 1.0};
    for (std::size_t i = 0; i < 5; ++i) {
        REQUIRE(post.states()[i].time.has_value());
        requireClose(post.states()[i].time->si(), times[i], kClassA, "the stored time");
    }
    CHECK(post.finalState().time->si() == 1.0); // exactly the end time
    const thermal::ThermalBalanceResult& balance = run->balance();
    REQUIRE(balance.stateCount() == 4);
    const std::array<double, 4> sizes{0.3, 0.3, 0.3, 0.1};
    const double generation = reference::kInsulatedGenerationWattsPerM3 * 6.4e-5; // 64 W
    requireClose(generation, 64.0, kClassA, "q''' V by hand");
    for (std::size_t j = 0; j < 4; ++j) {
        const thermal::ThermalHeatBalance& step = balance.states()[j];
        requireClose(step.stepSize->si(), sizes[j], Tolerance{1e-12, 1e-12}, "the ACTUAL step size");
        requireClose(step.time->si(), times[j + 1], kClassA, "the time at the step's end");
        // all of the generated heat is stored, in every step INCLUDING the short one
        requireClose(step.terms.generatedHeat.si(), generation, Tolerance{1e-9, 1e-9}, "generated heat");
        requireClose(step.terms.storedEnergyRate->si(), generation, Tolerance{1e-9, 1e-9}, "the stored energy rate in a step of size dt_actual");
    }
    evidence("RM-THERM-08 partial final step: last stored time " + format(post.finalState().time->si()) + " last step size " +
             format(balance.states().back().stepSize->si()) + " storage " + format(balance.states().back().terms.storedEnergyRate->si()));
}

TEST_CASE("ThermalReference_RM08_UniformGenerationTransient", "[refmod][thermal][rm08]") {
    // rho cp dT/dt = q''' on an insulated body: T(t) = T0 + q''' t /(rho cp), a line. Backward Euler is EXACT for it, so every
    // stored state, whatever the step, is the line's value to rounding and the field is uniform.
    for (const auto& [dt, end] : {std::pair{0.3, 1.0}, std::pair{0.25, 1.0}, std::pair{0.5, 2.0}}) {
        INFO("dt " << dt << " end " << end);
        auto run = runOf(reference::buildThermalInsulatedGenerationReferenceModel(dt, end));
        const thermal::ThermalPostResult& post = run->post();
        double worstLine = 0.0;
        double worstSpread = 0.0;
        for (const thermal::ThermalPostState& state : post.states()) {
            const double expected = heat::insulatedGenerationTemperature(state.time->si(), reference::kInsulatedInitialKelvin,
                                                                          reference::kInsulatedGenerationWattsPerM3, 8960.0, 385.0);
            double lo = 1e300;
            double hi = -1e300;
            for (const Temperature& t : state.temperatures) {
                worstLine = std::max(worstLine, std::abs(t.si() - expected));
                lo = std::min(lo, t.si());
                hi = std::max(hi, t.si());
            }
            worstSpread = std::max(worstSpread, hi - lo);
        }
        evidence("RM-THERM-08 insulated generation dt " + format(dt) + ": worst |T - line| " + format(worstLine) + " spread " + format(worstSpread));
        CHECK(worstLine <= 1.0e-9);
        CHECK(worstSpread <= 1.0e-9);
        CHECK(run->balance().worstNormalizedResidual() <= 1.0e-9);
        CHECK(run->balance().balanced());
    }
}


// ===========================================================================
// A non-uniform transient with a held cap and two DIFFERENT convections, against an independent dense integrator
// ===========================================================================

TEST_CASE("ThermalReference_RM08_NonUniformTransientAgainstIndependentBackwardEuler", "[refmod][thermal][rm08]") {
    // The lumped cube cannot tell a consistent capacity matrix from a lumped one (the bulk temperature is the same), cannot tell
    // one convection condition from another (all six are identical) and has no held node. This body has a held cap, an end-cap
    // convection (h 100, 300 K) and a side convection with DIFFERENT numbers (h 10, 350 K), starts uniform and develops a
    // gradient along z: the independent integrator below uses the consistent capacity rho cp V (1 + delta)/20, the Robin face
    // matrix h A (1 + delta)/12 and the load h Tinf A/3, none of it production code.
    auto run = runOf(reference::buildThermalTransientSlabReferenceModel());
    const meshing::Mesh& mesh = run->mesh();
    const heat::HeatMesh plain = plainMesh(mesh);
    std::unordered_map<std::uint64_t, std::size_t> index;
    for (std::size_t i = 0; i < mesh.nodes().size(); ++i) {
        index.emplace(mesh.nodes()[i].id.value(), i);
    }
    std::vector<std::optional<double>> held(plain.nodes.size());
    for (std::size_t i = 0; i < plain.nodes.size(); ++i) {
        if (std::abs(plain.nodes[i][2]) < 1e-9) {
            held[i] = reference::kTransientSlabHeldKelvin;
        }
    }
    std::vector<heat::RobinFace> robin;
    for (const meshing::Triangle& tri : mesh.triangles()) {
        std::array<std::size_t, 3> nodes{};
        std::array<Vec3, 3> p{};
        for (std::size_t c = 0; c < 3; ++c) {
            nodes[c] = index.at(tri.nodes[c].value());
            p[c] = plain.nodes[nodes[c]];
        }
        const auto allAt = [&p](std::size_t axis, double value) {
            return std::abs(p[0][axis] - value) < 1e-9 && std::abs(p[1][axis] - value) < 1e-9 && std::abs(p[2][axis] - value) < 1e-9;
        };
        if (allAt(2, 0.10)) {
            robin.push_back({nodes, reference::kTransientSlabEndCoefficient, reference::kTransientSlabEndAmbientKelvin});
        } else if (allAt(1, 0.0)) {
            robin.push_back({nodes, reference::kTransientSlabSideCoefficient, reference::kTransientSlabSideAmbientKelvin});
        }
    }
    REQUIRE(robin.size() >= 4); // two caps' worth of triangles at the end cap and the y = 0 side
    const std::vector<double> steps(10, 50.0);
    const auto expected = heat::solveTransient(plain, reference::kTransientSlabMaterial.conductivity, reference::kTransientSlabMaterial.density,
                                               reference::kTransientSlabMaterial.specificHeat, reference::kTransientSlabInitialKelvin, held,
                                               robin, steps);
    REQUIRE(expected.size() == 11);

    const thermal::ThermalPostResult& post = run->post();
    REQUIRE(post.stateCount() == 11);
    double worst = 0.0;
    for (std::size_t s = 0; s < post.stateCount(); ++s) {
        std::vector<double> produced;
        for (const Temperature& t : post.states()[s].temperatures) {
            produced.push_back(t.si());
        }
        worst = std::max(worst, maxAbsDifference(produced, expected[s]));
        // the held nodes are at the held value in EVERY state, including t = 0 (a prescription overrides the initial condition)
        for (std::size_t i = 0; i < produced.size(); ++i) {
            if (held[i].has_value()) {
                requireClose(produced[i], 400.0, kClassB, "a held node");
            }
        }
    }
    evidence("RM-THERM-08 non-uniform transient: worst |production - independent| over 11 states " + format(worst));
    CHECK(worst <= 1.0e-9);

    // THE TEST IS NOT VACUOUS: the field really is non-uniform and really moved.
    const std::vector<double> last = expected.back();
    const double hottest = *std::max_element(last.begin(), last.end());
    const double coolest = *std::min_element(last.begin(), last.end());
    CHECK(hottest - coolest > 5.0);
    CHECK(coolest > reference::kTransientSlabInitialKelvin);
    evidence("RM-THERM-08 non-uniform transient final field spans " + format(coolest) + " .. " + format(hottest) + " K");

    // The heat accounting of the run: every step balanced, with a held node supplying heat INTO the body early (the body is
    // cooler than the cap) and the stored-energy rate positive while the body heats.
    const thermal::ThermalBalanceResult& balance = run->balance();
    REQUIRE(balance.stateCount() == 10);
    CHECK(balance.balanced());
    CHECK(balance.worstNormalizedResidual() <= 1.0e-9);
    REQUIRE(balance.states().front().terms.storedEnergyRate.has_value());
    CHECK(balance.states().front().terms.storedEnergyRate->si() > 0.0);
    CHECK(balance.states().front().terms.prescribedTemperatureReaction.si() > 0.0);
}