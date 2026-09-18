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
| Next | `P12-PARAM-002` — Design equations and configurations |
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
| 11 | `P12-FEAT-006` Variable-radius fillet; setback and corner transitions deferred (scope decided 2026-09-18) | — | **done** (variable radius) — [evidence](docs/verification/P12-FEAT-006/README.md); setback and corner transitions deferred |
| 12 | `P12-HOLE-001` Threads, spotface, standard sizes, tolerance classes | `PARAM-001` | **done** — [evidence](docs/verification/P12-HOLE-001/README.md) |
| 13 | `P12-PATTERN-001` Symmetric, total-length, suppressed instances, patterns of patterns | `PARAM-001` | **done** — [evidence](docs/verification/P12-PATTERN-001/README.md) |
| 14 | `P12-SWEEP-001` Guide curves, twist, non-planar paths | `SKETCH-002` | **done** — [evidence](docs/verification/P12-SWEEP-001/README.md) |
| 15 | `P12-LOFT-001` Differing section shapes, smooth interpolation, end conditions | `SKETCH-002` | **done** (differing shapes, smooth interpolation) — [evidence](docs/verification/P12-LOFT-001/README.md); end conditions blocked: the kernel offers none |
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

#### P12-FEAT-006 — Variable-radius fillet (setback and corner transitions deferred)

The first assessment blocked this milestone
([investigation](docs/verification/P12-FEAT-006/investigation/README.md)). OCCT 8.0.1 has no setback or
corner-transition input, and its variable-radius law is a clamped cubic
spline whose shape depends on the blends meeting the edge; its law-function
inputs crash or silently do nothing. On 2026-09-18 the scope was decided
(options A and C):

- implement the variable-radius fillets that can be verified;
- defer setback and corner-transition controls, which would need a
  surface-patch subsystem P12 does not authorize.

A variable-radius fillet rounds straight edges between two planar faces,
each edge on its own: it continues smoothly into no other edge and meets no
other edge of the fillet, so the kernel's law depends on that edge's
stations alone. Radius stations lie at normalized positions along the edge,
0 at the end that comes first along the edge's canonical direction (see
`geometry::EdgeSignature`), 1 at the other, so no regeneration can reverse
them. Between stations the radius follows the kernel's law, which BetterCAD
computes itself: stations whose law would leave the radii of the stations on
either side are refused, and every result is checked against that law
before it is kept.

Deliverables:

- [x] `geometry::variableFilletEdges`: stations (first at 0, last at 1, strictly increasing; positive finite radii), straight edges between two planar faces meeting at an angle; an edge that continues smoothly, or meets another edge of the request, is refused
- [x] The radius law computed by BetterCAD (the clamped cubic spline through the stations and the end radii repeated at the kernel's spine extension); stations whose law leaves the range of the two stations around it are refused before the kernel runs; the room check uses the largest radius on the edge
- [x] Result verification: the kernel reports completion; one valid solid per input solid without self-intersection; one fillet face generated from each edge; the kernel's law equals BetterCAD's; the fillet surface lies on the rolling-ball sections of that law and touches both faces at the law's contact lines; otherwise a structured failure and no body
- [x] `VariableFilletFeature` (`target`, `edges`, each with its stations; station radii literal or driven by length parameters); it consumes its target and carries its names
- [x] Dependencies, validation, undo/redo, save/load (a new object type; constant-radius fillet files unchanged), CLI description; patterns and mirrors refuse it
- [x] Evidence: [docs/verification/P12-FEAT-006/](docs/verification/P12-FEAT-006/README.md) — PASS, 1036/1036 tests in Debug, Release and Debug-shared, 0 warnings

Deferred, not part of this milestone (both need a surface-patch capability:
trimmed blends, N-sided tangent fills, sewing and validation;
[investigation](docs/verification/P12-FEAT-006/investigation/README.md)):

- [ ] Setback controls — deferred
- [ ] Selectable corner-transition controls — deferred

Acceptance:

- Variable-radius fillets of isolated straight edges (a block's convex edges, an edge between faces at 120°, a concave inside edge) with increasing, decreasing, constant and multi-station radii match the volumes, centres and bounds computed from an independent derivation of the law, while literal and driven station radii and the body change.
- Invalid, unordered, duplicate and out-of-range stations; zero, negative and non-finite radii; laws that would leave their station range (the investigation's overshoot cases); radii too large for the faces; curved edges, edges between non-planar faces, edges continued smoothly and edges meeting another rounded edge fail with structured diagnostics and keep no body; nothing is substituted.
- Repeated, fresh and save → load → regenerate builds give the same bodies bit for bit; STEP exports read back with the same volume; every value the existing tests measure is unchanged; `P0`–`P12-FEAT-005` stays green in all three presets.

#### P12-HOLE-001 — Hole threads, spotface, standard sizes, tolerance classes

What a hole is *for* — a bolt of a standard size, a tapped thread, a seat
machined flat, a bore reamed to a fit — is engineering intent, and is kept
as the standard's own designation ("M8", "6H", "H7", the ISO 273 medium
series), never as the dimensions it stands for. The geometry is the hole the
standard gives; a thread and a tolerance class are manufacturing data, and
cost no geometry: no helix is cut, and nothing changes the nominal size.

The data is tabulated, so it is transcribed twice — once for BetterCAD and
once for the tests — from published copies of ISO 68-1, ISO 262, ISO 273,
ISO 286 and ISO 965, and both transcriptions are checked against the sources
([the sources and their cross-check](docs/verification/P12-HOLE-001/standards/SOURCES.md)).
BetterCAD knows the part of each standard that two independent copies agree
on, and refuses the rest rather than guessing it.

Deliverables:

- [x] `standards::MetricThread` (ISO 68-1, ISO 262, ISO 965-2): the sizes ISO 965-2 gives limits of size for (coarse M1 to M64, fine M8x1 to M64x4), parsed from and printed as their designations; the basic major, pitch and minor diameters of the profile
- [x] `standards::internalThreadLimits` (ISO 965-1): the limits of size of an internal thread in the tolerance class of ISO 965-2 (5H up to M1.4, 6H above) in position H or G; any other grade refused
- [x] `standards::clearanceHoleDiameter` (ISO 273): the fine, medium and coarse clearance holes of M1 to M64, and the tolerance class the standard gives each series for information
- [x] `standards::limitDeviations` (ISO 286): the standard tolerances IT1 to IT18 and the limit deviations of the hole tolerance classes ISO 286-2 tabulates for sizes up to 500 mm with the deviations D, E, F, G and H; sizes and grades outside them refused
- [x] `HoleType::Spotface`: a seat cut like a counterbore, naming its floor `spotface_floor` (a new face role); sketches and datums may be placed on it
- [x] `geometry::CosmeticThread`: a thread described and checked against its hole — wider than the bore, narrower than any head, longer than the head, and no longer than a blind hole or than the material under the face — and cut as nothing
- [x] `HoleDefinition`: a thread (with its length, literal or driven) or a standard clearance size, which gives the diameter; a tolerance class for a hole of its own diameter; `holeCallout()` resolves the designations, limits and deviations
- [x] Dependencies, validation, undo/redo, save/load (designations, not dimensions; files without them unchanged), CLI description; patterns and mirrors repeat a threaded hole as it is
- [x] Evidence: [docs/verification/P12-HOLE-001/](docs/verification/P12-HOLE-001/README.md) — PASS, 1070/1070 tests in Debug, Release and Debug-shared, 0 warnings; the standards data checked against published copies (21 checks, 0 failed)

Acceptance:

- Every value of every table matches published copies of its standard, checked by a script that reads the sources themselves; the limits BetterCAD computes for all 60 thread sizes match the ones ISO 965-2 tabulates, to the 0.001 mm they are rounded to.
- A tapped hole is cut at its thread's basic minor diameter, a standard clearance hole at its ISO 273 diameter and a spotface like a counterbore; volumes, centres and bounds match the values computed from the standards' own definitions, and a thread changes no geometry at all.
- A driven thread length and a driven bore rebuild the model; a thread longer than its blind hole, a head narrower than the thread, a class whose limits BetterCAD does not know and a size ISO 286 does not tabulate fail with structured diagnostics and keep no body; nothing is substituted.
- Save → load → regenerate gives the same bodies bit for bit, from the designations alone; STEP exports read back with the same volume; files written before this milestone load and save unchanged; every value the existing tests measure is unchanged; `P0`–`P12-FEAT-006` stays green in all three presets.

#### P12-PATTERN-001 — Symmetric, total-length, suppressed instances, patterns of patterns

The four things `P11-FEAT-005` and `P11-FEAT-006` left out. Each is
engineering intent the definition keeps, not a shape the regeneration
guesses:

- **Symmetric.** The instances sit on both sides of the source, which is the
  middle one. The count includes the source, so it must be odd: (N - 1) / 2
  copies each side. Every offset is computed from the source as a signed
  multiple of the step, never by adding to the instance before it.
- **Total length.** A direction gives either the spacing between
  neighbours or the total length of the row, from the first instance to the
  last: the step is then L / (N - 1), which needs N of at least 2.
- **Suppressed instances.** A pattern of 8 with instances 2 and 5
  suppressed still has 8 indexed positions: 2 and 5 make no geometry, and 3
  is still 3. Suppression is persistent intent, so a reference to a
  suppressed instance fails with `NotFound` — never another instance — and
  unsuppressing it brings the same index back.
- **Patterns of patterns.** A pattern may repeat another pattern: each of
  its instances repeats every instance of the one below. A face keeps the
  whole chain of copies that made it (the feature, then each pattern and
  the instance of it), so the original and every copy stay distinguishable.

Deliverables:

- [x] `PatternDistribution` (`spacing`, `total_length`) and a `symmetric` flag on each direction of a linear pattern; the step and every offset derived in closed form, with the instance order fixed and documented (a symmetric direction numbers its copies outward, the positive side first, so that raising the count keeps what the existing indices mean)
- [x] Symmetric circular patterns: the span centred on the source, with the same odd-count rule; refused for a full circle, which has no span
- [x] `suppressed` instance indices on linear and circular patterns: persistent, never renumbering, refusing index 0 (the source), duplicates and indices the count does not reach, and refusing to suppress every copy
- [x] Patterns of patterns: a linear or circular pattern may be the source of another; each outer instance repeats every instance of the inner pattern, its suppression included, and the effective instance count stays within the 500 cap
- [x] Face names keep the full chain of copies (`FaceSelector::copies`), so a sketch or datum on a nested copy names the feature, the inner pattern and instance, and the outer pattern and instance
- [x] Dependencies, validation, undo/redo, save/load (new fields; files written before this milestone keep their meaning: spacing, not symmetric, nothing suppressed), CLI description
- [x] Independent validation: the transforms, instance counts, volumes, centres and bounds of the reference models computed without the pattern implementation
- [x] Performance measured, not claimed: the cost of 10, 50, 100, 250 and 500 instances recorded against the `P11-FEAT-005` figures
- [x] Evidence: [docs/verification/P12-PATTERN-001/](docs/verification/P12-PATTERN-001/README.md)

Acceptance:

- Symmetric, total-length and plain patterns place their instances where a closed-form calculation says, to rounding, in one direction, in a grid and about an axis; volumes, centres and bounds match the analytic values for non-overlapping, touching and overlapping instances.
- Suppressing an instance removes its geometry and nothing else: the other instances keep their indices, their faces keep their names, a reference to a suppressed instance fails with `NotFound`, and unsuppressing restores the same index and the same geometry, bit for bit.
- A pattern of a pattern builds every combination once, carries the copy chain on every face, and fails atomically with the failing instance named; a combination that exceeds the instance cap is refused before the kernel runs.
- Invalid counts, spacings, total lengths, axes, suppression sets, missing sources, self-references and cycles fail with structured diagnostics and keep no body; no partial pattern ever becomes a result.
- Save → load → regenerate gives the same bodies bit for bit; repeated and fresh-document builds agree; files written before this milestone regenerate unchanged; every value the existing tests measure is unchanged; `P0`–`P12-HOLE-001` stays green in all three presets.

#### P12-SWEEP-001 — Guide curves, twist, non-planar paths

The three things `P11-FEAT-008` left out. Each is engineering intent the
definition keeps, not geometry inferred after regeneration.

The frame convention is settled by measurement, not by an OCCT default
(`docs/verification/P12-SWEEP-001/kernel-probe/`): on a planar path the fixed
binormal, Frenet and corrected Frenet frames give the same solid to the last
digit, so BetterCAD has **one** convention — the rotation-minimizing frame —
of which `P11-FEAT-008`'s fixed binormal is the closed-form planar case. On a
spatial path the fixed binormal throws as soon as a run runs parallel to it,
while corrected Frenet is exact.

- **Non-planar paths.** A path is one or more *runs*, each the edges of one
  sketch, joined end to end in model space. One run is the planar path of
  `P11-FEAT-008`; several runs, on different planes, make a spatial path. No
  3D-sketch subsystem is introduced: every run is still a planar sketch, and
  the joins happen in model space.
- **Twist.** The section turns about the path's tangent by
  `theta(u) = u theta_total`, u the normalized path coordinate. The total is
  literal or driven by an angle parameter. Zero twist must reproduce
  `P11-FEAT-008`'s solids exactly.
- **Guide curves.** A guide is a path of its own; the section turns to follow
  it about the tangent. A guide and a twist are two ways to say the same
  thing, so a definition carries one or the other, never both.

Deliverables:

- [x] `geometry::SweptPath`: a path of planar runs joined in model space, with the joint rules of `P11-FEAT-008` applied across runs (tangential, or two straight segments mitred at a corner), and the connectivity, degeneracy and finiteness checks applied in 3D
- [x] The rotation-minimizing frame for spatial paths (corrected Frenet), with the planar case unchanged; the convention measured and recorded, not assumed
- [x] `SweepPath::runs`: further runs on other sketches, each contributing its edges in order; dependencies, validation and the 3D connectivity diagnostics
- [x] `SweepDefinition::twist` (literal or driven by an angle parameter): `theta(u) = u theta_total`, built from an auxiliary spine BetterCAD generates from its own frame, so the law is BetterCAD's and not the kernel's
- [x] `SweepDefinition::guide`: a path of its own carrying the section; one guide (the kernel takes one auxiliary spine), `NoContact` without curvilinear equivalence, both chosen on measured evidence; a guide and a twist are mutually exclusive
- [x] Face names extended to the new modes: a side names its profile entity and the path edge it runs along and, across runs, the sketch that edge belongs to (`FaceSelector::alongSketch`; entity IDs are numbered per sketch, so the edge alone would not say which run)
- [x] Dependencies, validation, undo/redo, save/load (new fields; files written before this milestone keep their meaning: one run, no twist, no guide), CLI description
- [x] Independent validation: Pappus volumes, analytic centroids and bounds, segment transforms and the measured twist angle at chosen u, all computed without the sweep implementation
- [x] Evidence: [docs/verification/P12-SWEEP-001/](docs/verification/P12-SWEEP-001/README.md)

Acceptance:

- A spatial path of several runs sweeps a valid solid whose volume is the profile's area times its centroid's path length, to the tolerance the joints justify; the section's orientation at each run is the one the recorded frame convention predicts, checked segment by segment.
- A twisted sweep turns its section by `u theta_total`: measured at chosen u against the definition, for 0, +90, -90, 180 and 360 degrees and for a driven angle. Zero twist reproduces `P11-FEAT-008`'s solids bit for bit.
- A guide curve carries the section where the guide's own turning says, checked against the guide's mathematical form; a guide that cannot correspond to the path fails with a structured diagnostic.
- Missing, disconnected, degenerate, non-finite, self-referencing and cyclic inputs, an invalid twist, and a guide given with a twist all fail atomically with structured diagnostics and keep no body.
- Save -> load -> regenerate gives the same bodies bit for bit; repeated and fresh-document builds agree; files written before this milestone regenerate unchanged; STEP exports read back with the same volume; every value the existing tests measure is unchanged; `P0`-`P12-PATTERN-001` stays green in all three presets.

#### P12-LOFT-001 — Differing section shapes, smooth interpolation, end conditions

The three things `P11-FEAT-009` left out. What each can be is settled by
measurement, not by the kernel's defaults
(`docs/verification/P12-LOFT-001/kernel-probe/`):

- **Differing section shapes.** `P11-FEAT-009` refuses a circle against a
  polygon, 4 corners against 6, or a side split in two. The probe shows
  BetterCAD can match them itself, exactly as it already matches equal
  shapes: split both loops at the same normalized arc lengths so that they
  have the same number of segments, and hand the kernel the correspondence
  with `CheckCompatibility(false)`. A square against a circle split into
  four aligned quarter arcs gives the same solid the kernel's own matcher
  gives, to the last digit, and meets the prismatoid volume of the averaged
  section to 1e-7. Splitting the circle 45 degrees round instead gives a
  different solid, so the alignment is engineering intent and BetterCAD must
  choose it by the same least-twist rule it uses today.
- **Smooth interpolation.** `LoftInterpolation::Smooth`: the sides run
  continuously across every intermediate section instead of being split into
  a band per interval. The surface passes through the **end** sections
  exactly, since they are the caps, and through the **intermediate** ones
  only as closely as the kernel's single fitted surface allows: cutting the
  three-circle spool on its middle section's plane measures 78.54028940 mm^2
  against an exact 78.53981634, a radius 1.5e-5 mm out, where the same cut
  through the ruled loft is exact to rounding. BetterCAD says that rather
  than claiming interpolation it cannot show.
  The probe measured what can and cannot be predicted: with two sections a
  smooth loft **is** the ruled one, to the last digit; with three equally
  spaced sections it is the quadratic through them (7330.382866 measured
  against 7330.382858 computed); with four, or with unequal spacing, it is
  neither that nor any closed form BetterCAD can state (0.3 per cent and 31
  per cent away). So the exact prismatoid check cannot guard a smooth loft,
  and the correspondence is guarded instead by building the ruled loft from
  the same matched sections and checking *that* against the prismatoid
  volume, which is what catches a wrong matching.
- **End conditions.** `BRepOffsetAPI_ThruSections` offers none. Every setting
  it has -- `SetContinuity` C0/C1/C2, `SetParType`, `SetMaxDegree`,
  `SetSmoothing` -- leaves the solid identical to the last digit, and the
  ghost-section workaround is 23 per cent wrong in volume even at 0.01 mm.
  Offering a tangent or normal end condition needs a different surface
  builder, which is an unauthorized subsystem. **Not implemented; reported
  with the measurement that shows why.**

Deliverables:

- [x] Section correspondence for differing shapes: both loops split at the same normalized arc lengths, the start chosen by the existing least-twist rule over the candidate alignments, ties broken as they are today; circles take part by being split, and are aligned by angle about the centroid since they have no corners of their own
- [x] A chain matched in one go, not pair by pair: a section in the middle belongs to two pairs and both must split it the same way, or the kernel is handed sections with different numbers of edges and the first pair's correspondence is lost silently
- [x] The prismatoid volume generalized to matched sections of different shapes: the halfway area comes from the closed-form mixed area `M` of the two loops, `Am = (A_p + M + A_q)/4`, computed exactly for every pair of lines and arcs without sampling and without the kernel
- [x] `LoftInterpolation::Smooth`: sides continuous across intermediate sections; the surface passes through the end sections exactly and through the intermediate ones to within the kernel’s approximation, measured by cutting the solid on a section’s plane (1.5e-5 mm on a 5 mm radius, against an exact cut through the ruled loft as control); guarded by checking the ruled loft of the same matched sections against the prismatoid volume
- [x] Dependencies, validation, undo/redo, save/load (the new interpolation value; files written before this milestone keep `ruled`), CLI description
- [x] Independent validation: frustum and prismatoid volumes, centroids, bounds, cross-sectional areas at each section, and the two smooth cases the probe pins exactly (two sections, and three equally spaced)
- [x] Stable references: a loft still names its end caps and not its sides. Arc-length matching gives BetterCAD a stable index of its own, but a matched segment generally spans part of one sketch entity in one section and part of another in the next, so there is no `EntityId` to record. The limitation is retained deliberately rather than replaced by a face numbering
- [x] End conditions: **not implemented**, with the probe log showing the kernel offers none and the ghost-section cost measured
- [x] Evidence: [docs/verification/P12-LOFT-001/](docs/verification/P12-LOFT-001/README.md)

Acceptance:

- A circle to a rectangle, a triangle to a circle and polygons of different corner counts loft into one valid solid whose volume, centroid and bounds match values computed without the loft implementation; reversing a sketch's winding or starting it from another corner gives the same solid.
- A smooth loft passes through every section, its sides run continuously across the intermediate ones, and with two sections it is the ruled loft bit for bit; its correspondence is guarded by the ruled loft's exact prismatoid check.
- Every failure of the list in the acceptance gate fails atomically with a structured diagnostic and keeps no body.
- Save -> load -> regenerate gives the same bodies bit for bit; files written before this milestone regenerate unchanged; STEP exports read back with the same volume; every value the existing tests measure is unchanged; `P0`-`P12-SWEEP-001` stays green in all three presets.

## Next

`P12-PARAM-002` — Design equations and configurations.

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

### Advanced surface modeling

Deferred from `P12-FEAT-006` ([investigation](docs/verification/P12-FEAT-006/investigation/README.md)).

- [ ] Fillet setback controls
- [ ] Selectable fillet corner transitions

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
| `P12-HOLE-001` Hole threads, spotface, standard sizes, tolerance classes | "BetterCAD: implement P12 hole standards" | [P12-HOLE-001](docs/verification/P12-HOLE-001/README.md) |
| `P12-FEAT-006` Variable-radius fillet | `74f5c9d` | [P12-FEAT-006](docs/verification/P12-FEAT-006/README.md) |
| `P12-FEAT-005` Rib | `aa2d41a` | [P12-FEAT-005](docs/verification/P12-FEAT-005/README.md) |
| `P12-FEAT-004` Draft | `80f1fec` | [P12-FEAT-004](docs/verification/P12-FEAT-004/README.md) |

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
- **Variable-radius fillets round straight edges between two planes**,
  each on its own (no tangent chains, no corners with other rounded edges of
  the same fillet). Between stations the radius is the kernel's clamped
  cubic spline, exact at the stations and kept between them; station sets
  whose spline would leave that range are refused. They are not repeated by
  patterns or feature mirrors, and their volumes, bounds and adjacent face
  areas are measured to 1e-9, 1e-7 mm and 4.7e-6 relative
  ([P12-FEAT-006](docs/verification/P12-FEAT-006/README.md)).
- **Hole standards are the part of each standard two published copies
  agree on** ([the sources](docs/verification/P12-HOLE-001/standards/SOURCES.md)):
  the metric thread sizes of ISO 965-2 (coarse M1 to M64, fine M8x1 to
  M64x4) in the tolerance class ISO 965-2 gives each (5H up to M1.4, 6H
  above) and its G counterpart; the clearance holes of ISO 273 for those
  diameters; and the ISO 286 classes D6 to D13, E5 to E10, F3 to F10, G3 to
  G10 and H1 to H18 for sizes up to 500 mm. JS is not among them: published
  copies of ISO 286-2 disagree on whether its odd tolerances of grades 7 to
  11 are rounded to whole micrometres. Anything else — another thread class,
  a position from A to C or J to ZC, a larger size — is refused, not
  computed ([P12-HOLE-001](docs/verification/P12-HOLE-001/README.md)).
- **A thread is described, not cut.** A tapped hole is a bore at the
  thread's basic minor diameter; no helix is modelled, and the thread's
  length, class and limits are data the callout reports. A mirrored threaded
  hole keeps its thread as it is (threads are not handed), and neither STEP
  nor STL carries the thread
  ([P12-HOLE-001](docs/verification/P12-HOLE-001/README.md)).
- **A tolerance class does not change the geometry**: the hole is cut at its
  nominal size, and the class gives the limit deviations its callout
  reports. Tap drill sizes (ISO 2306), counterbore sizes for a fastener's
  head (ISO 4762, DIN 974) and thread engagement lengths are not known
  ([P12-HOLE-001](docs/verification/P12-HOLE-001/README.md)).
- **Fillets have no setback or corner-transition controls.** Where rounded
  edges meet, the kernel shapes the corner itself; setbacks and selectable
  corner transitions were deferred from `P12-FEAT-006`, since they need a
  surface-patch capability ([investigation](docs/verification/P12-FEAT-006/investigation/README.md)).
- **A sweep takes one guide curve, or a twist, never both**: the kernel
  offers one auxiliary spine, and the two say the same thing. A spatial or
  twisted sweep also needs its profile centred on the path, and meets the
  Pappus volume to 1e-5 rather than 1e-9 when a guide carries it (the
  auxiliary spine is fitted through samples). A path's runs are planar
  sketches: there is no 3D sketch, helix primitive or spline path
  (`P12-SWEEP-001`).
- **Loft sides stay B-splines** even where flat, costing about 6e-12 relative
  volume and 3.4e-6 mm in the centroid; plane references find only a loft's end
  faces. Between sections of different shapes the cost is larger and measured:
  a smooth loft through three equal circles is geometrically a cylinder and
  reproduces `pi r^2 h` to 4e-11 relative, so those lofts are checked to 1e-9
  rather than the 1e-12 equal-shape lofts still meet. The kernel also pads such
  a face's bounding box outwards by 1e-7 mm (`P12-LOFT-001`).
- **A loft has no end conditions**: `BRepOffsetAPI_ThruSections` offers none,
  and every setting it does have leaves the solid identical to the last digit.
  The usual ghost-section workaround moves the volume 23 per cent away from the
  frustum even at 0.01 mm and does not converge, so it is not offered. A
  tangent or normal end condition needs a surface builder that takes boundary
  derivatives, which is a new subsystem
  ([evidence](docs/verification/P12-LOFT-001/README.md)).
- **A smooth loft has no closed-form volume** in general: measured, it is the
  ruled loft at two sections and the quadratic through three equally spaced
  ones, but neither that nor any other law at four sections or at unequal
  spacing. Its correspondence is therefore guarded by the ruled loft of the
  same matched sections, which is still checked exactly, plus a stated
  envelope of 0.5x to 2.0x (`P12-LOFT-001`).
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
