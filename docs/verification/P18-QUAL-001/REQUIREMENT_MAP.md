# P18-QUAL-001 - requirement map

Every requirement of the qualification brief, the document that audits it, and what covers it. The row-level tables (with `file:line` evidence and the exact name of every covering test, 330 rows
in all) are the three audit files in `audit/`; this page is the index into them and records where a requirement was covered **only because this milestone added the test**.

Verdicts of the read-only audit (before any change): A 127 ADR clauses + 51 authority / currentness / DOF / units rows, B 71 rows, C 81 rows. **0 requirements without a test that fails if the property is
violated at the row level; 3 clause-level GAPs (all closed below); 31 PARTIAL rows (disposition below).**

| brief area | audited in | covered by | added or changed by this milestone |
|---|---|---|---|
| prerequisite, baseline, milestone and TODO audit | `MILESTONE_AUDIT.md`, `TODO_AUDIT.md` | 18 closed blocks, 0 open boxes, 0 failed stages | - |
| ADR-049 to ADR-057 | `ADR_AUDIT.md`, `audit/AUDIT_A` A1 | 127 clauses: 122 implemented, 5 documentation drifts, 0 violated | closed the GAPs 50.5 (valid-tree fixture), 51.2 (`P18Qualification_PossessionOfAThermalModelIsTheOnlyProof`) |
| authority model | `AUTHORITY_MODEL.md`, A2 | 15 of 16 COVERED; no second authority | `P18Qualification_RunningTheChainLeavesEveryReferenceDocumentByteIdentical` |
| currentness | `CURRENTNESS.md`, A3 | 11 of 15 COVERED; the two regimes recorded | `P18Qualification_EveryDependencyEditStalesExactlyTheObjectsItReaches`, `_TheTransientEditsAndTheCapacityStaleTheirOwnObjects`, `_AFailedRegenerationNeverYieldsACurrentResult`, `_AManualEditBackFollowsTheSameTwoRegimesAsUndo` |
| DOF model | `DOF_MODEL.md`, A4 | 10 of 10 COVERED | - |
| units | `ELEMENT_FORMULATION.md`, A5 | 8 of 10 COVERED | `P18Qualification_TheThermalTimeConstantIsATimeAndTheTransientSumIsDimensionallyLegal` (closes A5.5) |
| Tet4 formulation | `ELEMENT_FORMULATION.md`, B1-B5 | 26 of 26 COVERED | - |
| loads and boundary conditions | `LOADS_BCS.md`, B6-B7 | 16 of 18 COVERED (B6.6 NamedBoundarySet scope; B6.10 defensive branch) | - |
| assembly | `ASSEMBLY.md`, B8 | 4 of 5 COVERED (B8.4 dense guard is structural) | - |
| steady solve | `STEADY_SOLVER.md`, B9 | 8 of 8 after this milestone | `ThermalSolve_ADisconnectedMeshWithAnUnanchoredComponentFailsEndToEnd` (B9.5), `_AnUnmeetableToleranceRefusesTheSolveItself` (B9.8) |
| transient solve | `TRANSIENT_SOLVER.md`, B10 | 8 of 10 COVERED (B10.5 grid snap, B10.8 absolute residual) | - |
| tolerances | `TOLERANCES.md`, B11 | single owner each; 2 PARTIAL rows | `ThermalSolve_TheSolverSettingDefaultsAreTheKernelDefaults` |
| recovery | `POST.md`, C1 | 7 of 7 COVERED | - |
| heat balance | `BALANCE.md`, C2 | 9 of 10 COVERED (transient cancellation trap) | `ThermalValidation_AMeshAtTheEdgeOfTheAcceptedBoundsStillSolvesAndBalances` (D-E) |
| validation | `VALIDATION.md`, C3 | 10 of 10 after this milestone | `ThermalValidation_AMissingOrShortQualitySummaryIsNotAPass` (D-F, with a production fix) |
| visualisation | `VIZ.md`, C4 | 6 of 12 COVERED, 6 PARTIAL (caller-driven protocol) | the required `resultState` (D-B, with a production fix and a compile-fail case) |
| commands and history | `COMMANDS.md`, C5 | 3 of 5 COVERED | - |
| persistence | `PERSISTENCE.md`, C6 | 14 of 14 after this milestone | `ThermalPersistence_WhatTheWriterEmitsTheReaderAccepts` (D-A, with a production fix) |
| CLI | `CLI.md`, C7 | 13 of 23 COVERED, 10 PARTIAL | `ThermalCli_ValidationJsonCarriesAWarningReportWithEveryOptionalFieldItHolds`; `thermal-balance` added to the missing-material loop |
| architecture gates | `CLI.md`, C8 | 9 checkers, 188 fixture directories, 1:1 registered | the structural-includes-thermal direction added to the valid tree |
| reference models RM01-RM10 | `REFERENCE_MODELS.md` | 47 in-process cases + 17 fresh-process tests, per preset | re-run on all three presets |
| independent analytical validation | `ANALYTICAL_VALIDATION.md` | `tests/reference/ThermalAnalytic.hpp` + `verify-independent.ps1` | the PowerShell verifier over each preset's fresh CLI payloads, with a negative control |
| mutation testing | `MUTATION.md` | 28 production mutants, 27 killed, 1 documented survivor | - |
| adversarial review | `ADVERSARIAL_REVIEW.md` | an independent read-only reviewer over the integrated system | findings disposed there |
| freeze, fingerprints | `FREEZE.md` | 18 contract fingerprints, frozen twice, read back after the run | - |
| three-preset qualification, determinism, cross-preset, zero-match | `README.md`, `DETERMINISM.md`, `CROSS_PRESET.md`, `FINAL_GATE.md` | the harness, five repetitions in release and debug, the zero-match table | `postrun-qual.ps1` |
| fresh-binary proof | `FRESH_BINARY.md` | the workflows' identity record + the freshness check at the harness | `postrun-qual.ps1` (D-H) |

## Disposition of every PARTIAL row

Closed by a test or a fix in this milestone: B9.5, B9.8, B11.4 (settings defaults), A3.15 (manual edit-back), A5.5, C2 (mesh bounds against the balance), C3.7, C4.7a (the defaulted state), C6.1 (writer).
Closed at the harness: C7.5 (freshness). Recorded as limitations or owner decisions, with the reason, in `LIMITATIONS.md`: A2a.4, A3.2/A3.11/A3.12 (no per-field isolation test for POST and BALANCE sources;
the equality of the embedded system source is tested), B6.6, B6.10, B8.4, B10.5, B10.8, B11.3, C2.7 (transient), C4.1b, C4.2, C4.3, C4.5b, C4.7b, C4.7c, C5.1, C5.5, C7.1d, C7.1e, C7.2b, C7.3a, C7.3b, C7.3c, C7.4b, C7.7a, C7.7b.
