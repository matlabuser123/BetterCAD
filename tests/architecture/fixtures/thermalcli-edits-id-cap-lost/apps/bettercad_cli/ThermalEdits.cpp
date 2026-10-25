// Comments may name thermalConductivity, assembleThermalSystem, 293.15, std::clamp, ofstream and saveDocument: not code.
#include "EditSupport.hpp"
#include "Selectors.hpp"

#include <bettercad/thermal/ThermalCommands.hpp>

namespace x {
const char* usage = "--flux <W/m^2> 1e5W/m^3 degC std::abs(x) 273.15";
void f() {
    thermal::AddThermalLoadCommand a;
    thermal::RemoveThermalLoadCommand b;
    thermal::AddThermalBoundaryConditionCommand c;
    thermal::RemoveThermalBoundaryConditionCommand d;
    thermal::CreateThermalAnalysisCommand e;
    parseFaceReference(f1);
    parseFaceReference(f2);
    parseQuantity(q);
    thermalAnalysisIds(doc);
    executeEdit(e1);
    executeEdit(e2);
    executeEdit(e3);
    executeEdit(e4);
    executeEdit(e5);
    executeEdit(e6);
}
} // namespace x
