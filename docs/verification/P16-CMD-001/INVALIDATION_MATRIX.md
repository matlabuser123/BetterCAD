# P16-CMD-001 — the invalidation matrix

```text
SUBJECT:  for each canonical edit, exactly what becomes stale and what does not
DATE:     2026-10-04
```

"Invalidate everything on any edit" would pass a gate and be wrong in
practice: it remeshes a hundred thousand elements because somebody corrected
the spelling of a boundary set. The requirement is **precision** — and
precision means each row below is a decision that had to be made deliberately
and can be checked.

## 1. The matrix

`Mesher::currency` is the authority. It compares, in this order: a recorded
failure; the body the mesh was built from; the `VolumeMeshControls` it was
built from, **by value**; and the body's `GeometryRevision`.

| Canonical edit | Mesh | Quality report | Map | Why |
| --- | --- | --- | --- | --- |
| Global target size | **StaleIntent** | stale with it | stale with it | changes element size everywhere |
| Add / edit / remove local sizing | **StaleIntent** | stale with it | stale with it | changes element size near that face |
| Surface deflection or angle | **StaleIntent** | stale with it | stale with it | the boundary triangulation is the volume mesh's input |
| Body the control meshes | **StaleIntent** | stale with it | stale with it | a mesh of a different solid |
| Boundary set added, renamed, re-selected, removed | Current | Current | Current | a named selection of CAD faces; resolves against the mesh on demand and changes no element |
| Quality threshold policy | Current | **out of date** | Current | a policy classifies a number and can never change the number |
| Control's object name | Current | Current | Current | not read by the mesher at all |
| Geometry (any upstream feature) | **StaleGeometry** | stale with it | stale with it | the mesh describes a solid that no longer exists in that form |
| Control deleted | **StaleIntent**, then `NoMesh` after `forget` | — | — | a mesh of a control that does not exist is meaningless, not stale |
| Generation attempt failed | **GenerationFailed** (old mesh kept, inspectable) | the old one, kept | the old one, kept | "old" and "old AND the replacement failed" are different facts |

### The two rows that required a decision

**A boundary-set edit does not invalidate.** This is why `currency` compares
`VolumeMeshControls` by value rather than comparing the control's own
`revision()`. The revision is the obvious thing to compare and it is wrong
here: it bumps for *any* effective change to the definition, so a rename would
have remeshed. The value comparison asks the question actually being asked.
Tested by `MeshingCommand_ALocalSizeEditInvalidatesAndARenameDoesNot`, which
asserts the document revision *did* move while the currency stayed `Current`,
so it cannot pass by the rename having been a no-op.

**A threshold edit dates the report, not the mesh.** Collapsing these into one
state would remesh for a policy change. They are separate predicates:
`currency` and `qualityDescribesCurrentPolicy`. Tested by
`MeshingCommand_AThresholdEditDatesTheReportAndNotTheMesh`.

## 2. Where the invalidation comes from

```text
canonical intent (MeshControl, a document object)
        |
        | compared by value, by the core
        v
Mesher::currency  ->  MeshCurrency { NoMesh, Current, StaleIntent,
                                     StaleGeometry, GenerationFailed }
```

There is no `markMeshStale()`, no GUI flag and no timestamp anywhere in the
path. `Mesher` holds, beside each mesh, the `ObjectId`, the
`VolumeMeshControls`, the `QualityThresholds` and the `GeometryRevision` it was
built from — so "is this still what the document asks for?" is answered by
comparison, without regenerating anything and without trusting a caller to
have remembered to invalidate.

## 3. Undo and redo move currency, and never a mesh

An undo restores intent. If the restored intent is the intent a held mesh was
built from, that mesh becomes `Current` again **without being regenerated**,
because currency is a comparison and not a stored flag:

```text
mesh at 10 mm              Current
set global size 4 mm       StaleIntent   (the mesh is untouched, still 10 mm)
undo                       Current       (no remesh; the comparison matches again)
redo                       StaleIntent
generate                   Current       (now finer: more elements than at 10 mm)
```

`MeshingCommand_ASizeEditMakesTheHeldMeshStaleAndNothingElse` asserts exactly
this sequence, including that the element count is unchanged across the undo
and strictly greater after the final generate — so the restored intent
demonstrably reached the mesher rather than a cached argument.

## 4. One case where the answer is deliberately conservative

Restoring a geometric parameter to its original value does **not** return the
mesh to `Current`:

```text
mesh a 10 mm extrude       Current
set depth 25 mm            StaleGeometry
set depth 10 mm            StaleGeometry   <-- the same solid, a new stamp
generate                   Current         (and the same element count as the first mesh)
```

`geometryRevision` mixes the **revision counters** of a feature's dependency
sources (P16-GEOM-001), and a counter only moves forward, so the restored
solid carries a stamp it has never carried before.

This is wrong in the safe direction: the cost is one remesh that was not
strictly necessary, against the cost of presenting a mesh as describing a
model it does not. Making it exact would mean content-hashing the B-Rep, which
is neither this milestone's work nor obviously cheaper than the remesh it
would save. Asserted, with its reason, in
`MeshingCommand_AGeometryChangeIsStaleForADifferentReason` — including that
the regenerated mesh has the original element count, which is what makes
"the same solid" a measured claim rather than an assumption.

## 5. What this milestone did not wire up

`renderer::statusOf` (P16-VIZ-001) compares `GeometryRevision`s only. It
therefore cannot see an intent change — and for the meshes it is reachable
from today it does not need to, because the application generates with default
controls and creates no `MeshControl`. The two staleness answers in the tree
are disjoint by reachability, not by agreement:

```text
$ grep -rln "meshing::Mesher" apps src tests
src/meshing/Mesher.cpp
tests/meshing/MeshingCommandTests.cpp

$ grep -rln "CreateMeshControlCommand" apps
(nothing)
```

Wiring the application to a `MeshControl` requires a `CommandHistory` in the
GUI and Undo/Redo actions — a GUI undo feature, which `TODO.md` does not
authorize under this milestone. It is recorded as finding A3 in the
adversarial review and named at the risk site in
`include/bettercad/renderer/MeshInspection.hpp`, so the next caller to
generate from a control cannot take `statusOf`'s answer by accident.
