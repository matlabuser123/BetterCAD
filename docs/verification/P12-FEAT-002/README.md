# P12-FEAT-002 — Split Body / Combine Verification

## Status

**PASS.** Two body-level features:

- **Split.** Cuts another feature's body with a plane (any plane reference,
  a named face included) and keeps what lies in front of it, behind it, or
  both parts as separate solids of one body.
- **Combine.** Joins, cuts or intersects another feature's body with the
  bodies of one or more further features, and consumes them all: none of
  them remains a result body.

Both store only references and their choice, carry their inputs' face
names, and fail with structured diagnostics when the plane does not cross
the body or a combination leaves nothing.

Debug, Release and Debug-shared each passed **957/957** tests with **0
compiler warnings** from a verified clean rebuild. Every test of the P11
qualification and of `P12-FEAT-001` still passes in all three. Every value
the existing tests measure is unchanged, bit for bit.

Date: 2026-09-17. `main` was at `c976f9c` (`P12-FEAT-001`) before this
milestone. Every number below was measured in this session and is recorded
in this directory.

## Scope

The deliverables are the ones `TODO.md` lists for `P12-FEAT-002`.

| Deliverable | Status | Main evidence (test cases) |
| --- | --- | --- |
| `SplitFeature` (target, plane, keep front/back/both); both parts as one body of two solids | IMPLEMENTED | `Split_KeepsEachSideAsTheCutMoves`; `Split_ObliqueThroughTheCentreGivesCongruentHalves`; `Split_FollowsANamedFace`; `Split_KeepsTheSidesOfAPlane`; `GatherSolids_KeepsSolidsApart` |
| Planes that leave a side empty fail; names carried, the new faces unnamed | IMPLEMENTED | `Split_RefusesPlanesThatDoNotCrossTheBody`; `SplitCombine_FailuresAreStructuredAndAtomic`; `Split_KeepsTheSidesOfAPlane` |
| `CombineFeature` (target, tools, join/cut/intersect); all consumed; nothing left fails | IMPLEMENTED | `Combine_JoinsCutsAndIntersectsAsTheOverlapChanges`; `SplitCombine_FailuresAreStructuredAndAtomic` |
| Dependencies, validation, undo/redo, save/load, CLI | IMPLEMENTED | `SplitCombine_DefinitionsAreValidated`; `SplitCombine_CreationAndEditsAreUndoable`; `BodyOpsFile_*`; `BodyOpsCli_*`; `cli.info.body-ops`, `cli.validate.body-ops`, `cli.export-step.body-ops`; `SplitCombine_RegenerateDeterministically` |
| Evidence | this directory | — |

**Not in scope, and not implemented:**

- splitting by a surface or another body;
- picking one solid of a multi-solid body for later features;
- keeping a combine's tools as results;
- names for the faces a split creates on its plane.

## Design

```text
geometry::SplitKeep {Front, Back, Both}   Split.hpp
splitBody(body, plane, keep)              the body's intersection with (front) and difference from
                                          (back) a box on the plane's front side, enclosing the body's
                                          bounds by 1 mm; FailedPrecondition when a side is empty
gatherSolids(parts)                       a compound of the parts' solids, with their names (OcctSplit.cpp)
SplitFeature / SplitDefinition            target, plane (PlaneReference), keep; depends on the target and
                                          the plane's objects
CombineFeature / CombineDefinition        target, tools (in order), operation (join, cut, intersect);
                                          depends on and consumes them all
SolidFeature::consumedFeatures()          the target by default; a combine adds its tools;
                                          resultFeatures() uses it
regenerateSplit / regenerateCombine       in features/boolean/; registered through regenerateBodyFeature,
                                          which passes the regenerator's bodies
Validation                                a combine's tools must have bodies; a split plane is checked like
                                          any plane reference
BooleanJson                               "keep"; "tools"; "operation"
CLI info                                  "target Joined, plane Middle, keep both"; "join A with B, C"
Commands                                  Create/ModifySplitCommand, Create/ModifyCombineCommand
```

**Splitting.** The body's bounding box is projected on the plane's axes.
When all of it lies on one side (within 1e-7 mm), the split fails before any
kernel call. Otherwise a prism on the plane's front side, reaching 1 mm
beyond the box in every direction, is intersected with the body (the front
part) and subtracted from it (the back part). The booleans carry every name
of the body through the kernel's history (`P12-STREF-001`); the faces the
prism leaves on the plane have none. A side that is still empty (bounds
that cross the plane, a body that does not) fails. *Both* gathers the two
parts into one compound: a union would fuse them again.

**Combining.** The target's body is combined with each tool's body in turn,
with the name-carrying booleans. A tool without a body (a sketch, or a
failed feature) and a step that leaves nothing fail with FailedPrecondition.
A combine consumes its tools as well as its target, so result bodies
(`resultFeatures()`, and so exports and the CLI's validation) no longer
list them.

**The reference model** (`tests/support/BodyOpsModels.hpp`, saved as
`examples/models/body_ops.bcad`):

| Object | Definition |
| --- | --- |
| A | a 100 × 60 × `height` block |
| B | a 70 × 20 × 40 block at x 80..150, y 20..40 (overlapping A) |
| C | a cylinder r 10 × 30 about (40, 30) (overlapping A) |
| Joined | combine: A joined with B and C |
| Middle | datum plane x = `cut` |
| Halves | split of Joined by Middle, keeping both |

## Independent Validation

Expected values are written out in `BodyOpsModels.hpp` by
inclusion–exclusion:

- A ∩ B is the box [80, 100] × [20, 40] × [0, min(h, 40)];
- A ∩ C is the disc × [0, min(h, 30)];
- B and C do not meet.

So:

- the join is A + B + C − A∩B − A∩C;
- the cut is A − A∩B − A∩C;
- the intersection is A∩B.

For `cut` c in [50, 80], the front part is A beyond c with B, and the back
part is A before c with C. Centres are the parts' moments. The oblique split
is checked by symmetry: a plane through the block's centre gives two halves
of equal volume whose centres add up to twice the centre.

`deviations.txt` (`deviations.py` over `reference-values-release.txt`)
lists the largest deviations:

| Check | Tolerance | Checks | Largest deviation measured (Release) |
| --- | --- | --- | --- |
| Centres of mass (joins, cuts, intersections, split parts; the oblique halves' sum) | 1e-9 mm | 60 | 4.3e-14 mm |
| Bounds | 1e-7 mm | 75 | 2.8e-14 mm |
| Volumes and named faces' areas | 1e-12 rel | 36 | 7.6e-16 |
| Split parts at the geometry level: volumes, areas, bounds | 1e-12 rel, 1e-9 mm | 12 | 0 |
| STEP read-back volume and bounds | 1e-9 rel | 3 | 1.0e-15 |

**Following the parameters.**

- In `Combine_JoinsCutsAndIntersectsAsTheOverlapChanges`, `height` goes
  20 → 30 → 45 mm. The overlap with B and C grows, then covers them. The
  join, cut and intersection match at each step. Undo restores every volume
  bit for bit.
- The joined body carries A's top. At 20 mm it lacks B's and C's
  footprints (6000 − 400 − 100π mm²); at 45 mm A rises above both and the
  top is whole (6000).
- In `Split_KeepsEachSideAsTheCutMoves`, `cut` goes 60 → 70 and `height`
  20 → 25. Only Middle and Halves are rebuilt when `cut` changes. At each
  step the split is checked three ways:
  - kept both: 2 solids, the whole join;
  - kept front: A beyond c with B, and A's top (100 − c) · 60 − 400;
  - kept back: A before c with C, and A's top c · 60 − 100π.
- In `Split_FollowsANamedFace`, a tower is split at the height of another
  feature's top face (`ledge` 12 → 20 → 35): the kept part is 600 z,
  centred at z/2.

## Diagnostics

Asserted verbatim by the tests:

| Case | Code | Message |
| --- | --- | --- |
| no target | InvalidArgument | `a split needs a target feature`; `a combine needs a target feature` |
| a bad plane reference | InvalidArgument | `the split plane: a plane reference must name a valid object` (and the face reference's own messages) |
| an unknown side | InvalidArgument | `a split keeps the front, the back or both` |
| no tools, an invalid, repeated or self tool | InvalidArgument | `a combine needs one or more tool features`; `tool 2 must be a valid feature`; `tool 3 repeats an earlier tool`; `tool 2 is the target: a body is not combined with itself` |
| new body | InvalidArgument | `a combine joins, cuts or intersects bodies, got new body` |
| a plane beyond the body | FailedPrecondition | `Halves: split: the plane does not cross the body: all of it lies behind the plane` (and `in front of the plane`) |
| bounds crossed, body not | FailedPrecondition | `Cornered: split: the plane does not cross the body: nothing of it lies in front of the plane` (an L-shaped body and a diagonal plane past its inner corner) |
| nothing left | FailedPrecondition | `Erase: nothing is left after cutting Big (object:13)`; `Apart: nothing is left after intersecting with Far (object:15)` |
| a tool without a body | FailedPrecondition | `WithSketch: tool 1, CSketch (object:7), has no body` |
| a split plane that is not a plane | (resolution) | `Halves: the split plane: A (object:4) is an extrude, not a datum plane or a coordinate system` |
| validation | error | `WithSketch (object:12): tool 1 is CSketch (object:7), which is a sketch, not a feature with a body`; `Halves (object:11): the split plane is A (object:4), which is an extrude, not a datum plane or a coordinate system` |
| a pattern of a split | FailedPrecondition | `… a linear pattern cannot repeat a split` |
| a sketch on a split's face | InvalidArgument | `OnSplit (object:13): Halves (object:11) is a split, whose faces are not named (…)` |
| an invalid edit | InvalidArgument | refused by `ModifyCombineCommand`; the document is unchanged |
| file | — | `….keep: unknown value 'middle'`, `….side: unknown field`, `….data: the split plane: a plane reference must name a valid object`, `….data: a split needs a target feature`, `….operation: unknown value 'new_body'`, `….data: a combine needs one or more tool features`, `….data: tool 2 repeats an earlier tool`, `….tools[1]: expected an ID`, `….tools: expected an array` |
| CLI `validate` | failure | the regeneration message, `Result: invalid` |

A failed split or combine keeps no body. The rest of the model is built.

## Tests

18 new Catch2 test cases, tagged `[split]` or `[combine]` with `[p12]`, and
3 process tests:

- `tests/core/geometry/SplitTests.cpp`: 3 cases (the sides of a plane,
  refused planes, gathered solids).
- `tests/features/BodyOpsTests.cpp`: 8 cases. Validation; combine;
  split; oblique split; split on a face; failures; undo/redo;
  determinism.
- `tests/io/BodyOpsFileTests.cpp`: 5 cases. Round trip, the written form,
  malformed files, the example file, STEP (one body of two solids).
- `tests/cli/BodyOpsCliTests.cpp`: 2 cases (`info`, `validate`).
- `cli.info.body-ops`, `cli.validate.body-ops` and `cli.export-step.body-ops`
  run on `examples/models/body_ops.bcad`, each in a fresh process.

No existing test changed. The Release run of the 18 cases records
**1385 passed assertions and 0 failed**
(`reference-values-release.txt`).

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
| Debug | exit 0 | attempt 1 | exit 0 | 316 | 0 | **957/957 passed** (94.8 s) |
| Release | exit 0 | attempt 1 | exit 0 | 316 | 0 | **957/957 passed** (90.1 s) |
| Debug-shared | exit 0 | attempt 1 | exit 0 | 316 | 0 | **957/957 passed** (101.6 s) |

316 is `P12-FEAT-001`'s 306 translation units plus 10 new ones:

- `Split.cpp`, `occt/OcctSplit.cpp`;
- `boolean/SplitFeature.cpp`, `boolean/CombineFeature.cpp`,
  `boolean/BooleanRegeneration.cpp`;
- `json/BooleanJson.cpp`;
- the four new test files.

**Repeats.**
`ctest -R "[Ss]plit|[Cc]ombine|BodyOps|GatherSolids|[Rr]esult|[Ee]xtru|ThroughAll|[Pp]attern|[Mm]irror|[Cc]ircular|[Ll]inear|[Ff]ace|[Rr]eference|[Dd]atum|[Bb]oolean|[Pp]ropert|[Rr]egenerat|[Vv]alidat|[Dd]ocument|[Ff]ile|[Cc]ommand|[Ee]xport|cli|body-ops|through-slab|post-row|P9" --repeat until-fail:5`
selected 553 tests.

| Preset | Result | Time |
| --- | --- | --- |
| Release | 553/553, each 5 times (2765 passed runs) | 279.0 s |
| Debug | 553/553, each 5 times (2765 passed runs) | 284.2 s |

**Determinism.**

- `SplitCombine_RegenerateDeterministically` builds the model twice and
  gets bit-identical volumes, equal face names and equivalent documents. A
  second pass rebuilds nothing, and a full rebuild gives the same bits.
- The file round trip regenerates both bodies to the same bits and
  serializes to identical text.
- The three process tests work in fresh processes.
- Across configurations (`values-determinism.txt`), the Debug, Release and
  Debug-shared executables printed identical values for the 18 cases
  (633 lines, MD5 `be0f7df1adab4b81a92fa4f3972fb866`).

**Timing** (`timing-comparison.txt`: CTest's per-test times summed, `-j 8`,
same machine), P12-FEAT-001 → P12-FEAT-002, with 21 more tests:

| Preset | P12-FEAT-001 | P12-FEAT-002 |
| --- | --- | --- |
| Release | 652.4 s | 624.1 s |
| Debug | 625.2 s | 695.6 s |
| Debug-shared | 636.0 s | 679.4 s |

The 21 new tests take 8.0 s in Release. The largest move among the twelve
slowest tests was 0.5 s in Release, 1.8 s in Debug and 2.4 s in
Debug-shared. The totals moved by up to 70 s in both directions between
presets. The same presets moved as much between the last two milestones
(Debug 688.6 → 625.2 s), so no speed change is claimed.

## Legacy Regression

`regression-comparison.txt` compares every log by test name with
`../P11-QUAL-001/release-ctest.log` (737 names), and
`regression-comparison-feat001.txt` with `../P12-FEAT-001/ctest-release.log`
(935 names):

- every baseline name is present and passed in the Debug, Release and
  Debug-shared logs, and no entry failed;
- the new names are this milestone's 21 tests (219 against P11).

**Every value the existing tests measure is unchanged**
(`all-values-comparison.txt`):

- Before its clean rebuild, the qualified P12-FEAT-001 Release build (the
  `c976f9c` tree) ran every test case with `-s`. So did this milestone's
  qualified Release build.
- After dropping the 18 new cases (no existing case's name matches the
  pattern) and normalizing temporary paths, document UUIDs and the reported
  Git revision (line numbers kept: no existing test file changed), the
  33803 measured-value lines of the 877 existing cases are identical (MD5
  `c693a23c98ef8ed1b52364f05f07526d` both).

**The P0–P12-FEAT-001 regression suite remains green.** No existing test
changed. Changes to existing production code:

- `Feature`: `SolidFeature::consumedFeatures()`;
- `ResultBodies`: uses it;
- `Regenerator`: the split and combine handlers;
- `Validation`: combine tools and split planes;
- `FeatureCommands`: the command aliases;
- `DocumentJson`, `ObjectJson`: the two types;
- CLI `info`: their descriptions.

## Known Limitations

- **Planes only.** A split cuts with a plane; there is no split by a
  surface or a body.
- **A both-sides split is one body of two solids.** Later features take the
  whole body; there is no way to pick one of its solids.
- **A combine consumes its tools.** There is no option to keep them as
  results.
- **The faces a split makes are unnamed.** A sketch cannot be placed on the
  cut face, and references to a split's or combine's own faces are refused
  (their inputs' faces keep their names).
- **Splits and combines cannot be repeated** by patterns or feature
  mirrors. A body mirror of one works, as for any body.

## Evidence Files

- `README.md`: this file.
- `qualify.cmd`, `run-qualification.cmd`: the qualification as run.
- `qualification-times.txt`: every step's start, exit code and time, the HEAD
  and the Git tree IDs of the qualified sources.
- `configure-*.log`, `clean-*.log`, `build-*.log`, `ctest-*.log`: the three
  presets.
- `ctest-repeat-{release,debug}.log`: the related tests, 5 times each.
- `regression-comparison.txt`, `regression-comparison-feat001.txt`,
  `compare-regression.py`: the legacy comparisons.
- `all-values-comparison.txt`: every measured value of the P12-FEAT-001
  tests, against the P12-FEAT-001 Release build.
- `reference-values-release.txt`, `values.py`, `values-header.txt`: the new
  tests' measured values.
- `values-determinism.txt`, `compare-values.py`: the same from all three
  configurations.
- `deviations.txt`, `deviations.py`: the largest deviations.
- `timing-comparison.txt`, `compare-times.py`: per-test times.

## Final Result

```text
TASK:            P12-FEAT-002 Split body / combine
IMPLEMENTATION:  splitBody()/gatherSolids(); SplitFeature (target, plane
                 reference, keep front/back/both); CombineFeature (target,
                 tools, join/cut/intersect) consuming all its inputs;
                 consumedFeatures(); validation, JSON, CLI, commands
TESTS:           18 new test cases and 3 process tests; 957/957 in Debug,
                 Release, Debug-shared; 553 related tests x5 in Release and
                 Debug
VALIDATION:      joins, cuts, intersections and split parts match
                 inclusion-exclusion within 7.6e-16 rel (volume), 4.3e-14 mm
                 (centre), 2.8e-14 mm (bounds) as height and cut change;
                 oblique halves congruent; every existing measured value
                 unchanged; values identical across configurations
RESULT:          PASS
EVIDENCE:        docs/verification/P12-FEAT-002/
TODO:            P12-FEAT-002 deliverables ticked
```
