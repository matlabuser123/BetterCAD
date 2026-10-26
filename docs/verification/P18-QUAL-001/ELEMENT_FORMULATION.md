# P18-QUAL-001 - Tet4 thermal element and formulation audit (brief sections 22-27)

**Verdict: matches the formulation as written; the oracles are independent of the production kernel.**
Row-by-row: `audit/AUDIT_B_element_loads_bcs_assembly_solvers_tolerances.md` sections B1-B5 (26 rows: 26 COVERED).

## Formulation

```text
reference tetrahedron:  N1 = 1 - xi - eta - zeta,  N2 = xi,  N3 = eta,  N4 = zeta
J = [x2-x1, x3-x1, x4-x1]   (columns);   Vsigned = det(J)/6
grad_x N_i = J^-T grad_ref N_i
Ke = k V G^T G                       (4x4, W/K, naturally symmetric, positive semidefinite, rank 3)
Ce = rho cp V / 20 [2 1 1 1; 1 2 1 1; 1 1 2 1; 1 1 1 2]      (consistent, J/K, positive definite)
```

| property | where it is established | tests |
|---|---|---|
| local node order is the P16 order; no silent reorder | `cornersOf` reads `tetrahedron.nodes[i]` in stored order; an inverted element is refused and named, never swapped | `ThermalElement_CornersFromMeshInElementOrder`, `ThermalElement_InvertedTetRejected` ("swapping back restores validity: it was the order"), `ThermalElement_OddPermutationRejected` (all six transpositions) |
| signed volume, bit-identical to `meshing::signedVolume` | one predicate for P16 and P18 | `ThermalElement_VolumeAgreesWithMeshingSignedVolumeBitwise` |
| a transposed Jacobian is detected | the unit tetrahedron cannot see a transpose; the **general** tetrahedron against an oracle that inverts [1 x y z] can | `ThermalElement_GeneralTetGeometry` (25 random tets, `G rel <= 1e-12`); mutation QP02 killed by 69 tests |
| degeneracy tolerance is derived, not chosen | `B = 16 eps |e1||e2||e3|`, homogeneous of degree 3 (scale-free), the factor mirrored as a literal in the test | `ThermalElement_NearDegeneratePolicy`, `ThermalElement_RoundingNoiseIsBelowTheDegeneracyBound` |
| `abs(det J)` is a magnitude gate only; orientation and volume use the signed value | `ThermalElement.cpp:248` is the gate against the rounding bound (finding D4: it contradicts "no abs(detJ)" only on a grep) | `ThermalElement_InvertedTetRejected`, `ThermalReference_RM01_NegativeOrientationIsRefusedNotCorrected`; mutation QP01 killed by 9 tests |
| partition of unity measured, not by construction | all four gradients by full product | `ThermalElement_ShapeGradientPartitionOfUnity` |
| Ke natural symmetry, PSD, rank 3, `Ke 1 = 0` | no post-hoc symmetrisation anywhere | `ThermalElement_ConductionSymmetric` (exactly `0.0`), `_ConductionPositiveSemidefinite`, `_ConductionRankThree`, `_ConductionRowSumsZero` |
| consistent capacity, not the one-point rank-1 or lumped matrix | no lumped variant exists | `ThermalElement_OnePointQuadratureIsNotTheConsistentMatrix`, `_CapacityConsistentPolicy`, `_CapacityFullRank` (eigenvalues `rho cp V/4` once, `rho cp V/20` three times); mutation QP03 killed by 16 tests; compile-fail `the-capacity-has-no-lumped-variant` |
| rho and cp are separate, finite, positive | | `ThermalElement_CapacityRejectsInvalidDensity`, `_RejectsInvalidSpecificHeat`; compile-fail `rho-and-cp-are-not-interchangeable` |
| null mode: pure conduction `K 1 ~ 0`; no epsilon, anchor, auto-pin or penalty | | `ThermalAssembly_PureConductionPreservesConstantNullMode`, `_PrescribedTemperaturesNeverEnterTheMatrices` (bit-identical K, C, Q with and without a Dirichlet), `ThermalReference_RM09_PureNeumannSteadyFails` |
| no pruning of small entries | | `ThermalAssembly_DisconnectedComponentsRemainBlockDiagonal` (`nonZeros() == 32`; pruning exact zeros would lower it) |

## Independence of the oracle

The element oracle is test-local (`tests/thermal/ThermalElementTests.cpp`: inverse of [1 x y z], a 4x4 determinant, hand `Ce`), and the
reference suite has its own header `tests/reference/ThermalAnalytic.hpp`, which includes nothing from BetterCAD
(`ThermalReference_Independence_OracleIncludesNoProductionCode`, and `ThermalReference_Independence_TheGateCanFail` proves that gate
fails when it should).
