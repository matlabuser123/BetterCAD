# P17-CMD-001 — exact undo and exact redo

```text
"EXACT" IS A STATEMENT ABOUT CANONICAL CONTENT, NOT ABOUT IDENTITY.
The two are tested separately, and in OPPOSITE directions.
```

## The distinction that shapes every test here

BetterCAD's revisions are **monotonic**. An edit moves the analysis object's
revision; an undo restores the content and moves the revision **again**, rather
than rolling it back. So there are two different claims, and conflating them
would hide a bug either way:

```text
CONTENT     undo restores the canonical intent EXACTLY
            -> tested by an independent projection, below

IDENTITY    undo does NOT restore the old revision
            -> tested in the opposite direction, because monotonicity is
               exactly what stops a previously computed result becoming
               current again just because the numbers returned to a
               previous state
```

`StructuralCommand_UndoMovesTheRevisionForwardRatherThanBack` asserts both at
once: the loads are empty again **and** the revision is strictly greater than
it was before the undo, and not equal to where it started.

A test that compared the revision for equality would have been the natural
thing to write, and it would have been wrong.

## The canonical projection

Built by hand rather than by comparing two `StructuralAnalysisDefinition`s, so
that it **names every field it checks**:

```cpp
struct CanonicalIntent {
    MeshControlId mesh;
    StructuralAnalysisMode mode;
    std::vector<StructuralLoad> loads;        // identity, target, values, ORDER
    std::vector<StructuralRestraint> restraints;
    SolverSettings solver;
};
```

It carries **no revision**, no derived result, no mesh and no viewer state. The
same instrument `MeshFingerprint` is for P16: a new field added to the
definition does not silently fall outside the comparison, because the
projection would have to be extended to see it — and extending it is where the
new field's authority gets stated.

## What each command's round trip is required to restore

| Command | Checked beyond the projection |
| --- | --- |
| create analysis | the **same `AnalysisId`** after redo, and the analysis absent after undo |
| delete analysis | the whole analysis, loads and restraints and settings together |
| add load | the load gone after undo, present after redo |
| edit load | the **target** and the magnitude, `== 250.0` bit for bit after undo |
| remove load | the load back **at its original position**, not appended |
| add restraint | gone / present |
| edit restraint | face **and** mask together, `Ux` recovered and `Uz` absent |
| remove restraint | the same `RestraintId`, face and mask, at its position |
| solver settings | the exact previous tolerances, not a library default |

`RestraintComponentsSurviveUndoAndRedoExactly` walks **all seven** component
combinations P17-BC-001 defines — `Ux`, `Uy`, `Uz`, the three pairs and fixed —
and round-trips each. A mask that collapsed to "fixed" whatever it was would
satisfy a test that only used fixed supports, which is what the earlier
milestone's own defect looked like.

## The whole sequence, step by step

`UndoAllThenRedoAllIsExactAtEveryStep` runs the brief's sequence — create, two
loads, two restraints, edit a load, edit a restraint, change settings, remove a
load, remove a restraint — snapshotting the projection after each.

Then it undoes to the start, comparing **every** intermediate state, and redoes
forward, comparing every one again.

**With a non-vacuity gate first:** each consecutive pair of snapshots is
required to *differ*. Without that, nine comparisons of one unchanging value
would pass and prove nothing.

## No drift, because nothing is inverted

```cpp
undo()  ->  apply(document, analysis_, *before_)   // assign a stored value
redo()  ->  apply(document, analysis_, *after_)
```

`RepeatedUndoRedoDoesNotDriftTheStoredValues` cycles **fifty times** and
requires `magnitude.si() == 1234.56789` exactly on every pass — not within a
tolerance. An implementation that undid by `-= delta` and redid by `+= delta`
would accumulate error and fail here; one that assigns cannot.

The same reasoning covers direction vectors: nothing re-normalises on replay,
because replay assigns the stored record.

## Identity is preserved, and the ID is why

```text
CreateAnalysisUndoRedoKeepsTheSameAnalysisId
    redo restores the SAME AnalysisId, because AddObjectCommand restores the
    prototype's recorded ID rather than minting a new one. A result records
    which analysis it came from, so a redo that renumbered would orphan it.

RemoveLoadUndoRestoresItAtItsOriginalPosition
    the same LoadId, and the same POSITION. Order is the user's; a restored
    load appended at the end would be a different canonical state, and the
    projection compares the vectors in order so it would be caught.

TwoAnalysesDoNotConfuseTheirLoadIds
    the same LoadId in two analyses is two different loads. Editing one
    leaves the other untouched, asked of both objects independently.
```

## Refusals before execution

A command that was never executed has no before-state to restore.
`UndoAndRedoRefuseBeforeExecution` requires `FailedPrecondition` from both,
rather than letting `undo()` assign a default-constructed definition — which is
what an unchecked `*before_` would do.
