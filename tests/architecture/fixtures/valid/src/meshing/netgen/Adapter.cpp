// Fixture for tests/architecture/CheckLayering.cmake - never compiled.
// A backend header is allowed inside src/meshing/<backend>/, and meshing
// (layer 4) may use features (2). This also holds the layer-table entry in
// place: without `set(layer_meshing 4)` the checker reports an unknown
// module for this file (ADR-033).
#include <bettercad/features/FaceReferences.hpp>

#include <nglib.h>
