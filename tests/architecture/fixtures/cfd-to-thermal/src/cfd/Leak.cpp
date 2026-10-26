// Fixture for tests/architecture/CheckLayering.cmake - never compiled.
// Violation: cfd (layer 42) depends on thermal (layer 45).
//
// THE DIRECTION ADR-059 FIXES. A wall heat-transfer coefficient is a thermal
// boundary condition, so thermal must be able to include cfd and cfd must never
// include thermal. Buoyancy and conjugate heat transfer are deferred (ADR-058) and,
// when wanted, are an exchange orchestrated above both, never this include.
#include <bettercad/thermal/ThermalAnalysis.hpp>