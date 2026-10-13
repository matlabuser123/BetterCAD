# P17-CMD-001 — a failed command is atomic

```text
ATOMIC BY CONSTRUCTION, NOT BY CARE. There is no rollback path, because there
is nothing to roll back: a command validates and then assigns a whole
validated value.
```

## The order that makes it true

```text
execute():
  1  find the analysis                 -> refuse if absent
  2  capture `before` from the object
  3  edit(before) -> a WHOLE new definition, or a refusal
  4  apply() -> modifyObject -> setDefinition -> validate -> refuse if invalid
  5  ONLY NOW: before_ = before;  after_ = *after;  changed_ = *changed;
```

Two properties follow, and neither needs a `try`/`catch` or an undo-on-failure
path:

**Nothing is partially mutated.** `edit` returns a complete `StructuralAnalysisDefinition`
by value. The document is only ever assigned a definition that has already
passed `validate`, so there is no window in which one field is new and the rest
are old. A multi-field edit cannot leave the target changed and the magnitude
not.

**A failed command is not recorded.** `before_` is set at step 5. A refusal at
1, 3 or 4 leaves it unset, so the command is "not executed", and
`CommandHistory::execute` records only a command whose `execute()` succeeded.
The attempt reaches neither stack.

Probe **C3** mutates exactly that order — assigning `before_` before checking
the apply — and the suite catches it. That is the probe the whole design rests
on.

## The refusals, by shape

| Attempt | Code | Where the rule lives |
| --- | --- | --- |
| duplicate `LoadId` | `AlreadyExists` | the command's own precondition |
| edit a missing `LoadId` | `NotFound` | the command's own precondition |
| remove a missing `LoadId` | `NotFound` | the command's own precondition |
| non-finite load value | `InvalidArgument` | `validate(StructuralLoad)` — **P17-LOAD's rule, reused** |
| duplicate `RestraintId` | `AlreadyExists` | the command's own precondition |
| empty component mask | refused | `validate(StructuralRestraint)` — **P17-BC's rule, reused** |
| unusable solver settings | `InvalidArgument` | `validate(SolverSettings)` — **P17-SOLVE's rule, reused** |
| a missing analysis | `NotFound` | the edit base, before anything else |
| create with unusable settings | `InvalidArgument` | `StructuralAnalysis::create` → `validate` |

Four of the nine reuse a validator that already owned the rule. None of them
restates a pressure sign convention, a component semantic or a tolerance bound.

## What each failure test asserts

Every atomicity case checks the same three things, because checking only the
first would miss the interesting failures:

```text
the canonical projection is UNCHANGED      (nothing partially applied)
the undo depth is UNCHANGED                (no history entry)
and for the solve-backed cases:
  a current result is STILL CURRENT        (no spurious invalidation)
```

`AFailedCommandLeavesACurrentResultCurrent` is the one that matters most. An
implementation that staled the result whenever a command was *attempted* would
pass every invalidation test and fail only this one.

`EditingAMissingLoadFailsAndCreatesNothing` carries a second assertion past the
refusal: the loads vector is still **empty**. An edit-or-insert implementation
would have silently added the load, turning a stale dialog into an invented
one, and a test that stopped at `REQUIRE_FALSE(result)` would not have noticed.
Probe **C6** makes that implementation and the suite kills it.

## The redo branch

Two rules, and the second is the edge case the brief singles out.

```text
A SUCCESSFUL new command invalidates the redo branch.
    ASuccessfulNewCommandInvalidatesTheRedoBranch:
      add, add, undo -> canRedo
      then a NEW command -> canRedo is false, redoCount == 0

A FAILED new command does NOT.
    AFailedNewCommandLeavesTheRedoBranchIntact:
      add, undo -> canRedo
      then an INVALID command -> still canRedo, and the redo works
```

The second follows from the framework rather than from a P17 opinion:
`CommandHistory::execute` clears the redo stack only **after** the command
succeeds, so a refused attempt commits no new branch. The brief asked me to
determine the project's convention and match it rather than choose; this is
that convention, now asserted.

## Exception safety

The commands return `Result<void>` throughout and throw nothing. `edit` returns
a value or an error; `apply` returns `Result<bool>`. There is no path that
mutates a collection and then fails, so the strong guarantee is structural
rather than something to maintain.

`std::vector` operations inside `edit` run on a **local copy** of the
definition. If one threw, the copy would be destroyed and the document would
never have been touched.
