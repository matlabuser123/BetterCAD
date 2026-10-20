# P18-SOLVE-001 — the qualification freeze and the run

```text
SUBJECT:  the tree that was qualified, recorded before the first build, and what the run measured
METHOD:   the eight-path fingerprint every BetterCAD qualification since P11 has used -- a scratch
          git index seeded with `read-tree HEAD` and then `add -A` over apps include src tests
          examples cmake CMakeLists.txt CMakePresets.json. docs/ and deps/ are outside it by
          construction.
```

## The frozen candidate (the qualifying one)

Recorded 2026-10-20 15:08:57, after the pre-freeze run (debug-ext, full unfiltered ctest, 4638 of 4638,
0 failed, 55m49s after a 2m21s build), the two mutation batches and the two test additions they forced,
and an early build of the shared-library preset (the new tests and the P17 suite pass in it); before the
first stage of the qualifying run. The run recomputes the same eight hashes before its first configure
and after its last repeat, and the commit script refuses to commit unless the eight it recomputes equal
these.

```text
QUALIFIED_CANDIDATE_HEAD   241c13aba2701e661e355c5a310d3d04c06709b4
QUALIFIED_CANDIDATE_TREE   db7b2362999dd926a71a5adbfa6c6d0d4fff2bae   (scratch tree)

apps                 a95876bc6dbf843d4e0cec114059bc196fa16175
include              691d479cd800f40768b42b24856af83ddc5b3b2a
src                  9ae037dabc7d2433a40ff22be9f4d03f265c2427
tests                2c2d5cedaab22fe059bc02cc57a2fc201e06e6f6
examples             a955cf6511ce715fcb95bc06ef82b877535869e6
cmake                5a382115d7ed7775189d40b55e40f318eaae6cc7
CMakeLists.txt       3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd
CMakePresets.json    3229f0f89b236744d7d5c082bed47c28b718288e

SOURCE_TEST_FINGERPRINT                      the `tests` hash above, 2c2d5ced...
P18_STEADY_SOLVER_CONTRACT_FINGERPRINT       e9d68f4008bbf22ee44d4bfe1d45a68411ad5501
    git hash-object of the two lines
      ThermalSolve.hpp      7e2e0c43342043e0f675b83d37be722cfae0756c
      SymmetricSolve.hpp    55f84e2f2cf20886080c15990dae1ba8d674c236
    (the two public headers: every type, signature and contract comment a later milestone may rely on)
ADR_050_DEPENDENCY_FINGERPRINT               7c0158819237c593852bea9563fd38caa0ab374e
    git hash-object of the five lines
      src/thermal/CMakeLists.txt       500c77c30a3c7538e9b23ccf84f71e3466b756d7
      src/linalg/CMakeLists.txt        6ddd2cf742ed6e49b7500283d4d392446f37971b
      src/structural/CMakeLists.txt    1f7bf0b4bec0aadba989e244cedd02ef5c1f13b8
      CheckLayering.cmake              505897ecff3c3cad62370e785ee379e78a5884c4
      CheckTargetDependencies.cmake    987e0536567bfea1565318aa616218bd9a374805
    (the link lists and the two checkers: the dependency graph ADR-050 and ADR-052 constrain)
implementation blobs   ThermalSolve.cpp        304276ca9c1b60480e5907e2fae37bc90799a633
                       SymmetricSolve.cpp      3b0895bd8c204b71a12df574241819126a0d0008
                       StructuralSolve.cpp     deff2301440a25d6a52f67b160ab49bbf9f54f77
test blobs             ThermalSolveTests.cpp          0493cac1340b6a5d98b26a09db75ecc229d55720
                       ThermalSolveMappedTests.cpp    e26897077052ed1c4e136a3a02aaf9210b5b470d
                       ThermalSolveTestSupport.hpp    aa70ea30ca47f441c1161d0ce7ef954b9b1d522b
                       SymmetricSolveTests.cpp        591b5d4e8434c29fb64ebf836d06dfa1e26c5fa4
                       ThermalSolveMisuse.cpp         ece40e0fb9d717d400e9ed0a327ac97ef31114db

git diff --check            exit 0
whitespace audit            the 27 changed or new files under the eight paths: no BOM, no CR, no trailing
                            whitespace, final newline (eleven files written with a BOM and a stray CRLF by
                            the editing tool were normalised before the freeze)
```

## What moved, against P18-ASSEMBLY-001's qualified tree

```text
include   97c83ec2 -> 691d479c   + linalg/SymmetricSolve.hpp, thermal/ThermalSolve.hpp; ThermalData.hpp (two comments)
src       0f4d1f8f -> 9ae037da   + linalg/ (cpp, CMake); thermal/ThermalSolve.cpp; structural/StructuralSolve.cpp (kernel -> wrapper);
                                   structural, thermal and src CMake lists
tests     94c593a4 -> 2c2d5ced   + thermal/ThermalSolve{Tests,MappedTests,TestSupport}, linalg/SymmetricSolveTests (61 cases);
                                   compile_fail/ThermalSolveMisuse.cpp (26 cases); architecture/CheckTargetDependencies.cmake,
                                   4 fixtures and 5 registrations; CheckLayering.cmake (layer_linalg 5); ZeroMatchGuard.cmake (13 filters)
apps, examples, cmake, CMakeLists.txt, CMakePresets.json   unchanged
```

**A qualified module changed.** The only structural source change is `StructuralSolve.cpp` (the kernel
body replaced by a wrapper) and one `PRIVATE_LINK` line. No public structural header and no structural
test changed. P17-SOLVE-001 and everything built on it (reaction, validation, post, the commands and the
reference models) are therefore REQUALIFIED by the unfiltered runs below, in all three presets: 415
structural cases, 271 414 assertions, identical.

## The run — ONE, and it passed

Launched through `launch.cmd` → `run-qualification.cmd` → `qualify.cmd` (d313a640, the fortieth
consecutive milestone on that byte-identical harness; `verify-harness.cmd` 11855b8f PASS beforehand; every
line of `run-qualification.cmd` begins rem/set/call/echo/setlocal/endlocal/@echo, checked beforehand: 0
other lines, and 0 lines whose `rem` runs into the next word), detached from the session so a tool timeout
cannot touch it, with `PATH` set in the launching call. No file under the eight paths was touched while it
was active.

```text
stage                           exit   started    finished    elapsed
debug-ext configure             0      15:10:10   15:10:14
debug-ext clean                 0      15:10:16   15:10:16
debug-ext build                 0      15:10:16   15:42:15    31m59s   0 warnings, 693 objects
debug-ext no-op rebuild         0      15:42:15   15:42:16    1 edge
debug-ext ctest                 0      15:42:16   16:38:10    4638/4638   3353.68 s
release-ext configure           0      16:38:10   16:38:15
release-ext clean               0      16:38:16   16:38:16
release-ext build               0      16:38:16   17:17:34    39m18s   0 warnings, 693 objects
release-ext no-op rebuild       0      17:17:34   17:17:35    1 edge
release-ext ctest               0      17:17:35   18:09:12    4638/4638   3096.17 s
debug-shared-ext configure      0      18:09:12   18:09:16
debug-shared-ext clean          0      18:09:18   18:09:18
debug-shared-ext build          0      18:09:18   18:40:24    31m06s   0 warnings, 693 objects
debug-shared-ext no-op rebuild  0      18:40:24   18:40:25    1 edge
debug-shared-ext ctest          0      18:40:25   19:36:00    4638/4638   3334.94 s
repeat release-ext (1623 x 5)   0      19:36:01   19:57:49    1623/1623   1307.52 s
repeat debug-ext   (1623 x 5)   0      19:57:50   20:22:08    1623/1623   1457.45 s

17 stages, 17 exit 0, 0 failed            2026-10-20 15:10:06 -> 20:22:14   5h12m08s
"Qualification passed: every stage exited 0."   qualify.cmd exit 0
```

The repeat set selected 1623 cases (the 1557 of P18-ASSEMBLY-001, the 51 ThermalSolve cases, the 10 Linalg
cases and the 5 new architecture tests). 4638 is the 4546 of the predecessor plus 51, 10, 26 and 5. The 693
objects are the 687 of the predecessor plus six (the two new sources, the three new test files and the
compile-fail control). Harness stderr: 266 lines, all of them the harmless `CRLF will be replaced by LF`
notices git prints for a Windows working copy; **no other line** (the previous milestone's one stray
`remrem` line was not repeated).

## The fingerprint, three times

```text
                     at the freeze   before the first build   after the last repeat
apps                 a95876bc        a95876bc                 a95876bc
include              691d479c        691d479c                 691d479c
src                  9ae037da        9ae037da                 9ae037da
tests                2c2d5ced        2c2d5ced                 2c2d5ced
examples             a955cf65        a955cf65                 a955cf65
cmake                5a382115        5a382115                 5a382115
CMakeLists.txt       3ec3c10f        3ec3c10f                 3ec3c10f
CMakePresets.json    3229f0f8        3229f0f8                 3229f0f8
```

The full forty-character hashes are in `qualification/qualification-times.txt` (before and after) and in
the candidate section above (at the freeze). Nothing under the eight paths was touched between
2026-10-20 15:08:57 and 20:22:14.

## After the run

`qualification/postrun-summary.txt` (script `postrun-solve.ps1`) measured, on each preset's own binaries:
the layering checker (525 files, 0 violations), warnings and errors in each build log (0 and 0), the no-op
rebuilds (1 edge each), the clean counts, the per-filter ctest counts, the per-preset results of the
ThermalSolve, Linalg, ThermalAssembly, ThermalBC, Dof, Material, Data and **structural** suites, the bit
fingerprints, and a sorted comparison of the 60 message lines of the solve suite between presets (0
differences, timings normalised). One cosmetic slip: the script's closing line reads
`postrun-solvesembly finished` (a global replace of `postrun-as` hit `postrun-assembly`); no measurement
is affected.

Binary hashes, for the record:

```text
debug-ext         bettercad_tests.exe  1 272 068 224 B  9091f2a7925caea06b0952f1f53a25160ae33fb9a1dcebe3662797de67cc65ad
release-ext       bettercad_tests.exe     53 038 248 B  7db5a935b3f0223f1405718bae635c30eb4d184020ae4643ae0ab3dbafddd973
debug-shared-ext  bettercad_tests.exe    916 028 358 B  054d90a2a56f908746bf118263b67bbade29096c77176eba1ee2073be4aecf8c
debug-shared-ext  libbettercad_thermal.dll   25 637 001 B  663d15cf970ad5707fb44af12d6d7f5821733ddeaae0f8004c169c347b74c972   (12 libbettercad_*.dll, the new one is libbettercad_linalg.dll)
```

## The commit

The commit script recomputes the eight hashes from a scratch index, refuses to commit unless each equals
the frozen one, refuses any remaining fill-in token in this directory or in the message, commits, and reads
the eight subtree hashes back out of the commit's tree. Its output, with the commit hash, is appended to
this file by the follow-up documentation commit, because a commit cannot carry its own hash.