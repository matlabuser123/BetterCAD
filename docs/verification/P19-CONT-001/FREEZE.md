# P19-CONT-001 — qualification freeze

Frozen 2026-11-08, after the two adversarial reviews, the repairs they forced,
the hygiene pass and the mutation probes. **The source and test tree recorded
here is the tree that was qualified.** If anything under the frozen surface
changes after this record, the qualification is void and is run again from clean.

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
                   P19-CONT-001                                 vs P19-MOM-001
apps               fc398a3d2e524178cf38e8b667236379d8381086      same
include            bb7f9c6c442b2fa7bed549fd738cc182d585a862      CHANGED
src                9d7539d75f47bc003322dfd4917ae30fb88ce659      CHANGED
tests              55ba3a0a5a3ce1b2b974fef411b6d25ebd597cf8      CHANGED
examples           4da234ebf4779f7c16d5df0075f106f7e3d4c958      same
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7      same
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd      same
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e      same

HEAD at the freeze  8e6e71f932b63efee4fe91384d3ce65dc2cff337
```

**EXACTLY THREE SUBTREES MOVED AND THEY ARE THE RIGHT THREE** — one new public
header, one new source plus a one-line CMake edit, one new unit suite and one
new compile-fail group plus two one-line CMake edits. `apps`, `examples`,
`cmake` and both top-level files reproducing bit-for-bit is the check that
nothing else drifted during a long session.

The working-tree state of the frozen surface, **diffed for unexpected files**:

```text
 M src/cfd/CMakeLists.txt
 M tests/CMakeLists.txt
 M tests/compile_fail/CMakeLists.txt
?? include/bettercad/cfd/CfdContinuity.hpp
?? src/cfd/CfdContinuity.cpp
?? tests/cfd/CfdContinuityTests.cpp
?? tests/compile_fail/CfdContinuityMisuse.cpp
```

**NO EXISTING SOURCE FILE IS MODIFIED.** And the diff is read for unexpected
ENTRIES as well as unexpected content, which is not a formality on this
milestone: an adversarial reviewer left a
`src/cfd/CfdContinuity.cpp.adv-review-backup` inside the frozen surface, where
`git add -A src` would have committed it, together with a disabled range guard
in the source itself that the suite passes either way.

## A NEW CHECK THIS MILESTONE ADDS: the build caches

**THE HARNESS PINS THE TOOLCHAIN AND DOES NOT PIN DEPENDENCY RESOLUTION.**

`run-qualification.cmd` puts the pinned WinLibs bundle first on `PATH`, which
fixes gcc, g++, cmake, ctest and ninja. But `find_package(CONFIG)` does not care
about `PATH` ORDER — it derives candidate PREFIXES from `PATH` entries. This
session's `PATH` carries `C:/msys64/ucrt64/bin`, so MSYS2 is a candidate prefix
for Open CASCADE, Qt and Netgen; a CMake REGENERATE resolved OCCT to MSYS2's
**7.9.3** and wrote `OpenCASCADE_DIR=C:/msys64/...` into `CMakeCache.txt`.

```text
a cached OpenCASCADE_DIR SHORT-CIRCUITS find_package, so the tree cannot be
  repaired by re-running `cmake --preset` -- it has to be DELETED
`ninja -t clean` does NOT remove CMakeCache.txt, and qualify.cmd's configure
  stage reuses an existing cache, so the cache state AT LAUNCH is part of the
  qualified configuration -- and nothing in the harness looks at it
```

`P19-MOM-001` passed because no regenerate fired during it. That is luck, not a
guarantee. So the caches are verified here, and again after the run. **The
harness itself is NOT changed**: it stays byte-identical to the lineage every
previous milestone used, and a CHECK is added rather than a variable.

```text
debug-ext          OpenCASCADE_DIR = <deps>/lib/cmake/opencascade   msys64 refs 0
release-ext        OpenCASCADE_DIR = <deps>/lib/cmake/opencascade   msys64 refs 0
debug-shared-ext   OpenCASCADE_DIR = <deps>/lib/cmake/opencascade   msys64 refs 0

<deps> = C:/Users/Hasib/AppData/Local/bettercad-deps/gnu-16-mingw-amd64
```

## The harness

Five of the six files in `qualification/` are byte-identical copies of
`P19-MOM-001`'s, **verified by hash rather than asserted**:

```text
qualify.cmd            d313a64070718c44fae290ac042fe259d1a03c8b
verify-harness.cmd     11855b8ff891093b021d1e0351761283d0eedd24
launch.cmd             f11039e6dc366d7c469a15cf74fb3867fd5ffeff
preflight-space.cmd    a65d6578000b6e8a2811d5154bcec24be09c2b54
preflight-space.ps1    2f0d4228488f6a574ca57ec89f0fad0107b16b59
run-qualification.cmd  4492a8a3b2ba64676d5f6bb8670a49ca81c27b47   this
                       milestone's own, recorded by hash
```

All **165** lines of `run-qualification.cmd` begin with `rem`, `set`, `call`,
`echo`, `setlocal`, `endlocal` or `@echo` followed by a space or the end of the
line, re-checked by a WHOLE-WORD match after the last edit. That check exists
because `P19-ARCH-001`'s copy carried a line beginning `remrem` which a prefix
match let through.

`verify-harness.cmd` was re-run and **PASSES**:

```text
PASS: a failed stage gave qualify.cmd exit 2, and a zero-match
      repeat filter was counted as a failed stage.
```

Its repeat filter is byte-identical to `P19-MOM-001`'s, so **the only difference
between this qualification and that one is the tree under test.**

## The three figures, COUNTED and not predicted

Counted off the frozen tree with `ctest -N`, and written as sums so that a
figure needing a fudge term would mean a test had been dropped or registered
twice:

```text
full suite, per preset     6687  = 6601 (P19-MOM-001) + 49 unit + 37
                                   compile-fail, EXACTLY
repeat set, per preset     3355  = 3306 (P19-MOM-001) + 49 matching
                                   unit\.Cfd, EXACTLY
repeat EXECUTIONS per log 16775  = 3355 x 5
```

**THE THIRD FIGURE COUNTS EXECUTIONS, NOT RESULT LINES.** `ctest --repeat
until-fail:5` prints ONE result line per test and FIVE `Start` markers, so the
log holds 3355 result lines and 16,775 `Start` markers; the ratio of markers to
result lines must be exactly 5.000. `P19-CONV-001`'s freeze labelled these the
wrong way round and its reconciliation failed on the first run.

**AND THE FULL-SUITE FIGURE COUNTS TESTS, NOT DISTINCT NAMES**: a set of names
comes out smaller than the test count, because a `(\S+)` pattern truncates the
Catch2 names that contain spaces.

The cfd subsystem's own arithmetic, which is the check a passing suite cannot
make:

```text
before (8e6e71f)   716,417 assertions in 495 cases
at the freeze      723,852 assertions in 544 cases
delta                7,435              and      49
the continuity suite 7,435 assertions in  49 cases    EXACTLY

and identical in debug-ext, release-ext AND debug-shared-ext
```

A deleted test and an added test net to nothing in a pass/fail count. They do
not net to nothing here, so **no pre-existing cfd assertion moved.**

## Pre-freeze state of the gates

**EVERY LINE BELOW WAS MEASURED AFTER THE LAST EDIT TO THE FROZEN SURFACE.**

```text
all three presets    built to completion, 0 warnings, 0 errors.  The first pass
                     built 64 targets -- apps, GUI, CLI and examples had not
                     been built since the tree was wiped -- so the no-op check
                     was run AGAIN
no-op proof          a second `cmake --build` on ALL THREE presets does only the
                     always-run "Checking git revision" target, so every binary
                     is newer than every source
continuity suite     7,435 assertions in 49 cases, IDENTICAL in all three
compile-fail         37 of 37 pass; 37 registered = 37 in the file, diffed BOTH
                     ways
mutation probes      22 of 22 behavioural probes caught (19 by tests, 3 by the
                     COMPILER); 5 of 5 unreachable guards survived, which is the
                     correct answer; the NO-OP CONTROL SURVIVED, and both files'
                     sha256 match pristine before and after every probe
reviews              two independent, both BEFORE this freeze, 31 findings and
                     12 measured mutation survivors, all resolved or recorded
caches               verified above
```

## What is NOT frozen, and why it is recorded anyway

```text
the verdict          run-qualification.out's own sentence plus the failed-stage
                     count in qualification-times.txt. NOT the launcher's exit
                     code: P19-MOM-001's attempt 1 was recorded as "completed
                     (exit code 0)" while the harness wrote "QUALIFICATION
                     FAILED: 2 stage(s) failed"
the freshness half   that the binaries are newer than the clean and than the
                     newest source is checked AFTER the run, by hand, with a
                     GLOBAL sort -- `dir /s /b /o-d` sorts within each path
                     argument and not across them, which made P19-MOM-001's
                     first freshness check name a two-month-stale file as the
                     newest under the frozen surface
the caches           verified before the launch, per the section above, and
                     again after the run
```
