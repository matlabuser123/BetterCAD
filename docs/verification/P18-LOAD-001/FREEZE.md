# P18-LOAD-001 — the qualification freeze and the run

```text
SUBJECT:  the tree that was qualified, recorded before the first build, and
          what the run measured
METHOD:   the eight-path fingerprint every BetterCAD qualification since P11
          has used -- a scratch git index seeded with `read-tree HEAD` and then
          `add -A` over apps include src tests examples cmake CMakeLists.txt
          CMakePresets.json. docs/ and deps/ are outside it by construction.
```

## The frozen candidate

Recorded 2026-10-18 17:51:03, after the pre-freeze run (debug-ext, full
unfiltered ctest, 4325 of 4325, 0 warnings, 40m59s) and before the first
qualification stage. The qualification run recomputes the same eight hashes
before its first configure and after its last repeat, and the commit script
refuses to commit unless the eight it recomputes equal these.

```text
QUALIFIED_CANDIDATE_HEAD   f10f5f07694bc3c5cec001a485eaaf9f1f5eee30
QUALIFIED_CANDIDATE_TREE   ebb88487588fbceef425ec7d146bb2273e370e44   (scratch tree)

apps                 a95876bc6dbf843d4e0cec114059bc196fa16175
include              1822fe06db1599e3e67c41d4e031a9ffb3702b51
src                  f1e63c1a5e1407bf687b16f8e2232abeebe3799e
tests                4bfc1876b3db3a179f86efca456cd52ab50086e9
examples             a955cf6511ce715fcb95bc06ef82b877535869e6
cmake                5a382115d7ed7775189d40b55e40f318eaae6cc7
CMakeLists.txt       3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd
CMakePresets.json    3229f0f89b236744d7d5c082bed47c28b718288e

SOURCE_TEST_FINGERPRINT                    the `tests` hash above, 4bfc1876...
P18_THERMAL_LOAD_CONTRACT_FINGERPRINT      96e122c382463febddb85ff64f800e7be1d61777
    git hash-object of the two lines
      ThermalLoadVector.hpp c2803e98731f2d52c957078f4862081c7283e40d
      ThermalTarget.hpp     15e44ae3c3ebe3fec0fbadcf38616aed5bac9563
    (the two public headers: every type, signature and contract comment a
    later milestone may rely on)
implementation blobs   ThermalLoadVector.cpp b1b1d43d79bdba99226c52ef9b1a6b11179d1d2c
                       ThermalTarget.cpp     a6fb2722790d908d40b25f24550ae137429da4b9
test blobs             ThermalLoadTests.cpp  27b11f4255708f3167ecf80bf6e50544c76f40a6
                       ThermalLoadMisuse.cpp 9187844887e5a08cc740e48dab45e9d284ac1173

git status at the freeze
   M src/thermal/CMakeLists.txt
   M tests/CMakeLists.txt
   M tests/cli/ZeroMatchGuard.cmake
   M tests/compile_fail/CMakeLists.txt
  ?? docs/verification/P18-LOAD-001/
  ?? include/bettercad/thermal/ThermalLoadVector.hpp
  ?? include/bettercad/thermal/ThermalTarget.hpp
  ?? src/thermal/ThermalLoadVector.cpp
  ?? src/thermal/ThermalTarget.cpp
  ?? tests/compile_fail/ThermalLoadMisuse.cpp
  ?? tests/thermal/ThermalLoadTests.cpp
  (.claude/ untracked, outside the fingerprint, not committed)
```

The mutation probes ran BEFORE this freeze. Two production lines changed after
the first batch, both as a result of it: the two share formulas were reordered
and the dead finiteness scan was deleted (see MUTATIONS.md). Every probe that
touches either was rerun on the final text. The tests only gained cases after the
first batch. The last edit of the tree before the freeze was the zero-match
minimums, set to the observed counts.

## What moved, against P18-ELEM-001's qualified tree

```text
include   655275dd -> 1822fe06   + thermal/ThermalLoadVector.hpp, thermal/ThermalTarget.hpp
src       762b0a9c -> f1e63c1a   + thermal/ThermalLoadVector.cpp, thermal/ThermalTarget.cpp;
                                   thermal/CMakeLists.txt (2 sources)
tests     6dd66736 -> 4bfc1876   + thermal/ThermalLoadTests.cpp (66 cases);
                                   + compile_fail/ThermalLoadMisuse.cpp (19 cases);
                                   tests/CMakeLists.txt (1 source);
                                   compile_fail/CMakeLists.txt (1 group);
                                   cli/ZeroMatchGuard.cmake (9 filters)
apps, examples, cmake, CMakeLists.txt, CMakePresets.json   unchanged
```

No file under `src/core`, `src/features`, `src/meshing`, `src/structural` or
`apps/` changed: P16's mapping is consumed through its public functions and not
extended, so every P16 and P17 test in the unfiltered runs below is the
predecessor regression and passed exactly as it did for P18-ELEM-001. No
predecessor milestone needed requalifying (brief section 174).

## The run — ONE, and every stage exited 0

Launched through `launch.cmd` → `run-qualification.cmd` → `qualify.cmd`
(d313a640, the thirty-seventh consecutive milestone on that byte-identical
harness; `verify-harness.cmd` 11855b8f PASS beforehand; every line of
`run-qualification.cmd` begins rem/set/call/echo/setlocal/endlocal/@echo,
checked beforehand: 0 other lines), detached from the session so a tool timeout
cannot touch it. No source or test file was touched while it ran.

```text
stage                           exit   started    finished    elapsed
debug-ext configure             0      17:51:38   17:51:43
debug-ext clean                 0      17:51:48   17:51:48    699 files
debug-ext build                 0      17:51:48   18:22:40    30m52s   0 warnings, 677 objects
debug-ext no-op rebuild         0      18:22:41   18:22:41    1 edge
debug-ext ctest                 0      18:22:41   19:03:48    4325/4325   2466.32 s
release-ext configure           0      19:03:48   19:03:53
release-ext clean               0      19:03:54   19:03:54    695 files
release-ext build               0      19:03:54   19:41:39    37m45s   0 warnings, 677 objects
release-ext no-op rebuild       0      19:41:40   19:41:40    1 edge
release-ext ctest               0      19:41:40   20:19:08    4325/4325   2247.76 s
debug-shared-ext configure      0      20:19:08   20:19:12
debug-shared-ext clean          0      20:19:14   20:19:14    706 files
debug-shared-ext build          0      20:19:14   20:49:10    29m56s   0 warnings, 677 objects
debug-shared-ext no-op rebuild  0      20:49:11   20:49:11    1 edge
debug-shared-ext ctest          0      20:49:11   21:29:50    4325/4325   2438.36 s
repeat release-ext (1403 x 5)   0      21:29:51   21:50:04    1403/1403   1212.86 s
repeat debug-ext   (1403 x 5)   0      21:50:18   22:12:51    1403/1403   1353.56 s

17 stages, 17 exit 0, 0 failed            17:51:35 -> 22:13:03   4h21m28s
"Qualification passed: every stage exited 0."   qualify.cmd exit 0
```

The repeat set selected 1403 cases (the 1337 of P18-ELEM-001 plus the 66 new
ThermalLoad cases). 4325 is the 4240 of the predecessor plus 66 plus 19. The 677
objects are the 673 of the predecessor plus four (the two new sources, the test file
and the compile-fail control). Harness stderr: 268 lines, all of them the
harmless `CRLF will be replaced by LF` notices git prints for a Windows working
copy; no other line.

## The fingerprint, three times

```text
                     at the freeze   before the first build   after the last repeat
apps                 a95876bc        a95876bc                 a95876bc
include              1822fe06        1822fe06                 1822fe06
src                  f1e63c1a        f1e63c1a                 f1e63c1a
tests                4bfc1876        4bfc1876                 4bfc1876
examples             a955cf65        a955cf65                 a955cf65
cmake                5a382115        5a382115                 5a382115
CMakeLists.txt       3ec3c10f        3ec3c10f                 3ec3c10f
CMakePresets.json    3229f0f8        3229f0f8                 3229f0f8
```

The full forty-character hashes are in `qualification/qualification-times.txt`
(before and after) and in the candidate section above (at the freeze). Nothing
under the eight paths was touched between 17:51:03 and 22:13:03 on 2026-10-18; the
evidence under `docs/` was written during the run and is outside the fingerprint
by construction.

## After the run

`qualification/postrun-summary.txt` (script `postrun-load.ps1`) measured, on each
preset's own binaries: the layering checker (513 files, 0 violations), warnings
and errors in each build log (0 and 0), the no-op rebuilds (1 edge each), the
clean counts, the per-filter ctest counts, the per-preset results of the
ThermalLoad, ThermalElement, Dof, Material, Data and structural-element suites,
the bit fingerprints, and a sorted comparison of the 42 message lines of the load
suite between presets (0 differences).

## The commit

The commit script recomputes the eight hashes from a scratch index, refuses
to commit unless each equals the frozen one, refuses any remaining fill-in
token in this directory or in the message, commits, and reads the eight
subtree hashes back out of the commit's tree. Its output, with the commit
hash, is appended to this file by the follow-up documentation commit, because
a commit cannot carry its own hash.