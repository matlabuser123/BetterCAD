// Comments may name 1e-9, std::sqrt, Tolerance, generate( and setDefinition: not code.
#include <bettercad/thermal/ThermalAnalysisRun.hpp>

namespace x {
const char* note = "0.5 Tolerance std::sqrt generate( setDefinition";
void f() {
    const auto finish = [&]() {
        run.validation = validateThermalAnalysis(document, regenerator, mesher, analysis, run.derivedState());
    };
    requireThermalModel(a);
    buildThermalDofMap(a);
    prepareThermalLoads(a);
    prepareThermalBoundary(a);
    assembleThermalSystem(a);
    solveSteadyThermal(a);
    recoverSteadyThermal(a);
    recoverSteadyBalance(a);
    solveTransientThermal(a);
    recoverTransientThermal(a);
    recoverTransientBalance(a);
    const auto d = definition.solver;
    document.addObject(o);
}
} // namespace x
