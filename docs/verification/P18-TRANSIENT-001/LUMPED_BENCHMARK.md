# P18-TRANSIENT-001 — the lumped-body benchmark

```text
SUBJECT:  a body so conductive that it cools as one temperature, against the exponential it must follow
MODEL:    the 40 x 30 x 20 mm steel block (k = 45, rho = 7850, cp = 460), meshed by P16, convection
          h = 25 W/(m2 K) to 293.15 K on all six faces, initial 400 K, dt = 10 s, 100 steps
```

## The benchmark

For a body with a negligible internal gradient, `C dT/dt = -h A (T - T_inf)`, so
`T(t) = T_inf + (T_0 - T_inf) e^{-t/tau}`, `tau = rho cp V / (h A)`.

```text
V = 2.4e-5 m3     A = 5.2e-3 m2     rho cp V = 86.664 J/K     h A = 0.13 W/K     tau = 666.646 s
Biot = h (V/A) / k = 2.56e-3   (lumped analysis is valid below 0.1)
```

The test reads `rho cp V` from the **assembled capacity** (`1^T C 1`) and `h A` from the prepared film
facets (the sum of every facet term), and checks both against the dimensions to `1e-9` relative before using
them, so the benchmark depends on nothing it has not verified.

## What backward Euler must reproduce

On a lumped body the discrete step multiplies the excess by `1/(1 + dt/tau)`, exactly. The mass-weighted
mean of the FE field, `θ_n = 1^T C T[n] / 1^T C 1`, therefore follows

```text
   θ_n = T_inf + (T_0 - T_inf) (1 + dt/tau)^(-n)
```

and the test compares all 101 states with it:

| Quantity | Measured |
| --- | --- |
| `max |θ_n - discrete lumped|` over 100 steps | 0.0566 K (on an excess of 106.85 K: 0.053 %) |
| `|θ_100 - continuous exponential at 1000 s|` | 0.3187 K (the first-order difference; backward Euler's own estimate is `(t/tau)(dt/2tau) e^{-t/tau}` of the excess, 0.30 K) |
| spatial spread of the nodal temperatures at 1000 s | 0.1902 K |
| per-step energy identity, `max |dE/dt + film heat| / film heat` | `2.88e-13` |

The 0.0566 K is the genuine departure of a 9-node mesh with Biot `2.6e-3` from a lumped body, not a defect:
the thresholds (0.5 K on the mean, 1 K to the continuous solution, 0.5 K spread) are one order above the
measurement and three orders below the 106.85 K excess.

## The one-unknown version

The same law without a mesh: `C = 10 J/K`, `G = 2 W/K`, `T_inf = 300 K`, `T_0 = 400 K`, `tau = 5 s`,
`T(5 s) = 300 + 100/e = 336.787944117 K`. Errors for `dt = 1, 0.5, 0.25, 0.125` are `3.3998, 1.7664, 0.9010,
0.4551` K: ratios `1.92, 1.96, 1.98`, first order ([TIME_STEP_REFINEMENT.md](TIME_STEP_REFINEMENT.md)).
