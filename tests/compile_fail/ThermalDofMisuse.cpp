// Build-failure tests for the temperature degree-of-freedom index space (see
// CMakeLists.txt in this directory). Built once without any BETTERCAD_CF_*
// macro as a control, which must compile -- so every failure below is
// attributable to its own line and not to a missing header.
//
// Three claims of P18-DOF-001 are enforced here rather than reviewed:
//
//   1. A ThermalDofIndex, a ThermalFreeEquationIndex, a NodeId and an
//      ElementId are DIFFERENT TYPES. A node ordinal is not a node handle
//      (handles are sparse), and a row of the reduced system is not an
//      unknown of the full one (they agree only with nothing prescribed).
//      Confusing any pair assembles into the wrong row, silently.
//
//   2. A prescribed temperature is a Temperature in kelvin, not a double and
//      not a bit: the constraint cannot be written without its value and
//      cannot be given a bare number.
//
//   3. A numbering, a constraint map and a free-equation map CANNOT BE
//      CONJURED, and none of them can be a member of the canonical analysis
//      definition.
#include <bettercad/meshing/MeshIds.hpp>
#include <bettercad/thermal/ThermalAnalysisObject.hpp>
#include <bettercad/thermal/ThermalDof.hpp>

#include <cstdint>

using namespace bettercad;
using namespace bettercad::thermal;

namespace {

void takesDof(ThermalDofIndex /*unused*/) {}
void takesEquation(ThermalFreeEquationIndex /*unused*/) {}
void takesNode(meshing::NodeId /*unused*/) {}
void takesElement(meshing::ElementId /*unused*/) {}
void takesCount(std::uint64_t /*unused*/) {}
void takesTemperature(Temperature /*unused*/) {}

} // namespace

int main() {
    const ThermalDofIndex dof = ThermalDofIndex::fromValue(0);
    const ThermalFreeEquationIndex equation = ThermalFreeEquationIndex::fromValue(0);
    const meshing::NodeId node = meshing::NodeId::fromValue(1);
    const meshing::ElementId element = meshing::ElementId::fromValue(1);
    const ThermalTemperatureConstraint constraint{.dof = dof, .temperature = Temperature::fromSi(300.0)};

    // Each kind is usable as itself, so the control compiles.
    takesDof(dof);
    takesEquation(equation);
    takesNode(node);
    takesElement(element);
    takesTemperature(constraint.temperature);
    // And a genuine integer is a genuine integer, which also keeps every
    // helper used in the control build.
    takesCount(1);
    [[maybe_unused]] ThermalAnalysisDefinition definition;

#if defined(BETTERCAD_CF_NODE_ID_AS_THERMAL_DOF)
    // THE CENTRAL ONE. A node handle is not an unknown's index: handles are
    // sparse and ordinals are dense, and `temperature[node.value()]` is the
    // defect the whole module exists to prevent.
    takesDof(node);
#elif defined(BETTERCAD_CF_THERMAL_DOF_AS_NODE_ID)
    takesNode(dof);
#elif defined(BETTERCAD_CF_ELEMENT_ID_AS_THERMAL_DOF)
    takesDof(element);
#elif defined(BETTERCAD_CF_THERMAL_DOF_AS_FREE_EQUATION)
    // Indexing the reduced system by an unknown of the full one. For a model
    // with nothing prescribed the two agree, so a test on such a model would
    // not catch it.
    takesEquation(dof);
#elif defined(BETTERCAD_CF_FREE_EQUATION_AS_THERMAL_DOF)
    takesDof(equation);
#elif defined(BETTERCAD_CF_THERMAL_DOF_FROM_INTEGER)
    // A raw number is not an unknown's index; fromValue() is the only way in.
    takesDof(3);
#elif defined(BETTERCAD_CF_THERMAL_DOF_AS_INTEGER)
    // And it does not decay to one, so it cannot be mixed into arithmetic
    // with a count or a node handle's value.
    takesCount(dof);
#elif defined(BETTERCAD_CF_THERMAL_DOF_DEFAULT_CONSTRUCTED)
    // NO INVALID VALUE AND NO DEFAULT: an index that defaulted to 0 would be
    // the sentinel defect this type exists to avoid.
    const ThermalDofIndex uninitialised;
    takesDof(uninitialised);
#elif defined(BETTERCAD_CF_FREE_EQUATION_DEFAULT_CONSTRUCTED)
    const ThermalFreeEquationIndex uninitialised;
    takesEquation(uninitialised);
#elif defined(BETTERCAD_CF_A_CONSTRAINT_TAKES_A_TEMPERATURE_NOT_A_DOUBLE)
    // 300 of what? A prescribed temperature is a Temperature, in kelvin.
    [[maybe_unused]] const ThermalTemperatureConstraint bare{.dof = dof, .temperature = 300.0};
#elif defined(BETTERCAD_CF_DOF_MAP_CONSTRUCTED_DIRECTLY)
    // buildThermalDofMap is the only way to obtain one, so possessing a map
    // is evidence that it is the numbering of a mesh that exists.
    const ThermalDofMap fabricated;
    takesCount(fabricated.dofCount());
#elif defined(BETTERCAD_CF_CONSTRAINT_MAP_CONSTRUCTED_DIRECTLY)
    const ThermalConstraintMap fabricated;
    takesCount(fabricated.dofCount());
#elif defined(BETTERCAD_CF_FREE_EQUATION_MAP_CONSTRUCTED_DIRECTLY)
    const ThermalFreeEquationMap fabricated;
    takesCount(fabricated.dofCount());
#elif defined(BETTERCAD_CF_THE_DEFINITION_HAS_NO_DOF_MAP)
    // Derived, never canonical: the analysis definition has no member for a
    // numbering, a constraint map or a free-equation map.
    [[maybe_unused]] auto& field = definition.dofMap;
#elif defined(BETTERCAD_CF_THE_DEFINITION_HAS_NO_CONSTRAINED_DOFS)
    [[maybe_unused]] auto& field = definition.constrainedDofs;
#endif
    return 0;
}
