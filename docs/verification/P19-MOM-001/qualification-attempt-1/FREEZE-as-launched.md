# P19-MOM-001 — qualification freeze

Frozen 2026-11-08, before the gate was launched. **The source and test tree
below is the tree that was qualified.** If anything under the frozen surface
changes after this record, the qualification is void and is run again from
clean.

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
apps               fc398a3d2e524178cf38e8b667236379d8381086
include            3bec67c503693f2c8134de3c5ca490f6d7a7eeef
src                8fa9cccf3264215e80067885a1ea9fd1b1f1b200
tests              b014d2d8958aa6a2fc6a732791665db500b25608
examples           4da234ebf4779f7c16d5df0075f106f7e3d4c958
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e

HEAD at the freeze  db53c5095cd091a9abdb7cf143fd02326ef9a2b8
```

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

**NO EXISTING SOURCE FILE IS MODIFIED.** The three CMake lists gain one line
each; everything else under the frozen surface is new.

## The harness

Five of the six files in `qualification/` are byte-identical copies of
`P19-CONV-001`'s, **verified by hash rather than asserted**:

```text
qualify.cmd          d313a64070718c44fae290ac042fe259d1a03c8b
verify-harness.cmd   11855b8ff891093b021d1e0351761283d0eedd24
launch.cmd           f11039e6dc366d7c469a15cf74fb3867fd5ffeff
preflight-space.cmd  a65d6578000b6e8a2811d5154bcec24be09c2b54
preflight-space.ps1  2f0d4228488f6a574ca57ec89f0fad0107b16b59
```

`run-qualification.cmd` is this milestone's own, and **every one of its lines
begins with `rem`, `set`, `call`, `echo`, `setlocal`, `endlocal` or `@echo`
followed by a space or the end of the line, checked by a WHOLE-WORD match
before the run**. That check exists because `P19-ARCH-001`'s copy carried a
line beginning `remrem` which a prefix match let through; `cmd` ran it as a
command and printed "not recognized" to stderr. The check caught the same
splice twice more since.

Its repeat filter is byte-identical to `P19-CONV-001`'s, so **the only
difference between this qualification and that one is the tree under test.**

## The three figures, COUNTED and not predicted

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

**THE THIRD FIGURE COUNTS EXECUTIONS, NOT RESULT LINES, AND THE DISTINCTION IS
RECORDED HERE BECAUSE `P19-CONV-001`'S FREEZE GOT IT WRONG.** `ctest --repeat
until-fail:5` prints ONE result line per test and FIVE `Start` markers, so the
log holds 3306 result lines and 16,530 `Start` markers. That milestone's freeze
labelled the larger number as result lines, its reconciliation failed on its
first run, and the figure turned out right while the label was wrong. The ratio
of markers to result lines must be exactly 5.000.

**AND THE FULL-SUITE FIGURE COUNTS TESTS, NOT DISTINCT NAMES**, which is the
other label `P19-CONV-001` recorded wrongly: a set of names comes out smaller
than the test count, because a `(\S+)` pattern truncates the Catch2 names that
contain spaces.

The cfd subsystem's own arithmetic, which is the check a passing suite cannot
make:

```text
before (db53c50)   702,187 assertions in 447 cases
at the freeze      716,417 assertions in 495 cases
delta               14,230              and      48
the momentum suite  14,230 assertions in  48 cases    EXACTLY
```

A deleted test and an added test net to nothing in a pass/fail count. They do
not net to nothing here, so **no pre-existing cfd assertion moved.**

## What is NOT frozen, and why it is recorded anyway

```text
the verdict          run-qualification.out's own sentence plus the failed-stage
                     count in qualification-times.txt. NOT the launcher's exit
                     code: this session has already seen a launcher print
                     "is not recognized" while the surrounding task reported
                     exit 0, so the code and the verdict are different things
the freshness half   that the binaries are newer than the clean and than the
                     newest source is checked AFTER the run, by hand, and
                     recorded in the evidence
```

## Pre-freeze state of the gates

```text
debug-ext            48 unit cases / 14,230 assertions, all pass
debug-shared-ext     identical assertion count
release-ext          builds clean under -Werror after the array-bounds fix
compile_fail.cfdmom  28 of 28 pass
architecture         205 of 205 pass, layering included
mutation probes      22 of 24 real mutations killed; two honest survivors and
                     one no-op control
reviews              two independent, both BEFORE this freeze, 42 findings, all
                     resolved
```

**THE RELEASE PRESET WAS BUILT BEFORE THE FREEZE AND THAT IS WHY THIS FREEZE
EXISTS AT ALL.** `-Werror=array-bounds` at `-O3` rejected a `vector.back()` the
Debug preset accepts. Launching on the strength of a passing `debug-ext` run
would have voided this freeze four hours later — the three hours
`P19-DIV-001` lost.
