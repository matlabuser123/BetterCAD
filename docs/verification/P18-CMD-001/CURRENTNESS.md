# P18-CMD-001 — result invalidation and the two currentness regimes

```text
THIS MILESTONE ADDS NO INVALIDATION MECHANISM. It proves the existing ones answer correctly to the commands.
No flag. No notification. No GUI-visible stale bit. No second opinion about staleness anywhere in
src/thermal/ThermalCommands.cpp (gate rule 6).
```

## How the tests ask

On a **real mesh with a real solve** (`vztest::VizStudy`: the steel block, a face flux, a volumetric source, a held temperature, a convecting face), the tests ask each qualified milestone's own function whether its
derived object still describes the document — `isCurrent(PreparedThermalLoads, …)`, `isCurrent(PreparedThermalBoundary, …)`, `isCurrent(ThermalSystem, …)`, `isCurrent(SteadyThermalSolution | TransientThermalSolution, …)`,
`isCurrent(ThermalPostResult, …)`, `isCurrent(ThermalBalanceResult, …)`, `isCurrent(ThermalValidationReport, …)` and the P16 mesh currency — **before** and **after** a command. Nothing consulted is something a command
invented, because a command invents nothing. The precondition is asserted first: every layer is current, so "no longer current" is a transition and not a standing state.

## The invalidation matrix (measured; not assumed)

`T` = still describes the document after the command, `F` = stale.

| Command (success) | P16 mesh | prepared loads | prepared boundary | system (K, C, Q) | solution | post (T, ∇T, q) | balance | validation report |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| add / edit / remove **load** | T | **F** | T | **F** | **F** | **F** | **F** | **F** |
| add / edit / remove **held temperature** | T | T | **F** | **T** | **F** | **F** | **F** | **F** |
| add / edit / remove **convection** | T | T | **F** | **F** | **F** | **F** | **F** | **F** |
| **solver settings** | T | T | T | T | **F** | **F** | **F** | **F** |
| **initial temperature** (transient) | T | T | T | T | **F** | **F** | **F** | **F** |
| **transient settings** (transient) | T | T | T | T | **F** | **F** | **F** | **F** |

Read as granularity — **a command stales exactly the layers whose recorded source contains what it changed**:

* A **held temperature** is a constraint, not part of K or Q: the assembled system survives it and the solution does not.
* A **convection** adds Robin terms to K and Q, so the system goes too.
* **Settings, the initial temperature and the time discretisation** are in no K, C or Q: only the result and what follows from it go.
* **The mesh never goes**, for any thermal command — a thermal edit is not a meshing question, and a remesh of a hundred thousand elements for a tolerance change would be wrong
  (`LoadEditDoesNotInvalidateMesh`, `BoundaryEditDoesNotInvalidateMesh`: the mesh stamp is the same generation and `mesher.mesh()` is still held).

Every row is a test section: `LoadMutationStalesResult` (add, edit by one part in a million, edit the face, remove), `BCMutationStalesResult` (seven sections), `SolverSettingsMutationStalesResult`,
`InitialConditionMutationStalesTransientResult`, `TransientSettingsMutationStalesTransientResult` (step, end time, output policy alone), `ATransientLoadEditStalesTheTransientResult`.

**A steady analysis has no initial-temperature or transient-settings row, because those commands are refused for it** — so there is no "unrelated steady result" for them to leave alone.

## What a refused, a no-op and a command that solves nothing do NOT do

* **A refused command leaves a current result current.** `FailedMutationPreservesCurrentResult` attempts one of every shape of refusal — a duplicate id, a NaN, a missing target, a missing removal, a negative coefficient, unusable
  settings, an initial temperature on a steady analysis, transient settings on a steady analysis — and then requires all eight layers `T`, the analysis revision and the document revision unchanged, the undo depth unchanged,
  the canonical fingerprint unchanged, and the viewer layer (which reads only the core) still `Current` / `Pass` / `Available`. An implementation that staled the result on *attempt* passes every row above and fails only this.
* **A no-op edit leaves a current result current.** `ANoOpEditDoesNotStaleACurrentResult`: the same load, the same settings, the same held temperature — no revision moves, all eight layers `T`.
* **No command solves, remeshes or recomputes.** `ACommandRunsNoSolveAndNoRemesh`: after three successful commands the mesh stamp is the same, the document has no new object, and the old post result is exactly what it was
  — stale, not replaced. Structurally, the gate forbids the command files from naming any solve/prepare/assemble/recover/balance/validate function and from including any header that declares one.
* **The viewer sees commands through the core.** `ViewerSeesCommandsThroughTheCore`: after a canonical edit the P18-VIZ layer — which this milestone does not touch — reports `Stale`, the badge `Stale`, the reasons
  `Loads` (and not `Mesh` or `Material`), and the fields unavailable; after a refused edit it reports `Current` / `Pass`.

## THE TWO CURRENTNESS REGIMES (the audit finding the brief's sections 77–81 and 227–228 required)

The qualified P18 code does not have one currentness rule. It has two, and an undo lands differently in each:

| Regime | Which objects | What they record | After an undo that restores the intent exactly |
| --- | --- | --- | --- |
| **BY VALUE** | prepared loads, prepared boundary, `ThermalSystem`, `SteadyThermalSolution`, `TransientThermalSolution`, `ThermalPostResult`, `ThermalBalanceResult` | the canonical **intent itself** (the loads, conditions, settings, initial temperature, transient settings) plus the mesh stamp, geometry revision and material revision | **current again**, if and only if the *whole* recorded source equals the source now |
| **BY REVISION** | `ThermalValidationReport` (and the `ThermalResult` container's `ThermalResultSource`) | the analysis **object's revision**, which is monotonic | **stale and stays stale**; a new validation is current |

`UndoCurrentnessFollowsSourceIdentity` records it on a real solve:

```text
before the edit      analysis revision r1      every layer current
after the edit       revision r2 > r1          loads, system, solution, post, balance, report: stale   (mesh and boundary current)
after the undo       revision r3 > r2, r3 != r1  CONTENT is back exactly; the revision is NOT
                       by value:     mesh, loads, boundary, system, solution, post, balance   CURRENT again
                       by revision:  validation report                                        STALE
```

This is **not value-based resurrection invented here**, and it is not a guess:

* the by-value regime is the one P18-DATA, P18-LOAD, P18-BC, P18-ASSEMBLY, P18-SOLVE, P18-TRANSIENT, P18-POST and P18-BALANCE qualified, and P18-VIZ-001's `StaleResultHasExplicitState` already asserts that "the edit undone"
  makes the result `Current` again;
* an old result is the answer to a **question**, and the question is its recorded source. When the source — mesh stamp, geometry revision, material revision and the whole canonical intent — is equal to what the
  document holds now, the old arrays are what a new solve would produce. The test does not assume it: **it solves again and requires the new post result to equal the old one**
  (`*fresh->post == solvedBefore`, every array, bit for bit);
* a **remesh**, a **material edit** or a **geometry edit** between the edit and the undo would change the mesh stamp, the material revision or the geometry revision, and the old result would stay stale — the
  value comparison is over the *whole* source, not over the thermal intent alone.

`ARevisionKeyedRecordNeverRevives` shows the other regime: the validation report goes stale on the edit, **stays stale across the undo** (its recorded `analysisRevision` is not the current one;
`staleReasons` names `Analysis`), and a fresh validation of the restored intent is current — the sequence P17-CMD-001 proved for its result.

`RedoCurrentnessFollowsSourceIdentity`: after the redo the edit stands again, so the result of the *original* source is stale again, by value, and a result computed for the edited intent is current under it.

### What this means for a consumer

A viewer that shows the post result and the validation badge will, after an undo, show a **current result with a stale badge** until the analysis is validated again (`ViewerSeesCommandsThroughTheCore`). That
is each regime telling the truth about what it recorded. Unifying them would change qualified P18 semantics, which the brief forbids doing casually; it is recorded as an observation for the owner in
[ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md) (O1), not changed here.

### Redo source identity

Redo is symmetric: it re-applies the stored after-definition, which moves the revision forward (`UndoMovesTheRevisionForwardRatherThanBack` asserts `revision after redo > revision after undo`). By value, the
result of the edited intent is current again if one was computed; by revision, nothing from before the edit can be.

## Remesh between a command and its undo

`HistorySurvivesARemesh`: edit a load, remesh to a new generation, then undo and redo. The canonical intent does not move under the remesh, undo and redo restore it, the command's two definitions are
untouched, and preparation re-resolves the same `FaceName` against the new mesh. A payload that held facets of the first mesh could not do this.
