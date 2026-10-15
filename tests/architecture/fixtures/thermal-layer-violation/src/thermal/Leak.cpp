// Fixture for tests/architecture/CheckLayering.cmake - never compiled.
// Violation: thermal (layer 45) depends on io (layer 60).
//
// The direction is apps/io -> thermal, never the reverse. A solver that
// reached for io to read or write its own analysis file would invert it, and
// would couple the conduction formulation to a file format (ADR-050, ADR-051).
#include <bettercad/io/DocumentFile.hpp>
