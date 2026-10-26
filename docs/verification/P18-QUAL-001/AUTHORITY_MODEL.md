# P18-QUAL-001 - authority model audit (brief sections 13-15)

**Verdict: one authority per fact; no second authority found.**

The authority table of the brief, restated as what the code and tests establish. The row-by-row audit with `file:line` evidence and
every covering test is `audit/AUDIT_A_adr_authority_currentness_dof.md` section A2 (16 rows: 15 COVERED, 1 PARTIAL, 0 GAP).

| fact | sole owner | derived views (never authority) | enforced by |
|---|---|---|---|
| solid geometry, faces, regeneration | P15 CAD features (`Regenerator`) | the P16 mesh, `GeometryMeshMap`, every VIZ triangle | `requireThermalModel` asks P16's one geometry boundary (`geometryIneligibility`); `src/thermal` never reads a `Body` (search S8: 29 hits, all pass-through of the `Regenerator`) |
| material properties k, rho, cp | P15 materials (`requireThermalConductivity`, `requireTransientConductionProperties`) | the conductivity held by value in `ThermalModel`, re-resolved by every consumer | searches S3-S6: 0 stored properties, 0 literal properties, 0 material table, 5 hits all calls into P15; `architecture.thermalpersist-ownership.material-copy`, `thermalcli-ownership.material-property`; compile-fail `the-element-inspection-has-no-conductivity`, `the-result-stores-no-conductivity` |
| meshing intent | P16 `MeshControl` | the mesh, its stamp, the quality report | `Mesher::currency(document, control)` is exact (sizing intent + geometry revision) |
| thermal intent: mode, loads, boundary conditions, initial temperature, solver settings, transient settings | the `ThermalAnalysis` object's definition (P18-DATA) | everything below | the definition holds `FaceName`s and quantities only; compile-fail `thermalids.the-definition-has-no-*` (temperatures, matrix, result), `thermaldof.the-definition-has-no-dof-map` |
| node and element numbering, DOF numbering, constraint and free-equation maps | the mesh's own enumeration (P16), copied by `ThermalDofMap` | `ThermalDofIndex`, `ThermalFreeEquationIndex` | dense compact numbering (`ThermalDof_LargeSparseNodeIdDoesNotControlStorageSize`), distinct strong types (compile-fail group `thermaldof`, 13 cases) |
| K, C, Q, temperatures, histories, gradients, fluxes, reactions, balance, validation report, VIZ buffers | none: computed from the above on every run | all of them | never persisted (`ThermalPersistence_NoDerivedStateReachesTheFile`), never in history (`ThermalCommand_HistorySizeIndependentOfThermalResultSize`), never a stored "valid" or "stale" flag (compile-fail `thermalvalidation.the-report-has-no-stored-status`, `thermalviz.the-display-state-has-no-stale-flag`) |

## Searches (A2, repeated on the frozen tree by the final-gate test)

| search | scope | hits | reading |
|---|---|---:|---|
| mesh-local identity in canonical data, commands, persistence, CLI edits | `ThermalData`, `ThermalAnalysisObject`, `ThermalCommands`, `ThermalAnalysisJson.cpp`, `ThermalEdits.cpp` | 1 | `MeshStamp` inside `ThermalResultSource`, a provenance record that is not part of the definition and is never written |
| derived types in the same files | same | 0 | |
| material properties in the same files | same | 0 | |
| material database, default, "typical" property | `src/thermal`, `include/bettercad/thermal` | 0 | |
| literal conductivity, density or specific heat | same | 0 / 0 | |
| nearest-face, plane or centroid test deciding geometry | `src/thermal/*.cpp` | 3 | all in the time-grid rounding of `ThermalTransient.cpp`, unrelated to geometry |
| renderer stale flag | `src/renderer/Thermal*`, `include/bettercad/renderer/Thermal*` | 0 | |
| CLI mesh ids as input | `apps/bettercad_cli/Thermal*` | 4 | output-only: ids taken from the result through `ordinalOf` / `nodeOf` |
| Qt or OCCT in the thermal viewer files | `src/renderer/Thermal*` | 0 | |

## The executable statement

`P18Qualification_RunningTheChainLeavesEveryReferenceDocumentByteIdentical` (`tests/qualification/P18ThermalQualificationTests.cpp`)
saves every one of the 20 committed thermal reference documents, runs the whole production chain on it (regenerate, mesh, prepare
loads and conditions, assemble, solve, recover, balance, validate; the five refused documents included, so a refusal must not write
a diagnostic or a partial result into the document either), saves again, and requires the bytes, the object count and the document
revision to be unchanged. 15 accepted and 5 refused documents; every accepted one has a solution, recovered fields and a balance.

## Things a reviewer should know (none is a second authority)

1. `ThermalModel` holds the conductivity by value (`ThermalAnalysis.hpp`), a derived view; every consumer re-resolves it from the
   document and refuses a mismatch (`ThermalSystem.cpp`, `ThermalPost` `MaterialMismatch`).
2. `ThermalPostState.temperatures` is a copy of the solver's temperatures; the validator checks it bit for bit
   (`ThermalValidation_RejectsRecoveredTemperaturesThatAreNotTheSolversBitForBit`).
3. `ThermalResult` and `ThermalResultSlot` (P18-DATA) are a parallel currentness API that the production chain does not use (finding D-J,
   `FINDINGS.md`). They hold no authority; their tests protect a code path the product does not take.
4. Heat loads are integrated over the MESH facet areas and tetrahedron volumes. On a curved face Q follows the faceted area, not the CAD
   area. That is accuracy, not authority, and it is the P16 chord bound; the audit (A2a.4) records that no P18 test compares the
   two. Recorded as a limitation (`LIMITATIONS.md`), not closed: it concerns the accuracy of the faceted approximation of a curved face,
   which P16 bounds, and which no thermal acceptance rule claims to judge.
