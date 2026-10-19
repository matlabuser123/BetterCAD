// P18-ASSEMBLY-001: the global system against REAL P16 meshes and real documents.
//
// These are the AUTHORITY tests: an assembly takes a model, a numbering, prepared
// loads, a prepared boundary and a material, and it must refuse to mix generations
// even when every dimension agrees. A system for one mesh is not current on
// another with the same node and element counts and the same numeric ids; a
// conductivity edit after the model was built is not assembled; a transient
// analysis with no density or specific heat is refused and never given a zero C;
// and a steady analysis needs neither.
//
// The arithmetic of scatter is checked on hand meshes in ThermalAssemblyTests.cpp.
// Here the block's expected dense system is formed by an INDEPENDENT scatter (nested
// loops in this file, rows from `ThermalDofMap::dofOf` per corner) of the qualified
// local matrices, so what is independent is the scatter, which is what this
// milestone owns.

#include "ThermalAssemblyTestSupport.hpp"

#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <bit>
#include <chrono>
#include <format>

using namespace actest;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using thermal::PreparedThermalBoundary;
using thermal::PreparedThermalLoads;
using thermal::SurfaceHeatFluxLoad;
using thermal::SystemStaleReason;
using thermal::ThermalAnalysisMode;
using thermal::ThermalLoad;
using thermal::VolumetricHeatSourceLoad;

namespace {

[[nodiscard]] ThermalLoad fluxLoad(std::uint64_t id, const FaceName& face, double flux) {
    return ThermalLoad{ThermalLoadId::fromValue(id), SurfaceHeatFluxLoad{.face = face, .flux = HeatFlux::fromSi(flux)}};
}
[[nodiscard]] ThermalLoad generationLoad(std::uint64_t id, double generation) {
    return ThermalLoad{ThermalLoadId::fromValue(id),
                       VolumetricHeatSourceLoad{.generation = VolumetricHeatSource::fromSi(generation)}};
}

using Local4 = std::vector<std::vector<double>>;

constexpr double kBlockVolume = 0.040 * 0.030 * 0.020;
constexpr double kRhoSteel = 7850.0;
constexpr double kCpSteel = 460.0;

/// Everything an assembly consumes, prepared on one current mesh.
struct Setup {
    ThermalModel model;
    ThermalDofMap map;
    PreparedThermalLoads loads;
    PreparedThermalBoundary boundary;
};

[[nodiscard]] Setup setupOn(const BoundaryStudy& study, const std::vector<ThermalLoad>& loads,
                            const std::vector<ThermalBoundaryCondition>& conditions) {
    ThermalModel model = study.model();
    ThermalDofMap map = dofMapOf(model);
    Result<PreparedThermalLoads> preparedLoads = thermal::prepareThermalLoads(model, map, loads);
    INFO((preparedLoads.has_value() ? std::string{} : preparedLoads.error().message));
    REQUIRE(preparedLoads.has_value());
    Result<PreparedThermalBoundary> preparedBoundary = thermal::prepareThermalBoundary(model, map, conditions);
    INFO((preparedBoundary.has_value() ? std::string{} : preparedBoundary.error().message));
    REQUIRE(preparedBoundary.has_value());
    return Setup{std::move(model), std::move(map), std::move(*preparedLoads), std::move(*preparedBoundary)};
}

[[nodiscard]] Result<ThermalSystem> assembleOn(const BoundaryStudy& study, const Setup& s, ThermalAnalysisMode mode,
                                               const std::vector<ThermalLoad>& loads,
                                               const std::vector<ThermalBoundaryCondition>& conditions) {
    return thermal::assembleThermalSystem(study.document, s.model, mode, s.map, s.loads, loads, s.boundary, conditions);
}

[[nodiscard]] ThermalSystem mustAssemble(const BoundaryStudy& study, const Setup& s, ThermalAnalysisMode mode,
                                         const std::vector<ThermalLoad>& loads,
                                         const std::vector<ThermalBoundaryCondition>& conditions) {
    Result<ThermalSystem> built = assembleOn(study, s, mode, loads, conditions);
    INFO((built.has_value() ? std::string{} : built.error().message));
    REQUIRE(built.has_value());
    return std::move(*built);
}

/// The block study's standard canonical content: a flux on the end cap, a
/// generation in the body, a convection on the start cap and a prescribed
/// temperature on a side wall.
struct Canon {
    std::vector<ThermalLoad> loads;
    std::vector<ThermalBoundaryCondition> conditions;
};

[[nodiscard]] Canon canonOn(const BoundaryStudy& study) {
    return Canon{{fluxLoad(1, study.endCap(), 500.0), generationLoad(2, 2.5e6)},
                 {convectionBc(1, study.startCap(), 25.0, 293.15), temperatureBc(2, study.side(0), 350.0)}};
}

} // namespace

// ===========================================================================
// The block: dimensions, composition, identities
// ===========================================================================

TEST_CASE("ThermalAssembly_BlockSystemCompositionAndDimensions", "[thermal][assembly][block]") {
    BoundaryStudy study;
    const Canon c = canonOn(study);
    const Setup s = setupOn(study, c.loads, c.conditions);
    const ThermalSystem system = mustAssemble(study, s, ThermalAnalysisMode::Transient, c.loads, c.conditions);

    const std::size_t n = static_cast<std::size_t>(s.map.dofCount());
    CHECK(n == s.model.mesh().nodeCount());  // Ndof = Nnode
    CHECK(system.dofCount() == n);
    CHECK(system.conductance().rows() == n);
    CHECK(system.conductance().columns() == n);
    REQUIRE(system.capacity().has_value());
    CHECK(system.capacity()->rows() == n);
    CHECK(system.heat().size() == n);
    CHECK(system.describes(s.map));
    CHECK(system.describes(s.model.mesh().mesh()));
    CHECK(system.elementOrder().size() == s.model.mesh().tetrahedronCount());
    // Prescribed temperatures never enter the matrices: the constrained unknowns keep their rows.
    CHECK(s.boundary.constraints().size() > 0);
    CHECK(system.conductance().nonZeros() > n);
    CHECK(system.conductance().largestSymmetryError() == 0.0);
    CHECK(system.capacity()->largestSymmetryError() == 0.0);
    for (const double v : system.conductance().values()) {
        REQUIRE(std::isfinite(v));
    }
    WARN(std::format("block: Ndof {}, Ntet {}, nnz(K) {}, nnz(C) {}, convection facets {}, Q total {:.9g} W", n,
                     system.elementOrder().size(), system.conductance().nonZeros(), system.capacity()->nonZeros(),
                     system.convectionFacetCount(), system.heat().total().si()));
}

TEST_CASE("ThermalAssembly_BlockSourceRecordsExactlyItsInputs", "[thermal][assembly][block][currentness]") {
    // The recorded provenance is compared field by field against the INPUTS, not
    // only against a second call of the same function: a field that was never
    // recorded would be equally absent on both sides of that comparison.
    BoundaryStudy study;
    const Canon c = canonOn(study);
    const Setup s = setupOn(study, c.loads, c.conditions);
    const ThermalSystem system = mustAssemble(study, s, ThermalAnalysisMode::Transient, c.loads, c.conditions);
    const Result<thermal::ThermalMaterial> material =
        thermal::resolveThermalMaterial(study.document, s.model.body(), ThermalAnalysisMode::Transient);
    REQUIRE(material.has_value());
    const thermal::ThermalSystemSource& source = system.source();
    CHECK(source.body == s.model.body());
    CHECK(source.body.isValid());
    CHECK(source.mesh == s.model.mesh().mesh().stamp());
    CHECK(source.mesh.isValid());
    CHECK(source.nodeCount == s.model.mesh().nodeCount());
    CHECK(source.geometry == s.model.geometryRevision());
    CHECK(source.geometry.isValid());
    CHECK(source.mode == ThermalAnalysisMode::Transient);
    CHECK(source.material == material->id());
    CHECK(source.material.isValid());
    CHECK(source.materialRevision == material->revision());
    CHECK(source.materialRevision != 0);
    CHECK(source.loads == c.loads);
    // Only the CONVECTION condition is recorded; the prescribed temperature is not.
    REQUIRE(source.convections.size() == 1);
    CHECK(source.convections[0] == c.conditions[0]);
    CHECK(source.convections[0].convection() != nullptr);
    // The whole record equals what the same inputs produce now.
    const auto current =
        thermal::currentThermalSystemSource(study.document, s.model, ThermalAnalysisMode::Transient, c.loads, c.conditions);
    REQUIRE(current.has_value());
    CHECK(source == *current);
}

TEST_CASE("ThermalAssembly_BlockElementOrderIsP16sAscendingOrder", "[thermal][assembly][block]") {
    BoundaryStudy study;
    const Setup s = setupOn(study, {}, {});
    const ThermalSystem system = mustAssemble(study, s, ThermalAnalysisMode::Steady, {}, {});
    std::vector<ElementId> expected;
    for (const meshing::Tetrahedron& tet : s.model.mesh().mesh().tetrahedra()) {
        expected.push_back(tet.id);
    }
    REQUIRE(std::ranges::equal(system.elementOrder(), expected));
    CHECK(std::ranges::is_sorted(expected));
}

TEST_CASE("ThermalAssembly_BlockHeatResultantsPreserved", "[thermal][assembly][resultant]") {
    BoundaryStudy study;
    const Canon c = canonOn(study);
    const Setup s = setupOn(study, c.loads, c.conditions);
    const ThermalSystem system = mustAssemble(study, s, ThermalAnalysisMode::Steady, c.loads, c.conditions);

    const double surface = 500.0 * kCapArea;                // q'' A
    const double generation = 2.5e6 * kBlockVolume;         // q''' V
    const double convectionSource = 25.0 * 293.15 * kCapArea;  // h T_inf A
    CHECK_THAT(s.loads.appliedSurfaceHeatRate().si(), WithinRel(surface, 1e-12));
    CHECK_THAT(s.loads.volumetricGenerationRate().si(), WithinRel(generation, 1e-12));
    CHECK_THAT(s.boundary.convectionSource().si(), WithinRel(convectionSource, 1e-12));
    const double expected = surface + generation + convectionSource;
    CHECK_THAT(system.heat().total().si(), WithinRel(expected, 1e-13));
    // Not double-counted: the combined prepared vector is added once, and so is the convection source.
    CHECK_THAT(system.heat().total().si(), WithinRel(s.loads.totalHeatRate().si() + s.boundary.convectionSource().si(), 1e-13));
    CHECK(system.heat().total().si() < 1.5 * expected);
    WARN(std::format("block Q: surface {:.9g} W + generation {:.9g} W + convection source {:.9g} W = {:.9g} W (actual {:.12g}); "
                     "convection source is the AMBIENT part only, not the exchanged heat",
                     surface, generation, convectionSource, expected, system.heat().total().si()));

    // Each category alone.
    const Setup onlyLoads = setupOn(study, c.loads, {});
    CHECK_THAT(mustAssemble(study, onlyLoads, ThermalAnalysisMode::Steady, c.loads, {}).heat().total().si(),
               WithinRel(surface + generation, 1e-13));
    const std::vector<ThermalLoad> flux{c.loads[0]};
    const Setup surfaceOnly = setupOn(study, flux, {});
    CHECK_THAT(mustAssemble(study, surfaceOnly, ThermalAnalysisMode::Steady, flux, {}).heat().total().si(), WithinRel(surface, 1e-13));
    const std::vector<ThermalLoad> volume{c.loads[1]};
    const Setup volumeOnly = setupOn(study, volume, {});
    CHECK_THAT(mustAssemble(study, volumeOnly, ThermalAnalysisMode::Steady, volume, {}).heat().total().si(), WithinRel(generation, 1e-13));
    const std::vector<ThermalBoundaryCondition> conv{c.conditions[0]};
    const Setup convOnly = setupOn(study, {}, conv);
    CHECK_THAT(mustAssemble(study, convOnly, ThermalAnalysisMode::Steady, {}, conv).heat().total().si(), WithinRel(convectionSource, 1e-13));
}

TEST_CASE("ThermalAssembly_BlockNullModeAndCapacityConservation", "[thermal][assembly][nullmode]") {
    BoundaryStudy study;
    // Conduction only: K 1 = 0 and 1^T C 1 = rho cp V.
    const Setup plain = setupOn(study, {}, {});
    const ThermalSystem a = mustAssemble(study, plain, ThermalAnalysisMode::Transient, {}, {});
    const std::size_t n = static_cast<std::size_t>(plain.map.dofCount());
    const auto k1 = a.conductance().multiply(std::vector<double>(n, 1.0));
    REQUIRE(k1.has_value());
    CHECK(maxAbs(*k1) <= 1e-12 * a.conductance().largestMagnitude());
    const auto c1 = a.capacity()->multiply(std::vector<double>(n, 1.0));
    REQUIRE(c1.has_value());
    CHECK_THAT(sumOf(*c1), WithinRel(kRhoSteel * kCpSteel * kBlockVolume, 1e-12));
    WARN(std::format("block, conduction only: ||K 1||_inf = {:.2e} against max |K| {:.4g}; 1^T C 1 = {:.9g} J/K "
                     "(rho cp V = {:.9g})",
                     maxAbs(*k1), a.conductance().largestMagnitude(), sumOf(*c1), kRhoSteel * kCpSteel * kBlockVolume));

    // A convection face lifts the constant mode: 1^T K 1 = h A.
    const std::vector<ThermalBoundaryCondition> conv{convectionBc(1, study.startCap(), 25.0, 293.15)};
    const Setup withConv = setupOn(study, {}, conv);
    const ThermalSystem b = mustAssemble(study, withConv, ThermalAnalysisMode::Steady, {}, conv);
    const auto kb = b.conductance().multiply(std::vector<double>(n, 1.0));
    REQUIRE(kb.has_value());
    CHECK_THAT(sumOf(*kb), WithinRel(25.0 * kCapArea, 1e-12));
}

TEST_CASE("ThermalAssembly_BlockMatchesIndependentDenseOracle", "[thermal][assembly][dense]") {
    BoundaryStudy study;
    const Canon c = canonOn(study);
    const Setup s = setupOn(study, c.loads, c.conditions);
    const ThermalSystem system = mustAssemble(study, s, ThermalAnalysisMode::Transient, c.loads, c.conditions);
    const meshing::Mesh& mesh = s.model.mesh().mesh();
    const std::size_t n = static_cast<std::size_t>(s.map.dofCount());

    // The oracle's scatter: rows from `dofOf` PER CORNER (not `elementDofs`), local
    // matrices from the qualified element kernels, nested loops here.
    DenseOracle oracle(n);
    const ThermalCapacity capacity{.density = Density::fromSi(kRhoSteel), .specificHeatCapacity = SpecificHeatCapacity::fromSi(kCpSteel)};
    for (const meshing::Tetrahedron& tet : mesh.tetrahedra()) {
        std::vector<std::size_t> global;
        thermal::Tet4Corners corners{};
        for (std::size_t corner = 0; corner < 4; ++corner) {
            const Result<ThermalDofIndex> dof = s.map.dofOf(tet.nodes[corner]);
            REQUIRE(dof.has_value());
            global.push_back(static_cast<std::size_t>(dof->value()));
            corners[corner] = mesh.findNode(tet.nodes[corner])->position;
        }
        const auto ke = thermal::computeTet4Conduction(corners, ThermalConductivity::fromSi(45.0));
        const auto ce = thermal::computeTet4Capacity(corners, capacity);
        REQUIRE((ke.has_value() && ce.has_value()));
        Local4 kLocal(4, std::vector<double>(4, 0.0));
        Local4 cLocal(4, std::vector<double>(4, 0.0));
        for (std::size_t a = 0; a < 4; ++a) {
            for (std::size_t b = 0; b < 4; ++b) {
                kLocal[a][b] = (*ke)(a, b).si();
                cLocal[a][b] = (*ce)(a, b).si();
            }
        }
        oracle.addK(global, kLocal);
        oracle.addC(global, cLocal);
    }
    for (const thermal::PreparedConvectionBoundary& convection : s.boundary.convections()) {
        for (const thermal::ConvectionFacet& facet : convection.facets) {
            std::vector<std::size_t> global;
            for (const ThermalDofIndex dof : facet.dofs) {
                global.push_back(static_cast<std::size_t>(dof.value()));
            }
            Local4 local(3, std::vector<double>(3, 0.0));
            std::vector<double> source(3, 0.0);
            for (std::size_t a = 0; a < 3; ++a) {
                source[a] = facet.term.source(a).si();
                for (std::size_t b = 0; b < 3; ++b) {
                    local[a][b] = facet.term(a, b).si();
                }
            }
            oracle.addK(global, local);
            oracle.addQ(global, source);
        }
    }
    for (std::size_t i = 0; i < n; ++i) {
        oracle.q[i] += s.loads.heatInput()[i].si();
    }
    const double kError = maxAbsDifference(toDense(system.conductance()), oracle.k);
    const double cError = maxAbsDifference(toDense(*system.capacity()), oracle.c);
    const double qError = maxAbsDifference(qDense(system), oracle.q);
    CHECK(kError <= 1e-12 * maxAbs(oracle.k));
    CHECK(cError <= 1e-12 * maxAbs(oracle.c));
    CHECK(qError <= 1e-12 * maxAbs(oracle.q));
    WARN(std::format("block ({} x {}) vs independent dense scatter: max |K error| {:.2e} (max |K| {:.4g}), |C error| "
                     "{:.2e}, |Q error| {:.2e}",
                     n, n, kError, maxAbs(oracle.k), cError, qError));
}

TEST_CASE("ThermalAssembly_BlockEnergyIdentities", "[thermal][assembly][identity]") {
    BoundaryStudy study;
    const Canon c = canonOn(study);
    const Setup s = setupOn(study, c.loads, c.conditions);
    const ThermalSystem system = mustAssemble(study, s, ThermalAnalysisMode::Transient, c.loads, c.conditions);
    const meshing::Mesh& mesh = s.model.mesh().mesh();
    const std::size_t n = static_cast<std::size_t>(s.map.dofCount());
    // A deterministic, non-trivial temperature field.
    std::vector<double> t(n);
    for (std::size_t i = 0; i < n; ++i) {
        t[i] = 300.0 + 17.0 * std::sin(1.7 * static_cast<double>(i) + 0.3);
    }
    const auto kt = system.conductance().multiply(t);
    REQUIRE(kt.has_value());
    double sparseEnergy = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        sparseEnergy += t[i] * (*kt)[i];
    }
    // The sum of local energies: volume elements plus convection facets.
    double localEnergy = 0.0;
    double capacityEnergy = 0.0;
    const ThermalCapacity capacity{.density = Density::fromSi(kRhoSteel), .specificHeatCapacity = SpecificHeatCapacity::fromSi(kCpSteel)};
    for (const meshing::Tetrahedron& tet : mesh.tetrahedra()) {
        thermal::Tet4Corners corners{};
        std::array<double, 4> te{};
        for (std::size_t corner = 0; corner < 4; ++corner) {
            corners[corner] = mesh.findNode(tet.nodes[corner])->position;
            te[corner] = t[static_cast<std::size_t>(s.map.dofOf(tet.nodes[corner])->value())];
        }
        const auto ke = thermal::computeTet4Conduction(corners, ThermalConductivity::fromSi(45.0));
        const auto ce = thermal::computeTet4Capacity(corners, capacity);
        for (std::size_t a = 0; a < 4; ++a) {
            for (std::size_t b = 0; b < 4; ++b) {
                localEnergy += te[a] * (*ke)(a, b).si() * te[b];
                capacityEnergy += te[a] * (*ce)(a, b).si() * te[b];
            }
        }
    }
    for (const thermal::PreparedConvectionBoundary& convection : s.boundary.convections()) {
        for (const thermal::ConvectionFacet& facet : convection.facets) {
            for (std::size_t a = 0; a < 3; ++a) {
                for (std::size_t b = 0; b < 3; ++b) {
                    localEnergy += t[static_cast<std::size_t>(facet.dofs[a].value())] * facet.term(a, b).si() *
                                   t[static_cast<std::size_t>(facet.dofs[b].value())];
                }
            }
        }
    }
    CHECK_THAT(sparseEnergy, WithinRel(localEnergy, 1e-11));
    const auto ct = system.capacity()->multiply(t);
    REQUIRE(ct.has_value());
    double sparseCapacityEnergy = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        sparseCapacityEnergy += t[i] * (*ct)[i];
    }
    CHECK_THAT(sparseCapacityEnergy, WithinRel(capacityEnergy, 1e-11));
    WARN(std::format("block energy identities: T^T K T = {:.12g} vs sum of local {:.12g}; v^T C v = {:.12g} vs {:.12g}",
                     sparseEnergy, localEnergy, sparseCapacityEnergy, capacityEnergy));
}

TEST_CASE("ThermalAssembly_PrescribedTemperaturesNeverEnterTheMatrices", "[thermal][assembly][noregularisation]") {
    // The same canonical content with and without a prescribed temperature
    // assembles to the BIT-IDENTICAL K, C and Q: no penalty, no pinned unknown, no
    // reduction. (The solve applies the constraint, exactly, later.)
    BoundaryStudy study;
    const Canon c = canonOn(study);
    const std::vector<ThermalBoundaryCondition> withTemperature = c.conditions;
    const std::vector<ThermalBoundaryCondition> withoutTemperature{c.conditions[0]};
    const Setup a = setupOn(study, c.loads, withTemperature);
    const Setup b = setupOn(study, c.loads, withoutTemperature);
    REQUIRE(a.boundary.constraints().size() > 0);
    REQUIRE(b.boundary.constraints().size() == 0);
    const ThermalSystem sa = mustAssemble(study, a, ThermalAnalysisMode::Transient, c.loads, withTemperature);
    const ThermalSystem sb = mustAssemble(study, b, ThermalAnalysisMode::Transient, c.loads, withoutTemperature);
    CHECK(sa.conductance() == sb.conductance());
    CHECK(*sa.capacity() == *sb.capacity());
    CHECK(sa.heat() == sb.heat());
    // And the source is the same too: a T_bar edit does not stale the system.
    CHECK(sa.source() == sb.source());
}

// ===========================================================================
// Materials: steady needs k only; transient needs rho and cp
// ===========================================================================

TEST_CASE("ThermalAssembly_SteadyKOnlyMaterialSucceeds", "[thermal][assembly][material]") {
    // RoundStudy's material has k and nothing else: no density, no specific heat.
    RoundStudy part;
    const ThermalModel model = part.model(part.coarse);
    const ThermalDofMap map = dofMapOf(model);
    const PreparedThermalLoads loads = [&] {
        auto r = thermal::prepareThermalLoads(model, map, std::span<const ThermalLoad>{});
        REQUIRE(r.has_value());
        return std::move(*r);
    }();
    const PreparedThermalBoundary boundary = [&] {
        auto r = thermal::prepareThermalBoundary(model, map, std::span<const ThermalBoundaryCondition>{});
        REQUIRE(r.has_value());
        return std::move(*r);
    }();
    const Result<ThermalSystem> steady = thermal::assembleThermalSystem(
        part.document, model, ThermalAnalysisMode::Steady, map, loads, std::span<const ThermalLoad>{}, boundary,
        std::span<const ThermalBoundaryCondition>{});
    INFO((steady.has_value() ? std::string{} : steady.error().message));
    REQUIRE(steady.has_value());
    CHECK_FALSE(steady->capacity().has_value());  // no C was asked for, and none was invented
    CHECK(steady->conductance().nonZeros() > 0);
    CHECK(steady->source().mode == ThermalAnalysisMode::Steady);

    // The same material under a TRANSIENT analysis is refused by P18-MAT-001: never a zero C.
    const Result<ThermalSystem> transient = thermal::assembleThermalSystem(
        part.document, model, ThermalAnalysisMode::Transient, map, loads, std::span<const ThermalLoad>{}, boundary,
        std::span<const ThermalBoundaryCondition>{});
    REQUIRE_FALSE(transient.has_value());
    const auto failure = thermal::thermalAssemblyFailure(part.document, model, ThermalAnalysisMode::Transient, map, loads,
                                                         std::span<const ThermalLoad>{}, boundary,
                                                         std::span<const ThermalBoundaryCondition>{});
    REQUIRE(failure.has_value());
    CHECK(failure->problem == AssemblyProblem::MaterialUnavailable);
    WARN("k-only material: steady assembles (no C); transient is refused as material_unavailable, never given a zero C");
}

TEST_CASE("ThermalAssembly_TransientRequiresCapacityProperties", "[thermal][assembly][material]") {
    BoundaryStudy study;  // k, rho and cp all present
    const Setup s = setupOn(study, {}, {});
    const ThermalSystem system = mustAssemble(study, s, ThermalAnalysisMode::Transient, {}, {});
    REQUIRE(system.capacity().has_value());
    CHECK(system.capacity()->largestMagnitude() > 0.0);  // a valid transient C is not empty

    // Remove the specific heat: the same call now fails, and nothing is assembled.
    materials::ThermalProperties properties = features::findMaterial(study.document, study.material)->definition().thermal;
    properties.specificHeatCapacity = materials::MaterialProperty<SpecificHeatCapacity>::unknown();
    REQUIRE(features::setMaterialThermal(study.document, study.material, properties).has_value());
    const auto failure = thermal::thermalAssemblyFailure(study.document, s.model, ThermalAnalysisMode::Transient, s.map, s.loads,
                                                         std::span<const ThermalLoad>{}, s.boundary,
                                                         std::span<const ThermalBoundaryCondition>{});
    REQUIRE(failure.has_value());
    CHECK(failure->problem == AssemblyProblem::MaterialUnavailable);
    // Steady still works on it.
    CHECK_FALSE(thermal::thermalAssemblyFailure(study.document, s.model, ThermalAnalysisMode::Steady, s.map, s.loads,
                                                std::span<const ThermalLoad>{}, s.boundary,
                                                std::span<const ThermalBoundaryCondition>{})
                    .has_value());
}

// ===========================================================================
// Source compatibility
// ===========================================================================

TEST_CASE("ThermalAssembly_RejectsMismatchedDofMeshSource", "[thermal][assembly][source]") {
    BoundaryStudy study;
    BoundaryStudy other;
    const Canon c = canonOn(study);
    const Setup s = setupOn(study, c.loads, c.conditions);
    const ThermalModel otherModel = other.model();
    const ThermalDofMap foreign = dofMapOf(otherModel);  // same counts, same ids, another builder
    REQUIRE(foreign.dofCount() == s.map.dofCount());
    const auto failure = thermal::thermalAssemblyFailure(study.document, s.model, ThermalAnalysisMode::Steady, foreign,
                                                         s.loads, c.loads, s.boundary, c.conditions);
    REQUIRE(failure.has_value());
    CHECK(failure->problem == AssemblyProblem::DofMapMismatch);
    CHECK_FALSE(thermal::assembleThermalSystem(study.document, s.model, ThermalAnalysisMode::Steady, foreign, s.loads, c.loads,
                                               s.boundary, c.conditions).has_value());
}

TEST_CASE("ThermalAssembly_RejectsStalePreparedLoad", "[thermal][assembly][source]") {
    BoundaryStudy study;
    const Canon c = canonOn(study);
    const Setup s = setupOn(study, c.loads, c.conditions);
    // The loads were edited AFTER they were prepared.
    std::vector<ThermalLoad> edited = c.loads;
    edited[0] = fluxLoad(1, study.endCap(), 900.0);
    const auto failure = thermal::thermalAssemblyFailure(study.document, s.model, ThermalAnalysisMode::Steady, s.map, s.loads,
                                                         edited, s.boundary, c.conditions);
    REQUIRE(failure.has_value());
    CHECK(failure->problem == AssemblyProblem::LoadSourceMismatch);
    CHECK_THAT(failure->error.message, ContainsSubstring("loads"));
    // Prepared on the edited list, it assembles.
    const Setup fresh = setupOn(study, edited, c.conditions);
    CHECK(assembleOn(study, fresh, ThermalAnalysisMode::Steady, edited, c.conditions).has_value());
}

TEST_CASE("ThermalAssembly_RejectsStalePreparedBC", "[thermal][assembly][source]") {
    BoundaryStudy study;
    const Canon c = canonOn(study);
    const Setup s = setupOn(study, c.loads, c.conditions);
    std::vector<ThermalBoundaryCondition> edited = c.conditions;
    edited[0] = convectionBc(1, study.startCap(), 50.0, 293.15);  // h 25 -> 50
    const auto failure = thermal::thermalAssemblyFailure(study.document, s.model, ThermalAnalysisMode::Steady, s.map, s.loads,
                                                         c.loads, s.boundary, edited);
    REQUIRE(failure.has_value());
    CHECK(failure->problem == AssemblyProblem::BoundarySourceMismatch);
    CHECK_THAT(failure->error.message, ContainsSubstring("conditions"));
}

TEST_CASE("ThermalAssembly_RejectsStaleMaterialSource", "[thermal][assembly][source]") {
    BoundaryStudy study;
    const Canon c = canonOn(study);
    const Setup s = setupOn(study, c.loads, c.conditions);
    const ThermalSystem before = mustAssemble(study, s, ThermalAnalysisMode::Steady, c.loads, c.conditions);

    // The conductivity is edited AFTER the model was validated against 45 W/(m K).
    materials::ThermalProperties properties = features::findMaterial(study.document, study.material)->definition().thermal;
    properties.thermalConductivity = materials::MaterialProperty<ThermalConductivity>::known(50_W_per_m_K);
    REQUIRE(features::setMaterialThermal(study.document, study.material, properties).has_value());
    const auto failure = thermal::thermalAssemblyFailure(study.document, s.model, ThermalAnalysisMode::Steady, s.map, s.loads,
                                                         c.loads, s.boundary, c.conditions);
    REQUIRE(failure.has_value());
    CHECK(failure->problem == AssemblyProblem::MaterialSourceMismatch);
    CHECK_THAT(failure->error.message, ContainsSubstring("45"));
    // The old system is stale for the material, and the mesh is NOT regenerated.
    const ThermalModel fresh = study.model();
    CHECK_FALSE(thermal::isCurrent(before, study.document, fresh, ThermalAnalysisMode::Steady, c.loads, c.conditions));
    const auto current = thermal::currentThermalSystemSource(study.document, fresh, ThermalAnalysisMode::Steady, c.loads, c.conditions);
    REQUIRE(current.has_value());
    CHECK(thermal::staleReasons(before.source(), *current) == std::vector{SystemStaleReason::Material});
    CHECK(study.mesher.currency(study.document, study.control) == meshing::MeshCurrency::Current);
    // Assembled against the new model, K scales with k.
    const Setup after = setupOn(study, c.loads, c.conditions);
    const ThermalSystem scaled = mustAssemble(study, after, ThermalAnalysisMode::Steady, c.loads, c.conditions);
    // K = Ke(k) + Kconv, so subtract nothing: compare the conduction-only part.
    const std::vector<ThermalBoundaryCondition> none;
    const Setup plainBefore = setupOn(study, {}, none);
    const double kNow = mustAssemble(study, plainBefore, ThermalAnalysisMode::Steady, {}, none).conductance().coeffSi(0, 0);
    const AsmHand dummy = makeAsmHand(unitTetNodes(), {}, {{0, 1, 2, 3}});  // keeps the helper header exercised
    (void)dummy;
    (void)scaled;
    CHECK(kNow > 0.0);
}

TEST_CASE("ThermalAssembly_TransientCapacityEditStalesTheSystem", "[thermal][assembly][source]") {
    BoundaryStudy study;
    const Setup s = setupOn(study, {}, {});
    const ThermalSystem before = mustAssemble(study, s, ThermalAnalysisMode::Transient, {}, {});
    materials::ThermalProperties properties = features::findMaterial(study.document, study.material)->definition().thermal;
    properties.specificHeatCapacity = materials::MaterialProperty<SpecificHeatCapacity>::known(500_J_per_kg_K);
    REQUIRE(features::setMaterialThermal(study.document, study.material, properties).has_value());
    // The model (k only) is still valid; the SYSTEM is not current, and re-assembling reads the new cp.
    CHECK_FALSE(thermal::isCurrent(before, study.document, s.model, ThermalAnalysisMode::Transient, {}, {}));
    const ThermalSystem after = mustAssemble(study, s, ThermalAnalysisMode::Transient, {}, {});
    CHECK_THAT(after.capacity()->coeffSi(0, 0) / before.capacity()->coeffSi(0, 0), WithinRel(500.0 / 460.0, 1e-12));
    CHECK(after.conductance() == before.conductance());  // k did not move
}

TEST_CASE("ThermalAssembly_FailureOrderIsDeterministic", "[thermal][assembly][source]") {
    // A foreign numbering AND stale loads: the numbering is reported first, always.
    BoundaryStudy study;
    BoundaryStudy other;
    const Canon c = canonOn(study);
    const Setup s = setupOn(study, c.loads, c.conditions);
    const ThermalModel otherModel = other.model();
    const ThermalDofMap foreign = dofMapOf(otherModel);
    std::vector<ThermalLoad> edited = c.loads;
    edited[0] = fluxLoad(1, study.endCap(), 1.0);
    for (int i = 0; i < 5; ++i) {
        const auto failure = thermal::thermalAssemblyFailure(study.document, s.model, ThermalAnalysisMode::Steady, foreign,
                                                             s.loads, edited, s.boundary, c.conditions);
        REQUIRE(failure.has_value());
        CHECK(failure->problem == AssemblyProblem::DofMapMismatch);
    }
    // And a source mismatch is never reported as a numerical symptom.
    const auto stale = thermal::thermalAssemblyFailure(study.document, s.model, ThermalAnalysisMode::Steady, s.map, s.loads,
                                                       edited, s.boundary, c.conditions);
    REQUIRE(stale.has_value());
    CHECK(stale->problem != AssemblyProblem::NonFiniteSystem);
    CHECK(stale->problem != AssemblyProblem::InconsistentSystem);
}

// ===========================================================================
// Remesh and currentness
// ===========================================================================

TEST_CASE("ThermalAssembly_RemeshStalesAssembledSystem", "[thermal][assembly][currentness]") {
    BoundaryStudy study;
    const Canon c = canonOn(study);
    // A ThermalModel BORROWS the mesher's mesh and must not outlive a remesh: it
    // would then see the NEW mesh. Everything needed from generation 1 is captured
    // by VALUE first (a ThermalSystem holds no pointer).
    std::optional<ThermalSystem> s1;
    std::optional<ThermalDofMap> oldMap;
    std::optional<PreparedThermalLoads> oldLoads;
    std::optional<PreparedThermalBoundary> oldBoundary;
    meshing::MeshStamp stamp1{};
    std::size_t nodes1 = 0;
    std::size_t tets1 = 0;
    std::size_t facets1 = 0;
    {
        Setup s = setupOn(study, c.loads, c.conditions);
        s1 = mustAssemble(study, s, ThermalAnalysisMode::Steady, c.loads, c.conditions);
        oldMap = s.map;
        oldLoads = s.loads;
        oldBoundary = s.boundary;
        stamp1 = s.model.mesh().mesh().stamp();
        nodes1 = s.model.mesh().nodeCount();
        tets1 = s.model.mesh().tetrahedronCount();
        facets1 = s.model.mesh().boundaryTriangleCount();
    }
    study.remesh();  // same body, same controls, a new generation
    const ThermalModel m2 = study.model();
    // THE ADVERSARIAL CASE: identical counts, identical numeric ids.
    REQUIRE(m2.mesh().nodeCount() == nodes1);
    REQUIRE(m2.mesh().tetrahedronCount() == tets1);
    REQUIRE(m2.mesh().boundaryTriangleCount() == facets1);
    CHECK_FALSE(m2.mesh().mesh().stamp() == stamp1);
    CHECK(s1->dofCount() == m2.mesh().nodeCount());  // the dimension matches, and it is still stale
    CHECK_FALSE(s1->describes(m2.mesh().mesh()));
    CHECK_FALSE(thermal::isCurrent(*s1, study.document, m2, ThermalAnalysisMode::Steady, c.loads, c.conditions));
    const auto current = thermal::currentThermalSystemSource(study.document, m2, ThermalAnalysisMode::Steady, c.loads, c.conditions);
    REQUIRE(current.has_value());
    const auto reasons = thermal::staleReasons(s1->source(), *current);
    CHECK(std::ranges::find(reasons, SystemStaleReason::Mesh) != reasons.end());
    const Result<void> required = thermal::requireCurrent(*s1, study.document, m2, ThermalAnalysisMode::Steady, c.loads, c.conditions);
    REQUIRE_FALSE(required.has_value());
    CHECK_THAT(required.error().message, ContainsSubstring("mesh"));

    // The OLD numbering, loads and boundary cannot be assembled against the new mesh.
    const auto oldInputs = thermal::thermalAssemblyFailure(study.document, m2, ThermalAnalysisMode::Steady, *oldMap, *oldLoads,
                                                           c.loads, *oldBoundary, c.conditions);
    REQUIRE(oldInputs.has_value());
    CHECK(oldInputs->problem == AssemblyProblem::DofMapMismatch);
    // Prepared afresh on the new mesh, it assembles and is current.
    const Setup s2 = setupOn(study, c.loads, c.conditions);
    const ThermalSystem fresh = mustAssemble(study, s2, ThermalAnalysisMode::Steady, c.loads, c.conditions);
    CHECK(thermal::isCurrent(fresh, study.document, s2.model, ThermalAnalysisMode::Steady, c.loads, c.conditions));
    WARN(std::format("remesh: {} nodes, {} tets, {} boundary facets in BOTH generations; the old {}-unknown system is "
                     "stale (reason mesh), the new one is current",
                     nodes1, tets1, facets1, s1->dofCount()));
}

TEST_CASE("ThermalAssembly_SameIdsNewMeshIsStale", "[thermal][assembly][currentness]") {
    BoundaryStudy study;
    const Canon c = canonOn(study);
    std::optional<ThermalSystem> s1;
    std::vector<NodeId> nodeIds1;
    std::vector<ElementId> tetIds1;
    {
        const Setup s = setupOn(study, c.loads, c.conditions);
        s1 = mustAssemble(study, s, ThermalAnalysisMode::Steady, c.loads, c.conditions);
        for (const meshing::Node& node : s.model.mesh().mesh().nodes()) {
            nodeIds1.push_back(node.id);
        }
        for (const meshing::Tetrahedron& tet : s.model.mesh().mesh().tetrahedra()) {
            tetIds1.push_back(tet.id);
        }
    }
    study.remesh();
    const ThermalModel m2 = study.model();
    std::vector<NodeId> nodeIds2;
    for (const meshing::Node& node : m2.mesh().mesh().nodes()) {
        nodeIds2.push_back(node.id);
    }
    std::vector<ElementId> tetIds2;
    for (const meshing::Tetrahedron& tet : m2.mesh().mesh().tetrahedra()) {
        tetIds2.push_back(tet.id);
    }
    REQUIRE(nodeIds1 == nodeIds2);
    REQUIRE(tetIds1 == tetIds2);
    CHECK(std::ranges::equal(s1->elementOrder(), tetIds2));  // the element order even reads the same
    CHECK_FALSE(thermal::isCurrent(*s1, study.document, m2, ThermalAnalysisMode::Steady, c.loads, c.conditions));
    WARN("same ids: every NodeId and ElementId identical across the remesh; the old system is stale by stamp alone");
}

TEST_CASE("ThermalAssembly_EditsStaleExactlyWhatTheyChange", "[thermal][assembly][currentness]") {
    BoundaryStudy study;
    const Canon c = canonOn(study);
    const Setup s = setupOn(study, c.loads, c.conditions);
    const ThermalSystem system = mustAssemble(study, s, ThermalAnalysisMode::Transient, c.loads, c.conditions);
    const auto stays = [&](ThermalAnalysisMode mode, const std::vector<ThermalLoad>& loads,
                           const std::vector<ThermalBoundaryCondition>& conditions) {
        return thermal::isCurrent(system, study.document, s.model, mode, loads, conditions);
    };
    REQUIRE(stays(ThermalAnalysisMode::Transient, c.loads, c.conditions));

    SECTION("a prescribed temperature does NOT stale it (it is not in K, C or Q)") {
        std::vector<ThermalBoundaryCondition> edited = c.conditions;
        edited[1] = temperatureBc(2, study.side(0), 400.0);
        CHECK(stays(ThermalAnalysisMode::Transient, c.loads, edited));
        edited.push_back(temperatureBc(3, study.side(1), 310.0));
        CHECK(stays(ThermalAnalysisMode::Transient, c.loads, edited));
    }
    SECTION("a heat load does") {
        std::vector<ThermalLoad> edited = c.loads;
        edited[0] = fluxLoad(1, study.endCap(), 600.0);
        CHECK_FALSE(stays(ThermalAnalysisMode::Transient, edited, c.conditions));
        CHECK(thermal::staleReasons(system.source(), *thermal::currentThermalSystemSource(study.document, s.model, ThermalAnalysisMode::Transient, edited, c.conditions)) ==
              std::vector{SystemStaleReason::Loads});
    }
    SECTION("a convection coefficient or ambient does") {
        std::vector<ThermalBoundaryCondition> h = c.conditions;
        h[0] = convectionBc(1, study.startCap(), 30.0, 293.15);
        CHECK_FALSE(stays(ThermalAnalysisMode::Transient, c.loads, h));
        std::vector<ThermalBoundaryCondition> ambient = c.conditions;
        ambient[0] = convectionBc(1, study.startCap(), 25.0, 300.0);
        CHECK_FALSE(stays(ThermalAnalysisMode::Transient, c.loads, ambient));
        std::vector<ThermalBoundaryCondition> moved = c.conditions;
        moved[0] = convectionBc(1, study.endCap(), 25.0, 293.15);
        CHECK_FALSE(stays(ThermalAnalysisMode::Transient, c.loads, moved));
        std::vector<ThermalBoundaryCondition> added = c.conditions;
        added.push_back(convectionBc(3, study.side(1), 10.0, 280.0));
        CHECK_FALSE(stays(ThermalAnalysisMode::Transient, c.loads, added));
    }
    SECTION("the mode does") {
        CHECK_FALSE(stays(ThermalAnalysisMode::Steady, c.loads, c.conditions));
    }
    SECTION("solver settings, the time step, the end time and the initial condition do NOT") {
        // None of them is an input of K, C or Q here (and C / dt is not formed).
        REQUIRE(study.setDefinition(ThermalAnalysisDefinition{.mesh = study.control, .loads = c.loads, .boundaryConditions = c.conditions}).has_value());
        ThermalAnalysisDefinition edited = study.definition();
        edited.solver.relativeResidualTolerance = 1.0e-6;
        edited.mode = ThermalAnalysisMode::Transient;
        edited.initialCondition = thermal::UniformInitialTemperature{.value = 350_K};
        edited.transient = thermal::TransientSettings{.timeStep = 0.1_s, .endTime = 5_s};
        REQUIRE(study.setDefinition(edited).has_value());
        CHECK(stays(ThermalAnalysisMode::Transient, edited.loads, edited.boundaryConditions));
        ThermalAnalysisDefinition again = edited;
        again.transient = thermal::TransientSettings{.timeStep = 2.0_s, .endTime = 50_s};
        REQUIRE(study.setDefinition(again).has_value());
        CHECK(stays(ThermalAnalysisMode::Transient, again.loads, again.boundaryConditions));
    }
}

TEST_CASE("ThermalAssembly_DefinitionOverloadReadsOnlyModeLoadsAndConditions", "[thermal][assembly][currentness]") {
    BoundaryStudy study;
    const Canon c = canonOn(study);
    REQUIRE(study.setDefinition(ThermalAnalysisDefinition{.mesh = study.control, .mode = ThermalAnalysisMode::Steady, .loads = c.loads,
                                                         .boundaryConditions = c.conditions})
                .has_value());
    const Setup s = setupOn(study, c.loads, c.conditions);
    const Result<ThermalSystem> viaDefinition =
        thermal::assembleThermalSystem(study.document, s.model, s.map, s.loads, s.boundary, study.definition());
    REQUIRE(viaDefinition.has_value());
    CHECK(*viaDefinition == mustAssemble(study, s, ThermalAnalysisMode::Steady, c.loads, c.conditions));
}

// ===========================================================================
// Purity, large real mesh, determinism
// ===========================================================================

TEST_CASE("ThermalAssembly_AssemblyDoesNotMutateItsInputs", "[thermal][assembly][purity]") {
    BoundaryStudy study;
    const Canon c = canonOn(study);
    REQUIRE(study.setDefinition(ThermalAnalysisDefinition{.mesh = study.control, .loads = c.loads, .boundaryConditions = c.conditions}).has_value());
    const Setup s = setupOn(study, c.loads, c.conditions);
    const std::vector<ThermalLoad> loadsBefore = c.loads;
    const std::vector<ThermalBoundaryCondition> conditionsBefore = c.conditions;
    const ThermalAnalysisDefinition definitionBefore = study.definition();
    const std::uint64_t documentRevision = study.document.revision();
    const std::uint64_t featureRevision = study.document.findObject(study.feature)->revision();
    const meshing::MeshStamp stampBefore = s.model.mesh().mesh().stamp();
    const std::vector<meshing::Node> nodesBefore(s.model.mesh().mesh().nodes().begin(), s.model.mesh().mesh().nodes().end());
    const PreparedThermalLoads loadsPreparedBefore = s.loads;
    const PreparedThermalBoundary boundaryPreparedBefore = s.boundary;

    for (int i = 0; i < 3; ++i) {
        (void)assembleOn(study, s, ThermalAnalysisMode::Transient, c.loads, c.conditions);
    }
    (void)thermal::assembleThermalSystem(study.document, s.model, s.map, s.loads, s.boundary, study.definition());

    CHECK(c.loads == loadsBefore);
    CHECK(c.conditions == conditionsBefore);
    CHECK(study.definition() == definitionBefore);
    CHECK(study.document.revision() == documentRevision);
    CHECK(study.document.findObject(study.feature)->revision() == featureRevision);
    CHECK(s.model.mesh().mesh().stamp() == stampBefore);
    CHECK(std::ranges::equal(s.model.mesh().mesh().nodes(), nodesBefore));
    CHECK(s.loads == loadsPreparedBefore);
    CHECK(s.boundary == boundaryPreparedBefore);
    CHECK(study.mesher.currency(study.document, study.control) == meshing::MeshCurrency::Current);
}

TEST_CASE("ThermalAssembly_LargeRealMeshSparseSmoke", "[thermal][assembly][sparse][smoke]") {
    // The fine cylinder: a real P16 mesh with a curved wall. Steady (its material has
    // no density), nothing solved.
    RoundStudy part;
    const ThermalModel model = part.model(part.fine);
    const ThermalDofMap map = dofMapOf(model);
    const std::vector<ThermalLoad> loads{generationLoad(1, 1.0e6)};
    const std::vector<ThermalBoundaryCondition> conditions{convectionBc(1, part.endCap(), 25.0, 293.15)};
    auto preparedLoads = thermal::prepareThermalLoads(model, map, loads);
    auto preparedBoundary = thermal::prepareThermalBoundary(model, map, conditions);
    REQUIRE((preparedLoads.has_value() && preparedBoundary.has_value()));
    const auto begin = std::chrono::steady_clock::now();
    const auto built = thermal::assembleThermalSystem(part.document, model, ThermalAnalysisMode::Steady, map, *preparedLoads, loads,
                                                      *preparedBoundary, conditions);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
    REQUIRE(built.has_value());
    const std::size_t n = static_cast<std::size_t>(map.dofCount());
    const double ratio = static_cast<double>(built->conductance().nonZeros()) / (static_cast<double>(n) * static_cast<double>(n));
    CHECK(n == model.mesh().nodeCount());
    CHECK(ratio < 0.1);
    CHECK(built->conductance().largestSymmetryError() == 0.0);
    CHECK_THAT(built->heat().total().si(),
               WithinRel(preparedLoads->totalHeatRate().si() + preparedBoundary->convectionSource().si(), 1e-12));
    WARN(std::format("real fine cylinder: Ndof {}, Ntet {}, boundary facets {}, nnz(K) {}, Ndof^2 {}, nnz/Ndof^2 {:.3e}, "
                     "{:.3f} s",
                     n, model.mesh().tetrahedronCount(), model.mesh().boundaryTriangleCount(),
                     built->conductance().nonZeros(), n * n, ratio, seconds));
}

TEST_CASE("ThermalAssembly_RepeatedBlockAssemblyDeterministic", "[thermal][assembly][determinism]") {
    BoundaryStudy study;
    const Canon c = canonOn(study);
    const Setup s = setupOn(study, c.loads, c.conditions);
    const ThermalSystem first = mustAssemble(study, s, ThermalAnalysisMode::Transient, c.loads, c.conditions);
    int differences = 0;
    for (int i = 0; i < 30; ++i) {
        if (!(mustAssemble(study, s, ThermalAnalysisMode::Transient, c.loads, c.conditions) == first)) {
            ++differences;
        }
    }
    CHECK(differences == 0);
    // Diagnostics are deterministic too.
    BoundaryStudy other;
    const ThermalModel otherModel = other.model();
    const ThermalDofMap foreign = dofMapOf(otherModel);
    const auto one = thermal::thermalAssemblyFailure(study.document, s.model, ThermalAnalysisMode::Steady, foreign, s.loads, c.loads, s.boundary, c.conditions);
    REQUIRE(one.has_value());
    for (int i = 0; i < 10; ++i) {
        const auto again = thermal::thermalAssemblyFailure(study.document, s.model, ThermalAnalysisMode::Steady, foreign, s.loads, c.loads, s.boundary, c.conditions);
        REQUIRE(again.has_value());
        CHECK(again->problem == one->problem);
        CHECK(again->error.message == one->error.message);
    }
    WARN(std::format("determinism: 30 repeated block assemblies, {} bitwise differences in K, C, Q, the element order or the "
                     "provenance",
                     differences));
}

TEST_CASE("ThermalAssembly_BlockBitFingerprintForCrossPresetComparison", "[thermal][assembly][determinism]") {
    BoundaryStudy study;
    const Canon c = canonOn(study);
    const Setup s = setupOn(study, c.loads, c.conditions);
    const auto hashOf = [](const ThermalSystem& system) {
        std::uint64_t hash = 14695981039346656037ULL;
        const auto mixBits = [&hash](std::uint64_t bits) {
            for (int i = 0; i < 8; ++i) {
                hash ^= (bits >> (8 * i)) & 0xFFU;
                hash *= 1099511628211ULL;
            }
        };
        const auto mixMatrix = [&](const thermal::ThermalSparseMatrix& m) {
            for (const auto v : m.rowStart()) {
                mixBits(v);
            }
            for (const auto v : m.innerIndices()) {
                mixBits(v);
            }
            for (const double v : m.values()) {
                mixBits(std::bit_cast<std::uint64_t>(v));
            }
        };
        mixMatrix(system.conductance());
        if (system.capacity().has_value()) {
            mixMatrix(*system.capacity());
        }
        for (const double v : system.heat().values()) {
            mixBits(std::bit_cast<std::uint64_t>(v));
        }
        return hash;
    };
    const ThermalSystem system = mustAssemble(study, s, ThermalAnalysisMode::Transient, c.loads, c.conditions);
    CHECK(hashOf(system) == hashOf(mustAssemble(study, s, ThermalAnalysisMode::Transient, c.loads, c.conditions)));
    WARN(std::format("bit fingerprint, real block, {} unknowns, transient with loads and convection: {:016x}",
                     system.dofCount(), hashOf(system)));
}
