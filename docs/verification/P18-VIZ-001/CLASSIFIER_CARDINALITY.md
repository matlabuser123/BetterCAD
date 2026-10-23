# P18-VIZ-001 — Classifier cardinality

P16-VIZ-001 recorded the trap and P17 repeated the discipline: **a classifier that succeeds and classifies nothing is not a complete classification.** A highlight of zero facets is not "all of them", a contour
over the entities that happened to classify is not the result, and "no failure was reported" is not "every entity was handled". Every field and every boundary glyph in this milestone therefore carries
three counts:

* **expected** — what the domain says there should be (a channel's node or element count; P16's forward facet list for a face or a set);
* **classified** — what the independent classifier produced (the values that were finite and present; P18-LOAD's / P18-BC's integration count; the reverse linkage count for a named set);
* **rendered** — what was actually drawn (the render triangles of the highlight).

## Fields

`ThermalScalarField` holds `expected`, `classified`, `skipped` (no value for this channel: absent, not zero) and `nonFinite`. `complete()` is strict: `expected > 0`, `classified == expected`,
`skipped == 0`, `nonFinite == 0`. An incomplete field is **never returned** from the classifier — it is refused with the reason (`CardinalityMismatch`, `ChannelUnavailable`, `NonFiniteValue`,
`EmptyClassificationDomain`) — and every consumer re-checks `complete()` (legend, banding, corner mapping, extrema).

| Case | Test | Mutation |
|---|---|---|
| temperature array shorter or longer than the node count | `RejectsTemperatureCardinalityMismatch` | `VIZ07` |
| gradient / flux arrays not the tetrahedron count | `RejectsElementGradientCardinalityMismatch`, `RejectsHeatFluxCardinalityMismatch` | — |
| a NaN value | `RejectsANonFiniteValueInsteadOfMappingItToAColour` | `VIZ06`, `VIZ08` |
| a state the result does not hold | `RejectsAStateThatIsNotStored` | — |
| an empty domain is not a uniform field | `AnEmptyDomainIsNotAUniformField` | — |
| a partial field is never coloured | `APartialFieldIsNeverColoured` | `VIZ24` |
| an extreme over a partial field is refused | `thermalExtremeOf` re-checks `complete()` | `VIZ16` |
| classifier cardinality matches expected, on real documents | `ClassifierCardinalityMatchesExpected` | — |

## Boundary targets

`classifyBoundaryCardinality(kind, id, mapping, classified, rendered)` is a pure function with a decision table, tested exhaustively:

| Condition (in this order) | Status |
|---|---|
| the P16 mapping is not fully resolved | `Unresolved` |
| expected = 0 and classified > 0 | `Inconsistent` |
| expected = 0 or classified = 0 | `ZeroClassified` |
| classified > expected or rendered > expected | `Inconsistent` |
| classified < expected or rendered < expected | `Partial` |
| otherwise | `Complete` |

`complete()` is `status == Complete`. `ZeroClassifiedEntitiesRejected`, `PartialClassificationNotReportedComplete`, `ClassifierCardinalityMatchesExpected` and `AnUnresolvedReferenceIsKeptAndNeverRebound` walk
every row; mutations `VIZ61` (zero-classified read as complete), `VIZ62` (partial read as complete), `VIZ63` (unresolved ignored) and `VIZ64` (rendered ignored) are killed. The status is an enumeration, not a
boolean: it does not convert to `bool` or to an integer (compile-fail `A_BOUNDARY_STATUS_IS_NOT_A_BOOL`, `A_BOUNDARY_STATUS_IS_NOT_AN_INTEGER`) and `const` glyphs cannot have it edited
(`THE_CARDINALITY_IS_READ_ONLY_THROUGH_A_CONST_VIEW`).

The independent counts, on real meshes: the block's caps and side are classified by coordinates in the test (`capStart.size() == capEnd.size()`, areas by a written-out cross product to 1e-12) and compared with
P16's `expected`, P18-LOAD's `classified` and the highlight's `rendered`. On the curved cylinder (900 nodes, 4118 tetrahedra, 572 boundary facets) the lateral wall and both caps — 288 + 142 + 142 glyphs —
are all `Complete` (`ACurvedWallHasOutwardNormalsAndACompleteClassification`).

## What an overlay does with an incomplete target

It is still drawn from what was classified, and `ThermalOverlay::incompleteTargets` counts it, so the caller cannot present the overlay as the whole target without choosing to
(`IncompleteTargetsAreCountedByTheBoundaryOverlays`).

## Reported in the glyph, not hidden

Every glyph record carries its `BoundaryCardinality`; the viewer layer does not summarise it away. `BoundaryClassStatus::Complete` is the only status a caller should label a symbol "complete".
