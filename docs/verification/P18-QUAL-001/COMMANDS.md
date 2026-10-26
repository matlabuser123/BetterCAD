# P18-QUAL-001 - commands and history audit (brief sections 59-62)

**Verdict: ten commands edit one analysis definition; history holds two canonical definitions and nothing else; failure is atomic; undo and redo assign, never invert.**
Row-by-row: `audit/AUDIT_C_post_balance_validation_viz_commands_persistence_cli.md` section C5 (5 rows: 3 COVERED, 2 PARTIAL) and ADR-055 in `audit/AUDIT_A_*` (15 clauses, all COVERED).

| property | where | tests |
|---|---|---|
| ten commands over one base: create (wrapping `AddObjectCommand`), add / edit / remove a load, add / edit / remove a boundary condition, set the initial temperature, the solver settings, the transient settings | `ThermalCommands.hpp` | `ThermalCommand_CreateAnalysis*`, the `...UndoRedo` family (one per command family) |
| history holds canonical intent only: two `ThermalAnalysisDefinition` records, no mesh-local id, no derived object | the payload of each edit is `before_` and `after_` | `ThermalCommand_EveryEditCommandPayloadIsTwoCanonicalDefinitions` (static_asserts on the accessor types; hand-built expected before/after for 9 edit classes), 52 compile-fail cases in group `thermalcmd` (the DOF map, the model, the report and the result container are not reachable; ids of other kinds cannot stand in), `architecture.thermalcmd-ownership` (8 rules, 14 negative fixtures) |
| targets are stable canonical ids found by identity, never position | | `ThermalCommand_LoadIdPreserved`, `_BoundaryConditionIdPreserved`, `_EditMissingStableIdFails`, `_AnEditOfARemovedLoadDoesNotAliasTheNextOne`, `_AFaceIsTheOnlyTargetAThermalRecordTakes` |
| undo and redo ASSIGN the stored definition; no inverse arithmetic; no number is formatted and parsed | `apply(*before_)`, `apply(*after_)` | `ThermalCommand_NoValueIsRoundTrippedThroughText` (0.1 + 0.2, the smallest subnormal), `_RepeatedUndoRedoDoesNotDrift` (50 cycles), `_HeterogeneousFullUndoReturnsBaseline` (a bit fingerprint at every step, with a non-vacuity gate) |
| redo of a create restores the **same** `ObjectId` | `AddObjectCommand::redo` re-inserts a clone that carries the id | `ThermalCommand_CreateAnalysisRedoPreservesId` (five cycles) |
| failure is atomic: no change to the document, the fingerprint, the revision, the undo stack, the redo stack or any result's currentness | the edit runs on a copy; `modifyObject` validates before assigning; the history pushes and clears redo only after success | `ThermalCommand_FailedLoadCommandsAreAtomic` (9 sections), `_FailedBoundaryCommandsAreAtomic` (10), `_CreateAnalysisFailsAtomically`, `_ADefinitionTheDocumentRefusesLeavesTheCommandUnexecuted`, `_FailedMutationPreservesCurrentResult` (eight refusal shapes on a solved steady bed: all seven derived layers stay current); mutation QP20 (a refused command reported as executed) killed |
| a valid new command clears redo; a failed one preserves it | `CommandHistory` (one shared line) | `ThermalCommand_SuccessfulBranchClearsRedo`, `_FailedBranchDoesNotClearRedo`, `_BranchingHistoryHasNoGhostEdits` |
| the mode is respected, not switched | | `ThermalCommand_InitialTemperatureIsRefusedForASteadyAnalysis`, `_TransientSettingsAreRefusedForASteadyAnalysis` |
| a no-op edit succeeds, moves no revision, stales nothing, and is still recorded; a signed zero is a no-op | | `ThermalCommand_NoOpEditSucceedsAndMovesNothing`, `_ASignedZeroEditIsANoOpAndTheCommandRecordsWhatTheDocumentHolds`, `_ANoOpEditDoesNotStaleACurrentResult` |
| commands run no solve, no remesh, no assembly, no recovery, no balance, no validation; no invalidation mechanism was added | gate rules 1 and 2 | `ThermalCommand_ACommandRunsNoSolveAndNoRemesh`, `_NoCommandStalesAnUnrelatedLayer`, `architecture.thermalcmd-ownership.includes-solver`, `.solves` |
| an invalid feature handle is refused at the command boundary; a valid handle naming a missing feature is accepted (a load may precede any mesh) | `ThermalCommands.cpp:66-86` | `ThermalCommand_ATargetThatNamesNoFeatureIsRefused`, `_ALoadMayBeDefinedBeforeAnyMeshExists` |
| the undo of an edit moves the revision **forward**; a revision-keyed record never revives | `Document::modifyObject` | `ThermalCommand_UndoMovesTheRevisionForwardRatherThanBack`, `_ARevisionKeyedRecordNeverRevives`, `_UndoCurrentnessFollowsSourceIdentity`; see `CURRENTNESS.md` for the two regimes and the final-gate sequence |

## The two PARTIAL rows

* **C5.1 / C5.5**, "a hidden member": the gates forbid *named* derived types in the command classes; an anonymous member such as `std::vector<double> cache_` would pass
  every gate, and the runtime tests read only the command's published `before()` / `after()` copies. Recorded; the cheapest closure (a `sizeof`-bounded static_assert per edit
  class) is a candidate for the next milestone that touches the commands. The size-independence evidence stands on `ThermalCommand_HistorySizeIndependentOfThermalResultSize` and the payload test.
* **Failed-redo-preservation** is exercised for the load commands and the document-refused path; the clear lives in one shared line of `CommandHistory`.

No partial-mutation window exists in the command code (the audit read `execute`, `apply`, `undo`, `redo` and the history end to end).
