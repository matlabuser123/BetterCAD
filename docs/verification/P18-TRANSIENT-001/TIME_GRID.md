# P18-TRANSIENT-001 — the time grid

```text
SUBJECT:  how a step size and an end time become a deterministic sequence of steps, and where the grid
          is stricter or more permissive than the canonical data
CODE:     TimeGrid::create / timeAt / stepAt / times / integratedSpan   (ThermalTransient.hpp/.cpp)
```

## The rule

```text
t_0   = 0
t_k   = k * dt                       for 0 < k < N        a MULTIPLICATION, never a running sum
t_N   = t_end                        EXACTLY
step  = dt                           every full step
last  = t_end - n * dt               the shortened remainder, only if t_end / dt is not an integer
```

`ratio = t_end / dt`. If `|ratio - round(ratio)| <= 1e-9 * round(ratio)` (and `round(ratio) >= 1`) the grid
is a whole number of steps, **every step is exactly `dt`**, and the reported final time is the user's
`t_end`. Otherwise the grid is `floor(ratio)` full steps and one shortened step of `t_end - floor(ratio)*dt`,
which uses **its own** `C / dt_last`. There is no fifth case: no step is enlarged, no step is dropped and
nothing overshoots `t_end`.

The grid is **O(1) in memory**: it stores `dt`, `t_end`, the remainder and the step count, and computes any
time or step on demand, so a million-step grid costs nothing until a step is taken.

## The tolerance, and why it exists

`0.3 / 0.1` is `2.9999999999999996` in double precision. Without a tolerance the grid for `dt = 0.1`,
`t_end = 0.3` would take two full steps and a `5.5e-17` s remainder: a step whose `C / dt` is `1.8e16`
times the capacity. The relative tolerance `kTimeGridTolerance = 1e-9` is the **one** place the grid is
given a tolerance. It is not a solver tolerance and nothing else reads it. Both edges are tested:

```text
dt = 1, t_end = 1000 (1 + 0.5e-9)     inside   1000 steps, none partial; endTime() = the user's value
dt = 1, t_end = 1000 (1 + 2.0e-9)     outside  1001 steps, the last of 2e-6 s
```

Inside the tolerance the time actually integrated is `N dt`, which differs from `t_end` by at most
`1e-9 * t_end`. That is reported, not hidden: `integratedSpan()` returns `N dt` (or `(N-1) dt + remainder`),
the test `TimeGridSaysWhichSpanItIntegrates` pins it, and a consumer that needs the energy accumulated
(P18-BALANCE-001) reads it, not `t_end`. For the shortened-step grid the two agree to rounding.

## Worked grids (tested)

| dt | t_end | steps | notes |
| --- | --- | --- | --- |
| 0.25 | 1.0 | 0.25 × 4 | exact |
| 0.3 | 1.0 | 0.3, 0.3, 0.3, 0.1 | last step 0.10000000000000009 s (`1.0 - 3*0.3`); `t_4 == 1.0` exactly |
| 0.1 | 0.3 | 0.1 × 3 | ratio 2.9999999999999996 is an integer within the tolerance |
| 0.1 | 0.7 | 0.1 × 7 | ratio 6.999999999999999 |
| 7 | 100 | 7 × 14, then 2 | the real-block determinism run (15 steps) |
| 2 | 1 | one step of 1 | `dt > t_end`: grid only; the canonical data forbids it |
| 1 | 0 | no step | `t_end = 0`: one time; the canonical data forbids it |
| 0.1 | 10 000 | 100 000 steps | `t_k == k * 0.1` bit for bit; a running sum of `0.1` misses `10 000` |

## Stricter than the grid: the canonical data

P18-DATA-001's `validate(TransientSettings)` requires `0 < dt <= t_end`, both finite. The grid accepts
`t_end = 0` (a grid of one time) and `dt > t_end` (one shortened step), so that those cases are testable and
a later consumer can ask about them. **The solve is not so permissive**: `solveTransientThermalSystem` and
therefore `solveTransientThermal` apply the canonical rule first and refuse `t_end = 0` and `t_end < dt` as
`invalid_end_time`. The algebraic seam `solveThermalTransientAlgebra`, which takes a `TimeGrid`, runs the
permissive grids (the zero-step run returns the resolved initial state and no records). The distinction is
documented here and tested at both layers.

## Refusals

```text
dt not finite or not positive                 invalid_time_step
t_end not finite or negative                  invalid_end_time
t_end / dt > kMaxTimeSteps (1 000 000)        too_many_time_steps    (also when the quotient overflows to infinity)
```

`kMaxTimeSteps` is a **resource** guard: a larger count is refused, never turned into a larger step. A
step is never changed to make a count fit.
