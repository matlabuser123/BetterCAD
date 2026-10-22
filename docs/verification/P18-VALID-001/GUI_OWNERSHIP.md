# P18-VALID-001 — who owns the acceptance decision, and the gate that keeps it so

```text
GATE:     tests/architecture/CheckValidationOwnership.cmake      (real tree: 497 files outside the thermal module scanned, 0 violations)
FIXTURES: tests/architecture/fixtures/validation-{valid,gui-decides,reruns,second-tolerance,lacks-owners,second-owner,unordered}   (7 trees, 8 tests with the real one)
```

## The rule

Whether a thermal analysis is ACCEPTED is decided in ONE place — `validateThermalAnalysis` in `src/thermal/ThermalValidation.cpp`. The solver, the recovery, the balance, a viewer,
a command and a report CONSUME its verdict. None keeps a threshold, a severity or a pass/fail of its own, because a second acceptance policy is a second answer that drifts from the
first, and the one that drifts towards "accepted" ships a wrong temperature. Severity is owned by core validation (`severityOf(code)`); the GUI cannot convert a warning into an
error or the reverse, and there is no field for it to do so.

## Six rules, on the source text with `//` comments removed

| # | Rule | Scope | What fails it |
| --- | --- | --- | --- |
| 1 | No file outside the thermal module names a function or constant that DECIDES acceptance | `apps/`, `src/` and `include/` outside `thermal`, `examples/` | `checkSteadyResidual`, `checkTransientResiduals`, `checkHeatBalanceStates`, `checkPostStates`, `checkTemperatureStates`, the two well-posedness checks, `checkTransientSetup`, `classifyThermalMesh`, `thermalQualityThresholds`, `kThermalRadiusRatioFloor`, `kThermalAspectRatioCeiling`, `kThermalGradientAccuracy`, `balanceWithinTolerance`, `validateSteadySolution`, `validateTransientStep`, `analyseAnchoring` |
| 2 | The validator judges what exists | `ThermalValidation.cpp`, `.hpp` | any of: an element/facet/load integral, a Jacobian, a gradient, a recovery, a balance, a solve, an assembly, a mesh metric (`evaluateTetQuality`, `evaluateMeshQuality`), `generate(`, `regenerateAll`, `assignMaterial`, `setMaterial` |
| 3 | The validator adds no tolerance | `ThermalValidation.cpp` | a numeric literal in `1e-9 .. 1e-16` |
| 4 | The validator reads what the others published | `ThermalValidation.cpp` | not naming `validateSteadySolution`, `validateTransientStep`, `balanceWithinTolerance`, `analyseAnchoring`, `requireCurrent`, `requireThermalModel`, `staleReasons`, `summaries`, `direction` |
| 5 | The validator is deterministic | `ThermalValidation.cpp`, `.hpp` | `unordered_`, `std::hash`, `chrono`, `clock`, `rand(` |
| 6 | One acceptance API in the thermal module | the other thermal files | `isAccepted`, `acceptThermal*`, `ThermalAcceptance`, `overallStatus` |

The valid fixture also holds comments that name forbidden things and a viewer that calls the core's entry point and reads a report member (`accepted()`); none is flagged. Each defective
fixture holds exactly one defect: a viewer that decides (`checkSteadyResidual`), a validator that solves (`solveSteadyThermal`), a validator with its own tolerance (`1e-9`), a validator that
forgot two owners (`balanceWithinTolerance`, `validateTransientStep`), a second acceptance API (`isAccepted` in `ThermalPost.cpp`), and an unordered container.

## Ownership statements (the evidence the brief asks for)

```text
P16 computes mesh metrics:                         YES    (src/meshing/MeshQuality.cpp; the validator reads summaries and calls direction(metric))
P16 owns thermal acceptance:                        NO     (reportOnlyThresholds() classifies nothing; P16 was not changed)
P18-VALID owns thermal mesh acceptance:             YES    (thermalQualityThresholds(), derived in MESH_ACCEPTANCE.md)
P18-SOLVE owns the linear solve:                    YES
P18-SOLVE / the canonical settings own the residual tolerance:   YES    (ThermalSolverSettings.relativeResidualTolerance, consumed, not copied)
P18-BALANCE owns the balance metrics and their tolerance:        YES    (balanceWithinTolerance, consumed, not copied)
P18-VALID owns final P18 acceptance:                YES    (validateThermalAnalysis; architecture.validation-ownership)
```

## Three tolerances, three owners, no conflict

The brief asks that a tolerance does not exist independently in SOLVE, BALANCE and VALID with conflicting values. There are three numeric policies and each has exactly one owner:

| Policy | Owner | Value | How VALID uses it |
| --- | --- | --- | --- |
| steady / transient free-residual tolerance and pivot floor | canonical `ThermalSolverSettings` (user intent, persisted, undone) | `1e-9`, `1e-12` by default | passes the analysis's own settings to the qualified functions; a loosened or tightened setting changes the verdict AND the solution's source |
| heat-balance residual | P18-BALANCE-001 | `1e-12 W + 1e-13 (gross + arithmetic)` | calls `balanceWithinTolerance`; no copy of either constant exists in the validator (gate rule 3) |
| thermal mesh acceptance | **P18-VALID-001** | `3r/R ≥ 7.0e-6`, `aspect ≤ 700`, envelope warnings | the owner; P16 only measures |

The mesh thresholds are compared against dimensionless P16 metrics and never against a solver residual (the two live in different headers and no expression combines them).

## What a consumer does

A viewer or a CLI calls `validateThermalAnalysis(...)` (or reads a report), reads `status()`, `accepted()`, `solveAccepted()`, the findings and the metrics, and prints them. It does not
re-derive a status, re-order findings, re-classify a severity or recompute a residual. `ThermalValidationReport` is a plain value with stable rule ids (`TV_*`) and stable code names so a
later CLI can print and a script can compare them; this milestone implements neither the CLI nor the GUI.
