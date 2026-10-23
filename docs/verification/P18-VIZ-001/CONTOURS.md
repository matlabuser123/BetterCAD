# P18-VIZ-001 — Contours

Three contours: **temperature** (nodal), **temperature-gradient magnitude** and **heat-flux magnitude** (both element-wise). All three are built the same way from a *complete* `ThermalScalarField`
([FIELD_MAPPING.md](FIELD_MAPPING.md)): a legend (`thermalContourLegend`) → a band index per entity (`thermalContourBandsOf`) → a band index per render-triangle corner
(`thermalTriangleCornerBands`) → RGB per corner (`thermalContourCornerColours`, nine doubles per triangle, the layout `Viewer::setMeshContour` takes).

## Temperature: nodal, interpolated across the facet

Each corner of a boundary triangle takes the band of **its own node's** stored temperature (found through the numbering, never `node.value()`); the viewer's linear interpolation across the facet is the
element's own shape function, which is where a nodal field is defined — not an average of anything (`ThermalViz_TemperatureContourUsesNodalValues`: a hand post of 310, 350, 295, 350 K; the test computes the
expected band of every corner itself).

## Gradient and heat-flux magnitude: element-wise, unsmoothed

An element value is constant on its tetrahedron. A boundary triangle takes its owning tetrahedron's value on all three corners, so the facet draws flat; **nothing is averaged to nodes**
(`GradientMagnitudeContourUsesElementResult`, `HeatFluxMagnitudeContourUsesElementResult`: four tetrahedra with distinct values, each facet checked against its owner). The view must name tetrahedra
(`VolumeBoundaryByTetrahedron`); the boundary view of `Triangle3` elements is refused by name (`AnElementChannelNeedsAViewThatNamesTetrahedra`). Mutations `VIZ25` (accept it) and `VIZ26`
(smooth one corner) are killed.

## The legend

* **Bands partition the range exactly.** One boundary array is shared by adjacent bands (`upper[i] == lower[i+1]` bit for bit); the last band is closed above and no other is; every value of the field, including
  the minimum and the maximum, falls in exactly one band, at every count from 2 to 64 (`TheBandsPartitionTheRangeAtEveryCount`, `partitionsItsRange`). Mutation `VIZ20` (last band half-open) is killed; the boundary clamp (`VIZ22`) cannot bind and is kept as a guard ([MUTATIONS.md](MUTATIONS.md)).
* **A uniform range is one band.** `(v − min)/(max − min)` is 0/0; a range too narrow to subdivide (`min + width == min`, two temperatures one ulp apart) would print N bands of which only the last could ever hold
  a value. Both give a single band painted from the middle of the ramp (`ConstantScalarRangeHandled`, `ARangeTooNarrowToSubdivideIsOneBand`).
* **Auto range = the field's own extremes, unrounded; manual range = view state.** A manual range clamps the *colour* of a value outside it to the nearest end band; the number itself is untouched and the
  inspector still reports it (`ManualRangeClampsTheColourAndNeverTheValue`). An unordered, non-finite or empty-ended manual range, and a band count outside [2, 64], are refused
  (`RejectsAnUnusableDisplayRangeOrBandCount`).
* **The legend names the field.** `Temperature [K]`, `Temperature Gradient Magnitude [K/m]`, `Heat Flux Magnitude [W/m^2]`, and for a transient frame `… at t = 0.6 s`, the unit from the core's descriptor
  and the time from the stored state (`TheLegendNamesTheFieldTheUnitAndTheStoredTime`).
* **A legend belongs to its field.** Channel and both endpoints of the field must match, else `LegendMismatch` — two solves of one channel can share a name and differ in range, and banding through the wrong
  legend is wrong by a shift (`ALegendOfAnotherFieldIsRefused`; mutation `VIZ23`).
* **A partial field is never coloured.** A field that classified fewer entities than its domain holds is refused at the legend and at the banding (`APartialFieldIsNeverColoured`; mutation `VIZ24`).
* **The same scalar gets the same colour** and the two ends of the ramp are the ends (`TheSameScalarGetsTheSameColourAndTheEndsAreTheEnds`).

## Reaching the screen

`ThermalViz_ATemperatureContourReachesTheScreenAsManyColours` renders a 16-band temperature contour of a 20 mm slab through the production `Viewer` offscreen. The contour changes the picture and not the silhouette
(the background coverage is identical), the picture holds more than 12 distinct colours and more than the plain shaded mesh, and clearing the contour restores the plain picture exactly. `LargeMeshVisualizationReachesTheScreen`
does the same on the 161-node block.

## The cache

`ThermalContourCache` is derived and never authority: it can make a redraw cheaper and cannot make a result current. Its key is the result source (by value), the mesh stamp, the frame, the channel, the band count,
the display range **and the view** (kind, vertex and triangle counts, a fingerprint of the triangle index list). A hit is refused unless the view still describes the mesh; a miss clears the held buffer first, so
a failed rebuild leaves nothing behind as the answer (`TheContourCacheIsKeyedOnTheWholeSource`, `TheContourCacheIsKeyedOnTheViewAsWellAsTheSource`, `TransientCacheInvalidatesOnSourceChange`; mutations `VIZ28`–`VIZ32`).
The view was *not* part of the key in the first implementation; the independent review found it ([ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md), finding R1).
