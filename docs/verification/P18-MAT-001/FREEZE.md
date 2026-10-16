# P18-MAT-001 — the qualification freeze and the run

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
CANDIDATE          843469aa6ebb3b65a440877eb4043c14037664bb  (P18-DATA-001's
                   documentation record) plus the working tree: 5 files
                   modified, 3 new source and test files, the evidence
                   directory -- git status --short recorded at the freeze in
                   qualification/freeze-fingerprint.txt
FROZEN             2026-10-17 03:00:00, after the mutation probes were verified
                   restored and before the pre-freeze run finished; no path
                   inside the fingerprint changed afterwards

apps               a95876bc6dbf843d4e0cec114059bc196fa16175   == P18-DATA-001
include            5ba4f65a2b20c1cee0f350e31f5eb5675ad676b8   MOVED
src                5bd8d50b089a3b693eaaec489115fb67178a5499   MOVED
tests              1d8b2808cfa1af72c6f5e5aa20eb87695142dd08   MOVED
examples           a955cf6511ce715fcb95bc06ef82b877535869e6   == P18-DATA-001
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7   == P18-DATA-001
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd   == P18-DATA-001
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e   == P18-DATA-001

QUALIFIED_CANDIDATE_HEAD           843469a (the parent; the candidate is this
                                   working tree, committed as recorded below)
QUALIFIED_CANDIDATE_TREE           scratch tree fc87bd6738c441bd6cbd47224402403ce8ff72e5
SOURCE_TEST_FINGERPRINT            the eight lines above, taken together
P18_MATERIAL_CONTRACT_FINGERPRINT  include 5ba4f65a + src 5bd8d50b: the header
                                   and source the resolver consists of, with
                                   the result source that consumes it
```

## What moved, against P18-DATA-001's qualified tree

```text
include   5b4d1dfb -> 5ba4f65a   + thermal/ThermalMaterial.hpp;
                                            thermal/ThermalResult.hpp (documentation
                                            of currentResultSource and
                                            InputsUnavailable)
src       4fbeceef -> 5bd8d50b   + thermal/ThermalMaterial.cpp;
                                            thermal/ThermalResult.cpp
                                            (currentResultSource resolves the material
                                            for the analysis's mode);
                                            thermal/CMakeLists.txt (1 source)
tests     de83d2e5 -> 1d8b2808   + thermal/ThermalMaterialTests.cpp (31
                                            cases); tests/CMakeLists.txt (1 source);
                                            cli/ZeroMatchGuard.cmake (1 filter)
apps, examples, cmake, CMakeLists.txt, CMakePresets.json   unchanged
```

No file under `src/core`, `src/features`, `src/meshing`, `src/structural`
or `apps/` changed: P15's material model is consumed, not extended, so every
P15 and P17 test in the unfiltered runs below is the predecessor regression
and is expected to pass exactly as it did for P18-DATA-001.

## The run

```text
RESULT:    PASS
RAN:       Sat 17/10/2026 03:38:26 -> 07:29:33  =  3h51m07s
STAGES:    17, every one exit 0, 0 stage(s) failed
HARNESS:   qualify.cmd exit 0, "Qualification passed: every stage exited 0."
           qualify.cmd d313a64070718c44fae290ac042fe259d1a03c8b, byte-identical
           to every milestone's since P16-SIZE-001; verify-harness.cmd run
           before the launch (qualification/verify-harness.out: PASS)
MACHINE:   the hasib machine, as every milestone since P17-CLI-001
PRE-FREEZE the working tree passed the whole suite in debug-ext before the
           launch: 4115/4115, 1986.65 s (qualification/prefreeze.txt)
```

| | debug-ext | release-ext | debug-shared-ext |
| --- | --- | --- | --- |
| tests | **4115 / 4115** | **4115 / 4115** | **4115 / 4115** |
| `warning:` | 0 | 0 | 0 |
| `error:` | 0 | 0 | 0 |
| objects compiled | 667 / 667 (690 build steps) | 667 / 667 (690) | 667 / 667 (690) |
| configure | 4 s, exit 0 | 4 s, exit 0 | 4 s, exit 0 |
| clean | exit 0, 689 files | exit 0, 687 files | exit 0, 698 files |
| build | 28m45s | 36m03s | 28m05s |
| no-op rebuild | exit 0, 1 edge | exit 0, 1 edge | exit 0, 1 edge |
| ctest | 34m09s (2049.17 s) | 31m17s (1877.19 s) | 32m57s (1976.84 s) |

```text
repeat release-ext   1244 tests x 5   18m30s (1109.63 s)   exit 0, 100% passed
repeat debug-ext     1244 tests x 5   20m50s (1249.25 s)   exit 0, 100% passed
TOTAL                                 3h51m07s
```

Every count **measured, not predicted**, and each reconciles against the
P18-DATA-001 run plus what this milestone added:

```text
suite    4084 -> 4115   +31:  the unit.ThermalMaterial* cases
objects   665 ->  667    +2:  ThermalMaterial.cpp and ThermalMaterialTests.cpp
steps     688 ->  690    +2:  the two objects
repeat   1213 -> 1244   +31:  unit\.Thermal now also selects the 31 material
                              cases
layering  503 ->  505    +2:  one header and one source under thermal/
DLLs       11 ->   11         libbettercad_thermal.dll grew from 8 684 837 to
                              9 994 654 bytes with its new object
```

The no-op rebuild in every preset re-ran exactly one edge, "Checking git
revision", so the binaries that ran the tests are the ones the clean build
produced. The harness's stderr holds 268 lines, every one git's CRLF notice
from the fingerprint step (0 other lines); the one "Warning" per configure
log is CMake's "Warnings as errors : ON" status line. **0 unexpected
warnings.**

## Predecessor regression

No file under `src/core`, `src/features`, `src/meshing` or `src/structural`
changed, and the P15 and P17 suites ran unfiltered in every preset as part
of the 4115. Measured directly as well, with each preset's own binary:

```text
[structural][material]  All tests passed (878 assertions in 19 test cases)  x3
                        the same 11 message lines in each preset
unit\.StructuralMaterial  ctest -N: 19 in every preset
```

P17-MAT-001's resolution of E, nu and rho is unchanged.

## The tree that was qualified is the tree that was frozen

```text
apps               a95876bc6dbf843d4e0cec114059bc196fa16175   == frozen
include            5ba4f65a2b20c1cee0f350e31f5eb5675ad676b8   == frozen
src                5bd8d50b089a3b693eaaec489115fb67178a5499   == frozen
tests              1d8b2808cfa1af72c6f5e5aa20eb87695142dd08   == frozen
examples           a955cf6511ce715fcb95bc06ef82b877535869e6   == frozen
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7   == frozen
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd   == frozen
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e   == frozen
```

**Freeze == before == after**: taken by hand at 03:00:00 and again at 03:35
after the pre-freeze run, by the harness from a scratch index at 03:38:26
and again at 07:29:33. The closeout commit's own subtree hashes are compared
against these eight values after the commit and recorded below.

## The fresh-binary proof, per preset

```text
preset             absolute binary                                            bytes          sha256
debug-ext          C:/Users/hasib/bcdev/bc-build/debug-ext/bin/
                     bettercad-cli.exe                                        368 141 765    8181d8626ae93a31...
                     bettercad_example_reference_models.exe                   372 807 743    0d5ad72c70eb0080...
                     bettercad_tests.exe                                    1 186 782 364    47cc10f2c468ca46...
release-ext        C:/Users/hasib/bcdev/bc-build/release-ext/bin/
                     bettercad-cli.exe                                         10 113 338    8aa5781b8cb3c4bb...
                     bettercad_example_reference_models.exe                     9 496 684    977f8901e25aa85b...
                     bettercad_tests.exe                                       49 664 878    59eb9858e32d20c3...
debug-shared-ext   C:/Users/hasib/bcdev/bc-build/debug-shared-ext/bin/
                     bettercad-cli.exe                                         41 310 182    9c9d659f2934022c...
                     bettercad_example_reference_models.exe                    52 417 896    8aed2bfa296a5597...
                     bettercad_tests.exe                                      845 267 883    e6dd3afb026db242...
                     libbettercad_thermal.dll                                   9 994 654    6798548e97d44bf1...
```

Full hashes in `qualification/postrun/summary.txt`. The CLI and
reference-model binaries have exactly the sizes P18-DATA-001 recorded in
every preset (their sources did not change; only the embedded git revision
stamp differs); every test binary grew by the new objects.

## Zero-match protection

| filter | minimum | discovered | PASS |
| --- | --- | --- | --- |
| `unit\.ThermalMaterial` | 15 | 31 | ✓ in all three presets |
| `unit\.ThermalData` | 20 | 36 | ✓ |
| `compile_fail\.thermalids\.` | 12 | 24 | ✓ |
| `unit\.ThermalInput` | 8 | 11 | ✓ |
| `unit\.Thermal` (the repeat filter's own) | — | 102 | ✓ |
| `unit\.StructuralMaterial` (P17-MAT-001's) | — | 19 | ✓ |
| the whole suite | — | 4115 | ✓ |

Discovered with `ctest -N` after the run on every preset's tree; the repeat
filter's selected count is the harness's own, 1244.

## Cross-preset equivalence

The material suite, run with each preset's own test binary after the
harness:

```text
debug-ext          All tests passed (1529 assertions in 31 test cases)
release-ext        All tests passed (1529 assertions in 31 test cases)
debug-shared-ext   All tests passed (1529 assertions in 31 test cases)
message lines      6 in each, the same six (identical once sorted; Catch2
                   shuffles case order per run): the conductivity-edit line
                   and the entry-validation line
[thermal][data]    All tests passed (2349 assertions in 36 test cases), x3
```

Every discrete outcome -- resolution status, `MaterialProblem`, completeness
state and property lists, the exact canonical values 42.75 / 7853.25 / 486.5
-- is identical under `-O0 -g`, under the optimiser and across a DLL
boundary, and the only out-of-line symbols of the new header are behind
`BETTERCAD_THERMAL_EXPORT`.

## The commit record

```text
commit             448eb5e2cb0f851a7d053f9297f8a1e45ed17031
                   "BetterCAD: add thermal material resolution",
                   parent 843469a; the fingerprint recomputed from a scratch
                   index (read-tree HEAD + add -A over the eight paths)
                   immediately before `git add`, then read back with
                   git rev-parse <commit>^{tree}:<path>
apps               a95876bc6dbf843d4e0cec114059bc196fa16175   == frozen
include            5ba4f65a2b20c1cee0f350e31f5eb5675ad676b8   == frozen
src                5bd8d50b089a3b693eaaec489115fb67178a5499   == frozen
tests              1d8b2808cfa1af72c6f5e5aa20eb87695142dd08   == frozen
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
