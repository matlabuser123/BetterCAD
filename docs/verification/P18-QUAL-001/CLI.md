# P18-QUAL-001 - CLI and architecture-gate audit (brief sections 67-75)

**Verdict: the CLI is an adapter. It owns no formulation, no tolerance, no default and no verdict; its exit status is the core's `accepted()`.**
Row-by-row: `audit/AUDIT_C_post_balance_validation_viz_commands_persistence_cli.md` section C7 (23 rows: 13 COVERED, 10 PARTIAL, 0 GAP), section C8 (nine architecture checkers, 188 fixture directories, 1:1 registered)
and ADR-057 in `audit/AUDIT_A_*` (10 clauses, all COVERED).

| property | tests |
|---|---|
| ONE production entry: `runThermalAnalysis(document, regenerator, mesher, analysis)`; every report calls it and nothing else of the chain | `architecture.thermalcli-ownership` (21 numbered rules; 102 negative fixtures: `includes-*`, `names-solve` / `-assemble` / `-validate` / `-gradient` / `-time-grid`, `run-meshes`, `run-writes`, `run-edits-definition`, `verdict-lost` / `-extra` / `-return-weakened`, `header-solver-success` / `-validator-only` / `-missing`, ...) |
| the CLI's numbers are the core's, **bit for bit** | `ThermalCli_SteadyCoreEquivalence` and `_TransientCoreEquivalence` (min / max temperature and node, max gradient, max flux, Dirichlet reaction, worst balance residual, validation status, node count: `std::bit_cast<uint64>` against a core run on the same file), `_AddLoadCoreEquivalence` and its four siblings (the saved `.bcad` bytes equal the bytes of the core command plus `io::saveDocument`) |
| the CLI's numbers are the physics' (a real executable against closed forms and a dense backward-Euler oracle, tolerance 1e-9 for the text round trip) | `ThermalRefmodCli_SteadySlabAgreesWithFourier`, `_ConvectionAgreesWithSeriesResistance`, `_TransientAgreesWithBackwardEuler`, `_SurfaceFluxAndTheHeldCapTransientAgree`, `_EditedModelGivesTheNewAnswer` (run by the `refmod.thermal.workflow` process test) |
| exit status: 0 success, 1 failure (document, regeneration, mesh, refusal, not accepted), 2 a command-line fault; `thermal-validate` and `thermal-balance` print the payload **and** exit 1 | `ThermalCli_SuccessExitCode`, `_AnalysisFailureExitCode`, `_ARejectedBalanceIsStillPrintedAndTheExitStatusSaysNo`, `_ACommandLineFaultHasOneExitCodeWhateverTheDocumentHolds`; mutation QP22 (the CLI exits 0 whatever the verdict) killed by 8 tests |
| stdout is exactly one payload: strict RFC 8259, no log, no progress, no ANSI, no mesher chatter | `ThermalCli_StructuredStdoutIsClean`, `_ThirdPartyOutputDoesNotPolluteStdout` (the Netgen local-size warning path is provoked and the captured stream stays empty), `ThermalCliWorkflow_EveryPayloadIsStrictJson` (raw bytes of every real-process payload; at least 25 parsed, at least 10 empty), `ThermalRefmodCli_EveryPayloadIsStrictJson` (exactly 32 parsed, 25 empty) |
| a non-finite number is never written as a number | `ThermalCli_NonFiniteNumbersAreNeverWrittenAsNumbers`, `_ValidationJsonWritesANonFiniteMeasurementAsNullAndSaysSo` |
| the validator's report is written whole and in the validator's order, with **every optional field** | `ThermalCli_ValidationReport`, `ThermalCli_ValidationJsonCarriesAWarningReportWithEveryOptionalFieldItHolds` (added by this milestone: a hand-built `PASS_WITH_WARNINGS` report through the production writer - subject, metric, element, step, time, step size, value, threshold, count, unit, cause, message - and the status, acceptance and counts) |
| refusals carry the validator's code and a clean stdout | `ThermalCli_MissingMaterialFails` (now over **all five** result commands including `thermal-balance`), `_SingularSteadyFails`, `_InvalidLoadFailsAtomically`, `_InvalidBoundaryConditionFailsAtomically`, `_ConflictingPrescribedTemperaturesAreRefusedNotOrdered` |
| no flag bypasses validation and the CLI supplies no default | `ThermalCli_NoFlagBypassesValidation` (nine flags exit 2; the help text is scanned), `_AnalysisModeCompletenessIsTheCoresAndNeverGuessed` (twelve incomplete combinations exit 1 and change no byte) |
| genuinely separate OS processes: a result is never persisted, so a fresh process re-runs the analysis and gets the same bytes | `cli.thermal.workflow` (74 asserted steps), `refmod.thermal.workflow` (58 asserted steps), `ThermalCli_SaveLoadResolveAgreesAcrossFreshRuns` |

## Fresh-binary proof (D-H)

The two workflow scripts record the absolute path, the size, the SHA-256 and the resolved runtime libraries of the binary under test, and fail on a relative path, a binary
outside the build tree, an unresolved or foreign library, a GUI dependency and a shared/static mismatch. They do **not** compare the hash with an expected value or the binary with the
sources, and the header concedes that freshness is the harness's: a clean, a rebuild, a no-op rebuild. This milestone adds the missing half **at the harness**
(`qualification/postrun-qual.ps1`): for each preset the binaries under test must be newer than the clean of that preset's stage and newer than the newest file of the frozen source
tree, their size and SHA-256 are recorded, and the no-op rebuild log must contain "no work to do" (`FRESH_BINARY.md`). It is deliberately not a fatal check inside the CMake scripts: a file-system
timestamp touched by a synchronising folder (the tree lives in OneDrive) would fail a six-hour run on a fact that proves nothing about the binary.

## PARTIAL rows, and their disposition

| row | disposition |
|---|---|
| C7.1d no arithmetic on a quantity in the CLI: rule 13 sees `.si()` next to an operator, not `+=`, not raw-double accessors, not `std::accumulate` | the current CLI is clean (read end to end, 945 lines); the gate is not tight. Recorded (D-I); the cheapest closure is five more regexes and fixtures. |
| C7.1e logic hidden in a non-`Thermal*` CLI file is scanned by no CLI rule | currently clean (only the registry and the help mention thermal). Recorded. |
| C7.2b transient equivalence does not cover the heat-flux payload or the per-step attribution | the shared writer paths are covered by the steady counterparts; recorded. |
| C7.3a / C7.3b a true stale geometry or mesh cannot reach a one-shot process (every command regenerates and remeshes); the tests prove the **analogues** and "no memory across processes" | by design. The `dependency_cycle` and `regeneration_blocked` branches of `prepare()` (`ThermalReports.cpp:115-133`) have no test: a document that cycles or blocks cannot be built through the public API. Recorded. |
| C7.4b NaN in `threshold`, `time`, `stepSize` and the `metrics.*` doubles; the `nonfinite_value` refusal branch | the branch is unreachable from a real document (the core refuses before any NaN exists) and is pinned by the arity of `sawNonFinite()` (rule 12). The field coverage is now in the hand-built report test. |
| C7.7a, C7.7b the CLI's exit through `PASS_WITH_WARNINGS` | covered at the payload by the hand-built report test; a real document cannot produce a severity-warning finding, so the exit statement (pinned by rule 12 to be exactly `run.accepted() ? Success : Failure`) is the guarantee. |

## Architecture gates (C8)

Nine checkers relevant to thermal (`layering`, `targets`, `validation-ownership`, `thermalviz-ownership`, `thermalcmd-ownership`, `thermalpersist-ownership`, `thermalcli-ownership`,
`recovery-ownership`, `balance-ownership`) with 188 fixture directories (9 positive, 179 negative), each registered once. The gates are regex scans over text with `//` comments removed
(D6): a banned token placed after a string literal that contains `//`, or inside a `/* */` comment, escapes them. They are the sole enforcement of ADR-053 through ADR-057 ownership, so this is the
largest residual weakness of the whole phase, recorded for the owner (`LIMITATIONS.md`). Missing negative fixtures: structural-includes-thermal (allowed by design: the reason for layer 45),
`CheckTargetDependencies` rules 2 and 3.
