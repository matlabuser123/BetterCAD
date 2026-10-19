# P18-ASSEMBLY-001 — the dense oracle and its independence

```text
production sparse scatter used to compute the expected dense matrix:   NO
production assembly helper reused in the oracle:                       NO
```

## What the oracle is

`DenseOracle` in `tests/thermal/ThermalAssemblyTestSupport.hpp` is a plain array of
doubles with one routine:

```text
target[global[i] * n + global[j]] += local[i][j]        for i, j in the local range
```

written as two nested loops in the test. It has **no** `slotOf`, no `layoutOf`, no
`ThermalSparseMatrix`, no `ThermalSystem` and no `ThermalDofMap::elementDofs`. The only
production code it touches is on the *input* side of the comparison, never in the
expected value.

## Two oracle paths

1. **Hand meshes** (one tetrahedron, two tetrahedra, a sparse-handle pair, a
   disconnected pair, a convection facet). The local matrices are written out in the
   test from the derivations in SINGLE_TET_ORACLE.md and TWO_TET_ORACLE.md. Nothing
   upstream is asked for a number except the nodal heat input, which is
   P18-LOAD-001's by ownership and is compared to its own closed form separately.
2. **The real block** (9 unknowns). The local matrices come from the qualified
   P18-ELEM-001 kernels, called from the test, and the scatter is the test's own: rows
   from `ThermalDofMap::dofOf` **per corner**, not from `elementDofs`, which is what the
   production code uses. What is independent is the scatter, which is what this
   milestone owns; the kernels are qualified elsewhere. Convection facets use the
   prepared boundary's own bound unknowns and local terms.

## The comparison

`toDense(sparse)` is built from `coeffSi`, so it also exercises the lookup, and the
expected value is the oracle's array. The result:

| Case | max abs error in K | in C | in Q |
| --- | --- | --- | --- |
| two tetrahedra | `0.00e+00` | `0.00e+00` | `0.00e+00` |
| one tetrahedron, convection + flux | `<= 1e-14` | `<= 1e-14` | `<= 1e-13` |
| real block, 9 x 9 | `0.00e+00` (`max|K| = 6.1`) | `0.00e+00` | `0.00e+00` |

An oracle that cannot fail is worthless, so the mutation probes (MUTATIONS.md) include
scatter variants (overwrite instead of sum, wrong column map, rotated facet rows, a
one-entry epsilon) and every one is killed by an oracle comparison.