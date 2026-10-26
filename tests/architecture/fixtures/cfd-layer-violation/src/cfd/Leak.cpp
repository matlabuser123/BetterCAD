// Fixture for tests/architecture/CheckLayering.cmake - never compiled.
// Violation: cfd (layer 42) depends on io (layer 60).
//
// The direction is apps/io -> cfd, never the reverse. A solver that reached for
// io to read or write its own analysis file would invert it, and would couple the
// finite-volume formulation to a file format (ADR-059, ADR-060).
#include <bettercad/io/DocumentFile.hpp>