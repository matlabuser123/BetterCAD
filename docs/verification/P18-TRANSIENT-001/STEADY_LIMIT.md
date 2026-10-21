# P18-TRANSIENT-001 — the steady limit, against the qualified steady solver

```text
SUBJECT:  as t -> infinity the transient solution must be the steady solution
REFERENCE: P18-SOLVE-001's solveSteadyThermal / solveThermalAlgebra, run on the same chain
```

## Why it is a strong test

The steady solve of `K T = Q` and the transient solve of `C dT/dt + K T = Q` share almost nothing: the
steady one has no `C`, no `dt`, no history and (here) an anchoring analysis; the transient one has all
three and no anchoring analysis. They meet only at the fixed point. A transient solution that settles to a
different field than the qualified steady solver found would be wrong in `C`, in `dt`, in the history, or in
the treatment of a constraint.

## Hand system

A three-unknown chain (conductances 2 and 3, consistent capacity of two unit-cell segments, 1 W in the
middle, unknown 0 held at 400 K). The slowest generalised eigenvalue is `lambda = 0.227 /s` (the root of
`7 l^2 - 28 l + 6 = 0`), so a 10 s step multiplies the error by `0.31` and 80 steps by `1e-41`.

```text
80 steps of 10 s from a uniform 300 K          vs solveThermalAlgebra          <= 1e-9 K
```

## The steady state is a fixed point for every dt

Starting **at** the steady solution, three steps of `dt = 0.01, 1, 100, 1e6` s stay at it to `1e-8` K. This
is the property that a wrong `C`, a wrong history or a wrong `dt` breaks first: `(C/dt + K) T* = Q + (C/dt) T*`
holds exactly when `K T* = Q`. And one enormous step (`dt = 1e12 s`) lands on the steady state to `1e-6` K,
because `C/dt` vanishes against `K`: backward Euler is L-stable, and the integrator inherits it.

## Real block

| Model | Final time | Measured difference |
| --- | --- | --- |
| 400 K / 300 K caps, initial 300 K, `dt` 5 s | 500 s | `1.14e-13` K to the closed form `400 - 100 z/L`; `5.68e-14` K to the steady solver |
| generation 1e5 W/m3, 400 K cap, convection on the other, initial 293.15 K | 500 s | `1.71e-13` K to the steady solver |
| 6 mm cylinder, 900 unknowns, 350 K cap + convection | 100 s | ten steps, finite, between initial and held, residuals `~1e-17` (no steady reference run) |

The first two compare the transient with the qualified steady answer **for the same prepared boundary**
(the steady chain is assembled in steady mode on the same mesh and numbering); they agree to the last few
bits. The closed-form profile for the first is a third, independent reference.

## Both directions of the mode check

A steady system into the transient solve is refused (`stale_system`, "mode changed", at the model level;
`not_transient_system` at the seam). A transient system into the steady solve is refused by the steady
solver (`not_steady_system`, P18-SOLVE-001's own test, repeated here). Neither pretends to be the other.
