# P18-VIZ-001 — Scope

## In scope (the 21 items)

Undeformed mesh display; temperature contour; gradient-magnitude contour; heat-flux-magnitude contour; heat-flux vector display; element inspection; node temperature inspection; applied-heat-flux arrows;
prescribed-temperature symbols; convection symbols; boundary-set highlighting; highest/lowest temperature navigation; highest heat-flux navigation; transient playback; clearing the stale visual state;
rendering does not mutate results; the GUI does not duplicate canonical thermal data; the SI-metre → OCCT-millimetre boundary; classifier cardinality; Regression PASS; Evidence recorded.

## What was added

* `include/bettercad/renderer/Thermal{ResultView,Contour,Inspection,Overlay}.hpp` and `src/renderer/Thermal*.cpp` — the headless view layer (renderer, layer 70, links `BetterCAD::thermal`).
* `Viewer::displayOverlay(ObjectId, const ThermalOverlay&)`, `PresentationKind::ThermalOverlay`, and the OCCT presentation behind it (additive; the structural overlay path is unchanged and its tests unchanged).
* 97 unit cases (`tests/renderer/ThermalViz*.cpp`), 51 compile-fail cases (`ThermalVizMisuse.cpp`), the ownership gate with 11 fixtures (12 tests), 15 zero-match filters.
* ADR-054 and this evidence directory.

## What was not done, and why

* **No Qt result panels.** The Qt application had none for P17-VIZ-001 and has none for thermal; wiring them is a later, separate decision. The GUI smoke here is the headless layer plus offscreen renders of the
  production viewer path. This is reported, not hidden.
* **No deformed display.** Thermal analysis has no displacement; "undeformed" is the only mesh.
* **No new thermal physics, no change to POST, BALANCE, VALIDATION, LOAD, BC, MATERIAL, DOF, ELEMENT, ASSEMBLY, SOLVE, TRANSIENT, DATA, P16, P17 or the core.** The freeze lists every such file as byte-identical to the
  previous commit.
* **No commands, undo/redo, persistence of view state, CLI reports, reference models or qualification suite** — P18-CMD-001, P18-PERSIST-001, P18-CLI-001, P18-REFMOD-001 and P18-QUAL-001 are not started.

## TODO.md ordering

The brief asks for the *Future Phases* section to be the last section of `TODO.md`. In the repository (as at HEAD, and as it was at every previous milestone) three sections follow it; the order was kept and
this is reported, not changed.
