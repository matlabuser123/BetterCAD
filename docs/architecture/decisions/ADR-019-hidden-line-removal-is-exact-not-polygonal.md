# ADR-019 — Hidden-line removal is exact, not polygonal

```text
Status:    Accepted
Date:      2026-09-23
Milestone: P14-HLR-001
Builds on: ADR-011 (drawing intent is canonical, projection is derived)
           ADR-012 (a drawing references only what ADR-004 permits)
           ADR-013 (view orientation and drawing scale)
           ADR-015 (the drawing module and its layer)
```

## Context

Every line on a mechanical drawing is the result of hidden-line removal. Until
now `P14-VIEW-001` projected **all** edges of a body with no visibility
classification at all, which it recorded as a deliberate scope boundary; and
`P14-VIEW-002` added sections on the same basis. So the decision made here
governs how every drawn line in BetterCAD is produced from now on, and what a
drawing is allowed to claim about itself.

Three things have to come out of it:

```text
correctness     a rear edge must never be drawn as a visible one
exactness       a drawn circle must be a circle, not a chain of chords
determinism     the same model and direction must classify identically
                across builds, presets and runs
```

The last one is not optional. `P14-VIEW-002` is qualified on comparisons that
are exact to the last bit — the same loop points, the same areas — and
`P14-DIM-001` will measure the geometry this produces. A drawing pipeline that
is merely *close* each time cannot support either.

There is also a hard constraint from the architecture: OCCT headers may only
appear in an `occt/` adapter under `src/`, and there is exactly one such
directory, `src/core/geometry/occt`. Whatever is chosen, the kernel call sits
in `geometry` (layer 0) and `drawing` (layer 4) consumes a BetterCAD type.

## Options

### 1. OCCT exact hidden-line removal — `HLRBRep_Algo`

Works on the real B-Rep. Produces exact curves: a projected circle is a
circle, a cylinder's silhouette is a line. Separates its answer into eight
sets — sharp, smooth, sewn and outline edges, each visible and hidden — which
is more classification than the milestone asks for and maps cleanly onto it.

```text
+ exact curves; a drawn circle IS a circle
+ true silhouettes on analytic surfaces, computed not approximated
+ the classification BetterCAD needs comes out of the kernel already separated
+ no meshing parameter, so nothing to tune and nothing to drift
- slower than polygonal, materially so on large assemblies
- can raise on difficult geometry, so every call must be guarded
```

### 2. OCCT polygonal hidden-line removal — `HLRBRep_PolyAlgo`

Works on the triangulation rather than the B-Rep. Fast, and robust on shapes
the exact algorithm struggles with.

```text
+ substantially faster, and more tolerant of awkward input
- output is POLYLINES. Every circle becomes a chain of chords, so a drawn
  circle is not a circle and cannot be dimensioned as one
- the result depends on the mesh deflection it was given, which makes
  "the same model draws the same lines" conditional on a tuning parameter
- a mesh is not guaranteed identical between builds or OCCT versions, so
  exact cross-preset comparison is off the table by construction
```

### 3. Write it in BetterCAD

A projection and occlusion pass of our own, over the geometry module's
existing edge and face queries.

```text
+ complete control over classification and provenance
- silhouettes of curved surfaces are the hard part and would have to be
  solved from scratch; they are most of the value
- occlusion between curved surfaces is a large, subtle problem with a long
  tail of degenerate cases
- it would be the least-tested code in the repository, competing with an
  implementation that has been in industrial use for decades
- P14 is a drawings milestone, not a research project
```

## Decision

**Exact hidden-line removal, `HLRBRep_Algo` with `HLRBRep_HLRToShape`,
behind `geometry::hiddenLineDrawing`.**

Polygonal HLR is rejected on determinism and exactness together. Either alone
would be arguable; together they mean a drawing could not be compared bit for
bit between presets and a dimension could not be attached to a real circle.
Speed is the wrong thing to buy with that.

Writing our own is rejected as scope. Silhouette generation on curved surfaces
is the substance of the problem, and there is no reason to believe a first
implementation would beat one the industry has used for decades — while the
milestone's actual obligations, classification semantics and determinism, are
ours either way.

BetterCAD keeps, and does not delegate:

```text
classification semantics    what "hidden", "smooth" and "outline" MEAN, and
                            the two-axis model below
canonical ordering          the kernel's traversal order carries no meaning
                            and is not stable enough to compare runs by
policy                      which classes a drawing shows, per view
failure handling            a kernel raise becomes a diagnostic, never a
                            partial drawing
tolerances                  what counts as coincident, and what counts as a
                            projected curve too short to draw
```

### The classification model is two axes, not one enum

The milestone brief lists seven possible states. They collapse to two
independent facts, which is smaller *and* says more:

```text
visibility   Visible | Hidden
kind         Sharp | Smooth | Outline | Sewn
```

The mapping, recorded because the brief asks for it:

| Brief's state | Here |
| --- | --- |
| Visible | `{Visible, Sharp}` |
| Hidden | `{Hidden, Sharp}` |
| Silhouette | `kind == Outline`, either visibility |
| TangentVisible | `{Visible, Smooth}` |
| TangentHidden | `{Hidden, Smooth}` |
| SuppressedByPolicy | not a state of an edge — the drawing layer omits it and reports how many |
| CoincidentMerged | not a state of an edge — the drawing layer merges and reports how many |

The last two are outcomes of a decision, not properties of geometry, and
putting them in the same enum as "hidden" would mean an edge's class depended
on which view was asking. `Sewn` is kept because the kernel distinguishes it
and a seam is genuinely not a feature of the shape.

### Merging and policy live in `drawing`, not `geometry`

`geometry::hiddenLineDrawing` reports what the kernel found, faithfully,
including coincident duplicates — and they are pervasive: a box seen square-on
comes back as four visible edges and four hidden edges at *identical*
coordinates, because its rear face projects exactly onto its front face.

Resolving that is a drafting rule, not a geometric one. ISO 128 gives line
precedence: a visible line takes precedence over a hidden line, which takes
precedence over a centre line. That is a standard about drawings, so it
belongs with the module that knows about drawings, next to the tangent-edge
policy and the hidden-line toggle, where all of a view's presentation
decisions can be read in one place.

## Consequences

- A section view classifies visibility **after** it is cut, because the cut
  solid is what `hiddenLineDrawing` is given. A drawing that classified the
  uncut solid would hide the very faces the section exists to show.
- HLR output carries **no back-reference to a model edge**. ADR-012 already
  records that this codebase has no stable edge name, and the kernel's output
  does not carry one back either. So a dimension references the MODEL and
  projects it; it never references a drawn line. `P14-DIM-001` inherits that,
  and it is a consequence of ADR-012 rather than a new limitation.
- The projected curve's type is the *drawn* curve's type, which is not always
  the model edge's: a circle seen at an angle projects to an ellipse, and a
  circle seen edge-on projects to a straight segment the kernel still
  describes as a degenerate conic. Reported as it is, not as the model edge
  was.
- Exact HLR is the slower algorithm, and on a large assembly it will show.
  Nothing here caches: ADR-011 and ADR-014 make drawing geometry derived and
  built on demand, so if it becomes a problem the answer is a cache with an
  explicit invalidation rule, decided then and not assumed now.
