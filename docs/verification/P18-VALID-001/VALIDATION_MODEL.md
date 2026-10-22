# P18-VALID-001 — the validation model

```text
SUBJECT:  one authoritative, deterministic, read-only decision on whether a P18 thermal analysis is ACCEPTED, built from the qualified
          evidence of every earlier milestone and adding no formula, no tolerance and no solve of its own
ENTRY:    validateThermalAnalysis(document, regenerator, mesher, analysis, ThermalDerivedState) -> ThermalValidationReport
RESULT:   PASS | PASS_WITH_WARNINGS | FAIL, derived from the ordered checks and never stored
```

## Why a solver's "Success" is not acceptance

Every earlier P18 milestone decides one thing about its own output: the assembler that its matrices are consistent, the solver that its free residual is within
the tolerance the user set, the recovery that its fields are finite, the balance that its books close. None of them decides whether an analysis may be believed,
because a result can be algebraically perfect and be about a mesh the user has since replaced, about a material the user has since edited, about a model that was
never well posed, or on a mesh whose elements the gradient cannot be recovered on. This milestone is the single owner of that decision. The solver, the recovery,
the balance, a GUI and a CLI consume it; none keeps a threshold, a severity or a pass/fail of its own (the ownership gate, GUI_OWNERSHIP.md, fails any that does).

## The rules, in the order they run

The enumeration order IS the execution order and the report order. Every report carries every rule, in this order.

| # | Brief | Rule | Stable id | Group | Applies to | What it decides | Takes the facts from |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 0 | V00 | `AnalysisDefinition` | `TV_ANALYSIS_DEFINITION` | input | both | the analysis exists | `findThermalAnalysis` |
| 1 | V01 | `GeometryCurrent` | `TV_GEOMETRY_CURRENT` | input | both | the control exists; the body is regenerated, current, a solid, valid, not behind a configuration override | `requireThermalModel` (P18-ARCH-001), P16 `geometryIneligibility` |
| 2 | V02 | `MeshCurrent` | `TV_MESH_CURRENT` | input | both | a mesh is held for the control, describes the model, and its last generation did not fail | `requireThermalModel`, P16 `Mesher::currency` |
| 3 | V03 | `MeshStructural` | `TV_MESH_STRUCTURAL` | input | both | no inverted, degenerate, repeated-handle or non-finite element; every metric computable | P16 `MeshQualityReport::structural`, `invalidElements` |
| 4 | V04 | `MeshQuality` | `TV_MESH_QUALITY` | input | both | the thermal accuracy bounds (fatal) and the qualified envelope (warning) | P16 `summaries`, this milestone's thresholds |
| 5 | V05 | `Material` | `TV_MATERIAL` | input | both, by mode | steady: `k`; transient: `k`, `rho`, `cp` | `thermalMaterialProblem` (P18-MAT-001) over P15's requirement table |
| 6 | V06 | `Loads` | `TV_LOADS` | input | both | every configured load is finite, supported, resolved against the current mesh | `prepareThermalLoads`, `thermalLoadProblem` (P18-LOAD-001) |
| 7 | V07 | `BoundaryConditions` | `TV_BCS` | input | both | every condition is finite, supported, resolved; no conflicting prescription | `thermalBoundaryFailure` (P18-BC-001) |
| — | — | `TransientSetup` | `TV_TRANSIENT_SETUP` | input | **transient** (NOT_APPLICABLE for steady) | initial temperature, time settings, grid within its resource guard, history within its storage guard | `isAbsoluteTemperature`, `validate(TransientSettings)`, `TimeGrid::create` |
| 8 | V08 | `SourceCompatibility` | `TV_SOURCE_COMPAT` | system | both | every derived object describes THIS document and one another | `requireCurrent` of each milestone, by value; `postSourceOf` |
| 9 | V09 | `WellPosed` | `TV_WELL_POSED` | system | both, by mode | steady: every region anchored; transient: finite positive capacity | `analyseAnchoring` (P18-SOLVE-001); the capacity matrix (P18-ASSEMBLY-001) |
| 10 | V10 | `SolverResidual` | `TV_SOLVER_RESIDUAL` | solve | both | the independent free residual, every step, against the canonical tolerance; the pivot floor; the history contract | `validateSteadySolution`, `validateTransientStep` |
| 11 | V11 | `ResultFinite` | `TV_RESULT_FINITE` | result | both | every nodal temperature of every stored state is finite and not below 0 K | `isAbsoluteTemperature` (P18-DATA-001) |
| 12 | V12 | `PostResult` | `TV_POST` | result | both | one finite gradient, flux and magnitude per tetrahedron, the solver's temperatures bit for bit | P18-POST-001's result |
| 13 | V13 | `HeatBalance` | `TV_BALANCE` | balance | both | every state finite and within the balance milestone's policy; the attribution complete | `balanceWithinTolerance` (P18-BALANCE-001) |
| 14 | V14 | final acceptance | — | — | — | **derived**: `ThermalValidationReport::status()`, never a rule and never stored | the checks |

## The failure policy, frozen once

* **Input rules are judged independently.** A missing material does not hide a stale mesh, and a stale mesh does not hide a missing material. Each rule runs when its
  own prerequisite exists: the material needs a body (a control that names one), the mesh checks need a current mesh, the loads and conditions need a current model
  (which is a current mesh plus a usable conductivity).
* **A rule whose prerequisite is missing or invalid is SKIPPED, with the rule that blocked it, and a skipped rule is NEVER a pass.** `status()` counts a skipped rule
  as a failure; `blockedBy` always names an EARLIER rule.
* **The System, Solve, Result and Balance groups run only when every Input rule passed**, because a stale mesh makes a solve's residual meaningless and a report that
  said otherwise would be a cascade of consequences burying the cause. The System group is itself ordered: a source-compatibility failure blocks well-posedness, and a
  well-posedness failure blocks the solve.
* **The Solve, Result and Balance rules are independent of one another.** A residual that fails does not hide a balance that fails, and a balance that passes does not
  rescue a residual that fails. The two exceptions are honest ones: a recorded failure of the last solve attempt, or the absence of a solution, stops the three rules after
  it (there is nothing for them to judge).
* **Warnings never mask a fatal condition.** A warning has its own code, its own severity (a function of the code) and its own status; nothing in the report turns one
  into the other.

## Solve accepted is not the full analysis accepted

`ThermalValidationReport` exposes three distinct questions and does not blur them:

| Question | True when |
| --- | --- |
| `inputsAccepted()` | every Input-group rule passed (the model is fit to solve) |
| `solveAccepted()` | the rules up to and including `TV_RESULT_FINITE` passed: inputs, system, residual, temperatures |
| `accepted()` | `status() != FAIL`: **every** mandatory rule for the mode passed, the recovered fields and the heat balance included |

Full P18 acceptance requires the recovered fields (P18-POST-001) and the heat balance (P18-BALANCE-001): the result of a P18 analysis includes the gradient and the flux,
and a balance that does not close is a model that does not account for its heat. A transient with `FinalOnly` output and more than one step stores no intermediate state
and cannot be balanced step by step (P18-BALANCE-001 refuses it), so it can be SOLVE-accepted and cannot be fully accepted; the report says so and says how to fix it
(`EveryStep`).

## Ownership

| Question | Owner | Evidence |
| --- | --- | --- |
| mesh metrics (computation) | P16 (`evaluateMeshQuality`) | no metric formula in `ThermalValidation.cpp`; the ownership gate forbids `evaluateTetQuality`/`evaluateMeshQuality` there |
| thermal mesh acceptance (thresholds) | **P18-VALID** | `thermalQualityThresholds()`, MESH_ACCEPTANCE.md |
| the linear solve, the free residual tolerance | P18-SOLVE / canonical `ThermalSolverSettings` | the validator calls `validateSteadySolution` with the canonical settings |
| the per-step residual | P18-TRANSIENT | `validateTransientStep` |
| the balance metric and its tolerance policy | P18-BALANCE | `balanceWithinTolerance` |
| full P18 acceptance | **P18-VALID** | `validateThermalAnalysis`; `architecture.validation-ownership` |

P16 was NOT changed: no meshing file is touched by this milestone (FREEZE.md compares them by blob). P16 reports metrics and states no opinion; a mesh that P18-VALID
refuses is, to P16 under its own default, structurally valid and satisfying its policy (test `RejectionByThermalPolicyDoesNotFailP16GlobalMeshQuality`).

## What the validator never does

It never solves, assembles, recovers, balances, regenerates, remeshes, repairs, takes an absolute value, picks a nearest face, supplies a material property, loosens a
tolerance, or runs a formula another milestone owns. Every argument is a `const` reference or a pointer to const; the report is built in a local value and returned only
when complete (atomic); the document, the mesh, the assembled system, the solution, the recovered fields and the balance are byte-for-byte what they were
(`ValidationIsReadOnly`). It has no timestamp, no pointer identity, no unordered container and no random number (source audit; the ownership gate fails the first three).

## The algebra the decision is taken on

The result rules are decided by `judgeSolveEvidence(ThermalSolveEvidence&, ThermalValidationReport&)` from plain views of data, because a typed result cannot be
corrupted — that is what makes it evidence. The model-level function builds the evidence from the typed objects and calls the same function; the tests change only the
data (a perturbed free temperature, a NaN in a flux, a balance edited in place, a history one state short). Nothing in production is weakened to make an invalid state
constructible.
