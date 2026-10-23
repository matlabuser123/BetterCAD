# P18-VIZ-001 — Adversarial review

Two reviews: (1) the author's own, as a list of questions each answered by a test, a gate, a compile-fail case or a stated limit; (2) an **independent read** by a separate reviewer that was given the code, the
nine contract clauses and no conclusions. Findings of both are below with their disposition. Nothing is closed by assertion: every "fixed" has a test that fails without the fix, and every mutation named is in
[MUTATIONS.md](MUTATIONS.md).

## A. Findings

| # | Source | Finding | Disposition |
|---|---|---|---|
| F1 | author, first contour run | `std::hypot(0, NaN, 0) == 0` in the libstdc++ shipped here: a NaN gradient component would display as a quiet, valid zero | **fixed** — `gradientDisplayNorm` tests finiteness first and returns NaN; `RejectsANonFiniteValueInsteadOfMappingItToAColour`; mutation `VIZ06`. The core's `HeatFlux3::magnitude()` has the same property and is **untouched** (qualified predecessor; POST validates finiteness before storing). |
| F2 | author, while documenting | `ThermalViz_TransientFrameBindsAllFieldsToTheSameState` carried literal `\n` sequences in a comment line, so its final cache assertions were inside a `//` comment and **never ran** | see section C |
| R1 | independent, high | `ThermalContourCache` key omitted the `MeshView`: the same mesh, post, frame, channel and band count asked for a different view (boundary vs by-tetrahedron vs a selection) was a hit and returned a buffer for the wrong triangle order or length | **fixed** — the key now holds the view kind, the vertex and triangle counts and a fingerprint of the triangle index list, and a hit also requires `view.describes(mesh)`; `TheContourCacheIsKeyedOnTheViewAsWellAsTheSource`; mutation `VIZ31` |
| R2 | independent, medium | the glyph builders checked only `prepared.describes(mesh)` (stamp and node count), not that the loads or conditions presented are the ones the prepared object was made from: an edited load with the same counts would take `flux` and `facets` from the new load and `area` from the old contribution and classify `Complete` | **fixed** — by-value comparison with `prepared.source().loads` / `.conditions`; `AGlyphIsRefusedForLoadsThePreparedContributionsWereNotMadeFrom`; mutations `VIZ71`, `VIZ72` |
| R3 | independent, medium | `thermalDisplayState` passed any current report to the badge: `isCurrent(report, …)` judges a report against its *own* analysis, so a current Pass for analysis B showed green for A; and a report recorded against another result kept its badge | **fixed** — the report must name this analysis, and a differing recorded result source shows `Stale`; `TheValidationBadgeBelongsToItsOwnAnalysis`; mutations `VIZ42`, `VIZ43` |
| R4 | independent, medium-low | `thermalDisplayArrowLength` accepted finite operands whose quotient overflows (reference 1e-300, flux 1e10): an infinite length, then NaN geometry reaching the viewer | **fixed** — a non-finite length is refused; `AnOverflowingArrowLengthIsRefusedAndNeverReachesTheGeometry`; mutation `VIZ70` |
| R5 | independent, medium | an empty `elements` list to `heatFluxVectorGlyphs` means "all elements"; a density policy that selects nothing would draw everything | **accepted, documented** — the documented contract is "empty = all"; a caller that selects a subset passes a non-empty list, and a caller whose policy selects nothing does not call. Changing the meaning would be a breaking change to a contract the tests pin |
| R6 | independent, medium-low | glyph records carry copies of canonical q″, h, T∞, T̄ and facet ids with no mesh stamp or load identity; the overlay builders resolve facets with `mesh.findTriangle(id)`, so a glyph built on another generation still resolves if the ids exist; and a Partial glyph is drawn like a Complete one | **partly fixed, partly accepted** — the builders now bind the glyph to the prepared source by value (R2), and `ThermalOverlay::incompleteTargets` makes an incomplete target visible (`IncompleteTargetsAreCountedByTheBoundaryOverlays`; `VIZ73`). **Accepted:** a glyph is a per-call read-out, not stored by the view, the cache or the display state; it carries no stamp, and feeding a glyph from one mesh generation to an overlay on another is the caller's chain to keep. Recorded for the GUI wiring milestone |
| R7 | independent, medium-low | `inspectThermalNode/Element` default `resultState` to `Current`, so a caller that omits it for a stale post gets an inspection stamped Current | **accepted, documented** ([INSPECTION.md](INSPECTION.md)) — the inspection cannot verify what it is told (it holds no document); the default exists for the hand-built posts of the tests (about thirty call sites). The wiring milestone takes the state from `thermalDisplayState` |
| R8 | independent, low | a held post that is current by identity is `Current` even when `lastFailure != nullptr`; `reasonsFor` is empty when the current source cannot be formed | **accepted** — a result whose source equals what a solve now would carry *is* the result for the current inputs; a later failed attempt on identical inputs cannot be a different answer, and the failure is still reported in `failure`. Reasons are empty only when the model cannot be formed, in which case `inputProblem` is the reason |
| R9 | independent, low | `ThermalExtreme` carries no mesh stamp: an extreme from a previous generation whose id still exists navigates to that entity | **accepted** — an extreme is produced from a field that exists only while the result is current (fields are unavailable otherwise), and `thermalNavigationTarget` refuses ids the mesh does not hold; recorded in [EXTREMA_NAVIGATION.md](EXTREMA_NAVIGATION.md) |
| R10 | independent, low | the `std::set<pair<id, double>>` dedupe in `prescribedTemperatureOverlay` breaks strict weak ordering for a NaN temperature | **accepted** — a prescribed value is validated finite by P18-BC before a glyph exists; unreachable |
| R11 | independent, low | a header comment said the head of a negative-flux arrow "departs the surface to the outside"; the code puts the head at the facet centroid | **fixed** (comment) |

The independent reviewer also listed, clause by clause, what it found properly enforced: read-only, units, sparse identifiers (everything through `ordinalOf`, `dofOf`, `elementIds` and an `is_sorted`-guarded
binary search), field cardinality, core-path currentness, vector orientation and the applied-flux sign, extrema and band tiling, playback, and determinism. It found no wrong sign, no off-by-one, no NaN or
tie-break defect.

## B. The author's questions

Each answer names what holds it.

**Authority and scope**

1. *Does the view recompute any thermal quantity?* No. Gate rule 1 forbids the names; the only arithmetic is the documented display norm and arrow geometry. ([AUTHORITY.md](AUTHORITY.md))
2. *Does the GUI keep a copy of k, ρ, c_p, q″, T̄, h, T∞, T₀ or Δt?* No member of any view type holds one (compile-fail `…HAS_NO_CONDUCTIVITY`, `…HAS_NO_MATERIAL`); glyph records read canonical values per call and are bound to the prepared object by value (R2).
3. *Is there a second place that decides a result is current, or that a validation passed?* No: `isCurrent` / `staleReasons` / `ThermalValidationReport::status()`; gate rule 6; compile-fail `…HAS_NO_STALE_FLAG`; compile-fail `A_BADGE_IS_NOT_A_VALIDATION_STATUS`.
4. *Can the view solve, assemble, balance, remesh or validate?* No: gate rule 1; compile-fail `THERE_IS_NO_VIEW_SIDE_SOLVE`.
5. *Is anything persisted or written to the document?* No: the layer has no document write API; every input is const.

**Units**

6. *Is a temperature ever Celsius?* No: gate rule 2 and compile-fail `QUANTITIES_HAVE_NO_CELSIUS_FACTORY`; `TemperatureRemainsKelvin` reads 350 K as 350 K.
7. *Is a gradient ever K/mm or a flux W/mm²?* No: units come from the core descriptor (`ChannelsDescribeThemselvesWithTheCoreDescriptors`) and no rescaling exists (`GradientRemainsKelvinPerMetre`, `HeatFluxRemainsWattPerSquareMetre`).
8. *Is metre → millimetre converted more than once, or zero times?* Measured against the CAD solid at 1 × 2 × 3 mm and 40 mm (`SIMetreToOCCTMillimetre`, `AnOverlayInMetresCoincidesWithAFortyMillimetreSolid`); mutations `VIZ17` and `VIZ18`.
9. *Does display convert the core mesh in place?* No: `DoesNotMutateMeshDuringUnitConversion` (twice-displayed pictures are identical; the metre buffer is unchanged).
10. *Do two overlay constructors share the one conversion?* Yes — both call `toModelLength`; the freeze lists its 13 occurrences in the file.

**Fields and contours**

11. *Is an element value smoothed to nodes?* No: all three corners share the element's band (`GradientMagnitudeContourUsesElementResult`; `VIZ26`).
12. *Is the gradient magnitude POST's or recomputed?* The display norm of POST's stored vector; the heat-flux magnitude is POST's stored value (`TheGradientMagnitudeIsTheNormOfTheStoredVector`; `VIZ05`, `VIZ04`).
13. *Is an array of the wrong length padded or truncated?* No: refused (`RejectsTemperatureCardinalityMismatch`, `…ElementGradient…`, `…HeatFlux…`; `VIZ07`).
14. *Is a NaN ever coloured?* No: refused (`RejectsANonFiniteValueInsteadOfMappingItToAColour`; `VIZ06`, `VIZ08`).
15. *Do bands tile the range exactly at every count?* Yes, 2…64 (`TheBandsPartitionTheRangeAtEveryCount`; `VIZ20`; the boundary clamp, `VIZ22`, is unreachable and kept as a guard).
16. *What does a constant field do?* One band, no 0/0 (`ConstantScalarRangeHandled`, `ARangeTooNarrowToSubdivideIsOneBand`; `VIZ21`).
17. *Does a manual range change a value?* No: it clamps a colour (`ManualRangeClampsTheColourAndNeverTheValue`; `VIZ19`).
18. *Can a legend for another field colour this one?* No (`ALegendOfAnotherFieldIsRefused`; `VIZ23`).
19. *Can a partial field be coloured?* No (`APartialFieldIsNeverColoured`; `VIZ24`).
20. *Can the cache serve a buffer for another frame, source, range or view?* No (`TheContourCacheIsKeyedOnTheWholeSource`, `…AsWellAsTheSource`, `TransientCacheInvalidatesOnSourceChange`; `VIZ28`–`VIZ31`; R1).

**Vectors and symbols**

21. *Is q ever sign-flipped or normalised in storage?* No (`HeatFluxVectorDirection`, `DoesNotMutateThermalResult`; `VIZ57`, `VIZ58`).
22. *Does heat flow hot → cold in a real model?* Yes (`RecoveredHeatFluxFlowsFromHotToColdOnARealSlab`).
23. *Does a zero flux get a direction?* No, it gets no arrow (`ZeroHeatFluxVector`, `AZeroAppliedFluxHasNoArrowAndNoInventedDirection`; `VIZ59`, `VIZ60`).
24. *Does a positive q″ point into the body?* Yes: −n_out (`PositiveAppliedHeatFluxArrowPointsIntoBody`, `NegativeAppliedHeatFluxArrowPointsOut`; `VIZ56`).
25. *Is q″ distinct from q?* Yes: different overlays, colours and senses (`RecoveredQAndAppliedQDoublePrimeAreDistinct`).
26. *Is the outward normal really outward, also on a curved wall?* Verified against the owning tetrahedron's fourth vertex on the block and the cylinder (`OutwardNormalMatchesTheOwningTetrahedron`, `ACurvedWallHasOutwardNormalsAndACompleteClassification`; `VIZ55`).
27. *Can a symbol be placed on an internal face?* No (`NoInternalFaceIsDrawnAsAnExternalCondition`).
28. *Are identical prescriptions one symbol, and convection additive?* Yes (`IdenticalPrescriptionsAreOneSymbolAndAdditiveLoadsAreBothDrawn`; `VIZ66`).
29. *Is a reference ever rebound to the nearest face?* No (`AnUnresolvedReferenceIsKeptAndNeverRebound`; compile-fail `THERE_IS_NO_NEAREST_FACE_REBINDING`).
30. *Does the display threshold change physics?* No: readability only, counted in `belowThreshold` (`TheDisplayThresholdIsReadabilityNotPhysics`; `VIZ67`).

**Cardinality**

31. *Is a zero-classified target "complete"?* No (`ZeroClassifiedEntitiesRejected`; `VIZ61`).
32. *Is a partial one?* No (`PartialClassificationNotReportedComplete`; `VIZ62`, `VIZ64`).
33. *Is an unresolved mapping classified?* No (`VIZ63`).
34. *Is the named-set count independent of the forward answer?* Yes — the reverse linkage (`BoundarySetHighlight`; `VIZ69`).
35. *Is an incomplete target visible in the overlay?* Yes (`incompleteTargets`; `VIZ73`).

**Identifiers**

36. *Is a `NodeId` or `ElementId` ever an array index?* No: gate rule 3; sparse tests (`SparseNodeIdInspection`, `SparseElementIdInspection`, `HighestHeatFluxNavigation`); `VIZ44`, `VIZ45`, `VIZ74`.
37. *Are elements compared by count or by identity?* Id for id and in order (`ARecoveredResultIsBoundToItsOwnMeshAndNumbering`; the id-for-id comparison itself is unreachable by construction, `VIZ12` in [MUTATIONS.md](MUTATIONS.md)).
38. *Do ties resolve deterministically?* To the first entity (`ExtremaTieBreakDeterministic`; `VIZ09`, `VIZ10`).

**Currentness**

39. *Same counts after a remesh?* Stale (`SameCountsNewMeshStillStale`; `VIZ33`).
40. *Same identifiers after a remesh?* Stale (`SameIdsNewMeshStillStale`).
41. *A failed recompute?* Not resurrected (`FailedRecomputeDoesNotMakeOldResultCurrent`; `VIZ34`).
42. *A stale validation report?* Shown as Stale (`…GoesStaleWithTheModel`; `VIZ36`).
43. *Does the GUI downgrade a failed validation or hide warnings?* No (`AWarningStaysDrawnAndAFailureDoesNotPassAsAnAcceptedAnalysis`; `VIZ37`, `VIZ38`, `VIZ39`).
44. *Do view-state changes stale physics?* No (`ViewStateChangesDoNotStaleThePhysics`, `ViewerStateChangesDoNotStaleThePhysics`).

**Playback**

45. *Is a frame time `frame · dt`?* No: stored, 0 / 0.3 / 0.6 / 0.9 / 1.0, not 1.2 (`TransientPlaybackUsesStoredTimes`; `VIZ51`).
46. *Is anything interpolated in time?* No (`TransientPlaybackDoesNotInterpolatePhysics`; compile-fail `…HAS_NO_INTERPOLATION`).
47. *Are fields of one frame from one state?* Yes (`TransientFrameBindsAllFieldsToTheSameState`; see section C for the assertions that did not run).

**Process**

48. *Zero-match protection?* `ZeroMatchGuard.cmake` carries exact-floor filters for the 97 cases, 12 families, 51 compile-fail cases and 12 gate tests; the guard run lists each count.
49. *Does the gate itself work?* Eleven fixtures, each holding exactly one defect (or none), assert both the exit code and the diagnostic; the valid fixture holds comments naming forbidden things.
50. *Do the three presets agree?* The fingerprints are compared across presets by the post-run script; the result is in the README.

## C. The assertions that did not run (F2)

While writing the transient documentation the test source was read line by line. One line of `ThermalViz_TransientFrameBindsAllFieldsToTheSameState` held the end of the test — the cache assertions — **inside a
`//` comment**, because an earlier edit had written `\n` as two literal characters. The test passed; its last paragraph never executed. A whole-tree search found no other occurrence in the new tests or sources.
It was repaired after the first mutation pass: the paragraph is real code again, and un-commenting it exposed a wrong assertion (a nodal temperature on a slab's exterior facets is the same in every frame, because every corner node is prescribed), so it now uses an element channel under a shared manual range. Pass 2 of the mutation campaign ran against the repaired suite ([MUTATIONS.md](MUTATIONS.md)).
