# P18-BC-001 — remesh and currentness

```text
SUBJECT:  what a prepared boundary is bound to, what makes it stale, what does not,
          and the adversarial cases: same counts, same ids
```

## The source

```text
PreparedThermalBoundarySource {
    body         which solid
    mesh         the MeshStamp: WHICH MESH AND WHICH GENERATION
    nodeCount    a stamp identifies the builder, not the snapshot, so the count too
    geometry     the geometry revision the mapping resolved against
    conditions   the canonical conditions it was prepared FROM, by value
}
```

A VALUE: no pointer, no address, no timestamp, no hash. Equality is field-complete
by `= default`, so a dependency added to the struct without being compared is not
possible, and `staleReasons` can say WHICH field moved (`body`, `mesh`, `geometry`,
`conditions`). The prepared object additionally binds its constraint map to the
numbering's stamp and unknown count (`describes(ThermalDofMap)`) and to the mesh
(`describes(Mesh)`).

## What makes it stale

| Edit | Stale? | Reason reported | Test |
| --- | --- | --- | --- |
| remesh of the same body, same controls | yes | `mesh` | `RemeshInvalidatesPreparedBC` |
| remesh with IDENTICAL node, tetrahedron and boundary-facet counts | **yes** | `mesh` | `SameCountsNewMeshIsStale` |
| remesh with identical numeric `NodeId`s, `ElementId`s and cap facet handles | **yes** | `mesh` | `SameIdsNewMeshIsStale` |
| another solid in the same document | yes | `body`, `mesh` | `ADifferentBodyIsStale` |
| a geometry edit (extrude depth) | yes | `geometry`, `mesh` | `GeometryEditStalesPreparedBC` |
| `T_bar`, `h` or `T_inf` | yes | `conditions` | `EditStalesPreparedBC` |
| the target face | yes | `conditions` | same |
| the condition's type | yes | `conditions` | same |
| a condition added, removed, reordered or renumbered | yes | `conditions` | same |

## What does NOT make it stale

| Edit | Stale? | Why | Test |
| --- | --- | --- | --- |
| the material (conductivity) | **no** | neither a constraint nor a convection term involves `k`; the mesh is not even regenerated | `MaterialEditKeepsPreparedBoundaryMappingCurrent` |
| a heat load | no | a separate canonical axis | `LoadSolverAndTransientEditsDoNotInvalidate` |
| solver tolerance | no | does not enter a boundary term | same |
| analysis mode, initial condition, time step, end time | no | the initial BCs are time-independent | same |

The thermal RESULT does go stale on those, through its own gate
(`ThermalResultSource`, reason `Material` or `Analysis`): two gates, two
dependencies. The material test checks both at once: `k` 45 to 50 W/(m K) leaves
the mesh `Current`, the prepared boundary current and bit-identical when prepared
again, and a result stale for reason `Material` only.

**Policy for the orchestrator.** A prepared boundary depends on the conditions, not
on the whole analysis snapshot; a combined analysis snapshot, if a later
milestone builds one, may be coarser, but may not be finer. The analysis revision
is deliberately NOT the identity: it moves on any edit to the definition and would
stale this value for a load or tolerance edit that cannot change it.

## The same-counts and same-ids cases

A remesh of the same body with the same controls rebuilds the mesh with a new
stamp. In the block fixture the second generation has 9 nodes, 12 tetrahedra and
12 boundary facets, exactly as the first, and every `NodeId`, `ElementId` and cap
facet handle is numerically identical. A prepared boundary of the first is still
stale, and the constraint map is exactly the right dimension for the second. The
test prints it:

```text
same counts: 9 nodes, 12 tets, 12 boundary facets in both generations; constraint map dimension 9 matches M2 and P1 is still stale
same ids: every NodeId, ElementId and cap facet handle identical across the remesh; P1 stale by stamp alone
```

Currentness is the source, never the size. Mutations B43 (stamp omitted) and B44
(counts accepted) are the probes.

## Re-resolution against a genuinely different mesh

Coarse and fine meshes of one cylinder (156 and 900 nodes; 140 and 572 boundary
facets; 34 and 142 facets on the cap), prepared from the SAME canonical
conditions:

- the canonical `FaceName` and every physical number compare equal before and
  after (`RemeshReResolvesGeometryReference`);
- the prescribed temperature maps to a different number of nodes on each mesh
  and every one receives exactly the same `T_bar`, with no averaging and no
  interpolation from the other mesh;
- the convection conductance and source equal `h A` and `h T_inf A` of THEIR OWN
  mesh to a few ulps; against the CAD disc the coarse cap is within P16's
  tolerance and the fine cap is no worse;
- the constant-temperature resultant `h A (T_b - T_inf)` is mesh independent to
  the coarse mesh's own discretisation error;
- neither prepared boundary is current on the other's model.

## Failure

A stale prepared boundary is refused by `requireCurrent` with a
`FailedPrecondition` naming each reason that moved, and a numbering built for
another mesh is refused by preparation itself (`DofMapMismatch`). A consumer calls
`isCurrent` or `requireCurrent` before assembly. There is no assembly yet, so
nothing yet FORCES the call; P18-ASSEMBLY-001 must make it.
