# P12-STREF-001 — Stable Feature Face References Verification

## Status

**PASS.** Faces are named by the feature that generates them and their role
in it. Extrudes name their start and end caps and the side each profile
entity sweeps. The names ride on the faces of the feature's body through its
own booleans, via the kernel's history. A sketch or datum placed on such a
face is resolved by that name in the feature's body, with no geometric
fallback. It follows the face when parameters, including expressions, move
it, and fails with a structured diagnostic when the face is gone.

This milestone was authorized on 2026-09-17 as the minimal prerequisite of
`P12-SKETCH-003`, which had stopped on exactly this gap
(`../P12-SKETCH-003/README.md`). It is not the semantic-topology phase. There
is no edge naming, adjacency matching, confidence scoring, topology
reconciliation, or referencing for assemblies, drawings or simulation.

Debug, Release and Debug-shared each passed **892/892** tests with **0
compiler warnings** from a verified clean rebuild. Every test of the P11
qualification and of `P12-DATUM-001` still passes in all three. Every value
the existing tests measure is unchanged, bit for bit (see *Legacy
regression*).

Date: 2026-09-17. `main` was at `9b74f75` (the `P12-SKETCH-003` blocker
record, on `fe20b0f`, `P12-DATUM-001`) before this milestone. Every number
below was measured in this session and is recorded in this directory.

## Scope

The deliverables are the ones `TODO.md` lists for `P12-STREF-001`.

| Deliverable | Status | Main evidence (test cases) |
| --- | --- | --- |
| Persistent face names: feature ID and role (start cap, end cap, side by profile entity) | IMPLEMENTED | `FaceReference_ReferencesAreCheckedOnTheirOwn`; `FaceReferenceFile_SaveLoad_ResolvesTheSameFaces` (the file holds IDs and roles only) |
| Bodies carry the names their feature generated; extrudes name caps and sides | IMPLEMENTED | `FaceNames_PrismNamesItsCapsAndSides`; `FaceReference_ExtrudesNameTheirCapsAndSides` |
| Names follow the feature's own booleans (split, merged, deleted faces) | IMPLEMENTED | `FaceNames_BooleansCarryNamesThroughTheKernelHistory`; `FaceNames_AreTheSameOnEveryBuild`; `FaceReference_NeverTakesAnotherFeaturesFace` |
| Plane references to a feature's face, resolved by name, no geometric fallback | IMPLEMENTED | `FaceReference_SketchOnAnEndCapFollowsTheDepth`; `FaceReference_NeverTakesAnotherFeaturesFace`; `FaceFrame_UsesTheFaceCoordinatesAndTheOutwardNormal` |
| Structured failures | IMPLEMENTED | `FaceReference_FailuresAreStructuredAndBlockDependents`; `FaceReference_ExtrudesNameTheirCapsAndSides` (cylinder) |
| Sketches follow; datum planes and axes may be placed from faces | IMPLEMENTED | `FaceReference_SketchOnAnEndCapFollowsTheDepth`; `FaceReference_SketchOnASideFollowsItsEntity`; `FaceReference_DatumsMayBePlacedFromFaces` |
| Dependency edges, validation, CLI, save/load | IMPLEMENTED | `FaceReference_DatumsMayBePlacedFromFaces` (graph); `FaceReference_ValidationReportsBadFaceReferences`; `FaceReferenceCli_*`; `cli.info.face-block`, `cli.validate.face-block`, `cli.export-step.face-block`; `FaceReferenceFile_*`; `FaceReference_AttachingIsUndoable`; `FaceReference_RegeneratesDeterministically` |
| Evidence | this directory | — |

**Not in scope, and not implemented:** names for the faces of revolves,
sweeps, lofts, holes, chamfers, fillets, patterns and mirrors
(`P12-SKETCH-003` extends the naming to the features that generate planar
faces); names that survive later features (a reference is resolved in its
own feature's body); edge names; face references in mirror planes, holes,
chamfers and fillets (these keep their geometric signatures).

## Design

```text
FaceRole, FaceSelector, FaceName      the persistent name: feature ID, role, profile entity;
  (core, document/References)         PlaneReference::face names a feature's face
NamedFace, BodyData::names            names on the faces of a body, (face, name) once each, in
  (geometry adapter)                  the order of the shape's face map
canonicalNames, carriedNames          order; history through an operation: deleted -> dropped,
  (occt/OcctFaceNames)                modified -> every image, kept -> kept
makePrism(..., PrismFaceNamer)        caps (FirstShape, LastShape) and one side per segment
                                      (Generated(edge)), in the region's own segment order
booleanOperation                      carries both operands' names, including the merging of
                                      coplanar faces (SimplifyResult)
listFaces, findNamedFaces, faceFrame  names in face descriptions; lookup by name; a face's
  (geometry)                          sketch frame
extractLabelledRegions (features)     regions with the sketch entity of every segment
extrudeTool                           names caps (per direction) and sides (per entity)
FaceReferences (features)             namesFaces, describe, checkFaceName, resolveFacePlane
resolvePlane / resolveAxis            take a BodyLookup; face references resolve through it
Regenerator                           passes the bodies built so far to sketches and datums
Validation, DatumJson, CLI            checks, "face": {"role", "entity"}, descriptions
```

**Names.** A `FaceName` is `{feature, {role, entity}}`. The roles are
`start_cap`, `end_cap` and `side`; a side names the profile entity that
sweeps it. For an extrude:

- **Normal:** the start cap lies on the sketch plane and the end cap at the
  depth.
- **Reversed:** the start cap is on the sketch plane and the end cap at
  −depth.
- **Symmetric:** the start cap is at −depth/2 and the end cap at +depth/2.

The kernel faces, their order and their addresses are never stored.

**Generation.** `makePrism()` asks the namer once per face it generates. It
names the region at `from` (`FirstShape()`) and at `to` (`LastShape()`), and
for every segment of every loop, the face `Generated()` from its edge. The
adapter reverses loops that run the wrong way. It records the edges in the
region's segment order, so the side names follow the segments as given. The
kernel probe confirms that each profile edge generates exactly one face and
that the caps are faces of the result (`kernel-probe/face-history-probe.log`,
case A).

**Booleans.** `booleanOperation()` merges coplanar faces
(`SimplifyResult()`), and the kernel's history includes that merging. For
each named face of either operand:

- a deleted face loses its names;
- a modified face passes them to every face of the result it became;
- a kept face keeps them;
- a face the history does not mention loses them.

The probe shows each case (case B, OCCT 8.0.1):

| Case | Face | History |
| --- | --- | --- |
| side by side | both tops | modified into one merged face |
| side by side | the shared walls | deleted |
| boss on a block | the boss's bottom | deleted |
| boss on a block | the block's top | modified into the face with the hole |
| through slot | the block's top | modified into two faces |
| through slot | the tool's top and ends | deleted |
| pocket | the tool's top (on the block's top) | deleted |
| pocket | the tool's bottom | kept: the floor |
| common | faces outside the other operand | deleted; the rest modified |

**Resolution.** `resolveFacePlane()` runs these steps in order:

1. `checkFaceName()`: the feature exists, is a feature, and is of a kind that
   names faces (extrudes). The selector is valid. A side's entity is a
   non-construction curve of the profile sketch.
2. It takes the feature's body from the regenerator.
3. It collects the faces that carry the name.
4. It fails when there are none (NotFound), when a named face is not a plane
   (InvalidArgument), or when the named faces do not share one plane and
   side (1e-9 rad, 1e-7 mm; FailedPrecondition).
5. It returns `faceFrame()` of the first. The origin is the plane's point
   nearest the model origin, X the face's u axis, Z the outward normal, and
   Y = Z × X.

There is no step that looks for a face where the named face used to be.

**Why the feature's own body.** A reference names a face its feature
generates, and the feature's body is where that face exists by
construction. A dependent sketch depends on that feature only, so the
dependency graph matches the resolution. Later features cannot move the
reference, and a later cut that removes the face does not take the plane
away. The body also holds the target's faces (a join or cut), so the name,
not the geometry, has to pick the right face there:
`FaceReference_NeverTakesAnotherFeaturesFace` places the reference model's
base on a taller step for exactly this.

**The reference model** (`tests/support/FaceModels.hpp`,
`examples/models/face_block.bcad`):

| Object | Definition |
| --- | --- |
| `plate` | 10 mm |
| `height` | `2 * plate` |
| Step | a 50 × 60 × 35 block |
| Base | a 100 × 60 × `height` block joined to Step |
| Boss | r 8, 10 mm, on Base's end cap |
| Pocket | 20 × 10 × 5, cut into the side Base's front line sweeps |

## Independent Validation

Expected values are written out in the tests from the parameters:

- the planes and frames of an axis-aligned block's faces;
- volume = 6000 h + 105000 + 640π − 1000 mm³;
- the centre from the four parts' moments;
- the bounds from the tallest part.

| Check | Tolerance | Largest deviation measured (Release) |
| --- | --- | --- |
| Face frames: origins (sketches, datum planes, datum axis points) | 1e-12 mm | 3.6e-15 mm |
| Face frames: axes and normals | 1e-14 | 0 |
| Named faces' planes, normals and centroids (prisms, booleans) | 1e-9 mm, 1e-12 | 0 |
| Named faces' areas (caps with holes, merged, split, floors, a cylinder) | 1e-12 rel | 1.9e-16 |
| Reference model volume, at heights 20, 30, 35, 40 and 50 mm | 1e-12 rel | 4.1e-16 |
| Reference model centre of mass | 1e-9 mm | 1.4e-14 mm |
| Reference model bounds | 1e-7 mm | 2.8e-14 mm |
| Side-pad volumes (width 100, 130, 70 mm) | 1e-12 rel | 3.4e-16 |
| STEP read-back volume | 1e-9 rel | 1.0e-15 |

**Driven geometry.** In `FaceReference_SketchOnAnEndCapFollowsTheDepth`:

- `plate` 10 → 15 → 25 mm drives `height` 20 → 30 → 50 mm through its
  expression.
- The boss sketch's frame moves to z = 20, 30, 50 with it.
- Volumes, centres and bounds match at each height.
- Undo restores the first state bit for bit.

In `FaceReference_SketchOnASideFollowsItsEntity`, a pad sketched on the side
of a driven line follows it as the width goes 100 → 130 → 70 mm.

**Wrong-face prevention.** In `FaceReference_NeverTakesAnotherFeaturesFace`:

- At height 40, nothing lies on the old plane z = 20, and the nearest
  parallel face in Base's body is Step's top at z = 35. The boss goes to
  z = 40.
- At height 35 the two tops merge. The merged face carries both names and
  resolves at 35.
- The pocket's start cap was cut away with the front face lying on its
  plane. It fails with NotFound, although a plane match finds the front
  face.
- A top split in two by a wall resolves to the one plane.

## Diagnostics

Asserted verbatim by the tests:

| Case | Code | Message |
| --- | --- | --- |
| side without an entity | InvalidArgument | `a side face is named by a valid profile entity` |
| cap with an entity | InvalidArgument | `an end cap is not named by an entity` |
| face without a feature | InvalidArgument | `a face reference must name the feature that generates the face` |
| face with a plane | InvalidArgument | `a face reference has no principal plane of its own, got yz` |
| feature deleted | NotFound | `object:7 references object:6, which does not exist` (regeneration); `object:6 does not exist` (resolution) |
| a sketch | InvalidArgument | `BossSketch (object:7): StepSketch (object:3) is a sketch, not a feature, and has no faces` |
| a kind that names no faces | InvalidArgument | `OnRow (object:12): Row (object:11) is a linear pattern, whose faces cannot be referenced yet (extrudes name their faces)` |
| unknown entity | NotFound | `SideSketch (object:9): Base (object:6): entity:99 is not an entity of its profile BaseSketch (object:5)` |
| a point / construction | InvalidArgument | `… entity:1 of its profile is a point, which sweeps no face`; `… is construction geometry, which sweeps no face` |
| face gone | NotFound | `Late (object:11): the start cap of Pocket (object:10) is not a face of its body (the feature's operation left no such face)` |
| not a plane | InvalidArgument | `OnWall (object:11): the side from entity:2 of Boss (object:8) is a cylinder, not a plane` |
| no bodies | FailedPrecondition | `the end cap of Base (object:6) is found in its feature's regenerated body, which is not available here`; `… cannot be found: Base (object:6) has no body` |
| feature fails | — | the sketches and features on it are blocked; placements unchanged |
| cycle | FailedPrecondition | `dependency cycle: …` (a sketch attached to the feature it profiles) |
| mirror plane | InvalidArgument | `a mirror plane refers to a datum plane or a coordinate system, not to a face` |
| validation | error | `BossSketch (object:7): the attachment is the end cap of StepSketch (object:3): StepSketch (object:3) is a sketch, not a feature, and has no faces` |
| file | — | `….attachment.face.role: unknown value 'top'`; `….attachment: an end cap is not named by an entity`; `….attachment.plane: a face reference has no plane of its own`; `….attachment.face.id: unknown field`; `….attachment.face.entity: expected an ID` |

In every failure the object and its dependents keep no result, and after
undo the model regenerates to its analytic values.

## Tests

20 new Catch2 test cases, all tagged `[p12]`, and 3 process tests:

- `tests/core/geometry/FaceNameTests.cpp`: 4 (prism names, names through
  booleans, the same names on every build, face frames);
- `tests/features/FaceReferenceTests.cpp`: 10 (references on their own,
  extrude names, following the depth, wrong-face prevention, following a
  side, datums, failures, validation, undo, determinism);
- `tests/io/FaceReferenceFileTests.cpp`: 4 (round trip, example file,
  malformed files, STEP);
- `tests/cli/FaceReferenceCliTests.cpp`: 2 (`info`, `validate`);
- `cli.info.face-block`, `cli.validate.face-block`,
  `cli.export-step.face-block` on `examples/models/face_block.bcad`. Each is
  a fresh process that loads the file, resolves the references and prints
  the volume and bounds.

`tests/support/FaceModels.hpp` builds the reference model. Its saved form
is `examples/models/face_block.bcad`, which
`FaceReferenceFile_ExampleFileMatchesTheBuilder` keeps equal to it.

The Release run of the 20 cases records **2665 passed assertions and 0
failed** (`reference-values-release.txt`).

## Qualification

`qualify.cmd` was run through `run-qualification.cmd` and did the following:

- configured each preset;
- removed every build output (the first attempt succeeded for each);
- rebuilt with warnings as errors under code page 65001;
- ran CTest after each successful build;
- repeated the related tests five times in Release and Debug.

The Git tree IDs it recorded equal the working tree's after the run.

| Preset | Configure | Clean | Build | TUs compiled | `warning` lines | Tests |
| --- | --- | --- | --- | --- | --- | --- |
| Debug | exit 0 | attempt 1 | exit 0 | 301 | 0 | **892/892 passed** (95.7 s) |
| Release | exit 0 | attempt 1 | exit 0 | 301 | 0 | **892/892 passed** (91.4 s) |
| Debug-shared | exit 0 | attempt 1 | exit 0 | 301 | 0 | **892/892 passed** (95.0 s) |

301 is `P12-DATUM-001`'s 295 translation units plus 6 new ones:
`occt/OcctFaceNames.cpp`, `reference/FaceReferences.cpp` and the four new
test files.

**Repeats.** The change touches every boolean, so the selection is broad:
`ctest -R "[Ff]ace|[Rr]eference|[Dd]atum|[Ss]ketch|[Pp]rofile|[Ee]xtru|[Bb]oolean|[Hh]ole|[Mm]irror|[Cc]ircular|[Ll]inear|[Pp]attern|[Rr]evol|[Ss]weep|[Ll]oft|Curved|[Pp]ropert|[Rr]egenerat|[Vv]alidat|[Dd]ocument|[Cc]ommand|[Ff]ile|cli|P9" --repeat until-fail:5`
selected 629 tests.

| Preset | Result | Time |
| --- | --- | --- |
| Release | 629/629, each 5 times (3145 passed runs) | 282.5 s |
| Debug | 629/629, each 5 times (3145 passed runs) | 295.4 s |

**Determinism.**

- `FaceNames_AreTheSameOnEveryBuild` builds a slotted block six times and
  gets the same names on the same faces in the same order.
- `FaceReference_RegeneratesDeterministically` checks two independent
  models: bit-identical volumes, equal placements and equal face names. A
  second pass rebuilds nothing. A full rebuild gives the same volume and
  placements.
- `FaceReferenceFile_SaveLoad_…` checks the round trip. It overwrites the
  loaded placements before regenerating, so the references must find the
  faces again. It gets bit-identical volumes and placements and serializes
  to identical text.
- The three process tests resolve the references in fresh processes.
- Across configurations (`values-determinism.txt`), the Debug, Release and
  Debug-shared executables printed identical values for the 20 cases (1305
  lines, MD5 `666d069ce958092b4eb74176d2175ad2`).

**Timing** (`timing-comparison.txt`, CTest's per-test times summed, `-j 8`,
same machine): P12-DATUM-001 → P12-STREF-001, with 23 more tests:

| Preset | P12-DATUM-001 | P12-STREF-001 |
| --- | --- | --- |
| Release | 578.0 s | 602.9 s |
| Debug | 582.4 s | 648.4 s |
| Debug-shared | 606.2 s | 578.5 s |

The slowest tests moved both ways by up to about 10 % between the runs
(for example `LinearPattern_DoesNotAccumulateTransformDrift` 19.05 → 16.19 s
in Release, `ReferenceModel_AllModelsStressRegression` 19.54 → 21.27 s). No
systematic change is claimed either way.

## Legacy Regression

`regression-comparison.txt` compares every log by test name with
`../P11-QUAL-001/release-ctest.log` (737 names), and
`regression-comparison-datum001.txt` with `../P12-DATUM-001/ctest-release.log`
(868 names):

- every baseline name is present and passed in the Debug, Release and
  Debug-shared logs, and no entry failed;
- the new names are the 23 new tests (154 against P11).

**Every value the existing tests measure is unchanged**
(`all-values-comparison.txt`):

- Before its clean rebuild, the qualified P12-DATUM-001 Release build (the
  `fe20b0f` tree) ran every test case with `-s`. So did this milestone's
  qualified Release build.
- After dropping the 20 new cases and normalizing temporary paths,
  document UUIDs and the reported Git revision, the 27659 measured-value
  lines of the 819 existing cases are identical (MD5
  `088b45f24c8e13374119962cb9339711` both).
- Names change no geometry: every volume, area, centre, bound and message
  is the same, bit for bit.

**The P0–P12-DATUM-001 regression suite remains green.** No existing test
was changed. Changes to existing production code:

- `References`: `FaceRole`, `FaceSelector`, `FaceName`,
  `PlaneReference::face`, `validate()`;
- `OcctBody`: names on bodies;
- `OcctBooleans`: names through the history;
- `OcctSweeps`: `makePrism()` with a namer, and the loops' edges in the
  region's order;
- `Faces`: names in `FaceInfo`, `findNamedFaces()`, `faceFrame()`;
- `Profiles`: `extractLabelledRegions()` (`extractRegions()` now uses it);
- `SolidSupport`, `ExtrudeRegeneration`: labelled regions and face names;
- `DatumResolution`, `Regenerator`: the body lookup;
- `Datums`, `Sketch`, `MirrorFeature`: reference checks;
- `Validation`, `DatumJson`, CLI `info`: face references.

## Known Limitations

- **Only extrudes name their faces.** `P12-SKETCH-003` extends the naming.
- **A name is resolved in its feature's own body.** Later features do not
  move or remove the reference, so a sketch stays on the plane of a face a
  later cut removed.
- **The sketch frame on a face is a rule, not the feature's frame.** Its
  origin is the plane's point nearest the model origin, its X axis a
  projected model axis (Faces.hpp). A face that turns through 45° to a model
  axis switches the projected axis.
- **Mirror planes, holes, chamfers and fillets** keep their geometric face
  and edge references (P11).
- **Transforms, blends, revolutions, sweeps and lofts** give bodies without
  names.

## Evidence Files

- `README.md`: this file.
- `qualify.cmd`, `run-qualification.cmd`: the qualification as run.
- `qualification-times.txt`: every step's start, exit code and time, the HEAD
  and the Git tree IDs of the qualified sources.
- `configure-*.log`, `clean-*.log`, `build-*.log`, `ctest-*.log`: the three
  presets.
- `ctest-repeat-{release,debug}.log`: the related tests, 5 times each.
- `regression-comparison.txt`, `regression-comparison-datum001.txt`,
  `compare-regression.py`: the legacy comparisons.
- `all-values-comparison.txt`: every measured value of the P12-DATUM-001
  tests, against the P12-DATUM-001 Release build.
- `reference-values-release.txt`, `values.py`, `values-header.txt`: the new
  tests' measured values; `values-determinism.txt`, `compare-values.py`: the
  same from all three configurations.
- `timing-comparison.txt`, `compare-times.py`: per-test times.
- `kernel-probe/face_history_probe.cpp`, `kernel-probe/face-history-probe.log`:
  the kernel's face history.

## Final Result

```text
TASK:            P12-STREF-001 Stable feature face references
IMPLEMENTATION:  FaceName/FaceSelector; names on bodies; prism naming;
                 names through booleans; labelled profile regions; extrude
                 face names; face plane resolution; face references in
                 sketch attachments and datums; validation, JSON, CLI
TESTS:           20 new test cases and 3 process tests; 892/892 in Debug,
                 Release, Debug-shared; 629 related tests x5 in Release and
                 Debug
VALIDATION:      face planes within 3.6e-15 mm of the parameters; model
                 volumes, centres and bounds within 4.1e-16 rel and 2.8e-14
                 mm at five heights; the named face chosen over a nearer
                 one; every existing measured value unchanged; values
                 identical across configurations
RESULT:          PASS
EVIDENCE:        docs/verification/P12-STREF-001/
TODO:            P12-STREF-001 deliverables ticked
```
