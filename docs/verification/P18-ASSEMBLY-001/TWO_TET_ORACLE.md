# P18-ASSEMBLY-001 — the two-tetrahedra oracle

```text
SUBJECT:  two tetrahedra sharing a face; every shared entry is a sum
```

## Connectivity

```text
nodes     1 (0,0,0)  2 (1,0,0)  3 (0,1,0)  4 (0,0,1)  5 (1,1,1)
A         [1 2 3 4]   dofs [0 1 2 3]     V = 1/6
B         [2 3 4 5]   dofs [1 2 3 4]     V = 1/3
shared    nodes 2,3,4  =  dofs 1,2,3     (the face (1,0,0) (0,1,0) (0,0,1))
only A    dof 0           only B    dof 4
```

## Local matrices (derived by hand, `k = 6`, `rho cp = 120`)

```text
Ke_A = G^T G                       diag [3 1 1 1], off -1 against dof 0, 0 elsewhere
Ke_B = k V G_B^T G_B = 2 G_B^T G_B   diag 1.5, off -0.5  (grad N_B = (1,-1,-1)/2 ... (1,1,1)/2)
Ce_A = 1 * [2 1 1 1; ...]          diag 2, off 1
Ce_B = 2 * [2 1 1 1; ...]          diag 4, off 2
Q_A  = q''' V_A / 4 = 1 W per corner      Q_B = q''' V_B / 4 = 2 W per corner  (q''' = 24)
```

## Global entries

| Entry | A | B | Expected sum | Actual |
| --- | --- | --- | --- | --- |
| `K(0,0)` | 3.0 | — | 3.0 | 3.0 |
| `K(4,4)` | — | 1.5 | 1.5 | 1.5 |
| **`K(1,1)`** | 1.0 | 1.5 | **2.5** | **2.5** (neither 1.0 nor 1.5) |
| `K(2,3)` | 0 | -0.5 | -0.5 | -0.5 |
| `K(0,4)` | — | — | 0 | 0 (absent: no element holds both) |
| **`C(1,1)`** | 2 | 4 | **6** | **6** |
| `C(1,2)` | 1 | 2 | 3 | 3 |
| **`Q(1)`** | 1 W | 2 W | **3 W** | **3 W** (neither 1 nor 2) |
| `Q(0)`, `Q(4)` | 1 W, — | —, 2 W | 1 W, 2 W | 1 W, 2 W |

`nnz(K) = 16 + 16 - 9 = 23` (the shared 3 x 3 block is stored once). `1^T C 1 = 120
(1/6 + 1/3) = 60 J/K`. `||K 1||_inf = 0`.

## Independent dense oracle

`ThermalAssembly_SmallSystemMatchesIndependentDenseOracle`: the full 5 x 5 `K`, `C` and
the length-5 `Q` against a `DenseOracle` fed the hand matrices:

```text
max |K error| 0.00e+00     max |C error| 0.00e+00     max |Q error| 0.00e+00
```

A second system, one tetrahedron with a convection face and a surface flux
(`A_face = 1/2`, `h = 4`, `T_inf = 300`, `q'' = 10`), is checked the same way: embedded
`Kconv = (1/6)[2 1 1; 1 2 1; 1 1 2]` and `Qconv = 200 W` and `q'' A / 3 = 1.667 W` at the
three face unknowns, summed with the volume terms.

Also: a permutation test lists the same tetrahedra with one cyclic rotation of the first
three corners each (`A` as nodes `[2 3 1 4]` instead of `[1 2 3 4]`, `B` as `[3 4 2 5]`
instead of `[2 3 4 5]`; a cyclic rotation of three is an even permutation, so the
orientation stays positive) and the global matrices are unchanged to `1e-14`, which
shows the local rows move with the local matrix and nothing is sorted. The same is
checked for the single unit tetrahedron to `1e-15`.