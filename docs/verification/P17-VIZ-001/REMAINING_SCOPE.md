# P17-VIZ-001 — what is NOT done

> **Addendum, P17-QUAL-001, 2026-10-15.** Written after the glyph run; the
> element-contour run (`FREEZE_ELEMENTS.md`, candidate `974b62b`, 3835/3835 x
> 3) then covered the three boxes this file says "stay unticked only until the
> three-preset qualification covers them", and `TODO.md` ticks all 23. The
> milestone is COMPLETE. What genuinely remains out of scope is unchanged and
> still true: the Qt inspector surface is not wired, `LoadGlyph` is data and
> not drawn, and there is no logarithmic scale.

This milestone is **incomplete**. This file exists so the gap is stated rather
than discovered, and so nothing in the other evidence documents reads as a
claim about work that has not happened.

**5 of TODO.md's 23 checkboxes are unticked, and `ROADMAP.md` is untouched.**

Three of the five are BLOCKED rather than unfinished, and the distinction is
tested rather than claimed -- see below.

## Done, qualified

```text
the read-only result adapter        StructuralResultView
  15 contour channels, one descriptor table, element-constant stress
  classification with explicit cardinality and refusals
  signed ranges, uniform fields, extremes with deterministic ties
  deformed positions, presentation-only scale, non-cumulative

the public numerical kernel         channelValue, absent-is-not-zero
  added by the adversarial review; closes a complete, uniformly zero field
  that all 19 earlier tests accepted

colour banding and the legend       StructuralContour
  equal-width bands that partition the field's range BITWISE -- one shared
  boundary array, so band i ends at exactly the double band i+1 begins at
  a uniform field, and a range too narrow to subdivide, each collapse to
  ONE band rather than N that imply a variation nothing can draw

currentness                         StructuralDisplayState
  P16's MeshCurrency and P17-DATA's AnalysisState, side by side,
  never merged, no GUI flag

inspection                          NodeResultInspection,
                                    ElementResultInspection,
                                    ReactionInspection

overlays, as DATA                   LoadGlyph, NodalLoadArrow,
                                    RestraintGlyph, ReactionArrow
                                    arrow sizing as declared presentation

boundary highlighting, WIRED        loadTarget, restraintTarget,
                                    highlightForLoad, highlightForRestraint
  no resolution logic: P16's highlightFor does the work and its
  MappingState is carried whole

the DEFORMED display               ADR-043, an override position buffer
  through the ONE existing conversion, so the deformed and undeformed
  buffers reach the kernel from the same call site. Measured against the
  CAD solid's own scale: 42036 covered pixels, all four ways.

the FOUR NODAL contours            ADR-044, a colour per facet CORNER
  one mechanism for both channel domains: an element channel draws flat and
  a nodal one interpolates by the element's shape function. Measured: 1
  distinct colour for a uniform fill against 4807 for twelve bands, and
  clearing it returns a byte-identical image.
```

```text
66 targeted cases, 52 headless + 14 that need a GPU
3825 / 3825 x 3 presets, 970 x 5 in two; the glyphs qualified, 3h21m00s
113 mutation probe runs: 83 before the glyphs, 30 for them
21 real test gaps found and closed (15 + 6)
```

## Not built — and this is why the milestone stays open

```text
THE THREE ELEMENT CONTOURS: NO LONGER BLOCKED. Built and measured; see
  ELEMENT_CONTOURS.md and ADR-046. The boxes stay unticked only until the
  three-preset qualification covers them.

  THE BLOCKER RECORDED HERE WAS A WRONG CONCLUSION FROM A CORRECT DIAGNOSIS,
  and it stood for two milestones. It said closing the gap needed a
  triangle-to-owning-tetrahedron mapping in the qualified P16 meshing module,
  and that it was therefore a scope decision.

  Every fact was right: volumeBoundaryOf names Triangle3 elements, an element
  channel is per TETRAHEDRON, meshing::Triangle carries no owner. But the
  question "which tetrahedron owns this triangle" ONLY ARISES IF THE BOUNDARY
  IS TAKEN FROM THE TRIANGLE3 ELEMENTS. MeshView::volumeBoundaryByTetrahedronOf
  derives it from the tetrahedra instead, and ownership is known by
  construction because the tetrahedron is in hand when the face is emitted.

  No meshing change. No contour-layer change either. Everything needed was
  already in MeshView: kTetFaces with its winding already a tested property,
  Facet already carrying its owner, and the generic build.

  The lesson, recorded because it cost two milestones: A BLOCKER DERIVED FROM
  ONE IMPLEMENTATION ROUTE IS A STATEMENT ABOUT THAT ROUTE, NOT ABOUT THE
  CAPABILITY.

THE LOAD AND RESTRAINT GLYPHS: DONE and qualified (ADR-045).
  Drawn as segments in SI metres through the one existing conversion, and
  measured on screen. What remains unDRAWN is LoadGlyph, deliberately: a
  pressure or a traction has no single point and gravity has no location at
  all, so a per-facet pressure arrow needs facet centroids through
  GeometryMeshMap. That is a separate, larger piece, named here rather than
  half-built.

THE Qt INSPECTOR SURFACE
  No panel, no channel selector, no legend widget, no stale badge, no
  navigation buttons. The headless layer each would bind to is built,
  tested and qualified; none of it is wired.

  The LEGEND is worth singling out: ContourLegend carries the band
  boundaries, the channel's SI unit and the range, and partitionsItsRange()
  proves the bands tile it. Drawing it is a widget over data that is already
  complete -- there is no arithmetic left in it.
```

### A prediction in an earlier revision of this file was wrong

It said the per-facet colouring was "designed for (MeshView::elementOfTriangle
gives the owner-Tet mapping) but not implemented".

**`elementOfTriangle` does not give the owner-Tet mapping on a boundary view.**
It gives the Triangle3 surface element's id, and that is what blocks the
element contours. The implementation refuted the prediction, which is the
honest direction for that to go — and it is recorded here rather than quietly
rewritten, because the cost of the wrong note was a failing test that took a
diagnosis to read:

```text
band_out_of_range: element 59 owns render triangle 0 and channel von_mises
has no value for it
```

## Carried forward honestly

```text
SEVEN UNREACHABLE GUARDS, kept and documented
  V11 V12 V13 V28 V33 V35 C16. Each survives its mutation probe because a
  door in front of it is already closed, and each reason is DERIVED in
  MUTATION.md -- V12 by arithmetic, V28 through four links from
  prepare()'s mesh-currency refusal. They are kept because each guards a
  disagreement between two tables, or a future caller reaching the layer
  another way. Deleting them to reach 61 of 61 would remove real defence
  and report a worse result as a better one.

THE DRILLED-HOLE WALL still has no FaceName
  P16's carried limitation, and the viewer does NOT paper over it: no
  cylinder test, axis fit, radius or coordinate tolerance exists in any of
  the four production files (checked, comments stripped, all zero). The
  honest routes -- unresolved, or P16's face-INDEX overload -- are the only
  ones.

ONE LOGARITHMIC SCALE NOT SHIPPED
  ContourScale has a single value, Linear. A log scale is wrong for a
  signed channel and this project does not ship an option it has not
  validated; the enum exists so adding one later is additive rather than a
  reinterpretation of every stored preference.
```

## One predecessor change, discharged

`StructuralSolveOutcome` gained `fields` (`RecoveredFields`) and `reactions`
(`SupportReactions`). The reason is in [VIEWER_AUDIT.md](VIEWER_AUDIT.md):
`StructuralResult` carries `Strain6` and `Stress6` but **not** von Mises, the
principal stresses, the principal strains, the hydrostatic stress or `|u|`, so
a viewer restricted to the published result would have to recompute them — the
duplicate numerical authority the brief forbids. Both objects were already
computed one statement earlier and discarded.

An earlier revision of this file recorded that it **owed P17-VALID-001 a
requalification**. That debt is **discharged**: this milestone's three-preset
run rebuilt from clean and ran the whole suite in debug-ext, release-ext and
debug-shared-ext, and repeated P17-VALID's own set five times in two of them.
See [FREEZE.md](FREEZE.md).

## Honest status

```text
P17-VIZ-001 = PARTIAL. 18 of 23 boxes ticked.
              The headless layer and the DEFORMED and NODAL-CONTOUR displays
              are built and mutation-tested.
              Qualified through b04a9e1; the display surface has its own
              three-preset run in qualification-display/.
```

What remains is five boxes: three ELEMENT contours blocked on a mapping the
qualified P16 meshing module does not have, and the load and restraint GLYPHS,
whose data is complete and qualified and which need no new capability to draw.
Nothing here should be read as a passed gate for those five.
