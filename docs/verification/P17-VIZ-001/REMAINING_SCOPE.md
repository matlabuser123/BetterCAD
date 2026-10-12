# P17-VIZ-001 — what is NOT done

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
47 targeted cases, 37 headless + 10 that need a GPU
3806 / 3806 x 3 presets, 951 x 5 in two; the display surface requalified
83 mutation probes: 74 killed, 7 unreachable, 2 equivalent, 0 void
15 real test gaps found and closed
```

## Not built — and this is why the milestone stays open

```text
THE THREE ELEMENT CONTOURS: BLOCKED, not unfinished.

  MeshView::volumeBoundaryOf builds from the mesh's Triangle3 SURFACE
  elements, so elementOfTriangle returns a TRIANGLE's id. An element
  channel's values are per TETRAHEDRON. The two id spaces are disjoint, and
  meshing::Triangle carries id, nodes and region -- no owning tetrahedron,
  and no accessor in meshing answers for one.

  Closing it needs a NEW CAPABILITY IN THE QUALIFIED P16 MESHING MODULE: a
  triangle-to-owning-tetrahedron mapping, a topological search over sorted
  node triples. That is a scope decision and it touches a qualified module,
  so it is not reached across a boundary from the renderer.

  THE ELEMENT PATH ITSELF IS CORRECT, and that is a separate, tested
  statement: the same function and the same field on a MeshView::tetrahedraOf
  view produce flat per-facet bands matching an independent lookup. What is
  absent is the mapping, not the colouring
  (StructuralViz_AnElementChannelNeedsTheOwningTetrahedron).

THE LOAD AND RESTRAINT GLYPHS
  LoadGlyph, NodalLoadArrow, RestraintGlyph and ReactionArrow are built,
  tested and qualified -- the resultants read from the prepared
  contributions, and the sign, the scale, the zero-vector case and the
  components-not-flattened-to-fixed case are each mutation-killed. Nothing
  draws them, and they need NO new capability: a glyph is a position, a
  direction and a length, and the viewer already displays arbitrary
  geometry.

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
