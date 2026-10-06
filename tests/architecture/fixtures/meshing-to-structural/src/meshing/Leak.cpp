// Fixture for tests/architecture/CheckLayering.cmake - never compiled.
// Violation: meshing (layer 40) depends on structural (layer 50).
//
// THE CYCLE THIS ARCHITECTURE EXISTS TO PREVENT. P16 must know nothing about
// the solver that consumes it: a mesher that asked a structural analysis what
// it wanted would make the mesh depend on the solve that depends on the mesh.
// The one permitted direction is structural -> meshing (ADR-035).
#include <bettercad/structural/StructuralAnalysis.hpp>
