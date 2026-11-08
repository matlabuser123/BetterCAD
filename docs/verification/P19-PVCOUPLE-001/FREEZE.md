# P19-PVCOUPLE-001 — qualification freeze

Frozen 2026-11-09, after the two adversarial reviews, the ten production repairs
they forced, two rounds of mutation probes and the three repairs the FIRST round
forced. **The source and test tree recorded here is the tree that was
qualified.** If anything under the frozen surface changes after this record, the
qualification is void and is run again from clean.

## The frozen surface

`qualify.cmd`'s `:trees` records eight paths, and those eight are the frozen
surface:

```text
apps  include  src  tests  examples  cmake  CMakeLists.txt  CMakePresets.json
```

`TODO.md`, `ROADMAP.md` and everything under `docs/` are OUTSIDE it, which is
why the evidence in this directory and the `[x]` in `TODO.md` can be written
after the run without voiding it. Nothing else may move.

## The eight subtree identities at the freeze

Computed by `qualify.cmd`'s own method — a scratch `GIT_INDEX_FILE`,
`git read-tree HEAD`, `git add -A` over the eight paths, `git write-tree
--prefix=<dir>/` per directory and `git ls-files -s` for the two files — and
**never** by `git rev-parse HEAD:<path>`, which reads the COMMITTED tree and
would miss every uncommitted change. Four of the new files are untracked at the
freeze, so that distinction is the whole point.

```text
                   P19-PVCOUPLE-001                             vs P19-CONT-001
apps               fc398a3d2e524178cf38e8b667236379d8381086      same
include            bf2e12be62617a78fafd6b95b6b18801c28d3e0e      CHANGED
src                e640a27ca777a976a0d168cd007d73171b8c6496      CHANGED
tests              17558fc3b6d1a1f524ae0a5a431448aa65b3a840      CHANGED
examples           4da234ebf4779f7c16d5df0075f106f7e3d4c958      same
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7      same
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd      same
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e      same

HEAD at the freeze  6e45a80d9057cbc0e1042b404d02abdc0bd5504f
```

**EXACTLY THREE SUBTREES MOVED AND THEY ARE THE RIGHT THREE** — one new public
header, one new source plus a one-line CMake edit, one new unit suite and one
new compile-fail group plus two CMake edits. `apps`, `examples`, `cmake` and
both top-level files reproducing bit-for-bit is the check that nothing else
drifted during a long session.

The working-tree state of the frozen surface, **diffed for unexpected files**:

```text
 M src/cfd/CMakeLists.txt
 M tests/CMakeLists.txt
 M tests/compile_fail/CMakeLists.txt
?? include/bettercad/cfd/CfdCoupling.hpp
?? src/cfd/CfdCoupling.cpp
?? tests/cfd/CfdCouplingTests.cpp
?? tests/compile_fail/CfdCouplingMisuse.cpp
```

**NO EXISTING SOURCE FILE IS MODIFIED.** The diff is read for unexpected
ENTRIES as well as unexpected content, and that is not a formality on this
lineage: on `P19-CONT-001` an adversarial reviewer left a
`CfdContinuity.cpp.adv-review-backup` inside the frozen surface, where
`git add -A src` would have committed it. **The remedy this milestone applied is
structural rather than procedural: both reviewers were READ-ONLY**, so neither
could leave anything behind, and the probes were run afterwards by the author.
The probe harness's `.pristine` backups were written under
`docs/verification/P19-PVCOUPLE-001/experiments/`, outside the frozen surface,
and a `find` over `src/` and `include/` for `*.pristine`, `*.orig`, `*.bak` and
`*backup*` returns nothing. They are NOT committed: they are byte-identical
copies of two files this commit already carries, and what they are evidence
for is the pair of sha256 values recorded at the head and the foot of both
`experiments/mutations-round1.out` and `experiments/mutations.out`, which is
what says every probe reverted exactly.

## The build caches, verified — and a new check this milestone adds

`P19-CONT-001` added the cache check because `run-qualification.cmd` pins the
toolchain by `PATH` ORDER while `find_package(CONFIG)` derives candidate
PREFIXES from `PATH` MEMBERSHIP, so dependency resolution was never pinned.
**This milestone adds `CMAKE_COMMAND`, and it adds it because the check caught
something.**

In Git Bash a `PATH` entry must be a POSIX path. Every local build of this
session used

```text
export PATH="C:/Users/.../mingw64/bin:$PATH"
```

which puts NOTHING on the path, because bash does not read `C:/...` as a `PATH`
entry. So every build ran MSYS2's cmake 4.4.3 instead of the pinned WinLibs
4.4.1, and the `debug-ext` tree carried `CMAKE_COMMAND=C:/msys64/...` and five
`msys64` references. The compiler still came from the cache, so the object code
was representative; the generator state was not. The tree was DELETED and
reconfigured with the pinned cmake.

```text
preset             OpenCASCADE_DIR                  CMAKE_COMMAND   msys64 refs
debug-ext          <deps>/lib/cmake/opencascade     WinLibs                   0
release-ext        <deps>/lib/cmake/opencascade     WinLibs                   0
debug-shared-ext   <deps>/lib/cmake/opencascade     WinLibs                   0

<deps> = C:/Users/Hasib/AppData/Local/bettercad-deps/gnu-16-mingw-amd64
```

The harness itself is NOT changed to achieve this. It stays byte-identical to
the lineage, and a CHECK is added rather than a variable.

## The harness

Five of the six files in `qualification/` are byte-identical copies of
`P19-CONT-001`'s, **verified by hash rather than asserted**:

```text
qualify.cmd            244b38de8feac0475af7cdc56ba9415d27d8dd29   IDENTICAL
verify-harness.cmd     afd0197d4f4726db7f533c2f27f4f6e069ba83c6   IDENTICAL
launch.cmd             cf6c1f112d71586098a78652091d0375c8d04051   IDENTICAL
preflight-space.cmd    59910328ab43e07b9e0c474f969994ad483d3f68   IDENTICAL
preflight-space.ps1    b76d9325e8131c265443540a9ac914e236731a66   IDENTICAL
run-qualification.cmd  git hash-object fe6bc408df1c56a1e4506df31923fe533d4a6d0a
                       this milestone's own; one comment line differs from
                       P19-CONT-001's, naming this milestone
```

All **165** lines of `run-qualification.cmd` begin with `rem`, `set`, `call`,
`echo`, `setlocal`, `endlocal` or `@echo` followed by whitespace or the end of
the line, re-checked by a WHOLE-WORD match after the edit. That check exists
because `P19-ARCH-001`'s copy carried a line beginning `remrem` which a prefix
match let through.

`verify-harness.cmd` was re-run and **PASSES**:

```text
PASS: a failed stage gave qualify.cmd exit 2, and a zero-match
      repeat filter was counted as a failed stage.
```

Its repeat filter is byte-identical to `P19-CONT-001`'s, so **the only
difference between this qualification and that one is the tree under test.**

## The three figures, COUNTED and not predicted

Counted off the frozen tree with `ctest -N`, and written as sums so that a
figure needing a fudge term would mean a test had been dropped or registered
twice:

```text
full suite, per preset     6802  = 6687 (P19-CONT-001) + 79 unit + 36
                                   compile-fail, EXACTLY
repeat set, per preset     3434  = 3355 (P19-CONT-001) + 79 matching
                                   unit\.Cfd, EXACTLY
repeat EXECUTIONS per log 17170  = 3434 x 5
```

**THE THIRD FIGURE COUNTS EXECUTIONS, NOT RESULT LINES.** `ctest --repeat
until-fail:5` prints ONE result line per test and FIVE `Start` markers, so the
log holds 3434 result lines and 17,170 `Start` markers; the ratio of markers to
result lines must be exactly 5.000. `P19-CONV-001`'s freeze labelled these the
wrong way round and its reconciliation failed on the first run.

> **Correction, written after the run and left here rather than edited away.**
> The sentence above predicts the right FIGURES and names them imprecisely,
> and the imprecision cost the reconciliation an hour. A repeat log holds
> THREE counts that each deserve the name "result line": 17,170 `Start`
> markers, 17,170 `Test #N: name ... Passed` lines -- one per EXECUTION, not
> one per test -- and 3,434 of those `Passed` lines carrying a `k/3434`
> progress prefix, one per test, every index from 1 to 3434 present exactly
> once. The ratio that is 5.000 is markers to PROGRESS-PREFIXED lines; markers
> to `Passed` lines is 1.000. `README.md`'s REGRESSION section names all three,
> and `r24` records how close this came to reporting three earlier milestones'
> committed figures as false.

The cfd subsystem's own arithmetic, which is the check a passing suite cannot
make:

```text
before (6e45a80)   723,852 assertions in 544 cases
at the freeze      728,802 assertions in 623 cases
delta                4,950              and      79
the coupling suite   4,950 assertions in  79 cases    EXACTLY
```

A deleted test and an added test net to nothing in a pass/fail count. They do
not net to nothing here, so **no pre-existing cfd assertion moved**, and the
continuity suite still reads 7,435 in 49.

## Pre-freeze state of the gates

**EVERY LINE BELOW WAS MEASURED AFTER THE LAST EDIT TO THE FROZEN SURFACE.**

```text
debug-ext            built, 0 warnings, 0 errors under -Werror
release-ext          built BEFORE the freeze, deliberately: P19-MOM-001's first
debug-shared-ext     attempt failed a RELEASE-ONLY -Werror=array-bounds at -O3
                     sixty-six minutes into its gate, so compiling all three
                     presets before a 3.5-hour run is cheap insurance
coupling suite       79 cases / 4,950 assertions
compile-fail         36 of 36 pass; 36 registered = 36 in the file, diffed BOTH
                     ways, with the regex fixed to allow a trailing `)` -- the
                     first form missed the last entry of each `foreach`, which
                     is the defect P19-CONT-001 recorded as its r7
mutation probes      23 of 23 behavioural probes caught across two rounds, 2 by
                     the COMPILER; round one left 3 SURVIVORS, each a real
                     weakness, each fixed and re-probed; 1 documented
                     unreachable state survived; the NO-OP CONTROL SURVIVED in
                     both rounds, and both files' sha256 match pristine
reviews              two independent and READ-ONLY, both BEFORE this freeze;
                     18 + 15 findings; 10 production defects fixed, 1 finding
                     REFUTED by measurement, 1 SUSPECTED finding CONFIRMED
caches               verified above, including CMAKE_COMMAND
```

## What is NOT frozen, and why it is recorded anyway

```text
the verdict          run-qualification.out's own sentence plus the failed-stage
                     count in qualification-times.txt. NOT the launcher's exit
                     code: P19-MOM-001's attempt 1 was recorded as "completed
                     (exit code 0)" while the harness wrote "QUALIFICATION
                     FAILED: 2 stage(s) failed"
the freshness half   checked AFTER the run, by hand, and SCOPED: the naive
                     question "the oldest compiled output in the build tree"
                     names a vendored third-party header or a runtime DLL
                     copied out of the toolchain, because CMake's copy carries
                     the origin's mtime. P19-CONT-001 recorded that as its r17.
                     The check covers the objects and the linked outputs of THIS
                     project, with a GLOBAL sort
the per-preset       the assertion counts are identical in all three presets
assertion identity   only after the gate has built all three from clean; that
                     is checked from the three qualified binaries afterwards
the caches           verified before the launch, per the section above, and
                     again after the run
```
