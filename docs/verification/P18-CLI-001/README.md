# P18-CLI-001 — Headless Thermal Analysis

**PASSED 2026-10-26.** Qualified against `2ffba66` plus the working tree (committed: see `TODO.md`): **5725/5725 tests in each of three presets**, 2483 × 5 repeats in two, **0 warnings over 732 objects per preset**, 17 stages exit 0, 6 h 38 min 21 s, the source fingerprints at the freeze == before == after, first attempt.

The command line can inspect, edit, solve, summarise, balance and validate a thermal analysis without a display, and every number it prints is the number the qualified thermal core produced. It is an **adapter**: it contains no Tet4 matrix, no load or boundary integration, no assembly, no solver, no integration in time, no recovery, no balance, no validation policy, no `GeometryReference` resolution, no currentness test, no persistence format and no material property.

## What was built

| | |
|---|---|
| **One entry in the thermal core** (ADR-057) | `thermal::runThermalAnalysis(document, regenerator, mesher, analysis) -> ThermalAnalysisRun`: `include/bettercad/thermal/ThermalAnalysisRun.hpp`, `src/thermal/ThermalAnalysisRun.cpp`. It calls each qualified link once, in order, and hands the validator everything derived; it never meshes, never writes, does no arithmetic. **The core had no production orchestrator before this** (only the viewer's test-support `derive`), so a headless CLI could not exist without one — reported as a finding, additive, and held to the CLI's own standard by the gate. |
| **Seven reports** | `thermal-settings`, `thermal-validate`, `thermal-solve`, `thermal-temperature`, `thermal-gradient`, `thermal-heat-flux`, `thermal-balance` (`apps/bettercad_cli/ThermalReports.cpp`) — each with `--json`. |
| **Five edits** | `thermal-analysis-add`, `thermal-load-add`, `thermal-load-remove`, `thermal-boundary-add`, `thermal-boundary-remove` (`ThermalEdits.cpp`) — registered in the edit registry, so `batch` can script them; each is a load-edit-save transaction that writes nothing if the core refuses. |
| **A finite-checking JSON writer** | `ThermalOutput.hpp`: a non-finite number is written as `null` and the payload says so (`valueFinite: false`); the extremum tie-break (lowest id) lives here and is unit-tested on hand-built input. |
| **An example document** | `examples/models/thermal_bare_plate.bcad`: the plate with its material and mesh control and no analysis. |

## Result

| Gate | Result |
|---|---|
| Tests, three presets (debug-ext / release-ext / debug-shared-ext) | **5725/5725 each** (5504 before this milestone + 221 new) |
| Repeats (`--repeat until-fail:5`, 2483 tests) | release-ext and debug-ext **100%** |
| Warnings, errors | **0 / 0** over 732 objects per preset, `-Werror` |
| New tests | `ThermalCli_*` **85** cases (5399 assertions), `ThermalRun_*` **20** (614), `cli.thermal.*` **12** process tests, ownership gate **104** (102 violating fixtures) |
| Scripted workflow | **74 real processes** per preset, every exit code asserted, every payload parsed twice (CMake `string(JSON)` and the strict RFC 8259 reader), the fresh-binary ledger written |
| Mutation campaign | **45 probes, 45 killed** (22 by tests, 16 by tests and gate, 7 by the gate alone) — `MUTATIONS.md` |
| Independent adversarial review | 0 critical, 14 findings (3 HIGH), 11 changed code/tests/gate, 3 recorded — `ADVERSARIAL_REVIEW.md` |
| Cross-preset | same exit code for all 74 processes; 27 payloads: same keys, same text, **bit-identical numbers**; 11 documents: same SHA-256 — `DETERMINISM.md` |
| Freeze | scratch tree `c0f4abc50a8ea8598dd6bc34684e79ecf4231c78`; four CLI fingerprints equal before and after — `FREEZE.md` |
| Nothing qualified changed | 509 of 520 `src`/`include`/`apps` files byte-identical; 6 modified (registration), 5 new; **no existing thermal file touched** |

## Where each of the 27 items is shown

| # | Item | Evidence |
|---|---|---|
| 1 | Inspect thermal analysis | `thermal-settings`; `ThermalCli_InspectAnalysis*`, `…ShowsAnUnresolvedReferenceAndInventsNoSubstitute`, `…TwoAnalysesAreAmbiguous…` · `COMMANDS.md` |
| 2 | Add / remove loads | `ThermalCli_AddSurfaceHeatFlux`, `…AddVolumetricGeneration`, `…RemoveLoad`, `…LoadIdsAreTheUsersAndNeverAContainerIndex`, `…AddLoadCoreEquivalence` |
| 3 | Add / remove BCs | `ThermalCli_AddPrescribedTemperature`, `…AddConvection`, `…RemoveBoundaryCondition`, `…ConflictingPrescribedTemperaturesAreRefusedNotOrdered` |
| 4 | Steady solve | `ThermalCli_SteadySolve`, `…SteadyRobinOnly`, `…FullyConstrainedSteady`; workflow steps 8–10 |
| 5 | Transient solve | `ThermalCli_TransientSolve`, `…UsesActualStoredTimes`, `…FinalPartialStep`, `…FinalOnly…`; workflow transient block |
| 6 | Temperature summary | `ThermalCli_TemperatureSummary`, `…TemperatureExtremaTieBreak…` |
| 7 | Gradient summary | `ThermalCli_GradientSummary` |
| 8 | Heat-flux summary | `ThermalCli_HeatFluxSummary` |
| 9 | Heat-balance report | `ThermalCli_HeatBalanceReport`, `…TransientIsPerStep`, `…ARejectedBalanceIsStillPrinted…`; the workflow's closed-form check (generated heat = 1e5 W/m³ × the mesh volume reported by `mesh-info`, eleven digits) |
| 10 | Validation report | `ThermalCli_ValidationReport`, `…WarningOnlyReportExitsZero…` |
| 11 | Structured diagnostics | `STRUCTURED_OUTPUT.md`; every refusal is `<program> <command>: <code>: <message>` with a stable code |
| 12 | Correct process exit codes | `EXIT_CODES.md`; `ThermalCli_*ExitCode*`, `…NoFlagBypassesValidation`; workflow asserts all three codes |
| 13 | Stale-geometry failure | core: `ThermalRun_ARunOfAnEditedGeometryIsStale…`; CLI analogue `regeneration_failed` · `FAILURE_PATHS.md` |
| 14 | Stale-mesh failure | core: `ThermalRun_ARemeshWithTheSameCountsAndIds…`, `…AnEditAfterARunMakesItStale…`; CLI analogue `mesh_generation_failed` · `FAILURE_PATHS.md` |
| 15 | Missing-material failure | `ThermalCli_MissingMaterialFails`, `…TransientNeedsDensityAndSpecificHeat`; workflow steps 14, `k-only` |
| 16 | Singular-model failure | `ThermalCli_SingularSteadyFails` (`no_temperature_datum`); workflow step 13 |
| 17 | Invalid load / BC failure | `ThermalCli_InvalidLoadFails*`, `…InvalidBoundaryConditionFails*`, `…NoFlagBypassesValidation` |
| 18 | CLI / core equivalence | `CORE_EQUIVALENCE.md`; 7 `…CoreEquivalence` tests, bit-for-bit; edits compared as canonical file bytes |
| 19 | Multi-process save / load / re-solve | `MULTI_PROCESS.md`; workflow steps 2–12, byte-identical payloads across processes |
| 20 | No CLI-only thermal implementation | `CLI_ARCHITECTURE.md`, `SOURCE_AUDIT.md`, the 104-test ownership gate, `MUTATIONS.md` |
| 21 | Fresh-binary proof | `FRESH_BINARY.md`; `ledger.txt` of each preset (path, size, SHA-256, runtime closure) |
| 22 | Raw structured stdout clean | `STDOUT_CLEANLINESS.md`; five independent layers |
| 23 | Zero-match protection | `tests/cli/ZeroMatchGuard.cmake` (minimum counts per group); workflow step count asserted (74); `ctest -N -R` counts in the postrun summary |
| 24 | End-to-end scripted workflow | `tests/cli/ThermalWorkflow.cmake` · `qualification/workflow-<preset>/` |
| 25 | Adversarial review | `ADVERSARIAL_REVIEW.md` |
| 26 | Regression | `qualification/` — 5725/5725 × 3 presets, repeats, `0 warnings` |
| 27 | Evidence recorded | this directory |

## Files

| File | |
|---|---|
| `SCOPE.md` | what is in, what is deliberately not, and the **owner decisions** this milestone measured and did not change |
| `AUDIT.md` | the conventions of the existing CLIs and what the core did and did not offer |
| `COMMANDS.md`, `STRUCTURED_OUTPUT.md`, `EXIT_CODES.md`, `FAILURE_PATHS.md` | the contract |
| `CLI_ARCHITECTURE.md`, `SOURCE_AUDIT.md` | the boundary and the proof that it holds |
| `CORE_EQUIVALENCE.md`, `MULTI_PROCESS.md`, `DETERMINISM.md`, `FRESH_BINARY.md`, `STDOUT_CLEANLINESS.md` | the five behavioural claims |
| `ADVERSARIAL_REVIEW.md`, `MUTATIONS.md` | the attack on it |
| `FREEZE.md` | the tree and the fingerprints |
| `qualification/` | the harness, every log, the freeze files, `source-audit.ps1/.txt`, the mutation campaign (`mutations/`) and each preset's workflow ledger and payloads (`workflow-<preset>/`) |
| `../../architecture/decisions/ADR-057-…` | the one core addition and why it is there |

## Findings for the owner (measured here, not changed)

`SCOPE.md` §"Decisions the owner may want to take" lists seven; the three that matter most:

1. **`relative_residual_tolerance` has no upper bound.** A hand-edited `1e300` loads, and then every analysis is accepted. The acceptance threshold is user-editable canonical data.
2. **A `final_only` transient of more than one step can never be accepted**, because the balance of a backward-Euler step needs the states on both sides. The CLI requires `--output` explicitly rather than choosing for the user.
3. **A 23 750-step transient is refused** (`balance_out_of_tolerance` at state 812) at a valid, fine time step.

Two more were found by the adversarial review and recorded rather than changed because they live in shared infrastructure that P13–P17 qualify: a load or save failure inside the shared edit driver carries no machine-readable code for any edit family, and the "Assembly edits" help heading now lists thermal edits.
