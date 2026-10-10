# P17-VALID-001 — qualification freeze

## The freeze, and the one that was voided before it

**A qualification was started, run for 30 minutes, and VOIDED.** Recording it
here rather than only the successful run, because the reason is the process
working:

```text
00:09:29   qualification started, candidate c5609fe3
           frozen: include b24dff53 / src c4c336d3 / tests 4471ff35
00:39      a defect was found by re-reading the staged chain:
           `model->quality()` is the MESHER's report, produced under the MESH
           CONTROL's own QualityThresholds -- so refusing on
           `thresholdPolicyError` blocked a structurally sound solve over an
           unrelated error in the user's reporting preferences
           (PRIOR_DRAFT_CORRECTION.md Correction 3)
00:39      the run was stopped and every artefact of it deleted. Source
           changed after the freeze, so the qualification was void -- there
           is no partial credit for a run whose tree no longer exists.
```

CLAUDE.md on both halves: a credible defect found in review is resolved before
`[x]`, and if source or tests change after the freeze the qualification is void
and is run again from clean. The alternative was shipping a known false
rejection to save 30 minutes.

## Pre-freeze checks, all before the expensive one

The order matters and is the project's own lesson: all three of P15's voided
qualifications came from doing a cheap check after an expensive one.

```text
git diff --check                                    clean, exit 0
trailing whitespace / tabs in the five new files    0 lines each
no metric recomputed in production, comments
  stripped                                          0 matches
no repair verb in production, comments stripped     0 matches
no BETTERCAD_*_EXPORT on a constexpr or inline      0 matches
no out-of-line member of a non-exported struct      0 matches
run-qualification.cmd: every line begins with an
  allowed keyword                                   clean
run-qualification.cmd: no leaked heredoc
  terminator                                        0 matches
the 32 tests, 5x each, debug-ext                    100%, exit 0
the 38 tests + compile-fail, DEBUG-SHARED-EXT       38/38, 100%
```

The shared build is the one that cannot be deferred: it is the only preset that
catches a DLL-boundary export defect, and that defect has recurred in three
BetterCAD modules. This header was written against it —
`StructuralValidationReport::status()` is defined INLINE rather than
out-of-line, and all four `inline constexpr` constants carry no export macro —
and `debug-shared-ext` confirmed it before the freeze rather than after.

## One pre-freeze failure that was NOT a defect

```text
cli.new.unicode-path     FAILED in a full-suite run launched from the Bash tool
                         PASSED at codepage 65001 (PowerShell / cmd.exe)
```

The saved document was correct (`"name": "Plåt ✓"` in the JSON); only stdout
was transcoded, because Git Bash's console codepage is not UTF-8 and the test
matches the CLI's stdout against a regex containing non-ASCII characters.

Established as environmental rather than assumed: the test is recorded as
`Passed` in P17-REACTION-001's own `ctest-debug-ext.log`, this milestone cannot
reach the CLI, and it passes under the shell `qualify.cmd` actually uses. The
qualification runs through `qualify.cmd` under cmd.exe, so it is unaffected.

## The qualified tree

```text
candidate   c5609fe31615f2334d291af8df55d29b6cc0933a (HEAD at freeze)
started     Sun 2026-10-11 01:56:08

the eight component hashes, recorded by qualify.cmd BEFORE the first build:

  apps              b49722470580d4fe6c2bb2b3594bb3885eee10db   unchanged
  include           34b3ec8d5ef7a2777dfc03c3eb94bfd67bac1651   MOVED
  src               b6cddc446c2762db5b9a392cad23a7a23c77f48a   MOVED
  tests             81b34bee7723462ee24305f2bb13acc392441e96   MOVED
  examples          75ce1c6852e03011f350d6806f4e8d5f3207d63e   unchanged
  cmake             5a382115d7ed7775189d40b55e40f318eaae6cc7   unchanged
  CMakeLists.txt    3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd   unchanged
  CMakePresets.json 3229f0f89b236744d7d5c082bed47c28b718288e   unchanged
```

EXACTLY THREE MOVED, which is the shape this milestone should have: one new
public header, one new source, three new test files and three CMakeLists
registrations. `apps`, `examples`, `cmake` and both top-level files are
byte-identical to P17-REACTION-001 qualified tree, so nothing leaked outside
the module.

## The harness

```text
qualify.cmd           d313a64070718c44fae290ac042fe259d1a03c8b
verify-harness.cmd    11855b8ff891093b021d1e0351761283d0eedd24
```

Byte-identical to the pair P16-SIZE-001 settled on, and to every milestone
since — **twenty-two in a row** counting this one. Neither was edited.

## What the mutation probes ran against

The 28 probes ran before the freeze (2026-10-11 00:39 to 01:50), against:

```text
PROBED    src/structural/StructuralValidation.cpp   3e5acc20ff5936b0ff7dd8f6caed396ed46f7cf6
          include/.../StructuralValidation.hpp      a71542129b07b277fbc42f7c580e3afd0b135703

FROZEN    src/structural/StructuralValidation.cpp   3e5acc20ff5936b0ff7dd8f6caed396ed46f7cf6   IDENTICAL
          include/.../StructuralValidation.hpp      245031486ef2a64bc5c551295eb184cc21b8e23b   two comments
```

The `.cpp` is byte-identical. The header differs by exactly two doc comments,
both of which drifted when Correction 3 made `QualityPolicyUnusable` a warning:

```diff
-/// cannot be a failure in one report and a warning in another. The two
-/// warnings are `ElementOutsideQualifiedEnvelope` and `NoLoadsApplied`;
-/// everything else refuses.
+/// cannot be a failure in one report and a warning in another. The THREE
+/// warnings are `ElementOutsideQualifiedEnvelope`, `NoLoadsApplied` and
+/// `QualityPolicyUnusable`; everything else refuses.

-/// `MeshStructurallyInvalid`, `MeshHasInvalidElements` and
-/// `QualityPolicyUnusable` reachable: a held mesh can never be structurally
-/// invalid, because `Mesher::generate` refuses one, so those three refusals
-/// would otherwise be untestable.
+/// `MeshStructurallyInvalid` and `MeshHasInvalidElements` reachable: a HELD
+/// mesh can never be structurally invalid, because `Mesher::generate` refuses
+/// one, so those two refusals would otherwise be untestable.
+///
+/// `QualityPolicyUnusable` is different and is reachable from an ordinary
+/// document: a `MeshControl` carries its own `QualityThresholds` and the
+/// mesher evaluates the held report under them. It is a WARNING for that
+/// reason.
```

Neither can affect the executable or a test, and the probe results stand: a
mutation probe measures behaviour, and no behaviour changed between the probed
tree and the frozen one. The alternative — re-running 28 probes for two comment
corrections — would have bought nothing and left the comments stale in the
meantime.

Both counts were wrong in the same direction, which is the hazard worth naming:
**a fix that changes a classification leaves every prose count of that
classification stale**, and a count in a comment is not covered by any test.
They were found by grepping the new files for number words after the fix, which
is now part of this milestone's own checklist.

## The three-preset run

```text
started    Sun 2026-10-11 01:56:08
finished   Sun 2026-10-11 05:12:31        3h 16m
verdict    0 stage(s) failed
           "Qualification passed: every stage exited 0."
           qualify.cmd exit 0
```

| stage | clean build | no-op rebuild | ctest |
| --- | --- | --- | --- |
| debug-ext | 01:56:21 → 02:24:13 (27m52s) | 0 objects | **3759/3759**, 02:49:31 |
| release-ext | 02:49:40 → 03:23:38 (33m58s) | 0 objects | **3759/3759**, 03:47:55 |
| debug-shared-ext | 03:48:03 → 04:14:57 (26m54s) | 0 objects | **3759/3759**, 04:40:05 |

```text
640 objects compiled per preset, 0 WARNINGS in all three build logs
   (grep -ci warning: build-debug-ext.log 0, build-release-ext.log 0,
    build-debug-shared-ext.log 0)

the no-op rebuild compiled 0 objects in every preset, which is what proves
the binaries ctest ran are the ones that build just produced

run-qualification.err is 270 lines and ALL 270 are git's
"CRLF will be replaced by LF" notices from the `git ls-files | git
hash-object` tree-hashing step. Nothing else is in it. Pre-existing and
present in P17-REACTION-001's run for the same reason.
```

### The repeat runs

```text
repeat release-ext   836 tests selected, x5, exit 0   04:40:06 → 04:55:37
repeat debug-ext     836 tests selected, x5, exit 0   04:55:37 → 05:12:29
```

**836, AND THE NUMBER IS CHECKED RATHER THAN READ.** P17-REACTION-001's filter
selected 748. This milestone added `unit\.Quality` to the set, and nothing
else:

```text
748   P17-REACTION-001's selection
 56   unit\.Quality -- P16-QUALITY's own tests, counted independently as 56
      TEST_CASEs across seven prefixes, all beginning with Quality
 32   this milestone's, which fall under unit\.Structural
----
836   matches exactly
```

That arithmetic is the check P17-DOF-001's lesson demands: a filter that
*looks* right selected 17 of 31 tests there. Here the two ends agree.

`unit\.Quality` is the largest single debt this milestone owes. The policy is a
`QualityThresholds` value consumed by `evaluateMeshQuality`, classified by
P16's own `direction(metric)`, and read out of `MeshQualityReport::summaries`
— so a change to any metric, to a summary extremum, to the strictness of a
comparison or to `validate()` moves every threshold here.

## The qualified tree IS the committed tree

```text
                   before the first build                 after the last test run
apps               b49722470580d4fe6c2bb2b3594bb3885eee10db   IDENTICAL
include            34b3ec8d5ef7a2777dfc03c3eb94bfd67bac1651   IDENTICAL
src                b6cddc446c2762db5b9a392cad23a7a23c77f48a   IDENTICAL
tests              81b34bee7723462ee24305f2bb13acc392441e96   IDENTICAL
examples           75ce1c6852e03011f350d6806f4e8d5f3207d63e   IDENTICAL
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7   IDENTICAL
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd   IDENTICAL
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e   IDENTICAL
```

All eight recomputed by `qualify.cmd` after the last test run and identical to
the eight recorded before the first build. Nothing moved during the run, so the
tree that was qualified is the tree being committed.

## RESULT

```text
TASK:            P17-VALID-001 -- Structural Validation / Acceptance
IMPLEMENTATION:  one new public header, one new source, three new test files,
                 three CMakeLists registrations. No predecessor production
                 file modified; zero meshing source files changed.
TESTS:           3759/3759 in debug-ext, release-ext and debug-shared-ext.
                 836 tests x5 in two presets. 38 of them are this
                 milestone's: 27 unit, 5 reference, 6 compile-failure.
VALIDATION:      two accuracy laws measured through the production kernel
                 against a hand-written analytic oracle, over sixteen and
                 seven orders of magnitude, two displacement fields each;
                 both derived thresholds verified on BOTH sides; the quality
                 distributions reproduce P16-QUALITY-001's own published
                 figures; 28 mutation probes with 27 killed.
RESULT:          PASS
EVIDENCE:        docs/verification/P17-VALID-001/
TODO:            updated on PASS
```
