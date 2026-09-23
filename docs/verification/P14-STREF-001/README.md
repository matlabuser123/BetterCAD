# P14-STREF-001 — Stable Drawing References

```text
TASK:      P14-STREF-001
STATUS:    BLOCKED on one gate -- no silent rebinding. Everything else PASS.
BASELINE:  dca2e61 (P14-BOM-001), clean tree, HEAD == origin/main
```

## TASK

Audit every reference a drawing makes to the model, prove each one names
semantic identity rather than a transient position, and prove what happens
when a target moves, goes and comes back.

The milestone's own gate, from `TODO.md`:

```text
drawing references stable
+ no index-based identity
+ no silent rebinding
+ unresolved/recovery correct
+ persistence PASS
+ determinism PASS
```

**One of those is not met**, and the whole of this milestone's result turns on
it. See THE FINDING below.

## BASELINE

`P14-BOM-001` at `dca2e61`, verified before anything was written: 2030/2030 on
`debug`, `release` and `debug-shared`, tree clean, `HEAD == origin/main`.

## REFERENCE INVENTORY

Every reference-bearing drawing object, what it names, and how stable that is.

| Object | References | Representation | Verdict |
| --- | --- | --- | --- |
| `View` (base) | a part, a feature, or one component occurrence | `ObjectReference` — owning document + `ObjectId` | **QUALIFIED STABLE** |
| `View` (base, assembly) | every active occurrence | `ViewSubject::Assembly`; the set is asked of `activeComponents()` at draw time | **QUALIFIED STABLE** — nothing stored to go stale |
| `View` (projected/section/detail/auxiliary) | its parent | `ViewId` | **QUALIFIED STABLE** |
| `View` (section) | where to cut | a `CuttingPlane` — a plane, not a reference | intent, not a reference |
| `View` (detail) | a region of the parent's sheet | sheet coordinates | intent, not a reference |
| `Dimension` | a datum/principal plane, a datum/principal axis, or a **named face** | `PlaneReference` / `AxisReference` / `FaceName` | **QUALIFIED STABLE**, except one face role — see THE FINDING |
| `Annotation` (leader, centreline, centremark, datum, surface finish, FCF) | the same vocabulary | as above | as above |
| `Annotation` (hole callout) | the hole **feature** | `ObjectId` | **QUALIFIED STABLE** |
| `Annotation` (balloon) | one component **occurrence** | `ObjectId` naming a `ComponentId` | **QUALIFIED STABLE** |
| `Annotation` (BOM table) | the view whose occurrences it lists | `ViewId` | **QUALIFIED STABLE** |
| FCF datum references | letters (ISO 5459) | `char` | by design (ADR-020); its rename gap is recorded there |

Nothing in the list is an index into a kernel traversal, a vector offset, an
address or a name-match. The one exception is below, and it is a position in
a **stored list**, not in a traversal.

### The searches this claim rests on

```text
faceIndex|edgeIndex|shapeIndex|componentIndex|objectIndex|topologyIndex|ordinal
    -> no match anywhere in include/ or src/

nearest|closest|best.?match|most similar|first matching
    -> 12 matches, every one geometric CANONICALISATION or prose:
       "the point of the plane nearest the origin" (a canonical form),
       "the material nearest the viewer" (hidden-line), and a comment in
       Views.cpp explaining that a clip is solved exactly rather than by
       sampling. None is an identity fallback.
```

And the persisted form is asserted directly: a document carrying a view, a
dimension on a named side face, a datum annotation and a balloon is saved and
read as text, and required to contain none of `face_index`, `edge_index`,
`shape_index`, `topology`, `ordinal`, `pointer`, `address`, `handle` or
`traversal` — while containing `"role": "side"`, `"entity":` and
`"type": "balloon"`, which is semantic identity.

## THE RESOLUTION CONTRACT

`drawing/Resolution.hpp`, three states:

```text
Resolved     the target exists and the reference found it
Unresolved   the reference is well formed and its target is not there NOW.
             The intent is kept and it resolves again if the target returns
Invalid      the reference itself is incoherent
```

**It resolves nothing of its own.** Each answer comes from the resolver the
drawing already uses — `measure()` for a dimension, `draw()` for an
annotation, `projectedGeometry()` for a view — read through the error code it
already returns. A second resolution path would be a second answer to "where
is this", and the two could differ.

The mapping, and why the split falls there:

```text
NotFound, FailedPrecondition  -> Unresolved   the target is not here now
InvalidArgument               -> Invalid      it could not be right whatever
                                              the model does
anything else                 -> Invalid      the resolver broke; never
                                              reported as Resolved
```

That distinction is asserted on two references that both fail: a `HoleBottom`
role on an **extrude** is `Invalid` — an extrude has no hole bottom in any
configuration — while a face of a feature that has been removed is
`Unresolved`. Only the second is worth keeping, and only the second can
recover.

## THE FINDING: A CHAMFER FACE IS NAMED BY LIST POSITION

Every other face in this codebase is named semantically. An extrude's side
face is named by the **sketch entity** that sweeps it, so moving the profile,
renaming anything or adding features elsewhere leaves the name meaning what it
meant.

A chamfer's face is not. It is named `{role = Chamfer, edge = N}`, where `N`
is the **position of an edge reference in `ChamferDefinition::edges`**
(`src/features/chamfer/ChamferRegeneration.cpp`: `edge = reference + 1`). That
list is ordinary stored intent which a user may reorder.

So a drawing reference to a chamfer face runs down this chain:

```text
PlaneReference -> the chamfer feature     stable (an ObjectId)
               -> edge = N                A POSITION IN A LIST
               -> the Nth EdgeSignature   and ChamferFeature.hpp says of these
                                          in as many words: "they are not
                                          persistent topological names"
```

which is exactly the shape this milestone's brief forbids: *one stable link
followed by one unstable positional link.*

### What actually happens, measured

`Reference_AChamferFaceIsNamedByItsPositionInTheChamfersEdgeList` builds a
block with two chamfers — the top front edge at `y = 0` and the top back edge
at `y = 60` — takes a reference to the face of edge reference **2**, and then
**reorders the chamfer's edge list**. The solid is identical afterwards: the
same two edges, chamfered by the same amount. The stored reference is
untouched.

```text
before the reorder   the reference resolves to a face at y > 30   (the back)
after  the reorder   the same reference resolves to y < 30        (the front)
                     and the two are more than 30 mm apart
```

**The reference still resolves, and it now names different material.** That is
a silent rebind. A dimension on that face would keep showing a number, and the
number would be measuring something else.

The same test then shows, on the same body, that a side face named by its
sketch entity is unmoved by the identical edit — so the fault is the
positional name and not the model.

### What behaves well

`Reference_RemovingAChamferEdgeLeavesTheLastNameUnresolved`: shortening the
list does **not** slide a reference along it. The name of the last position
stops matching anything and the reference becomes `Unresolved` with
`NotFound`, which is the right answer. Only **reordering** is dangerous.

### Why it was not fixed here

The fix is to give each chamfer edge reference an identity of its own — a
stable id allocated per feature — and to name the face by that id rather than
by its position. That reaches:

```text
ChamferDefinition          a new field on a qualified P12 structure
its JSON                   a file-format change, with migration for every
                           existing document that has a chamfer
chamferFaceNamer           the name it emits
every chamfer test         which constructs `edge = N` meaning a position
the committed reference    P11/P12/P13 models with chamfers are committed
  models                   artifacts, compared in qualification
```

That is a change to a qualified subsystem's persisted format. It is not this
milestone's to make, and making it would be the "large refactor combined with
unrelated feature work" `CLAUDE.md` forbids. The brief's own instruction for
this case is followed instead: **report the exact capability gap, and leave
the checkbox open.**

### What it means for the gate

```text
drawing references stable    met, except the one path above
no index-based identity      met for persistence; NOT met for the chamfer
                             face name, which is a position in stored intent
no silent rebinding          NOT MET -- demonstrated above
unresolved/recovery correct  met
persistence PASS             met
determinism PASS             met
```

`P14-STREF-001` therefore **does not pass its final gate** and is not marked
`[x]`. `Prevent silent rebinding` stays `[ ]` with this finding recorded
against it.

## DRAWING VIEW REFERENCES

```text
a view keeps its source through regeneration, and through unrelated objects
    being added and removed
a view whose source is removed is Unresolved and does NOT adopt the identical
    second block sitting beside it; it still names what it always named
a derived view keeps its parent -- and the contract there is STRONGER than
    "becomes unresolved": removeView REFUSES while another view is projected
    from it ("... is projected from it"), so the reference cannot be orphaned.
    Removed in the right order, child first, it goes without complaint
```

## DIMENSION REFERENCES

```text
a dimension follows its face through a parametric edit: moving the profile's
    right-hand corners from 100 to 120 changes the number from "100.00" to
    "120.00" while the stored intent -- the entity that sweeps the face -- is
    unchanged
a dimension whose feature is removed is Unresolved, with an identical second
    block present and measurable, and it does not move to it
a dimension recovers: removeObject then insertObject restores the target with
    its original ID (the undo path), and the same reference resolves again to
    the same value, with the definition compared equal before and after
```

A note recorded rather than glossed: a **dimension's** target cannot be
suppressed by configuration in this build, because P13's configuration
suppression applies to components and mates, not to features. The recovery
test therefore uses the undo path, which is the way a feature legitimately
goes and returns with the same identity.

## ANNOTATION REFERENCES

```text
a hole callout follows its hole through a diameter change: the target is the
    hole FEATURE, so Ø10 THRU becomes Ø12 THRU with the same target identity
a callout whose hole is removed is Unresolved -- with an identical second
    hole, same diameter, same face, still in the model
```

## BALLOON / OCCURRENCE REFERENCES

```text
two balloons on two occurrences of ONE part hold two different targets, and
    neither holds the part: same BOM row, same item number, two references
a balloon recovers across configuration switching: Resolved -> Unresolved ->
    Resolved, with the same stored occurrence at the end
```

## RECOVERY VS REBINDING

The two are one test, so the difference cannot be read as one behaviour:

```text
RECOVERY   the same occurrence is suppressed and unsuppressed. Unresolved,
           then Resolved again, same target
REBINDING  the same occurrence is REMOVED, and an identical sibling of the
           same part remains active. The reference stays Unresolved and still
           names the occurrence that went
```

Every no-rebind fixture in this file is built so that a rebinding
implementation would **succeed**: there is always an identical survivor
present — a second block of the same size, a second hole of the same
diameter in the same face, a second occurrence of the same part.

## SAVE / LOAD

```text
deserialize(serialize(intent)) == intent for a base view, a projected view, a
    dimension on named faces and a balloon -- compared as whole definitions
the reloaded document regenerates and every one of them resolves
```

## DETERMINISM

```text
resolved six times: the STATE, the target and the DIAGNOSTIC are identical
    every time -- a message that varied would mean the resolver took a
    different path
an Invalid reference is as stable as a Resolved one
item numbers through the BOM chain are stable across the same repetitions
```

## KNOWN LIMITATIONS

```text
1  THE FINDING above: a chamfer face is named by its edge reference's
   POSITION in the chamfer's list, so reordering that list silently moves any
   drawing reference to it. Shortening the list is safe (Unresolved).
2  ADR-012's standing gap is unchanged: there is no stable EDGE name in this
   codebase, so a dimension to a fillet tangent edge has no spelling.
3  ADR-020's standing gap is unchanged: renaming a datum feature symbol from
   B to C silently changes what every frame citing either letter requires.
4  A dimension's target cannot be suppressed by configuration, because
   configuration suppression covers components and mates. Recovery for a
   dimension is therefore the undo path.
5  The resolution query recomputes the drawing to answer; it is a diagnostic
   route, not a cheap one.
```

## ADVERSARIAL REVIEW

This milestone IS an adversarial review — its whole purpose is to attack the
reference system rather than extend it. One genuine defect was found in the
system under audit (THE FINDING). No production code was changed: the audit's
conclusion is that the drawing layer's contract is already right, and the one
weakness is in a feature's face naming, out of this milestone's reach.

Four of my own test expectations were wrong, and each was wrong in a way that
taught something.

```text
"a role the feature cannot produce is Unresolved"
    It is INVALID, and the implementation is right. An extrude has no hole
    bottom in ANY configuration, so the reference is incoherent rather than
    waiting for something. The test now asserts both states side by side --
    Invalid for the impossible role, Unresolved for a removed target -- which
    is a better test than the one I meant to write

"a dimension can measure a component's faces"
    It cannot: a component is not a feature and has no faces of its own. Two
    tests were built on that and failed. The right fixture dimensions the
    PART the occurrence places

"a dimension's target can be suppressed by configuration"
    It cannot. P13's configuration suppression covers components and mates,
    not features, so a dimension's target goes and returns by the undo path
    instead. Recorded as a limitation rather than worked around

"deleting a parent view leaves its child unresolved"
    The contract is STRONGER: removeView refuses while another view is
    projected from it, so the reference cannot be orphaned at all. The test
    now asserts the refusal, the message, that nothing moved, and that
    removing child-first works -- so the refusal is about the dependency and
    not about the view
```

### What was attacked and held

| Question | Answer |
| --- | --- |
| Can inserting a feature change a referenced face because an index moved? | No. A side face is named by the sketch entity that sweeps it; adding and removing an unrelated block leaves it resolving to the same face |
| Can deleting Hole A make its callout jump to identical Hole B? | No — and B is left present, same diameter, same face, re-parented onto the plate so it survives A's removal |
| Can a balloon jump between repeated instances of one part? | No. Suppressed: Unresolved while the sibling stays active and item 1. Removed: still Unresolved, still naming the occurrence that went |
| Can configuration switching permanently lose a valid reference? | No — Resolved → Unresolved → Resolved, same stored target throughout |
| Can recovery create a new identity? | No. The definition is compared equal before the target goes and after it returns |
| Can save/load alter a target? | No — whole definitions compare equal for view, projected view, dimension and balloon, and all resolve after a fresh regeneration |
| Can an unresolved reference keep a stale value on show? | No. `measure()` has no value at all while unresolved; the old text is not carried |
| Can a view silently switch source? | No — an identical second block is present and is not adopted |
| Can Debug and Release pick different geometry for an ambiguous model? | Resolution is an exact name match with no candidate ranking, so there is nothing to pick; the three-preset qualification covers the rest |
| Can unordered iteration choose a different candidate? | `findNamedFaces` walks the body and matches names exactly; no unordered container is consulted |
| Can a datum rebind to a geometrically equal face? | No. A datum annotation names a `PlaneReference`, which is a name, not a shape |
| Can BOM regeneration change a balloon's target? | No. The balloon stores the occurrence; the BOM is derived from it, not the other way round (ADR-022) |
| Can an occurrence reference degrade into a part ID? | No — two balloons on two occurrences of one part hold two different targets, and neither equals the part |
| Can persisted reference data contain a topology ordinal? | Not in the drawing's own persisted form — asserted by reading the saved file. The chamfer `edge` field IS a position, and it is the finding |
| Can two stable-looking references end in an unstable link? | YES, once: the chamfer chain. That is the finding, and it is why this milestone does not pass |

## TESTS

**32 new test cases**, all in `tests/drawing/StableReferenceTests.cpp`.

```text
[stref]      962 assertions in  32 test cases
[drawing]   9367 assertions in 349 test cases   (was 8836 in 332)
```

Discovery confirmed: the suite grew from 2030 tests to 2047 in every preset,
and the 32 cases run under the `[stref]` filter. Two of them carry `[audit]`
and two more `[rebind]`.

## FULL REGRESSION

Three presets, each configured and **built from clean**, on the frozen tree:

| Preset | Build | Warnings | No-op rebuild | Tests |
| --- | --- | --- | --- | --- |
| `debug` | exit 0 | 0 | compiled 0, linked 0 | **2047 / 2047** (213.97 s) |
| `release` | exit 0 | 0 | compiled 0, linked 0 | **2047 / 2047** (186.50 s) |
| `debug-shared` | exit 0 | 0 | compiled 0, linked 0 | **2047 / 2047** (207.73 s) |

```text
qualification finished Thu 24/09/2026 6:45:01.49, 0 stage(s) failed
```

Baseline was 2030 tests; this milestone adds **17 ctest entries** for 32
Catch2 cases (`catch_discover_tests` groups the `SECTION`-free cases it can).

### The determinism gate

```text
repeat release  exit 0   2046 / 2046, each test run five times (873.67 s)
repeat debug    exit 0   2046 / 2046, each test run five times
```

The OneDrive filesystem fault that failed `P14-BOM-001`'s debug repeat did
**not** recur here, in either preset. As before that does not close it: it is
intermittent, and `TODO.md` still carries the decision as due.

### The harness

`verify-harness.cmd` was run before the qualification and passes: pointed at a
preset that does not exist it gives `QUALIFICATION FAILED: 3 stage(s) failed`
and exit 3.

### The qualified tree is the committed tree

Tree IDs from a scratch index, before the first build, by the harness after the
last test run, and again before the commit — identical in all three:

```text
apps              b32ce14e7be30b1c05432f740c2d25607be73b39
include           d5f78ad7013922a42d4130e51dd99fe9b5e7e098
src               697fbebf98ab3325bb64f7a367cd4b8f5c8f9679
tests             f20724b538d46bb744e7ab230e37f35fcae1fd9a
examples          d0d2ae4277ba99b46ff1384725292deb3519c199
cmake             a84e909339b24bc7ffca591888e10d48a3e5296c
CMakeLists.txt    a0adbb9c1d3583aab4e294fd41837537e53e4764
CMakePresets.json 951b53d6b7412e6057c188dd41e40cb9e57c6aa3
```

`apps`, `examples`, `cmake`, `CMakeLists.txt` and `CMakePresets.json` are
**byte-identical to the P14-BOM-001 baseline**.

### What changed, and what did not

The change is **purely additive**: `drawing/Resolution.{hpp,cpp}`, the test
file, and two build-list lines. **No existing production code was modified.**

That is the audit's own conclusion in the shape of a diff: the drawing layer's
reference contract was already right everywhere it reaches, and the one
weakness is in a feature's face naming, out of this milestone's reach. An
audit that had to rewrite what it was auditing would have been reporting a
different result.

## RESULT

```text
TASK:            P14-STREF-001 -- Stable drawing references
IMPLEMENTATION:  drawing/Resolution.hpp -- a three-state contract
                 (Resolved / Unresolved / Invalid) read from the resolvers
                 the drawing already uses, resolving nothing of its own. No
                 other production change: the audit found the existing
                 contract correct everywhere it reaches
TESTS:           32 new; 2047/2047 in debug, release and debug-shared, each
                 from clean; 2046/2046 five times over in both repeat presets
VALIDATION:      every reference class inventoried and classified; the
                 forbidden-pattern searches run and their 12 hits read one by
                 one; the persisted form asserted to contain no index, handle
                 or ordinal; every no-rebind fixture built so a rebinding
                 implementation would SUCCEED
ADVERSARIAL:     1 defect found in the system under audit (THE FINDING);
                 0 production defects introduced; 4 of my own test
                 expectations corrected, each revealing behaviour stronger or
                 different from what I assumed
WARNINGS:        0 in all three builds
DETERMINISM:     state, target and diagnostic identical over six resolutions;
                 repeat gate clean in both presets
RESULT:          BLOCKED on one gate. "No silent rebinding" is NOT met: a
                 chamfer face is named by its edge reference's POSITION in
                 the chamfer's list, and reordering that list silently moves
                 any drawing reference to it. Demonstrated, not inferred.
                 Every other gate passes.
EVIDENCE:        this directory
TODO:            P14-STREF-001 is NOT marked [x].
                 "Prevent silent rebinding" stays [ ] with this finding.
                 The eleven items that ARE proven are ticked.
NEXT:            P14-STREF-001 -- close the chamfer gap. It needs its own
                 authorization, because the fix changes a qualified P12
                 structure and its file format.
```

## THE REMAINING WORK, PRECISELY

To close the gate, chamfer edge references need identity of their own:

```text
1  ChamferDefinition::edges becomes a list of {EdgeSignature, id}, with ids
   allocated per feature and never reused
2  chamferFaceNamer emits {role = Chamfer, edge = id} instead of a position
3  the chamfer's JSON gains the id, with a migration that assigns
   positions as ids for documents written before the change
4  the P11/P12/P13 reference models containing chamfers are regenerated and
   re-qualified, because their committed .bcad files change
5  the audit test in this milestone is inverted: reordering must leave the
   reference where it was
```

Step 4 is why this was not done here: it changes a qualified subsystem's
persisted format and the committed artifacts three phases are qualified
against.

## FILES

```text
qualification/qualify.cmd               the harness, from P14-BOM-001
qualification/verify-harness.cmd        its exit-code regression; run and passing
qualification/run-qualification.cmd     the entry point and its repeat selection
qualification/qualification-times.txt   every stage, its exit code, and the tree IDs
qualification/build-*.log               three presets, 0 warnings each
qualification/rebuild-*.log             the fresh-binary proof
qualification/ctest-*.log               2047/2047 in each preset
qualification/ctest-repeat-*.log        2046/2046 five times over, debug and release
```
