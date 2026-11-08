# P19-MOM-001 — qualification freeze, ATTEMPT 2

Frozen 2026-11-08, after attempt 1 failed. **The source and test tree below is
the tree that was qualified.** If anything under the frozen surface changes
after this record, the qualification is void and is run again from clean.

Attempt 1's freeze is preserved verbatim at
[`qualification-attempt-1/FREEZE-as-launched.md`](qualification-attempt-1/FREEZE-as-launched.md)
**including the claim it got wrong**, and its failure is analysed in
[`qualification-attempt-1/README.md`](qualification-attempt-1/README.md). The
short version: that freeze recorded `release-ext builds clean under -Werror`,
the build it recorded was of an EARLIER tree, and the gate failed 66m28s in on
a `pop_back()` in a test helper written after it.

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
would miss every uncommitted change. Four of the seven new files are untracked
at the freeze, so that distinction is the whole point.

```text
                   attempt 2 (THIS RUN)                      vs attempt 1
apps               fc398a3d2e524178cf38e8b667236379d8381086   same
include            3bec67c503693f2c8134de3c5ca490f6d7a7eeef   same
src                8fa9cccf3264215e80067885a1ea9fd1b1f1b200   same
tests              5dfc326b0db05b78b865a38557f1e0666f806682   CHANGED
examples           4da234ebf4779f7c16d5df0075f106f7e3d4c958   same
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7   same
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd   same
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e   same

HEAD at the freeze  db53c5095cd091a9abdb7cf143fd02326ef9a2b8  same
attempt 1's tests   b014d2d8958aa6a2fc6a732791665db500b25608
```

**EXACTLY ONE OF EIGHT SUBTREES MOVED, AND IT IS THE RIGHT ONE.** The fix
touches `tests/cfd/CfdMomentumTests.cpp` and nothing else, so seven hashes
reproducing bit-for-bit is the check that no production source shifted while the
defect was chased — and it is also the evidence that the `CRLF will be replaced
by LF` warnings `git add` prints over this repository do not perturb the
identity, since those seven hashes came back identical across two separate
scratch-index runs.

The working-tree state of the frozen surface, recorded so a reader can see what
the eight hashes cover:

```text
 M src/cfd/CMakeLists.txt
 M tests/CMakeLists.txt
 M tests/compile_fail/CMakeLists.txt
?? include/bettercad/cfd/CfdMomentum.hpp
?? src/cfd/CfdMomentum.cpp
?? tests/cfd/CfdMomentumTests.cpp
?? tests/compile_fail/CfdMomentumMisuse.cpp
```

**NO EXISTING SOURCE FILE IS MODIFIED**, and the file list is identical to
attempt 1's: the fix added nothing and removed nothing.

## The harness

Five of the six files in `qualification/` are byte-identical copies of
`P19-CONV-001`'s, **verified by hash rather than asserted**, and unchanged from
attempt 1:

```text
qualify.cmd            d313a64070718c44fae290ac042fe259d1a03c8b
verify-harness.cmd     11855b8ff891093b021d1e0351761283d0eedd24
launch.cmd             f11039e6dc366d7c469a15cf74fb3867fd5ffeff
preflight-space.cmd    a65d6578000b6e8a2811d5154bcec24be09c2b54
preflight-space.ps1    2f0d4228488f6a574ca57ec89f0fad0107b16b59
run-qualification.cmd  dae1279bf12a965765ef1a26f06abeab9e47d346   (this
                       milestone's own; recorded by hash this time, which
                       attempt 1's freeze did not do)
```

`run-qualification.cmd` is unchanged between the two attempts, and **all 161 of
its lines begin with `rem`, `set`, `call`, `echo`, `setlocal`, `endlocal` or
`@echo` followed by a space or the end of the line, re-checked by a WHOLE-WORD
match after the fix**. That check exists because `P19-ARCH-001`'s copy carried a
line beginning `remrem` which a prefix match let through.

`verify-harness.cmd` was re-run and **PASSES**:

```text
PASS: a failed stage gave qualify.cmd exit 2, and a zero-match
      repeat filter was counted as a failed stage.
```

Its deliberate failure prints the same `QUALIFICATION FAILED: 2 stage(s) failed`
sentence attempt 1 produced, which is the check that attempt 1's verdict was a
genuine report and not a harness artefact.

Its repeat filter is byte-identical to `P19-CONV-001`'s, so **the only
difference between this qualification and that one is the tree under test.**

## The three figures, COUNTED and now CONFIRMED

Counted off the frozen tree with `ctest -N`, and written as sums so that a
figure needing a fudge term would mean a test had been dropped or registered
twice:

```text
full suite, per preset     6601  = 6525 (P19-CONV-001) + 48 unit + 28
                                   compile-fail, EXACTLY
repeat set, per preset     3306  = 3258 (P19-CONV-001) + 48 matching
                                   unit\.Cfd, EXACTLY
repeat EXECUTIONS per log 16530  = 3306 x 5
```

**ATTEMPT 1 CONFIRMED ALL THREE**, which is the one thing that run bought:

```text
debug-ext         100% tests passed out of 6601        matches
debug-shared-ext  100% tests passed out of 6601        matches
repeat debug-ext  100% tests passed out of 3306        matches
                  16530 Start markers / 3306 result lines = 5.000 exactly
```

The fix changes one helper's body and adds no test case, so **attempt 2 must
reproduce 6601, 3306 and 16530 unchanged.** That is a prediction recorded before
the run, not a figure to be read off afterwards.

**THE THIRD FIGURE COUNTS EXECUTIONS, NOT RESULT LINES, AND THE DISTINCTION IS
RECORDED HERE BECAUSE `P19-CONV-001`'S FREEZE GOT IT WRONG.** `ctest --repeat
until-fail:5` prints ONE result line per test and FIVE `Start` markers. Attempt
1 measured 16530 markers against 3306 result lines, so the ratio is now
**measured at exactly 5.000** rather than asserted.

**AND THE FULL-SUITE FIGURE COUNTS TESTS, NOT DISTINCT NAMES**: a set of names
comes out smaller than the test count, because a `(\S+)` pattern truncates the
Catch2 names that contain spaces.

The cfd subsystem's own arithmetic, re-measured on the fixed tree, which is the
check a passing suite cannot make:

```text
before (db53c50)   702,187 assertions in 447 cases
at this freeze     716,417 assertions in 495 cases
delta               14,230              and      48
the momentum suite  14,230 assertions in  48 cases    EXACTLY
```

A deleted test and an added test net to nothing in a pass/fail count. They do
not net to nothing here, so **no pre-existing cfd assertion moved** — and the
figures are unchanged from attempt 1's freeze, so the fix moved no assertion
either.

## What is NOT frozen, and why it is recorded anyway

```text
the verdict          run-qualification.out's own sentence plus the failed-stage
                     count in qualification-times.txt. NOT the launcher's exit
                     code. THIS IS NO LONGER A PRECAUTION: in attempt 1 the
                     launcher was recorded as "completed (exit code 0)" while
                     the harness wrote "QUALIFICATION FAILED: 2 stage(s)
                     failed" and "qualify.cmd exit 2". Reading the code would
                     have reported a pass
the freshness half   that the binaries are newer than the clean and than the
                     newest source is checked AFTER the run, by hand, and
                     recorded in the evidence
```

## Pre-freeze state of the gates

**EVERY LINE BELOW WAS MEASURED AFTER THE LAST EDIT TO THE FROZEN SURFACE**,
which is the discipline attempt 1 lacked:

```text
release-ext          BUILT TO COMPLETION on this tree. Attempt 1 stopped at
                     target 709 of 813, so about a hundred targets and every
                     link were unproven; the completion build ran 103 targets
                     including `Linking bettercad_tests.exe` with ZERO
                     occurrences of "warning:", "error:" or "FAILED"
debug-ext            built; 3 targets, zero diagnostics
no-op proof          a second `cmake --build` on BOTH presets does only the
                     always-run "Checking git revision" target, so both
                     binaries are newer than every source file
momentum suite       76 of 76 ctest entries pass in debug-ext AND release-ext
                     76 = 48 unit + 28 compile-fail, EXACTLY
assertions           14,230 in 48 cases, IDENTICAL in debug-ext and
                     release-ext, taken from the final binaries
cfd subsystem        716,417 assertions in 495 cases
architecture         205 of 205 (attempt 1, and they need no binary)
mutation probes      22 of 24 real mutations killed; two honest survivors and
                     one no-op control
reviews              two independent, both before attempt 1's freeze, 42
                     findings, all resolved
```

**THE NO-OP PROOF CAUGHT A REPEAT OF THE VERY MISTAKE THAT FAILED ATTEMPT 1.**
The first `cmake --build --preset release-ext` after the fix was expected to be
a no-op and was not: it recompiled `CfdMomentumTests.cpp.obj` and relinked,
because a comment edit had landed after the completion build began. Had that
check not been run, attempt 2 would have been launched on the strength of a
Release build that again predated the last edit — the same error, one attempt
later. The check was added because of attempt 1 and it earned its place
immediately.

```text
ATTEMPT 1'S LESSON   build the second preset BEFORE freezing
ATTEMPT 2 ADDS       and build it AFTER THE LAST EDIT to anything it compiles,
                     then prove it with a no-op build rather than a timestamp
```

## Outcome

Attempt 2 **PASSED**: `Qualification passed: every stage exited 0.`, 17 of 17
stages, 3h43m06s, and `0 stage(s) failed`. The three figures predicted above
came back exactly — 6601, 3306 and 16530.

The eight subtree identities above were then read a fourth time, out of the
commit object rather than out of a scratch index:

```text
committed as  b9842f21a6df97d8eb10562da357a482a5b28d9b
all eight     identical to this freeze, to the harness's before-build reading
              and to its after-last-test reading
```

**THE TREE THAT WAS QUALIFIED IS THE TREE THAT WAS COMMITTED.** Full
reconciliation in [`README.md`](README.md) under REGRESSION and QUALIFICATION.
