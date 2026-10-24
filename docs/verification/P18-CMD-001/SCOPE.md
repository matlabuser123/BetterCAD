# P18-CMD-001 — scope

## In scope (the 22 items)

Create-analysis, add/edit/remove load, add/edit/remove boundary condition, initial-temperature, solver-settings and transient-settings commands; exact undo and redo; atomic failure; redo invalidation;
`GeometryReference` preserved; thermal result invalidated correctly; no solver arrays in canonical history; compile-fail mesh-identity misuse; determinism; adversarial review; regression; evidence.

## What was added

* `include/bettercad/thermal/ThermalCommands.hpp`, `src/thermal/ThermalCommands.cpp` (+ one line in `src/thermal/CMakeLists.txt`) — ten commands on the existing framework.
* 95 unit cases (`tests/thermal/ThermalCommand*.cpp`), 52 compile-fail cases (`ThermalCommandMisuse.cpp`), the ownership gate with 15 fixtures (16 tests), 14 zero-match filters (the total, eleven exact-name groups, compile-fail, architecture).
* ADR-055 and this evidence directory.

## What was not done, and why

* **No whole-definition edit, no deletion, no mode switch.** The checklist names ten commands. Mode is canonical and the data model is mode-complete, so a steady analysis cannot carry an initial temperature or
  transient settings and those two commands are refused for it with a diagnostic. Switching mode is one whole-definition edit, which no checklist item asks for; the brief says not to invent a mode-switch command.
* **No `NamedBoundarySet` target.** The only canonical target of a thermal load or condition is a `FaceName` (P18-DATA); a named boundary set is a P16 mesh concept.
* **No output-time schedule.** The data model's transient settings are a step, an end time and an output *policy*; there is no vector of output times to store.
* **No unification of the two currentness regimes.** The derived chain compares its recorded intent by value and the validation report compares the analysis revision; both are qualified predecessors
  and are left exactly as they were ([CURRENTNESS.md](CURRENTNESS.md)).
* **No Qt panel, no CLI verb, no persistence.** P18-PERSIST-001, P18-CLI-001, P18-REFMOD-001 and P18-QUAL-001 are not started. The payload types are canonical, copyable data with no pointer and no callback, so they
  are serialisable as they stand; nothing here serialises them.
* **No change to any qualified file.** The freeze lists every other thermal, meshing, structural, linalg, core and renderer file as byte-identical to the previous commit.

## The checklist item the brief and the repository word differently

The brief asks for `Next: P18-PERSIST-001 — Thermal Persistence / Round-Trip` and for the Future Phases section to be the absolute end of `TODO.md`. In the repository (as at HEAD, and at every earlier
milestone) three sections follow it. The order was kept and is reported, not changed.
