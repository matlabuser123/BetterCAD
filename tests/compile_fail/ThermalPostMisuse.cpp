// Build-failure tests for the thermal result recovery (see CMakeLists.txt in this
// directory). Built once without any BETTERCAD_CF_* macro as a control, which must
// compile -- so every failure below is attributable to its own line and not to a
// missing header.
//
// Five claims of P18-POST-001 are enforced here rather than reviewed:
//
//   1. The numbers are DIMENSIONED. A temperature is K, a gradient K/m, a heat flux
//      W/m^2, a conductivity W/(m K): none converts to another, a bare double stands in
//      for none of them, and the recovered vector flux is not the scalar of a load.
//
//   2. The index spaces do not mix. A node, a tetrahedron's ordinal and an `ElementId`
//      are different things: `ordinalOf` takes an `ElementId`, not a `NodeId`, and a
//      result is not indexed by a raw id.
//
//   3. A RESULT CANNOT BE CONJURED OR EDITED. Possession is the evidence that the solution
//      was current, the numbering described the mesh, every element was valid and every
//      value finite. The arrays and the source are read-only views.
//
//   4. THE SCOPE LINE. A post result has no nodal or smoothed heat flux, no boundary or
//      normal flux, no heat balance or reaction, no storage or convection or generation
//      term in q, no time derivative, no Celsius, no colour and no component selection, and
//      its source has no post setting. Their absence from the types is the proof that this
//      milestone recovers the unsmoothed element fields of Fourier's law and does nothing else.
//
//   5. A post result is NOT a `ThermalResult` (which carries a heat balance this milestone
//      does not form), and a solver source is not a post source without the conversion.
#include <bettercad/meshing/MeshIds.hpp>
#include <bettercad/thermal/ThermalPost.hpp>
#include <bettercad/thermal/ThermalResult.hpp>

#include <span>
#include <vector>

using namespace bettercad;
using namespace bettercad::thermal;

int main() {
    // Dead code that must still COMPILE in the control, so every call below is checked against the
    // real signatures without needing a mesh at run time.
    const meshing::Mesh* mesh = nullptr;
    const ThermalDofMap* map = nullptr;
    const std::vector<Temperature>* temperatures = nullptr;
    const SteadyThermalSolution* steady = nullptr;
    const TransientThermalSolution* transient = nullptr;
    const ThermalPostResult* post = nullptr;
    if (mesh != nullptr && map != nullptr && temperatures != nullptr && steady != nullptr && transient != nullptr &&
        post != nullptr) {
        (void)recoverThermalElementFields(*mesh, *map, ThermalConductivity::fromSi(1.0), *temperatures);
        (void)recoverSteadyFromSolution(*mesh, *map, ThermalConductivity::fromSi(1.0), *steady);
        (void)recoverTransientFromSolution(*mesh, *map, ThermalConductivity::fromSi(1.0), *transient);
        (void)post->source();
        (void)post->mode();
        (void)post->nodeCount();
        (void)post->elementCount();
        (void)post->elementIds();
        (void)post->ordinalOf(meshing::ElementId::fromValue(1));
        (void)post->states();
        (void)post->finalState().temperatures;
        (void)post->finalState().fields.gradients;
        (void)post->finalState().fields.heatFluxes;
        (void)post->finalState().fields.heatFluxMagnitudes;
        (void)post->temperatureOf(0, *map, meshing::NodeId::fromValue(1));
        (void)post->gradientOf(0, meshing::ElementId::fromValue(1));
        (void)post->heatFluxOf(0, meshing::ElementId::fromValue(1));
        (void)post->heatFluxMagnitudeOf(0, meshing::ElementId::fromValue(1));
        (void)descriptorOf(ThermalPostField::HeatFlux).unit;
    }

#if defined(BETTERCAD_CF_A_GRADIENT_IS_NOT_A_HEAT_FLUX)
    // grad(T) is K/m; q is W/m^2. The conversion between them is -k, and it is written once.
    [[maybe_unused]] HeatFlux3 bad = TemperatureGradient3{};
#endif

#if defined(BETTERCAD_CF_A_TEMPERATURE_IS_NOT_A_HEAT_FLUX)
    [[maybe_unused]] HeatFlux bad = Temperature{};
#endif

#if defined(BETTERCAD_CF_A_HEAT_FLUX_IS_NOT_A_TEMPERATURE_GRADIENT)
    [[maybe_unused]] TemperatureGradient bad = HeatFlux{};
#endif

#if defined(BETTERCAD_CF_A_MAGNITUDE_IS_NOT_A_TEMPERATURE)
    if (post != nullptr) {
        [[maybe_unused]] Temperature bad = post->finalState().fields.heatFluxMagnitudes[0];
    }
#endif

#if defined(BETTERCAD_CF_A_BARE_DOUBLE_IS_NOT_A_CONDUCTIVITY)
    // 5 of what: W/(m K), or W/(mm K)?
    if (mesh != nullptr && map != nullptr && temperatures != nullptr) {
        (void)recoverThermalElementFields(*mesh, *map, 5.0, *temperatures);
    }
#endif

#if defined(BETTERCAD_CF_A_BARE_DOUBLE_IS_NOT_A_TEMPERATURE)
    [[maybe_unused]] Temperature bad = 300.0;
#endif

#if defined(BETTERCAD_CF_A_NODE_ID_IS_NOT_AN_ELEMENT_ID)
    // An element result is keyed by an ElementId (through its ordinal); a node is something else.
    if (post != nullptr) {
        (void)post->ordinalOf(meshing::NodeId::fromValue(1));
    }
#endif

#if defined(BETTERCAD_CF_A_SOLVER_SOURCE_IS_NOT_A_POST_SOURCE)
    // The conversion (`postSourceOf`) is named and explicit.
    [[maybe_unused]] ThermalPostSource bad = SteadySolutionSource{};
#endif

#if defined(BETTERCAD_CF_A_POST_RESULT_IS_NOT_A_THERMAL_RESULT)
    // A ThermalResult also holds a heat balance (P18-BALANCE-001).
    if (post != nullptr) {
        [[maybe_unused]] const ThermalResult& bad = *post;
    }
#endif

#if defined(BETTERCAD_CF_A_POST_RESULT_CONSTRUCTED_DIRECTLY)
    // Possession is the evidence that the solution was current and every element valid; there is
    // no public constructor.
    [[maybe_unused]] ThermalPostResult bad(ThermalPostSource{}, 0, {}, {});
#endif

#if defined(BETTERCAD_CF_THE_TEMPERATURES_ARE_READ_ONLY)
    // A recovered field is not edited to make it fit; it is recovered again.
    if (post != nullptr) {
        post->finalState().temperatures[0] = Temperature::fromSi(0.0);
    }
#endif

#if defined(BETTERCAD_CF_THE_GRADIENTS_ARE_READ_ONLY)
    if (post != nullptr) {
        post->finalState().fields.gradients[0] = TemperatureGradient3{};
    }
#endif

#if defined(BETTERCAD_CF_THE_HEAT_FLUXES_ARE_READ_ONLY)
    if (post != nullptr) {
        post->finalState().fields.heatFluxes[0] = HeatFlux3{};
    }
#endif

#if defined(BETTERCAD_CF_THE_MAGNITUDES_ARE_READ_ONLY)
    if (post != nullptr) {
        post->finalState().fields.heatFluxMagnitudes[0] = HeatFlux::fromSi(0.0);
    }
#endif

#if defined(BETTERCAD_CF_THE_ELEMENT_IDS_ARE_READ_ONLY)
    if (post != nullptr) {
        post->elementIds()[0] = meshing::ElementId::fromValue(7);
    }
#endif

#if defined(BETTERCAD_CF_THE_SOURCE_IS_READ_ONLY)
    if (post != nullptr) {
        post->source().mode = ThermalAnalysisMode::Transient;
    }
#endif

#if defined(BETTERCAD_CF_THE_RESULT_HAS_NO_NODAL_HEAT_FLUX)
    // Element-to-node averaging is deferred; a smoothed field is a visualisation representation.
    if (post != nullptr) {
        [[maybe_unused]] auto bad = post->finalState().fields.nodalHeatFlux;
    }
#endif

#if defined(BETTERCAD_CF_THE_RESULT_HAS_NO_SMOOTHED_FIELD)
    if (post != nullptr) {
        [[maybe_unused]] auto bad = post->finalState().fields.smoothedHeatFlux;
    }
#endif

#if defined(BETTERCAD_CF_THE_RESULT_HAS_NO_BOUNDARY_FLUX)
    // q . n needs a selected outward normal: P18-BALANCE-001's.
    if (post != nullptr) {
        [[maybe_unused]] auto bad = post->finalState().fields.normalHeatFlux;
    }
#endif

#if defined(BETTERCAD_CF_THE_RESULT_HAS_NO_HEAT_BALANCE)
    if (post != nullptr) {
        [[maybe_unused]] auto bad = post->heatBalance();
    }
#endif

#if defined(BETTERCAD_CF_THE_RESULT_HAS_NO_REACTIONS)
    if (post != nullptr) {
        [[maybe_unused]] auto bad = post->reactions();
    }
#endif

#if defined(BETTERCAD_CF_THE_RESULT_HAS_NO_STORAGE_FLUX)
    // C dT/dt is storage, not a spatial flux.
    if (post != nullptr) {
        [[maybe_unused]] auto bad = post->finalState().fields.storageHeatFlux;
    }
#endif

#if defined(BETTERCAD_CF_THE_RESULT_HAS_NO_CONVECTIVE_FLUX)
    // h (T - Tinf) is a boundary heat transfer, not part of the conductive q.
    if (post != nullptr) {
        [[maybe_unused]] auto bad = post->finalState().fields.convectiveHeatFlux;
    }
#endif

#if defined(BETTERCAD_CF_THE_RESULT_HAS_NO_TEMPERATURE_RATE)
    if (post != nullptr) {
        [[maybe_unused]] auto bad = post->finalState().temperatureRate;
    }
#endif

#if defined(BETTERCAD_CF_THE_RESULT_HAS_NO_CELSIUS)
    // SI throughout; a display unit is a viewer's formatting.
    if (post != nullptr) {
        [[maybe_unused]] auto bad = post->finalState().temperaturesCelsius;
    }
#endif

#if defined(BETTERCAD_CF_THE_RESULT_HAS_NO_GRADIENT_MAGNITUDE)
    // Not required by the milestone, and not added.
    if (post != nullptr) {
        [[maybe_unused]] auto bad = post->finalState().fields.gradientMagnitudes;
    }
#endif

#if defined(BETTERCAD_CF_THE_RESULT_STORES_NO_CONDUCTIVITY)
    // P15 stays the only owner of k; the result binds the material by source.
    if (post != nullptr) {
        [[maybe_unused]] auto bad = post->conductivity();
    }
#endif

#if defined(BETTERCAD_CF_THE_SOURCE_HAS_NO_COLOUR_MAP)
    // A view setting is not a dependency of the engineering result.
    if (post != nullptr) {
        [[maybe_unused]] auto bad = post->source().colourMap;
    }
#endif

#if defined(BETTERCAD_CF_THE_SOURCE_HAS_NO_COMPONENT_SELECTION)
    if (post != nullptr) {
        [[maybe_unused]] auto bad = post->source().component;
    }
#endif

#if defined(BETTERCAD_CF_THE_SOURCE_HAS_NO_SMOOTHING_SETTING)
    if (post != nullptr) {
        [[maybe_unused]] auto bad = post->source().smoothing;
    }
#endif

#if defined(BETTERCAD_CF_A_FIELD_DESCRIPTOR_HAS_NO_COLOUR)
    [[maybe_unused]] auto bad = descriptorOf(ThermalPostField::Temperature).colour;
#endif

    return 0;
}
