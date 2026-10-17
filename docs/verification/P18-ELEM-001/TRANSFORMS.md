# P18-ELEM-001 — translation, rotation, reflection and permutation

```text
TESTS:  ThermalElement_TranslationInvariant, _HugeOffsetTranslation,
        _RotationInvariant, _GradientRotatesWithGeometry,
        _ReflectionIsNotARotation, _EvenPermutationCovariant,
        _OddPermutationRejected
```

## Translation

`t = (12.3, -7.1, 4.9) m`, not an origin-centred shift, on the general
tetrahedron:

```text
baseline vs translated   relative change
det J, V                 4.26e-16
G                        5.31e-16
Ke                       7.85e-16
Ce                       1.95e-16
```

All within 1e-13 as asserted. `G` is unchanged because only edge vectors enter
`J`.

**A very large offset.** The same element 1e6 m from the origin: edge
subtraction loses digits, and the loss is *measured* rather than assumed away.

```text
offset (1e6, 1e6, 1e6) m     V relative change 5.21e-12     Ke 8.37e-11
```

consistent with double precision resolving ~1e-10 m at 1e6 m for a
metre-sized element; asserted only to 1e-6, recorded as measured. No
requirement tighter than the arithmetic allows is imposed.

## Proper rotation

`R = Rz(31 deg) Ry(-17 deg) Rx(23 deg)`, built in the test with Eigen and
checked **before use**: `|R^T R - I| <= 1e-15`, `det R = 1` to 1e-14
(measured `1`). Not axis-aligned.

```text
baseline vs rotated           relative change
V                             1.42e-16
Ke                            1.96e-16
Ce                            1.95e-16
G' - R G                      1.69e-16
```

`Ke' = Ke` for isotropic `k` because `G' = R G` and
`G'^T G' = G^T R^T R G = G^T G`. `Ce' = Ce` because `V` is invariant.

**The gradient transforms.** An affine field is transported rigidly:
nodal values are carried to the corresponding rotated-and-translated corner,
and the recovered gradient is compared with `R (7.2, -3.5, 11.4)` -- the
rotation applied to the hand-known gradient, not to production's:

```text
|grad' - R (7.2, -3.5, 11.4)|   4.97e-14 K/m
```

## Reflection is not a rotation

`det R = -1` (a mirror in `z`) with the corner order kept flips the signed
volume: the element is **refused as inverted** (`InvertedElement`), which is
why every rotation test uses `det R = +1` and why "invariant under any
orthogonal `R`" would be a false claim. An odd reorder of the reflected corners
restores validity.

## Even permutation — covariant, not unchanged

The 3-cycle `sigma = (0 1 2)`, corners `[p2, p3, p1, p4]`, is **even**: the
orientation is preserved (checked: `signedVolume > 0`). The matrices are
*permuted*:

```text
Ke' = P Ke P^T,   Ce' = P Ce P^T,   P_{i, sigma(i)} = 1

|Ke' - P Ke P^T|   1.96e-16 (relative)       |Ce' - P Ce P^T|   0.00e+00
entrywise  Ke'_ij = Ke_{sigma(i) sigma(j)}   to 1e-13 |Ke|
raw comparison     Ke' differs from Ke by 0.6 (relative): the comparison
                   is not vacuous
```

`P` is built in the test from `sigma`, independently of production; the volume
is unchanged.

## Odd permutation — rejected

Every one of the six transpositions flips `det J` (`4.695 -> -4.695`, to
1e-12) and is refused as `InvertedElement` by `tet4GeometryProblem`,
`computeTet4Geometry`, `computeTet4Conduction` and `computeTet4Capacity`. No
`P^T Ke P` is computed for an inverted input by any path, and nothing swaps
two corners to repair it.

```text
swaps (0 1) (0 2) (0 3) (1 2) (1 3) (2 3):  all inverted, all refused
even permutation used for covariance:       (0 1 2), parity even by 2 transpositions
odd permutations used for rejection:        the six transpositions
```
