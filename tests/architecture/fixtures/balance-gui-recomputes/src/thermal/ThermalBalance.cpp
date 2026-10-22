// A fixture: the accounting takes the exchange from the BC and the loads from the contributions.
int balance() { return convection.residualFrom(0) + load.integrated.heatRate; }
