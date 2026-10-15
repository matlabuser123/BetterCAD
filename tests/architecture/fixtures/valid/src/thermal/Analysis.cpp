// Fixture for tests/architecture/CheckLayering.cmake - never compiled.
// thermal (layer 45) may use meshing (40), features (20) and core (0).
// This also holds the layer-table entry in place: without
// `set(layer_thermal 45)` the checker reports an unknown module for this
// file (ADR-050).
#include <bettercad/core/Error.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/meshing/Mesher.hpp>
