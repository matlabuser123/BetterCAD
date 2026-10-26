# P19-ARCH-001 — the qualification

```text
RESULT:    PASS
CANDIDATE: e1a8eb29e4d477cdd5fe3a8b82edac25629420b6  (P18 qualified in the
           roadmap and the P19 plan in TODO.md) plus the working tree: the cfd
           module, its tests, eight fixtures, the checker tables, three ADRs
           and this directory -- git status --short recorded at the freeze in
           qualification/freeze-arch-pre.txt
RAN:       Tue 27/10/2026 02:48:09 -> 09:25:20  =  6h37m11s (attempt 2)
STAGES:    17, every one exit 0, 0 stage(s) failed
HARNESS:   qualify.cmd exit 0, "Qualification passed: every stage exited 0."
           qualify.cmd d313a64070718c44fae290ac042fe259d1a03c8b, byte-identical
           to every milestone's since P16-SIZE-001
MACHINE:   the hasib machine, as every milestone since P17-CLI-001
```

## Attempt 1 (stopped, not counted)

Launched 01:51. debug-ext's clean build compiled every object and then failed to **link** `bettercad_tests.exe`:
`ld.exe: ... could not read symbols: memory exhausted` (02:18:54). The cause was outside the tree: the OneDrive client
(PID 560, running since 2026-10-20) held about 56 GB of commit charge. The run was stopped during release-ext's build,
its logs kept under `qualification/attempt-1/`, OneDrive was restarted (its new process held 0.1 GB, commit free
61 GB) and the run relaunched unchanged at 02:48 against the same frozen tree. No source, test or harness file
changed between the attempts (`freeze-arch-pre.txt` and `freeze-arch-pre2.txt` were taken before attempt 1; the
post-run freeze equals them).

## The numbers

| | debug-ext | release-ext | debug-shared-ext |
| --- | --- | --- | --- |
| tests | **5826 / 5826** | **5826 / 5826** | **5826 / 5826** |
| `warning:` | 0 | 0 | 0 |
| `error:` | 0 | 0 | 0 |
| objects compiled | 745 (770 build steps) | 745 (770) | 745 (770) |
| configure | 6 s, exit 0 | 5 s, exit 0 | 11 s, exit 0 |
| clean | exit 0, 759 files | exit 0, 143 files | exit 0, 778 files |
| build | 30m33s | 36m17s | 30m56s |
| no-op rebuild | exit 0, 1 edge of 8 | exit 0, 1 edge of 8 | exit 0, 1 edge of 20 |
| ctest | 4764.74 s | 4559.90 s | 4938.93 s |

```text
repeat release-ext   2583 tests x 5   1670.18 s   exit 0, 100% passed
repeat debug-ext     2583 tests x 5   1951.05 s   exit 0, 100% passed
TOTAL                                 6h37m11s
```

Every count **measured, not predicted**, and each reconciles against P18-QUAL-001 plus what this milestone added:

```text
suite    5804 -> 5826   +22:  14 unit.CfdInput_* boundary cases and 8
                              architecture fixtures (5 checker, 3 targets)
objects   743 ->  745    +2:  CfdAnalysis.cpp and CfdAnalysisBoundaryTests.cpp
steps     767 ->  770    +3:  the two objects and libbettercad_cfd's link
repeat   2561 -> 2583   +22:  unit\.Cfd was ADDED to the filter (14) and the 8
                              fixtures are selected by `architecture`
DLLs       12 ->   13    +1:  libbettercad_cfd.dll in debug-shared-ext's bin/
```

The no-op rebuild in every preset re-ran exactly one edge, "Checking git revision", so the binaries that ran the tests
are the ones the clean build produced. **0 unexpected warnings.**

The harness's stderr holds 272 lines: 270 are git's line-ending notices from the fingerprint step, and **two are a
harness defect** recorded here rather than hidden. Line 50 of `run-qualification.cmd` is a comment that begins
`remrem` instead of `rem`; cmd tried to run it as a command and printed "'remrem' is not recognized as an internal or
external command". Nothing executed and no stage was affected (the line is before `setlocal`, and the error does not
set the run's result). The same typo was copied forward from P18-ASSEMBLY-001's harness. The pre-run check that
"every line begins with rem, set, call, ..." is a prefix match and `remrem` passes it. The file is left as it ran,
because it is evidence of the run; the next milestone's copy must fix the line.

## The tree that was qualified is the tree that will be committed

```text
apps               b4e2268d1a42fbdd543c7d4e0d10d58ecb70b117   == HEAD (e1a8eb2)
include            e4503a1712bc4295710d857727bd00660f6c8621   MOVED
src                3dd791701a4516c3ec17daae4994b8c7a4c43ddb   MOVED
tests              7967221f04e721f7fb6e91a7a6ddc47cddf6742e   MOVED
examples           4da234ebf4779f7c16d5df0075f106f7e3d4c958   == HEAD
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7   == HEAD
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd   == HEAD
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e   == HEAD
scratch tree       246a1719fa222f0659af0512b4c70941b3ebded6
```

**Before == after**, taken by the harness at 02:48:09 and again after the last test run, and by `freeze-arch.ps1`
before attempt 1 (twice, identical) and after the run (`freeze-arch-post.txt`, all 93 lines equal to
`freeze-arch-pre.txt`), including the milestone's own fingerprints:

```text
P19_ARCH_CFD_MODULE              b6ec0cb7...   header, source, both CMake files
P19_ARCH_CFD_TESTS               44ef629f...   boundary tests, registration, zero-match guard
P19_ARCH_ARCHITECTURE_CONTRACT   afd8f106...   checker tables, fixtures, registrations
P19_ARCH_ADRS                    8000581a...   ADR-058, 059, 060 and docs/architecture.md
```

What moved, and only what moved:

```text
include   + include/bettercad/cfd/CfdAnalysis.hpp
src       + src/cfd/CMakeLists.txt, src/cfd/CfdAnalysis.cpp;
            src/CMakeLists.txt one add_subdirectory line
tests     + tests/cfd/CfdAnalysisBoundaryTests.cpp; eight fixtures;
            tests/architecture/CMakeLists.txt eight process tests;
            CheckLayering.cmake one table entry; CheckTargetDependencies.cmake
            rule 4; tests/CMakeLists.txt one source and one link;
            tests/cli/ZeroMatchGuard.cmake the new groups and two raised minimums
```

No file under `src/thermal`, `src/structural`, `src/meshing`, `src/features`, `src/linalg` or `src/core` changed.

## The fresh-binary proof, per preset

Each binary is newer than its preset's clean and newer than the newest file of the frozen source tree
(`tests/architecture/CheckTargetDependencies.cmake`, 2026-10-27 01:45:02):

```text
preset            binary                    bytes          built      sha256
debug-ext         cleaned 02:48:27.59
                  bettercad-cli.exe           422 882 002  03:17:02   610e68766b4fc3ea...
                  bettercad_tests.exe       1 456 398 719  03:18:53   362807934c3ca580...
release-ext       cleaned 04:38:33.07
                  bettercad-cli.exe            11 585 182  05:13:56   436faa03e7e81f25...
                  bettercad_tests.exe          61 608 916  05:14:23   0bc2df6aedfec16e...
debug-shared-ext  cleaned 06:31:04.89
                  bettercad-cli.exe            48 966 994  06:59:51   f3e7d4609a1f49e3...
                  bettercad_tests.exe       1 066 974 014  07:01:54   2a0f7342a6645de2...
                  libbettercad_cfd.dll          2 926 205  06:42:22   0fdea645e2330cd8...
```

Full hashes in `qualification/postrun-summary.txt`. The first postrun pass printed "after clean True" without having
parsed the clean time: its regex expected one space between date and time where cmd writes two before a one-digit
hour, and a comparison with an unparsed time is vacuously true. The script was corrected (`\s+`, and a missing
clean time is now a recorded failure, not a skipped check) and rerun; the values above are from the corrected pass.

## Zero-match protection

237 groups in `ZeroMatchGuard.cmake`, each with an asserted minimum. For every group, in every preset: discovered
(`ctest -N -R`, fixture setups excluded) >= minimum, and executed-and-passed in the full ctest log == discovered.
**Groups failing either: 0.** An impossible filter discovers 0 tests. The new groups:

| filter | minimum | discovered and executed, each preset |
| --- | --- | --- |
| `unit\.CfdInput` | 14 | 14 |
| the cfd checker fixtures | 5 | 5 |
| the cfd link-graph fixtures | 3 | 3 |

## Cross-preset equivalence

The CFD boundary suite, run with each preset's own test binary after the harness:

```text
debug-ext          All tests passed (450 assertions in 14 test cases)
release-ext        All tests passed (450 assertions in 14 test cases)
debug-shared-ext   All tests passed (450 assertions in 14 test cases)
architecture       11 of 11 (cfd fixtures, valid trees, link graph) in each preset
checkers           554 files, 0 layering violations; cfd links neither thermal nor structural
```

The shared preset is the one that catches a DLL-boundary defect; `CfdModel`'s only out-of-line symbols are the
exported free functions and `toString`, each `BETTERCAD_CFD_EXPORT`.

## The commit record

Written after the commit, in the documentation commit that follows it.
