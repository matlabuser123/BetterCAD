# P18-CMD-001 — adversarial review

```text
TWO REVIEWS:  the author's own pass (questions below), then an INDEPENDENT read-only review by a separate agent that was given the code and not the conclusions.
The independent review found 7 defects, 7 weak tests, 6 gate gaps and 10 unpinned behaviours. Every one is listed with its disposition.
No atomicity hole and no wrong error code on an exercised path was found; the real defects were one validation hole, one equality quirk and one missing undo guard.
```

## Part 1 — the independent review, finding by finding

| # | Finding (severity as reported) | Disposition | Where it is pinned |
| --- | --- | --- | --- |
| D1 | **High** — a `FaceName{}` (invalid feature handle) passes `validate(ThermalLoad / ThermalBoundaryCondition)`, so a command records a target that names nothing | **FIXED at the command boundary** (add, edit, and inside a create's definition): `InvalidArgument`. The data model is untouched (O2). A *valid* handle naming a feature that does not exist stays accepted | `ATargetThatNamesNoFeatureIsRefused`; mutations CMD32–CMD38 |
| D2 | **Medium** — `+0.0 → −0.0` is a no-op to `setDefinition` (defaulted `==`), but `after_` held `−0.0`, so `after()` disagreed with the document | **FIXED**: a no-op records `after_ = before` | `ASignedZeroEditIsANoOpAndTheCommandRecordsWhatTheDocumentHolds`; CMD15 |
| D3 | **Medium** — undo/redo assign the stored whole definition with no precondition, so an out-of-history edit is silently discarded | **FIXED**: undo refuses unless the analysis equals `*after_`, redo unless it equals `*before_` (`FailedPrecondition`, nothing moves) | `UndoRefusesWhenTheAnalysisWasChangedOutsideTheHistory`; CMD17, CMD18 |
| D4 | **Low** — `before_` was copied *after* `apply` had changed the document; a `bad_alloc` there leaves a changed document and an unexecuted command | **FIXED**: both definitions are staged before `apply` and reset if it is refused | `ARefusedCommandIsLeftUnexecuted`; CMD14 |
| D5 | **Low-medium** — `Create` accepts a dangling or wrong-kind mesh-control handle | **ACCEPTED, recorded (O3) and pinned.** `validate(definition)` deliberately consults no document; the mesh handle is a dependency that `Document::dependencies()` reports as missing. P17-CMD-001 behaves identically. A resolving create would be a new policy, not a fix | `CreateKeepsTheMeshHandleAsGiven` |
| D6 | **Low** — the order of checks decided the code: a steady analysis given a NaN initial temperature said `InvalidArgument`, given a good one `FailedPrecondition` | **FIXED** for the two mode-dependent settings: the mode is checked first. (An add that is both invalid and a duplicate reports the invalid record first; that is the order of the checks, stated, not a defect.) | `RefusalsCarryStableCodes`; CMD30, CMD31 |
| D7 | **Low** — `validate(TransientSettings)` has no upper bound on the step count (`dt = 1e-300`, `end = 1e300` is canonical intent) | **RECORDED (O4), not changed.** Bounding a step count is a solver-resource question and belongs to P18-TRANSIENT, which carries a resource guard before it allocates; the command layer stores intent and does not duplicate that policy. Not claimed to be enforced here | — |

### The tests the review said did not assert what they claimed

| Test | What was wrong | Disposition |
| --- | --- | --- |
| `CreateAnalysisCreatesIntentOnly` | a tautological `static_assert`; compared type names, which cannot see an extra mesh-control or feature object; no object count | **REWRITTEN**: object count rises by exactly one, exactly one new id, it is the analysis' id, and it has the analysis' type name |
| `TransientSettingsDoNotPersistDerivedTimeGrid` | both `static_assert` disjuncts were 24 bytes — trivially true | **REPLACED** by a real comparison of the stored settings against the three given values |
| `NoCommandStalesAnUnrelatedLayer` | compares the test's own constants with each other | **KEPT, and labelled honestly** as a sanity check on the *shape* of the matrix (four rows must differ; the mesh survives all of them). It is not evidence; the evidence is the measured matrix tests that solve for real |
| `ACommandRunsNoSolveAndNoRemesh` | the held derived objects belong to the test, so a command that solved would still pass | **KEPT, comment corrected**: what it can observe is that the mesh stamp did not move, that no object appeared and that the old result is byte-for-byte what it was. "Runs no solve" is enforced by the *types* (compile-fail: no solver name is reachable from the command header) and by gate rules 1–2, and the review's point stands that this is by name |
| `HistorySizeIndependentOfThermalResultSize` | compares definitions, not the command's storage | **ACCEPTED as stated** in [HISTORY_MEMORY.md](HISTORY_MEMORY.md): the claim is "the payload *is* these two definitions", which the type of `before()/after()` and the schema fingerprint (`P18_CMD_CANONICAL_HISTORY_SCHEMA_FINGERPRINT`, every data member of every command) state; no allocation counter is used |
| atomic tests | `run` destroys a failed command, so `before()/after()/changedAnything()` were never observed after a failure | **ADDED** `ARefusedCommandIsLeftUnexecuted`: the failed command is kept; its `before()/after()` are empty, and its own undo/redo say "not executed" |
| `DescriptionsAreStable` | covered 3 of 10 | **EXTENDED to all ten** |

### The gate gaps (`CheckThermalCommandOwnership.cmake`)

| Gap | Disposition |
| --- | --- |
| rule 8 scanned only `apps/`, `src/renderer`, `include/renderer` | **WIDENED** to `apps/`, every file under `src/`, and `include/bettercad/renderer/` — for any file that mentions `ThermalAnalysis`. Fixtures `thermalcmd-io-edits` (an edit from `src/io`) and `thermalcmd-qualified-edit` (a fully qualified `modifyObject`) prove it |
| rule 8's regex missed `bettercad::thermal::ThermalAnalysis` | **WIDENED** to any qualification |
| rule 1 inspected only `<bettercad/...>` includes; a quoted or relative include bypassed it | **FIXED**: a quoted include is itself forbidden in a command file (`thermalcmd-quoted-include`) |
| rule 7 is a substring-presence check | **LIMITATION, recorded.** It states that the three owners are named; the behaviour is held by the runtime tests (CMD mutations that bypass them are killed) |
| rule 4 is a text pattern list; an alias (`auto& l = after.loads; l[0]`) slips past | **LIMITATION, recorded.** The behavioural tests (`AnEditOfARemovedLoadDoesNotAliasTheNextOne`, CMD12) are the second line |
| the comment stripper truncates after `//` inside a string literal | **LIMITATION, recorded.** The command files contain no `//` in a string; the audit script (`source-audit.ps1`) strips the same way and prints the counts |

### Behaviours the review found unpinned, now pinned

undo/redo after the analysis is gone (`UndoAndRedoRefuseWhenTheAnalysisIsGone`); after an out-of-history edit; create → dependent commands → undo everything → redo everything
(`CreateThenDependentCommandsUndoRedoEverything`); create with a transient definition, a bad name, a taken name; a recorded no-op's undo/redo (`AnUndoOrRedoOfANoOpMovesNothing`); the code of every refusal
(`RefusalsCarryStableCodes`); undo/redo currentness on a **transient** analysis (the currentness suite runs both beds); a failed undo/redo leaving both stacks as they were; a null feature, a signed zero and the
check order (above).

### Found afterwards by the mutation campaign (not by the review)

| # | Finding | Disposition |
| --- | --- | --- |
| M1 | CMD14 and CMD21 survived: the branch where the *document* refuses a definition that the command's own checks accepted was never exercised, because no real edit can reach it | **TEST ADDED** (`ADefinitionTheDocumentRefusesLeavesTheCommandUnexecuted`, a test-only command derived from the edit base); both mutants killed — [MUTATIONS.md](MUTATIONS.md) |
| M2 | GATE13 survived: the widened rule 8 (any qualification of `ThermalAnalysis`) was not pinned by a fixture | **FIXTURE ADDED** (`thermalcmd-qualified-edit`); the gate is now 16 tests |

## Part 2 — observations that are not defects

* **O1 — two currentness regimes** (the milestone's main finding). The derived chain compares its recorded intent *by value*; the validation report compares the analysis *revision*. Because revisions are
  monotonic, an undo that restores identical intent makes an old post/solution/balance current again (and a fresh solve gives bit-identical arrays — tested) while the validation report stays stale. Not changed;
  [CURRENTNESS.md](CURRENTNESS.md).
* **O2 — the data model accepts `FaceName{}`.** P18-DATA's qualified `validate` checks the selector, not the feature handle. Untouched; the command layer refuses it at its own boundary (D1).
* **O3 — a create accepts a dangling mesh handle.** Pinned, not changed (D5).
* **O4 — no step-count bound in `validate(TransientSettings)`.** Recorded (D7).
* **O5 — no Qt panel.** The application has no thermal panel and no command wiring; this milestone is the command layer only.

## Part 3 — the author's questions, answered

| Question | Answer |
| --- | --- |
| Does a command hold anything derived? | No. Members are listed by the schema fingerprint: a name, definitions, one record or id, one settings value. Compile-fail: thirteen derived names are not even declared where a command is |
| Could a node/element/DOF index be passed as a target? | No: 10 + 4 conversion cases refuse it; a record has no node form |
| What is the undo payload? | The previous whole `ThermalAnalysisDefinition`, assigned, never inverted |
| Is any value passed through text? | No. Gate rule 5 forbids parsing; `NoValueIsRoundTrippedThroughText` uses `0.1 + 0.2` and the smallest subnormal and compares bits |
| Does undo restore the revision? | No (monotonic): content exact, identity forward (`UndoMovesTheRevisionForwardRatherThanBack`) |
| Does redo keep ids? | Yes: create keeps the `ThermalAnalysisId` over five rounds; load/condition ids over three |
| Is an edit found by position? | No: by stable id (`AnEditOfARemovedLoadDoesNotAliasTheNextOne`, CMD12) |
| Is order preserved? | Add appends; edit replaces in place; remove's undo returns the record to its position (middle-removal tests, CMD01–CMD04, CMD10–CMD11) |
| Does a failed command change anything? | Field, revisions, both stacks, objects and the currentness of every layer are compared before/after (`Snapshot`, `FailedMutationPreservesCurrentResult`) |
| Does a failed command after an undo lose the redo? | No (`FailedBranchDoesNotClearRedo`); a successful one clears it (`SuccessfulBranchClearsRedo`) |
| What does a no-op do? | Succeeds, moves no revision, is recorded; undo/redo of it moves nothing |
| Is the GeometryReference ever resolved? | Never: stored as given, eight selector forms round-trip including copies in order and an unresolved one |
| Does a command solve or remesh? | It cannot (types) and does not (mesh stamp, object count, result unchanged) |
| What does an edit invalidate? | Measured on real solves: load → loads/system/solution/post/balance/report; held T → boundary/solution/post/balance/report; convection also the system; settings → solution/post/balance/report; **never the mesh** |
| Is the history size mesh-dependent? | No: same payload fingerprints on a coarse and a >4× larger mesh |
| Is anything nondeterministic? | No clock, hash order, pointer or random source in the command files (`source-audit.ps1`: all zero); `CMDFP journey` is equal across presets |
| What if the analysis is edited behind the history? | Undo/redo refuse and change nothing (D3) |
| Can a steady analysis take transient settings? | No: refused with a diagnostic naming the mode |
| Can two conditions conflict on one face? | The command layer accepts it as intent and invents no policy; P18-BC/P18-VALID report it (`ACommandPermitsAConflictThatPreparationReports`) |
| Is there a mode-switch / delete / whole-definition command? | No — not on the checklist and the brief says not to invent one |
| Does the Qt app use these? | No panel exists; a future panel must construct these commands and the gate fails one that edits around them |
