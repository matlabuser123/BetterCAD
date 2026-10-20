# P18-SOLVE-001 — determinism

```text
TESTS:  ThermalSolve_DeterministicRepeat, _RepeatedBlockSolveIsDeterministic,
        _BitFingerprintForCrossPresetComparison, _SolveDoesNotMutateItsInputs,
        _SolveDoesNotMutateTheModelOrTheSystem, Linalg_DeterministicRepeat
CLAIM:  the same frozen source gives the same temperatures, residuals, pivot ratio, full residual,
        provenance, classification and diagnostic text, BIT FOR BIT on the same build; across
        presets the claim is MEASURED, not assumed
```

## What makes it structural

Everything is serial. The reduction visits the full matrix row by row in ascending unknown order, so
`Kff`, `Kfc` and `Kfc Tc` are formed in a fixed order; the anchoring analysis is a union-find whose
root is always the smaller representative, so its outcome does not depend on the order edges are met
in; the kernel factorises with Eigen's `SimplicialLDLT` single-threaded (no OpenMP, no threads, no
`std::execution` in either file, audited), whose AMD ordering is a deterministic function of the
pattern; and the CSR handed to it holds each `(row, column)` once, so `setFromTriplets` has no
duplicate to reduce and its (unspecified) summation order cannot matter. No unordered container exists
in `ThermalSolve.cpp` or `SymmetricSolve.cpp`.

## Same-preset repeatability

```text
40 solves of the interleaved 5-unknown case, compared with the first by the class's own equality:   0 bitwise differences
20 solves of a 23-unknown SPD system with two prescribed unknowns:                                   0 bitwise differences
20 solves of a singular model (pure Neumann): same classification, same message every time:          0 differences
30 solves of the real block (Dirichlet + convection + generation, 9 unknowns) through the model-level entry: 0 bitwise differences
40 generic-kernel solves (4 x 4 SPD): identical solution, residual norms and pivot ratio; 10 singular: identical failure
```

"Equality" compares every temperature, the full residual, the residual norms, the pivot ratio, the anchoring
analysis and the provenance, bit for bit.

## Cross-preset

Measured on the qualifying run, on each preset's OWN binary, after the run
(`qualification/postrun-summary.txt`):

```text
preset             ThermalSolve_*                    Linalg_*                bit fingerprints
                                                                              5-unknown          23-unknown SPD     real block solve
debug-ext          51 / 51, 39 532 assertions        10 / 10, 171            b56e5e03da24db75   cddff5146fccbec9   08d6173d68fdd1e1
release-ext        51 / 51, 39 532 assertions        10 / 10, 171            b56e5e03da24db75   cddff5146fccbec9   08d6173d68fdd1e1
debug-shared-ext   51 / 51, 39 532 assertions        10 / 10, 171            b56e5e03da24db75   cddff5146fccbec9   08d6173d68fdd1e1

distinct fingerprints: 1, 1 and 1 -- BITWISE IDENTICAL across the three presets
the suite's 60 measurement lines (sorted, timings normalised): identical in all three, 0 differing
classifications (missing_temperature_datum, unanchored_thermal_region, singular_system, stale_*): identical
the P17 structural suite through the extracted kernel: 415 / 415, 271 414 assertions, identical in all three
```

Each fingerprint is a 64-bit FNV-1a hash over bit patterns: every temperature, every component of the
full residual, the normalized residual and the pivot ratio. They are recorded, not promised: the tests
assert only that each is reproducible within one process. That all three presets agree bit for bit is a
measured property of this toolchain, which compiles in ISO mode without fused-multiply-add contraction
and evaluates the same double-precision operations in the same order at every optimisation level and in
both linkages, **including inside Eigen's factorisation**. A different compiler, a different Eigen
version or a `-ffast-math` build would be expected to move the last bits; the tolerances elsewhere in
the suite (1e-9 to 1e-14) are what would hold in that case.

## The brief's cross-preset fixtures

| Fixture | Printed measurement |
| --- | --- |
| solution `T` | structured grid 6^3: max error 1.25e-12 K; block: 5.7e-14 K; series with convection: 1.7e-13 K |
| free residual | interleaved: 2.54e-13 W absolute, 3.56e-17 normalized; grid 18^3: 3.37e-16; cylinder: 1.28e-17 |
| constraint values | every prescribed unknown equal to its value exactly, all presets |
| status and diagnostic code | datum, region, singular, stale, constraint, numbering: identical |
| singularity classification | `[1 -1; -1 1]` and its chain generalisation: `missing_temperature_datum`; disconnected: `unanchored_thermal_region` |
| sparse solve | 18^3: Ndof 5832, Nfree 5184, nnz(Kff) 70 766, pivot ratio 1.75e-1; cylinder: Ndof 900, nnz(Kff) 8 500, ratio 2.1e-3 |