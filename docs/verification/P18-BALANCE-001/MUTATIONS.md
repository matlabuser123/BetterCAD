# P18-BALANCE-001 — mutation probes

```text
SUBJECT:  48 probe ids of src/thermal/ThermalBalance.cpp (and the tolerance constant in ThermalBalance.hpp), each applied to the production file, rebuilt, run against the balance cases (67 in the first pass, 68 after the additions), and restored
RESULT:   45 killed, 1 void (an equivalent mutant), 2 removed as redundant code, 0 survivors at the end; 6 survived the first pass (test gaps) and were closed by test additions, then killed on rerun
```

## Method

`mutate-t.ps1` (the harness every P18 milestone since P18-TRANSIENT-001 has used, unchanged) takes a name, a find string, a replacement and a Catch filter (`[thermal][balance]`): it applies the
replacement to the file (refusing if the find text is not present), builds `bettercad_tests` in debug-ext, runs the tests with a 240 s timeout, restores the original bytes in a `finally` block and logs
`VERDICT: KILLED | SURVIVED` from the **test output** (never from `Start-Process`'s exit code). `BUILD FAILED` is VOID. The ids `BAL01`–`BAL47` and `BAL12b` collide with no earlier scratch log.

* The production files' SHA-256 before the first probe and after the last is the same (`qualification/mutations/bal-premutation-hashes*.txt`); the second hash is the file after the two redundant checks were removed.
* The first pass ran 47 probes; the survivors forced test additions and two removals; the second pass reran the survivors and the redefined BAL12b.

## What the probes cover

| Family | Ids |
| --- | --- |
| the reaction (sign, remainder, free rows, double counting, order) | BAL01, BAL02, BAL03, BAL04, BAL42 |
| the categories (convection source only, convection sign, abs on a load, global Q added, a skipped load or condition, swapped kinds, order) | BAL05–BAL09, BAL16, BAL17, BAL21, BAL43 |
| the transient energy (no storage term in the reaction, diag(C), free rows only, sign, wrong step, nominal step, output interval as a step, `T[n]`, integrated nominal step, step and time stamps) | BAL10–BAL15, BAL34, BAL35, BAL39–BAL41, BAL12b |
| the residual (sign, non-finite accepted, step validation) | BAL19, BAL37, BAL46, BAL47 |
| the tolerance policy (loosened, arithmetic scale dropped, category-level gross scale, status always Balanced, `balanced()` ignoring states) | BAL18, BAL20, BAL36, BAL38, BAL44 |
| the attribution, the modes and the seam (attribution skipped, mode checks, constraint held, source compared by value) | BAL22, BAL23, BAL24, BAL25, BAL45 |
| currentness at the model level (solution, system, prepared loads, prepared boundary, convection ids, mode and stale reasons of a held result) | BAL26–BAL33 |

## The table

| Id | Mutation | Verdict | Result | Cases that failed |
| --- | --- | --- | --- | --- |
| BAL01 | the reaction sign is flipped: R = -r | KILLED | 28 of 67 failed | AnIncompleteConstraintMapIsVisibleAsAnImbalance, AppliedHeatBalancedByDirichletReaction, ConstantTemperatureUnderGenerationIsRemovedByTheReaction, DuplicateIdenticalDirichletConstraintsAreOneReacti... |
| BAL02 | the reaction is defined as whatever closes the balance (the remainder) | KILLED | 7 of 67 failed | AnIncompleteConstraintMapIsVisibleAsAnImbalance, FreeResidualsAreNotReactions, LargeCancellationScale, OutOfToleranceIsAResultNotAFailure, PerReactionTotalsMatchCategory, SteadyCombinedCategoriesOn... |
| BAL03 | the free rows are counted as reactions too | KILLED | 18 of 67 failed | AnIncompleteConstraintMapIsVisibleAsAnImbalance, AppliedHeatBalancedByDirichletReaction, FineCylinderSteadyBalanceSmoke, FreeResidualsAreNotReactions, LargeCancellationScale, MultipleTemperatureBou... |
| BAL04 | each prescribed unknown is counted twice | KILLED | 29 of 67 failed | AnIncompleteConstraintMapIsVisibleAsAnImbalance, AppliedHeatBalancedByDirichletReaction, ConstantTemperatureUnderGenerationIsRemovedByTheReaction, DuplicateIdenticalDirichletConstraintsAreOneReacti... |
| BAL05 | the convection is the ambient source Qconv alone | KILLED | 18 of 67 failed | AdditiveConvectionBoundariesBothCount, ConvectionHeatTransferIsKconvTMinusQconvAndNotTheSource, ConvectionSignAboveAmbient, ConvectionSignBelowAmbient, EveryDiagnosticIsReachable, FineCylinderStead... |
| BAL06 | the convection sign is reversed (outward counted as into the body) | KILLED | 15 of 67 failed | AdditiveConvectionBoundariesBothCount, ConvectionSignAboveAmbient, ConvectionSignBelowAmbient, FineCylinderSteadyBalanceSmoke, FineCylinderTransientBalanceSmoke, FixedTemperatureAndConvectionOverla... |
| BAL07 | the surface load totals are made absolute | KILLED | 7 of 67 failed | AppliedSurfaceHeatTotal, LargeCancellationScale, MultipleTemperatureBoundariesAndLoadsTogether, SignConvention, SteadyAppliedHeatAndReaction, SteadyCombinedCategoriesOnMultipleBoundaries, Volumetri... |
| BAL08 | the generation totals are made absolute | KILLED | 3 of 67 failed | SignConvention, SteadyCombinedCategoriesOnMultipleBoundaries, VolumetricGenerationTotal |
| BAL09 | the global heat input is added on top of the explicit categories | KILLED | 25 of 67 failed | AdditiveConvectionBoundariesBothCount, AnIncompleteConstraintMapIsVisibleAsAnImbalance, AppliedHeatBalancedByDirichletReaction, AppliedSurfaceHeatTotal, ConstantTemperatureUnderGenerationIsRemovedB... |
| BAL10 | the transient residual omits the storage term C dT/dt | KILLED | 3 of 67 failed | FineCylinderTransientBalanceSmoke, TransientDirichletBlockBalancesEveryStep, TransientDirichletReactionIncludesCapacity |
| BAL11 | the stored energy uses diag(C) only | KILLED | 12 of 67 failed | FineCylinderTransientBalanceSmoke, NonuniformStoredEnergyUsesConsistentCapacity, TransientConvectionCoolingOnTheBlock, TransientDirichletBlockBalancesEveryStep, TransientDirichletReactionIncludesCa... |
| BAL12 | the stored energy leaves out the prescribed unknowns | VOID (equivalent) | 0 failed | (none: prescribed temperatures are constant in time, so the change at a prescribed unknown is already 0 and zeroing it changes nothing; replaced by BAL12b) |
| BAL12b | the stored energy sums only the FREE rows of C dT (the prescribed rows' storage is dropped) | KILLED | 3 of 68 failed | FineCylinderTransientBalanceSmoke, TransientDirichletBlockBalancesEveryStep, TransientDirichletReactionIncludesCapacity |
| BAL13 | the nominal step is used where the actual one belongs | KILLED | 3 of 67 failed | TransientDirichletBlockBalancesEveryStep, TransientFinalPartialStepUsesActualDt, TransientInsulatedGenerationWithAPartialLastStep |
| BAL14 | the convection of a step is evaluated at T[n] instead of T[n+1] | KILLED | 3 of 67 failed | FineCylinderTransientBalanceSmoke, TransientConvectionCoolingOnTheBlock, TransientLumpedConvection |
| BAL15 | an output interval is accepted as an integration step (FinalOnly over many steps) | KILLED | 2 of 67 failed | TransientNeedsEveryStepToBalanceEachStep, TransientSeamNeedsEveryStep |
| BAL16 | the first convection condition is skipped | KILLED | 14 of 67 failed | AdditiveConvectionBoundariesBothCount, FineCylinderSteadyBalanceSmoke, FineCylinderTransientBalanceSmoke, FixedTemperatureAndConvectionOverlapOnOneFace, MultipleConvectionBoundariesAreAdditive, Per... |
| BAL17 | the first load is skipped | KILLED | 29 of 67 failed | AdditiveConvectionBoundariesBothCount, AnIncompleteConstraintMapIsVisibleAsAnImbalance, AppliedHeatBalancedByDirichletReaction, AppliedSurfaceHeatTotal, ConstantTemperatureUnderGenerationIsRemovedB... |
| BAL18 | the gross scale is the category-level scale (cancelling loads vanish from it) | KILLED | 1 of 67 failed | LargeCancellationScale |
| BAL19 | a non-finite residual component is accepted (steady) | KILLED | 1 of 67 failed | RefusesWhatItCannotCompute |
| BAL20 | the relative tolerance is loosened to 1e-3 | KILLED | 5 of 67 failed | AnIncompleteConstraintMapIsVisibleAsAnImbalance, LargeCancellationScale, MeasuredTolerancePolicy, OutOfToleranceIsAResultNotAFailure, TolerancePolicyConstantsArePinned |
| BAL21 | the convection conditions are listed in reverse order | KILLED | 4 of 67 failed | AdditiveConvectionBoundariesBothCount, MultipleConvectionBoundariesAreAdditive, PerConvectionTotalsMatchCategory, SteadyCombinedCategoriesOnMultipleBoundaries |
| BAL22 | the attribution check is skipped | KILLED | 2 of 67 failed | AttributionMustAccountForTheHeatInput, LargeCancellationScale |
| BAL23 | the mode of the system is not checked at the seam | KILLED | 1 of 68 failed | SeamRefusesAMixedUpModeOrConstraintMap *(first pass SURVIVED: a test gap; closed and rerun)* |
| BAL24 | a prescribed unknown that does not hold its temperature is accepted | KILLED | 1 of 68 failed | SeamRefusesAMixedUpModeOrConstraintMap *(first pass SURVIVED: a test gap; closed and rerun)* |
| BAL25 | the system is not compared with the solution's own (steady seam) | KILLED | 2 of 67 failed | RejectsMismatchedAssemblySource, RemeshStalesBalanceEvenWhenCountsAndIdsAgree |
| BAL26 | the steady solution is not required to be current | KILLED | 2 of 67 failed | RejectsStaleSteadyResult, RemeshStalesBalanceEvenWhenCountsAndIdsAgree |
| BAL27 | the assembled system is not required to be current | KILLED | 1 of 68 failed | RejectsMismatchedAssemblySource *(first pass SURVIVED: a test gap; closed and rerun)* |
| BAL28 | the prepared loads are not required to be current | KILLED | 1 of 68 failed | RejectsMismatchedAssemblySource *(first pass SURVIVED: a test gap; closed and rerun)* |
| BAL29 | the prepared boundary is not required to be current | KILLED | 1 of 68 failed | RejectsMismatchedAssemblySource *(first pass SURVIVED: a test gap; closed and rerun)* |
| BAL30 | the prepared convection conditions are not compared with the system's | REDUNDANT CODE REMOVED | 0 failed | (the prepared boundary is required to be current for the same conditions as the solution, so its convection conditions ARE the system's; the comparison could never differ) |
| BAL31 | a transient balance may be asked about as steady (mode check removed) | KILLED | 1 of 67 failed | RejectsStaleTransientResult |
| BAL32 | a steady balance may be asked about as transient (mode check removed) | KILLED | 1 of 67 failed | RejectsStaleSteadyResult |
| BAL33 | a held balance is always judged current (stale reasons ignored) | KILLED | 4 of 67 failed | FailedRecoveryDoesNotPublishACurrentBalance, RejectsStaleSteadyResult, RejectsStaleTransientResult, RemeshStalesBalanceEvenWhenCountsAndIdsAgree |
| BAL34 | the storage rate divides by 1 s instead of the step | KILLED | 7 of 67 failed | FineCylinderTransientBalanceSmoke, TransientConvectionCoolingOnTheBlock, TransientDirichletBlockBalancesEveryStep, TransientFinalPartialStepUsesActualDt, TransientInsulatedGeneration, TransientInsu... |
| BAL35 | the stored energy has the wrong sign | KILLED | 12 of 67 failed | FineCylinderTransientBalanceSmoke, NonuniformStoredEnergyUsesConsistentCapacity, TransientConvectionCoolingOnTheBlock, TransientDirichletBlockBalancesEveryStep, TransientDirichletReactionIncludesCa... |
| BAL36 | the status is always Balanced | KILLED | 4 of 67 failed | AnIncompleteConstraintMapIsVisibleAsAnImbalance, FreeResidualsAreNotReactions, LargeCancellationScale, OutOfToleranceIsAResultNotAFailure |
| BAL37 | the steady residual is K T + Q | KILLED | 9 of 67 failed | AnIncompleteConstraintMapIsVisibleAsAnImbalance, ConstantTemperatureUnderGenerationIsRemovedByTheReaction, FailedRecoveryDoesNotPublishACurrentBalance, FineCylinderSteadyBalanceSmoke, FixedTemperat... |
| BAL38 | the arithmetic scale is dropped from the tolerance | KILLED | 5 of 67 failed | AdditiveConvectionBoundariesBothCount, FineCylinderSteadyBalanceSmoke, SteadyAppliedSurfaceHeatBalancedByDirichletReactionOnTheBlock, SteadyGenerationAndConvectionOnTheBlock, ZeroHeatCase |
| BAL39 | the integrated external energy uses the nominal step | KILLED | 2 of 67 failed | TransientFinalPartialStepUsesActualDt, TransientInsulatedGenerationWithAPartialLastStep |
| BAL40 | the step number is not recorded | KILLED | 2 of 67 failed | TransientInsulatedGenerationWithAPartialLastStep, TransientInsulatedNoLoad |
| BAL41 | a step is stamped with the time of its start | KILLED | 2 of 67 failed | TransientFinalPartialStepUsesActualDt, TransientInsulatedGenerationWithAPartialLastStep |
| BAL42 | the reactions are listed in reverse order | KILLED | 2 of 67 failed | PerReactionTotalsMatchCategory, ReactionSignAnalytical |
| BAL43 | surface and generation loads are swapped | KILLED | 21 of 67 failed | AppliedHeatBalancedByDirichletReaction, AppliedSurfaceHeatTotal, ConstantTemperatureUnderGenerationIsRemovedByTheReaction, FailedRecoveryDoesNotPublishACurrentBalance, FineCylinderSteadyBalanceSmok... |
| BAL44 | balanced() ignores the state statuses | KILLED | 1 of 67 failed | AnIncompleteConstraintMapIsVisibleAsAnImbalance |
| BAL45 | the steady balance accepts a capacity-bearing algebra (mode check removed) | KILLED | 1 of 67 failed | RefusesWhatItCannotCompute |
| BAL46 | a transient step balance does not validate the time step | REDUNDANT CODE REMOVED | 0 failed | (the residual validates the same time step and is reached on every path; the early check was a second copy) |
| BAL47 | a time step in the residual is not validated | KILLED | 1 of 68 failed | RefusesWhatItCannotCompute *(first pass SURVIVED: a test gap; closed and rerun)* |


## The survivors, and what closed them

| Probe | What it showed | Closed by |
| --- | --- | --- |
| **BAL12** | Not a production survivor: prescribed temperatures are constant in time in this version, so the change at a prescribed unknown is already zero and the mutation (set it to zero) is the original program. An **equivalent mutant**, void. | replaced by **BAL12b**, which drops the prescribed rows' storage `(C dT)_c` (what "free rows only" really means); killed by 3 cases, among them the real-block Dirichlet transient and the capacity-coupling oracle |
| **BAL23** | The mode check at the seam was observable only by the *kind* of refusal: a transient system presented with a steady solution fell through to the source comparison, which also refuses (`source_mismatch`). | `SeamRefusesAMixedUpModeOrConstraintMap` requires the specific `mode_mismatch`. BAL23 killed |
| **BAL24** | No test presented a constraint map whose prescribed temperature differs from the one the solution holds. | the same case, with a 310 K map against a 300 K solution (steady) and a map that a warming history violates at step 1 (transient, naming the step). BAL24 killed |
| **BAL27, BAL28** | The model-level currentness of the system and of the prepared loads was shadowed by later checks that also refuse (`source_mismatch`), so only the *message* told them apart. | `RejectsMismatchedAssemblySource` asserts the staleness diagnostics by name ("the assembled system no longer describes", "the prepared heat input no longer describes"). Both killed |
| **BAL29** | The same for the prepared boundary: its constraints, prepared for 410 K, were refused later anyway as not held by the 400 K solution. | a prepared boundary for other conditions is presented and the staleness diagnostic ("the prepared boundary no longer describes") is asserted. BAL29 killed |
| **BAL47** | The time-step validation inside `transientFullResidual` was shadowed by the early check in the step balance. | the residual is called directly with `0`, `-1`, NaN and infinity. BAL47 killed |
| **BAL30** | The comparison of the prepared convection ids with the system's could never differ: the boundary is required to be current for the same conditions as the solution, so its convection conditions are the system's. | **removed** (dead code is not defence). The condition it was meant to catch is BAL29's |
| **BAL46** | The early time-step check in the step balance duplicated the one in the residual, reached on every path. | **removed**; the residual's own check is now tested directly (BAL47) |

## Honest notes

* A probe killed by exactly one case is killed by the case written for that refusal, which is what a precise test should do (BAL19, BAL31, BAL32, BAL44, BAL45, and BAL23, BAL24, BAL27, BAL28, BAL29, BAL47 after their closure).
* No tolerance was loosened and no test weakened to kill a probe; every addition adds assertions. BAL20 (the relative tolerance loosened to 1e-3) is killed by five cases, and the three constants are pinned by value
  in `TolerancePolicyConstantsArePinned`.
* BAL02 (the reaction as whatever closes the books) is killed by 7 cases, among them the ones that expect an imbalance to be visible (`FreeResidualsAreNotReactions`, `OutOfToleranceIsAResultNotAFailure`,
  `AnIncompleteConstraintMapIsVisibleAsAnImbalance`): a remainder reaction would make all of them pass a wrong model.
* BAL09 (the global `Q` added on top of the categories) is killed by 25 cases; BAL05 (the ambient source as the convection) by 18; BAL10 (no `C dT/dt` in the transient reaction) by 3, including the capacity-coupling oracle.
* The ownership gate's "GUI recomputation" mutant is a file in `balance-gui-recomputes`, and the gate fails on it (GUI_OWNERSHIP.md).
* Mutations of `ThermalBalance.hpp` other than the tolerance constant (the exported struct, the `static_assert`s) are covered by the compile-fail group (33 cases), itself a negative test of the header.
