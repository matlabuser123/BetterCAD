# P18-POST-001 — covariance, scaling and invariance

```text
SUBJECT:  how the recovered fields move when the problem is translated, rotated, scaled, offset or its conductivity changed
```

Each row is a test that recovers the transformed problem and compares it with the transform of the recovered original. The expected relation is
written in the test from the algebra, not read from the recovery; the worst value is what the suite prints.

| Transformation | Relation required | Test | Worst measured | Bound |
| --- | --- | --- | --- | --- |
| translate the mesh by `(3.1, -2.2, 7.7)` m, same nodal values | gradient, flux and magnitude unchanged | `TranslationInvariant` | — (printed as pass) | 1e-9 |
| proper rotation about `(1, 2, 3)` by 0.7 rad plus a translation `(1, -2, 0.5)` (`det R = 1.000000000000000`), nodal values carried with the nodes | `grad' = R grad`, `q' = R q`, `|q'| = |q|` | `RotationCovariant` | `|grad' - R grad|` 2.90e-13, `|q' - R q|` 1.45e-12, `||q'| - |q||` 1.08e-12 | 1e-9 / 1e-8 / 1e-9 |
| rotate an affine field: `R [7.2, -3.5, 11.4]` | `[11.818088, 0.234864, 7.370728]`; flux `-k R [b c d]` | `RotationCovariant` | within the bounds | 1e-9 / 1e-8 |
| geometry scale `s = 2` (nodal temperatures fixed) | `grad' = grad / 2`, `q' = q / 2` | `GeometryScalingDividesTheGradientAndTheFlux` | 0.00e+00 and 0.00e+00 | 1e-9 / 1e-8 |
| a mesh in millimetre-sized units (`h = 0.0005` m, same nodal values) | `grad' = 1000 grad` (the geometry is SI metres: a unit leak shows as 1000x) | same test | within bound | 1e-5 |
| conductivity ratio 4 (`k = 2.5` and `10`) | gradient **bit-identical**; `q' = 4 q`; `|q'| = 4 |q|` | `ConductivityScalingScalesTheFluxAndNotTheGradient` | 0.0e+00, 0.00e+00, 0.00e+00 | 1e-10 |
| temperature offset `+123.456 K` | gradient and flux unchanged | `TemperatureOffsetLeavesGradientAndFluxUnchanged` | <= 1e-10 | 1e-10 |
| temperature difference scale `a = 3` about 300 K | gradient and flux scale by 3 | `TemperatureDifferenceScalingIsLinear` | <= 1e-8 | 1e-8 |
| temperature of the node as recovered | unchanged by every one of the above, not Celsius | `NodalTemperatureRemainsKelvin…` | bit for bit | — |

## Why these are independent checks

* The rotation test rotates the *geometry* (the corners) and keeps the nodal values, so a gradient that is a function of the corner ordering, of a raw
  coordinate, or of one fixed axis would not rotate with it. The expected rotated affine gradient is computed from the rotation matrix and
  the original coefficients.
* The geometry-scale test divides the gradient by exactly the scale; the printed difference is zero because scaling by 2 is exact in binary
  floating point, so the test proves the length dimension enters once, in `G`, and nowhere in the Fourier line.
* The conductivity test shows the gradient does not know `k` (equality, not tolerance) and that the flux scales linearly (PM07 `k` omitted, PM08
  `k²`, killed by 13 and 12 cases).
* The offset test and the difference test together separate "only temperature *differences* matter" from "the recovery is linear in them", which
  a single gauge test cannot do.

## Not claimed

Reflection (an improper rotation) is not tested: the recovery has no handedness-dependent code, but a reflected mesh inverts every tetrahedron and is
refused by P18-ELEM (`invalid_tet_geometry`), which is the correct behaviour and is covered by `OneInvalidTetrahedronFailsTheWholeRecovery…`.
