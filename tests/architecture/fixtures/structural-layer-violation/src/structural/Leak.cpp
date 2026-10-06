// Fixture for tests/architecture/CheckLayering.cmake - never compiled.
// Violation: structural (layer 50) depends on io (layer 60).
//
// The direction is apps/io -> structural, never the reverse. A solver that
// reached for io to read or write its own analysis file would invert it, and
// would couple the Tet4 formulation to a file format (ADR-035, ADR-036).
#include <bettercad/io/DocumentFile.hpp>
