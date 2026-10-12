# P17-VIZ-001 — the load and restraint glyphs

```text
STATUS:  built, measured, probed and QUALIFIED. ADR-045.
         3825 / 3825 x 3 presets, 970 x 5, 3h21m00s -- FREEZE_GLYPHS.md.
         The adversarial review found FIVE gaps before a single probe ran.
         One DEFECT in committed, qualified code was found on the way in --
         see DEFECT_RESTRAINT_COMPONENTS.md.
```

## The thing that shaped the design

**None of the four glyph structures carries a position.**

```text
NodalLoadArrow   a NodeId and a Force3D
ReactionArrow    a NodeId, a Force3D and the constrained mask
RestraintGlyph   a RestraintId, a component mask and three counts
LoadGlyph        a LoadId, a kind, a resultant, an area, an element count
```

That is deliberate — the data layer computes no geometry — so the drawing
layer has to **resolve** positions, and every one here comes from
`Mesh::findNode`. There is no spatial search, no centroid fit and no inferred
location anywhere in `StructuralOverlay.cpp`.

ADR-045 puts that resolution in the headless layer because it is the part most
likely to be wrong: the direction, the length, the barb basis, the bar count.
The adapter receives points.

### What is drawable, and what is not

`LoadGlyph` is **not** drawn. A pressure or a traction has no single point and
gravity has no location at all — `LoadGlyph` is a summary for a panel
(resultant, area, element count), and its name promises more than its contents
hold. A per-facet pressure arrow needs facet centroids through
`GeometryMeshMap` and is a separate, larger piece; it is named here rather than
half-built.

## The conventions, fixed so tests can hold them

```text
AN ARROW'S HEAD IS AT THE POINT OF APPLICATION
  tail = position - length * direction, head = position
  so the head coincides EXACTLY with a canonical node position, and the test
  compares `shaft.to == node->position` bit for bit rather than within a
  tolerance.

FIVE SEGMENTS PER ARROW: one shaft and FOUR barbs, in two perpendicular
  pairs, because a two-barb head vanishes edge-on and an overlay that
  disappears at some camera angles is worse than none.

THE BARB BASIS SEEDS FROM THE AXIS LEAST ALIGNED WITH THE ARROW, so the
  cross product is never near-degenerate. A fixed seed collapses the barbs of
  any arrow pointing along it -- and an axial load is the most ordinary case
  there is.

A RESTRAINT SYMBOL IS ONE BAR PER CONSTRAINED COMPONENT, centred on the node
  and aligned with that axis. So the BAR COUNT IS THE CONSTRAINED DOF COUNT
  and the bar directions are which ones -- both directly assertable, and
  neither invents a support-triangle glyph a reader would have to be taught.

A ZERO FORCE DRAWS NOTHING and is counted as `skipped`.
A NON-POSITIVE DISPLAY LENGTH draws nothing and is counted as `tooSmall`.
  Two counters, because the remedies differ: the first is the model's load,
  the second is the caller's scale. `glyphs + skipped + tooSmall` is the
  number of glyphs given, and that invariant is asserted.

EVERYTHING IS SI METRES, converted once by `toModelLength` in the adapter.
```

## Measured

```text
the arrows on screen, at a fixed camera
  mesh + overlay    36586 px
  mesh alone        36308 px
  the arrows         278 px

the overlay alone, framed by fitAll
  NO PIXEL COUNT IS RECORDED HERE, because none was measured. That case
  asserts a RELATION and prints the counts to the log:
        0 < arrows_alone < mesh_alone
  which says the arrows reach the screen AND that they are line segments
  rather than a second solid. A specific figure would be an invented one.

picking through the overlay
  the same triangle with and without it, so the annotation never comes
  between the user and the element they clicked
```

### Why the arrows are only 278 px, and why that is not tuned away

The convention is head-at-the-node with the tail **upstream**. This fixture's
load is tensile — `+z` on the `+z` end cap — so the shaft runs back **into**
the solid and the depth buffer hides it. What shows is the head and the barbs
splaying past the cap.

That is a readability consequence of the convention, recorded rather than
hidden. The standard depiction keeps a load arrow outside the body, which
needs the surface normal and is a change to ADR-045 rather than a tweak. The
test's floor of 50 px is set far above what a thousand-times-small overlay can
contribute and far below the measured 278; it discriminates the **scale**,
which is what that case is for, and it does not claim the arrows are
prominent.

## The adversarial review, before any probe ran

Five findings. Two were proved by measurement rather than argued.

### 1 — a degenerate arrow was reachable

`displayArrowLength` is `scale * |magnitude| / reference`, which **underflows
to zero** for a small enough force against a large enough reference. The force
still has a direction, so it was not skipped as a zero — and a zero-length
shaft puts the tail on the head and collapses all four barbs onto the same
point, drawing five degenerate segments at a dot.

Measured: `displayArrowLength(1e-300, 1e300, 1.0)` is exactly `0.0`. Now
counted as `tooSmall`, and every emitted segment is asserted to have positive
length.

### 2 — the restraint bar's axis assignment was untestable

The first symbol test asserts each bar is axis-aligned, centred on a node, and
that the count matches the constrained DOF count. **With a fixed support all
three components are held, so any permutation of the axes satisfies all of
it.**

Proved rather than argued: permuting the axis table fails **exactly one** test,
the new one (probe G13). Only a partial restraint discriminates, so the new case holds Uz alone and requires at least one
partially restrained node for non-vacuity.

### 3 — picking through an overlay was unasserted

`ComputeSelection` is deliberately empty. Asserting that it adds nothing is
not the same as asserting that picking still **works**, and only the second is
a property a user has.

### 4 — an axis-aligned arrow could lose its barbs

Every arrow in every earlier test points along ±z, because that is what the
fixture's load does. A seed hard-coded to `unitX` would have worked for all of
them and collapsed the barbs of any arrow along x. The new case covers all
three axes, both directions, and a diagonal.

### 5 — a dropped millimetre conversion was undetected

The P16 trap again. Proved: dropping all six conversions fails **exactly one**
test — the new one. Every other test passes with the overlay a thousand times
too small.

## Two measurement mistakes, both mine

**The first scale discriminator was wrong.** It asserted that mesh-plus-overlay
covers *more* pixels than the mesh alone. Measured 42036 → **36586** —
coverage went *down*, because `fitAll` zooms out to fit the arrows and the
mesh's silhouette shrinks faster than thin segments add pixels. The arrows were
correct; the assertion was not. Fixed by holding the camera still: display
both, `fitAll` once, then hide the overlay with `setVisible`, which does not
move the camera, so the two images differ by exactly the arrows' pixels.

**Then the threshold was a guess**: five per cent, against a measured 0.77 per
cent. Replaced with a floor justified against both ends.

## A trap that made a mutation lie

Dropping all six conversions orphaned `toModelLength`, so `-Werror` failed the
build — and **ctest then ran the stale binary and reported 60/60 for a mutation
that never compiled.** A `(void)&toModelLength` reference turned it into a real
test verdict.

That is "a restored source is not a restored binary" in reverse: a *failed*
build leaves the previous binary in place, and the passes are meaningless. The
probe harness checks the build result before believing the tests.
