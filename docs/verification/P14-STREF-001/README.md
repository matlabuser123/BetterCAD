# P14-STREF-001 — Stable Drawing References

```text
TASK:      P14-STREF-001
STATUS:    PASS. The gate that blocked it -- no silent rebinding -- was CLOSED
           on 2026-09-26; see CLOSURE at the end of this document. Everything
           above CLOSURE is the original audit, at its own baseline, and is
           left exactly as it was written: it is the record that found the
           defect, and the defect was real.
BASELINE:  dca2e61 (P14-BOM-001), clean tree, HEAD == origin/main
CLOSURE:   d1d7c7d (P14-QUAL-001 audit), ADR-024
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

---

# CLOSURE — 2026-09-26

Everything above this line is the original audit, at baseline `dca2e61`, and is
unchanged. It found a real defect and recorded it accurately; rewriting it to
look tidier would destroy the only record of how the defect was found. What
follows is the separate work that closed it.

```text
TASK:      close "no silent rebinding"
BASELINE:  d1d7c7d, clean tree, HEAD == origin/main
DECISION:  ADR-024 — A chamfer's edge selection has an identity of its own
RESULT:    PASS
```

## THE GAP, RESTATED

A chamfer face was named `{role = Chamfer, edge = N}`, where `N` was the
**position** of an edge selection in `ChamferDefinition::edges` — a vector of
ordinary stored intent that a user may reorder. Reordering it left every stored
reference resolving, to different material, with the solid unchanged and the
reference untouched.

Confirmed present in production code before any change was made, rather than
taken from this document:

```text
include/bettercad/core/document/References.hpp:97
    std::optional<std::uint32_t> edge{};
    "a chamfer face names an edge reference from 1"

src/features/chamfer/ChamferRegeneration.cpp
    .edge = static_cast<std::uint32_t>(reference + 1)   // the position
```

## THE FIX

A chamfer's edge selection is now a thing with an identity.

```cpp
struct ChamferEdge {
    ChamferEdgeId id{};            // allocated, never reused
    geometry::EdgeSignature curve{};
};
struct FaceSelector {
    std::optional<ChamferEdgeId> edge{};   // the SELECTION, not its position
};
```

`ChamferFeature` owns an `IdAllocator`, whose existing contract is exactly the
property needed and was already written down: *"Values start at 1 and are never
reused, even after the identified item is deleted, so a stale reference can
never silently resolve to a newer item."*

`chamferFaceNamer` turns the kernel's request-edge index into that selection's
id, and is the only place the translation happens. Order now carries no meaning:
reordering, inserting into or shortening the list changes no reference, because
the id travels with the selection.

**`FaceSelector::edge` changed TYPE, not just meaning.** That was deliberate: it
made the compiler enumerate every consumer, so nothing could keep treating the
field as a position by accident. Every site it found is in FILES below.

Why an allocated id rather than the `EdgeSignature` already stored, and the
three alternatives rejected, are in
[ADR-024](../../architecture/decisions/ADR-024-a-chamfer-edge-selection-has-an-identity.md).

## EDIT SEMANTICS

Each row is a test, and each test fails against the pre-fix code.

```text
edit                                   reference            test
reorder the selections                 follows its own      ...KeepsItsMeaningWhenTheEdgeListIsReordered
insert another selection first         unchanged            ...IsUnmovedWhenAnotherEdgeIsInsertedBeforeIt
delete an unrelated selection          unchanged            ...SurvivesTheDeletionOfAnUnrelatedEdge
delete the referenced selection        Unresolved           ...WhoseEdgeIsDeletedBecomesUnresolved
add back an IDENTICAL curve            stays Unresolved     ...DoesNotRebindToAnIdenticalReplacementEdge
restore the same selection             recovers             ...AChamferEdgeIdIsRestorableButNotForgeable
undo / redo through the command        recovers / goes      ...UndoingAChamferEditRestoresItsReferences
save -> load                           same selection       ...RoundTripsThroughSaveAndLoad
reorder -> save -> load                same selection       ...AReorderedChamferStillResolvesAfterSaveAndLoad
resolve repeatedly, and after regen    one answer, exactly  ...ResolvesToTheSamePlaceEveryTime
```

The save/load pair matters more than it looks. A file records the list in its
current order, so under a positional scheme reordering and then saving would
write a file whose positions disagree with the references stored in it, and the
next load would resolve them to the wrong faces with nothing in the file to show
anything had gone wrong.

## TWO DEFECTS THE TESTS FOUND IN MY OWN DESIGN

Both were found by running the suite, not by reading the diff, and both are
recorded because the first draft of this fix was wrong in ways that looked
right.

**THE FIRST RULE WAS TOO STRONG, AND IT BROKE UNDO.** The obvious rule is "an
edit may only name ids the chamfer currently has", which stops a deleted
selection being revived under its old identity. `ReferenceModel_ShaftRegenerates`
failed against it: `ModifyFeatureCommand::undo` replays `setDefinition` with the
previous definition, so undoing a deletion restores exactly that — a selection
with the id it used to have. The rule made undo impossible, and it also
contradicted this milestone's own requirement that restoring the same semantic
target is *target recovery*.

The rule is now: an edit may name any id this chamfer has **ever** allocated;
an id above the high-water mark is refused, because nothing can be referring to
one that was never handed out and granting it would reserve a value a later
selection would also be given. Silent rebinding is prevented earlier — a new
selection carries no identity at all — so nothing is lost.

**THE IMPLICIT CONVERSION WAS A TRAP.** `ChamferEdge` first converted from a
bare `EdgeSignature`, which kept `.edges = {a, b}` compiling everywhere and
looked harmless. It is not, and the project's own reference test showed why:

```cpp
moved.edges[1] = geometry::circleSignature(...);   // ShaftTests, before
```

That is a user **repairing** a chamfer whose edge moved. The conversion silently
discarded the identity the selection had and minted a new one, so every drawing
reference to that face would have been stranded — by the one workflow whose
whole purpose is to put a broken reference back. The constructor is now
`explicit`, which does not compile, and the 25 call sites each say what they
mean:

```cpp
edges[1].curve = someCurve;          // re-select: same selection, references follow
edges[1] = ChamferEdge{someCurve};   // replace: a new selection, old references unresolve
```

## PERSISTENCE AND MIGRATION

The document format moves to **version 2**, and the reader accepts 1 and 2. The
version exists for a field that changes meaning, and this is one.

```text
version 2    "edges": [{"id": n, "edge": <curve>}], "last_edge_id": n
             face selector: "chamfer_edge": id
version 1    "edges": [<curve>, ...]
             face selector: "edge": n   -- a POSITION
```

**A version-1 document loads, and its references migrate exactly.** Its
selections arrive without ids and are identified 1..N in the file's own order,
so position n is the selection now identified n. The conversion preserves
precisely the face the file named. It is not a guess, and it is not silent: the
two keys are distinct, a file carrying both is refused rather than one being
preferred, and re-saving writes version 2.

The migration cannot recover intent from *before* a reorder. If the old scheme
had already moved a reference onto the wrong face by the time the file was
saved, the file means the wrong face and the migration faithfully preserves
that. The information is gone; nothing can do better.

Tested by `ChamferFeature_AVersionOneFileMigratesToIdentifiedSelections` and
`ChamferFeature_AVersionOneChamferFaceReferenceMeansTheSameFace`, which build
the version-1 form out of the version-2 writer's own output — so every byte fed
to the reader is a byte the old writer would have produced — and require the
migrated document to be byte-identical to one that never left version 2.

## COMMITTED ARTIFACTS

**No committed artifact carried a positional chamfer reference.** All 32
committed `.bcad` models were searched: six contain chamfers, and none names a
chamfer *face*. So no historical reference needed migrating, and the earlier
estimate that closing this gap would disturb the reference models P11, P12 and
P13 are qualified against was wrong — it was based on those models containing
chamfers, which is not the same thing.

All 32 were nevertheless regenerated, because the version field changed and,
in the six chamfered ones, the selection shape did. **The regeneration was
proved faithful rather than assumed**: before replacing anything, the byte
comparisons were run against the old files and

```text
readFile(built) == readFile(committed)    FAILED  20 of 20   (bytes changed)
equivalent(loaded, built)                 FAILED   0 of 20   (meaning did not)
```

Every committed version-1 file still loaded to a model equivalent to its
builder. The bytes moved; no model did.

## THE AUDIT BLIND SPOT, CLOSED STRUCTURALLY

`P14-QUAL-001`'s audit found that neither of the checks meant to catch this
could: a prohibited-name search returns **0** for every term because the field
is called `edge`, and
`Reference_NoPersistedReferenceCarriesAnIndexIntoTheKernel` has no chamfer in
its fixture and could not simply add `"edge"` to its forbidden list, because
`"edge"` legitimately names a curve elsewhere in the same file — a variable
fillet's selection is `{"edge": <curve>, "radii": [...]}`.

`Reference_NoChamferReferenceIsStoredAsAPositionInTheFile` closes it without
banning a word:

```text
1. the reference is stored as an identity, and the file really does contain
   a chamfer face reference -- checked, not assumed
2. no face selector anywhere carries "edge" followed by a NUMBER; the
   legitimate uses are always "edge" followed by an OBJECT, so the structure
   is what is checked and the word stays free to mean what it means
3. the selections are REORDERED, the file is written again, and the stored
   reference is byte-identical while the array order has changed
```

The third is the one no word list can fake: under a positional scheme the file
would have to say something different to keep meaning the same face, and it did
not — which is exactly how the defect survived a save and a load.

## THREE-PRESET REGRESSION

Run by `closure/run-qualification.cmd`, on `closure/qualify.cmd`, which is
byte-identical to the harness `P14-REFMOD-001` qualified with.
`closure/verify-harness.cmd` was run first and passed: pointed at a preset that
does not exist, the harness fails real stages and exits 3, so a failed stage
cannot reach nobody.

### The frozen tree

```text
qualification candidate   d1d7c7d + the working tree of this closure
apps                      d8b08545dbc80be58b4827977dcceaadeb60c82d
include                   0dec3a71f8a7334f8c03241d97fcbf27bf636bf4
src                       a552f8b5364c413e8cfd9f417e4a1dd8f760ac99
tests                     454eb29c62e8d40693ab8dee46a40ac70868dd0b
examples                  2e1ef60bb5e67fa3589e1246613261cd746ddce2
cmake                     a84e909339b24bc7ffca591888e10d48a3e5296c
CMakeLists.txt            a0adbb9c1d3583aab4e294fd41837537e53e4764
CMakePresets.json         951b53d6b7412e6057c188dd41e40cb9e57c6aa3
```

Recorded before the first build and again after the last test run, identical
both times. `CMakePresets.json` is unchanged from the baseline — see THE BUILD
LOCATION below for why that is worth stating.

### The stages

```text
stage                        exit   wall clock
debug configure                 0   03:58:29 -> 03:58:46
debug clean (attempt 1)         0   03:58:47
debug build                     0   03:58:47 -> 04:11:47
debug no-op rebuild             0   04:11:48      compiled 0, linked 0
debug ctest                     0   04:11:48 -> 04:15:49   2256/2256
release configure               0   04:15:49 -> 04:15:54
release clean (attempt 1)       0   04:15:55
release build                   0   04:15:55 -> 04:31:12
release no-op rebuild           0   04:31:12      compiled 0, linked 0
release ctest                   0   04:31:12 -> 04:34:25   2256/2256
debug-shared configure          0   04:34:25 -> 04:34:30
debug-shared clean (attempt 1)  0   04:34:31
debug-shared build              0   04:34:31 -> 04:46:43
debug-shared no-op rebuild      0   04:46:44      compiled 0, linked 0
debug-shared ctest              0   04:46:44 -> 04:50:11   2256/2256
repeat release (5x)             0   04:50:11 -> 05:04:24
repeat debug (5x)               0   05:04:24 -> 05:19:22
```

**0 stages failed**, and every clean was first-attempt.

```text
Debug          2256 / 2256     0 warnings
Release        2256 / 2256     0 warnings
Debug-shared   2256 / 2256     0 warnings
```

`grep -ci warning` returns 0 over all six logs — the three builds and the three
no-op rebuilds — under the 22 warning flags `BetterCADCompilerOptions.cmake`
sets, `-Werror` among them.

### Fresh binaries

Each build was followed immediately by a second build of the same preset. All
three had exactly one edge to run, `Checking git revision`, which is always
dirty by design and produced no recompile and no relink. The binaries CTest ran
are the binaries the build produced.

`ctest -N` lists **2256** tests, 11 more than `P14-REFMOD-001`'s 2245, and the
11 are accounted for exactly:

```text
  8   net new cases in tests/drawing/StableReferenceTests.cpp
      (10 chamfer cases replacing the 2 that recorded the gap)
  1   Reference_NoChamferReferenceIsStoredAsAPositionInTheFile
  2   the two version-1 migration cases in tests/io/ChamferFileTests.cpp
 ---
 11   = 2256 - 2245
```

A new SECTION was also added to `ChamferFeature_DefinitionIsValidatedOnCreateAndEdit`
— re-applying an unidentified definition is a new selection, not a no-op — and
sections do not add ctest entries, which is why the arithmetic still closes.

### Determinism

```text
repeat release   11275 "Passed" lines = 2255 x 5, exactly
repeat debug     11275 "Passed" lines = 2255 x 5, exactly
                 0 occurrences of ***Failed, "Not Run" or "Permission denied"
```

Counted from the logs rather than read off ctest's summary line, which reports
tests and not runs. The filter is broad on purpose: this change reaches `core`
(`FaceSelector`), `features` (the chamfer, regeneration, patterns, mirrors),
`io` (both serializers and the format version) and every committed model, so
repeating only the chamfer tests would repeat the new work and none of what it
could have disturbed.

**The OneDrive replace fault did not recur.** It has failed a repeat stage in
four earlier milestones; it did not here. That is one clean run, and it does not
close the decision.

## THE BUILD LOCATION — INVESTIGATED, AND BLOCKED BY A SEPARATE DEFECT

Moving build output out of the synchronised checkout was attempted first,
because `binaryDir` lives in `CMakePresets.json`, inside the fingerprint, so the
decision had to be taken before this tree was frozen rather than after.

A `-local` preset family was written and its mechanics validated: inheriting
`debug`/`release`/`debug-shared` unchanged, building at
`$BETTERCAD_BUILD_ROOT/<preset>`, with no absolute path committed — where a
build belongs is a property of the checkout, not of the project — and with the
presets disabled and CMake saying so when the variable is unset.

**The full three-preset qualification was then run on them, and all three failed
to link the GUI target.**

```text
windeployqt failed (1):
  Unable to find dependent libraries of
  <BETTERCAD_BUILD_ROOT>\gnu-16-mingw-amd64\bin\Qt6Core.dll
```

`windeployqt` resolves the Qt runtime **relative to the executable it is
deploying**, as `<exe dir>/../../<toolchain key>/bin`. That names the real Qt
only when the build tree happens to sit inside the source tree. With the build
elsewhere it looked under the build root — the right parent directory, the wrong
name — while `bettercad-deps` sat where it always had. The deps prefix itself is
correct and build-independent: `BETTERCAD_WINDEPLOYQT` resolves to the same
`bettercad-deps` path in both build trees.

Three fixes were tried and none changed it: Qt's `bin` on `PATH` for the deploy,
running `windeployqt` from Qt's own `bin`, and pre-placing `Qt6Core.dll` beside
the executable.

**So the `-local` presets and the attempted deploy fix were both reverted rather
than committed.** Committing presets that do not work, or a fix that fixes
nothing, is worse than leaving the decision open. This qualification ran on the
standard presets, which every prior milestone used.

What the attempt established, and it is new:

```text
the Qt deploy step has been depending on the build tree living inside the
source tree. A clean build anywhere else fails the GUI target, and that is
true independently of OneDrive.

so the build-location decision is NOT a configuration change. It needs the Qt
deployment made location-independent first, and that is its own piece of work,
outside what this milestone was authorized to do.
```

## ADVERSARIAL REVIEW

```text
Can a chamfer face reference still move under a reorder?
    No. Measured: the reorder test asserts both faces by position in mm, 30 mm
    apart, and the file-level test reorders, re-saves and shows the stored
    reference byte-identical.

Can an identical replacement capture the old reference?
    No, by the ordinary route: a new selection carries no identity, because
    ChamferEdge will not convert from a bare curve. Tested with a
    byte-identical curve.

Can a retired id be revived to capture references?
    Only by restoring the selection it identified, which is target recovery and
    is required. Forging an id above the high-water mark is refused. This is
    the weaker of the two rules I wrote, and it is weaker on purpose -- see the
    undo defect above.

Can a stale reference reach a DIFFERENT chamfer's selection?
    No. A FaceName is {feature ObjectId, selector}, ObjectIds are never reused,
    so a reference stored against one chamfer cannot name another whatever its
    selection ids are. This is why create() and restore() may take ids
    verbatim.

Does a pattern or mirror of a chamfer still name its copies correctly?
    Yes. A copy's faces carry the SAME selection ids as the original's; the
    instance is named by `copies`. PatternSupport captures the ids BY VALUE
    beside the request, because the operation outlives the scope that found the
    feature -- it must not hold a pointer to it.

Is FaceCopy::instance the same defect?
    No, and it was checked rather than assumed. A pattern instance ordinal is
    determined by the pattern's count and spacing, not a user-orderable list,
    so no edit reorders instances while leaving the solid identical; and
    LinearPatternFeature guarantees suppressing an instance never renumbers
    another.

Are fillets affected?
    No. FilletDefinition has the identical shape, but there is no
    FaceRole::Fillet -- a fillet face cannot be referenced at all, so its
    selections need no identity. If one ever becomes referenceable it needs
    this treatment first.

Could a rejected edit leave the allocator advanced?
    No. identify() validates in a first pass that allocates nothing, then
    allocates in a second that cannot fail.

Could {unset, id 1} collide?
    It could have. Every id a definition already carries is reserved BEFORE
    anything is allocated; reserving as it went would have handed the first
    edge id 1 and collided with the second. Found by reading the code, fixed
    before it ran.

Did any test get weakened to pass?
    No test was deleted or loosened. Two were REPLACED -- the pair that recorded
    the gap, one of which was written to fail if the gap closed, and it did.
    Expectations changed where the file format or a message deliberately
    changed, and each is listed in FILES.

Is the version bump honest?
    Yes. A version-2 file is genuinely unreadable by a version-1 reader, and the
    version says so rather than leaving it to a field that happens not to be
    recognised.
```

No credible defect was left unresolved. The two found in my own design are above,
with the tests that found them.

## KNOWN LIMITATIONS

**The build-location decision is still open**, now with a named blocker. See
above.

**The parameters file format is untouched** and stays at version 1. It is a
separate format with its own version and nothing in this change reaches it.

**Two of the migration paths are synthesised, not archived.** No committed file
carries a version-1 chamfer face reference, so the migration tests build the
version-1 form from the version-2 writer's output. That is faithful — every byte
is one the old writer would have produced — but it is not the same as a file
that has actually sat on disk since before the change. There is no such file to
archive: the corpus never had one.

## FILES

```text
include/bettercad/core/Id.hpp                      ChamferEdgeId
include/bettercad/core/document/References.hpp     FaceSelector::edge is an ID
include/bettercad/features/ChamferFeature.hpp      ChamferEdge, restore, lastEdgeId
include/bettercad/features/Regeneration.hpp        the namer takes ids
include/bettercad/io/DocumentFile.hpp              version 2, oldest readable 1
src/features/chamfer/ChamferFeature.cpp            identify(): allocate, keep, refuse
src/features/chamfer/ChamferRegeneration.cpp       index -> id, the only translation
src/features/reference/FaceReferences.cpp          resolve by identity, not by count
src/features/pattern/PatternSupport.cpp            ids captured by value
src/features/pattern/MirrorRegeneration.cpp        curves for the image check
src/core/document/References.cpp                   selector validation
src/io/json/FeatureJson.cpp                        chamfer selections, both shapes
src/io/json/DatumJson.cpp                          chamfer_edge, and the legacy edge
src/io/json/DocumentJson.cpp                       the version gate is a range
apps/bettercad_cli/DocumentCommands.cpp            "the face of chamfer edge 2"

tests/drawing/StableReferenceTests.cpp             the edit-semantics suite
tests/io/ChamferFileTests.cpp                      the two migration cases
tests/features/ChamferFeatureTests.cpp             identity on create and edit
tests/reference/ShaftTests.cpp                     re-select keeps identity
tests/core/geometry/FaceNameTests.cpp              the namer stand-in
tests/support/ChamferBlockModel.hpp                call sites
tests/support/FaceKindModels.hpp                   call sites
tests/io/FaceKindFileTests.cpp                     both keys, and both at once
tests/io/DocumentFileTests.cpp                     the version range
tests/assembly/PersistenceTests.cpp                version-agnostic gate test
tests/features/SketchOnFaceTests.cpp               message
tests/cli/FaceReferenceCliTests.cpp                message
tests/core/geometry/DraftTests.cpp                 message
examples/reference_models/*.cpp                    6 chamfered builders
examples/models/**/*.bcad                          32 models regenerated

docs/architecture/decisions/ADR-024-*.md           the decision and what it rejected
docs/verification/P14-STREF-001/closure/           this qualification's logs
```

## RESULT

```text
TASK:            close "no silent rebinding"
IMPLEMENTATION:  a chamfer edge selection has an allocated, persistent
                 identity, and a chamfer face is named by it (ADR-024).
                 Document format version 2, reading 1 and 2.
TESTS:           11 new ctest entries, reconciled exactly against 2256 - 2245.
                 2256/2256 in Debug, Release and Debug-shared, each from
                 clean, 0 warnings, fresh binaries. 2255/2255 five times over
                 in Release and in Debug -- 11275 = 2255 x 5 passes counted in
                 each log.
VALIDATION:      every edit shape in the table above asserted against the
                 drawn/resolved face, not against the definition alone; the
                 persisted form checked structurally, including by reordering
                 and re-saving; version-1 migration proved byte-identical to a
                 document that never left version 2; all 32 committed models
                 shown to load equivalent to their builders before being
                 regenerated.
ADVERSARIAL:     2 defects found in MY OWN design by the test suite -- a rule
                 that broke undo, and an implicit conversion that silently
                 retired identities in a repair workflow. Both fixed, both
                 recorded. 0 defects left open.
RESULT:          PASS
EVIDENCE:        this section and closure/
TODO:            "Prevent silent rebinding" -> [x]; P14-STREF-001 -> [x].
                 The build-location decision stays OPEN, with a named blocker.
```

**P14-STREF-001 is complete.** P14 is not yet qualified: that is `P14-QUAL-001`,
which this unblocks.
