# P18-ELEM-001 — geometry and material scaling

```text
TESTS:  ThermalElement_GeometryScalingConduction, _GeometryScalingCapacity,
        _ConductivityScaling, _DensityScaling, _SpecificHeatScaling,
        _DimensionalAnalysis
```

## Geometry: x' = s x

The brief's general tetrahedron scaled uniformly. Over `s` from 1e-3 m to
1e2 m (engineering scales; no `1e-150` stress):

| s | V ratio (s^3) | G ratio (1/s) | Ke ratio (s) | Ce ratio (s^3) |
| --- | --- | --- | --- | --- |
| 0.001 | 1e-09 | 1000 | 0.001 | 1e-09 |
| 0.1 | 0.001 | 10 | 0.1 | 0.001 |
| 2 | 8 | 0.5 | 2 | 8 |
| 10 | 1000 | 0.1 | 10 | 1000 |
| 100 | 1 000 000 | 0.01 | 100 | 1 000 000 |

Asserted to 1e-12 relative (the printed ratios are exact to the digits shown),
and the full matrices are compared, `Ke' = s Ke` and `Ce' = s^3 Ce`, to 1e-12.
`Ke ∝ s` and `Ce ∝ s^3` **together** are the unit cross-check: they scale
differently, so a kernel with one factor of length wrong, or with a hidden
`* 1000`, cannot satisfy both. Mutation E06 (a `1e3` factor in the Jacobian)
is killed here and by the unit-tet hand matrix.

## Material

```text
Ke(c k)    = c Ke(k)      c in {0.25, 2, 10};  k in {0.2, 15, 400} W/(m K)   1e-15
Ce(c rho)  = c Ce(rho)    c in {0.25, 2, 10}                                  1e-15
Ce(c cp)   = c Ce(cp)     c in {0.25, 2, 10}                                  1e-15
Ce depends on rho cp:     (1200, 800) vs (7800, 500) agree through the product  1e-14
```

| material input | affects | does not affect |
| --- | --- | --- |
| `k` | `Ke`, linearly | `Ce` (no `k` in its signature) |
| `rho` | `Ce`, linearly | `Ke` |
| `cp` | `Ce`, linearly | `Ke` |

## Dimensional analysis — checked by the compiler

```text
Ke:   V . G^T G . k   = m^3 . (1/m^2) . W/(m K)  = W/K
Ce:   rho . cp . V    = kg/m^3 . J/(kg K) . m^3  = J/K
grad: G . T           = (1/m) . K                = K/m
flow: Ke . T          = (W/K) . K                = W
```

Each line is a `static_assert` in `ThermalElement.hpp` over the `Quantity`
dimensions, restated in `DimensionalAnalysis`; `W/K` and `J/K` are different
dimensions, so a `Ke` entry cannot be handed where a `Ce` entry is wanted
(compile-fail `a-conductance-is-not-a-heat-capacity` and its converse).

## Millimetres do not exist in this kernel

`Point3D` holds `Length` in SI metres. The same element written through
millimetre literals (`1700_mm`) and through metres (`1.7`) gives the same `Ke`
and `Ce` to 1e-14 (`DimensionalAnalysis`). The unit audit (grep for `1e3`,
`1000`, `1e-3`, `0.001` in `ThermalElement.{hpp,cpp}`) is in
[DEGENERACY.md](DEGENERACY.md) alongside the other source audits.
