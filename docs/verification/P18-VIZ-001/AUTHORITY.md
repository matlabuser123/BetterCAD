# P18-VIZ-001 — Authority: a read-only view, not a thermal authority

## The rule

**THERMAL VISUALISATION = A READ-ONLY VIEW OF QUALIFIED DERIVED P18 DATA, NOT A NEW THERMAL ANALYSIS AUTHORITY.**

Every number the view shows is a stored member of an object a qualified milestone published, or a trivial presentation of one (a norm for display, a colour band index, a camera target).
The view owns no thermal quantity, recomputes none, and keeps no copy that outlives the call that read it.

## What each drawn thing is read from

| Shown | Read from | Owner | The view's own work |
|---|---|---|---|
| nodal temperature (K) | `ThermalPostState::temperatures[dof]`, parallel to `ThermalDofMap` | P18-POST / P18-DOF | none |
| gradient magnitude (K/m) | `ThermalPostState::fields.gradients[ordinal]` | P18-POST | the display norm of the stored vector (`gradientDisplayNorm`, NaN-propagating) |
| heat-flux magnitude (W/m²) | `ThermalPostState::fields.heatFluxMagnitudes[ordinal]` | P18-POST | none (POST's stored \|q\|) |
| recovered heat-flux vector q | `ThermalPostResult::heatFluxOf(state, element)` | P18-POST | the display direction q/\|q\| formed locally; the stored vector is never changed |
| applied heat flux q″ (W/m²) | the canonical `SurfaceHeatFluxLoad`, checked against `PreparedThermalLoads::source().loads` | P18-LOAD | an arrow along −n_out for q″ > 0 |
| prescribed temperature, convection | the canonical `ThermalBoundaryCondition`, checked against `PreparedThermalBoundary::source().conditions` | P18-BC | a symbol on each resolved facet |
| boundary-set highlight, facet linkage | P16 `highlightFor`, `sourceOfFacet` | P16 | the independent reverse count |
| currentness | `thermal::isCurrent`, `staleReasons`, `postSourceOf`, `requireThermalModel` | P18-POST / P18-ASSEMBLY | none — recomputed from the sources on every call |
| validation badge | `ThermalValidationReport::status()` while `thermal::isCurrent(report, …)` | P18-VALID | a mapping of the core's enumeration to a badge |
| node/element numbering | `ThermalDofMap::dofOf`, `ThermalPostResult::ordinalOf`, `elementIds()` | P18-DOF / P18-POST | none |
| stored times | `ThermalPostState::time` | P18-TRANSIENT / P18-POST | none |

## What the view does not do

* No gradient recovery, no Fourier's law, no heat balance, no residual, no validation rule, no assembly, no solve, no integration, no remeshing. The architecture gate
  (`CheckThermalVizOwnership.cmake`, rule 1) fails the build if one is named in a renderer thermal file or an application.
* No second unit conversion. The renderer layer emits SI metres; the metre-to-millimetre conversion is `toModelLength` in the OCCT presentation adapter, once ([UNITS.md](UNITS.md)).
* No raw `NodeId` / `ElementId` used as an index (gate rule 3; tests with sparse identifiers: `SparseNodeIdInspection`, `SparseElementIdInspection`).
* No stale flag. There is no `stale` member to set; `ThermalDisplayState` is a pure function of the document, the held result, the last failure and the report (gate rule 6; compile-fail `THE_DISPLAY_STATE_HAS_NO_STALE_FLAG`).
* No GUI-owned canonical k, ρ, c_p, q″, T̄, h, T∞, T₀ or Δt. The glyph records (`AppliedHeatFluxGlyph`, `PrescribedTemperatureGlyph`, `ConvectionGlyph`) hold the canonical value **read at
  build time** so the caller can label a symbol; they are verified to belong to the prepared object they are drawn with (`ThermalViz_AGlyphIsRefusedForLoadsThePreparedContributionsWereNotMadeFrom`)
  and are not stored by the view, the cache or the display state.

## Where the claim is enforced rather than stated

| Claim | Enforcement |
|---|---|
| no thermal physics in the view | architecture gate rule 1 + 11 fixtures; `Source audit` below |
| no unit conversion, no Celsius | gate rule 2; `ThermalViz_TemperatureRemainsKelvin`, `…GradientRemainsKelvinPerMetre`, `…HeatFluxRemainsWattPerSquareMetre`; compile-fail `QUANTITIES_HAVE_NO_CELSIUS_FACTORY` |
| nothing is mutated | `ThermalViz_DoesNotMutateThermalResult`, `…DoesNotMutateMeshDuringUnitConversion`, `…VectorScalingDoesNotMutateResult`; compile-fail const-view cases; gate rule 5 |
| currentness is the core's | `ThermalViz_SameCountsNewMeshStillStale`, `…SameIdsNewMeshStillStale`, `…FailedRecomputeDoesNotMakeOldResultCurrent` |
| classifier cardinality | [CLASSIFIER_CARDINALITY.md](CLASSIFIER_CARDINALITY.md) |

## GUI-physics audit

`qualification/source-audit.ps1` prints, per renderer thermal file, the count of matches for solve / assemble / recovery / balance / validation-rule names, unordered containers, clocks, raw-id indexing,
`const_cast`, `mutable`, numeric unit literals and the owning-milestone names each file must read. The expected output is zero for every forbidden pattern and non-zero for every required owner name;
`qualification/source-audit.txt` is the recorded run.

## The one honest limit

The Qt application (`apps/bettercad`) has no thermal result panel yet, exactly as it had none for P17-VIZ-001. The GUI smoke of this milestone is therefore the headless layer plus offscreen renders of the production
viewer path ([UNITS.md](UNITS.md), `ThermalViz_LargeMeshVisualizationReachesTheScreen`, `ThermalViz_ThermalOverlaysReachTheScreenInTheirOwnColours`). Wiring panels to this layer is a later decision that cannot change a number.
