# P16-VIZ-001 — CAD ↔ mesh selection linkage

```text
SUBJECT:  how a click becomes an engineering identity, and how a CAD face and
          its mesh find each other in both directions
DATE:     2026-10-04
```

## The chain, and where each link lives

```text
a pixel
  |  Viewer::pickMeshAt            OCCT: Select3D_SensitivePrimitiveArray with
  v                                SetDetectElements -> LastDetectedElement
a RENDER TRIANGLE INDEX
  |  MeshView::elementOfTriangle   the adapter's lookup table
  v
an ElementId
  |  MeshScene::inspectElement     the canonical mesh and the quality report
  v
what the panel shows
```

**The viewer stops at the render triangle, deliberately.** It has no
`MeshView` and no mesh, and an AIS object has no business producing
engineering identity. `Viewer::pickMeshAt` therefore returns
`MeshPick{presentation, triangle}` — a position in a GPU buffer — and the
window, which holds the cache that built the presentation, does the
translation. That is the only place it can be done correctly, because only the
cache knows which facet each buffer position came from.

Three compile-failure cases make the shortcut unwritable rather than merely
discouraged: a `std::size_t` cannot become a `NodeId` or an `ElementId`, and a
`NodeId` cannot become an index. The reverse one matters most — a mesh
enumerates nodes in ascending `NodeId`, which makes the mistake look plausible
and usually right. It is wrong exactly when a volume mesh has interior nodes,
which a boundary view does not draw.

## Selection mode

A CAD body and its mesh occupy the same space. Without a mode, the result of a
click would depend on drawing order, which is the one thing a selection must
never depend on.

```text
Pick: Mesh   -> pickMeshAt, answering a render triangle
Pick: CAD    -> pickAt, answering an ObjectId
```

GUI state, not model state. Switching it changes what the NEXT click resolves
and nothing about what is already selected, and nothing at all about the
model. `pickMeshAt` over a CAD body answers *nothing* rather than the nearest
mesh — asserted by `MeshDisplay_PickingAMeshFindsNothingWhereThereIsNoMesh`.

## CAD → mesh

```text
a chosen CAD face
  |  renderer::highlightFor(view, mesh, map, faceIndex | FaceName | NamedBoundarySet)
  v
meshing::boundaryFacetsOf / resolveBoundarySet          P16-MAP-001
  v
a BoundaryFacetSet: facets, and a MappingState per reference
  |  MeshView::trianglesOfElement
  v
render triangles -> Viewer::setMeshHighlight
```

**No geometric search, no nearest-triangle match, no tolerance anywhere in that
path.** If there were, the highlight would be the GUI's opinion rather than the
mapping's. Every step is `P16-MAP-001`'s own API.

### Why a face index, and not only a name

`highlightFor` has a `FaceName` overload and a **face-index** overload, and the
second is the one a click uses. The reason is a fact about the model, not a
convenience:

> `cutHole` names a hole's flat faces and not its cylindrical wall, so a bored
> hole's wall has no name — it "is still mapped and is still answerable in
> reverse; what it cannot be is the target of a forward query, because there is
> nothing to ask with."
> — `MappedFace`, P16-MAP-001

A GUI restricted to the `FaceName` overload could therefore not highlight the
mesh of a drilled hole's wall, which is one of this milestone's mandatory
reference cases. The index overload asks the map by position instead, so it is
still the mapping that answers.

An index is a correlation handle for ONE map and never an identity: not
persisted, not comparable between maps. A named boundary set stores
`FaceName`s, which is what survives a remesh — and the panel's face list shows
unnamed faces as unnamed rather than inventing a label for them.

## Mesh → CAD

```text
a boundary facet's ElementId
  |  renderer::sourceOfFacet  ->  meshing::sourceFaceOf      P16-MAP-001
  v
a FacetSource: the facet, the face's index in the map, and its names (possibly
empty)
```

Reported as it comes. An unnamed face is reported with an empty name list and
the panel says `CAD face 3 (unnamed)` — not a blank, and not a guess. A face
shared by two tetrahedra has no CAD boundary and answers
`NoBoundaryCorrespondence`, which is `P16-MAP-001`'s definition of interior and
not a second one.

## Unresolved is a status, not an empty list

`MeshHighlight` carries the mapping's answer **whole**, including the
per-reference `MappingState`:

```cpp
struct MeshHighlight {
    meshing::BoundaryFacetSet mapping;   // requested[] with a state each
    std::vector<std::size_t> triangles;
};
```

Restating it as one flag would lose exactly what `P16-MAP-001` was careful to
keep. A reference that no longer binds comes back `Unresolved` with its facets
empty, and the window says *"That face no longer resolves against this mesh."*
Flattening a deleted face and an unmeshed face both to "nothing highlighted"
would tell the user nothing, which the brief lists as an automatic failure.

## Generation agreement

Every entry point takes the current mesh and refuses when the pieces do not
belong together:

```text
the mapping's MeshStamp    vs the mesh's   -> FailedPrecondition
the render cache's stamp   vs the mesh's   -> FailedPrecondition
the quality report's stamp vs the mesh's   -> FailedPrecondition
```

One function, `requireSameGeneration`, so the check cannot be present on three
paths and forgotten on the fourth, and its message names both generations.

And `MeshScene::adopt` makes it structural rather than repeated: a scene can
only be constructed from a mesh, a map and a report that all carry the same
stamp, so after construction nothing inside has to re-check and a caller
cannot assemble a mismatched set at all.

## The results

```text
FIXTURE                      CAD -> MESH                       MESH -> CAD
box, named face (top)        resolved, 2 facets, 2 triangles   back to that
                             disjoint from the side face's     same FaceName,
                                                               every facet
box, a name that no longer   Unresolved, carried as a status,  n/a
binds (HoleBottom)           empty highlight, user told
bored block, hole wall       resolved BY INDEX -- the wall     face index with
(UNNAMED, cylindrical)       has no name; every highlighted    an EMPTY name
                             facet's nodes lie within 6.1 mm   list, reported
                             of the bore axis, which no outer  as unnamed
                             face of a 40x30 block can satisfy
bored block, remeshed        the same face, a finer facet set; n/a
coarse -> fine               the old cache and the old map are
                             both REFUSED against the new mesh
named boundary set           resolves against both a coarse    n/a
("fixed_end")                and a fine mesh; facets differ,
                             the set does not
```

Transformed bodies are covered by the frame test rather than here: a mesh
carries no transform (ADR-032), so the question is whether the adapter puts its
coordinates in the right frame and unit, and the silhouette comparison in
[ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md) Finding 1 answers it on three
fixtures.

## Revision

First issue, 2026-10-04.
