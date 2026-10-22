// A fixture: the validator reads what the others published, and decides nothing itself.
int validate() { return validateSteadySolution() + validateTransientStep() + balanceWithinTolerance() + analyseAnchoring() + requireCurrent() + requireThermalModel() + staleReasons() + summaries.size() + direction(0); }
double tolerance() { return 1e-9; }
