# P18-BALANCE-001 — determinism and aggregation order

```text
SUBJECT:  the same inputs give the same bits, in the same order, in every run and every preset; the order of every sum is an upstream canonical order
```

## What fixes every order

| Sum or list | Order | Fixed by |
| --- | --- | --- |
| rows of `K T` and `C dT` | CSR row order, columns ascending | `ThermalSparseMatrix::multiply` (serial, "a serial row-order sum, so the answer is reproducible") |
| the stored energy `1^T C dT` | row order of the product | `storedEnergyChange` |
| surface and generation per-load lists | the prepared loads' order = the definition's canonical load order | `PreparedThermalLoads::contributions()` |
| the surface and generation totals | the order of those lists | `categoriesOf` (serial `+=`) |
| convection per-condition list | the prepared boundary's order = the definition's condition order | `PreparedThermalBoundary::convections()` |
| a condition's exchange | P16's ascending facet order, then the three entries of `residualFrom` as `(p0 + p1) + p2` | `convectionHeatOf` |
| reactions | ascending `ThermalDofIndex` = the constraint map's order | `reactionHeats` |
| the reaction total | the order of that list | `finish` |
| transient steps | the grid's: step `k` is samples `(k-1, k)`, size `grid.stepAt(k-1)` | `balanceTransientSolution` |
| the integrated sums | step order | the same loop |

The source audit (`qualification/source-audit.txt`) finds in `ThermalBalance.hpp` and `ThermalBalance.cpp`, in code: **0** `unordered_*`, **0** `std::hash`, **0** threads or atomics,
**0** OpenMP or parallel algorithms, **0** `rand`, `time(` or `chrono`, **0** dense matrices (every product is sparse), **0** Eigen.

## Summation accuracy: measured, not assumed

Plain fixed-order double summation is used. The brief asks whether cancellation makes it insufficient. The measurements say it does not: the worst `|B|/(gross + arithmetic)` over every qualified reference case in every
preset is `~1.7e-16` — a small multiple of machine epsilon — and the policy sits `580x` above it (TOLERANCE_MEASUREMENT.md). The largest cancellation tested, +1e6 W / -1e6 W with a 5 W net
(`LargeCancellationScale`), closes to `|B| = 0` (exactly, here) against an allowed `2.0e-7 W` on a gross flow of `2.0e6 W`. A compensated sum was therefore **not** introduced: nothing measured needs it, and a more elaborate sum would be code
without a failing case behind it. The decision is recorded so that a future case that does show sensitivity can reopen it with evidence.

## Repeat tests

| Test | Repeats | Differences |
| --- | --- | --- |
| `RepeatedAggregationDeterministic` (hand network: two convection conditions, a chain, a prescribed node, two loads) | 25 balances | **0** (the defaulted `==` over every term, detail list and scale) |
| `RepeatedBlockBalanceIsDeterministic` (real block: Dirichlet + convection + surface + generation) | 20 balances | **0** |
| `FineCylinderSteadyBalanceSmoke` | the balance twice | equal |

The bit fingerprints (FNV-1a over the bytes of the five terms, the residual and every reaction and convection total) are compared across the three presets by the post-run script and must be identical
(`qualification/postrun-summary.txt`). Their values and the per-preset results are in README.md.

## Inputs are not mutated

Every input is a `const` reference or a view. `RecoveryDoesNotMutateInputs` records the solution, the system, the prepared sets and the mesh stamp, takes three balances, and compares afterwards; the compile-fail cases
`THE_STATES_ARE_READ_ONLY`, `…REACTIONS…`, `…TERMS…`, `…INTEGRATED_ENERGY…` and `THE_SOURCE_IS_READ_ONLY` show that a held balance cannot be edited through its accessors either.

## What is not claimed

The bits are identical for the same compiler flags of one preset across runs, and (measured, not assumed) across the three presets of this qualification. Another compiler or `-ffast-math` could change the last bit; the
policy depends on nothing tighter than `1e-13` of the arithmetic scale, which is `~600x` the measured error.
