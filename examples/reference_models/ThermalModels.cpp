#include "BuildSupport.hpp"
#include "ThermalReferenceModels.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/math/Direction.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/meshing/MeshQuality.hpp>
#include <bettercad/meshing/MeshSizing.hpp>
#include <bettercad/meshing/VolumeMesh.hpp>
#include <bettercad/thermal/ThermalAnalysisObject.hpp>
#include <bettercad/thermal/ThermalData.hpp>

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace bettercad::reference {

using namespace bettercad::literals;
using detail::sketchId;
using meshing::LocalMeshSizing;
using meshing::MeshControl;
using meshing::MeshControlDefinition;
using meshing::NamedBoundarySet;
using meshing::VolumeMeshControls;
using thermal::ThermalAnalysis;
using thermal::ThermalAnalysisDefinition;
using thermal::ThermalAnalysisMode;
using thermal::ThermalBoundaryCondition;
using thermal::ThermalLoad;

namespace {

constexpr std::string_view kSynthetic =
    "TEST / SYNTHETIC ENGINEERING DATA. Round, physically plausible values chosen so that "
    "closed-form validation is exact. Not sourced datasheet figures.";

/// A rectangle (0,0)..(w,d) on a plane, fully constrained, returning its four lines in order: y = 0, x = w,
/// y = d, x = 0. The same helper the structural suite restates, for the same reason: a side face is named by the
/// entity that swept it, so a model that cannot say which line is which cannot name its own faces.
struct Rectangle {
    std::array<EntityId, 4> lines{};
    EntityId origin{};
};

Rectangle rectangleProfile(detail::SketchBuilder& profile, double wMm, double dMm, ParameterId width,
                           ParameterId depth) {
    Rectangle rectangle;
    const EntityId origin = profile.point(0.0, 0.0);
    const EntityId alongX = profile.point(wMm, 0.0);
    const EntityId corner = profile.point(wMm, dMm);
    const EntityId alongY = profile.point(0.0, dMm);
    rectangle.origin = origin;
    rectangle.lines[0] = profile.line(origin, alongX);
    rectangle.lines[1] = profile.line(alongX, corner);
    rectangle.lines[2] = profile.line(corner, alongY);
    rectangle.lines[3] = profile.line(alongY, origin);
    profile.fixed(origin);
    profile.horizontal(rectangle.lines[0]);
    profile.horizontal(rectangle.lines[2]);
    profile.vertical(rectangle.lines[1]);
    profile.vertical(rectangle.lines[3]);
    profile.length(rectangle.lines[0], width);
    profile.length(rectangle.lines[1], depth);
    return rectangle;
}

features::MaterialDefinition syntheticMaterial(const ThermalMaterialSpec& spec, std::string designation) {
    using materials::MaterialProperty;
    features::MaterialDefinition d;
    d.designation = std::move(designation);
    d.family = "Synthetic";
    d.notes = std::string{kSynthetic};
    d.mechanical.density = MaterialProperty<Density>::known(Density::fromSi(spec.density));
    d.thermal.thermalConductivity = MaterialProperty<ThermalConductivity>::known(ThermalConductivity::fromSi(spec.conductivity));
    d.thermal.specificHeatCapacity =
        MaterialProperty<SpecificHeatCapacity>::known(SpecificHeatCapacity::fromSi(spec.specificHeat));
    return d;
}

[[nodiscard]] NamedBoundarySet region(std::uint64_t id, std::string name, FaceName face) {
    return NamedBoundarySet{.id = BoundarySetId::fromValue(id), .name = std::move(name), .faces = {std::move(face)}};
}

[[nodiscard]] ThermalBoundaryCondition held(std::uint64_t id, FaceName face, double kelvin) {
    return ThermalBoundaryCondition{ThermalBoundaryConditionId::fromValue(id),
                                    thermal::PrescribedTemperature{.face = std::move(face), .value = Temperature::fromSi(kelvin)}};
}

[[nodiscard]] ThermalBoundaryCondition convecting(std::uint64_t id, FaceName face, double h, double ambientKelvin) {
    return ThermalBoundaryCondition{
        ThermalBoundaryConditionId::fromValue(id),
        thermal::ConvectionBoundary{.face = std::move(face),
                                    .coefficient = HeatTransferCoefficient::fromSi(h),
                                    .ambient = Temperature::fromSi(ambientKelvin)}};
}

[[nodiscard]] ThermalLoad generation(std::uint64_t id, double wattsPerCubicMetre) {
    return ThermalLoad{ThermalLoadId::fromValue(id),
                       thermal::VolumetricHeatSourceLoad{.generation = VolumetricHeatSource::fromSi(wattsPerCubicMetre)}};
}

[[nodiscard]] ThermalLoad surfaceFlux(std::uint64_t id, FaceName face, double wattsPerSquareMetre) {
    return ThermalLoad{ThermalLoadId::fromValue(id),
                       thermal::SurfaceHeatFluxLoad{.face = std::move(face), .flux = HeatFlux::fromSi(wattsPerSquareMetre)}};
}

/// What differs between the models. The shape of the document (a sketch, an extrusion, a material, a mesh control,
/// one analysis) is the same for all of them and lives in `buildBlock`.
struct BlockSpec {
    std::string_view uuid;
    std::string name;
    /// The prefix of the three parameters: `<prefix>_a`, `<prefix>_b`, `<prefix>_l`.
    std::string prefix;
    double aMm = 0.0;
    double bMm = 0.0;
    double depthMm = 0.0;
    std::string_view solidName = "Block";
    ThermalMaterialSpec material{};
    Length globalTarget = 20_mm;
    /// A local size on EVERY face (both caps and the four sides): the mesh levels of RM-THERM-02 and RM-THERM-04. A local size makes a
    /// slab ONE TARGET DEEP beside its face, so only a size on all six faces lets the slabs meet in a thin body.
    std::optional<Length> sideTarget{};
    /// A local size on one cap (RM-THERM-07).
    std::optional<std::pair<bool, Length>> capTarget{}; // {onEndCap, size}
    bool placed = false;
    /// Fills the analysis definition once the faces are known (everything but the mesh control id).
    std::function<void(const ThermalBlockModel&, ThermalAnalysisDefinition&)> intent;
};

Result<ThermalBlockModel> buildBlock(const BlockSpec& spec) {
    ThermalBlockModel m{.document = Document(detail::fixedDocumentId(spec.uuid), spec.name), .placed = spec.placed};
    detail::ModelBuilder b(m.document);

    m.a = b.length(spec.prefix + "_a", spec.aMm);
    m.b = b.length(spec.prefix + "_b", spec.bMm);
    m.depth = b.length(spec.prefix + "_l", spec.depthMm);

    Frame3D plane = Frame3D::xy();
    if (spec.placed) {
        auto frame = thermalTransformedFrame();
        if (!frame) {
            return std::unexpected(frame.error());
        }
        plane = *frame;
    }

    detail::SketchBuilder profile(b, spec.name + "Sketch", plane);
    const Rectangle rectangle = rectangleProfile(profile, spec.aMm, spec.bMm, m.a, m.b);
    m.lines = rectangle.lines;
    m.sketch = profile.finish();

    m.solid = b.feature<features::ExtrudeFeature>(
        std::string{spec.solidName},
        {.profile = sketchId(m.sketch), .depth = Length::fromSi(spec.depthMm * 1.0e-3), .depthParameter = m.depth});

    const MaterialId material =
        b.need(features::createMaterial(m.document, "Material", syntheticMaterial(spec.material, spec.name + " material")),
               "Material");
    b.need(features::assignMaterial(m.document, material), "material assignment");

    VolumeMeshControls controls;
    controls.sizing.globalTargetSize = spec.globalTarget;
    if (spec.sideTarget.has_value()) {
        for (std::size_t i = 0; i < 4; ++i) {
            controls.sizing.local.push_back(LocalMeshSizing{.face = m.side(i), .targetSize = *spec.sideTarget});
        }
        controls.sizing.local.push_back(LocalMeshSizing{.face = m.start(), .targetSize = *spec.sideTarget});
        controls.sizing.local.push_back(LocalMeshSizing{.face = m.end(), .targetSize = *spec.sideTarget});
    }
    if (spec.capTarget.has_value()) {
        controls.sizing.local.push_back(
            LocalMeshSizing{.face = spec.capTarget->first ? m.end() : m.start(), .targetSize = spec.capTarget->second});
    }
    MeshControlDefinition definition;
    definition.body = m.solid;
    definition.mesh = controls;
    definition.quality = meshing::reportOnlyThresholds();
    definition.boundarySets = {region(1U, "start_cap", m.start()), region(2U, "end_cap", m.end())};
    m.control = b.feature<MeshControl>("Mesh", definition);

    ThermalAnalysisDefinition analysis;
    analysis.mesh = MeshControlId::fromValue(m.control.value());
    spec.intent(m, analysis);
    auto created = ThermalAnalysis::create(std::string{kThermalStudyName}, std::move(analysis));
    if (!created) {
        (void)b.need(Result<ObjectId>{std::unexpected(created.error())}, kThermalStudyName);
    } else {
        m.analysis = b.add(std::move(*created), kThermalStudyName);
    }

    if (auto status = b.status(); !status) {
        return std::unexpected(status.error());
    }
    return m;
}

[[nodiscard]] thermal::UniformInitialTemperature initialAt(double kelvin) {
    return thermal::UniformInitialTemperature{Temperature::fromSi(kelvin)};
}

[[nodiscard]] thermal::TransientSettings everyStep(double stepSeconds, double endSeconds) {
    return thermal::TransientSettings{.timeStep = Time::fromSi(stepSeconds),
                                      .endTime = Time::fromSi(endSeconds),
                                      .output = thermal::TransientOutputPolicy::EveryStep};
}

/// The slab-type intent shared by the steady models: the start cap held hot, the end cap held cold.
void heldCaps(const ThermalBlockModel& m, ThermalAnalysisDefinition& d, double startKelvin, double endKelvin) {
    d.mode = ThermalAnalysisMode::Steady;
    d.boundaryConditions = {held(1U, m.start(), startKelvin), held(2U, m.end(), endKelvin)};
}

} // namespace

// --- RM-THERM-01 -------------------------------------------------------------

Result<ThermalBlockModel> buildThermalPatchReferenceModel() {
    return buildBlock(BlockSpec{.uuid = "18de0001-0000-4000-8000-000000000001",
                                .name = "ThermalPatch",
                                .prefix = "patch",
                                .aMm = 40.0,
                                .bMm = 30.0,
                                .depthMm = 20.0,
                                .solidName = "PatchBlock",
                                .material = kPatchMaterial,
                                .globalTarget = 20_mm,
                                .intent = [](const ThermalBlockModel& m, ThermalAnalysisDefinition& d) {
                                    heldCaps(m, d, 400.0, 300.0);
                                }});
}

// --- RM-THERM-02 -------------------------------------------------------------

Result<ThermalBlockModel> buildThermalSlabReferenceModel(std::optional<Length> faceTarget) {
    return buildBlock(BlockSpec{.uuid = "18de0002-0000-4000-8000-000000000002",
                                .name = "ThermalSlab",
                                .prefix = "slab",
                                .aMm = 500.0,
                                .bMm = 500.0,
                                .depthMm = 1000.0,
                                .solidName = "Slab",
                                .material = kSlabMaterial,
                                .globalTarget = 500_mm,
                                .sideTarget = faceTarget,
                                .intent = [](const ThermalBlockModel& m, ThermalAnalysisDefinition& d) {
                                    heldCaps(m, d, kSlabHotKelvin, kSlabColdKelvin);
                                }});
}

// --- RM-THERM-04 -------------------------------------------------------------

Result<ThermalBlockModel> buildThermalGenerationReferenceModel(std::optional<Length> faceTarget) {
    return buildBlock(BlockSpec{.uuid = "18de0004-0000-4000-8000-000000000004",
                                .name = "ThermalGeneration",
                                .prefix = "gen",
                                .aMm = 80.0,
                                .bMm = 80.0,
                                .depthMm = 160.0,
                                .solidName = "GenerationSlab",
                                .material = kGenerationMaterial,
                                .globalTarget = 250_mm,
                                .sideTarget = faceTarget,
                                .intent = [](const ThermalBlockModel& m, ThermalAnalysisDefinition& d) {
                                    heldCaps(m, d, kGenerationFaceKelvin, kGenerationFaceKelvin);
                                    d.loads = {generation(1U, kGenerationWattsPerM3)};
                                }});
}

Result<ThermalTubeModel> buildThermalTubeReferenceModel(Length linearDeflection, Length globalTarget,
                                                         std::optional<Length> outerWallTarget) {
    ThermalTubeModel m{.document = Document(detail::fixedDocumentId("18de0004-0000-4000-8000-000000000020"), "ThermalTube")};
    detail::ModelBuilder b(m.document);

    m.outerRadius = b.length("tube_ro", kTubeOuterRadiusMm);
    m.innerRadius = b.length("tube_ri", kTubeInnerRadiusMm);
    m.length = b.length("tube_l", kTubeLengthMm);

    detail::SketchBuilder profile(b, "TubeSketch", Frame3D::xy());
    const EntityId centre = profile.point(0.0, 0.0);
    m.outerRim = profile.circle(centre, kTubeOuterRadiusMm);
    m.innerRim = profile.circle(centre, kTubeInnerRadiusMm);
    profile.fixed(centre);
    profile.radius(m.outerRim, m.outerRadius);
    profile.radius(m.innerRim, m.innerRadius);
    m.sketch = profile.finish();

    m.solid = b.feature<features::ExtrudeFeature>(
        "Tube", {.profile = sketchId(m.sketch), .depth = Length::fromSi(kTubeLengthMm * 1.0e-3), .depthParameter = m.length});

    const MaterialId material = b.need(
        features::createMaterial(m.document, "Material", syntheticMaterial(kTubeMaterial, "ThermalTube material")), "Material");
    b.need(features::assignMaterial(m.document, material), "material assignment");

    VolumeMeshControls controls;
    controls.sizing.globalTargetSize = globalTarget;
    controls.surface.linearDeflection = linearDeflection;
    // The angular limit at the surface layer's own default (20 degrees).
    controls.surface.angularDeflection = Angle::fromSi(0.3490658503988659);
    if (outerWallTarget.has_value()) {
        controls.sizing.local.push_back(LocalMeshSizing{.face = m.outerWall(), .targetSize = *outerWallTarget});
    }
    MeshControlDefinition definition;
    definition.body = m.solid;
    definition.mesh = controls;
    definition.quality = meshing::reportOnlyThresholds();
    definition.boundarySets = {region(1U, "outer_wall", m.outerWall()), region(2U, "inner_wall", m.innerWall())};
    m.control = b.feature<MeshControl>("Mesh", definition);

    ThermalAnalysisDefinition analysis;
    analysis.mesh = MeshControlId::fromValue(m.control.value());
    analysis.mode = ThermalAnalysisMode::Steady;
    // The outer wall is held; the inner wall and both annular caps are left natural, which is insulated.
    analysis.boundaryConditions = {held(1U, m.outerWall(), kTubeOuterKelvin)};
    analysis.loads = {generation(1U, kTubeGenerationWattsPerM3)};
    auto created = ThermalAnalysis::create(std::string{kThermalStudyName}, std::move(analysis));
    if (!created) {
        (void)b.need(Result<ObjectId>{std::unexpected(created.error())}, kThermalStudyName);
    } else {
        m.analysis = b.add(std::move(*created), kThermalStudyName);
    }
    if (auto status = b.status(); !status) {
        return std::unexpected(status.error());
    }
    return m;
}

// --- RM-THERM-05 -------------------------------------------------------------

Result<ThermalBlockModel> buildThermalConvectionReferenceModel() {
    return buildBlock(BlockSpec{.uuid = "18de0005-0000-4000-8000-000000000005",
                                .name = "ThermalConvection",
                                .prefix = "conv",
                                .aMm = 400.0,
                                .bMm = 500.0,
                                .depthMm = 500.0,
                                .solidName = "ConvectionSlab",
                                .material = kConvectionMaterial,
                                .globalTarget = 250_mm,
                                .intent = [](const ThermalBlockModel& m, ThermalAnalysisDefinition& d) {
                                    d.mode = ThermalAnalysisMode::Steady;
                                    d.boundaryConditions = {held(1U, m.start(), kConvectionHotKelvin),
                                                            convecting(2U, m.end(), kConvectionCoefficient,
                                                                       kConvectionAmbientKelvin)};
                                }});
}

// --- RM-THERM-06 -------------------------------------------------------------

Result<Frame3D> thermalTransformedFrame() {
    const RigidPlacement placement = thermalTransformedPlacement();
    // fromUnitComponents and fromAxes STORE the components unchanged after checking them within 1e-12, so the frame
    // holds exactly the thirds the suite's own transform uses -- the same reasoning as RM-MESH-06.
    auto direction = [](const std::array<double, 3>& v) { return Direction3D::fromUnitComponents(v[0], v[1], v[2]); };
    const auto x = direction(placement.xAxis);
    const auto y = direction(placement.yAxis);
    const auto normal = direction(placement.normal);
    if (!x || !y || !normal) {
        return makeError(ErrorCode::InvalidArgument, "RM-THERM-06's triad is not unit-length to within 1e-12");
    }
    return Frame3D::fromAxes(detail::pointMm(placement.origin[0], placement.origin[1], placement.origin[2]), *x, *y,
                             *normal);
}

namespace {

Result<ThermalBlockModel> buildTransformed(bool placed) {
    return buildBlock(BlockSpec{.uuid = placed ? "18de0006-0000-4000-8000-000000000007" : "18de0006-0000-4000-8000-000000000006",
                                .name = placed ? "ThermalTransformedPlaced" : "ThermalTransformedBase",
                                .prefix = placed ? "tplaced" : "tbase",
                                .aMm = 90.0,
                                .bMm = 55.0,
                                .depthMm = 24.0,
                                .solidName = placed ? "PlacedBlock" : "BaseBlock",
                                .material = kTransformedMaterial,
                                .globalTarget = 16_mm,
                                .placed = placed,
                                .intent = [](const ThermalBlockModel& m, ThermalAnalysisDefinition& d) {
                                    heldCaps(m, d, 400.0, 300.0);
                                }});
}

} // namespace

Result<ThermalBlockModel> buildThermalTransformedBaseReferenceModel() { return buildTransformed(false); }
Result<ThermalBlockModel> buildThermalTransformedPlacedReferenceModel() { return buildTransformed(true); }

// --- RM-THERM-07 -------------------------------------------------------------

Result<ThermalBlockModel> buildThermalLocalRefinementReferenceModel(ThermalLocalRefinement refined) {
    const char* uuid = refined == ThermalLocalRefinement::None    ? "18de0007-0000-4000-8000-000000000008"
                       : refined == ThermalLocalRefinement::Start ? "18de0007-0000-4000-8000-000000000009"
                                                                  : "18de0007-0000-4000-8000-000000000010";
    const char* name = refined == ThermalLocalRefinement::None    ? "ThermalLocalBaseline"
                       : refined == ThermalLocalRefinement::Start ? "ThermalLocalTarget"
                                                                  : "ThermalLocalMirror";
    const char* prefix = refined == ThermalLocalRefinement::None ? "locb" : refined == ThermalLocalRefinement::Start ? "loct" : "locm";
    BlockSpec spec{.uuid = uuid,
                   .name = name,
                   .prefix = prefix,
                   .aMm = 40.0,
                   .bMm = 60.0,
                   .depthMm = 100.0,
                   .solidName = "LocalBeam",
                   .material = kLocalMaterial,
                   .globalTarget = 20_mm,
                   .intent = [](const ThermalBlockModel& m, ThermalAnalysisDefinition& d) {
                       heldCaps(m, d, kLocalCapKelvin, kLocalCapKelvin);
                       d.loads = {generation(1U, kLocalGenerationWattsPerM3)};
                   }};
    if (refined != ThermalLocalRefinement::None) {
        spec.capTarget = std::pair<bool, Length>{refined == ThermalLocalRefinement::End, Length::fromSi(kLocalSizeMm * 1.0e-3)};
    }
    return buildBlock(spec);
}

// --- RM-THERM-08 -------------------------------------------------------------

Result<ThermalBlockModel> buildThermalLumpedReferenceModel(double timeStepSeconds, double endSeconds) {
    return buildBlock(BlockSpec{.uuid = "18de0008-0000-4000-8000-000000000011",
                                .name = "ThermalLumped",
                                .prefix = "lump",
                                .aMm = kLumpedSideMm,
                                .bMm = kLumpedSideMm,
                                .depthMm = kLumpedSideMm,
                                .solidName = "Cube",
                                .material = kLumpedMaterial,
                                .globalTarget = 20_mm,
                                .intent = [timeStepSeconds, endSeconds](const ThermalBlockModel& m, ThermalAnalysisDefinition& d) {
                                    d.mode = ThermalAnalysisMode::Transient;
                                    // ALL SIX faces convect, so the exchange area is the whole surface and Lc = V/A.
                                    d.boundaryConditions = {
                                        convecting(1U, m.start(), kLumpedCoefficient, kLumpedAmbientKelvin),
                                        convecting(2U, m.end(), kLumpedCoefficient, kLumpedAmbientKelvin),
                                        convecting(3U, m.side(0), kLumpedCoefficient, kLumpedAmbientKelvin),
                                        convecting(4U, m.side(1), kLumpedCoefficient, kLumpedAmbientKelvin),
                                        convecting(5U, m.side(2), kLumpedCoefficient, kLumpedAmbientKelvin),
                                        convecting(6U, m.side(3), kLumpedCoefficient, kLumpedAmbientKelvin)};
                                    d.initialCondition = initialAt(kLumpedInitialKelvin);
                                    d.transient = everyStep(timeStepSeconds, endSeconds);
                                }});
}

Result<ThermalBlockModel> buildThermalInsulatedGenerationReferenceModel(double timeStepSeconds, double endSeconds) {
    return buildBlock(BlockSpec{.uuid = "18de0008-0000-4000-8000-000000000012",
                                .name = "ThermalInsulatedGeneration",
                                .prefix = "ins",
                                .aMm = kLumpedSideMm,
                                .bMm = kLumpedSideMm,
                                .depthMm = kLumpedSideMm,
                                .solidName = "Cube",
                                .material = kLumpedMaterial,
                                .globalTarget = 20_mm,
                                .intent = [timeStepSeconds, endSeconds](const ThermalBlockModel&, ThermalAnalysisDefinition& d) {
                                    d.mode = ThermalAnalysisMode::Transient;
                                    d.loads = {generation(1U, kInsulatedGenerationWattsPerM3)};
                                    d.initialCondition = initialAt(kInsulatedInitialKelvin);
                                    d.transient = everyStep(timeStepSeconds, endSeconds);
                                }});
}

// --- RM-THERM-02 (surface heat flux) and RM-THERM-08 (held cap, two convections) ---

Result<ThermalBlockModel> buildThermalSurfaceFluxReferenceModel() {
    return buildBlock(BlockSpec{.uuid = "18de0002-0000-4000-8000-000000000019",
                                .name = "ThermalSurfaceFlux",
                                .prefix = "sf",
                                .aMm = 50.0,
                                .bMm = 50.0,
                                .depthMm = 100.0,
                                .solidName = "FluxBlock",
                                .material = kPatchMaterial,
                                .globalTarget = 50_mm,
                                .intent = [](const ThermalBlockModel& m, ThermalAnalysisDefinition& d) {
                                    d.mode = ThermalAnalysisMode::Steady;
                                    d.boundaryConditions = {held(1U, m.start(), kSurfaceFluxHeldKelvin)};
                                    d.loads = {surfaceFlux(1U, m.end(), kSurfaceFluxWattsPerM2)};
                                }});
}

Result<ThermalBlockModel> buildThermalTransientSlabReferenceModel() {
    return buildBlock(BlockSpec{.uuid = "18de0008-0000-4000-8000-000000000020",
                                .name = "ThermalTransientSlab",
                                .prefix = "ts",
                                .aMm = 40.0,
                                .bMm = 40.0,
                                .depthMm = 100.0,
                                .solidName = "TransientSlab",
                                .material = kTransientSlabMaterial,
                                .globalTarget = 50_mm,
                                .intent = [](const ThermalBlockModel& m, ThermalAnalysisDefinition& d) {
                                    d.mode = ThermalAnalysisMode::Transient;
                                    d.boundaryConditions = {
                                        held(1U, m.start(), kTransientSlabHeldKelvin),
                                        convecting(2U, m.end(), kTransientSlabEndCoefficient, kTransientSlabEndAmbientKelvin),
                                        convecting(3U, m.side(0), kTransientSlabSideCoefficient, kTransientSlabSideAmbientKelvin)};
                                    d.initialCondition = initialAt(kTransientSlabInitialKelvin);
                                    d.transient = everyStep(kTransientSlabStepSeconds, kTransientSlabEndSeconds);
                                }});
}

// --- RM-THERM-09 -------------------------------------------------------------

Result<ThermalBlockModel> buildThermalSingularReferenceModel() {
    return buildBlock(BlockSpec{.uuid = "18de0009-0000-4000-8000-000000000013",
                                .name = "ThermalSingular",
                                .prefix = "sing",
                                .aMm = 60.0,
                                .bMm = 40.0,
                                .depthMm = 25.0,
                                .solidName = "LooseBlock",
                                .material = kPatchMaterial,
                                .globalTarget = 20_mm,
                                .intent = [](const ThermalBlockModel&, ThermalAnalysisDefinition& d) {
                                    d.mode = ThermalAnalysisMode::Steady;
                                    d.loads = {generation(1U, 1.0e5)};
                                }});
}

Result<ThermalBlockModel> buildThermalSingularBalancedReferenceModel() {
    return buildBlock(BlockSpec{.uuid = "18de0009-0000-4000-8000-000000000014",
                                .name = "ThermalSingularBalanced",
                                .prefix = "singb",
                                .aMm = 60.0,
                                .bMm = 40.0,
                                .depthMm = 25.0,
                                .solidName = "LooseBlock",
                                .material = kPatchMaterial,
                                .globalTarget = 20_mm,
                                .intent = [](const ThermalBlockModel& m, ThermalAnalysisDefinition& d) {
                                    d.mode = ThermalAnalysisMode::Steady;
                                    // +1000 W/m^2 in at the start cap and 1000 W/m^2 out at the end cap: the caps have
                                    // the same area, so the net heat is zero to the last bit of the sum.
                                    d.loads = {surfaceFlux(1U, m.start(), 1000.0), surfaceFlux(2U, m.end(), -1000.0)};
                                }});
}

// --- RM-THERM-10 -------------------------------------------------------------

Result<ThermalBlockModel> buildThermalStaleMeshReferenceModel() {
    return buildBlock(BlockSpec{.uuid = "18de0010-0000-4000-8000-000000000015",
                                .name = "ThermalStaleMesh",
                                .prefix = "stale",
                                .aMm = 50.0,
                                .bMm = 40.0,
                                .depthMm = 30.0,
                                .solidName = "StaleBlock",
                                .material = kPatchMaterial,
                                .globalTarget = 20_mm,
                                .intent = [](const ThermalBlockModel& m, ThermalAnalysisDefinition& d) {
                                    heldCaps(m, d, 400.0, 300.0);
                                }});
}

Result<ThermalBlockModel> buildThermalUnresolvedReferenceModel(ThermalUnresolvedTarget target) {
    const char* uuid = target == ThermalUnresolvedTarget::Load          ? "18de0010-0000-4000-8000-000000000016"
                       : target == ThermalUnresolvedTarget::Temperature ? "18de0010-0000-4000-8000-000000000017"
                                                                        : "18de0010-0000-4000-8000-000000000018";
    const char* name = target == ThermalUnresolvedTarget::Load          ? "ThermalUnresolvedLoad"
                       : target == ThermalUnresolvedTarget::Temperature ? "ThermalUnresolvedTemperature"
                                                                        : "ThermalUnresolvedConvection";
    const char* prefix = target == ThermalUnresolvedTarget::Load ? "unl" : target == ThermalUnresolvedTarget::Temperature ? "unt" : "unc";
    return buildBlock(BlockSpec{.uuid = uuid,
                                .name = name,
                                .prefix = prefix,
                                .aMm = 50.0,
                                .bMm = 40.0,
                                .depthMm = 30.0,
                                .solidName = "UnresolvedBlock",
                                .material = kPatchMaterial,
                                .globalTarget = 20_mm,
                                .intent = [target](const ThermalBlockModel& m, ThermalAnalysisDefinition& d) {
                                    // A VALID held temperature and a VALID convection, so that the model would
                                    // solve if it were not for the one target that resolves to nothing -- and the
                                    // refusal is about that target and not about a missing datum.
                                    d.mode = ThermalAnalysisMode::Steady;
                                    d.boundaryConditions = {held(1U, m.start(), 400.0), convecting(2U, m.end(), 25.0, 300.0)};
                                    switch (target) {
                                    case ThermalUnresolvedTarget::Load:
                                        d.loads = {surfaceFlux(1U, m.missing(), 500.0)};
                                        break;
                                    case ThermalUnresolvedTarget::Temperature:
                                        d.boundaryConditions.push_back(held(3U, m.missing(), 350.0));
                                        break;
                                    case ThermalUnresolvedTarget::Convection:
                                        d.boundaryConditions.push_back(convecting(3U, m.missing(), 10.0, 290.0));
                                        break;
                                    }
                                }});
}

} // namespace bettercad::reference
