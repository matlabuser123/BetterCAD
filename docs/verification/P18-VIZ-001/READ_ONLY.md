# P18-VIZ-001 — Rendering does not mutate anything

**Rendering reads. It does not write to the mesh, the numbering, the solution, the post result, the balance, the loads, the boundary conditions, the analysis or the document.**

## By construction

* Every entry point takes its inputs by `const` reference or `std::span<const …>`. The only non-const members are the playback cursor (`ThermalPlayback`, view state) and the contour cache (a derived
  buffer that is cleared on a miss and can never make a result current).
* Gate rule 5 forbids `const_cast` and `mutable` in a renderer thermal file.
* The compile-fail group proves the result types cannot be written through a const view: `THE_FIELD_IS_READ_ONLY_THROUGH_A_CONST_VIEW`, `THE_EXTREME_IS_READ_ONLY_THROUGH_A_CONST_VIEW`,
  `THE_DISPLAY_STATE_IS_READ_ONLY_THROUGH_A_CONST_VIEW`, `THE_CARDINALITY_IS_READ_ONLY_THROUGH_A_CONST_VIEW`, `THE_INSPECTION_IS_READ_ONLY_THROUGH_A_CONST_VIEW`,
  `THE_PLAYBACK_CURSOR_IS_NOT_MOVED_THROUGH_A_CONST_VIEW`, `THE_BANNER_TEXT_IS_READ_ONLY`; and that the types offer no way to store a result, a stale flag, a conductivity, a material or a normalised flux
  (`THE_OVERLAY_HAS_NO_RESULT_STORE`, `THE_ELEMENT_INSPECTION_HAS_NO_CONDUCTIVITY`, `THE_ELEMENT_INSPECTION_HAS_NO_MATERIAL`, `THE_GLYPH_HAS_NO_NORMALISED_DIRECTION_STORE`).

## By measurement

* `DoesNotMutateThermalResult` — runs every channel's field, legend, colour buffer and extreme, the vector glyphs and overlay, an element and a node inspection and a playback binding on a hand post; the
  post compares equal to a copy taken before, the mesh compares equal to its copy, and the stored heat-flux vectors and magnitudes are bit-identical (q was neither normalised in place nor sign-flipped).
* `DoesNotMutateMeshDuringUnitConversion` (offscreen) — displaying the same view twice gives the same picture; the mesh compares equal to its copy; the view's metre buffer is unchanged and its largest
  coordinate is still 0.040 (a conversion in place would scale it by 1000 on each display).
* `VectorScalingDoesNotMutateResult` — three scalings of the same glyphs leave the post equal to its copy.
* `ViewStateChangesDoNotStaleThePhysics`, `ViewerStateChangesDoNotStaleThePhysics` — see [STALE_STATE.md](STALE_STATE.md).
* `ThermalViz_VisualizationDataIsDeterministicAndFingerprinted` repeats every construction after the others and compares the fingerprints of the outputs ([DETERMINISM.md](DETERMINISM.md)).

## What the GUI may and may not hold

The application may hold a result, a playback cursor, a colour map, a display range and a validation report — and nothing else about the physics. It holds no k, ρ, c_p, q″, T̄, h, T∞, T₀ or Δt of its
own: it reads them from the canonical objects when a glyph is built, and the builders refuse a prepared object that was not made from the canonical object presented.

## Unchanged files

`qualification/freeze-viz.txt` lists every file of the thermal, meshing, structural, linalg and core modules and the P17 renderer files as byte-identical to the previous milestone's commit. The only
modified renderer files are the additive `Viewer` overload and its OCCT presentation.
