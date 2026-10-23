# P18-VIZ-001 — Units and the SI-metre → OCCT-millimetre boundary

## The table

| Quantity | Stored / inspected as | Displayed as | Conversion |
|---|---|---|---|
| temperature | K (`Temperature`, SI) | K, legend `Temperature [K]` | none — no Celsius anywhere (gate rule 2; there is no `Temperature::fromCelsius`, compile-fail `QUANTITIES_HAVE_NO_CELSIUS_FACTORY`) |
| temperature gradient | K/m | K/m, legend `Temperature Gradient Magnitude [K/m]` | none — not K/mm |
| heat flux q, q″ | W/m² | W/m² | none — not W/mm² |
| time | s (stored per state) | s, `at t = … s` | none |
| position (mesh, glyph geometry) | metres (`Length`) | — | **`toModelLength` in `OcctOverlayPresentation.cpp` / `OcctMeshPresentation.cpp`, once** |

The core mesh stays in metres. `MeshView` buffers are SI metres. Every `ThermalOverlaySegment` is a `Point3D` in metres. The headless layer contains no `1000`, no `1e3`, no `0.001`
(gate rule 2, comments stripped). The OCCT adapter converts each coordinate exactly once through `geometry::occt::toModel`, the project's only metre-to-millimetre expression, and the thermal overlay
constructor shares that function with the structural one (no second expression).

## Measured, not asserted

The boundary is verified against the CAD kernel's own solid, the P16/P17 precedent: an overlay is rendered offscreen over the CAD solid of the same body and the silhouettes are compared.

* `ThermalViz_SIMetreToOCCTMillimetre` — a 1 × 2 × 3 mm box. CAD solid silhouette bounding box x 119..200, y 1..143 (pixels); thermal overlay x 118..201, y 0..144 (within the 3 px tolerance
  allowed for the line width). Mirrored in z the bounding box is y 95..239 on both. A factor of 1000 either way moves the overlay off the solid by hundreds of pixels; a double conversion by the
  same.
* `ThermalViz_AnOverlayInMetresCoincidesWithAFortyMillimetreSolid` — a 40 mm body: the overlay in metres coincides with the solid in millimetres.
* `ThermalViz_EverythingLeavingThisLayerIsSiMetres` — the headless counterpart, on a one-metre tetrahedron: the canonical node is at x = 1.0, the glyph centroid is exactly 0.25 (not 250), and the
  arrow's tail and head are in metres (tail 0.25, length 0.5 for a scale of 0.5 m). A hidden conversion in the layer would make the centroid 250.
* `ThermalViz_DoesNotMutateMeshDuringUnitConversion` — displaying a mesh, a contour and an overlay leaves the core mesh byte-identical.

Mutations `VIZ17` (conversion applied twice) and `VIZ18` (no conversion) are killed by these tests; `VIZ02`/`VIZ03`/`VIZ04` (Celsius, ×1000, kW/m²) by the unit tests.

## Unit labels

The legend carries the core descriptor's unit string and a name (`thermalLegendTitle`: `Temperature [K]`, `… at t = 0.6 s`). The inspection types carry unit-typed quantities
(`Temperature`, `TemperatureGradient`, `HeatFlux`, `Time`), none of which converts to a double or to another dimension (compile-fail `A_QUANTITY_IS_NOT_A_DOUBLE`,
`A_TEMPERATURE_IS_NOT_A_HEAT_FLUX`, `A_GRADIENT_IS_NOT_A_HEAT_FLUX`, `A_HEAT_FLUX_IS_NOT_A_GRADIENT`).

## Display ranges

A display range (manual min/max, band count) is view state in the same unit as the channel. It clamps a **colour**, never a value: `ManualRangeClampsTheColourAndNeverTheValue`.
