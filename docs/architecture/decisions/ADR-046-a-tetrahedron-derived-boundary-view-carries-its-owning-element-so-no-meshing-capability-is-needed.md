# ADR-046 — A tetrahedron-derived boundary view carries its owning element, so no meshing capability is needed

```text
STATUS:      ACCEPTED
SUPERSEDES:  the blocking claim in ADR-044 and in
             docs/verification/P17-VIZ-001/REMAINING_SCOPE.md
MILESTONE:   P17-VIZ-001, the three ELEMENT contour boxes
DECIDED:     2026-10-13, by an explicit scope decision from the owner
```

## Context

Three boxes of P17-VIZ-001 — the von Mises contour, the normal stress
components and the shear stress components — were recorded as **blocked**, and
the recorded reason was this:

> `MeshView::volumeBoundaryOf` builds from the mesh's Triangle3 SURFACE
> elements, so `elementOfTriangle` returns a TRIANGLE's id. An element
> channel's values are per TETRAHEDRON. The two id spaces are disjoint, and
> `meshing::Triangle` carries `id`, `nodes` and `region` — no owning
> tetrahedron, and no accessor in meshing answers for one.
>
> Closing it needs a NEW CAPABILITY IN THE QUALIFIED P16 MESHING MODULE: a
> triangle-to-owning-tetrahedron mapping.

**Every sentence of the diagnosis is true, and the conclusion does not
follow.** The question "which tetrahedron owns this boundary triangle" only
arises if the boundary is taken from the Triangle3 elements in the first place.
Build the boundary from the **tetrahedra's own exterior faces** and ownership
is known by construction, because you were holding the tetrahedron when you
emitted the face.

That was found by probing before building, which is the rule this repository
already carries: BetterCAD's existing infrastructure had covered the case.
`MeshView` already holds all three pieces:

```cpp
// the four faces of a Tet4, with winding already a TESTED property
// (MeshView_TetFaceWindingsPointAwayFromTheOppositeVertex)
constexpr std::array<std::array<std::size_t, 3>, 4> kTetFaces{...};

// a facet ALREADY carries its owning element id
struct Facet { meshing::ElementId element; std::array<meshing::NodeId,3> nodes; };

// and the generic assembler both existing factories go through
static Result<MeshView> build(const meshing::Mesh&, MeshSource, std::vector<Facet>);
```

`tetrahedraOf` already emits `Facet{tetId, face}` for a selection of tetrahedra,
which is why an element contour **already works correctly** on an interior view
— that is a tested statement, not a hope
(`StructuralViz_AnElementChannelNeedsTheOwningTetrahedron`). What was missing
was only a *boundary* built the same way.

## Candidates

### A — a new `MeshView` factory that derives the boundary from the tetrahedra

Count face incidences over every tetrahedron's four faces, keyed on the sorted
node triple. A face carried by exactly one tetrahedron is exterior; a face
carried by two is interior. Emit `Facet{tetId, face}` for each exterior face,
with the winding `kTetFaces` already defines, and hand them to `build` under a
new `MeshSource`.

```text
+ ownership is free and exact -- the tetrahedron is in hand when the face is
  emitted, so nothing is searched for, inferred or matched afterwards
+ no change to the qualified P16 meshing module
+ reuses kTetFaces, Facet and build -- no new knowledge of tet topology is
  introduced anywhere, and the winding is already a tested property
+ `source()` already exists to tell a caller what `elementOfTriangle` means,
  which is the exact distinction a fourth enumerator makes explicit
+ independently checkable: the tet-derived boundary MUST cover the same node
  triples as the Triangle3 boundary, so the two can be compared against each
  other rather than against the code that built either
- face-incidence counting is work VolumeBoundary explicitly does not do, so
  the two boundary views are no longer the same machinery
- the tet-derived facets carry no RegionId. Nothing in a view carries regions
  today, so nothing regresses -- but a caller wanting a boundary REGION must
  still use the Triangle3 boundary, and that is a real limitation
- O(4N log 4N) for N tetrahedra, against O(T) for the Triangle3 boundary
```

### B — a new capability in the qualified meshing module

Add `meshing` an adjacency: a triangle-to-owning-tetrahedron lookup, or a
general face adjacency object built beside the mesh.

```text
+ topology is arguably meshing's own subject, not the renderer's
+ one lookup would serve any future consumer, not just the contour
- it CHANGES A QUALIFIED MODULE for a need only the renderer has today, and
  P16's qualification would be owed again
- the renderer ALREADY owns the tet-face convention and its winding test, so
  meshing would either duplicate that knowledge or the renderer would have to
  give it up and depend on meshing's ordering instead
- it answers a question candidate A never has to ask. Adding a capability to
  resolve a mapping, when the mapping exists only because of how the boundary
  was chosen, is solving the symptom
- strictly more code, more surface, more qualification, for the same picture
```

### C — a side lookup built in the contour layer

Keep `volumeBoundaryOf` exactly as it is. In `StructuralContour`, build a map
from sorted node triple to owning tetrahedron, then for each boundary render
triangle recover its three `NodeId`s and look the tetrahedron up.

```text
+ no change to MeshView at all
- the render triangle's nodes have to be recovered through `triangleIndices()`
  and `nodeOfVertex()`, which is arithmetic on render indices in exactly the
  layer the four-function bridge exists to keep that out of
- it puts mesh topology in the contour layer, which owns colour and bands
- the caller must pair a view and a lookup correctly, and nothing in the types
  stops it pairing the wrong two
- it re-derives per draw what a view could carry once
```

## Decision

**Candidate A.**

```cpp
/// The volume mesh's boundary, derived from the TETRAHEDRA, so each drawn
/// triangle names the tetrahedron that owns it.
[[nodiscard]] static Result<MeshView> volumeBoundaryByTetrahedronOf(
    const meshing::Mesh& mesh);

enum class MeshSource : std::uint8_t {
    EngineeringSurface,
    VolumeBoundary,
    SelectedTetrahedra,
    VolumeBoundaryByTetrahedron,   // elementOfTriangle names a Tet4
};
```

`elementOfTriangle` on this view returns a **tetrahedron**, and `source()` is
how a caller knows that. The existing `volumeBoundaryOf` is untouched and still
names Triangle3 elements, because a caller asking about the boundary's
*regions* or its canonical boundary elements wants exactly that.

### What is fixed here so a test can hold it

```text
A FACE IS EXTERIOR IFF EXACTLY ONE TETRAHEDRON CARRIES IT. Two is interior.
  Three or more is a malformed mesh and is REFUSED by name rather than
  silently treated as exterior -- a face shared by three tetrahedra means the
  generator produced something non-manifold, and drawing it would hide that.

THE KEY IS THE SORTED NODE TRIPLE, and the EMITTED facet keeps the UNSORTED
  winding from kTetFaces. Sorting is how two tetrahedra are recognised as
  sharing a face; the winding is what makes the normal point out of the
  solid, and the two must not be confused.

DETERMINISM IS BY SORTING, NEVER BY HASHING. The faces are collected into a
  vector and sorted, not inserted into an unordered_map, because unordered
  iteration would make the facet order depend on the standard library and
  this repository requires byte-identical buffers across Debug, Release and
  Debug-shared.

A MESH WITH NO TETRAHEDRA IS REFUSED, not answered with an empty view: this
  factory is about a volume mesh, and an engineering surface has no
  tetrahedron to name.

THE TET-DERIVED BOUNDARY MUST COVER THE SAME NODE TRIPLES AS THE TRIANGLE3
  BOUNDARY. That is an INDEPENDENT CHECK, not an assumption, and it is the
  validation this ADR rests on: if the two disagree, either the assumption is
  wrong or the mesh is, and both are worth knowing. It is asserted as a set
  comparison, in both directions, with the counts required equal first so the
  comparison cannot pass vacuously.
```

## Consequences

```text
The three ELEMENT contour boxes become reachable with NO change to the
qualified P16 meshing module, and with no new mesh topology knowledge
anywhere -- kTetFaces and its winding test already carry it.

ADR-044's title claims "an element channel needs a mapping meshing does not
have". THAT CLAIM IS SUPERSEDED. The mapping is not needed; the premise that
the boundary must come from Triangle3 elements was the thing to drop. ADR-044's
other decision -- a contour is a colour per facet CORNER -- stands unchanged,
and is in fact what makes one mechanism serve both a nodal and an element
channel.

A caller now chooses between two boundary views, and choosing wrongly is
possible. It is not silent: `source()` reports which, `contourBandsOf` already
refuses an element channel whose value it cannot find for a render triangle,
and that refusal names the element and the channel.

RegionId is not available through the tet-derived view. Nothing in any view
carries regions today, so nothing regresses, but boundary-region work must use
`volumeBoundaryOf`.

The interior view keeps its meaning: `tetrahedraOf` is for a SELECTION and is
never built for a whole mesh, which this factory does not change.
```

## What was rejected, and the general lesson

Candidate B was the plan of record for two milestones, written into `TODO.md`,
`REMAINING_SCOPE.md` and ADR-044 as the blocker. It was rejected on the
evidence that it answers a question candidate A never asks.

The diagnosis was correct and the conclusion was not, which is a specific
failure worth naming: **a blocker derived from one implementation route is a
statement about that route, not about the capability.** The question to ask of
a blocker is not "how do I get this missing thing" but "why am I asking for it
at all".
