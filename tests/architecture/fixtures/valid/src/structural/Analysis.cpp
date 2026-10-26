// Fixture for tests/architecture/CheckLayering.cmake - never compiled.
// structural (layer 50) may use meshing (40), features (20) and core (0).
// This also holds the layer-table entry in place: without
// `set(layer_structural 50)` the checker reports an unknown module for this
// file (ADR-035).
#include <bettercad/core/Error.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/meshing/Mesher.hpp>
// P18-QUAL-001 (ADR-050). The direction the whole layer-45 decision exists to allow: structural (50) may include thermal (45), because a
// thermo-mechanical coupling reads a temperature field to compute thermal strain. The checker allows it by LESS, and until now no fixture
// proved it; thermal-to-structural proves the reverse is refused.
#include <bettercad/thermal/ThermalAnalysis.hpp>
