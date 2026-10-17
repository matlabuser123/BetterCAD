# P18-DOF-001 — node ↔ DOF mapping

```text
policy          ThermalDofIndex d  =  ordinal of the node in mesh.nodes()
forward         dofOf(NodeId)          binary search over the ascending handles;
                                       NotFound for an absent or invalid handle
reverse         nodeOf(ThermalDofIndex) index into the handle list; NotFound
                                       past the end
cardinality     dofCount() == nodeCount(); bijective; verified over every
                node of three fixtures in both directions
element         elementDofs(Tetrahedron) the four unknowns in the element's
                                       own node order; NotFound and nothing
                                       created for a foreign node
```

## The core invariant suite, as tested

| Invariant | Test |
| --- | --- |
| `Ndof == Nnode` | `OneScalarDofPerNode`, `NodeToDofAndDofToNodeAreExactInverses` |
| every NodeId of the source represented exactly once | `OneScalarDofPerNode` (set of indices is exactly {0..N-1}) |
| every unknown in `[0, Ndof)` | same |
| `dofOf(nodeOf(d)) == d` for every d | `NodeToDofAndDofToNodeAreExactInverses` |
| `nodeOf(dofOf(n)) == n` for every n | same |
| the order is the mesh's | `OrdinalsFollowTheMeshOwnNodeEnumeration` |
| an absent handle resolves to nothing | `SparseNodeIds`, `RefusesALookupItDoesNotHave` |
| an unknown past the end resolves to nothing | `RefusesALookupItDoesNotHave` |
| one node is numberable | `RefusesAMeshWithNothingToNumber` (the one-node section) |
| no nodes / no identity refused | same |

## Empty and degenerate meshes

```text
Nnode = 0           refused: DofMapProblem::MeshHasNoNodes. A numbering of
                    nothing would reach an assembler as a matrix with no rows
                    and solve trivially; ThermalResult::create refuses the
                    same mesh
no MeshStamp        refused: DofMapProblem::MeshHasNoIdentity. Only a
                    default-constructed Mesh is in this state
Nnode = 1           numbered: Ndof = 1, unknown 0. Indexing correctness is
                    separate from whether the mesh is a valid volume mesh,
                    which P18-VALID-001 judges
nodes not ascending unreachable: MeshBuilder refuses a handle not greater
                    than the last. buildThermalDofMap verifies the ascent
                    while copying and returns an Internal error otherwise --
                    a seam, deliberately not an enumerator and not claimed as
                    tested
```

## Complexity

```text
construction    O(N): one reserve, one copy of N handles
storage         O(N): 4 bytes a node, never sized by a handle's value
dofOf           O(log N) binary search; no hash, no unordered container
nodeOf          O(1) index
elementDofs     four dofOf calls
```

## Relation to the result array

`ThermalResult::temperatures()` (P18-DATA-001) is parallel to
`mesh.nodes()`; a `ThermalDofIndex` is the ordinal into the same enumeration.
So `temperatures()[dof.value()]` is the temperature of the node whose unknown
that is, and `ThermalDof_ResultArrayIsIndexedByThermalDof` asserts it
through the result's own `temperatureOf` for every node and for every
element's four local unknowns -- and asserts that after a remesh of the same
counts, both the old map and the old array are refused against the new mesh.
`temperature[i]` is never interpreted as `NodeId == i`.
