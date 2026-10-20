# P18-SOLVE-001 — adversarial review

```text
SUBJECT:  the brief's 30 attack questions and seven more of the reviewer's, each answered with a
          test, a probe or an audit
RESULT:   0 production defects; 1 documentation defect (fixed); 2 test gaps (found by mutation,
          closed); 0 gate-blocking
```

Probe ids are in MUTATIONS.md.

## The brief's questions

| # | Attack | Answer | Evidence |
| --- | --- | --- | --- |
| 1 | Can P18 include or link the P17 layer-50 solver despite ADR-050? | No. The layering check refuses the include (ARCH2), a link-graph check refuses the target (ARCH1), and each has a fixture holding exactly that violation | `architecture.layering`, `architecture.targets.*`, `architecture.checker.thermal-to-structural` |
| 2 | Can the P17 solver be hand-copied into thermal? | No. The kernel exists once, in `src/linalg/SymmetricSolve.cpp`; `src/thermal` holds no `Eigen`, no `SimplicialLDLT`, no `vectorD` and no elimination (audit below) | grep audit; ADR_050_COMPLIANCE.md |
| 3 | Can the extraction accidentally include structural types? | No. `linalg` is layer 5 and may include only core; its CMake file links core and Eigen only; two fixtures refuse a thermal and a structural include from it | `architecture.checker.linalg-to-*`, `architecture.targets.link-graph` |
| 4 | Can thermal use a different hand-written linear solver? | No: the only numerical routines in `ThermalSolve.cpp` are the reduction (an index-mapped copy), the anchoring analysis (union-find, a row sum) and a full-residual product; none solves | audit |
| 5 | Can solver status alone mark a solve successful? | No. The residual is computed by BetterCAD from the original arrays and gated; a finite wrong answer is shown to the gate and refused | `BadSolutionFailsResidual`, `Linalg_ResidualIsEvaluableWithoutASolve`; probes L02, L05, L09 |
| 6 | Can `Kfc Tc` be omitted? | No: the one-free-node answer would be 50 K, not 350 K | `NonzeroDirichletRhsMutationTrap`, `OneFreeNodeAnalytical`; probes S01, S03 |
| 7 | Can prescribed temperatures be treated as zero? | No | the same; probe S02 |
| 8 | Can free and prescribed unknowns be assumed contiguous? | No: the reduction takes both orders from P18-DOF's maps; the fixtures interleave them | `DirichletPartitionInterleavedConstraints`; probes S04, S05 |
| 9 | Can `ThermalDofIndex` be confused with `ThermalFreeEquationIndex`? | No: different types, no conversion either way | compile-fail AN_UNKNOWN_IS_NOT_A_FREE_EQUATION, A_FREE_EQUATION_IS_NOT_AN_UNKNOWN, A_NODE_ID_IS_NOT_A_FREE_EQUATION |
| 10 | Can `Nfree = 0` be treated as singular? | No: `T = Tc`, the kernel is not invoked, `Q` is irrelevant to `T` | `FullyConstrainedSystem`; probe S06 |
| 11 | Can a no-Dirichlet model be rejected although Robin anchors it? | No | `RobinOnlyNoDirichlet`, `BlockConvectionOnlyNoDirichlet`, `ConvectionAnchorsAFiniteElementModel`; probe S07 |
| 12 | Can a pure-Neumann model return an arbitrary finite solution? | No, compatible or incompatible load | `PureNeumannSingular`, `PureNeumannChainOfManyNodes`, `BlockWithoutAnyDatumIsRefused`; probes S10, L01 |
| 13 | Can the solver pin one unknown silently? | No | probe S09 (a large diagonal on the first free unknown) is killed by 36 cases |
| 14 | Can an epsilon diagonal regularize a singular `Kff`? | No | probe S08 |
| 15 | Can a pseudo-inverse produce an accepted gauge solution? | There is no such code path; the singular fixtures are refused and removing either refusal (S10, L01) is caught | audit; SINGULARITY.md |
| 16 | Can a disconnected unanchored component be missed? | No | `DisconnectedUnanchoredFails`, `ExplicitZerosDoNotConnectRegions`; probes S21, S22 |
| 17 | Can all disconnected regions be rejected although each is anchored? | No | `DisconnectedAnchoredPasses`; probe S23 |
| 18 | Can NaN or infinity in the solution pass? | No | `RejectsNonFiniteSolutionAndResidual`; probes L03, S28 |
| 19 | Can a NaN residual pass accidentally? | No: the residual is tested for finiteness per row AND in its norms before any comparison with the tolerance | `Linalg_ResidualOverflowNamesTheRow`; probe L04 |
| 20 | Can the full residual at prescribed unknowns be mistaken for a solver failure? | No: those entries are the boundary heat rates and are retained | `FreeResidualMatchesFullFreeComponents`; probe S11 |
| 21 | Can an old assembled system solve after a remesh because dimensions match? | No: same counts, same ids, a different stamp: refused before any arithmetic | `RemeshStalesTheSolutionAndRefusesTheOldSystem`; probes S12, S26 |
| 22 | Can a stale constraint map be applied to a new `K`? | No: `constraint_mismatch` (stamps differ though counts agree) | the same |
| 23 | Can an old stale result become current after a failed re-solve? | No: the failure is recorded and the result is untouched | `FailedResolveDoesNotCreateACurrentResult` |
| 24 | Can the solver mutate `K` or `Q` in place? | No: every input is a const reference; the inputs are copied before and after and compared | `SolveDoesNotMutateItsInputs`, `SolveDoesNotMutateTheModelOrTheSystem` |
| 25 | Can `Kff` be made dense in production? | No: no dense allocation and no `toDense` in `ThermalSolve.cpp` or the kernel; the 18^3 and cylinder solves report `nnz(Kff)` against `Nfree^2` (70 766 against 26.9 million; 8 500 against 571 536) | audit; compile-fail ..._NO_DENSE_FORM |
| 26 | Can residual acceptance use only an absolute tolerance? | No: the metric is normalized; replacing it by the raw norm is caught | probe L05; `ResidualToleranceIsScaleAware` |
| 27 | Can scaling `K` and `Q` by 1e6 change PASS or FAIL incorrectly? | No: five scales from 1e-6 to 1e6 give the same temperatures to 5.7e-14 K and a normalized residual in [1.7e-17, 6.3e-17] | `SystemScalingInvariant`, `Linalg_ScalingInvariant` |
| 28 | Can the production solver be reused as its own oracle? | No: every expected value is typed in from a derivation or comes from the test's own elimination | ANALYTICAL_ORACLES.md |
| 29 | Can different presets classify singularity differently? | Measured, not argued | DETERMINISM.md |
| 30 | Can zero targeted tests still report PASS? | No | `ZeroMatchGuard.cmake`, 13 filters for this milestone |

## The reviewer's additional questions

| # | Attack | Answer |
| --- | --- | --- |
| R1 | Can the anchoring tolerance make a solvable model unsolvable or the reverse? | It decides classification and the early refusal, not acceptance. A model whose anchor is weaker than `1e-10` of its conduction row is reported as lacking a datum (the matrix would have a pivot ratio near 1e-10 and be effectively singular); the weakest sensible anchor, 1e-6, is recognised (probes S19, S20). The pivot gate and the residual are not relaxed by it |
| R2 | Does the solve trust `reconstructTemperatures`? | It calls P18-DOF's function and then checks the result independently: every prescribed unknown must equal its value exactly (S18 perturbs the reconstruction by one ulp and is killed) |
| R3 | Is `Q` mutated to apply `Tc`? | No: `beff` is a new array; `Q` is a const view |
| R4 | Does the solve reject a physically impossible temperature (below 0 K)? | **No.** `solveSteadyThermal` accepts any finite field. A sink strong enough could drive a solution below absolute zero; `ThermalResult::create` (P18-POST-001) refuses it and P18-VALID-001 owns the policy. Recorded as a limitation, not a defect |
| R5 | `steadySolveFailure` costs a full solve | Yes: it runs the same function and discards the solution. Diagnostics and tests only |
| R6 | Why not produce a `ThermalResult`? | It requires a gradient and a flux per element and a heat balance; a half-filled one would claim to be a result and not be |
| R7 | Is a settings record documented wrongly? | It was: `ThermalSolverSettings::relativeResidualTolerance` was described as `||K T - Q|| / ||Q||`, which is neither what P17 qualified nor what this milestone computes. See F1 |

## Audits

```text
Eigen / factorisation in src/thermal and include/bettercad/thermal   none (two comment mentions in ThermalSparse.hpp from the assembly milestone, and one in ThermalSolve.cpp saying there is none)
#include of anything structural from thermal                         none
dense reduced or global matrix in ThermalSolve.cpp                   none (no MatrixXd, no vector<vector<double>>, no toDense, no n*n allocation)
epsilon / penalty / pin / 1e12 / 1e20 / pseudo-inverse / reference temperature   none in ThermalSolve.cpp or the kernel (code lines)
unordered containers, hashes in ThermalSolve.cpp / SymmetricSolve.cpp   none
threads, OpenMP, std::execution, async                                none
tolerance literals in ThermalSolve.cpp                                kAnchorTolerance (header) only; the two settings come from ThermalSolverSettings
who links bettercad_thermal                                           the module's own CMakeLists, tests, the architecture fixtures and the compile-fail group; no application
```

## Findings

| # | Finding | Class | Resolution |
| --- | --- | --- | --- |
| F1 | `ThermalSolverSettings` documented its tolerance as `||K T - Q|| / ||Q||` and its floor as "the smallest factor pivot" | documentation defect | comment corrected in `ThermalData.hpp` (no behaviour, no layout, no default changed) |
| F2 | two checks (the reduction's finiteness check, the residual loop's per-row check) each had a second line of defence behind them | test gaps | two assertions added; both probes now killed |
| F3 | the solve does not reject a field below 0 K | limitation | belongs to P18-POST-001 / P18-VALID-001; stated in SCOPE.md and R4 |
| F4 | `steadySolveFailure` runs a full solve | limitation | documented |
| F5 | datum classification depends on `kAnchorTolerance = 1e-10` | design note | measured against rounding (2.2e-16) and the weakest anchor (1e-6); both edges tested |

No production defect was found. The shared-library preset was built by the qualification run, not
earlier: the new `linalg` module has its own export macro and every function the tests call is annotated
`BETTERCAD_LINALG_EXPORT`, `BETTERCAD_THERMAL_EXPORT` or is a template-free inline of a type that is
(`ReducedThermalSystem` and `SteadyThermalSolution` are exported classes).