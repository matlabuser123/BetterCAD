# P12-FEAT-003 — Shell Verification

## Status

**PASS.** A shell feature hollows another feature's body into walls of a
given thickness, inward or outward, and opens it where named faces are
removed. The open faces are face names (`P12-STREF-001`) looked up in the
target's body, never positions or geometry. The kernel's result is checked,
not trusted: a kernel probe found that OCCT 8.0.1 reports success for walls
that are too thick, and the shell refuses those results.

Debug, Release and Debug-shared each passed **978/978** tests with
**0 compiler warnings** from a verified clean rebuild. Every test of the P11
qualification and of `P12-FEAT-002` still passes in all three. Every value
the existing tests measure is unchanged, bit for bit.

Date: 2026-09-17. `main` was at `77f1792` (`P12-FEAT-002`) before this
milestone. Every number below was measured in this session and is recorded
in this directory.

## Scope

The deliverables are the ones `TODO.md` lists for `P12-FEAT-003`.

| Deliverable | Status | Main evidence (test cases) |
| --- | --- | --- |
| `geometry::shellBody` (open faces by name, thickness, side) with structured failures when the kernel's result is not a shell | IMPLEMENTED | `Shell_HollowsABoxAndCarriesItsNames`; `Shell_OpensEveryFaceANameIsOn`; `Shell_FailuresAreStructured`; `Shell_RequestsAreValidated` |
| `ShellFeature` (target, open faces, driven thickness, inward/outward); consumes its target; names carried, rims keep the open faces' names, walls unnamed | IMPLEMENTED | `ShellFeature_BlockFollowsItsWallAndHeight`; `ShellFeature_TurnedAndCurvedBodies`; `ShellFeature_ConcaveDrilledAndRoundedBodies` |
| Open faces resolved by name in the target's body; NotFound and `checkFaceName` refusals | IMPLEMENTED | `ShellFeature_FailuresAreStructuredAndAtomic` |
| Dependencies, validation, undo/redo, save/load, CLI | IMPLEMENTED | `ShellFeature_DefinitionsAreValidated`; `ShellFeature_CreationAndEditsAreUndoable`; `ShellFeature_RegeneratesDeterministically`; `ShellFile_*`; `ShellCli_*`; `cli.info.shelled-block`, `cli.validate.shelled-block`, `cli.export-step.shelled-block` |
| Evidence | this directory | — |

**Not in scope, and not implemented:**

- closed hollows (a shell with no open face);
- different thicknesses on different faces;
- rounded wall corners;
- walls that thin or merge where the body is thinner than two walls;
- names for the walls.

## Kernel Probe

Before the design, two probes (`kernel-probe/`, built against the
dependency prefix as their headers say) ran
`BRepOffsetAPI_MakeThickSolid::MakeThickSolidByJoin` on bodies with analytic
shells:

- `shell_probe.cpp` (`shell-probe.log`): single cases and the kernel's
  history.
- `join_probe.cpp` (`join-probe.log`): 16 bodies, each inward and outward,
  with both join types (64 runs).

What they showed:

- **Walls too thick for the body pass as successes.** A 100 × 60 × 40 box,
  top removed:
  - with 35 mm walls (more than half of 60) the kernel reports success and
    no error, and returns the box unchanged. None of the five remaining
    faces has generated an offset face.
  - with 30 mm walls it reports success and returns an invalid solid of the
    box's volume.
  - with 29 mm walls the result is exact (239076 mm³).
- **The rounded join is not reliable.** Outward, the `GeomAbs_Arc` join
  gives an invalid solid (reported as a success) for an L-shaped prism,
  whose one concave edge the arcs cannot follow.
- **The intersection join is.** `GeomAbs_Intersection` extends offset faces
  until they meet. Of its 32 runs:
  - **17 have an analytic volume in the probe.** All 17 were single valid
    solids that passed the history check below, and matched within 1.0e-15
    relative. They cover boxes, a tube, a box open at two sides, the L
    prism, a cylinder, a drilled block, a block with rounded edges and a
    stepped revolved shaft, inward and outward.
  - **7 more were valid, with no probe value.** Checked by hand as the grown
    outlines, 5 of them match: the box outward with 29, 30 and 35 mm walls
    (1046436, 1104000, 1417500 mm³), the dumbbell outward (80500) and the U
    channel outward (46308). The other 2, the rounded blocks outward with
    r 5 and r 3, are checked by the feature tests.
  - **The other 8 are ones BetterCAD refuses.** Four were not done or
    threw: rounds of 5 and 3 mm inward, and the dumbbell and the U channel
    inward. Two were invalid solids reported as successes: 30 mm inward,
    and the U channel outward with its arm's side open. Two left remaining
    faces without walls: 35 mm inward, and the U channel inward with its
    arm's side open.
- **The history names what happened.** A remaining face is kept, and its
  offset is `Generated` from it. The removed face is `Modified` into the
  rim around the opening (1500 mm² inward, 1700 mm² outward).
- **Bodies too thin in places fail, or are caught.** A dumbbell whose
  8 mm bridge is narrower than two 5 mm walls fails in the kernel (not
  done). A U channel with a 4 mm arm is not done, or the history shows faces
  without walls. Rounds no larger than an inward wall fail (not done, or an
  exception).
- A closed hollow is not what the call builds: with no closing faces it
  returns the offset solid alone (6 faces, the cavity's volume).
- No crash occurred in any run.

## Design

```text
geometry::ShellSide {Inward, Outward}     Shell.hpp
ShellRequest                               open faces (FaceName), thickness, side
validate(request)                          one or more open faces, valid and not repeated; positive finite
                                           thickness; a known side (Shell.cpp)
shellBody(body, request)                   OcctShell.cpp: one solid; the faces each name is on (NotFound
                                           if none); MakeThickSolidByJoin(-t or +t, GeomAbs_Intersection);
                                           the result checked; names carried through the history
ShellFeature / ShellDefinition             target, openFaces, thickness or thicknessParameter, side;
                                           depends on the target, the parameter and the faces' features
                                           and copies; consumes the target
regenerateShell                            features/shell/: thickness; each open face through
                                           checkFaceName() and findNamedFaces() in the target's body;
                                           then shellBody()
Validation                                 the thickness parameter is a length; each open face passes
                                           checkFaceName() (shared with plane references)
ShellJson                                  "open_faces": [{"feature": id, "face": {...}}], "thickness",
                                           "thickness_parameter", "side"; the face selector's JSON is shared
                                           with plane references (DatumJson.cpp), unchanged
CLI info                                   "target Block, open the end cap of Block and the start cap of
                                           Block, thickness wall, inward"
Commands                                   Create/ModifyShellCommand
```

**Checking the kernel.** A shell is accepted only when all of these hold:

1. the kernel is done;
2. every face of the solid that is not open is kept and has generated a
   face of the result (its wall);
3. no open face is left;
4. the result is one valid solid with finite positive volume and area;
5. an inward shell has less volume than the body.

Otherwise the result is FailedPrecondition, naming the walls and the
reason. Kernel exceptions are reported the same way.

**Names.** The kernel's history carries the body's face names
(`carriedNames()`). A remaining face keeps its names. An open face's names
go to the rim the kernel makes of it. The walls get none.

**Where names are looked up.** A sketch or datum resolves a face name in
the body of the feature that generates it. A shell looks it up in its
*target's* body, where the features in between have carried it, because it
removes that face of that body. A face a later feature removed (e.g. a
split below it) cannot be opened, and fails with NotFound.

**The reference models** (`tests/support/ShellModels.hpp`; the first is
saved as `examples/models/shelled_block.bcad`):

| Model | Shells |
| --- | --- |
| ShelledBlock: a 100 × 60 × `height` block | Cup (top open, inward), Tube (both ends open, inward), Casing (top open, outward), all `wall` thick |
| TurnedShell: a cylinder r 20 × 50; a revolved shaft, r 30 for y 0..20 and r 15 for y 20..60 | CanCup/CanCase (top open); ShaftBore/ShaftSleeve (the small end, a side named by its profile line, open), `wall` thick |
| ConcaveShell: an L prism; a `height` plate with a through hole d 20 drilled from its bottom; a `height` pad with its vertical edges rounded by `round` | inward and outward shells of each, top open, `wall` thick |

## Independent Validation

Expected values are written out in `ShellModels.hpp` as sums and
differences of boxes and cylinders, and of prisms over rounded rectangles
(area w·d − (4 − π) r²). With sharp corners:

- **Inward**, the cavity is the body's section shrunk by the wall, from the
  wall above the floor to the opening. Rounds shrink by the wall, and a
  hole's wall becomes a tube of radius r + t.
- **Outward**, the outside is the section grown by the wall, from the wall
  below the floor to the opening. Rounds grow by it, and holes narrow to
  r − t.
- The L's cavity is its two arms shrunk, less their overlap. The shaft's
  cavity is r 30 − t for y in [t, 20 − t] and r 15 − t above. The
  dumbbell's cavity is two chambers joined by a 2 mm passage.

Centres are the parts' moments. Rim areas are the open face less the
cavity's mouth (inward), or the grown outline less the open face (outward).
Bounds follow the section.

The probe's values recur as fixed checks:

- 101500 and 129500 mm³ for the L;
- 82500 + 3875π and 106500 + 2875π mm³ for the drilled plate;
- 10602π and 13302π mm³ for the shaft at 3 mm;
- 36972 mm³ for the dumbbell at 3 mm.

`deviations.txt` (`deviations.py` over `reference-values-release.txt`)
lists the largest deviations:

| Check | Tolerance | Checks | Largest deviation measured (Release) |
| --- | --- | --- | --- |
| Centres of mass (the block's, can's, shaft's, L's, plate's, pad's and dumbbell's shells) | 1e-9 mm | 108 | 1.1e-13 mm |
| Bounds | 1e-7 mm | 147 | 5.7e-14 mm |
| Volumes and named faces' areas | 1e-12 rel | 99 | 1.5e-15 |
| Geometry level: volumes and rim areas; bounds | 1e-12 rel; 1e-9 mm | 7; 11 | 5.4e-16; 0 |
| STEP read-back volume and bounds | 1e-9 rel | 4 | 4.7e-16 |

**Following the parameters.**

- In `ShellFeature_BlockFollowsItsWallAndHeight`:
  - `wall` goes 5 → 2 mm, `height` 40 → 60 mm, then `wall` 2 → 2.5 mm.
    The three shells match at each step.
  - When the wall changes, only the shells are rebuilt. When the block
    grows, its top moves, and the shells open it where it now is.
  - Undo restores every volume bit for bit, and so does a fresh
    regeneration of a copy.
  - At each step, the rims carry the caps' names (inward
    6000 − (100 − 2t)(60 − 2t), outward (100 + 2t)(60 + 2t) − 6000). The
    four sides carry theirs whole. The five (tube: four) walls have none.
- In `ShellFeature_TurnedAndCurvedBodies`, `wall` goes 3 → 5 mm. The can
  and the shaft match inward and outward. The small end's rim keeps its
  side name; the big end and both cylinders keep theirs whole.
- In `ShellFeature_ConcaveDrilledAndRoundedBodies`, `wall` 5 → 3, `height`
  40 → 55 and `round` 10 → 8 mm. All six shells match. Undo restores every
  volume bit for bit.

## Diagnostics

Asserted verbatim by the tests (the kernel's own text only by its prefix):

| Case | Code | Message |
| --- | --- | --- |
| no target, no open face, a repeated or invalid face, a bad thickness or side | InvalidArgument | `a shell needs a target feature`; `a shell needs one or more open faces (a closed hollow is not built)`; `open face 2 repeats an earlier open face`; `open face 1: an end cap is not named by an entity`; `open face 2 must name a valid feature`; `the shell thickness must be positive and finite, got 0 mm` (`nan mm`, `inf mm`, `-2 mm`); `the thickness parameter ID must be valid`; `a shell's walls lie inward or outward` |
| walls overlapping across the body (35 mm) | FailedPrecondition | `Cup: shell: the kernel cannot build walls 35 mm thick inward on this body: its result is not a shell of the body: 5 of the 5 remaining faces have no wall; walls thicker than half the body where it is thinnest, or an inward wall at least as thick as a round it follows, cannot be built` |
| the cavity closing (30 mm) | FailedPrecondition | `Cup: shell: … 30 mm thick inward …: its result is not a shell of the body: it is not a valid solid; …` |
| walls meeting in a thin bridge; rounds smaller than the wall | FailedPrecondition | `Hollow: shell: … 5 mm thick inward …: kernel: unknown error; …`; `RoundCup: shell: … kernel: unknown error; …` |
| a round as large as the wall | FailedPrecondition | `RoundCup: shell: … kernel:` followed by the kernel's exception |
| a face of another body; a face a split removed | NotFound | `WrongBody: open face 1, the end cap of Boss (object:4), is not a face of the body of Block (object:2)`; `CutAway: open face 1, the end cap of Block (object:2), is not a face of the body of Base (object:6)` |
| a face of a split; of a sketch | InvalidArgument | `Unnamed: open face 1: Base (object:6) is a split, whose faces are not named (…)`; `OnSketch: open face 2: BlockSketch (object:1) is a sketch, not a feature, and has no faces` |
| a body of two solids | FailedPrecondition | `TwoSolids: shell: a shell hollows one solid; the body has 2` |
| a negative driven wall; an angle parameter | InvalidArgument; DimensionMismatch | `Cup: shell: the shell thickness must be positive and finite, got -2 mm`; `Cup: …` |
| geometry level | FailedPrecondition; NotFound | `shell: the body is empty`; `shell: open face 2 is not a face of the body`; `shell: a shell hollows one solid; the body has 2` |
| validation | error | `Unnamed (object:11): open face 1 is the end cap of Base (object:6): Base (object:6) is a split, …`; `Cup (object:5): the thickness is driven by tilt (object:8), which is an angle, not a length`; `NoBody (object:10): the target is BlockSketch (object:3), which is a sketch, not a feature with a body` |
| a pattern of a shell; a sketch on a shell's face; no target body | — | `… a linear pattern cannot repeat a shell`; `OnCup (object:9): Cup (object:5) is a shell, whose faces are not named (…)`; `NoBody: a shell needs the body of its target feature` |
| an invalid edit | InvalidArgument | refused by `ModifyShellCommand`; the document is unchanged |
| file | — | `….side: unknown value 'inside'`, `….depth: unknown field`, `….open_faces: missing required field`, `….open_faces: expected an array`, `….open_faces[0].face: missing required field`, `….open_faces[0].feature: missing required field`, `….open_faces[0].face.role: unknown value 'top'`, `….open_faces[0]: a side face is named by a valid profile entity`, `….open_faces[0].name: unknown field`, `….data: open face 1 must name a valid feature`, `….data: open face 2 repeats an earlier open face`, `….data: a shell needs a target feature`, `….data: the shell thickness must be positive and finite, got 0 mm`, `….thickness: expected a number` |
| CLI `validate` | failure | the regeneration message, `Result: invalid` |

A failed shell keeps no body; outward shells of the same body, and the
rest of the model, are built.

## Tests

18 new Catch2 test cases, tagged `[shell]` with `[p12]`, and 3 process
tests:

- `tests/core/geometry/ShellTests.cpp`: 4 cases. Requests; a box inward and
  outward with its names; a name on two faces; failures.
- `tests/features/ShellFeatureTests.cpp`: 7 cases. Validation; the block;
  turned and curved bodies; concave, drilled and rounded bodies; failures;
  determinism; undo/redo.
- `tests/io/ShellFileTests.cpp`: 5 cases. Round trips of the three models,
  the written form, malformed files, the example file, STEP (three
  bodies).
- `tests/cli/ShellCliTests.cpp`: 2 cases (`info`, `validate`).
- `cli.info.shelled-block`, `cli.validate.shelled-block` and
  `cli.export-step.shelled-block` run on `examples/models/shelled_block.bcad`,
  each in a fresh process.

No existing test changed. The Release run of the 18 cases records
**2260 passed assertions and 0 failed**
(`reference-values-release.txt`).

## Qualification

`qualify.cmd` was run through `run-qualification.cmd` and did the following:

- configured each preset;
- removed every build output;
- rebuilt with warnings as errors under code page 65001;
- ran CTest after each successful build;
- repeated the related tests five times in Release and Debug.

A first run was stopped during its Debug build to add
`ShellFeature_RegeneratesDeterministically`. The run recorded here started
afresh, with the test in the tree. The Git tree IDs it recorded equal the
working tree's after the run.

| Preset | Configure | Clean | Build | TUs compiled | `warning` lines | Tests |
| --- | --- | --- | --- | --- | --- | --- |
| Debug | exit 0 | attempt 1 | exit 0 | 325 | 0 | **978/978 passed** (101.5 s) |
| Release | exit 0 | attempt 1 | exit 0 | 325 | 0 | **978/978 passed** (92.8 s) |
| Debug-shared | exit 0 | attempt 1 | exit 0 | 325 | 0 | **978/978 passed** (106.4 s) |

325 is `P12-FEAT-002`'s 316 translation units plus 9 new ones:

- `Shell.cpp`, `occt/OcctShell.cpp`;
- `shell/ShellFeature.cpp`, `shell/ShellRegeneration.cpp`;
- `json/ShellJson.cpp`;
- the four new test files.

**Repeats.**
`ctest -R "[Ss]hell|[Ss]plit|[Cc]ombine|BodyOps|[Ff]illet|[Hh]ole|[Rr]evolve|[Ee]xtru|ThroughAll|[Rr]esult|[Pp]attern|[Mm]irror|[Ff]ace|[Rr]eference|[Dd]atum|[Bb]oolean|[Pp]ropert|[Rr]egenerat|[Vv]alidat|[Dd]ocument|[Ff]ile|[Cc]ommand|[Ee]xport|cli|shelled-block|body-ops|P9" --repeat until-fail:5`
selected 631 tests.

| Preset | Result | Time |
| --- | --- | --- |
| Release | 631/631, each 5 times (3155 passed runs) | 301.2 s |
| Debug | 631/631, each 5 times (3155 passed runs) | 312.6 s |

**Determinism.**

- `ShellFeature_RegeneratesDeterministically` builds the concave model
  twice and gets bit-identical volumes, equal face names and topology, and
  equivalent documents. A second pass rebuilds nothing, and a full rebuild
  gives the same bits.
- The file round trips of all three models regenerate every shell to the
  same bits and serialize to identical text.
- The three process tests work in fresh processes.
- Across configurations (`values-determinism.txt`), the Debug, Release and
  Debug-shared executables printed identical values for the 18 cases
  (1182 lines, MD5 `7cef5c5c316fb76967b2edf8f469651c`).

**Timing** (`timing-comparison.txt`: CTest's per-test times summed, `-j 8`,
same machine), P12-FEAT-002 → P12-FEAT-003, with 21 more tests:

| Preset | P12-FEAT-002 | P12-FEAT-003 |
| --- | --- | --- |
| Release | 624.1 s | 642.8 s |
| Debug | 695.6 s | 710.3 s |
| Debug-shared | 679.4 s | 681.3 s |

The 21 new tests take 13.2 s in Release, 15.0 s in Debug and 16.4 s in
Debug-shared. The totals grew by less than that (18.8, 14.7 and 1.9 s). The
largest move among the twelve slowest tests was 1.7 s in Release, 2.7 s in
Debug (a compile-fail test) and 1.4 s in Debug-shared. No speed change is
claimed.

## Legacy Regression

`regression-comparison.txt` compares every log by test name with
`../P11-QUAL-001/release-ctest.log` (737 names), and
`regression-comparison-feat002.txt` with `../P12-FEAT-002/ctest-release.log`
(957 names):

- every baseline name is present and passed in the Debug, Release and
  Debug-shared logs, and no entry failed;
- the new names are this milestone's 21 tests (240 against P11).

**Every value the existing tests measure is unchanged**
(`all-values-comparison.txt`):

- Before its clean rebuild, the qualified P12-FEAT-002 Release build (the
  `77f1792` tree) ran every test case with `-s`. So did this milestone's
  qualified Release build.
- After dropping the 18 new cases (no existing case's name matches the
  pattern) and normalizing temporary paths, document UUIDs and the reported
  Git revision (line numbers kept: no existing test file changed), the
  34429 measured-value lines of the 895 existing cases are identical (MD5
  `c1c733d7b833a31f6d45ca93a636109b` both).

**The P0–P12-FEAT-002 regression suite remains green.** No existing test
changed. Changes to existing production code:

- `Regenerator`: the shell handler;
- `Validation`: the thickness parameter and the open faces; the face check
  of plane references moved into a shared helper, unchanged;
- `FeatureCommands`, `Regeneration.hpp`: the command aliases and the
  regeneration functions;
- `DatumJson`: the face selector's JSON in shared helpers, used unchanged by
  plane references, and face names;
- `DocumentJson`, `ObjectJson`: the type;
- CLI `info`: the description; the face text of plane references moved into
  a shared helper, unchanged.

## Known Limitations

- **Closed hollows are not built.** A shell needs at least one open face.
- **One thickness.** Every wall has the same thickness.
- **Sharp corners.** Where offset faces part at an edge they are extended
  until they meet, so the wall is thicker across such corners. Rounded
  joins were not reliable in the kernel.
- **No thinning or merging.** Walls that would meet where the body is
  thinner than two walls, and inward walls at least as thick as a round they
  follow, are refused. They are not thinned, merged or left solid.
- **One solid.** A body of several solids (e.g. a both-sides split) is
  refused.
- **The walls are not named.** A sketch on a cavity floor needs a datum
  plane offset from a named face. Shells cannot be repeated by patterns or
  feature mirrors.

## Evidence Files

- `README.md`: this file.
- `kernel-probe/`: `shell_probe.cpp` and `join_probe.cpp`, with their
  output (`shell-probe.log`, `join-probe.log`).
- `qualify.cmd`, `run-qualification.cmd`: the qualification as run.
- `qualification-times.txt`: every step's start, exit code and time, the HEAD
  and the Git tree IDs of the qualified sources.
- `configure-*.log`, `clean-*.log`, `build-*.log`, `ctest-*.log`: the three
  presets.
- `ctest-repeat-{release,debug}.log`: the related tests, 5 times each.
- `regression-comparison.txt`, `regression-comparison-feat002.txt`,
  `compare-regression.py`: the legacy comparisons.
- `all-values-comparison.txt`: every measured value of the P12-FEAT-002
  tests, against the P12-FEAT-002 Release build.
- `reference-values-release.txt`, `values.py`, `values-header.txt`: the new
  tests' measured values.
- `values-determinism.txt`, `compare-values.py`: the same from all three
  configurations.
- `deviations.txt`, `deviations.py`: the largest deviations.
- `timing-comparison.txt`, `compare-times.py`: per-test times.

## Final Result

```text
TASK:            P12-FEAT-003 Shell
IMPLEMENTATION:  shellBody() (sharp-cornered offset walls, the kernel's
                 result checked); ShellFeature (target, named open faces,
                 driven thickness, inward/outward) consuming its target;
                 validation, JSON, CLI, commands
TESTS:           18 new test cases and 3 process tests; 978/978 in
                 Debug, Release, Debug-shared; 631 related tests x5 in
                 Release and Debug
VALIDATION:      shells of blocks, a tube, a cylinder, an L prism, a
                 drilled plate, a rounded pad, a revolved shaft and a
                 dumbbell match sums of boxes and cylinders within 1.5e-15
                 rel (volume), 1.1e-13 mm (centre), 5.7e-14 mm (bounds),
                 inward and outward, as wall, height and round change; walls
                 too thick refused; every existing measured value
                 unchanged; values identical across configurations
RESULT:          PASS
EVIDENCE:        docs/verification/P12-FEAT-003/
TODO:            P12-FEAT-003 deliverables ticked
```
