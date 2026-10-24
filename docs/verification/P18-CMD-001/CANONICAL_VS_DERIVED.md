# P18-CMD-001 — canonical versus derived

```text
THE CLAIM: the undo history carries canonical thermal intent and never a derived solver or result value.

PROVED THREE WAYS, each stronger than the one before:
  1. runtime tests show that no derived value WAS stored            (payload equals a hand-built pair of definitions)
  2. compile-fail cases show that none CAN be named or constructed  (52 cases, the control must compile)
  3. an architecture gate shows that this module's source reaches none of them (16 tests, 15 fixtures)
```

## The classification

| Data | Canonical history? | How that is enforced |
| --- | --- | --- |
| `ThermalAnalysisId`, the analysis definition | **YES** | every command is keyed on one; create stores the definition |
| `ThermalLoad` (id, kind, value, `FaceName`) | **YES** | the payload of add/edit; the `ThermalLoadId` addresses remove |
| `ThermalBoundaryCondition` (id, `FaceName`, values) | **YES** | likewise |
| `UniformInitialTemperature` | **YES** | one `double` in kelvin (`sizeof` asserted) |
| `ThermalSolverSettings` | **YES** | two ratios |
| `TransientSettings` | **YES** | step, end time, output policy |
| `FaceName` (the canonical GeometryReference) | **YES** | stored as given |
| | | |
| `NodeId`, `ElementId` as a target | **NO** | compile-fail: a node/element id is not an analysis, a load id or a condition id; a record has no node form |
| `ThermalDofIndex`, `ThermalFreeEquationIndex` | **NO** | compile-fail, and **not declared** in the command header |
| facet handles, `MeshStamp`, a resolved set | **NO** | gate rule 3; no member of any payload |
| `ThermalDofMap`, constraint and free-equation maps | **NO** | compile-fail: not declared in the command header |
| `PreparedThermalLoads`, `PreparedThermalBoundary` | **NO** | compile-fail: not declared |
| K, C, Q, `ThermalSystem`, factorisations | **NO** | compile-fail: not declared; gate rule 1 forbids the header |
| `SteadyThermalSolution`, `TransientThermalSolution`, temperature arrays and histories | **NO** | compile-fail: not declared |
| `ThermalPostResult`, gradient, heat flux, flux magnitude | **NO** | compile-fail: not declared |
| `ThermalBalanceResult`, reaction heat, the heat balance | **NO** | compile-fail: not declared |
| `ThermalValidationReport` | **NO** | compile-fail: not declared |
| `ThermalResult` (the result container), `ThermalModel` | **NO** | compile-fail: not declared |
| visualisation buffers, classifier output, render actors, a playback frame | **NO** | the renderer is a higher layer; gate rule 8 fails a renderer file that edits an analysis |

## Reachability, measured rather than asserted

A translation unit that includes **only** `ThermalCommands.hpp` was preprocessed (`g++ -E` with the `debug-ext` flags, 4.4 MB of output, re-measured after the review fixes) and searched for each name (whole-word for `ThermalResult`, because the *source stamp* `ThermalResultSource` is declared
by the analysis header and is not the result container):

```text
NOT DECLARED where a command is declared (0 occurrences)
  ThermalPostResult   ThermalSystem   SteadyThermalSolution   TransientThermalSolution
  ThermalBalanceResult   ThermalValidationReport   PreparedThermalLoads   PreparedThermalBoundary
  ThermalDofMap   ThermalDofIndex   ThermalFreeEquationIndex   ThermalResult   ThermalModel
  Eigen (any)   SparseMatrix   class Mesher   class Mesh   VolumeMesh

DECLARED, and PRE-EXISTING — not introduced by this milestone
  meshing::NodeId  meshing::ElementId  meshing::MeshStamp     (reached through ThermalAnalysisObject.hpp's
                                                               meshing handles, as they always were)
```

This is **stronger than P17-CMD-001's** (whose DOF family was reachable through a restraint header): here even the DOF index is unnameable from a command. The mesh-local `NodeId` and `ElementId` *are*
reachable, so they are probed as wrong **types** (they convert to no analysis, load or condition id, and no record takes one), not as unreachable names.

## No exception to declare

P17-CMD-001 carried one declared exception (`NodalForceLoad` names a `NodeId` by design). Thermal has none: a `ThermalLoad` is a surface flux on a `FaceName` or a volumetric source, and a
`ThermalBoundaryCondition` is a `FaceName` and a payload. Neither has a mesh-local form, so *nothing* in a thermal payload names a mesh entity.

## The derived side, for completeness

Reproducible from the canonical intent plus the current mesh and material, never persisted as authority, never undo payload: the numbering, the constraint and free-equation maps, the prepared loads and
conditions, K, C, Q, the temperature solution and history, the gradient, the heat flux and its magnitude, the reaction heat, the heat balance, the validation report, and every view of them.
**Undo restores the intent and nothing else.** Whether an older derived object is current again afterwards is decided by its own comparison, not by anything the history held ([CURRENTNESS.md](CURRENTNESS.md)).
