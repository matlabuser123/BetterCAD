// Build-failure tests for the thermal identity and data model (see
// CMakeLists.txt in this directory). Built once without any BETTERCAD_CF_*
// macro as a control, which must compile -- so every failure below is
// attributable to its own line and not to a missing header.
//
// What these prove is P18-DATA-001's central claim: three thermal document
// identities that do not convert into one another, into a mesh handle, into
// an integer or into P17's structural identities; canonical records that
// cannot be given a mesh handle or a value of the wrong unit; and a result
// that cannot be written to. Each case, written by hand, is a wrong program
// that compiles only if the model is broken.
#include <bettercad/core/Id.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/meshing/MeshIds.hpp>
#include <bettercad/thermal/ThermalAnalysisObject.hpp>
#include <bettercad/thermal/ThermalData.hpp>
#include <bettercad/thermal/ThermalResult.hpp>

#include <cstdint>

using namespace bettercad;
using namespace bettercad::thermal;

namespace {

void takesThermalAnalysis(ThermalAnalysisId /*unused*/) {}
void takesThermalLoad(ThermalLoadId /*unused*/) {}
void takesBoundaryCondition(ThermalBoundaryConditionId /*unused*/) {}
void takesStructuralAnalysis(AnalysisId /*unused*/) {}
void takesStructuralLoad(LoadId /*unused*/) {}
void takesObject(ObjectId /*unused*/) {}
void takesNode(meshing::NodeId /*unused*/) {}
void takesCount(std::uint64_t /*unused*/) {}

/// A result handed to a consumer the way every consumer receives one. Never
/// called: the control must compile, and these cases are about what the
/// consumer CANNOT write, which the compiler decides without running it.
[[maybe_unused]] void reads(const ThermalResult& result,
                            [[maybe_unused]] const ThermalAnalysisDefinition& definition) {
    [[maybe_unused]] const ThermalResultSource& source = result.source();
#if defined(BETTERCAD_CF_A_RESULT_SOURCE_CANNOT_BE_REWRITTEN)
    // THE FAILURE MODE THE DESIGN IS BUILT AGAINST: refreshing a stale result
    // by rewriting what it claims to come from. source() is a const reference
    // and there is no other accessor.
    result.source().mesh.generation = 0;
#elif defined(BETTERCAD_CF_A_RESULT_HAS_NO_TEMPERATURE_SETTER)
    result.setTemperatures({});
#elif defined(BETTERCAD_CF_A_RESULT_TEMPERATURE_IS_READ_ONLY)
    // Every array is a span of CONST elements: a renderer holding a result
    // cannot write a temperature back into it.
    result.temperatures()[0] = Temperature{};
#elif defined(BETTERCAD_CF_THE_DEFINITION_HAS_NO_TEMPERATURES)
    // K, C, Q, T, grad(T) and q can never become canonical document state:
    // the definition has no member to hold any of them.
    [[maybe_unused]] auto& field = definition.temperatures;
#elif defined(BETTERCAD_CF_THE_DEFINITION_HAS_NO_CONDUCTIVITY_MATRIX)
    [[maybe_unused]] auto& field = definition.conductivityMatrix;
#elif defined(BETTERCAD_CF_THE_DEFINITION_HAS_NO_RESULT)
    [[maybe_unused]] auto& field = definition.result;
#endif
}

} // namespace

int main() {
    const ThermalAnalysisId analysis = ThermalAnalysisId::fromValue(1);
    const ThermalLoadId load = ThermalLoadId::fromValue(1);
    const ThermalBoundaryConditionId condition = ThermalBoundaryConditionId::fromValue(1);
    const AnalysisId structuralAnalysis = AnalysisId::fromValue(1);
    const LoadId structuralLoad = LoadId::fromValue(1);
    const meshing::NodeId node = meshing::NodeId::fromValue(1);
    const meshing::ElementId element = meshing::ElementId::fromValue(1);

    // Each kind is usable as itself, so the control compiles.
    takesThermalAnalysis(analysis);
    takesThermalLoad(load);
    takesBoundaryCondition(condition);
    takesStructuralAnalysis(structuralAnalysis);
    takesStructuralLoad(structuralLoad);
    takesNode(node);
    takesCount(1);
    [[maybe_unused]] const auto elementValue = element.value();

    // A THERMAL ANALYSIS IS A DOCUMENT OBJECT, so its ID widens. The one
    // widening the model permits, and it must keep compiling.
    takesObject(analysis);

    // The canonical records, built the right way, so the control exercises
    // every constructor the cases below misuse.
    [[maybe_unused]] const SurfaceHeatFluxLoad flux{.face = {}, .flux = HeatFlux::fromSi(1.0)};
    [[maybe_unused]] const ConvectionBoundary convection{
        .face = {}, .coefficient = HeatTransferCoefficient::fromSi(10.0), .ambient = Temperature::fromSi(293.15)};
    [[maybe_unused]] const PrescribedTemperature held{.face = {}, .value = Temperature::fromSi(300.0)};

#if defined(BETTERCAD_CF_THERMAL_ANALYSIS_ID_AS_THERMAL_LOAD_ID)
    // Three thermal document identities, three domains.
    takesThermalLoad(analysis);
#elif defined(BETTERCAD_CF_THERMAL_ANALYSIS_ID_AS_BOUNDARY_CONDITION_ID)
    takesBoundaryCondition(analysis);
#elif defined(BETTERCAD_CF_THERMAL_LOAD_ID_AS_BOUNDARY_CONDITION_ID)
    // The one a copy-paste would produce: a load and a boundary condition are
    // both members of an analysis, both 64-bit, and they mean different
    // things -- heat in, versus a temperature held.
    takesBoundaryCondition(load);
#elif defined(BETTERCAD_CF_BOUNDARY_CONDITION_ID_AS_THERMAL_LOAD_ID)
    takesThermalLoad(condition);
#elif defined(BETTERCAD_CF_THERMAL_LOAD_ID_AS_OBJECT_ID)
    // A THERMAL LOAD IS NOT A DOCUMENT OBJECT. It is a member of an analysis's
    // definition, so it must not widen: if it did, a load could be passed to
    // findObject or the dependency graph and resolve to whatever object shares
    // its number.
    takesObject(load);
#elif defined(BETTERCAD_CF_BOUNDARY_CONDITION_ID_AS_OBJECT_ID)
    takesObject(condition);
#elif defined(BETTERCAD_CF_NODE_ID_AS_THERMAL_ANALYSIS_ID)
    // A mesh handle is not a document identity, in either direction.
    takesThermalAnalysis(node);
#elif defined(BETTERCAD_CF_NODE_ID_AS_THERMAL_LOAD_ID)
    // THE RULE ADR-032 STATES: a load's identity is canonical intent naming
    // CAD geometry, never a mesh entity. A heat load keyed on a node would be
    // rebound to unrelated material by the next remesh.
    takesThermalLoad(node);
#elif defined(BETTERCAD_CF_ELEMENT_ID_AS_THERMAL_LOAD_ID)
    takesThermalLoad(element);
#elif defined(BETTERCAD_CF_STRUCTURAL_LOAD_ID_AS_THERMAL_LOAD_ID)
    // P17'S IDENTITIES ARE NOT P18'S. A traction and a heat flux share a face
    // and nothing else; a thermal solve handed a structural load's identity
    // would look up heat where there is force.
    takesThermalLoad(structuralLoad);
#elif defined(BETTERCAD_CF_STRUCTURAL_ANALYSIS_ID_AS_THERMAL_ANALYSIS_ID)
    takesThermalAnalysis(structuralAnalysis);
#elif defined(BETTERCAD_CF_THERMAL_ANALYSIS_ID_AS_STRUCTURAL_ANALYSIS_ID)
    takesStructuralAnalysis(analysis);
#elif defined(BETTERCAD_CF_INTEGER_AS_THERMAL_LOAD_ID)
    // No bare integer becomes a canonical identity by accident.
    takesThermalLoad(1);
#elif defined(BETTERCAD_CF_A_HEAT_FLUX_LOAD_DOES_NOT_TARGET_A_NODE)
    // The canonical face load has nowhere for a node to hide.
    [[maybe_unused]] const SurfaceHeatFluxLoad wrong{node, HeatFlux::fromSi(1.0)};
#elif defined(BETTERCAD_CF_A_TEMPERATURE_IS_NOT_A_HEAT_FLUX)
    // K where W/m^2 is wanted: the units framework refuses, so a prescribed
    // temperature cannot be typed into a flux load.
    [[maybe_unused]] const SurfaceHeatFluxLoad wrong{.face = {}, .flux = Temperature::fromSi(300.0)};
#elif defined(BETTERCAD_CF_A_CONDUCTIVITY_IS_NOT_A_CONVECTION_COEFFICIENT)
    // W/(m K) where W/(m^2 K) is wanted: the two differ by one power of
    // length, and only the dimension system notices.
    [[maybe_unused]] const ConvectionBoundary wrong{
        .face = {}, .coefficient = ThermalConductivity::fromSi(45.0), .ambient = Temperature::fromSi(293.15)};
#elif defined(BETTERCAD_CF_A_HEAT_FLUX_IS_NOT_A_TEMPERATURE)
    [[maybe_unused]] const PrescribedTemperature wrong{.face = {}, .value = HeatFlux::fromSi(1.0)};
#elif defined(BETTERCAD_CF_THE_MODE_IS_NOT_A_BOOL)
    // An enumeration, not a flag.
    ThermalAnalysisDefinition definition;
    definition.mode = true;
#endif
    return 0;
}
