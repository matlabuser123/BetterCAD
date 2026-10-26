// Fixture for tests/architecture/CheckLayering.cmake - never compiled.
// thermal (layer 45) may use meshing (40), features (20) and core (0).
// This also holds the layer-table entry in place: without
// `set(layer_thermal 45)` the checker reports an unknown module for this
// file (ADR-050).
#include <bettercad/core/Error.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/meshing/Mesher.hpp>
// P19-ARCH-001 (ADR-059). thermal may READ a CFD result: a wall pressure is a structural load and a wall heat-transfer
// coefficient is a thermal boundary condition. The checker allows it by LESS; cfd-to-thermal proves the reverse is refused.
#include <bettercad/cfd/CfdAnalysis.hpp>
