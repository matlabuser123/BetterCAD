# P12-FEAT-001 — Through-All Extrude Verification

## Status

**PASS.** An extrude can end *through all* the material of its target. The
termination is stored as part of the definition (`termination =
through_all`), never as a large depth. Regeneration works out the tool's
length from the target body in the sketch's direction: along its normal,
against it, or both ways. The cut therefore goes through at every
thickness.

- Patterns and mirrors rebuild the tool at each instance, so a copy turned
  onto a thicker wall still cuts through it.
- A through-all extrude takes no depth and cuts; anything else is refused.
- A target wholly on the side the cut does not go to fails with a
  structured diagnostic, keeping no body.

Debug, Release and Debug-shared each passed **936/936** tests with **0
compiler warnings** from a verified clean rebuild. Every test of the P11
qualification and of `P12-SKETCH-003` still passes in all three. Every value
the existing tests measure is unchanged, bit for bit.

Date: 2026-09-17. `main` was at `650acd6` (`P12-SKETCH-003`) before this
milestone. Every number below was measured in this session and is recorded
in this directory.

## Scope

The deliverables are the ones `TODO.md` lists for `P12-FEAT-001`.

| Deliverable | Status | Main evidence (test cases) |
| --- | --- | --- |
| `ExtrudeTermination` (`blind`, `through_all`); no depth, a cut only | IMPLEMENTED | `ThroughAllExtrude_DefinitionIsValidated`; `ThroughAllExtrude_EditsAreUndoableAndAtomic` |
| Through all along the normal, reversed and symmetric, from the target's exact bounds; a target wholly behind fails | IMPLEMENTED | `ThroughAllExtrude_CutsThroughAtEveryThickness`; `ThroughAllExtrude_GoesTheWayItsDirectionSays` |
| Patterns and mirrors repeat a through-all cut at each instance | IMPLEMENTED | `ThroughAllExtrude_RepeatsThroughTheBodyAtEveryInstance` |
| Face names as for any extrude; the caps are gone | IMPLEMENTED | `ThroughAllExtrude_NamesItsSidesAndLosesItsCaps`; `ThroughAllExtrude_CutsThroughAtEveryThickness` (a peg on the slot's wall) |
| Save/load, validation, CLI | IMPLEMENTED | `ThroughAllFile_*`; `ThroughAllCli_*`; `cli.info.through-slab`, `cli.validate.through-slab`, `cli.export-step.through-slab`; `ThroughAllExtrude_RegeneratesDeterministically` |
| Evidence | this directory | — |

**Not in scope, and not implemented:**

- other terminations (*up to next*, *up to a face*, *up to a vertex*, a
  distance from a face);
- through-all joins and intersections;
- a second direction with its own termination.

## Design

```text
ExtrudeTermination {Blind, ThroughAll}   in ExtrudeDefinition (last field, default Blind)
validate()                               through all: no depth, no depth parameter; a cut with a target
extrudeTool(feature, document,           the extent at regeneration: the target's exact bounding box
            target, placement)           (Body::boundingBox()) projected on the sketch normal, 1 mm
                                         beyond it; normal: [0, max+1]; reversed: [min-1, 0];
                                         symmetric: [min(min,0)-1, max(max,0)+1]
throughAllOperation (PatternSupport)     patterns and feature mirrors rebuild the tool per instance,
                                         projecting on the moved normal (placement = the motion)
FeatureJson                              "termination": "through_all", no "depth" key; no key = blind
CLI info                                 "profile CutSketch, through all, reversed, cut Slab"
```

**No stored length.** Nothing in the document encodes how long the tool is.
The extent is a function of the target body at regeneration. It projects
the eight corners of the body's axis-aligned bounds, which `AddOptimal`
computes exactly, on the sketch normal. The tool reaches 1 mm beyond the
projections, so its caps lie clear of every face of the body. The box
contains the body, so the tool reaches through it, however the body lies
relative to the sketch.

**Failures.** The target must have a body (FailedPrecondition otherwise). A
cut along the normal whose target lies wholly behind the sketch plane (the
largest projection within 1e-7 mm of it) removes nothing and fails, as does
a reversed cut with the target wholly in front. A symmetric cut never fails
this way.

**Instances.** A pattern or feature mirror of a blind extrude moves one
tool. A through-all tool depends on where it is, so `throughAllOperation()`
calls `extrudeTool()` for each instance, with the instance's motion. The box
is then projected on the moved origin and normal, the tool is built in the
source's frame and moved, its faces are renamed as copies
(`P12-SKETCH-003`), and it is subtracted.

**Blind extrudes are unchanged.** `extrudeTool()` resolves the depth,
directions and face roles exactly as before, and the file format writes and
reads them as before. `termination` is written only for through-all
extrudes, so every existing file and example is byte-identical.

**The reference models** (`tests/support/ThroughCutModels.hpp`; the saved
slab is `examples/models/through_slab.bcad`):

| Model | Contents |
| --- | --- |
| ThroughSlab | a 100 × 60 × `thickness` slab. Cut: a circle r 8 and a 30 × 20 slot through all, reversed, from the top face. Slant: a circle r 5 on a plane tilted 30° about X, through all both ways. Peg: a boss on the slot's wall. |
| PerforatedPlate | a 100 × 60 × `thickness` plate; a hole r 4 through all; a linear pattern of `holes` copies, 20 mm apart |
| HollowSection | a 60 × 30 rectangular tube with 5 mm top and bottom walls and 10 mm side walls, 60 mm long; a hole r 3 through all along +Z from its mid-plane (through the top wall only) |

## Independent Validation

Expected values are written out in `ThroughCutModels.hpp` from the
parameters:

- **Slab.** V = 6000 t − 64π t − 600 t − 25π t / cos 30°. A cylinder
  crossing a slab at angle θ cuts each horizontal section in an ellipse of
  area π r² / cos θ, so its volume there is π r² t / cos θ (Cavalieri). Its
  centroid lies on its axis at mid-thickness, (50, 30 − (t/2 − 10) tan θ,
  t/2).
- **Plate.** V = 6000 t − n · 16π t, with the holes' centroids at
  x = 10 + 20k.
- **Tube.** 60000 mm³ less 45π for a hole through a 5 mm wall and 90π
  through a 10 mm wall. The cylindrical side of a copied hole has area
  2π · 3 · (wall thickness).

`deviations.txt` (`deviations.py` over `reference-values-release.txt`)
lists the largest deviations:

| Check | Tolerance | Checks | Largest deviation measured (Release) |
| --- | --- | --- | --- |
| Frames of sketches and the cut's side walls: origins | 1e-12 mm | 25 | 0 |
| Frames: axes and normals | 1e-14 | 28 | 0 |
| Centres of mass | 1e-9 mm | 39 | 8.2e-13 mm |
| Bounds | 1e-7 mm | 78 | 1.1e-14 mm |
| Volumes and copied holes' side areas | 1e-12 rel | 24 | 3.4e-15 |
| STEP read-back volume and bounds | 1e-9 rel | 3 | 6.0e-15 |

**Following the target.**

- In `ThroughAllExtrude_CutsThroughAtEveryThickness`, `thickness` goes
  20 → 30 → 12 mm. At each step the slab, the circle, the slot and the
  slanted hole match the formulas; the sketch on the top face follows it,
  and the peg on the slot's wall stays in place. The through-all
  definitions still hold no depth. Undo restores every placement and
  volume bit for bit.
- In the plate, `thickness` 10 → 25 and `holes` 5 → 3: every hole goes
  through.

**Directions.**

- From the top face, a reversed cut and a symmetric cut give the same
  volume (within 1e-12, and the formula). A cut along the normal fails
  (below), blocking Slant, the wall sketch and the peg; undo restores the
  model bit for bit.
- From a plane 5 mm below the slab, a cut along the normal goes through
  (120000 − 20 × 200 mm³, centre from the parts); a reversed one fails.

**Instances that need a different length.**

- A circular pattern of the tube's hole about +Y, 4 instances, puts copies
  on +X, −Z and −X. The body is 60000 − 2 · 45π − 2 · 90π, centred on the
  axis.
- Each copy's hole wall has the area of its own wall: 30π on +Z and −Z,
  60π on +X and −X. A copy built with the original's length (15 + 1 mm
  along its normal) would not reach the side walls at x = ±20.
- A feature mirror in the plane x = z turns the hole onto the +X wall:
  60000 − 45π − 90π, with the centre from the parts. The image's wall is
  60π.

## Diagnostics

Asserted verbatim by the tests:

| Case | Code | Message |
| --- | --- | --- |
| a depth or depth parameter | InvalidArgument | `a through-all extrude has no depth: it reaches through its target` |
| another operation | InvalidArgument | `a through-all extrude must be a cut, got new body` (`join`, `intersect`) |
| no target | InvalidArgument | `a cut feature needs a target feature` |
| target behind (normal) | FailedPrecondition | `Cut: the target lies wholly behind the sketch plane, so cutting through all along its normal removes nothing` |
| target in front (reversed) | FailedPrecondition | `Up: the target lies wholly in front of the sketch plane, so cutting through all against its normal removes nothing` |
| no target body | FailedPrecondition | `Perf: a through-all extrude needs the body of its target feature` |
| the caps | NotFound | `the start cap of Cut (object:5) is not a face of its body (the feature's operation left no such face)`; the same for the end cap |
| an invalid edit | InvalidArgument | refused by `ModifyExtrudeCommand`; the document is unchanged |
| file | — | `….data.termination: unknown value 'up_to_next'`, `… expected a string`, `….data.depth: a through-all extrude has no depth` (and `depth_parameter`), `….data: a through-all extrude must be a cut, got join`, `….data.depth: missing required field` (a blind extrude without a depth) |
| CLI `validate` | failure | the regeneration message above, `Result: invalid` |

A failed through-all cut keeps no body, and the features after it are
blocked.

## Tests

14 new Catch2 test cases, tagged `[extrude][p12]`, and 3 process tests:

- `tests/features/ThroughAllExtrudeTests.cpp`: 7 cases. Validation;
  following the thickness; directions; instances (linear, circular, mirror);
  names; edits, undo and atomicity; determinism.
- `tests/io/ThroughAllFileTests.cpp`: 5 cases. Round trip, the written form,
  malformed files, the example file, STEP.
- `tests/cli/ThroughAllCliTests.cpp`: 2 cases (`info`, `validate`).
- `cli.info.through-slab`, `cli.validate.through-slab` and
  `cli.export-step.through-slab` run on `examples/models/through_slab.bcad`,
  each in a fresh process.

No existing test changed. The Release run of the 14 cases records **1264
passed assertions and 0 failed** (`reference-values-release.txt`).

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
| Debug | exit 0 | attempt 1 | exit 0 | 306 | 0 | **936/936 passed** (96.0 s) |
| Release | exit 0 | attempt 1 | exit 0 | 306 | 0 | **936/936 passed** (95.0 s) |
| Debug-shared | exit 0 | attempt 1 | exit 0 | 306 | 0 | **936/936 passed** (105.3 s) |

306 is `P12-SKETCH-003`'s 303 translation units plus the three new test
files.

**Repeats.**
`ctest -R "[Ee]xtru|ThroughAll|[Pp]attern|[Mm]irror|[Cc]ircular|[Ll]inear|[Ff]ace|[Rr]eference|[Ss]ketch|[Hh]ole|[Bb]oolean|[Pp]ropert|[Rr]egenerat|[Vv]alidat|[Dd]ocument|[Ff]ile|[Cc]ommand|cli|through-slab|post-row|P9" --repeat until-fail:5`
selected 587 tests.

| Preset | Result | Time |
| --- | --- | --- |
| Release | 587/587, each 5 times (2935 passed runs) | 284.0 s |
| Debug | 587/587, each 5 times (2935 passed runs) | 298.0 s |

**Determinism.**

- `ThroughAllExtrude_RegeneratesDeterministically` builds the slab twice
  and gets bit-identical placements and volumes, equal face names and
  equivalent documents. A second pass rebuilds nothing, and a full rebuild
  gives the same bits.
- The file round trip regenerates every body to the same bits and
  serializes to identical text.
- The three process tests work in fresh processes.
- Across configurations (`values-determinism.txt`), the Debug, Release and
  Debug-shared executables printed identical values for the 14 cases (584
  lines, MD5 `35a27b00a304a6a59c01c0a45226e1bb`).

**Timing** (`timing-comparison.txt`: CTest's per-test times summed, `-j 8`,
same machine), P12-SKETCH-003 → P12-FEAT-001, with 17 more tests:

| Preset | P12-SKETCH-003 | P12-FEAT-001 |
| --- | --- | --- |
| Release | 630.3 s | 652.4 s |
| Debug | 688.6 s | 625.2 s |
| Debug-shared | 634.4 s | 636.0 s |

The 17 new tests take 7.2 s in Release. The largest move among the twelve
slowest tests was `ReferenceModel_AllModelsStressRegression`, faster in all
three presets (Release 21.63 → 20.11 s, Debug 23.22 → 20.57 s,
Debug-shared 21.28 → 19.14 s). The totals moved by up to 63 s, both ways,
with no systematic direction; no speed change is claimed.

## Legacy Regression

`regression-comparison.txt` compares every log by test name with
`../P11-QUAL-001/release-ctest.log` (737 names), and
`regression-comparison-sketch003.txt` with
`../P12-SKETCH-003/ctest-release.log` (918 names):

- every baseline name is present and passed in the Debug, Release and
  Debug-shared logs, and no entry failed;
- the new names are this milestone's 17 tests (198 against P11).

**Every value the existing tests measure is unchanged**
(`all-values-comparison.txt`):

- Before its clean rebuild, the qualified P12-SKETCH-003 Release build (its
  second run) ran every test case with `-s`. So did this milestone's
  qualified Release build.
- After dropping the 14 new cases and normalizing temporary paths, document
  UUIDs and the reported Git revision (line numbers kept: no existing test
  file changed), the 33226 measured-value lines of the 863 existing cases
  are identical (MD5 `af32778b12064cfcdde6aecb30e26e99` both).

**The P0–P12-SKETCH-003 regression suite remains green.** No existing test
changed. Changes to existing production code:

- `ExtrudeFeature`: `ExtrudeTermination`, `toString()` and `validate()`;
- `ExtrudeRegeneration`: the extent (blind as before, through all from the
  target); `extrudeTool()` takes the target and the placement;
- `PatternSupport`: `throughAllOperation()`;
- `FeatureJson`: `termination`;
- CLI `info`: through-all extrudes.

## Known Limitations

- **Through all is the only new termination.** It applies to cuts only:
  there is no *up to next* or *up to a face*, and a join cannot end through
  all.
- **The tool is as long as the target's bounding box along the sketch
  normal, plus 1 mm.** For a sketch tilted to the body's axes the box is
  larger than the body, so the tool is longer than it needs to be. The cut
  is the same.
- **A through-all cut whose profile misses the body removes nothing**
  without an error, as a blind cut does. Only a target wholly on the wrong
  side of the sketch plane is refused.
- **A through-all cut has no caps to refer to**: its start cap lies on the
  target's face or outside the body, and its end cap lies outside it.
- **A pattern or mirror of a through-all cut rebuilds the tool for each
  instance**, profile regions included. This adds to the pattern's cost,
  which already grows with the square of the count.

## Evidence Files

- `README.md`: this file.
- `qualify.cmd`, `run-qualification.cmd`: the qualification as run.
- `qualification-times.txt`: every step's start, exit code and time, the HEAD
  and the Git tree IDs of the qualified sources.
- `configure-*.log`, `clean-*.log`, `build-*.log`, `ctest-*.log`: the three
  presets.
- `ctest-repeat-{release,debug}.log`: the related tests, 5 times each.
- `regression-comparison.txt`, `regression-comparison-sketch003.txt`,
  `compare-regression.py`: the legacy comparisons.
- `all-values-comparison.txt`: every measured value of the P12-SKETCH-003
  tests, against the P12-SKETCH-003 Release build.
- `reference-values-release.txt`, `values.py`, `values-header.txt`: the new
  tests' measured values.
- `values-determinism.txt`, `compare-values.py`: the same from all three
  configurations.
- `deviations.txt`, `deviations.py`: the largest deviations.
- `timing-comparison.txt`, `compare-times.py`: per-test times.

## Final Result

```text
TASK:            P12-FEAT-001 Through-all extrude
IMPLEMENTATION:  ExtrudeTermination (blind, through all) in the definition;
                 the tool's extent from the target's bounds at regeneration,
                 per direction; per-instance tools in patterns and mirrors;
                 JSON "termination"; CLI description
TESTS:           14 new test cases and 3 process tests; 936/936 in Debug,
                 Release, Debug-shared; 587 related tests x5 in Release and
                 Debug
VALIDATION:      cuts through at every thickness; slanted cut = pi r^2 t /
                 cos(30 deg); instances on thicker walls cut through (hole
                 walls 30 pi / 60 pi); volumes within 3.4e-15 rel, centres
                 8.2e-13 mm, bounds 1.1e-14 mm; every existing measured
                 value unchanged; values identical across configurations
RESULT:          PASS
EVIDENCE:        docs/verification/P12-FEAT-001/
TODO:            P12-FEAT-001 deliverables ticked
```
