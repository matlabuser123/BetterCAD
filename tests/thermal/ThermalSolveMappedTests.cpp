// P18-SOLVE-001: the steady solve against REAL P16 meshes and real documents.
//
// These are the AUTHORITY tests: a solve takes an assembled system, the prepared
// boundary and the canonical loads and conditions, and it must refuse to mix
// generations even when every dimension agrees; it must hold the prescribed
// temperatures exactly; it must reproduce the one-dimensional conduction problems
// whose answers are closed-form; and a failed re-solve must never make an old
// result current.
//
// The arithmetic of the reduction and the refusals of the algebra are checked on
// hand systems in ThermalSolveTests.cpp. Here a 40 x 30 x 20 mm steel block (k = 45
// W/(m K), extruded along z) is meshed by P16, assembled by P18-ASSEMBLY and solved.
// Linear tetrahedra reproduce an affine temperature field exactly, so every nodal
// value below is compared with a CLOSED-FORM expectation, to 1e-6 K or better.

#include "ThermalSolveTestSupport.hpp"

#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <bit>
#include <format>

using namespace sotest;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using thermal::PreparedThermalBoundary;
using thermal::PreparedThermalLoads;
using thermal::SolutionStaleReason;
using thermal::SurfaceHeatFluxLoad;
using thermal::ThermalAnalysisMode;
using thermal::ThermalLoad;
using thermal::VolumetricHeatSourceLoad;

namespace {

constexpr double kLength = 0.020;          // the extrusion depth, along z
constexpr double kConductivity = 45.0;     // W/(m K)
constexpr double kCapAreaSi = 0.040 * 0.030;

[[nodiscard]] ThermalLoad generationLoad(std::uint64_t id, double generation) {
    return ThermalLoad{ThermalLoadId::fromValue(id),
                       VolumetricHeatSourceLoad{.generation = VolumetricHeatSource::fromSi(generation)}};
}
[[nodiscard]] ThermalLoad fluxLoad(std::uint64_t id, const FaceName& face, double flux) {
    return ThermalLoad{ThermalLoadId::fromValue(id), SurfaceHeatFluxLoad{.face = face, .flux = HeatFlux::fromSi(flux)}};
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
                            const std::vector<ThermalBoundaryCondition>& conditions) {
    ThermalModel model = study.model();
    ThermalDofMap map = dofMapOf(model);
    Result<PreparedThermalLoads> preparedLoads = thermal::prepareThermalLoads(model, map, loads);
    INFO((preparedLoads.has_value() ? std::string{} : preparedLoads.error().message));
    REQUIRE(preparedLoads.has_value());
    Result<PreparedThermalBoundary> preparedBoundary = thermal::prepareThermalBoundary(model, map, conditions);
    INFO((preparedBoundary.has_value() ? std::string{} : preparedBoundary.error().message));
    REQUIRE(preparedBoundary.has_value());
    Result<ThermalSystem> system = thermal::assembleThermalSystem(study.document, model, ThermalAnalysisMode::Steady, map,
                                                                  *preparedLoads, loads, *preparedBoundary, conditions);
    INFO((system.has_value() ? std::string{} : system.error().message));
    REQUIRE(system.has_value());
    return Chain{std::move(model), std::move(map), std::move(*preparedLoads), std::move(*preparedBoundary), std::move(*system)};
}

[[nodiscard]] Result<SteadyThermalSolution> solveOn(const BoundaryStudy& study, const Chain& c,
                                                    const std::vector<ThermalLoad>& loads,
                                                    const std::vector<ThermalBoundaryCondition>& conditions,
                                                    const ThermalSolverSettings& settings = {}) {
    return thermal::solveSteadyThermal(study.document, c.model, c.map, c.system, c.boundary, loads, conditions, settings);
}

[[nodiscard]] SteadyThermalSolution mustSolveOn(const BoundaryStudy& study, const Chain& c, const std::vector<ThermalLoad>& loads,
                                                const std::vector<ThermalBoundaryCondition>& conditions,
                                                const ThermalSolverSettings& settings = {}) {
    Result<SteadyThermalSolution> solved = solveOn(study, c, loads, conditions, settings);
    INFO((solved.has_value() ? std::string{} : solved.error().message));
    REQUIRE(solved.has_value());
    return std::move(*solved);
}

[[nodiscard]] double zOf(const meshing::Node& node) { return node.position.z.si(); }

} // namespace

// ===========================================================================
// The block through the whole chain
// ===========================================================================

TEST_CASE("ThermalSolve_BlockLinearProfileFromTwoPrescribedFaces", "[thermal][solve][block][affine]") {
    // 400 K on the start cap (z = 0), 300 K on the end cap (z = 20 mm), no load, adiabatic sides:
    // T(z) = 400 - 100 z / L at every node. Closed form; nothing is read from the solver.
    BoundaryStudy study;
    const std::vector<ThermalBoundaryCondition> conditions{temperatureBc(1, study.startCap(), 400.0),
                                                            temperatureBc(2, study.endCap(), 300.0)};
    const Chain c = chainOn(study, {}, conditions);
    const SteadyThermalSolution solved = mustSolveOn(study, c, {}, conditions);

    double worst = 0.0;
    for (const meshing::Node& node : c.model.mesh().mesh().nodes()) {
        const ThermalDofIndex dof = *c.map.dofOf(node.id);
        const double expected = 400.0 - 100.0 * zOf(node) / kLength;
        worst = std::max(worst, std::abs(solved.temperatures()[static_cast<std::size_t>(dof.value())].si() - expected));
    }
    CHECK(worst <= 1e-7);
    CHECK(solved.dofCount() == c.map.dofCount());
    CHECK(solved.freeEquations() + solved.constrainedCount() == solved.dofCount());
    CHECK(solved.constrainedCount() == c.boundary.constraints().size());
    CHECK(solved.residual().normalized <= 1e-9);
    // The prescribed unknowns hold their prescribed value EXACTLY.
    for (const thermal::ThermalTemperatureConstraint& constraint : c.boundary.constraints().constrained()) {
        CHECK(solved.temperatures()[static_cast<std::size_t>(constraint.dof.value())].si() == constraint.temperature.si());
    }
    // Nodal lookup through the numbering.
    for (const meshing::Node& node : c.model.mesh().mesh().nodes()) {
        const auto t = solved.temperatureOf(c.map, node.id);
        REQUIRE(t.has_value());
        CHECK_THAT(t->si(), WithinAbs(400.0 - 100.0 * zOf(node) / kLength, 1e-7));
    }
    // The heat the two faces exchange, read off the retained full residual, is k A dT / L = 270 W:
    // NOT qualified here (reactions are a later milestone), shown as a plausibility measurement.
    double outflow = 0.0;
    for (const thermal::ThermalTemperatureConstraint& constraint : c.boundary.constraints().constrained()) {
        if (constraint.temperature.si() == 300.0) {
            outflow += solved.fullResidual()[static_cast<std::size_t>(constraint.dof.value())];
        }
    }
    WARN(std::format("block, 400 K -> 300 K over 20 mm: {} unknowns ({} free), max |T - exact| = {:.2e} K, normalized residual "
                     "{:.2e}, heat through the cold face {:.6g} W (k A dT / L = {:.6g} W; not a qualified quantity here)",
                     solved.dofCount(), solved.freeEquations(), worst, solved.residual().normalized, outflow,
                     kConductivity * kCapAreaSi * 100.0 / kLength));
}

TEST_CASE("ThermalSolve_BlockDirichletInSeriesWithConvection", "[thermal][solve][block][robin]") {
    // 400 K on the start cap and a convection face on the end cap (h = 25, T_inf = 293.15 K). One-dimensional
    // series resistance: q = (400 - 293.15) / (L/k + 1/h), T(z) = 400 - q z / k.
    BoundaryStudy study;
    const double h = 25.0;
    const double ambient = 293.15;
    const std::vector<ThermalBoundaryCondition> conditions{temperatureBc(1, study.startCap(), 400.0),
                                                            convectionBc(2, study.endCap(), h, ambient)};
    const Chain c = chainOn(study, {}, conditions);
    const SteadyThermalSolution solved = mustSolveOn(study, c, {}, conditions);
    const double flux = (400.0 - ambient) / (kLength / kConductivity + 1.0 / h);
    double worst = 0.0;
    for (const meshing::Node& node : c.model.mesh().mesh().nodes()) {
        const double expected = 400.0 - flux * zOf(node) / kConductivity;
        const double actual = solved.temperatures()[static_cast<std::size_t>(c.map.dofOf(node.id)->value())].si();
        worst = std::max(worst, std::abs(actual - expected));
    }
    CHECK(worst <= 1e-7);
    // The convective face sits just below 400 K because the block is a far better conductor than the film.
    const double faceTemperature = 400.0 - flux * kLength / kConductivity;
    CHECK(faceTemperature > 398.0);
    CHECK(faceTemperature < 399.0);
    CHECK(solved.anchoring().regions == 1);
    CHECK(solved.anchoring().unanchored == 0);
    WARN(std::format("block, Dirichlet 400 K in series with convection: q = {:.6g} W/m2, end face expected {:.9g} K, max error {:.2e} K",
                     flux, faceTemperature, worst));
}

TEST_CASE("ThermalSolve_BlockConvectionOnlyNoDirichlet", "[thermal][solve][block][robin]") {
    // NOTHING prescribed. 60 W of generation leaves through a convection face; the model is well posed because the
    // film anchors it. Checked by the energy the face carries away, from the solved temperatures and the prepared
    // facets (a validation of the solution, not the balance milestone).
    BoundaryStudy study;
    const std::vector<ThermalLoad> loads{generationLoad(1, 2.5e6)};
    const std::vector<ThermalBoundaryCondition> conditions{convectionBc(1, study.endCap(), 25.0, 293.15)};
    const Chain c = chainOn(study, loads, conditions);
    REQUIRE(c.boundary.constraints().isEmpty());
    const SteadyThermalSolution solved = mustSolveOn(study, c, loads, conditions);
    CHECK(solved.constrainedCount() == 0);
    CHECK(solved.freeEquations() == solved.dofCount());
    CHECK(solved.anchoring().regions == 1);
    CHECK(solved.anchoring().anchored == 1);

    double leaving = 0.0;
    for (const thermal::PreparedConvectionBoundary& convection : c.boundary.convections()) {
        for (const thermal::ConvectionFacet& facet : convection.facets) {
            for (std::size_t a = 0; a < 3; ++a) {
                for (std::size_t b = 0; b < 3; ++b) {
                    leaving += facet.term(a, b).si() * solved.temperatures()[static_cast<std::size_t>(facet.dofs[b].value())].si();
                }
                leaving -= facet.term.source(a).si();
            }
        }
    }
    const double generated = 2.5e6 * 0.040 * 0.030 * kLength;
    CHECK_THAT(leaving, WithinRel(generated, 1e-8));
    CHECK_THAT(c.loads.volumetricGenerationRate().si(), WithinRel(generated, 1e-12));
    // The hottest point is farthest from the film and above the ambient.
    double hottest = 0.0;
    for (const Temperature t : solved.temperatures()) {
        hottest = std::max(hottest, t.si());
    }
    CHECK(hottest > 293.15);
    WARN(std::format("block, generation 60 W, convection only (no Dirichlet): heat leaving through the film {:.9g} W vs generated {:.9g} W; "
                     "hottest node {:.4f} K",
                     leaving, generated, hottest));
}

TEST_CASE("ThermalSolve_BlockWithoutAnyDatumIsRefused", "[thermal][solve][block][singular]") {
    // Generation and a surface flux and NO temperature and NO convection: the constant temperature is in the null
    // space. The solve refuses it; it does not pick a level.
    BoundaryStudy study;
    const std::vector<ThermalLoad> loads{generationLoad(1, 1.0e5), fluxLoad(2, study.startCap(), -100.0)};
    const Chain c = chainOn(study, loads, {});
    const std::optional<thermal::SteadySolveFailure> refused =
        thermal::steadySolveFailure(study.document, c.model, c.map, c.system, c.boundary, loads, {}, ThermalSolverSettings{});
    REQUIRE(refused.has_value());
    CHECK(refused->problem == SteadySolveProblem::MissingTemperatureDatum);
    CHECK_FALSE(solveOn(study, c, loads, {}).has_value());
    // A convection face whose coefficient is zero is not a datum either (assembled through the seam: the canonical
    // condition refuses h = 0).
    WARN("block, loads only, no prescribed temperature, no convection: missing_temperature_datum, accepted result NO");
}

TEST_CASE("ThermalSolve_FineCylinderSparseSolveSmoke", "[thermal][solve][smoke][sparse]") {
    // The fine mesh of a 6 mm radius cylinder, a real P16 mesh with a curved wall. 350 K on one cap, convection on the
    // other, no generation: the maximum principle bounds every temperature between the ambient and 350 K.
    RoundStudy part;
    const ThermalModel model = part.model(part.fine);
    const ThermalDofMap map = dofMapOf(model);
    const std::vector<ThermalBoundaryCondition> conditions{temperatureBc(1, FaceName{part.feature, FaceSelector{.role = FaceRole::StartCap}}, 350.0),
                                                           convectionBc(2, part.endCap(), 25.0, 293.15)};
    auto preparedLoads = thermal::prepareThermalLoads(model, map, std::span<const ThermalLoad>{});
    auto preparedBoundary = thermal::prepareThermalBoundary(model, map, conditions);
    REQUIRE((preparedLoads.has_value() && preparedBoundary.has_value()));
    auto system = thermal::assembleThermalSystem(part.document, model, ThermalAnalysisMode::Steady, map, *preparedLoads,
                                                 std::span<const ThermalLoad>{}, *preparedBoundary, conditions);
    REQUIRE(system.has_value());
    const auto solved = thermal::solveSteadyThermal(part.document, model, map, *system, *preparedBoundary,
                                                    std::span<const ThermalLoad>{}, conditions, ThermalSolverSettings{});
    INFO((solved.has_value() ? std::string{} : solved.error().message));
    REQUIRE(solved.has_value());
    double lowest = 1.0e9;
    double highest = 0.0;
    for (const Temperature t : solved->temperatures()) {
        REQUIRE(std::isfinite(t.si()));
        lowest = std::min(lowest, t.si());
        highest = std::max(highest, t.si());
    }
    CHECK(lowest >= 293.15 - 1e-6);
    CHECK(highest <= 350.0 + 1e-6);
    CHECK(solved->residual().normalized <= 1e-9);
    CHECK(solved->freeSystemNonZeros() < solved->freeEquations() * solved->freeEquations() / 10);
    WARN(std::format("real fine cylinder: Ndof {}, Nfree {}, Nconstrained {}, nnz(K) {}, nnz(Kff) {}, Nfree^2 {}, pivot ratio {:.3e}, "
                     "normalized residual {:.2e}, T in [{:.6f}, {:.6f}] K",
                     solved->dofCount(), solved->freeEquations(), solved->constrainedCount(), system->conductance().nonZeros(),
                     solved->freeSystemNonZeros(), solved->freeEquations() * solved->freeEquations(), solved->pivotRatio(),
                     solved->residual().normalized, lowest, highest));
}

// ===========================================================================
// Provenance and currentness
// ===========================================================================

TEST_CASE("ThermalSolve_SolutionRecordsExactlyItsInputs", "[thermal][solve][currentness]") {
    BoundaryStudy study;
    const std::vector<ThermalLoad> loads{generationLoad(1, 1.0e5)};
    const std::vector<ThermalBoundaryCondition> conditions{temperatureBc(1, study.startCap(), 400.0),
                                                            convectionBc(2, study.endCap(), 25.0, 293.15)};
    const Chain c = chainOn(study, loads, conditions);
    const ThermalSolverSettings settings{.relativeResidualTolerance = 1.0e-10, .pivotFloor = 1.0e-11};
    const SteadyThermalSolution solved = mustSolveOn(study, c, loads, conditions, settings);
    const thermal::SteadySolutionSource& source = solved.source();
    // Every recorded field is compared with the INPUT, not with a second call of the same function.
    CHECK(source.system == c.system.source());
    CHECK(source.system.mode == ThermalAnalysisMode::Steady);
    CHECK(source.system.loads == loads);
    REQUIRE(source.temperatures.size() == 1);
    CHECK(source.temperatures[0] == conditions[0]);  // ONLY the prescribed temperature; the convection is in the system
    CHECK(source.temperatures[0].prescribedTemperature() != nullptr);
    CHECK(source.settings == settings);
    CHECK(source.settings != ThermalSolverSettings{});
    const auto current = thermal::currentSteadySolutionSource(study.document, c.model, loads, conditions, settings);
    REQUIRE(current.has_value());
    CHECK(source == *current);
    CHECK(thermal::isCurrent(solved, study.document, c.model, loads, conditions, settings));
    CHECK(solved.describes(c.map));
}

TEST_CASE("ThermalSolve_EditsStaleExactlyWhatTheyChange", "[thermal][solve][currentness]") {
    BoundaryStudy study;
    const std::vector<ThermalLoad> loads{generationLoad(1, 1.0e5)};
    const std::vector<ThermalBoundaryCondition> conditions{temperatureBc(1, study.startCap(), 400.0),
                                                            convectionBc(2, study.endCap(), 25.0, 293.15)};
    const Chain c = chainOn(study, loads, conditions);
    const SteadyThermalSolution solved = mustSolveOn(study, c, loads, conditions);
    const auto reasonsFor = [&](const std::vector<ThermalLoad>& l, const std::vector<ThermalBoundaryCondition>& b,
                                const ThermalSolverSettings& s) {
        const auto now = thermal::currentSteadySolutionSource(study.document, c.model, l, b, s);
        REQUIRE(now.has_value());
        return thermal::staleReasons(solved.source(), *now);
    };
    using Reasons = std::vector<SolutionStaleReason>;
    SECTION("a prescribed-temperature edit stales the SOLUTION, not the assembled system") {
        std::vector<ThermalBoundaryCondition> edited = conditions;
        edited[0] = temperatureBc(1, study.startCap(), 410.0);
        CHECK(reasonsFor(loads, edited, ThermalSolverSettings{}) == Reasons{SolutionStaleReason::PrescribedTemperatures});
        CHECK_FALSE(thermal::isCurrent(solved, study.document, c.model, loads, edited, ThermalSolverSettings{}));
        // The assembled system is untouched by it: K, C and Q do not contain T_bar.
        CHECK(thermal::isCurrent(c.system, study.document, c.model, ThermalAnalysisMode::Steady, loads, edited));
    }
    SECTION("a convection edit stales both") {
        std::vector<ThermalBoundaryCondition> edited = conditions;
        edited[1] = convectionBc(2, study.endCap(), 30.0, 293.15);
        CHECK(reasonsFor(loads, edited, ThermalSolverSettings{}) == Reasons{SolutionStaleReason::Convections});
        CHECK_FALSE(thermal::isCurrent(c.system, study.document, c.model, ThermalAnalysisMode::Steady, loads, edited));
    }
    SECTION("a load edit stales both") {
        const std::vector<ThermalLoad> edited{generationLoad(1, 2.0e5)};
        CHECK(reasonsFor(edited, conditions, ThermalSolverSettings{}) == Reasons{SolutionStaleReason::Loads});
        CHECK_FALSE(thermal::isCurrent(c.system, study.document, c.model, ThermalAnalysisMode::Steady, edited, conditions));
    }
    SECTION("a solver-setting edit stales the solution only") {
        const ThermalSolverSettings edited{.relativeResidualTolerance = 1.0e-7, .pivotFloor = 1.0e-12};
        CHECK(reasonsFor(loads, conditions, edited) == Reasons{SolutionStaleReason::Settings});
        CHECK(thermal::isCurrent(c.system, study.document, c.model, ThermalAnalysisMode::Steady, loads, conditions));
        // And the same system solves again under the new settings.
        CHECK(solveOn(study, c, loads, conditions, edited).has_value());
    }
    SECTION("a material edit stales both") {
        materials::ThermalProperties properties = features::findMaterial(study.document, study.material)->definition().thermal;
        properties.thermalConductivity = materials::MaterialProperty<ThermalConductivity>::known(50_W_per_m_K);
        REQUIRE(features::setMaterialThermal(study.document, study.material, properties).has_value());
        CHECK_FALSE(thermal::isCurrent(solved, study.document, c.model, loads, conditions, ThermalSolverSettings{}));
        const auto refused = solveOn(study, c, loads, conditions);
        REQUIRE_FALSE(refused.has_value());
        CHECK(thermal::steadySolveFailure(study.document, c.model, c.map, c.system, c.boundary, loads, conditions, ThermalSolverSettings{})->problem ==
              SteadySolveProblem::StaleSystem);
    }
    SECTION("nothing at all leaves it current") {
        CHECK(reasonsFor(loads, conditions, ThermalSolverSettings{}).empty());
    }
}

TEST_CASE("ThermalSolve_RejectsStaleBoundaryAfterATemperatureEdit", "[thermal][solve][currentness]") {
    BoundaryStudy study;
    const std::vector<ThermalBoundaryCondition> conditions{temperatureBc(1, study.startCap(), 400.0),
                                                            temperatureBc(2, study.endCap(), 300.0)};
    const Chain c = chainOn(study, {}, conditions);
    const SteadyThermalSolution first = mustSolveOn(study, c, {}, conditions);

    // The boundary value moves. The OLD prepared boundary is refused although every dimension and every id agree.
    const std::vector<ThermalBoundaryCondition> edited{temperatureBc(1, study.startCap(), 450.0),
                                                       temperatureBc(2, study.endCap(), 300.0)};
    const auto refused = thermal::steadySolveFailure(study.document, c.model, c.map, c.system, c.boundary, {}, edited, ThermalSolverSettings{});
    REQUIRE(refused.has_value());
    CHECK(refused->problem == SteadySolveProblem::StaleBoundary);
    CHECK_THAT(refused->error.message, ContainsSubstring("conditions"));
    // The assembled system is still current (K and Q do not contain it), so ONLY the boundary is re-prepared.
    const PreparedThermalBoundary fresh = prepareOn(c.model, c.map, edited);
    const auto second = thermal::solveSteadyThermal(study.document, c.model, c.map, c.system, fresh, {}, edited, ThermalSolverSettings{});
    INFO((second.has_value() ? std::string{} : second.error().message));
    REQUIRE(second.has_value());
    CHECK_FALSE(thermal::isCurrent(first, study.document, c.model, {}, edited, ThermalSolverSettings{}));
    CHECK(thermal::isCurrent(*second, study.document, c.model, {}, edited, ThermalSolverSettings{}));
    // 450 K -> 300 K over 20 mm: the profile moved.
    for (const meshing::Node& node : c.model.mesh().mesh().nodes()) {
        CHECK_THAT(second->temperatures()[static_cast<std::size_t>(c.map.dofOf(node.id)->value())].si(),
                   WithinAbs(450.0 - 150.0 * zOf(node) / kLength, 1e-7));
    }
}

TEST_CASE("ThermalSolve_RemeshStalesTheSolutionAndRefusesTheOldSystem", "[thermal][solve][currentness]") {
    BoundaryStudy study;
    const std::vector<ThermalBoundaryCondition> conditions{temperatureBc(1, study.startCap(), 400.0),
                                                            temperatureBc(2, study.endCap(), 300.0)};
    // A ThermalModel BORROWS the mesher's mesh and must not outlive a remesh: everything needed from generation 1 is
    // captured by VALUE first.
    std::optional<ThermalSystem> oldSystem;
    std::optional<SteadyThermalSolution> oldSolution;
    std::optional<ThermalDofMap> oldMap;
    std::optional<PreparedThermalBoundary> oldBoundary;
    std::optional<ThermalConstraintMap> oldConstraints;
    meshing::MeshStamp stamp1{};
    std::size_t nodes1 = 0;
    {
        const Chain c = chainOn(study, {}, conditions);
        oldSolution = mustSolveOn(study, c, {}, conditions);
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
    CHECK_FALSE(thermal::isCurrent(*oldSolution, study.document, c2.model, {}, conditions, ThermalSolverSettings{}));
    const auto now = thermal::currentSteadySolutionSource(study.document, c2.model, {}, conditions, ThermalSolverSettings{});
    REQUIRE(now.has_value());
    const auto reasons = thermal::staleReasons(oldSolution->source(), *now);
    CHECK(std::ranges::find(reasons, SolutionStaleReason::Mesh) != reasons.end());

    // The OLD assembled system, against the new model and a fresh boundary: refused before any arithmetic.
    const auto refused = thermal::steadySolveFailure(study.document, c2.model, c2.map, *oldSystem, c2.boundary, {}, conditions,
                                                      ThermalSolverSettings{});
    REQUIRE(refused.has_value());
    CHECK(refused->problem == SteadySolveProblem::StaleSystem);
    CHECK_THAT(refused->error.message, ContainsSubstring("mesh"));
    // The OLD constraint map against the NEW system: the stamps differ although the counts and ids agree.
    const auto staleConstraints = thermal::solveSteadyThermalSystem(c2.system, c2.map, *oldConstraints, ThermalSolverSettings{});
    REQUIRE_FALSE(staleConstraints.has_value());
    CHECK(staleConstraints.error().problem == SteadySolveProblem::ConstraintMismatch);
    // The OLD numbering against the new system.
    const auto staleMap = thermal::solveSteadyThermalSystem(c2.system, *oldMap, c2.boundary.constraints(), ThermalSolverSettings{});
    REQUIRE_FALSE(staleMap.has_value());
    CHECK(staleMap.error().problem == SteadySolveProblem::DofMapMismatch);
    // The OLD prepared boundary with the new system and numbering: its numbering is not this one's.
    const auto staleBoundary = thermal::steadySolveFailure(study.document, c2.model, c2.map, c2.system, *oldBoundary, {}, conditions,
                                                            ThermalSolverSettings{});
    REQUIRE(staleBoundary.has_value());
    CHECK(staleBoundary->problem == SteadySolveProblem::DofMapMismatch);
    // Everything fresh: solves, and is current.
    const SteadyThermalSolution fresh = mustSolveOn(study, c2, {}, conditions);
    CHECK(thermal::isCurrent(fresh, study.document, c2.model, {}, conditions, ThermalSolverSettings{}));
    WARN(std::format("remesh: {} nodes in both generations; the old {}-unknown solution is stale (mesh), the old system, boundary, numbering "
                     "and constraints are each refused, the new solution is current",
                     nodes1, oldSolution->dofCount()));
}

// ===========================================================================
// The result lifecycle (P18-DATA-001's slot) around a failed solve
// ===========================================================================

namespace {

/// A held result for the lifecycle tests. A ThermalResult needs gradient, flux and balance channels that belong to
/// later milestones; here they are zero, and ONLY the source and the temperatures matter.
[[nodiscard]] thermal::ThermalResult heldResultOf(const BoundaryStudy& study, const SteadyThermalSolution& solved) {
    const meshing::VolumeMesh& volume = study.volume();
    std::vector<Temperature> temperatures(solved.temperatures().begin(), solved.temperatures().end());
    Result<thermal::ThermalResult> held = thermal::ThermalResult::create(
        study.currentSource(), volume, std::move(temperatures),
        std::vector<thermal::TemperatureGradient3>(volume.mesh().tetrahedra().size()),
        std::vector<thermal::HeatFlux3>(volume.mesh().tetrahedra().size()),
        thermal::HeatBalance{}, std::nullopt);
    INFO((held.has_value() ? std::string{} : held.error().message));
    REQUIRE(held.has_value());
    return std::move(*held);
}

} // namespace

TEST_CASE("ThermalSolve_FailedResolveDoesNotCreateACurrentResult", "[thermal][solve][lifecycle]") {
    BoundaryStudy study;
    const std::vector<ThermalLoad> loads{generationLoad(1, 1.0e5)};
    const std::vector<ThermalBoundaryCondition> anchored{temperatureBc(1, study.startCap(), 400.0)};
    REQUIRE(study.setDefinition(ThermalAnalysisDefinition{.mesh = study.control, .loads = loads, .boundaryConditions = anchored}).has_value());

    thermal::ThermalResultSlot slot;
    SECTION("R1 current, then the model is edited into a singular one") {
        {
            const Chain c = chainOn(study, loads, anchored);
            const SteadyThermalSolution r1 = mustSolveOn(study, c, loads, anchored);
            slot.recordSuccess(heldResultOf(study, r1));
        }
        CHECK(thermal::analysisState(study.document, study.regenerator, study.mesher, study.analysis, slot.result(), slot.failure()) ==
              thermal::AnalysisState::SolvedCurrent);

        // The datum is removed: a pure-Neumann model.
        const std::vector<ThermalBoundaryCondition> none;
        REQUIRE(study.setDefinition(ThermalAnalysisDefinition{.mesh = study.control, .loads = loads, .boundaryConditions = none}).has_value());
        CHECK(thermal::analysisState(study.document, study.regenerator, study.mesher, study.analysis, slot.result(), slot.failure()) ==
              thermal::AnalysisState::SolvedStale);

        const Chain c2 = chainOn(study, loads, none);
        const auto failed = solveOn(study, c2, loads, none);
        REQUIRE_FALSE(failed.has_value());
        slot.recordFailure(failed.error());

        // R1 is still held, still STALE, and the latest attempt FAILED. No current result exists.
        REQUIRE(slot.result() != nullptr);
        CHECK(thermal::resultCurrency(slot.result(), study.currentSource()) == thermal::ResultCurrency::Stale);
        CHECK(thermal::analysisState(study.document, study.regenerator, study.mesher, study.analysis, slot.result(), slot.failure()) ==
              thermal::AnalysisState::SolveFailed);
        CHECK_FALSE(thermal::describesTheModel(thermal::resultCurrency(slot.result(), study.currentSource())));
        WARN("lifecycle: R1 current -> edit to a datum-less model -> R1 stale -> solve fails (missing_temperature_datum) -> R1 STALE, "
             "latest attempt FAILED, new current result NONE");
    }
    SECTION("no previous result: a failed first solve leaves nothing") {
        const std::vector<ThermalBoundaryCondition> none;
        REQUIRE(study.setDefinition(ThermalAnalysisDefinition{.mesh = study.control, .loads = loads, .boundaryConditions = none}).has_value());
        const Chain c = chainOn(study, loads, none);
        const auto failed = solveOn(study, c, loads, none);
        REQUIRE_FALSE(failed.has_value());
        slot.recordFailure(failed.error());
        CHECK(slot.result() == nullptr);
        CHECK(slot.failure() != nullptr);
        CHECK(thermal::analysisState(study.document, study.regenerator, study.mesher, study.analysis, slot.result(), slot.failure()) ==
              thermal::AnalysisState::SolveFailed);
    }
}

TEST_CASE("ThermalSolve_SolveDoesNotMutateTheModelOrTheSystem", "[thermal][solve][purity]") {
    BoundaryStudy study;
    const std::vector<ThermalLoad> loads{generationLoad(1, 1.0e5)};
    const std::vector<ThermalBoundaryCondition> conditions{temperatureBc(1, study.startCap(), 400.0),
                                                            convectionBc(2, study.endCap(), 25.0, 293.15)};
    REQUIRE(study.setDefinition(ThermalAnalysisDefinition{.mesh = study.control, .loads = loads, .boundaryConditions = conditions}).has_value());
    const Chain c = chainOn(study, loads, conditions);
    const ThermalSystem systemBefore = c.system;
    const PreparedThermalBoundary boundaryBefore = c.boundary;
    const PreparedThermalLoads loadsBefore = c.loads;
    const ThermalAnalysisDefinition definitionBefore = study.definition();
    const std::uint64_t revision = study.document.revision();
    const meshing::MeshStamp stamp = c.model.mesh().mesh().stamp();
    for (int i = 0; i < 3; ++i) {
        (void)solveOn(study, c, loads, conditions);
    }
    CHECK(c.system == systemBefore);
    CHECK(c.boundary == boundaryBefore);
    CHECK(c.loads == loadsBefore);
    CHECK(study.definition() == definitionBefore);
    CHECK(study.document.revision() == revision);
    CHECK(c.model.mesh().mesh().stamp() == stamp);
    CHECK(study.mesher.currency(study.document, study.control) == meshing::MeshCurrency::Current);
}

TEST_CASE("ThermalSolve_RepeatedBlockSolveIsDeterministic", "[thermal][solve][determinism]") {
    BoundaryStudy study;
    const std::vector<ThermalLoad> loads{generationLoad(1, 1.0e5)};
    const std::vector<ThermalBoundaryCondition> conditions{temperatureBc(1, study.startCap(), 400.0),
                                                            convectionBc(2, study.endCap(), 25.0, 293.15)};
    const Chain c = chainOn(study, loads, conditions);
    const SteadyThermalSolution first = mustSolveOn(study, c, loads, conditions);
    int differences = 0;
    for (int i = 0; i < 30; ++i) {
        if (!(mustSolveOn(study, c, loads, conditions) == first)) {
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
    for (const Temperature t : first.temperatures()) {
        mix(t.si());
    }
    for (const double r : first.fullResidual()) {
        mix(r);
    }
    mix(first.residual().normalized);
    mix(first.pivotRatio());
    WARN(std::format("determinism: 30 repeated block solves, {} bitwise differences in T, residuals, pivot ratio or provenance", differences));
    WARN(std::format("bit fingerprint, real block solve (Dirichlet + convection + generation, {} unknowns): {:016x}", first.dofCount(), hash));
}
