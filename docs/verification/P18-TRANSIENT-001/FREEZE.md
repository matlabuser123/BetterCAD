# P18-TRANSIENT-001 — the qualification freeze and the run

```text
SUBJECT:  the tree that was qualified, recorded before the first build, and what the run measured
METHOD:   the eight-path fingerprint every BetterCAD qualification since P11 has used -- a scratch
          git index seeded with `read-tree HEAD` and then `add -A` over apps include src tests
          examples cmake CMakeLists.txt CMakePresets.json. docs/ and deps/ are outside it by
          construction.
```

## The frozen candidate (the qualifying one)

Recorded 2026-10-21 05:39:47, after the pre-freeze run (debug-ext, full unfiltered ctest, 4745 of 4745, 0 failed, after a
1m49s build), the four mutation passes and the test additions they forced, and an early build and run of the shared-library
preset (the 62 transient cases of that day passed in it); before the first stage of the qualifying run. The only edits after the
pre-freeze run were one comment in `ThermalData.hpp` (the end-time sentence, now a statement) and the mutation probe TA21,
which restored the production file to the byte (SHA-256 recorded in `qualification/mutations/`). The run recomputes the same
eight hashes before its first configure and after its last repeat, and the commit script refuses to commit unless the eight it
recomputes equal these.

```text
QUALIFIED_CANDIDATE_HEAD   147d35572045791f16e45ff65db2d64e4c4aefb4
QUALIFIED_CANDIDATE_TREE   ae393d962005accc57bd35749a9ae73911c9fcb4   (scratch tree)

apps                 a95876bc6dbf843d4e0cec114059bc196fa16175
include              4757ad6f819f5a62668e8d1bf7c9ef3b9af71e1a
src                  2d17a75fdcb2da0b9528fe2008bd12540849fb38
tests                97eebe01c11f8787c982ce61f17dc9b994bc0077
examples             a955cf6511ce715fcb95bc06ef82b877535869e6
cmake                5a382115d7ed7775189d40b55e40f318eaae6cc7
CMakeLists.txt       3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd
CMakePresets.json    3229f0f89b236744d7d5c082bed47c28b718288e

SOURCE_TEST_FINGERPRINT                      the `tests` hash above, 97eebe01...
P18_TRANSIENT_SOLVER_CONTRACT_FINGERPRINT   ec053b623d0b6c17548c9bf874011c4a9185ece0
    git hash-object of the two lines
      ThermalTransient.hpp   9b69f1f2089b383f0aad474c718f17fd4f2f8e36
      ThermalSolve.hpp       7e2e0c43342043e0f675b83d37be722cfae0756c
    (the two public headers a later milestone may rely on: every type, signature and contract comment of the
     transient solve and of the steady solve it is built on; the second is the SAME blob P18-SOLVE-001 froze)
TIME_GRID_POLICY_FINGERPRINT                 e92276197f2e1a2a499b96f95a106187c8137f3b
    git hash-object of the three constants and the function that applies them
      inline constexpr double kTimeGridTolerance = 1.0e-9;
      inline constexpr std::size_t kMaxTimeSteps = 1'000'000;
      inline constexpr std::size_t kMaxStoredValues = 50'000'000;
      TimeGrid::create (the whole function)
ADR_050_DEPENDENCY_FINGERPRINT               ca069ddeccc15b732a5155482faec45515dd62e3
    git hash-object of the five lines
      src/thermal/CMakeLists.txt       d2a1dc659f9619634d6fe7da228253db8063eb47   (+ ThermalTransient.cpp)
      src/linalg/CMakeLists.txt        6ddd2cf742ed6e49b7500283d4d392446f37971b   (unchanged)
      src/structural/CMakeLists.txt    1f7bf0b4bec0aadba989e244cedd02ef5c1f13b8   (unchanged)
      CheckLayering.cmake              505897ecff3c3cad62370e785ee379e78a5884c4   (unchanged)
      CheckTargetDependencies.cmake    987e0536567bfea1565318aa616218bd9a374805   (unchanged)
    (the link lists and the two checkers: the dependency graph ADR-050 and ADR-052 constrain; only the thermal
     source list moved, so the fingerprint differs from P18-SOLVE-001's by exactly that line)
implementation blobs   ThermalTransient.cpp         4849e68d3628c5a021c356142b03db26bfa50c54
test blobs             ThermalTransientTests.cpp          97809b28668f70ea2dcd3c16eee78004a7baf570
                       ThermalTransientMappedTests.cpp    d33b4b701894f2dbabc69b00b4d796bae035673a
                       ThermalTransientTestSupport.hpp    766cf2bcc26a764c318eb53eefd92658411560d6
                       ThermalTransientMisuse.cpp         42a65123f6c33289acf346655614eaab92e91cac

UNTOUCHED (byte-identical to HEAD, compared by blob):
      src/thermal/ThermalSolve.cpp                  304276ca9c1b60480e5907e2fae37bc90799a633
      include/bettercad/thermal/ThermalSolve.hpp    7e2e0c43342043e0f675b83d37be722cfae0756c
      src/linalg/SymmetricSolve.cpp                 3b0895bd8c204b71a12df574241819126a0d0008
      include/bettercad/linalg/SymmetricSolve.hpp   55f84e2f2cf20886080c15990dae1ba8d674c236
      src/structural/StructuralSolve.cpp            deff2301440a25d6a52f67b160ab49bbf9f54f77

git diff --check            exit 0
whitespace audit            the eleven changed or new files under the eight paths: no BOM, no CR, no trailing
                            whitespace, final newline
```

## What moved, against P18-SOLVE-001's qualified tree

```text
include   691d479c -> 4757ad6f   + thermal/ThermalTransient.hpp; ThermalData.hpp (one comment)
src       9ae037da -> 2d17a75f   + thermal/ThermalTransient.cpp; thermal/CMakeLists.txt (one source)
tests     2c2d5ced -> 97eebe01   + thermal/ThermalTransient{Tests,MappedTests,TestSupport}, compile_fail/ThermalTransientMisuse.cpp
                                   (72 + 35 cases); CMakeLists.txt, compile_fail/CMakeLists.txt (one group), cli/ZeroMatchGuard.cmake (16 filters)
apps, examples, cmake, CMakeLists.txt, CMakePresets.json   unchanged
```

**No qualified module changed.** The steady solver, the generic kernel and the structural wrapper are the same bytes as in
P18-SOLVE-001's qualified tree, so P17 and P18-SOLVE are re-RUN by the unfiltered suites below, not requalified: 415
structural cases, 271 414 assertions, identical in all three presets.

## The run — ONE, and it passed

Launched through `launch.cmd` → `run-qualification.cmd` → `qualify.cmd` (d313a640, the forty-first consecutive milestone on
that byte-identical harness; `verify-harness.cmd` 11855b8f PASS beforehand; every line of `run-qualification.cmd` begins
rem/set/call/echo/setlocal/endlocal/@echo, checked beforehand: 0 other lines, and 0 lines whose `rem` runs into the next
word), detached from the session so a tool timeout cannot touch it, with `PATH` set in the launching call. No file under the
eight paths was touched while it was active.

```text
stage                           exit   started    finished    elapsed
debug-ext configure             0      05:40:35   05:40:45
debug-ext clean                 0      05:40:47   05:40:47
debug-ext build                 0      05:40:47   06:12:13    31m26s   0 warnings, 697 objects
debug-ext no-op rebuild         0      06:12:13   06:12:14    1 edge
debug-ext ctest                 0      06:12:14   07:09:44    4745/4745   3450.08 s
release-ext configure           0      07:09:44   07:09:49
release-ext clean               0      07:09:50   07:09:50
release-ext build               0      07:09:50   07:45:56    36m06s   0 warnings, 697 objects
release-ext no-op rebuild       0      07:45:56   07:45:57    1 edge
release-ext ctest               0      07:45:57   08:37:18    4745/4745   3080.80 s
debug-shared-ext configure      0      08:37:18   08:37:22
debug-shared-ext clean          0      08:37:24   08:37:24
debug-shared-ext build          0      08:37:24   09:06:04    28m41s   0 warnings, 697 objects
debug-shared-ext no-op rebuild  0      09:06:04   09:06:06    1 edge
debug-shared-ext ctest          0      09:06:06   10:00:44    4745/4745   3277.53 s
repeat release-ext (1695 x 5)   0      10:00:44   10:21:07    1695/1695   1222.18 s
repeat debug-ext   (1695 x 5)   0      10:21:08   10:43:36    1695/1695   1348.57 s

17 stages, 17 exit 0, 0 failed            2026-10-21 05:40:32 -> 10:43:43   5h03m11s
"Qualification passed: every stage exited 0."   qualify.cmd exit 0
```

The repeat set selected 1695 cases (the 1623 of P18-SOLVE-001 and the 72 ThermalTransient cases). 4745 is the 4638 of the
predecessor plus 72 and 35. The 697 objects are the 693 of the predecessor plus four (the new source, the two new test
files and the compile-fail control). Harness stderr: 266 lines, all of them the harmless `CRLF will be replaced by LF`
notices git prints for a Windows working copy; **no other line**.

## The fingerprint, three times

```text
                     at the freeze   before the first build   after the last repeat
apps                 a95876bc        a95876bc                 a95876bc
include              4757ad6f        4757ad6f                 4757ad6f
src                  2d17a75f        2d17a75f                 2d17a75f
tests                97eebe01        97eebe01                 97eebe01
examples             a955cf65        a955cf65                 a955cf65
cmake                5a382115        5a382115                 5a382115
CMakeLists.txt       3ec3c10f        3ec3c10f                 3ec3c10f
CMakePresets.json    3229f0f8        3229f0f8                 3229f0f8
```

The full forty-character hashes are in `qualification/qualification-times.txt` (before and after) and in the candidate
section above (at the freeze). Nothing under the eight paths was touched between 2026-10-21 05:39:47 and 10:43:43.

## After the run

`qualification/postrun-summary.txt` (script `postrun-transient.ps1`) measured, on each preset's own binaries: the layering
checker (527 files, 0 violations), warnings and errors in each build log (0 and 0), the no-op rebuilds (1 edge each), the clean
counts, the per-filter ctest counts (identical in the three presets), the per-preset results of the ThermalTransient,
ThermalSolve, Linalg, ThermalAssembly, ThermalBC, Dof, Material, Data and **structural** suites, the bit fingerprints, and a
sorted comparison of the 51 message lines of the transient suite between presets (0 differences, timings normalised).

Binary hashes, for the record:

```text
debug-ext         bettercad_tests.exe  1 284 954 123 B  3d38271d7cd7b5b95bf5291aafb56bba6357b90afc83313b6b5dc39b88cf17d3
release-ext       bettercad_tests.exe     53 882 095 B  386a7e7e163144d2cc6ab398f1c2c5efea449c9473a32eab8818e7b963105e8e
debug-shared-ext  bettercad_tests.exe    925 754 254 B  7d9712735b46471b487f9ec589f7708f0c12dda7c50da34e8e8c31cb5c73d150
debug-shared-ext  libbettercad_thermal.dll   28 964 627 B  fb9c2559148161e670b439aebca986fa16f34b702c11804d89d99a94a797d0a6   (12 libbettercad_*.dll)
```

## The commit

The commit script recomputes the eight hashes from a scratch index, refuses to commit unless each equals the frozen one,
refuses any remaining fill-in token in this directory or in the message, commits, and reads the eight subtree hashes back out
of the commit's tree. Its output, with the commit hash, is appended to this file by the follow-up documentation commit,
because a commit cannot carry its own hash.
