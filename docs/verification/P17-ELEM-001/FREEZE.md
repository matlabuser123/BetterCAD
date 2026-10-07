# P17-ELEM-001 — qualification freeze

```text
FROZEN:   2026-10-07, before the first configure of the qualification run
```

## Baseline

```text
HEAD at start      28b0d5aa7215ee6c16100bccdb9504a220af5500
origin/main        28b0d5aa7215ee6c16100bccdb9504a220af5500
HEAD^{tree}        bd2437059dd9d954d85ea1285b1066d5bbf48fe0
working tree       clean, 0 porcelain lines
git log -1         28b0d5a BetterCAD: add the structural DOF numbering and
                   constraint model
```

Predecessors, verified **on this tree** rather than taken from their own
records. The eight source paths still fingerprinted P17-DOF-001's qualified
component list before any file was added — `include 9819575f`, `src 30569b8b`,
`tests 6e13b938`, `apps b4972247`, `examples 75ce1c68`, `cmake 5a382115`,
`CMakeLists.txt 3ec3c10f`, `CMakePresets.json 3229f0f8` — so:

```text
P17-ARCH-001   PASS, 20/20      qualified at 536e14b5
P17-DATA-001   PASS, 19/19      qualified at fcf2ea16
P17-MAT-001    PASS, 14/14      qualified at 4d4698b3
P17-DOF-001    PASS, 12/12      qualified at 9301b643   <- and still the tree
P16 — Meshing  QUALIFIED        P16-QUAL-001, 2026-10-06
```

The checkbox counts were derived with an `awk` pass that terminates each
section at the next `^# ` heading. A first attempt used a `sed` range ending at
`### Gate` and over-counted two milestones — P17-DATA-001 has no `### Gate`
heading, so the range ran on into the next section and reported 45 and 26
instead of 19 and 14. Recorded because it is the
`a-quoted-figure-is-an-unverified-figure` shape again: the zero-open-boxes
result was right either way, but the counts were not, and they were quoted.

## Pre-freeze checks

Run **before** the expensive stage, in the order
`bettercad-pre-freeze-checklist` records — all three of P15's voided
qualifications came from doing a cheap check after an expensive one:

```text
1  git diff --check over source and docs      clean
2  the repeat filter counted FROM INSIDE      797 selected, 29 of them
                                              Tet4Element_
3  the 29 new tests under --repeat 5          29/29 in debug-ext
4  the SHARED build                           debug-shared-ext built, 29/29
```

Check 2 is the one added after P17-DOF-001, where the inherited filter silently
covered 17 of 31 tests. `Tet4Element_*` matches **no** inherited term — not
`unit\.Structural`, not `unit\.Mesh` — so `unit\.Tet4Element` was added, and
the count was taken by grepping the milestone's own tests inside the selection
rather than by reading the filter. The new `unit\.Tet` term additionally pulls
in P16's tetrahedron-validation suite, which is why the total is 797 rather
than 794.

Check 4 and the export-macro grep are the pair that catches the defect which
has now recurred in three modules. The grep was clean:

```text
grep BETTERCAD_STRUCTURAL_EXPORT Tet4Element.hpp | grep -E 'constexpr|inline'
  -> 0 occurrences
```

`InverseLength`, `Stiffness`, `kTet4Nodes`, `kTet4Dofs`, `kVoigtComponents` and
`localDofIndex` are all header-defined and carry **no** macro; the three
classes and the seven free functions have out-of-line definitions and carry it.

Also confirmed static, before the freeze:

```text
#include <Eigen in production                 0   (4 mentions, all comments)
abs in src/structural/Tet4Element.cpp         0   (2 mentions, both comments)
0.5 * ( symmetrisation                        0   (1 mention, a comment)
static storage / mutable / thread_local       0
quality metrics (aspect, dihedral, radius)    0
```

## The frozen tree

```text
apps               b49722470580d4fe6c2bb2b3594bb3885eee10db   unchanged
include            10bf6abc0c4a5e27ae2dc362a3b0be4a98601e0c   MOVED
src                f6ddc1dbe2d4edad23cca52a8174a34be0d64d2c   MOVED
tests              61b2ef80697f91ed62770999fb7d7c6f3b279399   MOVED
examples           75ce1c6852e03011f350d6806f4e8d5f3207d63e   unchanged
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7   unchanged
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd   unchanged
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e   unchanged

WHOLE              845b118ce6edaa9286ca5731dc43a04b6842a757
```

Exactly three paths moved, and they are the three the milestone touches. Five
of the eight are byte-identical to P17-DOF-001's, which is the useful check: a
milestone that had quietly edited `cmake/` or `CMakePresets.json` would show it
here.

Working tree at the freeze:

```text
 M TODO.md                                    (the authorization)
 M src/structural/CMakeLists.txt              (+1 source)
 M tests/CMakeLists.txt                       (+2 sources)
?? include/bettercad/structural/Tet4Element.hpp
?? src/structural/Tet4Element.cpp
?? tests/structural/Tet4ElementTests.cpp
?? tests/reference/Tet4ElementReferenceTests.cpp
?? docs/verification/P17-ELEM-001/
```

## The mutation harness left nothing behind

Thirteen probes edited the two production files and restored them. Verified by
hash rather than by trusting the script, and then by running the suite again
against a **rebuilt** binary:

```text
live     src/structural/Tet4Element.cpp               7afa15dcb413
pristine                                              7afa15dcb413
live     include/bettercad/structural/Tet4Element.hpp f293ac1b3865
pristine                                              f293ac1b3865
restore build ok
post-restore                                          100% tests passed out of 29
```

The restore build and the post-restore run are there because of the lesson
`a-restored-source-is-not-a-restored-binary` records: restoring a source does
not relink a test executable, and a stale mutant binary produces a failure that
looks exactly like a real defect.

## What may change after the freeze

`docs/verification/P17-ELEM-001/`, `TODO.md`, `ROADMAP.md` and `README.md`.
None is inside the fingerprint; none is configured, compiled, linked or read by
a test. Anything under the eight paths voids the qualification and it is run
again from clean.

`docs/verification/P17-ELEM-001/qualification/run-qualification.cmd` is **not**
edited once the run has started, not even a comment — see its own header for
the four-hour qualification that was lost to a reworded comment.

## Harness provenance

```text
qualify.cmd          d313a64070718c44fae290ac042fe259d1a03c8b
verify-harness.cmd   11855b8ff891093b021d1e0351761283d0eedd24
```

Both byte-identical to P17-DOF-001's, and `qualify.cmd` unchanged since
**P16-SIZE-001** — fifteen milestones in a row counting this one. Hashed with
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
