# P18-CMD-001 — atomicity and redo invalidation

```text
A REFUSED COMMAND CHANGES NOTHING: no field, no revision, no history entry, no redo entry, no stale result.
```

## Atomic by construction, not by care

```text
execute():
    find the analysis                       -> refuse if absent (NotFound)
    capture `before` from the object
    edit(before)                            -> the command's own preconditions, IN THIS ORDER: wrong mode (steady has no
                                               initial temperature or transient settings), then the record's own validator
                                               (finite, absolute, positive, selector), then the named-feature rule, then
                                               duplicate id / missing id
    stage before_, after_                   -> BEFORE the document is touched, so a failed allocation cannot leave a
                                               half-recorded command behind a changed document
    apply() -> Document::modifyObject -> ThermalAnalysis::setDefinition -> validate(definition)
    refused?  reset before_, after_         -> the command is exactly as unexecuted as it was
    ok, no change?  after_ = before         -> what the document holds (signed zero)
    ok        record changed_
```

* **Nothing is written before the last check.** `edit` works on a *copy* of the definition and returns it; `setDefinition` validates the whole definition before assigning; only a successful apply keeps the
  command's state. A multi-field change therefore lands entirely or not at all — there is no sequence "assign dt, then find t_end invalid".
* **A refused edit leaves the command unexecuted.** `before_` is reset, so `CommandHistory` — which pushes only after `execute()` succeeds — records nothing, and clears redo only on that same path;
  `ARefusedCommandIsLeftUnexecuted` requires the refused command's own undo and redo to say "not executed".
* **The mode is checked before the value.** A steady analysis given an invalid initial temperature is refused for its *mode* (`FailedPrecondition`), not for the value, so the diagnostic names the real cause
  and a caller cannot be told to fix a number that would then be refused anyway. `RefusalsCarryStableCodes` pins the code of each refusal.
* **Both halves of a pair are tested.** `FailedBoundaryCommandsAreAtomic` edits a convection with a *valid* coefficient and an *invalid* ambient and requires the coefficient not to land;
  `InvalidSolverSettingsAtomic` pairs a valid residual tolerance with a NaN pivot floor; `InvalidTransientSettingsAtomic` pairs a valid step with a negative end time.

## What is checked, and what each refusal looks like

| Refusal | Code | Test |
| --- | --- | --- |
| the analysis does not exist, or the id names an object of another kind (found by a `dynamic_cast`, not trusted) | `NotFound` | `ACommandCannotReachAnObjectOfAnotherKind`, `FailedLoadCommandsAreAtomic` |
| a duplicate load or condition id | `AlreadyExists` | `FailedLoadCommandsAreAtomic`, `FailedBoundaryCommandsAreAtomic` |
| edit / remove of a missing id | `NotFound` | `EditMissingStableIdFails`, `AnEditOfARemovedLoadDoesNotAliasTheNextOne` |
| NaN / ±∞ flux or generation | `InvalidArgument` | `FailedLoadCommandsAreAtomic` |
| a temperature that is NaN, ∞ or below 0 K (no Celsius is accepted or converted) | `InvalidArgument` | `FailedBoundaryCommandsAreAtomic`, `InvalidInitialTemperatureAtomic` |
| a convection coefficient that is zero, negative or NaN (never `abs()`'d) | `InvalidArgument` | `FailedBoundaryCommandsAreAtomic` |
| solver ratios that are NaN, zero, negative or infinite | `InvalidArgument` | `InvalidSolverSettingsAtomic` |
| transient settings with `dt` ≤ 0, NaN, `dt` > span, non-finite or negative end | `InvalidArgument` | `InvalidTransientSettingsAtomic` |
| an initial temperature or transient settings on a steady analysis | `FailedPrecondition` | the two `…RefusedForASteadyAnalysis` cases |
| a malformed face selector (a side face with no entity) | `InvalidArgument` | `FailedLoadCommandsAreAtomic`, `FailedBoundaryCommandsAreAtomic` |
| a load or condition whose `FaceName` has the invalid feature handle (add, edit, or inside a create) | `InvalidArgument` | `ATargetThatNamesNoFeatureIsRefused` |
| undo / redo when the analysis was changed outside the history | `FailedPrecondition` | `UndoRefusesWhenTheAnalysisWasChangedOutsideTheHistory` |
| undo / redo when the analysis is gone | `NotFound` | `UndoAndRedoRefuseWhenTheAnalysisIsGone` |
| a create with an invalid definition, a bad name or a taken name | `InvalidArgument` / `AlreadyExists` | `CreateAnalysisFailsAtomically` |

A **negative** flux or generation is *not* refused: it is a sink, and the data model accepts it (`LoadSignedMagnitudePreserved`). 0 K is accepted as the limit of the scale (the data model's policy, reused).

## Every counter, before and after

`Snapshot` (`ThermalCommandTestSupport.hpp`) holds the canonical fingerprint, the analysis revision, the document revision, the object count, the undo depth and the redo depth. Every atomicity case
requires `snapshotOf(part) == before` after the refusal. `FailedCommandDoesNotPushHistory` and `FailedCommandDoesNotChangeSource` isolate the two halves; `FailedMutationPreservesCurrentResult` adds the derived
layers ([CURRENTNESS.md](CURRENTNESS.md)).

## Redo invalidation

| Sequence | undo depth | redo depth | Test |
| --- | --- | --- | --- |
| execute A, execute B | 2 | 0 | |
| undo B | 1 | **1** | `UndoMakesRedoAvailable` |
| **successful** C | 2 | **0** — B cannot come back; the document is exactly A + C | `SuccessfulBranchClearsRedo` |
| (same setup) **failed** C | 1 | **1** — B is still there to redo, and redoing it works | `FailedBranchDoesNotClearRedo` |
| execute A, undo, redo, redo | | the second redo is refused; nothing further to redo | `RedoRedoLeavesNothingToRedo` |
| A, B, C, undo C, undo B, execute D | | redo 2 → **0**; no ghost B or C, state is exactly A + D | `BranchingHistoryHasNoGhostEdits` |

A failed command after an undo is the case an over-eager implementation gets wrong: clearing redo on *attempt* would lose a perfectly good redo to a typo. `FailedBranchDoesNotClearRedo` requires the redo
of B to still work and the document to be "A only" in between.

## The no-op policy

`NoOpEditSucceedsAndMovesNothing`: an edit that assigns a load its existing value **succeeds**, `changedAnything()` is false, **neither the analysis revision nor the document revision moves**, the canonical fingerprint
is unchanged, and the command **is recorded** (undo depth + 1) — the behaviour of P15's material edit, P16's mesh-control edit and P17's analysis edit. The brief prefers "no history entry" if the framework
permits; the framework records every command whose `execute()` succeeded, and special-casing one module would make three modules disagree. The cost is one history entry; the benefit is that a no-op stales nothing
([CURRENTNESS.md](CURRENTNESS.md), `ANoOpEditDoesNotStaleACurrentResult`).

## Undo and redo themselves

Undo and redo assign a stored, already-validated definition through the same `apply`. They refuse — with the document and both stacks unchanged, `CommandHistory` leaving the command where it was — if the analysis
has been deleted, or if its definition is no longer the one the command expects ([UNDO_REDO.md](UNDO_REDO.md)). A recorded no-op moves nothing on undo or redo either (`AnUndoOrRedoOfANoOpMovesNothing`).
