# P18-TRANSIENT-001 — energy identities

```text
SUBJECT:  the conservation properties a backward-Euler step must have, checked step by step
NOT:      the heat balance. Aggregating boundary heat, reactions and the stored-energy rate into a
          balance with a stated tolerance is P18-BALANCE-001's. These are algebraic identities of the
          discrete step, used as tests.
```

## The identity

Summing the rows of `C (T[n+1] - T[n])/dt + K T[n+1] = Q` over **all** unknowns gives

```text
   1^T C (T[n+1] - T[n]) / dt  =  1^T Q  -  1^T K T[n+1]
```

`K` is the sum of a conduction part, whose rows each sum to zero (`K_cond 1 = 0`: a constant temperature
changes no flux), and the Robin part, so `1^T K T = (heat leaving through the film)`. The consequences,
each a test:

| Model | Identity | Measured |
| --- | --- | --- |
| insulated 64-unknown Kuhn body, 54 W generated | `1^T C T[n]` rises by `dt * sum(Q)` every step | 1080 J over 20 s against `20 * 54 = 1080` J; each of ten steps within `1e-9` relative |
| insulated 9-unknown steel block, 60 W | stored energy `= 60 W * t`; mean rise `60 t / (rho cp V)` | 69.232899 K over 100 s against 69.232899 K, `1e-9` relative |
| two-unknown pure-Neumann hand model, `Q = [10, 0]` | stored energy `+10 J` per 1 s step | within `1e-8` |
| block cooling through six convective faces | `(E[n+1]-E[n])/dt = -(Σ Kconv T[n+1] - Qconv)` at the NEW state | worst relative error `2.88e-13` over 100 steps |
| non-uniform two-unknown mean | `T0 + T1` constant when `K 1 = 0`, `Q = 0` | exact to `1e-10` |
| two disconnected bodies | each body's energy changes only by its own `Q` | region 1 mean constant, region 2 `+2.5 K/s` |

The convective identity is evaluated **at the new state**, which is what an implicit method conserves: the
film loses heat at `T[n+1]`, not at `T[n]`. Evaluated at the old state the identity would be off by a term of
order `dt` — the first-order error — and the test would fail.

## Where the identity lives, and what it is not

The identity holds because the step satisfies the heat equation; it is a consequence of the physical
residual being small, not a second mechanism, and a defect that broke it (a wrong history sign, a `C * dt`)
breaks the residual first. The tests do not call the production acceptance step: they recompute `1^T C T`
and the film heat from the stored states with the test's own sums.

The stored-energy rate that P18-BALANCE-001 will report is `(E[n+1] - E[n]) / dt` computed from these
states; the span to divide by is the grid's `integratedSpan()` ([TIME_GRID.md](TIME_GRID.md)).

## Prescribed temperatures

With a Dirichlet condition `1^T K T - 1^T Q` includes the heat the boundary supplies, so the same identity
reads `d(stored)/dt = Q_applied + Q_conv + Q_boundary`. The boundary term is the one the free-row residual
deliberately excludes ([TRANSIENT_RESIDUAL.md](TRANSIENT_RESIDUAL.md)); computing and bounding it is
P18-BALANCE-001's.
