# P18-VIZ-001 — Stale-result state

**A stale result must never be presented as current, and must never be put onto a mesh it was not made on.** The view has no stale flag. `thermalDisplayState(document, regenerator, mesher, analysis, post*,
lastFailure*, report*)` is a pure function that **recomputes** the answer from the sources on every call, through the core's own comparison (`thermal::isCurrent`, `staleReasons`, `postSourceOf`,
`requireThermalModel`). Nothing in the application can set it, forget to clear it, or cache it past an edit.

## The states

| `ThermalResultState` | Meaning | Fields | Banner |
|---|---|---|---|
| `NoAnalysis` | the document has no analysis with that identity | none | NO RESULT |
| `InputsUnavailable` | the inputs do not resolve (stale mesh, ineligible body …) and nothing is held | none | NO RESULT |
| `NoResult` | inputs resolve, nothing computed | none (intent may draw) | NO RESULT |
| `Current` | the held result's source equals the source a result computed now would carry | available (unless the validation failed) | — |
| `Stale` | a result is held and a dependency moved | **unavailable**; inspectable and marked | STALE RESULT - recompute required |
| `SolveFailed` | the last solve failed; any held result is stale by definition | unavailable | SOLVE FAILED - no current result |

The mesh state (P16's `MeshCurrency`), the result state and the validation state are reported **side by side and never merged**: a material edit leaves the mesh `Current` and the result `Stale` and
the state says that, not "remesh". `staleReasons` is the *set* of moved dependencies in the core's order (material, loads, prescribed temperatures, mesh …), not the first mismatch.

## What the tests hold

* `StaleResultHasExplicitState` — a material edit → `Stale` with reason `Material`; a load edit → `Loads` and not `Material`; a condition edit → `PrescribedTemperatures`; the edit undone → `Current` again,
  by identity, because the state follows the core.
* `DoesNotMapStaleResultToNewMesh` — after a remesh the old result is `Stale` (reason `Mesh`), the new mesh is `Current`, and **nothing** of the old result can be put on the new mesh: not a contour
  (`ThermalResultView::of` refuses, through the new numbering and through the old one), not a vector glyph, not a node or element inspection. A fresh derivation on the new mesh is current and displays.
* `SameCountsNewMeshStillStale` — the remesh has exactly the same node and tetrahedron counts; the old result's sizes agree with the new mesh; it is still `Stale` and still refused.
* `SameIdsNewMeshStillStale` — the remesh has exactly the same node and element **identifiers**; every handle in the old result resolves in the new mesh (to another physical entity); it is still `Stale` and every
  entry point still refuses it.
* `FailedRecomputeDoesNotMakeOldResultCurrent` — current → edit → stale → the new solve fails: the state is `SolveFailed`, the failure is reported beside the old result, the reason it is stale is still
  reported, `resultMayDrawAsCurrent()` is false and the fields are unavailable. A failure with nothing held is `SolveFailed`, not "no result".
* `StaleResultSuppressesTheContourAndMarksTheMeshStale` (offscreen) — when the state is not `Current` the contour is not applied and the mesh is shown with P16's stale marker; the picture differs from the current one.
* `TheStatesBeforeAnyResultAreDistinct` — no result, a failure with nothing held, an unknown analysis, and a geometry edit regenerated and not remeshed (`StaleGeometry`, `InputProblem::MeshStale`) are four
  different states with four different consequences for what may draw.
* `CurrentResultDisplaysNormally` — a current accepted result: every flag, the Pass badge, all three channels complete, no banner.

Mutations `VIZ33` (any held result is current), `VIZ34` (a failed recompute revives the old result) and `VIZ35` (reasons not reported) target this file's logic; `VIZ40`/`VIZ41` the availability and banner.

## The validation badge

The badge is the core's verdict (`ThermalValidationReport::status()`), shown **only while the report is current** and only if it is **this analysis's report about this result**:

* a current Pass is `Pass`, warnings are `PassWithWarnings`, a failure is `Fail`; the view does not downgrade (`VIZ37`, `VIZ38`);
* a report that no longer describes the model (an edit) is **`Stale`** and shows no status (`TheValidationBadgeIsTheCoresVerdictAndGoesStaleWithTheModel`; `VIZ36`);
* a report that names another analysis (or none) shows nothing (`TheValidationBadgeBelongsToItsOwnAnalysis`; `VIZ42`); one that recorded another result's source is `Stale` (`VIZ43`).
  Found by the independent review as a hole: the core's `isCurrent(report, …)` judges a report against its *own* analysis, so a current report for analysis B could carry a green badge for A.
* **a warning stays drawn; a failure does not pass as an accepted analysis**: `PassWithWarnings` leaves the fields available, `Fail` on a current result makes them unavailable with the banner
  INVALID RESULT - validation failed (`AWarningStaysDrawnAndAFailureDoesNotPassAsAnAcceptedAnalysis`, including the real failing validation of a singular steady model).

## View state never stales physics

The colour map (band count, manual range), the field selection, the playback cursor, the camera, visibility, mesh style and selection are **view state**: none is a field of any source. After exercising all
of them the display state compares equal to the one before and the report is still current (`ViewStateChangesDoNotStaleThePhysics`, `ViewerStateChangesDoNotStaleThePhysics`).
