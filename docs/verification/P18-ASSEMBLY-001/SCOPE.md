# P18-ASSEMBLY-001 — scope

```text
SUBJECT:  what this milestone implements and what it does not, with the
          evidence for each "not"
```

## Implemented

| Capability | Where |
| --- | --- |
| BetterCAD CSR sparse matrix with a validating constructor | `ThermalSparse.hpp/.cpp` |
| typed conductance and capacity matrices, dense finite heat vector | `ThermalSparse.hpp` |
| deterministic two-pass sparse scatter of `Ke`, `Ce`, the heat input and the Robin terms | `ThermalSystem.cpp` (`assembleThermalSystemFrom`) |
| model-level entry with source-compatibility checks | `assembleThermalSystem`, `thermalAssemblyFailure` |
| provenance, stale reasons, `isCurrent`, `requireCurrent` | `ThermalSystemSource`, `staleReasons` |
| steady (no C) versus transient (C) material contract | `run` over `resolveThermalMaterial` |

## NOT implemented, and the evidence

| Not implemented | Evidence |
| --- | --- |
| Dirichlet elimination or application | prescribed-temperature test: bit-identical matrices with and without; compile-fail ..._HAS_NO_REDUCED_MATRIX, ..._CONSTRAINED_MATRIX |
| penalty or any regularisation | mutation A45 (a `1e-12` diagonal) is killed; compile-fail ..._HAS_NO_PENALTY; the pure-conduction null mode is exact |
| any solve, factorisation or linear algebra beyond `multiply` | no linear-algebra library linked; compile-fail ..._HAS_NO_SOLUTION, ..._FACTORISATION |
| `C / dt`, time stepping, initial condition | `dt` is not an input; compile-fail ..._CAPACITY_OVER_TIME_STEP, source has no time step |
| residual, reaction, heat balance | compile-fail ..._HAS_NO_RESIDUAL, ..._REACTIONS; `total()` documented as not a balance |
| temperature, flux or any result field | compile-fail ..._HAS_NO_TEMPERATURES |
| symmetrisation, pruning, thresholding | the symmetry check refuses and never repairs; a stored exact zero is kept (SPARSE_FORMAT.md) |
| a dense production matrix | there is no `toDense`; compile-fail ..._MATRIX_HAS_NO_DENSE_FORM |
| radiation, contact, nonlinear or temperature-dependent properties | none; the property is a constant `k`, `rho`, `cp` |
| parallel assembly | serial by design, for a reproducible summation order |
| persistence of any assembled value | nothing is written anywhere |
| a CLI, viewer or command change | none |

## Unchanged

No file under `src/core`, `src/features`, `src/meshing`, `src/structural` or `apps/`
changed, and no existing thermal header changed. The existing files touched are the
CMake lists that register the new sources and tests and the zero-match guard.

## Handoff

`P18-SOLVE-001` receives a `ThermalSystem` and a `PreparedThermalBoundary`. It owns
elimination of the constrained unknowns (`K_ff T_f = Q_f - K_fc T_c`), singularity
detection, the linear solve and the residual. It must call `requireCurrent` on the
system before solving. **It is not authorized and nothing in this milestone starts it.**