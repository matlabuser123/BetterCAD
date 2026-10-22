// P18-BALANCE-001: the accounting against REAL P16 meshes and real documents.
//
// These are the AUTHORITY tests. A solved field is balanced for the model it was solved for, and for no
// other; the categories are taken from the qualified load attribution and the qualified convection terms, the
// reactions from the equation; the books close on a 40 x 30 x 20 mm steel block (k = 45 W/(m K), rho = 7850,
// cp = 460, extruded along z) and on a 6 mm radius cylinder that P16 meshes with a curved wall; and a stale
// solution, a remesh that kept every count and id, a model edit or a history without its steps is refused.
//
// The arithmetic and the traps are checked on hand networks in ThermalBalanceTests.cpp. Here every expectation
// is a closed form (a slab's k A dT / L, a face's h A (T - Tinf), q'' A, q''' V, rho cp V dT) or an identity.

#include "ThermalBalanceTestSupport.hpp"

#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <bit>
#include <format>

using namespace batest;
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

constexpr double kVolume = 0.040 * 0.030 * 0.020;     // the block, m^3
constexpr double kCapAreaM2 = 0.040 * 0.030;          // a cap, m^2
constexpr double kConductivity = 45.0;                // W/(m K)
constexpr double kAmbient = 293.15;                   // K
constexpr double kHeatCapacityTotal = 7850.0 * 460.0 * kVolume;   // rho cp V, J/K

[[nodiscard]] ThermalLoad generationLoad(std::uint64_t id, double generation) {
    return ThermalLoad{ThermalLoadId::fromValue(id),
                       VolumetricHeatSourceLoad{.generation = VolumetricHeatSource::fromSi(generation)}};
}

[[nodiscard]] ThermalLoad surfaceLoad(std::uint64_t id, const FaceName& face, double flux) {
    return ThermalLoad{ThermalLoadId::fromValue(id), SurfaceHeatFluxLoad{.face = face, .flux = HeatFlux::fromSi(flux)}};
}

struct Chain {
    ThermalModel model;
    ThermalDofMap map;
    PreparedThermalLoads loads;
    PreparedThermalBoundary boundary;
    ThermalSystem system;
};

[[nodiscard]] Chain chainFrom(const Document& document, ThermalModel model, const std::vector<ThermalLoad>& loads,
                              const std::vector<ThermalBoundaryCondition>& conditions,
                              ThermalAnalysisMode mode = ThermalAnalysisMode::Steady) {
    ThermalDofMap map = dofMapOf(model);
    Result<PreparedThermalLoads> preparedLoads = thermal::prepareThermalLoads(model, map, loads);
    INFO((preparedLoads.has_value() ? std::string{} : preparedLoads.error().message));
    REQUIRE(preparedLoads.has_value());
    Result<PreparedThermalBoundary> preparedBoundary = thermal::prepareThermalBoundary(model, map, conditions);
    INFO((preparedBoundary.has_value() ? std::string{} : preparedBoundary.error().message));
    REQUIRE(preparedBoundary.has_value());
    Result<ThermalSystem> system =
        thermal::assembleThermalSystem(document, model, mode, map, *preparedLoads, loads, *preparedBoundary, conditions);
    INFO((system.has_value() ? std::string{} : system.error().message));
    REQUIRE(system.has_value());
    return Chain{std::move(model), std::move(map), std::move(*preparedLoads), std::move(*preparedBoundary), std::move(*system)};
}

[[nodiscard]] SteadyThermalSolution solveSteady(const Document& document, const Chain& c, const std::vector<ThermalLoad>& loads,
                                                const std::vector<ThermalBoundaryCondition>& conditions,
                                                const ThermalSolverSettings& settings = {}) {
    Result<SteadyThermalSolution> solved =
        thermal::solveSteadyThermal(document, c.model, c.map, c.system, c.boundary, loads, conditions, settings);
    INFO((solved.has_value() ? std::string{} : solved.error().message));
    REQUIRE(solved.has_value());
    return std::move(*solved);
}

[[nodiscard]] ThermalBalanceResult balanceSteady(const Document& document, const Chain& c, const SteadyThermalSolution& solution,
                                                 const std::vector<ThermalLoad>& loads,
                                                 const std::vector<ThermalBoundaryCondition>& conditions,
                                                 const ThermalSolverSettings& settings = {}) {
    Result<ThermalBalanceResult> balance = thermal::recoverSteadyBalance(document, c.model, c.map, c.system, solution, c.loads,
                                                                         c.boundary, loads, conditions, settings);
    INFO((balance.has_value() ? std::string{} : balance.error().message));
    REQUIRE(balance.has_value());
    return std::move(*balance);
}

struct Timing {
    thermal::UniformInitialTemperature initial;
    thermal::TransientSettings transient;
};

[[nodiscard]] Timing timing(double initial, double step, double end,
                            thermal::TransientOutputPolicy output = thermal::TransientOutputPolicy::EveryStep) {
    return Timing{thermal::UniformInitialTemperature{Temperature::fromSi(initial)},
                  thermal::TransientSettings{.timeStep = Time::fromSi(step), .endTime = Time::fromSi(end), .output = output}};
}

[[nodiscard]] TransientThermalSolution solveTransient(const Document& document, const Chain& c,
                                                      const std::vector<ThermalLoad>& loads,
                                                      const std::vector<ThermalBoundaryCondition>& conditions, const Timing& t,
                                                      const ThermalSolverSettings& settings = {}) {
    Result<TransientThermalSolution> solved = thermal::solveTransientThermal(
        document, c.model, c.map, c.system, c.boundary, loads, conditions, t.initial, t.transient, settings);
    INFO((solved.has_value() ? std::string{} : solved.error().message));
    REQUIRE(solved.has_value());
    return std::move(*solved);
}

[[nodiscard]] ThermalBalanceResult balanceTransient(const Document& document, const Chain& c, const TransientThermalSolution& solution,
                                                    const std::vector<ThermalLoad>& loads,
                                                    const std::vector<ThermalBoundaryCondition>& conditions, const Timing& t,
                                                    const ThermalSolverSettings& settings = {}) {
    Result<ThermalBalanceResult> balance = thermal::recoverTransientBalance(
        document, c.model, c.map, c.system, solution, c.loads, c.boundary, loads, conditions, t.initial, t.transient, settings);
    INFO((balance.has_value() ? std::string{} : balance.error().message));
    REQUIRE(balance.has_value());
    return std::move(*balance);
}

/// The reaction heat at the unknowns prescribed by the @p index-th prescribed-temperature condition.
[[nodiscard]] double reactionOnCondition(const Chain& c, const ThermalHeatBalance& balance, std::size_t index) {
    double sum = 0.0;
    for (const ThermalDofIndex dof : c.boundary.temperatures()[index].dofs) {
        for (const ReactionHeat& reaction : balance.reactions) {
            if (reaction.dof == dof) {
                sum += reaction.heatInto.si();
            }
        }
    }
    return sum;
}

/// The measurement line a tolerance campaign collects.
void report(const std::string& name, const ThermalHeatBalance& b) {
    const double scale = b.grossScale().si() + b.arithmeticScale.si();
    const double allowed = thermal::kBalanceAbsoluteToleranceW + thermal::kBalanceRelativeTolerance * scale;
    const double residual = std::abs(b.residual().si());
    WARN(std::format("measure {}: gross {:.6e} W, arithmetic {:.6e} W, |B| {:.3e} W, |B|/(gross+arithmetic) {:.3e}, normalized {:.3e}, status {}", name,
                     b.grossScale().si(), b.arithmeticScale.si(), residual, residual / std::max(scale, 1.0e-300), b.normalizedResidual(),
                     thermal::toString(b.status)));
    // THE MARGIN OF THE POLICY over what was measured: the books close to the arithmetic, with room to spare.
    CHECK(residual * 100.0 <= allowed);
}

void reportWorst(const std::string& name, const ThermalBalanceResult& result) {
    const ThermalHeatBalance* worst = nullptr;
    double ratio = -1.0;
    for (const ThermalHeatBalance& s : result.states()) {
        const double r = std::abs(s.residual().si()) / std::max(s.grossScale().si() + s.arithmeticScale.si(), 1.0e-300);
        if (r > ratio) {
            ratio = r;
            worst = &s;
        }
    }
    REQUIRE(worst != nullptr);
    report(name + ", worst of " + std::to_string(result.stateCount()) + " steps", *worst);
}

[[nodiscard]] std::uint64_t fingerprintOf(const ThermalBalanceResult& result) {
    std::uint64_t hash = 14695981039346656037ULL;
    const auto mix = [&hash](double value) {
        const auto bits = std::bit_cast<std::uint64_t>(value);
        for (int i = 0; i < 8; ++i) {
            hash ^= (bits >> (8 * i)) & 0xFFU;
            hash *= 1099511628211ULL;
        }
    };
    for (const ThermalHeatBalance& state : result.states()) {
        mix(state.terms.appliedSurfaceHeat.si());
        mix(state.terms.generatedHeat.si());
        mix(state.terms.convectionHeat.si());
        mix(state.terms.prescribedTemperatureReaction.si());
        mix(state.terms.storedEnergyRate.value_or(Power{}).si());
        mix(state.residual().si());
        for (const ReactionHeat& r : state.reactions) {
            mix(r.heatInto.si());
        }
    }
    return hash;
}

} // namespace

// ===========================================================================
// Steady, end to end
// ===========================================================================

TEST_CASE("ThermalBalance_SteadySlabReactionMatchesAnalyticalAndPostFlux", "[thermal][balance][steady][slab][reaction][post]") {
    // 400 K on one cap, 300 K on the other, k = 45, L = 20 mm, cap area 1.2e-3 m^2: the heat flowing through the bar is
    //   k A dT / L = 45 * 1.2e-3 * 100 / 0.02 = 270 W.
    // The hot cap's constraints SUPPLY 270 W, the cold cap's REMOVE it, and the recovered conductive flux (225 000 W/m^2) times the cap
    // area is the same heat.
    BoundaryStudy study;
    const std::vector<ThermalBoundaryCondition> conditions{temperatureBc(1, study.startCap(), 400.0),
                                                            temperatureBc(2, study.endCap(), 300.0)};
    const Chain c = chainFrom(study.document, study.model(), {}, conditions);
    const SteadyThermalSolution solved = solveSteady(study.document, c, {}, conditions);
    const ThermalBalanceResult result = balanceSteady(study.document, c, solved, {}, conditions);
    REQUIRE(result.stateCount() == 1);
    const ThermalHeatBalance& b = result.finalState();

    const double hot = reactionOnCondition(c, b, 0);
    const double cold = reactionOnCondition(c, b, 1);
    const double expected = kConductivity * kCapAreaM2 * 100.0 / 0.020;
    CHECK_THAT(expected, WithinAbs(270.0, 1e-12));
    CHECK_THAT(hot, WithinRel(expected, 1e-8));
    CHECK_THAT(cold, WithinRel(-expected, 1e-8));
    CHECK(hot > 0.0);                       // the hot boundary supplies
    CHECK(cold < 0.0);                      // the cold boundary removes
    CHECK_THAT(hot + cold, WithinAbs(0.0, 1e-8));
    CHECK(b.reactions.size() == c.boundary.constraints().size());   // one reaction per UNIQUE prescribed unknown
    CHECK_THAT(b.terms.appliedSurfaceHeat.si(), WithinAbs(0.0, 1e-15));
    CHECK_THAT(b.terms.generatedHeat.si(), WithinAbs(0.0, 1e-15));
    CHECK_THAT(b.terms.convectionHeat.si(), WithinAbs(0.0, 1e-15));
    CHECK_THAT(b.residual().si(), WithinAbs(0.0, 1e-8));
    CHECK(b.status == BalanceStatus::Balanced);

    // The recovered flux of P18-POST-001 times the cap area, in the same convention (heat toward +z is positive).
    const Result<thermal::ThermalPostResult> post = thermal::recoverSteadyThermal(study.document, c.model, c.map, solved, {}, conditions, {});
    REQUIRE(post.has_value());
    const double fluxHeat = post->finalState().fields.heatFluxMagnitudes[0].si() * kCapAreaM2;
    CHECK_THAT(fluxHeat, WithinRel(expected, 1e-6));
    CHECK_THAT(hot, WithinRel(fluxHeat, 1e-6));
    report("steady slab 400/300 K", b);
    WARN(std::format("steady slab: analytical k A dT/L = {:.6f} W; hot reaction {:.9f} W; cold reaction {:.9f} W; POST |q| A {:.6f} W",
                     expected, hot, cold, fluxHeat));
}

TEST_CASE("ThermalBalance_SteadyAppliedSurfaceHeatBalancedByDirichletReactionOnTheBlock", "[thermal][balance][steady][applied][reaction]") {
    // 1e4 W/m^2 through the 40 x 20 mm side face (8e-4 m^2) is 8 W; the start cap is held at 300 K and removes all of it.
    BoundaryStudy study;
    const std::vector<ThermalLoad> loads{surfaceLoad(1, study.side(0), 1.0e4)};
    const std::vector<ThermalBoundaryCondition> conditions{temperatureBc(1, study.startCap(), 300.0)};
    const Chain c = chainFrom(study.document, study.model(), loads, conditions);
    const SteadyThermalSolution solved = solveSteady(study.document, c, loads, conditions);
    const ThermalBalanceResult result = balanceSteady(study.document, c, solved, loads, conditions);
    const ThermalHeatBalance& b = result.finalState();
    REQUIRE(b.surfaceLoads.size() == 1);
    CHECK(b.surfaceLoads[0].load == ThermalLoadId::fromValue(1));
    CHECK_THAT(b.surfaceLoads[0].heatInto.si(), WithinRel(8.0, 1e-12));
    CHECK_THAT(b.terms.appliedSurfaceHeat.si(), WithinRel(8.0, 1e-12));
    CHECK_THAT(b.terms.prescribedTemperatureReaction.si(), WithinRel(-8.0, 1e-8));
    CHECK_THAT(reactionOnCondition(c, b, 0), WithinRel(-8.0, 1e-8));
    CHECK_THAT(b.residual().si(), WithinAbs(0.0, 1e-8));
    CHECK(b.status == BalanceStatus::Balanced);
    // The prepared nodal vector carries the same heat: the attribution and the vector agree.
    CHECK_THAT(c.loads.appliedSurfaceHeatRate().si(), WithinRel(8.0, 1e-12));
    CHECK_THAT(c.loads.totalHeatRate().si(), WithinRel(8.0, 1e-12));
    report("steady surface heat 8 W vs 300 K cap", b);
}

TEST_CASE("ThermalBalance_SteadyGenerationAndConvectionOnTheBlock", "[thermal][balance][steady][generation][convection]") {
    // q''' = 1e5 W/m^3 in 2.4e-5 m^3 is 2.4 W; the only exit is the end cap, h = 25 W/(m^2 K) to 293.15 K. All of it leaves by convection,
    // nothing is prescribed, so there is no reaction. The cap is uniform (the sides are insulated), so h A (T_cap - Tinf) = 2.4 W.
    BoundaryStudy study;
    const std::vector<ThermalLoad> loads{generationLoad(1, 1.0e5)};
    const std::vector<ThermalBoundaryCondition> conditions{convectionBc(1, study.endCap(), 25.0, kAmbient)};
    const Chain c = chainFrom(study.document, study.model(), loads, conditions);
    const SteadyThermalSolution solved = solveSteady(study.document, c, loads, conditions);
    const ThermalBalanceResult result = balanceSteady(study.document, c, solved, loads, conditions);
    const ThermalHeatBalance& b = result.finalState();
    CHECK_THAT(b.terms.generatedHeat.si(), WithinRel(1.0e5 * kVolume, 1e-12));
    CHECK_THAT(b.terms.convectionHeat.si(), WithinRel(-1.0e5 * kVolume, 1e-8));
    REQUIRE(b.convections.size() == 1);
    CHECK_THAT(b.convections[0].area.si(), WithinRel(kCapAreaM2, 1e-9));
    CHECK(b.reactions.empty());
    CHECK_THAT(b.terms.prescribedTemperatureReaction.si(), WithinAbs(0.0, 1e-15));
    // h A (T_cap - Tinf) = 2.4 W with the temperature of the cap read from the solution.
    const auto& facets = c.boundary.convections()[0].facets;
    double meanCap = 0.0;
    double weight = 0.0;
    for (const thermal::ConvectionFacet& facet : facets) {
        const double area = facet.term.area().si();
        for (const ThermalDofIndex dof : facet.dofs) {
            meanCap += area / 3.0 * solved.temperatures()[static_cast<std::size_t>(dof.value())].si();
        }
        weight += area;
    }
    meanCap /= weight;
    CHECK_THAT(25.0 * kCapAreaM2 * (meanCap - kAmbient), WithinRel(1.0e5 * kVolume, 1e-6));
    CHECK_THAT(b.residual().si(), WithinAbs(0.0, 1e-8));
    CHECK(b.status == BalanceStatus::Balanced);
    report("steady generation 2.4 W vs convection", b);
}

TEST_CASE("ThermalBalance_SteadyCombinedCategoriesOnMultipleBoundaries", "[thermal][balance][steady][multiple][categories]") {
    // Everything at once: two prescribed caps (400 K, 330 K), two convecting sides, two applied faces (one an extraction), two generation loads
    // (one a sink). Each category is a signed sum in the given order; the books close.
    BoundaryStudy study;
    const std::vector<ThermalLoad> loads{surfaceLoad(1, study.side(1), 5.0e3), surfaceLoad(2, study.side(3), -2.0e3),
                                         generationLoad(3, 5.0e4), generationLoad(4, -1.0e4)};
    const std::vector<ThermalBoundaryCondition> conditions{
        temperatureBc(1, study.startCap(), 400.0), temperatureBc(2, study.endCap(), 330.0),
        convectionBc(3, study.side(0), 25.0, kAmbient), convectionBc(4, study.side(2), 15.0, 280.0)};
    const Chain c = chainFrom(study.document, study.model(), loads, conditions);
    const SteadyThermalSolution solved = solveSteady(study.document, c, loads, conditions);
    const ThermalBalanceResult result = balanceSteady(study.document, c, solved, loads, conditions);
    const ThermalHeatBalance& b = result.finalState();

    // The attribution, in the canonical order, with the signs kept: 6e-4 m^2 sides.
    REQUIRE(b.surfaceLoads.size() == 2);
    REQUIRE(b.generationLoads.size() == 2);
    REQUIRE(b.convections.size() == 2);
    CHECK(b.surfaceLoads[0].load == ThermalLoadId::fromValue(1));
    CHECK(b.surfaceLoads[1].load == ThermalLoadId::fromValue(2));
    CHECK(b.generationLoads[0].load == ThermalLoadId::fromValue(3));
    CHECK(b.generationLoads[1].load == ThermalLoadId::fromValue(4));
    CHECK(b.convections[0].condition == bid(3));
    CHECK(b.convections[1].condition == bid(4));
    CHECK_THAT(b.surfaceLoads[0].heatInto.si(), WithinRel(5.0e3 * 6.0e-4, 1e-12));      // +3 W
    CHECK_THAT(b.surfaceLoads[1].heatInto.si(), WithinRel(-2.0e3 * 6.0e-4, 1e-12));     // -1.2 W
    CHECK_THAT(b.generationLoads[0].heatInto.si(), WithinRel(5.0e4 * kVolume, 1e-12));  // +1.2 W
    CHECK_THAT(b.generationLoads[1].heatInto.si(), WithinRel(-1.0e4 * kVolume, 1e-12)); // -0.24 W
    CHECK_THAT(b.terms.appliedSurfaceHeat.si(), WithinRel(3.0 - 1.2, 1e-12));
    CHECK_THAT(b.terms.generatedHeat.si(), WithinRel(1.2 - 0.24, 1e-12));
    CHECK(b.reactions.size() == c.boundary.constraints().size());

    // Per-item totals sum to their category, bit for bit; the convection is the sum of its two conditions.
    CHECK((b.surfaceLoads[0].heatInto.si() + b.surfaceLoads[1].heatInto.si()) == b.terms.appliedSurfaceHeat.si());
    CHECK((b.convections[0].heatInto.si() + b.convections[1].heatInto.si()) == b.terms.convectionHeat.si());
    double reactions = 0.0;
    for (const ReactionHeat& r : b.reactions) {
        reactions += r.heatInto.si();
    }
    CHECK(reactions == b.terms.prescribedTemperatureReaction.si());

    // An algebraic cross-check from the retained full residual: its prescribed components are the reactions, and its free
    // components are the (tiny) defect the balance sees.
    const auto residual = thermal::steadyFullResidual(c.system.conductance(), c.system.heat(), solved.temperatures());
    REQUIRE(residual.has_value());
    double constrained = 0.0;
    for (const ThermalTemperatureConstraint& constraint : c.boundary.constraints().constrained()) {
        constrained += (*residual)[static_cast<std::size_t>(constraint.dof.value())];
    }
    CHECK_THAT(constrained, WithinRel(b.terms.prescribedTemperatureReaction.si(), 1e-12));
    // The retained residual of the solve is the same vector.
    REQUIRE(solved.fullResidual().size() == residual->size());
    for (std::size_t i = 0; i < residual->size(); ++i) {
        CHECK_THAT((*residual)[i], WithinAbs(solved.fullResidual()[i], 1e-9));
    }
    CHECK_THAT(b.residual().si(), WithinAbs(0.0, 1e-8));
    CHECK(b.status == BalanceStatus::Balanced);
    CHECK(b.terms.convectionHeat.si() < 0.0);               // the body is hotter than both ambients
    report("steady combined, two caps + two convections + four loads", b);
    WARN(std::format("combined steady: surface {:.6f} W, generation {:.6f} W, convection {:.6f} W, reaction {:.6f} W, residual {:.3e} W",
                     b.terms.appliedSurfaceHeat.si(), b.terms.generatedHeat.si(), b.terms.convectionHeat.si(),
                     b.terms.prescribedTemperatureReaction.si(), b.residual().si()));
}

TEST_CASE("ThermalBalance_DuplicateTemperatureBoundariesDoNotDoubleCountTheReaction", "[thermal][balance][reaction][duplicates]") {
    // Two canonical conditions prescribe the same face at the same temperature. P18-BC-001 collapses them to one constraint per unknown; the
    // reaction is the heat of that unknown and is counted once.
    BoundaryStudy study;
    const std::vector<ThermalBoundaryCondition> single{temperatureBc(1, study.startCap(), 400.0), temperatureBc(2, study.endCap(), 300.0)};
    const std::vector<ThermalBoundaryCondition> doubled{temperatureBc(1, study.startCap(), 400.0), temperatureBc(2, study.endCap(), 300.0),
                                                        temperatureBc(3, study.startCap(), 400.0)};
    const Chain a = chainFrom(study.document, study.model(), {}, single);
    const Chain d = chainFrom(study.document, study.model(), {}, doubled);
    REQUIRE(a.boundary.constraints().size() == d.boundary.constraints().size());   // deduplicated upstream
    const ThermalBalanceResult ra = balanceSteady(study.document, a, solveSteady(study.document, a, {}, single), {}, single);
    const ThermalBalanceResult rd = balanceSteady(study.document, d, solveSteady(study.document, d, {}, doubled), {}, doubled);
    CHECK(ra.finalState().reactions.size() == rd.finalState().reactions.size());
    for (std::size_t i = 0; i < ra.finalState().reactions.size(); ++i) {
        CHECK(ra.finalState().reactions[i].dof == rd.finalState().reactions[i].dof);
        CHECK_THAT(rd.finalState().reactions[i].heatInto.si(), WithinAbs(ra.finalState().reactions[i].heatInto.si(), 1e-12));
    }
    // The hot cap's reaction is 270 W once, not 540 W.
    CHECK_THAT(reactionOnCondition(d, rd.finalState(), 0), WithinRel(270.0, 1e-8));
    CHECK_THAT(reactionOnCondition(d, rd.finalState(), 2), WithinRel(270.0, 1e-8));   // the duplicate names the same unknowns: not an independent heat
    CHECK(rd.finalState().status == BalanceStatus::Balanced);
}

TEST_CASE("ThermalBalance_FixedTemperatureAndConvectionOverlapOnOneFace", "[thermal][balance][reaction][convection][overlap]") {
    // The start cap is held at 400 K AND convects to 293.15 K (P18-BC-001 keeps both). The cap is uniform, so it loses exactly
    // h A (400 - 293.15) = 25 * 1.2e-3 * 106.85 = 3.2055 W by convection, and its constraints supply that in addition to the 270 W of conduction.
    BoundaryStudy study;
    const std::vector<ThermalBoundaryCondition> conditions{temperatureBc(1, study.startCap(), 400.0), temperatureBc(2, study.endCap(), 300.0),
                                                            convectionBc(3, study.startCap(), 25.0, kAmbient)};
    const Chain c = chainFrom(study.document, study.model(), {}, conditions);
    const ThermalBalanceResult result = balanceSteady(study.document, c, solveSteady(study.document, c, {}, conditions), {}, conditions);
    const ThermalHeatBalance& b = result.finalState();
    REQUIRE(b.convections.size() == 1);
    const double lost = 25.0 * kCapAreaM2 * (400.0 - kAmbient);
    CHECK_THAT(b.convections[0].heatOut.si(), WithinRel(lost, 1e-9));
    CHECK_THAT(b.convections[0].heatInto.si(), WithinRel(-lost, 1e-9));
    CHECK_THAT(reactionOnCondition(c, b, 0), WithinRel(270.0 + lost, 1e-6));
    CHECK_THAT(reactionOnCondition(c, b, 1), WithinRel(-270.0, 1e-6));
    CHECK_THAT(b.residual().si(), WithinAbs(0.0, 1e-8));
    CHECK(b.status == BalanceStatus::Balanced);
    report("fixed temperature + convection overlap", b);
    WARN(std::format("fixed temperature + convection on one face: convection loses {:.6f} W (analytical {:.6f}), hot reaction {:.6f} W",
                     b.convections[0].heatOut.si(), lost, reactionOnCondition(c, b, 0)));
}

TEST_CASE("ThermalBalance_AdditiveConvectionBoundariesBothCount", "[thermal][balance][convection][duplicates][multiple]") {
    // Two convection conditions on the SAME face (25 W/(m^2 K) to 293.15 K and 10 W/(m^2 K) to 280 K) are two heat exchanges, not one, and they
    // are not deduplicated. 2.4 W is generated; the cap is uniform at T_b with (h1 + h2) A T_b = 2.4 + A (h1 T1 + h2 T2).
    BoundaryStudy study;
    const std::vector<ThermalLoad> loads{generationLoad(1, 1.0e5)};
    const std::vector<ThermalBoundaryCondition> conditions{convectionBc(1, study.endCap(), 25.0, kAmbient),
                                                            convectionBc(2, study.endCap(), 10.0, 280.0)};
    const Chain c = chainFrom(study.document, study.model(), loads, conditions);
    const ThermalBalanceResult result = balanceSteady(study.document, c, solveSteady(study.document, c, loads, conditions), loads, conditions);
    const ThermalHeatBalance& b = result.finalState();
    REQUIRE(b.convections.size() == 2);
    const double generated = 1.0e5 * kVolume;
    const double tb = (generated + kCapAreaM2 * (25.0 * kAmbient + 10.0 * 280.0)) / (kCapAreaM2 * 35.0);
    CHECK_THAT(b.convections[0].heatOut.si(), WithinRel(25.0 * kCapAreaM2 * (tb - kAmbient), 1e-6));
    CHECK_THAT(b.convections[1].heatOut.si(), WithinRel(10.0 * kCapAreaM2 * (tb - 280.0), 1e-6));
    CHECK_THAT(b.convections[0].heatOut.si() + b.convections[1].heatOut.si(), WithinRel(generated, 1e-6));
    CHECK_THAT(b.terms.convectionHeat.si(), WithinRel(-generated, 1e-6));
    CHECK_THAT(b.residual().si(), WithinAbs(0.0, 1e-8));
    report("two additive convection conditions on one face", b);
}

// ===========================================================================
// Transient, end to end
// ===========================================================================

TEST_CASE("ThermalBalance_TransientInsulatedGenerationWithAPartialLastStep", "[thermal][balance][transient][generation][partial-step]") {
    // The insulated block generating 2.4 W, from 300 K, 7 s steps to 100 s: fourteen full steps and a last one of 2 s. Every step stores exactly
    // what is generated; the last step's energy is 2.4 * 2 = 4.8 J and its rate is 2.4 W (the nominal 7 s would give 0.686 W).
    BoundaryStudy study;
    const std::vector<ThermalLoad> loads{generationLoad(1, 1.0e5)};
    const Timing t = timing(300.0, 7.0, 100.0);
    const Chain c = chainFrom(study.document, study.model(), loads, {}, ThermalAnalysisMode::Transient);
    const TransientThermalSolution solved = solveTransient(study.document, c, loads, {}, t);
    const ThermalBalanceResult result = balanceTransient(study.document, c, solved, loads, {}, t);
    const double power = 1.0e5 * kVolume;
    REQUIRE(result.stateCount() == 15);
    REQUIRE(result.integrated().has_value());
    for (std::size_t k = 0; k < result.stateCount(); ++k) {
        const ThermalHeatBalance& s = result.states()[k];
        const double dt = k + 1 < 15 ? 7.0 : 2.0;
        CAPTURE(k);
        CHECK(s.step == k + 1);
        CHECK_THAT(s.stepSize->si(), WithinAbs(dt, 1e-12));
        CHECK_THAT(s.time->si(), WithinAbs(k + 1 < 15 ? 7.0 * static_cast<double>(k + 1) : 100.0, 1e-12));
        CHECK_THAT(s.terms.generatedHeat.si(), WithinRel(power, 1e-12));
        CHECK_THAT(s.terms.storedEnergyRate->si(), WithinRel(power, 1e-6));
        CHECK_THAT(s.storedEnergyChange->si(), WithinRel(power * dt, 1e-6));
        CHECK_THAT(s.residual().si(), WithinAbs(0.0, 1e-8));
        CHECK(s.status == BalanceStatus::Balanced);
        CHECK(s.reactions.empty());
    }
    reportWorst("transient insulated generation", result);
    const double wrongRate = result.states().back().storedEnergyChange->si() / 7.0;
    CHECK_THAT(wrongRate, WithinRel(power * 2.0 / 7.0, 1e-6));
    CHECK(std::abs(wrongRate - power) > 1.0);                        // a nominal-dt rate would be off by 71%
    // The integrated energy: 2.4 W for 100 s = 240 J, stored as rho cp V dT.
    const thermal::IntegratedEnergy integrated = *result.integrated();
    CHECK_THAT(integrated.span.si(), WithinAbs(100.0, 1e-12));
    CHECK_THAT(integrated.external.si(), WithinRel(power * 100.0, 1e-9));
    CHECK_THAT(integrated.stored.si(), WithinRel(power * 100.0, 1e-6));
    CHECK_THAT(integrated.endpoints.si(), WithinRel(power * 100.0, 1e-6));
    CHECK_THAT(integrated.endpoints.si(), WithinRel(integrated.stored.si(), 1e-9));
    CHECK(result.balanced());
    // rho cp V dT = 240 J: the mean temperature rise (T uniform in an insulated generating block).
    const double rise = solved.finalTemperatures()[0].si() - 300.0;
    CHECK_THAT(kHeatCapacityTotal * rise, WithinRel(power * 100.0, 1e-6));
    WARN(std::format("insulated generation: {} steps, last dt {:.1f} s, per-step storage rate {:.6f} W = generation {:.6f} W; integrated external {:.6f} J, "
                     "stored {:.6f} J, endpoints {:.6f} J",
                     result.stateCount(), result.states().back().stepSize->si(), result.states().back().terms.storedEnergyRate->si(), power,
                     integrated.external.si(), integrated.stored.si(), integrated.endpoints.si()));
}

TEST_CASE("ThermalBalance_TransientDirichletBlockBalancesEveryStep", "[thermal][balance][transient][reaction][capacity]") {
    // The block starts at 300 K and its caps are held at 400 K and 300 K: heat flows in while the body warms up. Every step balances with the
    // full discrete reaction (storage included), the stored energy is the full 1^T C dT, and over the run the heat the constraints supplied is
    // the heat stored.
    BoundaryStudy study;
    const std::vector<ThermalBoundaryCondition> conditions{temperatureBc(1, study.startCap(), 400.0), temperatureBc(2, study.endCap(), 300.0)};
    const Timing t = timing(300.0, 7.0, 100.0);
    const Chain c = chainFrom(study.document, study.model(), {}, conditions, ThermalAnalysisMode::Transient);
    const TransientThermalSolution solved = solveTransient(study.document, c, {}, conditions, t);
    const ThermalBalanceResult result = balanceTransient(study.document, c, solved, {}, conditions, t);
    REQUIRE(result.stateCount() == 15);
    double worst = 0.0;
    for (const ThermalHeatBalance& s : result.states()) {
        CHECK(s.status == BalanceStatus::Balanced);
        worst = std::max(worst, s.normalizedResidual());
        CHECK(s.reactions.size() == c.boundary.constraints().size());
    }
    // The first step is dominated by storage: the hot cap supplies more than the steady 270 W.
    CHECK(reactionOnCondition(c, result.states().front(), 0) > 270.0);
    // After many diffusion times the stored rate is small and the reactions approach +270 / -270.
    CHECK_THAT(reactionOnCondition(c, result.finalState(), 0), WithinRel(270.0, 5e-3));
    CHECK_THAT(reactionOnCondition(c, result.finalState(), 1), WithinRel(-270.0, 5e-3));
    // The energy that entered at the hot cap and left at the cold one differs by what the body stored.
    const thermal::IntegratedEnergy integrated = *result.integrated();
    CHECK(std::abs(integrated.residual.si()) <= integrated.allowed.si());
    CHECK_THAT(integrated.stored.si(), WithinRel(integrated.endpoints.si(), 1e-9));
    CHECK(result.balanced());
    // The total stored equals rho cp times the mean rise, approximately (consistent mass, non-uniform mesh).
    CHECK(integrated.stored.si() > 0.0);
    report("transient Dirichlet block, worst step", [&] {
        const auto it = std::ranges::max_element(result.states(), {}, [](const ThermalHeatBalance& s) { return s.normalizedResidual(); });
        return *it;
    }());
    WARN(std::format("transient Dirichlet block: worst normalized residual {:.3e}; first-step hot reaction {:.4f} W; final-step hot reaction {:.4f} W; stored {:.4f} J",
                     worst, reactionOnCondition(c, result.states().front(), 0), reactionOnCondition(c, result.finalState(), 0),
                     integrated.stored.si()));
}

TEST_CASE("ThermalBalance_TransientConvectionCoolingOnTheBlock", "[thermal][balance][transient][convection]") {
    // A block at 400 K cooling through its end cap (h = 25 W/(m^2 K) to 293.15 K), insulated elsewhere: the body loses heat, the
    // storage rate is negative and equals the convection of the SAME step, which is evaluated at T[n+1] and not at T[n].
    BoundaryStudy study;
    const std::vector<ThermalBoundaryCondition> conditions{convectionBc(1, study.endCap(), 25.0, kAmbient)};
    const Timing t = timing(400.0, 50.0, 500.0);
    const Chain c = chainFrom(study.document, study.model(), {}, conditions, ThermalAnalysisMode::Transient);
    const TransientThermalSolution solved = solveTransient(study.document, c, {}, conditions, t);
    const ThermalBalanceResult result = balanceTransient(study.document, c, solved, {}, conditions, t);
    REQUIRE(result.stateCount() == 10);
    for (std::size_t k = 0; k < result.stateCount(); ++k) {
        const ThermalHeatBalance& s = result.states()[k];
        CAPTURE(k);
        CHECK(s.terms.storedEnergyRate->si() < 0.0);
        CHECK(s.terms.convectionHeat.si() < 0.0);
        CHECK_THAT(s.terms.convectionHeat.si(), WithinRel(s.terms.storedEnergyRate->si(), 1e-6));
        CHECK(s.reactions.empty());
        CHECK(s.status == BalanceStatus::Balanced);
        // At T[n] the same face would exchange more: the old state is hotter. It is not what the step's equation uses.
        const auto atOld = thermal::convectionHeatOf(c.boundary.convections()[0], solved.temperaturesAt(k));
        REQUIRE(atOld.has_value());
        CHECK(atOld->heatInto.si() < s.terms.convectionHeat.si());
    }
    CHECK(result.balanced());
    reportWorst("transient convection cooling", result);
}

TEST_CASE("ThermalBalance_TransientNeedsEveryStepToBalanceEachStep", "[thermal][balance][transient][step-data]") {
    // A history that stored only its final state has no states between the ends: the interval from t = 0 to t = 100 s is not a backward-Euler
    // step, and BALANCE neither integrates again nor pretends it is one. One step needs no intermediate state.
    BoundaryStudy study;
    const std::vector<ThermalLoad> loads{generationLoad(1, 1.0e5)};
    const Chain c = chainFrom(study.document, study.model(), loads, {}, ThermalAnalysisMode::Transient);

    const Timing sparse = timing(300.0, 25.0, 100.0, thermal::TransientOutputPolicy::FinalOnly);
    const TransientThermalSolution many = solveTransient(study.document, c, loads, {}, sparse);
    REQUIRE(many.sampleCount() == 1);
    const Result<ThermalBalanceResult> refused = thermal::recoverTransientBalance(study.document, c.model, c.map, c.system, many, c.loads,
                                                                                  c.boundary, loads, {}, sparse.initial, sparse.transient, {});
    REQUIRE_FALSE(refused.has_value());
    CHECK_THAT(refused.error().message, ContainsSubstring("EveryStep"));
    const auto detail = thermal::transientBalanceFailure(study.document, c.model, c.map, c.system, many, c.loads, c.boundary, loads, {},
                                                         sparse.initial, sparse.transient, {});
    REQUIRE(detail.has_value());
    CHECK(detail->problem == BalanceProblem::StepDataMissing);

    const Timing single = timing(300.0, 100.0, 100.0, thermal::TransientOutputPolicy::FinalOnly);
    const TransientThermalSolution one = solveTransient(study.document, c, loads, {}, single);
    const ThermalBalanceResult balanced = balanceTransient(study.document, c, one, loads, {}, single);
    REQUIRE(balanced.stateCount() == 1);
    CHECK(balanced.states()[0].status == BalanceStatus::Balanced);
    CHECK_THAT(balanced.states()[0].terms.storedEnergyRate->si(), WithinRel(1.0e5 * kVolume, 1e-6));
}

// ===========================================================================
// Currentness and provenance
// ===========================================================================

TEST_CASE("ThermalBalance_RejectsStaleSteadyResult", "[thermal][balance][currentness]") {
    BoundaryStudy study;
    const std::vector<ThermalLoad> loads{generationLoad(1, 1.0e5)};
    const std::vector<ThermalBoundaryCondition> conditions{temperatureBc(1, study.startCap(), 400.0),
                                                            convectionBc(2, study.endCap(), 25.0, kAmbient)};
    const Chain c = chainFrom(study.document, study.model(), loads, conditions);
    const SteadyThermalSolution solved = solveSteady(study.document, c, loads, conditions);
    const ThermalBalanceResult result = balanceSteady(study.document, c, solved, loads, conditions);
    const ThermalSolverSettings defaults{};
    REQUIRE(thermal::isCurrent(result, study.document, c.model, loads, conditions, defaults));
    REQUIRE(thermal::requireCurrent(result, study.document, c.model, loads, conditions, defaults).has_value());

    const auto refusal = [&](const std::vector<ThermalLoad>& l, const std::vector<ThermalBoundaryCondition>& b,
                             const ThermalSolverSettings& s) {
        return thermal::steadyBalanceFailure(study.document, c.model, c.map, c.system, solved, c.loads, c.boundary, l, b, s);
    };
    REQUIRE_FALSE(refusal(loads, conditions, defaults).has_value());

    SECTION("a load edit") {
        const std::vector<ThermalLoad> edited{generationLoad(1, 2.0e5)};
        const auto refused = refusal(edited, conditions, defaults);
        REQUIRE(refused.has_value());
        CHECK(refused->problem == BalanceProblem::SourceMismatch);
        CHECK_THAT(refused->error.message, ContainsSubstring("loads"));
        CHECK_FALSE(thermal::isCurrent(result, study.document, c.model, edited, conditions, defaults));
    }
    SECTION("a prescribed-temperature edit") {
        std::vector<ThermalBoundaryCondition> edited = conditions;
        edited[0] = temperatureBc(1, study.startCap(), 410.0);
        const auto refused = refusal(loads, edited, defaults);
        REQUIRE(refused.has_value());
        CHECK(refused->problem == BalanceProblem::SourceMismatch);
        CHECK_THAT(refused->error.message, ContainsSubstring("prescribed_temperatures"));
        CHECK_FALSE(thermal::isCurrent(result, study.document, c.model, loads, edited, defaults));
    }
    SECTION("a convection edit") {
        std::vector<ThermalBoundaryCondition> edited = conditions;
        edited[1] = convectionBc(2, study.endCap(), 30.0, kAmbient);
        const auto refused = refusal(loads, edited, defaults);
        REQUIRE(refused.has_value());
        CHECK(refused->problem == BalanceProblem::SourceMismatch);
        CHECK_FALSE(thermal::isCurrent(result, study.document, c.model, loads, edited, defaults));
    }
    SECTION("a solver-settings edit") {
        const ThermalSolverSettings edited{.relativeResidualTolerance = 1.0e-7, .pivotFloor = 1.0e-12};
        const auto refused = refusal(loads, conditions, edited);
        REQUIRE(refused.has_value());
        CHECK(refused->problem == BalanceProblem::SourceMismatch);
        CHECK_THAT(refused->error.message, ContainsSubstring("settings"));
        CHECK_FALSE(thermal::isCurrent(result, study.document, c.model, loads, conditions, edited));
    }
    SECTION("a conductivity edit: the old temperatures are not balanced against the new material") {
        materials::ThermalProperties properties = features::findMaterial(study.document, study.material)->definition().thermal;
        properties.thermalConductivity = materials::MaterialProperty<ThermalConductivity>::known(50_W_per_m_K);
        REQUIRE(features::setMaterialThermal(study.document, study.material, properties).has_value());
        const auto refused = refusal(loads, conditions, defaults);
        REQUIRE(refused.has_value());
        CHECK(refused->problem == BalanceProblem::SourceMismatch);
        CHECK_THAT(refused->error.message, ContainsSubstring("material"));
        CHECK_FALSE(thermal::isCurrent(result, study.document, c.model, loads, conditions, defaults));
    }
    SECTION("a steady result asked about as transient, and the reverse") {
        const Timing t = timing(300.0, 1.0, 2.0);
        const auto confused = thermal::requireCurrent(result, study.document, c.model, loads, conditions, t.initial, t.transient, defaults);
        REQUIRE_FALSE(confused.has_value());
        CHECK_THAT(confused.error().message, ContainsSubstring("steady solution"));
    }
}

TEST_CASE("ThermalBalance_RejectsStaleTransientResult", "[thermal][balance][currentness][transient]") {
    BoundaryStudy study;
    const std::vector<ThermalLoad> loads{generationLoad(1, 1.0e5)};
    const std::vector<ThermalBoundaryCondition> conditions{convectionBc(1, study.endCap(), 25.0, kAmbient)};
    const Timing t = timing(300.0, 10.0, 40.0);
    const Chain c = chainFrom(study.document, study.model(), loads, conditions, ThermalAnalysisMode::Transient);
    const TransientThermalSolution solved = solveTransient(study.document, c, loads, conditions, t);
    const ThermalBalanceResult result = balanceTransient(study.document, c, solved, loads, conditions, t);
    REQUIRE(thermal::isCurrent(result, study.document, c.model, loads, conditions, t.initial, t.transient, {}));

    const Timing edited = timing(300.0, 5.0, 40.0);
    CHECK_FALSE(thermal::isCurrent(result, study.document, c.model, loads, conditions, edited.initial, edited.transient, {}));
    const auto refused = thermal::transientBalanceFailure(study.document, c.model, c.map, c.system, solved, c.loads, c.boundary, loads,
                                                          conditions, edited.initial, edited.transient, {});
    REQUIRE(refused.has_value());
    CHECK(refused->problem == BalanceProblem::SourceMismatch);
    CHECK_THAT(refused->error.message, ContainsSubstring("time_step"));
    // The initial condition is part of the history.
    const Timing warmer = timing(310.0, 10.0, 40.0);
    CHECK_FALSE(thermal::isCurrent(result, study.document, c.model, loads, conditions, warmer.initial, warmer.transient, {}));
    // A transient result asked about as steady says so.
    const auto confused = thermal::requireCurrent(result, study.document, c.model, loads, conditions, ThermalSolverSettings{});
    REQUIRE_FALSE(confused.has_value());
    CHECK_THAT(confused.error().message, ContainsSubstring("transient history"));
}

TEST_CASE("ThermalBalance_RejectsMismatchedAssemblySource", "[thermal][balance][currentness][source]") {
    // The solution is for generation 1e5; the system presented was assembled for 2e5 (a different load list). Counts and sizes agree, the
    // sources do not, and the balance is refused rather than computed from a system the solution did not come from.
    BoundaryStudy study;
    const std::vector<ThermalLoad> loads{generationLoad(1, 1.0e5)};
    const std::vector<ThermalLoad> other{generationLoad(1, 2.0e5)};
    const std::vector<ThermalBoundaryCondition> conditions{temperatureBc(1, study.startCap(), 400.0)};
    const Chain c = chainFrom(study.document, study.model(), loads, conditions);
    const Chain stranger = chainFrom(study.document, study.model(), other, conditions);
    REQUIRE(stranger.system.dofCount() == c.system.dofCount());
    const SteadyThermalSolution solved = solveSteady(study.document, c, loads, conditions);
    const auto refused = thermal::steadyBalanceFailure(study.document, c.model, c.map, stranger.system, solved, c.loads, c.boundary, loads,
                                                       conditions, {});
    REQUIRE(refused.has_value());
    CHECK(refused->problem == BalanceProblem::SourceMismatch);
    CHECK_THAT(refused->error.message, ContainsSubstring("the assembled system no longer describes"));   // named as stale, with its reason
    // Prepared loads of another list are refused likewise.
    const auto wrongLoads = thermal::steadyBalanceFailure(study.document, c.model, c.map, c.system, solved, stranger.loads, c.boundary,
                                                          loads, conditions, {});
    REQUIRE(wrongLoads.has_value());
    CHECK(wrongLoads->problem == BalanceProblem::SourceMismatch);
    CHECK_THAT(wrongLoads->error.message, ContainsSubstring("the prepared heat input no longer describes"));
    // A prepared boundary for other conditions (a prescribed temperature of 410 K, where the solution holds 400 K) is refused as stale, before
    // the arithmetic can notice that its constraints are not the solution's.
    const std::vector<ThermalBoundaryCondition> hotter{temperatureBc(1, study.startCap(), 410.0)};
    const Chain otherBoundary = chainFrom(study.document, study.model(), loads, hotter);
    const auto wrongBoundary = thermal::steadyBalanceFailure(study.document, c.model, c.map, c.system, solved, c.loads,
                                                             otherBoundary.boundary, loads, conditions, {});
    REQUIRE(wrongBoundary.has_value());
    CHECK(wrongBoundary->problem == BalanceProblem::SourceMismatch);
    CHECK_THAT(wrongBoundary->error.message, ContainsSubstring("the prepared boundary no longer describes"));
    // The seam, which has no document, also compares the system with the solution by value.
    const auto seam = thermal::balanceSteadySolution(stranger.system, c.map, c.boundary.constraints(), stranger.loads.contributions(),
                                                     c.boundary.convections(), solved);
    REQUIRE_FALSE(seam.has_value());
    CHECK(seam.error().problem == BalanceProblem::SourceMismatch);
}

TEST_CASE("ThermalBalance_RemeshStalesBalanceEvenWhenCountsAndIdsAgree", "[thermal][balance][currentness][remesh]") {
    BoundaryStudy study;
    const std::vector<ThermalLoad> loads{generationLoad(1, 1.0e5)};
    const std::vector<ThermalBoundaryCondition> conditions{temperatureBc(1, study.startCap(), 400.0), temperatureBc(2, study.endCap(), 300.0)};
    std::optional<ThermalBalanceResult> oldBalance;
    std::optional<SteadyThermalSolution> oldSolution;
    meshing::MeshStamp stamp1{};
    std::size_t nodes1 = 0;
    std::size_t tets1 = 0;
    std::vector<NodeId> nodeIds1;
    {
        const Chain c = chainFrom(study.document, study.model(), loads, conditions);
        oldSolution = solveSteady(study.document, c, loads, conditions);
        oldBalance = balanceSteady(study.document, c, *oldSolution, loads, conditions);
        stamp1 = c.model.mesh().mesh().stamp();
        nodes1 = c.model.mesh().nodeCount();
        tets1 = c.model.mesh().mesh().tetrahedra().size();
        for (const meshing::Node& node : c.model.mesh().mesh().nodes()) {
            nodeIds1.push_back(node.id);
        }
    }
    study.remesh();
    const Chain c2 = chainFrom(study.document, study.model(), loads, conditions);
    // THE ADVERSARIAL CASE: identical counts, identical numeric ids, a different stamp.
    REQUIRE(c2.model.mesh().nodeCount() == nodes1);
    REQUIRE(c2.model.mesh().mesh().tetrahedra().size() == tets1);
    std::vector<NodeId> nodeIds2;
    for (const meshing::Node& node : c2.model.mesh().mesh().nodes()) {
        nodeIds2.push_back(node.id);
    }
    CHECK(nodeIds1 == nodeIds2);                                                   // the same ids
    CHECK_FALSE(c2.model.mesh().mesh().stamp() == stamp1);                         // not the same mesh

    CHECK_FALSE(thermal::isCurrent(*oldBalance, study.document, c2.model, loads, conditions, {}));
    // The old solution, the new model, the new system: refused before any arithmetic.
    const auto refused = thermal::steadyBalanceFailure(study.document, c2.model, c2.map, c2.system, *oldSolution, c2.loads, c2.boundary,
                                                       loads, conditions, {});
    REQUIRE(refused.has_value());
    CHECK(refused->problem == BalanceProblem::SourceMismatch);
    CHECK_THAT(refused->error.message, ContainsSubstring("mesh"));
    // The seam, with the new system and the old solution, refuses on the source by value.
    const auto seam = thermal::balanceSteadySolution(c2.system, c2.map, c2.boundary.constraints(), c2.loads.contributions(),
                                                     c2.boundary.convections(), *oldSolution);
    REQUIRE_FALSE(seam.has_value());
    CHECK(seam.error().problem == BalanceProblem::SourceMismatch);
    // A fresh solve and balance is current.
    const SteadyThermalSolution fresh = solveSteady(study.document, c2, loads, conditions);
    const ThermalBalanceResult freshBalance = balanceSteady(study.document, c2, fresh, loads, conditions);
    CHECK(thermal::isCurrent(freshBalance, study.document, c2.model, loads, conditions, {}));
    CHECK(freshBalance.source().system.mesh == c2.model.mesh().mesh().stamp());
    WARN(std::format("remesh: {} nodes and {} tetrahedra with the same ids in both generations; the old balance is stale and refused, the new one is current",
                     nodes1, tets1));
}

TEST_CASE("ThermalBalance_FailedRecoveryDoesNotPublishACurrentBalance", "[thermal][balance][currentness][lifecycle]") {
    BoundaryStudy study;
    const std::vector<ThermalLoad> loads{generationLoad(1, 1.0e5)};
    const std::vector<ThermalBoundaryCondition> conditions{temperatureBc(1, study.startCap(), 400.0), temperatureBc(2, study.endCap(), 300.0)};
    const Chain c = chainFrom(study.document, study.model(), loads, conditions);
    const SteadyThermalSolution solved = solveSteady(study.document, c, loads, conditions);
    const ThermalBalanceResult balance = balanceSteady(study.document, c, solved, loads, conditions);
    REQUIRE(thermal::isCurrent(balance, study.document, c.model, loads, conditions, {}));

    materials::ThermalProperties properties = features::findMaterial(study.document, study.material)->definition().thermal;
    properties.thermalConductivity = materials::MaterialProperty<ThermalConductivity>::known(60_W_per_m_K);
    REQUIRE(features::setMaterialThermal(study.document, study.material, properties).has_value());
    // The result is stale. A recovery attempted with the old solution fails...
    CHECK_FALSE(thermal::isCurrent(balance, study.document, c.model, loads, conditions, {}));
    CHECK_FALSE(thermal::recoverSteadyBalance(study.document, c.model, c.map, c.system, solved, c.loads, c.boundary, loads, conditions, {})
                    .has_value());
    // ...and the failure leaves the held result as it was: the old values, still stale.
    CHECK_FALSE(thermal::isCurrent(balance, study.document, c.model, loads, conditions, {}));
    CHECK_THAT(balance.finalState().terms.generatedHeat.si(), WithinRel(1.0e5 * kVolume, 1e-12));
    CHECK(balance.finalState().status == BalanceStatus::Balanced);
}

TEST_CASE("ThermalBalance_RecoveryDoesNotMutateItsInputs", "[thermal][balance][purity]") {
    BoundaryStudy study;
    const std::vector<ThermalLoad> loads{generationLoad(1, 1.0e5)};
    const std::vector<ThermalBoundaryCondition> conditions{temperatureBc(1, study.startCap(), 400.0),
                                                            convectionBc(2, study.endCap(), 25.0, kAmbient)};
    const Chain c = chainFrom(study.document, study.model(), loads, conditions);
    const SteadyThermalSolution solved = solveSteady(study.document, c, loads, conditions);
    const SteadyThermalSolution solutionBefore = solved;
    const ThermalSystem systemBefore = c.system;
    const PreparedThermalLoads loadsBefore = c.loads;
    const PreparedThermalBoundary boundaryBefore = c.boundary;
    const meshing::MeshStamp stamp = c.model.mesh().mesh().stamp();
    for (int i = 0; i < 3; ++i) {
        (void)balanceSteady(study.document, c, solved, loads, conditions);
    }
    CHECK(solved == solutionBefore);
    CHECK(c.system == systemBefore);
    CHECK(c.loads == loadsBefore);
    CHECK(c.boundary == boundaryBefore);
    CHECK(c.model.mesh().mesh().stamp() == stamp);
}

// ===========================================================================
// Determinism, tolerance measurement and the large models
// ===========================================================================

TEST_CASE("ThermalBalance_RepeatedBlockBalanceIsDeterministic", "[thermal][balance][determinism]") {
    BoundaryStudy study;
    const std::vector<ThermalLoad> loads{surfaceLoad(1, study.side(1), 5.0e3), generationLoad(2, 1.0e5)};
    const std::vector<ThermalBoundaryCondition> conditions{temperatureBc(1, study.startCap(), 400.0),
                                                            convectionBc(2, study.endCap(), 25.0, kAmbient)};
    const Chain c = chainFrom(study.document, study.model(), loads, conditions);
    const SteadyThermalSolution solved = solveSteady(study.document, c, loads, conditions);
    const ThermalBalanceResult first = balanceSteady(study.document, c, solved, loads, conditions);
    int differences = 0;
    for (int i = 0; i < 20; ++i) {
        if (!(balanceSteady(study.document, c, solved, loads, conditions) == first)) {
            ++differences;
        }
    }
    CHECK(differences == 0);
    WARN(std::format("determinism: 20 repeated block balances, {} differences", differences));
    WARN(std::format("bit fingerprint, steady block balance (Dirichlet + convection + surface + generation): {:016x}", fingerprintOf(first)));
}

TEST_CASE("ThermalBalance_FineCylinderSteadyBalanceSmoke", "[thermal][balance][smoke][steady][sparse]") {
    // The fine mesh of a 6 mm radius cylinder: 350 K on one cap, convection on the other, volumetric generation. A real P16 mesh with a curved wall.
    RoundStudy part;
    const std::vector<ThermalLoad> loads{generationLoad(1, 2.0e6)};
    const std::vector<ThermalBoundaryCondition> conditions{
        temperatureBc(1, FaceName{part.feature, FaceSelector{.role = FaceRole::StartCap}}, 350.0), convectionBc(2, part.endCap(), 25.0, kAmbient)};
    const Chain c = chainFrom(part.document, part.model(part.fine), loads, conditions);
    const SteadyThermalSolution solved = solveSteady(part.document, c, loads, conditions);
    const ThermalBalanceResult result = balanceSteady(part.document, c, solved, loads, conditions);
    const ThermalHeatBalance& b = result.finalState();
    constexpr double pi = 3.14159265358979323846;
    const double volume = pi * RoundStudy::kRadius * RoundStudy::kRadius * 0.020;
    CHECK_THAT(b.terms.generatedHeat.si(), WithinRel(2.0e6 * volume, 1e-3));      // the polygonal cylinder is slightly smaller
    CHECK(b.reactions.size() == c.boundary.constraints().size());
    CHECK(std::isfinite(b.terms.convectionHeat.si()));
    CHECK(std::isfinite(b.terms.prescribedTemperatureReaction.si()));
    CHECK(b.status == BalanceStatus::Balanced);
    CHECK(c.system.conductance().nonZeros() < solved.dofCount() * solved.dofCount() / 10);   // sparse
    const auto again = balanceSteady(part.document, c, solved, loads, conditions);
    CHECK(again == result);
    report("steady fine cylinder", b);
    WARN(std::format("steady fine cylinder: Ndof {}, Nconstrained {}, {} loads, {} convection facets, reactions {}, generation {:.6f} W, "
                     "convection {:.6f} W, reaction {:.6f} W, residual {:.3e} W, normalized {:.3e}",
                     solved.dofCount(), c.boundary.constraints().size(), c.loads.contributions().size(), c.system.convectionFacetCount(),
                     b.reactions.size(), b.terms.generatedHeat.si(), b.terms.convectionHeat.si(),
                     b.terms.prescribedTemperatureReaction.si(), b.residual().si(), b.normalizedResidual()));
    WARN(std::format("bit fingerprint, steady fine cylinder balance: {:016x}", fingerprintOf(result)));
}

TEST_CASE("ThermalBalance_FineCylinderTransientBalanceSmoke", "[thermal][balance][smoke][transient][sparse]") {
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
    const std::vector<ThermalLoad> loads{generationLoad(1, 2.0e6)};
    const std::vector<ThermalBoundaryCondition> conditions{
        temperatureBc(1, FaceName{part.feature, FaceSelector{.role = FaceRole::StartCap}}, 350.0), convectionBc(2, part.endCap(), 25.0, kAmbient)};
    const Timing t = timing(kAmbient, 10.0, 60.0);
    const Chain c = chainFrom(part.document, part.model(part.fine), loads, conditions, ThermalAnalysisMode::Transient);
    const TransientThermalSolution solved = solveTransient(part.document, c, loads, conditions, t);
    const ThermalBalanceResult result = balanceTransient(part.document, c, solved, loads, conditions, t);
    REQUIRE(result.stateCount() == 6);
    double worst = 0.0;
    for (const ThermalHeatBalance& s : result.states()) {
        CHECK(std::isfinite(s.terms.storedEnergyRate->si()));
        CHECK(s.status == BalanceStatus::Balanced);
        worst = std::max(worst, s.normalizedResidual());
    }
    CHECK(result.balanced());
    reportWorst("transient fine cylinder", result);
    WARN(std::format("transient fine cylinder: Ndof {}, 6 steps of 10 s, worst normalized residual {:.3e}; step 1 storage {:.4f} W, reaction {:.4f} W; "
                     "step 6 storage {:.4f} W, reaction {:.4f} W; integrated external {:.4f} J, stored {:.4f} J",
                     solved.dofCount(), worst, result.states().front().terms.storedEnergyRate->si(),
                     result.states().front().terms.prescribedTemperatureReaction.si(), result.finalState().terms.storedEnergyRate->si(),
                     result.finalState().terms.prescribedTemperatureReaction.si(), result.integrated()->external.si(), result.integrated()->stored.si()));
    WARN(std::format("bit fingerprint, transient fine cylinder balance: {:016x}", fingerprintOf(result)));
}
