# P16-CMD-001 — the command authority boundary

```text
SUBJECT:  what the undo history may hold, what it must never hold, and how
          each is enforced rather than asserted
DATE:     2026-10-04
```

The milestone's rule, verbatim from its brief:

```text
edit mesh size
→ undo history stores size intent

NOT

edit mesh size
→ undo history stores 100,000 generated tetrahedra
```

This file records how that is true of the code as built, which part of it is
enforced by construction, which part by test, and where the boundary is
narrower than "canonical versus derived".

## 1. The two sides of the boundary

### Canonical — may appear in the history, and does

| State | Where it lives | Carried by |
| --- | --- | --- |
| The body a control meshes | `MeshControlDefinition::body` (`ObjectId`) | every edit's before/after |
| Global element-size target | `mesh.sizing.globalTargetSize` (`std::optional<Length>`) | `SetGlobalMeshSizeCommand` |
| Face-local size refinements | `mesh.sizing.local` (`std::vector<LocalMeshSizing>`) | the three local-sizing commands |
| Surface discretisation | `mesh.surface` (`Length`, `Angle`) | `SetMeshControlDefinitionCommand` |
| Quality threshold policy | `quality.limits` (`std::map<QualityMetric, QualityThreshold>`) | `SetMeshControlDefinitionCommand` |
| Named boundary intent | `boundarySets` (`BoundarySetId`, name, `std::vector<FaceName>`) | the three boundary-set commands |

Every one of these is a parameter, a unit-bearing quantity, a stable typed ID
or a semantic face reference. None of them is proportional to a mesh.

### Derived — never in the history, and cannot be

| State | Where it lives | Why it is not canonical |
| --- | --- | --- |
| `VolumeMesh` (nodes, tetrahedra) | `Mesher::Held::mesh` | recomputed from intent and geometry |
| `GeometryMeshMap` | `Mesher::Held::map` | computed against one generated mesh |
| `MeshQualityReport` | `Mesher::Held::quality` | measured from one generated mesh |
| `ResolvedSizing` | returned by `resolveSizing`, never stored | resolves `FaceName`s against one regeneration |
| `ResolvedBoundarySet`, `BoundaryFacetSet` | returned, never stored | facet indices belong to one mesh |
| Render buffers | `renderer::MeshView`, built per display | a presentation of one mesh |
| Netgen handles | inside `src/meshing/netgen/`, per call | a backend's working state |

## 2. How the boundary is enforced

**By construction, not by discipline.** A command's only stored state is a
`MeshControlId`, the argument of its own edit, and a before/after pair of
`MeshControlDefinition`. A `MeshControlDefinition` contains no mesh type: it
cannot, because `VolumeMesh` and `GeometryMeshMap` have **private default
constructors** (ADR-030 forbids a mesh existing without a generator having
produced it), so neither can be a default-initialised member of an
aggregate that commands copy about. Storing one would require a deliberate
`std::optional` and an explicit construction.

**No command ever meshes.** `Mesher::generate` takes a `Document` by
`const&`, so it cannot be reached from a `Command::execute(Document&)` that
intended to mutate; and nothing in `src/meshing/MeshingCommands.cpp` includes
`Mesher.hpp`. The include direction is the other way: `Mesher.cpp` includes
`MeshingCommands.hpp` for `findMeshControl`.

```text
$ grep -c "Mesher" src/meshing/MeshingCommands.cpp
0
```

**A single mutation path.** Every edit — all seven — goes through one
four-line `apply` into `Document::modifyObject<MeshControl>`, which
type-checks the object, calls `MeshControl::setDefinition`, and bumps the
object and document revisions **only when the mutation reports a change**.
There is no second way to change a control's definition from outside, because
`setDefinition` is the only non-const member and `Document` hands out
`const` references.

## 3. What a history step costs

A `MeshControlDefinition` is a body handle, two unit-bearing quantities, an
optional `Length`, a `std::map` of thresholds and two short vectors of
references. An edit stores two of them. So a step is proportional to **the
number of controls the user has created**.

Measured, not estimated, by
`MeshingCommand_AHistoryStepCostsTheIntentAndNotTheMesh` and
`MeshingCommand_NoCommandIsLargeEnoughToHoldAMesh` (debug-ext, GCC 16.1.0,
x86_64; a r6 x h20 mm cylinder with one local control and one boundary set):

```text
MeshControlDefinition            136 B
SetGlobalMeshSizeCommand         328 B      AddLocalMeshSizingCommand    424 B
AddBoundarySetCommand            376 B      SetMeshControlDefinition     448 B
CreateMeshControlCommand         216 B      DeleteMeshControlCommand     384 B

one global-size history step    1190 B = 328 B command + 2 x 431 B intent

the mesh that step's intent describes
  at a 10 mm target            14896 B     504 elements   step = 7.99% of it
  at a 1.5 mm target           27760 B     932 elements   step = 4.29% of it
```

The step is **the same 1190 B at both mesh sizes** — asserted, not just
reported — because its size is a function of the controls and of nothing else.
The mesh nearly doubled; the step did not move. For the brief's
100,000-tetrahedron case the step is unchanged again: it is the same
`std::optional<Length>` whatever the mesh size.

This is why no command in this milestone needed a size limit, a compression
step or a "light" history mode. The history is small because of what it holds,
not because of a policy about how much of it to keep.

## 4. What is NOT in scope, and is refused rather than deferred silently

**Command-history persistence.** BetterCAD does not persist undo stacks: a
`.bcad` file holds document objects, and `CommandHistory` is constructed empty
beside a loaded document. P16-CMD-001 therefore persists no history, and
`MeshControl` itself is **not yet serialized** — `P16-PERSIST-001` owns that,
and `MeshControl_CannotYetBeSavedAndSaysSo` records the refusal as a test so
the gap is visible rather than assumed.

**Compound commands.** The framework has no compound-command type, so a dialog
that edits several fields in one user action uses
`SetMeshControlDefinitionCommand` — one command, one undo step, the whole
definition as its value. Introducing a general compound command would be a
core-framework change, which this milestone is not authorized to make.

## 5. The evidence

| Claim | Test |
| --- | --- |
| Create/delete keep the same `MeshControlId` | `MeshingCommand_CreateExecutesUndoesAndRedoesWithTheSameIdentity`, `..._DeleteRestoresTheWholeControlOnUndo` |
| Every edit undoes and redoes exactly | `..._GlobalSizeIsExactThroughUndoAndRedo`, `..._LocalSizingAddEditRemoveAreExactAndKeyedByFace`, `..._BoundarySetAddEditRemoveKeepItsIdentity` |
| The mandatory five-step sequence | `..._AMultiStepSequenceUndoesAndRedoesExactly` |
| A refusal changes nothing, including stack depths | `..._AnInvalidSizeFailsBeforeTouchingAnything`, `..._LocalSizingRefusesWhatTheModelForbids` |
| A new command discards the redo branch | `..._ANewCommandDiscardsTheRedoBranch` |
| No mesh in a history entry's text | `..._DescriptionsNameTheIntentAndNotAnyMesh` |
| No mesh restored by an undo | `..._DeletingAControlLeavesNoMeshBehindIt` |
| The mesh follows the restored intent | `..._RemeshingAfterAnUndoRedoChainFollowsTheRestoredIntent` |
