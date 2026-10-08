# P17-SOLVE-001 — determinism, measured

## Why it is measurable at all

```text
solver          Eigen::SimplicialLDLT, DIRECT
threading       single-threaded, audited rather than assumed
initial guess   N/A -- a direct solve has none, so there is no hidden state
                from a previous solve
iterations      N/A
ordering        AMD, deterministic for a given matrix, and solver-local
```

The threading audit, from [SOLVER_SELECTION.md](SOLVER_SELECTION.md): OpenMP is
not enabled anywhere in the BetterCAD build, Eigen's `EIGEN_HAS_OPENMP` appears
only in dense-product code and is gated on `_OPENMP`, and
`Eigen/src/SparseCholesky/` and `Eigen/src/OrderingMethods/` contain no
`pragma`, no `omp` and no `thread`. No external BLAS is linked.

So there is **no thread count to fix** and no environment variable that can
change the algorithm. Nothing in the solver's own code traverses an unordered
container either: `unordered` has zero occurrences.

## Repeat determinism

```text
Fixture              Runs  Solver          Threads  Status  Iterations  u          residual   PASS
---------------------------------------------------------------------------------------------------
block, fixed support    5  SimplicialLDLT        1  stable  N/A         BITWISE    BITWISE    PASS
                                                                        IDENTICAL  IDENTICAL
```

Measured, not asserted. Five solves of the same system, compared with
`std::ranges::equal` over the full displacement vector and the full residual
vector, plus `operator==` on the `ResidualMetrics`, the pivot ratio, the free
equation count and the whole `SolvedSystem`:

```text
Nfree                 45
pivot ratio           0.0726104
||r||_2               4.31108e-13 N
normalized residual   2.07357e-17
classification        BITWISE IDENTICAL across all five runs
```

```text
ctest -R "StructuralSolve_" --repeat until-fail:5    24/24, 120 executions
```

There is **no tolerance** in the repeat comparison, because there is nothing to
tolerance: the same operations in the same order on the same data.

## Cross-preset

The three-preset qualification runs the full unfiltered suite in `debug-ext`,
`release-ext` and `debug-shared-ext`, each from a clean build root with its own
freshly built binaries. The quantities compared across presets are the ones the
tests assert *in each preset independently*:

```text
Fixture                    Metric                         Contract
---------------------------------------------------------------------------------
2x2, 3x3 closed form       solution                       within 1e-14 relative
                                                          of the exact fraction
RM-MESH-01                 u vs independent elimination    within 1e-9 relative
block, C2                  success/failure classification  identical
block, C0 and C1           failure classification          identical
every successful fixture   normalized residual             <= 1e-9
every successful fixture    pivot ratio                     > 1e-12
constrained DOFs           displacement                    exactly 0.0
block, 5 runs              u, residual                     bitwise within a run
diagnostics                toString(SolveProblem)          string identity
```

**The failure classifications are the hard half.** A preset in which an
under-constrained model returned a large "successful" displacement would be the
brief's automatic failure, and the C0 and C1 fixtures assert the refusal in
every preset — they are ordinary tests in the unfiltered run, not a
Debug-only check.

```text
NO claim of BITWISE equality ACROSS presets is made.
```

The tests assert the properties above in each preset separately; no preset's
displacement vector was carried to another run and compared. `-ffast-math` is
not used and the arithmetic is the same source everywhere, but establishing
bitwise cross-preset equality would need the vectors persisted between runs and
that was not done — so it is not recorded.

## What would notice if the threading changed

The bitwise repeat comparison. If a future Eigen version parallelised
`SimplicialLDLT`, or if OpenMP were enabled in the build, the five-run equality
is what would fail first — before any residual or classification drifted. That
is deliberate: it is a cheap canary on an assumption the milestone depends on.

## No result shopping

The settings were chosen and written down before the measurements:
`relativeResidualTolerance = 1e-9` from backward stability, `pivotFloor = 1e-12`
from the LDLT error growth. Neither was adjusted after seeing a number, and the
evidence records the gaps — eight orders on the residual, ten on the pivot
ratio — so the margins are visible rather than claimed.

The one place a threshold was *changed* during the milestone was a test's own
expectation, not a production tolerance: a near-singular demonstration used a
perturbation of `1e-16`, which is below the ulp of 1 and therefore produced an
*exactly* singular matrix instead of a nearly singular one. It was corrected to
`1e-13` and the correction is recorded in
[ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md).
