# P18-TRANSIENT-001 — determinism

```text
SUBJECT:  the same inputs give the same bits: across runs, across the three presets, and independent of anything
          the process can observe about itself
```

## What could make a transient run depend on something other than its inputs, and what rules it out

| Source of variation | Rule | Evidence |
| --- | --- | --- |
| summation order | every sum is a serial loop in a fixed order: the CSR rows ascending, the columns ascending within a row, the union merge of `C/dt` and `K` entry by entry, the history product `C T[n]` row by row, the norms in index order | source audit: 0 threads, 0 parallel algorithms, 0 `unordered_*`; the bit fingerprints below |
| the time grid | `t_k = k * dt` (a product), the last step `t_end - floor(ratio) * dt`, one tolerance constant; no running sum, no clock | `TimeGridMultipliesAndNeverAccumulates` (100 000 steps bit for bit); `TimeGridIsDeterministic` |
| the factorisation | `linalg::solveSymmetricSparse`: one serial `SimplicialLDLT`, no OpenMP, no threads (ADR-052) | 30 + 20 repeated runs bitwise equal; fingerprints equal across presets |
| the free-equation numbering | P18-DOF-001's maps, ascending | unchanged and qualified |
| the order of steps | strictly increasing; a step is never skipped, merged or reordered; the state advances exactly once per step | mutation TA04 (the state does not advance) killed |
| floating-point contraction and precision | the build uses `-std=c++23` (ISO mode, `-ffp-contract=off`) and no `-ffast-math`; every preset is IEEE double | the three presets agree to the last bit, including `release-ext` at `-O2` |
| randomness, clocks | none: `rand`, `time(`, `chrono`, `clock` are absent from both files | source audit |
| the output policy | `FinalOnly` and `EveryStep` produce the same final state to the last bit | `OutputPoliciesStoreWhatTheyPromise` |
| the inputs | read, never written: matrices, heat vector, constraints, initial vector, model, system, boundary, document revision | `TheInputsAreNeverModified`, `RunDoesNotMutateTheModelOrTheSystem` |

## Repeated runs

```text
ThermalTransient_RepeatedRunsAreBitwiseIdentical   30 algebraic runs (three unknowns, one prescribed, a shortened last step,
                                                   10 states) compared with operator== on the whole result: 0 differences
ThermalTransient_RepeatedBlockRunIsDeterministic   20 runs of the real block (Dirichlet + convection + generation, 15 steps,
                                                   the last of 2 s): 0 differences
```

`operator==` on the algebraic result compares the grid, the sample times, every temperature, every step record
(both residuals and the pivot ratio), the resolved initial state, the override count and the kernel-solve count.

## Cross-preset

The suite prints two 64-bit FNV-1a fingerprints over the bit patterns of every stored temperature, every time and every
per-step residual and pivot ratio. `postrun-summary.txt` extracts them from each preset's own `bettercad_tests.exe`:

```text
algebraic repeated-run fingerprint     debug-ext  a52a8f274b935773   release-ext  a52a8f274b935773   debug-shared-ext  a52a8f274b935773
real block run (9 unknowns, 16 states) debug-ext  e9e19be85000cc6d   release-ext  e9e19be85000cc6d   debug-shared-ext  e9e19be85000cc6d
```

Each is **one** distinct value over the three presets. The 51 message lines the transient suite prints (every measured
value: errors, orders, tau, energies, residuals, with the timings normalised) are identical across the presets (0
differences in each of the two comparisons). The residuals recorded per step are part of the fingerprint, so `release-ext`
reproduces even the rounding noise of the acceptance step.

## What is not claimed

Bit equality across compilers or machines. The qualified compiler is GCC 16.1.0 on one machine; a different libm or a
different FMA contraction policy may change the last bit of an exponential in a test oracle, which is why the oracles are
compared with tolerances while the production result is fingerprinted.
