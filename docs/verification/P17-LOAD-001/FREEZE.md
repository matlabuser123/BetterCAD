# P17-LOAD-001 — qualification freeze

```text
FROZEN:   2026-10-08, before the first configure of the qualification run
```

## Baseline

```text
HEAD at start      dd2aca6a74d3042e3ba4ab692f6d052cc5ec387c
origin/main        dd2aca6a74d3042e3ba4ab692f6d052cc5ec387c
HEAD^{tree}        fc0cfe15013bc0a64ac85bb3d6ab4f9bf30cc602
working tree       clean, 0 porcelain lines
git log -1         dd2aca6 BetterCAD: add linear elastic Tet4 element
```

Predecessors, verified **on this tree** rather than taken from their own
records. The eight source paths still fingerprinted P17-ELEM-001's qualified
component list before any file was added — `include 10bf6abc`, `src f6ddc1db`,
`tests 61b2ef80`, and the other five unchanged — so:

```text
P17-ARCH-001   PASS, 20/20      qualified at 536e14b5
P17-DATA-001   PASS, 19/19      qualified at fcf2ea16
P17-MAT-001    PASS, 14/14      qualified at 4d4698b3
P17-DOF-001    PASS, 12/12      qualified at 9301b643
P17-ELEM-001   PASS, 18/18      qualified at 845b118c   <- and still the tree
P16 — Meshing  QUALIFIED        P16-QUAL-001, 2026-10-06
```

Counts derived with an `awk` pass terminating each section at the next `^# `
heading, for the reason P17-ELEM-001's freeze recorded: a `sed` range ending at
`### Gate` over-counts the sections that have no such heading.

## Pre-freeze checks

Run **before** the expensive stage, in the order
`bettercad-pre-freeze-checklist` records:

```text
1  git diff --check over source and docs      clean
2  the repeat filter counted FROM INSIDE      820 selected, 23 of them
                                              StructuralLoad_, 29 Tet4Element_
3  the 23 new tests under --repeat 5          23/23 in debug-ext, 281 s
4  the SHARED build                           debug-shared-ext built, 23/23
```

Check 2 found that no new filter term was needed — `unit\.Structural` already
matches `StructuralLoad_*` — but that was **verified by counting the
milestone's own tests inside the selection**, not inferred from reading the
pattern. The same check is what caught a 14-test gap in P17-DOF-001 and an
unmatched prefix in P17-ELEM-001.

Also confirmed static, before the freeze:

```text
export macro on a constexpr/inline entity, both new headers    0
BETTERCAD_CORE_EXPORT in core/math/Vector.hpp                  0  (every
                                                               addition is
                                                               inline or
                                                               constexpr, so a
                                                               macro there
                                                               would be the
                                                               recorded
                                                               dllimport trap)
tolerance / nearest / distance / centroid in the load module   0
ElementId outside comments in the canonical schema             0
NodeId outside comments in the canonical schema                1, and it is
                                                               NodalForceLoad::node,
                                                               the explicitly
                                                               mesh-local load
```

## The frozen tree

```text
apps               b49722470580d4fe6c2bb2b3594bb3885eee10db   unchanged
include            38b76d284081e0dc9d4a910db3d23196206a7186   MOVED
src                4d5eb402931aafc4351e51386d6a0a4602d72842   MOVED
tests              25903e4f6439599e67fc5e3d1df52c93ac802dc1   MOVED
examples           75ce1c6852e03011f350d6806f4e8d5f3207d63e   unchanged
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7   unchanged
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd   unchanged
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e   unchanged

WHOLE              27afe8d34f444b303940dc1ad9b3c51d8476f731
```

Three paths moved. Five are byte-identical to P17-ELEM-001's, which is the
check a milestone that had quietly edited `cmake/` or `CMakePresets.json` would
fail.

Working tree at the freeze — and this milestone modifies **more existing files
than the previous two**, which is why they are listed individually:

```text
 M TODO.md                                             the authorization
 M include/bettercad/core/math/Vector.hpp              Traction3D, Moment3D,
                                                       momentOf
 M include/bettercad/core/units/Units.hpp              the Torque alias
 M include/bettercad/structural/StructuralAnalysisObject.hpp
                                                       the loads collection, in
                                                       the place P17-DATA-001
                                                       left for it
 M src/structural/CMakeLists.txt                       +1 source
 M tests/CMakeLists.txt                                +2 sources
 M tests/structural/StructuralDataTests.cpp            the two assertions
                                                       P17-MAT-001 predicted
                                                       this milestone would
                                                       break
?? include/bettercad/structural/StructuralLoad.hpp
?? include/bettercad/structural/StructuralLoadVector.hpp
?? src/structural/StructuralLoad.cpp
?? tests/structural/StructuralLoadTests.cpp
?? tests/reference/StructuralLoadReferenceTests.cpp
?? docs/verification/P17-LOAD-001/
```

**Two of those edits are the reason the blast radius is wider than the last two
milestones'.** `core/math/Vector.hpp` and `core/units/Units.hpp` are included,
directly or transitively, by almost everything, so the repeat set is the broad
one and the driver's own comment says why rather than leaving a reader to infer
it. P17-DATA-001 was in the same position when it added `Force3D`.

## The mutation harness left nothing behind

Thirteen probes edited the two load files and restored them. Verified by hash
rather than by trusting the script, then by re-running the suite against a
**rebuilt** binary:

```text
live     src/structural/StructuralLoad.cpp                   e4e96124c2be
pristine                                                      e4e96124c2be
live     include/bettercad/structural/StructuralLoadVector.hpp 50fd53e27440
pristine                                                      50fd53e27440
restore build ok
post-restore                                                  100% tests passed out of 23
```

The restore build and the post-restore run are there because of
`a-restored-source-is-not-a-restored-binary`: restoring a source does not
relink a test executable, and a stale mutant binary produces a failure that
looks exactly like a real defect.

## What may change after the freeze

`docs/verification/P17-LOAD-001/`, `TODO.md`, `ROADMAP.md` and `README.md`.
None is inside the fingerprint; none is configured, compiled, linked or read by
a test. Anything under the eight paths voids the qualification and it is run
again from clean.

`docs/verification/P17-LOAD-001/qualification/run-qualification.cmd` is **not**
edited once the run has started, not even a comment — see its own header for
the four-hour qualification that was lost to a reworded comment.

## Harness provenance

```text
qualify.cmd          d313a64070718c44fae290ac042fe259d1a03c8b
verify-harness.cmd   11855b8ff891093b021d1e0351761283d0eedd24
```

Both byte-identical to P17-ELEM-001's, and `qualify.cmd` unchanged since
**P16-SIZE-001** — sixteen milestones in a row counting this one. Hashed with
`git hash-object` against the previous milestone's copy rather than assumed;
the claim "the harness is unchanged" was wrong once, in P16-QUAL-001, and is
checked rather than carried.

`verify-harness.cmd` was run before launching:

```text
PASS: a failed stage gave qualify.cmd exit 2, and a zero-match
      repeat filter was counted as a failed stage.
```
