// Fixture for tests/architecture/CheckLayering.cmake - never compiled.
// Violation: linalg (layer 5) depends on thermal (layer 45).
//
// The generic solve (ADR-052) must stay generic: it knows no physics, so a
// thermal type reaching into it would make the kernel thermal and put the
// dependency the wrong way round.
#include <bettercad/thermal/ThermalSystem.hpp>
