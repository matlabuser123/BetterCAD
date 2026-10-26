# P18-QUAL-001 - transient solve audit (brief sections 38-41)

**Verdict: backward Euler on a fixed grid, exact Dirichlet, the actual step size used everywhere, every step validated.**
Row-by-row: `audit/AUDIT_B_element_loads_bcs_assembly_solvers_tolerances.md` section B10 (10 rows: 8 COVERED, 2 PARTIAL - see below).

```text
(C/dt + K) T[n+1] = Q + (C/dt) T[n]         backward Euler; the initial state has the prescribed unknowns already replaced
```

| property | tests |
|---|---|
| the scheme is backward Euler, first order | `ThermalTransient_EffectiveMatrixIsExactlyCOverDtPlusK`, `_OneDofHandOracle`, `_TimeStepRefinementIsFirstOrder`, `_LumpedBodyMatchesTheExponentialAtFirstOrder`, `_AcceptanceRefusesTheWrongSchemesThatAreFinite`; compile-fail `the-transient-settings-have-no-theta`, `-no-method-selector` |
| a transient converges to the steady solve | `ThermalTransient_SteadyLimitMatchesTheSteadySolve`, `ThermalReference_RM08_NonUniformTransientAgainstIndependentBackwardEuler` |
| exact Dirichlet; a prescribed value dominates the initial temperature at t = 0 | `_PrescribedDominatesTheInitialTemperatureAtTimeZero`, `_NonzeroDirichletMatchesDenseOracle` |
| steady needs only k; a transient needs k, rho and cp, all finite and positive | `ThermalMaterial_SteadyNeedsNeitherDensityNorSpecificHeat`, `_DensityIsRequiredOnlyByTransient`, `ThermalAssembly_TransientRequiresCapacityProperties`; mutation QP16 |
| the final partial step uses its **actual** size in the matrix, the right-hand side, the residual and the balance | `ThermalTransient_TimeGridPartialFinalStep` (0.3, 0.3, 0.3, 0.1), `_LastPartialStepUsesItsOwnTimeStep`, `ThermalReference_RM08_FinalPartialStep`, `ThermalBalance_TransientFinalPartialStepUsesActualDt`; mutation QP17 (a full nominal step) killed by 12 tests |
| pure-Neumann **transient is valid** (the steady datum rule is not applied; positive capacity makes the system definite) | `ThermalTransient_PureNeumannTransientSucceedsWhereSteadyIsRefused` (the same K refused by steady, accepted by transient, +10 J per step, against a dense oracle), `_EnergyIsConservedOnAnInsulatedMeshedBody` |
| any failed mandatory step fails the whole run, naming the step | `ThermalTransient_AStepThatCannotBeTakenFailsTheRunWithTheStepAndTime`, `_AFailureAtALaterStepNamesThatStepAndPublishesNothing`, `_FailedRunDoesNotCreateACurrentResult` |
| zero or missing capacity fails | `ThermalTransient_RegionWithNoCapacityAtAllIsRefused` (also 1e-30), `ThermalValidation_TransientMissingCapacityFails` |
| the per-step residual is reported: worst step, end time, **actual** step, normalized | `ThermalTransient_EveryStepEvidenceIsRecordedPerStep`, `ThermalValidation_ATransientHistoryWithAPerturbedMiddleStateFailsTheResidualAtThatStep` (steps 6 and 7, size 7.0) |

## The two PARTIAL rows

1. **B10.5 - a documented time-grid snap.** `kTimeGridTolerance = 1e-9` snaps `t_end/dt` to an integer, so `N dt` can differ from the reported
   `t_end` by up to 1e-9 relative. It is documented and tested (`ThermalTransient_TimeGridToleranceBoundary`,
   `_TimeGridSaysWhichSpanItIntegrates`), but `TimeGrid::integratedSpan()` is read nowhere outside the module, so no report discloses the
   difference. Finding D-B1 in `FINDINGS.md`; not changed (it is the sole time-grid tolerance, a rounding guard of nine orders, and no
   accuracy claim depends on it); listed in `LIMITATIONS.md` for the owner.
2. **B10.8 - normalized residual only.** The transient step record carries two normalized ratios; there is no absolute (W) per-step solver
   residual (the balance module reports absolute heat-balance residuals per state, a different quantity). Recorded as the accepted design.

## Open owner decisions (carried unchanged from P18-BALANCE / P18-VALID)

* The default `TransientSettings::output` is `FinalOnly`, and the balance refuses a multi-step `FinalOnly` history, so a transient with default
  settings and more than one step can never be **accepted** (`ThermalValidation_AFinalOnlyHistoryOfSeveralStepsCannotBeFullyAccepted`).
  Deliberate and explained in the finding text; an accepted transient requires an explicit `EveryStep`. (D-D)
* The transient balance starts from the **resolved** initial state, so the heat implicitly injected at t = 0 by a prescription that differs
  from the user's initial temperature is not in the balance; it is reported as the information finding `InitialConditionOverriddenByPrescription`. (D-K)
