# P18-BC-001 — the qualification freeze and the runs

```text
SUBJECT:  the tree that was qualified, recorded before the first build, and
          what the runs measured
METHOD:   the eight-path fingerprint every BetterCAD qualification since P11
          has used -- a scratch git index seeded with `read-tree HEAD` and then
          `add -A` over apps include src tests examples cmake CMakeLists.txt
          CMakePresets.json. docs/ and deps/ are outside it by construction.
```

## The frozen candidate (the qualifying one)

Recorded 2026-10-19 07:54:28, after the pre-freeze run (debug-ext, full unfiltered
ctest, 4448 of 4448, 0 warnings, 47m25s), the mutation batches, and the one
production edit the first qualification run forced (below); before the first stage
of the qualifying run. The run recomputes the same eight hashes before its first
configure and after its last repeat, and the commit script refuses to commit unless
the eight it recomputes equal these.

```text
QUALIFIED_CANDIDATE_HEAD   b4714a487c04a3ac8ef2d294bc40f26585ec2f8e
QUALIFIED_CANDIDATE_TREE   063dcbb0e73dcbb2d322ecc857da2adefdf90132   (scratch tree)

apps                 a95876bc6dbf843d4e0cec114059bc196fa16175
include              ecc31f1413556446cd361c53949f65daf3aca2ff
src                  8a842d893095081f59490dccf1cb3deb6d21d424
tests                ce9f67bf6966e7345507ebc9fe2b2b90a5e3eb12
examples             a955cf6511ce715fcb95bc06ef82b877535869e6
cmake                5a382115d7ed7775189d40b55e40f318eaae6cc7
CMakeLists.txt       3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd
CMakePresets.json    3229f0f89b236744d7d5c082bed47c28b718288e

SOURCE_TEST_FINGERPRINT                    the `tests` hash above, ce9f67bf...
P18_THERMAL_BC_CONTRACT_FINGERPRINT        3c7e9500d53f4dd7d9d7e804830243db73f8392d
    git hash-object of the two lines
      ThermalBoundary.hpp   7e099d43380ea323f4b6664e86cec424c95e5514
      ThermalConvection.hpp 8135b9e2bbb76fa94c4440777793b36db0cbea81
    (the two public headers: every type, signature and contract comment a later
    milestone may rely on)
implementation blobs   ThermalBoundary.cpp          cde982c0e5106fe4b51fe9a564ef42db1e21276d
                       ThermalConvection.cpp        06e46a5b9d1ea2881e99a6c3e2ba1e14eadb57b4
test blobs             ThermalBoundaryTests.cpp     04747b7b1db4516e18d2bec4a15b06038eaa57a9
                       ThermalBoundaryMappedTests.cpp 4a29fc505c8e2572dd763ecbb7e391742e78863b
                       ThermalBoundaryTestSupport.hpp 224d6374105e3bf08c8060b2b81cb28cf9c5867e
                       ThermalBoundaryMisuse.cpp    e8f4965e61b4030800eb210da5ea2da4d55005ef

git status at the freeze
   M src/thermal/CMakeLists.txt
   M tests/CMakeLists.txt
   M tests/cli/ZeroMatchGuard.cmake
   M tests/compile_fail/CMakeLists.txt
  ?? docs/verification/P18-BC-001/
  ?? include/bettercad/thermal/ThermalBoundary.hpp
  ?? include/bettercad/thermal/ThermalConvection.hpp
  ?? src/thermal/ThermalBoundary.cpp
  ?? src/thermal/ThermalConvection.cpp
  ?? tests/compile_fail/ThermalBoundaryMisuse.cpp
  ?? tests/thermal/ThermalBoundaryMappedTests.cpp
  ?? tests/thermal/ThermalBoundaryTestSupport.hpp
  ?? tests/thermal/ThermalBoundaryTests.cpp
  (.claude/ untracked, outside the fingerprint, not committed)
```

## What moved, against P18-LOAD-001's qualified tree

```text
include   1822fe06 -> ecc31f14   + thermal/ThermalBoundary.hpp, thermal/ThermalConvection.hpp
src       f1e63c1a -> 8a842d89   + thermal/ThermalBoundary.cpp, thermal/ThermalConvection.cpp;
                                   thermal/CMakeLists.txt (2 sources)
tests     4bfc1876 -> ce9f67bf   + thermal/ThermalBoundaryTests.cpp, ThermalBoundaryMappedTests.cpp,
                                   ThermalBoundaryTestSupport.hpp (92 cases);
                                   + compile_fail/ThermalBoundaryMisuse.cpp (31 cases);
                                   tests/CMakeLists.txt (2 sources);
                                   compile_fail/CMakeLists.txt (1 group);
                                   cli/ZeroMatchGuard.cmake (16 filters)
apps, examples, cmake, CMakeLists.txt, CMakePresets.json   unchanged
```

No file under `src/core`, `src/features`, `src/meshing`, `src/structural` or `apps/`
changed: P16's mapping and P18-DOF-001's constraint map are consumed through their
public functions, so every P16 and P17 test in the unfiltered runs below is the
predecessor regression and passed exactly as it did for P18-LOAD-001. No
predecessor milestone needed requalifying (brief section 179).

## The runs — TWO, and why the second is the qualifying one

Launched through `launch.cmd` → `run-qualification.cmd` → `qualify.cmd` (d313a640,
the thirty-eighth consecutive milestone on that byte-identical harness;
`verify-harness.cmd` 11855b8f PASS beforehand; every line of `run-qualification.cmd`
begins rem/set/call/echo/setlocal/endlocal/@echo, checked beforehand: 0 other
lines), detached from the session so a tool timeout cannot touch it. No source or
test file was touched while either run was active.

### Run 1 — 2026-10-19 03:57:35 to about 07:28, ONE STAGE FAILED, stopped by me

```text
tree at run 1: scratch tree 6b8188cf5cfb27eaa735564e432bfbe91fe3985e, include f894a4c7
               (src 8a842d89, tests ce9f67bf, and the other five paths: as in run 2)
debug-ext         build 0  no-op 0  ctest 0   4448/4448
release-ext       build 0  no-op 0  ctest 0   4448/4448
debug-shared-ext  build 1  <-- the failure   ctest skipped
```

The failure, and nothing else:

```text
FAILED: bin/bettercad_tests.exe
tests/thermal/ThermalBoundaryMappedTests.cpp:170: undefined reference to
  `bettercad::thermal::PreparedConvectionBoundary::area() const'          (and :212)
collect2.exe: error: ld returned 1 exit status
```

**A production defect, not a flake.** `PreparedConvectionBoundary` is a plain struct
whose three accumulating members are defined in `ThermalBoundary.cpp`; on Windows a
shared build exports a symbol only if it is annotated, and the struct carried no
`BETTERCAD_THERMAL_EXPORT`. A static build links it without an annotation, which is
why 92 test cases, 58 mutation probes, the prefreeze and two of three presets all
passed. It is finding F7 of the adversarial review. The harness had continued into
the release-ext repeat stage; that stage's result was moot, and I stopped the run
(process tree killed) rather than spend 45 more minutes on a tree I was about to
change. Its seventeen generated files are preserved unmodified under
`qualification/run1-failed/` (the release-ext repeat log is partial because the run
was stopped inside it; there is no debug-ext repeat log and no shared-preset test
log, because that build had failed). `freeze-bc-run1.txt` is its freeze record.

**The fix:** `BETTERCAD_THERMAL_EXPORT` on the struct, with a comment. Nothing else
in `src` or `tests` changed (their hashes above equal run 1's). **Checked before the
rerun:** the shared test binary links and the 92 cases pass in it; the full
shared-preset build exits 0; five mutation probes spanning the kernel, the
constraint resolution, the preparation and the provenance were rerun on the final
text and all five are killed (MUTATIONS.md, batch 4).

**What was NOT done.** Nothing was excluded, retried inside the harness, loosened or
re-labelled. Run 1 is **not** counted as a pass. The tree was changed (the
annotation), the freeze was recorded again, and the whole qualification was rerun
from a fresh configure.

### Run 2 — 2026-10-19 07:54:43 to 12:42:59, 0 STAGES FAILED (the qualifying run)

```text
stage                           exit   started    finished    elapsed
debug-ext configure             0      07:54:46   07:54:50
debug-ext clean                 0      07:54:52   07:54:52    704 files
debug-ext build                 0      07:54:52   08:27:04    32m12s   0 warnings, 682 objects
debug-ext no-op rebuild         0      08:27:06   08:27:06    1 edge
debug-ext ctest                 0      08:27:06   09:14:52    4448/4448   2866.35 s
release-ext configure           0      09:14:52   09:14:58
release-ext clean               0      09:14:59   09:14:59    704 files
release-ext build               0      09:14:59   09:54:58    39m59s   0 warnings, 682 objects
release-ext no-op rebuild       0      09:54:59   09:54:59    1 edge
release-ext ctest               0      09:54:59   10:38:59    4448/4448   2640.29 s
debug-shared-ext configure      0      10:38:59   10:39:04
debug-shared-ext clean          0      10:39:06   10:39:06    715 files
debug-shared-ext build          0      10:39:06   11:10:08    31m02s   0 warnings, 682 objects
debug-shared-ext no-op rebuild  0      11:10:10   11:10:10    1 edge
debug-shared-ext ctest          0      11:10:10   11:57:32    4448/4448   2841.62 s
repeat release-ext (1495 x 5)   0      11:57:33   12:18:35    1495/1495   1262.27 s
repeat debug-ext   (1495 x 5)   0      12:18:37   12:42:45    1495/1495   1447.82 s

17 stages, 17 exit 0, 0 failed            07:54:43 -> 12:42:59   4h48m16s
"Qualification passed: every stage exited 0."   qualify.cmd exit 0
```

The repeat set selected 1495 cases (the 1337 of P18-ELEM-001, the 66 ThermalLoad
cases and the 92 new ThermalBC cases). 4448 is the 4325 of the predecessor plus 92
plus 31. The 682 objects are the 677 of the predecessor plus five (the two new
sources, the two new test files and the compile-fail control). Harness stderr: 268
lines, all of them the harmless `CRLF will be replaced by LF` notices git prints for
a Windows working copy; no other line.

## The fingerprint, three times (run 2)

```text
                     at the freeze   before the first build   after the last repeat
apps                 a95876bc        a95876bc                 a95876bc
include              ecc31f14        ecc31f14                 ecc31f14
src                  8a842d89        8a842d89                 8a842d89
tests                ce9f67bf        ce9f67bf                 ce9f67bf
examples             a955cf65        a955cf65                 a955cf65
cmake                5a382115        5a382115                 5a382115
CMakeLists.txt       3ec3c10f        3ec3c10f                 3ec3c10f
CMakePresets.json    3229f0f8        3229f0f8                 3229f0f8
```

The full forty-character hashes are in `qualification/qualification-times.txt`
(before and after) and in the candidate section above (at the freeze). Nothing under
the eight paths was touched between 07:54:28 and 12:42:59 on 2026-10-19; the
evidence under `docs/` was written during the run and is outside the fingerprint by
construction.

## After the run

`qualification/postrun-summary.txt` (script `postrun-bc.ps1`) measured, on each
preset's own binaries: the layering checker (517 files, 0 violations), warnings and
errors in each build log (0 and 0), the no-op rebuilds (1 edge each), the clean
counts, the per-filter ctest counts, the per-preset results of the ThermalBC,
ThermalLoad, ThermalElement, Dof, Material, Data and structural-element suites, the
bit fingerprints, and a sorted comparison of the 44 message lines of the new suite
between presets (0 differences).

## The commit

The commit script recomputes the eight hashes from a scratch index, refuses to
commit unless each equals the frozen one, refuses any remaining fill-in token in
this directory or in the message, commits, and reads the eight subtree hashes back
out of the commit's tree. Its output, with the commit hash, is appended to this file
by the follow-up documentation commit, because a commit cannot carry its own hash.