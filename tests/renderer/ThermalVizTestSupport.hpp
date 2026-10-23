#pragma once

// Shared fixtures for the P18-VIZ-001 tests.
//
// WHAT IS REAL AND WHAT IS HAND-BUILT, AND WHY.
//
//   A `ThermalPostResult` has a private constructor with two friends, so a viewer cannot be tested against a
//   synthetic one -- every result here comes out of the QUALIFIED pipeline, on a real document (`VizStudy`, the
//   steel block of the other thermal milestones) or on a hand mesh through the POST seam. A viewer tested against a
//   hand-built result would be tested against nothing.
//
//   A hand mesh can pose what a real one cannot: SPARSE NODE AND ELEMENT HANDLES, a field whose every node has an
//   independently chosen temperature (every unknown prescribed, which the solver reproduces bit for bit), and a
//   gradient the test knows without asking production for it.
//
// EVERY EXPECTED VALUE IS TYPED IN, or computed by the test's own arithmetic (a cross product written out, the
// norm of three typed components). Nothing here asks a viewer function for the number it is judged against.

#include "thermal/ThermalPostTestSupport.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/meshing/GeometryMeshMap.hpp>
#include <bettercad/meshing/MeshControl.hpp>
#include <bettercad/meshing/Mesher.hpp>
#include <bettercad/renderer/MeshInspection.hpp>
#include <bettercad/renderer/MeshView.hpp>
#include <bettercad/renderer/ThermalContour.hpp>
#include <bettercad/renderer/ThermalInspection.hpp>
#include <bettercad/renderer/ThermalOverlay.hpp>
#include <bettercad/renderer/ThermalResultView.hpp>
#include <bettercad/sketch/Sketch.hpp>
#include <bettercad/thermal/ThermalAnalysis.hpp>
#include <bettercad/thermal/ThermalAnalysisObject.hpp>
#include <bettercad/thermal/ThermalBalance.hpp>
#include <bettercad/thermal/ThermalBoundary.hpp>
#include <bettercad/thermal/ThermalLoadVector.hpp>
#include <bettercad/thermal/ThermalPost.hpp>
#include <bettercad/thermal/ThermalSolve.hpp>
#include <bettercad/thermal/ThermalSystem.hpp>
#include <bettercad/thermal/ThermalTransient.hpp>
#include <bettercad/thermal/ThermalValidation.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace vztest {

using namespace pstest;
using thermal::PreparedThermalBoundary;
using thermal::PreparedThermalLoads;
using thermal::SurfaceHeatFluxLoad;
using thermal::ThermalAnalysisMode;
using thermal::ThermalLoad;
using thermal::VolumetricHeatSourceLoad;

constexpr double kAmbient = 293.15;

// ---------------------------------------------------------------------------
// A hand mesh and a result whose nodal temperatures the test chose
// ---------------------------------------------------------------------------

/// A hand mesh and the qualified POST result of a field in which EVERY unknown is prescribed to a chosen value.
/// With every unknown constrained the solver's answer is the prescription, bit for bit, so the test controls each
/// nodal temperature -- and each tetrahedron's gradient follows from them by the test's own dense oracle.
struct HandPost {
    AsmHand hand;
    ThermalPostResult post;
    double conductivity = 0.0;
};

[[nodiscard]] inline HandPost makeHandPost(AsmHand hand, double conductivity, const std::vector<double>& kelvin) {
    REQUIRE(kelvin.size() == static_cast<std::size_t>(hand.dofs.dofCount()));
    const ThermalSystem system = assemble(hand, conductivity, std::nullopt, noHeat(hand));
    std::vector<thermal::ThermalTemperatureConstraint> list;
    for (std::size_t dof = 0; dof < kelvin.size(); ++dof) {
        list.push_back({dofIx(dof), Temperature::fromSi(kelvin[dof])});
    }
    const auto constraints = thermal::buildThermalConstraintMap(hand.dofs, list);
    REQUIRE(constraints.has_value());
    const auto solved = thermal::solveSteadyThermalSystem(system, hand.dofs, *constraints, ThermalSolverSettings{});
    INFO((solved.has_value() ? std::string{} : solved.error().error.message));
    REQUIRE(solved.has_value());
    auto post = thermal::recoverSteadyFromSolution(hand.mesh, hand.dofs, ThermalConductivity::fromSi(conductivity), *solved);
    INFO((post.has_value() ? std::string{} : post.error().error.message));
    REQUIRE(post.has_value());
    return HandPost{std::move(hand), std::move(*post), conductivity};
}

/// One tetrahedron with the given corner positions (in this order) and nodal handles (the element names its nodes in
/// the order given; the numbering is ascending by handle, so SPARSE handles give a numbering that is not the handles).
[[nodiscard]] inline AsmHand sparseTetHand(const std::array<Vec3, 4>& corners, const std::array<std::uint32_t, 4>& handles) {
    std::vector<std::pair<std::uint32_t, Vec3>> byHandle;
    for (std::size_t i = 0; i < 4; ++i) {
        byHandle.emplace_back(handles[i], corners[i]);
    }
    std::sort(byHandle.begin(), byHandle.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    Nodes nodes;
    for (const auto& [handle, position] : byHandle) {
        nodes.emplace_back(handle, at(position[0], position[1], position[2]));
    }
    std::array<std::size_t, 4> local{};
    for (std::size_t i = 0; i < 4; ++i) {
        for (std::size_t n = 0; n < byHandle.size(); ++n) {
            if (byHandle[n].first == handles[i]) {
                local[i] = n;
            }
        }
    }
    return makeAsmHand(nodes, {}, {local});
}

/// FOUR DISJOINT TETRAHEDRA (sixteen nodes, handles 1..16, no node shared), each translated by 10 m along x so
/// they do not touch, and a boundary triangle for each of their sixteen faces added FIRST -- so a tetrahedron's
/// ElementId is its ordinal plus sixteen, and an id used as an index lands past the end of every array. Each
/// tetrahedron is the unit tetrahedron; kelvin holds the sixteen nodal temperatures in numbering order.
[[nodiscard]] inline AsmHand fourTetrahedraHand() {
    Nodes nodes;
    std::vector<std::array<std::size_t, 3>> triangles;
    std::vector<std::array<std::size_t, 4>> tets;
    for (std::size_t t = 0; t < 4; ++t) {
        const double dx = 10.0 * static_cast<double>(t);
        const std::size_t base = nodes.size();
        const auto handle = [&](std::size_t i) { return static_cast<NodeId::ValueType>(base + i + 1); };
        nodes.emplace_back(handle(0), at(dx, 0, 0));
        nodes.emplace_back(handle(1), at(dx + 1, 0, 0));
        nodes.emplace_back(handle(2), at(dx, 1, 0));
        nodes.emplace_back(handle(3), at(dx, 0, 1));
        tets.push_back({base, base + 1, base + 2, base + 3});
        triangles.push_back({base, base + 2, base + 1});
        triangles.push_back({base, base + 1, base + 3});
        triangles.push_back({base, base + 3, base + 2});
        triangles.push_back({base + 1, base + 2, base + 3});
    }
    return makeAsmHand(nodes, triangles, tets);
}

/// The boundary view of the hand mesh's TETRAHEDRA, so every facet names the tetrahedron that owns it.
[[nodiscard]] inline renderer::MeshView byTetrahedron(const meshing::Mesh& mesh) {
    Result<renderer::MeshView> view = renderer::MeshView::volumeBoundaryByTetrahedronOf(mesh);
    INFO((view.has_value() ? std::string{} : view.error().message));
    REQUIRE(view.has_value());
    return std::move(*view);
}

// ---------------------------------------------------------------------------
// A real document: the steel block, optionally finer
// ---------------------------------------------------------------------------

/// The block of the other thermal milestones (40 x 30 x 20 mm, steel), with a thermal analysis and a mesh. The
/// default reproduces the 12-tetrahedron mesh they use.
///
/// A PLANAR FACE CANNOT BE REFINED BY A GLOBAL TARGET (OCCT triangulates it with two triangles whatever the target --
/// P16-SIZE-001 measured it), so a finer mesh of this block comes from a LOCAL size on one side face, as P17's
/// fixture does: localSideSize refines side face 0 and, through it, the volume.
struct VizStudy {
    Document document{"Part"};
    features::Regenerator regenerator;
    meshing::Mesher mesher;
    ObjectId feature{};
    MeshControlId control{};
    ThermalAnalysisId analysis{};
    MaterialId material{};
    std::array<EntityId, 4> lines{};

    /// @p cylinder swaps the block for a cylinder of 6 mm radius and 20 mm length, whose curved wall DOES subdivide under a
    /// global target and a tight deflection (as P16-SIZE-001 measured), so its mesh has many boundary facets. Its wall is
    /// `side(0)`; its caps are the same two caps.
    explicit VizStudy(std::optional<Length> targetSize = std::nullopt, std::optional<Length> localSideSize = std::nullopt,
                      bool cylinder = false) {
        auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
        if (cylinder) {
            const Result<EntityId> circle = sketch->addCircle(Point2D{0_mm, 0_mm}, 6_mm);
            REQUIRE(circle.has_value());
            lines = {*circle, *circle, *circle, *circle};
        } else {
            lines = addRectangle(*sketch, 0_mm, 0_mm, 40_mm, 30_mm);
        }
        const ObjectId profile = require(document.addObject(std::move(sketch)));
        auto extrude = features::ExtrudeFeature::create("Solid", {.profile = SketchId::fromValue(profile.value()), .depth = 20_mm});
        REQUIRE(extrude.has_value());
        feature = require(document.addObject(std::move(*extrude)));

        meshing::MeshControlDefinition definition{.body = feature};
        if (targetSize.has_value()) {
            definition.mesh.sizing.globalTargetSize = *targetSize;
        }
        if (cylinder) {
            // The boundary triangulation is governed by deflection, not by the volume target.
            definition.mesh.surface.linearDeflection = Length::fromSi(5.0e-6);
            definition.mesh.surface.angularDeflection = Angle::fromSi(0.0872664626);   // 5 degrees
        }
        if (localSideSize.has_value()) {
            definition.mesh.sizing.local.push_back(meshing::LocalMeshSizing{
                .face = FaceName{feature, FaceSelector{.role = FaceRole::Side, .entity = lines.at(0)}}, .targetSize = *localSideSize});
        }
        auto intent = meshing::MeshControl::create("Mesh", definition);
        REQUIRE(intent.has_value());
        control = MeshControlId::fromValue(require(document.addObject(std::move(*intent))).value());

        features::MaterialDefinition steel;
        steel.designation = "Steel";
        steel.mechanical.density = materials::MaterialProperty<Density>::known(7850_kg_per_m3);
        steel.thermal.thermalConductivity = materials::MaterialProperty<ThermalConductivity>::known(45_W_per_m_K);
        steel.thermal.specificHeatCapacity = materials::MaterialProperty<SpecificHeatCapacity>::known(460_J_per_kg_K);
        const Result<MaterialId> id = features::createMaterial(document, "Steel", steel);
        REQUIRE(id.has_value());
        material = *id;
        REQUIRE(features::assignMaterial(document, material).has_value());

        auto study = thermal::ThermalAnalysis::create("Study", thermal::ThermalAnalysisDefinition{.mesh = control});
        REQUIRE(study.has_value());
        analysis = ThermalAnalysisId::fromValue(require(document.addObject(std::move(*study))).value());
        requireReport(regenerator, document);
        remesh();
    }

    void remesh() {
        const Result<const meshing::VolumeMesh*> mesh = mesher.generate(document, regenerator, control);
        INFO((mesh.has_value() ? std::string{} : mesh.error().message));
        REQUIRE(mesh.has_value());
    }

    [[nodiscard]] FaceName startCap() const { return FaceName{feature, FaceSelector{.role = FaceRole::StartCap}}; }
    [[nodiscard]] FaceName endCap() const { return FaceName{feature, FaceSelector{.role = FaceRole::EndCap}}; }
    [[nodiscard]] FaceName side(std::size_t which) const {
        return FaceName{feature, FaceSelector{.role = FaceRole::Side, .entity = lines.at(which)}};
    }

    [[nodiscard]] thermal::ThermalModel model() const {
        Result<thermal::ThermalModel> prepared = thermal::requireThermalModel(document, regenerator, mesher, control);
        INFO((prepared.has_value() ? std::string{} : prepared.error().message));
        REQUIRE(prepared.has_value());
        return std::move(*prepared);
    }

    [[nodiscard]] const meshing::VolumeMesh& volume() const {
        const meshing::VolumeMesh* held = mesher.mesh(control);
        REQUIRE(held != nullptr);
        return *held;
    }
    [[nodiscard]] const meshing::Mesh& mesh() const { return volume().mesh(); }
    [[nodiscard]] const meshing::GeometryMeshMap& map() const {
        const meshing::GeometryMeshMap* held = mesher.map(control);
        REQUIRE(held != nullptr);
        return *held;
    }

    [[nodiscard]] Result<bool> setDefinition(thermal::ThermalAnalysisDefinition definition) {
        return document.modifyObject<thermal::ThermalAnalysis>(
            ObjectId::fromValue(analysis.value()),
            [&definition](thermal::ThermalAnalysis& object) -> Result<bool> { return object.setDefinition(definition); });
    }

    [[nodiscard]] thermal::ThermalAnalysisDefinition definition() const {
        const thermal::ThermalAnalysis* object = thermal::findThermalAnalysis(document, analysis);
        REQUIRE(object != nullptr);
        return object->definition();
    }

    /// A physical edit that leaves the MESH CURRENT and moves the result's source (the material revision): a result of
    /// the old conductivity is stale on a mesh that still describes the model.
    void editConductivity(double value) {
        materials::ThermalProperties properties;
        properties.thermalConductivity = materials::MaterialProperty<ThermalConductivity>::known(ThermalConductivity::fromSi(value));
        properties.specificHeatCapacity = materials::MaterialProperty<SpecificHeatCapacity>::known(460_J_per_kg_K);
        REQUIRE(features::setMaterialThermal(document, material, properties).has_value());
    }
};

[[nodiscard]] inline ThermalLoad generationLoad(std::uint64_t id, double generation) {
    return ThermalLoad{ThermalLoadId::fromValue(id), VolumetricHeatSourceLoad{.generation = VolumetricHeatSource::fromSi(generation)}};
}

[[nodiscard]] inline ThermalLoad surfaceLoad(std::uint64_t id, const FaceName& face, double flux) {
    return ThermalLoad{ThermalLoadId::fromValue(id), SurfaceHeatFluxLoad{.face = face, .flux = HeatFlux::fromSi(flux)}};
}

[[nodiscard]] inline thermal::ThermalAnalysisDefinition steadyDefinition(const VizStudy& study, std::vector<ThermalLoad> loads,
                                                                         std::vector<ThermalBoundaryCondition> conditions) {
    return thermal::ThermalAnalysisDefinition{.mesh = study.control,
                                              .mode = ThermalAnalysisMode::Steady,
                                              .loads = std::move(loads),
                                              .boundaryConditions = std::move(conditions)};
}

[[nodiscard]] inline thermal::ThermalAnalysisDefinition
transientDefinition(const VizStudy& study, std::vector<ThermalLoad> loads, std::vector<ThermalBoundaryCondition> conditions,
                    double initialKelvin, double dt, double end,
                    thermal::TransientOutputPolicy output = thermal::TransientOutputPolicy::EveryStep) {
    return thermal::ThermalAnalysisDefinition{
        .mesh = study.control,
        .mode = ThermalAnalysisMode::Transient,
        .loads = std::move(loads),
        .boundaryConditions = std::move(conditions),
        .initialCondition = thermal::UniformInitialTemperature{Temperature::fromSi(initialKelvin)},
        .transient = thermal::TransientSettings{.timeStep = Time::fromSi(dt), .endTime = Time::fromSi(end), .output = output}};
}

inline void define(VizStudy& study, const thermal::ThermalAnalysisDefinition& definition) {
    const Result<bool> set = study.setDefinition(definition);
    INFO((set.has_value() ? std::string{} : set.error().message));
    REQUIRE(set.has_value());
}

/// The whole derived chain of one analysis state, as VALUES (no model is kept: a model is a borrowed snapshot that
/// must not outlive a remesh). Every step is the qualified function of its own milestone.
struct Derived {
    thermal::ThermalDofMap map;
    std::optional<PreparedThermalLoads> loads;
    std::optional<PreparedThermalBoundary> boundary;
    std::optional<thermal::ThermalSystem> system;
    std::optional<thermal::SteadyThermalSolution> steady;
    std::optional<thermal::TransientThermalSolution> transient;
    std::optional<thermal::ThermalPostResult> post;
    std::optional<thermal::ThermalBalanceResult> balance;
    std::string stoppedBecause;

    [[nodiscard]] thermal::ThermalDerivedState view() const {
        thermal::ThermalDerivedState state;
        state.dofMap = &map;
        state.preparedLoads = loads.has_value() ? &*loads : nullptr;
        state.preparedBoundary = boundary.has_value() ? &*boundary : nullptr;
        state.system = system.has_value() ? &*system : nullptr;
        state.steady = steady.has_value() ? &*steady : nullptr;
        state.transient = transient.has_value() ? &*transient : nullptr;
        state.post = post.has_value() ? &*post : nullptr;
        state.balance = balance.has_value() ? &*balance : nullptr;
        return state;
    }
};

[[nodiscard]] inline std::unique_ptr<Derived> derive(const VizStudy& study) {
    const thermal::ThermalAnalysisDefinition def = study.definition();
    const thermal::ThermalModel model = study.model();
    Result<thermal::ThermalDofMap> built = thermal::buildThermalDofMap(model.mesh().mesh());
    REQUIRE(built.has_value());
    auto out = std::make_unique<Derived>(Derived{.map = std::move(*built), .loads = {}, .boundary = {}, .system = {},
                                                 .steady = {}, .transient = {}, .post = {}, .balance = {}, .stoppedBecause = {}});
    const auto stop = [&out](const Error& error) { out->stoppedBecause = error.message; };

    Result<PreparedThermalLoads> loads = thermal::prepareThermalLoads(model, out->map, def);
    if (!loads.has_value()) {
        stop(loads.error());
        return out;
    }
    out->loads = std::move(*loads);
    Result<PreparedThermalBoundary> boundary = thermal::prepareThermalBoundary(model, out->map, def);
    if (!boundary.has_value()) {
        stop(boundary.error());
        return out;
    }
    out->boundary = std::move(*boundary);
    Result<thermal::ThermalSystem> system = thermal::assembleThermalSystem(study.document, model, out->map, *out->loads, *out->boundary, def);
    if (!system.has_value()) {
        stop(system.error());
        return out;
    }
    out->system = std::move(*system);

    if (def.mode == ThermalAnalysisMode::Steady) {
        Result<thermal::SteadyThermalSolution> solved = thermal::solveSteadyThermal(
            study.document, model, out->map, *out->system, *out->boundary, def.loads, def.boundaryConditions, def.solver);
        if (!solved.has_value()) {
            stop(solved.error());
            return out;
        }
        out->steady = std::move(*solved);
        Result<thermal::ThermalPostResult> post = thermal::recoverSteadyThermal(
            study.document, model, out->map, *out->steady, def.loads, def.boundaryConditions, def.solver);
        if (!post.has_value()) {
            stop(post.error());
            return out;
        }
        out->post = std::move(*post);
        Result<thermal::ThermalBalanceResult> balance = thermal::recoverSteadyBalance(
            study.document, model, out->map, *out->system, *out->steady, *out->loads, *out->boundary, def.loads,
            def.boundaryConditions, def.solver);
        if (!balance.has_value()) {
            stop(balance.error());
            return out;
        }
        out->balance = std::move(*balance);
    } else {
        Result<thermal::TransientThermalSolution> solved =
            thermal::solveTransientThermal(study.document, model, out->map, *out->system, *out->boundary, def);
        if (!solved.has_value()) {
            stop(solved.error());
            return out;
        }
        out->transient = std::move(*solved);
        Result<thermal::ThermalPostResult> post = thermal::recoverTransientThermal(
            study.document, model, out->map, *out->transient, def.loads, def.boundaryConditions, *def.initialCondition,
            *def.transient, def.solver);
        if (!post.has_value()) {
            stop(post.error());
            return out;
        }
        out->post = std::move(*post);
        Result<thermal::ThermalBalanceResult> balance = thermal::recoverTransientBalance(
            study.document, model, out->map, *out->system, *out->transient, *out->loads, *out->boundary, def.loads,
            def.boundaryConditions, *def.initialCondition, *def.transient, def.solver);
        if (!balance.has_value()) {
            stop(balance.error());
            return out;
        }
        out->balance = std::move(*balance);
    }
    return out;
}

/// The core's validation report for the state @p derived holds.
[[nodiscard]] inline thermal::ThermalValidationReport validationOf(const VizStudy& study, const Derived& derived) {
    return thermal::validateThermalAnalysis(study.document, study.regenerator, study.mesher, study.analysis, derived.view());
}

/// The display state of the study with the held @p derived result and optional @p report and @p failure.
[[nodiscard]] inline renderer::ThermalDisplayState displayStateOf(const VizStudy& study, const Derived* derived,
                                                                  const thermal::ThermalValidationReport* report = nullptr,
                                                                  const Error* failure = nullptr) {
    return renderer::thermalDisplayState(study.document, study.regenerator, study.mesher, study.analysis,
                                         derived != nullptr && derived->post.has_value() ? &*derived->post : nullptr, failure, report);
}

/// The block's slab conditions: 400 K on the start cap and 300 K on the end cap.
[[nodiscard]] inline std::vector<ThermalBoundaryCondition> slabConditions(const VizStudy& study) {
    return {temperatureBc(1, study.startCap(), 400.0), temperatureBc(2, study.endCap(), 300.0)};
}

/// A steady analysis with generation, a held cap and a convecting cap: several free unknowns, so the field varies.
inline void multiFree(VizStudy& study) {
    define(study, steadyDefinition(study, {generationLoad(1, 1.0e5)},
                                   {temperatureBc(1, study.startCap(), 300.0), convectionBc(2, study.endCap(), 25.0, kAmbient)}));
}

/// A test-side typed expectation: the norm of three components, written out. NOT the viewer's.
[[nodiscard]] inline double normOf(double x, double y, double z) { return std::sqrt(x * x + y * y + z * z); }

} // namespace vztest
