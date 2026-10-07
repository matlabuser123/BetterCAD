# P17-DOF-001 — qualification freeze

```text
FROZEN:   2026-10-07, before the first configure of the qualification run
```

## Baseline

```text
HEAD at start      9e65b5117a6d2dc429df5890a8bdf5888b9556e9
origin/main        9e65b5117a6d2dc429df5890a8bdf5888b9556e9
working tree       clean, 0 porcelain lines
```

Predecessors, verified **on this tree** rather than taken from their own
records: the eight source paths still fingerprinted P17-MAT-001's qualified
component list (`include 9cb0ee3b`, `src 7e262539`, `tests 1140337f`,
`apps b4972247`, `examples 75ce1c68`, `cmake 5a382115`,
`CMakeLists.txt 3ec3c10f`, `CMakePresets.json 3229f0f8`) before any file was
added. So:

```text
P17-ARCH-001   PASS, 20/20      qualified at 536e14b5
P17-DATA-001   PASS, 19/19      qualified at fcf2ea16
P17-MAT-001    PASS, 14/14      qualified at 4d4698b3   <- and still the tree
P16 — Meshing  QUALIFIED        P16-QUAL-001, 2026-10-06
```

## Pre-freeze checks

Run **before** the expensive stage, in the order
`bettercad-pre-freeze-checklist` records — all three of P15's voided
qualifications came from doing a cheap check after an expensive one:

```text
git diff --check                        clean, no whitespace errors
the 31 new tests under --repeat 5       31/31 in debug-ext
the SHARED build                        debug-shared-ext built and 31/31 ran
export macro on a constexpr/inline      0 occurrences in the new header
```

The shared build and the export-macro grep are the pair that catches the defect
which has now recurred in three modules; both were done before the freeze
rather than discovered by the run.

One correction was made in this window and is recorded because it would have
produced a true-looking claim: the repeat filter inherited from P17-MAT-001
covered only 17 of the 31 new tests. Three terms were added and the selection
re-counted from inside. See `DETERMINISM.md`.

## The frozen tree

```text
apps               b49722470580d4fe6c2bb2b3594bb3885eee10db   unchanged
include            9819575fa997ba59acdfee42542dac960938bd40   MOVED
src                30569b8bfb94824eecb72605d3b4f08d93f2c4d3   MOVED
tests              6e13b938ba8b513a12a373f6a1c0eae81d590f45   MOVED
examples           75ce1c6852e03011f350d6806f4e8d5f3207d63e   unchanged
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7   unchanged
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd   unchanged
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e   unchanged

WHOLE              9301b6431f9802c2f6927dba18f2e192a568a890
```

Exactly three paths moved, and they are the three the milestone touches. Four
of the eight are byte-identical to P17-MAT-001's, which is the useful check: a
milestone that had quietly edited `cmake/` or `CMakePresets.json` would show it
here.

The whole value is a function of the eight paths **plus the base tree HEAD
pointed at**, so it moves when a documentation commit moves HEAD. The invariant
that survives is the component list.

## What may change after the freeze

`docs/verification/P17-DOF-001/`, `TODO.md`, `ROADMAP.md` and `README.md`.
None is inside the fingerprint; none is configured, compiled, linked or read by
a test. Anything under the eight paths voids the qualification and it is run
again from clean.

`docs/verification/P17-DOF-001/qualification/run-qualification.cmd` is **not**
edited once the run has started, not even a comment — see its own header for
the four-hour qualification that was lost to a reworded comment.

## Harness provenance

```text
qualify.cmd          d313a64070718c44fae290ac042fe259d1a03c8b
verify-harness.cmd   11855b8ff891093b021d1e0351761283d0eedd24
```

Both byte-identical to P17-MAT-001's, and `qualify.cmd` unchanged since
**P16-SIZE-001** — fourteen milestones in a row counting this one. Hashed with
`git hash-object` against the previous milestone's copy rather than assumed;
the claim "the harness is unchanged" was wrong once, in P16-QUAL-001, and is
checked rather than carried.

`verify-harness.cmd` was run before launching:

```text
PASS: a failed stage gave qualify.cmd exit 2, and a zero-match
      repeat filter was counted as a failed stage.
```

That is the regression which matters here, because the whole point of a
qualification is that a failure reaches the verdict instead of sitting in a
log.
