# P18-DOF-001 — source binding and invalidation

```text
mechanism   every map carries the mesh's MeshStamp {MeshId, generation};
            ThermalDofMap::describes(mesh) asks Mesh::owns(stamp) AND
            compares the node count; the constraint and free maps carry the
            stamp and the unknown count and are refused against a numbering
            that differs in either
counts      never identity: same Nnode, same Ntet, same NodeIds, different
            stamp -> stale
```

## The hard gate, measured

`ThermalDof_SameCountsAndSameIdsNewMeshIsStale` (hand-built) and
`ThermalDof_RemeshInvalidatesOldMap` (the real mesher):

```text
M1 source        MeshStamp of the first build
D1 source        == M1
M2 source        a new MeshId: the same body, the same settings, remeshed

same counts?     YES   9 nodes, 12 tetrahedra either way (pipeline);
                       4 nodes (hand-built)
same NodeIds?    YES   asserted element for element before the stamps are
                       compared, so the test cannot pass by the handles
                       happening to differ
D1 valid for M2? NO    describes(M2) false; D1 != D2 although every number
                       agrees; buildThermalFreeEquationMap(D2, C1) refused
                       (FailedPrecondition); F1.agreesWith(D2) false
recovery         D2 = buildThermalDofMap(M2); constraints resolved again
                 against D2
```

Two snapshots of one builder share a stamp and differ in count
(`ThermalDof_BindsExactMeshSource`): the count check in `describes()` and in
`buildThermalFreeEquationMap` closes that gap, and a constraint map built on
the larger snapshot is refused against the smaller numbering rather than
accepted as "every unknown free" -- the defect P17-DOF-001 found in itself,
tested here from the start (`ThermalDof_FreeMapRejectsDifferentDofSource`).

## The invalidation table

| Mutation | P16 mesh current? | `ThermalDofMap` current? | Thermal result current? |
| --- | --- | --- | --- |
| remesh | new generation | **NO** (stamp) | no |
| material edit, k 45 → 50 W/(m K) | **yes** | **YES**, and equal to a rebuild | **no** (reason `material`) |
| thermal load edit (flux added) | yes | **YES** | no (reason `analysis`) |
| solver-settings edit | yes | **YES** | no (reason `analysis`) |
| thermal BC edit (prescribed temperature added to the definition) | yes | **YES** for the base map | no (reason `analysis`) |

Then separately, at the constraint layer (synthetic constraints, the seam
P18-BC-001 will feed):

| Change to the resolved constraints | old `ThermalConstraintMap` | old `ThermalFreeEquationMap` |
| --- | --- | --- |
| which unknowns are prescribed ({1,4} → {1,2}) | different value | `agreesWith(new)` **false**; a rebuild differs |
| only the prescribed VALUES ({1:300,4:350} → {1:320,4:380}) | different value | `agreesWith(new)` **true** -- same free topology |

The second row is the dependency warning the brief asks to be recorded: the
free topology is unchanged when only values change, but the reduced
right-hand side `Qf - Kfc Tc` is not. No assembled or solved system may be
called current on the strength of a matching free map; P18-ASSEMBLY-001 and
P18-SOLVE-001 bind to the constraint map's VALUE as well. Written in the
`ThermalFreeEquationMap` header where the map is defined.

Tests: `ThermalDof_MaterialEditKeepsDofMapCurrent`,
`ThermalDof_LoadSolverAndBoundaryConditionEditsKeepDofMapCurrent`,
`ThermalDof_AChangedConstraintSetStalesTheFreeMapButNotTheDofMap`.

## Provenance tables

`ThermalDofMap`:

| Source dependency | Included? |
| --- | --- |
| P16 mesh source (stamp + node count) | **YES** |
| material revision | NO |
| thermal load revision | NO |
| thermal BC revision | NO |
| solver settings | NO |
| initial condition | NO |
| analysis identity | NO |

`ThermalConstraintMap`:

| Source dependency | Included? |
| --- | --- |
| P16 mesh / DOF source (stamp + unknown count) | **YES** |
| the resolved prescribed temperatures (P18-BC-001 will supply them from the canonical BCs) | **YES**, as its content |
| material, loads, solver settings | NO |

`ThermalFreeEquationMap`:

| Source dependency | Included? |
| --- | --- |
| `ThermalDofMap` source (stamp + unknown count) | **YES** |
| constraint topology (which unknowns) | **YES**, re-derived by `agreesWith` |
| constraint values | NO for the free topology; see the warning above |
| anything else | NO |

## Nothing cached, nothing global

A map is built by one O(N) copy; there is no cache and no static. Two
documents whose meshes carry the same NodeIds give two maps, each describing
only its own mesh (`ThermalDof_MultipleDocumentsHaveIndependentMaps`). A
rigidly transformed or regenerated body produces a new P16 mesh and a new
stamp, and gets its own map; nothing matches coordinates or reuses a handle
across generations.
