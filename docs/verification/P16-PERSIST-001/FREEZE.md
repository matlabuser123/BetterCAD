# P16-PERSIST-001 — qualification freeze

```text
SUBJECT:  the identity of the tree that was qualified, so it can be compared
          with the tree that was committed
FROZEN:   13:31 local, 2026-10-05
```

A milestone with a qualification gate needs the tree that was qualified to be
the tree that is committed. This file exists so that claim can be checked
rather than believed.

## Why the gates run in this order

Cheapest first, because all three of P15's voided qualifications came from
doing a cheap check after an expensive one — and this session's own
`debug-shared-ext` gate caught a real defect for P16-CMD-001 two milestones
ago:

```text
1. git diff --check, tracked AND untracked          seconds
2. grep for an exported constexpr in new headers    seconds
3. the new tests, 5 repeats                         1 minute
4. the shared build                                 minutes   <- DLL boundary
5. the shared full suite                            20 minutes
6. the debug-ext full suite                         19 minutes
7. FREEZE
8. three-preset qualification                       hours
9. the qualified tree == the committed tree
```

Step 2 is new this milestone: `BETTERCAD_*_EXPORT` on a header-defined
`constexpr` compiles in both static presets and is fatal only across a DLL
boundary, which cost P16-CMD-001 a build. Grepping for it takes a second and
is now done before the expensive gates rather than discovered by them.

## Pre-freeze gate results

```text
git diff --check                clean. The untracked files were checked
                                separately -- git cannot see whitespace in a
                                file it does not track
exported constexpr              none in src/io/json/ObjectJson.hpp
new tests, until-fail:5         33 tests x 5 back to back, 0 failures, 64.68 s
debug-shared-ext configure      exit 0
debug-shared-ext build          exit 0, 0 errors, no dllimport diagnostics
debug-shared-ext full suite     3300 / 3301, 1170.88 s
debug-ext build                 exit 0, 0 warnings
debug-ext full suite            3300 / 3301, 1131.51 s
```

Both suites' single failure is `cli.new.unicode-path`, the known code-page
artefact: these runs were made without `chcp 65001`, which `qualify.cmd` sets
as its first act. Diagnosed by re-running that one test under 65001 on
P16-CMD-001 rather than assumed, and unchanged since.

**Mutation testing, before the freeze:** 12 mutations of
`src/io/json/MeshControlJson.cpp`, **12 killed, every one by a test**, 0
survivors. One (M11) was first killed only by the compiler and was rewritten
so a test had to catch it.

## The frozen tree

Eight paths, as `qualify.cmd`'s `:trees` computes them — from a scratch git
index, so a dirty working tree is captured exactly as it stands rather than as
`HEAD` has it.

```text
apps               5b2059dc47dd2db91e3fc6a1c6b361074c15a0a6   unchanged
include            8783fbab3e0ebc5257ba4ddd27908725e789a2f3   unchanged
src                abbe62da60e85364ddc76ab88f6ba8921066cc3b   moved
tests              e3e4b9d7d123bd244894360e4819d4d2ff6328b9   moved
examples           d46fb1706c6548f3f73035e23a6e3f2a0818edfa   moved
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7   unchanged
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd   unchanged
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e   unchanged

whole fingerprint  a8ffe41b42ddab5860a15a2463f12bf1efcbbb83
frozen from        8ae2999  (HEAD, P16-CMD-001)
```

`apps` is **unchanged**, which is the expected shape: this milestone persists
meshing intent and touches the application not at all. `include` is unchanged
too — the only header edited is `src/io/json/ObjectJson.hpp`, which is private
to `io` and therefore under `src`. `examples` moved because of the
hand-written reference model.

`docs/` and `deps/` are outside the fingerprint, so this evidence directory may
be written after the freeze without voiding it. Nothing that can reach the
executable or the tests may be.

**The mutation harness's `pristine/` snapshot is under `docs/`**, and the
working file was diffed against it immediately before the freeze — because
this session found a run that had died leaving a mutation applied to an
untracked source file, which `git status` and `git diff` both report as clean.

## The qualification

Launched **detached** at 13:32, so no wrapper lifetime bounds it. One run,
uninterrupted, 1 h 57 min.

```text
Qualification passed: every stage exited 0.
qualify.cmd exit 0           (the exit code IS the number of failed stages)

PRESET            CONFIGURE  CLEAN  BUILD  NO-OP REBUILD  CTEST
debug-ext              0       0      0         0           0   3301/3301
release-ext            0       0      0         0           0   3301/3301
debug-shared-ext       0       0      0         0           0   3301/3301

REPEAT (5x each selected test, back to back)
release-ext            0                          1359/1359   357.29 s
debug-ext              0                          1359/1359   357.55 s

warnings, all three builds   0       (-Werror, 589 objects each)
no-op rebuilds               0 compiles, 0 links -- the binaries tested are
                             the ones just built
shared build                 9 DLLs linked, including libbettercad_io.dll AND
                             libbettercad_meshing.dll, so the new io->meshing
                             edge was exercised across a real DLL boundary
```

Timeline: 13:32:04 start, debug-ext 14:06:15, release-ext 14:43:19,
debug-shared-ext 15:17:17, repeats 15:23:16 and 15:29:14, finished 15:29:16.

The repeat stages took **6 minutes each, not the 35 estimated**. The estimate
scaled from the full suite's runtime, which is dominated by slow meshing and
rendering cases; the 1359 selected are cheap unit tests that parallelise
well. Recorded because the estimate was quoted beforehand and was wrong by a
factor of five.

## The qualified tree is the committed tree

`qualify.cmd` records the eight paths at both ends of the run. After the last
test:

```text
apps               5b2059dc47dd2db91e3fc6a1c6b361074c15a0a6   unchanged
include            8783fbab3e0ebc5257ba4ddd27908725e789a2f3   unchanged
src                abbe62da60e85364ddc76ab88f6ba8921066cc3b   unchanged
tests              e3e4b9d7d123bd244894360e4819d4d2ff6328b9   unchanged
examples           d46fb1706c6548f3f73035e23a6e3f2a0818edfa   unchanged
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7   unchanged
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd   unchanged
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e   unchanged
```

Identical to the freeze, component for component — whole fingerprint
`a8ffe41b42ddab5860a15a2463f12bf1efcbbb83`. Nothing that can reach the
executable or the tests moved during the run.

Only `docs/verification/P16-PERSIST-001/` was written after the freeze, and it
is outside the fingerprint by construction.
