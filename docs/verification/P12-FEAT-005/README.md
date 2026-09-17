# P12-FEAT-005 — Rib Verification

## Status

**PASS.** A rib feature fills the space between an open sketched profile
and another feature's body with a wall of a given thickness in the
sketch's plane.

- **The profile.** A chain of the sketch's lines, arcs and open splines,
  kept as sketch entities. It is extended along its end tangents, so a
  profile that stops short of the body still reaches it.
- **The filled side.** The side to the left of the profile's direction of
  travel, or to its right. That side must be closed off by the body:
  otherwise the rib is refused, never guessed.
- **Names.** The rib names its two walls and the side each profile edge
  makes, so sketches can be placed on it.

Debug, Release and Debug-shared each passed **1017/1017** tests with
**0 compiler warnings** from a verified clean rebuild. Every test of the P11
qualification and of `P12-FEAT-004` still passes in all three. Every value
the existing tests measure is unchanged, bit for bit, except one message
this milestone deliberately extended (see Legacy Regression).

Date: 2026-09-18. `main` was at `80f1fec` (`P12-FEAT-004`) before this
milestone. Every number below was measured in this session and is recorded
in this directory.

## Scope

The deliverables are the ones `TODO.md` lists for `P12-FEAT-005`.

| Deliverable | Status | Main evidence (test cases) |
| --- | --- | --- |
| `geometry::addRib` (open profile, thickness, placement, side): the prism of the extended profile's side less the body; the pieces the profile bounds are kept; an open side fails | IMPLEMENTED | `Rib_FillsTheCornerOfABracket`; `Rib_FailuresAreStructured`; `Rib_RequestsAreValidated` |
| `RibFeature` (target, profile sketch and edges, driven thickness, placement, flipped); consumes its target; names its walls and sides | IMPLEMENTED | `RibFeature_FollowsItsBracketAndThickness`; `RibFeature_ArcsChainsSplinesAndShortProfiles`; `RibFeature_WallsCarrySketchesAndBodiesCarryRibs` |
| Profile resolution (head to tail; lines, arcs, open splines; open) with structured errors | IMPLEMENTED | `RibFeature_FailuresAreStructuredAndAtomic` |
| Dependencies, validation, undo/redo, save/load, CLI | IMPLEMENTED | `RibFeature_DefinitionsAreValidated`; `RibFeature_CreationAndEditsAreUndoable`; `RibFeature_RegeneratesDeterministically`; `RibFile_*`; `RibCli_*`; `cli.info.ribbed-bracket`, `cli.validate.ribbed-bracket`, `cli.export-step.ribbed-bracket` |
| Evidence | this directory | — |

**Not in scope, and not implemented:**

- ribs drawn across their thickness ("normal to the sketch": the profile
  thickened in its plane and extruded to the body);
- draft on rib walls;
- several separate ribs from one sketch in one feature;
- profiles of ellipse arcs;
- names for the faces the extensions make.

## Design

```text
geometry::RibPlacement {Symmetric, AlongNormal, AgainstNormal}   Rib.hpp
RibRequest                         profile (PlanarPath: plane + open chain), thickness, placement, flipped
validate(request)                  an open chain of non-degenerate lines, arcs and open splines, head to tail
                                   within 1e-7 mm; a positive finite thickness; a known placement
addRib(body, request, namer)       Rib.cpp, built from public geometry operations only:
                                   1. bounds: the profile's extent in the plane (arc extremes, spline poles)
                                      and the body's bounding box projected on the plane, plus 1 mm
                                   2. the loop: start extension (along the start tangent, backwards, to the
                                      bounds), the chain, end extension, then counter-clockwise along the
                                      bounds back to the start (the chain reversed when flipped)
                                   3. makePrism(loop, thickness offsets) with marker names on every face
                                   4. that prism less the body (its names dropped): solidsOf() the rest
                                   5. keep each piece with a profile-marked face; a kept piece with a
                                      bounds-marked face is an open side
                                   6. markers -> the namer's names; booleanUnion() each piece with the body
                                   7. valid, same number of solids, volume = body + pieces (1e-9 rel)
solidsOf(body)                     Split.hpp: the body's solids as bodies, with their faces' names
RibFeature / RibDefinition         target, profile, edges, thickness or thicknessParameter, placement,
                                   flipped; depends on the target, the sketch and the parameter
resolveRibProfile                  features/rib/: the edges in order, head to tail, each later edge
                                   starting exactly where the one before ends, in the sketch's placement
regenerateRib                      the rib's names: walls = start/end caps, side i = Side(edges[i])
FaceReferences                     ribs name their faces (namesFaces()); checkRole(): caps, and sides whose
                                   entity is one of the rib's edges, without a path edge
Validation                         the profile is a sketch; the thickness parameter is a length; every edge
                                   exists in the sketch
RibJson                            "profile", "edges", "thickness", "thickness_parameter", "placement",
                                   "flipped"
CLI info                           "target Bracket, profile RibSketch (1 edge), thickness thickness,
                                   symmetric, left side"
Commands                           Create/ModifyRibCommand
```

**No kernel probe.** The rib adds no kernel algorithm. It uses prisms,
booleans and the booleans' face history, which earlier milestones
qualified. The kernel behaviour it relies on is exercised by its tests:

- a face split by a boolean keeps its name on every piece, and a deleted
  face loses it;
- coplanar neighbouring faces are merged. A straight profile's extensions
  continue it, so the rib's free face is one plane from wall to wall,
  carrying the profile's name (`ShortRib`).

**Why pieces and markers.** The slab less the body can leave material in
several places:

- between the profile and the body: the rib;
- beyond the body, where the extensions run out into the open: pieces that
  touch the bounds but not the profile.

Only pieces with a face of the profile are the rib. If one of them also
has a face of the bounds, the side is open and the rib would run to
infinity; it is refused. Nothing is decided by position or proximity.

**The reference models** (`tests/support/RibModels.hpp`; the first is saved
as `examples/models/ribbed_bracket.bcad`):

| Model | Ribs |
| --- | --- |
| RibbedBracket: an L bracket (floor 80 × `floor`, wall `wall` × 60, 40 deep, as a join) | a line x + y = 50 on a datum plane `mid` up, `thickness` thick, symmetric |
| RibProfiles: the same bracket, fixed | a tangent arc (flipped), a two-line chain (symmetric and along the normal), a cubic spline, and a line stopping short of the walls |
| (in the tests) a shelled box | a divider on the plane y = 30, filling the cavity below z = 30 |
| (in the tests) the ribbed bracket | a circular boss on a sketch on the rib's upper wall |

## Independent Validation

Every rib is a prism, so its volume is the area it fills times its
thickness, and its centre is that area's centroid at the slab's mid-plane.
The areas are written out in `RibModels.hpp`:

- the line x + y = 50 across a corner at (w, f) fills a right triangle with
  legs 50 − w − f, centroid a third of the way along each leg;
- the tangent arc about (40, 40), radius 30, fills the square 30 × 30 less a
  quarter disc: 900 − 225π, centroid from the square's and the disc's
  moments (the disc's centroid is 4r/(3π) from its centre);
- the chain (40, 10) (20, 20) (10, 40) fills two triangles of 150 each;
- the spline fills the region it closes with the walls. BetterCAD's own
  Green's-theorem functions (`regionArea()`, `regionCentroid()`,
  independent of the kernel, qualified in P12-SKETCH-002) give its area and
  centroid;
- the divider fills 90 × 25 × 2 between the shell's inner walls and floor.

The bracket's parts and the rib are summed as boxes and prisms. The named
faces are checked by area:

- the walls: the filled area;
- the profile's side: its length between the walls times the thickness;
- the bracket's floor top and wall face: less the rib's foot, l × t each.

`deviations.txt` (`deviations.py` over `reference-values-release.txt`)
lists the largest deviations:

| Check | Tolerance | Checks | Largest deviation measured (Release) |
| --- | --- | --- | --- |
| Centres of mass (the bracket with its ribs; the boss on the rib wall; the divided box) | 1e-9 mm | 52 | 1.2e-13 mm |
| Bounds | 1e-7 mm | 61 | 5.7e-14 mm |
| Volumes and named faces' areas | 1e-12 rel | 42 | 1.0e-15 |
| The spline rib's wall area (the kernel's face-area integration, see Known Limitations) | 1e-5 rel | 1 | 4.3e-6 |
| Geometry level: volumes and areas; centres | 1e-12 rel; 1e-9 mm | 12; 5 | 4.1e-16; 1.4e-14 mm |
| Geometry level: the spline rib's volume | 1e-9 rel | 1 | 1.4e-16 |
| STEP read-back volume and bounds | 1e-9 rel | 3 | 2.7e-16 |

The geometry-level spline check was written with 1e-9 rather than the
1e-12 its Green's-theorem reference supports (the feature-level spline check
uses 1e-12). The measured 1.4e-16 would meet either.

**Following the parameters.** In `RibFeature_FollowsItsBracketAndThickness`:

- `thickness` 4 → 6 mm regenerates only the rib. Undoing a change of the
  rib's own thickness restores it bit for bit.
- `mid` 20 → 30 mm moves the rib with its sketch's plane, and its upper
  wall with it.
- `floor` 10 → 15 and `wall` 10 → 5 mm move the corner: the same line then
  closes a triangle with legs 30.
- Undo restores every value to its analytic value. Undoing the walls
  re-solves their sketches, which is exact only to rounding
  (P12-SKETCH-003), so this is not asserted bit for bit; a fresh
  regeneration of a copy gives the same bits.

In `RibFeature_WallsCarrySketchesAndBodiesCarryRibs`, a boss sketched on the
rib's upper wall moves up with it as the rib thickens (4 → 8 mm).

## Diagnostics

Asserted verbatim by the tests:

| Case | Code | Message |
| --- | --- | --- |
| definitions | InvalidArgument | `a rib needs a target feature`; `a rib needs a profile sketch`; `a rib needs one or more profile edges`; `profile edge 3 must be a valid entity`; `profile edge 3 repeats an earlier edge`; `the rib thickness must be positive and finite, got 0 mm`; `the thickness parameter ID must be valid`; `a rib's thickness lies symmetric, along or against the normal` |
| geometry requests | InvalidArgument | `a rib needs a profile of one or more segments`; `profile segment 1 is a full circle; a rib profile is an open chain of lines, arcs and open splines`; `profile segment 2 has zero length`; `profile segment 1 does not meet the next`; `the profile is closed; a rib profile is open`; `profile segment 1 is an arc whose ends are not on one circle`; `profile segment 1: a spline of degree 3 needs at least 4 poles, got 2`; `profile segment 1 is not finite` |
| an open side | FailedPrecondition | `Rib: rib: the side the rib fills is not closed off by the body: it reaches past the body (fill the other side, or turn the profile towards the body)` (also a rib on a plane beside the body) |
| a profile inside the body | FailedPrecondition | `Rib: rib: the profile lies inside the body, so there is nothing to fill` |
| a profile that crosses itself once extended | FailedPrecondition | `rib: the profile, extended along its end tangents, crosses itself (makePrism: the profile face is invalid (self-intersecting or overlapping loops?))` |
| profile edges | InvalidArgument; NotFound | `OnCircle: the profile edge entity:2 is a circle, not a line, arc or open spline`; `OnHelper: the profile edge entity:5 is construction geometry, which makes no rib`; `Apart: the profile is not connected: the edges entity:8 and entity:11 do not meet (their nearest ends are 1 mm apart)`; `Closed: rib: the profile is closed; a rib profile is open`; `Missing: the profile edge entity:99 does not exist in sketch 'Odd'`; `NotSketch: profile sketch:4 is not a sketch in this document` |
| thickness | DimensionMismatch; InvalidArgument | `Rib: …` (an angle parameter); `Rib: rib: the rib thickness must be positive and finite, got -2 mm` |
| validation | error | `Missing (object:20): the profile edge entity:99 does not exist in Odd (object:15)`; `NotSketch (object:21): the profile is Bracket (object:4), which is an extrude, not a sketch`; `Rib (object:11): the thickness is driven by tilt (object:12), which is an angle, not a length` |
| names of rib faces | NotFound; InvalidArgument | `NotEdge (object:12): Rib (object:11): entity:1 is not an edge of its profile`; `Bottom (object:13): Rib (object:11) is a rib, which has no hole bottom`; `Along (object:14): Rib (object:11) is a rib, whose sides are not named by a path edge` |
| a pattern of a rib | — | `… a linear pattern cannot repeat a rib` |
| an invalid edit | InvalidArgument | refused by `ModifyRibCommand`; the document is unchanged |
| file | — | `….placement: unknown value 'both'`, `….flipped: expected true or false`, `….side: unknown field`, `….data: a rib needs one or more profile edges`, `….edges: expected an array`, `….edges[0]: expected an ID`, `….data: profile edge 2 repeats an earlier edge`, `….data: a rib needs a profile sketch`, `….data: a rib needs a target feature`, `….profile: missing required field`, `….data: the rib thickness must be positive and finite, got 0 mm` |
| CLI `validate` | failure | the regeneration message, `Result: invalid` |

A failed rib keeps no body; the rest of the model is built.

## Tests

17 new Catch2 test cases, tagged `[rib]` with `[p12]`, and 3 process tests:

- `tests/core/geometry/RibTests.cpp`: 3 cases. Requests; the bracket's
  corner filled by lines (on, inside and short of the walls; flipped; each
  placement), an arc, a chain and a spline; failures.
- `tests/features/RibFeatureTests.cpp`: 7 cases. Validation; the bracket
  following its parameters; arcs, chains, splines and short profiles; a
  sketch on a rib wall and a divider in a shelled box; failures;
  determinism; undo/redo.
- `tests/io/RibFileTests.cpp`: 5 cases. Round trips of both models, the
  written form, malformed files, the example file, STEP.
- `tests/cli/RibCliTests.cpp`: 2 cases (`info`, `validate`).
- `cli.info.ribbed-bracket`, `cli.validate.ribbed-bracket` and
  `cli.export-step.ribbed-bracket` run on
  `examples/models/ribbed_bracket.bcad`, each in a fresh process.

The Release run of the 17 cases records **1881 passed assertions
and 0 failed** (`reference-values-release.txt`).

## Qualification

`qualify.cmd` was run through `run-qualification.cmd` and did the following:

- configured each preset;
- removed every build output;
- rebuilt with warnings as errors under code page 65001;
- ran CTest after each successful build;
- repeated the related tests five times in Release and Debug.

The Git tree IDs it recorded equal the working tree's after the run.

| Preset | Configure | Clean | Build | TUs compiled | `warning` lines | Tests |
| --- | --- | --- | --- | --- | --- | --- |
| Debug | exit 0 | attempt 1 | exit 0 | 342 | 0 | **1017/1017 passed** (100.3 s) |
| Release | exit 0 | attempt 1 | exit 0 | 342 | 0 | **1017/1017 passed** (101.2 s) |
| Debug-shared | exit 0 | attempt 1 | exit 0 | 342 | 0 | **1017/1017 passed** (116.1 s) |

342 is `P12-FEAT-004`'s 334 translation units plus 8 new ones:

- `Rib.cpp`;
- `rib/RibFeature.cpp`, `rib/RibRegeneration.cpp`;
- `json/RibJson.cpp`;
- the four new test files.

**Repeats.**
`ctest -R "[Rr]ib|[Dd]raft|[Ss]hell|[Ss]plit|[Cc]ombine|BodyOps|[Ff]illet|[Hh]ole|[Rr]evolve|[Ee]xtru|ThroughAll|[Rr]esult|[Pp]attern|[Mm]irror|[Ff]ace|[Rr]eference|[Dd]atum|[Bb]oolean|[Pp]ropert|[Rr]egenerat|[Vv]alidat|[Dd]ocument|[Ff]ile|[Cc]ommand|[Ee]xport|cli|ribbed-bracket|drafted-block|shelled-block|body-ops|P9" --repeat until-fail:5`
selected 671 tests.

| Preset | Result | Time |
| --- | --- | --- |
| Release | 671/671, each 5 times (3355 passed runs) | 306.2 s |
| Debug | 671/671, each 5 times (3355 passed runs) | 337.4 s |

**Determinism.**

- `RibFeature_RegeneratesDeterministically` builds the profiles model twice
  and gets bit-identical volumes, equal face names and topology, and
  equivalent documents. A second pass rebuilds nothing, and a full rebuild
  gives the same bits.
- The file round trips of both models regenerate every rib to the same bits
  and serialize to identical text.
- The three process tests work in fresh processes.
- Across configurations (`values-determinism.txt`), the Debug, Release and
  Debug-shared executables printed identical values for the 17 cases
  (642 lines, MD5 `e60da683ac187c4184a5c12d7d99617a`).

**Timing** (`timing-comparison.txt`: CTest's per-test times summed, `-j 8`,
same machine), P12-FEAT-004 → P12-FEAT-005, with 20 more tests:

| Preset | P12-FEAT-004 | P12-FEAT-005 |
| --- | --- | --- |
| Release | 690.3 s | 718.2 s |
| Debug | 714.8 s | 741.4 s |
| Debug-shared | 707.6 s | 762.3 s |

The 20 new tests take 13.2 s in Release, 13.5 s in Debug and 14.4 s in
Debug-shared. The existing tests' times moved by +14.6, +13.0 and +40.4 s
(`timing-split.py`, its output at the end of `timing-comparison.txt`):

- In Debug-shared, +29.8 s of that is spread over the 750 tests under
  0.5 s, which is process start-up rather than computation.
- For the tests of 0.5 s or more, the median ratio of new to old time was
  1.020, 1.015 and 1.014, with 55–59 % of them slower.
- The largest move among the twelve slowest tests was +0.7 s in Release,
  +1.4 s in Debug and +0.5 s in Debug-shared.

The only code the existing tests run that changed is the face-name check's
list of kinds and the new `dynamic_cast` branches. No speed change is
claimed.

## Legacy Regression

`regression-comparison.txt` compares every log by test name with
`../P11-QUAL-001/release-ctest.log` (737 names), and
`regression-comparison-feat004.txt` with `../P12-FEAT-004/ctest-release.log`
(996 names):

- every baseline name is present and passed in the Debug, Release and
  Debug-shared logs, and no entry failed;
- the new names are this milestone's 20 tests (279 against P11).

**One message changed, on purpose.** A reference to a face of a kind that
names no faces is refused with a list of the kinds that do. Ribs now name
theirs, so the list reads "(extrudes, revolves, sweeps, lofts, holes,
chamfers and ribs name theirs)" instead of "(extrudes, revolves, sweeps,
lofts, holes and chamfers name theirs)". Eight assertions quote it:
`SketchOnFaceTests.cpp` (1), `BodyOpsTests.cpp` (1),
`ShellFeatureTests.cpp` (3) and `DraftFeatureTests.cpp` (3). They were
updated to the new text, and nothing else in them changed. One of them was
re-wrapped onto an extra line, which moves the later line numbers of
`ShellFeatureTests.cpp`.

**Every other value the existing tests measure is unchanged**
(`all-values-comparison.txt`):

- Before its clean rebuild, the qualified P12-FEAT-004 Release build (the
  `80f1fec` tree) ran every test case with `-s`. So did this milestone's
  qualified Release build.
- The 17 new cases were dropped (no existing case's name matches the
  pattern), and temporary paths, document UUIDs and the reported Git
  revision were normalized. Source line numbers were replaced, because
  `ShellFeatureTests.cpp`'s lines moved.
- The old message text was replaced by the new one in the P12-FEAT-004
  values (14 lines hold it). After that, the 36895 measured-value lines of
  the 929 existing cases are identical (MD5
  `ba2371a47dd89f1dbade97511629f673` both).
- Without that replacement, exactly those 14 lines differ.

**The P0–P12-FEAT-004 regression suite remains green.** Changes to existing
production code:

- `Regenerator`: the rib handler;
- `Validation`: the rib's profile, parameter and edges;
- `FaceReferences`: ribs name their faces; the message above;
- `Split.hpp` / `OcctSplit.cpp`: `solidsOf()`;
- `FeatureCommands`, `Regeneration.hpp`: the command aliases and the
  regeneration functions;
- `DocumentJson`, `ObjectJson`: the type;
- CLI `info`: the description.

## Known Limitations

- **Ribs lie along their sketch plane** and fill towards the body on one
  side of the profile. Ribs thickened across the plane and extruded to the
  body are not built.
- **The filled side must be closed off by the body.** An open side is
  refused, not trimmed. A profile whose tangent extensions cross it is
  refused.
- **Profiles are lines, arcs and open splines**, one chain per rib.
- **Face areas of planes bounded by splines** (as a spline rib's walls) come
  from the kernel's default surface integration, 4.3e-6 relative off here,
  while the rib's volume is within 1e-12 of its reference. The test checks
  that area within 1e-5, which is the reason for that tolerance.
- **The faces the extensions make are not named**, except where they merge
  with the profile's own face.
- Ribs cannot be repeated by patterns or feature mirrors.

## Evidence Files

- `README.md`: this file.
- `qualify.cmd`, `run-qualification.cmd`: the qualification as run.
- `qualification-times.txt`: every step's start, exit code and time, the HEAD
  and the Git tree IDs of the qualified sources.
- `configure-*.log`, `clean-*.log`, `build-*.log`, `ctest-*.log`: the three
  presets.
- `ctest-repeat-{release,debug}.log`: the related tests, 5 times each.
- `regression-comparison.txt`, `regression-comparison-feat004.txt`,
  `compare-regression.py`: the legacy comparisons.
- `all-values-comparison.txt`: every measured value of the P12-FEAT-004
  tests, against the P12-FEAT-004 Release build.
- `reference-values-release.txt`, `values.py`, `values-header.txt`: the new
  tests' measured values.
- `values-determinism.txt`, `compare-values.py` (with `--substitute`, new
  here): the same from all three configurations.
- `deviations.txt`, `deviations.py`: the largest deviations.
- `timing-comparison.txt`, `compare-times.py`, `timing-split.py`: per-test
  times.

## Final Result

```text
TASK:            P12-FEAT-005 Rib
IMPLEMENTATION:  addRib() (the extended profile's side as a slab, less the
                 body, the bounded pieces kept, open sides refused, joined);
                 solidsOf(); RibFeature (target, profile sketch and edges,
                 driven thickness, placement, side) naming its faces;
                 validation, JSON, CLI, commands
TESTS:           17 new test cases and 3 process tests; 1017/1017 in
                 Debug, Release, Debug-shared; 671 related tests x5 in
                 Release and Debug
VALIDATION:      ribs of lines (on, inside and short of the walls), a
                 tangent arc, a two-line chain and a spline, a divider in a
                 shelled box and a boss on a rib wall match analytic and
                 Green's-theorem values within 1.0e-15 rel (volume),
                 1.2e-13 mm (centre), 5.7e-14 mm (bounds) as the thickness,
                 plane and walls change; open sides refused; every existing
                 measured value unchanged but for one deliberately extended
                 message; values identical across configurations
RESULT:          PASS
EVIDENCE:        docs/verification/P12-FEAT-005/
TODO:            P12-FEAT-005 deliverables ticked
```
