# ADR-045 — A glyph overlay is a list of segments in SI metres, and the glyph data carries no position

```text
STATUS:   Accepted
DATE:     2026-10-12
CONTEXT:  P17-VIZ-001, the load and restraint glyphs
```

## Context

P17-VIZ-001's overlay data is qualified: `LoadGlyph`, `NodalLoadArrow`,
`RestraintGlyph` and `ReactionArrow`, with the resultant sign, the arrow
scaling, the zero-vector case and the component mask each mutation-killed.
Nothing draws them.

**The first thing to notice is that none of those four structures carries a
position.** They describe the load, not where to put it:

```text
NodalLoadArrow   a NodeId and a Force3D
ReactionArrow    a NodeId, a Force3D and the constrained mask
RestraintGlyph   a RestraintId, a component mask and three counts
LoadGlyph        a LoadId, a kind, a resultant, an area, an element count
```

That is deliberate — the data layer computes no geometry — and it means the
drawing layer has to **resolve** positions from canonical sources. Which
sources, and where that resolution lives, is what this decides.

The viewer also has no way to draw a glyph today. `PresentationKind` has two
values, `CadBody` and `EngineeringMesh`, and the only entry points take a
`geometry::Body` or a `MeshView`.

## What is drawable, and what is not

```text
DRAWABLE, with an unambiguous position
  a nodal load arrow    at its node
  a reaction arrow      at its node
  a restraint symbol    at each restrained node, with that node's mask

NOT DRAWABLE as a single glyph
  LoadGlyph for a pressure, a traction or gravity. A face load has no
  point and gravity has no location at all -- its "position" is the whole
  body. LoadGlyph is a SUMMARY for a panel (resultant, area, element
  count), and its name promises more than its contents hold.
```

So this decision covers the three positioned kinds. A per-facet pressure
arrow needs facet centroids through `GeometryMeshMap` and is a separate,
larger piece; it is named in the evidence rather than half-built here.

## Candidates for the drawing interface

### A — the headless layer produces a list of SEGMENTS in SI metres

```cpp
struct OverlaySegment { Point3D from; Point3D to; };
struct StructuralOverlay { OverlayRole role; std::vector<OverlaySegment> segments; };
```

```text
+  EVERYTHING INTERESTING IS HEADLESS AND TESTABLE: resolving a node's
   position, the arrow's direction and length, the arrowhead's barbs, the
   number of bars in a restraint symbol. None of it needs a GPU.
+  The presentation holds no mesh, no result and no engineering identity --
   the rule MeshPresentation already follows. It receives points.
+  ONE primitive type for the whole overlay, so the adapter is a loop over
   segments and the single metres-to-millimetres conversion applies to it
   unchanged.
+  A segment list is trivially checkable: count them, measure them, compare
   endpoints against the canonical node position EXACTLY.
-  An arrowhead drawn from line segments rather than a shaded cone. That is
   a fidelity choice, and lines are legible at any zoom without lighting,
   which a thin cone is not.
```

### B — the presentation takes the glyph structs and resolves positions itself

```text
-  It would need the MESH to turn a NodeId into a point, and the OCCT
   adapter holding a mesh is exactly what P16-VIZ-001's design forbids:
   "IT HOLDS NO MESH ... Every engineering question is answered by asking
   the canonical mesh, which this class has no reference to."
-  The arrow geometry would then be untestable without a GPU.
```

### C — typed primitives (arrow, symbol) passed to the presentation

```text
+  The adapter could draw a nicer arrowhead.
-  The barb geometry, the perpendicular basis and the symbol shape move
   INTO the OCCT adapter, where a headless test cannot reach them -- and
   they are the part most likely to be wrong.
-  Two primitive kinds to keep in step for no correctness gain.
```

## Decision

**Candidate A.** The headless layer resolves and emits segments; the adapter
draws them.

```cpp
Result<StructuralOverlay> loadArrowOverlay(mesh, arrows, scale, reference);
Result<StructuralOverlay> reactionArrowOverlay(mesh, arrows, scale, reference);
Result<StructuralOverlay> restraintSymbolOverlay(mesh, restraints, numbering, barLength);
```

and a third presentation kind, `PresentationKind::Overlay`, with
`Viewer::displayOverlay`. Named `Overlay` rather than `StructuralOverlay` so
the KIND and the struct it depicts do not share a name.

`restraintSymbolOverlay` takes the `MeshDofMap` because the per-node component
mask CANNOT be derived from the restraint resolutions -- `RestraintResolution`
carries a node COUNT and never a node list. That was found as a defect in
already-qualified code while this ADR was being implemented; see
docs/verification/P17-VIZ-001/DEFECT_RESTRAINT_COMPONENTS.md.

### The conventions, fixed here so a test can hold them

```text
AN ARROW'S HEAD IS AT THE POINT OF APPLICATION, and its tail is upstream:
    tail = position - length * direction
    head = position
  so the arrow points AT the loaded or restrained node. Both conventions
  exist in the field; this one is chosen because "the force acts HERE" is
  what a reader needs from a load arrow, and because it makes the head
  coincide exactly with a canonical node position -- which a test can
  compare bit for bit.

FIVE SEGMENTS PER ARROW: one shaft and FOUR barbs, in two perpendicular
  pairs, so the head reads from any viewing direction. A two-barb head
  disappears edge-on.

A RESTRAINT SYMBOL IS ONE BAR PER CONSTRAINED COMPONENT, centred on the
  node and aligned with that axis. So the BAR COUNT IS THE CONSTRAINED DOF
  COUNT and the bar directions are which ones -- both directly assertable,
  and neither invents a support-triangle glyph whose meaning a reader would
  have to be taught.

A ZERO VECTOR PRODUCES NO ARROW, not a degenerate one. `hasDirection` is
  already false for it in the data layer, and an arrow of zero length with
  an arbitrary direction would be a drawn claim about a direction that does
  not exist.

AND A NON-POSITIVE DISPLAY LENGTH IS A SECOND, DIFFERENT CAUSE, counted
  separately as `tooSmall`. `displayArrowLength` is scale * |magnitude| /
  reference, which UNDERFLOWS TO ZERO for a small enough force against a
  large enough reference -- the force still HAS a direction, so it is not
  skipped as a zero, and a zero-length shaft collapses all four barbs onto
  one point. Two counters because the remedies differ: `skipped` is the
  model's load, `tooSmall` is the caller's scale.

THE BARB BASIS SEEDS FROM THE AXIS LEAST ALIGNED WITH THE SHAFT, so the
  cross product is never near-degenerate. A fixed seed axis collapses the
  barbs of any arrow pointing along it, and an axial load is the most
  ordinary case there is.

EVERYTHING IS SI METRES, converted once in the adapter by `toModelLength`.
```

## Consequences

```text
The overlay is DERIVED AND DISPOSABLE, like MeshView: a new scale means a
new overlay, and there is no in-place update.

It carries no identity. A segment list cannot be mistaken for a result, and
a caller that wants the force asks the glyph.

Position resolution REFUSES rather than skips: a NodeId the mesh does not
hold is an error, not an arrow quietly dropped -- the same rule
`triangleCornerBands` follows, and for the same reason.

LoadGlyph stays undrawn and its name stays misleading. Recorded rather than
renamed: renaming a qualified public type for a documentation reason is not
worth a requalification, and the header says what it holds.
```
