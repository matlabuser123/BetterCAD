# P18-ELEM-001 — determinism

```text
TESTS:  ThermalElement_RepeatEvaluationDeterministic, _NoHiddenState
CLAIM:  the same element evaluated again gives the same classification, the
        same diagnostic code and the same V, G, Ke and Ce BIT FOR BIT on the
        same build; across presets the claim is MEASURED, not assumed
```

## What makes it structural

`ThermalElement.cpp` is a set of pure functions over their arguments: no
`static` storage, no cache, no `mutable`, no `thread_local`, no
parallelism, no unordered reduction, fixed loop bounds of 3 and 4, and every
fixed-size array value-initialised (`{}`), so there is no uninitialised
matrix entry. The audit is in [DEGENERACY.md](DEGENERACY.md). Local matrices
are reentrant and need no lock; a thread stress test is not claimed.

## Same-preset repeatability

```text
200 evaluations of the general tetrahedron (geometry, Ke, Ce each compared
with the first by the classes' own defaulted equality over their stored
doubles):                                        0 bitwise differences
classification of an inverted and a valid element, 50 repeats each:   stable
A, then B, then A again (a different element, conductivity and capacity
between): the second A equals the first bit for bit, and B differs from A
```

## Cross-preset

Measured on the qualifying run, on each preset's OWN binary, after the run:

```text
preset             ThermalElement_*                      bit fingerprint (V, det J, G, Ke, Ce; 14 fixtures)
debug-ext          60 / 60, 11 402 assertions            a59150e680dd9121
release-ext        60 / 60, 11 402 assertions            a59150e680dd9121
debug-shared-ext   60 / 60, 11 402 assertions            a59150e680dd9121

distinct fingerprints: 1 -- BITWISE IDENTICAL across the three presets
the suite's 74 measurement lines (sorted): identical in all three
discrete classifications (verdicts, diagnostic codes, ranks, nullities): identical
```

The fingerprint is a 64-bit FNV-1a hash over the bit patterns of the volume,
the determinant, the twelve gradient entries, and the sixteen entries each of
`Ke` and `Ce` of 14 fixtures (the four named ones and ten seeded random
tetrahedra). It is recorded, not promised: the test itself asserts only that
it is reproducible within one process. That all three presets agree
bit for bit is a measured property of this toolchain, which compiles in
ISO mode without fused-multiply-add contraction and evaluates the same
double-precision operations in the same order at every optimisation level and
in both linkages. A different compiler or a `-ffast-math` build would be
expected to move the last bits, and the tolerances elsewhere in the suite
(1e-13 to 1e-15, or in rounding units) are what would hold in that case.
