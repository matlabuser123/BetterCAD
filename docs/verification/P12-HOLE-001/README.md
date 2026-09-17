# P12-HOLE-001 — Hole Standards Verification

## Status

**PASS.** Holes take the four things `TODO.md` authorizes for this
milestone — threads, a spotface, standard sizes and tolerance classes — as
the designations engineers write, not as the dimensions they stand for.

- **Threads are described, not cut.** A tapped hole is a bore at its
  thread's basic minor diameter (ISO 68-1); no helix is modelled. The
  thread's size, class, basic diameters, limits of size and length are data
  the hole's callout reports.
- **A spotface** is cut like a counterbore and names its floor
  `spotface_floor`, a new face role that sketches and datums can be placed
  on.
- **Standard sizes** are the metric threads of ISO 965-2 (coarse M1 to M64,
  fine M8x1 to M64x4) and the clearance holes of ISO 273 (fine, medium and
  coarse) for the same diameters.
- **Tolerance classes** are the internal thread classes of ISO 965-1 that
  ISO 965-2 gives each size (5H up to M1.4, 6H above) in position H or G,
  and the hole tolerance classes ISO 286-2 tabulates for sizes up to 500 mm
  with the fundamental deviations D, E, F, G and H.

Every table is transcribed twice — once for BetterCAD, once for the tests —
from published copies of the standards, and **both transcriptions are
checked against the sources themselves** by
[`standards/crosscheck.py`](standards/crosscheck.py): **21 checks, 0 failed**.

Debug, Release and Debug-shared each passed **1070/1070** tests
with **0 compiler warnings** from a verified clean rebuild. Every test of
the P11 qualification and of `P12-FEAT-006` still passes in all three, and
every value the existing tests measure is unchanged, bit for bit, apart from
two deliberate changes recorded below.

Date: 2026-09-18. `main` was at `74f5c9d` (`P12-FEAT-006`) before this
milestone.

| Path | What |
| --- | --- |
| `README.md` | This record: scope, the standards data, validation, qualification, limitations. |
| `standards/SOURCES.md` | Where each table comes from, with the SHA-256 of every source and what checks it. |
| `standards/crosscheck.py`, `crosscheck.log` | The script that reads the sources and compares them with BetterCAD's tables and the tests' reference tables, and its run. |
| `qualification/` | The qualification scripts, logs, comparisons and measured values. |

## Gate Results

Every figure below is from the logs in `qualification/`, not from a
targeted run.

| Gate | Result |
| --- | --- |
| Debug | **PASS** — 1070/1070 tests, clean rebuild, 360 translation units |
| Release | **PASS** — 1070/1070 tests, clean rebuild, 360 translation units |
| Debug-shared | **PASS** — 1070/1070 tests, clean rebuild, 360 translation units |
| Compiler warnings | **0** in all three builds (`-Werror`; no `warning` line in any build log) |
| Legacy tests (P11 qualification) | **PASS** — all 737 names present and passed in all three presets |
| P12 tests (through `P12-FEAT-006`) | **PASS** — all 1035 names present and passed in all three presets |
| `P12-HOLE-001` tests | **PASS** — 34 new tests (31 Catch2 cases, 7906 assertions, 0 failed) |
| Standards-source validation | **PASS** — 21 checks, 0 failed (`standards/crosscheck.log`) |
| Failure paths | **PASS** — every refusal in the diagnostics table below is a test case |
| Serialization | **PASS** — save → destroy → load → regenerate keeps the definitions bit for bit and the body's fingerprint; files written before this milestone load and save unchanged |
| Determinism | **PASS** — identical values across Debug, Release and Debug-shared (8746 lines, MD5 `7aa30390cc96361efc735128226ded90`); repeated, fresh-regenerator and fresh-document builds identical |
| Repeats | **PASS** — 725 related tests five times over in Release and Debug (3625 passed runs each) |
| CLI | **PASS** — `info`, `validate` and `export-step` on the example model, as Catch2 cases and as process tests |
| STEP read-back | **PASS** — the exported plate reads back as 1 valid solid, volume within 1.14e-15 relative |
| Every existing measured value | **PASS** — 38698 lines identical, MD5 `d739c331dcdd48477742d1d2de28d713` |
| The qualified tree | **PASS** — the Git tree IDs recorded by the run equal the committed tree's |

### Build and platform

| | |
| --- | --- |
| Compiler | GCC 16.1.0 (MinGW-W64 x86_64-ucrt-posix-seh, Brecht Sanders r4), C++23, `-Werror` |
| Build system | CMake 4.4.2, Ninja 1.13.2; presets debug, release, debug-shared |
| Geometry kernel | Open CASCADE Technology 8.0.1 (`OCC_VERSION_COMPLETE "8.0.1"`) |
| Test framework | Catch2 3.16.0 (fetched) |
| GUI toolkit | Qt 6.11.2 (the placeholder application only) |
| Platform | Windows 11 Pro 10.0.26200, AMD Ryzen 7 5800H |
| Python (evidence tools) | CPython 3.13, with `pdftotext` (xpdf 4.06) for the PDF sources |

## Scope

The deliverables are the ones `TODO.md` lists for `P12-HOLE-001`.

| Deliverable | Status | Main evidence (test cases) |
| --- | --- | --- |
| The metric thread sizes of ISO 965-2, by designation, with the basic diameters of ISO 68-1 | IMPLEMENTED | `MetricThreads_KnowTheSizesOfIso965Part2`, `MetricThreads_ParseTheirDesignations`, `MetricThreads_BasicDiametersFollowIso68` |
| The limits of size of an internal thread (ISO 965-1), in the class of ISO 965-2 | IMPLEMENTED | `MetricThreads_LimitsMatchIso965Part2`, `MetricThreads_PositionGRaisesEveryLimitByItsDeviation`, `MetricThreads_TolerancesFollowTheIso965Part1Formulae`, `MetricThreads_RefuseClassesWithoutKnownTolerances` |
| The clearance holes of ISO 273 and the classes it gives the series | IMPLEMENTED | `ClearanceHoles_MatchIso273` |
| The standard tolerances and hole tolerance classes of ISO 286 | IMPLEMENTED | `HoleTolerances_StandardTolerancesMatchIso286`, `HoleTolerances_LimitDeviationsMatchIso286`, `HoleTolerances_KnowOnlyTheTabulatedClasses`, `HoleTolerances_RefuseSizesWithoutTabulatedDeviations` |
| Spotfaces, with their own floor role | IMPLEMENTED | `Hole_SpotfaceIsCutLikeACounterbore`, `HoleStandards_TappedPlateHasTheVolumeOfItsStandardHoles` |
| Cosmetic threads, checked against their hole and cut as nothing | IMPLEMENTED | `Hole_CosmeticThreadCutsNoGeometry`, `Hole_RefusesThreadsThatDoNotFitTheirHole` |
| Threads, clearance sizes and tolerance classes in the hole feature, with callouts | IMPLEMENTED | `HoleStandards_CalloutsDescribeTheStandardsData`, `HoleStandards_ParametersDriveThreadsAndBores`, `HoleStandards_ClearanceSeriesChangesTheDiameter`, `HoleStandards_RejectDefinitionsTheStandardsDoNotAllow` |
| Dependencies, validation, undo/redo, save/load, CLI, patterns and mirrors | IMPLEMENTED | `HoleStandards_ValidationReportsTheThreadLengthParameter`, `HoleStandards_SaveLoadPreservesTheStandardsData`, `HoleStandards_FileStoresDesignationsNotDimensions`, `HoleStandards_PatternsAndMirrorsRepeatThreadedHoles`, `HoleStandardsCli_InfoDescribesThreadsAndStandardSizes` |

## What BetterCAD Knows, and What It Refuses

A standard's table is data, and BetterCAD holds the part of it that two
published copies agree on. Everything else is refused with a diagnostic
rather than computed from a formula whose rounding BetterCAD cannot check.

| | Known | Refused |
| --- | --- | --- |
| Thread sizes | the 60 sizes ISO 965-2 gives limits for: coarse M1 to M64, fine M8x1 to M64x4 | `M9`, `M6x0.75` (an ISO 262 size ISO 965-2 leaves out), `M8x1.5` (not a pitch of M8), sizes above M64 |
| Thread classes | the grade ISO 965-2 gives the size (5 up to M1.4, 6 above), in position H or G | every other grade (4, 7, 8), and classes that give the pitch and minor diameters different grades (`5H6H`) |
| Clearance holes | ISO 273, fine, medium and coarse, for the nominal diameters of those sizes | nothing else: every known thread's diameter is in the table |
| Hole tolerance classes | D6 to D13, E5 to E10, F3 to F10, G3 to G10, H1 to H18, for sizes above 0 up to 500 mm | JS (see below), A to C, CD, EF, FG, J and K to ZC; grades outside each position's range; sizes over 500 mm; IT14 to IT18 for sizes up to 1 mm (ISO 286-1 does not use them there) |

**JS is not known.** ISO 286-1 defines it as ±ITn/2, but published copies of
ISO 286-2 disagree on the tabulated values where ITn is an odd number of
micrometres: MISUMI's excerpt of JIS B 0401 gives JS7 of an 18 to 30 mm hole
as ±10 µm (IT7 = 21 µm rounded down to 20), and Roymech's tables give
±10.5 µm. BetterCAD cannot settle which the current edition intends from the
sources it has, so it refuses the position instead of picking one.

## The Standards Data

### Where each number comes from

`standards/SOURCES.md` records every source, its SHA-256 and what it
checks. In short:

| Table | BetterCAD's source | The tests' source |
| --- | --- | --- |
| Standard tolerances IT1 to IT18 | ISO 286-2:2010 preview, Table 1 | the Wikipedia article "IT Grade" (the same ISO 286-1 table) |
| The fundamental deviations D, E, F, G | ISO 286-2:2010 preview, Table 3 (D and E, as limits) | MISUMI's excerpt of JIS B 0401 (D, E, F, G and H, as limits) |
| Metric thread sizes and their limits | ISO 965-1:2013 preview (Tables 1, 2 and the printed part of 4) and the limits of ISO 965-2 | the limits of ISO 965-2:1998 and :2024 |
| Clearance holes | the ISO 273:1979 preview | Engineering Hardware's chart and the German Wikipedia article |

### How it is checked

`standards/crosscheck.py` reads the sources (PDF previews through
`pdftotext -table`, wiki text and HTML directly), parses their tables, and
compares them with the two transcriptions. Its run,
[`standards/crosscheck.log`](standards/crosscheck.log): **21 checks, 0 failed**.

A published preview stamps a watermark over its pages, and the text under it
comes out interleaved with the stamp. The script skips any line the
watermark crosses and reports how many rows it read, so nothing is silently
dropped:

| Source | Rows read | Rows its watermark hides |
| --- | --- | --- |
| ISO 286-2:2010, Table 1 (standard tolerances) | 11 of 13 size ranges | 2, which the Wikipedia copy covers (BetterCAD's table matches both wherever each is readable) |
| ISO 286-2:2010, Table 3 (D and E) | 9 of 13 size ranges | 4 |
| ISO 965-1:2013, Table 2 (TD1) | 18 of 22 pitches | 4, whose tolerances the limits of ISO 965-2 confirm |
| ISO 965-1:2013, Table 4 (TD2) | the 5 entries the preview prints (ranges up to 2.8 mm) | the rest is not printed at all; those tolerances come from the limits of ISO 965-2 |
| ISO 965-2 (internal thread limits) | 56 of 60 sizes | 4 (see below) |
| MISUMI's excerpt of JIS B 0401 | all 13 size ranges | none |
| The ISO 273 charts | all 34 sizes | none |

The four thread rows no preview leaves readable (M18x1.5, M20x1.5, M20x2 and
M22x1.5) were read from the characters the watermark leaves interleaved, and
each is checked against the row of the same pitch two millimetres below it,
which the previews do print: a fine thread's limits differ by exactly the
difference of the two diameters, since the pitch, the tolerances and the
fundamental deviation are the same.

### Independent checks inside the tests

The tests do more than compare two transcriptions:

- **The basic diameters** are recomputed from the profile
  (H = sqrt(3)/2 P, D2 = D − 3/4 H, D1 = D − 5/4 H) for all 60 sizes, and
  match the minimum limits ISO 965-2 lists for position H to the 0.001 mm
  they are rounded to.
- **The limits** BetterCAD computes for all 60 sizes match ISO 965-2's
  tabulated ones within that rounding, and the difference of the tabulated
  limits equals the tolerance exactly (both are whole micrometres).
- **Position G** raises every limit of a thread by one deviation, and that
  deviation equals the one ISO 965-2's 6g external threads are below the
  basic profile — a different table of the same standard.
- **The thread tolerances** follow the formulae of ISO 965-1
  (TD1(6) = 433P − 190P^1.22 up to P = 0.8 mm, 230P^0.7 above;
  TD2 = 1.32 (grade 6) or 1.06 (grade 5) of 90 P^0.4 d^0.1, d the geometric
  mean of the diameter range) within one step of the R40 series the standard
  rounds them to (5.93 %), for all 60 sizes.
- **The standard tolerances** follow ISO 286-1's rule that a grade is ten
  times the grade five steps below it, for grades 7 and up. The table breaks
  that rule once, at IT6 of sizes over 3 up to 6 mm (8 µm, whose IT11 is
  75 µm, not 80): every published copy agrees, and the test records it.
- **The limit deviations** of every known class satisfy ES = EI + ITn, and
  EI is zero for exactly the position H.

## Geometry

A thread and a tolerance class change no geometry at all; a spotface and a
standard size change only the dimensions of the hole that is cut.

| | What is cut |
| --- | --- |
| A tapped hole | a bore at the thread's basic minor diameter D1 |
| A standard clearance hole | a bore at the ISO 273 diameter of its bolt and series |
| A spotface | a counterbore of its own diameter and depth, whose floor is named `spotface_floor` |
| A tolerance class | nothing: the hole keeps its nominal size |

`Hole_CosmeticThreadCutsNoGeometry` drills the same hole with and without a
thread and compares the results bit for bit (volume, area and centre);
`Hole_SpotfaceIsCutLikeACounterbore` does the same for a spotface against a
counterbore of the same dimensions.

**The thread's own checks** (`geometry::validate`, `cutHole`): its major
diameter must be larger than the bore and smaller than any head; its length
must be positive (or zero for the whole hole), longer than the head, no
longer than a blind hole, and no longer than the material under the face for
a through hole. Every one is a test case in
`Hole_RefusesThreadsThatDoNotFitTheirHole`.

## Independent Validation

The reference model is `tests/support/HoleStandardModels.hpp`, saved as
[`examples/models/tapped_plate.bcad`](../../../examples/models/tapped_plate.bcad):
a plate 80 × 50 × 12 mm with five holes, each consuming the one before it.

| Hole | What it is | What it removes |
| --- | --- | --- |
| Tapped | M8-6H through, threaded full length | π/4 · D1(8, 1.25)² · 12 |
| BlindTapped | M6-6H blind 10 mm deep, thread `thread_length` long | π/4 · D1(6, 1)² · 10 |
| Seat | clearance for M8 (medium series, 9 mm), H13, with a 15 mm counterbore 5 mm deep | π/4 · (9² · 12 + (15² − 9²) · 5) |
| Boss | M10-6H through with a 20 mm spotface 1 mm deep | π/4 · (D1(10, 1.5)² · 12 + (20² − D1(10, 1.5)²) · 1) |
| Reamed | blind 8 mm deep, diameter `bore`, H7 | π/4 · bore² · 8 |

Every expected value is written out from the standards' own definitions —
D1 = D − 1.25 · (√3/2) · P, and the ISO 273 diameter — never read from a
result. The plate's volume, centre and bounds, and each hole's own removed
volume, are checked against them
(`qualification/deviations.txt`; the largest of each group:

| Check | Tolerance | Checks | Largest deviation measured (Release) |
| --- | --- | --- | --- |
| The thread limits against ISO 965-2's tabulated ones | 0.5 um (their rounding) | 240 | 0.481 um |
| The basic diameters against the tables (mm) | 5e-4 mm | 10 | 2.79e-4 mm |
| The tolerances against the ISO 965-1 formulae | 5.93 % (one R40 step) | 120 | 4.88 % |
| Limit deviations, tolerances and clearance holes against the sources (um) | 1e-9 um | 1766 | 6.93e-12 um |
| The basic diameters recomputed from the profile | 1e-15 rel | 120 | 2.17e-16 |
| The plate's volume, centre and bounds | 1e-12 rel; 1e-9 mm | 14; 20 | 1.58e-13; 1.07e-14 mm |
| Callout diameters and lengths | 1e-12 mm | 6 | 0 |
| STEP read-back volume | 1e-9 rel | 1 | 1.14e-15 |).

The CLI reports the plate's volume as 44516.171 mm³, which an independent
calculation in exact decimal arithmetic gives as 44516.171 mm³
(`cli.validate.tapped-plate`).

## Diagnostics

Every refusal names what was asked for and what the standard allows:

| Case | Message |
| --- | --- |
| A size no standard covers | `'M9' is not a metric thread size: BetterCAD knows the sizes of ISO 965-2 (coarse M1 to M64, fine M8x1 to M64x4)` |
| A thread class without known limits | `BetterCAD knows the M8 thread tolerances of grade 6 only (the grade of ISO 965-2), not 7H` |
| A hole class BetterCAD does not know | `'K7' is not a hole tolerance class BetterCAD knows (D6 to D13, E5 to E10, F3 to F10, G3 to G10, H1 to H18)` |
| A size ISO 286 does not tabulate | `BetterCAD knows the ISO 286 tolerances of sizes above 0 up to 500 mm, not 600 mm` |
| A grade a size may not use | `ISO 286 does not use grade IT14 for sizes up to 1 mm, like 0.5 mm` |
| Two standards for one diameter | `a hole takes a thread or a clearance size, not both` |
| A diameter beside a standard | `a threaded hole's diameter comes from its thread; it takes no diameter` |
| A class on a threaded hole | `a threaded hole takes no tolerance class; its thread has one (6H)` |
| A head narrower than the thread | `the spotface diameter (9 mm) must be larger than the thread's major diameter (10 mm)` |
| A thread longer than its hole | `the thread (12 mm long) must not be longer than the blind hole (10 mm deep)` |
| A thread longer than the material | `hole: the thread (25 mm long) is longer than the material along the axis under the face (20 mm)` |

## Tests

34 tests were added (1036 to 1070), 31 of them Catch2 cases with
7906 assertions:

| Where | Cases | What |
| --- | --- | --- |
| `tests/core/standards/HoleStandardsTests.cpp` | 12 | The tables against the published sources, the formulae and the standards' own rules; every refusal. |
| `tests/core/geometry/HoleTests.cpp` (new cases) | 3 | Spotfaces, cosmetic threads and the thread's checks against its hole. |
| `tests/features/HoleStandardTests.cpp` | 9 | The reference model's volumes and faces, callouts, driven values, refusals, undo/redo, determinism, patterns and mirrors. |
| `tests/io/HoleStandardFileTests.cpp` | 5 | Save/load of the designations, the file's shape, malformed data, files written before this milestone, the example model. |
| `tests/cli/HoleStandardCliTests.cpp` | 2 | What `info` and `validate` print. |
| `tests/CMakeLists.txt` (process tests) | 3 | `cli.info`, `cli.validate` and `cli.export-step` on the example model. |

**Determinism** is checked with the reference-model fingerprint
(validity, topology, volume, area, centre, bounds and every face name, bit
for bit) over repeated regenerations, a fresh regenerator, a fresh document,
and save → destroy → load → regenerate.

## Qualification

`qualification/qualify.cmd` was run through
`qualification/run-qualification.cmd` and did the following:

- configured each preset;
- removed every build output;
- rebuilt with warnings as errors under code page 65001;
- ran CTest after each successful build;
- repeated the related tests five times in Release and Debug.

The Git tree IDs it recorded equal the working tree's after the run.

| Preset | Configure | Clean | Build | TUs compiled | `warning` lines | Tests |
| --- | --- | --- | --- | --- | --- | --- |
| Debug | exit 0 | attempt 1 | exit 0 | 360 | 0 | **1070/1070 passed** (177.6 s) |
| Release | exit 0 | attempt 1 | exit 0 | 360 | 0 | **1070/1070 passed** (187.3 s) |
| Debug-shared | exit 0 | attempt 1 | exit 0 | 360 | 0 | **1070/1070 passed** (144.9 s) |

360 is `P12-FEAT-006`'s 352 translation units plus 8 new ones:

- `standards/MetricThreads.cpp`, `standards/HoleTolerances.cpp`,
  `standards/ClearanceHoles.cpp`;
- `json/HoleStandardsJson.cpp`;
- the four new test files.

**Repeats.** `ctest -R "[Hh]ole|[Ss]tandard|[Tt]hread|[Cc]learance|[Tt]oleranc|[Vv]ariable|[Rr]ib|[Dd]raft|[Ss]hell|[Ss]plit|[Cc]ombine|BodyOps|[Ff]illet|[Rr]evolve|[Ee]xtru|ThroughAll|[Rr]esult|[Pp]attern|[Mm]irror|[Ff]ace|[Rr]eference|[Dd]atum|[Bb]oolean|[Pp]ropert|[Rr]egenerat|[Vv]alidat|[Dd]ocument|[Ff]ile|[Cc]ommand|[Ee]xport|cli|tapped-plate|tapered-block|ribbed-bracket|drafted-block|shelled-block|body-ops|P9" --repeat until-fail:5` selected
725 tests.

| Preset | Result | Time |
| --- | --- | --- |
| Release | 725/725, each 5 times (3625 passed runs) | 398.3 s |
| Debug | 725/725, each 5 times (3625 passed runs) | 377.3 s |

**Across configurations** (`qualification/values-determinism.txt`), the
Debug, Release and Debug-shared executables printed identical values for the
milestone's cases (8746 lines, MD5 `7aa30390cc96361efc735128226ded90`).

**Timing** (`qualification/timing-comparison.txt`: CTest's per-test times
summed, `-j 8`, same machine), P12-FEAT-006 → P12-HOLE-001:

| Preset | P12-FEAT-006 | P12-HOLE-001 |
| --- | --- | --- |
| Release | 736.0 s | 1330.9 s (810 s and 827 s when re-run) |
| Debug | 757.2 s | 1360.1 s |
| Debug-shared | 761.6 s | 1018.5 s |

**No speed change is claimed, in either direction.** The qualification
run's own figures are inflated by load on this machine, which the tests that
run none of BetterCAD's code show: `compile_fail.ids.integer-to-id` (a
compiler run) took 6.52 s in `P12-FEAT-006`, 19.17 s in this qualification
and 6.18 s when the same build was re-run, and
`architecture.checker.layer-violation` (a CMake script) moved the same way.

Two re-runs of the same Release build with nothing else running
(`qualification/ctest-release-rerun.log`, and a second run kept out of the
evidence) summed 809.7 s and 826.7 s. Against `P12-FEAT-006`:

- the 34 new tests take 22 to 23 s;
- of the 1036 existing tests, the 247 that take 0.5 s or more have a median
  new/old ratio of 1.035 and 1.086 in the two re-runs, and every one of the
  twelve slowest is *faster* than in `P12-FEAT-006` (up to 2.8 s);
- the remaining +50 to +79 s is spread over the 789 tests under 0.5 s, whose
  floor moved from 0.060 s to 0.090 s on this machine — including tests that
  are CMake script runs and never load a BetterCAD binary.

The existing tests run no new code at all: a hole without a thread, a
clearance size or a tolerance class takes the same path it did before
(`qualification/all-values-comparison.txt` shows every value unchanged).

## Legacy Regression

`qualification/regression-comparison.txt` compares every log by test name
with the P11 qualification's Release log, and
`qualification/regression-comparison-feat006.txt` with `P12-FEAT-006`'s:

- the P11 baseline has 737 names, and the `P12-FEAT-006` log 1035;
- every baseline name is present and passed in the Debug, Release and
  Debug-shared logs, and no entry failed;
- the new names are this milestone's 34 tests (332 against P11).

**Every measured value** of the whole suite was compared, not just the test
names. A Release run with `-s` prints every assertion's expansion;
`qualification/values.py` extracts them and `compare-values.py` compares the
files:

- `feat006-release`: 928 test cases with measured values, 77211 assertions
  passed, 0 failed;
- `hole001-release`: 958 test cases with measured values, 84672 assertions
  passed, 0 failed;
- after dropping the 31 new test cases, ignoring source line numbers (the
  hole file test gained and lost lines) and substituting the two messages
  this milestone deliberately changed, both files hold **38698 identical
  lines, MD5 `d739c331dcdd48477742d1d2de28d713`**: every value the existing
  tests measure is unchanged, bit for bit.

The two substituted messages are in
`HoleFeature_MalformedDataIsRejectedWithTheJsonPath`, which feeds a hole a
`"type": "spotface"` and a `"thread"` key. Both were unknown before this
milestone and are known now, so the test still refuses both inputs at the
same path, with the new reasons: the spotface has no dimensions, and the
thread is not an object.

## Known Limitations

- **Only the part of each standard two published copies agree on** is known;
  everything else is refused (see the table above). JS is not known, and
  neither are the thread classes outside ISO 965-2's own.
- **A thread is not modelled.** No helix, no thread relief, no tap drill
  size (ISO 2306) and no thread engagement length (ISO 965-1's S, N and L
  groups). A tapped hole's bore is the thread's basic minor diameter, which
  lies inside the class's limits for the minor diameter but is not a drill
  size a shop would order.
- **Threads are right-handed** and have one start: there is no `LH`, no
  `Ph`/`P` multi-start designation, and no external thread.
- **A tolerance class is metadata**: the geometry is the nominal size, not
  the middle of the limits, and nothing checks that a shaft fits the hole.
- **No standard head sizes.** A counterbore or spotface for a particular
  fastener (ISO 4762, DIN 974) is given by its own diameter and depth.
- **Neither STEP nor STL carries the thread or the class**: they are exact
  geometry formats, and the thread is not geometry.
- **A mirrored threaded hole keeps its thread as it is.** Threads are not
  handed here: the image of a right-hand tapped hole is a right-hand tapped
  hole, which is what a shop would cut.

## Evidence Files

| File | What |
| --- | --- |
| `standards/SOURCES.md` | Every source, its SHA-256, and what each table is checked against. |
| `standards/crosscheck.py` | Reads the sources and compares them with BetterCAD's tables and the tests' reference tables. |
| `standards/crosscheck.log` | Its run: **21 checks, 0 failed**. |
| `qualification/qualify.cmd`, `run-qualification.cmd` | The qualification: clean rebuild and tests in each preset, then the repeats. |
| `qualification/configure-*.log`, `clean-*.log`, `build-*.log`, `ctest-*.log` | Each preset's configure, clean, build and test output. |
| `qualification/ctest-repeat-*.log` | The five-times repeats in Release and Debug. |
| `qualification/qualification-times.txt` | Times, exit codes and the Git tree IDs of what was built. |
| `qualification/reference-values-release.txt` | Every value the milestone's tests measured (Release, `-s`). |
| `qualification/values-determinism.txt` | The same values from all three presets, and their MD5. |
| `qualification/all-values-comparison.txt` | Every assertion of the whole suite, compared with `P12-FEAT-006`. |
| `qualification/regression-comparison.txt`, `-feat006.txt` | Every P11 and `P12-FEAT-006` test, by name, in all three presets. |
| `qualification/deviations.txt` | The largest deviation measured in each group of checks. |
| `qualification/timing-comparison.txt` | Per-test times against `P12-FEAT-006`, and the split by new and existing tests. |
| `qualification/values.py`, `compare-values.py`, `compare-regression.py`, `compare-times.py`, `deviations.py`, `timing-split.py` | The comparison tools. |

## Final Result

```text
TASK:            P12-HOLE-001 (hole threads, spotface, standard sizes, tolerance classes)
IMPLEMENTATION:  standards/ (ISO 68-1, 262, 273, 286, 965) in core; spotfaces and
                 cosmetic threads in geometry; threads, clearance sizes and
                 tolerance classes in the hole feature, with callouts; JSON,
                 CLI, face role spotface_floor
TESTS:           1070/1070 in Debug, Release and Debug-shared (34 new), 0 warnings;
                 725 related tests five times over in Release and Debug
VALIDATION:      every table against published copies of its standard (21 checks,
                 0 failed); the limits of all 60 thread sizes against
                 ISO 965-2 within its 0.001 mm rounding; the tolerances
                 against the ISO 965-1 formulae within one R40 step; the
                 plate's volume, centre and bounds against the standards'
                 own definitions; every existing measured value unchanged
RESULT:          PASS
EVIDENCE:        docs/verification/P12-HOLE-001/
TODO:            P12-HOLE-001 ticked; next is P12-PATTERN-001
```
