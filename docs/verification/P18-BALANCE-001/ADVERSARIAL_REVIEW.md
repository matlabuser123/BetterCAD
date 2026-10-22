# P18-BALANCE-001 — adversarial review

```text
METHOD:   36 questions a hostile reviewer would ask of the accounting; each answered with a test, a mutation probe, an audit or a stated limit
RESULT:   no defect in a qualified module. Three defects of the NEW code were found during development (a gross scale, a tolerance, a missing export), all fixed before the freeze.
          Six test gaps closed, two redundant checks and three unreachable branches removed, one equivalent probe replaced. No gate-blocking finding.
```

## The questions

| # | Question | Answer and evidence |
| --- | --- | --- |
| 1 | Can the reaction sign be flipped? | The sign is derived and frozen by a conductance network whose heat flow is obvious: `g = 2`, 400 K / 300 K, `K T = [+200, -200]`, hot reaction `+200`, cold `-200`; each reaction is asserted on its own so a flip cannot hide in a cancellation. **BAL01** killed by 28 cases. |
| 2 | Can the reaction be "whatever closes the balance"? | No: `reactionHeats` reads only the residual, and the cases that need an imbalance to be VISIBLE (`FreeResidualsAreNotReactions`, `OutOfToleranceIsAResultNotAFailure`, `AnIncompleteConstraintMapIsVisibleAsAnImbalance`) fail if it were. **BAL02** killed by 7 cases; no remainder symbol exists (compile-fail `THE_RESULT_HAS_NO_REMAINDER_REACTION`). |
| 3 | Can free residuals be counted as reactions? | No: only the constraint map's unknowns. `FreeResidualsAreNotReactions` (a field with free residuals 30 W and -10 W: one reaction, the free ones appear only as `B = -sum r_free`). **BAL03** killed by 18. |
| 4 | Can duplicate identical Dirichlet constraints double-count a reaction? | The unit is the unique unknown. `DuplicateIdenticalDirichletConstraintsAreOneReaction` and, on the real block, `DuplicateTemperatureBoundariesDoNotDoubleCountTheReaction` (270 W once, not 540). **BAL04** killed by 29. |
| 5 | Can two additive Robin conditions be deduplicated? | No: they are separate entries and ADD. The twin case (15 W + 15 W) and the real block (25 and 10 W/(m² K) on one cap, `out_i = h_i A (Tb - Tinf_i)`). **BAL16** (skip the first condition) killed by 14. |
| 6 | Can convection use `Qconv` alone? | The ambient source is 1200 W for a face that exchanges 0 W at equilibrium. `ConvectionHeatTransferIsKconvTMinusQconvAndNotTheSource`. **BAL05** killed by 18. |
| 7 | Can the convection sign be reversed? | Above ambient `-40`, below `+40`, at ambient `0`. **BAL06** killed by 15. |
| 8 | Can `q''` be reinterpreted through a face normal? | The balance reads `integrated.heatRate` (signed, from P18-LOAD); the source audit finds no normal. |
| 9 | Can negative generation or extraction be `abs()`-corrected? | `SignConvention` (-20 W stays -20), `SteadyAppliedHeatAndReaction` (-9 W). **BAL07** (abs surface) killed by 7, **BAL08** (abs generation) by 3; the audit lists every `std::abs` and each is a scale. |
| 10 | Can global `Q` be added on top of the explicit totals? | `GlobalHeatInputIsNotAddedToTheCategories`; **BAL09** killed by 25. |
| 11 | Can generation be counted twice because it is in `Q`? | The generation is reported from the load attribution only; the residual contributes through the reaction and the imbalance. `AttributionMustAccountForTheHeatInput` refuses an extra 7 W. **BAL22** (attribution check skipped) killed by 2. |
| 12 | Can the transient reaction reuse the steady `K T - Q`? | No: `TransientDirichletReactionIncludesCapacity` (-30 W against the steady -20). **BAL10** killed by 3. |
| 13 | Can the stored energy use only the free unknowns? | `1^T C dT` over every row. **BAL12b** (free rows only) killed by 3; the first form of the probe (BAL12) was an equivalent mutant, because prescribed temperatures are constant (below). |
| 14 | Can it use only `diag(C)`? | `NonuniformStoredEnergyUsesConsistentCapacity`: 18 J, diag(C) 12 J. **BAL11** killed by 12. |
| 15 | Can the consistent `C` be silently lumped? | Row-sum lumping gives the same TOTAL energy, so the claim is carried by the reaction: -22.5 W lumped against -30 W consistent. The compile-fail `THE_ALGEBRA_HAS_NO_LUMPING` and the audit (0 `lump`, 0 `diag` in code). |
| 16 | Can the nominal step be used on the last partial step? | `TransientFinalPartialStepUsesActualDt` (1 J over 0.1 s = 10 W, not 3.33 W) and the block (2 s). **BAL13** killed by 3, **BAL39** (integrated nominal) by 2, **BAL34** (divide by 1 s) by 7. |
| 17 | Can an output interval be mistaken for an integration step? | A `FinalOnly` history of several steps is refused (`step_data_missing`). **BAL15** killed by 2. |
| 18 | Can BALANCE rerun the transient integration? | The ownership gate fails any `solveTransient`/`solveSteady`/`effectiveMatrix` in the files (fixture `balance-reintegrates`); compile-fail `THE_RESULT_HAS_NO_TIME_INTEGRATION`. |
| 19 | Can convection use `T[n]` instead of `T[n+1]` under backward Euler? | `TransientLumpedConvection` and `TransientConvectionCoolingOnTheBlock` (the old-state exchange is different at every one of ten steps). **BAL14** killed by 3. |
| 20 | Can constrained storage coupling be omitted? | See 12 and 13; the first-step hot reaction on the block (334.3 W) exceeds the steady 270 W because it feeds the warm-up. |
| 21 | Can the balance pass because large local errors cancel? | Yes, by construction, and it is tested as such: `K = I`, `Q = 0`, `T = [+5, -5]` has `B = 0` and a local error of 5 W. `BalanceCannotRescueAWrongFreeField`. The accepted-solution type is the gate (a failing field cannot be a `SteadyThermalSolution`). |
| 22 | Can the balance tolerance replace the solver residual? | No: separate gates, see 21; the result type cannot be taken from a rejected solve. |
| 23 | Can the tolerance be loosened to hide a bug? | The three constants are pinned by value in a test and by a fingerprint at the freeze; the margin over the measured error is asserted in every real-mesh case. **BAL20** (relative tolerance 1e-3) killed by 5. |
| 24 | Can normalisation divide by the small net heat? | The scale is the gross sum of individual magnitudes plus the arithmetic scale; `LargeCancellationScale` (+1e6 / -1e6 / 5 W net). **BAL18** (category-level gross) killed. |
| 25 | Can a zero-heat case divide by zero? | The scale floor; `ZeroHeatCase` (`normalizedResidual == 0` exactly for an all-zero body). |
| 26 | Can a NaN category compare as PASS? | `balanceWithinTolerance` is false for any non-finite argument; every diagnostic is reachable (`EveryDiagnosticIsReachable`, five sections) and `RefusesWhatItCannotCompute`. **BAL19** killed. |
| 27 | Can a stale solution be balanced after a remesh with equal counts? | No: `RemeshStalesBalanceEvenWhenCountsAndIdsAgree` (9 nodes, 12 tetrahedra, same ids). **BAL26** killed by 2. |
| 28 | Can new `k` be paired with old `T`? | A conductivity edit stales the solution and the system; `RejectsStaleSteadyResult`. |
| 29 | Can one failed category be omitted and the balance still reported? | The attribution check refuses a missing load or condition; the facet-count check refuses a missing facet. **BAL16/17** (a skipped condition / load) killed by 14 / 29. |
| 30 | Can the GUI recompute the balance? | The ownership gate: 497 files scanned, 0 violations; fixture `balance-gui-recomputes` fails it. |
| 31 | Can unordered aggregation change a cancellation result? | No unordered container; every order is upstream canonical (DETERMINISM.md). **BAL21, BAL42** (reversed lists) killed. |
| 32 | Can zero targeted tests report PASS? | Seventeen new guard filters with minimums equal to the observed counts; the post-run script recounts them in each preset. |
| 33 | Can the sign of the residual `K T - Q` be flipped? | **BAL37** killed by 9. |
| 34 | Is the tolerance honest about what it measures? | It is derived from measurement (TOLERANCE_MEASUREMENT.md): the worst `|B|/(gross + arithmetic)` is `1.7e-16`, the policy `1e-13`. |
| 35 | Can an incomplete constraint map pass? | It is computed and reported as an imbalance (`OutOfTolerance`, `balanced() == false`), never silently accepted. **BAL44** (`balanced()` ignores the states) killed. |
| 36 | Does the milestone start anything it was told not to? | No validation verdict, no GUI, no persistence, no command, no heat-flux field: SCOPE.md and the compile-fail group. |

## Findings

| # | Kind | Finding | Disposition |
| --- | --- | --- | --- |
| F1 | design defect of the new code (found by `LargeCancellationScale`) | The first gross scale reused `HeatBalance::scale()`, the sum of the CATEGORY totals. Two loads of +1e6 W and -1e6 W cancel inside their category and vanish from it, so a cancellation-heavy balance had a tiny scale and an over-strict tolerance — the very trap the brief names. | the gross scale is now the sum of the magnitudes of every individual heat rate (each load, each condition, each reaction, the storage); BAL18 pins it |
| F2 | calibration of the new code (found by the measurement) | The first relative tolerance (`1e-10`) was `~10^6` above the measured error (`~1.7e-16` of the scale), loose enough to hide a 5e-5 relative attribution defect on the cylinder. | tightened to `1e-13` (`~580x` the worst measured), with a margin assertion in every real-mesh case; BAL20 pins it |
| F3 | build defect of the new code (found by the early shared-library build) | `ThermalHeatBalance` has two out-of-line members and was not exported: a static build linked, `debug-shared-ext` did not (the same defect P18-BC-001's first shared run found in `PreparedConvectionBoundary`). | the struct is exported; a comment says why |
| F4–F9 | test gaps | BAL23, BAL24, BAL27, BAL28, BAL29, BAL47 survived the first pass (MUTATIONS.md). | six assertions / one case added; all killed on rerun |
| F10 | redundant code | BAL30 (prepared convection ids compared with the system's) and BAL46 (an early copy of the residual's time-step check) could never differ from what precedes them. | removed |
| F11 | unreachable code | a sample-time cross-check of values that are the grid's by construction; a non-finite-storage branch in `finish` shadowed by the residual's own check; an integrated-energy clause in `balanced()` implied by the per-step clauses. | removed, so nothing is left that no test can reach |
| F12 | probe | BAL12 was an equivalent mutant (prescribed temperatures are constant in time, so zeroing their change changes nothing). | replaced by BAL12b |

## Limits, stated

* **A balance PASS is not a solve PASS.** The balance is a global scalar and can cancel large local errors; it is computed from an accepted solution and never substitutes for the free residual (question 21).
* **Prescribed temperatures are constant in time in this version,** so the change at a prescribed unknown is zero and its storage enters only through the capacity coupling to its free neighbours. A later time-varying prescription would
  add the unknown's own change; the full-`C` form is already the right one for it.
* **The per-condition reaction is not reported:** conditions that share an unknown share its heat, and no ownership rule exists that does not invent one (REACTION_HEAT.md).
* **A history stored with `FinalOnly` over several steps cannot be balanced,** and the refusal says how to obtain one that can.
* **The books closing does not prove the physics is right,** only that the discrete model accounts for its heat; accuracy against analytical solutions is P18-VALID-001's.
* **Kelvin versus Celsius is not proven here:** every term depends on temperature differences only (SIGN_CONVENTION.md).
* **The tolerance is measured on the cases listed** (heat scales from 5 W to 540 W gross and arithmetic scales to `4.7e6 W`, in three presets); a model outside that range with a larger rounding scale is still judged by the scale-aware term, which
  grows with it.
