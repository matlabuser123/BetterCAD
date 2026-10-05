# P16-CLI-001 — qualification freeze

```text
SUBJECT:  the identity of the tree that was qualified, so it can be compared
          with the tree that was committed
FROZEN:   18:21 local, 2026-10-05
```

## Why the gates run in this order

Cheapest first. All three of P15's voided qualifications came from doing a
cheap check after an expensive one, and the `debug-shared-ext` gate caught a
real defect for P16-CMD-001.

```text
1. git diff --check, tracked AND untracked             seconds
2. grep the new headers for an exported constexpr      seconds
3. the new tests, 5 repeats                            35 s
4. the shared build                                    minutes   <- DLL boundary
5. the shared full suite                               22 minutes
6. the debug-ext full suite                            21 minutes
7. FREEZE
8. three-preset qualification                          hours
9. the qualified tree == the committed tree
```

Mutation testing came **before** the freeze too, and this time it earned its
place: it found a production defect (finding C1's sibling, M6) that every
other gate had passed over. Freezing first would have meant fixing the defect
after the freeze and re-running everything.

## Pre-freeze gate results

```text
git diff --check                clean. The untracked files were checked
                                separately -- git cannot see whitespace in a
                                file it does not track
exported constexpr              none in StructuredOutput.hpp or Selectors.hpp
new tests, until-fail:5         27 tests x 5 back to back, 0 failures, 35.41 s
debug-shared-ext configure      exit 0
debug-shared-ext build          exit 0, 0 errors, no dllimport diagnostics
debug-shared-ext full suite     3327 / 3328, 1316.49 s
debug-ext build                 exit 0, 0 warnings
debug-ext full suite            3327 / 3328, 1255.05 s
```

Both suites' single failure is `cli.new.unicode-path`, the known code-page
artefact: these runs were made without `chcp 65001`, which `qualify.cmd` sets
as its first act. Diagnosed on P16-CMD-001 by re-running that one test under
65001, and unchanged since.

**Mutation testing, before the freeze:** 8 mutations of the CLI — **6 killed,
2 proven equivalent mutants, 1 production defect found and fixed**, with the
mutation then re-expressed against the fix and killed on 7 assertions.

**A shell bug of my own, recorded because it wasted a suite run:** the first
attempt chained `grep -c ... && Q=...`, and `grep -c` exits 1 when it matches
nothing, so `Q` was never set and the suite's output went to an invalid path.
Re-run cleanly. `&&` after a counting `grep` is a trap.

## The frozen tree

Eight paths, as `qualify.cmd`'s `:trees` computes them — from a scratch git
index, so a dirty working tree is captured exactly as it stands.

```text
apps               b49722470580d4fe6c2bb2b3594bb3885eee10db   moved
include            8783fbab3e0ebc5257ba4ddd27908725e789a2f3   unchanged
src                f4814471cfa6a9c0706cd69c1c6ff5ccabf85bd1   moved
tests              a90ec164cb1c9eb8e55bc73ee6c8b552665bf2c2   moved
examples           d46fb1706c6548f3f73035e23a6e3f2a0818edfa   unchanged
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7   unchanged
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd   unchanged
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e   unchanged

whole fingerprint  f58a1e64cbff9ec9f2d6f7c3d0816bace905843e
frozen from        7024649  (HEAD, P16-PERSIST-001)
```

`apps` moved for the first time in three milestones — this is the one that
adds a CLI surface. `src` moved for a single file:
`src/meshing/netgen/NetgenBackend.cpp`, which gained the scope that silences
the backend's stdout. `include` is unchanged, because the CLI's headers live
under `apps/` and the backend change is entirely internal.

`examples` is unchanged: the workflow script drives the existing
`plate.bcad`, and P16-PERSIST-001's `meshed_plate.bcad` serves the new process
tests, so this milestone needed no new fixture of its own.

`docs/` and `deps/` are outside the fingerprint, so the evidence directory may
be written after the freeze. The mutation harness's `pristine/` snapshots live
there, and the working files were diffed against them immediately before the
freeze — because an orphaned harness once left a mutation applied to an
untracked source file, which `git status` and `git diff` both report as clean.

## The qualification

Launched **detached** at 18:22, so no wrapper lifetime bounds it. One run,
uninterrupted, 2 h 17 min.

```text
Qualification passed: every stage exited 0.
qualify.cmd exit 0          (the exit code IS the number of failed stages)

PRESET            CONFIGURE  CLEAN  BUILD  NO-OP REBUILD  CTEST
debug-ext              0       0      0         0           0   3328/3328
release-ext            0       0      0         0           0   3328/3328
debug-shared-ext       0       0      0         0           0   3328/3328

REPEAT (5x each selected test, back to back)
release-ext            0                            442/442   155.24 s
debug-ext              0                            442/442   181.55 s

warnings, all three builds   0       (-Werror, 592 objects each)
no-op rebuilds               0 compiles, 0 links -- the binaries tested are
                             the ones just built
shared build                 9 DLLs linked, including libbettercad_meshing.dll,
                             so the CLI process tests had to resolve that
                             closure at load time -- the runtime proof
```

Timeline: 18:22:27 start, debug-ext 19:04:29, release-ext 19:52:08,
debug-shared-ext 20:34:04, repeats 20:36:40 and 20:39:42, finished 20:39:45.

**Fresh-binary proof, per preset.** Every CLI process test and the workflow
script take the executable from `$<TARGET_FILE:bettercad_cli>`, which resolves
per preset, so each preset ran the binary it had just built. Nothing is
resolved from `PATH`, and the workflow script prints the absolute path and the
size of the executable it ran.

## The qualified tree is the committed tree

`qualify.cmd` records the eight paths at both ends of the run. After the last
test:

```text
apps               b49722470580d4fe6c2bb2b3594bb3885eee10db   unchanged
include            8783fbab3e0ebc5257ba4ddd27908725e789a2f3   unchanged
src                f4814471cfa6a9c0706cd69c1c6ff5ccabf85bd1   unchanged
tests              a90ec164cb1c9eb8e55bc73ee6c8b552665bf2c2   unchanged
examples           d46fb1706c6548f3f73035e23a6e3f2a0818edfa   unchanged
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7   unchanged
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd   unchanged
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e   unchanged
```

Identical to the freeze, component for component — whole fingerprint
`f58a1e64cbff9ec9f2d6f7c3d0816bace905843e`. Nothing that can reach the
executable or the tests moved during the run.

Only `docs/verification/P16-CLI-001/` was written after the freeze, and it is
outside the fingerprint by construction.
