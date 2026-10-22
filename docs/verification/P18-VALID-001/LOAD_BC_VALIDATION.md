# P18-VALID-001 — load and boundary-condition validation

```text
RULES:    TV_LOADS (V06), TV_BCS (V07)
SUBJECT:  every configured load and condition is finite, supported, RESOLVED against the current mesh and non-conflicting; nothing is rebound, and nothing is required to exist
```

The validator redoes none of the physics. It asks the qualified preparation functions whether the configured set resolves on the current model and reports their
classification, unchanged, as the finding's `cause`: P18-LOAD-001's `thermalLoadProblem` (through `prepareThermalLoads`) and P18-BC-001's `thermalBoundaryFailure`. No `q''A/3`,
no `q'''V/4`, no Robin matrix, no integration is repeated here (the ownership gate forbids those names in the file).

## Loads

| Case | Result | Cause | Test |
| --- | --- | --- | --- |
| a face the body does not have (a well-formed `FaceName` naming no face) | **FAIL** `loads_invalid` | `target_unresolved` | `RejectsUnresolvedLoad` |
| a face that resolves to no boundary facet | **FAIL** | `target_without_facets` | (P18-LOAD-001's classification, unchanged) |
| duplicate load identity, non-finite value, invalid selector, degenerate facet | **FAIL** | upstream cause | P18-LOAD-001 |
| no load at all | PASS, information `no_loads_configured` | — | `NoLoadsIsNotAnError` |
| a load of magnitude 0 on a face that resolves | PASS | — | `AZeroMagnitudeLoadIsValid` |
| a stale prepared set (the same counts and ids on a new mesh generation, or a load edit) | **FAIL** at `TV_SOURCE_COMPAT`, subject `prepared_loads` | `source_stale` | `RejectsSameCountsNewMeshSource`, `EveryEditStalesExactlyWhatDependsOnIt` |

* **No nearest-face rebinding.** An unresolved target is a failure naming the cause. Nothing nearby is substituted (P18-LOAD-001 refuses to, and the validator does not
  ask it to).
* **A zero magnitude is not an unresolved target.** The set with a `0 W/m²` flux on a real face and a `0 W/m³` generation is `loads PASS` and the analysis is accepted;
  the empty set is not an error either. The two are different statements: "there is no heat" and "I cannot find where the heat goes".
* **At least one load is NOT required.** A model with no loads and a valid pair of prescribed temperatures is a conduction problem like any other. (Mutation VAL15, which
  demands one, is killed.)

## Boundary conditions

| Case | Result | Cause | Test |
| --- | --- | --- | --- |
| two prescribed temperatures on one face (400 K and 350 K) | **FAIL** `boundary_conditions_invalid` | `conflicting_temperature` | `RejectsConflictingTemperatureBC` |
| two IDENTICAL prescriptions on one face | PASS (collapsed to one constraint per unknown upstream) | — | `IdenticalDuplicateTemperatureBCIsNotAConflict` |
| a condition on a face the body does not have | **FAIL** | `target_unresolved` | `RejectsUnresolvedBoundaryCondition` |
| a prescribed temperature below 0 K or non-finite; a non-positive or non-finite `h`; a non-finite ambient | refused by the canonical definition at entry | — | `InvalidConvectionCoefficientNeverReachesTheDocument` |
| no condition at all | PASS, information `no_boundary_conditions_configured` | — | `TransientPureNeumannModelPassesEndToEnd` |
| a stale prepared boundary | **FAIL** at `TV_SOURCE_COMPAT`, subject `prepared_boundary` | `source_stale` | the currentness matrix |

* **`h = 0` is not valid in this version, and the validator does not pretend otherwise.** The canonical `ConvectionBoundary` requires a finite and strictly POSITIVE
  coefficient (a zero coefficient is an adiabatic face wearing a convection record), so no analysis the validator sees can carry one; the brief's "h ≥ 0 under the current
  policy" resolves to "h > 0 under the current policy". The definition refuses `0`, a negative value and NaN, and a negative ambient, and the document is left unchanged.
* **A generic "BC count must be positive" rule does not exist.** A steady model with no condition is refused for what is wrong with it — no temperature datum, at
  `TV_WELL_POSED` — and a transient insulated body with no condition is accepted. The mode decides, not a count.
* **A fixed temperature and a convection on the same face are both valid** (P18-BC-001 keeps both; the face's constraints supply the exchange as well as the conduction —
  see the BALANCE evidence).

## What is reported when the set is wrong

One finding per failing rule, carrying P18's own classification as `cause` and its message verbatim (the first target that did not resolve, as P17's validation reports
`prepareStructuralLoads`). That is the upstream contract and this milestone does not re-litigate it. The rules downstream of the model — the whole System, Solve, Result and
Balance groups — are SKIPPED with `blockedBy` naming the failing rule; the other Input rule is still judged (a bad load does not hide a bad condition).
