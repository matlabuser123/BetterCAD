// Fixture for tests/architecture/CheckLayering.cmake - never compiled.
// Violation: meshing (layer 40) depends on cfd (layer 42).
//
// THE CYCLE THIS ARCHITECTURE EXISTS TO PREVENT, for the third consumer of a mesh.
// P16 must know nothing about the solvers that consume it: a mesher that asked a
// CFD analysis what it wanted would make the mesh depend on the solve that depends
// on the mesh. The one permitted direction is cfd -> meshing (ADR-059).
#include <bettercad/cfd/CfdAnalysis.hpp>