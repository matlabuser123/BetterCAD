# BetterCAD — TODO

> `[x]` = implemented **+** tested **+** validated **+** evidence recorded.
> Nothing else earns a tick. Documentation work never ticks a box.

This file authorizes implementation. [ROADMAP.md](ROADMAP.md) does not: a
capability listed there is not authorized by being listed, by being next, or by
being easy. Milestone IDs are allocated here, in sequence, when work is
authorized — never in advance.

## Status

| | |
| --- | --- |
| Current | **`P12` — Parametric CAD Completion** |
| Next | `P12-FEAT-006` — Variable-radius fillet, setback, corner transitions |
| Blocked / Manual | None |
| Last qualified | `P11` Production Part Modeling — **QUALIFIED** |
| Released | `v0.1.0` (`P0`–`P10`); `P11` qualified, not released |

## Current

### P12 — Parametric CAD Completion

Authorized scope: the capabilities that complete parametric part modeling.
Nothing else. Assemblies, drawings, desktop GUI, the full semantic-topology
phase, STEP import, materials, meshing, FEA, thermal, CFD, optimization, Python,
version control, AI and CAM all remain unauthorized.

**Implementation order is dependency-driven, not the order the scope was
listed.** The reasoning:

```text
Parameter expressions          core, layer 0, depends on nothing
        ↓                      unblocks driven dimensions everywhere
Sketch constraints/entities    layer 1; richer profiles
        ↓
Datum geometry                 first-class reference inputs
        ↓
Sketch on planar face          needs stable references — RISK, see below
        ↓
Feature work                   layer 2; consumes all of the above
        ↓
Design equations/configurations  needs expressions AND features
        ↓
Reference models → qualification
```

| # | Milestone | Depends on | Status |
| --- | --- | --- | --- |
| 1 | `P12-PARAM-001` Parameter expression evaluation | — | **done** — [evidence](docs/verification/P12-PARAM-001/README.md) |
| 2 | `P12-SKETCH-001` Constraints: angle, tangent, concentric, midpoint, symmetric, diameter | `PARAM-001` | **done** — [evidence](docs/verification/P12-SKETCH-001/README.md) |
| 3 | `P12-SKETCH-002` Entities: ellipse, spline | `SKETCH-001` | **done** — [evidence](docs/verification/P12-SKETCH-002/README.md) |
| 4 | `P12-DATUM-001` Datum planes, axes, coordinate systems | `PARAM-001` | **done** — [evidence](docs/verification/P12-DATUM-001/README.md) |
| 4a | `P12-STREF-001` Stable feature face references (prerequisite, authorized 2026-09-17) | `DATUM-001` | **done** — [evidence](docs/verification/P12-STREF-001/README.md) |
| 5 | `P12-SKETCH-003` Sketches on arbitrary planar faces | `DATUM-001`, `STREF-001` | **done** — [evidence](docs/verification/P12-SKETCH-003/README.md) |
| 6 | `P12-FEAT-001` Through-all extrude | — | **done** — [evidence](docs/verification/P12-FEAT-001/README.md) |
| 7 | `P12-FEAT-002` Split body / combine | — | **done** — [evidence](docs/verification/P12-FEAT-002/README.md) |
| 8 | `P12-FEAT-003` Shell | `FEAT-002` | **done** — [evidence](docs/verification/P12-FEAT-003/README.md) |
| 9 | `P12-FEAT-004` Draft | `DATUM-001` | **done** — [evidence](docs/verification/P12-FEAT-004/README.md) |
| 10 | `P12-FEAT-005` Rib | `SKETCH-002`, `FEAT-001` | **done** — [evidence](docs/verification/P12-FEAT-005/README.md) |
| 11 | `P12-FEAT-006` Variable-radius fillet, setback, corner transitions | — | not started |
| 12 | `P12-HOLE-001` Threads, spotface, standard sizes, tolerance classes | `PARAM-001` | not started |
| 13 | `P12-PATTERN-001` Symmetric, total-length, suppressed instances, patterns of patterns | `PARAM-001` | not started |
| 14 | `P12-SWEEP-001` Guide curves, twist, non-planar paths | `SKETCH-002` | not started |
| 15 | `P12-LOFT-001` Differing section shapes, smooth interpolation, end conditions | `SKETCH-002` | not started |
| 16 | `P12-PARAM-002` Design equations and configurations | `PARAM-001`, features | not started |
| 17 | `P12-REF-001` Production reference models | all above | not started |
| 18 | `P12-QUAL-001` Phase qualification | all above | not started |

**Numbering note.** The proposed plan used `P12-REF-001` for datum geometry and
`P12-REF-002` for reference models. In `P11`, `REF` means *reference models*
(`P11-REF-001`). Datum geometry is therefore `P12-DATUM-001` and reference
models keep `P12-REF-001`, so `REF` means one thing across phases. Feature
milestones are numbered in dependency order rather than in the order the scope
was listed. No historical milestone is renumbered.

**Stable-reference risk.** `P12-SKETCH-003` (sketches on planar faces), and to a
lesser degree shell, draft and variable fillets, lean on face and edge
references that today are geometric signatures and do not follow geometry a
parameter moves. Persisting transient kernel face indices is forbidden, and
substituting a different face is forbidden. If a milestone cannot be completed
safely within the current reference architecture, it stops, documents the exact
failing requirement, and proposes a minimal prerequisite — it does not expand
into the semantic-topology phase, which is not authorized.

#### P12-PARAM-001 — Parameter expression evaluation

Deliverables:

- [x] Unit-aware expression grammar: literals with units, parameter references, `+ - * /`, unary minus, parentheses, precedence
- [x] Dimensional analysis at evaluation: `Length + Angle` fails structurally; `Length / Length` is dimensionless
- [x] Expression dependencies participate in the document dependency graph
- [x] Cycle detection across expression references
- [x] Unknown-symbol, malformed-syntax and dimension diagnostics naming the offending token
- [x] Deterministic evaluation order
- [x] Failure propagation: a parameter whose expression fails keeps its last value and is reported
- [x] Save/load round-trip of expressions and evaluated values
- [x] Evidence: [docs/verification/P12-PARAM-001/](docs/verification/P12-PARAM-001/README.md) — PASS, 777/777 tests in Debug, Release and Debug-shared, 0 warnings

Acceptance:

- `height = width / 2`, `thickness = 0.1 * width`, `hole_spacing = width - 2 * edge_distance` evaluate correctly and re-evaluate when `width` changes.
- Dimensional errors, unknown symbols and cycles are refused with structured diagnostics; nothing is silently coerced.
- The unit-safe parameter system is not reduced to raw doubles anywhere.
- The existing `P0`–`P11` regression suite stays green in all three presets.

#### P12-SKETCH-001 — Sketch constraints: angle, tangent, concentric, midpoint, symmetric, diameter

Deliverables:

- [x] Angle: two lines, counter-clockwise from the first, in (0°, 180°), literal or driven by an angle parameter
- [x] Tangent: a line and a circle or arc, or two circles or arcs (external or internal contact)
- [x] Concentric: two circles or arcs
- [x] Midpoint: a point halfway along a line
- [x] Symmetric: two points mirrored across a line
- [x] Diameter: a circle or arc, literal or driven by a length parameter
- [x] Solver equations with analytic Jacobians; degrees of freedom, redundancy and conflicts diagnosed for the new types
- [x] Reference validation and canonical entity order at creation; structured errors
- [x] Driving parameters (angles for angle constraints), regeneration, validation
- [x] Undoable sketch edits (`ModifySketchCommand`)
- [x] Save/load round-trip of the new constraints
- [x] Evidence: [docs/verification/P12-SKETCH-001/](docs/verification/P12-SKETCH-001/README.md) — PASS, 804/804 tests in Debug, Release and Debug-shared, 0 warnings

Acceptance:

- Each constraint holds after solving, checked with geometry computed independently from the solved points (atan2 angles, point-line distances, reflections), to the solver's tolerance.
- Each constraint removes exactly its number of equations from the degrees of freedom; redundant and conflicting combinations are diagnosed with the constraint IDs and change nothing.
- Profiles built from the new constraints (a regular hexagon, an obround slot, a wedge) have the analytic area, and driven angles and diameters rebuild features with the analytic volume.
- `P0`–`P12-PARAM-001` stays green in all three presets.

#### P12-SKETCH-002 — Sketch entities: ellipse, spline

Deliverables:

- [x] Ellipse: a centre and two vertex points on perpendicular axes; created from centre, semi-axes and rotation, or on existing points
- [x] Spline: a non-rational B-spline with uniform knots, degree 2 to 5, on pole points; open (clamped) or periodic
- [x] Solver: ellipse axes kept perpendicular; vertices and poles take point constraints; concentric with ellipses; tangency of open splines at joints with lines, arcs and splines; degeneracy checks
- [x] Queries: end points, centre, length, bounds
- [x] Profiles: ellipses and periodic splines as loops, open splines as edges; exact area, centroid and containment
- [x] Kernel: exact ellipse and B-spline edges in extrude, revolve and sweep profiles; loft sections and sweep paths refuse them with a diagnostic
- [x] Reference validation, structured errors, save/load round-trip
- [x] Evidence: [docs/verification/P12-SKETCH-002/](docs/verification/P12-SKETCH-002/README.md) — PASS, 845/845 tests in Debug, Release and Debug-shared, 0 warnings

Acceptance:

- Extruded and revolved ellipses have the volumes π·a·b·h and 2π·R·π·a·b; spline profiles have the area of an independent computation (Archimedes' parabola quadrature, Bernstein polynomials) and extrude and revolve to the matching volumes.
- A free ellipse has 5 degrees of freedom and a spline 2 per pole; constraints remove exactly their equations; redundancy and conflicts are diagnosed.
- Ellipses and splines survive save → load → regenerate with bit-identical geometry, and STEP export reads back with the same volume.
- `P0`–`P12-SKETCH-001` stays green in all three presets.

#### P12-DATUM-001 — Datum planes, axes, coordinate systems

Deliverables:

- [x] Datum planes: fixed; offset from a plane; turned about an axis in a plane; offsets and angles literal or driven by parameters
- [x] Datum axes: fixed; the intersection of two planes
- [x] Coordinate systems: fixed; placed from another (or the model's) by rotations about its axes and a translation, literal or driven
- [x] References to the principal planes and axes of the model and of coordinate systems
- [x] Document objects with stable IDs, dependency-graph edges, regeneration and failure propagation (missing, wrong kind, degenerate geometry, cycles)
- [x] Sketches attached to datum or coordinate-system planes follow them at regeneration
- [x] Mirror planes and circular pattern axes may refer to datum geometry
- [x] Undoable creation and editing, validation diagnostics, CLI description
- [x] Save/load round-trip
- [x] Evidence: [docs/verification/P12-DATUM-001/](docs/verification/P12-DATUM-001/README.md) — PASS, 869/869 tests in Debug, Release and Debug-shared, 0 warnings

Acceptance:

- Resolved planes, axes and frames match geometry computed independently (rotation matrices written out, plane equations solved by hand) to 1e-12.
- Changing a driving parameter moves the datum, the attached sketch and the features built on it; volumes, centres and bounds match analytic values; undo restores them.
- Missing, wrong-kind and cyclic references and degenerate geometry fail with structured diagnostics and block their dependents; nothing is substituted.
- Save → load → regenerate gives bit-identical geometry; `P0`–`P12-SKETCH-002` stays green in all three presets.

#### P12-STREF-001 — Stable feature face references

Authorized on 2026-09-17 as the minimal prerequisite of `P12-SKETCH-003`
(its blocker record). It is not the semantic-topology phase: no general edge
naming, adjacency matching, confidence scoring, topology reconciliation or
references for assemblies, drawings or simulation. It is numbered `4a` so
that no later milestone is renumbered.

Deliverables:

- [x] Persistent face names: the generating feature's ID and the face's role (start cap, end cap, or the side swept by one profile entity); never a kernel face, index or address
- [x] Bodies carry the names of the faces their feature generated; extrudes name their caps and sides
- [x] Names follow the feature's own booleans (join, cut, intersect, uniting regions) through the kernel's history: a split face keeps its name on every part, merged faces keep all their names, a deleted face loses its name
- [x] Plane references may name a feature's face; resolution finds the named faces in the feature's body, with no geometric fallback, and gives the face's plane and outward side
- [x] Structured failures: missing feature, wrong object kind, a kind that names no faces, a role or entity it does not generate, a name no face carries any more, a face that is not planar, faces on different planes
- [x] Sketches attached to a feature's face follow it at regeneration; datum planes may be placed from one
- [x] Dependency edges, validation diagnostics, CLI description, save/load round trip
- [x] Evidence: [docs/verification/P12-STREF-001/](docs/verification/P12-STREF-001/README.md) — PASS, 892/892 tests in Debug, Release and Debug-shared, 0 warnings

Acceptance:

- A sketch on an extrude's end cap follows it when the depth (a literal, a parameter or an expression) changes; the features built on the sketch match analytic volumes, centres and bounds; undo restores them.
- With another feature's face nearer the old position, the reference still resolves to its own face; when its face is gone, it fails with NotFound and nothing is substituted.
- Save → load → regenerate resolves the same reference to the same plane, bit for bit; repeated builds and fresh documents agree.
- Each failure case gives a structured diagnostic naming the reference and blocks what depends on it.
- `P0`–`P12-DATUM-001` stays green in all three presets.

#### P12-SKETCH-003 — Sketches on arbitrary planar faces

A sketch on a model face must stay on that face when upstream parameters or
features change, and must fail rather than move to another face. The only
persistent face reference, `FaceSignature` (plane and outward side), matches
nothing once the face moves. A face's identity across regenerations is known
only to the feature that made it, and the reference architecture does not
record it. A minimal reproducer shows a signature losing an extrude's top
face after a height change, and a nearest-geometry guess choosing another
feature's face instead
([record](docs/verification/P12-SKETCH-003/README.md)).

The prerequisite, stable face references, was authorized on 2026-09-17 as
`P12-STREF-001` and has passed. This milestone extends the face names of
`P12-STREF-001` from extrudes to every feature that generates planar faces,
and to the copies patterns and mirrors make.

Deliverables:

- [x] Revolves name their caps (partial turns) and the side each profile entity sweeps
- [x] Sweeps name their caps (open paths) and the side each profile entity sweeps along each path edge
- [x] Lofts name their end caps (their sides are not planar)
- [x] Holes name their flat faces: a blind hole's bottom and a counterbore's floor
- [x] Chamfers name the face each edge reference cuts
- [x] Patterns and mirrors name the copies they make (the copied face's name, the copying feature and the instance); transforms, fillets and chamfers carry their inputs' names
- [x] Sketches and datums attach to any named planar face; references to copies depend on the copying features; failures as in `P12-STREF-001`, plus a role, entity, path edge, edge reference or copy that the feature does not have
- [x] Validation, CLI description and save/load of the new roles and copies
- [x] Evidence: [docs/verification/P12-SKETCH-003/](docs/verification/P12-SKETCH-003/README.md) — PASS, 919/919 tests in Debug, Release and Debug-shared, 0 warnings

Acceptance:

- For every kind of planar face each feature generates, a sketch on it follows the face when a driving parameter moves it; the features built on the sketch match analytic volumes, centres and bounds; undo restores them.
- A sketch on a pattern instance's copy follows that instance when the spacing changes, and fails with NotFound when the count no longer makes it; no other instance is taken.
- Curved faces are refused as planes; faces the feature's own operation removed fail with NotFound; nothing is substituted.
- Save → load → regenerate resolves every reference to the same plane, bit for bit; every value the existing tests measure is unchanged; `P0`–`P12-STREF-001` stays green in all three presets.

#### P12-FEAT-001 — Through-all extrude

A cut that goes through all the material of its target, however the target
changes. The termination is part of the definition (`termination =
through_all`), never a large depth: the tool's length is worked out at
regeneration from the target body, in the sketch's direction.

Deliverables:

- [x] `ExtrudeTermination` (`blind`, `through_all`) in `ExtrudeDefinition`; a through-all extrude has no depth or depth parameter, and cuts (the only operation it takes)
- [x] Through all along the sketch normal, against it (reversed) and both ways (symmetric), from the target body's exact bounds, with a clearance; a target wholly behind the sketch plane fails
- [x] Patterns and mirrors repeat a through-all cut through the body at each instance
- [x] Face names as for any extrude; the caps that lie outside the target are gone after the cut
- [x] Save/load (`"termination": "through_all"`, no depth), validation and CLI description
- [x] Evidence: [docs/verification/P12-FEAT-001/](docs/verification/P12-FEAT-001/README.md) — PASS, 936/936 tests in Debug, Release and Debug-shared, 0 warnings

Acceptance:

- A through-all cut removes profile area x thickness from a block whose height is driven by a parameter, at every height, and a slanted one area x thickness / cos(angle); volumes, centres and bounds match the analytic values.
- A depth, a non-cut operation or a missing target is refused with a structured diagnostic; a target behind the sketch plane fails at regeneration, keeping no body.
- Save → load → regenerate gives the same bodies bit for bit; files without `termination` load as blind extrudes, unchanged; `P0`–`P12-SKETCH-003` stays green in all three presets.

#### P12-FEAT-002 — Split body / combine

Two body-level features. A split cuts another feature's body with a plane
and keeps what lies in front of it, behind it, or both parts side by side. A
combine joins, cuts or intersects another feature's body with the bodies of
one or more further features, all of which it consumes. Both store only
references (features, a plane reference) and their choice; the geometry is
regenerated from the inputs.

Deliverables:

- [x] `SplitFeature` (`target`, `plane`: any plane reference, a named face included; `keep`: front, back or both); both parts are kept as one body of separate solids
- [x] A split whose plane leaves nothing on a kept side fails; face names of the target are carried, the new faces on the plane are not named
- [x] `CombineFeature` (`target`, `tools`, `operation`: join, cut or intersect); the target and every tool are consumed (result bodies); a combination that leaves no material fails
- [x] Dependencies, validation (tool and plane kinds, duplicates, self-reference), undo/redo, save/load, CLI description
- [x] Evidence: [docs/verification/P12-FEAT-002/](docs/verification/P12-FEAT-002/README.md) — PASS, 957/957 tests in Debug, Release and Debug-shared, 0 warnings

Acceptance:

- Splits of a box by a datum plane at a driven offset give the analytic volumes, centres and bounds of each part at every offset, and the two parts together give the whole; an oblique split through the centre gives two halves of equal volume whose centres are symmetric about it.
- Joins, cuts and intersections of overlapping blocks match the inclusion-exclusion volumes and centres while a parameter changes the overlap; the consumed features are no longer result bodies.
- Failures are structured and atomic; save → load → regenerate gives the same bodies bit for bit; `P0`–`P12-FEAT-001` stays green in all three presets.

#### P12-FEAT-003 — Shell

Hollows another feature's body into walls of a given thickness, opened
where named faces are removed. The open faces are face names
(P12-STREF-001) resolved in the target's body, never positions or
geometry. The walls are offsets of the remaining faces, inward (the body's
outside stays) or outward (its inside stays). Where offset faces part at
an edge they are extended until they meet, so corners stay sharp: a kernel
probe found rounded joins invalid on bodies with concave edges, and the
kernel's silent wrong results for walls that are too thick, which the
shell must detect.

Deliverables:

- [x] `geometry::shellBody` (open faces by name, thickness, side), failing with a structured diagnostic when the kernel cannot build the walls or its result is not a shell of the body: not one valid solid, a remaining face without its wall, an open face still there, or (inward) nothing removed
- [x] `ShellFeature` (`target`, `open_faces`, `thickness` literal or driven by a length parameter, `side`: inward or outward); it consumes its target, carries the target's face names (the rim the kernel makes of an open face keeps its name) and does not name the walls
- [x] Open faces resolved by name in the target's body: a name no face there carries fails with NotFound; names that `checkFaceName` refuses are refused
- [x] Dependencies (target, thickness parameter, the open faces' features and copies), validation, undo/redo, save/load, CLI description
- [x] Evidence: [docs/verification/P12-FEAT-003/](docs/verification/P12-FEAT-003/README.md) — PASS, 978/978 tests in Debug, Release and Debug-shared, 0 warnings

Acceptance:

- Inward and outward shells of a box (open top; open top and bottom), a cylinder, an L-shaped prism, a drilled block, a block with rounded vertical edges and a revolved stepped shaft match their analytic volumes, centres and bounds while a parameter changes the wall or the body, and the open face follows its feature when the body grows.
- Walls too thick for the body, overall or locally, rounds smaller than an inward wall, an open face that is not in the target's body, a target of several solids and invalid thicknesses fail with structured diagnostics and keep no body; nothing is substituted.
- Save → load → regenerate gives the same bodies bit for bit; every value the existing tests measure is unchanged; `P0`–`P12-FEAT-002` stays green in all three presets.

#### P12-FEAT-004 — Draft

Tapers faces of another feature's body for moulding: each named face turns
by the draft angle about its line on a neutral plane. The faces are face
names (P12-STREF-001) resolved in the target's body; the neutral plane is
any plane reference, a named face included, and its normal is the pull
direction. A kernel probe found the kernel exact on planes, cylinders and
their tangent chains, and found it reporting an invalid, self-intersecting
solid as a success when a face shrinks to nothing, so the result is
checked.

Deliverables:

- [x] `geometry::draftFaces` (faces by name, neutral plane, signed angle in (-90, 90) deg): planes, cylinders and cones only; faces tangent to a drafted face are drafted with it; failing with a structured diagnostic when the kernel cannot turn a face or build the draft, or its result is not one valid, non-self-intersecting solid of the same topology with every face kept
- [x] `DraftFeature` (`target`, `faces`, `neutral_plane`, `angle` literal or driven by an angle parameter); it consumes its target and carries the target's face names to the turned faces
- [x] Faces resolved by name in the target's body (NotFound when no face carries a name); the neutral plane resolved like any plane reference
- [x] Dependencies, validation, undo/redo, save/load, CLI description
- [x] Evidence: [docs/verification/P12-FEAT-004/](docs/verification/P12-FEAT-004/README.md) — PASS, 997/997 tests in Debug, Release and Debug-shared, 0 warnings

Acceptance:

- Drafts of a block's sides about a datum plane, about a plane partway up and about the block's own top and bottom faces, of a cylinder, a hole, a pocket's walls, an L-shaped prism and a block with rounded vertical edges match analytic volumes (sections integrated exactly), centres and bounds while the angle and the body change; drafted faces keep their names, with their turned areas.
- Angles that make a face vanish, faces parallel to the neutral plane or tangent to such a face, faces that are not planes, cylinders or cones, names the body does not carry, a target of several solids and invalid angles fail with structured diagnostics and keep no body; nothing is substituted.
- Save → load → regenerate gives the same bodies bit for bit; every value the existing tests measure is unchanged; `P0`–`P12-FEAT-003` stays green in all three presets.

#### P12-FEAT-005 — Rib

A rib fills the space between an open sketched profile and another
feature's body with a wall of a given thickness, lying in the sketch plane.
The profile is a chain of the sketch's lines, arcs and open splines, stored
as sketch entities. It is extended along its end tangents, and the side of
it that the rib fills (left of its direction of travel, or right) must be
closed off by the body. That wall is joined to the body. The thickness
lies on both sides of the sketch plane or on one. The rib is built only
from prisms and the qualified booleans, and is refused when the side it
fills is open.

Deliverables:

- [x] `geometry::addRib` (an open profile chain in a plane, a thickness, its placement: symmetric, along or against the normal, and the side): the prism of the profile's side, extended beyond the body, less the body; only the pieces the profile bounds are kept, and one that reaches the extended bounds fails (the side is not closed off)
- [x] `RibFeature` (`target`, `profile` sketch and ordered `edges`, `thickness` literal or driven, `placement`, `flipped`); it consumes its target, carries its names and names its own faces: the side each profile edge makes and the two wall faces (start and end caps)
- [x] Profile resolution: the edges head to tail (lines, arcs, open splines) and open; structured errors for missing, closed, disconnected or degenerate profiles
- [x] Dependencies, validation, undo/redo, save/load, CLI description
- [x] Evidence: [docs/verification/P12-FEAT-005/](docs/verification/P12-FEAT-005/README.md) — PASS, 1017/1017 tests in Debug, Release and Debug-shared, 0 warnings

Acceptance:

- Ribs across the inside corner of an L bracket (a line ending on the walls, inside them and short of them; an arc; a two-line chain; a spline) and inside a shelled box match analytic volumes (Green's theorem for curved profiles), centres and bounds while the thickness, the profile and the body change; they follow their sketch when it moves, and sketches can be placed on their wall faces.
- A side that is not closed off, a profile inside the body or crossing itself once extended, closed or disconnected profiles and invalid thicknesses fail with structured diagnostics and keep no body; nothing is substituted.
- Save → load → regenerate gives the same bodies bit for bit; every value the existing tests measure is unchanged; `P0`–`P12-FEAT-004` stays green in all three presets.

## Next

`P12-FEAT-006` — Variable-radius fillet, setback, corner transitions.

## Blocked / Manual

| Item | Why |
| --- | --- |
| `LICENSE` | Not yet chosen. A decision, not an implementation. |
| Release tagging | Manual, and only on request. `v0.1.0` is the only tag. |

## Planned — Not Authorized

Not started. Not authorized. Grouped to match
[ROADMAP.md](ROADMAP.md#capability-roadmap).

> Parametric CAD completion moved to [**Current**](#current) as `P12` and is no
> longer listed here.

### Desktop application

- [ ] Tessellated display pipeline and 3D viewport
- [ ] Model tree and property editor
- [ ] Command system and diagnostics panel
- [ ] Sketch environment
- [ ] Selection: body, face, edge, vertex, sketch, feature
- [ ] GUI smoke tests

### Semantic topology

- [ ] Minimal stable-reference layer: feature provenance and persistent naming
- [ ] Adjacency signatures and semantic matching
- [ ] Ambiguity detection and confidence scores
- [ ] Reference recovery after topology-changing edits

### Interchange

- [ ] STEP import
- [ ] DXF import and export
- [ ] IGES and OBJ
- [ ] Document schema migration between format versions

### Assemblies

- [ ] Component model: part reference, instance, transform, suppression
- [ ] Mate representation
- [ ] Assembly solver, separate from the mate model
- [ ] Interference and clearance analysis
- [ ] Assembly mass properties
- [ ] Exploded views and motion constraints

### Drawings

- [ ] Sheets and standard views
- [ ] Projected, section and detail views
- [ ] Dimensions, centre marks and annotations
- [ ] GD&T foundation
- [ ] BOM tables, balloons, revision tables
- [ ] PDF, DXF and SVG export

### Engineering data

- [ ] Material database
- [ ] Material assignment, persisted through save/load
- [ ] Mass properties driven by assigned materials

### Simulation

- [ ] Meshing infrastructure: 1D/2D/3D, sizing, quality metrics
- [ ] Structural FEA: linear elastic, static
- [ ] Analytical FEA benchmarks: bar, cantilever, simply supported, plate
- [ ] Thermal analysis, coupled into structural expansion
- [ ] CFD: fluid volume extraction through solver results

### Optimization

- [ ] Design variables and design studies
- [ ] Parameter sweeps
- [ ] Gradient, population and surrogate methods

### Automation and versioning

- [ ] Python bindings (pybind11) over the public API
- [ ] Plugin system with a declared API version
- [ ] Semantic document diff
- [ ] Engineering version control: commit, branch, merge, history

### AI engineering

- [ ] Structured tool layer over the public API
- [ ] Intent extraction and engineering plan validation

### Manufacturing

- [ ] Manufacturing metadata: stock, threads, tolerances, finish, bends
- [ ] 2.5D CAM foundation

### Tooling and hardening

- [ ] CI workflow (`.github/workflows` does not exist)
- [ ] Sanitizer presets (ASan/UBSan)
- [ ] Coverage configuration
- [ ] Enforced `.clang-format` check
- [ ] Automated documentation link check
- [ ] Regeneration and pattern performance benchmarks
- [ ] Crash recovery and autosave

## Recently Completed

| Milestone | Commit | Evidence |
| --- | --- | --- |
| `P12-FEAT-005` Rib | "BetterCAD: implement P12 rib" | [P12-FEAT-005](docs/verification/P12-FEAT-005/README.md) |
| `P12-FEAT-004` Draft | `80f1fec` | [P12-FEAT-004](docs/verification/P12-FEAT-004/README.md) |
| `P12-FEAT-003` Shell | `eb3ad66` | [P12-FEAT-003](docs/verification/P12-FEAT-003/README.md) |
| `P12-FEAT-002` Split body / combine | `77f1792` | [P12-FEAT-002](docs/verification/P12-FEAT-002/README.md) |

## Completed Milestones

`P0`–`P11` are complete. Each milestone, what it delivered and its evidence are
recorded in [ROADMAP.md](ROADMAP.md#capability-roadmap) under the capability it
belongs to, so this file stays a list of work still to do.

Per-milestone acceptance criteria, measured values, analytic validation and
known limits live with the evidence in
[docs/verification/](docs/verification/), and are not summarised here.

## Known Limitations

Properties of the system as qualified. Each modeling limitation is pinned by a
regression test, so none can change silently. Detail:
[P11-QUAL-001](docs/verification/P11-QUAL-001/README.md).

### Modeling

- **Geometric references do not follow moved geometry.** Edges, and the faces
  holes, chamfers and fillets refer to, are matched by geometry — an edge's
  supporting line or circle, a face's plane and outward side — not named
  semantically. When a parameter moves the referenced geometry, the feature
  fails with `NotFound` and keeps no body. Nothing is ever substituted.
- **Face references by name** (sketch attachments, datum planes and axes)
  follow their faces. Extrudes, revolves, sweeps, lofts (end caps), holes
  (bottoms, counterbore floors) and chamfers name their planar faces, and
  patterns and mirrors name their copies by instance index. Fillets and
  loft sides name nothing. A name is looked up in the body of the feature
  that made or last copied the face: later features do not move or remove
  it. A copy is named by its index, so a reference to an instance a pattern
  no longer makes fails. Mirror planes cannot refer to faces
  ([P12-STREF-001](docs/verification/P12-STREF-001/README.md),
  [P12-SKETCH-003](docs/verification/P12-SKETCH-003/README.md)).
- **Undo after a change to a sketch-driving parameter** restores the model
  to rounding, not bit for bit: the solver re-solves the sketch from its
  changed shape ([P12-SKETCH-003](docs/verification/P12-SKETCH-003/README.md)).
- **Parameter expressions** have `+ - * /`, unary signs, parentheses, units
  and parameter names only: no functions, powers or constants. Only
  parameters take expressions; a feature field takes a literal or one
  parameter. Renaming a parameter does not rewrite expressions that use it.
  Driven values are computed at regeneration
  ([P12-PARAM-001](docs/verification/P12-PARAM-001/README.md)).
- **Extrudes end at a depth or through all** of their target (cuts only);
  there is no *up to next* or *up to a face*. A through-all tool is as long
  as the target's bounding box along the sketch normal, plus 1 mm
  ([P12-FEAT-001](docs/verification/P12-FEAT-001/README.md)).
- **Splits cut with planes only**, and a split that keeps both sides gives
  one body of two solids, which later features take whole. A combine
  consumes its tools. The faces a split makes are not named
  ([P12-FEAT-002](docs/verification/P12-FEAT-002/README.md)).
- **Shells need an open face** (no closed hollows) and hollow one solid.
  Wall corners are sharp. Walls that meet where the body is thinner than
  two walls, and inward walls at least as thick as a round they follow, are
  refused rather than thinned or merged. The walls are not named
  ([P12-FEAT-003](docs/verification/P12-FEAT-003/README.md)).
- **Drafts pull along the neutral plane's normal**, one angle per feature,
  on planes, cylinders and cones. The kernel turns whole tangent chains, so
  a side joined to the top by a round cannot be drafted (draft first, then
  round). Angles that make a face vanish are refused
  ([P12-FEAT-004](docs/verification/P12-FEAT-004/README.md)).
- **Ribs lie along their sketch plane** and fill towards the body on one
  side of an open chain of lines, arcs and open splines; a side the body
  does not close off is refused. Face areas of planes bounded by splines
  are the kernel's default integration (4.3e-6 relative off for a spline
  rib's walls; its volume is within 1e-12)
  ([P12-FEAT-005](docs/verification/P12-FEAT-005/README.md)).
- **Loft sides stay B-splines** even where flat, costing about 6e-12 relative
  volume and 3.4e-6 mm in the centroid; plane references find only a loft's end
  faces.
- **Uniting a half body with its mirror image** is refused when a half cylinder
  lies on the mirror plane: the kernel's fuse returns a shape its own checker
  rejects, so BetterCAD refuses it rather than building it wrongly.
- **Sketch constraints** are coincident, horizontal, vertical, parallel,
  perpendicular, distance, radius, equal, fixed, angle, tangent, concentric,
  midpoint, symmetric and diameter. Tangency keeps the side or kind of
  contact the sketch starts with, and is exact at a joint only where the
  entities share a point or a coincident constraint
  ([P12-SKETCH-001](docs/verification/P12-SKETCH-001/README.md)).
- **Sketch entities** are points, lines, circles, arcs, ellipses and
  uniform non-rational B-splines (degree 2 to 5). Splines are tangent only
  where they meet a line, an arc or a spline; nothing is tangent to an
  ellipse. Spline bounds and the revolution-axis check use the poles, which
  bound the curve. Lofts and sweep paths take lines, arcs and circles only
  ([P12-SKETCH-002](docs/verification/P12-SKETCH-002/README.md)).
- **Datum geometry** is fixed, offset, angled (planes), fixed or two-plane
  intersections (axes) and fixed or offset (coordinate systems). Sketches
  attach to datum planes and coordinate-system planes; mirror planes and
  circular-pattern axes may refer to datums. Nothing else takes a datum
  reference yet, and there are no point datums
  ([P12-DATUM-001](docs/verification/P12-DATUM-001/README.md)).
- **Mass properties**: volumes and centres are exact to rounding for every
  body probed. Areas are exact for elementary faces and for swept-curve faces
  that cover their parameter rectangle; cut (trimmed) swept-curve faces and
  B-spline faces keep the kernel's area integration, unverified beyond the
  P11 loft cases. Cut swept-curve faces take the kernel's Gauss–Kronrod
  volume integration, measured at up to 0.25 s a face
  ([P12-DATUM-001](docs/verification/P12-DATUM-001/README.md)).
- **Pattern cost** grows with the square of the instance count, capped at 500.

### Not implemented

- The desktop application is a placeholder shell; no GUI functionality is
  claimed or qualified.
- STEP **export** only. The kernel-based reader under `tests/support/occt/` is
  test-only tooling that reads exports back to check them. No DXF, IGES or OBJ.
- Everything under *Planned — Not Authorized* above.

### Limits of the evidence

- One platform: Windows 11 AMD64, GCC 16.1.0 (MinGW-w64), OCCT 8.0.1, Qt 6.11.2.
- No CI, no sanitizers, no coverage, no memory checking; `.clang-format` is
  defined but unenforced.
- 738 tests passing means 738 tests passed. Where a behaviour has no test, the
  evidence says nothing about it.

## Project Documents

| Document | Purpose |
| --- | --- |
| [README.md](README.md) | Project overview and getting started |
| [ROADMAP.md](ROADMAP.md) | Long-term capability direction and completed capabilities |
| [TODO.md](TODO.md) | Authoritative implementation status and next work |
| [ARCHITECTURE.md](ARCHITECTURE.md) | Technical architecture and invariants |
| [CLAUDE.md](CLAUDE.md) | Engineering workflow for coding agents |
| [docs/verification/](docs/verification/) | Proof of completion |
