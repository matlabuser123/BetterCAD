# P18-VALID-001 — the structured report

```text
TYPE:      ThermalValidationReport   (include/bettercad/thermal/ThermalValidation.hpp)
PRODUCER:  validateThermalAnalysis(document, regenerator, mesher, analysis, derived-state)
SHAPE:     15 checks in a fixed order · findings in a fixed order · metrics · the source it describes · the policy fingerprint
DERIVED:   status() · accepted() · solveAccepted() · inputsAccepted() · errorCount() · warningCount() · infoCount()   (computed on every call; nothing is stored)
```

## What a report holds

| Part | Content | Order |
| --- | --- | --- |
| `checks` | one per rule, `TV_*` id, `CheckState` (Evaluated, Skipped, NotApplicable), the rule that blocked it when skipped | the 15 rules of `kValidationRules` |
| `findings` | rule, stable code name, severity (a function of the code), subject, metric, element, step, time, step size, value, threshold, count, unit, upstream cause, message | rule, then severity (errors first), code, element id, step |
| `metrics` | tetrahedron count, unknowns, free equations, constrained unknowns, steady residual and its tolerance, worst transient residual with its step, time and step size, steps, worst NORMALIZED balance residual with its step and time, temperature range, maximum \|q\| — information, never a verdict | fixed |
| `worstByMetric` | the worst value of each of the four mesh metrics and the element that holds it | the metric enumeration |
| `source` | the `ThermalResultSource` (body, control, geometry revision, mesh stamp, material and its revision, analysis and its revision, mode) the report was produced for | — |
| `resultSource` | the post-source of the solution that was judged | — |
| `policyFingerprint` | `p18-valid-v1:<16 hex>` of the rule order, every code's rule and severity, the mesh bounds and the authorities consumed | — |

There is no timestamp, no pointer, no host name and no random value in a report. A report is current exactly while its source equals a freshly computed one **and** its fingerprint is the one in force.

The evidence lines below are what the real-document tests print (`REPORT`, `FINDING`, `METRICS`, `OVERALL`, `FP`). `FP` is a digest of every classification in the report (not of the
numbers), and it is the line the post-run script compares across the three presets. A skipped rule names its blocker; a rule that does not apply to the mode says so.

## 1. A steady analysis that passes (the steel block: caps held at 400 K and 330 K, a convecting side, an applied side flux and generation)

```text
REPORT steady | TV_ANALYSIS_DEFINITION | PASS
REPORT steady | TV_GEOMETRY_CURRENT | PASS
REPORT steady | TV_MESH_CURRENT | PASS
REPORT steady | TV_MESH_STRUCTURAL | PASS
REPORT steady | TV_MESH_QUALITY | PASS
REPORT steady | TV_MATERIAL | PASS
REPORT steady | TV_LOADS | PASS
REPORT steady | TV_BCS | PASS
REPORT steady | TV_TRANSIENT_SETUP | NOT_APPLICABLE
REPORT steady | TV_SOURCE_COMPAT | PASS
REPORT steady | TV_WELL_POSED | PASS
REPORT steady | TV_SOLVER_RESIDUAL | PASS
REPORT steady | TV_RESULT_FINITE | PASS
REPORT steady | TV_POST | PASS
REPORT steady | TV_BALANCE | PASS
METRICS steady | tets 12 | dof 9 | free 1 | constrained 8 | steady residual 0 | tolerance 1e-09 | worst transient residual - at step - t - dt - | steps - | worst balance 8.598964038013002e-16 at step 0 t - | T range 330 .. 400 K | max |q| 157721.3114564908
OVERALL steady | PASS | errors 0 | warnings 0 | info 0 | solveAccepted yes | policy p18-valid-v1:f97b835a659f144e
FP steady 699726b5b995f361
```

## 2. A transient analysis that passes (caps at 400 K and 300 K, 2.4 W generated, 7 s steps to 100 s: 15 steps with a shortened last step)

```text
REPORT transient | TV_ANALYSIS_DEFINITION | PASS
REPORT transient | TV_GEOMETRY_CURRENT | PASS
REPORT transient | TV_MESH_CURRENT | PASS
REPORT transient | TV_MESH_STRUCTURAL | PASS
REPORT transient | TV_MESH_QUALITY | PASS
REPORT transient | TV_MATERIAL | PASS
REPORT transient | TV_LOADS | PASS
REPORT transient | TV_BCS | PASS
REPORT transient | TV_TRANSIENT_SETUP | PASS
REPORT transient | TV_SOURCE_COMPAT | PASS
REPORT transient | TV_WELL_POSED | PASS
REPORT transient | TV_SOLVER_RESIDUAL | PASS
REPORT transient | TV_RESULT_FINITE | PASS
REPORT transient | TV_POST | PASS
REPORT transient | TV_BALANCE | PASS
FINDING transient | TV_SOLVER_RESIDUAL | initial_condition_overridden_by_prescription | info | 4 unknown(s) had an initial temperature different from their prescription; the prescription dominates at t = 0
METRICS transient | tets 12 | dof 9 | free 1 | constrained 8 | steady residual - | tolerance 1e-09 | worst transient residual 9.0694901599024e-17 at step 1 t 7 dt 7 | steps 15 | worst balance 1.0845929202170076e-15 at step 15 t 100 | T range 300 .. 400 K | max |q| 225442.62291156355
OVERALL transient | PASS | errors 0 | warnings 0 | info 1 | solveAccepted yes | policy p18-valid-v1:f97b835a659f144e
FP transient 3a0874bbcfd3c5ad
```

The one Information finding is true and harmless: the 400 K cap overrides the 300 K initial temperature at t = 0.

## 3. A model accepted with warnings (a thin plate: past the envelope of every qualified mesh, inside the accuracy bound)

```text
REPORT warning | TV_ANALYSIS_DEFINITION | PASS
REPORT warning | TV_GEOMETRY_CURRENT | PASS
REPORT warning | TV_MESH_CURRENT | PASS
REPORT warning | TV_MESH_STRUCTURAL | PASS
REPORT warning | TV_MESH_QUALITY | WARN
REPORT warning | TV_MATERIAL | PASS
REPORT warning | TV_LOADS | PASS
REPORT warning | TV_BCS | PASS
REPORT warning | TV_TRANSIENT_SETUP | NOT_APPLICABLE
REPORT warning | TV_SOURCE_COMPAT | PASS
REPORT warning | TV_WELL_POSED | PASS
REPORT warning | TV_SOLVER_RESIDUAL | PASS
REPORT warning | TV_RESULT_FINITE | PASS
REPORT warning | TV_POST | PASS
REPORT warning | TV_BALANCE | PASS
FINDING warning | TV_MESH_QUALITY | element_outside_qualified_envelope | warning | tet_radius_ratio 0.00010765757878429911 of element 3 is outside the range of every qualified P16 reference mesh (bound 3e-04)
FINDING warning | TV_MESH_QUALITY | element_outside_qualified_envelope | warning | tet_max_dihedral_angle 3.129093079838016 of element 3 is outside the range of every qualified P16 reference mesh (bound 3.125)
FINDING warning | TV_MESH_QUALITY | element_outside_qualified_envelope | warning | tet_aspect_ratio 133.33708328060044 of element 5 is outside the range of every qualified P16 reference mesh (bound 81)
FINDING warning | TV_LOADS | no_loads_configured | info | the analysis applies no heat load
METRICS warning | tets 12 | dof 9 | free 1 | constrained 8 | steady residual 8.660501099337443e-17 | tolerance 1e-09 | worst transient residual - at step - t - dt - | steps - | worst balance 5.052748343182936e-16 at step 0 t - | T range 300 .. 400 K | max |q| 15000000.000000017
OVERALL warning | PASS_WITH_WARNINGS | errors 0 | warnings 3 | info 1 | solveAccepted yes | policy p18-valid-v1:f97b835a659f144e
FP warning f502e9a5f2b1b62b
```

## 4. A stale mesh (the geometry changed after the mesh was cut)

```text
REPORT stale-mesh | TV_ANALYSIS_DEFINITION | PASS
REPORT stale-mesh | TV_GEOMETRY_CURRENT | PASS
REPORT stale-mesh | TV_MESH_CURRENT | FAIL
REPORT stale-mesh | TV_MESH_STRUCTURAL | SKIPPED (blocked by TV_MESH_CURRENT)
REPORT stale-mesh | TV_MESH_QUALITY | SKIPPED (blocked by TV_MESH_CURRENT)
REPORT stale-mesh | TV_MATERIAL | PASS
REPORT stale-mesh | TV_LOADS | SKIPPED (blocked by TV_MESH_CURRENT)
REPORT stale-mesh | TV_BCS | SKIPPED (blocked by TV_MESH_CURRENT)
REPORT stale-mesh | TV_TRANSIENT_SETUP | NOT_APPLICABLE
REPORT stale-mesh | TV_SOURCE_COMPAT | SKIPPED (blocked by TV_MESH_CURRENT)
REPORT stale-mesh | TV_WELL_POSED | SKIPPED (blocked by TV_MESH_CURRENT)
REPORT stale-mesh | TV_SOLVER_RESIDUAL | SKIPPED (blocked by TV_MESH_CURRENT)
REPORT stale-mesh | TV_RESULT_FINITE | SKIPPED (blocked by TV_MESH_CURRENT)
REPORT stale-mesh | TV_POST | SKIPPED (blocked by TV_MESH_CURRENT)
REPORT stale-mesh | TV_BALANCE | SKIPPED (blocked by TV_MESH_CURRENT)
FINDING stale-mesh | TV_MESH_CURRENT | mesh_stale | error | meshing control 3 has no mesh that describes the model: stale: the geometry changed
METRICS stale-mesh | tets - | dof - | free - | constrained - | steady residual - | tolerance - | worst transient residual - at step - t - dt - | steps - | worst balance - at step - t - | T range - .. - K | max |q| -
OVERALL stale-mesh | FAIL | errors 1 | warnings 0 | info 0 | solveAccepted no | policy p18-valid-v1:f97b835a659f144e
FP stale-mesh 74e0d862780c4204
```

Everything that needs the mesh is **skipped**, not failed and not passed: a cascade of numerical findings on a model that does not exist would bury the cause. The material rule does not
need a mesh, and is still judged.

## 5. A singular steady model (pure Neumann: no temperature datum)

```text
REPORT steady-pure-neumann | TV_ANALYSIS_DEFINITION | PASS
REPORT steady-pure-neumann | TV_GEOMETRY_CURRENT | PASS
REPORT steady-pure-neumann | TV_MESH_CURRENT | PASS
REPORT steady-pure-neumann | TV_MESH_STRUCTURAL | PASS
REPORT steady-pure-neumann | TV_MESH_QUALITY | PASS
REPORT steady-pure-neumann | TV_MATERIAL | PASS
REPORT steady-pure-neumann | TV_LOADS | PASS
REPORT steady-pure-neumann | TV_BCS | PASS
REPORT steady-pure-neumann | TV_TRANSIENT_SETUP | NOT_APPLICABLE
REPORT steady-pure-neumann | TV_SOURCE_COMPAT | PASS
REPORT steady-pure-neumann | TV_WELL_POSED | FAIL
REPORT steady-pure-neumann | TV_SOLVER_RESIDUAL | SKIPPED (blocked by TV_WELL_POSED)
REPORT steady-pure-neumann | TV_RESULT_FINITE | SKIPPED (blocked by TV_WELL_POSED)
REPORT steady-pure-neumann | TV_POST | SKIPPED (blocked by TV_WELL_POSED)
REPORT steady-pure-neumann | TV_BALANCE | SKIPPED (blocked by TV_WELL_POSED)
FINDING steady-pure-neumann | TV_BCS | no_boundary_conditions_configured | info | the analysis has no boundary condition
FINDING steady-pure-neumann | TV_WELL_POSED | no_temperature_datum | error | no part of the model is anchored to an absolute temperature: 1 independent region(s), none with a prescribed temperature or a positive convection coefficient
METRICS steady-pure-neumann | tets 12 | dof 9 | free 9 | constrained 0 | steady residual - | tolerance - | worst transient residual - at step - t - dt - | steps - | worst balance - at step - t - | T range - .. - K | max |q| -
OVERALL steady-pure-neumann | FAIL | errors 1 | warnings 0 | info 1 | solveAccepted no | policy p18-valid-v1:f97b835a659f144e
FP steady-pure-neumann bff6412a1f0215a4
```

## 6. A corrupted residual (a free temperature moved by 0.25 K, still finite, still plausible; the balance and the recovered fields untouched)

```text
REPORT bad-residual | TV_ANALYSIS_DEFINITION | PASS
REPORT bad-residual | TV_GEOMETRY_CURRENT | PASS
REPORT bad-residual | TV_MESH_CURRENT | PASS
REPORT bad-residual | TV_MESH_STRUCTURAL | PASS
REPORT bad-residual | TV_MESH_QUALITY | PASS
REPORT bad-residual | TV_MATERIAL | PASS
REPORT bad-residual | TV_LOADS | PASS
REPORT bad-residual | TV_BCS | PASS
REPORT bad-residual | TV_TRANSIENT_SETUP | NOT_APPLICABLE
REPORT bad-residual | TV_SOURCE_COMPAT | PASS
REPORT bad-residual | TV_WELL_POSED | PASS
REPORT bad-residual | TV_SOLVER_RESIDUAL | FAIL
REPORT bad-residual | TV_RESULT_FINITE | PASS
REPORT bad-residual | TV_POST | PASS
REPORT bad-residual | TV_BALANCE | PASS
FINDING bad-residual | TV_SOLVER_RESIDUAL | residual_above_tolerance | error | the independent residual is too large: ||r||_2 = 0.49723866964454144 W, ||r||_inf = 0.35437500536900757 W, normalized = 7.023264910706577e-05 against a threshold of 1e-09. The solver reported success; BetterCAD does not
METRICS bad-residual | tets - | dof 9 | free 5 | constrained 4 | steady residual 7.023264910706577e-05 | tolerance 1e-09 | worst transient residual - at step - t - dt - | steps - | worst balance 1.2952601953960332e-14 at step 0 t - | T range 300 .. 300.6038842988629 K | max |q| 1249.2214437729854
OVERALL bad-residual | FAIL | errors 1 | warnings 0 | info 0 | solveAccepted no | policy
FP bad-residual 1dcc805ede441069
```

This and the next report judge the evidence of a real analysis after one number has been edited, through `judgeSolveEvidence` — the function the model-level decision itself calls.
The rules before the solve are shown as the all-passing header the evidence test supplies.

## 7. A corrupted balance (0.5 W of convection heat that goes nowhere; the algebra is fine)

```text
REPORT bad-balance | TV_ANALYSIS_DEFINITION | PASS
REPORT bad-balance | TV_GEOMETRY_CURRENT | PASS
REPORT bad-balance | TV_MESH_CURRENT | PASS
REPORT bad-balance | TV_MESH_STRUCTURAL | PASS
REPORT bad-balance | TV_MESH_QUALITY | PASS
REPORT bad-balance | TV_MATERIAL | PASS
REPORT bad-balance | TV_LOADS | PASS
REPORT bad-balance | TV_BCS | PASS
REPORT bad-balance | TV_TRANSIENT_SETUP | NOT_APPLICABLE
REPORT bad-balance | TV_SOURCE_COMPAT | PASS
REPORT bad-balance | TV_WELL_POSED | PASS
REPORT bad-balance | TV_SOLVER_RESIDUAL | PASS
REPORT bad-balance | TV_RESULT_FINITE | PASS
REPORT bad-balance | TV_POST | PASS
REPORT bad-balance | TV_BALANCE | FAIL
FINDING bad-balance | TV_BALANCE | balance_out_of_tolerance | error | the heat balance residual 0.5000000000000622 W of state 0 exceeds the policy 1.1016825461799441e-09 W
METRICS bad-balance | tets - | dof 9 | free 5 | constrained 4 | steady residual 5.082602518057071e-17 | tolerance 1e-09 | worst transient residual - at step - t - dt - | steps - | worst balance 0.10416666666668101 at step 0 t - | T range 300 .. 300.3850885518848 K | max |q| 1249.2214437729854
OVERALL bad-balance | FAIL | errors 1 | warnings 0 | info 0 | solveAccepted yes | policy
FP bad-balance f90ffc773f618a62
```

The metric `worst balance` is the NORMALIZED residual; the finding states the residual in watts against the policy of `balanceWithinTolerance`.

## More reports (all in the test output)

| Name | Verdict | Where |
| --- | --- | --- |
| `missing-mesh`, `missing-control`, `body-removed`, `geometry-not-regenerated`, `generation-failed` | FAIL at the geometry or mesh rule | `ThermalValidationMappedTests.cpp` |
| `same-counts-new-source` | FAIL: seven derived objects named stale | same |
| `no-material`, `steady-k-only`, `transient-k-only` | material rule | same |
| `unresolved-load`, `conflicting-bc` | load and condition rules | same |
| `steady-robin-only`, `steady-all-faces-prescribed`, `transient-pure-neumann` | PASS (well posed by convection, by full prescription, by capacity) | same |
| `no-system` | FAIL: nothing assembled to judge | same |
| `transient-final-only`, `transient-too-many-steps` | history contract and the resource guard | same |
| `quality-reject-aspect`, `quality-reject-radius` | FAIL at the quality rule on a real mesh | same |
