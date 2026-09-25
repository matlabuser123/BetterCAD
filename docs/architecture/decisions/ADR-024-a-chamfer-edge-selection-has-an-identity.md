# ADR-024 — A chamfer's edge selection has an identity of its own

```text
STATUS:    Accepted
DATE:      2026-09-26
MILESTONE: P14-STREF-001 (closing its one open gate)
SUPERSEDES: nothing
TOUCHES:   ADR-012 (a drawing may reference only semantic geometry)
```

## Context

Every face in BetterCAD is named semantically. An extrude's side face is named
by the sketch entity that sweeps it; a hole's bottom by the hole; a pattern
copy's face by the source face plus the instance. A stored reference therefore
survives any edit that does not remove the thing it names.

A chamfer's face was the exception. It was named

```text
FaceSelector{ .role = Chamfer, .edge = N }
```

where `N` was the **position**, from 1, of an edge selection in
`ChamferDefinition::edges`. That vector is ordinary stored intent: it is part
of the feature's definition, a user may reorder it, and nothing about the solid
depends on its order.

So a drawing reference to a chamfer face was one stable link — the chamfer's
`ObjectId`, which is never reused — followed by one positional link.
`P14-STREF-001`'s audit measured the consequence: on a block with two chamfers,
a reference to "edge reference 2", then the edge list reordered. The solid is
identical, the stored reference is untouched, and it resolves to the face 60 mm
away. That is a silent rebind, and "no silent rebinding" is the gate
`P14-STREF-001` exists to hold. It was the only gate in P14 that was not met,
and it blocked `P14-QUAL-001` at its precondition.

Two further findings from the `P14-QUAL-001` audit shaped this decision, because
both say the defect was invisible to the checks meant to catch it:

- A prohibited-name search finds nothing. `faceIndex`, `edgeIndex`,
  `shapeIndex`, `topologyIndex`, `bestMatch`, `firstMatch` and
  `reinterpret_cast` all return **0** across `src/` and `include/`. The field
  is called `edge`.
- `Reference_NoPersistedReferenceCarriesAnIndexIntoTheKernel` passes. Its
  forbidden list has `edgeIndex` and `edge_index`, not `edge`; and `"edge"`
  could not simply be added, because it is a legitimate key elsewhere in the
  same file — a variable fillet's selection is `{"edge": <curve>, "radii": …}`.
  Its fixture also builds no chamfer, so no chamfer reference was in the text
  it searched. The test's name is exact: an index into the **kernel**. This was
  an index into stored intent.

## Decision

**A chamfer's edge selection is a thing with an identity, and a chamfer face is
named by that identity.**

```cpp
struct ChamferEdge {
    ChamferEdgeId id{};            // allocated, never reused
    geometry::EdgeSignature curve{};
};
struct ChamferDefinition {
    std::vector<ChamferEdge> edges{};   // order is presentation only
    …
};
struct FaceSelector {
    std::optional<ChamferEdgeId> edge{};   // the SELECTION, not its position
    …
};
```

`ChamferFeature` owns an `IdAllocator` and assigns identities:

- a selection whose `id` is unset gets a **fresh** one;
- a selection whose `id` is set **keeps** it, and on an edit must be one the
  chamfer already has;
- the allocator's high-water mark is persisted, so a reload continues the
  sequence instead of restarting it.

`chamferFaceNamer` translates the kernel's request-edge index into that index's
selection id, and it is the only place the translation happens. An index the
ids do not cover names nothing, which fails loudly.

Order carries no meaning. Reordering, inserting into or shortening the list
changes no reference, because the id travels with the selection it identifies.

### Why an allocated id and not the `EdgeSignature`

The obvious cheaper option is to name the face by the selected curve, which is
already stored and is content-based, so a reorder cannot disturb it. It was
rejected on one case:

```text
reference -> selection B
delete B
add a selection with EXACTLY B's curve
```

A signature-named reference resolves, because the replacement's signature is
byte-identical to the original's. The user's dimension silently moves onto new
material and nothing says the thing they dimensioned was deleted. An allocated
id stays unresolved, which is the truth.

This is also the rule a side face already follows: it is named by the sketch
entity's id, so redrawing an identical line does not adopt the old line's
references. Identity in this codebase is allocated, not derived from geometry,
and a chamfer selection is now no different.

### Why the identity lives in the definition

It has to travel with the selection. An identity held beside the definition —
a parallel id vector, or a map the feature keeps — would have to be re-matched
to the selections on every edit, and the only thing available to match on is
the curve. That is geometry similarity, it is what this ADR forbids, and it
fails the same identical-replacement case.

Putting the id in the selection makes a reorder correct by construction:
`std::swap(def.edges[0], def.edges[1])` moves the pairs.

### Why an edit may not name an id the chamfer does not have

Without that rule, the scheme would rest on callers choosing not to attack it:
delete selection 2, add one back asking for id 2, and every reference to the
deleted selection rebinds. `setDefinition` rejects an id this chamfer does not
currently hold, so a retired identity cannot be revived. A *fresh* feature is
exempt — it carries a fresh `ObjectId`, and a `FaceName` is
`{feature, selector}`, so no reference stored against an older feature can name
it whatever its selection ids are.

## Alternatives rejected

**Name the face by the `EdgeSignature`.** Cheapest, no format change, immune to
reordering. Rejected: it resolves to an identical replacement, which is the
silent rebind in a different costume. Recorded above.

**Keep the position and forbid reordering.** Make `ChamferDefinition::edges`
append-only in the API. Rejected: it does not fix the file — a hand-edited or
third-party-written document can still carry any order — and it takes a
capability away from the user to protect an implementation detail. It also
leaves the reference positional, so the next edit shape that renumbers the list
reopens the same hole.

**Accept it as a documented limitation and re-scope the gate.** Cheapest of
all. Rejected because it was explicitly ruled out: the gate is not to be
weakened. It is also the wrong trade — this is the one reference class in the
system that can silently mean different material, and P14's whole claim is
that a drawing stays attached to the model.

**Give the chamfer a sub-object document model**, with each selection a real
document object carrying an `ObjectId`. Rejected as far more than the problem
needs: selections are not independently referenceable, do not appear in the
dependency graph, and have no name, so an `ObjectId` each would add a node
kind, serialization, and undo surface for nothing. `EntityId` within a sketch
is the established precedent for exactly this shape — an identified sub-object
inside one owner — and `ChamferEdgeId` follows it.

## Consequences

**The file format moves to version 2**, and the reader accepts 1 and 2. A
version-2 chamfer writes `{"id": n, "edge": <curve>}` selections and a
`"last_edge_id"`, and a version-2 face selector writes `"chamfer_edge": id`.
None of those is readable by a version-1 reader, so the version says so rather
than leaving it to a field that happens not to be recognised.

**Version-1 documents load, and their chamfer references are migrated
exactly.** A version-1 chamfer's selections are bare curves; they are read in
file order and identified 1..N in that order. A version-1 face selector says
`"edge": n`, meaning position n, and position n is now the selection identified
n — so the conversion preserves precisely the face the file named. The two keys
are distinct, and a file carrying both is rejected rather than one being
preferred.

The migration cannot recover intent from *before* a reorder. If the old
positional scheme had already moved a reference onto the wrong face by the time
the file was saved, the file means the wrong face and the migration faithfully
preserves that. Nothing can do better; the information is gone.

**No committed artifact carried a positional chamfer reference.** Checked
across all 32 committed `.bcad` models: six contain chamfers, none names a
chamfer *face*. So no historical reference needed migrating. The committed
models were regenerated because the version field and the chamfer selection
shape changed, not because any of them meant something different.

**Fillets are unaffected.** `FilletDefinition::edges` has the identical shape,
but there is no `FaceRole::Fillet`: a fillet face cannot be referenced at all,
so its selections need no identity. If a fillet face ever becomes
referenceable, it needs this treatment first.

**`FaceCopy::instance` stays a positional ordinal, deliberately.** It was
examined and is not the same defect. A pattern instance ordinal is determined
by the pattern's own count and spacing rather than by a user-orderable list, so
no edit reorders instances while leaving the solid identical; and
`LinearPatternFeature` guarantees that suppressing an instance never renumbers
another. A reference to instance 3 cannot be moved by an edit that preserves
the geometry.

**A definition built from scratch means new selections.** Re-applying a
hand-built `ChamferDefinition` to an existing chamfer replaces its identities,
because curves without identities are new selections. Callers that mean "change
the distance" read `definition()`, edit it and set it back — which preserves
ids — and that is what every caller in the repository does.

**The audit that missed this is fixed, structurally.**
`Reference_NoChamferReferenceIsStoredAsAPositionInTheFile` builds a chamfer,
references its second face, and checks the file three ways: the reference is
stored as an identity; no face selector carries `"edge"` followed by a *number*
(the key stays free to mean a curve, which it legitimately does elsewhere); and
— the part no word list can fake — the selections are reordered, the file is
written again, and the stored reference is byte-identical while the array order
has changed.

**The `-local` build presets arrive with this change**, because `P14-QUAL-001`
must be refrozen and `CMakePresets.json` is inside the fingerprint, so the
build-location decision had to be taken before the refreeze rather than after.
`debug-local`, `release-local` and `debug-shared-local` inherit the plain
presets and put the build at `$BETTERCAD_BUILD_ROOT/<preset>`. No absolute path
is committed: where a build belongs is a property of the checkout, not of the
project, and a clone outside a synchronising folder keeps using the plain
presets unchanged. With the variable unset the presets are disabled and CMake
says so, rather than building somewhere unintended.
