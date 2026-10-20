# P18-SOLVE-001 — residual validation

```text
SUBJECT:  why a solver saying "success" is not an accepted solution, the metric that is, and its
          tolerance
```

## The gate

```text
r  = Kff Tf - beff                              the independent free residual, computed by BetterCAD

                          ||r||_2
normalized = ---------------------------------   dimensionless, scale free
              ||Kff||_inf ||Tf||_2 + ||beff||_2
```

The numerator is in watts; so is each term of the denominator (`W/K * K`, and `beff`). Scaling `K` and
`Q` by `c` scales numerator and denominator alike, so the gate means the same thing at every magnitude
— measured, not argued:

| Case | `||r||_2` (W) | normalized | threshold | result |
| --- | --- | --- | --- | --- |
| one free node | 0 | 0 | 1e-9 | PASS |
| 3-node chains | below 1e-12 | below 1e-16 | 1e-9 | PASS |
| 5-unknown interleaved case | 2.54e-13 | 3.56e-17 | 1e-9 | PASS |
| scaled copies, `c` from 1e-6 to 1e6 | scales with `c` (checked: ratio equals `c` to 1e-12) | between 1.67e-17 and 6.26e-17 at every `c` | 1e-9 | PASS at all five scales |
| real block, 9 unknowns | 0 | 0 | 1e-9 | PASS |
| structured 6^3 grid | — | 9.56e-17 | 1e-9 | PASS |
| structured 18^3 grid (5832 unknowns, 5184 free) | — | 3.37e-16 | 1e-9 | PASS |
| real fine cylinder (900 unknowns, 756 free) | — | 1.28e-17 | 1e-9 | PASS |

**Why no scale floor.** The denominator vanishes only when `Tf` and `beff` are both exactly zero, and
then `r = Kff 0 - 0` is exactly zero too. The honest value is `0`. If the denominator were zero while
`r` was not, the check fails rather than reporting a flattering ratio (P17's rule, kept). A fully
prescribed model has no free equations: the residual is empty, accepted trivially, and reported as `0`
with `kernelInvoked = false`.

## "Success" is not an input

`validateSteadySolution` is the acceptance step without a solve. It is shown finite, plausible, WRONG
answers:

```text
exact interleaved solution                     accepted
one free temperature off by 1 K (each of 3)    residual_too_large, "The solver reported success; BetterCAD does not"
one free temperature off by 1e-12 K            accepted     (far below the tolerance: the gate is scale-aware)
the same 1e-12 K error, tolerance set to 1e-18 refused      (the threshold is the SETTING)
a perturbation of 1e-6 relative                refused at c = 1e-6, 1, and 1e6 alike
a perturbation of 1e-13 relative               accepted at c = 1e-6, 1, and 1e6 alike
NaN, +Inf, -Inf in a free or prescribed entry  non_finite_solution
a prescribed unknown one ulp off its value     constraint_not_preserved (exact equality, deliberately)
a candidate of the wrong length                dof_map_mismatch
an overflowing residual                        refused, never compared
```

A raw absolute-only threshold would accept the first at `c = 1e-6` and refuse a correct solution at
`c = 1e6`; probe L05 (the normalization replaced by the raw norm) is killed by exactly this.

## The free residual and the full residual are different things

```text
full   K T - Q        over ALL unknowns
free components       equal the reduced residual r (identity, tested) and are ~0
prescribed components are the heat the boundary supplies (the reactions) and are CORRECTLY non-zero
```

The interleaved case: `K T - Q = [0, -51.1538, 1.1e-13, -2.3e-13, 46.1538] W`. Free entries vanish
to rounding; the prescribed entries are `-51.15` and `+46.15` W and **are not a solver failure**
(probe S11, which treats them as one, is killed by every test that has a Dirichlet condition). Their sum
is `-5 W = -sum(Q)`, because the rows of a conduction matrix sum to zero. Interpreting them is a
later milestone's; this one retains the vector and does not qualify reaction heat.

The test writes `r` and `K T - Q` out with its own loops and asserts
`max |r_e - (K T - Q)_{dof(e)}| = 0.00e+00`.

## Tolerances

| Name | Value | Scale | Source | Why |
| --- | --- | --- | --- | --- |
| `relativeResidualTolerance` | 1e-9 | dimensionless | `ThermalSolverSettings` (P18-DATA, canonical) | P17's measured value; a correct direct solve of a well-conditioned reduced system sits at 1e-16 (the table above), so 1e-9 has seven orders of margin and still refuses a one-kelvin error by eleven |
| `pivotFloor` | 1e-12 | dimensionless, `min|D|/max|D|` | `ThermalSolverSettings` | P17's measured value; the cylinder's ratio is 2.1e-3 and the grids' 1.7e-1 |
| `kAnchorTolerance` | 1e-10 | relative row sum | `ThermalSolve.hpp` | between rounding (2.2e-16, measured) and the weakest sensible anchor (1e-6); decides classification only (SINGULARITY.md) |
| constraint equality | exact | — | the postcondition | a prescribed value is assigned, not computed |

Each tolerance has ONE source. The settings are canonical and persisted by P18-DATA; the thermal solve
maps them onto the kernel's record and defines no second copy.