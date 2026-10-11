# P17-VIZ-001 — what is NOT done

This milestone is **incomplete**. This file exists so the gap is stated rather
than discovered, and so nothing in the other evidence documents reads as a
claim about work that has not happened.

**12 of TODO.md's 23 checkboxes are unticked, and `ROADMAP.md` is untouched.**

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
```

```text
33 targeted cases
3792 / 3792 x 3 presets, 937 x 5 in two, 0 warnings
61 mutation probes: 54 killed, 7 unreachable, 0 void
```

## Not built — and this is why the milestone stays open

```text
THE OCCT RESULT PRESENTATION
  No presentation consumes the deformed buffer or the colour bands. So:
    - no millimetre coordinate has been measured reaching the viewer
    - the brief's x_view_mm table has four of six columns (UNIT_BOUNDARY.md)
    - per-facet constant colouring for element-constant stress is designed
      for (MeshView::elementOfTriangle gives the owner-Tet mapping) but
      not implemented

  THE ARITHMETIC IS NOT PART OF THIS GAP. The bands and the legend are in
  StructuralContour.hpp and are tested headlessly, because the interesting
  failures there are numerical: a partition with a gap, a value in two
  bands or in none, a uniform field divided by a zero range, a range too
  narrow to subdivide. 26 probes, 25 killed. What is missing is the drawing.

  AND THE HARNESS FOR THE REST ALREADY EXISTS, which lowers the risk an
  earlier revision of this file recorded here. P16-VIZ-001 left
  Viewer::createOffscreen plus RenderedImage with at() and coverage(), and
  MeshDisplayTests already proves the pattern the deformed buffer needs:
      MeshDisplay_TheMeshOverlaysItsOwnCadSolidAtTheSameScale
          the exact shape of the owed millimetre measurement
      MeshDisplay_AStaleMeshDoesNotLookLikeACurrentOne
      MeshDisplay_HighlightingChangesTheImageAndNotTheMesh

  So the work is a Viewer entry point taking an override position buffer and
  per-entity colours, and a MeshPresentation that honours them. That is
  additive, but it changes a QUALIFIED P16 file and an OCCT adapter, so it
  deserves its own architecture pass rather than being appended to the end
  of a long session.

THE Qt INSPECTOR SURFACE
  No panel, no channel selector, no legend widget, no stale badge, no
  navigation buttons. The headless layer each would bind to is built,
  tested and qualified; none of it is wired.

THE DISPLAY CHECKBOXES
  Consequently unticked: the undeformed and deformed displays, the
  deformation scale factor, the eight contours, the load arrows and the
  restraint symbols. See TODO.md for the rule applied and the reason
  against each.
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
P17-VIZ-001 = PARTIAL. Qualified, committed, 11 of 23 boxes ticked.
```

The headless layer — where every numerical gate in the brief lives — is built,
mutation-tested and qualified. The viewer surface is not. Nothing here should
be read as a passed gate for the twelve boxes above.
