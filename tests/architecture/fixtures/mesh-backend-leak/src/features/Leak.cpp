// Fixture for tests/architecture/CheckLayering.cmake - never compiled.
// Violation: volume-meshing backend header outside src/meshing/<backend>/.
// Rule 1 does not catch this one: it keys on the .hxx extension, and every
// candidate backend ships .h headers (ADR-033).
#include <bettercad/core/BuildInfo.hpp>

#include <nglib.h>
