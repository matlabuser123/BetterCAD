# P18-BALANCE-001 — the transient energy balance

```text
SUBJECT:  B = Q_surface + Q_generation + Q_convection + Q_reaction - dE/dt ~ 0 per backward-Euler step: the storage, the history audit, the actual step size
```

## The formulas

```text
dE_n      = 1^T C (T[n+1] - T[n])                         J      the energy step n stores; every unknown, prescribed ones included
rate_n    = dE_n / dt_n                                   W      dt_n = the grid's ACTUAL step size
r         = C (T[n+1] - T[n]) / dt_n + K T[n+1] - Q       W      the full discrete residual; its prescribed components are the reactions
B_n       = Q_surface + Q_generation + Q_convection(T[n+1]) + Q_reaction - rate_n
```

`C` is the consistent capacity as assembled. It is never lumped, diagonalised or restricted to the free unknowns:

| Wrong storage | What it gives for `C = [2 1; 1 2]`, `dT = [10, -4]` | Right: `1^T C dT` |
| --- | --- | --- |
| `diag(C) . dT` | 12 J | **18 J** |
| `C_ff dT_f` only (free unknowns) | misses the constrained unknowns and their coupling | |
| nominal `dt` on a shortened step | wrong by the ratio of the steps | |

Row-sum lumping gives the same TOTAL (`1^T C = row sums`), so the stored energy alone cannot detect it; what lumping changes is each ROW, and therefore the
reaction at a prescribed node (-22.5 W lumped against -30 W consistent in the capacity-coupling oracle). That is why the reaction test, not the energy test,
carries the lumping claim (REACTION_HEAT.md).

## Why the convection is at `T[n+1]`

The backward-Euler equation is `C (T[n+1]-T[n])/dt + K T[n+1] = Q`, and `K` contains `Kconv`: the convective exchange of a step is the one the equation
uses, at the END of the step. `TransientLumpedConvection`: a lumped body (10 J/K) cooling through `hA = 2 W/K` to 300 K from 400 K with `dt = 1 s`:

```text
10 (T1 - 400) + 2 (T1 - 300) = 0     =>     T1 = 4600/12 = 383.3333 K
storage rate        = 10 (383.3333 - 400) = -166.667 W
convection INTO     = -2 (383.3333 - 300) = -166.667 W        B = 0
at T[n] (400 K) the same face would give   -2 (400 - 300) = -200 W      not the storage rate (the old state is not the step's state)
```

## The audit of the transient history (the hard gate)

The brief asks whether an accepted transient result retains the states a step balance needs. The audit of `TransientThermalSolution` (P18-TRANSIENT-001):

| Fact | Source |
| --- | --- |
| every integration step has its own size | `TimeGrid::stepAt(i)`: `dt` for a full step, the remainder for a shortened last step; `integratedSpan()` |
| the sample times are the grid's | `sampleTimes`: `{t_end}` (FinalOnly) or `t_0 ... t_N` (EveryStep) |
| the states stored | `FinalOnly`: only the final state (and `initialTemperatures()`); `EveryStep`: all `N + 1` |
| the resolved initial state | `initialTemperatures()`, always |

So the answer is: **yes for an `EveryStep` history, and for a history of one step; no for a `FinalOnly` history of several steps.** The architectural choice among the brief's
options is therefore the third — restrict the per-step balance to consecutive stored solver steps and refuse the rest — and **no upstream interface changed**:

```text
EveryStep, N steps            N + 1 samples: step k is (sample k-1, sample k), size grid.stepAt(k-1), time sample k      -> balanced per step
one step, FinalOnly           (initialTemperatures(), the final state), size grid.stepAt(0)                              -> balanced
FinalOnly, N > 1 steps        the states between are not stored                                                          -> step_data_missing
```

The refusal names the problem ("the interval between two stored states is not a backward-Euler step; solve with the EveryStep output policy to balance each step").
BALANCE does not integrate again to recover the missing states (that would be a second solver), does not treat the output interval as a step, and does not
approximate the storage of a multi-step interval from its endpoints. The sample times of an `EveryStep` history are the grid's by construction (`TimeGrid::timeAt`), so no separate
check of them exists: a defensive comparison of values that cannot differ would be code no test could reach.

Evidence: `TransientNeedsEveryStepToBalanceEachStep` (real block: 4 steps FinalOnly refused naming "EveryStep"; one step FinalOnly balanced), `TransientSeamNeedsEveryStep`
(hand tetrahedron: "4 integration steps"; one step; EveryStep; a steady system refused with `mode_mismatch`).

## The actual step size

`TransientFinalPartialStepUsesActualDt` on a hand tetrahedron (`V = 1/6 m³`, `60 W/m³` is 10 W, `rho cp V = 1200 * 5 / 6 = 1000 J/K`, so 0.01 K/s):

| Step | Size (s) | Time (s) | `dE` (J) | Storage rate (W) | `B` |
| --- | --- | --- | --- | --- | --- |
| 1 | 0.3 | 0.3 | 3.0 | 10.0 | 0 |
| 2 | 0.3 | 0.6 | 3.0 | 10.0 | 0 |
| 3 | 0.3 | 0.9 | 3.0 | 10.0 | 0 |
| 4 | **0.1** | **1.0** (exactly `t_end`) | **1.0** | **10.0** | 0 |

With the NOMINAL 0.3 s the last step would be 1 J / 0.3 s = 3.333 W: wrong by two thirds (asserted). On the real block (7 s steps to 100 s) the last step is 2 s and stores
`2.4 W * 2 s = 4.8 J`; the rate is 2.4 W like every other step.

## Integrated energy over the run

```text
external   = sum_j dt_j (Q_surface + Q_generation + Q_convection + Q_reaction)_j
stored     = sum_j dE_j
endpoints  = 1^T C (T[N] - T[0])                  an independent evaluation of the same energy
residual   = external - stored;   allowed = sum_j dt_j (the policy's tolerance of step j)
```

| Case | external | stored | endpoints |
| --- | --- | --- | --- |
| insulated block, 2.4 W, 100 s (14 steps of 7 s and one of 2 s) | 240.000000 J | 240.000000 J | 240.000000 J |
| hand tetrahedron, 10 W, 1 s (0.3 + 0.3 + 0.3 + 0.1) | 10 J | 10 J | 10 J |
| fine cylinder, 6 steps of 10 s | 426.0495 J | 426.0495 J | equal |

`stored == endpoints` (telescoping) is asserted to `1e-9` relative; `rho cp V dT = 86.664 J/K * 2.769 K = 240 J` for the block, from material properties and the final temperature.

## Insulated, no-load and constant-temperature checks

| Case | Result |
| --- | --- |
| BAL-TRANS-01 insulated body, uniform, nothing applied | every term 0, `normalizedResidual == 0` exactly |
| BAL-TRANS-02 `C = [3 2; 2 3]` (10 J/K), 20 W, `dt = 2 s` | `dT = 4 K`, `dE = 40 J`, rate 20 W = generation, `B = 0` |
| BAL-TRANS-03 lumped convection (above) | rate = convection = -166.667 W; with 30 W applied, `30 + conv = rate` |
| BAL-TRANS-04 prescribed node with consistent `C` | reaction -30 W (steady formula would give -20) |
| fully constrained, constant temperatures, `Q = [5, 3]` | reactions -5, -3 (they remove the 8 W applied); no storage; no free row to hide behind |
| constant temperature under 20 W generation | reaction -20 W, steady or transient |
| 400 K block cooling through a cap, 10 steps of 50 s | storage < 0, equals the same step's convection to `1e-6` relative at every step |
| prescribed caps 400 K / 300 K from a 300 K block | first-step hot reaction 334.3 W (> steady 270 W: it feeds the warm-up); final-step 270.0000 W |

## What is not claimed

* Time-varying loads and conditions do not exist in this version (P18-TRANSIENT rejects them); the loads of a step are the constant ones.
* The absolute stored energy `1^T C T` depends on the zero of the temperature scale and is not reported; the reference-independent difference is.
* A transient balance proves the books close for the steps taken; it does not prove the time step is small enough for accuracy (that is P18-TRANSIENT's refinement study).
