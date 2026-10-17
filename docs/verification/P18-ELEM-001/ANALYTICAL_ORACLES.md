# P18-ELEM-001 — the independent oracles

```text
SUBJECT:  what the expected values are made of, and proof that none of them
          is production's own output
```

## The independence statement

```text
production G used to generate expected G:       NO
production Ke used to generate expected Ke:     NO
production Ce used to generate expected Ce:     NO
production determinant / volume used as oracle: NO  (the oracle V is a 4x4
                                                     determinant; P16's
                                                     signedVolume is compared
                                                     separately, as a P16
                                                     agreement test)
```

The reference implementation in `tests/thermal/ThermalElementTests.cpp`
(`reference(...)`) is written so that a defect in production's Jacobian path
**cannot be mirrored** in it:

| | production | reference |
| --- | --- | --- |
| geometry route | a 3 x 3 Jacobian from edge vectors, `J^-T` applied to the reference gradients | the 4 x 4 matrix `A` with rows `[1 xj yj zj]`; `Ni(xj) = delta_ij` is solved by `A^-1`; rows 1..3 of `A^-1` ARE the gradients |
| Jacobian formed | yes | **never** -- so a transposed `J` cannot be reproduced |
| volume | `e1 . (e2 x e3) / 6`, P16's association | `det(A) / 6`, a 4 x 4 determinant by Eigen |
| `Ke` | `k V (grad Ni . grad Nj)` in nested loops | `V k G^T G` as an Eigen triple product |
| `Ce` | closed form `rho cp V/20`, doubled on the diagonal | the hand matrix `[2 1 1 1; ...]` written out, times `rho cp V / 20`; **no integration** |
| library | none (hand-written arithmetic) | Eigen (`Matrix4d::inverse`, `determinant`) |

A mechanical check of the claim, run on the final test file: the 44-line
`reference` region (the struct and its function, which also holds the hand
matrix for `Ce`) contains no `thermal::` and no `meshing::` symbol.

```text
grep 'thermal::' inside reference():            0 matches
grep 'meshing::' inside reference():            0 matches
production symbols the reference touches:        Point3D members (x, y, z reads)
```

## The oracle ladder, weakest to strongest

1. **Hand matrices on the unit tetrahedron** (`J = I`): `G` is the reference
   table; `Ke = G^T G` at `k = 6`; `Ce = [2 1 1 1; ...]` at `rho cp = 120`. Every
   number is derivable on paper in a line. They cannot see a transposed
   Jacobian, a global scale error that cancels, or a swapped axis in a
   symmetric fixture -- which is why they are only the first rung.
2. **The `[1 x y z]` inverse on the brief's general tetrahedron** and on 25
   seeded random ones, for `V`, `G`, `Ke` and `Ce`.
3. **Affine-field oracles that need no matrix at all:** `T_i = x_i` gives
   `(1, 0, 0)`; `T = a + bx + cy + dz` gives `(b, c, d)`; the energy
   `Te^T Ke Te = k V (b^2 + c^2 + d^2)` and the rigidly transported gradient
   `R (b, c, d)`. These are properties of the *physics*, not of any
   implementation.
4. **Structural identities of the formulation:** `sum_i grad Ni = 0`, null
   mode, row sums, total capacity `rho cp V`, spectrum `{rho cp V/4, rho cp V/20 x 3}`.
5. **Transformation laws:** translation, proper rotation, even/odd
   permutation, geometry scaling (`V s^3`, `G 1/s`, `Ke s`, `Ce s^3`), and
   material scaling.

A defect has to survive every rung to pass; the mutation table in
[ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md) records which rung kills each.

## Agreement achieved

```text
unit tet           max |G - hand|  0.00e+00   |Ke - hand|  0.00e+00   |Ce - hand|  0.00e+00
general tet        V rel 2.84e-16   G rel 1.57e-16   Ke rel 1.96e-16   Ce rel 0.00e+00
29 fixtures        worst G < 1e-12 (asserted)  Ke 1.21e-15  Ce 4.77e-16
```

## Eigen is a TEST dependency only

`src/thermal/CMakeLists.txt` records that no linear algebra is linked and that
admitting one is P18-SOLVE-001's decision. The kernel is hand-written 3 x 3 and
4 x 4 arithmetic, as P17's is; the test binary already links Eigen.
