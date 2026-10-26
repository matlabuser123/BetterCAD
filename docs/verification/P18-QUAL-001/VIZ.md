# P18-QUAL-001 - visualisation audit (brief sections 53-58)

**Verdict: the viewer is a read-only presentation of derived data, in SI, undeformed; staleness is derived by the core; the one hole found (a defaulted `Current` label) is closed.**
Row-by-row: `audit/AUDIT_C_post_balance_validation_viz_commands_persistence_cli.md` section C4 (12 rows: 6 COVERED, 6 PARTIAL, 0 GAP) and ADR-054 in `audit/AUDIT_A_*` (16 clauses).
There is no GUI application in this repository (`apps/` holds only `bettercad_cli`), so "the viewer" is the headless library API (`ThermalResultView`, `ThermalContour`,
`ThermalInspection`, `ThermalOverlay`) plus `renderer::Viewer`. Everything below is therefore about what the API **permits**, which is the only thing a future GUI can rely on.

| property | where | tests |
|---|---|---|
| read-only: every entry point takes const inputs; only the viewer's own cursor and cache mutate | all `Thermal*.hpp` signatures; no `const_cast` or `mutable` (gate rule) | `ThermalViz_DoesNotMutateThermalResult` (copies the result and the mesh and compares after every entry point), `ThermalViz_VectorScalingDoesNotMutateResult`, `architecture.thermalviz-ownership.mutates`, compile-fail `the-field-is-read-only-through-a-const-view` and six more |
| no thermal formula in the viewer (no gradient recovery, Fourier, balance, Robin) | the only arithmetic allowed is the display norm of a stored gradient; `|q|` is POST's stored value | `ThermalViz_ConsumesQualifiedHeatFlux` (bitwise), `_ConsumesQualifiedGradient`, `_TheGradientMagnitudeIsTheNormOfTheStoredVector`; `architecture.thermalviz-ownership.gui-recomputes`, `.second-norm` |
| SI metres all the way to the kernel adapter; the only metre-to-millimetre conversion is `toModelLength` in the OCCT adapter | `OcctOverlayPresentation.cpp:23`, `OcctMeshPresentation.cpp:54` | `ThermalViz_SIMetreToOCCTMillimetre` (a 1x2x3 mm CAD box vs the overlay traced in metres, plus a mirrored `-3 mm` endpoint), `ThermalViz_AnOverlayInMetresCoincidesWithAFortyMillimetreSolid`, `ThermalViz_DoesNotMutateMeshDuringUnitConversion`; mutation QP23 on the **overlay** adapter |
| fields stay K, K/m and W/m^2 | `descriptorOf` reads unit and location from POST's own descriptor | `ThermalViz_TemperatureRemainsKelvin`, `_GradientRemainsKelvinPerMetre`, `_HeatFluxRemainsWattPerSquareMetre`; `architecture.thermalviz-ownership.celsius`, `.converts-units` |
| the undeformed mesh: no displacement is ever applied | no thermal function takes displaced positions | `ThermalViz_EverythingLeavingThisLayerIsSiMetres`, `_ATemperatureContourReachesTheScreenAsManyColours` (coverage equals the plain mesh) |
| classifier cardinality: expected / classified / rendered; zero-classified is rejected; partial is never complete; an unresolved reference is kept, never rebound | `classifyBoundaryCardinality` | `ThermalViz_ZeroClassifiedEntitiesRejected` (all four kinds), `_PartialClassificationNotReportedComplete` (the full 9x9 grid), `_AnUnresolvedReferenceIsKeptAndNeverRebound`, `_APartialFieldIsNeverColoured` |
| the applied flux q'' (a scalar, positive into the body) is never conflated with the recovered q (a vector, `-k grad T`) | separate types and roles; separate colours | `ThermalViz_PositiveAppliedHeatFluxArrowPointsIntoBody`, `_NegativeAppliedHeatFluxArrowPointsOut`, `_RecoveredHeatFluxFlowsFromHotToColdOnARealSlab`, `_RecoveredQAndAppliedQDoublePrimeAreDistinct`, `_ThermalOverlaysReachTheScreenInTheirOwnColours` |
| a stale result is never placed on a mesh it was not recovered on - even with the same counts, even with the same ids | `ThermalResultView::of`, glyphs, inspection all test `mesh.owns(stamp)` | `ThermalViz_DoesNotMapStaleResultToNewMesh`, `_SameCountsNewMeshStillStale`, `_SameIdsNewMeshStillStale` (every old id resolves in the new mesh and the inspection is still refused) |
| the display state is derived from the core on every call (current only if `isCurrent`; stale with the **set** of moved dependencies); the validation badge is the core's verdict while the report is current, else `Stale` | `thermalDisplayState` | `ThermalViz_StaleResultHasExplicitState`, `_FailedRecomputeDoesNotMakeOldResultCurrent`, `_TheValidationBadgeIsTheCoresVerdictAndGoesStaleWithTheModel`, compile-fail `the-display-state-has-no-stale-flag`, `-no-override` |
| playback is a cursor over the stored states with the stored time; no interpolation | `ThermalPlayback` | `ThermalViz_TransientPlaybackUsesStoredTimes`, `_TransientFrameBindsAllFieldsToTheSameState`, `_TransientPlaybackDoesNotInterpolatePhysics` |
| caches are keyed on the whole source | `ThermalContourCache` | `ThermalViz_TheContourCacheIsKeyedOnTheWholeSource`, `_OnTheViewAsWellAsTheSource` |
| sparse node and element ids are never array indices | | `ThermalViz_SparseNodeIdInspection`, `_SparseElementIdInspection` |

## Defect found and fixed by this milestone: D-B, an inspection could be labelled `Current` by omission

`inspectThermalNode` and `inspectThermalElement` defaulted their `resultState` parameter to `ThermalResultState::Current`, and the two
inspection records defaulted the field to `Current` as well. A caller that forgot to pass the display state's result would have stamped a
**stale** result `Current` - the one claim this layer must never make by omission. The default is now removed from both functions and the records
default to `NoAnalysis`; every call site (31, in six test files) now states the state it means. A new compile-fail case,
`thermalviz.an-inspection-must-state-the-result-state`, proves the call without the argument no longer compiles ("too few arguments").
This is not a numerical defect: no existing test passed a stale state and read the label, and the display state itself was always derived correctly.

## PARTIAL rows, recorded (`LIMITATIONS.md`)

* **D-C** `Viewer::displayOverlay(ThermalOverlay)` does not look at `ThermalOverlay::incompleteTargets`: a boundary overlay in which one glyph is `Complete` and
  another `ZeroClassified` is drawn as if whole. By design the overlay carries the count so the **caller** can refuse to present it as the whole target; there
  is no caller in this repository. Not changed: refusing would also remove the legitimate display of a partially classified target, which is a UI decision.
* **C4.7b** staleness on screen is caller-driven (`setMeshStale`, `clearMeshContour`); `ThermalContourCache::colours` takes no currentness argument. The test
  performs the suppress-and-mark protocol by hand. A thermal overlay presentation has no stale mark at all.
* **C4.1b** the architecture gate is name-based; inline arithmetic with no listed name would pass it. The runtime tests use consistent POST data, so a viewer that
  recomputed `|q|` from the gradient would still pass them. Recorded.
* **C4.3** the unit test compares silhouettes within 3 px on a 320x240 frame; it would fail on a missing or doubled conversion (a factor of a thousand) and does
  not assert the literal `[1, 2, -3] mm`. The conversion exists in two wrappers, both delegating to `geometry::occt::toModel`.
* **D3** a non-finite stored flux is counted as "skipped" like a zero flux instead of being refused in the glyph builders (the field channel path does refuse).
  POST never emits one.
* **D4** the Pass badge can be shown with no result held (after a failed re-solve with no old result), beside a "NO RESULT" banner.
