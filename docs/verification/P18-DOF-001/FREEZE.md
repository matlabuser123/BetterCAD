# P18-DOF-001 — the qualification freeze and the run

```text
SUBJECT:  the tree that was qualified, recorded before the first build, and
          what the run measured
METHOD:   the eight-path fingerprint every BetterCAD qualification since P11
          has used -- a scratch git index seeded with `read-tree HEAD` and then
          `add -A` over apps include src tests examples cmake CMakeLists.txt
          CMakePresets.json. docs/ and deps/ are outside it by construction.
```

## The frozen candidate

Recorded 2026-10-17 16:26:46, after the pre-freeze run (debug-ext, full
unfiltered ctest, 4163 of 4163, 0 warnings, 33m34s) and before the first
qualification stage. The qualification run recomputes the same eight hashes
before its first configure and after its last repeat, and the commit script
refuses to commit unless the eight it recomputes equal these.

```text
HEAD at the freeze   d40001ca68382ebc71295fcee413cdc9e6f125f7
scratch tree         3c9e3584369c3f8e1109e3fb3379bbb460f9776f

apps                 a95876bc6dbf843d4e0cec114059bc196fa16175
include              f5477fa04f211206185502d87a4c24c494e14d84
src                  1cc3182e1e385e964664fa143516fdec8c969f04
tests                023734eb5ff478258ee5f61487f03902e320fb84
examples             a955cf6511ce715fcb95bc06ef82b877535869e6
cmake                5a382115d7ed7775189d40b55e40f318eaae6cc7
CMakeLists.txt       3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd
CMakePresets.json    3229f0f89b236744d7d5c082bed47c28b718288e

git status at the freeze
   M src/thermal/CMakeLists.txt
   M tests/CMakeLists.txt
   M tests/cli/ZeroMatchGuard.cmake
   M tests/compile_fail/CMakeLists.txt
  ?? docs/verification/P18-DOF-001/
  ?? include/bettercad/thermal/ThermalDof.hpp
  ?? src/thermal/ThermalDof.cpp
  ?? tests/compile_fail/ThermalDofMisuse.cpp
  ?? tests/thermal/ThermalDofTests.cpp
  (.claude/ untracked, outside the fingerprint, not committed)
```

## What moved, against P18-MAT-001's qualified tree

```text
include   5ba4f65a -> f5477fa0   + thermal/ThermalDof.hpp
src       5bd8d50b -> 1cc3182e   + thermal/ThermalDof.cpp;
                                            thermal/CMakeLists.txt (1 source)
tests     1d8b2808 -> 023734eb   + thermal/ThermalDofTests.cpp (33 cases);
                                            + compile_fail/ThermalDofMisuse.cpp (15
                                            cases); tests/CMakeLists.txt (1 source);
                                            compile_fail/CMakeLists.txt (1 group);
                                            cli/ZeroMatchGuard.cmake (2 filters)
apps, examples, cmake, CMakeLists.txt, CMakePresets.json   unchanged
```

No file under `src/core`, `src/features`, `src/meshing`, `src/structural`
or `apps/` changed: P16's mesh and P17's index space are consumed and
mirrored, not extended, so every P16 and P17 test in the unfiltered runs
below is the predecessor regression and is expected to pass exactly as it
did for P18-MAT-001.

## The run

Launched 2026-10-17 16:28:50 through `launch.cmd` → `run-qualification.cmd`
→ `qualify.cmd` (d313a640, the thirty-fifth consecutive milestone on that
byte-identical harness; `verify-harness.cmd` 11855b8f PASS beforehand; every
line of `run-qualification.cmd` begins rem/set/call/echo/setlocal/endlocal/
@echo, checked beforehand). A first launch at 16:27:38 through the session's
tool runner was stopped three minutes in, before its first build finished,
because that runner carries a ten-minute limit; its six partial logs were
deleted and the run below is the detached relaunch, from a fresh configure.

```text
stage                           exit   started      finished     elapsed
debug-ext configure             0      16:28:54     16:28:57
debug-ext clean                 0      16:28:58     16:28:58     20 files
debug-ext build                 0      16:28:58     16:56:53     27m55s   0 warnings, 670 objects
debug-ext no-op rebuild         0      16:56:53     16:56:55     1 edge
debug-ext ctest                 0      16:56:55     17:30:36     4163/4163   2021.49 s
release-ext configure           0      17:30:36     17:30:41
release-ext clean               0      17:30:41     17:30:42     689 files
release-ext build               0      17:30:42     18:04:14     33m32s   0 warnings, 670 objects
release-ext no-op rebuild       0      18:04:14     18:04:15     1 edge
release-ext ctest               0      18:04:15     18:34:36     4163/4163   1821.04 s
debug-shared-ext configure      0      18:34:36     18:34:40
debug-shared-ext clean          0      18:34:40     18:34:42     700 files
debug-shared-ext build          0      18:34:42     19:01:38     26m56s   0 warnings, 670 objects
debug-shared-ext no-op rebuild  0      19:01:38     19:01:39     1 edge
debug-shared-ext ctest          0      19:01:39     19:34:28     4163/4163   1968.49 s
repeat release-ext (1277 x 5)   0      19:34:29     19:52:46     1277/1277   1097.71 s
repeat debug-ext   (1277 x 5)   0      19:52:47     20:12:58     1277/1277   1210.24 s

17 stages, 17 exit 0, 0 failed            16:28:50 -> 20:13:03   3h44m12s
"Qualification passed: every stage exited 0."   qualify.cmd exit 0
```

## The fingerprint, three times

```text
                     at the freeze   before the first build   after the last repeat
apps                 a95876bc        a95876bc                 a95876bc
include              f5477fa0        f5477fa0                 f5477fa0
src                  1cc3182e        1cc3182e                 1cc3182e
tests                023734eb        023734eb                 023734eb
examples             a955cf65        a955cf65                 a955cf65
cmake                5a382115        5a382115                 5a382115
CMakeLists.txt       3ec3c10f        3ec3c10f                 3ec3c10f
CMakePresets.json    3229f0f8        3229f0f8                 3229f0f8
```

The full forty-character hashes are in `qualification/qualification-times.txt`
(before and after) and in the candidate section above (at the freeze).
Nothing under the eight paths was touched between 16:26:46 and 20:13:03;
the evidence under `docs/` was written during the run and is outside the
fingerprint by construction.

## The commit

The commit script recomputes the eight hashes from a scratch index, refuses
to commit unless each equals the frozen one, refuses any remaining
fill-in token in this directory or in the message, commits, and reads
the eight subtree hashes back out of the commit's tree. Its output, with the
commit hash, is appended to this file by the follow-up documentation commit,
because a commit cannot carry its own hash.

### The commit record

```text
commit    daea78be0d8ae3d6bd8b0b504e0c5fd541b5596b
subject   BetterCAD: add thermal temperature DOF model
tree      3ca0a117063cf359a118c2aa6c22dcd793331417
parent    d40001ca68382ebc71295fcee413cdc9e6f125f7   (P18-MAT-001's record commit)
date      2026-10-17 20:22:59 +1000
files     59 changed, 65 279 insertions, 30 deletions
          include 1   src 2   tests 5   docs 49   TODO.md   ROADMAP.md

recomputed from a scratch index immediately before the commit
  apps               a95876bc6dbf843d4e0cec114059bc196fa16175   == frozen
  include            f5477fa04f211206185502d87a4c24c494e14d84   == frozen
  src                1cc3182e1e385e964664fa143516fdec8c969f04   == frozen
  tests              023734eb5ff478258ee5f61487f03902e320fb84   == frozen
  examples           a955cf6511ce715fcb95bc06ef82b877535869e6   == frozen
  cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7   == frozen
  CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd   == frozen
  CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e   == frozen

read back from daea78be^{tree} after the commit
  all eight                                                     == frozen
  "ALL EIGHT == FROZEN"

untracked left behind   .claude/ only (the session's settings, never committed)
message                 no BOM (first bytes 66,101,116), no fill-in token
```

qualified == committed: the tree the three presets built and tested between
16:28:50 and 20:13:03 is, under every one of the eight paths, the tree commit
daea78be carries. The 49 files under `docs/` are this directory: the
evidence, the harness copies, the mutation scripts and summaries, and the
five qualification logs per preset.

```text
origin/main at the time of this record   a3b721b150338cc1cbd24ee592a11a70a6196c0c
unpushed, oldest first                   7e7578f  e6fded3  314166e  843469a
                                         448eb5e  d40001c  daea78b  (+ this record)
```

The push is the owner's; this record is written before it and does not
claim it.
