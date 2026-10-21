// P18-POST-001: the recovery against REAL P16 meshes and real documents.
//
// These are the AUTHORITY tests: a solved temperature field is recovered for the
// model it was solved for, and for no other. The recovery must refuse a solution that
// is stale -- a remesh that kept every count and every id, a conductivity edit, a load,
// a condition or a settings edit -- and must refuse a model that is older than the
// material it is paired with; it must reproduce the one-dimensional conduction problems
// whose flux is closed-form, end to end, on two different meshes; and a failed recovery
// must never make an old result current.
//
// The kernel's arithmetic and its refusals are checked on hand meshes in
// ThermalPostTests.cpp. Here a 40 x 30 x 20 mm steel block (k = 45 W/(m K), extruded
// along z) and a 6 mm radius cylinder are meshed by P16, assembled by P18-ASSEMBLY,
// solved by P18-SOLVE / P18-TRANSIENT and recovered by P18-POST. Every expectation is
// a closed form or an invariant.

#include "ThermalPostTestSupport.hpp"

#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <bit>
#include <format>

using namespace pstest;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using thermal::PreparedThermalBoundary;
using thermal::PreparedThermalLoads;
using thermal::StaleReason;
using thermal::ThermalAnalysisMode;
using thermal::ThermalLoad;
using thermal::VolumetricHeatSourceLoad;

namespace {

constexpr double kLength = 0.020;       // the extrusion depth, along z
constexpr double kConductivity = 45.0;  // W/(m K)

[[nodiscard]] ThermalLoad generationLoad(std::uint64_t id, double generation) {
    return ThermalLoad{ThermalLoadId::fromValue(id),
                       VolumetricHeatSourceLoad{.generation = VolumetricHeatSource::fromSi(generation)}};
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

[[nodiscard]] ThermalPostResult recoverSteady(const Document& document, const Chain& c, const SteadyThermalSolution& solution,
                                              const std::vector<ThermalLoad>& loads,
                                              const std::vector<ThermalBoundaryCondition>& conditions,
                                              const ThermalSolverSettings& settings = {}) {
    Result<ThermalPostResult> post = thermal::recoverSteadyThermal(document, c.model, c.map, solution, loads, conditions, settings);
    INFO((post.has_value() ? std::string{} : post.error().message));
    REQUIRE(post.has_value());
    return std::move(*post);
}

struct SlabErrors {
    double gradient = 0.0;
    double flux = 0.0;
    double magnitude = 0.0;
    double transverse = 0.0;
};

/// The error of every element of @p post against the closed form of a z-slab, hot at z = 0 and cold at z = L:
///   grad T = [0, 0, -(Thot - Tcold)/L]    q = [0, 0, +k (Thot - Tcold)/L]    |q| = k |Thot - Tcold| / L
[[nodiscard]] SlabErrors slabErrors(const ThermalPostResult& post, double hot, double cold, double k) {
    const double dTdz = (cold - hot) / kLength;
    const Vec3 gradient{0.0, 0.0, dTdz};
    const Vec3 flux{0.0, 0.0, -k * dTdz};
    SlabErrors out;
    for (std::size_t e = 0; e < post.elementCount(); ++e) {
        const Vec3 g = vec(post.finalState().fields.gradients[e]);
        const Vec3 q = vec(post.finalState().fields.heatFluxes[e]);
        out.gradient = std::max(out.gradient, maxDifference(g, gradient));
        out.flux = std::max(out.flux, maxDifference(q, flux));
        out.magnitude = std::max(out.magnitude, std::abs(post.finalState().fields.heatFluxMagnitudes[e].si() - k * std::abs(dTdz)));
        out.transverse = std::max({out.transverse, std::abs(g[0]), std::abs(g[1]), std::abs(q[0]), std::abs(q[1])});
    }
    return out;
}

} // namespace

// ===========================================================================
// The steady slab, end to end: mesh -> solve -> recover
// ===========================================================================

TEST_CASE("ThermalPost_SteadySlabOnTheBlockFollowsFourierLaw", "[thermal][post][slab][fourier][block]") {
    // 400 K at z = 0, 300 K at z = L = 20 mm, k = 45: dT/dz = -5000 K/m, q = [0, 0, +225000] W/m2 (heat flows toward +z,
    // from hot to cold). Recovered from the solver's own field on the P16 mesh.
    BoundaryStudy study;
    const std::vector<ThermalBoundaryCondition> conditions{temperatureBc(1, study.startCap(), 400.0),
                                                            temperatureBc(2, study.endCap(), 300.0)};
    const Chain c = chainFrom(study.document, study.model(), {}, conditions);
    const SteadyThermalSolution solved = solveSteady(study.document, c, {}, conditions);
    const ThermalPostResult post = recoverSteady(study.document, c, solved, {}, conditions);

    REQUIRE(post.elementCount() == c.model.mesh().mesh().tetrahedra().size());
    REQUIRE(post.elementCount() > 0);
    const SlabErrors errors = slabErrors(post, 400.0, 300.0, kConductivity);
    CHECK(errors.gradient <= 1e-4);        // 5000 K/m, so 2e-8 relative
    CHECK(errors.flux <= 1e-2);            // 225 000 W/m2
    CHECK(errors.magnitude <= 1e-2);
    CHECK(errors.transverse <= 1e-4);
    CHECK(vec(post.finalState().fields.heatFluxes[0])[2] > 0.0);
    CHECK_THAT(post.finalState().fields.heatFluxMagnitudes[0].si(), WithinRel(225000.0, 1e-7));

    // Temperatures: the solver's, in K, bit for bit, and found through the numbering.
    REQUIRE(post.finalState().temperatures.size() == solved.dofCount());
    for (std::size_t d = 0; d < solved.dofCount(); ++d) {
        CHECK(post.finalState().temperatures[d].si() == solved.temperatures()[d].si());
    }
    for (const meshing::Node& node : c.model.mesh().mesh().nodes()) {
        const auto t = post.temperatureOf(0, c.map, node.id);
        REQUIRE(t.has_value());
        CHECK_THAT(t->si(), WithinAbs(400.0 - 100.0 * node.position.z.si() / kLength, 1e-6));
    }
    WARN(std::format("steady block slab L = {} m, k = {}, 400 K -> 300 K: expected dT/dz = -5000 K/m, qz = +225000 W/m2; {} elements, "
                     "max errors {:.2e} K/m, {:.2e} W/m2, {:.2e} W/m2 (|q|), transverse {:.2e}",
                     kLength, kConductivity, post.elementCount(), errors.gradient, errors.flux, errors.magnitude, errors.transverse));

    // Reversed: 300 K at z = 0, 400 K at z = L: heat flows toward -z.
    const std::vector<ThermalBoundaryCondition> reversedConditions{temperatureBc(1, study.startCap(), 300.0),
                                                                    temperatureBc(2, study.endCap(), 400.0)};
    const Chain r = chainFrom(study.document, study.model(), {}, reversedConditions);
    const SteadyThermalSolution rSolved = solveSteady(study.document, r, {}, reversedConditions);
    const ThermalPostResult rPost = recoverSteady(study.document, r, rSolved, {}, reversedConditions);
    const SlabErrors reversed = slabErrors(rPost, 300.0, 400.0, kConductivity);
    CHECK(reversed.flux <= 1e-2);
    CHECK(vec(rPost.finalState().fields.heatFluxes[0])[2] < 0.0);
}

TEST_CASE("ThermalPost_SteadySlabOnTwoDifferentCylinderMeshesFollowsFourierLaw", "[thermal][post][slab][fourier][cylinder]") {
    // The same slab law on a 6 mm radius cylinder, on its coarse AND its fine mesh (different node and element counts).
    RoundStudy part;
    const std::vector<ThermalBoundaryCondition> conditions{
        temperatureBc(1, FaceName{part.feature, FaceSelector{.role = FaceRole::StartCap}}, 400.0),
        temperatureBc(2, part.endCap(), 300.0)};
    std::vector<std::size_t> elementCounts;
    for (const MeshControlId control : {part.coarse, part.fine}) {
        const Chain c = chainFrom(part.document, part.model(control), {}, conditions);
        const SteadyThermalSolution solved = solveSteady(part.document, c, {}, conditions);
        const ThermalPostResult post = recoverSteady(part.document, c, solved, {}, conditions);
        const SlabErrors errors = slabErrors(post, 400.0, 300.0, kConductivity);
        elementCounts.push_back(post.elementCount());
        CHECK(errors.gradient <= 1e-3);
        CHECK(errors.flux <= 0.1);
        CHECK(errors.magnitude <= 0.1);
        CHECK(errors.transverse <= 1e-3);
        WARN(std::format("steady cylinder slab, {} nodes / {} tetrahedra: max errors {:.2e} K/m, {:.2e} W/m2, transverse {:.2e}",
                         solved.dofCount(), post.elementCount(), errors.gradient, errors.flux, errors.transverse));
    }
    REQUIRE(elementCounts.size() == 2);
    CHECK(elementCounts[0] != elementCounts[1]);   // genuinely two meshes
}

// ===========================================================================
// Currentness and provenance
// ===========================================================================

TEST_CASE("ThermalPost_RecoveryIsRefusedForEveryStaleInput", "[thermal][post][currentness]") {
    BoundaryStudy study;
    const std::vector<ThermalLoad> loads{generationLoad(1, 1.0e5)};
    const std::vector<ThermalBoundaryCondition> conditions{temperatureBc(1, study.startCap(), 400.0),
                                                            convectionBc(2, study.endCap(), 25.0, 293.15)};
    const Chain c = chainFrom(study.document, study.model(), loads, conditions);
    const SteadyThermalSolution solved = solveSteady(study.document, c, loads, conditions);
    const ThermalPostResult post = recoverSteady(study.document, c, solved, loads, conditions);
    const ThermalSolverSettings defaults{};
    REQUIRE(thermal::isCurrent(post, study.document, c.model, loads, conditions, defaults));
    REQUIRE(thermal::requireCurrent(post, study.document, c.model, loads, conditions, defaults).has_value());

    const auto refusal = [&](const std::vector<ThermalLoad>& l, const std::vector<ThermalBoundaryCondition>& b,
                             const ThermalSolverSettings& s) {
        return thermal::steadyRecoveryFailure(study.document, c.model, c.map, solved, l, b, s);
    };
    SECTION("a load edit") {
        const std::vector<ThermalLoad> edited{generationLoad(1, 2.0e5)};
        const auto refused = refusal(edited, conditions, defaults);
        REQUIRE(refused.has_value());
        CHECK(refused->problem == PostProblem::PostSourceMismatch);
        CHECK_THAT(refused->error.message, ContainsSubstring("loads"));
        CHECK_FALSE(thermal::isCurrent(post, study.document, c.model, edited, conditions, defaults));
    }
    SECTION("a prescribed-temperature edit") {
        std::vector<ThermalBoundaryCondition> edited = conditions;
        edited[0] = temperatureBc(1, study.startCap(), 410.0);
        const auto refused = refusal(loads, edited, defaults);
        REQUIRE(refused.has_value());
        CHECK(refused->problem == PostProblem::PostSourceMismatch);
        CHECK_THAT(refused->error.message, ContainsSubstring("prescribed_temperatures"));
        CHECK_FALSE(thermal::isCurrent(post, study.document, c.model, loads, edited, defaults));
    }
    SECTION("a convection edit") {
        std::vector<ThermalBoundaryCondition> edited = conditions;
        edited[1] = convectionBc(2, study.endCap(), 30.0, 293.15);
        const auto refused = refusal(loads, edited, defaults);
        REQUIRE(refused.has_value());
        CHECK(refused->problem == PostProblem::PostSourceMismatch);
        CHECK_THAT(refused->error.message, ContainsSubstring("convections"));
    }
    SECTION("a solver-settings edit") {
        const ThermalSolverSettings edited{.relativeResidualTolerance = 1.0e-7, .pivotFloor = 1.0e-12};
        const auto refused = refusal(loads, conditions, edited);
        REQUIRE(refused.has_value());
        CHECK(refused->problem == PostProblem::PostSourceMismatch);
        CHECK_THAT(refused->error.message, ContainsSubstring("settings"));
        CHECK_FALSE(thermal::isCurrent(post, study.document, c.model, loads, conditions, edited));
    }
    SECTION("a conductivity edit: the old temperatures are not post-processed with the new k") {
        materials::ThermalProperties properties = features::findMaterial(study.document, study.material)->definition().thermal;
        properties.thermalConductivity = materials::MaterialProperty<ThermalConductivity>::known(50_W_per_m_K);
        REQUIRE(features::setMaterialThermal(study.document, study.material, properties).has_value());
        const auto refused = refusal(loads, conditions, defaults);
        REQUIRE(refused.has_value());
        CHECK(refused->problem == PostProblem::PostSourceMismatch);
        CHECK_THAT(refused->error.message, ContainsSubstring("material"));
        CHECK_FALSE(thermal::isCurrent(post, study.document, c.model, loads, conditions, defaults));
        const auto reasons = [&] {
            const auto now = thermal::currentSteadySolutionSource(study.document, c.model, loads, conditions, defaults);
            REQUIRE(now.has_value());
            return thermal::staleReasons(post.source(), thermal::postSourceOf(*now));
        }();
        CHECK(std::ranges::find(reasons, thermal::TransientStaleReason::Material) != reasons.end());
    }
    SECTION("a model older than a material edit is refused although the solution is current") {
        // The conductivity is edited, the whole chain is rebuilt and solved afresh (current for the new k), and then the
        // OLD model (which still carries k = 45) is offered with the NEW solution: pairing 45 with a 50-solution is refused.
        materials::ThermalProperties properties = features::findMaterial(study.document, study.material)->definition().thermal;
        properties.thermalConductivity = materials::MaterialProperty<ThermalConductivity>::known(50_W_per_m_K);
        REQUIRE(features::setMaterialThermal(study.document, study.material, properties).has_value());
        const Chain fresh = chainFrom(study.document, study.model(), loads, conditions);
        const SteadyThermalSolution freshSolved = solveSteady(study.document, fresh, loads, conditions);
        const auto okay = thermal::steadyRecoveryFailure(study.document, fresh.model, fresh.map, freshSolved, loads, conditions, defaults);
        CHECK_FALSE(okay.has_value());
        const auto refused = thermal::steadyRecoveryFailure(study.document, c.model, c.map, freshSolved, loads, conditions, defaults);
        REQUIRE(refused.has_value());
        CHECK(refused->problem == PostProblem::MaterialMismatch);
        CHECK_THAT(refused->error.message, ContainsSubstring("prepare the model again"));
    }
    SECTION("a transient result asked about as a steady one, and the reverse") {
        const auto confused = thermal::requireCurrent(post, study.document, c.model, loads, conditions,
                                                      thermal::UniformInitialTemperature{Temperature::fromSi(300.0)},
                                                      thermal::TransientSettings{.timeStep = Time::fromSi(1.0), .endTime = Time::fromSi(2.0)},
                                                      defaults);
        REQUIRE_FALSE(confused.has_value());
        CHECK_THAT(confused.error().message, ContainsSubstring("steady solution"));
    }
}

TEST_CASE("ThermalPost_RemeshStalesTheResultEvenWhenCountsAndIdsAgree", "[thermal][post][currentness][remesh]") {
    BoundaryStudy study;
    const std::vector<ThermalBoundaryCondition> conditions{temperatureBc(1, study.startCap(), 400.0),
                                                            temperatureBc(2, study.endCap(), 300.0)};
    // Everything from generation 1 is held by VALUE: a ThermalModel borrows the mesher's mesh and must not outlive a remesh.
    std::optional<ThermalPostResult> oldPost;
    std::optional<SteadyThermalSolution> oldSolution;
    std::optional<ThermalDofMap> oldMap;
    meshing::MeshStamp stamp1{};
    std::size_t nodes1 = 0;
    std::size_t tets1 = 0;
    std::vector<NodeId> nodeIds1;
    {
        const Chain c = chainFrom(study.document, study.model(), {}, conditions);
        oldSolution = solveSteady(study.document, c, {}, conditions);
        oldPost = recoverSteady(study.document, c, *oldSolution, {}, conditions);
        oldMap = c.map;
        stamp1 = c.model.mesh().mesh().stamp();
        nodes1 = c.model.mesh().nodeCount();
        tets1 = c.model.mesh().mesh().tetrahedra().size();
        for (const meshing::Node& node : c.model.mesh().mesh().nodes()) {
            nodeIds1.push_back(node.id);
        }
    }
    study.remesh();
    const Chain c2 = chainFrom(study.document, study.model(), {}, conditions);
    // THE ADVERSARIAL CASE: identical counts, identical numeric ids, a different stamp.
    REQUIRE(c2.model.mesh().nodeCount() == nodes1);
    REQUIRE(c2.model.mesh().mesh().tetrahedra().size() == tets1);
    std::vector<NodeId> nodeIds2;
    for (const meshing::Node& node : c2.model.mesh().mesh().nodes()) {
        nodeIds2.push_back(node.id);
    }
    CHECK(nodeIds1 == nodeIds2);
    CHECK_FALSE(c2.model.mesh().mesh().stamp() == stamp1);

    const ThermalSolverSettings defaults{};
    CHECK_FALSE(thermal::isCurrent(*oldPost, study.document, c2.model, {}, conditions, defaults));
    const auto refused = thermal::steadyRecoveryFailure(study.document, c2.model, c2.map, *oldSolution, {}, conditions, defaults);
    REQUIRE(refused.has_value());
    CHECK(refused->problem == PostProblem::PostSourceMismatch);
    CHECK_THAT(refused->error.message, ContainsSubstring("mesh"));
    // The seam, which has no document, refuses on the stamp alone.
    const auto viaSeam = thermal::recoverSteadyFromSolution(c2.model.mesh().mesh(), c2.map, ThermalConductivity::fromSi(kConductivity), *oldSolution);
    REQUIRE_FALSE(viaSeam.has_value());
    CHECK(viaSeam.error().problem == PostProblem::PostSourceMismatch);
    // The old numbering does not describe the new mesh either.
    CHECK_FALSE(oldPost->describes(c2.map));
    CHECK(oldPost->describes(*oldMap));
    // A fresh solve and recovery is current.
    const SteadyThermalSolution fresh = solveSteady(study.document, c2, {}, conditions);
    const ThermalPostResult freshPost = recoverSteady(study.document, c2, fresh, {}, conditions);
    CHECK(thermal::isCurrent(freshPost, study.document, c2.model, {}, conditions, defaults));
    CHECK(freshPost.source().system.mesh == c2.model.mesh().mesh().stamp());
    WARN(std::format("remesh: {} nodes and {} tetrahedra with the same ids in both generations; the old recovery is stale and refused, the new one is current",
                     nodes1, tets1));
}

TEST_CASE("ThermalPost_AFailedRecoveryNeverMakesAnOldResultCurrent", "[thermal][post][currentness][lifecycle]") {
    BoundaryStudy study;
    const std::vector<ThermalBoundaryCondition> conditions{temperatureBc(1, study.startCap(), 400.0),
                                                            temperatureBc(2, study.endCap(), 300.0)};
    const Chain c = chainFrom(study.document, study.model(), {}, conditions);
    const SteadyThermalSolution solved = solveSteady(study.document, c, {}, conditions);
    const ThermalPostResult post = recoverSteady(study.document, c, solved, {}, conditions);
    const ThermalSolverSettings defaults{};
    REQUIRE(thermal::isCurrent(post, study.document, c.model, {}, conditions, defaults));

    materials::ThermalProperties properties = features::findMaterial(study.document, study.material)->definition().thermal;
    properties.thermalConductivity = materials::MaterialProperty<ThermalConductivity>::known(60_W_per_m_K);
    REQUIRE(features::setMaterialThermal(study.document, study.material, properties).has_value());
    // The result is stale. A recovery attempted with the old solution fails...
    CHECK_FALSE(thermal::isCurrent(post, study.document, c.model, {}, conditions, defaults));
    CHECK_FALSE(thermal::recoverSteadyThermal(study.document, c.model, c.map, solved, {}, conditions, defaults).has_value());
    // ...and the failure does not touch the held result: it is still the old one, still stale.
    CHECK_FALSE(thermal::isCurrent(post, study.document, c.model, {}, conditions, defaults));
    CHECK(post.finalState().fields.heatFluxMagnitudes[0].si() > 0.0);
    CHECK_THAT(post.finalState().fields.heatFluxMagnitudes[0].si(), WithinRel(225000.0, 1e-6));   // k = 45, not 60
}

TEST_CASE("ThermalPost_RecoveryDoesNotMutateItsInputs", "[thermal][post][purity]") {
    BoundaryStudy study;
    const std::vector<ThermalLoad> loads{generationLoad(1, 1.0e5)};
    const std::vector<ThermalBoundaryCondition> conditions{temperatureBc(1, study.startCap(), 400.0),
                                                            convectionBc(2, study.endCap(), 25.0, 293.15)};
    const Chain c = chainFrom(study.document, study.model(), loads, conditions);
    const SteadyThermalSolution solved = solveSteady(study.document, c, loads, conditions);
    const SteadyThermalSolution copy = solved;
    const ThermalSystem systemBefore = c.system;
    const std::uint64_t revision = study.document.revision();
    const meshing::MeshStamp stamp = c.model.mesh().mesh().stamp();
    const std::vector<Temperature> before(solved.temperatures().begin(), solved.temperatures().end());
    for (int i = 0; i < 3; ++i) {
        (void)thermal::recoverSteadyThermal(study.document, c.model, c.map, solved, loads, conditions, ThermalSolverSettings{});
    }
    CHECK(solved == copy);
    CHECK(std::equal(before.begin(), before.end(), solved.temperatures().begin()));
    CHECK(c.system == systemBefore);
    CHECK(study.document.revision() == revision);
    CHECK(c.model.mesh().mesh().stamp() == stamp);
    CHECK(study.mesher.currency(study.document, study.control) == meshing::MeshCurrency::Current);
}

// ===========================================================================
// Transient
// ===========================================================================

TEST_CASE("ThermalPost_TransientRecoveryOnTheBlockKeepsOrderAndFollowsTheSolution", "[thermal][post][transient][block]") {
    BoundaryStudy study;
    const std::vector<ThermalBoundaryCondition> conditions{temperatureBc(1, study.startCap(), 400.0),
                                                            temperatureBc(2, study.endCap(), 300.0)};
    const thermal::UniformInitialTemperature initial{Temperature::fromSi(300.0)};
    const thermal::TransientSettings transient{.timeStep = Time::fromSi(7.0), .endTime = Time::fromSi(100.0),
                                               .output = thermal::TransientOutputPolicy::EveryStep};
    const ThermalSolverSettings settings{};
    const Chain c = chainFrom(study.document, study.model(), {}, conditions, ThermalAnalysisMode::Transient);
    Result<TransientThermalSolution> solved = thermal::solveTransientThermal(study.document, c.model, c.map, c.system, c.boundary,
                                                                              {}, conditions, initial, transient, settings);
    INFO((solved.has_value() ? std::string{} : solved.error().message));
    REQUIRE(solved.has_value());
    Result<ThermalPostResult> post =
        thermal::recoverTransientThermal(study.document, c.model, c.map, *solved, {}, conditions, initial, transient, settings);
    INFO((post.has_value() ? std::string{} : post.error().message));
    REQUIRE(post.has_value());

    // 100 / 7: fourteen full steps and a shortened last one; sixteen states including the initial one.
    REQUIRE(solved->sampleCount() == 16);
    REQUIRE(post->stateCount() == 16);
    CHECK(post->mode() == ThermalAnalysisMode::Transient);
    for (std::size_t i = 0; i < post->stateCount(); ++i) {
        const ThermalPostState& state = post->states()[i];
        REQUIRE(state.time.has_value());
        CHECK(state.time->si() == solved->timeOf(i).si());     // the solution's order and times, nothing re-sorted
        if (i > 0) {
            CHECK(state.time->si() > post->states()[i - 1].time->si());
        }
        REQUIRE(state.temperatures.size() == solved->temperaturesAt(i).size());
        for (std::size_t d = 0; d < state.temperatures.size(); ++d) {
            CHECK(state.temperatures[d].si() == solved->temperaturesAt(i)[d].si());
        }
        REQUIRE(state.fields.gradients.size() == post->elementCount());
        // The same Fourier law at every state.
        for (std::size_t e = 0; e < state.fields.gradients.size(); ++e) {
            const Vec3 g = vec(state.fields.gradients[e]);
            const Vec3 q = vec(state.fields.heatFluxes[e]);
            CHECK(maxDifference(q, scaled(g, -kConductivity)) <= 1e-9 * std::max(1.0, norm(g) * kConductivity));
            CHECK(dot(q, g) <= 0.0);
        }
    }
    CHECK(post->finalState().time->si() == 100.0);
    CHECK(post->source().mode == ThermalAnalysisMode::Transient);
    REQUIRE(post->source().transient.has_value());
    CHECK(*post->source().transient == transient);
    // At 100 s (many diffusion times of this 20 mm block) the history has settled to the slab.
    const SlabErrors settled = slabErrors(*post, 400.0, 300.0, kConductivity);
    CHECK(settled.flux <= 1.0);
    CHECK(thermal::isCurrent(*post, study.document, c.model, {}, conditions, initial, transient, settings));

    // A time-step edit stales the history, and the recovery of the old solution is refused.
    const thermal::TransientSettings edited{.timeStep = Time::fromSi(5.0), .endTime = Time::fromSi(100.0),
                                            .output = thermal::TransientOutputPolicy::EveryStep};
    CHECK_FALSE(thermal::isCurrent(*post, study.document, c.model, {}, conditions, initial, edited, settings));
    // A transient history asked about as a steady solution is a different question, and says so.
    const auto askedAsSteady = thermal::requireCurrent(*post, study.document, c.model, {}, conditions, settings);
    REQUIRE_FALSE(askedAsSteady.has_value());
    CHECK_THAT(askedAsSteady.error().message, ContainsSubstring("transient history"));
    CHECK_FALSE(thermal::isCurrent(*post, study.document, c.model, {}, conditions, settings));
    const auto refused = thermal::transientRecoveryFailure(study.document, c.model, c.map, *solved, {}, conditions, initial, edited, settings);
    REQUIRE(refused.has_value());
    CHECK(refused->problem == PostProblem::PostSourceMismatch);
    CHECK_THAT(refused->error.message, ContainsSubstring("time_step"));
    WARN(std::format("transient block recovery: 16 states in the solution's order (the last step shortened to 2 s), final |q| {:.6f} W/m2 "
                     "against the slab's 225000; max flux error at t_end {:.2e} W/m2",
                     post->finalState().fields.heatFluxMagnitudes[0].si(), settled.flux));
}

TEST_CASE("ThermalPost_TransientUniformHeatingHasZeroConductiveFluxOnTheBlock", "[thermal][post][transient][storage]") {
    // The insulated block heats uniformly: dT/dt != 0 everywhere, grad T = 0 everywhere, q = 0 everywhere. A storage term
    // C dT/dt, a generation term or a convection term inside q would show here.
    BoundaryStudy study;
    const std::vector<ThermalLoad> loads{generationLoad(1, 2.5e6)};
    const thermal::UniformInitialTemperature initial{Temperature::fromSi(300.0)};
    const thermal::TransientSettings transient{.timeStep = Time::fromSi(10.0), .endTime = Time::fromSi(100.0),
                                               .output = thermal::TransientOutputPolicy::EveryStep};
    const Chain c = chainFrom(study.document, study.model(), loads, {}, ThermalAnalysisMode::Transient);
    Result<TransientThermalSolution> solved = thermal::solveTransientThermal(study.document, c.model, c.map, c.system, c.boundary,
                                                                              loads, {}, initial, transient, ThermalSolverSettings{});
    REQUIRE(solved.has_value());
    Result<ThermalPostResult> post = thermal::recoverTransientThermal(study.document, c.model, c.map, *solved, loads, {}, initial,
                                                                       transient, ThermalSolverSettings{});
    REQUIRE(post.has_value());
    double worstGradient = 0.0;
    double worstFlux = 0.0;
    double heated = 0.0;
    for (const ThermalPostState& state : post->states()) {
        for (std::size_t e = 0; e < state.fields.gradients.size(); ++e) {
            worstGradient = std::max(worstGradient, norm(vec(state.fields.gradients[e])));
            worstFlux = std::max(worstFlux, state.fields.heatFluxMagnitudes[e].si());
        }
        heated = std::max(heated, state.temperatures[0].si());
    }
    CHECK(heated > 360.0);      // 69 K of rise: it is heating
    CHECK(worstGradient <= 1e-6);
    CHECK(worstFlux <= 1e-4);
    WARN(std::format("insulated block, 60 W: hottest node {:.4f} K at t = 100 s (dT/dt != 0), max |grad T| {:.2e} K/m, max |q| {:.2e} W/m2", heated,
                     worstGradient, worstFlux));
}

TEST_CASE("ThermalPost_ConvectionIsNotPartOfTheConductiveFlux", "[thermal][post][transient][convection]") {
    // A block at a uniform 400 K starts cooling through a film on every face (h = 25 W/(m2 K) to 293.15 K): the film exchanges
    // h (T - Tinf) = 2671 W/m2 at t = 0, and the conductive flux inside the solid is EXACTLY zero because the field is uniform.
    // Neither the film nor the storage rate belongs to q = -k grad(T).
    BoundaryStudy study;
    const double h = 25.0;
    const std::vector<ThermalBoundaryCondition> conditions{
        convectionBc(1, study.startCap(), h, 293.15), convectionBc(2, study.endCap(), h, 293.15),
        convectionBc(3, study.side(0), h, 293.15),    convectionBc(4, study.side(1), h, 293.15),
        convectionBc(5, study.side(2), h, 293.15),    convectionBc(6, study.side(3), h, 293.15)};
    const thermal::UniformInitialTemperature initial{Temperature::fromSi(400.0)};
    const thermal::TransientSettings transient{.timeStep = Time::fromSi(20.0), .endTime = Time::fromSi(200.0),
                                               .output = thermal::TransientOutputPolicy::EveryStep};
    const Chain c = chainFrom(study.document, study.model(), {}, conditions, ThermalAnalysisMode::Transient);
    Result<TransientThermalSolution> solved = thermal::solveTransientThermal(study.document, c.model, c.map, c.system, c.boundary, {},
                                                                              conditions, initial, transient, ThermalSolverSettings{});
    REQUIRE(solved.has_value());
    Result<ThermalPostResult> post = thermal::recoverTransientThermal(study.document, c.model, c.map, *solved, {}, conditions, initial,
                                                                       transient, ThermalSolverSettings{});
    REQUIRE(post.has_value());
    const double film = h * (400.0 - 293.15);
    CHECK_THAT(film, WithinRel(2671.25, 1e-12));
    double atStart = 0.0;
    for (std::size_t e = 0; e < post->elementCount(); ++e) {
        atStart = std::max(atStart, post->states()[0].fields.heatFluxMagnitudes[e].si());
    }
    CHECK(atStart <= 1e-6);            // the solid has no internal gradient yet, whatever the film does
    CHECK(film > 2000.0);
    // Later the block has an internal gradient, and at EVERY state q is exactly -k grad(T): nothing is added to it, neither the
    // film h (T - Tinf) nor a storage term, in any element.
    double later = 0.0;
    double additive = 0.0;
    for (const ThermalPostState& state : post->states()) {
        for (std::size_t e = 0; e < state.fields.gradients.size(); ++e) {
            const Vec3 g = vec(state.fields.gradients[e]);
            const Vec3 q = vec(state.fields.heatFluxes[e]);
            additive = std::max(additive, norm({q[0] + kConductivity * g[0], q[1] + kConductivity * g[1], q[2] + kConductivity * g[2]}));
        }
    }
    for (std::size_t e = 0; e < post->elementCount(); ++e) {
        later = std::max(later, post->finalState().fields.heatFluxMagnitudes[e].si());
    }
    CHECK(later > 0.0);
    CHECK(additive <= 1e-6);
    WARN(std::format("convection is not in q: film exchange {:.2f} W/m2 at t = 0; conductive |q| max {:.2e} W/m2 at t = 0 and {:.2f} W/m2 at 200 s; max |q + k grad T| over all states {:.2e}",
                     film, atStart, later, additive));
}

// ===========================================================================
// Scale and determinism
// ===========================================================================

TEST_CASE("ThermalPost_FineCylinderRecoverySmoke", "[thermal][post][smoke][sparse]") {
    RoundStudy part;
    const std::vector<ThermalBoundaryCondition> conditions{
        temperatureBc(1, FaceName{part.feature, FaceSelector{.role = FaceRole::StartCap}}, 350.0),
        convectionBc(2, part.endCap(), 25.0, 293.15)};
    const Chain c = chainFrom(part.document, part.model(part.fine), {}, conditions);
    const SteadyThermalSolution solved = solveSteady(part.document, c, {}, conditions);
    const ThermalPostResult post = recoverSteady(part.document, c, solved, {}, conditions);
    const std::size_t ntet = c.model.mesh().mesh().tetrahedra().size();
    CHECK(post.finalState().temperatures.size() == solved.dofCount());
    CHECK(post.finalState().fields.gradients.size() == ntet);
    CHECK(post.finalState().fields.heatFluxes.size() == ntet);
    CHECK(post.finalState().fields.heatFluxMagnitudes.size() == ntet);
    for (std::size_t e = 0; e < ntet; ++e) {
        REQUIRE(thermal::isFinite(post.finalState().fields.gradients[e]));
        REQUIRE(thermal::isFinite(post.finalState().fields.heatFluxes[e]));
        REQUIRE(std::isfinite(post.finalState().fields.heatFluxMagnitudes[e].si()));
        CHECK(post.finalState().fields.heatFluxMagnitudes[e].si() >= 0.0);
        CHECK_THAT(post.finalState().fields.heatFluxMagnitudes[e].si(),
                   WithinAbs(norm(vec(post.finalState().fields.heatFluxes[e])), 1e-9));
        CHECK(dot(vec(post.finalState().fields.heatFluxes[e]), vec(post.finalState().fields.gradients[e])) <= 0.0);
    }
    // Repeated recovery is bitwise identical.
    const ThermalPostResult again = recoverSteady(part.document, c, solved, {}, conditions);
    CHECK(again == post);
    std::uint64_t hash = 14695981039346656037ULL;
    const auto mix = [&hash](double value) {
        const auto bits = std::bit_cast<std::uint64_t>(value);
        for (int i = 0; i < 8; ++i) {
            hash ^= (bits >> (8 * i)) & 0xFFU;
            hash *= 1099511628211ULL;
        }
    };
    for (const Temperature t : post.finalState().temperatures) {
        mix(t.si());
    }
    for (std::size_t e = 0; e < ntet; ++e) {
        for (const double v : vec(post.finalState().fields.heatFluxes[e])) {
            mix(v);
        }
        mix(post.finalState().fields.heatFluxMagnitudes[e].si());
    }
    WARN(std::format("real fine cylinder recovery: Ndof {}, Ntet {}, temperature/gradient/flux/magnitude counts {}/{}/{}/{}, all finite",
                     solved.dofCount(), ntet, post.finalState().temperatures.size(), post.finalState().fields.gradients.size(),
                     post.finalState().fields.heatFluxes.size(), post.finalState().fields.heatFluxMagnitudes.size()));
    WARN(std::format("bit fingerprint, real fine cylinder recovery (350 K cap + convection): {:016x}", hash));
}

TEST_CASE("ThermalPost_RepeatedBlockRecoveryIsDeterministic", "[thermal][post][determinism]") {
    BoundaryStudy study;
    const std::vector<ThermalLoad> loads{generationLoad(1, 1.0e5)};
    const std::vector<ThermalBoundaryCondition> conditions{temperatureBc(1, study.startCap(), 400.0),
                                                            convectionBc(2, study.endCap(), 25.0, 293.15)};
    const Chain c = chainFrom(study.document, study.model(), loads, conditions);
    const SteadyThermalSolution solved = solveSteady(study.document, c, loads, conditions);
    const ThermalPostResult first = recoverSteady(study.document, c, solved, loads, conditions);
    int differences = 0;
    for (int i = 0; i < 20; ++i) {
        if (!(recoverSteady(study.document, c, solved, loads, conditions) == first)) {
            ++differences;
        }
    }
    CHECK(differences == 0);
    WARN(std::format("determinism: 20 repeated block recoveries, {} bitwise differences", differences));
}
