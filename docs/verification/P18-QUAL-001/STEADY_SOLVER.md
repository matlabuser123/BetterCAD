# P18-QUAL-001 - steady solve audit (brief sections 34-37)

**Verdict: exact Dirichlet partition, no penalty, no regularisation, an independent residual, and a datum-less model is refused.**
Row-by-row: `audit/AUDIT_B_element_loads_bcs_assembly_solvers_tolerances.md` section B9 (8 rows).

```text
Kff Tf = Qf - Kfc Tc         (exact partition; Tc reconstructed as the PRESCRIBED value, not zero)
r_free = Kff Tf - (Qf - Kfc Tc)    evaluated over the ORIGINAL CSR arrays, Eigen not consulted
normalized = ||r|| / (||K|| ||T|| + ||b||)   <= relativeResidualTolerance (1e-9, canonical, editable, persisted)
```

| property | where | tests |
|---|---|---|
| exact partition; a held 350 K is reconstructed as 350 K | `ThermalSolve.cpp:191-267`, reconstruction `:451` | `ThermalSolve_DirichletPartitionInterleavedConstraints`, `_NonzeroDirichletRhsMutationTrap`, `_MatchesIndependentDenseOracle`, `_LinearProfileOnAStructuredGrid`, `_AffineFieldPatch` |
| `Nfree = 0` is valid and no singular solve is attempted | `rows > 0` guard | `ThermalSolve_FullyConstrainedSystem` (kernel not invoked, pivot ratio 1, T equals Tc), `Linalg_EmptySystemIsSolved` |
| **pure Neumann steady fails**; no arbitrary temperatures, no pin | `MissingTemperatureDatum` | `ThermalSolve_PureNeumannSingular` (compatible **and** incompatible load both refused), `_PureNeumannChainOfManyNodes`, `ThermalReference_RM09_PureNeumannSteadyFails`; mutation QP09 (anchoring disabled) killed by 22 tests |
| Robin-only steady is valid | the convection anchors the null mode | `ThermalSolve_RobinOnlyNoDirichlet`, `_RobinAnchoredChain`, `_BlockConvectionOnlyNoDirichlet` |
| disconnected regions: every region must be anchored | union-find over the **non-zero** couplings (`ThermalSolve.cpp:273-335`) | `ThermalSolve_DisconnectedUnanchoredFails` ("1 of the model's 2"), `_DisconnectedAnchoredPasses`, `_ExplicitZerosDoNotConnectRegions`; **end to end from an assembled system**: `ThermalSolve_ADisconnectedMeshWithAnUnanchoredComponentFailsEndToEnd` (added by this milestone) |
| independent residual, finite and within tolerance | `SymmetricSolve.cpp:138-236` | `ThermalSolve_IndependentResidualAndAcceptance` (hand recomputation of both norms and the normalisation), `_ResidualToleranceIsScaleAware`, `Linalg_ResidualOverflowNamesTheRow`; mutation QP10 (the gate skipped) killed by 10 tests |
| corrupted-solution negative control | `validateSteadySolution` | `ThermalSolve_BadSolutionFailsResidual` (each free unknown +1 K refused; 1e-12 K accepted), `ThermalValidation_SolverSuccessDoesNotOverrideResidualFailure` |
| the backend's own success is never accepted, **inside the solve** | `acceptSolution` after `solver.solve` | `ThermalSolve_AnUnmeetableToleranceRefusesTheSolveItself` (added: a tolerance of 1e-30 makes the production solve return `ResidualTooLarge` with "The solver reported success"; the control solve at the default tolerance has a non-zero residual) |

## Findings carried

* **D3 (low)** the anchoring classifier is row-relative: a row is anchored iff `|sum| > 1e-10 sum|row|`. A genuinely weak but valid Robin
  anchor below that ratio would be refused as a missing datum rather than solved. Tests prove a 1e-6 anchor works and 3e-16 noise does
  not; nothing between is probed. Not changed (it is a conservative refusal, never a wrong number); recorded in `LIMITATIONS.md`.
* The solver-setting defaults (1e-9, 1e-12) exist in three places; `ThermalSolve_TheSolverSettingDefaultsAreTheKernelDefaults` (added) fails the day one moves alone.
