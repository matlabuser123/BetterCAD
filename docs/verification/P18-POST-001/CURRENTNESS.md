# P18-POST-001 — currentness and provenance

```text
SUBJECT:  a recovered field is current exactly when the solution it came from is, by the solution's own comparison; and it never pairs a new k with an old T
```

## The principle

A `ThermalPostResult` records the **solution's own source** (`ThermalPostSource`, built by `postSourceOf(SteadySolutionSource | TransientSolutionSource)`):

```text
mode, ThermalSystemSource (body, mesh stamp, node count, geometry, material AND its revision, loads, convections),
the canonical prescribed temperatures, the solver settings, [initial temperature, transient settings]
```

`staleReasons(recovered, current)` converts both sources to the transient solve's source type and calls **that solve's** comparison
(`asTransientSource`), adding only a stored-mode difference. There is no second comparison: whatever makes a solution stale makes its recovery stale,
by the same code and with the same vocabulary of reasons (`TransientStaleReason`). There is **no post setting** in the source. Which component a
viewer shows, a colour range or a smoothing display is a view of the result, so editing one cannot stale it (compile-fail
`THE_SOURCE_HAS_NO_COLOUR_MAP`, `…NO_COMPONENT_SELECTION`, `…NO_SMOOTHING_SETTING`).

## The order of checks at the model level (`recoverSteadyThermal`, `recoverTransientThermal`)

```text
1  requireCurrent(solution, document, model, loads, conditions, [initial, transient,] settings)     PostSourceMismatch
2  resolveThermalMaterial(document, model.body(), mode)                                               MaterialMismatch
3  model.conductivity() == the document's k now                                                       MaterialMismatch
4  seam: conductivity finite and > 0; mesh.owns(system.mesh) && node count; numbering describes mesh
5  every state: size, finite, >= 0 K
6  the element loop; any element or value that fails fails everything
```

## The table

| Edit or situation | Result | Test (section) | Mutation that removes the check |
| --- | --- | --- | --- |
| a load edit | refused, `post_source_mismatch`, message names `loads` | `RecoveryIsRefusedForEveryStaleInput` | PM23 killed |
| a prescribed-temperature edit | refused, names `prescribed_temperatures` | same | PM23 |
| a convection edit | refused, names `convections` | same | PM23 |
| a solver-settings edit | refused, names `settings` | same | PM23 |
| a **conductivity edit** after the solve | refused, names `material`; `staleReasons` contains `Material`; `isCurrent` false | same ("the old temperatures are not post-processed with the new k") | PM23, PM35 |
| a model older than a material edit, offered with a **new** (current) solution | refused, `material_mismatch`, "prepare the model again" | same | PM25 killed (the cross-check) |
| a **remesh with the same node count, element count and ids** | refused: the stamp differs although every count and id agrees; the old result is stale, the new one current | `RemeshStalesTheResultEvenWhenCountsAndIdsAgree` (9 nodes, 12 tetrahedra, the same ids in both generations) | PM22 killed (stamp compared to counts only) |
| a transient time-step edit | refused, names `time_step`; `isCurrent` false | `TransientRecoveryOnTheBlock…` | PM24 killed, PM36 killed |
| a transient result asked about as steady | refused, "transient history" | `TransientRecoveryOnTheBlock…` | PM37 killed |
| a steady result asked about as transient | refused, "steady solution" | `RecoveryIsRefusedForEveryStaleInput` ("and the reverse") | PM38 killed |
| a held steady result after the model changed | `isCurrent` false | `RecoveryIsRefusedForEveryStaleInput` | PM35 killed |
| a held transient result after the model changed | `isCurrent` false | `TransientRecoveryOnTheBlock…` | PM36 killed |
| a **failed** recovery after a successful one | the old result stays stale; nothing new is published; the held values are still the old `k = 45` ones (225 000 W/m², not 270 000) | `AFailedRecoveryNeverMakesAnOldResultCurrent` | holds by construction (a result has no mutator and a refused recovery constructs nothing); PM31 (a steady source recorded as transient) is also killed by this case, among four |
| the numbering of another mesh | refused, `numbering_mismatch`; `temperatureOf` says "another numbering" | `RefusesWhatItCannotRecover…`, `ElementResultsAreKeyed…` | PM21, PM33 killed |

## Never new k with stale T

Three independent defences, each with its own test and mutation:

1. the **solution** is stale when the material (or its revision) differs from the one it was assembled with (check 1; PM23);
2. the **model** is refused when it carries a `k` that differs from the document's now (check 3; PM25), which is how an old `ThermalModel` could
   otherwise pair its stale `k` with a solution freshly solved for the new one;
3. the **result** is stale by `staleReasons` the moment the document's material revision moves (PM35).

Because `k` is not stored in the result (compile-fail `THE_RESULT_STORES_NO_CONDUCTIVITY`), it cannot be reused later without being read again.

## Mesh generation, not mesh size

The seam asks `mesh.owns(system.mesh)` (the stamp) together with the node count. A remesh that happens to reproduce the same counts and the same
`ElementId`s is still a different mesh, and the old recovery of it is stale and refused; recovering it from the *old* solution on the *new* mesh is
refused too (`recoverSteadyFromSolution` returns `post_source_mismatch`). PM22 compares counts only and is killed by this test alone.

## What the result is not

It is **derived** (RESULT_MODEL.md): not canonical, not persisted, not in the command history. Nothing here writes a document, so `currentness` is
always a question about identity; no array is read to answer it.
