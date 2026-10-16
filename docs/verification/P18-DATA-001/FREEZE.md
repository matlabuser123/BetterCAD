# P18-DATA-001 — the qualification freeze and the run

```text
SUBJECT:  the tree that was qualified, recorded before the first build, and
          what the run measured
METHOD:   the eight-path fingerprint every BetterCAD qualification since P11
          has used -- a scratch git index seeded with `read-tree HEAD` and then
          `add -A` over apps include src tests examples cmake CMakeLists.txt
          CMakePresets.json. docs/ and deps/ are outside it by construction.
```

## The frozen candidate

```text
CANDIDATE          e6fded36acfa1fec41670ed6b14b7a243c145d4a  (P18-ARCH-001's
                   documentation record) plus the working tree: 10 files
                   modified, 8 new source and test files, the evidence
                   directory -- git status --short recorded at the freeze in
                   qualification/prefreeze.txt
FROZEN             2026-10-16 20:54:16, before the pre-freeze run finished
                   and before the qualification was launched; no path inside
                   the fingerprint changed afterwards

apps               a95876bc6dbf843d4e0cec114059bc196fa16175   == P18-ARCH-001
include            5b4d1dfbd362823779373f679f7a715b6f388ee9   MOVED
src                4fbeceef5f669bdfd9d065683055598986c23624   MOVED
tests              de83d2e5092413f76ff59ca9b62ffd470b2753e7   MOVED
examples           a955cf6511ce715fcb95bc06ef82b877535869e6   == P18-ARCH-001
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7   == P18-ARCH-001
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd   == P18-ARCH-001
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e   == P18-ARCH-001

SOURCE_TEST_FINGERPRINT     the eight lines above, taken together
P18_DATA_MODEL_FINGERPRINT  include 5b4d1dfb + src 4fbeceef: the headers and
                            sources the model consists of
```

## What moved, against P18-ARCH-001's qualified tree

```text
include   33ea62c2 -> 5b4d1dfb   core/Id.hpp (3 tags, 3 aliases, 1 widening)
                                 core/units/Dimension.hpp (4 dimensions)
                                 core/units/Units.hpp (4 quantities, 4 units)
                                 core/units/Literals.hpp (8 literals)
                                 + thermal/ThermalData.hpp,
                                   thermal/ThermalAnalysisObject.hpp,
                                   thermal/ThermalResult.hpp
src       9e992c58 -> 4fbeceef   core/units/UnitCatalog.cpp (4 entries)
                                 thermal/CMakeLists.txt (3 sources)
                                 + thermal/ThermalData.cpp,
                                   thermal/ThermalAnalysisObject.cpp,
                                   thermal/ThermalResult.cpp
tests     360b9e04 -> de83d2e5   + thermal/ThermalDataTests.cpp (36 cases)
                                 + compile_fail/ThermalIdMisuse.cpp (24 cases)
                                 CMakeLists.txt (1 source),
                                 compile_fail/CMakeLists.txt (1 group),
                                 cli/ZeroMatchGuard.cmake (2 filters),
                                 core/units/FormatAndCatalogTests.cpp
                                 (catalog count 54 -> 58)
```

No file under `src/structural`, `src/meshing`, `src/features`, `src/io` or
`apps/` changed. `core/Id.hpp` and the three units headers are included by
nearly every translation unit, which is why the three clean builds and
unfiltered runs below are the predecessor regression and not a formality.

## The run

```text
RESULT:    PASS
RAN:       Thu 16/10/2026 21:55:13 -> Fri 17/10/2026 01:35:57  =  3h40m44s
STAGES:    17, every one exit 0, 0 stage(s) failed
HARNESS:   qualify.cmd exit 0, "Qualification passed: every stage exited 0."
           qualify.cmd d313a64070718c44fae290ac042fe259d1a03c8b, byte-identical
           to every milestone's since P16-SIZE-001; verify-harness.cmd run
           before the launch (qualification/verify-harness.out: PASS)
MACHINE:   the hasib machine, as every milestone since P17-CLI-001
PRE-FREEZE the working tree passed the whole suite in debug-ext before the
           freeze: 4084/4084, 1890.98 s (qualification/prefreeze.txt)
```

| | debug-ext | release-ext | debug-shared-ext |
| --- | --- | --- | --- |
| tests | **4084 / 4084** | **4084 / 4084** | **4084 / 4084** |
| `warning:` | 0 | 0 | 0 |
| `error:` | 0 | 0 | 0 |
| objects compiled | 665 / 665 (688 build steps) | 665 / 665 (688) | 665 / 665 (688) |
| configure | 8 s, exit 0 | 9 s, exit 0 | 10 s, exit 0 |
| clean | exit 0, 687 files | exit 0, 682 files | exit 0, 693 files |
| build | 27m24s | 33m31s | 27m39s |
| no-op rebuild | exit 0, 1 edge | exit 0, 1 edge | exit 0, 1 edge |
| ctest | 31m54s (1913.45 s) | 28m45s (1725.12 s) | 31m53s (1913.24 s) |

```text
repeat release-ext   1213 tests x 5   18m12s (1092.26 s)   exit 0, 100% passed
repeat debug-ext     1213 tests x 5   20m38s (1237.26 s)   exit 0, 100% passed
TOTAL                                 3h40m44s
```

Every count **measured, not predicted**, and each reconciles against the
P18-ARCH-001 run plus what this milestone added:

```text
suite    4024 -> 4084   +60:  36 unit.ThermalData* cases and 24
                              compile_fail.thermalids.* cases
objects   660 ->  665    +5:  ThermalData.cpp, ThermalAnalysisObject.cpp,
                              ThermalResult.cpp, ThermalDataTests.cpp and the
                              thermalids compile-fail control
steps     683 ->  688    +5:  the five objects (the thermal library's link
                              already existed)
repeat   1177 -> 1213   +36:  unit\.Thermal now also selects the 36
                              ThermalData cases
layering  497 ->  503    +6:  three headers and three sources under thermal/
DLLs       11 ->   11         libbettercad_thermal.dll grew from 2 927 334 to
                              8 684 837 bytes with its three new objects
```

The no-op rebuild in every preset re-ran exactly one edge, "Checking git
revision", so the binaries that ran the tests are the ones the clean build
produced. The harness's stderr holds 268 lines, every one git's CRLF notice
from the fingerprint step (0 other lines); the one "Warning" per configure
log is CMake's "Warnings as errors : ON" status line. **0 unexpected
warnings.**

## The tree that was qualified is the tree that was frozen

```text
apps               a95876bc6dbf843d4e0cec114059bc196fa16175   == frozen
include            5b4d1dfbd362823779373f679f7a715b6f388ee9   == frozen
src                4fbeceef5f669bdfd9d065683055598986c23624   == frozen
tests              de83d2e5092413f76ff59ca9b62ffd470b2753e7   == frozen
examples           a955cf6511ce715fcb95bc06ef82b877535869e6   == frozen
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7   == frozen
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd   == frozen
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e   == frozen
```

**Freeze == before == after**: taken by hand at 20:54:16 (and again at
21:54:24 after the pre-freeze run), by the harness from a scratch index at
21:55:13 and again at 01:35:57. The closeout commit's own subtree hashes are
compared against these eight values after the commit and recorded below.

## The fresh-binary proof, per preset

```text
preset             absolute binary                                            bytes          sha256
debug-ext          C:/Users/hasib/bcdev/bc-build/debug-ext/bin/
                     bettercad-cli.exe                                        368 141 765    738e9884109ffb32...
                     bettercad_example_reference_models.exe                   372 807 743    b9ae30116947b0cb...
                     bettercad_tests.exe                                    1 181 095 165    c0da74809b0faedf...
release-ext        C:/Users/hasib/bcdev/bc-build/release-ext/bin/
                     bettercad-cli.exe                                         10 113 338    9059615232a49fde...
                     bettercad_example_reference_models.exe                     9 496 684    00409c13aba00198...
                     bettercad_tests.exe                                       49 401 404    16807e5657205197...
debug-shared-ext   C:/Users/hasib/bcdev/bc-build/debug-shared-ext/bin/
                     bettercad-cli.exe                                         41 310 182    dc89096ef82894bf...
                     bettercad_example_reference_models.exe                    52 417 896    49d82d096d7e8c6e...
                     bettercad_tests.exe                                      840 826 547    3f850b00106c2f41...
                     libbettercad_thermal.dll                                   8 684 837    1b20ac229d5f8ed5...
```

Full hashes in `qualification/postrun/summary.txt`. The release CLI has
exactly the size P18-ARCH-001 recorded (its sources did not change; only the
embedded git revision stamp differs) and every test binary grew by the new
objects. In debug-shared-ext the eleven `libbettercad_*.dll` modules sit
beside the three executables in their own `bin/`.

## Zero-match protection

| filter | minimum | discovered | PASS |
| --- | --- | --- | --- |
| `unit\.ThermalData` | 20 | 36 | ✓ in all three presets |
| `compile_fail\.thermalids\.` | 12 | 24 | ✓ |
| `unit\.ThermalInput` | 8 | 11 | ✓ |
| `architecture\.checker\.[a-z-]*thermal` | 4 | 4 | ✓ |
| `unit\.Thermal` (the repeat filter's own) | — | 71 | ✓ |
| `compile_fail\.` (whole group) | — | 272 | ✓ |
| the whole suite | — | 4084 | ✓ |

Discovered with `ctest -N` after the run on every preset's tree; the repeat
filter's selected count is the harness's own, 1213.

## Cross-preset equivalence

The data-model suite, run with each preset's own test binary after the
harness:

```text
debug-ext          All tests passed (2349 assertions in 36 test cases)
release-ext        All tests passed (2349 assertions in 36 test cases)
debug-shared-ext   All tests passed (2349 assertions in 36 test cases)
message lines      8 in each, the same eight lines in all three (identical
                   once sorted; Catch2 shuffles the case order per run, so
                   the files differ only in order): the same-IDs remesh
                   counts, the conductivity-edit line, the size-independence
                   counts and sizeof, the determinism tally
[units][catalog]   All tests passed (250 assertions in 3 test cases), x3
```

and the layering checker on the source tree: 503 files, 0 violations. The
shared preset is the one that catches a DLL-boundary defect, and every
out-of-line symbol of the three new headers is behind
`BETTERCAD_THERMAL_EXPORT`.

## The commit record

```text
commit             314166e339a875b41461b09bd906808c98711a5e
                   "BetterCAD: add thermal analysis data model",
                   parent e6fded3; the fingerprint recomputed from a scratch
                   index (read-tree HEAD + add -A over the eight paths)
                   immediately before `git add`, then read back with
                   git rev-parse <commit>^{tree}:<path>
apps               a95876bc6dbf843d4e0cec114059bc196fa16175   == frozen
include            5b4d1dfbd362823779373f679f7a715b6f388ee9   == frozen
src                4fbeceef5f669bdfd9d065683055598986c23624   == frozen
tests              de83d2e5092413f76ff59ca9b62ffd470b2753e7   == frozen
examples           a955cf6511ce715fcb95bc06ef82b877535869e6   == frozen
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7   == frozen
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd   == frozen
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e   == frozen
```

All eight equal, at all five readings: the freeze, the post-pre-freeze
recheck, the harness before the first build, the harness after the last test
run, and the commit. This record could only be written after the commit
existed, so it lands in a documentation commit that follows it; `docs/` is
outside the fingerprint by construction, and anyone can re-check the eight
lines against whatever `HEAD` is current with the one command P16-QUAL-001
gave.
