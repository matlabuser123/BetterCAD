# P18-QUAL-001 - heat balance audit (brief sections 45-48)

**Verdict: one frozen sign convention; the reaction is the full residual at the constrained unknowns, never "what closes the books"; the balance cannot rescue a bad residual and the residual cannot rescue a bad balance.**
Row-by-row: `audit/AUDIT_C_post_balance_validation_viz_commands_persistence_cli.md` section C2 (10 rows: 9 COVERED, 1 PARTIAL - below).

```text
every heat rate is positive when heat goes INTO the thermal domain
steady     B = Qsurface + Qgeneration + Qconvection + QR                         (QR = +r_i at a prescribed unknown, r = K T - Q)
transient  B = Qsurface + Qgeneration + Qconvection + QR - dE/dt                (convection evaluated at T[n+1], the ACTUAL step size)
within tolerance: |B| <= 1e-12 W + 1e-13 (gross + arithmetic scale)
```

| property | tests |
|---|---|
| the sign convention | `ThermalBalance_SignConvention`, `_ReactionSignAnalytical` (400 K / 300 K, g = 2: +200 / -200), `_ConvectionSignAboveAmbient`, `_ConvectionSignBelowAmbient`, `ThermalData_HeatBalanceSignConventionAndNormalization` |
| the reaction is the full residual at the constrained unknowns | `ThermalBalance_FreeResidualsAreNotReactions`, `_AnIncompleteConstraintMapIsVisibleAsAnImbalance` (an omitted constraint is a 10 W imbalance, not absorbed), `_DuplicateIdenticalDirichletConstraintsAreOneReaction` |
| steady categories sum to zero | `ThermalBalance_SteadyPureConductionBalance`, `_SteadyAppliedHeatAndReaction`, `_SteadyGenerationAndConvection`, `_SteadyCombinedCategories`, and the real-mesh `Mapped` counterparts |
| transient: external heat in equals the stored-energy rate, per step, with the actual step | `ThermalBalance_TransientInsulatedGeneration`, `_TransientDirichletReactionIncludesCapacity`, `_TransientStoredEnergyUsesFullC`, `_TransientFinalPartialStepUsesActualDt` (0.3, 0.3, 0.3, 0.1: 10 W, where the nominal step would give 3.33 W); the integrated, telescoping energy is asserted |
| a transient balance needs per-step data | `ThermalBalance_TransientNeedsEveryStepToBalanceEachStep` (`StepDataMissing` for a multi-step `FinalOnly` history) |
| **residual good / balance bad fails** | `ThermalValidation_AResidualThatPassesDoesNotRescueABalanceThatFailsOnTheModel`, `_ABalanceMissingACategoryFailsTheAttributionOnTheModel` |
| **balance good / residual bad fails** (the cancellation trap, built on the real model: the balance of the moved field is `Balanced`, the residual fails) | `ThermalValidation_ABalanceThatCancelsDoesNotRescueAFailedResidual`, `ThermalBalance_BalanceCannotRescueAWrongFreeField` |
| the balance is bound to the current load attribution and result source | `ThermalBalance_RejectsStaleSteadyResult`, `_RejectsStaleTransientResult`, `_RejectsMismatchedAssemblySource`, `_RemeshStalesBalanceEvenWhenCountsAndIdsAgree` |
| tolerance policy measured, pinned; non-finite is never within | `ThermalBalance_TolerancePolicyConstantsArePinned`, `_MeasuredTolerancePolicy`, `_LargeCancellationScale`, `_RefusesWhatItCannotCompute`; mutation QP11 (the validator no longer recomputes the tolerance) killed by 6 tests |

## Closed by this milestone

* **D-E, the interaction of the mesh bounds with the balance gate.** The solver residual gate is 1e-9 and the balance gate is four orders tighter;
  no test drove a solve at the edge of the accepted element shapes through the balance. `ThermalValidation_AMeshAtTheEdgeOfTheAcceptedBoundsStillSolvesAndBalances`
  takes each degeneration family to the **last shape the validator's classifier accepts** (found by bisection on the classifier's own verdict) and
  requires the production solve and the balance to close. Measured: flattening, last accepted at `3r/R = 7.00e-6`, aspect 3.0, solve error `2.6e-13`, worst
  balance residual `4.8e-11`; stretching, last accepted at `3r/R = 7.00e-6`, aspect 264.6, solve error `7.8e-13`, worst balance `0`. Both close with margin.
  Side finding: stretching reaches the radius-ratio floor at aspect 265, so **the aspect-ratio ceiling of 700 is never the binding bound** in either family.

## The PARTIAL row

C2.7, **transient** cancellation: the steady cancellation trap is built on the real model; the transient analogue (a perturbation of one state whose
balance cancels but whose step residual does not) is not. The perturbed-middle-state test asserts the residual fails at the right steps while the balance of
the *unmoved* history passes. Recorded as a limitation; the transient residual and the transient balance are judged by separate code paths from separate
evidence (`checkTransientResiduals`, `checkHeatBalanceStates`), which is what the steady trap established.

## Carried

* **D-K** the transient balance starts from the resolved initial state; the heat implicitly injected at t = 0 by a prescription that differs from the user's initial
  temperature is not in the balance and is reported as `InitialConditionOverriddenByPrescription`.
* The balance tolerance expression is written inline three times (`ThermalBalance.cpp:206`, `:698-699`, `ThermalValidation.cpp:940`); the verdict itself calls `balanceWithinTolerance`.
