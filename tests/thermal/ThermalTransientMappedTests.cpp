// P18-TRANSIENT-001: the transient solve against REAL P16 meshes and real documents.
//
// These are the AUTHORITY tests: a transient run takes an assembled TRANSIENT system,
// the prepared boundary and the canonical loads, conditions, initial condition and
// time settings, and it must refuse to mix generations even when every dimension agrees;
// it must hold the prescribed temperatures exactly at every state; it must relax to the
// steady solution the qualified steady solver finds; it must follow a lumped body's
// exponential and conserve energy step by step; and a failed run must never make an old
// result current.
//
// The arithmetic and the refusals of the algebra are checked on hand systems in
// ThermalTransientTests.cpp. Here a 40 x 30 x 20 mm steel block (k = 45 W/(m K),
// rho = 7850 kg/m3, cp = 460 J/(kg K), extruded along z) is meshed by P16, assembled
// by P18-ASSEMBLY and integrated. Every expectation is a CLOSED FORM, an energy
// identity, or the qualified steady solver's own answer for the same chain.

#include "ThermalTransientTestSupport.hpp"

#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <bit>
#include <format>

using namespace trtest;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using thermal::PreparedThermalBoundary;
using thermal::PreparedThermalLoads;
using thermal::SurfaceHeatFluxLoad;
using thermal::ThermalAnalysisMode;
using thermal::ThermalLoad;
using thermal::VolumetricHeatSourceLoad;

namespace {

constexpr double kLength = 0.020;       // the extrusion depth, along z
constexpr double kVolume = 0.040 * 0.030 * 0.020;
constexpr double kSurface = 2.0 * (0.040 * 0.030 + 0.040 * 0.020 + 0.030 * 0.020);
constexpr double kDensity = 7850.0;
constexpr double kSpecificHeat = 460.0;
constexpr double kAmbient = 293.15;

[[nodiscard]] ThermalLoad generationLoad(std::uint64_t id, double generation) {
    return ThermalLoad{ThermalLoadId::fromValue(id),
                       VolumetricHeatSourceLoad{.generation = VolumetricHeatSource::fromSi(generation)}};
}

[[nodiscard]] UniformInitialTemperature initialAt(double kelvin) {
    return UniformInitialTemperature{Temperature::fromSi(kelvin)};
}

[[nodiscard]] TransientSettings timing(double dt, double end, TransientOutputPolicy output = TransientOutputPolicy::EveryStep) {
    return TransientSettings{.timeStep = Time::fromSi(dt), .endTime = Time::fromSi(end), .output = output};
}

/// Everything a model-level solve consumes, prepared on one current mesh.
struct Chain {
    ThermalModel model;
    ThermalDofMap map;
    PreparedThermalLoads loads;
    PreparedThermalBoundary boundary;
    ThermalSystem system;
};

[[nodiscard]] Chain chainOn(const BoundaryStudy& study, const std::vector<ThermalLoad>& loads,
                            const std::vector<ThermalBoundaryCondition>& conditions,
                            ThermalAnalysisMode mode = ThermalAnalysisMode::Transient) {
    ThermalModel model = study.model();
    ThermalDofMap map = dofMapOf(model);
    Result<PreparedThermalLoads> preparedLoads = thermal::prepareThermalLoads(model, map, loads);
    INFO((preparedLoads.has_value() ? std::string{} : preparedLoads.error().message));
    REQUIRE(preparedLoads.has_value());
    Result<PreparedThermalBoundary> preparedBoundary = thermal::prepareThermalBoundary(model, map, conditions);
    INFO((preparedBoundary.has_value() ? std::string{} : preparedBoundary.error().message));
    REQUIRE(preparedBoundary.has_value());
    Result<ThermalSystem> system =
        thermal::assembleThermalSystem(study.document, model, mode, map, *preparedLoads, loads, *preparedBoundary, conditions);
    INFO((system.has_value() ? std::string{} : system.error().message));
    REQUIRE(system.has_value());
    return Chain{std::move(model), std::move(map), std::move(*preparedLoads), std::move(*preparedBoundary), std::move(*system)};
}

[[nodiscard]] Result<TransientThermalSolution> solveOn(const BoundaryStudy& study, const Chain& c,
                                                       const std::vector<ThermalLoad>& loads,
                                                       const std::vector<ThermalBoundaryCondition>& conditions,
                                                       const UniformInitialTemperature& initial, const TransientSettings& transient,
                                                       const ThermalSolverSettings& settings = {}) {
    return thermal::solveTransientThermal(study.document, c.model, c.map, c.system, c.boundary, loads, conditions, initial, transient,
                                          settings);
}

[[nodiscard]] TransientThermalSolution mustSolveOn(const BoundaryStudy& study, const Chain& c, const std::vector<ThermalLoad>& loads,
                                                   const std::vector<ThermalBoundaryCondition>& conditions,
                                                   const UniformInitialTemperature& initial, const TransientSettings& transient,
                                                   const ThermalSolverSettings& settings = {}) {
    Result<TransientThermalSolution> solved = solveOn(study, c, loads, conditions, initial, transient, settings);
    INFO((solved.has_value() ? std::string{} : solved.error().message));
    REQUIRE(solved.has_value());
    return std::move(*solved);
}

[[nodiscard]] double zOf(const meshing::Node& node) { return node.position.z.si(); }

[[nodiscard]] double sumOf(std::span<const double> values) {
    double total = 0.0;
    for (const double v : values) {
        total += v;
    }
    return total;
}

[[nodiscard]] std::vector<double> kelvinOf(std::span<const Temperature> values) {
    std::vector<double> out;
    for (const Temperature t : values) {
        out.push_back(t.si());
    }
    return out;
}

/// `1^T C T`, J.
[[nodiscard]] double storedEnergyOf(const ThermalSystem& system, std::span<const Temperature> t) {
    const auto product = system.capacity()->multiply(kelvinOf(t));
    REQUIRE(product.has_value());
    return sumOf(*product);
}

[[nodiscard]] double totalCapacityOf(const ThermalSystem& system) {
    const std::vector<double> ones(system.dofCount(), 1.0);
    const auto product = system.capacity()->multiply(ones);
    REQUIRE(product.has_value());
    return sumOf(*product);
}

/// The heat leaving through the prepared convection facets for the field @p t, W:
/// `sum(Kconv T - Qconv)`.
[[nodiscard]] double convectedHeatOf(const Chain& c, std::span<const Temperature> t) {
    double leaving = 0.0;
    for (const thermal::PreparedConvectionBoundary& convection : c.boundary.convections()) {
        for (const thermal::ConvectionFacet& facet : convection.facets) {
            for (std::size_t a = 0; a < 3; ++a) {
                for (std::size_t b = 0; b < 3; ++b) {
                    leaving += facet.term(a, b).si() * t[static_cast<std::size_t>(facet.dofs[b].value())].si();
                }
                leaving -= facet.term.source(a).si();
            }
        }
    }
    return leaving;
}

/// `h A` summed over every convection facet, W/K.
[[nodiscard]] double filmConductanceOf(const Chain& c) {
    double total = 0.0;
    for (const thermal::PreparedConvectionBoundary& convection : c.boundary.convections()) {
        for (const thermal::ConvectionFacet& facet : convection.facets) {
            for (std::size_t a = 0; a < 3; ++a) {
                for (std::size_t b = 0; b < 3; ++b) {
                    total += facet.term(a, b).si();
                }
            }
        }
    }
    return total;
}

[[nodiscard]] std::uint64_t fingerprintOf(const TransientThermalSolution& s) {
    std::uint64_t hash = 14695981039346656037ULL;
    const auto mix = [&hash](double value) {
        const auto bits = std::bit_cast<std::uint64_t>(value);
        for (int i = 0; i < 8; ++i) {
            hash ^= (bits >> (8 * i)) & 0xFFU;
            hash *= 1099511628211ULL;
        }
    };
    for (std::size_t i = 0; i < s.sampleCount(); ++i) {
        mix(s.timeOf(i).si());
        for (const Temperature t : s.temperaturesAt(i)) {
            mix(t.si());
        }
    }
    for (const thermal::TransientStepRecord& record : s.steps()) {
        mix(record.reducedResidual);
        mix(record.physicalResidual);
        mix(record.pivotRatio);
    }
    return hash;
}

} // namespace

// ===========================================================================
// The steady limit, against the qualified steady solver
// ===========================================================================

TEST_CASE("ThermalTransient_BlockRelaxesToTheClosedFormSteadyProfile", "[thermal][transient][block][steady-limit]") {
    // 400 K on the start cap, 300 K on the end cap, nothing else, initial 300 K everywhere. The transient settles to the
    // linear profile T(z) = 400 - 100 z / L (a closed form), and to the qualified steady solver's answer for the same chain.
    BoundaryStudy study;
    const std::vector<ThermalBoundaryCondition> conditions{temperatureBc(1, study.startCap(), 400.0),
                                                            temperatureBc(2, study.endCap(), 300.0)};
    const Chain c = chainOn(study, {}, conditions);
    const TransientThermalSolution solved =
        mustSolveOn(study, c, {}, conditions, initialAt(300.0), timing(5.0, 500.0, TransientOutputPolicy::FinalOnly));

    double worst = 0.0;
    for (const meshing::Node& node : c.model.mesh().mesh().nodes()) {
        const std::size_t dof = static_cast<std::size_t>(c.map.dofOf(node.id)->value());
        worst = std::max(worst, std::abs(solved.finalTemperatures()[dof].si() - (400.0 - 100.0 * zOf(node) / kLength)));
    }
    CHECK(worst <= 1e-6);

    const Chain steadyChain = chainOn(study, {}, conditions, ThermalAnalysisMode::Steady);
    const Result<SteadyThermalSolution> steady =
        thermal::solveSteadyThermal(study.document, steadyChain.model, steadyChain.map, steadyChain.system, steadyChain.boundary, {},
                                    conditions, ThermalSolverSettings{});
    REQUIRE(steady.has_value());
    double against = 0.0;
    for (std::size_t i = 0; i < solved.dofCount(); ++i) {
        against = std::max(against, std::abs(solved.finalTemperatures()[i].si() - steady->temperatures()[i].si()));
    }
    CHECK(against <= 1e-6);

    // Bookkeeping: one sample at t_end, a hundred steps, every prescribed unknown exact, the 400 K nodes were overrides.
    CHECK(solved.sampleCount() == 1);
    CHECK(solved.timeOf(0).si() == 500.0);
    CHECK(solved.steps().size() == 100);
    std::size_t hot = 0;
    for (const thermal::ThermalTemperatureConstraint& constraint : c.boundary.constraints().constrained()) {
        CHECK(solved.finalTemperatures()[static_cast<std::size_t>(constraint.dof.value())].si() == constraint.temperature.si());
        if (constraint.temperature.si() == 400.0) {
            ++hot;
        }
    }
    CHECK(solved.initialOverrides() == hot);
    CHECK(solved.initialOverrides() > 0);
    CHECK(solved.worstReducedResidual() <= 1e-9);
    CHECK(solved.worstPhysicalResidual() <= 1e-9);
    // "Worst" is the maximum over the records, not a minimum or a last value.
    double maxReduced = 0.0;
    double maxPhysical = 0.0;
    for (const thermal::TransientStepRecord& record : solved.steps()) {
        maxReduced = std::max(maxReduced, record.reducedResidual);
        maxPhysical = std::max(maxPhysical, record.physicalResidual);
        CHECK(solved.worstReducedResidual() >= record.reducedResidual);
        CHECK(solved.worstPhysicalResidual() >= record.physicalResidual);
    }
    CHECK(solved.worstReducedResidual() == maxReduced);
    CHECK(solved.worstPhysicalResidual() == maxPhysical);
    CHECK(maxReduced > 0.0);
    WARN(std::format("block, 400 K -> 300 K caps, initial 300 K, dt = 5 s over 500 s: max |T - closed form| = {:.2e} K, max |T - steady "
                     "solver| = {:.2e} K, worst step residuals {:.2e} (reduced) {:.2e} (physical), {} initial overrides",
                     worst, against, solved.worstReducedResidual(), solved.worstPhysicalResidual(), solved.initialOverrides()));
}

TEST_CASE("ThermalTransient_BlockWithGenerationAndConvectionRelaxesToTheSteadySolve", "[thermal][transient][block][steady-limit]") {
    // Dirichlet + convection + generation: the whole boundary vocabulary at once, to the same chain's steady answer.
    BoundaryStudy study;
    const std::vector<ThermalLoad> loads{generationLoad(1, 1.0e5)};
    const std::vector<ThermalBoundaryCondition> conditions{temperatureBc(1, study.startCap(), 400.0),
                                                            convectionBc(2, study.endCap(), 25.0, kAmbient)};
    const Chain c = chainOn(study, loads, conditions);
    const TransientThermalSolution solved =
        mustSolveOn(study, c, loads, conditions, initialAt(kAmbient), timing(5.0, 500.0, TransientOutputPolicy::FinalOnly));
    const Chain steadyChain = chainOn(study, loads, conditions, ThermalAnalysisMode::Steady);
    const Result<SteadyThermalSolution> steady =
        thermal::solveSteadyThermal(study.document, steadyChain.model, steadyChain.map, steadyChain.system, steadyChain.boundary,
                                    loads, conditions, ThermalSolverSettings{});
    REQUIRE(steady.has_value());
    double against = 0.0;
    double hottest = 0.0;
    double coldest = 1.0e9;
    for (std::size_t i = 0; i < solved.dofCount(); ++i) {
        against = std::max(against, std::abs(solved.finalTemperatures()[i].si() - steady->temperatures()[i].si()));
        hottest = std::max(hottest, solved.finalTemperatures()[i].si());
        coldest = std::min(coldest, solved.finalTemperatures()[i].si());
    }
    CHECK(against <= 1e-6);
    // Moved off the 293.15 K start: the held face is the hottest node (this mesh has nodes on the caps only), and the convective
    // cap settles between the ambient and the held 400 K.
    CHECK(hottest == 400.0);
    CHECK(coldest > kAmbient + 50.0);
    CHECK(coldest < 400.0);
    WARN(std::format("block, generation + Dirichlet + convection: max |T(500 s) - steady solve| = {:.2e} K, T in [{:.6f}, {:.6f}] K", against,
                     coldest, hottest));
}

// ===========================================================================
// The lumped benchmark and the energy identity
// ===========================================================================

TEST_CASE("ThermalTransient_BlockCoolingUnderConvectionFollowsTheLumpedExponential", "[thermal][transient][block][lumped][energy]") {
    // All six faces convect (h = 25 W/(m2 K) to 293.15 K); Biot = h (V/A) / k = 2.6e-3, so the block is nearly isothermal and
    // behaves as a lumped body with tau = rho cp V / (h A). Backward Euler on a LUMPED body multiplies the excess by 1/(1 + dt/tau)
    // per step, which the mass-weighted mean must reproduce; the ENERGY IDENTITY holds exactly for the mesh, step by step.
    BoundaryStudy study;
    const double h = 25.0;
    const std::vector<ThermalBoundaryCondition> conditions{
        convectionBc(1, study.startCap(), h, kAmbient), convectionBc(2, study.endCap(), h, kAmbient),
        convectionBc(3, study.side(0), h, kAmbient),    convectionBc(4, study.side(1), h, kAmbient),
        convectionBc(5, study.side(2), h, kAmbient),    convectionBc(6, study.side(3), h, kAmbient)};
    const Chain c = chainOn(study, {}, conditions);
    REQUIRE(c.boundary.constraints().isEmpty());

    const double capacity = totalCapacityOf(c.system);
    const double film = filmConductanceOf(c);
    CHECK_THAT(capacity, WithinRel(kDensity * kSpecificHeat * kVolume, 1e-9));
    CHECK_THAT(film, WithinRel(h * kSurface, 1e-9));
    const double tau = capacity / film;
    CHECK_THAT(tau, WithinRel(kDensity * kSpecificHeat * kVolume / (h * kSurface), 1e-9));
    CHECK(h * (kVolume / kSurface) / 45.0 < 0.01);  // the Biot number

    const double dt = 10.0;
    const std::size_t stepCount = 100;
    const TransientThermalSolution solved =
        mustSolveOn(study, c, {}, conditions, initialAt(400.0), timing(dt, dt * static_cast<double>(stepCount)));
    REQUIRE(solved.sampleCount() == stepCount + 1);

    double worstMean = 0.0;
    double worstEnergy = 0.0;
    for (std::size_t n = 0; n <= stepCount; ++n) {
        const double mean = storedEnergyOf(c.system, solved.temperaturesAt(n)) / capacity;
        const double lumped = kAmbient + (400.0 - kAmbient) * std::pow(1.0 + dt / tau, -static_cast<double>(n));
        worstMean = std::max(worstMean, std::abs(mean - lumped));
        if (n > 0) {
            // dE/dt = -(heat leaving through the film), at the NEW state: the backward-Euler identity, exactly.
            const double stored = (storedEnergyOf(c.system, solved.temperaturesAt(n)) - storedEnergyOf(c.system, solved.temperaturesAt(n - 1))) / dt;
            const double leaving = convectedHeatOf(c, solved.temperaturesAt(n));
            worstEnergy = std::max(worstEnergy, std::abs(stored + leaving) / std::abs(leaving));
        }
    }
    CHECK(worstMean <= 0.5);
    CHECK(worstEnergy <= 1e-9);

    // The continuous lumped exponential is a first-order neighbour of the discrete one.
    const double continuous = kAmbient + (400.0 - kAmbient) * std::exp(-solved.timeOf(stepCount).si() / tau);
    const double finalMean = storedEnergyOf(c.system, solved.temperaturesAt(stepCount)) / capacity;
    CHECK(std::abs(finalMean - continuous) < 1.0);

    // And the body really is nearly isothermal.
    double lowest = 1.0e9;
    double highest = 0.0;
    for (const Temperature t : solved.temperaturesAt(stepCount)) {
        lowest = std::min(lowest, t.si());
        highest = std::max(highest, t.si());
    }
    CHECK(highest - lowest < 0.5);
    WARN(std::format("lumped block: tau = {:.3f} s (rho cp V/(h A) = {:.3f}), Bi = {:.2e}; mass-weighted mean vs discrete lumped "
                     "exponential: max {:.3e} K over 100 steps; vs continuous at t = {:.0f} s: {:.3e} K; spatial spread at the end "
                     "{:.3e} K; per-step energy identity max relative error {:.2e}",
                     tau, kDensity * kSpecificHeat * kVolume / (h * kSurface), h * (kVolume / kSurface) / 45.0, worstMean,
                     solved.timeOf(stepCount).si(), std::abs(finalMean - continuous), highest - lowest, worstEnergy));
}

TEST_CASE("ThermalTransient_ABlockAtItsAmbientDoesNotMove", "[thermal][transient][block][constant]") {
    // A real mesh with no loads and a uniform start that is already in equilibrium: the constant temperature is the solution.
    BoundaryStudy study;
    SECTION("convection on every face to the temperature it starts at") {
        const std::vector<ThermalBoundaryCondition> conditions{
            convectionBc(1, study.startCap(), 25.0, kAmbient), convectionBc(2, study.endCap(), 25.0, kAmbient),
            convectionBc(3, study.side(0), 25.0, kAmbient),    convectionBc(4, study.side(1), 25.0, kAmbient),
            convectionBc(5, study.side(2), 25.0, kAmbient),    convectionBc(6, study.side(3), 25.0, kAmbient)};
        const Chain c = chainOn(study, {}, conditions);
        const TransientThermalSolution solved = mustSolveOn(study, c, {}, conditions, initialAt(kAmbient), timing(10.0, 200.0));
        double worst = 0.0;
        for (std::size_t n = 0; n < solved.sampleCount(); ++n) {
            for (const Temperature t : solved.temperaturesAt(n)) {
                worst = std::max(worst, std::abs(t.si() - kAmbient));
            }
        }
        CHECK(worst <= 1e-10);
        WARN(std::format("block at its ambient ({} K), film on all faces, 20 steps: max |T - T_inf| = {:.2e} K", kAmbient, worst));
    }
    SECTION("both caps held at the temperature it starts at, adiabatic sides") {
        const std::vector<ThermalBoundaryCondition> conditions{temperatureBc(1, study.startCap(), 350.0), temperatureBc(2, study.endCap(), 350.0)};
        const Chain c = chainOn(study, {}, conditions);
        const TransientThermalSolution solved = mustSolveOn(study, c, {}, conditions, initialAt(350.0), timing(10.0, 200.0));
        CHECK(solved.initialOverrides() == 0);
        double worst = 0.0;
        for (std::size_t n = 0; n < solved.sampleCount(); ++n) {
            for (const Temperature t : solved.temperaturesAt(n)) {
                worst = std::max(worst, std::abs(t.si() - 350.0));
            }
        }
        CHECK(worst <= 1e-10);
    }
    SECTION("no condition at all and no load: the insulated block keeps its uniform temperature") {
        const Chain c = chainOn(study, {}, {});
        const TransientThermalSolution solved = mustSolveOn(study, c, {}, {}, initialAt(321.5), timing(10.0, 100.0));
        for (std::size_t n = 0; n < solved.sampleCount(); ++n) {
            for (const Temperature t : solved.temperaturesAt(n)) {
                CHECK_THAT(t.si(), WithinAbs(321.5, 1e-10));
            }
        }
    }
}

TEST_CASE("ThermalTransient_InsulatedBlockWithGenerationSucceedsAndGainsEnergyExactly", "[thermal][transient][block][neumann][singularity]") {
    // 60 W of generation and NO temperature and NO convection. The steady problem has no solution (the constant mode is in the
    // null space) and is refused; the transient one does, because C/dt is positive definite and the initial temperature fixes the
    // level. The stored energy rises by exactly 60 W * t.
    BoundaryStudy study;
    const std::vector<ThermalLoad> loads{generationLoad(1, 2.5e6)};
    const Chain steadyChain = chainOn(study, loads, {}, ThermalAnalysisMode::Steady);
    const auto refused = thermal::steadySolveFailure(study.document, steadyChain.model, steadyChain.map, steadyChain.system,
                                                     steadyChain.boundary, loads, {}, ThermalSolverSettings{});
    REQUIRE(refused.has_value());
    CHECK(refused->problem == SteadySolveProblem::MissingTemperatureDatum);

    const Chain c = chainOn(study, loads, {});
    REQUIRE(c.boundary.constraints().isEmpty());
    const TransientThermalSolution solved = mustSolveOn(study, c, loads, {}, initialAt(300.0), timing(10.0, 100.0));
    const double generated = 2.5e6 * kVolume;
    CHECK_THAT(generated, WithinRel(60.0, 1e-12));
    const double e0 = storedEnergyOf(c.system, solved.temperaturesAt(0));
    for (std::size_t n = 1; n <= 10; ++n) {
        const double gained = storedEnergyOf(c.system, solved.temperaturesAt(n)) - e0;
        CHECK_THAT(gained, WithinRel(generated * 10.0 * static_cast<double>(n), 1e-9));
    }
    const double rise = (storedEnergyOf(c.system, solved.finalTemperatures()) - e0) / totalCapacityOf(c.system);
    CHECK_THAT(rise, WithinRel(generated * 100.0 / (kDensity * kSpecificHeat * kVolume), 1e-9));
    CHECK(solved.constrainedCount() == 0);
    CHECK(solved.freeEquations() == solved.dofCount());
    WARN(std::format("insulated block, 60 W: steady refused (missing_temperature_datum); transient accepted, mean temperature rise "
                     "{:.6f} K over 100 s (60*100/(rho cp V) = {:.6f} K)",
                     rise, generated * 100.0 / (kDensity * kSpecificHeat * kVolume)));
}

// ===========================================================================
// The definition overload
// ===========================================================================

TEST_CASE("ThermalTransient_SolvesADefinitionAndRefusesASteadyOne", "[thermal][transient][definition]") {
    BoundaryStudy study;
    const std::vector<ThermalLoad> loads{generationLoad(1, 1.0e5)};
    const std::vector<ThermalBoundaryCondition> conditions{temperatureBc(1, study.startCap(), 400.0),
                                                            convectionBc(2, study.endCap(), 25.0, kAmbient)};
    const TransientSettings transient = timing(5.0, 50.0);
    REQUIRE(study.setDefinition(ThermalAnalysisDefinition{.mesh = study.control,
                                                         .mode = ThermalAnalysisMode::Transient,
                                                         .loads = loads,
                                                         .boundaryConditions = conditions,
                                                         .initialCondition = initialAt(300.0),
                                                         .transient = transient})
                .has_value());
    const Chain c = chainOn(study, loads, conditions);
    const Result<TransientThermalSolution> fromDefinition =
        thermal::solveTransientThermal(study.document, c.model, c.map, c.system, c.boundary, study.definition());
    INFO((fromDefinition.has_value() ? std::string{} : fromDefinition.error().message));
    REQUIRE(fromDefinition.has_value());
    const TransientThermalSolution explicitCall = mustSolveOn(study, c, loads, conditions, initialAt(300.0), transient);
    CHECK(*fromDefinition == explicitCall);
    CHECK_FALSE(thermal::transientSolveFailure(study.document, c.model, c.map, c.system, c.boundary, study.definition()).has_value());

    // A steady definition is not a transient one.
    REQUIRE(study.setDefinition(ThermalAnalysisDefinition{.mesh = study.control, .loads = loads, .boundaryConditions = conditions}).has_value());
    const auto refused = thermal::transientSolveFailure(study.document, c.model, c.map, c.system, c.boundary, study.definition());
    REQUIRE(refused.has_value());
    CHECK(refused->problem == TransientProblem::NotTransientDefinition);
    const Result<TransientThermalSolution> viaResult =
        thermal::solveTransientThermal(study.document, c.model, c.map, c.system, c.boundary, study.definition());
    REQUIRE_FALSE(viaResult.has_value());
    CHECK_THAT(viaResult.error().message, ContainsSubstring("not a transient one"));

    // A STEADY-mode definition that carries transient fields (the canonical validate would refuse it; this function must not
    // take its word for it) is not a transient one either.
    ThermalAnalysisDefinition confused{.mesh = study.control,
                                       .mode = ThermalAnalysisMode::Steady,
                                       .initialCondition = initialAt(300.0),
                                       .transient = transient};
    CHECK(thermal::transientSolveFailure(study.document, c.model, c.map, c.system, c.boundary, confused)->problem ==
          TransientProblem::NotTransientDefinition);

    // A hand-made definition in transient mode that lacks its settings is refused the same way.
    ThermalAnalysisDefinition incomplete{.mesh = study.control, .mode = ThermalAnalysisMode::Transient};
    CHECK(thermal::transientSolveFailure(study.document, c.model, c.map, c.system, c.boundary, incomplete)->problem ==
          TransientProblem::NotTransientDefinition);
    incomplete.initialCondition = initialAt(300.0);
    CHECK(thermal::transientSolveFailure(study.document, c.model, c.map, c.system, c.boundary, incomplete)->problem ==
          TransientProblem::NotTransientDefinition);
}

// ===========================================================================
// Provenance and currentness
// ===========================================================================

TEST_CASE("ThermalTransient_SolutionRecordsExactlyItsInputs", "[thermal][transient][currentness]") {
    BoundaryStudy study;
    const std::vector<ThermalLoad> loads{generationLoad(1, 1.0e5)};
    const std::vector<ThermalBoundaryCondition> conditions{temperatureBc(1, study.startCap(), 400.0),
                                                            convectionBc(2, study.endCap(), 25.0, kAmbient)};
    const Chain c = chainOn(study, loads, conditions);
    const UniformInitialTemperature initial = initialAt(310.0);
    const TransientSettings transient = timing(4.0, 20.0, TransientOutputPolicy::FinalOnly);
    const ThermalSolverSettings settings{.relativeResidualTolerance = 1.0e-10, .pivotFloor = 1.0e-11};
    const TransientThermalSolution solved = mustSolveOn(study, c, loads, conditions, initial, transient, settings);
    const thermal::TransientSolutionSource& source = solved.source();

    // Every recorded field is compared with the INPUT, not with a second call of the same function.
    CHECK(source.system == c.system.source());
    CHECK(source.system.mode == ThermalAnalysisMode::Transient);
    CHECK(source.system.loads == loads);
    REQUIRE(source.temperatures.size() == 1);
    CHECK(source.temperatures[0] == conditions[0]);  // ONLY the prescribed temperature; the convection is in the system
    CHECK(source.initial == initial);
    CHECK(source.transient == transient);
    CHECK(source.transient.output == TransientOutputPolicy::FinalOnly);
    CHECK(source.settings == settings);
    CHECK(source.settings != ThermalSolverSettings{});
    const auto current = thermal::currentTransientSolutionSource(study.document, c.model, loads, conditions, initial, transient, settings);
    REQUIRE(current.has_value());
    CHECK(source == *current);
    CHECK(thermal::isCurrent(solved, study.document, c.model, loads, conditions, initial, transient, settings));
    CHECK(thermal::requireCurrent(solved, study.document, c.model, loads, conditions, initial, transient, settings).has_value());
    CHECK(solved.describes(c.map));
    CHECK(solved.grid().stepCount() == 5);
    CHECK(solved.grid().endTime() == 20.0);
}

TEST_CASE("ThermalTransient_EditsStaleExactlyWhatTheyChange", "[thermal][transient][currentness]") {
    BoundaryStudy study;
    const std::vector<ThermalLoad> loads{generationLoad(1, 1.0e5)};
    const std::vector<ThermalBoundaryCondition> conditions{temperatureBc(1, study.startCap(), 400.0),
                                                            convectionBc(2, study.endCap(), 25.0, kAmbient)};
    const Chain c = chainOn(study, loads, conditions);
    const UniformInitialTemperature initial = initialAt(300.0);
    const TransientSettings transient = timing(5.0, 50.0);
    const TransientThermalSolution solved = mustSolveOn(study, c, loads, conditions, initial, transient);

    const auto reasonsFor = [&](const std::vector<ThermalLoad>& l, const std::vector<ThermalBoundaryCondition>& b,
                                const UniformInitialTemperature& i, const TransientSettings& t, const ThermalSolverSettings& s) {
        const auto now = thermal::currentTransientSolutionSource(study.document, c.model, l, b, i, t, s);
        REQUIRE(now.has_value());
        return thermal::staleReasons(solved.source(), *now);
    };
    using Reasons = std::vector<TransientStaleReason>;
    const ThermalSolverSettings defaults{};

    SECTION("nothing at all leaves it current") {
        CHECK(reasonsFor(loads, conditions, initial, transient, defaults).empty());
    }
    SECTION("a prescribed-temperature edit stales the SOLUTION, not the assembled system") {
        std::vector<ThermalBoundaryCondition> edited = conditions;
        edited[0] = temperatureBc(1, study.startCap(), 410.0);
        CHECK(reasonsFor(loads, edited, initial, transient, defaults) == Reasons{TransientStaleReason::PrescribedTemperatures});
        CHECK(thermal::isCurrent(c.system, study.document, c.model, ThermalAnalysisMode::Transient, loads, edited));
    }
    SECTION("an initial-temperature edit") {
        CHECK(reasonsFor(loads, conditions, initialAt(301.0), transient, defaults) == Reasons{TransientStaleReason::InitialCondition});
        CHECK_FALSE(thermal::isCurrent(solved, study.document, c.model, loads, conditions, initialAt(301.0), transient, defaults));
    }
    SECTION("a time-step edit") {
        CHECK(reasonsFor(loads, conditions, initial, timing(2.5, 50.0), defaults) == Reasons{TransientStaleReason::TimeStep});
    }
    SECTION("an end-time edit") {
        CHECK(reasonsFor(loads, conditions, initial, timing(5.0, 100.0), defaults) == Reasons{TransientStaleReason::EndTime});
    }
    SECTION("an output-policy edit") {
        CHECK(reasonsFor(loads, conditions, initial, timing(5.0, 50.0, TransientOutputPolicy::FinalOnly), defaults) ==
              Reasons{TransientStaleReason::OutputPolicy});
    }
    SECTION("a solver-setting edit stales the solution only") {
        const ThermalSolverSettings edited{.relativeResidualTolerance = 1.0e-7, .pivotFloor = 1.0e-12};
        CHECK(reasonsFor(loads, conditions, initial, transient, edited) == Reasons{TransientStaleReason::Settings});
        CHECK(thermal::isCurrent(c.system, study.document, c.model, ThermalAnalysisMode::Transient, loads, conditions));
    }
    SECTION("a convection edit stales both") {
        std::vector<ThermalBoundaryCondition> edited = conditions;
        edited[1] = convectionBc(2, study.endCap(), 30.0, kAmbient);
        CHECK(reasonsFor(loads, edited, initial, transient, defaults) == Reasons{TransientStaleReason::Convections});
        CHECK_FALSE(thermal::isCurrent(c.system, study.document, c.model, ThermalAnalysisMode::Transient, loads, edited));
    }
    SECTION("a load edit stales both") {
        const std::vector<ThermalLoad> edited{generationLoad(1, 2.0e5)};
        CHECK(reasonsFor(edited, conditions, initial, transient, defaults) == Reasons{TransientStaleReason::Loads});
    }
    SECTION("a specific-heat edit stales both, although K does not change") {
        materials::ThermalProperties properties = features::findMaterial(study.document, study.material)->definition().thermal;
        properties.specificHeatCapacity = materials::MaterialProperty<SpecificHeatCapacity>::known(500_J_per_kg_K);
        REQUIRE(features::setMaterialThermal(study.document, study.material, properties).has_value());
        CHECK_FALSE(thermal::isCurrent(solved, study.document, c.model, loads, conditions, initial, transient, defaults));
        const auto refused =
            thermal::transientSolveFailure(study.document, c.model, c.map, c.system, c.boundary, loads, conditions, initial, transient, defaults);
        REQUIRE(refused.has_value());
        CHECK(refused->problem == TransientProblem::StaleSystem);
        CHECK_THAT(refused->error.message, ContainsSubstring("material"));
    }
}

TEST_CASE("ThermalTransient_RejectsStaleBoundaryAfterATemperatureEdit", "[thermal][transient][currentness]") {
    BoundaryStudy study;
    const std::vector<ThermalBoundaryCondition> conditions{temperatureBc(1, study.startCap(), 400.0),
                                                            temperatureBc(2, study.endCap(), 300.0)};
    const Chain c = chainOn(study, {}, conditions);
    const TransientThermalSolution first = mustSolveOn(study, c, {}, conditions, initialAt(300.0), timing(10.0, 100.0));

    const std::vector<ThermalBoundaryCondition> edited{temperatureBc(1, study.startCap(), 450.0), temperatureBc(2, study.endCap(), 300.0)};
    const auto refused = thermal::transientSolveFailure(study.document, c.model, c.map, c.system, c.boundary, {}, edited,
                                                         initialAt(300.0), timing(10.0, 100.0), ThermalSolverSettings{});
    REQUIRE(refused.has_value());
    CHECK(refused->problem == TransientProblem::StaleBoundary);
    CHECK_THAT(refused->error.message, ContainsSubstring("conditions"));
    // The assembled system is still current (K, C and Q do not contain T_bar), so ONLY the boundary is re-prepared.
    const PreparedThermalBoundary fresh = prepareOn(c.model, c.map, edited);
    const auto second = thermal::solveTransientThermal(study.document, c.model, c.map, c.system, fresh, {}, edited, initialAt(300.0),
                                                       timing(10.0, 100.0), ThermalSolverSettings{});
    INFO((second.has_value() ? std::string{} : second.error().message));
    REQUIRE(second.has_value());
    CHECK_FALSE(thermal::isCurrent(first, study.document, c.model, {}, edited, initialAt(300.0), timing(10.0, 100.0), ThermalSolverSettings{}));
    CHECK(thermal::isCurrent(*second, study.document, c.model, {}, edited, initialAt(300.0), timing(10.0, 100.0), ThermalSolverSettings{}));
    // 450 K -> 300 K: the converged profile moved.
    for (const meshing::Node& node : c.model.mesh().mesh().nodes()) {
        const std::size_t dof = static_cast<std::size_t>(c.map.dofOf(node.id)->value());
        CHECK_THAT(second->finalTemperatures()[dof].si(), WithinAbs(450.0 - 150.0 * zOf(node) / kLength, 1e-5));
    }
}

TEST_CASE("ThermalTransient_RemeshStalesTheSolutionAndRefusesTheOldPieces", "[thermal][transient][currentness]") {
    BoundaryStudy study;
    const std::vector<ThermalBoundaryCondition> conditions{temperatureBc(1, study.startCap(), 400.0),
                                                            temperatureBc(2, study.endCap(), 300.0)};
    const TransientSettings transient = timing(10.0, 100.0);
    std::optional<ThermalSystem> oldSystem;
    std::optional<TransientThermalSolution> oldSolution;
    std::optional<ThermalDofMap> oldMap;
    std::optional<PreparedThermalBoundary> oldBoundary;
    std::optional<thermal::ThermalConstraintMap> oldConstraints;
    meshing::MeshStamp stamp1{};
    std::size_t nodes1 = 0;
    {
        const Chain c = chainOn(study, {}, conditions);
        oldSolution = mustSolveOn(study, c, {}, conditions, initialAt(300.0), transient);
        oldSystem = c.system;
        oldMap = c.map;
        oldBoundary = c.boundary;
        oldConstraints = c.boundary.constraints();
        stamp1 = c.model.mesh().mesh().stamp();
        nodes1 = c.model.mesh().nodeCount();
    }
    study.remesh();
    const Chain c2 = chainOn(study, {}, conditions);
    // THE ADVERSARIAL CASE: identical counts, identical numeric ids, a different stamp.
    REQUIRE(c2.model.mesh().nodeCount() == nodes1);
    CHECK_FALSE(c2.model.mesh().mesh().stamp() == stamp1);
    CHECK(oldSolution->dofCount() == c2.map.dofCount());
    CHECK_FALSE(thermal::isCurrent(*oldSolution, study.document, c2.model, {}, conditions, initialAt(300.0), transient, ThermalSolverSettings{}));
    const auto now = thermal::currentTransientSolutionSource(study.document, c2.model, {}, conditions, initialAt(300.0), transient, ThermalSolverSettings{});
    REQUIRE(now.has_value());
    const auto reasons = thermal::staleReasons(oldSolution->source(), *now);
    CHECK(std::ranges::find(reasons, TransientStaleReason::Mesh) != reasons.end());

    const auto refused = thermal::transientSolveFailure(study.document, c2.model, c2.map, *oldSystem, c2.boundary, {}, conditions,
                                                         initialAt(300.0), transient, ThermalSolverSettings{});
    REQUIRE(refused.has_value());
    CHECK(refused->problem == TransientProblem::StaleSystem);
    CHECK_THAT(refused->error.message, ContainsSubstring("mesh"));
    const auto staleConstraints = thermal::solveTransientThermalSystem(c2.system, c2.map, *oldConstraints, initialAt(300.0), transient, {});
    REQUIRE_FALSE(staleConstraints.has_value());
    CHECK(staleConstraints.error().problem == TransientProblem::ConstraintMismatch);
    const auto staleMap = thermal::solveTransientThermalSystem(c2.system, *oldMap, c2.boundary.constraints(), initialAt(300.0), transient, {});
    REQUIRE_FALSE(staleMap.has_value());
    CHECK(staleMap.error().problem == TransientProblem::DofMapMismatch);
    const auto staleBoundary = thermal::transientSolveFailure(study.document, c2.model, c2.map, c2.system, *oldBoundary, {}, conditions,
                                                               initialAt(300.0), transient, ThermalSolverSettings{});
    REQUIRE(staleBoundary.has_value());
    CHECK(staleBoundary->problem == TransientProblem::DofMapMismatch);
    const TransientThermalSolution fresh = mustSolveOn(study, c2, {}, conditions, initialAt(300.0), transient);
    CHECK(thermal::isCurrent(fresh, study.document, c2.model, {}, conditions, initialAt(300.0), transient, ThermalSolverSettings{}));
    WARN(std::format("remesh: {} nodes in both generations; the old {}-unknown history is stale (mesh), the old system, boundary, numbering "
                     "and constraints are each refused, the new history is current",
                     nodes1, oldSolution->dofCount()));
}

TEST_CASE("ThermalTransient_SteadyAndTransientSystemsAreNotInterchangeable", "[thermal][transient][mode]") {
    BoundaryStudy study;
    const std::vector<ThermalBoundaryCondition> conditions{temperatureBc(1, study.startCap(), 400.0),
                                                            convectionBc(2, study.endCap(), 25.0, kAmbient)};
    const Chain steadyChain = chainOn(study, {}, conditions, ThermalAnalysisMode::Steady);
    const Chain transientChain = chainOn(study, {}, conditions);
    REQUIRE_FALSE(steadyChain.system.capacity().has_value());
    REQUIRE(transientChain.system.capacity().has_value());

    // A steady system into the transient solve: refused by the model-level currentness check (the mode moved) and, at the seam,
    // by name.
    const auto viaModel = thermal::transientSolveFailure(study.document, steadyChain.model, steadyChain.map, steadyChain.system,
                                                          steadyChain.boundary, {}, conditions, initialAt(300.0), timing(5.0, 50.0), {});
    REQUIRE(viaModel.has_value());
    CHECK(viaModel->problem == TransientProblem::StaleSystem);
    CHECK_THAT(viaModel->error.message, ContainsSubstring("mode"));
    const auto viaSeam = thermal::solveTransientThermalSystem(steadyChain.system, steadyChain.map, steadyChain.boundary.constraints(),
                                                              initialAt(300.0), timing(5.0, 50.0), {});
    REQUIRE_FALSE(viaSeam.has_value());
    CHECK(viaSeam.error().problem == TransientProblem::NotTransientSystem);
    // And the other way: a transient system into the steady solve is the steady solver's own refusal.
    const auto steadyOfTransient =
        thermal::solveSteadyThermalSystem(transientChain.system, transientChain.map, transientChain.boundary.constraints(), {});
    REQUIRE_FALSE(steadyOfTransient.has_value());
    CHECK(steadyOfTransient.error().problem == SteadySolveProblem::NotSteadySystem);
}

TEST_CASE("ThermalTransient_SettingsAreRefusedBeforeAnyArithmetic", "[thermal][transient][refusals]") {
    BoundaryStudy study;
    const std::vector<ThermalBoundaryCondition> conditions{temperatureBc(1, study.startCap(), 400.0)};
    const Chain c = chainOn(study, {}, conditions);
    const auto problemFor = [&](const UniformInitialTemperature& i, const TransientSettings& t, const ThermalSolverSettings& s) {
        const auto refused = thermal::transientSolveFailure(study.document, c.model, c.map, c.system, c.boundary, {}, conditions, i, t, s);
        REQUIRE(refused.has_value());
        CHECK(refused->step == 0);
        return refused->problem;
    };
    const UniformInitialTemperature ok = initialAt(300.0);
    for (const double dt : {0.0, -1.0, kNaN, kInf}) {
        CHECK(problemFor(ok, timing(dt, 10.0), {}) == TransientProblem::InvalidTimeStep);
    }
    for (const double end : {0.0, -1.0, kNaN, kInf, 0.5}) {  // 0.5 < the 1 s step
        CHECK(problemFor(ok, timing(1.0, end), {}) == TransientProblem::InvalidEndTime);
    }
    CHECK(problemFor(ok, TransientSettings{.timeStep = Time::fromSi(1.0), .endTime = Time::fromSi(2.0),
                                           .output = static_cast<TransientOutputPolicy>(9)},
                     {}) == TransientProblem::InvalidOutputPolicy);
    for (const double bad : {kNaN, kInf, -1.0}) {
        CHECK(problemFor(initialAt(bad), timing(1.0, 2.0), {}) == TransientProblem::InvalidInitialTemperature);
    }
    CHECK(problemFor(ok, timing(1.0, 2.0), ThermalSolverSettings{.relativeResidualTolerance = 0.0}) == TransientProblem::InvalidSettings);
    CHECK(problemFor(ok, timing(1.0, 2.0), ThermalSolverSettings{.pivotFloor = kNaN}) == TransientProblem::InvalidSettings);
}

TEST_CASE("ThermalTransient_SettingsAreRefusedBeforeStalenessIsExamined", "[thermal][transient][refusals][order]") {
    // THE ORDER IS PART OF THE CONTRACT: a bad setting is reported as a bad setting, not as whatever else is also wrong. Here the
    // assembled system is a STEADY one for the transient solve (stale by mode), and each setting below is ALSO invalid.
    BoundaryStudy study;
    const std::vector<ThermalBoundaryCondition> conditions{temperatureBc(1, study.startCap(), 400.0)};
    const Chain steadyChain = chainOn(study, {}, conditions, ThermalAnalysisMode::Steady);
    const auto problemFor = [&](const UniformInitialTemperature& i, const TransientSettings& t, const ThermalSolverSettings& s) {
        const auto refused = thermal::transientSolveFailure(study.document, steadyChain.model, steadyChain.map, steadyChain.system,
                                                             steadyChain.boundary, {}, conditions, i, t, s);
        REQUIRE(refused.has_value());
        return refused->problem;
    };
    // Control: with valid settings the stale system is what is reported.
    CHECK(problemFor(initialAt(300.0), timing(1.0, 2.0), {}) == TransientProblem::StaleSystem);
    CHECK(problemFor(initialAt(300.0), timing(0.0, 2.0), {}) == TransientProblem::InvalidTimeStep);
    CHECK(problemFor(initialAt(300.0), timing(1.0, 0.5), {}) == TransientProblem::InvalidEndTime);
    CHECK(problemFor(initialAt(300.0), TransientSettings{.timeStep = Time::fromSi(1.0), .endTime = Time::fromSi(2.0),
                                                         .output = static_cast<TransientOutputPolicy>(9)},
                     {}) == TransientProblem::InvalidOutputPolicy);
    CHECK(problemFor(initialAt(kNaN), timing(1.0, 2.0), {}) == TransientProblem::InvalidInitialTemperature);
    CHECK(problemFor(initialAt(300.0), timing(1.0, 2.0), ThermalSolverSettings{.pivotFloor = 0.0}) == TransientProblem::InvalidSettings);
}

// ===========================================================================
// The result lifecycle (P18-DATA-001's slot) around a failed run
// ===========================================================================

namespace {

/// A held result for the lifecycle tests. A ThermalResult needs gradient and flux channels and a heat balance that belong to later
/// milestones; here they are zero, and ONLY the source, the temperatures and the time matter.
[[nodiscard]] thermal::ThermalResult heldResultOf(const BoundaryStudy& study, const TransientThermalSolution& solved) {
    const meshing::VolumeMesh& volume = study.volume();
    std::vector<Temperature> temperatures(solved.finalTemperatures().begin(), solved.finalTemperatures().end());
    Result<thermal::ThermalResult> held = thermal::ThermalResult::create(
        study.currentSource(), volume, std::move(temperatures), std::vector<thermal::TemperatureGradient3>(volume.mesh().tetrahedra().size()),
        std::vector<thermal::HeatFlux3>(volume.mesh().tetrahedra().size()), thermal::HeatBalance{.storedEnergyRate = Power{}},
        solved.sampleTimes().back());
    INFO((held.has_value() ? std::string{} : held.error().message));
    REQUIRE(held.has_value());
    return std::move(*held);
}

[[nodiscard]] ThermalAnalysisDefinition transientDefinition(const BoundaryStudy& study, const std::vector<ThermalLoad>& loads,
                                                            const std::vector<ThermalBoundaryCondition>& conditions,
                                                            const ThermalSolverSettings& solver = {}) {
    return ThermalAnalysisDefinition{.mesh = study.control,
                                     .mode = ThermalAnalysisMode::Transient,
                                     .loads = loads,
                                     .boundaryConditions = conditions,
                                     .initialCondition = initialAt(300.0),
                                     .solver = solver,
                                     .transient = timing(10.0, 100.0)};
}

} // namespace

TEST_CASE("ThermalTransient_FailedRunDoesNotCreateACurrentResult", "[thermal][transient][lifecycle]") {
    BoundaryStudy study;
    const std::vector<ThermalLoad> loads{generationLoad(1, 1.0e5)};
    const std::vector<ThermalBoundaryCondition> conditions{temperatureBc(1, study.startCap(), 400.0)};
    REQUIRE(study.setDefinition(transientDefinition(study, loads, conditions)).has_value());

    thermal::ThermalResultSlot slot;
    SECTION("R1 current, then the user tightens the residual tolerance beyond what arithmetic can meet") {
        {
            const Chain c = chainOn(study, loads, conditions);
            const TransientThermalSolution r1 = mustSolveOn(study, c, loads, conditions, initialAt(300.0), timing(10.0, 100.0));
            slot.recordSuccess(heldResultOf(study, r1));
        }
        CHECK(thermal::analysisState(study.document, study.regenerator, study.mesher, study.analysis, slot.result(), slot.failure()) ==
              thermal::AnalysisState::SolvedCurrent);

        // A VALID canonical edit (finite, positive) that no double-precision solve can satisfy.
        const ThermalSolverSettings impossible{.relativeResidualTolerance = 1.0e-30, .pivotFloor = 1.0e-12};
        REQUIRE(study.setDefinition(transientDefinition(study, loads, conditions, impossible)).has_value());
        CHECK(thermal::analysisState(study.document, study.regenerator, study.mesher, study.analysis, slot.result(), slot.failure()) ==
              thermal::AnalysisState::SolvedStale);

        const Chain c2 = chainOn(study, loads, conditions);
        const auto failed = solveOn(study, c2, loads, conditions, initialAt(300.0), timing(10.0, 100.0), impossible);
        REQUIRE_FALSE(failed.has_value());
        slot.recordFailure(failed.error());

        // R1 is still held, still STALE, and the latest attempt FAILED. No current result exists.
        REQUIRE(slot.result() != nullptr);
        CHECK(thermal::resultCurrency(slot.result(), study.currentSource()) == thermal::ResultCurrency::Stale);
        CHECK(thermal::analysisState(study.document, study.regenerator, study.mesher, study.analysis, slot.result(), slot.failure()) ==
              thermal::AnalysisState::SolveFailed);
        CHECK_FALSE(thermal::describesTheModel(thermal::resultCurrency(slot.result(), study.currentSource())));
        WARN("lifecycle: R1 current -> tolerance edited to 1e-30 -> R1 stale -> run fails (residual_too_large) -> R1 STALE, latest attempt "
             "FAILED, new current result NONE");
    }
    SECTION("no previous result: a failed first run leaves nothing") {
        const ThermalSolverSettings impossible{.relativeResidualTolerance = 1.0e-30, .pivotFloor = 1.0e-12};
        REQUIRE(study.setDefinition(transientDefinition(study, loads, conditions, impossible)).has_value());
        const Chain c = chainOn(study, loads, conditions);
        const auto failed = solveOn(study, c, loads, conditions, initialAt(300.0), timing(10.0, 100.0), impossible);
        REQUIRE_FALSE(failed.has_value());
        slot.recordFailure(failed.error());
        CHECK(slot.result() == nullptr);
        CHECK(slot.failure() != nullptr);
        CHECK(thermal::analysisState(study.document, study.regenerator, study.mesher, study.analysis, slot.result(), slot.failure()) ==
              thermal::AnalysisState::SolveFailed);
    }
}

TEST_CASE("ThermalTransient_AFailedRunNamesTheStepAndLeavesNoHistory", "[thermal][transient][lifecycle][atomic]") {
    BoundaryStudy study;
    const std::vector<ThermalBoundaryCondition> conditions{temperatureBc(1, study.startCap(), 400.0)};
    const Chain c = chainOn(study, {}, conditions);
    const ThermalSolverSettings impossible{.relativeResidualTolerance = 1.0e-30, .pivotFloor = 1.0e-12};
    const auto refused = thermal::transientSolveFailure(study.document, c.model, c.map, c.system, c.boundary, {}, conditions,
                                                         initialAt(300.0), timing(10.0, 100.0), impossible);
    REQUIRE(refused.has_value());
    CHECK(refused->problem == TransientProblem::ResidualTooLarge);
    CHECK(refused->step == 1);   // fails at the FIRST step: nothing before it was accepted
    CHECK(refused->time == 10.0);
    // The Result form carries no history, only the error.
    const auto result = solveOn(study, c, {}, conditions, initialAt(300.0), timing(10.0, 100.0), impossible);
    CHECK_FALSE(result.has_value());
}

TEST_CASE("ThermalTransient_RunDoesNotMutateTheModelOrTheSystem", "[thermal][transient][purity]") {
    BoundaryStudy study;
    const std::vector<ThermalLoad> loads{generationLoad(1, 1.0e5)};
    const std::vector<ThermalBoundaryCondition> conditions{temperatureBc(1, study.startCap(), 400.0),
                                                            convectionBc(2, study.endCap(), 25.0, kAmbient)};
    REQUIRE(study.setDefinition(transientDefinition(study, loads, conditions)).has_value());
    const Chain c = chainOn(study, loads, conditions);
    const ThermalSystem systemBefore = c.system;
    const PreparedThermalBoundary boundaryBefore = c.boundary;
    const PreparedThermalLoads loadsBefore = c.loads;
    const ThermalAnalysisDefinition definitionBefore = study.definition();
    const std::uint64_t revision = study.document.revision();
    const meshing::MeshStamp stamp = c.model.mesh().mesh().stamp();
    for (int i = 0; i < 3; ++i) {
        (void)solveOn(study, c, loads, conditions, initialAt(300.0), timing(10.0, 100.0));
    }
    CHECK(c.system == systemBefore);
    CHECK(c.boundary == boundaryBefore);
    CHECK(c.loads == loadsBefore);
    CHECK(study.definition() == definitionBefore);
    CHECK(study.document.revision() == revision);
    CHECK(c.model.mesh().mesh().stamp() == stamp);
    CHECK(study.mesher.currency(study.document, study.control) == meshing::MeshCurrency::Current);
}

// ===========================================================================
// Determinism
// ===========================================================================

TEST_CASE("ThermalTransient_RepeatedBlockRunIsDeterministic", "[thermal][transient][determinism]") {
    BoundaryStudy study;
    const std::vector<ThermalLoad> loads{generationLoad(1, 1.0e5)};
    const std::vector<ThermalBoundaryCondition> conditions{temperatureBc(1, study.startCap(), 400.0),
                                                            convectionBc(2, study.endCap(), 25.0, kAmbient)};
    const Chain c = chainOn(study, loads, conditions);
    const TransientThermalSolution first = mustSolveOn(study, c, loads, conditions, initialAt(300.0), timing(7.0, 100.0));
    int differences = 0;
    for (int i = 0; i < 20; ++i) {
        if (!(mustSolveOn(study, c, loads, conditions, initialAt(300.0), timing(7.0, 100.0)) == first)) {
            ++differences;
        }
    }
    CHECK(differences == 0);
    CHECK(first.grid().hasPartialFinalStep());  // 100 / 7: fourteen full steps and a shortened last one
    CHECK(first.grid().stepCount() == 15);
    CHECK(first.sampleTimes().back().si() == 100.0);
    WARN(std::format("determinism: 20 repeated block runs (15 steps, partial last), {} bitwise differences in T, residuals, pivot ratios or "
                     "provenance",
                     differences));
    WARN(std::format("bit fingerprint, real block run (Dirichlet + convection + generation, {} unknowns, {} states): {:016x}", first.dofCount(),
                     first.sampleCount(), fingerprintOf(first)));
}

TEST_CASE("ThermalTransient_FineCylinderSparseRunSmoke", "[thermal][transient][smoke][sparse]") {
    // The fine mesh of a 6 mm radius cylinder, a real P16 mesh with a curved wall. 350 K on one cap, convection on the other,
    // initial 293.15 K, a transient to well past the diffusion time (L^2/alpha = 32 s).
    RoundStudy part;
    {
        const std::vector<MaterialId> ids = features::materialIds(part.document);
        REQUIRE(ids.size() == 1);
        materials::MechanicalProperties mechanical = features::findMaterial(part.document, ids[0])->definition().mechanical;
        mechanical.density = materials::MaterialProperty<Density>::known(7850_kg_per_m3);
        REQUIRE(features::setMaterialMechanical(part.document, ids[0], mechanical).has_value());
        materials::ThermalProperties thermalProperties = features::findMaterial(part.document, ids[0])->definition().thermal;
        thermalProperties.specificHeatCapacity = materials::MaterialProperty<SpecificHeatCapacity>::known(460_J_per_kg_K);
        REQUIRE(features::setMaterialThermal(part.document, ids[0], thermalProperties).has_value());
    }
    const ThermalModel model = part.model(part.fine);
    const ThermalDofMap map = dofMapOf(model);
    const std::vector<ThermalBoundaryCondition> conditions{temperatureBc(1, FaceName{part.feature, FaceSelector{.role = FaceRole::StartCap}}, 350.0),
                                                           convectionBc(2, part.endCap(), 25.0, kAmbient)};
    auto preparedLoads = thermal::prepareThermalLoads(model, map, std::span<const ThermalLoad>{});
    auto preparedBoundary = thermal::prepareThermalBoundary(model, map, conditions);
    REQUIRE((preparedLoads.has_value() && preparedBoundary.has_value()));
    auto system = thermal::assembleThermalSystem(part.document, model, ThermalAnalysisMode::Transient, map, *preparedLoads,
                                                 std::span<const ThermalLoad>{}, *preparedBoundary, conditions);
    INFO((system.has_value() ? std::string{} : system.error().message));
    REQUIRE(system.has_value());
    const auto solved = thermal::solveTransientThermal(part.document, model, map, *system, *preparedBoundary, std::span<const ThermalLoad>{},
                                                       conditions, initialAt(kAmbient), timing(10.0, 100.0), ThermalSolverSettings{});
    INFO((solved.has_value() ? std::string{} : solved.error().message));
    REQUIRE(solved.has_value());
    for (const Temperature t : solved->finalTemperatures()) {
        REQUIRE(std::isfinite(t.si()));
    }
    double mean = 0.0;
    for (const Temperature t : solved->finalTemperatures()) {
        mean += t.si();
    }
    mean /= static_cast<double>(solved->dofCount());
    CHECK(mean > kAmbient);
    CHECK(mean < 350.0);
    CHECK(solved->worstReducedResidual() <= 1e-9);
    CHECK(solved->worstPhysicalResidual() <= 1e-9);
    CHECK(solved->kernelSolves() == 10);
    CHECK(system->conductance().nonZeros() < solved->dofCount() * solved->dofCount() / 10);
    WARN(std::format("real fine cylinder transient: Ndof {}, Nfree {}, nnz(K) {}, 10 steps of 10 s, mean temperature {:.4f} K, worst residuals "
                     "{:.2e} (reduced) {:.2e} (physical)",
                     solved->dofCount(), solved->freeEquations(), system->conductance().nonZeros(), mean,
                     solved->worstReducedResidual(), solved->worstPhysicalResidual()));
}

TEST_CASE("ThermalTransient_SolutionAccessorsAreBoundsChecked", "[thermal][transient][accessors]") {
    BoundaryStudy study;
    const std::vector<ThermalBoundaryCondition> conditions{temperatureBc(1, study.startCap(), 400.0)};
    const Chain c = chainOn(study, {}, conditions);
    const TransientThermalSolution solved = mustSolveOn(study, c, {}, conditions, initialAt(300.0), timing(10.0, 30.0));
    REQUIRE(solved.sampleCount() == 4);
    CHECK(solved.timeOf(0).si() == 0.0);
    CHECK(solved.timeOf(3).si() == 30.0);
    CHECK(solved.timeOf(4).si() == 0.0);                       // out of range: zero
    CHECK(solved.temperaturesAt(4).empty());                   // out of range: empty
    CHECK(solved.temperaturesAt(0).size() == solved.dofCount());
    CHECK(solved.finalTemperatures().data() == solved.temperaturesAt(3).data());
    CHECK(solved.initialTemperatures().size() == solved.dofCount());
    CHECK(solved.constrainedCount() + solved.freeEquations() == solved.dofCount());

    const meshing::Node& node = c.model.mesh().mesh().nodes().front();
    const auto byNode = solved.temperatureOf(3, c.map, node.id);
    REQUIRE(byNode.has_value());
    CHECK(byNode->si() == solved.finalTemperatures()[static_cast<std::size_t>(c.map.dofOf(node.id)->value())].si());
    CHECK_FALSE(solved.temperatureOf(4, dofIx(0)).has_value());
    CHECK_FALSE(solved.temperatureOf(0, dofIx(solved.dofCount())).has_value());
    // Another numbering is refused, not silently indexed.
    study.remesh();
    const Chain other = chainOn(study, {}, conditions);
    const auto wrong = solved.temperatureOf(3, other.map, node.id);
    REQUIRE_FALSE(wrong.has_value());
    CHECK_THAT(wrong.error().message, ContainsSubstring("another numbering"));
    CHECK_FALSE(solved.describes(other.map));
}
