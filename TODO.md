# BetterCAD — TODO

`[x]` means implemented **and** verified by passing tests, with evidence
recorded under `docs/verification/`. `[ ]` means not complete.

## Done

### P0 — Repository Foundation

- [x] P0-001 Build system and repository skeleton — [evidence](docs/verification/P0-001/README.md)

### P1 — Core Engineering Types

- [x] P1-001 Unit-safe quantities — [evidence](docs/verification/P1-001/README.md)
- [x] P1-002 Strong IDs — [evidence](docs/verification/P1-002/README.md)
- [x] P1-003 Parameter system — [evidence](docs/verification/P1-003/README.md)

### P2 — Document Model

- [x] P2-001 Document — [evidence](docs/verification/P2/README.md)
- [x] P2-002 Command architecture + undo/redo — [evidence](docs/verification/P2/README.md)

### P3 — Geometry Foundation

- [x] P3-001 Open CASCADE integration — [evidence](docs/verification/P3/README.md)
- [x] P3-002 Primitive solids — [evidence](docs/verification/P3/README.md)
- [x] P3-003 Analytic geometry-property validation — [evidence](docs/verification/P3/README.md)
- [x] P3-004 Boolean operations — [evidence](docs/verification/P3/README.md)

### P4 — Sketch Data Model

- [x] P4-001 Sketch coordinate system — [evidence](docs/verification/P4/README.md)
- [x] P4-002 Point/Line/Circle/Arc — [evidence](docs/verification/P4/README.md)
- [x] P4-003 Geometric query API — [evidence](docs/verification/P4/README.md)
- [x] CreateSketchCommand (sketch module; built on `AddObjectCommand` from P2-002) — [evidence](docs/verification/P4/README.md)

### P5 — Constraints

- [x] Constraint representation — [evidence](docs/verification/P5/README.md)
- [x] Constraint reference validation — [evidence](docs/verification/P5/README.md)

### P6 — Sketch Solver

- [x] Constraint equation system — [evidence](docs/verification/P6/README.md)
- [x] Solver diagnostics — [evidence](docs/verification/P6/README.md)
- [x] Rectangle validation — [evidence](docs/verification/P6/README.md)
- [x] Conflict/over-constraint validation — [evidence](docs/verification/P6/README.md)

### P7 — Extrude

- [x] Closed-profile detection — [evidence](docs/verification/P7/README.md)
- [x] Extrude feature — [evidence](docs/verification/P7/README.md)
- [x] Parameter-driven regeneration — [evidence](docs/verification/P7/README.md)
- [x] Analytic volume regression — [evidence](docs/verification/P7/README.md)

### P8 — Dependency Graph

- [x] Dependency representation — [evidence](docs/verification/P8/README.md)
- [x] Dirty propagation — [evidence](docs/verification/P8/README.md)
- [x] Topological regeneration — [evidence](docs/verification/P8/README.md)
- [x] Cycle detection — [evidence](docs/verification/P8/README.md)

### P9 — Persistence

- [x] Save — [evidence](docs/verification/P9/README.md)
- [x] Load — [evidence](docs/verification/P9/README.md)
- [x] Round-trip regression — [evidence](docs/verification/P9/README.md)

### P10 — CLI

- [x] new — [evidence](docs/verification/P10/README.md)
- [x] info — [evidence](docs/verification/P10/README.md)
- [x] validate — [evidence](docs/verification/P10/README.md)
- [x] STEP export — [evidence](docs/verification/P10/README.md)
- [x] STL export — [evidence](docs/verification/P10/README.md)

## Current

Nothing in progress: P0–P10 are complete. The items below are deliberately
not started.

## Later — Do Not Start Yet

- [ ] Revolve
- [ ] Sweep
- [ ] Loft
- [ ] Fillet
- [ ] Chamfer
- [ ] Hole wizard
- [ ] Patterns
- [ ] Assemblies
- [ ] Drawings
- [ ] Semantic topology naming
- [ ] STEP import
- [ ] DXF
- [ ] GPU renderer
- [ ] FEA
- [ ] CFD
- [ ] Thermal
- [ ] Optimization
- [ ] Python API
- [ ] Collaboration/version control
- [ ] AI engineering agent
- [ ] CAM/manufacturing
