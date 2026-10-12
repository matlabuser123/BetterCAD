# ADR-044 — A contour is a colour per facet corner, and an element channel needs a mapping meshing does not have

```text
STATUS:   Accepted for the colour-per-facet-corner decision.
          ITS SECOND CLAIM IS SUPERSEDED BY ADR-046.
DATE:     2026-10-12
CONTEXT:  P17-VIZ-001, the contour display
```

> **SUPERSEDED, IN PART.** This ADR's title says an element channel "needs a
> mapping meshing does not have". **It does not.** The diagnosis below is
> correct in every particular -- a Triangle3 boundary names triangles, an
> element channel is per tetrahedron, and `meshing::Triangle` carries no owner
> -- but the conclusion that closing the gap needs a new capability in the
> qualified P16 meshing module was wrong. The question only arises if the
> boundary is taken from the Triangle3 elements; build it from the tetrahedra
> and ownership is known by construction. See
> [ADR-046](ADR-046-a-tetrahedron-derived-boundary-view-carries-its-owning-element-so-no-meshing-capability-is-needed.md).
>
> **The colour-per-facet-corner decision stands unchanged**, and is in fact
> what lets one mechanism serve a nodal and an element channel honestly.

## Context

`StructuralContour` bands a `ScalarField` into colours, qualified and
mutation-tested. Nothing drew them. The seven contour checkboxes split by the
channel's **domain**, and the brief is explicit that the two must not be
conflated:

```text
NODAL    displacement magnitude, Ux, Uy, Uz   values at NODES
ELEMENT  von Mises, normal, shear components  values CONSTANT over a tetrahedron
```

"Do NOT silently average stress to nodes" is a hard rule for this milestone, so
the representation has to keep the two honest rather than flatten them into one.

## The decision: one colour per facet CORNER

`MeshPresentation::corners_` is **non-indexed** — nine doubles per facet, each
facet carrying its own three corners. So a colour per corner is expressible
without any vertex sharing between facets, and it serves both domains with one
mechanism:

```text
ELEMENT  all three corners of a facet take the owning element's band, so the
         facet draws FLAT. Element-constant stress IS constant over the
         element, and interpolating it would invent a variation.

NODAL    each corner takes its own node's band, and the interpolation across
         the facet is the element's LINEAR SHAPE FUNCTION -- which is where a
         nodal field is actually defined, so it is not an invention either.
         Nothing reads an element value and nothing combines two nodes.
```

A per-**triangle** index was the obvious alternative and is wrong: it cannot
express the nodal case without picking one of three nodes, and picking is a
lie. A per-**vertex** array over `MeshView::positions()` is also wrong, because
that buffer is indexed and a vertex shared by facets of two different elements
cannot carry two colours.

**Where flat-versus-smooth is decided is the point of the design.** It lives in
`renderer::triangleCornerBands`, headless and tested, not in the OCCT adapter
and not in a UI.

## The measurement that had to come first

Per-vertex colour is an **array attribute** in OCCT while the facet colour is a
**group aspect**, and whether the attribute wins was a question, not an
assumption. So it was measured before anything was claimed:

```text
the same mesh, Shaded, isometric, fitAll, 320 x 240

distinct non-background colours, uniform fill      1
distinct non-background colours, 12-band contour   4807
```

One versus 4807. The attribute wins, and 4807 also confirms the GPU is
*interpolating* between band colours across each facet rather than flat-filling
— twelve bands drawn flat would show twelve colours, not thousands. That is the
shape function doing its job.

Clearing the contour returns a **byte-identical** image to the uniform one,
which says the contour added no state that outlives it: no changed aspect, no
changed style, no moved geometry.

## The consequence left open: element channels on a boundary view

Implementing this surfaced a gap that is **not** in this milestone's reach.

```text
MeshView::volumeBoundaryOf builds from the mesh's Triangle3 SURFACE elements,
so elementOfTriangle returns a TRIANGLE's id.

An element channel's values are per TETRAHEDRON.

The two id spaces are disjoint. meshing::Triangle carries `id`, `nodes` and
`region` -- no owning tetrahedron -- and no accessor in `meshing` answers for
one.
```

So a boundary-view element contour needs a **new capability in the qualified
P16 meshing module**: a triangle-to-owning-tetrahedron mapping, which is a
topological search over sorted node triples and deserves its own design rather
than an inline helper reaching across a module boundary from the renderer.

**It is refused, by name, rather than coloured from a mismatched lookup** — the
error names the element it could not find a value for.

### And the element path is correct, which is a different statement

`StructuralViz_AnElementChannelNeedsTheOwningTetrahedron` proves the
distinction rather than asserting it: the same function and the same field,
given a `MeshView::tetrahedraOf` view whose triangles really do name
tetrahedra, produce flat per-facet bands that match an independent lookup, with
more than one band in use.

**What is absent is the mapping, not the colouring.** Recording that difference
is the reason the test exists; without it, "element contours do not work" would
be indistinguishable from "the element code path is broken".

## Consequences

```text
The four NODAL contour boxes are end to end and ticked.
The three ELEMENT ones are blocked and say why, in TODO.md.

setMeshContour refuses a buffer of the wrong length, and refuses a component
that is not finite and within [0, 1] rather than clamping it -- because
contourCornerColours cannot produce one, so reaching that means the buffer did
not come from it, and clamping would draw a plausible picture from an
implausible input.

A SELECTED FACET KEEPS THE HIGHLIGHT COLOUR, not its band: `facets()` takes a
`withColours` flag that the highlight group passes as false. Otherwise
selecting something during a contour display would do nothing visible.

The contour is NOT the result. It is a colour per corner, and a caller that
wants the number asks the field.
```
