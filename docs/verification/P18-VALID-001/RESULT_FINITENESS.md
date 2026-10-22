# P18-VALID-001 — result finiteness and cardinality

```text
RULES:    TV_RESULT_FINITE (V11), TV_POST (V12), and the finiteness parts of TV_BALANCE (V13) and TV_SOLVER_RESIDUAL (V10)
SUBJECT:  every number a result holds is finite before any statistic, comparison or derived metric uses it; every array is the size its contract says
ORDER:    primary arrays first (temperatures), then the derived fields (gradient, flux, magnitude), then the accounting (reactions, balance)
```

## What is checked, and by which rule

| Result class | Required finite | Size | Rule | Code | Test |
| --- | --- | --- | --- | --- | --- |
| nodal temperature, every stored state (steady: 1; transient: every sample, plus the initial state of a single-sample run) | finite AND not below 0 K (`isAbsoluteTemperature`, the one policy) | `Ndof` per state | `TV_RESULT_FINITE` | `temperature_not_finite` (`cause = non_finite` / `below_absolute_zero`), `result_size_mismatch` | `RejectsNonFiniteTemperature`, `RejectsATemperatureBelowAbsoluteZero`, `RejectsAStateOfTheWrongSizeBeforeReadingIt`, `EveryNonFiniteStateOfAHistoryIsFoundNotOnlyTheLast` |
| temperature gradient `grad(T)` (K/m), per tetrahedron | each component | `Ntet` | `TV_POST` | `post_field_not_finite` | `RejectsNonFiniteGradient` |
| heat flux `q` (W/m²), per tetrahedron | each component | `Ntet` | `TV_POST` | `post_field_not_finite` | `RejectsNonFiniteHeatFlux` |
| heat-flux magnitude `|q|` | the stored value | `Ntet` | `TV_POST` | `post_field_not_finite` | `RejectsNonFiniteHeatFlux` |
| recovered temperatures | equal to the solver's, bit for bit | `Ndof` | `TV_POST` | `post_temperature_mismatch` | `RejectsRecoveredTemperaturesThatAreNotTheSolversBitForBit` |
| number of recovered states | = the solution's | — | `TV_POST` | `post_cardinality_mismatch` | `RejectsARecoveredResultWithTheWrongNumberOfStates` |
| reaction heat per prescribed unknown; per-load and per-condition totals; category totals; storage energy and rate; the scale; the residual | all of them | one reaction per unknown, one entry per prepared load and condition | `TV_BALANCE` | `balance_not_finite`, `balance_attribution_mismatch` | `RejectsNonFiniteBalance` (eight corruptions) |
| the residual metrics of the solve (steady normalised residual, per-step reduced and physical residual, pivot ratio) | all of them, BEFORE any threshold comparison | one record per step | `TV_SOLVER_RESIDUAL` | `residual_not_finite`, `pivot_below_floor` | `ANotANumberResidualMetricIsAnErrorNotAPass`, `ANotANumberTransientRecordIsAnErrorNotAPass` |

## The rules that make it trustworthy

* **A missing element result is fatal, never skipped.** A gradient array of 1 against 2 tetrahedra is `post_cardinality_mismatch` (a magnitude array one short, a temperature array
  one short likewise); the cardinality is judged FIRST and a finiteness scan of a short array is never reported as if it covered the mesh (`RejectsAMissingElementResultInsteadOfSkippingIt`).
* **NaN never reaches a comparison.** Every finiteness test precedes the threshold test it guards. A NaN residual, a NaN balance term, a NaN mesh metric (`ANotANumberMetricIsInvalid…`
  for all four metrics and NaN, +inf, −inf) and a NaN pivot ratio are each an ERROR, not a pass and not a warning by comparison fall-through. (Mutations VAL04, VAL40 and VAL42, which let
  one fall through, are killed.)
* **No statistic over a number that is not a number.** The informational minimum and maximum temperature are formed only when `TV_RESULT_FINITE` passed
  (`ANotANumberTemperatureFailsTheFinitenessGateAndTheResidual` asserts both are absent), and the informational maximum heat flux only when `TV_POST` passed
  (`ANotANumberHeatFluxFailsThePostGate…`).
* **The temperature being finite does not excuse the flux.** A NaN in one flux component with finite temperatures and a closing balance fails `TV_POST` alone, and the solve is still
  reported as accepted — the full analysis is not.
* **A transient is finite in EVERY stored state.** The scan covers all samples (and the resolved initial state when only the last is stored), not "only the final state": the test places
  a NaN in the middle and a value below 0 K at the end of a four-state history and gets both findings with their sample index and time.
* **There is no temperature-range gate.** A high but finite temperature is not rejected, and a large finite heat flux is not rejected: P18 has no physical range in its material model and
  no design requirement in its inputs, so inventing `T < 2000 K` or an allowable flux would be an invented number. The minimum, maximum and peak flux are INFORMATION in the report metrics
  (`minimumTemperature`, `maximumTemperature`, `maximumHeatFlux`), never a gate.

## Order in the report

`TV_SOLVER_RESIDUAL` (which itself checks the metric is finite first) → `TV_RESULT_FINITE` → `TV_POST` → `TV_BALANCE`. The primary arrays are judged before anything derived, so the min/max,
the norms and the balance never operate on a NaN and then emit a misleading downstream diagnostic. If the solve rule finds a recorded failure or no solution, the three rules after it are
SKIPPED with `blockedBy = TV_SOLVER_RESIDUAL`.
