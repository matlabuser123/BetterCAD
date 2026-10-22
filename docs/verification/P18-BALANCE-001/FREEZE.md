# P18-BALANCE-001 — the qualification freeze and the run

```text
SUBJECT:  the tree that was qualified, recorded before the first build, and what the run measured
METHOD:   the eight-path fingerprint every BetterCAD qualification since P11 has used -- a scratch
          git index seeded with `read-tree HEAD` and then `add -A` over apps include src tests
          examples cmake CMakeLists.txt CMakePresets.json. docs/ and deps/ are outside it by
          construction.
```

## The frozen candidate (the qualifying one)

Recorded 2026-10-22 05:15:16, after the pre-freeze run (debug-ext, full unfiltered ctest, 4927 of 4927, 0 failed, after a 24 s incremental build at 04:11), the mutation passes and the test
additions and removals they forced, and an early build and run of the shared-library preset (which found the missing export of `ThermalHeatBalance`, ADVERSARIAL_REVIEW.md F3); before the first stage of the qualifying run.
The mutation probes restored the production file to the byte (SHA-256 recorded in `qualification/mutations/`; the second hash is the file after the two redundant checks were removed). The run recomputes the same eight hashes
before its first configure and after its last repeat, and the commit script refuses to commit unless the eight it recomputes equal these.

```text
QUALIFIED_CANDIDATE_HEAD   fa15d36f257f698683a2d52fc4f9dcd5f8b7b684
QUALIFIED_CANDIDATE_TREE   14722d52616af83f2a6696448dcf18b0e99bd871   (scratch tree)

apps                 a95876bc6dbf843d4e0cec114059bc196fa16175
include              ee2dd26d23f2a84600bf05a33090bcaaeeb14499
src                  4dd2f59fd14a4ec4b768dff526be706192df8d19
tests                314e71c94b1152ee53cbb13cc96322b134027c06
examples             a955cf6511ce715fcb95bc06ef82b877535869e6
cmake                5a382115d7ed7775189d40b55e40f318eaae6cc7
CMakeLists.txt       3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd
CMakePresets.json    3229f0f89b236744d7d5c082bed47c28b718288e

SOURCE_TEST_FINGERPRINT                      the `tests` hash above, 314e71c9...
P18_BALANCE_CONTRACT_FINGERPRINT             d1925ef3d79f038081924d1c255f48206191921e
    git hash-object of the two lines
      ThermalBalance.hpp   93e1cc10f7c42667a580ffa1f52c2f3a27c27589
      ThermalData.hpp      c13722336f4d6cc9e2b20d16415c5154464f81c3   (the SAME blob as at HEAD)
    (the public header a later milestone may rely on, with the header whose HeatBalance it fills)
P18_BALANCE_SIGN_CONVENTION_FINGERPRINT      2121f672bec9a533d75e30d7b1ea4a17b9fe9960
    git hash-object of the nine lines that write the sign: the stored-energy rate, the residual, the steady and transient full residuals
    (r = K T - Q and r = C dT/dt + K T - Q), the reaction (`heat = fullResidual[dof]`), the convection (`heatInto = -out`), the categories, the actual
    step (`grid.stepAt(k - 1)`) and the net (`netImbalance`)
P18_BALANCE_TOLERANCE_FINGERPRINT            6a6b1de34effcbcf91b62a056eb2152e83149055
    git hash-object of the three constants
      kBalanceAbsoluteToleranceW = 1.0e-12     kBalanceRelativeTolerance = 1.0e-13     kBalanceScaleFloorW = 1.0e-12
BALANCE_OWNERSHIP_FINGERPRINT                9782e55f5dc58314357b06f19377be31e38c313b
    git hash-object of the ownership gate and its fixtures, one "path blob" line each (19 files)
ADR_050_DEPENDENCY_FINGERPRINT               2ae700e12ffc461048a970b7455d342488c3ebff
    git hash-object of the five lines
      src/thermal/CMakeLists.txt       5171f0644902cf7058831b4fc8eea4f5ab09d87a   (+ ThermalBalance.cpp)
      src/linalg/CMakeLists.txt        6ddd2cf742ed6e49b7500283d4d392446f37971b   (unchanged)
      src/structural/CMakeLists.txt    1f7bf0b4bec0aadba989e244cedd02ef5c1f13b8   (unchanged)
      CheckLayering.cmake              505897ecff3c3cad62370e785ee379e78a5884c4   (unchanged)
      CheckTargetDependencies.cmake    987e0536567bfea1565318aa616218bd9a374805   (unchanged)
implementation blob    ThermalBalance.cpp               fcf79d4518a07da2dd37a740aed9dae76dc49e6b
test blobs             ThermalBalanceTests.cpp          e1c838a8ba8155d919704584927eb05f9bd400d2
                       ThermalBalanceMappedTests.cpp    e360aa75b3bf3be48dcdf40d29145f4f3be9d14d
                       ThermalBalanceTestSupport.hpp    0a51ff5cc5992a715c6022f2a4e41a808df75104
                       ThermalBalanceMisuse.cpp         5d37c46b39121925dc543241ce18b63ebaa21742

UNTOUCHED (byte-identical to HEAD, compared by blob; 25 files, every one "EQUAL" in qualification/freeze-balance.txt):
      src/thermal/ThermalSolve.cpp                      304276ca9c1b60480e5907e2fae37bc90799a633
      include/bettercad/thermal/ThermalSolve.hpp        7e2e0c43342043e0f675b83d37be722cfae0756c
      src/thermal/ThermalTransient.cpp                  4849e68d3628c5a021c356142b03db26bfa50c54
      include/bettercad/thermal/ThermalTransient.hpp    9b69f1f2089b383f0aad474c718f17fd4f2f8e36
      src/thermal/ThermalPost.cpp                       29f81073ba27a67a348a7e98e2c54cfc2eec50b6
      include/bettercad/thermal/ThermalPost.hpp         22a1933bf4d9e2c30f28e9647849eaa42c775298
      src/thermal/ThermalElement.cpp                    c6b85fffab477b2828ef9c9e0300027bc83bc3fa
      include/bettercad/thermal/ThermalElement.hpp      a37d1e5561d013da8e3fed7027916fd9c05eb8fd
      include/bettercad/thermal/ThermalData.hpp         c13722336f4d6cc9e2b20d16415c5154464f81c3
      src/thermal/ThermalData.cpp                       e23fb2014da40c9416f96c4eed5dbcd39caad420
      src/thermal/ThermalDof.cpp                        56be927c6923e7184720c7343d7744f64ba2d0ea
      include/bettercad/thermal/ThermalDof.hpp          fc68a94a213e17015ea52570a9943a439c2823ae
      src/thermal/ThermalLoadVector.cpp                 b1b1d43d79bdba99226c52ef9b1a6b11179d1d2c
      include/bettercad/thermal/ThermalLoadVector.hpp   c2803e98731f2d52c957078f4862081c7283e40d
      src/thermal/ThermalBoundary.cpp                   cde982c0e5106fe4b51fe9a564ef42db1e21276d
      include/bettercad/thermal/ThermalBoundary.hpp     7e099d43380ea323f4b6664e86cec424c95e5514
      src/thermal/ThermalConvection.cpp                 06e46a5b9d1ea2881e99a6c3e2ba1e14eadb57b4
      include/bettercad/thermal/ThermalConvection.hpp   8135b9e2bbb76fa94c4440777793b36db0cbea81
      src/thermal/ThermalSystem.cpp                     698741e3b1e0a8b1de389693bac0a751b32b6edd
      include/bettercad/thermal/ThermalSystem.hpp       1ddb90cbb3737e2194cf53a4afe5d3291c772382
      src/linalg/SymmetricSolve.cpp                     3b0895bd8c204b71a12df574241819126a0d0008
      include/bettercad/linalg/SymmetricSolve.hpp       55f84e2f2cf20886080c15990dae1ba8d674c236
      src/structural/StructuralSolve.cpp                deff2301440a25d6a52f67b160ab49bbf9f54f77

git diff --check            exit 0
whitespace audit            the 30 changed or new files under the eight paths (5 modified, 25 new): no BOM, no CR,
                            no trailing whitespace, final newline
```

## What moved, against P18-POST-001's qualified tree

```text
include   6139370a -> ee2dd26d   + thermal/ThermalBalance.hpp
src       9826f86b -> 4dd2f59f   + thermal/ThermalBalance.cpp; thermal/CMakeLists.txt (one source)
tests     3465f39d -> 314e71c9   + thermal/ThermalBalance{Tests,MappedTests,TestSupport}, compile_fail/ThermalBalanceMisuse.cpp (69 + 33 cases),
                                   architecture/CheckBalanceOwnership.cmake + 4 fixtures (5 tests); CMakeLists.txt, compile_fail/CMakeLists.txt
                                   (one group), architecture/CMakeLists.txt, cli/ZeroMatchGuard.cmake (17 filters)
apps, examples, cmake, CMakeLists.txt, CMakePresets.json   unchanged
```

**No qualified module changed.** Every solver, the generic kernel, the structural wrapper and the P18-POST recovery are the same bytes as in P18-POST-001's qualified tree, so P17 and
P18-SOLVE / TRANSIENT / POST are re-RUN by the unfiltered suites below, not requalified: 415 structural cases, 271 414 assertions, identical in all three presets.

## The run — ONE, and it passed

Launched through `launch.cmd` → `run-qualification.cmd` → `qualify.cmd` (d313a640, the forty-third consecutive milestone on that byte-identical harness;
`verify-harness.cmd` 11855b8f PASS beforehand; every line of `run-qualification.cmd` begins rem/set/call/echo/setlocal/endlocal/@echo, checked beforehand: 0 other lines, and
0 lines whose `rem` runs into the next word), detached from the session so a tool timeout cannot touch it, with `PATH` set in the launching call. No file under the eight paths
was touched while it was active.

```text
stage                           exit   finished    elapsed / measure
debug-ext configure             0      05:16:25
debug-ext clean                 0      05:16:27
debug-ext build                 0      05:45:40    29m13s   0 warnings, 705 objects
debug-ext no-op rebuild         0      05:45:42    1 edge
debug-ext ctest                 0      06:50:23    4927/4927   3880.56 s
release-ext configure           0      06:50:27
release-ext clean               0      06:50:29
release-ext build               0      07:26:07    35m39s   0 warnings, 705 objects
release-ext no-op rebuild       0      07:26:08    1 edge
release-ext ctest               0      08:24:13    4927/4927   3484.85 s
debug-shared-ext configure      0      08:24:17
debug-shared-ext clean          0      08:24:18
debug-shared-ext build          0      08:52:52    28m34s   0 warnings, 705 objects
debug-shared-ext no-op rebuild  0      08:52:53    1 edge
debug-shared-ext ctest          0      09:55:37    4927/4927   3763.37 s
repeat release-ext (1813 x 5)   0      10:16:40    1813/1813   1262.07 s
repeat debug-ext   (1813 x 5)   0      10:40:17    1813/1813   1416.33 s

17 stages, 17 exit 0, 0 failed            2026-10-22 05:16:10 -> 10:40:25   5h24m15s
"Qualification passed: every stage exited 0."   qualify.cmd exit 0
```

The repeat set selected 1813 cases (the 1739 of P18-POST-001, the 69 ThermalBalance cases and the 5 ownership tests, which the `architecture` alternative picks up). 4927 is the
4820 of the predecessor plus 69, 33 and 5. The 705 objects are the 701 of the predecessor plus four (the new source, the two new test files and the compile-fail control).
Harness stderr: 266 lines, all of them the harmless `CRLF will be replaced by LF` notices git prints for a Windows working copy; **no other line** (0 non-CRLF-notice lines, `postrun-summary.txt`).

## The fingerprint, three times

```text
                     at the freeze   before the first build   after the last repeat
apps                 a95876bc        a95876bc                 a95876bc
include              ee2dd26d        ee2dd26d                 ee2dd26d
src                  4dd2f59f        4dd2f59f                 4dd2f59f
tests                314e71c9        314e71c9                 314e71c9
examples             a955cf65        a955cf65                 a955cf65
cmake                5a382115        5a382115                 5a382115
CMakeLists.txt       3ec3c10f        3ec3c10f                 3ec3c10f
CMakePresets.json    3229f0f8        3229f0f8                 3229f0f8
```

The full forty-character hashes are in `qualification/qualification-times.txt` (before and after) and in the candidate section above (at the freeze). Nothing under the eight
paths was touched between 2026-10-22 05:15:16 and 10:40:25.

## After the run

`qualification/postrun-summary.txt` (script `postrun-balance.ps1`) measured, on each preset's own binaries: the layering checker (531 files, 0 violations), warnings and errors in each
build log (0 and 0), the no-op rebuilds (1 edge each), the clean counts, the per-filter ctest counts (identical in the three presets), the per-preset results of the ThermalBalance,
ThermalPost, ThermalTransient, ThermalSolve, Linalg, ThermalAssembly, ThermalBC, ThermalLoad, ThermalElement, Dof, Material, Data and **structural** suites (all pass, same case and assertion counts in the three), the bit
fingerprints (`6b81a56797403e0d`, `ee1e1c859a3999d5`, `eb44675aa9d555a1` and `5a8ba61c30276a46`, identical in all three presets), and a sorted comparison of the 79 message lines of the balance suite between presets
(0 differences, timings normalised). The tolerance measurements (`qualification/tolerance-measurements.txt`, 36 lines: twelve cases in three presets) were collected afterwards from each preset's own test binary with the XML reporter,
because the console reporter wraps a long line in the middle of a token; the post-run summary's own table, parsed from the wrapped console output, holds the eight cases that did not wrap.

Binary hashes, for the record:

```text
debug-ext         bettercad_tests.exe  1 307 985 033 B  16aaa227b6b6c60e5152e12a46009cc7aee87bb3587b7f1fa5f13b44a1401824
release-ext       bettercad_tests.exe     55 161 506 B  1859d3adb327b56ff85b080487c99abee34225dc16947ef7e61582532e773280
debug-shared-ext  bettercad_tests.exe    942 912 273 B  e29aae9eb568dea3241ed14cb3c84d68e89b472cf510d8a8c6dcff3b96e66d43
debug-shared-ext  libbettercad_thermal.dll   35 134 334 B  00806a2a00076592a3afa2e852889b9552f78533ea701cc2ef2f9358c7e978f5
```

## The commit

The commit script recomputes the eight hashes from a scratch index, refuses to commit unless each equals the frozen one, refuses any remaining fill-in token in this directory
or in the message, commits, and reads the eight subtree hashes back out of the commit's tree. Its output, with the commit hash, is appended to this file by the follow-up
documentation commit, because a commit cannot carry its own hash.
