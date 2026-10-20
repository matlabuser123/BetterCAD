# P18-SOLVE-001 — the solver backend

```text
backend:                    Eigen 5.0.1, header-only, private to bettercad_linalg (MPL-2.0, ADR-039)
algorithm:                  SimplicialLDLT (sparse LDL^T, no pivoting) of the reduced matrix Kff
matrix type:                BetterCAD CSR (64-bit unsigned indices, strictly ascending columns) converted
                            once to Eigen::SparseMatrix<double> (signed 32-bit StorageIndex), O(nnz)
ordering:                   Eigen's default AMD fill-reducing permutation, internal to the factorisation;
                            the solution is returned in the order the system was given
thread mode:                single-threaded: no OpenMP, no BETTERCAD parallelism, no std::execution; the
                            kernel and the reduction are serial
factorization status API:   solver.info() after compute() and after solve(), AND vectorD() read by BetterCAD
singularity handling:       three independent conditions, below
fallback:                   NONE: one algorithm, no retry with another
```

## The three conditions (unchanged from P17)

```text
1  info() == Success, every D(k,k) > 0, and min|D| / max|D| >= pivotFloor
2  every component of x is finite
3  the independent residual r = A x - b is finite and its normalized norm <= relativeResidualTolerance
```

`SimplicialLDLT` reports `NumericalIssue` only when a pivot is **exactly** zero, so a matrix that is
singular in floating point almost never fails there and `solve()` returns an enormous, finite vector
lying in the null space — for which `A x` is still close to `b`, so the residual passes too. The
pivot ratio is the only thing that refuses it, which is why it is a gate and not a diagnostic.
(These are P17's measurements, ADR-039; the thermal tests reproduce them on thermal fixtures:
`[1 1; 1 1]` is an exact zero pivot, `[1 2; 2 1]` has pivots 1 and -3, `diag(1, 1e-14)` has ratio 1e-14.)

## The kernel in thermal terms

| Kernel quantity | Thermal meaning |
| --- | --- |
| `A` | `Kff`, W/K |
| `b` | `beff = Qf - Kfc Tc`, W |
| `x` | `Tf`, K |
| `ResidualNorms::euclidean` | `||r||_2` in W |
| `ResidualNorms::matrixInfinity` | `||Kff||_inf` in W/K |
| `ResidualNorms::normalized` | `||r||_2 / (||Kff||_inf ||Tf||_2 + ||beff||_2)`, dimensionless |

## What the backend is not asked to do

It is not asked whether the model is well posed (the anchoring analysis in `ThermalSolve.cpp` is),
whether the constraints hold (the postcondition is), or whether the answer is right (the residual
is, and it is computed by BetterCAD from the original arrays). The library's own error estimate is
never consulted.