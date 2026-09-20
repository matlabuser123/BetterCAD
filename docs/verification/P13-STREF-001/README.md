# P13-STREF-001 — Stable Assembly References

```text
STATUS:          PASS
BASELINE:        42a581b (P13-STREF-001 authorization), clean,
                 HEAD == origin/main
SCOPE:           that an assembly reference survives the model changing under
                 it, or is honestly broken. No assembly regeneration
                 pipeline, no commands, no external-reference implementation
                 beyond the contract P13-REF-001 already built.
IMPLEMENTATION:  one shared mate-target resolution, and the mate counterpart
                 of unresolvedComponents(). The solver got smaller.
TESTS:           15 new Catch2 cases in 1 file (530 lines)
EVIDENCE:        this directory
```

## Scope

**The failure this milestone exists to prevent is a mate that still solves, on
the wrong face.** Nothing reports that one: the solve succeeds, the assembly
looks plausible, and the parts are in the wrong places. Every other kind of
reference failure announces itself.

That shapes the whole verification. An assertion that a reference "still
resolves" is worth almost nothing here, because the wrong-face failure
resolves perfectly well. Every case below asserts **which geometry** it
resolved to.

## Reference model

Three identity schemes, each already built, each doing a different job:

| What | Identity | Built by |
| --- | --- | --- |
| a component's part | `ObjectReference`: an `ObjectId`, plus an optional `DocumentId` for an external one | `P13-REF-001` |
| a mate's component | `ComponentId` — the instance, not the part | `P13-COMP-001` |
| a mate's geometry | `PlaneReference`, `AxisReference`, or `FaceName` = the feature plus the **role** the face plays there | `P12-STREF-001`, `P13-MATE-001` |

None of them is a position in an array, and none is recoverable from one.
`ObjectReference`'s own header states the rule this milestone rests on: the
canonical identity is the document and the object, the `hint` is a locator and
never identity, and "a hint leading to a document with a different UUID is a
failed reference, not a match".

### What this milestone added

Almost nothing, because almost all of it existed. Two things:

**`resolveMateTarget()`** — the geometry a mate target names, in its part's own
space. The solver had this logic inline; it now calls the shared function, so
`SolverSystem.cpp` got **28 lines shorter and 11 longer**.

**`unresolvedMateTargets()`** — the mate counterpart of the
`unresolvedComponents()` that `P13-REF-001` already provided for parts. A
document could report a broken *part* reference and be opened anyway; a broken
*mate target* had no such report, and the only way to discover one was to
attempt a solve and have the whole thing fail.

**The two share one resolution path, deliberately.** The question "does this
target resolve" is answered by the same code that answers "to what", so a
report and a solve cannot disagree — and one of the cases below is there to
prove that they don't.

## Component references

| Claim | Measured |
| --- | --- |
| two components of one part stay distinguishable | same `FaceName`, different `ComponentId`; one placed at the origin, one at 60 mm |
| identity is the component, not its position | two documents built with the components added in **opposite orders** give each reference the same meaning, and the IDs differ between them |
| a part reference survives save/load | the component's `ObjectReference` compares equal, and resolves to the same part |

The ordering case is the one worth stating plainly: the two documents allocate
different IDs to "First" because it was created second in one of them. The
references still mean what they name.

## Mate-target references

Only the kinds ADR-004 permits — plane, axis, face — and no expansion into
semantic topology.

The oracle for every geometric assertion, derived by hand and **not** from
BetterCAD: a 40 × 30 rectangle on the XY plane extruded to depth `d` has its
end cap at `z = d`, with the normal facing out of the material, `+Z`.
Established against two depths before anything leans on it.

## Topology-identity rules

**A face is named by the feature that makes it and the role it plays there.**
Not `Face3`, not an index, not a traversal position — and this is enforced by
the type, not by a check: a `MateTarget` holds a `PlaneReference`, an
`AxisReference` or a `FaceName`, and has no field a `geometry::FaceSignature`
could occupy. `MateReference.hpp` records why a signature is refused — it
matches a plane in model space, not a named face.

The consequence is what the regeneration case measures: when the extrude's
depth changes from 10 mm to 25 mm, the topology underneath is rebuilt and the
reference **follows the role**, resolving to `z = 0.025`. A reference keyed to
a topology position could not have done that, and one keyed to the old plane
would have been left behind.

## Save/load

| Claim | Measured |
| --- | --- |
| the mate definition | compares **equal** to the original, in full |
| the named feature and component | still the same IDs |
| what it resolves to after loading | `z = 0.010`, the hand-derived value |
| an **unresolved** reference | persists as unresolved — not dropped on save, not repaired on load |
| and then recovers | resolves again to `z = 0.010` when the part returns |

The third row is the one byte-comparison would not give: the file matching
proves the bytes, not that the reference still means the same geometry in the
loaded document.

## Configuration switching

The case `P13-CONF-001` made possible, and the one where silent retargeting
would be easiest.

```text
base     -> the target's component is in the build, resolves to z = 0.010
Lean     -> its component is suppressed
base     -> and it resolves to exactly the geometry it did before
```

| Claim | Measured |
| --- | --- |
| while its component is out of the build | the mate is **inactive**, and `unresolvedMateTargets()` is **empty** |
| the reference itself | unchanged — full definition equality, across the switch and back |
| does it retarget to the component still present? | no — it still names the suppressed one, asserted against the other component's ID |
| after switching back | resolves to the **same** geometry, compared exactly |

**Inactive is not unresolved**, and conflating them is the mistake this
section exists to rule out: a report that treated a suppressed component's
mate as broken would tell an engineer their assembly was damaged every time
they switched to a leaner build. A separate case checks both directions — the
suppressed component is silent, a genuinely missing feature is reported in
either build.

## Regeneration

Only the currently qualified regeneration behaviour; `P13-REGEN-001` is not
implemented here. What is in scope is regeneration of the **part beneath a
component**, which exists today.

| Change | Should the reference survive? | Measured |
| --- | --- | --- |
| the extrude's depth changes 10 mm → 25 mm | **yes** — it is the same face, in the same role | resolves to `z = 0.025`, reference unchanged, nothing unresolved |
| the feature is removed | **no** — the intended target is gone | reported unresolved, both sides, with `NotFound` |
| the feature returns under its own ID | **yes** | resolves again to `z = 0.010` |

## Unresolved state

`unresolvedMateTargets()` reports the mate, which of its up to four targets,
and why:

```text
{mate, side, reason}      side ∈ {a, b, a2, b2}      reason = ErrorCode
```

Four sides because a slider carries a roll reference as well as its axis
(`P13-MATE-002`). Measured on a `Coincident` whose feature was removed: two
entries, sides `a` and `b`, both `NotFound`.

## Recovery

```text
resolves -> target removed -> unresolved -> target returns -> resolves again
```

The claim that matters through that sequence is the one about the reference
itself: **the canonical identity never changed.** The mate definition is
captured before the removal and compared for full equality while broken and
again after recovery. A reference that had quietly re-pointed at something, or
been cleared when it broke, would fail that comparison rather than the
resolution.

Measured twice: in memory, and across a save and load with the target missing
in the file.

## No silent rebinding

The adversarial case the whole model exists to defeat, and it is built to be
as tempting as possible:

1. A mate names the end cap of `Block`, resolving to `z = 0.010`.
2. A second part, `Other`, is created with **identical dimensions**, so its end
   cap is at the same plane — `z = 0.010`, measured, to confirm the two really
   are geometrically indistinguishable.
3. `Block` is removed.

A reference keyed to geometry, to a plane, to a name, or to "the nearest
matching face" would attach to `Other`. Measured: the mate is reported
unresolved, and still names the feature that is gone — asserted both as
`== Block` and `!= Other`.

Two further cases that `P13-REF-001` already established and that still hold:
identity does not follow a display name, and a locator change does not change
identity.

## Cross-document rules

```text
Document A object 42     and     Document B object 42     are different objects
```

Measured: a reference carrying a foreign `DocumentId` and the object number of
a local part fails with `NotFound` — while **the local object of that very
number is present and resolvable**, which is what makes the negative result
mean something rather than being a failure for any reason at all.

Also measured: `sameTarget()` ignores the locator, so the same reference with
a different `hint` is the same reference; and a foreign reference is not the
same target as the local one of equal number.

This is validation of the **contract**. `TODO.md`'s accepted P13 constraints
state that assemblies operate inside one `Document` and cross-document
dependencies are not implemented, so there is no end-to-end external reference
here and none was built.

## Determinism

| Claim | Measured |
| --- | --- |
| resolving the same target repeatedly | **exactly equal** over 8 passes — `MateTargetGeometry` compares by value, so this is bitwise agreement, not agreement to a tolerance |
| re-regenerating | resolves to the same geometry again |
| the unresolved report | empty on every pass |
| Debug / Release / Debug-shared | the same tests, asserting the same hand-derived values |

## Failure atomicity

| Operation | Expected | Measured |
| --- | --- | --- |
| a target on a component that is not there | structured failure | `NotFound` |
| a face of a feature that is not there | structured failure | fails |
| a face target with **no bodies** to resolve against | failure, not a guess | fails |
| after all three | nothing written | mate definition unchanged, document revision unchanged |
| and then | a valid resolution still works | resolves to `z = 0.010` |

Resolution is a read: it has nothing to write, and the revision check proves
it wrote nothing.

## Independent validation

Every expected value is arithmetic on the geometry, never a second call into
the code under test.

| Case | Expected | Measured | Result |
| --- | --- | --- | --- |
| end cap of a 10 mm block | `z = 0.010`, `+Z` | as expected | PASS |
| end cap of a 25 mm block | `z = 0.025`, `+Z` | as expected | PASS |
| same reference after depth 10 → 25 | `z = 0.025` | as expected | PASS |
| same reference after save/load | `z = 0.010` | as expected | PASS |
| same reference after a configuration round trip | **exactly** the earlier value | equal | PASS |
| deleted target | unresolved, `NotFound`, sides a and b | as expected | PASS |
| replacement part with an identical end cap | **must not rebind** | still names the removed feature | PASS |
| foreign `DocumentId` with a local object's number | **must not bind** | `NotFound`, while the local object resolves | PASS |
| target returns under its own ID | resolves, identity unchanged | as expected | PASS |

## Adversarial review

**No production defect.** One finding, and it is about the tests rather than
the code — but it is the finding that matters most in this milestone, so it is
recorded first.

### Finding 1 — the oracle problem, which this milestone is uniquely exposed to

The brief asks: *"Were tests written using the same resolver logic as the
oracle?"* It is the right question, and for reference tests it is nearly fatal
if the answer is yes. A test that asks the resolver where a face is, and then
asserts the resolver still says that, passes just as happily when the
reference has silently attached to the wrong face — because it asks the wrong
thing twice and gets a consistent answer.

**How it was avoided, concretely.** The oracle is arithmetic on the sketch and
the extrude depth: a 40 × 30 rectangle on the XY plane extruded to `d` has its
end cap at `z = d`, normal `+Z`. Literals, derived by hand, checked against two
different depths before anything leant on them. No expected value in this file
came from `resolveMateTarget()`.

Where an assertion *is* comparison-based — "the same geometry after a
configuration round trip" — the earlier value is captured before the change,
and the comparison is exact equality rather than a tolerance. That is
comparing a measurement to a measurement across a state change, which is the
thing under test, not asking the same question twice.

The residual risk is stated rather than hidden: the oracle assumes the end cap
of an upward extrude is its top face. If that convention were wrong, these
tests would agree with each other and be wrong together. It was checked
against two depths precisely so the assumption has to hold at more than one
point, and the depth-change case then measures a *move* from one hand-derived
value to another.

### Cleared

| Question | Answer |
| --- | --- |
| Can `Face3`/`Edge17` leak into canonical identity? | No — and not by check but by type: a `MateTarget` has no field a `geometry::FaceSignature` could occupy, and `FaceName` is a feature plus a role |
| Can traversal-order changes break a valid reference? | No — measured through a depth change that rebuilds the topology; the reference follows the role to the new plane |
| Can a deleted target silently rebind? | No — measured against an identically-dimensioned replacement whose end cap sits at the very plane the original occupied |
| Can a similar target steal an old reference? | No — same case; the mate still names the removed feature, asserted both as equal to it and unequal to the replacement |
| Can configuration switching change reference identity? | No — full definition equality across a switch and back, and it still names the suppressed component rather than the present one |
| Can suppressed components cause stale target reuse? | No — the mate is inactive, not resolved-and-ignored; nothing is resolved for it at all while its component is out of the build |
| Can save/load alter reference identity? | No — full definition equality, and the loaded document resolves to the hand-derived value |
| Can regeneration turn unresolved into a wrong resolved state? | No — measured both directions: a benign change keeps it resolved to the right place, a removed feature leaves it unresolved even with a lookalike present |
| Can the same numeric `ObjectId` in another document misbind? | No — `NotFound`, while the local object of that number is present and resolvable |
| Can path changes alter canonical identity? | No — `sameTarget()` ignores the `hint`, measured |
| Can a resolver cache return stale objects? | There is no cache. `resolveMateTarget()` computes from the document and the current bodies on every call, which is also why resolution is a read with nothing to write |
| Can a reference outlive its source object unsafely? | A reference is a value — IDs and a role, no pointers. What it resolves *to* borrows the caller's bodies, whose lifetime the caller owns, as `BodyLookup` already required |
| Can Debug and Release resolve differently? | The three presets run the same tests against the same hand-derived literals |
| Were any tolerances loosened? | No tolerance was added, changed or removed. The geometric tolerance here is 1e-9, tighter than the solver's assertions, because resolution is arithmetic and not iteration |
| Can the report and the solve disagree? | No, and this is structural rather than tested-into-existence: they call the same function. A case asserts it in both directions anyway — sound model, report empty and solve succeeds; broken model, report non-empty and solve fails |

### Finding 2 — a test-only API misuse, fixed

Two recovery cases failed on their first run at
`Document::restoreObject()`. That is the file-loading path and requires an
object whose ID is still unset; an object that has been removed keeps its ID,
so putting it back is `insertObject()`, the undo path. My call, not a defect:
the document was right to refuse.

Worth keeping because the distinction is the point of the test — a reference
recovers when *the same object* comes back under *its own ID*, which is
exactly what `insertObject()` expresses and what `restoreObject()` would have
quietly turned into something else.

## Regression

Three presets, each configured, cleaned to nothing and rebuilt from scratch
before its tests ran. Every stage's exit code is in
`qualification/qualification-times.txt`.

| Preset | Targets | Compiler warnings | Tests | Time |
| --- | --- | --- | --- | --- |
| `debug` | 437/437 | 0 | **1461/1461** | 189.1 s |
| `release` | 437/437 | 0 | **1461/1461** | 202.7 s |
| `debug-shared` | 437/437 | 0 | **1461/1461** | 212.9 s |

Then the milestone's related tests, five times over until failure — 1009
tests selected by the reference, resolution, face, configuration,
suppression, solver, mate, component, placement, datum, object, persistence,
parameter, sketch, regeneration, CLI and architecture names:

| Preset | Tests | Time |
| --- | --- | --- |
| `release` | **1009/1009 ×5** | 754.1 s |
| `debug` | **1009/1009 ×5** | 749.1 s |

1461 = the 1446 of `P13-CONF-001` plus this milestone's 15. All fifteen stage
exit codes are 0.

**Qualified tree.** The harness records a git tree ID per source directory
before building. Recomputed from the working tree after the run, all eight are
identical, so the tree that was qualified is the tree that is committed:

```text
apps              61778b8e1eb22169857799d8cb00c49be1c4771a
include           70cf2a4b7fff75de7b94ec4a388da68835e8dc07
src               49f0ff20efec113e2edd7d467264fd27ab526696
tests             a657a8d1d9c2062446f8b68c1d928e942f102e4d
examples          1e07d2ff797c2fc49505733931a24051e9171fdc
cmake             a84e909339b24bc7ffca591888e10d48a3e5296c
CMakeLists.txt    a0adbb9c1d3583aab4e294fd41837537e53e4764
CMakePresets.json 951b53d6b7412e6057c188dd41e40cb9e57c6aa3
```

`ninja: warning: premature end of file; recovering` heads each build log. It
is ninja's own `.ninja_log`, damaged when runs were killed during
`P13-SOLVE-001`, and it makes ninja rebuild more rather than less — all 437
targets were built in every preset regardless. It is not a compiler warning,
and none appears in any of the three build logs.

**Test discovery, checked by name.** A new test file that compiles but never
registers would leave the suite green and the milestone unverified. Each of
the 15 new cases was looked for individually in the qualification's own ctest
log by its own name; all 15 are there, and none is reported other than
`Passed`.

## Known limitations

- **`unresolvedMateTargets()` reports, it does not repair.** There is no
  rebinding UI and no "pick a new face" flow; that is a commands question,
  and `P13-CMD-001`'s.
- **A face target needs the current bodies.** Without a `BodyLookup` a face
  cannot be resolved at all, and that is reported as a failure rather than
  guessed at. A caller asking about unresolved targets without regenerating
  first will be told everything face-shaped is unresolved, which is true of
  what it asked rather than of the model.
- **Stability is only claimed for changes that keep the role.** A face
  reference follows its feature through a parameter change; whether a given
  *shape* change preserves a role is `P12-STREF-001`'s contract, inherited
  here rather than restated.
- **External references remain a contract, not a capability.** The
  cross-document rules are validated at the level `P13-REF-001` built; no
  document actually resolves another one, per the accepted P13 constraints.
- **Assembly regeneration is not here.** References were tested across
  regeneration of the part beneath a component, which exists.
  `P13-REGEN-001` is the pipeline that will regenerate assemblies themselves.

## Result

```text
TASK:            P13-STREF-001 — Stable assembly references
IMPLEMENTATION:  +72 Resolution.hpp, +86 its .cpp, and -17 net in
                 SolverSystem.cpp -- the solver's inline target resolution
                 replaced by a call to the shared one. No new identity
                 scheme, no second resolution path.
TESTS:           15 new cases in 1 file (530 lines)
VALIDATION:      every expected geometry derived by hand from the sketch and
                 the extrude depth, never from the resolver under test
REGRESSION:      1461/1461 on debug, release and debug-shared, each from
                 clean; 1009/1009 five times over in release and debug;
                 0 compiler warnings in all three builds; all 15 new cases
                 confirmed by name in the qualification's own log
ADVERSARIAL:     2 findings, 0 production defects
RESULT:          PASS
EVIDENCE:        this directory
```

What is claimed: a reference follows its intended target through a parameter
change, a configuration switch, and a save and load, resolving each time to
geometry derived independently; it becomes explicitly unresolved when that
target goes, recovers unchanged when it returns, and does **not** attach to an
identically-shaped replacement sitting at the very plane it used to occupy.

What is **not** claimed: that a reference can be repaired or rebound (no
commands); that a face target resolves without the current bodies (it fails
rather than guesses); that any document resolves another one (the
cross-document rules are validated as a contract, per the accepted P13
constraints); or that assemblies themselves regenerate — that is
`P13-REGEN-001`.

The milestone found no defect in the code under test. Its two findings are
about the tests: how the oracle problem was avoided, and one API misuse of
mine that the document was right to refuse.

## Revision

| When | What |
| --- | --- |
| 02:4x | surveyed what existed: most of the checklist was built by P12-STREF-001, P13-REF-001 and P13-MATE-001. Added only the mate-target half that was missing |
| 03:1x | 13 of 15 cases passed on the first run; the hand-derived oracle held |
| 03:2x | the two failures were one API misuse of mine — `restoreObject()` is the loading path and refuses an object that kept its ID; `insertObject()` is the undo path and is what recovery means |
| 03:31 | full debug suite 1461/1461; tree frozen and qualified from clean |
| 04:39 | three presets and both repeat stages PASS on the final tree |
