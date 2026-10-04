# P16-VIZ-001 — the qualification freeze

```text
SUBJECT:  the identity of the tree that was qualified, recorded BEFORE the
          qualification started
DATE:     2026-10-04
FROZEN:   14:47 local
```

**The tree that is qualified must be the tree that is committed.** This file
exists so that claim can be checked rather than believed: the fingerprint below
was taken before the first preset was configured, and `qualify.cmd` records the
same eight tree IDs again after the last test run.

## The fingerprint

Eight paths, as `qualify.cmd`'s `:trees` computes them — from a scratch git
index, so a dirty working tree is captured exactly as it stands rather than as
`HEAD` has it.

```text
apps               5b2059dc47dd2db91e3fc6a1c6b361074c15a0a6
include            a16629454910ea4cd2d120bd0f0087690a2d9836
src                3d7a140bdbcf906d28eeace4bf5ed43f825242c8
tests              1806573a4638121d8c32bf125ad6a305933249bc
examples           9187931fa6824e98d3da6296cdc40af6ca4b8607
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e

whole fingerprint  6103d53414a0ccdfa87d7edf0c6f62d921e1b535
frozen from        d9c8d53  (HEAD, INFRA-VIEWER-001)
```

`examples`, `cmake`, `CMakeLists.txt` and `CMakePresets.json` are unchanged
from `INFRA-VIEWER-001`'s freeze, which is the expected shape: this milestone
touches `apps`, `include`, `src` and `tests` and nothing else. `docs/` and
`deps/` are outside the fingerprint, so this evidence directory may still be
written to after the freeze; nothing under the eight paths above may move, and
if it does the qualification is void and is run again from clean.

## The gates cleared BEFORE the freeze

In this order, cheapest first. **The ordering is the point:** three of `P15`'s
qualifications were lost to running a cheap check after an expensive one.

```text
git diff --check, trailing whitespace, tabs           clean
19 mutations, one uninterrupted run                   19 killed by a TEST,
                                                      0 compiler-only,
                                                      0 survived
determinism: 150 tests x5, debug-ext                  100% passed, 291 s
debug-shared-ext configure + build + targeted run     0 warnings, 150/150,
                                                      58 s
full debug-ext suite                                  100% passed out of
                                                      3219, 789 s
```

The shared build was checked for substance rather than exit code: its log shows
33 compile and link actions including every new translation unit and a relink
of `libbettercad_renderer.dll`, so the DLL-boundary gate ran against freshly
built shared libraries rather than a stale tree.

## What was deliberately NOT done before the freeze

**Eight lines exceed `.clang-format`'s `ColumnLimit: 100` and were left
alone.** No test enforces the limit and 474 committed files already contain
6185 lines over it, including files inside qualified milestones. Reflowing
eight lines would be unrelated cleanup, and because the mutation patterns are
textual it would have voided a mutation run that had just finished — a third
run for no gain in correctness or consistency. Recorded in
[ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md) as a decision rather than left
as an oversight.

## The mutation run, and why there were two

The first run found one real gap and two faults in the mutations themselves.
Both runs are kept: `results-first-run.txt` beside `results.txt`, because what
the first found is the reason three of the mutations and two of the tests look
the way they do.

```text
first run    15 killed by a test, 1 compiler-only, 3 SURVIVED
final run    19 killed by a test, 0 compiler-only, 0 survived
```

The qualifying run was made once, uninterrupted, against the tree fingerprinted
above.

## Revision

First issue, 2026-10-04.
