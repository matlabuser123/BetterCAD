# P12-FEAT-004 — Draft Verification

## Status

**PASS.** A draft feature tapers named faces of another feature's body about
a neutral plane, for moulding. Each face turns by a signed angle about its
line on the plane, and the plane's normal is the pull direction. The faces
are face names (`P12-STREF-001`) looked up in the target's body. The plane
is any plane reference, including a named face, so a draft about a
part's own top follows the top when the part grows. The kernel's result is
checked, not trusted: a kernel probe found that OCCT 8.0.1 reports success
for a draft that shrinks a face to nothing.

Debug, Release and Debug-shared each passed **997/997** tests with
**0 compiler warnings** from a verified clean rebuild. Every test of the P11
qualification and of `P12-FEAT-003` still passes in all three. Every value
the existing tests measure is unchanged, bit for bit.

Date: 2026-09-17. `main` was at `eb3ad66` (`P12-FEAT-003`) before this
milestone. Every number below was measured in this session and is recorded
in this directory.

## Scope

The deliverables are the ones `TODO.md` lists for `P12-FEAT-004`.

| Deliverable | Status | Main evidence (test cases) |
| --- | --- | --- |
| `geometry::draftFaces` (faces by name, neutral plane, signed angle); planes, cylinders and cones; tangent chains; structured failures when the kernel's result is not a draft | IMPLEMENTED | `Draft_TurnsFacesAndCarriesTheirNames`; `Draft_FailuresAreStructured`; `Draft_RequestsAreValidated` |
| `DraftFeature` (target, faces, neutral plane, driven angle); consumes its target; names carried to the turned faces | IMPLEMENTED | `DraftFeature_BlockFollowsItsAngleAndHeight`; `DraftFeature_CurvedPocketedConcaveAndRoundedBodies` |
| Faces resolved by name in the target's body; the plane like any plane reference | IMPLEMENTED | `DraftFeature_FailuresAreStructuredAndAtomic` |
| Dependencies, validation, undo/redo, save/load, CLI | IMPLEMENTED | `DraftFeature_DefinitionsAreValidated`; `DraftFeature_CreationAndEditsAreUndoable`; `DraftFeature_RegeneratesDeterministically`; `DraftFile_*`; `DraftCli_*`; `cli.info.drafted-block`, `cli.validate.drafted-block`, `cli.export-step.drafted-block` |
| Evidence | this directory | — |

**Not in scope, and not implemented:**

- drafts with a separate pull direction, or about a parting line;
- different angles on different faces in one feature;
- drafting faces that are not planes, cylinders or cones;
- stopping the kernel's propagation along tangent faces;
- names for faces the draft creates (it creates none).

## Kernel Probe

`kernel-probe/draft_probe.cpp` (built against the dependency prefix as its
header says; output in `draft-probe.log`) ran `BRepOffsetAPI_DraftAngle` 31
times on bodies whose drafted volumes integrate exactly.

- **17 drafts were built as valid solids that do not intersect themselves.**
  - 16 of them have an analytic volume, and all 16 match within 4.0e-16
    relative. They cover:
    - a box's sides about its base, about its middle and about a plane above
      it;
    - negative angles, and pulling downwards;
    - one face, a cylinder, a hole and a pocket's walls;
    - an L-shaped prism;
    - a box with rounded vertical edges;
    - zero angles;
    - angles up to 36.8 degrees.
  - The 17th drafted one side of the rounded box. The kernel turned the
    whole tangent chain of sides and rounds, and gave the four-side draft's
    volume.
- **Every draft kept its topology.** Each result had the input's numbers of
  faces, edges and vertices.
- **A face shrunk to nothing is reported as a success.** At atan(0.75)
  (36.87 degrees) the 60 mm top of a 40 mm box has no width. The kernel
  returns a solid that `BRepCheck_Analyzer` finds invalid and
  `BRepAlgoAPI_Check` finds self-intersecting.
- **Larger angles fail in the kernel.** Ten runs failed at build with "edge
  recomputation": 37–60 degrees on the box, 70 and 89 degrees on one face,
  and a pull along a face's normal.
- **Faces that cannot turn fail at `Add`.** Three runs failed with "face
  recomputation": the top (parallel to the neutral plane), and a side of a
  box with rounded top edges, whose tangent chain reaches the top (with one
  side and with four).
- **The history is not the booleans'.** A turned face is reported as
  `Generated`, not `Modified`, so name carrying through `Modified()` would
  lose it. `ModifiedShape()` gives every input face's image in the result.
- No crash occurred in any run.

## Design

```text
geometry::DraftRequest                     faces (FaceName), neutral plane (Frame3D: origin, normal =
                                           pull), angle
validate(request)                          one or more faces, valid and not repeated; a finite angle in
                                           (-90, 90) deg (Draft.cpp; the face-name check is shared with
                                           shells, FaceNameList.hpp)
draftFaces(body, request)                  OcctDraft.cpp: one solid; the faces each name is on (NotFound
                                           if none), each a plane, cylinder or cone; DraftAngle::Add per
                                           face (AddDone), Build; the result checked; names through
                                           ModifiedShape()
DraftFeature / DraftDefinition             target, faces, neutralPlane (PlaneReference), angle or
                                           angleParameter; depends on the target, the parameter, the
                                           plane's objects and the faces' features; consumes the target
regenerateDraft                            features/draft/: the angle; the plane (resolvePlane() with the
                                           regenerator's bodies); requireNamedFaces() (shared with
                                           shells); draftFaces()
Validation                                 the angle parameter is an angle; the neutral plane like any
                                           plane reference; each face through checkFaceName()
DraftJson                                  "faces", "neutral_plane", "angle", "angle_parameter"
CLI info                                   "target Block, faces the side from entity:5 of Block and ...,
                                           neutral plane the end cap of Block, angle taper"
Commands                                   Create/ModifyDraftCommand
```

**Checking the kernel.** A draft is accepted only when all of these hold:

1. every face is added (`AddDone`) and the kernel is done;
2. the result is one solid with the input's numbers of faces, edges and
   vertices (the kernel's contract is that a draft changes no topology);
3. every face of the input has an image in the result;
4. the result is valid, does not intersect itself, and has finite
   positive volume and area.

Otherwise the result is FailedPrecondition, naming the face or the
reason. Kernel exceptions are reported the same way.

**The pull direction.** It is the neutral plane's normal. For a datum or
principal plane that is the plane's own normal. For a named face it points
out of the material: a block's top pulls up and its bottom pulls down. So a
draft about the bottom narrows the block going up with a *negative* angle
(OnBottom below). A draft about the top keeps the top and widens the block
below it with a positive angle (OnTop).

**The reference models** (`tests/support/DraftModels.hpp`; the first is
saved as `examples/models/drafted_block.bcad`):

| Model | Drafts |
| --- | --- |
| DraftedBlock: a 100 × 60 × `height` block | Tapered (about the model's XY plane, by `taper`), TaperedMid (about a datum plane `rise` up), Flared (−5°), OnTop (about the block's top, by `taper`), OnBottom (about its bottom, −5°) |
| DraftedParts | a cylinder's side (a frustum); a through bore's wall (widening upwards); a pocket's four walls about the block's top (narrowing downwards); an L prism's six sides; a pad with rounded vertical edges, all four sides and one side, all by `angle` |

## Independent Validation

Expected values are written out in `DraftModels.hpp`. At height z a drafted
outline has moved in by s(z) = tan(a) (z − z0), or the opposite pulled
downwards. So each section is a rectangle, circle, rounded rectangle or L
with a fixed centroid, and its area is a quadratic in z:

- a rectangle: (w − 2s)(d − 2s);
- a rounded rectangle: that less (4 − π)(r − s)²;
- a circle: π(r ± s)²;
- the L: its two arms less their overlap.

Volumes and centres are the exact integrals of the area and of z times the
area. Turned planar faces have area ∫ width dz / cos(a); a frustum's side
has π(r₀ + r₁) h / cos(a). Bounds follow the widest section.

`deviations.txt` (`deviations.py` over `reference-values-release.txt`)
lists the largest deviations:

| Check | Tolerance | Checks | Largest deviation measured (Release) |
| --- | --- | --- | --- |
| Centres of mass (the block's five drafts; the frustum, bored plate, pocketed block, L and pads) | 1e-9 mm | 96 | 1.1e-13 mm |
| Bounds | 1e-7 mm | 176 | 5.7e-14 mm |
| Volumes and named faces' areas | 1e-12 rel | 155 | 1.3e-15 |
| Geometry level: volumes and areas; bounds | 1e-12 rel; 1e-9 mm | 9; 6 | 3.7e-16; 1.4e-14 mm |
| STEP read-back volume and bounds | 1e-9 rel | 4 | 8.6e-14 |

**Following the parameters.**

- In `DraftFeature_BlockFollowsItsAngleAndHeight`, the parameters change
  in three steps, and the five drafts match at each step:
  1. `taper` 5 → 8°;
  2. `height` 40 → 55 mm;
  3. `rise` 20 → 35 mm and `taper` → −2.5°.
- When the taper changes, only the three drafts it drives are rebuilt.
- When the block grows, OnTop turns about the top where it now is.
- The four sides keep their names with their turned areas, and the top and
  bottom keep theirs with their new areas.
- OnBottom at −5° equals Tapered at 5°.
- Undo restores every volume bit for bit, and so does a fresh regeneration
  of a copy.
- In `DraftFeature_CurvedPocketedConcaveAndRoundedBodies`, `angle` goes
  3 → 5°, `height` 40 → 55 mm and `round` 10 → 8 mm. All six drafts match
  at both states, with these named areas:
  - the frustum's side and top;
  - the bore's wall, and the plate's top with its widened mouth;
  - the pocket's mouth (unchanged), its shrunk floor and its four walls;
  - the L's top;
  - the pad's planar sides, whose width between the rounds stays
    w − 2r.

## Diagnostics

Asserted verbatim by the tests:

| Case | Code | Message |
| --- | --- | --- |
| no target, no faces, a repeated or invalid face, a bad plane or angle | InvalidArgument | `a draft needs a target feature`; `a draft needs one or more faces`; `face 2 repeats an earlier face`; `face 1: a side face is named by a valid profile entity`; `the neutral plane: a plane reference must name a valid object`; `the draft angle must be in (-90, 90) deg, got 90 deg` (`inf deg`, `nan deg`, `-180 deg`); `the angle parameter ID must be valid` |
| an angle that makes the top vanish (37°) | FailedPrecondition | `Tapered: draft: the kernel cannot build the draft: kernel: an edge cannot be recomputed; an angle this large may make faces vanish` |
| the degenerate angle atan(0.75) (geometry level) | FailedPrecondition | `draft: the kernel cannot build the draft: …` |
| a driven angle out of range | InvalidArgument | `Tapered: draft: the draft angle must be in (-90, 90) deg, got 95 deg` |
| a face parallel to the plane; a tangent chain reaching one | FailedPrecondition | `WithTop: draft: face 2 cannot be turned about the neutral plane (kernel: a face cannot be recomputed); a face parallel to the plane, or a chain of tangent faces that reaches one, cannot be drafted`; `Chained: draft: face 1 cannot be turned …` |
| a torus | FailedPrecondition | `TurnedRing: draft: face 1 is a torus; only planes, cylinders and cones can be drafted` |
| a face of another body; a face a split removed | NotFound | `WrongBody: face 1, the end cap of Other (object:13), is not a face of the body of Block (object:5)`; `CutAway: face 1, the end cap of Block (object:5), is not a face of the body of Base (object:15)` |
| a face of a split | InvalidArgument | `Unnamed: face 1: Base (object:15) is a split, whose faces are not named (…)` |
| a body of two solids | FailedPrecondition | `TwoSolids: draft: a draft turns faces of one solid; the body has 2` |
| a sketch as neutral plane | InvalidArgument | `OnSketch: the neutral plane: BlockSketch (object:4) is a sketch, not a datum plane or a coordinate system` |
| no target body | FailedPrecondition | `NoBody: a draft needs the body of its target feature` |
| a length driving the angle | DimensionMismatch | `Tapered: …` |
| geometry level | FailedPrecondition; NotFound | `draft: the body is empty`; `draft: face 5 is not a face of the body`; `draft: a draft turns faces of one solid; the body has 2` |
| validation | error | `Unnamed (object:20): face 1 is the end cap of Base (object:15): Base (object:15) is a split, …`; `OnSketch (object:22): the neutral plane is BlockSketch (object:4), which is a sketch, not a datum plane or a coordinate system`; `NoBody (object:23): the target is BlockSketch (object:4), which is a sketch, not a feature with a body`; `Tapered (object:7): the angle is driven by height (object:1), which is a length, not an angle` |
| a pattern of a draft; a sketch on a draft's face | — | `… a linear pattern cannot repeat a draft`; `OnDraft (object:13): Tapered (object:7) is a draft, whose faces are not named (…)` |
| an invalid edit | InvalidArgument | refused by `ModifyDraftCommand`; the document is unchanged |
| file | — | `….neutral_plane.plane: unknown value 'xw'`, `….neutral_plane: missing required field`, `….data: the neutral plane: a plane reference must name a valid object`, `….pull: unknown field`, `….data: the draft angle must be in (-90, 90) deg, got …`, `….angle: expected a number`, `….data: a draft needs one or more faces`, `….faces: expected an array`, `….faces[0].face.role: unknown value 'edge'`, `….faces[0]: an end cap is not named by an entity`, `….faces[0].face: missing required field`, `….faces[0].id: unknown field`, `….data: a draft needs a target feature` |
| CLI `validate` | failure | the regeneration message, `Result: invalid` |

A failed draft keeps no body. The other drafts of the same body, and the
rest of the model, are built.

## Tests

16 new Catch2 test cases, tagged `[draft]` with `[p12]`, and 3 process
tests:

- `tests/core/geometry/DraftTests.cpp`: 3 cases. Requests; a box's sides
  turned with their names; failures.
- `tests/features/DraftFeatureTests.cpp`: 6 cases. Validation; the block;
  curved, pocketed, concave and rounded bodies; failures; determinism;
  undo/redo.
- `tests/io/DraftFileTests.cpp`: 5 cases. Round trips of both models, the
  written form, malformed files, the example file, STEP (five bodies).
- `tests/cli/DraftCliTests.cpp`: 2 cases (`info`, `validate`).
- `cli.info.drafted-block`, `cli.validate.drafted-block` and
  `cli.export-step.drafted-block` run on `examples/models/drafted_block.bcad`,
  each in a fresh process.

No existing test changed. The Release run of the 16 cases records
**2474 passed assertions and 0 failed**
(`reference-values-release.txt`).

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
| Debug | exit 0 | attempt 1 | exit 0 | 334 | 0 | **997/997 passed** (97.0 s) |
| Release | exit 0 | attempt 1 | exit 0 | 334 | 0 | **997/997 passed** (99.3 s) |
| Debug-shared | exit 0 | attempt 1 | exit 0 | 334 | 0 | **997/997 passed** (109.4 s) |

334 is `P12-FEAT-003`'s 325 translation units plus 9 new ones:

- `Draft.cpp`, `occt/OcctDraft.cpp`;
- `draft/DraftFeature.cpp`, `draft/DraftRegeneration.cpp`;
- `json/DraftJson.cpp`;
- the four new test files.

**Repeats.**
`ctest -R "[Dd]raft|[Ss]hell|[Ss]plit|[Cc]ombine|BodyOps|[Ff]illet|[Hh]ole|[Rr]evolve|[Ee]xtru|ThroughAll|[Rr]esult|[Pp]attern|[Mm]irror|[Ff]ace|[Rr]eference|[Dd]atum|[Bb]oolean|[Pp]ropert|[Rr]egenerat|[Vv]alidat|[Dd]ocument|[Ff]ile|[Cc]ommand|[Ee]xport|cli|drafted-block|shelled-block|body-ops|P9" --repeat until-fail:5`
selected 650 tests.

| Preset | Result | Time |
| --- | --- | --- |
| Release | 650/650, each 5 times (3250 passed runs) | 324.9 s |
| Debug | 650/650, each 5 times (3250 passed runs) | 325.9 s |

**Determinism.**

- `DraftFeature_RegeneratesDeterministically` builds the parts model twice
  and gets bit-identical volumes, equal face names and topology, and
  equivalent documents. A second pass rebuilds nothing, and a full rebuild
  gives the same bits.
- The file round trips of both models regenerate every draft to the same
  bits and serialize to identical text.
- The three process tests work in fresh processes.
- Across configurations (`values-determinism.txt`), the Debug, Release and
  Debug-shared executables printed identical values for the 16 cases
  (1298 lines, MD5 `689626d2b4828313550f58f15fe423d9`).

**Timing** (`timing-comparison.txt`: CTest's per-test times summed, `-j 8`,
same machine), P12-FEAT-003 → P12-FEAT-004, with 19 more tests:

| Preset | P12-FEAT-003 | P12-FEAT-004 |
| --- | --- | --- |
| Release | 642.8 s | 690.3 s |
| Debug | 710.3 s | 714.8 s |
| Debug-shared | 681.3 s | 707.6 s |

The 19 new tests take 11.6 s in Release, 12.0 s in Debug and 11.8 s in
Debug-shared. The existing tests' times moved by +35.8, −7.5 and +14.6 s:

- In Release, 28.4 s of that is spread over the 770 tests under 0.5 s
  (about 37 ms each), which is process start-up rather than computation.
- For the tests over 0.5 s, the median ratio of new to old time was 1.008
  in Release, 1.071 in Debug and 0.970 in Debug-shared, with about half of
  them slower in each.
- The largest move among the twelve slowest tests was 2.0 s in Release,
  3.4 s in Debug (a compile-fail test) and 1.4 s in Debug-shared.

No code the existing tests run changed apart from the shell's refactored
checks, and no speed change is claimed.

## Legacy Regression

`regression-comparison.txt` compares every log by test name with
`../P11-QUAL-001/release-ctest.log` (737 names), and
`regression-comparison-feat003.txt` with `../P12-FEAT-003/ctest-release.log`
(977 names):

- every baseline name is present and passed in the Debug, Release and
  Debug-shared logs, and no entry failed;
- the new names are this milestone's 19 tests (259 against P11).

**Every value the existing tests measure is unchanged**
(`all-values-comparison.txt`):

- Before its clean rebuild, the qualified P12-FEAT-003 Release build (the
  `eb3ad66` tree) ran every test case with `-s`. So did this milestone's
  qualified Release build.
- After dropping the 16 new cases (no existing case's name matches the
  pattern) and normalizing temporary paths, document UUIDs and the reported
  Git revision (line numbers kept: no existing test file changed), the
  35604 measured-value lines of the 913 existing cases are identical (MD5
  `1ca22a3b179cf852839c3c4a6cb72a76` both). They include the shell's
  cases, whose checks this milestone moved into shared helpers.

**The P0–P12-FEAT-003 regression suite remains green.** No existing test
changed. Changes to existing production code:

- `Regenerator`: the draft handler;
- `Validation`: the draft's angle parameter, neutral plane and faces;
- `FeatureCommands`, `Regeneration.hpp`: the command aliases and the
  regeneration functions;
- `SolidSupport`: `requireNamedFaces()`, which the shell's regeneration now
  uses too (its messages are unchanged);
- `Shell.cpp`: the face-name list check moved to `FaceNameList.hpp`,
  messages unchanged;
- `DocumentJson`, `ObjectJson`: the type;
- CLI `info`: the description.

## Known Limitations

- **The pull direction is the neutral plane's normal.** A named face pulls
  out of the material; the angle's sign chooses which way the faces lean.
- **One angle per feature.**
- **Planes, cylinders and cones only.** Other faces are refused.
- **Tangent chains turn as a whole**, as the kernel drafts them. A chain
  that reaches a face parallel to the plane (a side joined to the top by a
  round) cannot be drafted: draft first, then round.
- **Angles that make a face vanish are refused**, not trimmed.
- **One solid.** Shells and drafts cannot be repeated by patterns or
  feature mirrors, and their own faces are not named.

## Evidence Files

- `README.md`: this file.
- `kernel-probe/`: `draft_probe.cpp` and its output, `draft-probe.log`.
- `qualify.cmd`, `run-qualification.cmd`: the qualification as run.
- `qualification-times.txt`: every step's start, exit code and time, the HEAD
  and the Git tree IDs of the qualified sources.
- `configure-*.log`, `clean-*.log`, `build-*.log`, `ctest-*.log`: the three
  presets.
- `ctest-repeat-{release,debug}.log`: the related tests, 5 times each.
- `regression-comparison.txt`, `regression-comparison-feat003.txt`,
  `compare-regression.py`: the legacy comparisons.
- `all-values-comparison.txt`: every measured value of the P12-FEAT-003
  tests, against the P12-FEAT-003 Release build.
- `reference-values-release.txt`, `values.py`, `values-header.txt`: the new
  tests' measured values.
- `values-determinism.txt`, `compare-values.py`: the same from all three
  configurations.
- `deviations.txt`, `deviations.py`: the largest deviations.
- `timing-comparison.txt`, `compare-times.py`: per-test times.

## Final Result

```text
TASK:            P12-FEAT-004 Draft
IMPLEMENTATION:  draftFaces() (planes, cylinders, cones and their tangent
                 chains, the kernel's result checked); DraftFeature (target,
                 named faces, neutral plane reference, driven angle)
                 consuming its target; validation, JSON, CLI, commands
TESTS:           16 new test cases and 3 process tests; 997/997 in
                 Debug, Release, Debug-shared; 650 related tests x5 in
                 Release and Debug
VALIDATION:      drafts of a block about planes and its own faces, a
                 frustum, a bore, a pocket, an L prism and rounded pads
                 match exact section integrals within 1.3e-15 rel (volume),
                 1.1e-13 mm (centre), 5.7e-14 mm (bounds) as the angle,
                 height, plane and round change; vanishing faces refused;
                 every existing measured value unchanged; values identical
                 across configurations
RESULT:          PASS
EVIDENCE:        docs/verification/P12-FEAT-004/
TODO:            P12-FEAT-004 deliverables ticked
```
