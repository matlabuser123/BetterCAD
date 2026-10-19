# P18-ASSEMBLY-001 — determinism

```text
TESTS:  ThermalAssembly_RepeatedAssemblyDeterministic, _RepeatedBlockAssemblyDeterministic,
        _FailureOrderIsDeterministic, _BitFingerprintForCrossPresetComparison,
        _BlockBitFingerprintForCrossPresetComparison, _LocalOrderIsPreservedUnderEvenPermutation
CLAIM:  the same frozen source gives the same dimension, the same pattern, the same
        values, the same Q, the same provenance and the same diagnostics, BIT FOR BIT
        on the same build; across presets the claim is MEASURED, not assumed
```

## What makes it structural

`ThermalSystem.cpp` and `ThermalSparse.cpp` are serial. There is no unordered
container, no `static`, `mutable` or `thread_local` storage, no parallelism, no clock
and no random source (the audit is in [ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md)).
Many elements write the same entry, so the order of the sums is the order of the
traversal, and every part of it is fixed by something deterministic:

```text
elements      mesh.tetrahedra(), which P16 guarantees ascending ElementId
local entries local row 0..3, then local column 0..3, in the element's own node order
              (local degrees of freedom are never sorted)
facets        the prepared boundary's conditions in the order given, then each
              condition's facets in P16's ascending facet order
Q             the prepared heat input in unknown order, then the convection sources
patterns      per row, sort + unique; one slot per (row, column), found by binary search
```

`ThermalSystem`'s defaulted `operator==` compares both matrices (layout and values),
`Q`, the element order, the facet count and the provenance bit for bit, and that is
what the repeat tests use. The **compressed storage order is also stable**: the column
indices are strictly ascending and the layout is a function of connectivity alone, so
the CSR arrays themselves are reproducible, not only the mathematical matrix.

## Same-preset repeatability

```text
50 assemblies of a two-Tet system with convection, loads and capacity, each compared with
the first by the class's own equality:                         0 bitwise differences
30 assemblies of the real block (9 unknowns, transient, loads, convection):  0 bitwise differences
diagnostics: a foreign numbering with stale loads, 5 repeats:  always dof_map_mismatch
             one failure, 10 repeats:                          same problem, same message
```

## Cross-preset

Measured on the qualifying run, on each preset's OWN binary, after the run
(`qualification/postrun-summary.txt`):

```text
preset             ThermalAssembly_*                   bit fingerprints
                                                       two-Tet+convection   9^3 grid (729)     real block (9)
debug-ext          62 / 62, 220 586 assertions         6398b41313705d28     c6a042bf47d865e0   07c4bb5e30c65a38
release-ext        62 / 62, 220 586 assertions         6398b41313705d28     c6a042bf47d865e0   07c4bb5e30c65a38
debug-shared-ext   62 / 62, 220 586 assertions         6398b41313705d28     c6a042bf47d865e0   07c4bb5e30c65a38

distinct fingerprints: 1, 1 and 1 -- BITWISE IDENTICAL across the three presets
the suite's 51 measurement lines (sorted, timings normalised): identical in all three, 0 differing
dimensions, sparsity patterns, nnz, resultants, diagnostic classes and stale reasons: identical
```

Each fingerprint is a 64-bit FNV-1a hash over bit patterns: every row start, every
column index and every value of `K` and, where present, `C`, and every entry of `Q`.
They are recorded, not promised: the tests assert only that each is reproducible
within one process. That the three presets agree bit for bit is a measured property of
this toolchain, which compiles in ISO mode without fused-multiply-add contraction and
evaluates the same double-precision operations in the same order at every optimisation
level and in both linkages. A different compiler or a `-ffast-math` build would be
expected to move the last bits, and the tolerances elsewhere in the suite (1e-12 to
1e-15) are what would hold in that case.

The brief's cross-preset fixtures, and the lines that carry them:

| Fixture | Printed measurement |
| --- | --- |
| dimensions | block: Ndof 9, Ntet 12, nnz(K) 61, nnz(C) 61, 2 convection facets; fine cylinder: 900, 4118, 11 506 |
| analytical agreement | unit tetrahedron `K` and `C` max entry error 0.00e+00; two tetrahedra against the dense oracle 0.00e+00 |
| resultants | block `Q` 0.6 + 60 + 8.7945 = 69.3945 W (actual 69.3945); hand mesh 617 W |
| null mode and conservation | block `||K 1||_inf = 2.22e-16`; `1^T C 1 = 86.664 J/K` (expected 86.664) |
| energy identities | `T^T K T` 5075.81169256 both ways; `v^T C v` 8018351.61539 both ways |
| diagnostics | material, numbering, load and boundary sources each print their classification |
| sparse smoke | 22^3 grid: Ndof 10 648, nnz(K) 148 366, `nnz / Ndof^2 = 1.309e-03` |