# P18-ASSEMBLY-001 — analytical validation

```text
SUBJECT:  hand-derived expected systems, and the independent oracles the production
          scatter is compared against
```

Every expected value below is derived by hand from the shape functions or from physics
and typed into a test. The only oracle that uses a qualified kernel is the dense scatter
for the real meshes, where what is independent is the **scatter** (nested loops in the
test, rows from `dofOf` per corner rather than `elementDofs`), which is what this
milestone owns.

## Single unit tetrahedron (corners (0,0,0) (1,0,0) (0,1,0) (0,0,1), V = 1/6)

Shape gradients are `(-1,-1,-1)`, `(1,0,0)`, `(0,1,0)`, `(0,0,1)`. `Ke = k V G G^T` with
`k = 6` is

```text
K = [  3 -1 -1 -1 ]
    [ -1  1  0  0 ]      W/K,  max entry error 0.00e+00
    [ -1  0  1  0 ]
    [ -1  0  0  1 ]
```

and `Ce = (rho cp V / 20)[2 1 1 1; ...]` with `rho cp = 120` is `[2 1 1 1; 1 2 1 1; 1 1 2 1;
1 1 1 2]` J/K, error `0.00e+00`. Generation `q''' V / 4` per node, and the composite
(`K`, `C`, generation, a surface flux and a convection face together) are separate cases:
the combined `Q` is 12 W generation + 5 W surface + 600 W convection source = 617 W,
actual 617.

## Two tetrahedra sharing a face

`B` has fifth node (1,1,1) and `V = 1/3`. The shared nodes receive **both** contributions:

```text
K(1,1) = A 1.0 + B 1.5 = 2.5           Q(1) = 1 W + 2 W = 3 W
K(2,3) = A 0   + B -0.5                K(0,4) = 0   (no shared element)
nnz(K) = 23    ||K 1||_inf = 0.00e+00
```

A zero entry from one element and a non-zero from the other stays in the pattern. The
complete `5 x 5` `K`, `C` and `Q` equal the independent dense oracle with
`max |error| = 0.00e+00`.

## Resultants and invariants

| Invariant | Hand mesh | Real 9-node block |
| --- | --- | --- |
| `K 1 = 0` (pure conduction) | `||K 1|| = 0` | `2.22e-16` against `max|K| = 6.1` |
| `1^T C 1 = rho cp V` | 20 J/K (one tet, `120/6`) to `1e-14`; 60 J/K (two tets, `120 (1/6 + 1/3)`) to `1e-13` | `86.664 J/K` = `7850 * 460 * 2.4e-5` |
| `1^T K 1 = h A` with one convection face | `2.0` added by a `h = 4`, `A = 0.5` face, to `1e-14` | `25 * 0.0012` to `1e-12` relative |
| `sum Q = q'' A + q''' V + h T_inf A` | 617 W | `0.6 + 60 + 8.7945 = 69.3945 W` |
| `T^T K T` against the sum of local energies | `39.0073708222` both | `5075.81169256` both |
| `v^T C v` against the sum of local energies | exact | `8018351.61539` both |
| `K`, `C` symmetric | `0.00e+00` | `0.00e+00` |

The convection source is the ambient part only, so the block's 8.7945 W is not heat
leaving the body (ASSEMBLY_MODEL.md).

## Independent dense oracle on a real mesh

The block (9 unknowns, 12 tetrahedra, 61 stored entries, two convection facets): `K`, `C`
and `Q` against the oracle, `max |K error| = 0.00e+00` (`max|K| = 6.1`), `|C error| = 0`,
`|Q error| = 0`.

## Physical tests that are NOT claimed

This milestone solves nothing, so there is no temperature profile, no `T = q'''L^2/(2k)`
check and no energy balance. Those belong to P18-SOLVE-001 and P18-BALANCE-001. What is
shown here is that the *matrices a solver will be given* have the exact structure those
checks need.