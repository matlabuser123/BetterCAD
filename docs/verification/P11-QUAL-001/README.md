# P11-QUAL-001 — Production Part Modeling Qualification

## Status

**PASS — 20 gates, 20 passed, 0 failed, 0 blocked.**

Revision `79dab04` was rebuilt clean in all three supported configurations
and ran the complete test suite in each: **738/738 tests passed with 0
compiler warnings over 274 translation units, three times over.** The six
reference models are bit-identical across every build and every process that
produced them. No production code was changed during the run, and no test
failed at any point.

## Qualified Revision

```text
79dab04bd6e83a5a396b0e6a57199587f720c4eb
BetterCAD: add and verify P11 mechanical reference models
```

This qualification measures that commit and nothing else. It was committed
and pushed to `origin/main` before the run began, the working tree was clean
(`qualified-revision.txt` records `git rev-parse HEAD` and `git status
--porcelain` as the run started), and no source file was touched while the
run was in progress. Every build below starts from `cmake --build
--clean-first`, so no object file from an earlier build survives into it.

Date: 2026-09-16. As each configure log records it: Windows AMD64 (Windows 11
Pro 26200), C++ compiler GNU 16.1.0 (WinLibs MinGW-w64 POSIX UCRT), C++23,
CMake 4.4.2 with Ninja, Catch2 3.16.0, Qt 6.11.2, Open CASCADE 8.0.1, and
`Warnings as errors : ON` in all three presets.

Tags are unchanged by this milestone: `v0.1.0` remains at
`93d84f0cf0ea54c58fd79b89e2f7f699f970caa4`, and it is still the only tag.

## Scope

P11-FEAT-001 Revolve, P11-FEAT-002 Chamfer, P11-FEAT-003 Fillet,
P11-FEAT-004 Hole, P11-FEAT-005 Linear pattern, P11-FEAT-006 Circular
pattern, P11-FEAT-007 Mirror, P11-FEAT-008 Sweep, P11-FEAT-009 Loft and
P11-REF-001 Mechanical reference models — the whole P11 production
part-modelling system, and the P0–P10 core it is built on.

This milestone adds no feature. It asks one question with reproducible
evidence: can BetterCAD build, regenerate, persist, validate and export
realistic parametric mechanical parts, in every supported configuration,
without regressing the CAD core?

Each milestone's own evidence stays where it is
(`docs/verification/P11-FEAT-00*/`, `docs/verification/P11-REF-001/`);
nothing here is copied from those runs. Every number below comes from this
qualification's own commands.

## Test Inventory

The inventory was built **before** the qualification run, so the gate numbers
below were fixed in advance rather than read off the results. `test-inventory.py`
takes three independent sources — `ctest --show-only=json-v1` for every
registered test, the test binary's `--list-tests --reporter xml` for each
Catch2 case with its tags and source file, and the `v0.1.0` tag for what
existed before P11 — and produces `test-inventory.txt` (788 lines, every test
named):

```text
registered tests          738
  Catch2 unit tests       694
  process and check tests 44
legacy (present at v0.1.0, the P0-P10 release)  301
P11 (feature or reference-model tags)           417
added since v0.1.0 without a P11 tag            20
```

301 + 417 + 20 = 738, and the three sets are disjoint, so every registered
test belongs to exactly one of them and none is unaccounted for.

| Milestone | Tests |
| --- | --- |
| P11-FEAT-001 Revolve | 37 |
| P11-FEAT-002 Chamfer | 36 |
| P11-FEAT-003 Fillet | 36 |
| P11-FEAT-004 Hole | 71 |
| P11-FEAT-005 Linear pattern | 39 |
| P11-FEAT-006 Circular pattern | 42 |
| P11-FEAT-007 Mirror | 41 |
| P11-FEAT-008 Sweep | 48 |
| P11-FEAT-009 Loft | 49 |
| P11-REF-001 Reference models | 53 |

(A test may cover more than one milestone, so the column sums to more than
417.)

**The 20 tests that are neither.** These are not an unclassified bucket:
`test-inventory.txt` names every one of them with its source file and tags.
They are the core geometry, math and build checks the P11 milestones brought
with them rather than P11 features themselves, and they account for all 20
exactly:

- **7 edge and face signature tests** — `EdgeSignature_…`, `Edges_…` (×4),
  `FaceSignature_…`, `Faces_…`: the machinery that chamfer, fillet and hole
  references resolve against.
- **11 transform tests** — `RigidTransform_…` (×4) and `Transform_…` (×7):
  what linear pattern, circular pattern and mirror are built on.
- **1 boolean regression** — "A half united with its mirror image: a planar
  seam merges, a cylindrical seam is refused", the limitation the bearing
  housing found, pinned so it cannot change silently.
- **1 architecture check** — `architecture.checker.examples-occt-leak`,
  which proves the layering checker fails a build if an example reaches into
  Open CASCADE.

They are counted in the 738 and run in every configuration below.

## Debug

```text
cmake --preset debug                         exit 0   (8.6 s)
cmake --build --preset debug --clean-first   exit 0   (4 m 13 s)
ctest --preset debug --output-on-failure     exit 0   (6 m 27 s)
```

274 translation units compiled, 0 compiler warnings, **738/738 tests
passed**, total test time 386.64 s. Logs: `debug-configure.log`,
`debug-build.log`, `debug-ctest.log`. Configuration as reported by CMake:
`Build type: Debug`, `Shared libraries: OFF`, `Warnings as errors: ON`,
`Tests: ON`.

## Release

```text
cmake --preset release                       exit 0   (4.6 s)
cmake --build --preset release --clean-first exit 0   (5 m 02 s)
ctest --preset release --output-on-failure   exit 0   (5 m 55 s)
```

274 translation units compiled, 0 compiler warnings, **738/738 tests
passed**, total test time 354.44 s. Logs: `release-configure.log`,
`release-build.log`, `release-ctest.log`. `Build type: Release`,
`Shared libraries: OFF`, `Warnings as errors: ON`.

The Release fingerprints, the CLI smoke and the failure-path run below were
all taken from this build. The per-model assertion values in
`../P11-REF-001/<model>/values-release.txt` are *not* from this build — they
were captured from P11-REF-001's own Release build of the same sources, and
are cited here as that milestone's evidence, not re-labelled as this run's.
The two builds agree exactly where they can be compared: their model
fingerprints are byte-identical (see *Reference Model Determinism*).

## Debug-shared

```text
cmake --preset debug-shared                        exit 0   (5.0 s)
cmake --build --preset debug-shared --clean-first  exit 0   (4 m 31 s)
ctest --preset debug-shared --output-on-failure    exit 0   (7 m 35 s)
```

274 translation units compiled, 0 compiler warnings, **738/738 tests
passed**, total test time 454.70 s. Logs: `debug-shared-configure.log`,
`debug-shared-build.log`, `debug-shared-ctest.log`. `Build type: Debug`,
`Shared libraries: ON`, `Warnings as errors: ON`.

This preset matters because it links the modules as separate DLLs rather
than into one static binary. It therefore exercises the export macros, the
one-definition rule across module boundaries and the runtime DLL copying
that a static build cannot check. It is the slowest of the three (cross-DLL
calls are not inlined), and it passes the same 738 tests with the same
results.

## Compiler Warning Audit

**0 compiler warnings in all three builds** (`warnings.txt`, produced by
`warning-audit.py` from the three build logs).

"Zero warnings" is a claim that is easy to make dishonestly, so the method
is stated explicitly:

- A compiler diagnostic is counted **as GCC prints it** —
  `<file>:<line>:<column>: warning:` or `error:` — not by searching a log
  for the word "warning", which would match file names, test names and
  ordinary output.
- CMake warnings (`CMake Warning`) are counted separately.
- Every other line that merely contains the word is counted too, so nothing
  is dismissed silently.

```text
== debug-build.log / release-build.log / debug-shared-build.log
   translation units compiled : 274   (each)
   compiler warnings or errors: 0
   CMake warnings or errors   : 0
   other lines mentioning it  : 0
```

Three points make the result meaningful rather than vacuous:

1. **Nothing was silenced to obtain it, and the diagnostics are not weak.**
   `cmake/BetterCADCompilerOptions.cmake` turns on 22 warning flags for
   GCC — `-Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor
   -Wold-style-cast -Wcast-align -Wunused -Woverloaded-virtual -Wconversion
   -Wsign-conversion -Wdouble-promotion -Wformat=2 -Wimplicit-fallthrough
   -Wextra-semi -Wundef -Wmissing-declarations -Wsuggest-override
   -Wmisleading-indentation -Wduplicated-cond -Wduplicated-branches
   -Wlogical-op` — plus `-Werror`. A search of the build system and of all
   of `src/`, `include/`, `apps/`, `tests/` and `examples/` finds **no
   `-Wno-…` flag and no `#pragma GCC diagnostic` anywhere**: not one
   warning is suppressed, locally or globally. `-Wconversion` and
   `-Wsign-conversion` in particular are demanding in numeric geometry
   code, and they are on everywhere.
2. **Every file was actually compiled.** Each build ran with
   `--clean-first`, and the audit counts 274 `Building CXX object` lines in
   each log — the full set. A stale object file cannot hide a warning here,
   because there were no stale object files.
3. **A warning could not have survived anyway.** All three presets configure
   with `Warnings as errors : ON` (`-Werror`), so any diagnostic would have
   failed the build and the run would have stopped. The audit confirms the
   builds were clean, rather than being the only thing standing between a
   warning and a pass.

## Legacy Regression

**301/301 legacy tests passed in every configuration.**

The legacy set is defined by the `v0.1.0` tag, not by judgement: a test is
legacy when its name was already registered at
`93d84f0` — the P0–P10 release, before any P11 work began.
`categorize-results.py` maps each result line in each CTest log back to the
inventory, so these numbers come from the runs themselves rather than from
test names in isolation (`results-by-category.txt`):

| Configuration | Legacy | P11 | Added since v0.1.0 | Unclassified | Total |
| --- | --- | --- | --- | --- | --- |
| Debug | 301/301 | 417/417 | 20/20 | 0 | 738/738 |
| Release | 301/301 | 417/417 | 20/20 | 0 | 738/738 |
| Debug-shared | 301/301 | 417/417 | 20/20 | 0 | 738/738 |

No test reported anything but `Passed` in any configuration — no failure, no
timeout, no skip, and nothing "Not Run". The unclassified column is zero in
all three: every test that ran was accounted for by the inventory built
beforehand.

This is the regression gate that matters most: ten feature milestones were
added on top of the P0–P10 core, and the core's own tests — units,
parameters, documents, sketches, the constraint solver, the dependency
graph, persistence, the CLI — still pass unchanged, in all three builds.

## P11 Regression

**417/417 P11 tests passed in every configuration**, and the 20 core tests
the P11 milestones brought with them passed too, giving 437/437 of the work
P11 added.

Per milestone (a test may cover more than one, so the column sums to more
than 417):

| Milestone | Tests | Debug | Release | Debug-shared |
| --- | --- | --- | --- | --- |
| P11-FEAT-001 Revolve | 37 | pass | pass | pass |
| P11-FEAT-002 Chamfer | 36 | pass | pass | pass |
| P11-FEAT-003 Fillet | 36 | pass | pass | pass |
| P11-FEAT-004 Hole | 71 | pass | pass | pass |
| P11-FEAT-005 Linear pattern | 39 | pass | pass | pass |
| P11-FEAT-006 Circular pattern | 42 | pass | pass | pass |
| P11-FEAT-007 Mirror | 41 | pass | pass | pass |
| P11-FEAT-008 Sweep | 48 | pass | pass | pass |
| P11-FEAT-009 Loft | 49 | pass | pass | pass |
| P11-REF-001 Reference models | 53 | pass | pass | pass |

Every P11 feature is therefore exercised in an unoptimised build, an
optimised build and a shared-library build, and none of them depends on the
optimisation level or on how the modules are linked.

## Reference Model Determinism

Determinism is checked at three widths: inside one process, across fresh
processes, and across separate qualification runs.

**1. Repeated runs of the reference-model tests.** Every reference-model
test was run five times over in both static presets, with
`--repeat until-fail:5`, which stops at the first failure:

```text
ctest --preset release -R "ReferenceModel_|reference" --repeat until-fail:5
  100% tests passed out of 70   (350 executions, 1211.16 s)
ctest --preset debug   -R "ReferenceModel_|reference" --repeat until-fail:5
  100% tests passed out of 70   (350 executions, 1232.54 s)
```

70 tests × 5 rounds = 350 recorded passes in each preset, 700 in total, with
no early stop. Those tests contain their own repetition: each model's
`…RegeneratesDeterministically` case regenerates the document ten times and
requires a bit-identical fingerprint each time, and
`ReferenceModel_AllModelsStressRegression` builds, regenerates and discards
all six models five times over. The unchanged-regeneration requirement is
therefore met many times over, not five.

**2. Two fresh processes per configuration.** The example program was run
twice in each of the three builds — six independent processes, each starting
from nothing and building all six models:

```text
fingerprints-debug-run1.txt         95f874ced3eb07d1c2f5afff8d264f27
fingerprints-debug-run2.txt         95f874ced3eb07d1c2f5afff8d264f27
fingerprints-release-run1.txt       95f874ced3eb07d1c2f5afff8d264f27
fingerprints-release-run2.txt       95f874ced3eb07d1c2f5afff8d264f27
fingerprints-debug-shared-run1.txt  95f874ced3eb07d1c2f5afff8d264f27
fingerprints-debug-shared-run2.txt  95f874ced3eb07d1c2f5afff8d264f27
```

One MD5 over all six (of everything but the `timing_ms` lines, which are
wall-clock measurements and are expected to differ). Identical object IDs,
names, feature and body counts, validity, topology counts, volumes, areas,
centroids and bounds — to the last printed digit.

**3. Across qualification runs.** The same digest is produced by
P11-REF-001's own Release run
(`../P11-REF-001/fingerprints-release.txt`), which was a separate clean
rebuild several hours earlier. Two independent qualifications, six
processes, three build configurations: one result.

Nothing here depends on iteration order, hashing, timing, threading or
locale. The models are reproducible.

## Cross-Configuration Comparison

All six reference models, compared across all six runs by `cross-build.py`
(`cross-configuration.txt`), taking Debug run 1 as the baseline:

| Model | Volume/area agreement | Position agreement | Structure |
| --- | --- | --- | --- |
| Shaft | 0 relative | 0 mm | same |
| Flange | 0 relative | 0 mm | same |
| Pulley | 0 relative | 0 mm | same |
| Bearing housing | 0 relative | 0 mm | same |
| Mounting bracket | 0 relative | 0 mm | same |
| U-bolt | 0 relative | 0 mm | same |

```text
Worst over all models and builds: 0 relative, 0 mm.
No structural differences.
```

The comparison is written to tolerate rounding differences between builds —
it reports how far apart the numbers are — and the answer is that they are
not apart at all. `-O0` static, `-O2` static and `-O0` shared produce the
same doubles. This is worth stating plainly because it is the property a CAD
system needs and does not always have: a part measured in a debug session is
the same part the optimised build ships.

## Persistence

**20 save/load tests and 22 undo/redo tests passed in all three
configurations.**

The round trip that is tested is the one that matters — create, save,
destroy the in-memory model, load, regenerate, compare — not a file-size or
object-count check. For the six reference models
(`ReferenceModel_<model>SaveLoad`), the comparison is the full fingerprint:
every object ID, name, parameter, feature, body, topology count, volume,
area, centroid and bound must match the model that was saved.

Two further properties are checked:

- **The committed models match their builders byte for byte.**
  `ReferenceModel_SavedModelsMatchTheBuilders` saves each of the six models
  and compares the bytes with the committed
  `examples/models/reference/*.bcad`. The files in the repository are
  therefore exactly what the code produces today — they cannot drift out of
  date silently, and the CLI tests below load those same files.
- **Undo/redo returns the document to a compared state.**
  `ReferenceModel_<model>UndoRedo` runs parameter edits through the command
  history and compares document state after undo and after redo, rather than
  assuming the command objects are correct.

Stable IDs survive all of it: the fingerprints that are compared include
every `ObjectId`, so a load that renumbered anything would fail.

## STEP/STL Smoke Qualification

**43 STEP/STL export tests passed in all three configurations.**

Exports are checked by reading the result back and measuring it, not by
checking that a file appeared:

- **STEP** is re-imported through the kernel and the recovered solid's
  volume, area and centroid are compared with the exact geometry. A STEP
  file that parses but describes the wrong solid fails.
- **STL** is checked as a mesh: closed (every edge shared by exactly two
  triangles), outward-facing, and with a volume that agrees with the exact
  solid to within the tessellation bound — the deflection (0.01 mm)
  multiplied by the surface area, which is the most a mesh of that
  deflection can differ by. That bound is derived rather than fitted, so a
  mesh that drifted would be caught.

The reference-model export tests (`ReferenceModel_<model>Exports`) apply
this to all six parts, and the CLI smoke below exports all six again through
the shipped executable.

## CLI Smoke Qualification

**28 invocations of the real `bettercad-cli.exe`: 27 succeeded and the one
intended failure failed correctly** (`cli-smoke.log`).

This is the shipped Release executable run as a user would run it — a
separate process, its own argument parsing, its own output — not a test
harness calling the library:

```text
bettercad-cli --version
bettercad-cli new <tmp>\scratch.bcad --name QualificationScratch
bettercad-cli info <tmp>\scratch.bcad
for each of shaft, flange, pulley, bearing_housing, mounting_bracket, u_bolt:
    bettercad-cli info        examples\models\reference\<model>.bcad
    bettercad-cli validate    examples\models\reference\<model>.bcad
    bettercad-cli export-step examples\models\reference\<model>.bcad <tmp>\<model>.step
    bettercad-cli export-stl  examples\models\reference\<model>.bcad <tmp>\<model>.stl
bettercad-cli validate <tmp>\does-not-exist.bcad
```

Every command returned 0 except the last, which is meant to fail and does so
as a diagnostic rather than a crash or a silent success:

```text
Validating ...\does-not-exist.bcad
  document consistency  1 error
    error: '...\does-not-exist.bcad' does not exist or is not a file
Result: invalid (1 error)
exit 1
```

All twelve exports were written and none is empty — 17–111 kB of STEP and
50–438 kB of STL:

```text
shaft.step 36902            shaft.stl 159684
flange.step 39269           flange.stl 167384
pulley.step 50100           pulley.stl 438084
bearing_housing.step 66125  bearing_housing.stl 50084
mounting_bracket.step 111203 mounting_bracket.stl 69384
u_bolt.step 17243           u_bolt.stl 100684
```

These are the committed `.bcad` files, so the CLI is exercising the same
documents the tests do, through the public API, from a cold process. The
suite also registers nine of these commands as CTest process tests
(`cli.validate.reference.<model>`, `cli.info.reference.shaft`,
`cli.export-step.reference.bracket`, `cli.export-stl.reference.pulley`),
which assert on the printed volumes, so they ran in all three configurations
as part of the 738.

## Failure-Path Qualification

**124/124 failure-path tests passed** (`failure-paths-release.log`, 113.39 s).

Selected by running every test whose name describes a refusal or a failure:

```text
ctest --preset release -R "[Rr]ejects|[Ff]ail|[Rr]efuses|[Ii]nvalid|[Aa]tomic|Intact"
  100% tests passed out of 124
```

They cover every P11 feature and the reference models:

| Feature | Failure-path tests |
| --- | --- |
| Sweep | 16 |
| Hole | 11 |
| Loft | 11 |
| Linear pattern | 9 |
| Chamfer | 8 |
| Circular pattern | 8 |
| Fillet | 7 |
| Mirror | 7 |
| Reference models | 6 |
| Revolve | 2 |
| Transform | 1 |
| **P11 features and models** | **86** |
| Core, CLI, persistence, solver and units | 25 |
| Compile-time refusals (`compile_fail.*`) | 13 |
| **Total** | **124** |

The last row is worth naming separately. Thirteen of these tests assert that
certain code **does not compile**, which is the only way to test a guarantee
that is meant to be impossible to violate at runtime:

```text
compile_fail.units.add-length-angle          compile_fail.ids.integer-to-id
compile_fail.units.assign-mass-to-length     compile_fail.ids.id-to-integer
compile_fail.units.assign-double-to-length   compile_fail.ids.object-to-sketch-id
compile_fail.units.length-to-double          compile_fail.ids.sketch-id-as-feature-id
compile_fail.units.force-per-length-as-pressure  compile_fail.ids.entity-id-as-object-id
compile_fail.units.compare-length-time       compile_fail.ids.compare-different-kinds
compile_fail.units.sqrt-of-volume
```

Adding a length to an angle, assigning a mass to a length, passing a raw
`double` where a `Length` belongs, comparing a length with a time, using a
`SketchId` as a `FeatureId`, converting an ID to an integer — each of these
is required to be a compiler error. The unit safety and stable typed
identity that the architecture depends on are enforced by the type system,
and these tests prove the enforcement is still there.

What these tests require is stronger than "an error was returned". A
refusal must be specific: the error identifies what failed, which object
caused it, and why — a chamfer that does not fit reports the room it needed
and the room it had; a reference that no longer resolves reports the
geometry it was looking for. Nothing is guessed: when a parameter moves the
edge a chamfer was attached to, the feature fails with `NotFound` rather
than silently taking a different edge, and the reference-model failure tests
then restore the parameter and require the model to come back bit for bit.

Failures are also checked to be *reachable*: each reference model has a
`…FailsSafelyOnInvalidParameters` test built around a genuinely invalid
dimension (a fillet that cannot fit, a hole off the edge, a bolt circle
through the rim, a bore wider than the hub, a bore that eats the boss, a
chamfer wider than the rod), and during P11-REF-001 one such case had to be
made *more* extreme when the original value turned out to still produce
valid geometry — the test was strengthened rather than accepted.

## Atomic-Regeneration Qualification

A failed feature must leave the document exactly as it was. Every P11
feature has a test that requires this by name, and all of them passed in all
three configurations:

```text
ChamferFeature_FailuresLeaveTheDocumentAndUpstreamBodiesIntact
FilletFeature_FailuresLeaveTheDocumentAndUpstreamBodiesIntact
HoleFeature_FailuresLeaveTheDocumentAndUpstreamBodiesIntact
LinearPattern_FailuresLeaveTheDocumentAndUpstreamBodiesIntact
CircularPattern_FailuresLeaveTheDocumentAndUpstreamBodiesIntact
MirrorFeature_FailuresLeaveTheDocumentAndUpstreamBodiesIntact
SweepFeature_FailuresLeaveTheDocumentAndUpstreamBodiesIntact
LoftFeature_FailuresLeaveTheDocumentAndUpstreamBodiesIntact
LinearPattern_FailsAtomicallyWhenInstanceInvalid
CircularPattern_FailsAtomicallyWhenInstanceInvalid
MirrorFeature_FailsAtomicallyWhenMirroredFeatureInvalid
```

The pattern and mirror cases are the demanding ones: a pattern that produces
twelve instances of which the tenth is invalid must commit none of the
twelve, not ten of them. A half-applied pattern would be far worse than a
refusal, because the document would then contain geometry the model does not
describe.

The reference models check the same property end to end, from the outside:
each `…FailsSafelyOnInvalidParameters` test sets a dimension that cannot
produce valid geometry, requires the regeneration to fail with a structured
diagnostic, requires the document to be unchanged, then restores the
dimension and requires the model to return **bit for bit** — the same
fingerprint as before the failed attempt. A document that had been left
subtly modified by the failure would not reproduce its fingerprint.

## Bugs Found During Qualification

**None.** No test failed, in any configuration, at any point of this
qualification: the three clean builds, the 700 repeat executions, the
failure-path run and the CLI smoke all passed on the first attempt, and the
run's own step log (`qualification-times.txt`) records exit 0 for all 21
steps with no retry.

That is a claim about *this* run, and it should be read with the limits
below rather than as a claim that P11 is free of defects. Two things this
qualification cannot do are worth stating plainly:

- It re-runs an existing suite. It can only find a defect that some test
  already describes, or one bad enough to break a build, a determinism
  check or the CLI. A feature nobody wrote a test for would pass silently.
- It is one machine, one compiler, one kernel version. Nothing here says
  what happens under MSVC, Clang, a different Open CASCADE, or a
  non-Windows platform.

Defects *were* found during the milestones this qualification covers — most
recently the two recorded under Known Limitations, which were found while
building the reference models — and each was fixed or documented and pinned
by a regression test at the time. Those tests are part of the 738 that pass
here.

## Fixes Made

**None, and none were needed.**

No production code was changed during this qualification. The tree was
committed and pushed before the run started, `qualified-revision.txt`
records `git rev-parse HEAD` and a `git status --porcelain` showing no
tracked file modified, and the only files written while the run was in
progress were this evidence directory's own logs.

This matters for the integrity of the result: a qualification that edits
code as it goes qualifies nothing, because the artefact that passed at the
end is not the artefact that was measured at the start. Every number in this
document was produced from `79dab04`, and `79dab04` is what is on
`origin/main`.

## Known Limitations

These are properties of the system as qualified, recorded so that the PASS
above is not read as more than it is.

**Modelling limitations** (each documented with its milestone, each pinned
by a test):

- **Geometric references do not follow moved geometry.** Edges and faces are
  matched geometrically, not named semantically. When a parameter moves the
  edge a chamfer or fillet was attached to, the feature fails with
  `NotFound` and keeps no body; nothing is ever substituted. Semantic
  topology naming is a later milestone. The reference models show both
  sides: references that survive a parameter change, and references that do
  not.
- **A half bore on a mirror plane.** Uniting a half body with its mirror
  image is refused when a half cylinder lies on the mirror plane — the
  kernel's fuse returns a shape its own checker rejects, so BetterCAD
  refuses it rather than building it wrongly. The bearing housing therefore
  cuts its bore after joining its halves. Pinned by a boolean regression
  test.
- **Parameter expressions are not evaluated** (P1-003). A derived dimension
  needs either its own parameter or a sketch that builds the relation
  geometrically.
- **No through-all extrude.** A cut is given a depth; the housing's bore is
  driven by the same width parameter as the housing.
- **Loft faces stay B-splines.** The kernel keeps a loft's sides as B-spline
  surfaces even where they are flat, so the bracket agrees with exact
  geometry to 6.0e-12 relative and 3.4e-6 mm rather than to rounding.
- **Sketch constraints** cover coincident, horizontal, vertical, parallel,
  perpendicular, distance, radius, equal and fixed. There is no angle,
  tangent, midpoint or symmetry constraint; symmetry is built with
  construction geometry and equal constraints.

**Limits of this qualification as evidence:**

- **One platform.** Windows 11 AMD64, GCC 16.1.0 (MinGW-w64), Open CASCADE
  8.0.1, Qt 6.11.2. No MSVC, Clang, Linux or macOS result is claimed.
- **No CI.** There is no `.github/workflows`, so nothing re-runs this
  automatically; qualification is a deliberate act, and a regression between
  milestones would not be caught until the next one.
- **No sanitizers, no coverage, no memory checking.** No ASan/UBSan preset,
  no gcov/lcov, no valgrind or Dr. Memory configuration exists. Memory
  errors that do not crash, and code that no test reaches, would not be
  reported. The 738 tests say what passes, not how much of the code they
  touch.
- **`.clang-format` is not enforced.** The file exists and defines the
  house style, but no test, target or hook checks it, so formatting
  consistency rests on convention.
- **The GUI is not qualified.** `bettercad.exe` builds in all three
  configurations and is a placeholder shell; no GUI smoke test is claimed.
  P9's desktop workflow is exercised through the core and the CLI.
- **The suite defines the ceiling.** 738 tests passing means 738 tests
  passed. Where a behaviour has no test, this document says nothing about
  it.

## Final Gate Matrix

Every gate was fixed before the run and is answered from the run's own logs.

| # | Gate | Required | Measured | Result |
| --- | --- | --- | --- | --- |
| 1 | Debug clean build | builds, 0 warnings | 274 TUs, 0 warnings | PASS |
| 2 | Release clean build | builds, 0 warnings | 274 TUs, 0 warnings | PASS |
| 3 | Debug-shared clean build | builds, 0 warnings | 274 TUs, 0 warnings | PASS |
| 4 | Full suite, Debug | 738/738 | 738/738 | PASS |
| 5 | Full suite, Release | 738/738 | 738/738 | PASS |
| 6 | Full suite, Debug-shared | 738/738 | 738/738 | PASS |
| 7 | Legacy regression | 301/301 × 3 | 301/301 × 3 | PASS |
| 8 | P11 regression | 417/417 × 3 | 417/417 × 3 | PASS |
| 9 | Additional core tests | 20/20 × 3, none unclassified | 20/20 × 3, 0 unclassified | PASS |
| 10 | Inventory arithmetic | 301 + 417 + 20 = 738 | 738 registered | PASS |
| 11 | Failure paths and atomicity | all pass | 124/124 | PASS |
| 12 | Reference-model repeat runs | 5 rounds, 2 presets | 350 × 2 = 700 passes | PASS |
| 13 | Fresh processes | 2 per preset, identical | 6 runs, one MD5 | PASS |
| 14 | Cross-configuration | identical structure and values | 0 relative, 0 mm | PASS |
| 15 | Persistence | round trip compared | 20 save/load, 22 undo/redo | PASS |
| 16 | STEP/STL | read back and measured | 43 export tests | PASS |
| 17 | CLI smoke | real executable | 27 ok + 1 intended failure | PASS |
| 18 | Evidence audit | 10 milestones documented | 171 files, 0 placeholders | PASS |
| 19 | Warning audit | method stated, 0 warnings | 0 compiler, 0 CMake, 0 other | PASS |
| 20 | Revision integrity | no source change during run | clean tree, 21 steps exit 0 | PASS |

**20 gates, 20 PASS, 0 FAIL, 0 BLOCKED.**

The evidence audit (gate 18) covers all ten P11 milestones. Each has a
complete evidence directory in the repository, with no unfilled
placeholders and a stated result:

| Milestone | Evidence files | Result |
| --- | --- | --- |
| P11-FEAT-001 Revolve | 12 | PASS |
| P11-FEAT-002 Chamfer | 16 | PASS |
| P11-FEAT-003 Fillet | 15 | PASS |
| P11-FEAT-004 Hole | 16 | PASS |
| P11-FEAT-005 Linear pattern | 16 | PASS |
| P11-FEAT-006 Circular pattern | 16 | PASS |
| P11-FEAT-007 Mirror | 17 | PASS |
| P11-FEAT-008 Sweep | 19 | PASS |
| P11-FEAT-009 Loft | 19 | PASS |
| P11-REF-001 Reference models | 25 | PASS |

All 171 files are tracked in git, and `TODO.md` records 171 checked P11
items with none unchecked.

## Final Result

**PASS.** Revision `79dab04` is qualified as BetterCAD's production part
modelling system.

Twenty gates, twenty passes, no failure and nothing blocked. Three clean
builds from an empty tree — unoptimised static, optimised static and
shared-library — each compiling all 274 translation units with 22 warning
flags and `-Werror` and producing **no diagnostic at all**, each running the
**complete** 738-test suite to 738 passes. The P0–P10 core that P11 was
built on still passes all 301 of its own tests, unchanged, in every
configuration.

What the system can now do, with evidence rather than assertion: build real
mechanical parts — a stepped shaft, a bolted flange, a V-belt pulley, a
pillow block, an L bracket and a U-bolt — from parameters, sketches and
constraints through nine parametric features; regenerate them when a
dimension changes and return them exactly when it is restored; save, load
and reproduce them bit for bit; export them to STEP and STL that read back
as the same geometry; and refuse invalid input with a diagnostic that says
what failed and why, leaving the document untouched. Every dimension of
every part agrees with geometry computed independently from its parameters,
at every step of every feature chain, to double-precision rounding.

The determinism result is the one worth keeping: six independent processes
across three build configurations, plus a separate qualification run hours
earlier, produced one identical digest of every model's items, IDs,
topology, volumes, areas, centroids and bounds. Not "within tolerance" —
identical.

The system's honesty held up too. Nothing was special-cased to make a model
pass, no test was weakened and no tolerance was relaxed; the two limitations
the reference models exposed are documented and pinned by regression tests
rather than worked around; the compile-time refusals still make unit and
identity errors impossible to write. Where this qualification cannot speak —
one platform, no CI, no sanitizers or coverage, no GUI — it says so above
instead of implying otherwise.

P11 is complete. BetterCAD has a parametric CAD core that can be trusted to
build the same part twice.
