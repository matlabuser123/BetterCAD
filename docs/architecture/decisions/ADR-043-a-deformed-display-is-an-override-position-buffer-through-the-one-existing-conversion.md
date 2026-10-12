# ADR-043 — A deformed display is an override position buffer through the one existing conversion

```text
STATUS:   Accepted
DATE:     2026-10-12
CONTEXT:  P17-VIZ-001, the display surface
```

## Context

P17-VIZ-001's headless layer is qualified. `StructuralResultView::deformedPositions`
produces `x + s·u` in **SI metres**, in exactly the layout of
`MeshView::positions()`. Nothing draws it, so twelve of the milestone's
checkboxes are unticked and `UNIT_BOUNDARY.md` owes two columns of its
`x_view_mm` table — there is no measurement of a millimetre coordinate reaching
the viewer.

The constraint that shapes everything: **`toModelLength` in
`OcctMeshPresentation.cpp` is the project's only metres-to-millimetres
conversion.** P16-VIZ-001 found the defect that rule exists for — a mesh in
metres beside a body in millimetres is 1000× too small, and *alone in the scene
it is invisible as an error*, because `FitAll` frames the only thing there. The
clue was one silhouette measuring 32674 pixels as a mesh and 54120 with the
solid added.

So a deformed display must not acquire a second conversion. If the undeformed
and deformed buffers reach the kernel through two call sites, they are
consistent only as long as two pieces of code agree.

## Candidates

### A — `MeshPresentation` takes an optional override position buffer

```cpp
MeshPresentation(const MeshView& view, MeshDrawStyle style,
                 std::span<const double> positions = {});
```

Empty means "use `view.positions()`". The constructor already reads positions
in exactly one place and converts them there.

```text
+  The single conversion stays single, and BOTH buffers pass through the SAME
   call site -- so `x_view_mm = toModelLength(x_m + s*u_m)` is consistent with
   the undeformed buffer BY CONSTRUCTION rather than by two sites agreeing.
+  Normals are recomputed from the deformed corners, which is correct: a
   deformed facet has a different normal, and reusing the undeformed normals
   would light the deformed shape as though it had not moved.
+  The selection array is built from the same corners, so picking resolves
   against what is on screen rather than against where the mesh used to be.
+  Render triangle i is still facet i of the view, so the index-to-NodeId
   bridge is untouched and selection provenance needs no second mapping.
+  One defaulted parameter in a qualified file. The existing call site
   compiles unchanged.
-  Changes a qualified P16 file at all.
```

### B — a separate `ResultPresentation : AIS_InteractiveObject`

```text
+  Leaves the qualified file untouched.
-  Duplicates the facet winding, the normal computation, the edge buffer, the
   stale style, the highlight grouping and the whole sensitive-primitive
   selection path -- about 150 lines of machinery whose ONLY difference is
   where the positions came from.
-  Two classes that must be kept in step. A change to the winding convention,
   to the double-sided face culling, or to how a pick reports its index has to
   be made twice, and the second one is the one that gets forgotten.
-  No answer to the question that matters: it would still need its own
   conversion call, so the single-conversion rule now depends on two files
   agreeing -- which is the defect P16-VIZ-001 recorded.
```

### C — a mutable `setPositions()` that rebuilds the buffers in place

```text
+  One presentation object per mesh, re-scaled without re-displaying.
-  Contradicts the discipline the whole layer already follows: `MeshView` and
   `StructuralResultView` have NO in-place update, by ADR-031, precisely so a
   stale object answers `describes(...) == false` instead of reinterpreting
   its buffers against different numbers.
-  The corners, the normals and the SELECTION array would have to be rebuilt
   together, and a partial update leaves picking resolving against the
   previous scale while the image shows the new one -- a defect that looks
   like a picking bug and is a state bug.
-  Needs SetToUpdate and a re-Compute, so it is not even simpler.
```

## Decision

**Candidate A.** `MeshPresentation` takes an optional override position buffer
in SI metres, and `Viewer` gains an explicitly named sibling entry point:

```cpp
Result<PresentationId> displayDeformedMesh(ObjectId source, const MeshView& view,
                                           std::span<const double> positions,
                                           MeshStyle style = MeshStyle::ShadedWithEdges);
```

**A named sibling rather than a defaulted parameter on `displayMesh`.** A
defaulted span that silently means "undeformed" hides which path a caller took,
and this layer's rule is the opposite — `loadTarget` and `restraintTarget` exist
"to make the path a UI must take EXPLICIT, and to give it a name a test can
hold". The same reasoning applies here.

**It refuses a length mismatch.** A buffer of the wrong size is not scaled
wrongly, it is refused: `positions.size()` must equal
`view.positions().size()`, because a short buffer would read past its end and a
long one means the caller has a different mesh.

## Consequences

```text
The deformed buffer leaves the headless layer in METRES and acquires its
millimetres from toModelLength, the same call, in the same constructor, as the
undeformed one. The x_view_mm table can finally be measured rather than owed.

A new scale means a NEW presentation, matching ADR-031. A caller that wants to
re-scale removes and re-displays, and the old presentation cannot be mistaken
for the new one because its buffers are what they were.

The presentation still holds no mesh and no engineering identity: it is
constructed from a MeshView plus a span of doubles, and `lastDetectedTriangle`
still returns a render index for the caller to translate.
```

## What this ADR does NOT decide

**Contour colouring.** It is a separate decision and it is deliberately not
bundled here, because the smallest correct subsystem is the one that can be
validated on its own. The groundwork is noted so the next pass starts from it:

```text
Compute() ALREADY groups facets by colour -- `plain` and `highlighted_`, each
a Graphic3d_Group with its own uniform aspect. N bands are N groups, which
needs no per-vertex colour machinery at all, and flat-per-facet is the HONEST
rendering for element-constant stress rather than a smoothing of it.

A NODAL channel is a different question, and conflating the two would be the
"silently average to nodes" defect wearing a palette. Nodal displacement is
defined at nodes and interpolated linearly inside a Tet4, so per-VERTEX colour
is that element's own shape function rather than an invention -- but it needs
Graphic3d_ArrayFlags_VertexColor and a second path. Decided separately.
```

### How that forecast turned out

**ADR-044 decided otherwise, and the note above is left as written rather than
rewritten** — an ADR records what was decided and what was expected at the
time, and quietly editing a forecast after the fact destroys exactly the
information a later reader wants.

What changed:

```text
FORECAST   N bands are N GROUPS, needing no per-vertex colour machinery; a
           nodal channel needs a SECOND path.

DECIDED    ONE path, per-vertex colour for both, because the presentation's
           corner buffer is NON-INDEXED -- nine doubles per facet -- so a
           colour per facet CORNER expresses both cases:
             an element channel gives all three corners the same colour and
             the facet draws flat, which is what the group approach would
             have achieved anyway;
             a nodal channel gives each corner its own.
           Two mechanisms would have had to be kept in step, and the flat
           case falls out of the general one for free.
```

The forecast's *reasoning* held — flat-per-facet is the honest rendering for
element-constant stress, and conflating the domains would be the averaging
defect wearing a palette. Only the mechanism changed.

And one thing the forecast could not have known, which ADR-044 records: a
boundary view names **Triangle3** surface element ids, not owning tetrahedra,
so the element channels are blocked on a mapping `meshing` does not have.
