# P12-QUAL-001 — P12 Phase Qualification

```text
STATUS:          PASS — P12 QUALIFIED
SCOPE:           prove that the final P12 implementation is integrated,
                 tested, independently validated, adversarially reviewed,
                 deterministic within its documented contracts, regression
                 clean, and qualified on the tree that is committed
FROZEN REVISION: 15d7f756adda8f676750ec5539b317a17c77c81e
BUILD:           Debug, Release, Debug-shared — each configured, fully
                 cleaned, rebuilt and tested
TESTS:           1259/1259 in each of the three presets, then every test run
                 five more times in Release and in Debug
WARNINGS:        0 in all three builds, with 22 warning flags and -Werror
EVIDENCE:        this directory
```

This is a qualification milestone. It adds no CAD capability and changes no
executable source: the tree it qualifies is the tree `P12-REF-001` committed,
byte for byte.

## Scope

`P12` was delivered as eighteen milestones, each with its own evidence
directory and its own gates. This milestone does not re-litigate them. It
asks the question none of them could ask alone:

> Does the whole of P12 hold together, on one frozen tree, built and tested
> from clean, three ways?

## Frozen revision

```text
HEAD                15d7f756adda8f676750ec5539b317a17c77c81e
origin/main         15d7f756adda8f676750ec5539b317a17c77c81e
working tree        clean (git status --short empty)
git diff --check    clean
```

Source and test tree identity, from a scratch index:

```text
apps              9b7b03a72ef1a773304a38dd02690818f66d50a3
include           0347b09b01baf7ad55ec6a67d3a8e3256676e3a1
src               b5f9b6e92cc4ee1451f67fda6983f89c4b222435
tests             b2184a7bdb512f1882bbc84dce0449727b18ff98
examples          1e07d2ff797c2fc49505733931a24051e9171fdc
cmake             a84e909339b24bc7ffca591888e10d48a3e5296c
CMakeLists.txt    a0adbb9c1d3583aab4e294fd41837537e53e4764
CMakePresets.json 951b53d6b7412e6057c188dd41e40cb9e57c6aa3
```

Nothing under `apps`, `include`, `src`, `tests`, `examples`, `cmake` or the
two top-level build files changed at any point during this qualification.
The only files this milestone writes are documentation and evidence, which
cannot reach the executable or the tests. The identity above was recorded
before the first build and recomputed after the last, and is compared with
the committed tree under "Qualified tree".

## Build environment

```text
BetterCAD         0.1.0
CMake             4.4.2 (Ninja)
C++ compiler      GNU 16.1.0 (MinGW-W64 ucrt)
C++ standard      C++23
Target platform   Windows AMD64 (Windows 11 Pro 10.0.26200)
CPU               AMD Ryzen 7 5800H
Kernel            OCCT 8.0.1
Qt                6.11.2
Tests             Catch2 3.16.0 (fetched)
Warnings          22 flags, warnings as errors (-Werror)
```

## Build results

Each preset was configured, then **fully cleaned** (`ninja -t clean`, which
removed all 406 build outputs), then rebuilt. A preset's tests were run only
after its build returned 0.

| Preset | Configure | Clean | Build | Translation units | Warnings |
| --- | --- | --- | --- | --- | --- |
| `debug` | exit 0 | 406 files removed | exit 0 | 390 | 0 |
| `release` | exit 0 | 406 files removed | exit 0 | 390 | 0 |
| `debug-shared` | exit 0 | 406 files removed | exit 0 | 390 | 0 |

Logs: `qualification/configure-*.log`, `clean-*.log`, `build-*.log`.

## Test results

| Preset | Tests | Result | Time |
| --- | --- | --- | --- |
| `debug` | 1259 | **100% passed** | 169.70 s |
| `release` | 1259 | **100% passed** | 164.99 s |
| `debug-shared` | 1259 | **100% passed** | 173.15 s |

Repeat runs — the **whole** suite, each test run up to five times, to catch
order-, timing- and scheduling-dependence. A phase qualification has no
reason to repeat a chosen subset:

| Repeat | Tests | Result |
| --- | --- | --- |
| `release`, `--repeat until-fail:5` | 1259 | **100% passed** |
| `debug`, `--repeat until-fail:5` | 1259 | **100% passed** |

Of the 1259 registered tests, 1171 are Catch2 cases run in-process and 88 are
process-level tests that launch a real executable or run a build check.

Logs: `qualification/ctest-*.log`, `ctest-repeat-*.log`; timings in
`qualification-times.txt`.

## Warning audit

The three build logs were searched rather than inferred from exit codes:

```text
build-debug.log         0 lines containing "warning"
build-release.log       0 lines containing "warning"
build-debug-shared.log  0 lines containing "warning"
```

0 warnings, BetterCAD or third-party, in 390 translation units per preset.
`-Werror` is on, so a warning would have failed the build anyway; the logs
are searched because a gate that reads only an exit code proves less.

## P0–P11 regression

Measured against two baselines by `qualification/test-inventory.py`, which
reads the `TEST_CASE` names and the CTest registrations out of Git at each
baseline revision and compares them with the tests registered now.

| Baseline | Catch2 cases then | Process tests then | Missing now |
| --- | --- | --- | --- |
| `v0.1.0` — the P0–P10 release | 266 | 34 | **0 + 0** |
| `79dab04` — the tree `P11-QUAL-001` qualified | 693 | 44 | **0 + 0** |

**Not one test that existed at either baseline has been removed, renamed or
disabled.** All of them run, and all of them pass, in all three presets.

Two apparent misses were investigated and were artifacts of reading CMake
rather than real: a test registered through a loop variable
(`cli.validate.reference.${_stem}`) and the *definition* line of the
compile-failure helper function. The inventory script now expands the loop
from its own rows and anchors the helper pattern, so both families are
genuinely checked rather than skipped. The fix is in the script, and the
table above is the result after it.

Skipped tests: **0**. The suite contains no hidden (`[.]`), `!mayfail`,
`!shouldfail` or `SKIP()` case, and no commented-out `TEST_CASE`. The two
lines matching "disabled" in each CTest log are a test *name* — "Constraints
can be disabled, changed and driven by parameters" — which passes.

Inventory: `qualification/test-inventory.txt`.

## P12 test inventory

477 of the 1171 Catch2 cases carry a `[p12]` tag. Every one is attributed to
a milestone; none is unattributed. A test may cover more than one milestone
(a file test for spatial sweeps is both persistence and `P12-SWEEP-001`).

| Milestone | Tests |
| --- | --- |
| `P12-PARAM-001` Parameter expressions | 36 |
| `P12-SKETCH-001` Advanced sketch constraints | 27 |
| `P12-SKETCH-002` Ellipse and spline entities | 43 |
| `P12-DATUM-001` Datums | 21 |
| `P12-STREF-001` Stable face references | 54 |
| `P12-SKETCH-003` Sketches on planar faces | 13 |
| `P12-FEAT-001` Through-all extrude | 14 |
| `P12-FEAT-002` Split and combine | 18 |
| `P12-FEAT-003` Shell | 18 |
| `P12-FEAT-004` Draft | 16 |
| `P12-FEAT-005` Rib | 17 |
| `P12-FEAT-006` Variable-radius fillet | 16 |
| `P12-HOLE-001` Hole standards | 31 |
| `P12-PATTERN-001` Advanced patterns | 40 |
| `P12-SWEEP-001` Sweeps: paths, twist, guides | 35 |
| `P12-LOFT-001` Lofts: shapes and smoothness | 27 |
| `P12-PARAM-002` Design equations and configurations | 44 |
| `P12-REF-001` Production reference models | 37 |

Every milestone has coverage; the smallest, `P12-FEAT-001`, has 14 tests
across the feature, its persistence and the CLI.

By qualification concern:

```text
persistence (io round trip)      183
CLI (in-process)                  64
undo / redo                       46
validation                        29
STEP                              18
determinism (tagged)              13
STL                                5
failure paths (by case name)     130
acceptance                       356
reference models                  90
```

## Reference models

Twelve, all built through the public document, parameter, sketch and feature
APIs only — six from `P11` and the six production parts `P12-REF-001` added.
All twelve construct, regenerate, validate, save and load, and appear in the
cross-cutting suites that compare them exactly.

`P12-REF-001`'s own adversarial review found eight real defects in the six
new models **after** their tests were green, four of them parameters that
destroyed their own model at any value other than the one they were authored
at. All eight are fixed, each with a regression test, and the decision that
came out of it is recorded as
[ADR-001](../../architecture/decisions/ADR-001-reference-model-datum-placement.md).
Full account: [P12-REF-001](../P12-REF-001/README.md).

## Independent geometry validation

Expected values are derived by hand from each model's definition, never read
back from BetterCAD. Across P12 the closed forms in use include:

```text
box, cylinder, sphere, frustum, torus       elementary solids
Pappus                                      sweeps and revolutions
Green's theorem                             sketched cross-sections
prismatoid with a mixed-area term           square-to-round lofts,
                                            M = (2 n^2 r R / pi) sin^2(pi/n)
quadratic of revolution                     smooth three-section nozzles
drafted-box integral                        LWH - t(L+W)H^2 + (4/3)t^2H^3
offset cavity of a drafted box              shells of drafted solids
linear variable-fillet integral             (1-pi/4) L (r0^2+r0r1+r1^2)/3
ISO 724 / ISO 273 / ISO 286                 thread, clearance and tolerance
                                            diameters, from the standards
                                            layer, not from typed numbers
```

**Independence, checked mechanically.** All 141 test source files were
scanned for a `WithinRel`/`WithinAbs`/`WithinULP` whose *expected* argument is
computed from BetterCAD's own output. Six matches, all examined and all
legitimate:

- `BearingHousingTests.cpp:194` — a mirrored body is exactly twice the half it
  came from. A symmetry property; the absolute volume is pinned to a closed
  form at `1e-14` on the line above.
- `RibbedBracketTests.cpp:308–329` (five) — moving a datum must not change a
  volume and must shift a centroid by exactly the translation. Rigid-motion
  invariance and equivariance; the absolute values are pinned by closed forms
  elsewhere in the same file.

Neither kind can be written any other way: the claim *is* a relation between
two states. No test anywhere in the suite takes BetterCAD's answer as the
expected value of an absolute geometric quantity.

**Tolerances were not widened.** Every `constexpr double k…` tolerance in
`tests/` was compared with the `P11`-qualified tree: 41 constants existed
then, 121 exist now, and of the 41, **zero were loosened** — none were
changed at all. The shared reference tolerances are still `kRel = 1e-10`,
`kPositionMm = 1e-9`, `kBoundsPaddingMm = 1e-7`, `kRelStep = 1e-9`.

## Failure paths

Failure behaviour is a contract, not an absence: a failed feature must name
what failed, where and why, commit nothing, leave the document recoverable,
and regenerate correctly once the input is corrected. 130 cases exercise
this across P12 — invalid expressions and dimension mismatches, cyclic
expressions, under- and over-constrained sketches, missing stable
references, blends that do not fit, invalid shells, ribs whose side is not
closed off, degenerate loft sections, unsweepable paths, bad configuration
names and out-of-range hole parameters.

An example from the CLI, the whole diagnostic:

```text
$ bettercad-cli validate motor_mount.bcad --configuration Enormous
Validating motor_mount.bcad
bettercad-cli validate: no configuration named 'Enormous' in this document
exit 1
```

and from regeneration, where the atomicity matters:

```text
object:11: BoltHole: hole: the centre (200, 30) mm is not on a face of the
body on the plane through (0, 0, 0) mm facing (0, 0, -1)
(1 face(s) lie on that plane elsewhere)
object:12: blocked
object:14: blocked
```

The features after the failure are *blocked*, not stale: they hold no body,
and the document's object count is unchanged.

## Persistence

183 tests run the real round trip — create, save, destroy, load, regenerate,
compare — never a file-size or object-count check. What is compared is
engineering intent as well as geometry: parameters, expressions,
configuration tables and the active configuration, sketch entities and
constraints, datums, feature definitions, stable references, hole metadata,
patterns, sweep and loft data, and then topology, volume, area, centroid and
bounds exactly.

The twelve saved reference models in `examples/models/reference/` are held to
a stronger rule still: saving a freshly built document must reproduce the
committed file **byte for byte**.

## Determinism

Within its documented contracts, which this project states rather than
assumes:

- **Repeated regeneration** and **rebuilding from scratch** give the same
  model exactly, compared by a fingerprint over IDs, names, feature count,
  topology, volume, area, centroid and bounds.
- **Built together vs. built alone** — each reference model built in company
  equals the same model built on its own, which is what hidden global state
  would break.
- **Save/load/regenerate** reproduces the model exactly.
- **The whole suite, every test five times**, in Release and in Debug: 100%
  passed both times.
- **Configuration switching** restores parameter state exactly. Geometry may
  show bounded last-bit variation, because the sketch solver warm-starts from
  the geometry the sketch currently holds, so its converged point depends on
  the path taken. `P12-PARAM-002` measured this at **1.3e-15** relative over
  a whole configuration cycle — bounded, and not accumulating: returning a
  parameter to its old value returns the model to its old shape within
  `1e-12`, which is the bound `AllModelsTests` holds every one of the twelve
  reference models to.

This project does not claim bit-identical geometry where its contract allows
bounded variation, and does not relax the bound where the contract does not.

## Cross-preset comparison

The **whole** suite was run under each preset with `--reporter xml
--rng-seed 1`, and `values.py` extracted every passed assertion that shows a
value, with its test, section and `INFO` context — 53,580 lines per preset.
`compare-values.py` then compared them, normalizing only what is already
justified: pointer values, temporary directory names, document UUIDs and the
Git revision string.

**Every difference is accounted for. There are two kinds, and only two.**

### 1. Build-configuration tests — different by design

`BuildInfoTests.cpp` exists to report the configuration the library was built
with, so it *must* differ per preset: `build type : Debug` against `Release`,
`libraries : static` against `shared`. It also accounts for the three-line
count difference in the shared build, where the linkage assertion takes its
other branch:

```text
== Build info reports the library linkage
BuildInfoTests.cpp:51  CHECK_FALSE( !(buildInfo().sharedLibraries) )
```

Excluding those tests leaves **53,536 comparable lines in every preset** —
the same count in all three, so nothing else is present in one build and
absent from another.

### 2. Last-bit arithmetic between optimization levels

| Comparison | Differing lines | Build strings | Numeric | Max relative deviation |
| --- | --- | --- | --- | --- |
| `debug` vs `release` | 20 of 53,536 | 1 | 19 | **4.46e-16** |
| `debug-shared` vs `release` | 20 of 53,536 | 1 | 19 | **4.46e-16** |
| `debug-shared` vs `debug` | **1** of 53,536 | 1 | **0** | **none** |

The one remaining "build string" in each row is the CLI's own `version`
output, quoted in a CLI test.

Two things follow, and they are worth separating:

- **Linkage changes nothing.** Static against shared at the same optimization
  level produces **zero** numeric differences across 53,536 lines. The single
  differing line is the build-info string saying "shared".
- **Optimization level moves 19 values, by at most 2 ulp.** The largest
  disagreement in the entire suite is

  ```text
  release: 254.64790894703248
  debug:   254.64790894703260
  ```

  a relative difference of 4.46e-16, against a double epsilon of 2.22e-16 —
  **two units in the last place**. This is the compiler's latitude in
  associating and contracting floating-point expressions at different
  optimization levels, and it is inside the bounded variation BetterCAD's
  contract allows. It is not required to be zero, and it is not claimed to
  be.

### 3. The production geometry *is* bit-identical

Narrowing to what matters most — the twelve reference models, the parts that
represent real engineering output — the agreement is exact:

```text
release         8693 lines  MD5 f02272f84a4f574e75caca2a94faae2e
debug           8693 lines  MD5 f02272f84a4f574e75caca2a94faae2e
debug-shared    8693 lines  MD5 f02272f84a4f574e75caca2a94faae2e

reference-model measured values identical across all presets: YES
```

Every volume, area, centroid, bound and derived parameter of all twelve
models is **bit-identical** across Debug, Release and Debug-shared. The 2-ulp
differences above are confined to lower-level geometry and math unit tests.

**No unexplained cross-preset difference.**

Evidence: `qualification/values-comparison.txt` (all tests),
`values-comparison-geometry.txt` (build-configuration tests excluded),
`values-comparison-linkage.txt` (static against shared),
`reference-model-hashes.txt`, and the scripts beside them. The three
5.3 MB per-preset value files are not committed — they are regenerated by
`collect-evidence.cmd`, and the MD5s above are what the comparison rests on.

## CLI

The real production executable, `build/release/bin/bettercad-cli.exe`, run
against the committed reference documents. 88 process-level tests already
exercise the CLI inside the suite; this is a separate smoke run of the
shipped binary, recorded in `qualification/cli-smoke.log`.

| Case | Expected | Exit |
| --- | --- | --- |
| `--version` | 0 | 0 |
| `info motor_mount.bcad` | 0 | 0 |
| `validate gearbox_cover.bcad` | 0 | 0 |
| `validate manifold_tube.bcad` | 0 | 0 |
| `validate transition_duct.bcad` | 0 | 0 |
| `validate index_plate.bcad` | 0 | 0 |
| `validate ribbed_bracket.bcad` | 0 | 0 |
| `validate motor_mount.bcad --configuration Large` | 0 | 0 |
| `validate motor_mount.bcad --configuration Small` | 0 | 0 |
| `validate motor_mount.bcad --configuration Enormous` | 1 | 1 |
| `export-step transition_duct.bcad` | 0 | 0 |
| `validate <missing file>` | 1 | 1 |

12 of 12 as expected, with correct diagnostics and no crash. The
configuration selection is visible in the output, not merely in the exit
code:

```text
  feature regeneration  ok, 6 objects regenerated
  geometry              ok, 1 result body
Result bodies (1):
  Pilot (object:14): 1 solid, volume 199728.141 mm^3, area 40337.699 mm^2,
  bounds (0, 0, 0) to (180, 90, 22) mm
Result: valid
```

## STEP read-back

18 tests export STEP and then **reopen the exported file** with the kernel's
own reader, checking the solid count, shape validity, volume and bounds of
what comes back — not what was sent.

Be precise about what this proves. STEP read-back proves **exported geometry
interoperates**. It does not preserve, and is not claimed to preserve, the
feature tree, parameters, expressions, configurations, semantic references or
design intent. Those live in the native `.bcad` format, and are covered by
the persistence gate above.

## Adversarial review

Run against the phase as a whole, after every other gate had passed. The
questions and what answering them showed:

| Question | Answer |
| --- | --- |
| Did a feature pass isolated tests but fail integration? | **Yes — and it was caught.** `P12-REF-001` exists to integrate the P12 features into realistic parts, and its adversarial review found eight defects that isolated feature tests could not see. All fixed with regression tests before that milestone was ticked. |
| Are the independent expected values genuinely independent? | Checked mechanically across all 141 test files. Six hits, all rigid-motion invariance or mirror symmetry, each with its absolute value pinned to a closed form elsewhere. |
| Were tolerances widened during development? | No. Of the 41 tolerance constants that existed at the P11-qualified tree, zero changed. |
| Were tests removed, weakened, renamed or skipped? | No. 266+34 tests at `v0.1.0` and 693+44 at the P11-qualified tree are all still present and passing. 0 skipped, 0 hidden, 0 `!mayfail`, 0 commented out. |
| Could topology ordering make stable-reference tests pass by accident? | The 54 `P12-STREF-001` tests assert the face's *role and generating feature*, and the reference models regenerate after parameter changes that reorder topology. A reference that stops matching fails loudly; the contract forbids substituting another face, and `RibbedBracketIsBuiltOnItsFrame` demonstrates the failure rather than assuming it. |
| Can save/load silently change engineering intent? | 183 persistence tests compare intent, not just geometry, and the twelve saved models must round-trip byte for byte. |
| Can undo/redo leave stale state? | 46 undo/redo tests, including the production edit driven through `CommandHistory` on all six new models. |
| Can configuration switching accumulate solver drift? | Measured, bounded at 1.3e-15 per cycle, and shown not to accumulate: restoration is exact to 1e-12. |
| Can repeated regeneration depend on execution order? | The whole suite ran five times in each of two presets with no failure, and the models are compared built-together against built-alone. |
| Could Debug and Release disagree? | Answered by the cross-preset comparison above. |
| Did STEP validation reopen the exported file independently? | Yes — `checkStepExport` calls the kernel's reader on the written file. |
| Can failure paths leave partial document state? | Failure tests assert no downstream body and an unchanged object count. |
| Did any reference model bypass the public API? | No. The builders use only the public document, parameter, sketch and feature APIs; `examples` is layer-checked by `architecture.layering`. |
| Hidden global-state dependencies? | The built-together-vs-alone test exists precisely to find them; it passes. |
| Did P12 introduce architecture outside ARCHITECTURE.md? | No. `architecture.layering` enforces containment and layering and fails the build; OCCT stays in its adapters and Qt in the app and renderer. |
| Are the documented limitations accurate? | Audited below. |
| Were deferred features presented as supported? | No. Fillet setback, selectable corner transitions and loft end conditions are marked deferred in `TODO.md` and `ROADMAP.md`, and appear nowhere in `include/bettercad/`. |

**Findings requiring a source change: none.** The two corrections this gate
did produce were to the qualification tooling and to documentation: the
inventory script's CMake parsing (described under P0–P11 regression) and the
documentation fixes below. Neither touches the executable or the tests, so
the frozen tree stands.

## Documentation audit

Checked against the implementation, and corrected where they disagreed:

| Document | Finding | Action |
| --- | --- | --- |
| `README.md` | "Next: `P12-REF-001`" — stale, that milestone is complete | Status table rewritten: `P12` qualified, no capability in progress, next phase is a scope decision |
| `README.md` | "Six mechanical parts" as the reference-model set | Corrected to twelve, with both evidence links |
| `README.md` | Headline qualification numbers were `P11`'s 738/738 | `P12-QUAL-001`'s numbers now lead; the `P11` figures are kept as history with their evidence link |
| `README.md` | "738 tests passing means 738 tests passed" | Corrected to 1259 |
| `ROADMAP.md` | `P12` shown as **In progress** in two places | Both now **Qualified**, with the qualification summary |
| `ROADMAP.md` | Milestone table missing `P12-REF-001` and `P12-QUAL-001` | Both added with evidence links |
| `TODO.md` | — | Checklist ticked; status advanced (see below) |
| `CLAUDE.md`, `docs/engineering/` | — | Consistent; 0 broken links across the changed documents |

Verified: no document claims `P13` is authorized. `TODO.md` lists it under
"Future Phases — Do not start without explicit authorization".

## Known limitations

Carried forward, all accurate and all recorded in `TODO.md` and `README.md`:

- Edge and face references are geometric, not full semantic topology. A
  parameter that moves the plane or edge a feature was placed on makes the
  reference match nothing; the feature then fails and keeps no body. **No
  entity is ever substituted.** A consequence P12-REF-001 demonstrated: a
  model cannot be relocated by moving its own datum, because its holes and
  fillets name planes in model space ([ADR-001](../../architecture/decisions/ADR-001-reference-model-datum-placement.md)).
- `HoleDefinition.face` accepts only a geometric `FaceSignature`, not a
  semantic `FaceName`, unlike shell, draft and sketch attachment.
- Variable-radius fillets cover the independently verified safe subset;
  setback and selectable corner transitions are deferred.
- Loft end conditions are unavailable on this kernel, reported with the
  measurement rather than emulated.
- Parameter expressions have arithmetic, units and parameter names only — no
  functions, powers or constants — and only parameters take them.
- Lofts and sweep paths take lines, arcs and circles only.
- Configuration switching restores parameter state exactly; geometry may show
  bounded last-bit variation from the sketch-solver warm start, measured at
  1.3e-15.
- STEP is an export and validation path, not import interoperability.
- The desktop application is a placeholder shell; no GUI behaviour is
  qualified.
- One platform: Windows 11 AMD64, GCC 16.1.0, OCCT 8.0.1, Qt 6.11.2. No CI,
  no sanitizers, no coverage, no memory checking.

## Qualified tree

The gate is that **the tree that was qualified is the tree that is
committed**, for everything that can reach the executable or the tests.

`qualify.cmd` recorded the eight tree IDs from a scratch index before the
first configure. They were recomputed after the last repeat run, and again
after the closeout commit, and are identical every time:

```text
apps              9b7b03a72ef1a773304a38dd02690818f66d50a3
include           0347b09b01baf7ad55ec6a67d3a8e3256676e3a1
src               b5f9b6e92cc4ee1451f67fda6983f89c4b222435
tests             b2184a7bdb512f1882bbc84dce0449727b18ff98
examples          1e07d2ff797c2fc49505733931a24051e9171fdc
cmake             a84e909339b24bc7ffca591888e10d48a3e5296c
CMakeLists.txt    a0adbb9c1d3583aab4e294fd41837537e53e4764
CMakePresets.json 951b53d6b7412e6057c188dd41e40cb9e57c6aa3
```

They are also the tree IDs `P12-REF-001` qualified and committed as
`15d7f75`: this milestone rebuilt and retested **exactly** that tree, and
added nothing to it. The closeout commit changes only `README.md`,
`ROADMAP.md`, `TODO.md` and this evidence directory — documentation, none of
which the build or the tests read.

Two corrections were made during the qualification. Neither touched the
qualified tree:

- the inventory script's CMake parsing, in
  `qualification/test-inventory.py`, which is evidence tooling in this
  directory, not a repository test;
- the documentation fixes listed in the audit above.

Had either required a source or test change, this qualification would have
been void and rerun from a new frozen tree. Neither did.

## Result

```text
P12-QUAL-001: PASS
P12:          QUALIFIED
```

| # | Gate | Result |
| --- | --- | --- |
| 1 | Freeze final source tree | PASS — `15d7f75`, clean, `HEAD == origin/main` |
| 2 | Clean Debug build | PASS — 390 TUs, exit 0 |
| 3 | Clean Release build | PASS — 390 TUs, exit 0 |
| 4 | Clean Debug-shared build | PASS — 390 TUs, exit 0 |
| 5 | Full test suite — Debug | PASS — 1259/1259 |
| 6 | Full test suite — Release | PASS — 1259/1259 |
| 7 | Full test suite — Debug-shared | PASS — 1259/1259 |
| 8 | 0 unexpected compiler warnings | PASS — 0 in all three logs |
| 9 | P0–P11 regression unchanged | PASS — 0 of 266+34 and 0 of 693+44 missing |
| 10 | All P12 milestone tests | PASS — 477 tests, all 18 milestones covered |
| 11 | Production reference models | PASS — 12 models, all gates |
| 12 | Independent geometry validation | PASS — closed forms; independence checked mechanically |
| 13 | Failure-path validation | PASS — 130 cases, atomic and diagnosed |
| 14 | Persistence validation | PASS — 183 round trips; saved models byte for byte |
| 15 | Determinism validation | PASS — repeats, rebuilds, and bounded drift measured |
| 16 | Cross-preset comparison | PASS — every difference accounted for; max 2 ulp |
| 17 | CLI smoke | PASS — 12/12 through the real executable |
| 18 | STEP export/read-back | PASS — 18 tests reopen the exported file |
| 19 | Adversarial review | PASS — no finding required a source change |
| 20 | Documentation consistent | PASS — 7 corrections made, listed above |
| 21 | Final evidence recorded | PASS — this directory |
| 22 | Qualified tree == committed tree | PASS — 8 of 8 tree IDs identical |

22 gates, 22 passed.

What this does **not** say: that BetterCAD is finished, that the GUI works,
that STEP round-trips design intent, that semantic topology exists, or that
any behaviour without a test behind it is correct. The Known limitations
above are part of the result, not a footnote to it.

`P13` is not authorized. The next phase is an explicit scope decision.
