# P17-CMD-001 — the command model

```text
STATUS:  built and measured. ADR-047.
         NO NEW COMMAND FRAMEWORK. NO NEW INVALIDATION MECHANISM.
         NO NEW IDENTITY TYPE.
```

## The audit came first, and it said build almost nothing

The milestone brief asks for a command layer, exact undo and redo, atomic
failure and result invalidation. Four of those already existed, and the audit
that established it is the reason this milestone is small.

| Capability | Existing API | Reuse? | Why |
| --- | --- | --- | --- |
| command, undo, redo | `bettercad::Command` | **yes** | its contract already states all three guarantees this milestone is about |
| the two stacks, redo clearing | `bettercad::CommandHistory` | **yes** | binds to one document, clears redo when a new command executes |
| create / delete an object | `AddObjectCommand`, `DeleteObjectCommand` | **yes** | `StructuralAnalysis` **is** a `DocumentObject`; P16-CMD-001 proved these restore the same `ObjectId` on redo, by probe |
| atomic definition edit | `Document::modifyObject` | **yes** | validates through `setDefinition` **before** assigning |
| the edit-command shape | `MeshControlEditCommand` | **yes, as the model** | one base, subclasses override one `edit(before)` |
| result invalidation | the analysis object's **revision** | **yes** | `setDefinition` returns `Result<bool>`; `false` is a no-op and moves no revision |
| no-op policy | documented | **yes** | succeeds, invalidates nothing, still recorded |
| compile-fail enforcement | `tests/compile_fail/` | **yes** | control target must compile, so each failure is attributable |

`Command`'s own header already said it:

```text
execute() fails  ->  the document is unchanged and the command is discarded
undo()           ->  exactly reverts a successful execute() or redo()
redo()           ->  reproduces the state execute() produced, INCLUDING THE
                     SAME IDs
```

**Three questions the brief asked me to decide were already decided**, and
following the project rather than inventing an answer is the whole value of
auditing first:

```text
LOAD ORDER        the user's, preserved. It does not affect the physical
                  result -- linear statics superposes and the nodal field
                  accumulates into an ascending map -- but reordering a
                  user's list is not this layer's to do.
WHAT GOES STALE   the result, not the mesh. "A restraint names a FaceName,
                  not a mesh control, so editing one leaves the mesh current
                  and the result stale -- the same asymmetry a material edit
                  has."
A NO-OP EDIT      succeeds, moves no revision, invalidates nothing, and is
                  STILL RECORDED by CommandHistory, which records every
                  command whose execute() succeeded. The same behaviour P15's
                  material edit and P16's mesh-control edit have.
```

## What was built

Eight commands, in `bettercad::structural`:

```text
CreateStructuralAnalysisCommand     wraps AddObjectCommand
DeleteStructuralAnalysisCommand     wraps DeleteObjectCommand
StructuralAnalysisEditCommand       the base: execute/undo/redo once
  SetStructuralAnalysisDefinitionCommand
  AddStructuralLoadCommand
  EditStructuralLoadCommand
  RemoveStructuralLoadCommand
  AddStructuralRestraintCommand
  EditStructuralRestraintCommand
  RemoveStructuralRestraintCommand
  SetSolverSettingsCommand
```

Every one is keyed on `AnalysisId`, which widens implicitly to `ObjectId`, so
no command needs to know that an analysis is a document object.

### Why loads and restraints are edits of one definition and not objects

They live **inside** `StructuralAnalysisDefinition` by value. So "add a load" is
an edit of the analysis, which is exactly the shape `MeshControlEditCommand`
already solves. ADR-047 compares that against a command-per-field design
(rejected: eight copies of the four lines where an atomicity bug hides, and
undo-by-inversion invites floating drift) and against promoting each load to a
`DocumentObject` (rejected: it would give each load an `ObjectId` as well as a
`LoadId`, and ADR-032 settled that a `LoadId` is the identity).

### Failure is atomic by construction, not by care

```text
execute():
    find the analysis                     -> refuse if absent
    capture `before` from the object
    edit(before)                          -> refuse on a command precondition
    apply() -> modifyObject -> setDefinition -> validate   -> refuse if invalid
    ONLY NOW record before_, after_, changed_
```

The before-state is recorded **after** a successful apply, so a refused edit
leaves the command unexecuted and `CommandHistory` never records it. Probe C3
mutates that order -- recording `before_` before checking the apply -- and the
tests catch it.

### Undo and redo assign, they do not invert

```cpp
undo()  ->  apply(document, analysis_, *before_)
redo()  ->  apply(document, analysis_, *after_)
```

Neither computes an inverse. `RepeatedUndoRedoDoesNotDriftTheStoredValues`
cycles fifty times and requires the magnitude to equal its stored value **bit
for bit**, which a `-= delta` / `+= delta` implementation could not.

## Two changes to qualified code, and why each was necessary

### Solver settings became canonical (ADR-047)

`StructuralAnalysisDefinition` deliberately omitted them, and recorded the
reason:

> Named rather than declared. An empty `std::vector<Setting>` here would be a
> placeholder for a type that does not exist.

**`SolverSettings` exists now** — P17-SOLVE-001 created it, with its own
`validate`. The reason for the omission expired rather than being overruled.
And the field had to exist for a command to edit: settings that live only as an
argument to the solve cannot be undone, redone or persisted, so a
solver-settings command would have had nothing to mutate.

`solveStructuralAnalysis` now **reads them from the document** and lost its
settings parameter. One existing call site passed settings; its fixture now
installs them in the analysis first, which exercises the canonical path a
function argument could not.

The type was **moved to `StructuralData.hpp`**, where the other canonical data
lives, so a command header reaches the definition without also reaching
`GlobalStructuralSystem`, `ConstraintSet` or the Tet4 element matrix.
Source-compatible: `StructuralSolve.hpp` already reached that header
transitively through `StructuralDof.hpp`.

### A record-level load validator, factored rather than duplicated

`validate(StructuralRestraint)` already existed and is explicitly record-level
— it "deliberately does NOT consult a mesh". There was no counterpart for a
load: loads were checked inside `prepareStructuralLoads`, which needs one.

A command must refuse a NaN force **with no mesh in hand**, so
`validate(const StructuralLoad&)` is now public, implemented by the same
`isFiniteValue` helper the prepare path uses, in the same translation unit.
**One rule, two callers** with different diagnostic shapes — the prepare path
keeps its `LoadProblem::NonFiniteValue` classification, which is why its loop
was left alone rather than replaced.

## A static_assert caught the new field, which is the instrument working

`StructuralDataTests` keeps a **mirror struct** of the permitted definition
members and asserts a size equality, so a fifth field breaks the build. Its own
comment states the convention:

> THE MIRROR IS EXTENDED BY A MILESTONE THAT ADDS AN AUTHORIZED FIELD, and
> extending it is the point at which the field's authority is stated.

So the mirror gained `SolverSettings solver;` and a note saying why the
settings are canonical intent: a chosen algorithm and two tolerances are a
decision a user makes, not a number measured from a mesh or read back out of a
factorisation. That assertion is the type-level guard the brief's section 59
asks for, and it already existed.
