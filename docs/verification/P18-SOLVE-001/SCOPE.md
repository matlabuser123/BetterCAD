# P18-SOLVE-001 — scope

```text
SUBJECT:  what this milestone implements and what it does not, with the evidence for each "not"
```

## Implemented

| Capability | Where |
| --- | --- |
| the generic symmetric sparse solve, extracted below layer 45 | `linalg` (ADR-052) |
| the exact Dirichlet reduction `Kff Tf = Qf - Kfc Tc` over P18-DOF's maps | `reduceThermalSystem` |
| anchoring analysis: missing datum, unanchored region | `analyseAnchoring` |
| the algebraic solve and the acceptance of a candidate field | `solveThermalAlgebra`, `validateSteadySolution` |
| the assembled-system and model-level solves | `solveSteadyThermalSystem`, `solveSteadyThermal` |
| a source-bound, immutable `SteadyThermalSolution` | `ThermalSolve.hpp` |
| the staleness comparison, `isCurrent`, `requireCurrent` | `SteadySolutionSource` |

## NOT implemented, and the evidence

| Not implemented | Evidence |
| --- | --- |
| temperature gradient, heat flux | compile-fail THE_SOLUTION_HAS_NO_GRADIENT, ..._HEAT_FLUX; `ThermalResult` is not produced |
| reaction heat, heat balance | THE_SOLUTION_HAS_NO_REACTIONS, ..._HEAT_BALANCE; the full residual is retained, not interpreted |
| transient solve, `C / dt`, initial condition | THE_SOLUTION_HAS_NO_TIME_STEP; a transient system is refused (`not_steady_system`) |
| penalty, epsilon, pinning, pseudo-inverse, hidden datum | THE_SOLUTION_HAS_NO_PENALTY, THE_SETTINGS_HAVE_NO_REGULARISATION, ..._REFERENCE_TEMPERATURE; probes S08, S09 |
| an algorithm selector or a fallback | THE_SETTINGS_HAVE_NO_ALGORITHM_SELECTOR; one algorithm |
| an iterative solver | none; P18 is correctness-first |
| a dense reduced matrix | THE_REDUCED_SYSTEM_HAS_NO_DENSE_FORM; the smoke solves report `Nfree^2` against `nnz(Kff)` |
| a cached or persisted factorisation | THE_SOLUTION_HAS_NO_FACTORISATION; nothing is retained |
| validation of the model beyond well-posedness (mesh quality, material completeness, balance policy) | P18-VALID-001's |
| a CLI or GUI path | none; the core API is what a later CLI calls |
| a change to any canonical data | `ThermalSolverSettings` is P18-DATA's and was not edited (one comment sentence corrected, see README) |

## Unchanged

No public structural header and no structural test changed. The structural source change is confined to
`StructuralSolve.cpp` (the kernel body replaced by a wrapper) and one `PRIVATE_LINK` line. No existing
thermal header changed beyond the comment above.

## Handoff

`P18-TRANSIENT-001` receives the same `ThermalSystem` (assembled in transient mode, with `C`), the
same `ThermalSolverSettings`, the same reduction and the same kernel. It owns `C/dt + K`, the initial
condition, the time loop and the per-step acceptance. **It is not authorized and nothing in this
milestone starts it.**