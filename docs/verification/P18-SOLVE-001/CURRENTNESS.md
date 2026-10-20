# P18-SOLVE-001 — source compatibility, staleness and the result lifecycle

```text
SUBJECT:  how a solve refuses to mix generations, what a solution is bound to, and what a failed
          solve does and does not do to a held result
```

## What a solution records

A value, not a hash: no pointer, no address, no timestamp.

```text
SteadySolutionSource {
    ThermalSystemSource  system        body, mesh stamp, node count, geometry revision, mode, material and
                                       revision, the canonical loads, the canonical CONVECTION conditions
    vector<ThermalBoundaryCondition> temperatures
                                       the canonical PRESCRIBED-TEMPERATURE conditions, by value
    ThermalSolverSettings settings     the two tolerances
}
```

The assembled system deliberately carries no prescribed temperature (it never enters `K`, `C` or `Q`),
so the solution adds them, and the settings that decide acceptance. `staleReasons` names which moved:
`body, mesh, geometry, mode, material, loads, convections, prescribed_temperatures, settings`.

## The checks, in order (`solveSteadyThermal`)

All before any arithmetic, so a stale input is reported as one and never as a numerical symptom.

| # | Check | Refusal |
| --- | --- | --- |
| 1 | the settings | `invalid_settings` |
| 2 | the assembled system against the model, loads, conditions and material (`requireCurrent`) | `stale_system` |
| 3 | the prepared boundary was built for this numbering | `dof_map_mismatch` |
| 4 | the prepared boundary against the same conditions | `stale_boundary` |
| 5 | (seam) the system is a STEADY one | `not_steady_system` |
| 6 | (seam) the system describes the numbering | `dof_map_mismatch` |
| 7 | (seam) the constraints are the numbering's | `constraint_mismatch` |
| 8 | (algebra) the matrix, vector, numbering and constraints agree in size | `dof_map_mismatch` |

## The source-compatibility table

| Source pair | Compatible? | Evidence |
| --- | --- | --- |
| system S1, model M1, boundary B1, constraints C1 | yes | solves; `isCurrent` true |
| system S1, model M2 (remesh, same counts, same ids) | NO — `stale_system` (mesh) | `RemeshStalesTheSolutionAndRefusesTheOldSystem` |
| numbering D1 against system S2 | NO — `dof_map_mismatch` | same |
| constraints C1 against system S2 | NO — `constraint_mismatch` (the stamps differ though counts agree) | same |
| prepared boundary B1 against numbering D2 | NO — `dof_map_mismatch` | same |
| boundary B1, conditions with `T_bar` edited | NO — `stale_boundary` ("conditions") | `RejectsStaleBoundaryAfterATemperatureEdit` |
| system S1, conditions with `T_bar` edited | **yes** — `K`, `C`, `Q` do not contain it | the same test: S1 is reused with a fresh boundary |
| system S1, conditions with `h` edited | NO — `stale_system` (convections) | `EditsStaleExactlyWhatTheyChange` |
| system S1, loads edited | NO — `stale_system` (loads) | same |
| system S1, conductivity edited | NO — `stale_system` | same (the material revision moved) |
| system S1, solver settings edited | **yes**; the SOLUTION is stale (settings) and re-solving works | same |
| transient system | NO — `not_steady_system` | `SeamRefusals` |

## What stales a solution

```text
remesh                      mesh                 even with identical counts and identical NodeIds
material edit               material
load edit                   loads
convection edit             convections
prescribed temperature edit prescribed_temperatures        (the assembled system survives it)
solver settings edit        settings                       (the assembled system survives it)
geometry edit               geometry / body
```

Each is a test (`StaleReasonsNameEachFieldAlone` for the comparison, `EditsStaleExactlyWhatTheyChange`
and the remesh test for the model-level behaviour). `SolutionRecordsExactlyItsInputs` compares every
recorded field with the INPUT, not with a second call of the same function, which is the lesson of the
previous milestone's survivors.

## The lifecycle around a failed solve

P18-DATA-001's `ThermalResultSlot` holds at most one result and one failure. A failed solve records the
failure and **touches nothing else**:

```text
R1 current                                                  analysisState = solved_current
the model is edited into a datum-less one                   R1 stale          solved_stale
the new solve fails (missing_temperature_datum)             slot.recordFailure(error)
R1 after the failure                                        STILL held, still STALE (resultCurrency = stale)
latest attempt                                              FAILED          analysisState = solve_failed
a new current result                                        NONE

no previous result, the first solve fails                   slot.result() == nullptr, slot.failure() set, solve_failed
```

The held result in these tests is a `ThermalResult` with zero gradient and flux channels, because only
its source and its temperatures matter to the lifecycle. The solve itself returns a
`SteadyThermalSolution` and not a `ThermalResult`: that type carries a gradient and a flux per element
and a heat balance, which are P18-POST-001's and P18-BALANCE-001's, and a half-filled one would claim
to be a result and not be (P17's `SolvedSystem` stood in the same relation to `StructuralResult`).