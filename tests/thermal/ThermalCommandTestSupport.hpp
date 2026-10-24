#pragma once

// P18-CMD-001 -- the fixtures and the independent oracles the thermal command tests share.
//
// THE ORACLES ARE THE TEST'S OWN. A canonical-intent projection that names every field it compares, and a
// fingerprint that hashes the exact bit patterns of those fields in a fixed order, are written here by hand: they
// are never obtained by asking the command, the definition's own `operator==` or a command's undo helper what the
// expected state is. The projection carries NO revision, no derived state and no mesh: BetterCAD's revisions are
// monotonic, so "undo restored the intent" is a statement about CONTENT, and the revision is checked separately and
// in the opposite direction.

#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Command.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/meshing/MeshControl.hpp>
#include <bettercad/sketch/Sketch.hpp>
#include <bettercad/thermal/ThermalAnalysisObject.hpp>
#include <bettercad/thermal/ThermalCommands.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cmdtest {

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using thermal::ConvectionBoundary;
using thermal::PrescribedTemperature;
using thermal::SurfaceHeatFluxLoad;
using thermal::ThermalAnalysis;
using thermal::ThermalAnalysisDefinition;
using thermal::ThermalAnalysisMode;
using thermal::ThermalBoundaryCondition;
using thermal::ThermalLoad;
using thermal::ThermalSolverSettings;
using thermal::TransientOutputPolicy;
using thermal::TransientSettings;
using thermal::UniformInitialTemperature;
using thermal::VolumetricHeatSourceLoad;

inline constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
inline constexpr double kInf = std::numeric_limits<double>::infinity();

// ---------------------------------------------------------------------------
// Records
// ---------------------------------------------------------------------------

[[nodiscard]] inline ThermalLoad surfaceLoad(std::uint64_t id, const FaceName& face, double flux) {
    return ThermalLoad{ThermalLoadId::fromValue(id),
                       SurfaceHeatFluxLoad{.face = face, .flux = HeatFlux::fromSi(flux)}};
}

[[nodiscard]] inline ThermalLoad generationLoad(std::uint64_t id, double generation) {
    return ThermalLoad{ThermalLoadId::fromValue(id),
                       VolumetricHeatSourceLoad{.generation = VolumetricHeatSource::fromSi(generation)}};
}

[[nodiscard]] inline ThermalBoundaryCondition heldTemperature(std::uint64_t id, const FaceName& face,
                                                              double kelvin) {
    return ThermalBoundaryCondition{ThermalBoundaryConditionId::fromValue(id),
                                    PrescribedTemperature{.face = face, .value = Temperature::fromSi(kelvin)}};
}

[[nodiscard]] inline ThermalBoundaryCondition convecting(std::uint64_t id, const FaceName& face, double h,
                                                         double ambientKelvin) {
    return ThermalBoundaryCondition{
        ThermalBoundaryConditionId::fromValue(id),
        ConvectionBoundary{.face = face,
                           .coefficient = HeatTransferCoefficient::fromSi(h),
                           .ambient = Temperature::fromSi(ambientKelvin)}};
}

[[nodiscard]] inline TransientSettings transientOf(double dt, double end,
                                                   TransientOutputPolicy output = TransientOutputPolicy::FinalOnly) {
    return TransientSettings{.timeStep = Time::fromSi(dt), .endTime = Time::fromSi(end), .output = output};
}

// ---------------------------------------------------------------------------
// The independent oracles
// ---------------------------------------------------------------------------

/// A hash of exact bit patterns in a fixed order. Discrete values are mixed as integers and continuous ones by their
/// bits, so two states agree only if they agree exactly.
struct Fnv {
    std::uint64_t hash = 14695981039346656037ULL;

    void word(std::uint64_t value) {
        for (unsigned shift = 0U; shift < 64U; shift += 8U) {
            hash ^= (value >> shift) & 0xFFU;
            hash *= 1099511628211ULL;
        }
    }
    void real(double value) { word(std::bit_cast<std::uint64_t>(value)); }
    void flag(bool value) { word(value ? 1U : 0U); }
};

inline void mix(Fnv& f, const FaceName& face) {
    f.word(face.feature.value());
    f.word(static_cast<std::uint64_t>(face.face.role));
    const auto optionalWord = [&f](bool present, std::uint64_t value) {
        f.flag(present);
        f.word(present ? value : 0U);
    };
    optionalWord(face.face.entity.has_value(), face.face.entity ? face.face.entity->value() : 0U);
    optionalWord(face.face.along.has_value(), face.face.along ? face.face.along->value() : 0U);
    optionalWord(face.face.alongSketch.has_value(), face.face.alongSketch ? face.face.alongSketch->value() : 0U);
    optionalWord(face.face.edge.has_value(), face.face.edge ? face.face.edge->value() : 0U);
    f.word(face.face.copies.size());
    for (const FaceCopy& copy : face.face.copies) {
        f.word(copy.feature.value());
        f.word(copy.instance);
    }
}

/// The fingerprint of the canonical thermal intent: every field, in a fixed order, by exact bits. Carries no revision,
/// no mesh, no derived state, no pointer, no timestamp and no iteration order.
[[nodiscard]] inline std::uint64_t fingerprintOf(const ThermalAnalysisDefinition& definition) {
    Fnv f;
    f.word(definition.mesh.value());
    f.word(static_cast<std::uint64_t>(definition.mode));
    f.word(definition.loads.size());
    for (const ThermalLoad& load : definition.loads) {
        f.word(load.id().value());
        f.word(static_cast<std::uint64_t>(load.kind()));
        if (const SurfaceHeatFluxLoad* surface = load.surfaceHeatFlux()) {
            mix(f, surface->face);
            f.real(surface->flux.si());
        }
        if (const VolumetricHeatSourceLoad* volume = load.volumetricHeatSource()) {
            f.real(volume->generation.si());
        }
    }
    f.word(definition.boundaryConditions.size());
    for (const ThermalBoundaryCondition& condition : definition.boundaryConditions) {
        f.word(condition.id().value());
        f.word(static_cast<std::uint64_t>(condition.kind()));
        mix(f, condition.face());
        if (const PrescribedTemperature* held = condition.prescribedTemperature()) {
            f.real(held->value.si());
        }
        if (const ConvectionBoundary* convection = condition.convection()) {
            f.real(convection->coefficient.si());
            f.real(convection->ambient.si());
        }
    }
    f.flag(definition.initialCondition.has_value());
    f.real(definition.initialCondition ? definition.initialCondition->value.si() : 0.0);
    f.real(definition.solver.relativeResidualTolerance);
    f.real(definition.solver.pivotFloor);
    f.flag(definition.transient.has_value());
    f.real(definition.transient ? definition.transient->timeStep.si() : 0.0);
    f.real(definition.transient ? definition.transient->endTime.si() : 0.0);
    f.word(definition.transient ? static_cast<std::uint64_t>(definition.transient->output) : 0U);
    return f.hash;
}

/// An independent, field-naming projection of the canonical intent, for comparisons that should print their members.
struct CanonicalIntent {
    MeshControlId mesh{};
    ThermalAnalysisMode mode{};
    std::vector<ThermalLoad> loads{};
    std::vector<ThermalBoundaryCondition> conditions{};
    std::optional<UniformInitialTemperature> initial{};
    ThermalSolverSettings solver{};
    std::optional<TransientSettings> transient{};

    explicit CanonicalIntent(const ThermalAnalysisDefinition& definition)
        : mesh(definition.mesh), mode(definition.mode), loads(definition.loads),
          conditions(definition.boundaryConditions), initial(definition.initialCondition),
          solver(definition.solver), transient(definition.transient) {}

    friend bool operator==(const CanonicalIntent&, const CanonicalIntent&) = default;
};

// ---------------------------------------------------------------------------
// A document with a body, a meshing control and one analysis -- and no mesh, no solve
// ---------------------------------------------------------------------------

/// Every canonical, undo, redo and atomicity property is about the DOCUMENT, and generating a mesh for each would make
/// the suite slow for no added coverage. The cases that need a mesh and a solve use `vztest::VizStudy` and say so.
struct CmdPart {
    Document document{"Part"};
    features::Regenerator regenerator;
    CommandHistory history;
    ObjectId feature{};
    MeshControlId control{};
    ThermalAnalysisId analysis{};
    std::array<EntityId, 4> lines{};

    explicit CmdPart(ThermalAnalysisMode mode = ThermalAnalysisMode::Steady) {
        auto profile = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
        lines = addRectangle(*profile, 0_mm, 0_mm, 40_mm, 30_mm);
        const ObjectId sketchId = require(document.addObject(std::move(profile)));
        auto extrude = features::ExtrudeFeature::create(
            "Solid", {.profile = SketchId::fromValue(sketchId.value()), .depth = 20_mm});
        REQUIRE(extrude.has_value());
        feature = require(document.addObject(std::move(*extrude)));

        meshing::MeshControlDefinition controls;
        controls.body = feature;
        controls.mesh.sizing.globalTargetSize = 12_mm;
        auto meshControl = meshing::MeshControl::create("Mesh", controls);
        REQUIRE(meshControl.has_value());
        control = MeshControlId::fromValue(require(document.addObject(std::move(*meshControl))).value());

        ThermalAnalysisDefinition seed{.mesh = control, .mode = mode};
        if (mode == ThermalAnalysisMode::Transient) {
            seed.initialCondition = UniformInitialTemperature{Temperature::fromSi(300.0)};
            seed.transient = transientOf(0.3, 1.0);
        }
        auto study = ThermalAnalysis::create("Study", seed);
        REQUIRE(study.has_value());
        analysis = ThermalAnalysisId::fromValue(require(document.addObject(std::move(*study))).value());
        requireReport(regenerator, document);
    }

    [[nodiscard]] FaceName startCap() const { return FaceName{feature, FaceSelector{.role = FaceRole::StartCap}}; }
    [[nodiscard]] FaceName endCap() const { return FaceName{feature, FaceSelector{.role = FaceRole::EndCap}}; }
    [[nodiscard]] FaceName side(std::size_t which = 0) const {
        return FaceName{feature, FaceSelector{.role = FaceRole::Side, .entity = lines.at(which)}};
    }

    [[nodiscard]] const ThermalAnalysis& object() const {
        const ThermalAnalysis* held = thermal::findThermalAnalysis(document, analysis);
        REQUIRE(held != nullptr);
        return *held;
    }
    [[nodiscard]] const ThermalAnalysisDefinition& definition() const { return object().definition(); }
    [[nodiscard]] std::uint64_t revision() const { return object().revision(); }
    [[nodiscard]] std::uint64_t fingerprint() const { return fingerprintOf(definition()); }

    /// Runs @p C through the history, so the undo/redo stacks are the real ones and not a hand-driven imitation.
    template <typename C, typename... Args>
    [[nodiscard]] Result<void> run(Args&&... args) {
        return history.execute(document, std::make_unique<C>(std::forward<Args>(args)...));
    }
};

/// Everything a refused command must leave exactly as it found it.
struct Snapshot {
    std::uint64_t intent{};
    std::uint64_t analysisRevision{};
    std::uint64_t documentRevision{};
    std::size_t objects{};
    std::size_t undoDepth{};
    std::size_t redoDepth{};

    friend bool operator==(const Snapshot&, const Snapshot&) = default;
};

[[nodiscard]] inline Snapshot snapshotOf(const CmdPart& part) {
    return Snapshot{.intent = part.fingerprint(),
                    .analysisRevision = part.revision(),
                    .documentRevision = part.document.revision(),
                    .objects = part.document.objectCount(),
                    .undoDepth = part.history.undoCount(),
                    .redoDepth = part.history.redoCount()};
}

/// The face references a command must carry unchanged: every selector form the data model validates, and one naming a
/// feature that does not exist (an UNRESOLVED reference, which is still valid canonical intent).
[[nodiscard]] inline std::vector<FaceName> everyFaceForm(ObjectId feature) {
    std::vector<FaceName> faces;
    faces.push_back(FaceName{feature, FaceSelector{.role = FaceRole::StartCap}});
    faces.push_back(FaceName{feature, FaceSelector{.role = FaceRole::EndCap}});
    faces.push_back(FaceName{feature, FaceSelector{.role = FaceRole::Side, .entity = EntityId::fromValue(1)}});
    faces.push_back(FaceName{feature, FaceSelector{.role = FaceRole::Side,
                                                   .entity = EntityId::fromValue(2),
                                                   .along = EntityId::fromValue(3)}});
    faces.push_back(FaceName{feature, FaceSelector{.role = FaceRole::Side,
                                                   .entity = EntityId::fromValue(2),
                                                   .along = EntityId::fromValue(3),
                                                   .alongSketch = SketchId::fromValue(4)}});
    faces.push_back(FaceName{feature, FaceSelector{.role = FaceRole::Chamfer, .edge = ChamferEdgeId::fromValue(5)}});
    faces.push_back(FaceName{feature, FaceSelector{.role = FaceRole::EndCap,
                                                   .copies = {FaceCopy{ObjectId::fromValue(9), 1},
                                                              FaceCopy{ObjectId::fromValue(10), 2}}}});
    faces.push_back(FaceName{ObjectId::fromValue(987654),
                             FaceSelector{.role = FaceRole::Side, .entity = EntityId::fromValue(77)}});
    return faces;
}

} // namespace cmdtest
