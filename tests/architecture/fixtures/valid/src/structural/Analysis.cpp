// Fixture for tests/architecture/CheckLayering.cmake - never compiled.
// structural (layer 50) may use meshing (40), features (20) and core (0).
// This also holds the layer-table entry in place: without
// `set(layer_structural 50)` the checker reports an unknown module for this
// file (ADR-035).
#include <bettercad/core/Error.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/meshing/Mesher.hpp>
