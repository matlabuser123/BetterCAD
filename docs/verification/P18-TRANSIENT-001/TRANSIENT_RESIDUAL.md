# P18-TRANSIENT-001 — the per-step residual and the acceptance of a step

```text
SUBJECT:  how a step is accepted without taking a solver's word for it
CODE:     validateTransientStep, physicalStepResidual, kernelFailure
```

## Two residuals, both from the original arrays

A backend returning success is not evidence. Every step is accepted by two **independent** quantities, both
recomputed after the solve from `K`, `C` and `Q` as assembled:

```text
reduced     r_f = A_ff T_f - (b_f - A_fc T_c)
            ||r_f||_2 / (||A_ff||_inf ||T_f||_2 + ||b_f - A_fc T_c||_2)
            the solve of the system that was built      (linalg::evaluateResidual, rebuilt from K, C, Q here)

physical    r   = C (T[n+1] - T[n]) / dt + K T[n+1] - Q,   over the FREE rows
            ||r||_2 / ( ||C||_inf ||T[n+1]-T[n]||_2 / dt + ||K||_inf ||T[n+1]||_2 + ||Q||_free )
            the heat equation itself      (physicalStepResidual)
```

The acceptance step rebuilds `A` and `b` itself; it does not read the matrix the solve factorised. Both
must be finite and `<= relativeResidualTolerance` (default `1e-9`, the user's canonical setting). The
reduced residual is the steady solver's metric and has the same scale-free definition; the physical
residual uses the same backward-error normalization (`||r|| / (||A|| ||x|| + ||b||)` generalised to the
three terms of the equation), so scaling `K`, `C` and `Q` together leaves both unchanged (tested at `1e-6`
and `1e6`).

## Why two

If the effective matrix is **built** wrongly — `C * dt` for `C / dt`, a flipped history sign, a dropped
history term, a lumped diagonal — the solve of the wrong system is perfectly accurate and its reduced
residual is rounding noise. Only a residual taken from the original `K`, `C` and `Q` can say the answer is
not the heat equation's. `PhysicalResidualSeesAWronglyBuiltSystemTheReducedOneCannot` shows exactly that on a
three-unknown chain: the wrong system's own residual is `<= 1e-10`, and the physical residual of its answer
is above `1e-4`. The same holds for a flipped history sign. Both are public functions so a defence can be
shown a wrong build and refuse it.

## The prescribed rows are excluded, on purpose

`C dT/dt + K T - Q` at a prescribed unknown is the heat the boundary supplies and is **correctly nonzero**
(`ThermalTransient_PhysicalResidualExcludesTheBoundaryHeatAtPrescribedRows` measures a boundary heat above
1 W while the free-row residual is below `1e-13`). Counting it would fail every correct solve with a
Dirichlet condition. The retained full residual and its interpretation are P18-BALANCE-001's.

## Candidates the acceptance step refuses

Every one is finite, so finiteness alone would pass it:

```text
a free temperature off by 1e-3 K                     residual_too_large
a stagnant candidate (T[n+1] = T[n])                 residual_too_large
the right candidate judged against another dt        residual_too_large
explicit Euler's step                                residual_too_large
the step of a system built with C * dt               residual_too_large
the steady state (history taken from the new state)  residual_too_large
a Crank-Nicolson step                                residual_too_large
a NaN or infinite component                          non_finite_solution
a prescribed unknown moved by 1e-9 K                 constraint_not_preserved
a candidate whose residual overflows                 non_finite_residual
a state of the wrong size, a bad dt, a bad setting   dof_map_mismatch / invalid_time_step / invalid_settings
```

A tolerance loosened to `1e-3` accepts the `1e-5` K perturbation the default refuses: the knob is real, and
it is the user's.

## What a failed step does

The run **fails**. There is no retry, no smaller step, no partial history. The failure carries the step
(counted from 1) and the time at the end of that step; the run returns no history, so a step accepted before
the failure is not published as a result. Tested for a failure at step 1, at a shortened last step (step 2
of 2 at 1.01 s), and in the lifecycle test at the model level.

## The physical residual is stricter for a small step, and where that ends

The two residuals are not equivalent. For a correct system, a small `dt` makes the reduced residual's scale
(`||A|| ||x|| + ||b||`, dominated by `C T/dt`) large while the physical residual's scale (`C dT/dt + K T`, the temperature
CHANGE and the conduction) stays modest. Worked in `PhysicalResidualIsStricterThanTheReducedOneForASmallStep`
(`C = K = 1`, `T0 = 300 K`, `dt = 1e-3`): a candidate 1e-7 K off has a reduced residual of `1.7e-10` (accepted) and a
physical residual of `1.7e-7` (refused). The physical residual is the test of whether the state moved by the right
amount, and it is why the mutation that removes it from the acceptance (TV04) is killed.

That strictness has an end, and it is stated rather than hidden. When `dt/tau` is so small that the increment `T[n+1] - T[n]`
is below the rounding of the temperature itself, no double can represent the step well enough to satisfy the heat equation
to `1e-9`: at `dt = 1e-12` (`C = K = 1`, `T = 300 K`) the increment is `3e-10 K` and a double near 300 carries an error of
`5.7e-14 K`, so `C dT/dt` is wrong in its fourth digit. The step is refused as `residual_too_large`, the message says that "a step
so small that the temperature change is near the rounding of the temperature itself does this", and the run reports step 1.
`1e-6` and `1e-3` run; a user tolerance of `1` accepts the `1e-12` step
(`AStepBelowTheResolutionOfTheTemperatureIsRefusedAndSaysSo`). No smaller step is ever substituted and no precision is added.
## Measured

On the real 40 × 30 × 20 mm block (9 unknowns), 100 steps of 5 s: worst reduced residual `8.3e-17`, worst
physical residual `4.3e-17`. On the 900-unknown fine cylinder, ten steps: `1.2e-17` and `9.4e-18`. Both are
rounding noise, eight orders under the tolerance.

## Reachability

Every `TransientProblem` is reached by a test except `factorization_failure`, which the kernel reports for
an arrays-do-not-fit or library failure that cannot occur on arrays this module has validated and whose
mapping is the steady solver's. It is classified, named and kept (the failure of a library is not a thing
to hide), and recorded as defensive.
