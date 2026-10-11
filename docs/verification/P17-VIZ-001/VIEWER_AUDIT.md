# P17-VIZ-001 — the viewer audit, done before any production code

The brief's first requirement: inspect what exists before building, and do not
create a second mesh rendering stack if P16 already provides one.

**It does, and it provides most of this milestone.** The audit's headline is
that P16-VIZ-001 and P16-MAP-001 already solved the unit boundary, the render
cache rule, the render-index→identity bridge, the stale-mesh visual state and
boundary highlighting — including the drilled-hole case. What is genuinely
missing is the structural half: result channels, a scalar classifier, deformed
positions, result currentness, and the overlays.

## Capability table

| Capability | Existing API / type | Units | Owner | Reusable for P17? | Decision |
| --- | --- | --- | --- | --- | --- |
| Mesh render buffers | `renderer::MeshView` — positions, triangle and edge indices | **SI metres** | P16-VIZ-001 | **Yes, fully** | Reuse. Undeformed display is `MeshView` unchanged. |
| Render cache keying | `MeshView::stamp()`, `describes(mesh)` | — | P16-VIZ-001 | **Yes** | Reuse the pattern for the result view. |
| Render index → identity | `nodeOfVertex`, `elementOfTriangle`, `vertexOfNode`, `trianglesOfElement` | — | P16-VIZ-001 | **Yes, fully** | Reuse. This is selection provenance; no spatial search is needed anywhere. |
| m → mm conversion | `toModelLength()` → `geometry::occt::toModel(Length::fromSi(x))` in `OcctMeshPresentation.cpp` | m → mm | P16-VIZ-001 | **Yes** | Reuse. The project's only conversion; deformed positions go through the same one. |
| Mesh stale visual state | `MeshVisualState`, `MeshStatus`, `statusOf(document, holding)` | — | P16-VIZ-001 | Pattern only | The mesh analogue. The RESULT needs its own, derived from P17-DATA. |
| Stale draw style | `MeshDrawStyle`, `staleFacetColour()`, `staleEdgeColour()` | — | P16-VIZ-001 | **Yes** | Reuse the desaturated-stale convention. |
| Quality report binding | `MeshQualityView` — stamp-bound report, `worstElementFor`, `classOf` | per metric | P16-VIZ-001 | Pattern only | The closest analogue to a contour classifier, but it classifies into four `QualityClass` buckets under a threshold policy, **not a continuous scalar range**. |
| Boundary highlighting | `highlightFor(view, mesh, map, FaceName / NamedBoundarySet / faceIndex)` → `MeshHighlight` | — | P16-MAP/VIZ | **Yes, fully** | Reuse. Carries the mapping's `MappingState` whole, so Unresolved stays distinguishable from empty. |
| Drilled-hole wall | the `faceIndex` overload, which exists precisely because a bored hole's wall has **no `FaceName`** | — | P16-MAP-001 | **Yes** | Reuse. No viewer-side geometric recognition is needed or permitted. |
| Facet → CAD face | `sourceOfFacet(mesh, map, facet)` → `FacetSource` | — | P16-MAP-001 | **Yes** | Reuse for facet provenance. |
| Scalar colour / legend | **NONE** | — | — | — | **Must build.** No continuous scalar range, no bucket map, no legend exists. |
| Deformed positions | **NONE** | — | — | — | **Must build**, as a derived display buffer. |
| Result channels | **NONE** | — | — | — | **Must build** a channel descriptor over the qualified result. |
| Result currentness | `resultCurrency`, `analysisState`, `staleReasons` | — | P17-DATA-001 | **Yes, as the source of truth** | Consume. No GUI stale flag (brief item 69). |
| Load / restraint / reaction overlays | **NONE** | — | — | — | **Must build**, consuming canonical intent and `SupportReactions`. |
| Extreme navigation | `MeshQualityView::worstElementFor` (quality only) | — | P16-VIZ-001 | Pattern only | **Must build** for result channels. |

## The unit boundary, as it already stands

Frozen and already centralized. `OcctMeshPresentation.cpp` carries the
reasoning verbatim, including how the defect was found:

```text
MeshView::positions()        SI metres
OCCT model space             MILLIMETRES
conversion                   geometry::occt::toModel(Length::fromSi(x))
                             "the project's only one"
```

P16-VIZ recorded that a mesh uploaded in metres beside a CAD body in
millimetres is **1000 times too small**, that it looked perfect displayed alone
because `FitAll` framed the only thing in the scene, and that the same box
measured 32674 pixels as a mesh and 54120 with the solid added — "one
silhouette, two scales".

**Decision: the deformed buffer converts at the same boundary and nowhere
else.** `x_view_mm = 1000 * (x_m + s * u_m)`, with the multiplication by 1000
performed only by `toModelLength`. No `* 1000.0` is added anywhere in P17-VIZ.

## The finding that changes a predecessor

**The published result cannot supply von Mises or principal stresses without
recomputation, and recomputation in the GUI is forbidden.**

```text
StructuralResult carries    displacements, reactions, strains (Strain6),
                            stresses (Stress6)
it does NOT carry           von Mises, principal stresses, principal strains,
                            hydrostatic stress, |u|
```

The brief forbids a second von Mises formula (item 16) and a second
eigensolver (item 17). `structural::vonMisesStress()` and
`principalStresses()` are public, so the GUI *could* call them — but that is
the renderer running a stage of the pipeline, and the brief's own wording is
that P17-VIZ **reads** `elementResult.vonMises`.

`RecoveredFields` already holds every one of them, as stored values:

```text
displacements()   NodalDisplacement{node, displacement, magnitude}   -- |u| stored
elements()        ElementFields{element, strain, stress,
                                principalStress, principalStrain,
                                vonMises, hydrostatic}
displacementOf(mesh, node)   identity-safe lookup, refuses a foreign mesh
fieldsOf(mesh, element)      identity-safe lookup, refuses a foreign mesh
describes(mesh)              the revision guard
mesh()                       MeshStamp
source()                     AssemblySource
largestDisplacementMagnitude(), largestVonMises()
```

**`RecoveredFields` is exactly the contour source this milestone needs** — every
channel, identity-safe lookups, a stamp guard, and two extremes already
computed. Nothing requires recalculation.

But `solveStructuralAnalysis` computes it, extracts `Strain6`/`Stress6` into the
`StructuralResult`, and **discards it**.

```text
DECISION    StructuralSolveOutcome gains `RecoveredFields fields`.
            Additive; the value is already computed one statement earlier.
            RecoveredFields deletes no copy or move operation, so this costs
            a move.

WHY NOT     have the GUI call recoverFields() itself -- that is the renderer
            executing a pipeline stage, and it would compute the same numbers
            a second time from the same inputs, which is the duplicate
            authority the milestone exists to prevent.

OWED        P17-VALID-001 requalification. The struct is P17-VALID's, so its
            suite re-runs, and `unit\.Structural` is in the repeat set.
```

## What P17-VIZ must build, and nothing else

```text
StructuralResultView        a derived, disposable, SOURCE-KEYED render cache
                            over RecoveredFields + SupportReactions:
                              deformed positions (display only)
                              per-channel scalar extraction
                              classification with explicit cardinality
                              extremes per channel

ResultVisualState           the result analogue of MeshVisualState, DERIVED
                            from P17-DATA's resultCurrency/analysisState.
                            No GUI flag of its own.

channel descriptor          channel -> domain (node|element) -> unit ->
                            display name -> extractor. One table, no
                            scattered switches.

scalar range + classifier   min/max/argmin/argmax over the EXACT classified
                            entities, with expected/classified/skipped counts
                            reported. Signed ranges preserved. Uniform and
                            empty handled explicitly.

overlays                    load arrows, restraint symbols, reaction arrows --
                            all from canonical intent and qualified reactions,
                            with ID provenance, no re-integration.
```

Explicitly NOT built: a second mesh render stack, a stress smoother, a nodal
averaging rule, a geometric face classifier, a GUI-owned editable result copy.

## Layering

```text
renderer   70        may include structural 50 and meshing 40
structural 50        may NOT include renderer -- enforced by
                     tests/architecture/CheckLayering.cmake, which fails the
                     build
Qt                   apps/bettercad/ and src/renderer/ only
OCCT .hxx            src/**/occt/ only
```

`src/renderer/CMakeLists.txt` currently links `core`, `geometry`, `meshing`. It
gains `BetterCAD::structural`. No structural source file gains a renderer,
Qt or OCCT include, which the layering test proves rather than the review
asserting it.
