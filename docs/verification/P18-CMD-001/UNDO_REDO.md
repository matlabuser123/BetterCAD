# P18-CMD-001 — exact undo and exact redo

```text
"EXACT" IS A STATEMENT ABOUT CANONICAL CONTENT, NOT ABOUT IDENTITY.
The two are tested separately, and in OPPOSITE directions.
```

## What "exact" means here

BetterCAD's revisions are **monotonic**: an edit moves the analysis object's revision, and an undo restores the content and moves the revision **again** rather than rolling it back. So there are two claims:

```text
CONTENT   undo restores the canonical intent EXACTLY — ids, order, values bit for bit, FaceNames
          -> tested by an independent projection and a bit-exact fingerprint, below
IDENTITY  undo does NOT restore the old revision
          -> tested in the opposite direction (UndoMovesTheRevisionForwardRatherThanBack)
```

## The independent oracles

Written in `tests/thermal/ThermalCommandTestSupport.hpp`, by hand, so that they **name every field they check**:

* `CanonicalIntent` — a struct of the control handle, mode, loads, conditions, initial condition, solver settings and transient settings; no revision, no mesh, no derived state.
* `fingerprintOf(definition)` — FNV-1a over exact bit patterns in a fixed order: every load (id, kind, face with every selector member, value bits), every condition (id, kind, face, value bits), the optional initial
  temperature, the two ratios and the optional transient settings. No pointer, no timestamp, no iteration order, no revision. A field added to the definition falls outside it until the oracle is extended, and
  extending it is where the new field's authority gets stated (the mirror assertion in `ThermalDataTests` fails the build first).

Neither is obtained by asking a command, `operator==` or an undo helper for the expected state: the expected values are typed in or built by hand in the test.

## What each command's round trip is required to restore

| Command | Required after undo, and after redo |
| --- | --- |
| create | the analysis **absent** after undo, and after redo the **same `ThermalAnalysisId`** (five round trips) and the same definition |
| add load | the load gone / present with its **id, kind, `FaceName` and flux bits** (1250 W/m² on a named face) |
| edit load | the **sign, the target face and the id**: +1250 on face A ↔ −300 on face B; also a *kind* change (surface flux ↔ volumetric source) |
| remove load | the load back **at its original position**: L1 L2 L3 − L2 → L1 L3 → undo → L1 L2 L3, fingerprint equal |
| add / edit / remove condition | the same, for a held 350 K ↔ 325 K on a different face, a convection h = 25 / 300 K ↔ 40 / 290 K, and a held-temperature ↔ convection kind change |
| initial temperature | 300.125 K ↔ 347.875 K, equal as doubles — non-round decimals chosen to catch any loss through formatting |
| solver settings | the previous *values*, not a library default (3.7e-11 / 2.5e-13 ↔ 1.0e-9 / 1.0e-12) |
| transient settings | 0.3 s / 1.0 s / FinalOnly ↔ 0.125 s / 2.0 s / EveryStep, with `0.3` still the exact double it was |

## The whole sequence, step by step

`HeterogeneousFullUndoReturnsBaseline` executes **one of every command class** on a transient analysis (11 steps: two loads, two conditions, an initial temperature, solver settings, transient settings, an
edit of a load, an edit of a condition, two removals) and records the fingerprint after each. Then:

* **non-vacuity first** — every consecutive pair of fingerprints must *differ*; without it eleven comparisons of one unchanging value would pass and prove nothing;
* **full undo** — walking back, the fingerprint equals the recorded one at *every* step, and ends at the baseline fingerprint;
* **full redo** — walking forward, the fingerprint equals the recorded one at every step, and ends at the original final fingerprint.

`MultiLevelUndoRedo` does the same for six adds, and `BranchingHistoryHasNoGhostEdits` for the A, B, C, undo, undo, D shape.

## No drift, because nothing is inverted

```cpp
undo()  ->  apply(document, analysis_, *before_)   // assign a stored definition
redo()  ->  apply(document, analysis_, *after_)
```

`RepeatedUndoRedoDoesNotDrift` cycles **fifty times** and requires `1234.56789` and `−987.654321` to be equal **exactly** on every pass, which a `-= delta` / `+= delta` implementation could not do.
`NoValueIsRoundTrippedThroughText` uses `0.1 + 0.2` and the smallest subnormal double and compares `std::bit_cast<uint64_t>`; the gate (rule 5) forbids `stod`, `from_chars`, `strtod`, `sscanf` and
`istringstream` in the command files, so a text round trip cannot be added without failing the build.

## Identity is preserved, and the id is why

* `CreateAnalysisRedoPreservesId` — the `ThermalAnalysisId` is the same after five undo/redo rounds, because `AddObjectCommand` restores the prototype's recorded `ObjectId` rather than minting one.
* `LoadIdPreserved` / `BoundaryConditionIdPreserved` — ids 41 and 51 survive an add, an edit and three full undo/redo rounds. An edit never replaces the record with a new id.
* `TwoAnalysesDoNotConfuseTheirIds` — the same `ThermalLoadId` in two analyses is two different loads; an edit or removal keyed on one leaves the other untouched.

## Refusals before execution

A command that was never executed has no before-state to restore. `UndoAndRedoRefuseBeforeExecution` requires an error from `undo()` and `redo()` of a never-executed edit and of a never-executed create, and
the document is unchanged — without the check, `undo()` would assign a default-constructed definition.

## The stack discipline, checked rather than assumed

Undo of command *N* assigns the definition command *N* found, on the assumption that the document is in the state *N* left. That is `CommandHistory`'s stack discipline, the same for every module (P15, P16,
P17), and the earlier modules simply rely on it. The independent review of this milestone observed that an assignment of a *whole* definition makes the assumption unusually costly to break: an out-of-band edit
of the analysis would be silently discarded by the next undo. So the command **checks** it:

```text
undo()   refuses (FailedPrecondition, "changed outside the command history") unless the analysis' definition == the one this command installed (*after_)
redo()   refuses (FailedPrecondition) unless the analysis' definition == the one this command found (*before_)
         either one: NotFound if the analysis is gone; "not executed" if it never ran
a refusal changes nothing -- no field, no revision, and CommandHistory leaves the command on the stack it was on
```

`UndoRefusesWhenTheAnalysisWasChangedOutsideTheHistory` edits the analysis behind the history's back and requires that undo refuses, that the out-of-band value is still there, that the revision did not move and
that the stacks are exactly as they were; `UndoAndRedoRefuseWhenTheAnalysisIsGone` covers the missing analysis. The guard costs one definition comparison and keeps the stronger property —
a refused undo is better than a silent loss — without changing what a correct sequence does. The ownership gate (rule 8) still fails an application or renderer file that edits a thermal analysis around the
commands, so in the product no edit can intervene unrecorded; the runtime check is the second line.

## Signed zero

The document's own equality treats `+0.0` and `-0.0` as equal, so an edit that only changes the sign of zero is a **no-op** to `setDefinition` (no revision moves). The command then records `after() == before()`
rather than the requested bit pattern, so `after()` always agrees with the document. `ASignedZeroEditIsANoOpAndTheCommandRecordsWhatTheDocumentHolds` pins that; it is the one case where "the value given" and "the
value stored" are, by the data model's design, the same value.

## An undo or redo of a no-op moves nothing

`AnUndoOrRedoOfANoOpMovesNothing`: a recorded no-op has `before() == after()`, so its undo and redo re-assign an equal definition, which `setDefinition` reports as no change — neither revision moves.
