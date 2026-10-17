# P18-DOF-001 — sparse node handles

```text
SUBJECT:  the case the design exists for: a mesh whose NodeIds are nothing
          like 0 .. N-1, numbered densely, with storage proportional to the
          node count and not to the largest handle
```

## The mapping table, by hand

`ThermalDof_SparseNodeIds`, on a four-node mesh built through `MeshBuilder`
with the brief's handles:

| Mesh ordinal | NodeId | ThermalDofIndex | `DofIndex == NodeId`? |
| --- | --- | --- | --- |
| 0 | 7 | 0 | no |
| 1 | 104 | 1 | no |
| 2 | 9001 | 2 | no |
| 3 | 500000 | 3 | no |

Every handle between them (1, 8, 103, 105, 9000, 499999, 500001) resolves to
`NotFound`: no clamp, no insert, no nearest node. The invalid handle resolves
to `NotFound` as well.

`ThermalDof_NodeIdIsNotRawEquationNumber` adds the two directions a raw
index would get wrong: `temperature[7]` would read past a four-entry array,
and `NodeId 2` -- a valid-looking index -- is not a node of the mesh while
`ThermalDofIndex 2` is an unknown of it.

## Memory

`ThermalDof_LargeSparseNodeIdDoesNotControlStorageSize`:

```text
Nnodes                       2
maximum NodeId               4 294 967 294   (2^32 - 2)
DOF-map storage              2 handles, 8 bytes
DOF-map cardinality          dofCount() = 2
allocation by max NodeId     would be 17 GB of handles; does not happen
```

The map holds one `std::vector<NodeId>` of exactly `nodeCount()` entries;
the forward lookup is a binary search over it and the inverse is an index
into it. Nothing is sized by a handle's value. Mutation D02 ("dofCount =
max NodeId + 1") is killed by this test and by `OneScalarDofPerNode`.

## Order

P16's order is ascending `NodeId`, enforced at construction: `MeshBuilder`
refuses a handle that is not greater than the last
(`ThermalDof_OrdinalsFollowTheMeshOwnNodeEnumeration` asserts the refusal,
`AlreadyExists`). So "permuted insertion order" cannot produce a mesh whose
storage order differs from numeric order, and the map's order is the mesh's
enumeration, copied element for element and asserted equal to it. A sort by
`NodeId` would give the same answer today and would be a second definition
of the order; the module does not sort. Mutation D03 (reverse the copied
order) is killed.

## Element-local unknowns on sparse handles

`ThermalDof_TetLocalDofsPreserveNodeOrder`: a tetrahedron on the sparse
mesh with local node order `[9001, 7, 500000, 104]`:

```text
local Tet node order        9001   7   500000   104
local thermal DOF order        2   0        3     1     (by hand)
sorted form, NOT produced      0   1        2     3
```

`elementDofs` preserves the element's own order, exactly; mutation D16 (sort
the four) is killed. `ThermalDof_SharedNodeUsesSameDof`: two tetrahedra
sharing nodes 5, 9 and 14 receive the same three unknowns in both, at the
local positions each element gives them; five nodes give five unknowns.
Mutation D17 (per-element numbering) is killed. A tetrahedron record naming
a foreign node is refused with `NotFound` naming the node, and
`dofCount()` is unchanged: nothing is created.
