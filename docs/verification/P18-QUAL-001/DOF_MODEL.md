# P18-QUAL-001 - DOF model audit (brief sections 19-21)

**Verdict: one scalar temperature per node; three distinct index spaces; dense compact numbering; no structural assumption leaks.**
Row-by-row: `audit/AUDIT_A_adr_authority_currentness_dof.md` section A4 (10 rows, 10 COVERED, 0 PARTIAL, 0 GAP).

| requirement | evidence | tests |
|---|---|---|
| exactly one scalar temperature DOF per P16 node (`kThermalDofsPerNode = 1`; a Tet4 has 4 local DOFs) | `ThermalDof.hpp:79`, `ThermalElement.hpp:122-129` | `ThermalDof_OneScalarDofPerNode`, `ThermalDof_NodeToDofAndDofToNodeAreExactInverses`, `ThermalElement_LocalDofsAreOneScalarPerNode`, `ThermalDof_FullReconstructionOracle` |
| `NodeId`, `ThermalDofIndex` and `ThermalFreeEquationIndex` are distinct strong types | explicit constructors, deleted defaults, no conversion | 13 compile-fail cases in group `thermaldof` plus downstream ones (`thermalsolve.a-node-id-is-not-a-free-equation`, `thermalsys.a-node-id-is-not-a-row`, `thermalbalance.a-dof-index-is-not-a-node-id`, `thermalelem.corners-are-not-node-ids`) |
| numbering is dense 0..N-1, never `max(NodeId)+1` | the mesh's own ascending enumeration, copied not sorted (`ThermalDof.cpp:118-126`) | `ThermalDof_LargeSparseNodeIdDoesNotControlStorageSize`, `ThermalAssembly_LargeSparseNodeIdDoesNotControlMatrixSize`, `ThermalLoad_LargeSparseNodeIdDoesNotSizeTheVector`, `ThermalBC_LargeSparseNodeIdDoesNotSizeConstraintStorage` |
| sparse node ids (7, 42, 104, 9001, 500000) | hand-written oracle ordering by mesh ordinal | `ThermalDof_SparseNodeIds`, `ThermalAssembly_SparseNodeIds` (the full five), `ThermalSolve_SparseNodeIdsThroughTheSolve`, `ThermalTransient_SparseNodeIdsAreLookedUpThroughTheNumbering`, `ThermalPost_NodalTemperatureUsesThermalDofOrderForSparseNodeIds`, `ThermalViz_SparseNodeIdInspection`, `ThermalBalance_SparseNodeIdsKeyReactionsByUnknownAndNotByHandle` |
| identical duplicate prescribed temperatures deduplicate | exact equality (`ThermalDof.cpp:205-223`) | `ThermalDof_DuplicateIdenticalConstraintDeduplicates`, `ThermalBC_IdenticalOverlapAlongAnEdgeDeduplicates`, `ThermalBalance_DuplicateTemperatureBoundariesDoNotDoubleCountTheReaction` |
| conflicting prescribed temperatures fail (no first, last or mean) | `ThermalDof.cpp:212-220` | `ThermalDof_ConflictingConstraintFails`, `ThermalBC_ConflictingTemperatureRejected`, `ThermalRun_ConflictingPrescribedTemperaturesAreRefusedNotResolvedByOrder`, `ThermalCli_ConflictingPrescribedTemperaturesAreRefusedNotOrdered` |
| `Nfree = 0` is valid | the solve takes a `kernelInvoked = false` path | `ThermalSolve_FullyConstrainedSystem`, `ThermalTransient_FullyPrescribedModelSucceedsWithoutASolve`, `ThermalRun_FullyConstrainedSteadyIsAccepted`, `ThermalCli_FullyConstrainedSteady` |
| no penalty enforcement: the reduction is exact, `Kff Tf = Qf - Kfc Tc` | grep for penalty / 1e20 / diagonal epsilon: comments only | `ThermalBC_PreparedBoundaryHasNoPenaltyAssemblyOrSolve`, `ThermalDof_ConstraintMapStoresNonzeroTemperature` (a held 350 K is reconstructed as 350 K, not 0), compile-fail `the-system-has-no-penalty`, `the-settings-have-no-regularisation` (steady and transient) |
| no structural 3-DOF assumption | thermal links no structural target; no `3 *` DOF arithmetic; zero-based thermal vs one-based structural `DofIndex` | `ThermalDof_SolverLocalTypesAreDistinct`, `architecture.checker.thermal-to-structural`, `architecture.targets.thermal-links-structural` |
| the free numbering is bound to the pair it was built from | `ThermalDof.cpp:271-291` | `ThermalDof_FreeMapRejectsDifferentDofSource`, `ThermalDof_AChangedConstraintSetStalesTheFreeMapButNotTheDofMap` |

Mutations that attack this layer, and what killed them: QP13 (the mesh stamp not compared by the DOF map) and QP24 (the DOF index taken
from the `NodeId` value instead of the dense rank) - see `MUTATION.md`.

Identity note (finding D4, `FINDINGS.md`): a mesh's identity is its `MeshStamp` plus its node count. Two snapshots of one `MeshBuilder` with
the same node count and different elements are indistinguishable to `describes`. The `Mesher` always issues a fresh stamp, so this is
reachable only with hand-built meshes (`ThermalAssembly_SameBuilderLaterSnapshotIsNotTheSameMesh` covers the node-count variant).
