# P18-VALID-001 — source compatibility and currentness

```text
RULES:    TV_GEOMETRY_CURRENT, TV_MESH_CURRENT (V01, V02) and TV_SOURCE_COMPAT (V08)
SUBJECT:  every derived object describes THIS document and every other derived object, by IDENTITY; never by size, count, id, timestamp, pointer or array content
```

## What currentness is made of

Identity comes from the qualified source types and nothing else. There is no timestamp (no `chrono` in the validator, gate-checked), no raw pointer, no comparison of a
temperature array, and no judgement from a vector's length.

| Object | Its source | Compared by |
| --- | --- | --- |
| geometry | the body's geometry revision | `requireThermalModel` → `meshing::geometryIneligibility` (P16): regenerated, current, a solid, valid |
| mesh | the `MeshStamp` (WHICH MESH AND WHICH GENERATION) | `Mesher::currency` against the document as it is NOW |
| numbering (`ThermalDofMap`) | the stamp and the node count | `ThermalDofMap::describes(mesh)` |
| prepared loads | body, mesh stamp, node count, geometry, the load list by value | `requireCurrent(PreparedThermalLoads, model, loads)` |
| prepared boundary | the same, with the condition list | `requireCurrent(PreparedThermalBoundary, model, conditions)` |
| assembled system | mesh, body, geometry, MODE, material identity AND revision, loads, convection conditions | `requireCurrent(ThermalSystem, document, model, mode, loads, conditions)` |
| steady solution | the system's source + the prescribed-temperature conditions + the solver settings | `requireCurrent(SteadyThermalSolution, …)` |
| transient solution | the same + initial temperature + time step, end time, output policy | `requireCurrent(TransientThermalSolution, …)` |
| recovered fields, heat balance | the solution's own source, by value (`postSourceOf`) | `requireCurrent(ThermalPostResult / ThermalBalanceResult, …)` |
| the report | `ThermalResultSource` (body, control, geometry, mesh, material + revision, analysis + revision, mode) + the policy fingerprint | `isCurrent(report, …)` |

Each owner's own comparison (`staleReasons`) is used; the validator holds no second one, so the vocabulary of reasons is theirs (`mesh`, `geometry`, `material`, `loads`,
`convections`, `prescribed_temperatures`, `settings`, `initial_condition`, `time_step`, `end_time`, `output_policy`, `mode`).

## The source-compatibility matrix (what `TV_SOURCE_COMPAT` checks)

```text
numbering          describes the CURRENT mesh
prepared loads     current for the model and the analysis's loads
prepared boundary  current for the model and the analysis's conditions
system             current for the model, the MODE, the loads and the conditions          system.source().loads == preparedLoads.source().loads
steady solution    current                                                                  solution.source().system == system.source()
transient solution current (initial, time settings, output included)                       solution.source().system == system.source()
recovered fields   current; their mode is the analysis's                                    post.source()    == postSourceOf(solution.source())
heat balance       current; its mode is the analysis's                                      balance.source() == postSourceOf(solution.source())
mode               a steady object for a transient analysis, or the reverse               -> mode_mismatch
```

An object that is not presented is not an error HERE: the rule that needs it reports its absence (`system_missing`, `solution_missing`, `post_missing`, `balance_missing`), so a partial
state (a system but no solution yet) is described honestly.

## Same counts, same ids, new generation (the hard adversarial case)

`RejectsSameCountsNewMeshSource`: the block is solved, recovered and balanced; then `remesh()` is called with nothing else changed. The new mesh has the SAME node count (9), the
SAME tetrahedron count (12) and the SAME NodeIds in the same order — and a different stamp. The report from the OLD derived objects:

```text
TV_MESH_CURRENT      PASS            the NEW mesh is current
TV_SOURCE_COMPAT     FAIL            seven errors, one per derived object that carries the old generation:
                                     dof_map, prepared_loads, prepared_boundary, system, solution, post_result, balance_result   (all source_stale, with the mesh named)
TV_WELL_POSED        SKIPPED         blocked by TV_SOURCE_COMPAT
TV_SOLVER_RESIDUAL…  SKIPPED
```

`RejectsSameIdsNewMeshSource` presents the OLD recovered fields, then the OLD balance, then the OLD solution alone with the NEW generation's other objects: each is refused by
itself, naming only itself. `RejectsMismatchedAssemblySource` presents a system assembled for ANOTHER load list with the same sizes: `source_stale` for the system and
`source_mismatch` (a solution that was not computed from the system beside it). A nodal array of the right size is not evidence of anything.

## What each edit stales (mutation matrix, steady and transient; `MATRIX` lines of the suite)

The mesh survives every edit but a geometry or meshing-intent edit (`mesher.currency` is asserted `Current` after each row: nothing asks for a remesh).

| Edit | Mesh | numbering | prepared loads | prepared boundary | system | solution | recovered | balance | Validation report |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| geometry edit (regenerated, not remeshed) | **stale** (`mesh_stale`) | — | — | — | — | — | — | — | stale |
| geometry edit, NOT regenerated | **`geometry_ineligible`** | — | — | — | — | — | — | — | stale |
| meshing intent (global target size) | **stale** | — | — | — | — | — | — | — | stale |
| remesh, same counts and ids | current (new) | **stale** | **stale** | **stale** | **stale** | **stale** | **stale** | **stale** | stale |
| load edit | current | — | stale | — | stale | stale | stale | stale | stale |
| prescribed-temperature edit | current | — | — | stale | — | stale | stale | stale | stale |
| convection edit | current | — | — | stale | stale | stale | stale | stale | stale |
| solver-settings edit | current | — | — | — | — | stale | stale | stale | stale |
| conductivity edit | current | — | — | — | stale | stale | stale | stale | stale |
| *transient:* initial-temperature edit | current | — | — | — | — | stale | stale | stale | stale |
| *transient:* time-step edit | current | — | — | — | — | stale | stale | stale | stale |
| *transient:* end-time edit | current | — | — | — | — | stale | stale | stale | stale |
| *transient:* output-schedule edit | current | — | — | — | — | stale | stale | stale | stale |
| *transient:* density edit | current | — | — | — | stale | stale | stale | stale | stale |
| *transient:* specific-heat edit | current | — | — | — | stale | stale | stale | stale | stale |
| a VIZ setting (colour map, camera, legend, component) | current | — | — | — | — | — | — | — | **NOT stale** (no such field exists in any source; see below) |
| an unrelated object added to the document | current | — | — | — | — | — | — | — | **NOT stale** (`InputEditStalesTheReportButAnUnrelatedEditDoesNot`) |

A prescribed-temperature edit does NOT stale the assembled system (a `T_bar` never enters `K`, `C` or `Q`) and does stale the solution (it changes `Tc`); the table is exactly that
dependency structure, and the test asserts the EXACT set of stale subjects for every row, so a dependency that is missing from a source (an edit that stales nothing) fails as surely as
one that is surplus.

## Lifecycle

* **R1 current and accepted → an input edit → R1 stale; the old report stale.** `InputEditStalesTheReportButAnUnrelatedEditDoesNot`: after a load edit `isCurrent(report)` is false.
  Editing the load BACK does not resurrect the old report (the analysis revision is part of the source): currentness is identity, not "the same values again".
* **A failed new solve cannot be rescued by an older result.** `FailedResolveDoesNotRevalidateOldResult`: with the source unchanged and a recorded failure of the last attempt the
  report is `FAIL` (`solve_failed`, with the failure's own message), the rules after the solve are skipped, and the same state without a recorded failure is accepted — it is the failure that
  decides. `ANewSolveThatFailsAfterAnEditLeavesNothingAccepted`: the old result is stale AND the new attempt failed; neither is accepted, with or without the old result presented.
* **A validation report of a stale result is `FAIL`, never a numerical PASS.** The stale rules are errors; the numerical rules after them are skipped, not evaluated.
* **The report itself is stale when the policy it was produced under is no longer in force** (`policyFingerprint`), checked by `isCurrent`.

## VIZ edits

A color map, a camera or a legend range must not stale an engineering validation. None is part of any source in the chain: `ThermalResultSource`, `SteadySolutionSource`,
`TransientSolutionSource` and `ThermalPostSource` hold no display field, and the compile-fail cases `THE_REPORT_SOURCE_HAS_NO_DISPLAY_SETTING` and
`THE_REPORT_SOURCE_HAS_NO_TOLERANCE_OVERRIDE` show one cannot be added without the build failing. P18-VIZ-001 does not exist yet, so the behaviour is demonstrated by the absence of any
field it could change; the first VIZ setting that did become part of a source would have to appear in these types and these tests.
