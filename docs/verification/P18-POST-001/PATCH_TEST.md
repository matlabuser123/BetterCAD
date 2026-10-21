# P18-POST-001 — the affine patch test

```text
SUBJECT:  a patch of genuinely different tetrahedra carrying an affine field: the gradient, the flux and the magnitude are exact
```

## The field

```text
T(x, y, z) = 307.3 + 7.2 x - 3.5 y + 11.4 z                K, with x, y, z in m
grad T     = [7.2, -3.5, 11.4]                             K/m
k          = 5                                              W/(m K)
q          = -k grad T = [-36, 17.5, -57]                   W/m^2
|q|        = sqrt(36^2 + 17.5^2 + 57^2) = 69.650915285     W/m^2
```

The expected values are computed from the coefficients above. They are not obtained from any recovery call, and the nodal temperatures are
checked against the formula node by node (`<= 1e-12`) before the recovery is called.

## The patch

A 4x4x4-node grid (64 nodes) with every interior node jittered, split into **162** tetrahedra. The companion case
`MultiTetAffinePatchUsesGenuinelyDifferentTetrahedra` establishes that the patch is a patch and not one tetrahedron repeated:

| Property of the patch | Measured / required |
| --- | --- |
| every tetrahedron positively oriented | `det > 0` for all 162 |
| distinct volumes | more than 100 distinct values (of 162), by `llround(det · 1e9)` |
| sizes differ | largest `det` > 1.5 × smallest |
| node sharing | 64 nodes carry 162 elements of four corners each |

## Results

| Quantity | Expected | Worst error over 162 elements |
| --- | --- | --- |
| gradient | `(7.2, -3.5, 11.4)` K/m | **2.73e-13** K/m (bound 1e-9) |
| flux | `(-36, 17.5, -57)` W/m² | **1.36e-12** W/m² (bound 1e-8) |
| magnitude | 69.650915285 W/m² | **1.51e-12** W/m² (bound 1e-8) |
| `q . grad T + k |grad T|²` | 0 | 2.27e-13 (bound 1e-6) |
| `|q x grad T|` | 0 | 1.18e-13 (bound 1e-6) |

A linear tetrahedron reproduces an affine field exactly, so these are rounding-level and the bounds are loose by four orders of magnitude on
purpose: they are not tightened to the observed value, and not loosened for any mutation.

## What the patch refuses to pass

| Defect | Killed by the patch |
| --- | --- |
| a swapped pair of components | PM03, PM04 |
| a missing or doubled `k` | PM07, PM08 |
| a flipped sign (all components, or z only) | PM05, PM06 |
| a sorted or raw-id gather | PM01, PM02b |
| an L1 or max-component magnitude | PM11, PM12 |
| a unit factor on the gradient | PM10b |
| smoothing between neighbouring elements | PM32, killed by the scattered-field oracle, the convection case and the block recovery; the affine patch **cannot** see it, because every element of an affine field has the same gradient and an average of equals is equal (which is why the oracle uses a scattered field) |

The axis fields (`T = x`, `T = y`, `T = z`) and the large-mesh smoke (`LargeMeshRecoveryIsExactForAnAffineFieldAndCountsMatch`: an affine field on a
9x9x9-node grid, 729 nodes and 3072 tetrahedra, with gradient, flux and magnitude counts equal to 3072 and every magnitude finite) extend the same claim.
