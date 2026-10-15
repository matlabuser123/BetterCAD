# P17-QUAL-001 — final regression, three presets, and the warning audit

```text
RESULT:    PASS
HARNESS:   qualify.cmd exit 0, "Qualification passed: every stage exited 0."
           qualification-times.txt: "0 stage(s) failed"
RAN:       Thu 15/10/2026 19:16:23 -> 23:06:20  =  3h49m57s, one detached run
STAGES:    17, every one exit 0
CANDIDATE: a99f0b6c332c4c122d495c55b99df598a083d86a (P17-REFMOD-001's commit)
           plus the working tree: 2 files modified, 1 test source new, the
           evidence directory and the documentation amendments new
```

## The numbers

| | debug-ext | release-ext | debug-shared-ext |
| --- | --- | --- | --- |
| tests | **4009 / 4009** | **4009 / 4009** | **4009 / 4009** |
| `warning:` in the build | 0 | 0 | 0 |
| `error:` in the build | 0 | 0 | 0 |
| objects compiled | 658 / 658 (680 build steps) | 658 / 658 (680) | 658 / 658 (680) |
| configure | 3 s, exit 0 | 4 s, exit 0 | 4 s, exit 0 |
| clean | exit 0, 679 files | exit 0, 678 files | exit 0, 688 files |
| build | 29m27s | 38m28s | 30m26s |
| no-op rebuild | exit 0, 1 edge of 8 | exit 0, 1 edge of 8 | exit 0, 1 edge of 17 |
| ctest | 31m13s (1872.59 s) | 29m16s (1755.95 s) | 31m37s (1896.14 s) |

```text
repeat release-ext   1138 tests x 5   18m21s (1101.68 s)   exit 0, 100% passed, 0 failure lines
repeat debug-ext     1138 tests x 5   20m33s (1232.44 s)   exit 0, 100% passed, 0 failure lines
TOTAL                                 3h49m57s
```

Every count **measured, not predicted**, and each reconciles against the
P17-REFMOD-001 run plus what this milestone added:

```text
suite    4006 -> 4009   +3:  the three unit.P17Qualification* final gates
repeat   1135 -> 1138   +3:  unit\.P17Qualification was ADDED to the repeat
                             filter so the final gates are repeated five times
                             too; everything else the filter selects is
                             unchanged from P17-REFMOD-001
objects   657 ->  658   +1:  P17StructuralQualificationTests.cpp.obj
steps     679 ->  680   +1:  the same
```

The no-op rebuild in every preset re-ran exactly one edge, "Checking git
revision", which is the always-run step that stamps the binaries; nothing
was recompiled or relinked, so the binaries that ran the tests are the ones
the clean build produced. The debug-shared-ext plan shows 17 edges because
the shared build has the DLL copy steps in its graph; one ran.

## Warning audit

The gate is 0 unexpected warnings, and "unexpected" is defined before
looking: anything a human would have to read and decide about. A status
line that happens to contain the word is not a warning.

```text
| CHANNEL   | debug-ext | release-ext | debug-shared-ext | NOTE                      |
| configure |     0     |      0      |        0         | one line per preset matches
|           |           |             |                  | "Warning": CMake's own
|           |           |             |                  | "Warnings as errors : ON"
|           |           |             |                  | status report of the POLICY|
| compile   |     0     |      0      |        0         | -Werror, 658 objects       |
| link      |     0     |      0      |        0         |                            |
| ctest     |     0     |      0      |        0         | 4009/4009 each; a test
|           |           |             |                  | NAME containing "Failed"
|           |           |             |                  | is a name                  |
| repeat    |     0     |      0      |        —         | 1138 x 5 in two presets    |
| harness   |    268 lines on stderr, all one class     | see below                  |
```

The harness's stderr holds 268 lines, every one git's "in the working copy
of '<file>', CRLF will be replaced by LF the next time Git touches it",
emitted by the fingerprint step over files whose `.gitattributes` line
endings differ from the working copy. P17-CLI-001 and P17-REFMOD-001 recorded
the same 268 lines; none is from a build or a test. **0 unexpected
warnings.**

## Zero-match protection

| filter | minimum | discovered | executed | PASS |
| --- | --- | --- | --- | --- |
| `unit\.P17Qualification` | 3 | 3 | 3 | ✓ in all three presets |
| `unit\.Structural(Patch\|AxialBar\|Cantilever\|PressureBlock\|GravityBlock\|Transformed\|LocalRefinement\|UnderConstrained\|StaleMesh\|Unresolved\|Reference)` | 12 | 16 | 16 | ✓ |
| `refmod\.structural\.` | 30 | 41 | 41 | ✓ |
| `unit\.StructuralCli` | 15 | 19 | 19 | ✓ |
| `cli\.structural\.` | 8 | 9 | 9 | ✓ |
| the whole suite | — | 4009 | 4009 | ✓ |

Discovered with `ctest -N` after the run on every preset's tree
(`qualification/postrun/summary.txt`); executed by the harness's own ctest
logs. `cli.mesh.zero-match-guard`, which proves the counter itself against
an impossible filter, passed in all three. The repeat filter's selected
count is the harness's own, 1138, recorded in `qualification-times.txt`;
`verify-harness.cmd` was run before the launch and proved that a filter
matching no tests is counted as a FAILED stage (`qualification/verify-harness.out`).

## The pre-freeze check

Before the launch, from the tree restored after the seven mutation probes:
debug-ext built with exit 0 and the whole suite ran, **4009 / 4009 in
1880.51 s** (`qualification/prefreeze.txt`, `prefreeze-ctest-full-debug-ext.log`);
`git status --short` at the freeze is recorded in the same file and names no
file under `src/`, `include/`, `apps/` or `examples/`.

## Where this ran

The hasib machine, as P17-CLI-001 and P17-REFMOD-001: Windows 11 Pro
10.0.26300, Ryzen 7 5800H, 16 logical cores, 32 GB; GCC 16.1.0 WinLibs
POSIX UCRT r4 with its bundled CMake 4.4.1 and Ninja 1.13.2; the dependency
prefix `C:/Users/hasib/bcdev/bettercad-deps/gnu-16-mingw-amd64` built by
`deps/CMakeLists.txt` from the hash-verified archives (Qt 6.11.2, OCCT
8.0.1, zlib 1.3.1, Netgen 6.2.2604); build root
`C:/Users/hasib/bcdev/bc-build`, in the cache since P17-CLI-001 with the
three `FETCHCONTENT_SOURCE_DIR_*` entries that milestone recorded. Nothing
about the environment changed between the three milestones; the deviations
from the uqhas machine and their reasons are restated in
`qualification/run-qualification.cmd`.
