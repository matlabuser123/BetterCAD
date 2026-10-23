# P18-VIZ-001 — Boundary symbols and highlights

Four boundary things are drawn: applied heat-flux arrows (q″), prescribed-temperature symbols, convection symbols, and named-boundary-set highlights. Each is built from the **canonical** load or
condition through the **qualified mapping**, never by geometry:

1. the canonical object (`ThermalLoad` / `ThermalBoundaryCondition`) names a CAD `FaceName`;
2. P16's `highlightFor(view, mesh, map, face)` resolves it to boundary facets (`MappingState::Resolved` / `Unresolved`);
3. the prepared object (P18-LOAD / P18-BC) supplies the independent *classified* count;
4. `classifyBoundaryCardinality` compares them ([CLASSIFIER_CARDINALITY.md](CLASSIFIER_CARDINALITY.md)).

There is **no nearest-face rebinding**: an unresolved reference stays unresolved and its glyph says so (`AnUnresolvedReferenceIsKeptAndNeverRebound`; compile-fail `THERE_IS_NO_NEAREST_FACE_REBINDING`).
The glyph builders also refuse loads or conditions that are not the ones the prepared object was made from, compared by value — counts that agree prove nothing about a load moved to another face
(`AGlyphIsRefusedForLoadsThePreparedContributionsWereNotMadeFrom`; mutations `VIZ71`, `VIZ72`).

## Applied heat flux q″ — positive INTO the body

For q″ > 0 the heat enters, so the arrow's **direction is −n_out** (head at the facet centroid, tail outside the body); for q″ < 0 it is +n_out (head at the facet centroid, tail inside); for q″ = 0 there is **no
arrow and no invented direction** (`AZeroAppliedFluxHasNoArrowAndNoInventedDirection`; counted in `skipped`). The sign is read from the canonical load; nothing re-derives it
(`PositiveAppliedHeatFluxArrowPointsIntoBody`, `NegativeAppliedHeatFluxArrowPointsOut`; mutation `VIZ56` flips it and is killed). The magnitude scales the length as for the vectors, with
`|q″|`, so a negative flux draws (mutation `VIZ65`).

The **outward normal** is the stored winding of the boundary triangle, (p₂−p₁)×(p₃−p₁) normalised. P16 gives each boundary triangle the winding of the tetrahedron that owns it, and the test verifies
the claim rather than assuming it: for every facet of the block the fourth vertex of the owning tetrahedron lies on the *inner* side of the facet plane (`OutwardNormalMatchesTheOwningTetrahedron`), and the same
on a curved cylinder wall, where "outward" is not an axis (`ACurvedWallHasOutwardNormalsAndACompleteClassification`; mutation `VIZ55` reverses the winding and is killed).

## Prescribed temperature, convection

* **Prescribed temperature** — a stem with a cross-bar along the outward normal at each resolved facet centroid, blue. The value carried is the canonical Kelvin value (`PrescribedTemperatureSymbol` checks 350 K and 300 K, not 76.85 or 623.15; each stem stands on its facet centroid
  along the outward normal — −z on the start cap, +z on the end cap — and is exactly the requested length). **Identical** prescriptions of one facet (the same value from two conditions) are one mathematical constraint (P18-BC collapses them): one symbol is drawn, the other is counted in
  `duplicatesGrouped`, and both conditions remain in the glyph list (`IdenticalPrescriptionsAreOneSymbolAndAdditiveLoadsAreBothDrawn`; mutation `VIZ66`).
* **Convection** — a zig-zag along the outward normal, cyan. Convection is **additive**: every condition is drawn even over a facet another covers.
* Both read their value from the canonical condition, not from a copy held by the view. Coefficient `h` in W/(m²·K), ambient `T∞` in K.

`MultipleBoundariesDoNotOverwriteEachOther` puts all four kinds on one body and checks each facet set and each overlay independently.

## Internal faces are never boundary conditions

`NoInternalFaceIsDrawnAsAnExternalCondition`: a face between two tetrahedra is not a boundary triangle, so no glyph is placed on it; a glyph's facet list is a subset of `mesh.triangles()` by construction
(`cornersOf` refuses an element that is not a boundary triangle).

## Named boundary sets

`namedBoundaryHighlight` returns the P16 highlight plus a `BoundaryCardinality` whose *classified* count is **independent**: it asks every boundary triangle where it came from (`sourceOfFacet`, mesh → CAD)
and counts those attributed to a face the set resolved to. The forward answer (CAD face → facets) is `expected`; the two directions of P16's mapping must agree, and the render triangles must cover them
(`BoundarySetHighlight`; mutation `VIZ69` counts every triangle and is killed).

## Colours

Role colours (OCCT adapter, `colourOf(ThermalOverlayRole)`): heat-flux vectors magenta, applied flux red, prescribed temperature blue, convection cyan. `ThermalOverlaysReachTheScreenInTheirOwnColours`
renders each of the four overlays offscreen over the mesh: the picture differs from the same picture with the overlay hidden, the role's own colour (8-bit sRGB within 30 per channel) appears somewhere in it, the
presentation reports kind `ThermalOverlay`, and an empty overlay is refused rather than displayed.

## An overlay says when it is not the whole target

A glyph whose cardinality is not `Complete` is still drawn from what was classified, and `ThermalOverlay::incompleteTargets` counts such glyph records so a caller cannot present the overlay as the whole
target without choosing to (`IncompleteTargetsAreCountedByTheBoundaryOverlays`; mutation `VIZ73`).
