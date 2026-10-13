# P17-CMD-001 — result invalidation

```text
THIS MILESTONE ADDS NO INVALIDATION MECHANISM. It proves the existing one
covers the commands.

No flag. No notification. No GUI-visible stale bit. No second opinion about
staleness anywhere in src/structural/StructuralCommands.cpp.
```

## The mechanism, which already existed

An edit goes through `Document::modifyObject` → `StructuralAnalysis::setDefinition`,
which **moves the object's revision when something actually changed**. A result
records its provenance in a `StructuralResultSource` that already carries that
`analysisRevision`. So:

```text
command changes the intent
    -> the analysis object's revision moves
        -> the result's recorded source no longer matches the current source
            -> resultCurrency(result, currentResultSource(...)) == Stale
```

Every test here asks that question through **P17-DATA-001's own two
functions** — `currentResultSource` reads the document, `resultCurrency`
compares the provenance — precisely so the tests cannot pass by consulting
something P17-CMD invented.

## The asymmetry, which is the point

| After a successful… | geometry | P16 mesh | FEA result |
| --- | --- | --- | --- |
| add / edit / remove **load** | current | **current** | **stale** |
| add / edit / remove **restraint** | current | **current** | **stale** |
| **solver settings** edit | current | **current** | **stale** |

A load edit changes what the *answer* should be and changes nothing about the
mesh it would be computed on. Over-invalidating the mesh would force a remesh
of a hundred thousand elements for a tolerance change.

`EveryPhysicsCommandStalesTheResultAndNotTheMesh` has one section per command
and asserts **both halves** each time: the result became `Stale` *and*
`describesTheModel(meshCurrency())` still holds. A test that only checked
staleness would pass an implementation that also staled the mesh.

**With the precondition asserted first:** the result is required to be
`Current` and the mesh current *before* each command, so "no longer current" is
a transition and not a standing state.

One section edits a pressure by **one part in a million** — `-1.0e6` to
`-1.000001e6` — and requires the same staleness. Magnitude is not the test; any
change to the intent makes the old answer an answer to a different question.

## The three cases that run the opposite way

These are the ones an over-eager implementation fails, and they matter more
than the seven above.

### A failed command leaves a current result current

`AFailedCommandLeavesACurrentResultCurrent` solves, then attempts **one of each
shape of refusal** — a duplicate `LoadId`, a missing removal target, a
non-finite value, unusable settings — and requires afterwards:

```text
the result is STILL Current
the mesh is STILL current
the undo stack is unchanged
```

An implementation that staled the result on *attempt* would pass all seven
invalidation sections and fail only this one. Nothing changed, so nothing is
stale — and an invalid edit must not cost the user a solve.

### A no-op edit does not stale a current result

`ANoOpEditDoesNotStaleACurrentResult` assigns the settings their existing value
and the load its existing value. `changedAnything()` is required to be false,
no revision moves, and the result stays `Current`.

This is the other half of the project's no-op policy: the command **succeeds**
and is **recorded**, but the canonical physics did not change, so charging the
user a re-solve would be wrong.

### Undo does not resurrect the old result

`UndoDoesNotResurrectTheOldResult` is the adversarial scenario the brief
singles out:

```text
solve            -> result R1 is Current
edit the load    -> R1 is Stale
undo the edit    -> the CONTENT is back, bit for bit (-1.0e6)
                 -> R1 IS STILL STALE
```

It holds because revisions are monotonic: the undo takes a *new* revision
rather than restoring the old one, so R1's recorded source cannot match again
however the numbers compare.

A sub-section then solves again, requires the **new** result to be `Current`
and the **first** one to still be `Stale` — so the fresh solve recomputed from
the restored intent rather than reviving anything. The history never held an
array to revive; that is what HISTORY_AUTHORITY.md proves at compile time.

## No command triggers a solve, a remesh or a postprocess

Structurally rather than by counting calls: `StructuralCommands.cpp` includes
`StructuralCommands.hpp` and `Document.hpp` and nothing else from the
structural module. It cannot reach `solveStructuralAnalysis`,
`prepareStructuralLoads`, `Mesher::generate` or any post-processing entry
point, because none of them is declared in what it includes — which the
compile-fail group's "not a member of" diagnostic demonstrates for the result
types.

The invalidation tests are consistent with that: after every command the mesh
is still current, which it would not be if a command had remeshed, and the
result is stale rather than replaced, which it would not be if a command had
solved.
