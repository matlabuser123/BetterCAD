# BetterCAD — TODO

**This file is the authority for what is implemented and for what happens
next.** A capability listed in [ROADMAP.md](ROADMAP.md) is not status. A
subsystem described in [ARCHITECTURE.md](ARCHITECTURE.md) is not status. If any
document disagrees with this one about what exists, this one is right — and if
this one disagrees with the evidence under
[docs/verification/](docs/verification/), the evidence is right and this file
is wrong.

## Rules

```text
[x] = implemented
    + tested
    + validated
    + deterministic regression
    + evidence recorded
```

`[ ]` means not complete, for any reason. Nothing is ticked because code was
written, a file exists, something compiles, a test was skipped or mocked, or a
result looks correct. Every `[x]` below links to the evidence that earned it.
Documentation work never ticks a box. Blocked items say so, with the reason.
See [CLAUDE.md](CLAUDE.md) for how milestones are executed and recorded.

Milestone IDs — `P11`, `P11-FEAT-003`, `P11-QUAL-001` — are allocated **here**,
in sequence, at the moment work is authorized. [ROADMAP.md](ROADMAP.md) names
capabilities but never numbers them and never allocates an ID.

## Status

```text
Current:           None
Next:              Awaiting explicit scope decision
Blocked / Manual:  None
```

Last milestone: **P11 Production Part Modeling — QUALIFIED** at revision
`79dab04` ([evidence](docs/verification/P11-QUAL-001/README.md)). 20 gates, 20
passed, 0 failed, 0 blocked: 738/738 tests and 0 compiler warnings over 274
translation units in each of Debug, Release and Debug-shared, each rebuilt
clean; 301/301 legacy tests unchanged; six reference models bit-identical
across all three configurations and six fresh processes.

Released: `v0.1.0` covers `P0`–`P10`. `P11` is qualified but not released;
there is no `v0.2.0`.

## Current

Nothing is in progress. No milestone is open, and no source file is being
changed.

## Next

**Awaiting an explicit scope decision.** The next milestone is a decision about
scope, not a feature. It must be recorded here — with its ID, its deliverables
and its acceptance gates — before any implementation begins.

[ROADMAP.md](ROADMAP.md) lists the candidate capabilities and the dependency
order between them, including a note on whether a minimal stable-reference
layer should be pulled forward ahead of assemblies and drawings. Being listed
there, or being next in that order, does not authorize anything.

## Planned / Not Authorized

Not started, not authorized, in no fixed order. See [ROADMAP.md](ROADMAP.md)
for what each of these means and what it depends on.

- [ ] Desktop CAD application (the executable today is a placeholder shell)
- [ ] Assemblies
- [ ] Semantic topology naming
- [ ] Technical drawings
- [ ] STEP import
- [ ] DXF
- [ ] Datum geometry, configurations and design equations
- [ ] Shell, draft and rib features
- [ ] Parameter expression evaluation
- [ ] Materials and engineering data
- [ ] GPU renderer
- [ ] Meshing
- [ ] FEA
- [ ] Thermal
- [ ] CFD
- [ ] Optimization
- [ ] Python API
- [ ] Collaboration / engineering version control
- [ ] AI engineering agent
- [ ] CAM / manufacturing

## Recently Completed

| Milestone | Commit | Date | Evidence |
| --- | --- | --- | --- |
| P11-QUAL-001 Qualification | `449d5fd` | 2026-09-16 | [evidence](docs/verification/P11-QUAL-001/README.md) |
| P11-REF-001 Mechanical reference models | `79dab04` | 2026-09-16 | [evidence](docs/verification/P11-REF-001/README.md) |
| P11-FEAT-009 Loft | `24a8134` | 2026-09-15 | [evidence](docs/verification/P11-FEAT-009/README.md) |
| P11-FEAT-008 Sweep | `0a59e4c` | 2026-09-15 | [evidence](docs/verification/P11-FEAT-008/README.md) |
| P11-FEAT-007 Mirror | `97d426f` | 2026-09-15 | [evidence](docs/verification/P11-FEAT-007/README.md) |

Full detail for every milestone, including the ones above, is below.

## Completed Milestones

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

Release `v0.1.0` is `P0`–`P10`: the annotated tag `v0.1.0` (tag object
`93d84f0`) points at commit `2da8966`. Evidence written before a milestone may
cite either hash; they are the same release.

### P11 — Production Part Modeling (complete and qualified)

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

#### P11-FEAT-007 — Mirror — [evidence](docs/verification/P11-FEAT-007/README.md)

- [x] Mirror feature definition
- [x] Stable source feature/body reference
- [x] Explicit 3D mirror-plane representation (origin, normal, offset; the
  offset literal or driven by a length parameter)
- [x] Arbitrary-plane reflection (including offset planes)
- [x] Body mirror (keep original or mirror image only)
- [x] Additive feature mirror
- [x] Subtractive/Hole mirror (all hole types and extents; cut extrudes and
  revolves)
- [x] Mirror of extruded, revolved, chamfered and filleted sources
- [x] Deterministic reflection transform
- [x] Geometry validity checks (orientation-safe mirror images)
- [x] Parameter-driven regeneration
- [x] Atomic failure diagnostics
- [x] Save/load round-trip
- [x] Undo/redo regression
- [x] STEP/STL export regression
- [x] Analytic/geometric validation
- [x] Failure diagnostics
- [x] Evidence under `docs/verification/P11-FEAT-007/`

**Acceptance (met):**

- Every point goes to p' = p − 2((p − p0)·n̂)n̂ (a Householder reflection,
  det −1), checked against the formula, M(M(p)) = p and the signed-distance
  invariant. (4, 1, 3) across the plane through (1, 0, 0) with normal
  (1, 1, 0) lands 1.2e-15 mm from (0, −3, 3).
- A 10 mm cube centred at (20, 0, 0) mirrors across x = 0 to (−20, 0, 0),
  with 1000 mm³ each and X bounds [−25, 25]. Across x = 10 a cube at x = 30
  goes to −10. A double mirror returns the source. Volume, area and
  centroid are preserved across skew planes.
- A 10 mm through hole at x = 30 in a 100 × 50 × 20 mm block, mirrored
  across x = 50, gives a hole at x = 70 and V0 − 2πr²H (1.5e-16 relative).
  A boss pair matches V_plate + 2V_boss. Counterbores, countersinks, blind
  holes, chamfered and filleted rims, a revolve and a revolved cut match
  closed-form volumes.
- Changing the source or the plane (offset parameter, origin, normal)
  regenerates the mirror, and through holes stay through. An image that
  fails fails the whole mirror, naming the image, its plane and the cause.
- Mirror images of solids keep their faces pointing outward: a face-normal
  bug for left-handed plane frames was found and fixed, with a regression
  proof. STL meshes of mirrored bodies are closed and outward-facing.
- Save → destroy → load → regenerate reproduces the definition, the source's
  ID, the plane bit for bit and the geometry.
- The mirror shares the pattern subsystem and transform infrastructure. The
  existing P0–P11-FEAT-006 regression suite remains green.

**Not implemented:** mirror planes referencing datum planes, planar faces or
sketch planes (the plane is explicit model coordinates); named global
planes; feature mirrors of patterns or mirrors, and patterns of mirrors (a
body mirror of a pattern works). **Known limitation:** chamfer, fillet and
hole images use geometric references reflected exactly, not semantic
topology naming.

#### P11-FEAT-008 — Sweep — [evidence](docs/verification/P11-FEAT-008/README.md)

- [x] Sweep feature definition
- [x] Stable profile reference (the profile sketch's ID; its closed regions)
- [x] Stable path representation/reference (a sketch's ID and an ordered
  list of its line, arc and circle entity IDs; no kernel wire is stored)
- [x] Straight-path sweep
- [x] Curved-path sweep (arcs; a full circle as a closed path)
- [x] Connected multi-segment path (mitred line corners, tangent line/arc
  joints; closed chains)
- [x] Closed-profile solid sweep (profiles with holes; open profiles refused)
- [x] Deterministic orientation policy (follow path: fixed path-plane
  binormal, no twist)
- [x] Profile/path compatibility validation (the path starts on the
  profile's plane and leaves it at right angles)
- [x] New-body operation
- [x] Add/remove/intersect operation
- [x] Geometry validity checks (preflight against folding and short mitre
  legs; self-interference check; Pappus volume check of every result)
- [x] Parameter-driven regeneration (profile, path and target)
- [x] Atomic failure diagnostics
- [x] Save/load round-trip
- [x] Undo/redo regression
- [x] STEP/STL export regression
- [x] Analytic/geometric validation (Sweep vs Extrude, Sweep vs Revolve)
- [x] Failure diagnostics
- [x] Evidence under `docs/verification/P11-FEAT-008/`

**Acceptance (met):**

- A 10 × 20 mm rectangle swept along a straight 100 mm path gives
  20000 mm³, and a circle r = 5 mm gives 2500π; both equal the extrusion of
  the same profile, and a reversed path sweeps the other way.
- A circle r = 2 mm swept a quarter turn of radius 20 mm gives 40π² with
  its centroid where Pappus puts it; around a full circle it gives the torus
  2π²Rr² = 160π², equal to the revolve of the same profile for R = 20 and
  30 mm.
- A mitred L polyline and a line–tangent arc–line path give A·L; the
  profile keeps its orientation (no twist) along arcs.
- New body, join, cut and intersect match closed-form volumes (a handle, a
  boss, a through channel, a curved blind groove, a clipped rod).
- Changing the profile, the path or the target regenerates the sweep with
  no stale geometry; open profiles, disconnected or branching paths,
  zero-length edges, corners at arcs, misplaced profiles, folding and
  self-intersecting sweeps fail with structured diagnostics, before the
  kernel where possible, and nothing is committed.
- Save → destroy → load → regenerate reproduces the definition, the profile
  and path IDs in order, and the geometry bit for bit. STEP read-back and
  closed STL meshes match.
- The existing P0–P11-FEAT-007 regression suite remains green.

**Not implemented:** guide curves, twist, scale along the path, variable
sections, multiple profiles, thin-wall and surface sweeps, corner
transition controls, other orientation modes, and non-planar paths (helices,
splines, 3D edge chains or model edges). **Known limitations:** the path is
one planar sketch; only straight segments may meet at a (mitred) corner, and
arcs must join tangentially; the profile must sit on the path's start plane;
feature-scope patterns and mirrors do not take a sweep as their source.

#### P11-FEAT-009 — Loft — [evidence](docs/verification/P11-FEAT-009/README.md)

- [x] Loft feature definition
- [x] Stable ordered profile references (sketch IDs, each with an offset
  along its plane's normal, literal or driven by a length parameter; the
  order kept as given)
- [x] Two-profile loft
- [x] Multi-section loft (three or more sections, ruled piecewise)
- [x] Different profile sizes
- [x] Offset profile sections
- [x] Differently oriented sketches on parallel planes (turned axes,
  reversed normals, turned sections)
- [x] Closed-profile solid loft (lines, arcs of equal sweep, circles; open
  profiles refused)
- [x] Deterministic section correspondence (least twist from the centroids,
  ties to the first start, winding normalized)
- [x] New-body operation
- [x] Add/remove/intersect operation
- [x] Geometry validity checks (fold preflight, self-interference check,
  prismatoid volume check of every result)
- [x] Parameter-driven regeneration (section geometry, spacing, offset,
  target)
- [x] Atomic failure diagnostics
- [x] Save/load round-trip (section order preserved)
- [x] Undo/redo regression
- [x] STEP/STL export regression
- [x] Analytic/geometric validation (Loft vs Extrude, Loft vs Revolve)
- [x] Failure diagnostics
- [x] Evidence under `docs/verification/P11-FEAT-009/`

**Acceptance (met):**

- Two 10 × 20 mm rectangles 100 mm apart loft to 20000 mm³, and two
  circles r = 5 mm to 2500π; both equal the extrusion of the same profile.
- Circles r 10 → r 5 over 30 mm give πh/3 (r1² + r1r2 + r2²) = 1750π, equal
  to the revolve of the matching trapezoid, at r2 = 5, h = 30 and after
  r2 = 8, h = 60. Similar rectangles and hexagons give
  h/3 (A1 + A2 + √(A1A2)), three and four circular sections the sum of
  their frustums, and an offset frustum the same volume (Cavalieri).
- Sections match with the least twist, whatever corner a rectangle is drawn
  from or which way round; ties go to a fixed start under rounding noise; a
  section turned by θ follows V = hA (2 + cos θ)/3.
- New body, join, cut and intersect match closed-form volumes (a tapered
  boss, a tapered blind hole, its intersection with the block).
- Changing a section's size, the spacing, a section's position or the
  target regenerates the loft with no stale section; open, multiple or
  holed profiles, sections of different shapes, non-parallel, coincident
  or out-of-order sections, folding and self-intersecting lofts fail with
  structured diagnostics, before the kernel where possible, and nothing is
  committed.
- Save → destroy → load → regenerate reproduces the definition, the section
  order (also against the sketches' ID order) and the geometry bit for bit.
  STEP read-back and closed STL meshes match.
- The existing P0–P11-FEAT-008 regression suite remains green.

**Not implemented:** sections of different shapes (circle to polygon,
different corner counts), sections on non-parallel planes, smooth
interpolation, guide curves, centreline lofts, tangency or curvature end
conditions, sections with holes, thin-wall and surface lofts, closed lofts.
**Known limitations:** a loft's sides are B-spline faces for the kernel, so
plane references find only its end faces; ties in the matching go to the
first start of the loop; a section's corners must match one to one (a side
split in two is another shape); the volume check cannot see a wrong matching
that keeps the volume (BetterCAD's matching never builds one).

#### P11-REF-001 — Mechanical reference models — [evidence](docs/verification/P11-REF-001/README.md)

- [x] Shaft — [evidence](docs/verification/P11-REF-001/shaft/values-release.txt)
- [x] Flange — [evidence](docs/verification/P11-REF-001/flange/values-release.txt)
- [x] Pulley — [evidence](docs/verification/P11-REF-001/pulley/values-release.txt)
- [x] Bearing housing — [evidence](docs/verification/P11-REF-001/bearing_housing/values-release.txt)
- [x] Mounting bracket — [evidence](docs/verification/P11-REF-001/mounting_bracket/values-release.txt)

The five parts above are the milestone's deliverable. A swept U-bolt was built
alongside them so the sweep feature has a natural use; it is validated,
fingerprinted, exported and regression-tested exactly like the five
([evidence](docs/verification/P11-REF-001/u_bolt/values-release.txt)), which is
why six models appear throughout the qualification.

**Acceptance (met):**

- Five production parts, built only through the public document, parameter,
  sketch and feature APIs: a stepped shaft, a bolted flange, a V-belt pulley,
  a pillow block and an L bracket, with a swept U-bolt alongside them so the
  sweep feature has a natural use. Between them they use every P11 feature.
- Each part's volume, area, centroid and bounds match geometry computed
  independently from its dimensions (cross-sections of lines and arcs
  integrated with Green's theorem, and Pappus' theorems), at every step of
  its feature chain: within 1.2e-13 relative for the parts bounded by
  planes, cylinders, cones and tori, and 6.0e-12 for the bracket, whose
  lofted gusset the kernel keeps as B-spline faces.
- Ten full regenerations and three fresh builds of every model give
  bit-identical fingerprints (items, IDs, topology, volume, area, centroid,
  bounds); building all six in one process gives the same models as building
  each alone; five rounds of building and discarding them all leave nothing
  behind.
- Changing a main dimension of each model regenerates it to the expected new
  geometry, and restoring the dimension restores the model to rounding.
- Save → destroy → load → regenerate reproduces every model exactly, and
  `examples/models/reference/*.bcad` stay byte-identical to what the builders
  save.
- STEP read-back and closed, outward-facing STL meshes agree with the exact
  volumes; the CLI loads, validates and exports every model.
- Invalid parameters (a fillet that does not fit, a hole off the edge, a bolt
  circle through the rim, a bore wider than the hub, a bore that eats the
  boss, a chamfer wider than the rod) fail with structured diagnostics,
  commit nothing, and recover when the parameter is restored.
- The existing P0–P11-FEAT-009 regression suite remains green: Debug, Release
  and Debug-shared each rebuilt clean, 738/738 tests and 0 compiler warnings
  over 274 translation units in each.

**Known limitations:** geometric references do not follow geometry a
parameter moves (the shaft's tail chamfer and the flange's rim chamfer show
it, and say so instead of taking another edge); uniting a half body with its
mirror image is refused when a half cylinder lies on the mirror plane, so the
housing cuts its bore after joining its halves; parameter expressions are
still not evaluated, so a derived dimension needs its own parameter or a
sketch that builds the relation; an extrude has no through-all mode; a loft's
B-spline sides cost about 6e-12 in volume and 3.4e-6 mm in the centroid.

#### P11-QUAL-001 — Qualification — [evidence](docs/verification/P11-QUAL-001/README.md)

- [x] Debug build — 274 translation units, 0 warnings, 738/738 tests
- [x] Release build — 274 translation units, 0 warnings, 738/738 tests
- [x] Debug-shared build — 274 translation units, 0 warnings, 738/738 tests
- [x] Zero compiler warnings — 0 compiler, 0 CMake, 0 other, over all three
      clean build logs, counted as GCC prints diagnostics; 22 warning flags
      plus `-Werror`, and no `-Wno-` flag or diagnostic pragma anywhere
- [x] All legacy tests pass — 301/301 in each configuration
- [x] All P11 tests pass — 417/417 in each configuration, plus the 20 core
      tests P11 added; 0 unclassified
- [x] Reference models regenerate deterministically — 700 repeat executions,
      six fresh processes across three builds giving one identical
      fingerprint digest, 0 relative and 0 mm across configurations
- [x] Evidence recorded — [docs/verification/P11-QUAL-001/](docs/verification/P11-QUAL-001/README.md)

**Acceptance (met):** 20 gates, 20 passed, 0 failed, 0 blocked, against the
frozen revision `79dab04`. Three clean rebuilds (Debug, Release,
Debug-shared) each compiled all 274 translation units with zero diagnostics
and ran the complete 738-test suite to 738 passes. The failure-path and
atomic-regeneration gate passed 124/124, including 13 tests requiring that
unit and identity misuse fails to compile. The CLI smoke ran 28 commands
through the shipped executable: 27 succeeded and the intended failure was
reported as a diagnostic, not a crash. All ten P11 milestones have complete
committed evidence (171 files, no placeholders). No production code was
changed during the run and no test failed at any point.

**Known limitations:** unchanged by this run — geometric references do not
follow moved geometry; a half cylinder on a mirror plane blocks the union;
parameter expressions are not evaluated; no through-all extrude; loft sides
stay B-splines. As evidence this qualification is one platform only
(Windows, GCC 16.1, OCCT 8.0.1), with no CI, no sanitizers, no coverage, no
enforced formatting check, and no GUI qualification.

## Known Limitations

Properties of the system as qualified, so the completed milestones above are
not read as more than they are. Each modeling limitation is pinned by a
regression test, so none can change silently. Full detail:
[docs/verification/P11-QUAL-001/README.md](docs/verification/P11-QUAL-001/README.md).

### Modeling

- **Geometric references do not follow moved geometry.** Edges and faces are
  matched by their geometry — an edge's supporting line or circle, a face's
  plane and outward side — not named semantically. When a parameter moves the
  edge a chamfer or fillet was attached to, or the face a hole was placed on,
  the feature fails with `NotFound` and keeps no body. No other entity is ever
  substituted. The shaft's tail chamfer and the flange's rim chamfer
  demonstrate both sides of this. Semantic topology naming is not authorized;
  see the dependency note in [ROADMAP.md](ROADMAP.md).
- **A half bore on a mirror plane.** Uniting a half body with its mirror image
  is refused when a half cylinder lies on the mirror plane: the kernel's fuse
  returns a shape its own checker rejects, so BetterCAD refuses it rather than
  building it wrongly. The bearing housing therefore cuts its bore after
  joining its halves. Pinned by a boolean regression test.
- **Parameter expressions are not evaluated** (`P1-003`). Expressions are
  stored but not computed, so a derived dimension needs its own parameter or a
  sketch that builds the relation geometrically.
- **No through-all extrude.** A cut is given a depth; the housing's bore is
  driven by the same width parameter as the housing.
- **Loft faces stay B-splines.** The kernel keeps a loft's sides as B-spline
  surfaces even where they are flat, so the bracket agrees with exact geometry
  to 6.0e-12 relative and 3.4e-6 mm rather than to rounding, and plane
  references find only a loft's end faces.
- **Sketch constraints** cover coincident, horizontal, vertical, parallel,
  perpendicular, distance, radius, equal and fixed. There is no angle, tangent,
  midpoint or symmetry constraint; symmetry is built with construction geometry
  and equal constraints.
- **Pattern size.** Building time grows with the square of the instance count,
  which is capped at 500.

### Not implemented

- **The desktop application is a placeholder shell.** `bettercad.exe` builds in
  all three configurations; no GUI functionality is claimed and no GUI smoke
  test exists. The parametric workflow is exercised through the core and the
  CLI.
- **STEP export only.** There is no STEP import. The kernel-based STEP reader
  under `tests/support/occt/` is test-only tooling that reads exports back to
  check them. DXF, IGES and OBJ do not exist in either direction.
- Everything under *Planned / Not Authorized* above.

### Limits of the evidence

- **One platform.** Windows 11 AMD64, GCC 16.1.0 (MinGW-w64), Open CASCADE
  8.0.1, Qt 6.11.2. No MSVC, Clang, Linux or macOS result is claimed.
- **No CI.** There is no `.github/workflows`, so nothing re-runs the suite
  automatically; qualification is a deliberate act, and a regression between
  milestones would not be caught until the next one.
- **No sanitizers, no coverage, no memory checking.** No ASan/UBSan preset, no
  gcov/lcov, no valgrind or Dr. Memory configuration. Memory errors that do not
  crash, and code no test reaches, would not be reported.
- **`.clang-format` is not enforced.** The file defines the house style, but no
  test, target or hook checks it.
- **The suite defines the ceiling.** 738 tests passing means 738 tests passed.
  Where a behaviour has no test, the evidence says nothing about it.

## Project Documents

- [README.md](README.md) — project overview and getting started
- [ROADMAP.md](ROADMAP.md) — long-term capability direction
- [TODO.md](TODO.md) — authoritative implementation status and next work (this document)
- [ARCHITECTURE.md](ARCHITECTURE.md) — system architecture and dependency rules
- [CLAUDE.md](CLAUDE.md) — engineering workflow and verification rules
- [docs/verification/](docs/verification/) — evidence for every completed milestone
