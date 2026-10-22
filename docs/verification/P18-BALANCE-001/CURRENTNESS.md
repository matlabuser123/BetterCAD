# P18-BALANCE-001 — currentness and provenance

```text
SUBJECT:  a balance is current exactly when the solution it was taken from is; it never balances a stale, mismatched or foreign input
```

## The principle

A `ThermalBalanceResult` carries the solution's own source (`ThermalBalanceSource = ThermalPostSource`, built by `postSourceOf`):

```text
mode, the assembled system's source (body, mesh stamp, node count, geometry, material AND its revision, loads, convections),
the canonical prescribed temperatures, the solver settings, [initial temperature, transient settings]
```

`isCurrent` / `requireCurrent` convert both sides with the same `postSourceOf` and call **the transient solve's own comparison** (`staleReasons`), so there is no second rule: whatever makes a
solution stale makes its balance stale, with the same vocabulary of reasons. There is **no balance setting** in the source: the tolerance is one constant, and a view choice has no home there
(compile-fail `THE_SOURCE_HAS_NO_TOLERANCE_SETTING`, `…NO_COLOUR_MAP`, `…NO_COMPONENT_SELECTION`).

## The order of checks at the model level (`recoverSteadyBalance`, `recoverTransientBalance`)

```text
1  requireCurrent(solution, document, model, loads, conditions, [initial, transient,] settings)             source_mismatch
2  requireCurrent(system, document, model, mode, loads, conditions)                                          source_mismatch
3  requireCurrent(preparedLoads, model, loads); requireCurrent(preparedBoundary, model, conditions)          source_mismatch
4  preparedLoads, preparedBoundary and system describe the numbering                                         numbering_mismatch
5  preparedLoads.source().loads == system.source().loads                                                    source_mismatch
6  the seam: mode, numbering, constraint map, convection facet count; solution.source().system == system.source(); constraints held
7  the arithmetic
```

Everything before the arithmetic; a stale input is reported as one and never as a numerical symptom.

## The table (`ThermalBalanceMappedTests.cpp`)

| Edit or situation | Result | Test |
| --- | --- | --- |
| a load edit | refused, `source_mismatch`, names `loads`; `isCurrent` false | `RejectsStaleSteadyResult` |
| a prescribed-temperature edit | refused, names `prescribed_temperatures` | same |
| a convection edit | refused | same |
| a solver-settings edit | refused, names `settings` | same |
| a conductivity edit | refused, names `material` | same |
| a transient time-step edit | refused, names `time_step` | `RejectsStaleTransientResult` |
| a transient initial-condition edit | `isCurrent` false | same |
| a steady result asked about as transient, and the reverse | refused, "steady solution" / "transient history" | `RejectsStaleSteadyResult`, `RejectsStaleTransientResult` |
| a system assembled for another load list presented with this solution (same sizes) | refused: the system is not current for the loads, and the seam compares the system with the solution by value | `RejectsMismatchedAssemblySource` |
| prepared loads of another list | refused | same |
| a **remesh with the same node count, the same tetrahedron count and the same ids** | refused, names `mesh`: the old balance is stale, the old solution is refused with the new model, the seam refuses on the source; the fresh balance is current | `RemeshStalesBalanceEvenWhenCountsAndIdsAgree` (9 nodes, 12 tetrahedra) |
| a **failed** recovery after a successful one | the old balance stays stale and unchanged (generation 2.4 W, balanced); nothing new is published | `FailedRecoveryDoesNotPublishACurrentBalance` |
| a `FinalOnly` history of several steps | refused, `step_data_missing` | `TransientNeedsEveryStepToBalanceEachStep`, `TransientSeamNeedsEveryStep` |

## Never new data with old results

A conductivity edit stales the solution (check 1) and the system (check 2), so neither a new `k` with the old temperatures nor an old system with a new solution is balanced. The
balance never rereads the material: it reads `K` from the assembled system, whose source is compared with the model's current one.

## Mesh generation, not mesh size

The system and the prepared sets are tied to the mesh by stamp AND node count, and the check is by value against the model's current source. A remesh that reproduces every count and every id is a
different mesh; the old balance is stale, and a recovery from the old solution on the new model is refused (`Mesh`).

## What the result is not

**Derived** (not canonical, not persisted, not in the command history). Nothing here writes a document, so currentness is always a question about identity; no array is read to answer it. A
viewer's colour range, selected component or smoothing display cannot stale a balance, because none is part of its source.
