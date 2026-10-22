# P18-VALID-001 — heat-balance acceptance, and why the two numerical gates are independent

```text
RULE:     TV_BALANCE (V13)
SUBJECT:  the books must close, state by state, to P18-BALANCE-001's own measured policy; this milestone adds no tolerance and no formula
AUTHORITY: balanceWithinTolerance, kBalanceAbsoluteToleranceW = 1e-12 W, kBalanceRelativeTolerance = 1e-13 (P18-BALANCE-001)
INDEPENDENT OF: TV_SOLVER_RESIDUAL — both must pass; neither rescues the other
```

## What is judged, per state

| Check | Failure | Code | Test |
| --- | --- | --- | --- |
| a heat rate, a category total, a reaction, a storage energy, the scale or the residual is not finite | before any comparison, so a NaN cannot fall through one | `balance_not_finite` | `RejectsNonFiniteBalance` (eight corruptions) |
| `balanceWithinTolerance(residual, gross, arithmetic)` is false on the NUMBERS the state holds | even though the state still RECORDS itself as `Balanced` | `balance_out_of_tolerance` (`cause = recomputed`) | `RejectsBadSteadyBalance`, `BalanceIsJudgedByBalancesOwnTolerancePolicy` |
| the state records `OutOfTolerance` although its numbers are within the policy | a result that disagrees with itself is not accepted | `balance_out_of_tolerance` (`cause = recorded_status`) | `ABalanceThatRecordsItselfOutOfToleranceIsNotAccepted` |
| the number of states differs from the history (steady: 1; transient: one per step) | missing steps are not skipped | `balance_states_mismatch` | `ABalanceWithTheWrongNumberOfStatesIsRefused`, `…OnTheModel` |
| a state does not list one entry per prepared load, per convection condition and per prescribed UNKNOWN | omission or double counting of a category | `balance_attribution_mismatch` | `ABalanceMustAccountForEveryPreparedCategory`, `ABalanceMissingACategoryFailsTheAttributionOnTheModel` |
| no balance was presented | full acceptance is withheld; a solve can still be accepted | `balance_missing` | `AFinalOnlyHistoryOfSeveralStepsCannotBeFullyAccepted` |

Reactions are counted per prescribed UNKNOWN, as the balance milestone froze: two overlapping Dirichlet conditions share an unknown's heat and no per-condition split is
invented, so the attribution check does not demand one.

## The tolerance is consumed, not copied

The residual's allowed size is `1e-12 W + 1e-13 (gross + arithmetic)`, where the gross is the sum of the magnitudes of every individual heat rate (so a +1e6 W / −1e6 W
cancellation cannot make the scale tiny) and the arithmetic scale is the size of the numbers the residual is formed from. `BalanceIsJudgedByBalancesOwnTolerancePolicy` builds one
residual at half the allowed size (accepted) and one at four times it (refused) from the constants BALANCE froze, and a source audit finds no tolerance literal in the
validator. The measured margin of that policy over the worst qualified balance is 583× (P18-BALANCE-001, TOLERANCE_MEASUREMENT.md); nothing here changes it.

## Transient: every step, and the worst is named

`EveryStepOfATransientBalanceIsJudgedAndTheWorstIsReported` perturbs the middle step of three: the finding carries `step 2`, `time 1.0 s` and `stepSize 0.5 s`, and the
verdict's `worstStep` is 2. The model-level report carries `worstBalanceStep`, `worstBalanceTime` and `worstBalanceResidual` (BALANCE's `normalizedResidual`, the single authority
of that metric). A `FinalOnly` history of more than one step cannot be balanced step by step (P18-BALANCE-001 refuses it), so it can be SOLVE-accepted and never fully accepted, and
the finding says: "a FinalOnly history of several steps stores no intermediate state and cannot be balanced step by step: request EveryStep".

## The independence of the gates (the cancellation trap)

A global balance is a scalar. Large errors of opposite sign cancel in it, and a balance that closes does not prove a field correct. The rule that follows is that the residual
is the gate that is not allowed to be rescued, and the converse. Both directions are tested, on hand data and on the real model.

**Balance PASS, residual FAIL.** On the real model (the start cap held, a convecting far cap, generation; five free unknowns) two free temperatures are moved by `+e` and
by `−e c_a / c_b`, where `c_j` is the sum of column `j` of `K` over the free rows. The net `Σ(K T − Q)` over the free rows does not move, so the heat balance of the moved field,
recomputed through the qualified algebraic seam, closes to rounding — while two free equations are each wrong:

```text
CANCELLATION | |B| 6.217e-14 W | status balanced | steady residual 1.545e-04 (tolerance 1.0e-09)
```

The balance PASSES, the residual FAILS by five orders, and `solveAccepted()` is false (`ABalanceThatCancelsDoesNotRescueAFailedResidual`). On hand data, `K = I`, `Q = 0`,
`T = [+5, −5]` has `B = 0` exactly and a 5 W local error (`BalancePassDoesNotOverrideResidualFailure`).

**Residual PASS, balance FAIL.** A field that solves the algebra with 0.2–7 W of heat attributed to a category that is nowhere in the system: the residual passes, the balance
gate fails, the report is `FAIL` (`ResidualPassDoesNotOverrideBalanceFailure`, `AResidualThatPassesDoesNotRescueABalanceThatFailsOnTheModel`, `ACorruptedBalanceFailsTheBalanceAlone`).

## What the balance gate does not claim

It does not prove the physics: the books closing means the discrete model accounts for its heat, not that the model describes the world (that is P18-VALID's analytical cases and
P18-REFMOD-001's reference models). It does not replace the solver residual. It does not decide a temperature range or a heat-flux limit — there is no design allowable in this
milestone.
