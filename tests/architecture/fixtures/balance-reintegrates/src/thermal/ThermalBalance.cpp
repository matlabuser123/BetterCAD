// A fixture that integrates again.
int balance() { auto h = solveTransientThermalSystem(); return convection.residualFrom(0) + load.integrated.heatRate + h; }
