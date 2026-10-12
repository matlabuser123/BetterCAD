# P17-VIZ-001 — the display surface

```text
STATUS:  the DEFORMED display and the NODAL contours are built, measured and
         mutation-probed. The ELEMENT contours are BLOCKED on a mapping that
         `meshing` does not have, and that blocker is a scope decision
         rather than a defect.

         ONE REAL DEFECT was found here and fixed: a stale CONTOURED mesh
         drew its contour and hid the stale colour, which breaks the
         brief's hardest requirement. It aborted a qualification run.

         Qualification of this increment: see qualification-display/.
```

Two decisions, each with its own ADR:

| | |
| --- | --- |
| [ADR-043](../../architecture/decisions/ADR-043-a-deformed-display-is-an-override-position-buffer-through-the-one-existing-conversion.md) | a deformed display is an override position buffer through the **one** existing conversion |
| [ADR-044](../../architecture/decisions/ADR-044-a-contour-is-a-colour-per-facet-corner-and-an-element-channel-needs-a-mapping-meshing-does-not-have.md) | a contour is a colour per facet **corner**, and an element channel needs a mapping `meshing` does not have |

## The deformed display

The change to the qualified P16 presentation is **one line**: the position span
is the override buffer when it is non-empty, and `view.positions()` otherwise.
Everything below it — the winding, the normals, the edge buffer, the millimetre
conversion, the selection array — is the undeformed path unchanged, so the two
cannot drift apart.

### Measured: it reaches model space at the right scale

`StructuralViz_ADeformedMeshOverlaysItsOwnCadSolidAtTheSameScale`, at
320 × 240, isometric, `fitAll`:

| displayed | covered pixels |
| --- | --- |
| the CAD solid alone | **42036** — asserted `> 0` first, so the equalities are not four zeros agreeing |
| the **deformed** mesh alone | **42036** |
| the solid **and** the deformed mesh | **42036** |
| the undeformed mesh alone | **42036** |

All four exactly equal. P16-VIZ-001 measured **32674** and **54120** for the
defect this shape exists to catch — one silhouette, two scales. A boundary mesh
of a solid has the same silhouette as the solid, so from a fixed camera a scale
error between them gives different numbers and a factor of 1000 gives a speck.

Two more pin the ends of the range:

```text
scale 0      the image is BYTE-IDENTICAL to the undeformed one, and the
             buffer is first asserted equal to view.positions() element by
             element -- so the case cannot pass for the wrong reason

scale 2e4    the image DIFFERS, and the buffer's largest coordinate change is
             measured as > 1e-3 m BEFORE anything is drawn, so a failure is
             about the display rather than the arithmetic
```

Without the second, the first would be satisfied by a path that ignored its
buffer entirely — which is exactly what probe D1 does, and it is killed.

### The edge buffer, found by inspection

The presentation builds **two** buffers from the position span: the facet
corners and the edge endpoints. Nothing proved they move together, because
every other deformed case draws either `Shaded` (no edges) or
`ShadedWithEdges`, where the facets moving changes the image regardless.

`StructuralViz_ADeformedDisplayMovesItsEDGESToo` draws **Wireframe**, so the
only thing on screen is the edges. Probe D3 — the edges keeping the undeformed
shape — is killed by it.

This one was found by reading the constructor, not by a failure, and the test
was written before the probe ran.

## The contours

### The measurement that had to come first

Per-vertex colour is an OCCT **array attribute** while the facet colour is a
**group aspect**. Whether the attribute wins was a question, so it was measured
rather than assumed — `StructuralViz_AContourReachesTheScreenAsManyColours`, on
the same mesh from the same camera:

| | distinct non-background colours |
| --- | --- |
| uniform fill | **1** |
| 12-band contour | **4807** |

The attribute wins. And **4807 rather than 12** is the second thing the number
says: the GPU is *interpolating* between band colours across each facet, which
is what a nodal channel requires. Twelve bands drawn flat would show twelve.

Clearing the contour returns a **byte-identical** image to the uniform one,
which says it added no state that outlives it: no changed aspect, no changed
style, no moved geometry.

### One colour per facet CORNER, which keeps the two domains honest

```text
ELEMENT  all three corners of a facet take the owning element's band, so the
         facet draws FLAT. Element-constant stress IS constant over the
         element, and interpolating it would invent a variation.

NODAL    each corner takes its own node's band. The interpolation across the
         facet is the element's LINEAR SHAPE FUNCTION -- where a nodal field
         is actually defined -- so it is not an invention either. Nothing
         reads an element value and nothing combines two nodes, which is the
         "do NOT silently average stress to nodes" rule kept by construction.
```

`StructuralViz_ANodalChannelColoursEachCornerFromItsOwnNode` compares every
corner's band against an independent lookup — the view's own
vertex-to-node bridge plus a linear scan of the field's id array — and then
asserts the thing that distinguishes a nodal contour from an element one:

```text
at least one facet must span TWO bands

otherwise every corner happened to agree, and the per-corner mapping would be
indistinguishable from a per-facet one. The count is REQUIREd, not printed.
```

### The documented order, checked rather than trusted

The entity lookup is a **binary search**, so an unsorted id array would return
a *wrong index* rather than an error — the quiet kind of wrong. `ScalarField`
documents its arrays as ascending; `triangleCornerBands` checks it and refuses,
and a test swaps two ids to prove the check fires.

## The element contours are blocked

Diagnosed from a real failure, not predicted:

```text
band_out_of_range: element 59 owns render triangle 0 and channel von_mises
has no value for it
```

```text
MeshView::volumeBoundaryOf builds from the mesh's Triangle3 SURFACE elements,
so elementOfTriangle returns a TRIANGLE's id.

An element channel's values are per TETRAHEDRON.

The two id spaces are disjoint. meshing::Triangle carries `id`, `nodes` and
`region` -- no owning tetrahedron -- and no accessor in meshing answers for
one.
```

So it needs a **new capability in the qualified P16 meshing module**: a
triangle-to-owning-tetrahedron mapping, a topological search over sorted node
triples. That is a scope decision and it touches a qualified module, so it is
not bolted on from the renderer.

**It is refused by name rather than coloured from a mismatched lookup.**

### And the element path is correct, which is a different statement

`StructuralViz_AnElementChannelNeedsTheOwningTetrahedron` proves the
distinction instead of asserting it. The same function and the same field,
given a `MeshView::tetrahedraOf` view whose triangles really do name
tetrahedra:

```text
every facet FLAT          all three corners share a band, asserted per facet
the band is the owner's   compared against an independent scan of
                          field.elements
more than one band in use so "every facet flat" is not "every facet the same
                          colour"
```

What is absent is the mapping, not the colouring. Without this test, "element
contours do not work" would be indistinguishable from "the element code path is
broken", and the second would have sent the next session looking in the wrong
place.

## Refusals, and why each is a refusal rather than a repair

```text
a deformed buffer of the wrong length        refused: a short one would be
                                             read past its end and a long one
                                             means a different mesh
an EMPTY deformed buffer                     refused with its OWN code: the
                                             presentation reads an empty span
                                             as "undeformed", so it would
                                             silently draw the undeformed mesh
                                             through the deformed entry point
                                             -- indistinguishable from a solve
                                             that produced no displacement
a contour buffer of the wrong length         refused: a contour over some
                                             facets and the uniform colour
                                             over the rest is a picture that
                                             reads as data
a colour component outside [0, 1], or NaN    refused, NOT clamped:
                                             contourCornerColours cannot
                                             produce one, so reaching it means
                                             the buffer did not come from it,
                                             and clamping would draw a
                                             plausible picture from an
                                             implausible input
a selected facet during a contour            keeps the HIGHLIGHT colour, not
                                             its band -- `facets()` takes a
                                             withColours flag the highlight
                                             group passes as false
```

The guard **order** mattered and the first version had it wrong: with the
length check first, an empty buffer is simply a length mismatch and reports
`InvalidArgument`, so the specific diagnostic could never be reached. A test
asserting the specific code caught it, and probe D6 now measures it.

## Mutation probes

22 across two batches -- 8 for the deformed display, 14 for the contour --
run against the whole `StructuralViz_` set **and** P16's own
`unit.MeshDisplay` set, because a mutation in a qualified P16 file ought to be
caught by P16's tests too, and if it is not, that is worth knowing.

```text
the deformed display    8 probes, 6 KILLED, 2 EQUIVALENT MUTANTS -- flaws in
                        the probes, demonstrated as such rather than asserted
the contour display    14 probes, 14 KILLED, 0 survived, after FOUR real gaps
                        were closed
```

Full table and the reasoning in [MUTATION.md](MUTATION.md). D4 is the one to
note besides D3: removing `toModelLength` from the facet corners kills **six**
tests, and they are P16's own `MeshDisplay` cases — the ones that found the
original metres-versus-millimetres defect. The single conversion is still
guarded by the tests that earned it.

## The defect that aborted a qualification

**Found by asking an adversarial question about an interaction, while the first
qualification run was already in flight.** It is the most serious thing this
milestone produced, and it nearly shipped with a tick beside it.

### What it was

`MeshPresentation::Compute` sets the plain facet group's interior colour to
`staleFacetColour()` when the mesh is stale. But the 1-versus-4807 measurement
above establishes that **per-vertex colours override a group aspect's interior
colour.**

So a stale **contoured** mesh drew its contour and swallowed the stale colour.
In `MeshStyle::Shaded` there was then no stale indication whatsoever; in
`ShadedWithEdges` only the edges changed, while the facets — the dominant
visual — showed a contour as though the result were current.

That breaks the requirement the brief calls its hardest:

```text
a stale result must never be presented indistinguishably from a current one
```

and it made the tick on **"Clear stale-result visual state"** wrong for the
contoured case — a false checkbox, which is the failure this project guards
against above every other.

### Measured, in both directions

`StructuralViz_AStaleContourIsNotDrawnAtAll`, drawn `Shaded` so that nothing
but the facets can carry the distinction — which is the case the defect hid in.

| | distinct non-background colours |
| --- | --- |
| current, contoured | **4807** |
| stale, contoured, **with the defect** | **1535** |
| stale, contoured, **fixed** | the stale uniform image, byte-identical |

1535 is the defect quantified: a stale contoured mesh was showing over a
thousand distinct colours — unmistakably a contour — where it should have
shown the stale appearance. The stale aspect's material tinted the lighting,
which is why it is not 4807, and that tint is exactly the "quiet" distinction
the brief forbids.

### The fix, and why it is a drop rather than a tint

```cpp
facets(plain, !stale_)   // was facets(plain, true)
```

A stale contour is not merely mislabelled, it is **wrong data**: the colours
are derived from a result that no longer describes the model. So the contour is
dropped, the stale facet colour reaches the screen, and the image becomes the
stale uniform one — asserted byte-identical.

The colours are **retained, not discarded**: `meshHasContour` still answers
true while stale, and clearing stale restores the bands. A UI that marks a
result stale and then re-solves must not have to re-send them.

### What it cost, and the ordering lesson

The first qualification run was aborted 15 minutes in rather than finished —
see [qualification-display/ABORTED.md](qualification-display/ABORTED.md).

```text
abort now, fix, re-run     ~15 minutes lost
finish, fix, re-run        ~195 minutes lost, AND a PASS on record for a tree
                           containing a known defect
```

**The adversarial review belongs before the qualification, not beside it.**
This milestone already recorded that probes belong after the review for the
same reason; the same argument applies one stage later, and this is the second
time in one milestone that an expensive run was started too early.

A side effect worth noting, because it looked like a regression and was not:
the aborted run's **clean** stage had already deleted every build output, so a
later targeted build of `bettercad_tests` alone left `bettercad.exe` missing
and all ten `gui.*` tests failed with *"exit code no such file or directory"*.
Building the full default target restored 313/313. A partial build tree after
an aborted clean is not a defect, but it reads like one.


## Four more gaps, found by probing before qualifying

The contour display batch found four gaps in code that a qualification run was
already certifying. That run was aborted to probe first; the gaps are in
[MUTATION.md](MUTATION.md) with their reasoning, and the short version is:

```text
E4   a missing entity clamped to BAND 0 -- the bottom of the range, so an
     absent value would draw as the minimum. I nearly filed this as
     unreachable: it is unreachable on the production path and NOT on the
     function, because triangleCornerBands takes a raw MeshView and a raw
     ScalarField and ScalarField carries NO STAMP.

E9   a selected facet taking its band colour instead of the highlight. A
     contract written in the Viewer header with nothing asserting it, and the
     first test I wrote for it was TRUE and still proved nothing -- the
     mutation satisfied it, because the two groups carry different materials
     so the lighting differs anyway.

E12  the contour length guard, and
E13  the out-of-range colour component guard: both written, both documented,
     neither exercised. The display case only ever handed setMeshContour a
     buffer contourCornerColours had just produced, which is by construction
     the right length and in range. A happy path cannot test a guard that
     exists for the unhappy one.
```

### What E9 cost, and the rule it refined

Two attempts. The first asserted "a highlight changes the image" — true, and
the mutation satisfied it. The decisive form highlights **every** triangle, so
the plain group empties and the whole mesh is drawn by the highlight group:

```text
correct   withColours=false -> ONE uniform colour
mutated   withColours=true  -> the contour, thousands of colours
```

Orders apart rather than a lighting difference.

**A true assertion that the mutation also satisfies is not a test of the
contract.** That is the C20 disjunction lesson in a new shape: there the
assertion was too weak to discriminate, here it discriminated the wrong thing.

### And the fixture for E4 had to be chosen, not assumed

Crossing two meshes was the obvious instrument and is unreliable: two meshes
can number their nodes over the same range, so the lookup would **succeed with
the wrong value** — which a test cannot distinguish from success. Removing one
entry from a real field is deterministic, keeps the field complete and sorted
so only the entity-presence check can fire, and names the node in the
diagnostic.

## What the display surface still does not have

```text
THE ELEMENT CONTOURS            blocked as above
THE LOAD AND RESTRAINT GLYPHS   the data is built, tested and qualified --
                                LoadGlyph, NodalLoadArrow, RestraintGlyph,
                                ReactionArrow, with sign and scale each
                                mutation-killed -- and nothing draws them.
                                They need NO new capability.
THE Qt INSPECTOR SURFACE        no panel, no channel selector, no legend
                                widget, no stale badge, no navigation
                                buttons.
```

The legend is worth a note: `ContourLegend` carries the band boundaries, the
channel's SI unit and the range, and `partitionsItsRange()` proves the bands
tile it. Drawing it is a Qt widget over data that is already complete and
qualified — there is no arithmetic left in it.
