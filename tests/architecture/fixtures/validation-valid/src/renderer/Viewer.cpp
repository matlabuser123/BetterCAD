// The viewer consumes the verdict: validateThermalAnalysis is the core's entry point and accepted() a member.
int view() { auto r = validateThermalAnalysis(); return r.accepted() ? r.metrics.steadyResidual : 0; }
