# P18-VALID-001 — solver residual acceptance, steady and transient

```text
RULE:     TV_SOLVER_RESIDUAL (V10)
SUBJECT:  a solver's "success" is never an input; the independent free residual is recomputed, judged against the ONE canonical tolerance, for EVERY step
AUTHORITY: ThermalSolverSettings.relativeResidualTolerance (canonical, user intent) and .pivotFloor — through P18-SOLVE-001's and P18-TRANSIENT-001's own functions
```

## One tolerance, one formula

The residual that is judged is `r = Kff Tf - (Qf - Kfc Tc)` normalised as `||r|| / (||Kff|| ||Tf|| + ||beff||)` (steady) and the reduced and physical free residuals of
a backward-Euler step (transient), computed by `validateSteadySolution` and `validateTransientStep`. The tolerance is the analysis's own canonical setting
(`1e-9` by default). **This milestone adds no residual formula and no residual constant.** The ownership gate fails the validator if it carries any numeric literal in
`1e-9 .. 1e-16`, if it does not reach the residual through those functions, and if any file outside the thermal module names them.

Where the validator reads the NUMBER of a failed residual it calls the same function a second time with a tolerance that cannot refuse and uses only the value; the verdict
is the first call's, with the canonical tolerance. (`ResidualIsJudgedAgainstTheCanonicalToleranceAndNoOther` shows the verdict follow the setting both ways: a loose
canonical tolerance accepts what the default refuses, and `1e-30` refuses a field one ulp off; mutations VAL59 and VAL60, which loosen the tolerance by twelve and fifteen orders,
are killed.)

## Steady

| Case | Result | Test |
| --- | --- | --- |
| the solver's own field | PASS, residual reported (`≤ 1e-9`; the real-block slab 0 to 8.7e-17) | `AcceptsQualifiedSteadyResidual`, `ValidSteadyAnalysisPasses` |
| a finite, plausible, WRONG free temperature (350.5 K for 350 K), constraints held | **FAIL** `residual_above_tolerance`, value and threshold reported | `RejectsBadSteadyResidual`, `APerturbedFreeTemperatureFailsTheResidualAlone` |
| one ulp of a free temperature, tolerance `1e-30` | FAIL | `ResidualIsJudgedAgainst…` |
| NaN or infinity in the field | **FAIL** `residual_not_finite` — never a pass | `ANotANumberResidualMetricIsAnErrorNotAPass` |
| a prescribed unknown that does not hold its temperature | **FAIL** `constraint_not_held` | `RejectsAPrescribedTemperatureThatIsNotHeld` |
| pivot ratio below the canonical floor (or NaN) | **FAIL** `pivot_below_floor`; ON the floor passes | `RejectsAPivotRatioBelowTheCanonicalFloor` |
| non-finite solver settings | **FAIL** `solver_settings_invalid`, not a comparison against NaN | `RejectsInvalidSolverSettingsInsteadOfComparingAgainstThem` |
| `Nfree = 0` | PASS (empty residual, accepted trivially) | `FullyConstrainedSteadyResidualIsNotAFailure` |
| a field of the wrong size | **FAIL** `source_mismatch`, not a residual | `ASteadyFieldOfTheWrongSizeIsASourceMismatchNotAResidual` |

`SolverSuccessDoesNotOverrideResidualFailure` states the central rule: the function judging the field is never told anything about a backend; the same field is accepted
and, after one free temperature moves by 1e-3 K, refused.

## Transient — every step, with the ACTUAL step size

Each step of the history is held to the tolerance twice:

1. **The recorded metrics** — the integrator's per-step reduced and physical free residuals and pivot ratio, as it accepted them (the qualified per-step metric). Finiteness is checked
   first, then each against the tolerance and the pivot against the floor. This is the evidence for a `FinalOnly` history, which stores only its last state.
2. **The independent re-evaluation** — for every step whose two states are stored (`EveryStep`, or a single-step run against the resolved initial state), `validateTransientStep`
   recomputes both residuals from `K`, `C` and `Q` with the grid's ACTUAL step size.

| Case | Result | Test |
| --- | --- | --- |
| a 4-step run to 1.7 s (3 × 0.5 s and a shortened 0.2 s) | PASS; the worst step is reported with its index, end time and ACTUAL size | `AcceptsQualifiedTransientResiduals` |
| a perturbed MIDDLE state (state 2, t = 1.0 s) | **FAIL** at steps 2 and 3 (the step that ends at the state and the step that starts from it), step 4 fine; time 1.0 s, size 0.5 s | `RejectsAnyBadTransientStepResidualNotOnlyTheLastOne` |
| a perturbed state on the real model (state 6 of 15, 7 s steps) | FAIL at steps {6, 7}; worst step 6 or 7, size 7 s; the balance of the untouched history stays PASS | `ATransientHistoryWithAPerturbedMiddleStateFailsTheResidualAtThatStep` |
| the final partial step perturbed | FAIL at step 4 with `stepSize = 0.2` (1.7 − 3 × 0.5), NOT the nominal 0.5; the unperturbed history passes WITH 0.2 | `TheFinalPartialStepIsJudgedWithItsActualStepSize` |
| `FinalOnly`, a recorded step residual of 1e-3 at step 3 | FAIL at step 3, `physical_recorded`; a recorded reduced residual at step 1 → `reduced_recorded` | `FinalOnlyHistoriesAreJudgedByTheirRecordedStepResiduals` |
| `FinalOnly` single step | recomputed from the resolved initial state | `ASingleStepFinalOnlyRunIsStillRecomputedFromTheInitialState` |
| a recorded NaN / infinity, or a recorded pivot below the floor | FAIL `residual_not_finite` / `pivot_below_floor`, with the step | `ANotANumberTransientRecordIsAnErrorNotAPass` |
| the wrong number of step records, or a history at the wrong times | FAIL `history_contract_violated` | `ATransientHistoryWithTheWrongNumberOfStepRecordsViolatesItsContract`, `…WithAWrongStateCountViolatesItsContractOnTheModel` |
| the canonical tolerance loosened to 1.0 | accepts what the default refuses | `TransientResidualsUseTheCanonicalToleranceToo` |

The WORST step is the report's `worstResidualStep`, `worstResidualTime`, `worstResidualStepSize` and `worstTransientResidual` (the larger of the physical and the reduced residual at
that step). Judging only the final step would let a bad middle state pass; the tests perturb the middle and the end separately.

## Where the verdicts come from (the real block)

```text
steady   slab 400/330 K + convection + surface flux + generation      steady residual 0 (one free unknown)          tolerance 1e-9
steady   Dirichlet + convection + generation (5 free unknowns)         steady residual 5.08e-17                       tolerance 1e-9
transient Dirichlet block, 15 steps to 100 s (a 2 s last step)        worst reduced/physical residual 9.07e-17 at step 1 (t 7, dt 7)
transient insulated, 4 steps of 25 s                                   worst 4.44e-17 at step 3 (t 75, dt 25)
bad (perturbed 0.25 K free temperature, five free unknowns)            FAIL; value ≫ 1e-9
bad (the cancellation field below)                                     steady residual 1.545e-04 against 1e-09
```
