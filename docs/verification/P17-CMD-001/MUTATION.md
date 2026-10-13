# P17-CMD-001 — the mutation probes

```text
16 probe runs over 13 distinct mutations.
12 killed, 1 equivalent mutant kept and documented.
THREE REAL TEST GAPS found and closed.

PROBES RAN BEFORE THE QUALIFICATION, not after it. A surviving probe stopped
the pre-freeze gates by design rather than being discovered inside a
three-hour run.
```

## The batch

Filtered across **both** the command suite and the structural suites, because
the solver settings becoming canonical changed the solve too: a mutation that
ignored the document's settings would leave every command test green.

| Probe | Mutation | Verdict |
| --- | --- | --- |
| C1 | undo applies the AFTER state | KILLED (11) |
| C2 | redo applies the BEFORE state | KILLED (8) |
| C3 | `before_` recorded even when apply failed | **SURVIVED** → closed |
| C4 | a no-op claims it changed something | KILLED (2) |
| C5 | a duplicate `LoadId` is allowed | KILLED (2) |
| C6 | editing a missing load creates it | KILLED (2) |
| C7 | a NaN load value is accepted | KILLED (2) |
| C8 | an edit erases and appends, losing position | **SURVIVED** → closed |
| C8b | the same on the restraint path | KILLED (1) |
| C9 | remove erases the first load, not by id | KILLED (2) |
| C10 | an empty component mask is accepted | KILLED (1) |
| C11 | the settings validator removed | **SURVIVED** — equivalent |
| C12 | the solve ignores the document's settings | KILLED (1) |

**C12 is the one that validates the whole settings decision.** It died to
exactly one test — P17-SOLVE-001's own residual gate, which installs
`relativeResidualTolerance = 1.0e-300` and requires the solve to refuse. That
test became the proof only because its fixture now installs the settings
*through the document* instead of passing them as an argument. Had the fixture
kept passing them, C12 would have survived and nothing would have shown the new
field is read.

## C8 — a gap every edit test was blind to

An edit that erased the record and pushed the replacement onto the end passed
everything. **Every edit case used a single load**, so reordering was
invisible: with one element, erase-and-append leaves it exactly where it was.

Closed by two cases that edit the **middle of three** — the only position that
detects either an append or a prepend — asserting the ids in order, that the
edited record sits in the middle, and that the neighbours kept their own
values. The restraint equivalent was added at the same time, and C8b confirms
it has teeth; no probe in the original batch had made that mutation.

## C3 — a gap that took two attempts to even reach

C3 moves `before_ = before` above the check on `apply()`'s result, so a command
whose apply **failed** still records a before-state and becomes undoable: undo
would then assign a state that was never current.

It had to be run twice. The first attempt reported `ANCHOR NOT FOUND` — the
anchor said `*after_` where the code says `*after`, and a silent patch failure
had left the wrong text in the script. **An anchor failure is not a verdict**,
and recording it as one would have been the quiet kind of wrong this whole
instrument exists to avoid.

When it finally ran, it **survived**. Working out why is what made it fixable:

```text
For the add, edit and remove commands APPLY CANNOT FAIL AFTER EDIT SUCCEEDED.
  They validate their own record, and validate(definition) only checks the
  mesh handle and the solver settings -- neither of which those commands
  touch. So for seven of the eight commands C3's mutation sits in an
  UNREACHABLE path.

SetStructuralAnalysisDefinitionCommand IS THE ONE THAT CAN REACH IT.
  It replaces the whole definition and its edit() validates NOTHING --
  setDefinition does -- so a definition carrying an invalid mesh handle
  reaches apply() and is refused there.
```

Closed by `ACommandWhoseApplyFailedCannotBeUndone`, which constructs that
command **directly rather than through the history**, because `CommandHistory`
discards a command whose execute failed — a history-driven test could never ask
the command anything afterwards. It requires the document untouched and both
`undo()` and `redo()` to refuse with `FailedPrecondition`, plus a non-vacuity
section showing the same command with a *valid* mesh is accepted.

## C11 — an equivalent mutant, with a measured explanation

Removing `validate(settings_)` from `SetSolverSettingsCommand` changes nothing
observable: `setDefinition` still refuses the edit, because
`validate(StructuralAnalysisDefinition)` validates the solver settings itself.

The asymmetry that explains all three validator probes at once:

```text
validate(StructuralAnalysisDefinition) checks
    the mesh handle          -> so a bad mesh is caught without the command
    validate(solver)         -> so C11 is REDUNDANT and survives
but NOT the loads
but NOT the restraints       -> so C7 and C10 are LOAD-BEARING and die
```

The check is kept. It is defence in depth, and it keeps the three commands
symmetric so a reader is not misled into thinking the settings are unvalidated
where the loads are. Recorded as equivalent rather than left looking like an
open gap — and **measured**, not argued: the same probe shape killed its two
siblings.

## What the three gaps have in common

Each was invisible for a structural reason, not through carelessness:

```text
C8   every edit case used a SINGLE load, so position could not be observed
C3   the path is unreachable through seven of the eight commands, so the
     obvious fixture cannot get there
C11  a second validator upstream makes the first one redundant
```

That is the fourth, fifth and sixth time in P17 that a gap turned out to be
"the only caller in the tests produced input that could not expose it". The
standing lesson holds: ask what the FUNCTION accepts, not what today's callers
pass — and when a probe survives, work out *why* before deciding whether it is
a gap or an equivalence.

Full logs: `logs/probe-cmd.log`, `logs/probe-c3c8.log`, `logs/probe-c3-again.log`.
