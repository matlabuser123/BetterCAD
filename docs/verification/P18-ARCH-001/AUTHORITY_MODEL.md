# P18-ARCH-001 — authority, currentness and invalidation

```text
SUBJECT:  who owns what, what must be current before a thermal solve, and
          what a change invalidates
DECISIONS: ADR-051 (the input boundary), ADR-049 (scope), ADR-050 (the
          layer), and ADR-028 / ADR-030 / ADR-032 / ADR-036 inherited
```

## The pipeline, with the authority of each stage

```text
authoritative CAD model                     AUTHORITY, owned by core/features
  -> regenerated authoritative geometry     derived, P16's eligibility boundary
+ P15 canonical material definition         AUTHORITY, owned by core/materials
+ P16 current validated Tet4 mesh           DERIVED, held by Mesher
+ P16 geometry/mesh mapping                 DERIVED, held with the mesh
+ P18 analysis / load / BC / initial-       AUTHORITY, owned by thermal
  condition / solver / time intent          (types arrive in P18-DATA-001)
  -> thermal analysis input                  the validated snapshot, ADR-051
  -> thermal DOF numbering, Ke, Ce, K, C, Q  DERIVED, later milestones
  -> T, grad(T), q, heat balance             DERIVED, never persisted as
                                             authority
```

## Authority matrix

| Data | Canonical? | Owner | May P18 mutate it? |
| --- | --- | --- | --- |
| CAD geometry, features, sketches | Yes | `core`, `features` | **No** |
| Material definition and properties | Yes | `core/materials` | **No.** Only through P15's own commands; ADR-028 forbids a second material store |
| Meshing controls | Yes | `meshing` | **No.** Only through P16's commands |
| Volume mesh, map, quality report | No, derived | `meshing`, held by `Mesher` | **No.** Not even to repair |
| Thermal analysis definition | Yes | `thermal` | Yes, through its own commands (`P18-CMD-001`) |
| Loads, BCs, initial condition, steady and transient settings | Yes | `thermal` | Yes, as above |
| Thermal DOF map, `K`, `C`, `Q` | No, derived | `thermal` | Rebuilt, never persisted |
| `T`, `grad(T)`, `q`, heat-balance report | No, derived | `thermal` | Rebuilt, never persisted as authority |
| A structural result | No, derived | `structural` | **Not reachable** from thermal (ADR-050); a coupling reads thermal from structural, never the reverse |

**Analysis intent is canonical, the solution is not.** A saved document
carries what the user asked for and regenerates the rest -- the byte-identity
test P18-PERSIST-001 names ("save before thermal solve == save after") is the
executable form of this row.

## Currentness: what must be true before a solve

Checked at one boundary, `requireThermalModel`, and each check delegates to
the phase that owns the question (ADR-051).

```text
| REQUIREMENT                  | ASKED OF                        | OWNER |
| the control exists           | meshing::findMeshControl        | P16   |
| geometry is eligible         | meshing::geometryIneligibility  | P16   |
|   regenerated, not failed,   |   -- including                  |       |
|   not blocked, not stale,    |   ConfigurationOverrideActive   |       |
|   a solid, non-empty, valid  |                                 |       |
| a mesh describes the model   | Mesher::currency +              | P16   |
|                              | describesTheModel               |       |
| the mesh's data is valid     | NOT re-derived -- P16 refuses   | P16   |
|                              | to HOLD an invalid mesh         |       |
| the map belongs to the mesh  | one lookup, one Held struct     | P16   |
| a material resolves          | requireEffectiveMaterial        | P15   |
| k is usable                  | requireThermalConductivity      | P15   |
```

**Nothing in that list is implemented in P18.** The thermal module does not
own a second opinion about geometry currency, mesh validity, or whether a
conductivity is positive. It owns the decision to ask.

### What the boundary deliberately does NOT ask

```text
rho and cp for a transient     P18-MAT-001, per analysis MODE -- a material
                               with only k is a good steady conductor
mesh quality                   P18-VALID-001, staged after the boundary
loads and BCs resolve          P18-LOAD-001, P18-BC-001, staged
a temperature datum exists     P18-SOLVE-001, staged
dt > 0, times ordered          P18-TRANSIENT-001, staged
```

P17 forecast that such gates would be added to its boundary and then put
them in a staged validation entry instead (ADR-042; ADR-036 amended). P18
adopts the staged shape from the start: possession proves the input, the
validation entry judges it, the one solve entry runs both.

### Why geometry is asked first, and configuration inherited

Unchanged from P17-ARCH-001: a boundary that asked the mesh first would, for
a deleted body, report a perfectly healthy mesh; and the carried configuration
defect reaches a thermal solve as P16's refusal, by reusing the check, so it
stops being refused in one place on the day the defect is fixed.

## Invalidation matrix

| Change | Geometry | Mesh | Thermal result |
| --- | --- | --- | --- |
| CAD geometry edit, not yet regenerated | stale | stale (`StaleGeometry`) | stale |
| CAD geometry edit, then regenerated | current | **still** `StaleGeometry` | stale |
| Mesh size or deflection edit | unchanged | `StaleIntent` | stale |
| Remesh | unchanged | `Current`, new `MeshStamp` | stale, new handles |
| Boundary-set rename | unchanged | `Current` | current |
| `k` edit | unchanged | **current** | stale |
| `rho` or `cp` edit | unchanged | current | stale for a TRANSIENT result; a steady result is stale too, because the material revision moved and the result does not know which property changed -- conservative, and recorded |
| Load, BC, initial-condition edit | unchanged | current | stale |
| Time step, end time, output times | unchanged | current | stale -- time settings are canonical intent (the brief's "transient time-step changes must stale the old result") |
| Solver setting with semantic effect | unchanged | current | stale |
| Display mode, contour channel, playback time | unchanged | unchanged | current |

Three rows are worth saying out loud.

**A conductivity edit invalidates the result and NOT the mesh**, for the same
reason a modulus edit does: `k` is in neither the geometry nor the meshing
intent.

**A `rho` or `cp` edit stales a steady result too**, although steady
conduction does not read them. The alternative -- a result that records which
material properties it read and compares per property -- is precision that
P17 did not need either, and a wrong answer in that direction costs one
unneeded re-solve. Recorded here so P18-DATA-001 chooses it knowingly rather
than inherits it silently.

**Time settings are intent.** A transient result is a function of `dt`, the
end time and the output times as much as of the loads; they live on the
definition, their edit moves the analysis revision, and the old result is
stale. The brief names this attack explicitly.

## Result currentness, for P18-DATA-001

A thermal result is current only if ALL of these still match what it was
computed from -- the same list as P17's, with time settings added:

```text
geometry revision              meshing::geometryRevision(document, body)
mesh stamp                     VolumeMesh::mesh().stamp()
material id and revision       the P15 material's own
analysis id and revision       P18 canonical intent: loads, BCs, initial
                               condition, mode, solver and time settings
```

A result carries the identity of the mesh it was computed on, so it can never
be displayed over a different one, and a transient result carries its time
axis as derived data keyed to the same stamp.

## Mesh handles in a result: permitted, and why

As ADR-031 allows and P17 practises: derived arrays belonging to ONE mesh
revision may be indexed by `NodeId` and `ElementId`, and are discarded with
it. Forbidden: a handle in a `.bcad` file, in a `MeshControl`, in a command,
in a `FaceName`, or as the identity of a load or a boundary condition. A heat
flux names a CAD face and resolves to facets now; after a remesh the same
intent resolves to different handles and an old result is stale rather than
rebindable.

## The quality boundary

P16 measures; P18 judges, through `P18-VALID-001`, exactly as P17-VALID-001
judged for structural -- reading `summaries`, never `satisfiesPolicy()`.
Whether the thermal thresholds equal the structural ones is that milestone's
measurement: a thermal gradient is first-order in the same shape functions,
so the accuracy laws are the same kind, but the numbers are not inherited
without being re-measured.

## Diagnostics, units and determinism, fixed now

```text
diagnostics    every refusal names the reason as an enumerator a caller can
               switch on (InputProblem now; validation codes later) AND a
               message naming the object; "cannot solve" is never the whole
               answer
units          SI throughout the module: K, W, W/m^2, W/m^3, W/(m K),
               J/(kg K), K/m. Celsius is a boundary conversion, never a type.
               The m -> mm conversion happens once, in the renderer adapter,
               as P17 did
ordering       nodes in the mesh's own enumeration; elements in ascending
               ElementId; loads and BCs in the user's order, which does not
               affect the answer; findings by severity, code, element; no
               unordered container anywhere in the module
```

## What this milestone does not decide

```text
the canonical analysis, load, BC, initial-condition and time types   P18-DATA-001
the per-mode material requirements                                   P18-MAT-001
the DOF numbering                                                    P18-DOF-001
the element formulation and the capacity policy                      P18-ELEM-001
the sign conventions                                                 P18-LOAD-001, P18-BC-001
the sparse representation and the solver                             P18-ASSEMBLY-001, P18-SOLVE-001
the time integration details                                         P18-TRANSIENT-001
the acceptance thresholds                                            P18-VALID-001
```

Each is named rather than guessed at, and the input boundary is where the
validation entry that judges them will attach.
