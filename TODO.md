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

## Current

### P11 — Production Part Modeling

#### P11-QUAL-001 — Qualification ← NEXT

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
