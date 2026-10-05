# P16-PERSIST-001 — round trip, determinism and regeneration

```text
SUBJECT:  what survives a save and a load, what is recomputed, and the
          measurements behind both claims
```

## 1. The canonical fingerprint

Every round-trip comparison here is of **canonical meshing intent** and
nothing else, using the same fingerprint P16-CMD-001's command tests use:

```text
body
global target size, or the token "default" when absent
linear and angular deflection
each local control, through orderedLocalSizing(): the full face reference
    (feature, role, entity, along, along sketch, chamfer edge, copies)
    and the size
each boundary set, through orderedBoundarySets(): identity, name, and each
    face reference in the user's order
each threshold: metric, warning, failure
```

It reads **no generated anything** — no node count, no element count, no
quality figure. A fingerprint that matched because a cached mesh happened to
match would be no evidence at all, and this one cannot.

## 2. The full round trip

The brief's primary fixture: asymmetric geometry (30 x 20 x 10 mm), a global
size, three local controls of which one names a face that cannot resolve, two
boundary sets with one and two faces, a threshold policy, and non-default
deflections.

```text
fingerprint before save  ==  fingerprint after load          PASS
MeshControlId preserved                                      PASS
object name preserved                                        PASS
```

Covered field by field as well as by fingerprint:

| Claim | Test |
| --- | --- |
| whole intent survives | `MeshingPersistence_TheWholeCanonicalIntentRoundTrips` |
| a global size, and its **absence** | `..._GlobalSizingRoundTripsIncludingItsAbsence` |
| local controls keep face and size | `..._LocalControlsKeepTheirFaceAndSize` |
| set identity, name, face order | `..._BoundarySetsKeepTheirIdentityNameAndOrder` |
| every threshold-capable metric | `..._EveryQualityMetricRoundTrips` |
| deflections, exactly | `..._SurfaceDeflectionsRoundTripExactly` |
| the hand-written file | `..._TheHandWrittenReferenceFileLoadsAsWritten` |

**Identity is preserved, not reallocated.** The `MeshControlId` is the
document's `ObjectId`, restored by the existing loader; a boundary set's
`BoundarySetId` is restored as written. A local control has no synthetic
identity at all — it is keyed by its `FaceName` (P16-SIZE-001) — so there is
nothing for a load to allocate differently, which is the cleanest possible
answer to the brief's §8.

## 3. Units

The format stores bare SI. The test crosses the boundary in both directions
for five quantities and requires exact canonical equality:

```text
1 mm, 10 mm, 1 m, 0.001 m, 0.0125 m    globally and as a local control
```

and then pins the file's own unit, which a round trip alone cannot:

```text
10 mm  ->  "target_size": 0.01
```

A serializer writing millimetres would round-trip perfectly and still be wrong
for every other reader of the format. Mutation **M2** writes millimetres and
mutation **M3** reads with the wrong scale; both must die.

## 4. No generated mesh reaches the file

Checked against the **written bytes**, with a mesh generated first so there is
something that could leak:

```text
absent from the file:  nodes, tetrahedra, triangles, elements, node_id,
                       element_id, facet, connectivity, netgen,
                       quality_report, worst, render, ng_mesh
present, so the test is not passing on an empty file:
                       "mesh-control", "target_size", "boundary_sets"
```

### The file-size measurement

Same canonical intent but for the target size. A cylinder, because a block's
planar boundary cannot refine:

```text
                 ELEMENTS     FILE
coarse, 10 mm         745     1778 bytes
fine, 1.2 mm         1871     1780 bytes
                 ---------    ---------
difference          +1126     +2 bytes
```

**The mesh grew by 1126 elements and the file grew by two bytes** — the digits
of one number. Persisting even the coarse mesh would have cost tens of
kilobytes. This is the brief's §40 and §109 answered by measurement.

## 5. Deterministic serialization

`Json` is `nlohmann::ordered_json`, so the file's order is the **insertion**
order — determinism is earned, not given. Earned by writing every collection
through `MeshControl`'s ordered accessors and never by iterating a stored
vector.

```text
save the same document twice                  byte-identical       PASS
save, load, save                              byte-identical       PASS
save, load, save, load, save                  byte-identical       PASS
two documents whose controls were added in
    OPPOSITE order                            identical sections   PASS
```

The last one is the real test: the stored order of local controls carries no
meaning (P16-SIZE-001 declares it so, P16-CMD-001's fingerprint ignores it),
so two documents with the same intent must produce the same file. Mutations
**M5** and **M6** write the stored order instead of the canonical one.

**A consequence, stated plainly:** the stored vector order is *not* preserved
across a save — a document reloaded has its local controls in canonical
order. That is what makes save/load/save converge, and it loses nothing
semantic.

## 6. Regeneration after load

```text
save
    -> meshing intent

load
    -> intent restored, NO mesh, currency = NoMesh
    -> regenerate geometry (the document holds definitions, not bodies)
    -> generate mesh on request, from the loaded intent
    -> currency = Current
```

| Claim | Measured |
| --- | --- |
| a loaded document has intent and no mesh | `currency == NoMesh`, `mesh() == nullptr` |
| the regenerated mesh matches the pre-save one | identical element count |
| a local refinement still refines, after load | refined count > plain count, both equal to their pre-save values |
| a boundary set resolves against the new mesh | `fullyResolved()`, 34 facets of 624 elements |
| quality is recomputed under the loaded policy | `qualityDescribesCurrentPolicy()`, 0 invalid elements |

**Mesh identities are explicitly not part of the promise.** `NodeId`,
`ElementId` and facet handles belong to one generation. The boundary-set test
reports its facet count rather than comparing it with a number from before the
save, and says so: persistence promises the **face**, not the facets.

## 7. A fresh process

An in-process round trip cannot rule out an in-memory identity dependence —
a pointer, an address, a per-process hash — because the same process wrote the
state it is reading.

`examples/models/meshed_plate.bcad` is **hand-written**, and two CTest
*process* tests run the real `bettercad-cli` executable on it:

```text
cli.info.mesh-control       exit 0, reports "object:7  mesh-control  PlateMesh"
cli.validate.mesh-control   exit 0, "2 objects regenerated", "Result: valid"
```

Nothing that process reads was ever in another one, and the file was produced
by a person rather than by the serializer. The in-process tests then load the
same file and check every field, so the schema is anchored by meaning as well
as by a successful parse.

End to end from that file: regenerate, mesh, resolve the boundary set —
**81 nodes, 287 elements, `fixed_end` → 40 facets, 0 invalid elements.**

## 8. The command layer after a load

Persistence has to restore intent in a form the command layer fully
understands, or an undo after a load would not be exact.

```text
the loaded MeshControlId equals the original                      PASS
SetGlobalMeshSizeCommand: execute, undo to the loaded state,
    redo                                                          PASS
EditLocalMeshSizingCommand against a reference out of the file    PASS
RemoveLocalMeshSizingCommand, undo restores the same FaceName     PASS
undo back to exactly the file's fingerprint                       PASS
a control created after a load takes no ID the file used          PASS
```

The last one is the allocator: `last_allocated_id` is persisted in the
document header and the loader refuses a value below an ID in use, so a new
object cannot collide with a restored one. Verified through the meshing path
rather than assumed from the header.
