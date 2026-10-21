# P18-POST-001 — the qualification freeze and the run

```text
SUBJECT:  the tree that was qualified, recorded before the first build, and what the run measured
METHOD:   the eight-path fingerprint every BetterCAD qualification since P11 has used -- a scratch
          git index seeded with `read-tree HEAD` and then `add -A` over apps include src tests
          examples cmake CMakeLists.txt CMakePresets.json. docs/ and deps/ are outside it by
          construction.
```

## The frozen candidate (the qualifying one)

Recorded 2026-10-21 17:10:39, after the pre-freeze run (debug-ext, full unfiltered ctest, 4820 of 4820, 0 failed, after a 2m18s build), the mutation passes and the test
additions they forced, and an early build and run of the shared-library preset (the 80 `[post]`-tagged cases, which include this milestone's, passed in it); before the first
stage of the qualifying run. The only edits after the pre-freeze build were whitespace: a blank line between two test cases in `ThermalPostTests.cpp` (the pre-freeze
binary was built the minute before) and, earlier, the re-aligned continuation line of `recoverThermalElementFields` in the header and the source. The mutation probes restored the
production file to the byte (SHA-256 recorded in `qualification/mutations/`; the second hash is the file after the alignment edit). The run recomputes the same eight hashes
before its first configure and after its last repeat, and the commit script refuses to commit unless the eight it recomputes equal these.

```text
QUALIFIED_CANDIDATE_HEAD   7a9ec06f76e72fab8c1fa4bfeebe2593ccfef453
QUALIFIED_CANDIDATE_TREE   4f821e821379c15dd15798b6c68bb59a89d8b7ce   (scratch tree)

apps                 a95876bc6dbf843d4e0cec114059bc196fa16175
include              6139370a9603d494069544f0ebbc746638f0a462
src                  9826f86b4822fe3560923e5091a8be4d1dc00ed6
tests                3465f39da6c67b9c05d8adea634c1a6125cc169d
examples             a955cf6511ce715fcb95bc06ef82b877535869e6
cmake                5a382115d7ed7775189d40b55e40f318eaae6cc7
CMakeLists.txt       3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd
CMakePresets.json    3229f0f89b236744d7d5c082bed47c28b718288e

SOURCE_TEST_FINGERPRINT                      the `tests` hash above, 3465f39d...
P18_POST_CONTRACT_FINGERPRINT                7ad133be753f511d3c42953d5ca851562e3b3797
    git hash-object of the three lines
      ThermalPost.hpp      22a1933bf4d9e2c30f28e9647849eaa42c775298
      ThermalElement.hpp   a37d1e5561d013da8e3fed7027916fd9c05eb8fd
      ThermalData.hpp      c13722336f4d6cc9e2b20d16415c5154464f81c3
    (the public header a later milestone may rely on, with the two headers whose types it returns; the second and third are the SAME blobs as at HEAD)
FIELD_DESCRIPTOR_FINGERPRINT                 3860e3426442a4e660c9a5c1b16f46724876a1a8
    git hash-object of the four `descriptorOf` cases (location, components, dimension, unit of each field)
FOURIER_LINE_FINGERPRINT                     54f0e63e0a6775cf743199a2a18c65e3064f4f0a
    git hash-object of the three lines that write q = -k grad(T) (the one place the law is written)
      HeatFlux3 flux{.x = HeatFlux{} - conductivity * gradient.x,
                     .y = HeatFlux{} - conductivity * gradient.y,
                     .z = HeatFlux{} - conductivity * gradient.z};
RECOVERY_OWNERSHIP_FINGERPRINT               1cdb2b750020345057f21cf6cf5e5f986c2bb303
    git hash-object of the ownership gate and its fixtures, one "path blob" line each (14 files)
ADR_050_DEPENDENCY_FINGERPRINT               632af6215a2fbd384f1dee41bdf0368da0b57629
    git hash-object of the five lines
      src/thermal/CMakeLists.txt       75b5e164b877ea95972be01c30e1c125f0735dd0   (+ ThermalPost.cpp)
      src/linalg/CMakeLists.txt        6ddd2cf742ed6e49b7500283d4d392446f37971b   (unchanged)
      src/structural/CMakeLists.txt    1f7bf0b4bec0aadba989e244cedd02ef5c1f13b8   (unchanged)
      CheckLayering.cmake              505897ecff3c3cad62370e785ee379e78a5884c4   (unchanged)
      CheckTargetDependencies.cmake    987e0536567bfea1565318aa616218bd9a374805   (unchanged)
implementation blob    ThermalPost.cpp                  29f81073ba27a67a348a7e98e2c54cfc2eec50b6
test blobs             ThermalPostTests.cpp             28ceae341b3e978cffb8b06df296be61ab202b6b
                       ThermalPostMappedTests.cpp       0dc3be4b1eca6d671cc4bc64237912670d00b9fd
                       ThermalPostTestSupport.hpp       f0c17f94c49442805e4e901951925307c7f3b723
                       ThermalPostMisuse.cpp            1f44733cd822591056825f842e44d9ed5c9a764b

UNTOUCHED (byte-identical to HEAD, compared by blob):
      src/thermal/ThermalSolve.cpp                      304276ca9c1b60480e5907e2fae37bc90799a633
      include/bettercad/thermal/ThermalSolve.hpp        7e2e0c43342043e0f675b83d37be722cfae0756c
      src/thermal/ThermalTransient.cpp                  4849e68d3628c5a021c356142b03db26bfa50c54
      include/bettercad/thermal/ThermalTransient.hpp    9b69f1f2089b383f0aad474c718f17fd4f2f8e36
      src/thermal/ThermalElement.cpp                    c6b85fffab477b2828ef9c9e0300027bc83bc3fa
      include/bettercad/thermal/ThermalElement.hpp      a37d1e5561d013da8e3fed7027916fd9c05eb8fd
      include/bettercad/thermal/ThermalData.hpp         c13722336f4d6cc9e2b20d16415c5154464f81c3
      src/thermal/ThermalData.cpp                       e23fb2014da40c9416f96c4eed5dbcd39caad420
      src/thermal/ThermalDof.cpp                        56be927c6923e7184720c7343d7744f64ba2d0ea
      include/bettercad/thermal/ThermalDof.hpp          fc68a94a213e17015ea52570a9943a439c2823ae
      src/linalg/SymmetricSolve.cpp                     3b0895bd8c204b71a12df574241819126a0d0008
      include/bettercad/linalg/SymmetricSolve.hpp       55f84e2f2cf20886080c15990dae1ba8d674c236
      src/structural/StructuralSolve.cpp                deff2301440a25d6a52f67b160ab49bbf9f54f77

git diff --check            exit 0
whitespace audit            the 26 changed or new files under the eight paths: no BOM, no CR, no trailing
                            whitespace, final newline
```

## What moved, against P18-TRANSIENT-001's qualified tree

```text
include   4757ad6f -> 6139370a   + thermal/ThermalPost.hpp
src       2d17a75f -> 9826f86b   + thermal/ThermalPost.cpp; thermal/CMakeLists.txt (one source)
tests     97eebe01 -> 3465f39d   + thermal/ThermalPost{Tests,MappedTests,TestSupport}, compile_fail/ThermalPostMisuse.cpp (40 + 31 cases),
                                   architecture/CheckRecoveryOwnership.cmake + 3 fixtures (4 tests); CMakeLists.txt, compile_fail/CMakeLists.txt
                                   (one group), architecture/CMakeLists.txt, cli/ZeroMatchGuard.cmake (13 filters)
apps, examples, cmake, CMakeLists.txt, CMakePresets.json   unchanged
```

**No qualified module changed.** Every solver, the generic kernel and the structural wrapper are the same bytes as in P18-TRANSIENT-001's qualified tree, so P17 and
P18-SOLVE / TRANSIENT are re-RUN by the unfiltered suites below, not requalified: 415 structural cases, 271 414 assertions, identical in all three presets.

## The run — ONE, and it passed

Launched through `launch.cmd` → `run-qualification.cmd` → `qualify.cmd` (d313a640, the forty-second consecutive milestone on that byte-identical harness;
`verify-harness.cmd` 11855b8f PASS beforehand; every line of `run-qualification.cmd` begins rem/set/call/echo/setlocal/endlocal/@echo, checked beforehand: 0 other lines, and
0 lines whose `rem` runs into the next word), detached from the session so a tool timeout cannot touch it, with `PATH` set in the launching call. No file under the eight paths
was touched while it was active.

```text
stage                           exit   started    finished    elapsed
debug-ext configure             0      17:12:06   17:12:15
debug-ext clean                 0      17:12:18   17:12:18
debug-ext build                 0      17:12:18   17:42:41    30m23s   0 warnings, 701 objects
debug-ext no-op rebuild         0      17:42:41   17:42:43    1 edge
debug-ext ctest                 0      17:42:43   18:47:46    4820/4820   3904.09 s
release-ext configure           0      18:47:46   18:47:52
release-ext clean               0      18:47:53   18:47:53
release-ext build               0      18:47:53   19:25:51    37m58s   0 warnings, 701 objects
release-ext no-op rebuild       0      19:25:51   19:25:52    1 edge
release-ext ctest               0      19:25:52   20:27:09    4820/4820   3676.48 s
debug-shared-ext configure      0      20:27:09   20:27:14
debug-shared-ext clean          0      20:27:16   20:27:16
debug-shared-ext build          0      20:27:16   20:56:50    29m34s   0 warnings, 701 objects
debug-shared-ext no-op rebuild  0      20:56:50   20:56:52    1 edge
debug-shared-ext ctest          0      20:56:52   22:03:47    4820/4820   4014.46 s
repeat release-ext (1739 x 5)   0      22:03:48   22:25:55    1739/1739   1326.86 s
repeat debug-ext   (1739 x 5)   0      22:26:06   22:50:51    1739/1739   1484.92 s

17 stages, 17 exit 0, 0 failed            2026-10-21 17:12:03 -> 22:51:02   5h38m59s
"Qualification passed: every stage exited 0."   qualify.cmd exit 0
```

The repeat set selected 1739 cases (the 1695 of P18-TRANSIENT-001, the 40 ThermalPost cases and the 4 ownership tests, which the `architecture` alternative picks up). 4820 is the
4745 of the predecessor plus 40, 31 and 4. The 701 objects are the 697 of the predecessor plus four (the new source, the two new test files and the compile-fail control).
Harness stderr: 266 lines, all of them the harmless `CRLF will be replaced by LF` notices git prints for a Windows working copy; **no other line**.

## The fingerprint, three times

```text
                     at the freeze   before the first build   after the last repeat
apps                 a95876bc        a95876bc                 a95876bc
include              6139370a        6139370a                 6139370a
src                  9826f86b        9826f86b                 9826f86b
tests                3465f39d        3465f39d                 3465f39d
examples             a955cf65        a955cf65                 a955cf65
cmake                5a382115        5a382115                 5a382115
CMakeLists.txt       3ec3c10f        3ec3c10f                 3ec3c10f
CMakePresets.json    3229f0f8        3229f0f8                 3229f0f8
```

The full forty-character hashes are in `qualification/qualification-times.txt` (before and after) and in the candidate section above (at the freeze). Nothing under the eight
paths was touched between 2026-10-21 17:10:39 and 22:51:02.

## After the run

`qualification/postrun-summary.txt` (script `postrun-post.ps1`) measured, on each preset's own binaries: the layering checker (529 files, 0 violations), warnings and errors in each
build log (0 and 0), the no-op rebuilds (1 edge each), the clean counts, the per-filter ctest counts (identical in the three presets), the per-preset results of the ThermalPost,
ThermalTransient, ThermalSolve, Linalg, ThermalAssembly, ThermalBC, Dof, Material, Data and **structural** suites (all pass, same case and assertion counts in the three), the bit
fingerprints (`6ba1d58f018354ca` and `262ce7759cb1d043`, identical in all three presets), and a sorted comparison of the 52 message lines of the recovery suite between presets
(0 differences, timings normalised).

Binary hashes, for the record:

```text
debug-ext         bettercad_tests.exe  1 296 059 404 B  d9f4d8b5f96c79d393843dded8977e9b3e0e6f97a7ccfe85c4db81740c0f619d
release-ext       bettercad_tests.exe     54 438 769 B  08c1dcd90aae331e47b87aee7d24bbf8432f1ce544b3d1d9cac9882559d7587d
debug-shared-ext  bettercad_tests.exe    934 039 957 B  ef42392eaa888b72269d7eda46eaafbfee521048a2a4e792ea4116625cab6be5
debug-shared-ext  libbettercad_thermal.dll   31 916 316 B  63d2137b8585d284cd3f4e42b2b1c9ac4334d7a9ccb44054983a1dbfa1dd8413   (12 libbettercad_*.dll)
```

## The commit

The commit script recomputes the eight hashes from a scratch index, refuses to commit unless each equals the frozen one, refuses any remaining fill-in token in this directory
or in the message, commits, and reads the eight subtree hashes back out of the commit's tree. Its output, with the commit hash, is appended to this file by the follow-up
documentation commit, because a commit cannot carry its own hash.

### The commit record

```text
commit    35839d3f70165d0f7a0aa2e8790afb7f11a93c1f
subject   BetterCAD: add thermal result recovery
tree      977680b308777d831a4290419dedd9c9fd95a41c
parent    7a9ec06f76e72fab8c1fa4bfeebe2593ccfef453   (P18-TRANSIENT-001's record commit)
date      2026-10-21 23:36:13 +1000
files     139 files changed, 73466 insertions(+), 43 deletions(-)
          docs 111   include 1   src 2   tests 23   ROADMAP.md 1   TODO.md 1

recomputed from a scratch index immediately before the commit
  apps               a95876bc6dbf843d4e0cec114059bc196fa16175   == frozen
  include            6139370a9603d494069544f0ebbc746638f0a462   == frozen
  src                9826f86b4822fe3560923e5091a8be4d1dc00ed6   == frozen
  tests              3465f39da6c67b9c05d8adea634c1a6125cc169d   == frozen
  examples           a955cf6511ce715fcb95bc06ef82b877535869e6   == frozen
  cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7   == frozen
  CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd   == frozen
  CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e   == frozen

read back from 35839d3f^{tree} after the commit
  all eight                                                     == frozen
  "ALL EIGHT == FROZEN"

untracked left behind   .claude/ only (the session's settings, never committed)
message                 no BOM (first bytes 66,101,116), no fill-in token
```

qualified == committed: the tree the three presets built and tested in the qualifying run (2026-10-21
17:12:03 to 22:51:02) is, under every one of the eight paths, the tree commit 35839d3f carries. The
files under `docs/` are this directory: the evidence, the harness copies, the mutation scripts, drivers
and logs, the logs of the qualifying run and the post-run measurements.

```text
origin/main at the time of this record   7a9ec06f76e72fab8c1fa4bfeebe2593ccfef453
unpushed, oldest first                   35839d3f  (this milestone)  (+ this record)
```

The push is the owner's; this record is written before it and does not claim it.