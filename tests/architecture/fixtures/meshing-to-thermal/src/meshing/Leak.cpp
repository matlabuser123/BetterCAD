// Fixture for tests/architecture/CheckLayering.cmake - never compiled.
// Violation: meshing (layer 40) depends on thermal (layer 45).
//
// THE CYCLE THIS ARCHITECTURE EXISTS TO PREVENT, for the second consumer of a
// mesh. P16 must know nothing about the solvers that consume it: a mesher
// that asked a thermal analysis what it wanted would make the mesh depend on
// the solve that depends on the mesh. The one permitted direction is
// thermal -> meshing (ADR-050).
#include <bettercad/thermal/ThermalAnalysis.hpp>
