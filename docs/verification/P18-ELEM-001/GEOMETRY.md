# P18-ELEM-001 — geometry: Jacobian, volume, gradients, G

```text
SUBJECT:  Tet4Geometry -- V, det J and G -- against two independent oracles
SOURCE:   include/bettercad/thermal/ThermalElement.hpp, src/thermal/ThermalElement.cpp
TESTS:    ThermalElement_UnitTetGeometry, _GeneralTetGeometry,
          _VolumeAgreesWithMeshingSignedVolumeBitwise, _ShapeGradient*,
          _AffineTemperatureGradient, _ConstantFieldHasZeroGradient
```

## What is implemented (the frozen convention, FORMULATION.md)

```text
local node order      [n1, n2, n3, n4] exactly as the P16 Tetrahedron stores
                      them; no sort by NodeId, coordinate or unknown
local temperatures    [T1, T2, T3, T4]^T, the same order
J                     columns are the edge vectors from corner 1:
                          | x2-x1  x3-x1  x4-x1 |
                      J = | y2-y1  y3-y1  y4-y1 |      J[i][j] = d x_i / d xi_j
                          | z2-z1  z3-z1  z4-z1 |
det J                 e1 . (e2 x e3), grouped exactly as meshing::signedVolume
V                     det J / 6, NO absolute value
grad_x Ni             J^-T grad_ref Ni, J^-1 by the adjugate
G                     3 x 4, row = axis, column = corner; grad T = G Te
```

## Unit tetrahedron — the primary exact oracle

```text
nodes     x1 (0,0,0)  x2 (1,0,0)  x3 (0,1,0)  x4 (0,0,1)      metres

J         | 1 0 0 |
          | 0 1 0 |         det J = 1          V = 1/6 = 0.16666666666666666
          | 0 0 1 |

grad N1   (-1, -1, -1)       grad N2 ( 1, 0, 0)
grad N3   ( 0,  1,  0)       grad N4 ( 0, 0, 1)            1/m

G         | -1  1  0  0 |
          | -1  0  1  0 |       expected by hand from FORMULATION.md
          | -1  0  0  1 |

max |G - G_hand|   0.00e+00       |det J - 1|   0        |V - 1/6|  0
```

## General tetrahedron — the oracle that can see a transposed Jacobian

The unit tetrahedron has `J = I`, so it cannot tell `J` from `J^T`. The
brief's fixture can:

```text
x1 (0.2, -0.4, 0.1)   x2 (1.7, 0.3, 0.2)   x3 (0.4, 1.5, 0.6)   x4 (0.1, 0.2, 2.0)   m

J = | 1.5  0.2 -0.1 |      e2 x e3 = (3.31, -0.43, 0.31)
    | 0.7  1.9  0.6 |      det J = 1.5*3.31 + 0.7*(-0.43) + 0.1*0.31 = 4.695
    | 0.1  0.5  1.9 |      V = 0.7825 m^3   (positively oriented, checked first)
```

The expected `G` comes from the independent route of
[ANALYTICAL_ORACLES.md](ANALYTICAL_ORACLES.md), never from production:

```text
V   production 0.7824999999999999   oracle 0.7825000000000001   rel 2.84e-16
G   max relative |G - G_oracle|    1.57e-16
over the 4 named fixtures + 25 seeded random tetrahedra:
    worst relative |G - G_oracle|  < 1e-12 asserted;  V within 1e-12
```

## Volume agrees with P16's, bit for bit

`V` is `meshing::signedVolume`'s expression with its association, not a
second definition. On 4 fixtures and 50 seeded random tetrahedra
`g.volume().si() == meshing::signedVolume(...)` holds **exactly**
(`==`, not a tolerance), and `det J = 6 V` to 1e-15. P17 found a cofactor
expansion differs by one ulp on a skew tetrahedron; the same grouping is used
here for the same reason.

## Partition of unity, measured

`sum_i grad Ni = 0`, i.e. `G [1 1 1 1]^T = 0`. All four gradients are computed
by the full product, **not** `grad N1 = -(grad N2 + grad N3 + grad N4)`, so
this is a measurement and not an identity that holds by construction.

```text
worst |G 1| / max|G|   1.30e-16   over 29 fixtures (asserted <= 1e-14)
```

## Coordinate reproduction — the component-swap detector

```text
T_i = x_i   ->  grad T = (1, 0, 0)       worst error 1.22e-15
T_i = y_i   ->  grad T = (0, 1, 0)       worst error 5.55e-16
T_i = z_i   ->  grad T = (0, 0, 1)       worst error 1.33e-15
```

over 14 fixtures. A swapped y/z or transposed `G` fails here (mutation E03).

## The core patch oracle — a general affine field

`T = 305.7 + 7.2 x - 3.5 y + 11.4 z` gives `grad T = (7.2, -3.5, 11.4) K/m`
on every valid tetrahedron, with the 305.7 K offset gone.

```text
worst |grad T - (7.2, -3.5, 11.4)|   3.49e-11 K/m   (the 1000:1 sliver)
in rounding units eps |G||T|          1.63           (asserted <= 64)
```

The absolute error looks large only because the sliver has `|G| ~ 1e3 /m` and
`T ~ 300 K`: the error of a sum of products is bounded by the size of the
products, not of the answer. The assertion is made in those units. On the
well-shaped fixtures the error is two orders smaller.

## Constant field

`T0 = 327.4 K` at all four corners: `grad T = 0` and `Ke T = 0` to
`1e-13 * |G| * 327.4` and `1e-13 * |Ke| * 327.4`, on every fixture including
the sliver.

## Rejection

An inverted, coplanar, coincident-corner or non-finite tetrahedron yields no
`Tet4Geometry` at all: possession of the type is the proof the geometry is
valid, and its constructor is private (compile-fail
`geometry-constructed-directly`). See [DEGENERACY.md](DEGENERACY.md).
