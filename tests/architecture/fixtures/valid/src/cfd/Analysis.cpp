// Fixture for tests/architecture/CheckLayering.cmake - never compiled.
// cfd (layer 42) may use meshing (40), features (20) and core (0).
// This also holds the layer-table entry in place: without `set(layer_cfd 42)` the
// checker reports an unknown module for this file (ADR-059).
#include <bettercad/core/Error.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/meshing/Mesher.hpp>