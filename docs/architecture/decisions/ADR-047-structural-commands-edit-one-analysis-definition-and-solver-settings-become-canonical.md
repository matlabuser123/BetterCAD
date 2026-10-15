# ADR-047 — Structural commands edit one analysis definition, and solver settings become canonical

```text
STATUS:     ACCEPTED
MILESTONE:  P17-CMD-001
DECIDED:    2026-10-13, by an explicit scope decision from the owner
DEPENDS ON: the existing Command / CommandHistory framework (core, layer 0)
            P16-CMD-001's MeshControlEditCommand pattern
```

## Context

P17-CMD-001 needs undoable commands for creating a structural analysis and for
adding, editing and removing its loads, restraints and solver settings. The
history must hold canonical engineering intent and never a generated solver
array.

**The framework already exists and nothing here adds a second one.** `Command`
declares `description/execute/undo/redo`, each returning `Result<void>`, and its
contract already states the three guarantees this milestone is about:

```text
execute() fails  ->  the document is unchanged and the command is discarded
undo()           ->  exactly reverts a successful execute() or redo()
redo()           ->  reproduces the state execute() produced, INCLUDING THE
                     SAME IDs
```

`CommandHistory` keeps the two stacks, binds to one document, and clears the
redo stack when a new command executes.

Two facts about the structural data model shaped everything below, and both
were found by audit rather than assumed.

### `StructuralAnalysis` is a `DocumentObject`

So `AddObjectCommand` and `DeleteObjectCommand` already create and delete one
correctly, and `Document::modifyObject` already routes an edit through
`StructuralAnalysis::setDefinition`, which **validates before assigning**.
`setDefinition` returns `Result<bool>`: `false` means a no-op, and a no-op
**does not move the revision**. That is the same contract
`MeshControl::setDefinition` has, and the revision is already what makes a
previously computed result non-current.

**Result invalidation therefore needs no new mechanism, and this milestone
introduces none.** No GUI flag, no invalidation list, no notification. An edit
that changes the definition moves the revision; a result computed under the old
revision is already not current.

### Loads and restraints live *inside* one definition

```cpp
struct StructuralAnalysisDefinition {
    MeshControlId mesh{};
    StructuralAnalysisMode mode = StructuralAnalysisMode::LinearStatic;
    std::vector<StructuralLoad> loads{};
    std::vector<StructuralRestraint> restraints{};
    // Solver settings -- P17-SOLVE-001   (named, NOT declared)
};
```

They are not separate `DocumentObject`s. So "add a load" is an **edit of the
analysis definition**, which is exactly the shape `MeshControlEditCommand`
already solves.

## Candidates for the command layer

### A — one edit base over the whole definition, subclasses supply the delta

A `StructuralAnalysisEditCommand` implements `execute/undo/redo` once; each
subclass overrides a single `edit(before) -> Result<StructuralAnalysisDefinition>`.

```text
+ FAILURE IS ATOMIC BY CONSTRUCTION, and not by care. `modifyObject` validates
  through setDefinition before assigning anything, and the before-state is
  recorded only after a successful apply -- so a refused edit leaves the
  document untouched and CommandHistory never records the command.
+ undo and redo ASSIGN A STORED DEFINITION. They do not invert a delta, so
  there is no inverse arithmetic and no floating drift however many times the
  pair is repeated.
+ exactly P16-CMD-001's shape, already qualified and already understood
+ the before/after pair is proportional to the number of LOADS AND
  RESTRAINTS, never to the mesh: a definition is a MeshControlId, a mode and
  two short vectors of canonical records
+ one place where an atomicity mistake could hide, so one place to test
- a whole-definition copy per command, where a single field changed. For an
  analysis that is two small vectors; for a mesh it would be unacceptable,
  which is why P16 made the same trade for the same reason
- two commands editing different loads still serialise through one object,
  which is correct for a single-threaded document history and would need
  revisiting only if concurrent editing ever arrived
```

### B — a command per field, inverting its own change on undo

```text
+ the smallest possible payload per command
- every command re-implements capture, apply, undo, redo -- four lines each
  that are the four lines where an atomicity bug hides, multiplied by eight
- UNDO BY INVERSION INVITES DRIFT: `magnitude -= delta` on undo and
  `+= delta` on redo is not exactly reversible in floating point, and the
  spec's own section 131 names this failure
- a new field in the definition means a new inversion to remember, forever
```

### C — promote each load and restraint to its own `DocumentObject`

```text
+ add/remove would reuse AddObjectCommand/DeleteObjectCommand directly
+ per-entity revisions
- IT CHANGES THE QUALIFIED P17-DATA MODEL WHOLESALE. The definition owns its
  loads by value, `validate` is defined over the whole definition, and the
  solve reads them from it
- it would give each load an ObjectId as well as a LoadId -- two identities
  for one thing, and ADR-032 settled that a LoadId is the identity
- P17-PERSIST-001 is not written; changing the ownership model now would
  decide that milestone's format from here
```

## Decision

**Candidate A.**

```cpp
// bettercad::structural, layer 50; Command comes from core, layer 0
class CreateStructuralAnalysisCommand;        // wraps AddObjectCommand
class StructuralAnalysisEditCommand;          // the base
  class AddStructuralLoadCommand;
  class EditStructuralLoadCommand;
  class RemoveStructuralLoadCommand;
  class AddStructuralRestraintCommand;
  class EditStructuralRestraintCommand;
  class RemoveStructuralRestraintCommand;
  class SetSolverSettingsCommand;
```

Every command is keyed on `AnalysisId`, which widens implicitly to `ObjectId`,
so no command needs to know that an analysis is a document object.

### Solver settings become canonical, and the reason the field was absent has expired

`StructuralAnalysisDefinition` deliberately omitted them, and said why:

> Named rather than declared. An empty `std::vector<Setting>` here would be a
> placeholder for a type that does not exist, and a reader could not tell an
> unimplemented field from an intentionally empty one.

**`SolverSettings` exists now.** P17-SOLVE-001 created it, with its own
`validate(const SolverSettings&)`. So the stated reason for the omission is
gone, and the field is added using the existing qualified type:

```cpp
struct StructuralAnalysisDefinition {
    ...
    SolverSettings solver{};
};
```

`solveStructuralAnalysis` reads the settings from the definition rather than
taking them as an argument, because an analysis whose solver settings live
outside the document cannot be undone, redone or persisted — and a
settings command would otherwise mutate nothing.

This **changes a qualified type**, so P17-DATA-001, P17-SOLVE-001 and
P17-VALID-001 are requalified by this milestone's run. That is a cost the owner
accepted explicitly; the alternative was to report the solver-settings box
blocked.

### A record-level load validator, factored rather than duplicated

`validate(const StructuralRestraint&)` already exists and is explicitly
record-level: it "deliberately does NOT consult a mesh". There is no
counterpart for a load — loads are checked inside `prepareStructuralLoads`,
which needs a mesh.

A command must refuse a NaN force **without** a mesh, so this ADR exposes

```cpp
[[nodiscard]] Result<void> validate(const StructuralLoad& load);
```

implemented by the `isFiniteValue` helper that `prepareStructuralLoads`
already uses, in the same translation unit. **One definition, two callers** —
not a second copy of the rule. Whether the load's *target resolves* stays where
it is, in preparation, because that is a question about a particular mesh.

### What a command may carry, and what it may not

```text
ALLOWED, because it is canonical engineering intent
  AnalysisId, LoadId, RestraintId
  FaceName / GeometryReference
  StructuralLoad, StructuralRestraint, RestraintComponents
  SolverSettings, StructuralAnalysisMode, MeshControlId

FORBIDDEN, because it is derived from a mesh that will be regenerated
  DofIndex, FreeEquationIndex
  boundary facet handles or indices
  ConstraintSet, PreparedStructuralRestraints
  PreparedStructuralLoads, the equivalent nodal force vector
  GlobalStructuralSystem, K, F, factorisations
  StructuralSolution, u
  StructuralPostResult, strain, stress, von Mises, principal stress
  StructuralReactionResult
  MeshView, render buffers, deformation scale, contour selection
```

### `NodalForceLoad` carries a `NodeId`, and that is qualified intent

The milestone brief lists a `NodeId` in a load command as an automatic
failure. **The qualified codebase disagrees, and it wins** under this
repository's conflict order (verification evidence → TODO → architecture).

`StructuralLoad` is a variant of four payloads. Three are `FaceName`-targeted
and carry no mesh identity. The fourth is

```cpp
struct NodalForceLoad {
    meshing::MeshStamp mesh{};   // "Without it the handle means nothing."
    meshing::NodeId node{};
    Force3D force{};
};
```

whose header calls it "MESH-LOCAL BY DECLARATION, and that is a decision rather
than an oversight", says it "is refused against any other mesh, including one
with the same node count and the same numeric handles", and records that what
it is for is fixtures — an element test, a solver check, a headless numerical
case.

So a command that adds a nodal force load necessarily carries that `NodeId`,
**as part of a canonical record that declares its own mesh binding and refuses
a foreign mesh.** Forbidding it would leave a qualified canonical load form
with no undoable editing path, which is a worse answer than carrying it.

What this ADR forbids instead is everything genuinely derived, and a `NodeId`
or `ElementId` as a **restraint** target — restraints are `RestraintId +
FaceName + components` and have no mesh-local form at all.

## Consequences

```text
No new command framework, no new invalidation mechanism, no new identity type.

Undo and redo assign stored definitions, so repeating the pair cannot drift.

A no-op edit succeeds, moves no revision, invalidates nothing, and is still
recorded -- the project's existing policy, not a P17 special case.

solveStructuralAnalysis loses its settings parameter. Every call site and the
P17-VALID entry are updated, and three predecessors are requalified.

A durable point load on the model is still unavailable: it would need a CAD
VERTEX reference P16 does not provide. Recorded as a limitation by
P17-LOAD-001 and unchanged here.

Compile-fail cases prove the forbidden list is unconstructible rather than
merely absent. P16's trap applies: an aggregate's brace elision will silently
accept a wrong first member, so the probes go through COMMAND CONSTRUCTOR
PARAMETERS, where a typed parameter is copy-initialised and no elision
applies.
```

## Amendment (P17-QUAL-001, 2026-10-15)

> **Recorded because the evidence outranks this document.** The Decision's
> class list omits two commands that shipped with it:
> `DeleteStructuralAnalysisCommand` and `SetStructuralAnalysisDefinitionCommand`
> (`include/bettercad/structural/StructuralCommands.hpp`). Both follow the
> decision exactly -- the delete wraps the document's own object removal and
> restores the whole definition on undo (`StructuralCommand_DeleteAnalysisUndoRestoresTheWholeAnalysis`),
> and the set-definition command is one more `edit(before)` subclass. Every
> other clause -- one base, failure atomic, undo and redo ASSIGN a stored
> definition, canonical solver settings, no new invalidation mechanism -- was
> verified against the code by P17-QUAL-001. See
> `docs/verification/P17-QUAL-001/ADR_AUDIT.md`.
