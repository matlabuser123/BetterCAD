// Build-failure tests for the thermal heat loads (see CMakeLists.txt in this
// directory). Built once without any BETTERCAD_CF_* macro as a control, which
// must compile -- so every failure below is attributable to its own line and not
// to a missing header.
//
// Four claims of P18-LOAD-001 are enforced here rather than reviewed:
//
//   1. The load values are DIMENSIONED and distinct. A heat flux is W/m^2, a
//      generation is W/m^3, a power is W and a temperature is K; none converts
//      to another and none is a bare double. A scalar written in the wrong
//      unit would pass a runtime check and be wrong by a factor of an area.
//
//   2. A canonical load holds NOTHING MESH-LOCAL. The surface load has a
//      FaceName and a flux; the volumetric one has a generation. There is no
//      node, facet, element or unknown to find in either, and the volumetric
//      load has no face -- it targets the whole analysed solid.
//
//   3. Identities do not convert. A node handle is not a load identity; a
//      structural LoadId is not a ThermalLoadId; a list of nodes is not a list
//      of facets; a vector of doubles is not a heat-input vector.
//
//   4. A PREPARED heat input cannot be conjured, cannot be edited, and has no
//      convection, no temperature constraint and no material -- those belong
//      to P18-BC-001 and P15, and their absence from the type is the scope line.
#include <bettercad/core/document/References.hpp>
#include <bettercad/meshing/MeshIds.hpp>
#include <bettercad/thermal/ThermalLoadVector.hpp>

#include <span>

using namespace bettercad;
using namespace bettercad::thermal;

int main() {
    const FaceName face{ObjectId::fromValue(3), FaceSelector{.role = FaceRole::EndCap}};
    const SurfaceHeatFluxLoad surface{.face = face, .flux = HeatFlux::fromSi(12.0)};
    const VolumetricHeatSourceLoad volumetric{.generation = VolumetricHeatSource::fromSi(24.0)};
    const ThermalLoad surfaceLoad{ThermalLoadId::fromValue(1), surface};
    const ThermalLoad volumeLoad{ThermalLoadId::fromValue(2), volumetric};

    // Dead code that must still COMPILE in the control, so every call below is
    // checked against the real signatures without needing a mesh at run time.
    const meshing::Mesh* mesh = nullptr;
    const ThermalDofMap* map = nullptr;
    const PreparedThermalLoads* prepared = nullptr;
    std::span<Power> heatInput{};
    std::span<const meshing::ElementId> facets{};
    if (mesh != nullptr && map != nullptr && prepared != nullptr) {
        (void)integrateSurfaceHeatFlux(*mesh, *map, facets, surface.flux, heatInput);
        (void)integrateVolumetricHeatSource(*mesh, *map, facets, volumetric.generation, heatInput);
        (void)prepared->totalHeatRate();
        (void)prepared->heatInput();
    }
    (void)surfaceLoad.kind();
    (void)volumeLoad.kind();

#if defined(BETTERCAD_CF_A_HEAT_FLUX_IS_NOT_A_DOUBLE)
    // A bare number has no unit. 12 of what: W/m^2, W/mm^2, W?
    [[maybe_unused]] const SurfaceHeatFluxLoad bad{.face = face, .flux = 12.0};
#endif

#if defined(BETTERCAD_CF_A_HEAT_FLUX_IS_NOT_A_GENERATION)
    // W/m^3 where W/m^2 is wanted: an area's worth of error.
    [[maybe_unused]] const SurfaceHeatFluxLoad bad{.face = face, .flux = VolumetricHeatSource::fromSi(1.0)};
#endif

#if defined(BETTERCAD_CF_A_GENERATION_IS_NOT_A_HEAT_FLUX)
    [[maybe_unused]] const VolumetricHeatSourceLoad bad{.generation = HeatFlux::fromSi(1.0)};
#endif

#if defined(BETTERCAD_CF_A_POWER_IS_NOT_A_HEAT_FLUX)
    // A total in W is not a flux in W/m^2.
    [[maybe_unused]] const SurfaceHeatFluxLoad bad{.face = face, .flux = Power::fromSi(1.0)};
#endif

#if defined(BETTERCAD_CF_A_TEMPERATURE_IS_NOT_A_GENERATION)
    [[maybe_unused]] const VolumetricHeatSourceLoad bad{.generation = Temperature::fromSi(300.0)};
#endif

#if defined(BETTERCAD_CF_A_NODE_ID_IS_NOT_A_LOAD_ID)
    // A mesh node handle is not the identity of a load.
    [[maybe_unused]] const ThermalLoad bad{meshing::NodeId::fromValue(1), surface};
#endif

#if defined(BETTERCAD_CF_A_STRUCTURAL_LOAD_ID_IS_NOT_A_THERMAL_LOAD_ID)
    [[maybe_unused]] const ThermalLoad bad{LoadId::fromValue(1), surface};
#endif

#if defined(BETTERCAD_CF_A_NODE_LIST_IS_NOT_A_FACET_LIST)
    // Facets are boundary triangles; nodes are their corners. Passing the wrong
    // kind of handle list would index the mesh by the wrong thing.
    if (mesh != nullptr && map != nullptr) {
        [[maybe_unused]] auto bad = integrateSurfaceHeatFlux(*mesh, *map, std::span<const meshing::NodeId>{},
                                                             surface.flux, heatInput);
    }
#endif

#if defined(BETTERCAD_CF_A_DOUBLE_SPAN_IS_NOT_A_HEAT_INPUT)
    // The output is Power, in W, indexed by unknown; a span of bare doubles has
    // no unit and is refused.
    if (mesh != nullptr && map != nullptr) {
        std::span<double> raw{};
        [[maybe_unused]] auto bad = integrateSurfaceHeatFlux(*mesh, *map, facets, surface.flux, raw);
    }
#endif

#if defined(BETTERCAD_CF_A_PREPARED_LOAD_CONSTRUCTED_DIRECTLY)
    // Possession is the evidence that every load resolved and every value was
    // finite; there is no public constructor.
    [[maybe_unused]] PreparedThermalLoads bad;
#endif

#if defined(BETTERCAD_CF_THE_SURFACE_LOAD_HAS_NO_NODE)
    [[maybe_unused]] auto bad = surface.node;
#endif

#if defined(BETTERCAD_CF_THE_SURFACE_LOAD_HAS_NO_FACET)
    [[maybe_unused]] auto bad = surface.facet;
#endif

#if defined(BETTERCAD_CF_THE_SURFACE_LOAD_HAS_NO_UNKNOWN)
    [[maybe_unused]] auto bad = surface.dof;
#endif

#if defined(BETTERCAD_CF_THE_VOLUMETRIC_LOAD_HAS_NO_ELEMENT)
    [[maybe_unused]] auto bad = volumetric.element;
#endif

#if defined(BETTERCAD_CF_THE_VOLUMETRIC_LOAD_HAS_NO_FACE)
    // It targets the whole analysed solid; there is nothing to name.
    [[maybe_unused]] auto bad = volumetric.face;
#endif

#if defined(BETTERCAD_CF_THE_PREPARED_LOAD_HAS_NO_CONVECTION)
    // Convection (h, T_inf) is P18-BC-001's. Its absence from this type is the
    // scope line.
    [[maybe_unused]] auto bad = prepared->convection();
#endif

#if defined(BETTERCAD_CF_THE_PREPARED_LOAD_HAS_NO_TEMPERATURE_CONSTRAINT)
    [[maybe_unused]] auto bad = prepared->constraints();
#endif

#if defined(BETTERCAD_CF_THE_PREPARED_LOAD_HAS_NO_MATERIAL)
    // A prescribed heat input depends on no k, rho or cp.
    [[maybe_unused]] auto bad = prepared->material();
#endif

#if defined(BETTERCAD_CF_THE_PREPARED_HEAT_INPUT_IS_READ_ONLY)
    // A derived vector is not edited in place; it is prepared again.
    if (prepared != nullptr) {
        prepared->heatInput()[0] = Power::fromSi(1.0);
    }
#endif

    return 0;
}
