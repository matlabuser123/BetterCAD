# ADR-055 — Thermal commands edit one analysis definition, and the history holds intent only

```text
STATUS:    Accepted
DATE:      2026-10-23
MILESTONE: P18-CMD-001
TOUCHES:   ADR-047 (the structural command layer this one follows), ADR-030 (derived state carries its source), ADR-032 (the load identity),
           ADR-049 (the analysis modes and the uniform initial temperature), ADR-053 (the validation report's source),
           ADR-054 (the view that reads the same currentness), ADR-036 (possession is the proof)
```

## Context

By the end of P18-VIZ-001 a thermal analysis is a document object holding a `ThermalAnalysisDefinition` — a mesh-control handle, a mode, the heat loads, the boundary conditions, at most one uniform
initial temperature, two solver ratios and at most one set of transient settings — and a chain of derived objects (a numbering, prepared loads and conditions, a system, a solution, a recovered field,
a balance, a validation report), each carrying what it was computed from. Nothing edits the definition with undo. A user who changes a flux, a held temperature or a time step needs that edit to be
one undoable step that restores the previous intent exactly, and needs the results computed from the previous intent to stop describing the model — without the history ever holding a matrix or a
temperature.

P17-CMD-001 solved the same problem for the structural kernel and recorded the method. P18 inherits the method. It does not inherit the types, and the thermal data model differs in three ways that matter
here: its `FaceName` is the only canonical target of a load or condition; its initial temperature and its transient settings exist only in a transient analysis (the definition is *mode-complete*); and
its derived objects compare their recorded canonical intent **by value** where P17's result compared a monotonic revision.

## Constraints

* One command framework exists (`Command`, `CommandHistory`, `AddObjectCommand`, `Document::modifyObject`). It already guarantees what this milestone needs: a failed `execute` leaves the document unchanged,
  is not recorded and does not clear the redo stack; `redo` of a created object restores the same `ObjectId`; `ThermalAnalysis::setDefinition` validates before assigning and returns false, moving no revision,
  for an effective no-op.
* A command must not name, hold or reach a derived object. The thermal module also contains the solver, so isolation must be enforced rather than assumed.
* Floating-point values are canonical data. An undo that inverts or re-parses them would drift.
* Nothing in a payload may be a mesh-local handle: a remesh must leave every payload valid.
* The checklist names ten commands. It names no whole-definition edit, no deletion and no mode switch.

## Options

### A — a command per field, undone by inverting the field

Rejected: nine copies of the lines where an atomicity bug hides, inversion invites floating-point drift (`-= delta`), and every new definition field is a thing to forget.

### B — snapshot the document, or the analysis with its results, before each edit

Rejected: it would put the derived chain into the history. A single flux edit would copy a mesh, a numbering, K, Q, T and the recovered fields, and undo would restore an old result as if it were authority.

### C — one base that edits one definition, carrying a before and an after of that definition

Chosen. The payload of every edit is two canonical definitions, whose size is the number of loads and conditions and is independent of the mesh and of any result.

## Decision

**Option C**, with these clauses.

1. **Ten commands, one base.** `CreateThermalAnalysisCommand` wraps `AddObjectCommand`. `AddThermalLoadCommand`, `EditThermalLoadCommand`, `RemoveThermalLoadCommand`, the three boundary-condition counterparts,
   `SetInitialTemperatureCommand`, `SetThermalSolverSettingsCommand` and `SetTransientSettingsCommand` derive from `ThermalAnalysisEditCommand`, which captures the before-state, applies the after-state
   through `Document::modifyObject`, and records both only after a successful apply.
2. **Targets are stable canonical identities.** A command is keyed on `ThermalAnalysisId` and addresses a record by `ThermalLoadId` or `ThermalBoundaryConditionId`, found by identity and never by position.
   A record's target is a `FaceName` as given; there is no resolved set, no nearest-face fallback, no mesh handle.
3. **Undo and redo assign.** Neither computes an inverse and neither formats or parses a number. Repeating the pair cannot drift.
4. **Edits replace a whole validated record, in place.** An edit keeps the record's id and its position; a multi-field edit changes all its fields or none; an edit may change the kind of a load or a
   condition because the payload is a validated whole. An add appends, because the order is the user's and is canonical state.
5. **Failure is atomic by construction.** A refused command moves no field, no revision, no history entry and no redo entry, and stales no result.
6. **The mode is respected, not switched.** Setting an initial temperature or transient settings on a steady analysis is refused with a clear diagnostic, because the data model refuses the inert field.
   Switching mode is one whole-definition edit that the checklist does not ask for, and none is added.
7. **No-ops follow the project.** An edit that changes nothing succeeds, moves no revision, invalidates nothing and is still recorded — the behaviour of P15, P16 and P17.
8. **No invalidation mechanism is added.** A command changes the definition and the analysis object's revision; every derived object answers through its own existing comparison. The commands run no solve,
   remesh, assembly, recovery, balance or validation. The two currentness regimes that already exist — by value for the derived chain, by revision for the validation report — are recorded and left as they are.
9. **Enforced, not described.** The command header makes none of a DOF index, a DOF map, the assembled system, a solution, a post result, a balance, a validation report, a prepared load or boundary, the result
   container or the model *nameable* (measured by preprocessing, then probed by compile-fail cases); a node, an element or another thermal id cannot stand in for an analysis, a load or a condition; a quantity is
   a typed quantity; and an architecture gate fails any command file that includes a derived header, names derived work or a mesh identity, addresses a record by position, parses a number, keeps its own stale
   flag or bypasses `modifyObject`/`setDefinition`/`validate`, and fails any application or renderer file that edits a thermal analysis around the commands.

## Consequences

* After an undo, the older derived objects of the by-value regime (prepared objects, system, solution, post, balance) describe the document again when — and only when — their whole recorded source equals
  the current one; they are not guessed: a fresh solve of that source gives the same arrays bit for bit. The validation report does not revive, because it records the analysis revision. A consumer that
  shows both (the P18-VIZ badge) therefore shows a current result with a stale badge until the analysis is validated again. Unifying the two regimes would change qualified P18 semantics and is not done here.
* `validate(ThermalLoad)` and `validate(ThermalBoundaryCondition)` accept a `FaceName` whose feature is the invalid default id. That qualified data-model behaviour is left untouched, but a command
  refuses such a target at its own boundary (add, edit, and inside a create): an invalid handle is not a representation of a target, whereas a valid handle that names no existing feature is accepted and
  reported unresolved when the load or condition is prepared, because a load may be defined before any mesh exists.
* Undo and redo check the stack discipline instead of assuming it: if the analysis is no longer the definition the command installed (or found), the undo (redo) refuses rather than silently discarding an
  out-of-band edit. The mode is checked before the value for the two mode-dependent settings, and a no-op records what the document holds (signed zero).
* The Qt application has no thermal panel and no command wiring; a panel that edits an analysis must construct these commands, and the gate fails one that edits around them.
* Persistence of the history is not implemented (P18-PERSIST-001). The payload types are canonical, copyable data with no pointer and no callback, so they are serialisable as they stand.
* A change to a command's payload, to the canonical/derived boundary, to the id semantics or to the failure behaviour changes the contract fingerprints in `FREEZE.md` and is a new milestone.
