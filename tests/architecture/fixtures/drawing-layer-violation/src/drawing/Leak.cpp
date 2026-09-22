// Fixture for tests/architecture/CheckLayering.cmake - never compiled.
// Violation: drawing (layer 4) depends on io (layer 5).
//
// This is the direction the ADR-015 renumber established, and the one a
// future change is most likely to get wrong: io must serialize a drawing's
// objects, so io depends on drawing and never the other way round. A drawing
// that reached for io to write its own file would invert it.
#include <bettercad/io/DocumentFile.hpp>
