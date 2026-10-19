# P18-ASSEMBLY-001 — the sparse format

```text
SUBJECT:  the containers a global thermal system is held in, and why they are
          BetterCAD's own
```

## Compressed sparse row, the same as P17

`ThermalSparseMatrix` is three vectors, deliberately the representation of P17's
`StiffnessMatrix` (ADR-038), so a solver can wrap them in
`Eigen::Map<const Eigen::SparseMatrix<double, RowMajor, std::int64_t>>`, SuiteSparse
or anything else at zero copy:

```text
rowStart   size rows()+1, rowStart[0] = 0, non-decreasing, rowStart[rows()] = nonZeros()
inner      size nonZeros(), STRICTLY ASCENDING within a row, each < columns()
values     size nonZeros(), parallel to inner, SI
```

- **64-bit indices.** A 32-bit inner index is the classic overflow. The bound is
  proved by a `static_assert` (a 32-bit `NodeId` bounds the unknowns, and the entry
  count runs at forty times that).
- **No linear-algebra library is linked into `bettercad_thermal`.** Its CMake file says
  P18-SOLVE-001 decides that. Nothing new is linked.
- **An absent entry is zero.** `coeffSi` returns zero for a pair the pattern does not
  hold. The symmetry measure counts a missing transpose as zero, so a stored `K(i,j)`
  with `K(j,i)` absent is an asymmetry of `|K(i,j)|`, not a pass.
- **Possession is the evidence.** The only constructor is `fromCsr`, which validates
  the layout, every column, every value and, if asked, the symmetry. A matrix whose
  arrays are inconsistent does not exist (six named problems, each reachable and
  tested).
- **Nothing is dense.** There is no `toDense`; the small-system oracles in the tests
  build their own dense copies from `coeffSi`.
- **`multiply` and nothing else.** Every invariant worth checking at assembly time
  (`K 1`, `1^T C 1`, `T^T K T`) is a product, and a solver needs one too. No
  factorisation, no transpose, no solve.

## Typed wrappers

`ConductanceMatrix` (W/K) and `CapacityMatrix` (J/K) are
`TypedThermalMatrix<ThermalConductance>` and `<HeatCapacity>`: different types, so K
cannot be handed where C is wanted (compile-fail cases 1 to 3). They add a typed
accessor and nothing else.

`ThermalHeatVector` is dense, `Ndof` long, every entry finite, zero-initialised by the
assembler.

## The symbolic and numeric passes

```text
symbolic   per row, the columns it will ever receive (volume pattern, then facets);
           sort + unique per row; lay out the CSR.  O(nnz)
numeric    values[slot] += v   for each local entry, slot found by binary search
```

A triplet list would have staged sixteen records per element. The two-pass design
needs one slot per `(row, column)`, so a duplicate contribution is a **sum by
construction**, in a fixed order, and not by a library's reduction policy. This is P17's
design adopted, with the same reason (memory, and determinism).

## Measured scale

| mesh | unknowns | tetrahedra | nnz(K) | `nnz / Ndof^2` |
| --- | --- | --- | --- | --- |
| 40 x 30 x 20 mm block (real P16) | 9 | 12 | 61 | 0.753 |
| fine 6 mm cylinder (real P16) | 900 | 4118 | 11 506 | 1.42e-2 |
| synthetic 22^3 Kuhn grid (smoke) | 10 648 | 55 566 | 148 366 | 1.31e-3 |
| sparse handles (4 nodes, max id 4 294 967 294) | 4 | 1 | 16 | 1 |

The synthetic grid's dense form would be 113 379 904 doubles. It is never formed; the
smoke test counts `nnz / Ndof^2` and checks finiteness and symmetry only.