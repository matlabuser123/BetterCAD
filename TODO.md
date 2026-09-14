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

Release v0.1.0 (commit `2da8966`, tag `v0.1.0`) is P0–P10.

### P11 — Production Part Modeling (completed items)

#### P11-FEAT-001 — Revolve — [evidence](docs/verification/P11-FEAT-001/README.md)

- [x] Revolve feature definition
- [x] Axis/reference representation
- [x] Closed-profile revolve
- [x] Partial-angle revolve
- [x] Positive/negative direction
- [x] New-body operation
- [x] Add/remove/intersect operation
- [x] Geometry validity checks
- [x] Parameter-driven regeneration
- [x] Save/load round-trip
- [x] Undo/redo regression
- [x] STEP/STL export regression
- [x] Analytic volume validation
- [x] Failure diagnostics
- [x] Evidence under `docs/verification/P11-FEAT-001/`

**Acceptance (met):**

- Full 360° revolve of a known profile matches analytic volume.
- Partial revolve produces the expected proportional volume where
  analytically applicable.
- Changing angle/radius regenerates downstream geometry correctly.
- Invalid or intersecting-axis profiles fail with structured diagnostics.
- Save → destroy → load → regenerate reproduces equivalent geometry and
  stable IDs.
- Existing P0–P10 regression suite remains green.

#### P11-FEAT-002 — Chamfer — [evidence](docs/verification/P11-FEAT-002/README.md)

- [x] Chamfer feature definition
- [x] Edge references (geometric signatures; limits documented)
- [x] Equal-distance chamfer
- [x] Two-distance chamfer
- [x] Distance-angle chamfer
- [x] Single-edge chamfer
- [x] Multiple-edge chamfer (separate, adjacent, tangent chain)
- [x] Chamfer of extruded and revolved bodies
- [x] Geometry validity checks
- [x] Parameter-driven regeneration
- [x] Topology-change diagnostics (no edge substitution)
- [x] Save/load round-trip
- [x] Undo/redo regression
- [x] STEP/STL export regression
- [x] Analytic volume validation
- [x] Failure diagnostics
- [x] Evidence under `docs/verification/P11-FEAT-002/`

**Acceptance (met):**

- A single-edge chamfer of a 100 × 50 × 20 mm block matches V0 − ½d²L.
- Multiple, adjacent and chained edges and revolved rims match analytic
  volumes.
- Changing upstream dimensions regenerates the chamfer. Edges that move or
  split fail with structured diagnostics, never with a substituted edge.
- Chamfers that do not fit are refused before the kernel, which crashes on
  them in this toolchain.
- Save → destroy → load → regenerate reproduces the definitions, stable IDs
  and geometry.
- Existing P0–P11-FEAT-001 regression suite remains green.

**Known limitation:** edge references are geometric (the edge's supporting
line or circle), not semantic topology naming, which stays under "Later".

#### P11-FEAT-003 — Fillet — [evidence](docs/verification/P11-FEAT-003/README.md)

- [x] Fillet feature definition
- [x] Edge references (P11-FEAT-002 geometric signatures, reused)
- [x] Constant-radius fillet
- [x] Single-edge fillet
- [x] Multiple-edge fillet
- [x] Adjacent-edge behavior (two- and three-edge corners)
- [x] Tangent-chain fillet
- [x] Concave-edge fillet
- [x] Fillet of extruded and revolved bodies
- [x] Geometry validity checks
- [x] Radius/preflight validation
- [x] Parameter-driven regeneration
- [x] Topology-change diagnostics
- [x] Save/load round-trip
- [x] Undo/redo regression
- [x] STEP/STL export regression
- [x] Analytic volume validation
- [x] Failure diagnostics
- [x] Evidence under `docs/verification/P11-FEAT-003/`

**Acceptance (met):**

- A single-edge fillet of a 100 × 50 × 20 mm block matches
  V0 − L r²(1 − π/4).
- Independent, adjacent (two- and three-edge corners), chained, concave and
  revolved-rim fillets match closed-form volumes.
- Changing upstream dimensions or the radius regenerates the fillet. Edges
  that move or split fail with structured diagnostics, never with a
  substituted edge.
- Radii that do not fit are refused before the kernel, which crashes on
  them in this toolchain.
- Save → destroy → load → regenerate reproduces the definitions, stable IDs
  and geometry.
- Existing P0–P11-FEAT-002 regression suite remains green.

**Not implemented:** variable-radius fillets and setback/corner-transition
controls. **Known limitation:** edge references are geometric, as for
Chamfer.

#### P11-FEAT-004 — Hole feature — [evidence](docs/verification/P11-FEAT-004/README.md)

- [x] Hole feature definition
- [x] Planar-face placement/reference
- [x] Through hole
- [x] Blind hole
- [x] Counterbore
- [x] Countersink
- [x] Diameter/depth parameters
- [x] Direction/orientation
- [x] Hole on extruded and revolved bodies
- [x] Geometry validity checks
- [x] Parameter-driven regeneration
- [x] Topology-change diagnostics
- [x] Save/load round-trip
- [x] Undo/redo regression
- [x] STEP/STL export regression
- [x] Analytic volume validation
- [x] Failure diagnostics
- [x] Evidence under `docs/verification/P11-FEAT-004/`

**Acceptance (met):**

- A 10 mm through hole in a 100 × 50 × 20 mm block matches LWH − π(d/2)²H,
  and a blind hole V0 − π(d/2)²h. Counterbores and countersinks (the latter
  checked by two independent frustum derivations) match closed-form
  volumes.
- A through hole stays through when the block goes from 20 to 40 mm thick.
  A blind hole keeps its depth, and one that would reach the far side is
  refused.
- Diameter, depth and position parameters regenerate only the hole. Faces
  that move, vanish or become ambiguous fail with structured diagnostics,
  never with a substituted face.
- Holes that would break out of their face's side are refused (policy:
  contained holes only).
- Save → destroy → load → regenerate reproduces the definitions, stable IDs
  and geometry.
- Existing P0–P11-FEAT-003 regression suite remains green.

**Not implemented:** threads, drill points, spotface, standards databases,
tolerance classes and cosmetic threads. **Known limitation:** face references
are geometric (a face's plane and side), not semantic topology naming.

#### P11-FEAT-005 — Linear pattern — [evidence](docs/verification/P11-FEAT-005/README.md)

- [x] Linear Pattern feature definition
- [x] Stable source feature/body reference
- [x] Direction representation
- [x] Instance count
- [x] Instance spacing
- [x] One-direction pattern
- [x] Two-direction rectangular pattern
- [x] Deterministic instance placement
- [x] Additive feature pattern
- [x] Subtractive/Hole pattern
- [x] Geometry validity checks
- [x] Parameter-driven regeneration
- [x] Atomic failure diagnostics
- [x] Save/load round-trip
- [x] Undo/redo regression
- [x] STEP/STL export regression
- [x] Analytic volume validation
- [x] Failure diagnostics
- [x] Evidence under `docs/verification/P11-FEAT-005/`

**Acceptance (met):**

- The count includes the source; instance i is the source moved by
  i · spacing · d̂, computed afresh for every instance (100 instances land
  exactly on k × s).
- Four 10 mm cubes 20 mm apart give 4000 mm³ and a 70 mm extent. Five
  10 mm through holes 20 mm apart in a 120 × 50 × 20 mm block match
  V0 − 5πr²H. Bosses, pockets, chamfered and filleted rims, a revolve and a
  grid match closed-form volumes.
- Changing the source, the count, the spacing or the direction regenerates
  every instance; through holes stay through.
- An instance that fails fails the whole pattern, naming the instance; no
  partial pattern is kept.
- Save → destroy → load → regenerate reproduces the definition, the source's
  ID and the geometry.
- Existing P0–P11-FEAT-004 regression suite remains green.

**Not implemented:** symmetric and total-length modes, suppressed instances,
patterns of patterns. **Known limitation:** building time grows with the
square of the count (at most 500 instances); references stay geometric.

#### P11-FEAT-006 — Circular pattern — [evidence](docs/verification/P11-FEAT-006/README.md)

- [x] Circular Pattern feature definition
- [x] Stable source feature/body reference
- [x] Axis representation (explicit origin and direction, any 3D axis)
- [x] Instance count
- [x] Full-circle equal-spacing pattern
- [x] Partial-angle pattern (included angle; also an explicit angle step)
- [x] Positive/negative direction
- [x] Deterministic instance rotation
- [x] Body pattern
- [x] Additive feature pattern
- [x] Subtractive/Hole pattern
- [x] Geometry validity checks
- [x] Parameter-driven regeneration
- [x] Atomic failure diagnostics
- [x] Save/load round-trip
- [x] Undo/redo regression
- [x] STEP/STL export regression
- [x] Analytic/geometric validation
- [x] Failure diagnostics
- [x] Evidence under `docs/verification/P11-FEAT-006/`

**Acceptance (met):**

- The count includes the source. Instance i is the source turned by θ_i,
  computed afresh for every instance: 2πi/N for a full circle (never 360°),
  iα/(N − 1) over an included angle, iα for an angle step. With 100
  instances, θ₉₉ = 356.4° to 3.4e-14°.
- Six 10 mm holes on a 40 mm bolt circle in a 60 × 10 mm flange give 34500π
  mm³ (2.7e-16 relative), each at 40·(cos 60i°, sin 60i°). Cubes, bosses,
  pockets, chamfered and filleted rims, a revolve, and pegs about X, Y, Z
  and (1, 1, 1) match closed-form volumes and positions.
- Changing the source, the count, the angle, the direction or the axis
  regenerates every instance; through holes stay through.
- An instance that fails fails the whole pattern, naming the instance and
  its angle; no partial pattern is kept.
- Save → destroy → load → regenerate reproduces the definition, the source's
  ID and the geometry bit for bit.
- Linear and circular patterns share one pattern subsystem. The existing
  P0–P11-FEAT-005 regression suite remains green.

**Not implemented:** symmetric patterns, suppressed instances, patterns of
patterns, and axis references to sketch lines, edges or datum axes (the axis
is explicit model coordinates). **Known limitation:** building time grows
with the square of the count (at most 500 instances); references stay
geometric.

## Current

### P11 — Production Part Modeling

#### P11-FEAT-007 — Mirror ← NEXT

- [ ] Not started.

## Next

### P11 — Production Part Modeling (remaining)

#### P11-FEAT-008 — Sweep

- [ ] Not started.

#### P11-FEAT-009 — Loft

- [ ] Not started.

#### P11-REF-001 — Mechanical reference models

- [ ] Shaft
- [ ] Flange
- [ ] Pulley
- [ ] Bearing housing
- [ ] Mounting bracket

#### P11-QUAL-001 — Qualification

- [ ] Debug build
- [ ] Release build
- [ ] Debug-shared build
- [ ] Zero compiler warnings
- [ ] All legacy tests pass
- [ ] All P11 tests pass
- [ ] Reference models regenerate deterministically
- [ ] Evidence recorded

## Later — Do Not Start Yet

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
