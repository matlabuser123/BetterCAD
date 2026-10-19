# P18-ASSEMBLY-001 — sparse smoke

```text
SUBJECT:  that a mesh larger than any hand case assembles sparsely, finitely and
          symmetrically, and is never made dense
```

| Field | Synthetic grid | Real fine cylinder |
| --- | --- | --- |
| fixture | 22 x 22 x 22 nodes, Kuhn six-tetrahedra cells, `h = 1 mm` | P16 6 mm-radius cylinder, fine control |
| `Ndof` | 10 648 | 900 |
| `Ntet` | 55 566 | 4118 |
| boundary facets | — | 572 |
| `nnz(K)` | 148 366 | 11 506 |
| `nnz(C)` | 148 366 (transient) | — (steady; the material has `k` only) |
| `Ndof^2` | 113 379 904 | 810 000 |
| `nnz / Ndof^2` | **1.31e-3** | 1.42e-2 |
| assembly status | success | success |
| every entry finite | yes | yes |
| `K` symmetric | `0.00e+00` | `0.00e+00` |
| `K 1 = 0` | within `1e-12 max|K|` | — |
| `sum Q` against the resultants | yes | `1e-12` relative |
| time (debug) | 2.59 s | 0.137 s |

**Dense production allocation found: NO.** The production code has no `n * n` allocation
and no `toDense`; the only dense arrays are in `tests/` and are bounded by the hand
meshes and the 9-node block. The smoke test counts `nnz / Ndof^2` and asserts it is
below `0.1`; it never forms the 113-million-entry dense matrix.

Sparse identifiers: a four-node mesh whose largest `NodeId` is 4 294 967 294 assembles
to a `4 x 4` system with 16 entries, and handles `7 42 104 9001 500000` assemble to a
`5 x 5` one, identical to the dense-handle case. The dimension is the number of unknowns,
never the largest handle.