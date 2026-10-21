# P18-TRANSIENT-001 — time-step refinement: the integrator is first order

```text
SUBJECT:  the observed order of convergence of backward Euler as the step is halved
RULE:     the error ratio of successive halvings must be near 2 (order 1), not 4 (order 2) and not 1 (no
          convergence); the discrete solution must also match the DISCRETE closed form to rounding
```

## The non-uniform two-unknown problem

`C = [2 1; 1 2]`, `K = [1 -1; -1 1]`, `Q = 0`, `T(0) = [300, 330]`. The mean mode `(1,1)` has `K 1 = 0`; the
antisymmetric mode `(1,-1)` has `C v = v` and `K v = 2 v`, so it decays as `e^{-2t}`:

```text
continuous     T_0(t) = 315 - 15 e^{-2t}          T_1(t) = 315 + 15 e^{-2t}
backward Euler T_0(n)  = 315 - 15 (1 + 2 dt)^{-n}     (exactly, after n steps)
```

Two checks, both in `TimeStepRefinementIsFirstOrder`:

1. **Discrete.** For `dt = 0.2, 0.1, 0.05, 0.025` the integrator's `T_0(1)` equals `315 - 15 (1+2dt)^{-1/dt}`
   to `1e-10`. This is a statement about the *implementation*: it uses the consistent `C`, the right history
   and the right step.
2. **Continuous.** The distance from `T_0(1) = 315 - 15 e^{-2} = 312.9699707515 K` is the time error:

   | dt | 0.2 | 0.1 | 0.05 | 0.025 |
   | --- | --- | --- | --- | --- |
   | error (K) | 7.5899e-01 | 3.9255e-01 | 1.9963e-01 | 1.0066e-01 |
   | observed order | | 0.951 | 0.976 | 0.988 |

   The orders approach 1 from below, as they must for a first-order method; the test requires
   `0.9 < order < 1.1` at each halving and a strictly decreasing error.

## The one-unknown lumped body

`C = 10`, `G = 2`, `T_inf = 300`, `T_0 = 400`: `T(5 s) = 336.787944117 K`. For `dt = 1, 0.5, 0.25, 0.125`
the errors are `3.3998, 1.7664, 0.9010, 0.4551` K (orders 0.945, 0.971, 0.985), and the finest is below 0.5 K.

## The real block

The 100-step lumped block run (`dt = 10 s`) differs from the continuous exponential by 0.3187 K at `t = tau`;
at `dt = 10 s` against `tau = 666.6 s` the predicted first-order error is 0.30 K. The match of
measurement and prediction is the third piece of evidence.

## What this does not claim

The *spatial* error of the Tet4 mesh is a separate matter and is not studied here; the refinement is in
time only, on problems whose spatial discretisation is exact (a two-unknown algebraic system) or has a
Biot number of `2.6e-3`. A coarse mesh and a large step do not combine into a stated accuracy; the solver
claims the algebra of backward Euler, and the stability (A-stable, L-stable) that lets the steady-limit and
huge-step tests succeed.
