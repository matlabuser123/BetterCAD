# P18-POST-001 — Fourier's law and the sign convention

```text
SUBJECT:  q = -k grad(T), the sign, what q does and does not contain, and the magnitude
```

## The line

```cpp
// ThermalPost.cpp, fieldsFrom -- the only place the law is written
const HeatFlux3 flux{.x = HeatFlux{} - conductivity * gradient.x,
                     .y = HeatFlux{} - conductivity * gradient.y,
                     .z = HeatFlux{} - conductivity * gradient.z};
const HeatFlux magnitude = flux.magnitude();     // std::hypot of the three components
```

`conductivity * gradient.x` is `ThermalConductivity * TemperatureGradient = HeatFlux`: a `static_assert` in the header states that the product
is exactly the `HeatFlux` type (W/(m K) · K/m = W/m²). The negation is written `0 - k g` so that a zero gradient gives `+0` and not `-0`
(`signbit(q.x) == false` is checked on an exactly constant field; PM30, the `-(k g)` form, is killed by that check).

## The sign convention, stated once

```text
HeatFlux3 points where heat FLOWS: toward decreasing temperature.
q . grad(T) = -k |grad(T)|^2  <=  0
```

| Situation | grad(T) | q (k = 5) | Test |
| --- | --- | --- | --- |
| temperature rises with x, +10 K/m | `(+10, 0, 0)` | `(-50, 0, 0)` | `FourierLawSign` |
| temperature falls with x, -10 K/m | `(-10, 0, 0)` | `(+50, 0, 0)` | `FourierLawSign` |
| slab, 400 K at z = 0, 300 K at z = 1 m, k = 45 | `(0, 0, -100)` | `(0, 0, +4500)`: heat flows toward +z, toward the cold face | `SteadySlabOnAJitteredMesh…` |
| the same slab reversed (300 K at z = 0, 400 K at z = 1) | `(0, 0, +100)` | `(0, 0, -4500)` | same, "reversed" |
| affine `307.3 + 7.2x - 3.5y + 11.4z` | `(7.2, -3.5, 11.4)` | `(-36, 17.5, -57)` | `AffineFieldGradientAndFourierFlux` |

The quantity is the physical conductive flux inside the solid. It is **not** the applied scalar surface flux of P18-LOAD-001, whose positive
sign means heat entering the body through a face; the two meet only at a boundary through `q . n`, which is P18-BALANCE-001's.

## Invariants measured over the 162-tetrahedron jittered patch (`AffineFieldGradientAndFourierFlux`)

| Invariant | Worst value over 162 elements |
| --- | --- |
| gradient error against `(7.2, -3.5, 11.4)` | 2.73e-13 K/m |
| flux error against `(-36, 17.5, -57)` | 1.36e-12 W/m² |
| `|q|` error against `sqrt(36² + 17.5² + 57²) = 69.650915285` | 1.51e-12 W/m² |
| `q . grad(T) + k |grad(T)|²` (the sign, independently) | 2.27e-13 |
| `|q x grad(T)|` (antiparallel) | 1.18e-13 |
| `q . grad(T) < 0` for the first element | yes |

## What q is not

| Not in q | Why | Evidence |
| --- | --- | --- |
| convection `h (T - T_inf)` | a boundary heat transfer, not a flux inside the solid | `ConvectionIsNotPartOfTheConductiveFlux`: with a film on the end cap the film exchange is 2671.25 W/m² at t = 0 while the conductive `|q|` is `3.27e-10` W/m² then and 1753.07 W/m² at 200 s; `max |q + k grad T|` over all states is 0 |
| volumetric generation | it changes the solved `T`, not Fourier's law | the insulated block with 60 W has `T` rising and `max |q| = 9.58e-10 W/m²` |
| storage `C dT/dt` | storage is not a spatial flux | `TransientUniformHeatingHasZeroConductiveFluxOnTheBlock`: mean temperature 341.5397 K at 60 s, `dT/dt != 0`, `max |grad T| = 3.99e-11 K/m`, `max |q| = 1.79e-09 W/m²` |

The conductive flux therefore agrees to rounding with `-k grad T` in every state, and the three compile-fail cases
`THE_RESULT_HAS_NO_CONVECTIVE_FLUX`, `…NO_STORAGE_FLUX` and `…NO_TEMPERATURE_RATE` show there is no accessor for the other quantities.

## The magnitude

`|q| = std::hypot(qx, qy, qz)`, computed from the stored components every time (`HeatFlux3::magnitude`), never stored independently of them.

| Test | Components | Euclidean | Not |
| --- | --- | --- | --- |
| `HeatFluxMagnitudeIsEuclideanAndKeepsTheVector` | `(3, 4, 12)` | **13** | L1 norm 19 (PM11 killed), largest component 12 (PM12 killed) |
| an exactly constant field | `(0, 0, 0)` | exactly 0, not NaN, `signbit` false | — |
| the stored magnitude vs the stored vector | any | `|norm(q) - |q|| <= 1e-12` | — |

The magnitude is non-negative, and the vector keeps every sign (a magnitude is never a substitute for it).

## Conductivity

`k` is read from the document through P18-MAT-001 (`resolveThermalMaterial`) at every recovery and cross-checked against the one the model carries:

| Condition | Result |
| --- | --- |
| `k` not finite and positive (0, -1, NaN, inf, -inf) | `thermal_conductivity_invalid` at the kernel and at both solution seams (PM20 and PM39 killed) |
| the material does not resolve for the solution's mode | `material_mismatch` |
| the model's `k` differs from the document's now | `material_mismatch`, "prepare the model again" (PM25 killed) |
| the document's `k` was edited after the solution | `post_source_mismatch`, the material revision named (CURRENTNESS.md) |

There is no `abs`, no `max(k, eps)` and no default conductivity. Anisotropic and temperature-dependent conductivity are outside ADR-049's scope.
