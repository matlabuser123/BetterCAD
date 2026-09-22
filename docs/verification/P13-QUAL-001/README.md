# P13-QUAL-001 — P13 Phase Qualification

```text
STATUS:          PASS — P13 QUALIFIED
SCOPE:           prove that the whole of P13 — assemblies — holds together on
                 one frozen tree: integrated, independently validated,
                 adversarially reviewed, deterministic within its documented
                 contracts, regression clean, and qualified on the tree that
                 is committed
FROZEN REVISION: 9cd2330f2c99bbf1e9cd48c7dcd1b81c5bf3c6cf
BUILD:           Debug, Release, Debug-shared — each configured, fully
                 cleaned, rebuilt, proved current, and tested
TESTS:           1674/1674 in each of the three presets, then the whole suite
                 run five more times in Release and in Debug
VALIDATION:      271 further checks against expectations derived on paper,
                 through the real CLI and the real example program, in every
                 preset
WARNINGS:        0 in all three builds, with 22 warning flags and -Werror
EVIDENCE:        this directory
```

This is a qualification milestone. It adds no CAD capability and changes no
executable source: the tree it qualifies is the tree `P13-REFMOD-001`
committed, byte for byte.

## Scope

`P13` was delivered as fifteen milestones, each with its own evidence
directory and its own gates. This milestone does not re-litigate them. It
asks the question none of them could ask alone:

> Does the whole of P13 hold together, on one frozen tree, built and tested
> from clean, three ways — and is what the documentation says about it true?

The second half of that question is where this milestone found everything it
found. The code came through clean. The documents around it did not.

## Frozen revision

```text
HEAD                9cd2330f2c99bbf1e9cd48c7dcd1b81c5bf3c6cf
origin/main         9cd2330f2c99bbf1e9cd48c7dcd1b81c5bf3c6cf
working tree        clean (git status --short empty)
git diff --check    clean
```

Source and test tree identity, from a scratch index, recorded **before the
first build** and again **after the last test run** by `qualify.cmd` itself:

```text
apps              8da209723f46d3e0194750fcaad4ee2981af723c
include           da4a989538fdfc7dbd8641540993d31dc30be5e3
src               6fc8350a9c8ab6acfe760f509e6b35ab58eb7068
tests             d12ca6408e4876b2c75b3d8bc518fb72099f5b40
examples          d0d2ae4277ba99b46ff1384725292deb3519c199
cmake             a84e909339b24bc7ffca591888e10d48a3e5296c
CMakeLists.txt    a0adbb9c1d3583aab4e294fd41837537e53e4764
CMakePresets.json 951b53d6b7412e6057c188dd41e40cb9e57c6aa3
```

The two recordings are identical (`qualification/qualification-times.txt`,
first and last blocks), and they are also identical to the eight values
`P13-REFMOD-001` recorded — so this qualification ran on exactly the tree the
last implementation milestone qualified, and nothing moved underneath it
while it ran.

They were recomputed a third time after this milestone's documentation was
written, and are unchanged. See "Qualified tree".

## Build environment

```text
BetterCAD         0.1.0 (revision 9cd2330f2c99, reported by the binary)
CMake             4.4.2 (Ninja 1.13.2)
C++ compiler      GNU 16.1.0 (MinGW-W64 ucrt, POSIX threads, SEH)
C++ standard      C++23 (__cplusplus=202302)
Target platform   Windows AMD64 (Windows 11 Pro 10.0.26200)
Kernel            OCCT (via BetterCAD::geometry adapters)
Qt                6.11.2
Tests             Catch2 3.16.0 (fetched)
Warnings          22 flags, warnings as errors (-Werror)
```

The revision the executable reports is itself evidence: `bettercad-cli
version` prints `revision : 9cd2330f2c99`, which is the qualification
candidate. The binaries under test were built from the frozen commit, not
from something older that happened to be lying in the build directory.

## Build results

Each preset was configured, then **fully cleaned** (`ninja -t clean`), then
rebuilt. A preset's tests ran only after its build returned 0.

| Preset | Configure | Clean | Build | TUs | Links | Warnings |
| --- | --- | --- | --- | --- | --- | --- |
| `debug` | exit 0 | 454 files removed | exit 0 | 437 | 16 | 0 |
| `release` | exit 0 | 454 files removed | exit 0 | 437 | 16 | 0 |
| `debug-shared` | exit 0 | 460 files removed | exit 0 | 437 | 16 | 0 |

### The binaries under test are the ones just built

`P13-XFORM-001` recorded an incident in which a stale executable was reported
as a pass: a link failed, the previous binary stayed on disk, and a later
test run reported its results as evidence for two tests that were not in it.
The per-milestone scripts fixed the half of that which lets a *failed* build
reach `ctest`. They never positively proved freshness.

This milestone adds that proof. After each build and before each `ctest`,
`qualify.cmd` runs the build again and logs the result:

| Preset | No-op rebuild | Compiles | Links |
| --- | --- | --- | --- |
| `debug` | exit 0, 0.71 s | 0 | 0 |
| `release` | exit 0, 0.60 s | 0 | 0 |
| `debug-shared` | exit 0, 0.65 s | 0 | 0 |

Nothing was left to do, in any preset. The only step the no-op rebuild
performs is `[1/8] Checking git revision`, which is the stamping target.
Logs: `qualification/rebuild-*.log`.

## Test results

| Preset | Tests | Result | Time |
| --- | --- | --- | --- |
| `debug` | 1674 | **100% passed** | 230.90 s |
| `release` | 1674 | **100% passed** | 181.18 s |
| `debug-shared` | 1674 | **100% passed** | 218.32 s |

Composition of the 1674, counted from the `ctest` log rather than assumed:

```text
1553  unit.*          Catch2 cases, in process
  96  cli.*           process tests that launch the real executable
  17  compile_fail.*  builds that must fail to compile
   7  architecture.*  layering, containment, public/private split
   1  gui.*
```

Of those, **382 carry the `[p13]` tag**, across 23 files — the assembly model,
placements, references, mates, the solver and its analytic derivatives,
mechanical joints, configurations, stable references, regeneration, commands,
persistence, STEP and the CLI.

Every qualification stage's real exit code is recorded, and the build's exit
code gates the tests rather than being assumed:

```text
17 stages with a recorded exit code
17 of them 0
 0 skipped
```

Logs: `qualification/configure-*.log`, `clean-*.log`, `build-*.log`,
`rebuild-*.log`, `ctest-*.log`; stage timings in `qualification-times.txt`.

## Warning audit

The build logs were searched, not inferred from exit codes:

```text
build-debug.log         0 lines containing "warning"
build-release.log       0 lines containing "warning"
build-debug-shared.log  0 lines containing "warning"
```

Zero, literally, in all three. The single line matching "error" in each log is
the filename `Error.cpp.obj`.

This is cleaner than any previous P13 milestone, each of which carried one
`ninja: warning: premature end of file; recovering` in `build-debug.log` —
ninja repairing its own truncated log, not a compiler diagnostic. It is absent
here because each preset's build directory was configured fresh.

**0 unexpected compiler warnings.** With `-Werror` and 22 warning flags
(`-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow
-Wold-style-cast -Wcast-align -Wdouble-promotion -Wduplicated-branches
-Wduplicated-cond -Wextra-semi -Wformat=2 -Wimplicit-fallthrough -Wlogical-op
-Wmisleading-indentation -Wmissing-declarations -Wnon-virtual-dtor
-Woverloaded-virtual -Wsuggest-override -Wundef -Wunused`), a warning would
have failed the build anyway; the logs are checked so that the claim rests on
a measurement rather than on that argument.

## Determinism

The **whole** suite was re-run with `--repeat until-fail:5` — every test up to
five times — in two presets. A phase qualification has no reason to repeat a
chosen subset.

| Repeat | Tests | Result | Time |
| --- | --- | --- | --- |
| `release`, `--repeat until-fail:5` | 1674 | **100% passed** | 866.31 s |
| `debug`, `--repeat until-fail:5` | 1674 | **100% passed** | 901.44 s |

Beyond the suite, the determinism contract was measured directly, per
subsystem, and stated exactly rather than claimed as blanket byte identity:

| Subsystem | Contract | Measured |
| --- | --- | --- |
| Solver | status, unknowns, equations, DOF, iterations, residual and every transform identical between runs and between presets | **byte-identical** CLI `solve` output for all 8 models across `debug`, `release`, `debug-shared` |
| `.bcad` save | byte-reproducible | all 8 committed models byte-identical to a fresh build of them, twice over |
| Reference models | same answer from a fresh process | two runs of the example program agree byte for byte on all 8 |
| CLI | exit codes and stdout stable | three consecutive runs agree byte for byte |
| Configuration switching | A → B → A returns to A exactly | `solve` output byte-identical before and after the round trip |
| STEP | **not** whole-file byte identity | the header carries a wall-clock timestamp and the output filename, and OCCT numbers `NEXT_ASSEMBLY_USAGE_OCCURRENCE` from a process-global counter. Everything else — every entity number, coordinate and name — is byte-identical, which is what `AssemblyStep_RepeatedExportsAgreeByteForByteExceptTheKernelCounter` asserts |

The STEP row is the one place a byte-identity claim would be false, and it is
stated as false rather than quietly dropped.

Logs: `qualification/ctest-repeat-*.log`, `cross-preset.log`,
`reference-models-*.log`, `end-to-end-*.log`.

## Cross-preset agreement

`cross-preset.py` runs the real CLI from each of the three built presets over
every committed assembly model and compares exit codes and stdout.

```text
67/67 checks passed
```

Debug, Release and Debug-shared return the same exit code and the same bytes
for `solve` and `status` on all eight models. Since the `solve` line carries
the iteration count, the largest residual and every component's solved
position, that is a strong claim: the three builds took the same number of
Gauss-Newton steps and landed on the same doubles.

The two other validation suites were run in all three presets and their
check-by-check results are identical across them:

```text
end-to-end.py        121/121 in debug, release and debug-shared — identical
reference-models.py   83/83 in debug, release and debug-shared — identical
```

"Debug and Release disagreeing" was on the adversarial list. They do not.

## Reference models

The committed suite is eight assemblies, `RM-A` … `RM-H`
(`examples/models/reference/assembly_*.bcad`).

`reference-models.py` runs the example program in a **fresh process**, writes
every model to a scratch directory, and compares each with the committed file.

```text
83/83 checks passed, in each of the three presets
```

| Check | Result |
| --- | --- |
| Every committed model is byte-identical to what its builder produces now | 8/8 |
| Two runs of the builder agree byte for byte | 8/8 |
| Solve status matches the paper derivation | 8/8 |
| Unknowns match | 8/8 |
| Equations match | 8/8 |
| Degrees of freedom match | 8/8 |

**The expected numbers are independent of the solver.** They were re-derived
for this milestone from the mate equation counts that `P13-SOLVE-001` and
`P13-MATE-002` qualified, before any output was read:

```text
Fixed grounds a component: removes its 6 unknowns, adds no equation
Distance 1  Perpendicular 1  Angle 1  Parallel 2  Coincident 3
Planar 3    Concentric 4     Cylindrical 4        Revolute 5   Slider 5

unknowns = 6 x (active components - grounded)
DOF      = unknowns - rank of the constraint Jacobian
```

| Model | What it covers | Comps | Mates | Unknowns | Equations | DOF | Status |
| --- | --- | --- | --- | --- | --- | --- | --- |
| RM-A | grounded pair | 2 | 2 | 0 | 0 | 0 | FULLY_CONSTRAINED |
| RM-B | fully constrained, repeated instances | 3 | 11 | 12 | 12 | 0 | FULLY_CONSTRAINED |
| RM-C | under-constrained (slide + turn) | 2 | 2 | 6 | 4 | 2 | UNDER_CONSTRAINED |
| RM-D | all four mechanical joints | 5 | 5 | 24 | 17 | 7 | UNDER_CONSTRAINED |
| RM-E | configurations / suppression | 4 | 16 | 18 | 18 | 0 | FULLY_CONSTRAINED |
| RM-F | stable reference, parameter-driven | 2 | 5 | 6 | 6 | 0 | FULLY_CONSTRAINED |
| RM-G | production scale | 8 | 31 | 42 | 40 | 2 | UNDER_CONSTRAINED |
| RM-H | committed **broken** | 2 | 3 | 6 | 2 | **5** | INCONSISTENT |

Worked through: RM-D is 6 × (5 − 1) = 24 unknowns against revolute 5 + slider
5 + cylindrical 4 + planar 3 = 17, leaving 7 freedoms that decompose as
1 + 1 + 2 + 3 — one per joint, in the freedom each joint is named for. RM-G is
6 × (8 − 1) = 42 against cover 6 + cylindrical 4 + rear shaft 6 + four feet at
6 = 40, leaving the output shaft's slide and turn.

RM-H is the row worth reading twice. Its two `Distance` mates constrain the
**same pair of planes** at different distances, so their Jacobian rows are
identical: the rank is 1, not 2, and the DOF is 6 − 1 = **5**, not 6 − 2 = 4.
It is the one model for which `DOF = unknowns − equations` does not hold, and
the measured value agrees with the rank argument. See finding **F-8**.

## Persistence

| Check | Where | Result |
| --- | --- | --- |
| construct → regenerate → solve → save → destroy → load → regenerate → solve → compare | `AssemblyReference_SurvivesSaveLoadRegenerateSolve`, on the **committed** files | PASS |
| Re-saving a loaded model reproduces the committed bytes | same test, `readFile(again) == readFile(committed)` | PASS |
| Derived transforms recomputed identically after a round trip | same test | PASS |
| 27 persistence cases: components, placements, all basic and mechanical mates, configurations, suppression, stable references, unresolved references | `tests/assembly/PersistenceTests.cpp` | 27/27 |
| No derived state reaches the file | `Persist_NoDerivedStateReachesTheFile` — saves, solves, saves again, asserts byte equality | PASS |
| A failed save leaves no partial file | `Persist_AFailedSaveLeavesNoPartialFile` | PASS |

Independently of the suite, this milestone parsed all eight committed
assemblies and confirmed the file holds **intent only**:

```text
8/8 models: no "transform" and no "solved" key anywhere in the document
8/8 models: every component's part is a bare local ObjectId
0/8 models: any absolute path, user name or machine-specific string
```

The writer's key set is a whitelist (`requireObject(data, path, {"part",
"suppressed", "placement"})`), so an unknown key is a parse error rather than
something that could quietly appear.

## Solver and DOF classification

| Class | Represented by | Result |
| --- | --- | --- |
| Fully constrained | RM-A, RM-B, RM-E, RM-F | PASS, DOF 0, residual < 1e-9 m |
| Under-constrained | RM-C, RM-D, RM-G | PASS, DOF 2, 7, 2 — each matching the paper derivation |
| Inconsistent | RM-H | PASS — conflicting mates named (`Near`, `Far`), nothing published |
| Over-constrained | `MechanicalSolveTests` (a slider whose roll reference lies along its slide axis) | PASS — reported redundant, not silently accepted |
| Solver failure | `SolverTests` | PASS — kept distinct from Inconsistent |

The five states are never collapsed to a boolean: `SolveStatus` has exactly
the five, `solved()` is true only for the two that produced an answer, and the
CLI's exit code follows it.

Supporting evidence: 30 cases in `SolverTests.cpp`, 24 in
`MechanicalSolveTests.cpp`, 3 in `SolverDerivativeTests.cpp` (the analytic
Jacobian against finite differences, including all four joints), 15 in
`MechanicalMateTests.cpp`.

**No silent grounding, no canonical mutation.** `assembly::solve()` takes a
`const Document&`. The solver cannot write to the document even by mistake —
the type system forbids it — and `Regeneration_LeavesCanonicalIntentAlone`
pins the behaviour from the outside as well.

## Configuration and suppression

`A → B → A` was exercised through the real CLI on RM-G, which has two
configurations:

| Check | Result |
| --- | --- |
| `status` runs in both configurations | PASS |
| `Bare` puts fewer objects in force than `Assembled` | PASS |
| `Bare` suppresses its five components | PASS |
| `Assembled → Bare → Assembled` returns the same exit code | PASS |
| `Assembled → Bare → Assembled` returns **byte-identical** solve output | PASS |
| A suppressed component is measurably absent from the STEP export | PASS — 3 occurrences, not 8 |

No drift and no stale state survives switching. In the suite, 25 cases in
`ConfigurationTests.cpp` cover the same ground, including
`Configuration_SwitchingThereAndBackRestoresTheSameStateExactly`,
`Configuration_SwitchingNeverTouchesCanonicalState` and
`Regeneration_NoStaleSolveLeaksBetweenConfigurations`.

ADR-007 holds structurally: suppression overrides live on the existing
`Configuration`, there is one `ConfigurationId` and one active configuration,
and `AssemblyConfigurationId` exists nowhere in the codebase except a comment
recording that it was rejected.

## Stable references and recovery

The failure this part of P13 exists to defeat is **a mate that still solves,
on the wrong face** — the only reference failure that does not announce
itself.

| Scenario | Test | Result |
| --- | --- | --- |
| Upstream geometry changes; the same intended geometry resolves | `StableReference_AFaceTargetFollowsItsFeatureThroughARegeneration` | PASS |
| The target disappears → explicit unresolved | `StableReference_AFaceTargetBecomesUnresolvedWhenItsFeatureIsGone` | PASS |
| The target returns → the same intended target resolves again | `StableReference_RecoversWhenTheIntendedTargetReturns` | PASS |
| **A geometrically identical face takes its place** | `StableReference_NeverRebindsToASimilarFaceThatTookItsPlace` | PASS |

The fourth is the one that matters. The test adds a second part whose end cap
is at exactly the plane the original occupied, removes the original, and then
asserts not merely that something is unresolved but **which feature the mate
still names** — that it did not acquire the identical face next door. A test
that only checked "resolved" would pass on the broken implementation.

Underneath, `features` resolves a face **by name only**: "when no face carries
the name, the reference fails, and no other face is taken because it lies
where the named face used to be". A search of `src/assembly`,
`include/bettercad/assembly`, `src/features`, `src/io` and the CLI for
`nearest|closest|fallback|best match|approximate|fuzzy|heuristic` finds no
resolution path with a second choice.

## Regeneration and failure propagation

| Trigger | Result |
| --- | --- |
| Component change | re-solves (`ObjectChanged`) |
| Mate change | re-solves (`ObjectChanged`) |
| Configuration change | re-solves (`ConfigurationChanged`) |
| A configuration overriding a **free parameter** | re-solves (`PlacementChanged`) — the case a revision-based trigger would miss, because the base value never changes |
| Suppression change | re-solves (`ComponentsInForceChanged` / `MatesInForceChanged`) |
| Nothing the solve reads moved | does **not** re-solve (`NotNeeded`) |
| An unrelated change | does **not** re-solve |
| Dependency failure, cycle, unresolved mate target | publishes **nothing**, and says which object is responsible |
| Repair | recovers |

22 cases in `RegenerationTests.cpp`. The all-or-nothing rule is explicit in
the implementation and in the tests: a broken assembly publishes no
transforms, "not a partial set and not the previous one, because a transform
that is one edit out of date still renders, which makes it worse than
absent". `Regenerator::regenerate()` clears `transforms_` before any final
pass runs, so a stale transform cannot survive a pass.

## Undo and redo

17 cases in `CommandTests.cpp`. The gate's realistic sequence is
`Command_AMultiStepHistoryRewindsAndReplaysExactly`: four edits — component
create, placement edit, mate create, suppression — then unwound one at a time
with canonical state checked at **every** position, then replayed to the top
and compared.

| Requirement | Test |
| --- | --- |
| Component create/delete | `Command_CreateComponentUndoAndRedoKeepTheSameIdentity` |
| Placement edit | `Command_PlacementEditUndoesAndRedoesExactly` |
| Mate create/edit/delete | `Command_MateCreateEditAndUndoPreserveEverythingAboutIt` |
| Mechanical mate edit | `Command_AMechanicalMateSurvivesUndoAndRedoWithItsRollReference` |
| Suppression | `Command_SuppressionUndoesToTheExactPriorState` |
| Suppression changes solver participation | `Command_SuppressionUndoChangesSolverParticipation` |
| Redo invalidation | `Command_ADivergentEditClearsTheRedoStack` |
| Failed-command history semantics | `Command_AFailedCommandNeverEntersHistory` |
| Stable references across undo | `Command_AMateStillNamesItsComponentAfterTheComponentIsUndoneAndRedone` |
| Determinism | `Command_TheSameSequenceGivesTheSameDocument` |
| Placement edit stores intent, not the solved position | `Command_PlacementEditStoresIntentAndNotTheSolvedPosition` |

"Undo restoring objects but not configuration overrides" was on the
adversarial list. It was a real defect, found and fixed in `P13-CMD-001`:
`DeleteObjectCommand` captures `configurationOverridesFor(id)` **before** the
removal that clears them and restores them after re-inserting the object,
pinned by `Command_UndoingADeleteRestoresTheConfigurationOverridesItCleared`.

## CLI end-to-end

`end-to-end.py` drives the **real `bettercad-cli` process** against the
**real committed files**, one fresh process per step.

```text
121/121 checks passed, in each of the three presets
```

| Workflow | Result |
| --- | --- |
| `solve` on all 8 models: exit code, status, DOF against the paper table | PASS |
| `status` on all 8: component and mate counts | PASS |
| load → `solve` → `component-place` → `solve` → reload in a fresh process | PASS |
| An edit naming a missing component fails **and leaves the file byte-identical** | PASS |
| A batch failing at line 2 exits non-zero **and leaves the file byte-identical** | PASS |
| `configuration-activate`, then export | PASS |
| An unsolvable assembly is a non-zero exit with its conflicting mates named | PASS |
| Three consecutive runs agree byte for byte | PASS |

The exit-code contract holds end to end: `0` solved, `1` the command ran and
reported a problem, `2` the command line was invalid. RM-H returns `1` from
both `solve` and `status`, naming `Near` and `Far`.

Two incidental confirmations, from checks that failed on the first pass
because **the script** was wrong and the CLI was right:

- `status` on RM-H exits `1`, not `0`. The exit code follows the document, not
  the command's ability to print a report. The script had assumed otherwise.
- `component-place <file> Shaft` was refused with `Shaft (object:9) has type
  'extrude'; this takes a component`. `Shaft` is the extrude feature; the
  component is `ShaftPin`. The selector refused to coerce one into the other
  rather than guessing.

"CLI behavior differing from core behavior" was on the adversarial list. The
CLI has no assembly implementation of its own — it calls the same public API —
and `AssemblyCli_BuildsTheSameAssemblyAsTheCoreApi` pins that. Independently,
the DOF and status this milestone measured through the CLI agree with the
values derived on paper and with the example program's in-process results.

## STEP export and read-back

| Model | Components | `NEXT_ASSEMBLY_USAGE_OCCURRENCE` in the file | Solid geometry |
| --- | --- | --- | --- |
| RM-A | 2 | 2 | yes |
| RM-B | 3 | 3 | yes |
| RM-C | 2 | 2 | yes |
| RM-D | 5 | 5 | yes |
| RM-E | 4 | 4 | yes |
| RM-F | 2 | 2 | yes |
| RM-G | 8 | 8 | yes |
| RM-H | — | **no file written** | — |
| RM-G in `Bare` | 3 in force, 5 suppressed | **3** | yes |

Counted from the STEP text itself, not from anything BetterCAD reported about
it. Two rows carry the weight:

- **RM-H writes nothing.** An assembly that does not solve has no honest
  placement to write, and `export-step` exits non-zero rather than exporting
  intent positions that were never solved.
- **RM-G in `Bare` holds 3, not 8.** A suppressed component is measurably
  absent from the export. "Suppressed component leaking into solve/export" was
  on the adversarial list; it does not.

"STEP exporting stale solved state" was also on the list. It cannot:
`io::exportStep()` clones the document and runs `regenerateAll()` — a full
rebuild, not an incremental one — and then refuses to write any component for
which the pass published no transform.

In the suite, 34 cases in `tests/io/AssemblyStepTests.cpp` read the file back
through a reader that shares no code with the writer, checking product and
instance counts separately, placements against bounds derived on paper, and
totals against closed forms.

## Milestone audit

Every P13 milestone was audited against its own evidence, its logs, and the
current tree. All 17 commit hashes quoted across the fifteen directories
resolve, and every recorded per-directory tree ID matches the implementation
commit it claims.

| Milestone | TODO | Evidence | Gate | Limitations | Verdict |
| --- | --- | --- | --- | --- | --- |
| `P13-ARCH-001` | `[x]` | complete, design-only | PASS | 4, current | **PASS** |
| `P13-COMP-001` | `[x]` | complete, 17 logs | 1282/1282 ×3 | 5, current | **PASS** (D-1) |
| `P13-XFORM-001` | `[x]` | complete, 17 logs | 1301/1301 ×3 | 5, current | **PASS** |
| `P13-REF-001` | `[x]` | complete, 17 logs | 1323/1323 ×3 | 6, current | **PASS** |
| `P13-MATE-001` | `[x]` | complete, 17 logs | 1349/1349 ×3 | 6, current | **PASS** |
| `P13-SOLVE-001` | `[x]` | complete, 18 logs | 1382/1382 ×3 | 9, current | **PASS** (F-4) |
| `P13-MATE-002` | `[x]` | complete, 17 logs | 1421/1421 ×3 | 5, current | **PASS** (F-7) |
| `P13-CONF-001` | `[x]` | complete, 17 logs | 1446/1446 ×3 | 5, current | **PASS** (F-6, F-7) |
| `P13-STREF-001` | `[x]` | complete, 17 logs | 1461/1461 ×3 | 5, current | **PASS** (F-6, F-7) |
| `P13-REGEN-001` | `[x]` | complete, 17 logs | 1483/1483 ×3 | 6, one superseded | **PASS** (F-6, F-7, F-9) |
| `P13-CMD-001` | `[x]` | complete, 17 logs | 1500/1500 ×3 | 5, current | **PASS** (F-6, F-7) |
| `P13-PERSIST-001` | `[x]` | complete, 17 logs | 1527/1527 ×3 | 5, one superseded | **PASS** (F-6, F-9) |
| `P13-CLI-001` | `[x]` | complete, 17 logs | 1595/1595 ×3 | 7, current | **PASS** (F-5, F-6) |
| `P13-STEP-001` | `[x]` | complete, 17 logs | 1631/1631 ×3 | 8, one superseded | **PASS** (F-9) |
| `P13-REFMOD-001` | `[x]` | complete, 17 logs | 1674/1674 ×3 | 6, current | **PASS** (F-8) |

No cited log file is missing. No `***Failed`, `***Not Run`, `***Skipped`,
`***Timeout` or `***Exception` appears in any P13 `ctest` log. Every
`qualification-times.txt` records exit 0 for every stage.

The test-count chain verifies end to end against the source tree, which is
what makes the counts auditable rather than asserted:

```text
1259 (P12) → +23 → 1282 → +19 → 1301 → +22 → 1323 → +26 → 1349
     → +33 → 1382 → +39 → 1421 → +25 → 1446 → +15 → 1461 → +22 → 1483
     → +17 → 1500 → +27 → 1527 → +68 → 1595 → +36 → 1631 → +43 → 1674
```

## TODO audit

```text
unchecked [ ] items in TODO.md   22 — all of them P13-QUAL-001's own checklist
BLOCKED / UNVERIFIED / FIXME     0 in TODO.md, ROADMAP.md, ARCHITECTURE.md
TODO / HACK / WORKAROUND         0 in src/, include/, apps/
NotImplemented / stub paths      0 in the assembly stack
```

Every P13 milestone before this one is `[x]` with an evidence directory. The
one `not implemented yet` string in the repository is the Qt viewport
placeholder in `apps/bettercad/MainWindow.cpp`, which is the P0-era GUI shell
and outside P13 — the desktop application is `Planned` in `ROADMAP.md`.

## ADR compliance matrix

All P13 ADRs are `Status: Accepted`; none is Proposed, Deprecated or
Superseded. ADR-009 was added during implementation and is included.

| ADR | Invariant | Implementation | Pinned by | Verdict |
| --- | --- | --- | --- | --- |
| **002** Assemblies live in the Document | Components and mates are `DocumentObject` kinds; the file format gains no top-level key | `core/Id.hpp` tag types; `io/json/DocumentJson.cpp` envelope unchanged | `ComponentFile_UsesTheExistingObjectEnvelopeAndNoNewKey`, `Persist_APreAssemblyFileStillLoadsAndHasNoAssembly`, 2 compile-fail cases | **PASS** |
| **003** Internal references first | A component names a part within its own document; no silent rebinding | `ObjectReference::localTarget()`; component handler fails an unresolvable part | `Reference_NeverRebindsToAnObjectThatTookTheTargetsPlace`, `Component_WhosePartIsDeletedFailsAndSaysSo` | **PASS on substance**; deferral clause overtaken — see **F-3** |
| **004** Mates reference semantic geometry only | No `FaceSignature` target; a removed face fails loudly with no substitute | `MateTarget` holds only `PlaneReference`/`AxisReference`/`FaceName` — structurally impossible, stronger than validation | `Mate_WhoseTargetIsDeletedBecomesUnresolvedAndIsNeverRebound`, `StableReference_NeverRebindsToASimilarFaceThatTookItsPlace` | **PASS on substance**; the exclusion itself is unpinned — see **F-11** |
| **005** Placement is intent, transforms derived | The solved transform is never persisted | `solve()` takes `const Document&`; the serializer whitelists `part`/`suppressed`/`placement`; resolution is a pure function with no store | `Persist_NoDerivedStateReachesTheFile`, `PlacementFile_HoldsIntentAndNeverASolvedTransform`, `Solve_WritesNothingDerivedIntoTheSavedFile` | **PASS** |
| **006** `assembly` at layer 3, `io` up | `core` 0, `sketch` 1, `features` 2, `assembly` 3, `io` 4, `renderer`/`scripting` 5 | `tests/architecture/CheckLayering.cmake:23-32`; `assembly` includes only `assembly`, `core`, `features` | `architecture.layering`, over the whole tree | **PASS on code**; the two doc updates the ADR mandated were missing — see **F-1**, **F-2** |
| **007** One configuration system | One `Configuration`, one `ConfigurationId`, one active configuration | Suppression overrides on the existing `Configuration`; `Document::removeObject` calls `forgetObject` | `Configuration_SwitchingThereAndBackRestoresTheSameStateExactly`, `Configuration_ForgetsAnObjectThatIsDeleted` | **PASS** |
| **008** The solve is a document-level final pass | Runs after the objects; a broken assembly publishes nothing | `Regenerator::registerFinalPass`; `transforms_.clear()` before any pass | `Regeneration_SolvesTheAssemblyAndPublishesItsTransforms`, `Regeneration_DropsTransformsWhenAComponentsPartFails`, `Regeneration_ReSolvesWhenAConfigurationOverridesAFreeParameter` | **PASS** |
| **009** A CLI edit is a document transaction | Nothing is written until every edit has succeeded | One `EditApply` table; single-shot is the batch of one; `saveDocument` only after the loop | `AssemblyCli_BatchThatFailsWritesNothingAtAll`, `AssemblyCli_SingleShotAndBatchProduceTheSameDocument` | **PASS** |

Independently re-measured for this milestone: `assembly` reaches only
`assembly`, `core` and `features`; `core`, `sketch` and `features` include a
`bettercad/assembly` header **zero** times; `io` reaches `assembly` as the
layering intends; and there is no `occt/` directory under `src/assembly/`.

## Adversarial review

P13 was reviewed as one system rather than milestone by milestone, working
from the integration-failure list the milestone brief names.

### Production defects found: 0

Each item was probed against the code, not accepted on the strength of a
passing suite.

| Integration failure | Finding |
| --- | --- |
| Derived state becoming persistent canonical state | **Cannot happen.** `solve()` takes `const Document&`; the serializer whitelists three keys; the resolved transform is a function with no store. Three tests pin it by grepping the saved bytes. |
| STEP exporting stale solved state | **Cannot happen.** The exporter clones and runs `regenerateAll()`, then refuses any component with no published transform. |
| A configuration change failing to trigger regeneration | **Covered.** The trigger compares *resolved* placements, which is what catches a configuration overriding a free parameter — a change that alters no object's revision. |
| A reference resolving but to the wrong geometry | **Covered**, by the strongest test in the phase: an identical face at the identical plane is offered and refused. |
| Undo restoring objects but not configuration overrides | **Was real; fixed in P13-CMD-001** with capture-and-restore and a regression test. |
| Suppressed component leaking into solve or export | **Does not.** Filtered at the solver and the exporter; measured at 3-of-8 in the STEP file. |
| Solver succeeding with wrong DOF | **Independently checked.** All eight models' DOF re-derived on paper here and matched, including RM-H's rank-deficient case. |
| Tests using the implementation as their oracle | **They do not.** The reference expectations are arithmetic on the mate equation table; this milestone re-derived them from scratch and agrees. |
| CLI behaviour differing from core | **Does not.** Same public API, pinned by a test, and independently cross-checked here. |
| Debug and Release disagreeing | **They agree byte for byte**, including Debug-shared. |
| Shared-build linkage gaps | **None.** Debug-shared configures, links and passes 1674/1674. |
| Absolute paths leaking into committed models | **None** in any of the 8. |
| Stale binaries producing false green runs | **Disproved positively**: the no-op rebuild compiled 0 and linked 0 in every preset. |
| A failed solve leaving partial state committed | **Does not.** A failed solve returns no transforms at all, and RM-H is committed broken to prove it end to end. |

### Findings: 13, all documentation

None changes a result, a measurement or a PASS. Three were fixed in this
milestone; the rest are recorded here. **F-1**, **F-2** and **F-3** were fixed
because they concern *living* documents that are wrong about the system as it
stands today. **F-4** to **F-13** concern *historical* milestone evidence,
whose raw logs are correct and complete; those are recorded rather than
rewritten, because evidence is a record of what was run, and correcting the
record belongs in the audit rather than in the history.

| # | Finding | Severity | Disposition |
| --- | --- | --- | --- |
| **F-1** | `ARCHITECTURE.md` published the pre-ADR-006 layer table with no `assembly` row and `io` at 3; said `src/assembly/` was "not yet created"; said the new table "is **not yet in force**". All false since `P13-COMP-001`. ADR-006's Consequences name `ARCHITECTURE.md:80` as a must-update. | **High** — an authority document wrong about the current structure | **Fixed** |
| **F-2** | `docs/architecture.md:36` carried the same stale table and listed no `bettercad_assembly` target; the file mentioned assemblies zero times. ADR-006 names `docs/architecture.md:35` too. | **High** | **Fixed** |
| **F-3** | ADR-003 says "nothing in `P13` implements it, and no placeholder type, field or key for it is added", with the P13 criterion "no persisted field exists for an external source". `P13-REF-001` — authorized by `TODO.md` — built exactly that, with persisted `document` and `hint` keys. The ADR was never amended. | **Medium** — governance. `TODO.md` outranks an ADR, so the work was authorized; the fields are implemented and tested, not placeholders; and no committed model uses them, so the decision still describes what ships. | **Fixed** — amendment added to ADR-003 |
| **F-4** | `P13-SOLVE-001/README.md:494` names `Solve_ParallelTurnsTheNearWayRoundNotTheFarWay`. No such test exists, or ever did. The real cases are `Solve_ParallelFromANearStartTurnsBackTheWayItCame` and `Solve_ParallelAcceptsEitherSenseAndTheBranchIsNotGuaranteed`, both present and both covering what the text describes. | **Medium** — the one milestone where a Jacobian error would be silent | Recorded |
| **F-5** | `P13-CLI-001/README.md:236-240` states "14 new process tests" and "1527 to 1597". Those are the **void** run's figures, which the same document later discards; the qualified figures are 12 and 1595, and the logs agree. | Low | Recorded |
| **F-6** | Eight milestones say the ninja warning "heads **each** build log". Measured: it appears only in `build-debug.log`; `build-release.log` and `build-debug-shared.log` contain zero. The substantive claim — not a compiler diagnostic, 0 compiler warnings — is correct everywhere. | Low | Recorded |
| **F-7** | Five milestones claim "fifteen" qualification stage exit codes. There are **fourteen** in those runs. All are 0; the count is wrong, not the result. | Low | Recorded |
| **F-8** | `P13-REFMOD-001/README.md:110` asserts `DOF = unknowns − equations` "here", then prints RM-H as 6, 2, 5. The value is right (rank 1, not 2) but the rule needs an explicit carve-out. The same overstatement is in the test file's header comment; RM-H's `redundant.empty()` assertion also passes trivially, because the solver returns on `Inconsistent` before rank analysis populates `redundant`. | Low | Recorded |
| **F-9** | `P13-PERSIST-001` and `P13-STEP-001` both rest arguments on "all 24 committed models … none contains a component". There are now **32** committed models and **8 contain components**, because `P13-REFMOD-001` deliberately added them. `P13-REFMOD-001` handled this in the tests; the two earlier READMEs are unannotated. Likewise `P13-REGEN-001`'s "no viewer or exporter consumes them", which `P13-STEP-001` superseded. | Low | Recorded |
| **F-10** | Four forward promises were never delivered and never re-recorded: the `Document::modifyObject` bypass (`P13-COMP-001` → `P13-REGEN-001`); a resolver supplied from the CLI (`P13-REF-001` → `P13-CLI-001`); mate rebinding/repair (`P13-STREF-001` → `P13-CMD-001`); `info` showing where a component sits (`P13-XFORM-001` → `P13-CLI-001`). | Low — each is a capability gap, not a defect | Recorded; carried to Known limitations |
| **F-11** | ADR-004 requires validation to refuse a `FaceSignature` target "with a distinguishable error". No such error exists, because `MateTarget` has no such member — structurally stronger than the ADR asked. But nothing pins it: adding the member would compile and no test would fail. | Low | Recorded — a compile-fail case is the natural fix |
| **F-12** | `RegenerationReport::succeeded()` is true for a document whose assembly is `Inconsistent`, because the solve outcome is reported separately via `AssemblyRegeneration::status`. | **Not a defect** — designed, and correct: an under-constrained assembly is a normal state, so `succeeded()` must be true there. Both production consumers read `status` (the exporter fails on a null transform; the CLI exits non-zero), and RM-H proves it end to end. The observation is that a *future* caller passing `report = nullptr` would not see it. | Recorded |
| **F-13** | `TODO.md` lists the P13 ADRs as ADR-002 … ADR-008. ADR-009 appears in no project index. | Low | **Fixed** in the TODO closeout |

### What was tried and did not break

The three claims most worth disproving were attacked directly and held: the
serializer writes no derived state under any path found (ADR-005); no
resolution path in the assembly stack has a second choice (ADR-003/004); and
there is no second configuration store anywhere (ADR-007).

## Known limitations

Collected from the fifteen milestones, checked against the current code, and
stated as they stand today. All are within accepted P13 scope as
`TODO.md` records it.

**Scope of an assembly**

- Assemblies operate inside one `Document`. Cross-document dependencies are
  not implemented.
- The external-reference *vocabulary* exists and is tested (`P13-REF-001`);
  cross-document **execution** — regeneration, circularity detection, a
  resolver supplied from the CLI — does not. A component whose part is
  external fails regeneration explicitly rather than appearing to succeed.
- Copying a `.bcad` file duplicates its `DocumentId`; which of two claimants
  resolves is the resolver's choice. Inherent to UUID identity.

**Configurations**

- Configurations are document-global. A component cannot independently select
  a different part configuration.
- Suppression is per object, not per branch: suppressing a component takes its
  mates with it but does not cascade to components positioned only by them.

**Solver**

- Solved transforms are derived state. Opening an assembly costs a solve;
  there is no cached-position fast path, by design.
- An **over-constrained** assembly returns no transforms — a diagnostic and no
  positions. Inherited from `P13-SOLVE-001` and unchanged.
- Convergence is local. The basin measurements cover perturbations to 200 mm
  and 60°; nothing claims convergence from an arbitrary start.
- Which branch a `Parallel`, `Perpendicular` or `Angle` mate reaches is not
  guaranteed near the stationary point of its residual, and a start exactly at
  one is reported `Inconsistent`, which over-claims.
- Redundancy detection is linear and local (Gram-Schmidt at the solution).
- The solve is dense and about cubic in the unknowns: 20 components take
  ~104 ms in Debug. A real ceiling on assembly size, measured, not optimized.
- A mate's own value is a literal; a `Distance` or `Angle` cannot be driven by
  a parameter, though a component's **placement** can.
- The four joints do not distinguish the sense of an axis or a normal.

**Persistence and history**

- There is no schema migration, because no version has been superseded.
- Command history is not persisted: an edit made before a save cannot be
  undone after a reload. There is no command coalescing.
- No cross-version file compatibility is measured; it is argued from the
  schema rules and the unknown-type refusal.

**Export**

- STEP output is **not** byte-reproducible: timestamp and a kernel counter.
  Everything describing the assembly is.
- AP214, not AP242. There is no general STEP **import**; read-back is test
  infrastructure.
- A body no component places is not exported once a document has components.
- STL assemblies are flattened, though instances are still placed
  individually.

**CLI**

- Last writer wins: two processes editing one document will overwrite each
  other with no warning or locking.
- `validate` does not run the assembly final pass; `status` is the command
  that does. (`features` is layer 2 and cannot register layer-3 handlers.)
- A face made by a pattern or mirror cannot be named from the CLI grammar; a
  target that needs one is marked rather than printed as something that would
  parse back to a different face.
- A placement's parameter binding is dimension-checked at resolve time, not at
  bind time.
- No undo across processes; a batch is the unit of recovery.

**Carried forward with no owner** (F-10): the `Document::modifyObject` bypass
of the document-level reference check; a CLI resolver for external parts; a
mate rebinding/repair command; and `info` showing a component's placement.

**Not P13:** the Qt desktop application is a placeholder, and the viewport is
not implemented. `ROADMAP.md` lists it as `Planned`.

## Qualified tree

```text
git status --porcelain -- src include apps tests examples cmake \
    CMakeLists.txt CMakePresets.json
    (empty)
git rev-parse HEAD        9cd2330f2c99bbf1e9cd48c7dcd1b81c5bf3c6cf
git rev-parse origin/main 9cd2330f2c99bbf1e9cd48c7dcd1b81c5bf3c6cf
```

The eight tree IDs were recorded three times: before the first build, after
the last test run, and after this milestone's documentation was written. All
three recordings are identical, and identical to `P13-REFMOD-001`'s.

**No executable source or test file was changed by this milestone.** What it
writes is this directory, three documentation corrections (`ARCHITECTURE.md`,
`docs/architecture.md`, an amendment note on ADR-003), and the `TODO.md` /
`ROADMAP.md` closeout. None of those can reach the executable or the tests,
which is why the qualification stands: the proof is the tree hashes above, not
the assertion.

## Result

```text
TASK:            P13-QUAL-001 — Full P13 qualification
IMPLEMENTATION:  none — no executable source or test file changed
TESTS:           1674/1674 in debug, release and debug-shared, each from
                 clean; 1674/1674 five times over in release and in debug
VALIDATION:      271 checks per preset against expectations derived on paper
                 (121 end-to-end through the real CLI, 83 on the reference
                 models through the real example program, 67 cross-preset),
                 identical in all three presets
ADVERSARIAL:     13 findings, 0 production defects; 3 fixed here, 10 recorded
WARNINGS:        0 in all three builds
TREE:            qualified tree == committed tree
RESULT:          PASS
EVIDENCE:        this directory
TODO:            P13-QUAL-001 → [x]; P13 QUALIFIED
```

`P13` is qualified.

## Files

```text
qualification/qualify.cmd                  the three-preset harness
qualification/run-qualification.cmd        its entry point
qualification/qualification-times.txt      every stage, its exit code, tree IDs
qualification/configure-*.log              3 presets
qualification/clean-*.log                  3 presets
qualification/build-*.log                  3 presets
qualification/rebuild-*.log                the no-op rebuild freshness proof
qualification/ctest-*.log                  3 presets, 1674/1674 each
qualification/ctest-repeat-*.log           release and debug, until-fail:5
qualification/end-to-end.py                real-process CLI qualification
qualification/end-to-end-*.log             121/121 in each preset
qualification/reference-models.py          the committed models vs their builders
qualification/reference-models-*.log       83/83 in each preset
qualification/cross-preset.py              do the three presets agree?
qualification/cross-preset.log             67/67
```
