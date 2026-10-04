# P16-CMD-001 — qualification freeze

```text
SUBJECT:  the identity of the tree that was qualified, so it can be compared
          with the tree that was committed
FROZEN:   22:38 local, 2026-10-04
```

A milestone with a qualification gate needs the tree that was qualified to be
the tree that is committed. This file exists so that claim can be checked
rather than believed.

## Why the order of the gates is what it is

Every cheap check runs **before** the expensive one, because all three of
P15's voided qualifications came from doing a cheap check after an expensive
one and having to start again:

```text
1. git diff --check                     seconds
2. the new tests, 5 repeats             seconds
3. the shared build                     tens of minutes   <- the DLL boundary
4. the full suite in debug-ext          minutes
5. FREEZE
6. three-preset qualification           hours
7. the qualified tree == the committed tree
```

Only the shared build catches a missing export at a DLL boundary, and
discovering one after the freeze voids the qualification.

## Pre-freeze gate results

```text
git diff --check                 clean (no whitespace errors), including the
                                 nine new untracked files
targeted tests, debug-ext        41 cases, 1390 assertions, 0 failures
ctest --repeat until-fail:5      41 tests x 5 back to back, 0 failures, 11.92 s
compile_fail.meshcmd             13 of 13, and the control compiles
debug-shared-ext configure       exit 0
debug-shared-ext build           exit 0 AFTER one real defect was fixed
                                 (finding A10: BETTERCAD_MESHING_EXPORT on a
                                 constexpr function is dllimport on an inline
                                 function; both static presets accepted it)
debug-shared-ext full suite      3272 / 3273, 1237.89 s
                                 the one failure is cli.new.unicode-path, the
                                 known code-page artefact: re-run under
                                 `chcp 65001` it passes, and qualify.cmd sets
                                 65001 as its first act
debug-ext full suite             3272 / 3273, 1153.17 s -- the same single
                                 code-page failure, and the same resolution
```

Both pre-freeze suites were run without `chcp 65001`, which is why each shows
that one failure; the qualifying runs, which set it, are 3273/3273.

**A10 is the reason this gate exists.** Only `debug-shared-ext` has a DLL
boundary, so only it could refuse that declaration. Taking the freeze first
would have put the failure in the third preset of the three-preset run, hours
in, and voided it.

## The frozen tree

Eight paths, as `qualify.cmd`'s `:trees` computes them — from a scratch git
index, so a dirty working tree is captured exactly as it stands rather than as
`HEAD` has it.

```text
apps               5b2059dc47dd2db91e3fc6a1c6b361074c15a0a6
include            8783fbab3e0ebc5257ba4ddd27908725e789a2f3
src                8eb4087647da0f693124aa7a31094c35f01669db
tests              41795dafbfb682f1e85011fad5bd8c6e97f2ccc2
examples           9187931fa6824e98d3da6296cdc40af6ca4b8607
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e

whole fingerprint  b9593bbc9276e6dbd809a93945152386837c0061
frozen from        0b13e20  (HEAD, P16-VIZ-001)
```

`apps`, `examples`, `cmake`, `CMakeLists.txt` and `CMakePresets.json` are
**unchanged** from P16-VIZ-001's freeze, which is the expected shape: this
milestone adds canonical meshing intent and commands over it, and touches the
application not at all. `include`, `src` and `tests` moved.

`docs/` and `deps/` are outside the fingerprint, so this evidence directory may
be written after the freeze without voiding it. Nothing that can reach the
executable or the tests may be.

## The qualification

### The first attempt was stopped at two hours, and is not the qualification

```text
22:38:55 -> 00:38:55   killed by the wrapper that launched it, at its ceiling
                       debug-ext        every stage 0, ctest 3273 / 3273
                       release-ext      every stage 0, ctest 3273 / 3273
                       debug-shared-ext built and rebuilt 0, ctest interrupted
                       repeat stages    never reached
```

Not a failure: every stage that ran exited 0. The run simply needs about two
hours and ten minutes and the wrapper allowed two. Kept under
[interrupted-run/](qualification/interrupted-run/) rather than deleted, with
its own README.

Before re-running, the tree was re-fingerprinted and **still matched the
freeze exactly** — `b9593bbc`, component for component — so the second run
qualifies the same tree. The whole run was repeated rather than only its
missing stages, because `qualify.cmd`'s exit code is the number of failed
stages across every preset and that one number over one coherent set of logs
is what the gate asks for; splicing two invocations together is the ambiguity
that voided INFRA-VIEWER-001's first qualification.

### The qualifying run

Launched **detached** at 00:40:30, so no wrapper lifetime bounds it. One run,
uninterrupted, 4 h 30 min.

```text
Qualification passed: every stage exited 0.
qualify.cmd exit 0            (the exit code IS the number of failed stages)

PRESET            CONFIGURE  CLEAN  BUILD  NO-OP REBUILD  CTEST
debug-ext              0       0      0         0           0   3273/3273
release-ext            0       0      0         0           0   3273/3273
debug-shared-ext       0       0      0         0           0   3273/3273

REPEAT (5x each selected test, back to back)
release-ext            0                            436/436   4203.47 s
debug-ext              0                            436/436   4362.84 s

warnings, all three builds   0       (-Werror, 587 objects each)
no-op rebuilds               0 compiles, 0 links -- so the binaries tested
                             are the ones just built
shared build                 9 DLLs linked, including libbettercad_meshing.dll,
                             so the DLL boundary was exercised rather than a
                             static tree re-tested
```

Timeline: 00:40:30 start, debug-ext 01:21:11, release-ext 02:07:19,
debug-shared-ext 02:47:45, repeats 03:57:49 and 05:10:33, finished 05:10:35.

## The qualified tree is the committed tree

`qualify.cmd` records the eight paths at both ends of the run. After the last
test:

```text
apps               5b2059dc47dd2db91e3fc6a1c6b361074c15a0a6   unchanged
include            8783fbab3e0ebc5257ba4ddd27908725e789a2f3   unchanged
src                8eb4087647da0f693124aa7a31094c35f01669db   unchanged
tests              41795dafbfb682f1e85011fad5bd8c6e97f2ccc2   unchanged
examples           9187931fa6824e98d3da6296cdc40af6ca4b8607   unchanged
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7   unchanged
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd   unchanged
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e   unchanged
```

Identical to the freeze, component for component — whole fingerprint
`b9593bbc9276e6dbd809a93945152386837c0061`. Nothing that can reach the
executable or the tests moved during the run.

Only `docs/verification/P16-CMD-001/` was written after the freeze, and it is
outside the fingerprint by construction.
