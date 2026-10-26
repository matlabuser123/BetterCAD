# P18-QUAL-001 - global assembly audit (brief sections 32-33)

**Verdict: K, C and Q are assembled sparsely, by summation, with no Dirichlet elimination; duplicate contributions add.**
Row-by-row: `audit/AUDIT_B_element_loads_bcs_assembly_solvers_tolerances.md` section B8 (5 rows: 4 COVERED, 1 PARTIAL - see below).

| property | tests |
|---|---|
| `K = sum Ke + sum Kconv`, `C = sum Ce`, `Q = surface + volume + Robin` | `ThermalAssembly_SingleTetCompositeSystemAnalytical`, `_TwoTetSharedNodeConductivity`, `_TwoTetSharedNodeCapacity`, `_SmallSystemMatchesIndependentDenseOracle` (a hand-derived dense scatter), `_EnergyIdentities` |
| no Dirichlet elimination in base assembly: K, C and Q are bit-identical with and without a prescribed temperature | `ThermalAssembly_PrescribedTemperaturesNeverEnterTheMatrices`; compile-fail `the-system-has-no-reduced-matrix`, `the-source-has-no-prescribed-temperatures` |
| **duplicate contributions SUM** (a test that fails if `+=` became `=`) | `ThermalAssembly_TwoTetDuplicateContributionsSum` (K(1,1) = 1.0 + 1.5, C(1,1) = 6, Q(1) = 3 - not 1, not 2), `_RepeatedConvectionAddsTwice`, `_ConvectionSharedNodesAccumulate`; mutation QP04 (the conductance scatter overwrites) killed by **234** tests |
| the Robin source and the heat loads reach the heat vector | mutation QP05 (omitted) killed by 120 tests, QP06 (prepared loads omitted) killed by 155 |
| one sparse slot per (row, column) from a symbolic pass; strictly ascending columns; no pruning of exact zeros | `ThermalAssembly_UsesSparseGlobalMatrices`, `_DisconnectedComponentsRemainBlockDiagonal` (`nonZeros() == 32`) |
| the assembled system is stale for the right reasons | `ThermalAssembly_StaleReasonsNameEachFieldAlone`, `_RejectsStaleMaterialSource`, `_RejectsStalePreparedLoad`, `_RejectsStalePreparedBC`, `_RemeshStalesAssembledSystem`, `_SameIdsNewMeshIsStale`, `_TransientCapacityEditStalesTheSystem` |
| independent dense oracle on a real mesh | `ThermalAssembly_BlockMatchesIndependentDenseOracle` (its own scatter; it trusts the separately qualified element kernel) |

## The one PARTIAL row: no dense Ndof x Ndof allocation

The large-mesh smoke tests (`ThermalAssembly_LargeMeshSparseSmoke`: 22^3 = 10 648 unknowns, 55 566 tetrahedra; the fine-cylinder and
solve smokes) assert the stored non-zero ratio (`nnz / Ndof^2 < 0.01`) and that the cases complete. None would fail if assembly built a
dense `Ndof x Ndof` temporary and freed it: no allocation or peak-memory assertion exists, and the counts are logged through `WARN`.
`src/thermal` includes no Eigen and contains no `n*n` allocation (greps in the audit), and the compile-fail cases
`the-matrix-has-no-dense-form` and `the-reduced-system-has-no-dense-form` forbid a dense accessor. **Recorded as a limitation**
(`LIMITATIONS.md`); the structural guarantee is the absence of the code and of any dense type in the public API, which the architecture
gates and the compile-fail cases keep absent.

## Duplicated constant (tolerance ownership)

`kSymmetryTolerance` (`ThermalSystem.cpp:47`) and `kEffectiveSymmetryTolerance` (`ThermalTransient.cpp:38`) are two independent `1.0e-13`
literals linked only by a comment (finding D6, `FINDINGS.md`, `TOLERANCES.md`). They agree; both are anonymous-namespace constants, so a
test cannot name them. Both gates are exercised: `ThermalAssembly_SparseSymmetryCheckSeesAMissingTranspose` (with the literal) and
`ThermalTransient_EffectiveMatrixRefusals` (an asymmetric capacity is refused).
