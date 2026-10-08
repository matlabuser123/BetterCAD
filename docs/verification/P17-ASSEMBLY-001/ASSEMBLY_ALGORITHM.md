# P17-ASSEMBLY-001 — the algorithm, and both frozen orders

## Two passes

```text
SYMBOLIC   walk the elements once; for each of the 12 rows record the 12
           columns it will receive. Sort and unique each row, then lay out the
           CSR. Cost O(nnz)

NUMERIC    walk the elements again; for each (a, b) find the slot and do
           values[slot] += Ke(a, b). ONE slot per (row, column), so a
           duplicate contribution is a sum BY CONSTRUCTION
```

**Why not triplets.** 144 entries per element at 24 bytes each is 3.5 KB per
element — 345 MB of staging for a 100 000-element mesh, before any reduction.
The symbolic pass costs O(nnz) instead, and needs no sort of values at all,
which is also why there is no stable-sort-by-insertion-sequence anywhere: the
accumulation order **is** the traversal order.

## Both orders are frozen, and neither is this module's invention

```text
Element traversal source   model.mesh().mesh().tetrahedra()

                           P16's own contract, from Mesh.hpp: "Nodes are
                           stored and enumerated in ascending NodeId;
                           triangles and tetrahedra each in ascending
                           ElementId [...] Nothing here is an unordered
                           container, so the same construction gives the same
                           enumeration in Debug, Release and Debug-shared."

                           So this milestone writes NO sort for the elements.
                           The order is recorded on the system
                           (`elementOrder()`) and the test asserts it equals
                           the mesh's own and is ascending.

Local row order            0 .. 11
Local column order         0 .. 11
                           Written out as two nested loops, not left to an
                           expression template.

Load traversal source      PreparedLoads::nodal(), which P17-LOAD documents as
                           "the loaded nodes, ascending by handle". The test
                           asserts it is sorted and without repeats.

Node / DOF ordering        P17-DOF's MeshDofMap, through
                           FreeEquationMap::equationOf.
```

Why the order is a numerical contract and not a diagnostic: up to **22**
elements write a single entry on the unit fixture, so a different traversal
order would change the last bits of that sum. Freezing it is what makes the
cross-preset comparison a comparison of equal values rather than of nearly
equal ones.

## No index arithmetic at all

The row of a degree of freedom comes from `FreeEquationMap::equationOf`, which
ADR-037 made a **zero-based position** for exactly this reason:

> `FreeEquationIndex` is a POSITION. It is the row and column number an
> assembler indexes K and F with, so the value that comes out must BE the row
> — a 1-based handle would put a `- 1` at every assembly site, and the one that
> was forgotten would be an off-by-one in the stiffness matrix.

So the global row space is the **free-equation numbering of the EMPTY
constraint set**: with nothing prescribed, every degree of freedom is free,
`freeCount() == dofCount()`, and the free numbering IS the global one.

```text
buildMeshDofMap(mesh)              ->  MeshDofMap
buildConstraintSet(map, {})        ->  an empty ConstraintSet
buildFreeEquationMap(map, empty)   ->  the identity numbering
```

Consequence, verified by search over the implementation:

```text
"- 1" as a row adjustment      0 occurrences
"3 * node"                     only in comments saying there is none
raw NodeId as an index         0 occurrences
```

And the mutation that replaces the lookup with `kDofsPerNode * nodeId + c` —
raw handle arithmetic, which would be correct only for a dense, zero-based,
gap-free handle space — is **killed by 24 of 26 tests**.

## The local Tet4 map, pinned directly

```text
 0,  1,  2    node 0   Ux Uy Uz
 3,  4,  5    node 1   Ux Uy Uz
 6,  7,  8    node 2   Ux Uy Uz
 9, 10, 11    node 3   Ux Uy Uz
```

`elementDegreesOfFreedom` is public for one reason: it is the single place
where the local and global orders meet, so a test can pin that correspondence
directly rather than inferring it from a matrix entry.
`StructuralSystem_MapsLocalTet4DofsOntoTheGlobalNumbering` asks P17-DOF what
index each (node, component) has and checks it is what that local position
holds, for all twelve, and asserts `localDofIndex` agrees — plus five
`static_assert`s beside the definition.

## What is consumed, and what is therefore not written here

```text
Ke                 P17-ELEM-001's computeTet4Kinematics + computeTet4Stiffness
nodal forces       P17-LOAD-001's PreparedLoads::nodal()
DOF numbering      P17-DOF-001's MeshDofMap and FreeEquationMap
element order      P16's Mesh
mesh currency      ADR-036: possession of a StructuralModel
```

Searched over the implementation, every match is prose in a comment:

```text
B matrix, D matrix, detJ, Lame     0 executable occurrences
pressure, traction, facet area     0
density, gravity integration       0
SparseLU / LDLT / CG / BiCGSTAB    0
displacement, strain, stress,
von Mises, reaction                0
```

So there is no second copy of the element formulation, no re-integration of a
surface load, no second density lookup, no solver call and no post-processing.

## Atomic

All working state lives in one local `Working` struct. Nothing touches a
`GlobalStructuralSystem` until every element has been accepted, every load
scattered and every value checked finite; the system is then constructed in one
step through its only (private) constructor.

```text
element E42 fails after 100 elements were inserted  ->  no system is returned
a load names a node the numbering lacks             ->  no system is returned
a non-finite entry                                  ->  no system is returned
```

The mutation that skips an element and carries on is **killed by 11 tests**;
the mutation that drops one local row is **killed by 9**.

## Serial, by design

There is no thread, no atomic and no parallel scatter. Many elements write the
same global entry, so a schedule-dependent summation order would make the last
bits of `K` depend on the machine, and the brief's own gate is "no
race-sensitive assembly".

```text
race-sensitive assembly avoided by design: the assembly is serial.
```

A deterministic parallel merge — thread-local buffers reduced in a fixed order
— is a later performance decision with its own evidence, and it is not needed:
nothing in this milestone claims a performance result.

## Failure model

```text
MeshHasNoDegreesOfFreedom    the mesh has no nodes. Refused by P17-DOF's own
                             numbering
MeshHasNoElements            no tetrahedron, so K would be structurally empty
ElementNodeMissing           an element names a node the mesh does not have
ElementRejected              P17-ELEM refused the element, or the material is
                             unusable. Propagated with the ElementId
LoadSourceMismatch           the prepared loads were built for another mesh
LoadNodeMissing              a prepared load names a node the numbering lacks
NonFiniteSystem              an assembled entry is not finite
```

`structuralAssemblyProblem` and `assembleStructuralSystem` run **one shared
pass**, so a caller that asks which problem there is cannot be told something
different from the caller that asks for the system.

Reachability is recorded rather than implied — see
[ANALYTICAL_VALIDATION.md](ANALYTICAL_VALIDATION.md) for the diagnostics
matrix, including the three values a validated `StructuralModel` makes
unreachable and why they are kept anyway.
