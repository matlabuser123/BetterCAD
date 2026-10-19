# P18-ASSEMBLY-001 — the qualification freeze and the run

```text
SUBJECT:  the tree that was qualified, recorded before the first build, and
          what the run measured
METHOD:   the eight-path fingerprint every BetterCAD qualification since P11
          has used -- a scratch git index seeded with `read-tree HEAD` and then
          `add -A` over apps include src tests examples cmake CMakeLists.txt
          CMakePresets.json. docs/ and deps/ are outside it by construction.
```

## The frozen candidate (the qualifying one)

Recorded 2026-10-19 21:26:23, after the pre-freeze run (debug-ext, full unfiltered
ctest, 4546 of 4546, 0 failed, 53m06s after a 2m23s build) and after the mutation
batches and the five test additions they forced; before the first stage of the
qualifying run. The run recomputes the same eight hashes before its first configure and
after its last repeat, and the commit script refuses to commit unless the eight it
recomputes equal these.

```text
QUALIFIED_CANDIDATE_HEAD   a5b8cc2c13b4c786885f743d1f621349df58377c
QUALIFIED_CANDIDATE_TREE   11c861209cd211b01130f69f02c104cb5db99c0e   (scratch tree)

apps                 a95876bc6dbf843d4e0cec114059bc196fa16175
include              97c83ec23d321c4ef611c46554e441e9d54c1864
src                  0f4d1f8f4b189ad914770577f4e24ebc71450041
tests                94c593a4281988e1b9067db1546076267c59d2c2
examples             a955cf6511ce715fcb95bc06ef82b877535869e6
cmake                5a382115d7ed7775189d40b55e40f318eaae6cc7
CMakeLists.txt       3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd
CMakePresets.json    3229f0f89b236744d7d5c082bed47c28b718288e

SOURCE_TEST_FINGERPRINT                      the `tests` hash above, 94c593a4...
P18_THERMAL_ASSEMBLY_CONTRACT_FINGERPRINT    6dab28b5974c865198a20e10468b2a08a39ba60a
    git hash-object of the two lines
      ThermalSystem.hpp   1ddb90cbb3737e2194cf53a4afe5d3291c772382
      ThermalSparse.hpp   204a3ed55b5bbd3cb480e95498db150a8cab9974
    (the two public headers: every type, signature and contract comment a later
    milestone may rely on)
implementation blobs   ThermalSystem.cpp            698741e3b1e0a8b1de389693bac0a751b32b6edd
                       ThermalSparse.cpp            c455b4354474887d27ebb976b6772200d906d10b
test blobs             ThermalAssemblyTests.cpp     cd04fb2141ca3ee03efc664a1691f55b041292ff
                       ThermalAssemblyMappedTests.cpp c681595c7bfa0632fa064bdb3910eed1437aecbc
                       ThermalAssemblyTestSupport.hpp 56c2392fca9ab5b53867081fe2ef86640f913006
                       ThermalSystemMisuse.cpp      c9677b27a70f0c2bfc074d92c311ac8e6f59efbe

git status at the freeze
   M src/thermal/CMakeLists.txt
   M tests/CMakeLists.txt
   M tests/cli/ZeroMatchGuard.cmake
   M tests/compile_fail/CMakeLists.txt
  ?? docs/verification/P18-ASSEMBLY-001/
  ?? include/bettercad/thermal/ThermalSparse.hpp
  ?? include/bettercad/thermal/ThermalSystem.hpp
  ?? src/thermal/ThermalSparse.cpp
  ?? src/thermal/ThermalSystem.cpp
  ?? tests/compile_fail/ThermalSystemMisuse.cpp
  ?? tests/thermal/ThermalAssemblyMappedTests.cpp
  ?? tests/thermal/ThermalAssemblyTestSupport.hpp
  ?? tests/thermal/ThermalAssemblyTests.cpp
  (.claude/ untracked, outside the fingerprint, not committed)
git diff --check            exit 0
whitespace audit            the eight new files: no trailing whitespace, no tab, no CR, final newline
```

## What moved, against P18-BC-001's qualified tree

```text
include   ecc31f14 -> 97c83ec2   + thermal/ThermalSparse.hpp, thermal/ThermalSystem.hpp
src       8a842d89 -> 0f4d1f8f   + thermal/ThermalSparse.cpp, thermal/ThermalSystem.cpp;
                                   thermal/CMakeLists.txt (2 sources)
tests     ce9f67bf -> 94c593a4   + thermal/ThermalAssemblyTests.cpp, ThermalAssemblyMappedTests.cpp,
                                   ThermalAssemblyTestSupport.hpp (62 cases);
                                   + compile_fail/ThermalSystemMisuse.cpp (36 cases);
                                   tests/CMakeLists.txt (3 files);
                                   compile_fail/CMakeLists.txt (1 group);
                                   cli/ZeroMatchGuard.cmake (15 filters)
apps, examples, cmake, CMakeLists.txt, CMakePresets.json   unchanged
```

No file under `src/core`, `src/features`, `src/meshing`, `src/structural` or `apps/`
changed, and no existing thermal header changed. Every P16 and P17 test in the
unfiltered runs below is therefore the predecessor regression and passed exactly as it
did for P18-BC-001. No predecessor milestone needed requalifying (brief section 178).

## The run — ONE, and it passed

Launched through `launch.cmd` → `run-qualification.cmd` → `qualify.cmd` (d313a640, the
thirty-ninth consecutive milestone on that byte-identical harness; `verify-harness.cmd`
11855b8f PASS beforehand; every line of `run-qualification.cmd` begins
rem/set/call/echo/setlocal/endlocal/@echo, checked beforehand: 0 other lines), detached
from the session so a tool timeout cannot touch it. No file under the eight paths was
touched while it was active.

**A launch that did not count.** The first launch, at 21:26:50, exited in under a second
with every configure at 9009 ("not recognized"): the detached process had not been given
`PATH`. Nothing had been built or tested. Its outputs (three one-line configure logs, the
times file, the `.out` and `.err`) were deleted, and the run below was launched again
with `PATH`, `BETTERCAD_BUILD_ROOT` and `CMAKE_BUILD_PARALLEL_LEVEL` set in the same call.
It is recorded here because it was a run that failed, not because it measured anything.

```text
stage                           exit   started    finished    elapsed
debug-ext configure             0      21:27:34   21:27:43
debug-ext clean                 0      21:27:44   21:27:44    709 files
debug-ext build                 0      21:27:44   22:00:25    32m41s   0 warnings, 687 objects
debug-ext no-op rebuild         0      22:00:27   22:00:27    1 edge
debug-ext ctest                 0      22:00:27   22:53:47    4546/4546   3199.58 s
release-ext configure           0      22:53:47   22:53:53
release-ext clean               0      22:53:54   22:53:54    704 files
release-ext build               0      22:53:54   23:34:24    40m30s   0 warnings, 687 objects
release-ext no-op rebuild       0      23:34:26   23:34:26    1 edge
release-ext ctest               0      23:34:26   00:23:39    4546/4546   2952.87 s
debug-shared-ext configure      0      00:23:39   00:23:49
debug-shared-ext clean          0      00:23:51   00:23:51    719 files
debug-shared-ext build          0      00:23:51   00:56:07    32m16s   0 warnings, 687 objects
debug-shared-ext no-op rebuild  0      00:56:10   00:56:10    1 edge
debug-shared-ext ctest          0      00:56:10   01:49:49    4546/4546   3219.09 s
repeat release-ext (1557 x 5)   0      01:49:50   02:11:32    1557/1557   1301.37 s
repeat debug-ext   (1557 x 5)   0      02:11:33   02:36:07    1557/1557   1473.70 s

17 stages, 17 exit 0, 0 failed            2026-10-19 21:27:30 -> 2026-10-20 02:36:15   5h08m45s
"Qualification passed: every stage exited 0."   qualify.cmd exit 0
```

The repeat set selected 1557 cases (the 1495 of P18-BC-001 plus the 62 new
ThermalAssembly cases). 4546 is the 4448 of the predecessor plus 62 plus 36. The 687
objects are the 682 of the predecessor plus five (the two new sources, the two new test
files and the compile-fail control).

Harness stderr: 270 lines. 268 are the harmless `CRLF will be replaced by LF` notices
git prints for a Windows working copy. The other two are one message,
`'remrem' is not recognized as an internal or external command`, from one comment line in
`run-qualification.cmd` whose `rem` had been joined to the next line by a splicing slip
when the file was written (`remrem THIS RUN HAPPENS ON...`). cmd.exe treated it as a
command, printed the error and carried on; it has no effect on any stage, and all 17
exit codes are 0. The file is preserved as it ran. Nothing else was written to stderr.

## The fingerprint, three times

```text
                     at the freeze   before the first build   after the last repeat
apps                 a95876bc        a95876bc                 a95876bc
include              97c83ec2        97c83ec2                 97c83ec2
src                  0f4d1f8f        0f4d1f8f                 0f4d1f8f
tests                94c593a4        94c593a4                 94c593a4
examples             a955cf65        a955cf65                 a955cf65
cmake                5a382115        5a382115                 5a382115
CMakeLists.txt       3ec3c10f        3ec3c10f                 3ec3c10f
CMakePresets.json    3229f0f8        3229f0f8                 3229f0f8
```

The full forty-character hashes are in `qualification/qualification-times.txt` (before
and after) and in the candidate section above (at the freeze). Nothing under the eight
paths was touched between 2026-10-19 21:26:23 and 2026-10-20 02:36:15; the evidence under
`docs/` was written during the run and is outside the fingerprint by construction.

## After the run

`qualification/postrun-summary.txt` (script `postrun-assembly.ps1`) measured, on each
preset's own binaries: the layering checker (521 files, 0 violations), warnings and
errors in each build log (0 and 0), the no-op rebuilds (1 edge each), the clean counts,
the per-filter ctest counts, the per-preset results of the ThermalAssembly, ThermalBC,
ThermalLoad, ThermalElement, Dof, Material, Data and structural-element suites, the bit
fingerprints, and a sorted comparison of the 51 message lines of the new suite between
presets (0 differences, timings normalised).

Binary hashes, for the record:

```text
debug-ext         bettercad_tests.exe  1 259 501 743 B  43786fa6ed14e8fd7e88460d8c24aa7b85ce9e614f372e85df0fe2e2b4944ca9
release-ext       bettercad_tests.exe     52 384 594 B  b2af4c197525a0fac6eb0d6b3e5986dad4b9edacd9443a22290f44895aea9f1a
debug-shared-ext  bettercad_tests.exe    906 916 379 B  53ebbe75190b057405cc157e397d04441b5dd6799e168191c4d2fd3b10c7c798
debug-shared-ext  libbettercad_thermal.dll 22 626 040 B  0f03f3b32ebb1f4eb30de76d378305266ed8f48a0432d28e2ffac14aebab1e16
```

## The commit

The commit script recomputes the eight hashes from a scratch index, refuses to commit
unless each equals the frozen one, refuses any remaining fill-in token in this directory
or in the message, commits, and reads the eight subtree hashes back out of the commit's
tree. Its output, with the commit hash, is appended to this file by the follow-up
documentation commit, because a commit cannot carry its own hash.