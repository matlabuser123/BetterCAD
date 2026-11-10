# P19-TRANSIENT-001 — the qualification freeze

```text
FROZEN:   2026-11-10
MILESTONE: P19-TRANSIENT-001 -- Transient Incompressible Solver
ADR:       ADR-078
```

CLAUDE.md: *"Freeze the source and test tree and record its identity before
qualifying. ... If source or tests change after the freeze, the qualification is
void and is run again from clean."* This file is that record, written before the
harness was launched.

## The frozen surface

Eight paths, the same eight every milestone freezes:

```text
apps  include  src  tests  examples  cmake  CMakeLists.txt  CMakePresets.json
```

`TODO.md`, `ROADMAP.md` and everything under `docs/` are OUTSIDE it. They cannot
reach the executable or the tests, so the project's policy lets them follow the
run — which is what makes it possible for this file to be written at all, and
for the README's REGRESSION and QUALIFICATION sections to be filled from the
harness's logs afterwards.

## The eight identities, as frozen

Read through a scratch `GIT_INDEX_FILE` so the working tree's own index is never
touched: `git read-tree HEAD`, `git add -A` over the eight paths, then
`git write-tree --prefix=<dir>/` for each directory and `git ls-files -s` for
the two files.

```text
apps               fc398a3d2e524178cf38e8b667236379d8381086
include            4759b76839047d95cf096ae80bf071c52c7e3edb
src                c33a7b6fdbe7cb1b4deb66727152525e85dd4a48
tests              47658ea579a4cf9e46d2f77410b5b1c12b71ccef
examples           4da234ebf4779f7c16d5df0075f106f7e3d4c958
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e
```

**`git rev-parse HEAD:<path>` IS NOT USED HERE, AND THAT IS DELIBERATE.** Before
the commit it reads the PREVIOUS commit's tree and would agree with itself for
the wrong reason. It is used only AFTER the commit, as the independent final
reading. `P19-STEADY-001`'s evidence records that distinction and this freeze
keeps it.

## What moved, and what did not

Five of the eight are **bit-identical to the committed tree at `10c871a`**,
which was read directly with `git rev-parse HEAD:<path>` before the freeze:

```text
apps               fc398a3d   unchanged
examples           4da234eb   unchanged
cmake              5a382115   unchanged
CMakeLists.txt     3ec3c10f   unchanged
CMakePresets.json  3229f0f8   unchanged

include            588dd50b -> 4759b768   CfdTransient.hpp, new
src                9fce91eb -> c33a7b68   CfdTransient.cpp, new; CMakeLists +1
tests              f3eb700c -> 47658ea5   CfdTransientTests.cpp, new
                                          CfdTransientMisuse.cpp, new
                                          two CMakeLists registrations
```

**THAT IS THE BLAST RADIUS, MEASURED RATHER THAN ARGUED.** The milestone's claim
is that no existing declaration changed, and three subtrees moving while five
stay bit-identical is what that claim looks like in hashes. `apps` unchanged
says no GUI or CLI surface was touched; `examples` unchanged says no reference
model moved; `cmake` and the two root files unchanged say no build policy
changed.

## The figures, counted off the frozen tree

By `ctest -N` and by the test binary, never predicted. `P19-GEOM-001`'s freeze
recorded getting that wrong once, and `P19-LINSOLVE-001`'s post-gate
reconciliation found an internally consistent figure that was four short.

```text
full suite, per preset   6965 = 6903 + 10 unit.CfdTimeGrid_
                                     + 24 unit.CfdTransient_
                                     + 28 compile_fail.cfdtransient
repeat set, per preset   3527 = 3493 + 34 matching the existing filter
repeat EXECUTIONS        17635 = 3527 x 5, per repeat log
architecture             205, IDENTICAL to baseline
```

The repeat filter is **unchanged**: both `unit.CfdTimeGrid_` and
`unit.CfdTransient_` already match its `unit\.Cfd` branch, so nothing had to be
added to it to cover the new tests. `compile_fail` stays excluded from the
repeat, as in every milestone — each case is a real compilation serialised on
the build-tree lock — and the new group's 28 cases are in the full run of all
three presets.

## The subsystem arithmetic, which a passing suite cannot make

A green run says every assertion held. It does not say the right number of
assertions ran. These are the identities that do.

```text
[cfd]           755043 assertions in 697 test cases
                = 753230 + 1813   and   663 + 34        EXACT
[cfd][transient]  1813 assertions in  34 test cases     the new suite
[cfd][steady]     1263 assertions in  19 test cases     IDENTICAL to baseline
[linalg]          2560 assertions in  29 test cases     IDENTICAL
[structural]    271414 assertions in 415 test cases     IDENTICAL
[thermal]       595622 assertions in 1168 test cases    IDENTICAL
```

`[cfd][steady]` being bit-identical is the check that `ADR-078` Decision 4 was
kept: `CfdSteady.hpp` and `CfdSteady.cpp` are not touched, so the steady suite
cannot have moved, and it did not. `[linalg]`, `[structural]` and `[thermal]`
identical is the check that a cfd-only change really was cfd-only.

**WHEN EACH OF THESE WAS MEASURED, because the table above would otherwise
imply they were all read at the same moment.** All six were read off the FROZEN
tree, after the last edit. `[structural]` and `[thermal]` were the last two to
land, and both agree with the figures measured before the review-driven test
additions — which they had to, since those five cases touch `tests/cfd/` and
`src/cfd/` only, but agreeing and having-to-agree are different claims and only
the measurement is evidence.

This paragraph first said `[thermal]` had been read EARLIER and offered the
argument in place of the measurement. The measurement finished and the
paragraph is corrected rather than left standing, which is the correction
`P19-STEADY-001`'s freeze had to make about its own pre-freeze claim.

The post-gate **binary readout** is still the stronger reading, and it is taken
as well: it reads the per-tag assertion identity from each of the three
QUALIFIED executables, which is the tree that was actually run rather than the
tree that was frozen.

## Pre-freeze state of the gates, on debug-ext

Every one of these was run on the frozen tree before the freeze:

```text
[cfd][transient]              34 cases, 1813 assertions, PASS, 13.8 s
compile_fail.cfdtransient     28 of 28 PASS, 75 s
  + the control target        compiles with no BETTERCAD_CF_* macro
architecture                  205 of 205 PASS
release-ext      builds bettercad_tests and the control, 0 error / 0 warning
debug-shared-ext builds bettercad_tests and the control, 0 error / 0 warning
```

The last two were built deliberately before freezing. A configuration-specific
compile error — an unused variable that only `-DNDEBUG` exposes, say — would
otherwise have voided a three-and-a-half-hour run, and `P19-STEADY-001` has just
paid that price once for a different reason.

### The compile-fail group passed on its first run, and one regex was narrowed anyway

All 28 gates failed to compile for the reason they name, first time. The
`P19-GRAD-001` lesson — list every diagnostic GCC can emit, not the one a reader
expects — was applied when the group was written rather than after it failed.

One regex was then **narrowed** rather than widened, which is the opposite of
the usual pressure. `A_TIME_GRIDS_STEP_COUNT_IS_WRITABLE` originally accepted
`has no member|no member named` alongside the access diagnostics, and an
adversarial review pointed out that this re-opens exactly the hole the gate's
own comment closes: rename or delete `steps_` and the gate would pass on a "no
member" diagnostic while the ACCESS claim stopped being tested. That is
`P19-STEADY-001`'s arity-for-access failure in a different disguise. The regex
is now `is private|within this context` and the gate still passes, so the
diagnostic really is the access one.

## The reviews and the probes, both before this freeze

Two independent reviews ran against the implementation, on concerns the author
should not judge alone: an adversarial review and a mutation / test-gap
analysis. **Both found the same live defect, and the author had found it first**
— `CfdTransientStepRecord::balance` was never assigned, so every step record
published two zeros where the qualified evaluator's two measures belong, and the
suite passed because the only assertion on it was `isfinite` and zero is finite.

Between them they produced nine further findings and about thirty-one surviving
mutations. What was acted on, and what was not, is in the README's ADVERSARIAL
REVIEW section with reasons. Five test cases and roughly 370 assertions were
added after the reviews, which is why this freeze's counts are higher than the
launcher's first draft carried — that draft was counted off the pre-review tree
and is corrected in place, with the correction stated.

## The harness

Carried forward unchanged. Five files are **byte-identical** to
`P19-STEADY-001`'s, by raw `sha1sum`:

```text
cf6c1f112d71586098a78652091d0375c8d04051  launch.cmd
59910328ab43e07b9e0c474f969994ad483d3f68  preflight-space.cmd
b76d9325e8131c265443540a9ac914e236731a66  preflight-space.ps1
244b38de8feac0475af7cdc56ba9415d27d8dd29  qualify.cmd
afd0197d4f4726db7f533c2f27f4f6e069ba83c6  verify-harness.cmd
```

`run-qualification.cmd` is the only one that differs, and only in its `rem`
header: the executable half — from `@echo off` to the final `exit /b` — was
diffed against `P19-STEADY-001`'s and is **identical**, so the presets, the
repeat filter, the space preflight and the parallelism are the same ones the
last sixty-seven milestones ran.

This is the SIXTY-EIGHTH milestone the harness has run with its executable
content unchanged, and it is self-checked before each run, because an unchanged
harness is not the same as a working one.

### An inherited defect in `verify-harness.cmd`, carried forward unfixed

`verify-harness.cmd` contains a literal `0x08` byte where `\bc-build` was meant,
in 72 of the 96 committed copies across the lineage, entered 2026-09-30 with the
P15 materials work. It fires only when `BETTERCAD_BUILD_ROOT` is undefined,
which `run-qualification.cmd` always defines, so **no gate in any milestone has
ever evaluated it**. It is carried forward byte-for-byte here rather than fixed:
repairing it across 96 copies is a lineage-wide change and a scope decision for
the owner, not something to fold into a feature milestone. `TODO.md` records it
as an open finding.

## What the verdict is, and what it is not

**The verdict is the harness's own two statements:**

```text
1  run-qualification.out        its own sentence about pass or failure
2  qualification-times.txt      its failed-stage count
```

**It is NOT this launcher's exit code.** `P19-MOM-001`'s first attempt was
reported as "completed (exit code 0)" while the harness had written
`QUALIFICATION FAILED: 2 stage(s) failed`, and `P19-STEADY-001`'s first attempt
did exactly the same thing one day ago — exit 0 from the launcher, one test of
6903 failing in all three presets. Two milestones running. The exit code is not
read.

A clean ctest run prints `100% tests passed out of 6965`. A failing one prints
`99% tests passed, N tests failed out of 6965`. The *presence* of the substring
`tests failed out of` is itself a failure marker, and
`0 tests failed out of 6965` is a string ctest cannot print in a passing run —
`P19-STEADY-001`'s reconciliation script looked for exactly that and reported
three false mismatches against itself.

## What is reconciled after the run

```text
the verdict          both harness statements, quoted
stage count          17 of 17 exit 0
the three builds     0 `error:` and 0 `warning:` under -Werror
the three ctest logs 6965 of 6965, counted more than one way per log
the two repeat logs  17635 executions each, and the marker-to-progress ratio
freshness            no graph output newer than the test binary; no build run
                     to read it
the binaries         per-tag assertion identity read FROM the three qualified
                     executables, because ctest captures no stdout from a
                     passing test and the ctest logs therefore cannot establish
                     it -- the earlier practice of claiming they could had no
                     log behind it
the eight identities re-read from the working tree by this file's own method,
                     then off the staged tree, then OUT OF the commit object
```

## What is not frozen

`docs/`, `TODO.md` and `ROADMAP.md`. Nothing in them can reach the executable or
the tests. The qualification's own logs land under
`docs/verification/P19-TRANSIENT-001/qualification/` while the run is in
progress, which is why they cannot be inside the frozen surface.

`.claude/`, `AGENTS.md` and `P19-PVCOUPLE-001`'s four `.pristine` and log files
are untracked and stay that way: that milestone's own FREEZE.md and README
record them as deliberately not committed, with the reason.
