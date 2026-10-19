# P18-ASSEMBLY-001 — symmetry, the null mode, conservation and energy identities

```text
SUBJECT:  the structural invariants of a correct assembly, none of which is repaired
          into place
```

## Symmetry

```text
max |Kij - Kji|     0.00e+00 against max |K| = 3.411 W/K     PASS (hand grid)
max |Cij - Cji|     0.00e+00 against max |C| = 6 J/K          PASS
real block          0.00e+00 for both
```

The scatter writes the same local numbers into both triangles in the same order, so a
correct assembly is symmetric **to the last bit**. The production gate is
`max |A(i,j) - A(j,i)| <= 1e-13 * max |A|`, a refusal and never a repair. There is no
post-symmetrisation anywhere (see the audit in README.md).

The sparse measure counts a missing transpose as zero, so `A(i,j)` stored with `A(j,i)`
absent is an asymmetry of `|A(i,j)|` (test `SparseSymmetryCheckSeesAMissingTranspose`).
An assembly that scattered only the upper triangle is refused by it (mutation A13).

## Pure-conduction null mode

| Mesh | `||K 1||_inf` | Scale `max |K|` |
| --- | --- | --- |
| single unit tetrahedron | `<= 1e-15` | 3 |
| two tetrahedra | `0.00e+00` | 3 |
| real block, 9 unknowns | `2.22e-16` | 6.1 |
| 9^3 Kuhn grid (729 unknowns) | within `1e-12 * max|K|` | |

This is the evidence that no `epsilon`, spring, penalty or pin was added anywhere. With
one convection face the constant is lifted: `1^T K 1 = h A` (`2.0` for `h = 4`,
`A = 0.5`; `0.03` W/K for the block's end cap at `h = 25`).

## Capacity conservation

```text
1^T C 1 = rho cp V_total
single tetrahedron       20 J/K          (120 / 6)
two tetrahedra           60 J/K          (120 (1/6 + 1/3))
real block               86.664 J/K      (7850 * 460 * 2.4e-5)
```

## Energy identities

```text
global  T^T K T                      =  sum_e Te^T Ke Te  +  sum_faces Tf^T Kconv Tf
global  v^T C v                      =  sum_e ve^T Ce ve
```

| Fixture | `T^T K T` | sum of local | difference |
| --- | --- | --- | --- |
| hand grid | 39.0073708222 | 39.0073708222 | 0 |
| real block, `T_i = 300 + 17 sin(1.7 i + 0.3)` | 5075.81169256 | 5075.81169256 | `<= 1e-11` relative |
| real block `v^T C v` | 8018351.61539 | 8018351.61539 | `<= 1e-11` relative |