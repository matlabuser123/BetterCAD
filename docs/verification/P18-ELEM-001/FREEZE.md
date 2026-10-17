# P18-ELEM-001 — the qualification freeze and the run

```text
SUBJECT:  the tree that was qualified, recorded before the first build, and
          what the run measured
METHOD:   the eight-path fingerprint every BetterCAD qualification since P11
          has used -- a scratch git index seeded with `read-tree HEAD` and then
          `add -A` over apps include src tests examples cmake CMakeLists.txt
          CMakePresets.json. docs/ and deps/ are outside it by construction.
```

## The frozen candidate

Recorded 2026-10-17 23:57:03, after the pre-freeze run (debug-ext, full
unfiltered ctest, 4240 of 4240, 0 warnings, 35m50s) and before the first
qualification stage. The qualification run recomputes the same eight hashes
before its first configure and after its last repeat, and the commit script
refuses to commit unless the eight it recomputes equal these.

```text
HEAD at the freeze   d4800f79e394d8a893526707a82c68b1600095df
scratch tree         ed577c183376f8fe0d688624bf124fa88e8d36cb

apps                 a95876bc6dbf843d4e0cec114059bc196fa16175
include              655275dd348a91fe20bb771157aca08077fc70a4
src                  762b0a9c6d4430bf17309b8ae9477604bba4193e
tests                6dd667361cbc723897342d956d8c6b9dbf64b8d1
examples             a955cf6511ce715fcb95bc06ef82b877535869e6
cmake                5a382115d7ed7775189d40b55e40f318eaae6cc7
CMakeLists.txt       3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd
CMakePresets.json    3229f0f89b236744d7d5c082bed47c28b718288e

git status at the freeze
   M src/thermal/CMakeLists.txt
   M tests/CMakeLists.txt
   M tests/cli/ZeroMatchGuard.cmake
   M tests/compile_fail/CMakeLists.txt
  ?? docs/verification/P18-ELEM-001/
  ?? include/bettercad/thermal/ThermalElement.hpp
  ?? src/thermal/ThermalElement.cpp
  ?? tests/compile_fail/ThermalElementMisuse.cpp
  ?? tests/thermal/ThermalElementTests.cpp
  (.claude/ untracked, outside the fingerprint, not committed)
```

The mutation probes ran BEFORE this freeze, against the kernel as it stands
in it; the source restored after each probe was grepped for every mutant
fragment and for every original line. Two things were added to the test
file between the two mutation batches, both pure additions of checks: a
diagnostic assertion and the bit-fingerprint case. No production line
changed after the first mutation batch began.

## What moved, against P18-DOF-001's qualified tree

```text
include   f5477fa0 -> 655275dd   + thermal/ThermalElement.hpp
src       1cc3182e -> 762b0a9c       + thermal/ThermalElement.cpp;
                                            thermal/CMakeLists.txt (1 source)
tests     023734eb -> 6dd66736     + thermal/ThermalElementTests.cpp (60 cases);
                                            + compile_fail/ThermalElementMisuse.cpp (17
                                            cases); tests/CMakeLists.txt (1 source);
                                            compile_fail/CMakeLists.txt (1 group);
                                            cli/ZeroMatchGuard.cmake (9 filters)
apps, examples, cmake, CMakeLists.txt, CMakePresets.json   unchanged
```

No file under `src/core`, `src/features`, `src/meshing`, `src/structural`
or `apps/` changed: P16's mesh and P17's element are consumed as conventions,
not extended, so every P16 and P17 test in the unfiltered runs below is the
predecessor regression and is expected to pass exactly as it did for
P18-DOF-001.

## The runs — TWO, and why the second is the qualifying one

Launched through `launch.cmd` → `run-qualification.cmd` → `qualify.cmd`
(d313a640, the thirty-sixth consecutive milestone on that byte-identical
harness; `verify-harness.cmd` 11855b8f PASS beforehand; every line of
`run-qualification.cmd` begins rem/set/call/echo/setlocal/endlocal/@echo,
checked beforehand), detached from the session so a tool timeout cannot touch
it. Both runs are on the frozen tree above; both recorded the same eight
hashes before their first configure and after their last repeat.

### Run 1 — 2026-10-17 23:57:42 to 2026-10-18 03:53:41, ONE STAGE FAILED

```text
debug-ext         build 0  no-op 0  ctest 0   4240/4240   2165.69 s
release-ext       build 0  no-op 0  ctest 0   4240/4240   1944.09 s
debug-shared-ext  build 0  no-op 0  ctest 8   4239/4240   2117.50 s   <-- the failure
repeat release-ext  1337 x 5   0   1111.72 s      repeat debug-ext  1337 x 5   0   1376.85 s
17 stages, 16 exit 0, 1 exit 8                                   3h55m59s
```

The one failed case, and nothing else:

```text
unit.DrawingCli_TheReportKeepsItsColumnsApartWhateverAnObjectIsCalled   (debug-shared-ext)
  tests/cli/DrawingCliTests.cpp:129  REQUIRE( run.exitCode == ExitCode::Success )  1 == 0
  bettercad-cli annotation-add: cannot replace
  'C:\Users\hasib\AppData\Local\Temp\bettercad-test-a9c17305-...\part.bcad':
  Permission denied
```

That is the Windows sharing-violation signature this project has recorded
since P14 (P14-DIM-001, -ANNO-001, -BOM-001, -REFMOD-001, -QUAL-001,
P15-UNITS-001 and the INFRA-QT-DEPLOY-001 investigation): one
`std::filesystem::rename` over a file the same test had just written, in
`writeFileAtomically` (`src/io/FileIo.cpp`), failing when another process
(an indexer or scanner) briefly holds the new file. INFRA-QT-DEPLOY-001
recorded that the fault is avoided for build output and NOT fixed in the
product, and left it as an open item. It is unrelated to this milestone: no
file under `src/io`, `src/drawing` or `apps/` is in this change, and the
other 4239 cases of that stage passed.

**Evidence that it is the flake and not a regression**, gathered before
deciding anything:

```text
the failing case in isolation, same preset, same tree, 30 consecutive runs:   30 passed, 0 failed
the same case in the other two presets, run 1:                                passed in both
the same case in run 2, all three presets:                                    passed in all three
apps, src/io, src/drawing and tests/cli/DrawingCliTests.cpp against HEAD:    byte-identical
                                                                              (the same sources that
                                                                              passed it for P18-DOF-001)
```

**What was NOT done.** Nothing was excluded, retried inside the harness,
loosened or re-labelled; `ctest -j 8` was not reduced; no security or
scanner setting on the machine was touched. The harness requires one run in
which every stage exits 0, so run 1 is **not** counted as a pass. Its twenty
generated files are preserved unmodified under `qualification/run1-failed/`.
The tree was unchanged (fingerprint recorded again after run 1) and the whole
qualification was rerun from a fresh configure. This is two attempts at one
frozen candidate, both disclosed; had run 2 failed in any stage, the work was
to stop and report rather than choose a result.

### Run 2 — 2026-10-18 03:57:49 to 08:08:24, 0 STAGES FAILED (the qualifying run)

```text
stage                           exit   started    finished    elapsed
debug-ext configure             0      03:57:54   03:57:58
debug-ext clean                 0      03:58:00   03:58:00    695 files
debug-ext build                 0      03:58:00   04:28:36    30m36s   0 warnings, 673 objects
debug-ext no-op rebuild         0      04:28:36   04:28:37    1 edge
debug-ext ctest                 0      04:28:37   05:06:59    4240/4240   2301.56 s
release-ext configure           0      05:06:59   05:07:02
release-ext clean               0      05:07:03   05:07:03    695 files
release-ext build               0      05:07:03   05:44:28    37m25s   0 warnings, 673 objects
release-ext no-op rebuild       0      05:44:28   05:44:29    1 edge
release-ext ctest               0      05:44:29   06:19:17    4240/4240   2087.88 s
debug-shared-ext configure      0      06:19:17   06:19:21
debug-shared-ext clean          0      06:19:22   06:19:23    706 files
debug-shared-ext build          0      06:19:23   06:48:58    29m35s   0 warnings, 673 objects
debug-shared-ext no-op rebuild  0      06:48:58   06:48:59    1 edge
debug-shared-ext ctest          0      06:48:59   07:26:09    4240/4240   2229.75 s
repeat release-ext (1337 x 5)   0      07:26:10   07:46:03    1337/1337   1192.47 s
repeat debug-ext   (1337 x 5)   0      07:46:04   08:08:20    1337/1337   1336.00 s

17 stages, 17 exit 0, 0 failed            03:57:49 -> 08:08:24   4h10m35s
"Qualification passed: every stage exited 0."   qualify.cmd exit 0
```

## The fingerprint, three times (run 2; run 1 recorded the same values)

```text
                     at the freeze   before the first build   after the last repeat
apps                 a95876bc        a95876bc                 a95876bc
include              655275dd        655275dd                 655275dd
src                  762b0a9c        762b0a9c                 762b0a9c
tests                6dd66736        6dd66736                 6dd66736
examples             a955cf65        a955cf65                 a955cf65
cmake                5a382115        5a382115                 5a382115
CMakeLists.txt       3ec3c10f        3ec3c10f                 3ec3c10f
CMakePresets.json    3229f0f8        3229f0f8                 3229f0f8
```

The full forty-character hashes are in `qualification/qualification-times.txt`
(before and after) and in the candidate section above (at the freeze).
Nothing under the eight paths was touched between 23:57:03 on 2026-10-17 and
08:08:24 on 2026-10-18; the evidence under `docs/` was written during both
runs and is outside the fingerprint by construction.

## The commit

The commit script recomputes the eight hashes from a scratch index, refuses
to commit unless each equals the frozen one, refuses any remaining fill-in
token in this directory or in the message, commits, and reads the eight
subtree hashes back out of the commit's tree. Its output, with the commit
hash, is appended to this file by the follow-up documentation commit, because
a commit cannot carry its own hash.

### The commit record

```text
commit    72872de0dc7963e73c89807517c4579b22adb389
subject   BetterCAD: add linear Tet4 thermal element
tree      72ae0b5a3c38cdf76962dc30d123a893e7e774a5
parent    d4800f79e394d8a893526707a82c68b1600095df   (P18-DOF-001's record commit)
date      2026-10-18 08:12:12 +1000
files     79 files changed, 123573 insertions(+), 45 deletions(-)
          ROADMAP.md                                                                              |     4 +- 1   TODO.md                                                                                 |   116 +- 1   docs 69   include 1   src 2   tests 5

recomputed from a scratch index immediately before the commit
  apps               a95876bc6dbf843d4e0cec114059bc196fa16175   == frozen
  include            655275dd348a91fe20bb771157aca08077fc70a4   == frozen
  src                762b0a9c6d4430bf17309b8ae9477604bba4193e   == frozen
  tests              6dd667361cbc723897342d956d8c6b9dbf64b8d1   == frozen
  examples           a955cf6511ce715fcb95bc06ef82b877535869e6   == frozen
  cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7   == frozen
  CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd   == frozen
  CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e   == frozen

read back from 72872de0^{tree} after the commit
  all eight                                                     == frozen
  "ALL EIGHT == FROZEN"

untracked left behind   .claude/ only (the session's settings, never committed)
message                 no BOM (first bytes 66,101,116), no fill-in token
```

qualified == committed: the tree the three presets built and tested in the
qualifying run (2026-10-18 03:57:49 to 08:08:24) is, under every one of the
eight paths, the tree commit 72872de0 carries. The files under `docs/` are this
directory: the evidence, the harness copies, the mutation scripts and
summaries, the logs of the qualifying run, and the preserved logs of the
first attempt.

```text
origin/main at the time of this record   a3b721b150338cc1cbd24ee592a11a70a6196c0c
unpushed, oldest first                   7e7578f  e6fded3  314166e  843469a
                                         448eb5e  d40001c  daea78b  d4800f7
                                         72872de  (+ this record)
```

The push is the owner's; this record is written before it and does not
claim it.