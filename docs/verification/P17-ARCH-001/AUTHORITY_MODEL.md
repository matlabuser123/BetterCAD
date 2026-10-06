# P17-ARCH-001 — authority, currentness and invalidation

```text
SUBJECT:  who owns what, what must be current before a solve, and what a change
          invalidates
DECISIONS: ADR-036 (the input boundary), ADR-034 (scope), and ADR-028/ADR-030/
          ADR-032 inherited unchanged
```

## The pipeline, with the authority of each stage

```text
authoritative CAD model                     AUTHORITY, owned by core/features
  -> regenerated authoritative geometry     derived, P16's eligibility boundary
+ P15 canonical material definition         AUTHORITY, owned by core/materials
+ P16 current validated Tet4 mesh           DERIVED, held by Mesher
+ P16 geometry/mesh mapping                 DERIVED, held with the mesh
+ P17 load / restraint / solver intent      AUTHORITY, owned by structural
                                            (types arrive in P17-DATA-001)
  -> structural analysis input               the validated snapshot, ADR-036
  -> DOF numbering, Tet4 matrices, K / F     DERIVED, later milestones
  -> displacement, strain, stress, reaction  DERIVED, never persisted as
                                             authority
```

## Authority matrix

| Data | Canonical? | Owner | May P17 mutate it? |
| --- | --- | --- | --- |
| CAD geometry, features, sketches | Yes | `core`, `features` | **No.** A deformed display is a visual transform of a result, never a change to a vertex |
| Material definition and properties | Yes | `core/materials` | **No.** Only through P15's own commands. ADR-028 forbids a second material store |
| Meshing controls (`MeshControl`) | Yes | `meshing` | **No.** Only through P16's commands. A solver that edited a size to get a better answer would be changing the user's intent |
| Volume mesh, geometry/mesh map, quality report | No — derived | `meshing`, held by `Mesher` | **No.** Not even to repair. P16 is the mesh authority |
| Structural analysis definition | Yes | `structural` | Yes, through its own commands (`P17-CMD-001`) |
| Loads, restraints, solver settings | Yes | `structural` | Yes, as above |
| DOF numbering, `K`, `F` | No — derived | `structural` | Rebuilt, never persisted |
| Displacement, strain, stress, reactions | No — derived | `structural` | Rebuilt, never persisted as authority |

The two halves of P17 are deliberately different: **analysis intent is
canonical, the solution is not.** A saved document carries what the user asked
for and regenerates the rest.

## Currentness: what must be true before a solve

Checked at one boundary, `requireStructuralModel`, and each check delegates to
the phase that owns the question (ADR-036).

```text
| REQUIREMENT                  | ASKED OF                        | OWNER |
| the control exists           | meshing::findMeshControl        | P16   |
| geometry is eligible         | meshing::geometryIneligibility  | P16   |
|   regenerated, not failed,   |   -- ten reasons, including     |       |
|   not blocked, not stale,    |   ConfigurationOverrideActive   |       |
|   a solid, non-empty, valid  |                                 |       |
| a mesh describes the model   | Mesher::currency +              | P16   |
|                              | describesTheModel               |       |
| the mesh's data is valid     | NOT re-derived -- P16 refuses   | P16   |
|                              | to HOLD an invalid mesh         |       |
| the map belongs to the mesh  | one lookup, one Held struct     | P16   |
| a material resolves          | requireEffectiveMaterial        | P15   |
| E and nu are usable          | requireLinearElasticConstants   | P15   |
```

**Nothing in that list is implemented in P17.** That is the point: the
structural module does not own a second opinion about geometry currency, mesh
validity, or whether a Poisson ratio is in range. It owns the decision to ask.

### Why geometry is asked first

`requireMeshableGeometry` already argues the ordering, and this boundary copies
it: asking the mesh first, for a body that has been deleted, reports a
perfectly healthy mesh. The honest diagnostic is the one about the model, and it
is only honest if nothing downstream of it has run yet. Proved by
`StructuralInput_RefusesAnIneligibleBodyBeforeLookingAtTheMesh`, which deletes
the body while a `Current` mesh is still held.

### Configuration, inherited rather than restated

The carried configuration defect means an active parameter override can change
the effective parameter without rebuilding the geometry that reads it. P16's
answer is `GeometryIneligibility::ConfigurationOverrideActive` — a refusal.

P17 inherits that refusal **by reusing the check**, not by remembering to make
it. A structural solve of an overridden configuration is refused for the same
reason a mesh of it is, with the same diagnostic, and it will stop being
refused on the day the configuration defect is fixed — in one place.

## Invalidation matrix

What a change makes stale. Written against P16's actual semantics, which
distinguish the two kinds of mesh staleness.

| Change | Geometry | Mesh | Structural result |
| --- | --- | --- | --- |
| CAD geometry edit, not yet regenerated | stale | stale (`StaleGeometry`) | stale |
| CAD geometry edit, then regenerated | current | **still** `StaleGeometry` | stale |
| Mesh size or deflection edit | unchanged | `StaleIntent` | stale |
| Remesh | unchanged | `Current`, new `MeshStamp` | stale — new handles |
| Boundary-set **rename** | unchanged | `Current` | current |
| Quality threshold edit | unchanged | `Current` | current; the classification changes, the mesh does not |
| `E` or `nu` edit | unchanged | **current** | stale |
| Density edit | unchanged | current | stale only if self-weight is in scope |
| Load edit | unchanged | current | stale |
| Restraint edit | unchanged | current | stale |
| Solver setting with semantic effect | unchanged | current | stale |
| CAD display mode, mesh visibility, deformation scale | unchanged | unchanged | current |

Three rows are worth saying out loud.

**A material edit invalidates the result and NOT the mesh.** The mesh is a
function of geometry and meshing intent; `E` is in neither. A structural
architecture that invalidated the mesh on a modulus change would remesh a
hundred thousand elements to answer a question about a number.

**A regenerated geometry edit leaves the mesh stale, permanently.** P16's
`geometryRevision` mixes revision counters, which only move forward, so
restoring the original dimension produces the original solid under a new stamp.
Conservative, and deliberately so: the cost of being wrong this way is one
remesh that was not needed, and the cost of the other way is a result presented
as describing a model it does not.

**A boundary-set rename invalidates nothing.** P16 compares controls **by
value** rather than by the control's revision precisely so that a rename does
not remesh. P17 inherits that precision for free, and must not undo it by
keying result currency on a control's revision instead.

## Result currentness, for P17-DATA-001

A structural result is current only if **all** of these still match what it was
computed from:

```text
geometry revision            meshing::geometryRevision(document, body)
mesh stamp                   VolumeMesh::mesh().stamp()
meshing intent               the VolumeMeshControls it was built from, by value
material revision            the P15 material's own revision
load / restraint revision    P17 canonical intent, per P17-DATA-001
solver settings revision     as above
```

The concrete representation is `P17-DATA-001`'s. What this milestone fixes is
the dependency list, and that a result carries **the identity of the mesh it was
computed on** so it can never be displayed over a different one. `MeshStamp`
already exists for exactly this, and `Mesh::owns(stamp)` is how a holder asks.

## Mesh handles in a result: permitted, and why

A `StructuralResult` may index its arrays by `NodeId` and `ElementId`. That is
not a contradiction of ADR-031 — it is what ADR-031 allows:

```text
PERMITTED   derived arrays belonging to ONE mesh revision, discarded with it
FORBIDDEN   a handle in a .bcad file, in a MeshControl, in a command, in a
            FaceName, or as the identity of a load or a restraint
```

A load names a CAD face by `FaceName` and resolves to facets now (ADR-032). After
a remesh the same intent resolves to different handles, and an old result is
stale rather than rebindable. Stresses are never re-attached to the same numeric
IDs on a new mesh.

## The quality boundary

```text
P16   MEASURES. Every metric, every summary, min/max/mean and the worst element
      per metric. Ships reportOnlyThresholds(), which classifies nothing, and
      says so: "P17 owns what a structural analysis needs from a mesh."
P17   JUDGES. P17-VALID-001 states thresholds and owns the accept/warn/reject
      decision.
```

One consequence is specific enough to be worth recording now:
`MeshQualityReport::satisfiesPolicy()` answers the question the **mesh control**
asked, under those empty thresholds, so it is true for any structurally valid
mesh. A structural acceptance policy must read `summaries` — the measured
numbers — and not that predicate. `StructuralModel::quality()` documents this at
the point of use.

P16 also measured what the metrics can and cannot see, and P17's policy should
start there rather than from intuition: `l_max/l_min` saturates at `sqrt(3)` and
cannot detect a sliver, while the radius ratio collapses three orders of
magnitude on the same element. A policy written on aspect ratio alone would
accept a sliver.

## What this milestone does not decide

```text
the acceptance thresholds themselves            P17-VALID-001
the canonical load, restraint and analysis
  types, and their revisions                    P17-DATA-001, LOAD, BC
the DOF numbering                               P17-DOF-001
the element formulation and Voigt ordering       P17-ELEM-001
the sparse representation and the solver         P17-ASSEMBLY-001, SOLVE-001
whether density is in the initial scope          P17-LOAD-001
```

Each is named rather than guessed at, and the input boundary is where the first
four will attach.
