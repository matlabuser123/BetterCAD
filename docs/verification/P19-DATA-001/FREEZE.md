# P19-DATA-001 — the qualification

```text
RESULT:    PASS
CANDIDATE: 24a1496805e8059336dc5ead1e332dc6e281eabb  (P19-ARCH-001's record) plus the
           working tree: core's Id.hpp, three new cfd headers and sources, one
           test file, one compile-fail file, their registrations, the guard,
           ADR-061 and this directory -- git status recorded in
           qualification/freeze-data-pre.txt
RAN:       Tue 27/10/2026 12:07:55 -> 18:51:18  =  6h43m23s, first attempt
STAGES:    17, every one exit 0, 0 stage(s) failed
HARNESS:   qualify.cmd exit 0, "Qualification passed: every stage exited 0."
           qualify.cmd d313a640, verify-harness.cmd 11855b8f, launch.cmd f11039e6,
           all byte-identical to P19-ARCH-001's; verify-harness.cmd run before
           the launch (qualification/verify-harness.out: PASS)
MACHINE:   the hasib machine; the OneDrive client was restarted before the launch
           (18.6 GB after nine hours -> 0.08 GB; 61 GB commit free), the lesson
           of P19-ARCH-001's first attempt
```

## The numbers

| | debug-ext | release-ext | debug-shared-ext |
| --- | --- | --- | --- |
| tests | **5893 / 5893** | **5893 / 5893** | **5893 / 5893** |
| `warning:` / `error:` | 0 / 0 | 0 / 0 | 0 / 0 |
| objects compiled | 750 (775 build steps) | 750 (775) | 750 (775) |
| configure | 6 s, exit 0 | 5 s, exit 0 | 6 s, exit 0 |
| clean | exit 0, 774 files | exit 0, 769 files | exit 0, 782 files |
| build | 30m34s | 36m31s | 31m03s |
| no-op rebuild | exit 0, 1 edge of 8 | exit 0, 1 edge of 8 | exit 0, 1 edge of 20 |
| ctest | 4951.99 s | 4708.31 s | 4948.05 s |

```text
repeat release-ext   2622 tests x 5   1645.94 s   exit 0, 100% passed
repeat debug-ext     2622 tests x 5   1989.65 s   exit 0, 100% passed
```

Every count was **measured**, and each reconciles against P19-ARCH-001:

```text
suite    5826 -> 5893   +67:  39 unit.CfdData_* cases and 28 compile_fail.cfdids cases
objects   745 ->  750    +5:  CfdData.cpp, CfdAnalysisObject.cpp, CfdResult.cpp,
                              CfdDataTests.cpp and the cfdids control
                              (4 cfd source objects and 3 cfd test objects per preset)
steps     770 ->  775    +5:  the five objects (libbettercad_cfd's link was already counted)
repeat   2583 -> 2622   +39:  unit\.Cfd selects the 39 new data cases
DLLs                    13    unchanged; libbettercad_cfd.dll grew 2.9 MB -> 8.8 MB
```

Because core's `Id.hpp` changed, every module recompiled from clean in every preset,
and the whole suite, not only the new cases, ran on those binaries. The no-op rebuild
re-ran one edge, "Checking git revision". **0 unexpected warnings.** The harness's
stderr holds 270 lines, every one git's line-ending notice. **P19-ARCH-001's `remrem`
line is gone**: this copy of `run-qualification.cmd` was checked before the launch by
a whole-word match (`^(rem|set|call|echo|setlocal|endlocal|@echo)( |$)`), and no
"not recognized" line appears.

## The tree that was qualified is the tree that will be committed

```text
apps               b4e2268d1a42fbdd543c7d4e0d10d58ecb70b117   == HEAD (24a1496)
include            859e5bf3abe1c39b25224727d6b4231b2ce72f17   MOVED
src                16c042610f108dc717a7ec72be67a54567e4df10   MOVED
tests              ada28684ee1f1da0e397f28e31bd64dbd7401d7c   MOVED
examples           4da234ebf4779f7c16d5df0075f106f7e3d4c958   == HEAD
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7   == HEAD
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd   == HEAD
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e   == HEAD
scratch tree       265fb3de90ea8bb10496f494119ee822fb959ad8
```

**Before == after.** The harness took the trees at 12:07:55 and again after the last
test run. `freeze-data.ps1` ran twice before the launch (identical) and once after
(`freeze-data-post.txt`), and all 56 lines equal `freeze-data-pre.txt`, including the
milestone's own fingerprints:

```text
P19_DATA_CFD_DATA_MODEL   86242eab...   Id.hpp, the four cfd headers, three sources, CMake
P19_DATA_CFD_DATA_TESTS   992e8c40...   the data tests, the compile-fail file and both
                                         registrations, the zero-match guard
P19_DATA_ADRS             347943d8...   ADR-061
```

No file under `src/thermal`, `src/structural`, `src/meshing`, `src/features`,
`src/linalg` or `src/core` changed. `include/bettercad/core/Id.hpp` gained two tags
and two aliases and nothing else.

## The fresh-binary proof

Each binary is newer than its preset's clean and newer than the newest file of the
frozen tree (`include/bettercad/cfd/CfdResult.hpp`, 11:55:43):

```text
preset            binary                    bytes          built      sha256
debug-ext         cleaned 12:08:12.23
                  bettercad-cli.exe           422 882 002  12:36:55   22c8a4ec40b8e1a5...
                  bettercad_tests.exe       1 469 144 445  12:38:39   cbe9ef292902f49b...
release-ext       cleaned 14:01:25.87
                  bettercad-cli.exe            11 585 182  14:36:54   dec6b2b4114e7b54...
                  bettercad_tests.exe          62 216 951  14:37:24   108ef57e8494ee06...
debug-shared-ext  cleaned 15:56:34.71
                  bettercad-cli.exe            48 966 994  16:25:29   f733db9917fe91d0...
                  bettercad_tests.exe       1 074 705 959  16:27:30   e0d9762088e6689a...
                  libbettercad_cfd.dll          8 758 235  16:08:25   3d01449caa4104e5...
```

Full hashes are in `qualification/postrun-summary.txt`. The CLI binaries have exactly
the sizes P19-ARCH-001 recorded, because nothing they link changed in behaviour.

## Zero-match protection

There are 240 groups in `ZeroMatchGuard.cmake`. In every preset, every group's
discovered count is at least its minimum and its executed count equals its
discovered count. **Groups failing either: 0.** An impossible filter discovers 0.
The new groups:

| filter | minimum | discovered = executed, each preset |
| --- | --- | --- |
| `unit\.CfdData_` | 39 | 39 |
| the two golden, truth-table and regime cases by name | 4 | 4 |
| `compile_fail\.cfdids\.` | 28 | 28 |

## Cross-preset equivalence

Each preset ran the CFD cases with its own test binary after the harness, and ran
the compile-fail group through its own CTest:

```text
debug-ext          All tests passed (2244 assertions in 53 test cases)   cfdids 28/28
release-ext        All tests passed (2244 assertions in 53 test cases)   cfdids 28/28
debug-shared-ext   All tests passed (2244 assertions in 53 test cases)   cfdids 28/28
golden fingerprint cfd-definition-v1:20707079b32e4964 in all three
checkers           560 files, 0 layering violations; cfd links neither thermal nor structural
```

**A display defect in the post-run script, recorded rather than hidden.** The
summary prints "(cfd: 0)" objects per preset. The pattern copied from P19-ARCH-001
was narrowed to `src/cfd/Cfd`, but object paths read `src/cfd/CMakeFiles/...`, so it
matched nothing. The line is display only and gates nothing. Counted directly, each
preset built 4 cfd source objects and 3 cfd test objects (the table above). The
script is kept as it ran.

## The commit record

Written after the commit, in the documentation commit that follows it.
