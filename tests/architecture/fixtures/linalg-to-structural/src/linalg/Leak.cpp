// Fixture for tests/architecture/CheckLayering.cmake - never compiled.
// Violation: linalg (layer 5) depends on structural (layer 50).
//
// The kernel was EXTRACTED from structural precisely so that it could sit below
// thermal; including a structural header from it would undo the extraction and
// recreate the cycle ADR-050 and ADR-052 exist to prevent.
#include <bettercad/structural/StructuralSolve.hpp>
