# P19-LINSOLVE-001 — qualification freeze

Frozen 2026-11-09, after two read-only adversarial reviews, the repairs their
findings forced, **three** rounds of mutation probes and the two further repairs
rounds one and two forced. **The source and test tree recorded here is the tree
that was qualified.** If anything under the frozen surface changes after this
record, the qualification is void and is run again from clean.

## The frozen surface

`qualify.cmd`'s `:trees` records eight paths, and those eight are the frozen
surface:

```text
apps  include  src  tests  examples  cmake  CMakeLists.txt  CMakePresets.json
```

`TODO.md`, `ROADMAP.md` and everything under `docs/` are OUTSIDE it, which is
why the evidence in this directory and the `[x]` in `TODO.md` can be written
after the run without voiding it.

## The eight subtree identities at the freeze

Computed by `qualify.cmd`'s own method — a scratch `GIT_INDEX_FILE`,
`git read-tree HEAD`, `git add -A` over the eight paths, `git write-tree
--prefix=<dir>/` per directory and `git ls-files -s` for the two files — and
**never** by `git rev-parse HEAD:<path>`, which reads the COMMITTED tree and
would miss every uncommitted change. Eight of this milestone's files are
untracked at the freeze, so that distinction is the whole point.

```text
                   P19-LINSOLVE-001                             vs P19-PVCOUPLE-001
apps               fc398a3d2e524178cf38e8b667236379d8381086      same
include            d52820c56a1ae388f2a4f12b126b45c103308bca      CHANGED
src                56358d8c8c6c97f55ded83b74e5d36e0759281ed      CHANGED
tests              ba7cf940350cf23c61563eb51aef65f379f75b98      CHANGED
examples           4da234ebf4779f7c16d5df0075f106f7e3d4c958      same
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7      same
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd      same
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e      same

HEAD at the freeze  30c04cc6ccc97e176dce141d0d9d9042707f4b00
```

**EXACTLY THREE SUBTREES MOVED AND THEY ARE THE RIGHT THREE** — two new public
headers, two new sources plus one modified source and two one-line CMake edits,
and three new test files plus one appended case and two CMake edits. `apps`,
`examples`, `cmake` and both top-level files reproducing bit-for-bit is the check
that nothing else drifted during a long session.

The working-tree state of the frozen surface, **diffed for unexpected files**:

```text
 M src/cfd/CMakeLists.txt
 M src/linalg/CMakeLists.txt
 M src/linalg/SymmetricSolve.cpp
 M tests/CMakeLists.txt
 M tests/cfd/CfdCouplingTests.cpp
 M tests/compile_fail/CMakeLists.txt
?? include/bettercad/cfd/CfdLinearSolve.hpp
?? include/bettercad/linalg/SparseSolve.hpp
?? src/cfd/CfdLinearSolve.cpp
?? src/linalg/SparseSolve.cpp
?? tests/cfd/CfdLinearSolveTests.cpp
?? tests/compile_fail/CfdLinearSolveMisuse.cpp
?? tests/compile_fail/LinalgSparseMisuse.cpp
?? tests/linalg/SparseSolveTests.cpp
```

**ONE EXISTING SOURCE FILE IS MODIFIED, AND IT IS THE QUALIFIED KERNEL.** The
three milestones before this one could say that none was. `SymmetricSolve.cpp`
gains the row-offset guard because the audit found a reachable out-of-bounds read
in it; the scope question went to the owner and the answer on 2026-11-09 was to
keep the repair here (`ADR-076` Decision 10, and the README's `r18`). Its
behaviour on any well-formed input is unchanged, measured against the baselines:

```text
[structural]  271,414 assertions in 415 cases   identical to the baseline
[thermal]     595,622 assertions in 1168 cases  identical to the baseline
```

The diff is read for unexpected ENTRIES as well as unexpected content, and that
is not a formality on this lineage: on `P19-CONT-001` an adversarial reviewer
left a `CfdContinuity.cpp.adv-review-backup` inside the frozen surface, where
`git add -A src` would have committed it. **Both reviewers here were READ-ONLY**,
so neither could leave anything behind, and the probes were run afterwards by the
author through a driver that re-hashes all four files it can touch after every
single probe. A `find` over the whole frozen surface for `*.pristine`, `*.orig`,
`*.bak`, `*.rej` and `*backup*` returns nothing.

## The probe campaign, which changed the code twice

```text
round one   21 attempted   19 KILLED, 2 SURVIVED, 1 ERROR stopped the run
round two    8 probes       6 KILLED, 1 SURVIVED, control SURVIVED
round three  5 probes       4 KILLED,              control SURVIVED
```

**ALL THREE SURVIVORS WERE IN THE FIXES RATHER THAN IN THE ORIGINAL CODE**, which
is the argument for probing a repair instead of trusting it. One of them, `M4e`,
was not a missing test but a PROOF that one clause of the new row-offset guard
could not fire; it was removed from both kernels and monotonicity now carries the
whole weight. The guard that is frozen here is simpler than the one the audit's
finding first produced.

Twenty-nine behavioural probes are killed, the no-op control survived all three
rounds, and every revert was verified by hash. Details in
[experiments/mutations-summary.md](experiments/mutations-summary.md).

## The build caches, verified

`P19-CONT-001` added this check because `run-qualification.cmd` pins the
toolchain by `PATH` ORDER while `find_package(CONFIG)` derives candidate
PREFIXES from `PATH` MEMBERSHIP. `P19-PVCOUPLE-001` added `CMAKE_COMMAND` to it
and caught a whole session built with the wrong cmake.

```text
preset             OpenCASCADE_DIR                  CMAKE_COMMAND   msys64 refs
debug-ext          <deps>/lib/cmake/opencascade     WinLibs                   0
release-ext        <deps>/lib/cmake/opencascade     WinLibs                   0
debug-shared-ext   <deps>/lib/cmake/opencascade     WinLibs                   0

<deps> = C:/Users/Hasib/AppData/Local/bettercad-deps/gnu-16-mingw-amd64
```

## The harness

Five of the six files in `qualification/` are byte-identical copies of
`P19-PVCOUPLE-001`'s, **verified by hash rather than asserted**:

```text
qualify.cmd            244b38de8feac0475af7cdc56ba9415d27d8dd29   IDENTICAL
verify-harness.cmd     afd0197d4f4726db7f533c2f27f4f6e069ba83c6   IDENTICAL
launch.cmd             cf6c1f112d71586098a78652091d0375c8d04051   IDENTICAL
preflight-space.cmd    59910328ab43e07b9e0c474f969994ad483d3f68   IDENTICAL
preflight-space.ps1    b76d9325e8131c265443540a9ac914e236731a66   IDENTICAL
run-qualification.cmd  git hash-object f80acaa80552617393a8501cb86213abb7169aab
```

All **204** non-blank lines of `run-qualification.cmd` begin with `rem`, `set`,
`call`, `echo`, `setlocal`, `endlocal` or `@echo` followed by whitespace or the
end of the line, re-checked by a WHOLE-WORD match after every edit. That check
exists because `P19-ARCH-001`'s copy carried a line beginning `remrem` which a
prefix match let through.

**ITS INHERITED BODY WAS STALE AND IS CORRECTED RATHER THAN CARRIED ON.**
`P19-PVCOUPLE-001`'s copy still described `P19-CONT-001`'s continuity evaluator,
because that milestone changed only the heading line. Every line is `rem`, so
nothing executed differently, but a committed file described work it was not run
against. The body now describes this milestone — including that this is the first
of P19 to touch below `cfd`, which is why `unit\.Structural` and `unit\.Thermal`
in the repeat filter are REQUIRED here rather than merely kept.

`verify-harness.cmd` was re-run and **PASSES**:

```text
PASS: a failed stage gave qualify.cmd exit 2, and a zero-match
      repeat filter was counted as a failed stage.
```

The repeat filter is byte-identical to `P19-PVCOUPLE-001`'s, so **the only
difference between this qualification and that one is the tree under test.**

## The three figures, COUNTED and not predicted

Counted off the frozen tree with `ctest -N`, and written as sums so that a figure
needing a fudge term would mean a test had been dropped or registered twice:

```text
full suite, per preset     6870  = 6802 (P19-PVCOUPLE-001) + 19 linalg unit
                                   + 20 cfd unit + 1 appended coupling case
                                   + 28 compile-fail, EXACTLY
repeat set, per preset     3474  = 3434 (P19-PVCOUPLE-001) + 40 matching
                                   unit\.Linalg and unit\.Cfd, EXACTLY
repeat EXECUTIONS per log 17370  = 3474 x 5
```

**THE THIRD FIGURE COUNTS EXECUTIONS, NOT RESULT LINES**, and
`P19-PVCOUPLE-001`'s correction to that sentence is relied on here: `ctest
--repeat until-fail:5` prints one `Start` marker and one `Test #N: ... Passed`
line per EXECUTION, and prefixes the LAST execution of each test with a
`k/3474` progress counter. So the log holds 17,370 of each of the first two and
3,474 of the third, and the ratio that must be exactly 5.000 is markers to
PROGRESS-PREFIXED lines.

The two subsystem arithmetics, which a passing suite cannot make:

```text
                   before (30c04cc)        at the freeze           delta
cfd                728,802 in 623          751,771 in 644          22,969 and 21
linalg                 171 in  10            2,560 in  29           2,389 and 19
structural         271,414 in 415          271,414 in 415               0 and 0
thermal            595,622 in 1168         595,622 in 1168              0 and 0
```

**THE cfd LINE READ 751,767 AND 22,965 UNTIL THE BINARIES WERE ASKED, AND IT
COULD NOT HAVE BEEN CAUGHT FROM INSIDE THIS DOCUMENT.** 728,802 + 22,965 =
751,767, so the line was internally consistent and every arithmetic check it
invites passes. The measurement it was built from is the one recorded elsewhere
in this milestone as `[linsolve]` 22,969 in 21 cases, and 728,802 + 22,969 =
751,771, which is what all three qualified binaries read. A figure that agrees
with itself is not evidence; the four-assertion error survived precisely because
it was self-consistent, and only reading `[cfd]` off the built binary exposed
it. The other three lines are confirmed unchanged by the same reading.

The last two lines are the ones that matter most, because this milestone changes
a module both of those physics link. A deleted test and an added test net to
nothing in a pass/fail count; they do not net to nothing here.

## Pre-freeze state of the gates

**EVERY LINE BELOW WAS MEASURED AFTER THE LAST EDIT TO THE FROZEN SURFACE.**

```text
debug-ext            built, 0 error:, 0 warning: under -Werror
release-ext          built BEFORE the freeze, deliberately: P19-MOM-001's first
debug-shared-ext     attempt failed a RELEASE-ONLY -Werror=array-bounds at -O3
                     sixty-six minutes into its gate, so compiling all three
                     presets before a long run is cheap insurance. All three are
                     clean, and all three were rebuilt again after the probe
                     campaign's last repair
architecture         205 of 205 tests pass; architecture.layering reports
                     596 files, 0 violations, and its globs cover include/ and
                     src/, so the new files were scanned
compile-fail         28 of 28 pass; 28 registered = 28 guards in the two files,
                     diffed BOTH ways. Each gate is ATTRIBUTABLE, which the
                     inherited convention was not: the new cases are
                     `[[maybe_unused]] static`, so the only diagnostic is the
                     intended one rather than that plus -Wmissing-declarations
probes               29 of 29 behavioural probes killed across three rounds; the
                     no-op control survived in all three; every revert verified
                     by hash over all four watched files
reviews              two independent and READ-ONLY, both BEFORE this freeze;
                     32 findings; 3 production defects fixed, 17 test gaps
                     closed, 1 scope question escalated to the owner and decided
                     by them
caches               verified above, including CMAKE_COMMAND
stray files          none in the frozen surface
```

## What is NOT frozen, and why it is recorded anyway

```text
the verdict          run-qualification.out's own sentence plus the failed-stage
                     count in qualification-times.txt. NOT the launcher's exit
                     code: P19-MOM-001's attempt 1 was recorded as "completed
                     (exit code 0)" while the harness wrote "QUALIFICATION
                     FAILED: 2 stage(s) failed"
the freshness half   checked AFTER the run, by hand, and SCOPED to the objects
                     and linked outputs of THIS project with a GLOBAL sort. The
                     naive question names a runtime DLL copied out of the
                     toolchain, because CMake's copy carries the origin's mtime
                     (P19-CONT-001's r17), and the excluded binaries are
                     excluded by PROVENANCE -- each hashed against its origin --
                     rather than by convenience
the cross-preset     READ FROM THE THREE QUALIFIED BINARIES, not from the logs.
fingerprints         An earlier form of the README claimed the identity was
                     established by diffing the presets' logs; `ctest --preset`
                     captures no stdout on a PASSING run, and
                     `grep -c "bit fingerprint"` over the committed logs of the
                     two previous milestones returns 0 and 0. The claim had no
                     log behind it and is replaced by a post-run measurement
the per-preset       the assertion counts are identical in all three presets
assertion identity   only after the gate has built all three from clean; checked
                     from the three qualified binaries afterwards
the caches           verified before the launch and again after the run
cli.new.unicode-path passes from cmd.exe at code page 65001 and FAILS from Git
                     Bash, because it matches the CLI's confirmation line against
                     a UTF-8 regex and its verdict depends on the launching
                     shell's console code page. The harness launches from
                     cmd.exe, which is the qualified path. Recorded because the
                     author saw it fail and chased it down rather than
                     attributing it (the README's r21)
```

## Addendum: the post-gate reconciliation

Everything above was written before the gate ran. Everything here was written
after it passed, and **nothing here touched the frozen surface** — whose eight
identities the harness re-read after the last test and found unmoved, and which
were recomputed from the working tree a third time during this reconciliation
and matched again.

### `run-qualification.cmd` was corrected after the run, and its hash changed

Two defects in its comment body, both found by reading the file whole rather
than by reading the diff that produced them:

1. the claim `src/linalg/SymmetricSolve.cpp AND ITS HEADER ARE NOT MODIFIED`
   survived **four lines above its own correction**. The correction had been
   appended as a new paragraph instead of amending the paragraph it corrected,
   so the file asserted both things at once and the false one came first.
2. `the six new ones` undercounted: there are **eight** new files. The sentence
   named the `tests/compile_fail/CMakeLists.txt` block that registers the two
   compile-fail groups while counting neither file, so its enumeration did not
   close over the tree it claimed to enumerate.

Both are corrected, and the correction is confined to `rem` lines — proven, not
asserted, by reconstructing the pre-edit body from the inverse replacements and
hashing it:

```text
reconstructed pre-edit body   f80acaa80552617393a8501cb86213abb7169aab
                              == the hash recorded above, so the reconstruction
                                 IS the file the gate executed
changed lines that are not rem   0
executable statements            16 before, 16 after, IDENTICAL
non-blank lines                  204 -> 220, all 220 passing the whole-word check
run-qualification.cmd  git hash-object ff3f3b5e49aefd8216a61714844c27a42b581a03
                       sha1sum         1391b15a7eee01b297f4d14ec904e3627d22a290
```

The single hit from the `remrem` trap regex is `setlocal` at line 166 — `set`
followed by a letter, which is the regex being crude, not a trap.

**The two hashing conventions in this file must not be mixed.** The five
`IDENTICAL` harness hashes are raw `sha1sum` of the bytes;
`run-qualification.cmd`'s is `git hash-object`, which prepends a blob header and
applies the CRLF clean filter. They never agree for the same file, and both are
labelled wherever they appear. The reconciliation checked this rather than
assuming it: `sha1sum` of `verify-harness.cmd` is `afd0197d...`, exactly what
this file records, while `git hash-object` of the same bytes is `11855b8f...`.

### A defect in the inherited `verify-harness.cmd`, recorded and NOT fixed here

```text
line 41  if not defined BETTERCAD_BUILD_ROOT set "BETTERCAD_BUILD_ROOT=%LOCALAPPDATA%<0x08>c-build"
```

That is a literal **backspace byte**, 0x08, where `\bc-build` was meant — the
backslash-escape mangling family this lineage has now recorded five times. Its
reach is bounded and was checked rather than assumed:

```text
fires only when   BETTERCAD_BUILD_ROOT is undefined, and
                  run-qualification.cmd always defines it, so no gate in any
                  milestone ever evaluated this line
present in        72 of the 96 committed copies, all hashing identically
entered           2026-09-30, with the P15 materials work -- it pre-dates P19
                  entirely and is not this milestone's defect
what it would do  set the build root to a path containing a control character,
                  which would make the zero-match-guard half of the check
                  (line 38: "the repeat preset must be a REAL, configured one")
                  exercise something other than what it claims
```

**It is deliberately not fixed here.** The file is not executed by the gate, it
is not in the frozen surface, and repairing an inherited harness defect across
96 committed copies is work this milestone's `TODO.md` checklist does not
authorize. It is reported to the owner as a scope decision, exactly as the
`SymmetricSolve.cpp` question was.

### The freshness check meant nothing until it was re-scoped

The first attempt asked *"does this file have a build-artifact extension and sit
inside the build tree?"*, which is not the question. It reported **8 artifacts
per preset newer than the build exit** and a FAIL. All 24 were written by the
deployment *tests* into their own `ctest` scratch directories, three to five
minutes into each run:

```text
tests/infra/deploy/<test-name>/app/bettercad.exe   different hash from bin/bettercad.exe
tests/infra/deploy/<test-name>/app/Qt6Core.dll     ZERO BYTES (the real one is 16,640,828)
```

Stubs a test authors to exercise the deployment checker, not build output. The
tell was that the same 8 files appeared under identically-named fixture
directories in three independently built trees, which a relink cannot produce.

The second attempt asks the build tool instead: `ninja -t targets all` is the
authoritative list of what ninja produces, and anything else in the tree is not
build output whoever wrote it.

```text
debug-ext          5899 declared, 1145 on disk, 0 newer than its build exit
release-ext        5899 declared, 1145 on disk, 0 newer than its build exit
debug-shared-ext   5912 declared, 1158 on disk, 0 newer than its build exit
newest artifact in each tree precedes its build exit by 2.40 s / 0.01 s / 0.02 s
```

**`ninja -n` is not a staleness check, and must not be mistaken for one.** Asked
now, each tree's dry run plans 8 edges (20 for the shared preset), beginning with
`Checking git revision` and going on to relink `libbettercad_core`. A dry run
cannot restat, so it assumes the always-dirty git-revision stamp propagates. The
gate's real no-op rebuild logs are one line each — `[1/8] Checking git revision`,
exit 0 — byte-identical in shape to `P19-PVCOUPLE-001`'s, `P19-CONT-001`'s and
`P19-MOM-001`'s committed copies, because the real run restats, finds the
generated header unchanged and prunes the other seven edges.

**No build was run during this reconciliation**, and that was a decision rather
than an omission: a relink would have moved the binaries' mtimes and destroyed
the very evidence being checked. That is `r20`'s failure, and the way to not
repeat it is to not rebuild.
