# P16-CMD-001 — command architecture and canonical-intent audit

```text
SUBJECT:  what command framework exists, where P16's canonical intent lives,
          and what this milestone must build versus what it gets for free
DATE:     2026-10-04
AT:       0b13e20
```

Written before any production code, because the answer to the second question
decides whether this milestone is buildable at all.

## 1. A general command framework exists, and P16 must use it

```text
include/bettercad/core/document/Command.hpp     Command, CommandHistory
include/bettercad/core/document/Commands.hpp    the generic commands
src/core/document/CommandHistory.cpp            the stacks
```

`Command`'s contract is already what this milestone needs, stated in its own
header:

```text
execute()  applies the edit for the first time. If it fails, the document is
           unchanged and the command is discarded.
undo()     exactly reverts a successful execute() or redo().
redo()     re-applies the edit and reproduces the state execute() produced,
           INCLUDING THE SAME IDs.
```

`CommandHistory` binds to the first document it is used with and refuses any
other; executing a new command clears the redo stack; it carries an optional
depth limit and exposes `canUndo/canRedo/undoCount/redoCount` and the
descriptions.

```text
EXISTING COMMAND TYPE   bettercad::Command, pure virtual, Result-returning
MUTATION MODEL          commands call Document's public API; no direct GUI
                        mutation
UNDO STORAGE            each command stores its own before-state; there is no
                        document snapshot
REDO STORAGE            the command itself, re-executed
FAILURE SEMANTICS       a failed execute leaves the document unchanged and the
                        command is discarded -- the framework's own contract
NESTED / GROUPED        no compound-command type exists. Per-module commands
                        that need atomicity over several fields carry a
                        before/after pair of one value object instead (the
                        material precedent)
CANONICAL EXAMPLES      sketch, features, datums, assembly, drawing, materials
```

**There will be no `MeshUndoManager`.** The brief forbids a parallel undo
system and the project already has the general one; P16 adds commands to it,
exactly as P15 did for materials.

## 2. P16's canonical intent does NOT yet live in the Document

This is the finding that shaped the milestone.

```text
MeshSizingControls     a value struct in meshing/MeshSizing.hpp
SurfaceMeshControls    a value struct in meshing/SurfaceMesh.hpp
VolumeMeshControls     a value struct in meshing/VolumeMesh.hpp
NamedBoundarySet       a value struct in meshing/GeometryMeshMap.hpp
```

Every one is **passed as an argument** to
`volumeMeshFor(document, regenerator, feature, controls)`. None of them appears
anywhere under `core/`, and no document object holds one. Searching for
`MeshControl`, `MeshControlId` and `Mesher` in `src/`, `include/` and `apps/`
returns **nothing**.

So on the tree this milestone starts from, there is no canonical meshing intent
for a command to mutate. A command framework with nothing canonical to edit
cannot satisfy a gate that reads *"commands mutate canonical meshing intent"*.

### This was decided, and assigned here, two milestones ago

`ADR-030` chose its option 3 explicitly:

> **A `MeshControl` is a document object.** It gets a `MeshControlId` that
> widens to `ObjectId`, it is a node in the dependency graph, **it is created
> and edited only through commands**, and it is persisted. It is a document
> object for the same reason ADR-025 made a material one: *"changing a density
> must be able to invalidate a derived mass"* — changing a body must be able to
> invalidate a derived mesh, and only a graph node can express that.
>
> **A generated mesh is derived state held by a `Mesher` service**, keyed by
> its control, exactly as the `Regenerator` holds bodies keyed by their
> features. It is never a document object, has no `ObjectId`, and is not in the
> dependency graph.

and its validation section names this milestone's debt:

```text
P16-CMD-001      the full chain undoes and redoes to an exactly equal canonical
                 state, with the mesh RECOMPUTED and not restored
```

`P16-SIZE-001` recorded the gap as a carried limitation and handed it over by
name:

> **no document object for the controls** — Canonical intent is a value passed
> to the mesher. A MeshControl document object is ADR-030's design and
> **P16-CMD-001/P16-PERSIST-001's to build**; finding F6 remains open.

**So the milestone is NOT blocked.** Building the canonical object is its
foundation, it was designed before it was deferred, and it was deferred *to
here*. This is the opposite of the `P16-VIZ-001` situation, where the missing
piece — a viewport — had been decided by nobody and belonged to no milestone.

```text
NOTE, and not this milestone's to fix: "finding F6" in P16-SIZE-001's README
points at a finding that does not exist in its evidence -- that directory
records findings 1 to 4 in AUDIT.md and 1 to 4 in ADVERSARIAL_REVIEW.md, and
nothing numbered 6. A dangling reference in a qualified milestone's document,
recorded here rather than silently repaired.
```

## 3. What comes for free, and what must be written

`DocumentObject` already provides everything ADR-030 said would come for free:

```text
id()             an ObjectId
revision()       a PER-OBJECT revision, bumped by Document::modifyObject
                 ONLY when the mutation reports a change
clone()          value semantics, which is what undo needs
contentEquals()  semantic equality, which is what "exact undo" means
dependencies()   the dependency-graph hook
typeName()       dispatch and diagnostics
```

and the generic commands already handle the lifecycle:

```text
AddObjectCommand       "redo re-inserts it with the same ID"
DeleteObjectCommand    "undo restores it with the same ID and name", and it
                       restores the configuration overrides too (P13-CMD-001)
RenameObjectCommand    works on any object
```

`Document::modifyObject<T>` type-checks with a `dynamic_cast`, refuses a
mismatch with a diagnostic naming the actual type, and **bumps the object and
document revisions only when the mutation returns `true`** — which is the
no-op contract this milestone needs, already implemented.

```text
FREE        create, delete, rename, ID preservation on redo, per-object
            revision, dependency-graph membership, no-op suppression
TO BUILD    MeshControlId; the MeshControl document object; commands for the
            edits nothing generic covers (global sizing, local sizing
            add/edit/remove, boundary sets); and a Mesher service so that
            "the mesh is stale" is core state rather than a GUI flag
```

The last one matters for a specific reason. P16-VIZ-001 derives mesh currency
by comparing a `GeometryRevision` — so it notices a **geometry** change and
would *not* notice a sizing change. The brief requires invalidation to
*"originate from canonical intent revision"* and forbids a UI-owned
`markMeshStale()`. A `Mesher` keyed by control, holding the control revision it
built from, is ADR-030's design and is what makes that true.

## 4. The command authority boundary

```text
CANONICAL HISTORY MAY HOLD
    a MeshControlId and a stable local-control identity
    a target body's ObjectId
    canonical Length / Angle / dimensionless values
    FaceName and other geometry references -- INTENT, not resolution
    boundary-set identities and names
    a before/after pair of one small value object

CANONICAL HISTORY MUST NEVER HOLD
    VolumeMesh, EngineeringSurfaceMesh, meshing::Mesh
    Node or Tetrahedron or Triangle arrays
    NodeId or ElementId, singly or in sets
    boundary FACET identities (ElementId) -- they are remesh-scoped
    a MeshQualityReport or a GeometryMeshMap
    a renderer MeshView, a render index, or an AIS handle
    a Netgen handle or backend marker
    the Document itself
```

The distinction that matters most, because it is the one a GUI gets wrong: a
user may start a sizing edit by clicking a **mesh facet**. The UI must resolve
that facet to a `FaceName` through `P16-MAP-001` *before* the command is
created, and the command stores the `FaceName`. A `BoundaryFacetRef` or a
render triangle index would be intent that expires at the next remesh.

## 5. Persistence boundary, found while auditing

`io/json/DocumentJson.cpp`'s `objectToJson` dispatches over an explicit
`dynamic_cast` chain and ends:

```cpp
return makeError(ErrorCode::InvalidArgument,
                 std::format("objects of type '{}' cannot be saved", object.typeName()));
```

So **a document holding a `MeshControl` cannot be saved until
`P16-PERSIST-001` adds its serializer.** That is a refusal with a diagnostic
naming the type, not silent data loss, and it is the correct behaviour while
the schema is a later milestone's decision. It is pinned by a test here so the
boundary is explicit rather than discovered.

## 6. Scope

```text
IN    MeshControlId, and MeshControl as a document object holding only intent
IN    commands: create, delete, set global sizing, add/edit/remove local
      sizing, add/edit/remove named boundary set
IN    a Mesher service holding derived meshes keyed by control, with currency
      derived from the control's revision AND the geometry revision
IN    P16-VIZ's visual state reading that core currency instead of a GUI flag
OUT   persistence of the control (P16-PERSIST-001) -- and a test pinning that
      saving refuses until then
OUT   a CLI surface for meshing (P16-CLI-001)
OUT   making mesh generation itself a command: the brief says not to introduce
      that without architecture authorization, and ADR-030 makes the generated
      mesh derived state owned by a service, which is the opposite of history
OUT   command coalescing: no merge/compaction exists in the framework, and the
      brief forbids inventing one
```

## Revision

First issue, 2026-10-04.
