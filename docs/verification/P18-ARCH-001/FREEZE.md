# P18-ARCH-001 — the qualification

```text
RESULT:    PASS
CANDIDATE: a3b721b150338cc1cbd24ee592a11a70a6196c0c  (P17-QUAL-001's
           documentation record) plus the working tree: 8 files modified,
           the thermal module, its tests, five fixture files, three ADRs and
           the evidence directory new -- git status --short recorded at the
           freeze in qualification/prefreeze.txt
RAN:       Fri 16/10/2026 01:56:17 -> 05:26:04  =  3h29m47s
STAGES:    17, every one exit 0, 0 stage(s) failed
HARNESS:   qualify.cmd exit 0, "Qualification passed: every stage exited 0."
           qualify.cmd d313a64070718c44fae290ac042fe259d1a03c8b, byte-identical
           to every milestone's since P16-SIZE-001; verify-harness.cmd run
           before the launch (qualification/verify-harness.out: PASS)
MACHINE:   the hasib machine, as P17-CLI-001, P17-REFMOD-001 and P17-QUAL-001
```

## The numbers

| | debug-ext | release-ext | debug-shared-ext |
| --- | --- | --- | --- |
| tests | **4024 / 4024** | **4024 / 4024** | **4024 / 4024** |
| `warning:` | 0 | 0 | 0 |
| `error:` | 0 | 0 | 0 |
| objects compiled | 660 / 660 (683 build steps) | 660 / 660 (683) | 660 / 660 (683) |
| configure | 3 s, exit 0 | 4 s, exit 0 | 4 s, exit 0 |
| clean | exit 0, 682 files | exit 0, 679 files | exit 0, 689 files |
| build | 27m21s | 33m09s | 26m38s |
| no-op rebuild | exit 0, 1 edge of 8 | exit 0, 1 edge of 8 | exit 0, 1 edge of 18 |
| ctest | 29m14s (1754.46 s) | 26m28s (1588.25 s) | 28m25s (1705.31 s) |

```text
repeat release-ext   1177 tests x 5   17m55s (1074.75 s)   exit 0, 100% passed
repeat debug-ext     1177 tests x 5   20m09s (1209.68 s)   exit 0, 100% passed
TOTAL                                 3h29m47s
```

Every count **measured, not predicted**, and each reconciles against the
P17-QUAL-001 run plus what this milestone added:

```text
suite    4009 -> 4024   +15:  11 unit.ThermalInput* boundary cases and 4
                              architecture.checker.*thermal* fixtures
objects   658 ->  660    +2:  ThermalAnalysis.cpp and
                              ThermalAnalysisBoundaryTests.cpp
steps     680 ->  683    +3:  the two objects and libbettercad_thermal's link
repeat   1138 -> 1177   +39:  unit\.Thermal was ADDED to the filter and
                              selects 35 (11 ThermalInput + 24 of P15's
                              unit.ThermalProperties* cases, which carry the
                              same prefix), plus the 4 new checker fixtures
                              that `architecture` already selected
DLLs       10 ->   11    +1:  libbettercad_thermal.dll beside the three
                              executables in debug-shared-ext's own bin/
```

The no-op rebuild in every preset re-ran exactly one edge, "Checking git
revision", so the binaries that ran the tests are the ones the clean build
produced. The harness's stderr holds 268 lines, every one git's CRLF notice
from the fingerprint step; the one "Warning" per configure log is CMake's
"Warnings as errors : ON" status line. **0 unexpected warnings.**

## The tree that was qualified is the tree that was committed

```text
apps               a95876bc6dbf843d4e0cec114059bc196fa16175   == P17-QUAL-001
include            33ea62c2406871dc5d65df6eb381e247fc6ff6c6   MOVED
src                9e992c58ffefc159d75bc780d29f756386053fa4   MOVED
tests              360b9e04cfb11ada85f4fafd7cbad51d4962ab11   MOVED
examples           a955cf6511ce715fcb95bc06ef82b877535869e6   == P17-QUAL-001
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7   == P17-QUAL-001
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd   == P17-QUAL-001
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e   == P17-QUAL-001
```

**Before == after**, taken by the harness from a scratch index at 01:56:17
and again at 05:26:04. What moved, and only what moved:

```text
include   + include/bettercad/thermal/ThermalAnalysis.hpp
src       + src/thermal/CMakeLists.txt, src/thermal/ThermalAnalysis.cpp;
            src/CMakeLists.txt one add_subdirectory line
tests     + tests/thermal/ThermalAnalysisBoundaryTests.cpp; five fixture
            files; tests/architecture/CMakeLists.txt four process tests;
            tests/architecture/CheckLayering.cmake one table entry and its
            comment; tests/CMakeLists.txt one source and one link;
            tests/cli/ZeroMatchGuard.cmake two filters
```

No file under `src/structural`, `src/meshing`, `src/features` or `src/core`
changed; `apps` and `examples` are byte-identical to the P17-QUAL-001
freeze. The closeout commit's own subtree hashes are compared against these
eight values after the commit and recorded below.

## The fresh-binary proof, per preset

```text
preset             absolute binary                                            bytes          sha256
debug-ext          C:/Users/hasib/bcdev/bc-build/debug-ext/bin/
                     bettercad-cli.exe                                        368 170 949    72992f5c2a85ea39...
                     bettercad_example_reference_models.exe                   372 836 927    28d41bdbdbde7748...
                     bettercad_tests.exe                                    1 169 754 596    c31d83f1a13f2149...
release-ext        C:/Users/hasib/bcdev/bc-build/release-ext/bin/
                     bettercad-cli.exe                                         10 113 338    4e73ad5e57118d0e...
                     bettercad_example_reference_models.exe                     9 496 172    bfe7b84913342cea...
                     bettercad_tests.exe                                       48 883 857    50ebc523de27f332...
debug-shared-ext   C:/Users/hasib/bcdev/bc-build/debug-shared-ext/bin/
                     bettercad-cli.exe                                         41 310 182    3d94608fe9c4fa96...
                     bettercad_example_reference_models.exe                    52 417 896    59a7ac09a0c0676d...
                     bettercad_tests.exe                                      834 480 180    a9e20af194956a19...
                     libbettercad_thermal.dll                                   2 927 334    3c17100ee109bb17...
```

Full hashes in `qualification/postrun/summary.txt`. The CLI and runner
binaries have exactly the sizes P17-QUAL-001 recorded in every preset (their
sources did not change; only the embedded git revision stamp differs); the
test binaries grew by the one new object. In debug-shared-ext the eleven
`libbettercad_*.dll` modules -- the tenth and eleventh being `structural` and
the new `thermal` -- sit beside the three executables in their own `bin/`.

## Zero-match protection

| filter | minimum | discovered | executed | PASS |
| --- | --- | --- | --- | --- |
| `unit\.ThermalInput` | 8 | 11 | 11 | ✓ in all three presets |
| `architecture\.checker\.[a-z-]*thermal` | 4 | 4 | 4 | ✓ |
| `^architecture\.` (the repeat filter's own) | — | 19 | 19 | ✓ |
| the whole suite | — | 4024 | 4024 | ✓ |

Discovered with `ctest -N` after the run on every preset's tree; executed by
the harness's ctest logs. The repeat filter's selected count is the
harness's own, 1177.

## Cross-preset equivalence

The thermal boundary suite, run with each preset's own test binary after the
harness:

```text
debug-ext          All tests passed (259 assertions in 11 test cases)
release-ext        All tests passed (259 assertions in 11 test cases)
debug-shared-ext   All tests passed (259 assertions in 11 test cases)
message lines      4 in each, identical: the prepared mesh's counts and k,
                   the two stale-refusal diagnostics, the roster
```

and the layering checker on the source tree: 497 files, 0 violations. The
shared preset is the one that catches a DLL-boundary defect -- an unexported
out-of-line member, a runtime use of an exported class's `static constexpr`
-- and `ThermalModel`'s only out-of-line symbols are the two exported free
functions and `toString`, each `BETTERCAD_THERMAL_EXPORT`.

## The commit record

```text
commit             7e7578feac3e582618d21904491145801ef0af1a
                   "BetterCAD: define thermal analysis architecture",
                   parent a3b721b; the fingerprint recomputed from a scratch
                   index (read-tree HEAD + add -A over the eight paths)
                   immediately before `git add`, then read back with
                   git rev-parse <commit>^{tree}:<path>
apps               a95876bc6dbf843d4e0cec114059bc196fa16175   == frozen
include            33ea62c2406871dc5d65df6eb381e247fc6ff6c6   == frozen
src                9e992c58ffefc159d75bc780d29f756386053fa4   == frozen
tests              360b9e04cfb11ada85f4fafd7cbad51d4962ab11   == frozen
examples           a955cf6511ce715fcb95bc06ef82b877535869e6   == frozen
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7   == frozen
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd   == frozen
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e   == frozen
```

All eight equal, at all three readings. This record could only be written
after the commit existed, so it lands in a documentation commit that follows
it; `docs/` is outside the fingerprint by construction, and anyone can
re-check the eight lines against whatever `HEAD` is current with the one
command P16-QUAL-001 gave.
