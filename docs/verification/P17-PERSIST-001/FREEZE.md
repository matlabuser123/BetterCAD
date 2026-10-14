# P17-PERSIST-001 — the qualification

```text
RESULT:    PASS
CANDIDATE: bde6f8bfbf86f8fcb20e2f84141cbbe4a0a97ad7  (the parent commit)
RAN:       Wed 14/10/2026 03:30:06 -> 06:58:57  =  3h28m51s
STAGES:    17, every one exit 0, 0 stage(s) failed
HARNESS:   qualify.cmd exit 0, "Qualification passed: every stage exited 0."
```

## The numbers

| | debug-ext | release-ext | debug-shared-ext |
| --- | --- | --- | --- |
| tests | **3921 / 3921** | **3921 / 3921** | **3921 / 3921** |
| `warning:` | 0 | 0 | 0 |
| `error:` | 0 | 0 | 0 |
| configure | 5.9s | 9.2s | 7.7s |
| clean | 1.8s | 1.4s | 1.8s |
| build | 28m36s | 34m30s | 27m34s |
| no-op rebuild | exit 0 | exit 0 | exit 0 |
| ctest | 27m59s | 26m36s | 27m28s |

```text
repeat release-ext   1050 tests x 5   16m37s   exit 0
repeat debug-ext     1050 tests x 5   18m54s   exit 0
TOTAL                                 3h28m51s
```

Every count **measured, not predicted**, and each reconciles against the
P17-CMD-001 run plus what this milestone added:

```text
suite    3883 -> 3921   +38:  38 StructuralPersistence cases
repeat   1012 -> 1050   +38:  THE FILTER IS UNCHANGED -- `unit\.Structural`
                              already catches unit.StructuralPersistence_* as
                              a substring, so the new cases joined the repeat
                              set without touching it. compile_fail is
                              deliberately NOT in the repeat set: each case is
                              a real compilation, so repeating one measures
                              the compiler's determinism and not BetterCAD's.
```

## The tree that was qualified is the tree that was committed

```text
apps               b49722470580d4fe6c2bb2b3594bb3885eee10db
include            c9696d26cd8583a90cdddd00a51ee654fc3ba72b
src                f3c1245dabbfda68008db3660f3d15a73e8e607f
tests              bb3e3ce6fe1f586664b2194d13db9b4d4f558a57
examples           776d36875941b214081765680efa3da6c996793a
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e
```

**Before == after**, and `freeze-fingerprint.txt` beside this file carries the
same eight values taken independently, in Bash, before the launch. Both checks
extract the block **by name** with `awk` rather than by line offset, which is
the lesson the element-contour run's false `*** THE TREE MOVED ***` taught.

**Only three of the eight moved**, and the fourth fact is the interesting one:

```text
MOVED      src        the new serializer, two dispatch branches, one build file
           tests      the new test file and one build file
           examples   the new hand-written fixture

UNCHANGED  apps       no application code
           include    *** NO PUBLIC HEADER MOVED AT ALL ***
           cmake      no build infrastructure
           CMakeLists.txt, CMakePresets.json
```

`include` is byte-identical to P17-CMD-001's value. **There is no public API
change in this milestone** — the serializer's declarations live in
`src/io/json/ObjectJson.hpp`, a private header. That is the narrowest blast
radius of any P17 milestone so far, and it is why no predecessor is
requalified.

## No predecessor is requalified

The opposite of P17-CMD-001, where four were. **Not one structural production
file changed**, so nothing any earlier P17 milestone qualified has moved:

```text
P17-ARCH, DATA, MAT, DOF, ELEM, LOAD, BC, ASSEMBLY, SOLVE, POST,
REACTION, VALID, VIZ, CMD      all untouched
P16                            untouched
P15                            untouched -- P17 stores no material data, it
                               references P15's assignment
```

What this run does re-exercise is **io**: `ObjectJson.hpp` and
`DocumentJson.cpp` are shared by every object kind, so the whole
document-persistence suite ran unfiltered in all three presets — including
P16's 26 `MeshingPersistence` cases and every `*FileTests` suite.

## The pre-freeze gates

Cheap first, and **gated on the probes**: a surviving probe would have stopped
the chain before any of this ran.

```text
0  THE PROBE GATE. 14 settled verdicts, 0 survived. The gate also refuses a
   count other than 14, so the VOID P14 could not have been silently skipped
   -- it was rerun as P14b and killed before this chain started.
1  the mutated source byte-compared against its pristine copy
   (8f236003e7b03ab9e68727ffedf0e76e631c9261)
2  line endings: no mixed file in the changed set (8 files checked)
3  git diff --check, tracked and staged -- no whitespace errors
4  no BETTERCAD_*_EXPORT on a header-defined constexpr or inline. The
   serializer's four constexpr name tables are .cpp-local in an anonymous
   namespace, which is why there was nothing to find
5  the batch runner's rem/set/call/echo invariant: 0 offending lines, and both
   harness hashes printed
6  the FULL default target built -- the clean stage deletes the application, so
   a targeted rebuild would leave gui.* with no executable
7  counts recounted with ctest -N: 1050 / 38 / 26 / 3921
8  the 38 persistence cases under --repeat until-fail:5  ->  38/38
9  the debug-shared-ext build  ->  0 diagnostics, 0 undefined references, and
   421/421 on the persistence + structural filter in that preset
```

Gate 9 earned its place for the usual reason, and this time found nothing: the
new translation unit links `BetterCAD::structural` into `bettercad_io` for the
first time, and an export defect across the DLL boundary fails **only** in that
preset. It has recurred in three modules before now.

## The harness

Byte-identical to the one P16-SIZE-001 settled on, **twenty-eight milestones in
a row** counting this one. Both hash forms recorded, because they differ and a
hash without its algorithm cannot be checked:

```text
qualify.cmd          git hash-object d313a64070718c44fae290ac042fe259d1a03c8b
                     sha1sum         244b38de8feac0475af7cdc56ba9415d27d8dd29
verify-harness.cmd   git hash-object 11855b8ff891093b021d1e0351761283d0eedd24
                     sha1sum         afd0197d4f4726db7f533c2f27f4f6e069ba83c6
```

`run-qualification.cmd`'s every-line-begins-with-`rem`/`set`/`call`/`echo`
invariant was checked before the run; no stray line was found. It was converted
to CRLF in the working tree to match its predecessor, because `.gitattributes`
sets `*.cmd text eol=crlf` and a batch file that cmd.exe reads by byte offset
should not differ from the one that has worked twenty-seven times.

## One thing that happened after the run, recorded because it is true

The qualification finished at 06:58:57 and **disk C: then hit 0 bytes free**,
which blocked writing this file and the commit. Three live preset build trees
plus two stale ones from 2026-10-01 — `bc-build/cleanckout` and
`bc-build/nonetgen`, neither a current preset — came to 6.7 GB. The owner was
asked and chose to delete both stale trees, freeing 4.4 GB. No live build tree
and nothing under the repository was touched, and the qualification had already
completed and recorded its own logs before the disk filled, so no evidence was
lost or regenerated afterwards.
