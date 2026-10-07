# P17-DOF-001 — the degree-of-freedom model

```text
SUBJECT:  which degrees of freedom exist, how they are numbered, and why the
          numbering is the one it is
```

## The P16 audit that decided the design

The brief says not to assume `NodeId.value == vector index` unless P16
guarantees it. P16 guarantees the opposite, in as many words:

```text
Mesh::findNode     "Resolution is BY IDENTITY, with a binary search over the
                    ascending node storage -- never nodes_[id.value()].
                    Handles may be sparse (1, 4, 10), so indexing by value
                    would silently resolve to the wrong node"

MeshBuilder        "HANDLES ARE STRICTLY INCREASING. A caller may let the
                    builder allocate, or supply a handle that is greater than
                    the last one of its kind ... gaps are allowed, so a
                    generator that skips numbers is representable"

Mesh               "ORDERING IS DEFINED AND DETERMINISTIC. Nodes are stored and
                    enumerated in ascending NodeId ... Nothing here is an
                    unordered container, so the same construction gives the
                    same enumeration in Debug, Release and Debug-shared."

NodeId             32-bit, 0 is invalid, valid handles start at 1
```

So the two facts this milestone needs are both already true and both already
qualified: the enumeration is **dense, ascending and deterministic**, and the
handles are **not**. The numbering therefore comes from the enumeration and
never from the handle.

`dof = 3 * nodeId.value() + component` is forbidden for a concrete reason, with
the brief's own numbers: a mesh whose nodes are 3, 1000 and 9000000 has **9**
degrees of freedom, and handle arithmetic would give a system with **27 000
003** rows of which nine were used. Measured in
`MeshDofMap_NumbersSparseNodeHandlesDenselyRatherThanByHandleValue`, which
asserts both the 9 and — explicitly — that the count is not 27 000 003.

## The ordinal was already decided, by P17-DATA-001

This is the finding that shaped the milestone. `StructuralResult` documents its
displacement array:

```text
displacements   DENSE, parallel to mesh.nodes(). Entry i is the displacement of
                mesh.nodes()[i], whose NodeId is mesh.nodes()[i].id.

NOT INDEXED BY RAW NodeId, and that is deliberate. P16 allocates node IDs from
1 and MeshBuilder::addNode(NodeId, ...) lets a caller choose them, so nothing
guarantees they are 0..N-1.
```

A DOF map that ordered its nodes any other way would make a solver write node
`i`'s answer into node `j`'s slot — and the numbers would be plausible, which is
the worst kind of wrong. So the node ordinal is **not a new convention**: it is
`StructuralResult`'s, adopted.

That is asserted rather than asserted-in-prose, in
`MeshDofMap_UsesTheSameNodeOrdinalAsAStructuralResult`: a real meshed block, a
real `StructuralResult` whose entry `i` carries the number `i`, and then for
every ordinal

```text
result->displacementOf(volume, map.nodeAt(ordinal)).x == ordinal
map.dofAt(3 * ordinal + 1)->node == map.nodeAt(ordinal)
```

Two independent APIs, compared. `displacementOf` is the result's own lookup, so
the test does not simply re-index the array it filled.

## The numbering

```text
    DofIndex value  =  kDofsPerNode * k + c + 1

    k   the node's ordinal in mesh.nodes(), 0-based, ascending by NodeId
    c   the component's offset in kDofComponents: 0 Ux, 1 Uy, 2 Uz

    node ordinal 0 -> 1, 2, 3
    node ordinal 1 -> 4, 5, 6
    node ordinal k -> 3k+1, 3k+2, 3k+3
```

**One-based**, because `DofIndex` is. P17-DATA-001 fixed that a
default-constructed one is invalid "rather than pointing at the first
equation", and this milestone inherits the convention rather than inventing a
second.

**Interleaved per node, not blocked by component.** Both are valid and the
choice is permanent, so here is the reason: an element's stiffness contribution
couples the degrees of freedom of its own four nodes, so interleaving draws the
twelve indices it writes from four short runs, while component blocking
scatters them across three regions `N` apart. Assembled bandwidth and
element-loop locality both follow. It is also the convention an
`[ux1 uy1 uz1 ux2 ...]` element vector is written in, which is what
`P17-ELEM-001` will need.

The test asserts the convention **and its negation**:

```text
node ordinal k, Ux == 3k+1,  Uy == 3k+2,  Uz == 3k+3
and, for a 5-node mesh,
  the second node's Ux != 2      <- what component blocking would give
  the second node's Ux == 4
  the fifth  node's Ux == 13
```

Without the second half, a switch to component blocking could satisfy a formula
check on node 0 alone.

`kDofsPerNode` is `kDofComponents.size()`, and `componentAt` / `offsetOf` are
functions **over that array** rather than a second table — so a reordering of
`kDofComponents` cannot leave two definitions disagreeing.

## Three degrees of freedom, and the reason is physical

A node of a Tet4 continuum mesh translates and nothing else: the element's
displacement field is linear in position and its gradient is the strain, so
there is no rotational degree of freedom to carry. Shells and beams would add
them and are explicitly out of P17's scope (ADR-034). `DofComponent` was
already defined by P17-DATA-001 with exactly those three; this milestone uses
it and adds no fourth.

## Overflow: proved, not branched

`DofIndex::ValueType` is 64-bit and `NodeId::ValueType` is 32-bit, and a mesh's
handles are strictly increasing, so a mesh holds at most `2^32 - 1` nodes:

```text
3 * (2^32 - 1) + 1  =  12 884 901 885   <   2^64 - 1
3 * (2^32 - 1)      =  12 884 901 885   >   2^32 - 1
```

The first line is why no overflow check is needed. The second is why `DofIndex`
is 64-bit and not 32 — a fact P17-DATA-001 asserted in a comment and this
milestone turns into an assertion.

Both are `static_assert`s: the header carries the bound beside `MeshDofMap`, and
`StructuralDof_ScalesWithoutOverflowingTheIndexSpace` carries both. A
**compile-time proof rather than a runtime branch nothing could ever take** —
which is the lesson P17-ARCH-001 learned when it found three unreachable enum
values in its own first draft and deleted them.

All DOF arithmetic is done in `DofIndex::ValueType`, widened **before**
multiplying, so the `size_t` side cannot overflow on a 32-bit platform either.

## What a numbering is bound to

`MeshStamp`, carried, and `describes()` asks the mesh's own `owns()` so there is
one definition of a matching stamp.

An audit finding worth recording: `MeshBuilder`'s constructor is

```cpp
MeshBuilder::MeshBuilder(std::uint32_t generation) {
    mesh_.stamp_ = MeshStamp{nextMeshId(), generation};
}
```

and `nextMeshId()` is a process-local atomic counter. `generateVolumeMesh` uses
`MeshBuilder builder;`, so **generation is always 0 in production** and the
`MeshId` is what distinguishes one mesh from the next. The brief's adversarial
case — a remesh with identical node counts and identical numeric NodeIds — is
therefore defeated by the `MeshId`, not by the generation counter. Measured in
`MeshDofMap_IsBoundToTheMeshItWasBuiltFrom` and in
`StructuralDof_IsDeterministicAcrossARemeshOfTheSameReferenceModel`, which
asserts the node handles **are** identical and the stamp is not.

A deliberate non-invalidation, recorded because a reader could expect the
opposite: **copying a mesh keeps its stamp**, so the numbering still describes
the copy. That is `Mesh`'s documented behaviour — "copying a mesh ... is not a
remesh and does not renumber anything" — and the test says so.

## Why `buildMeshDofMap` takes a `Mesh` and not a `StructuralModel`

ADR-036 puts the stale-input gate at the boundary a **solve** is prepared at,
and a numbering is not a solve: it is combinatorics over node identity and
cannot produce a wrong engineering answer by itself. `P17-ASSEMBLY-001` and
`P17-SOLVE-001` will take a `StructuralModel` and a `MeshDofMap` together, and
the stamp binding is what proves the two agree — so the gate stays where it is,
and numbering a bare `Mesh`, which is what the sparse-handle fixtures need,
stays possible. Said out loud in the header so it reads as a decision rather
than an omission.

## The refusals, and that each is reachable

```text
MeshHasNoNodes        MeshBuilder{}.build() -- a valid stamp, no nodes. A
                      numbering of nothing is not a degenerate system but an
                      absent one: 3N == 0 would reach an assembler as a matrix
                      with no rows and solve trivially, reporting success for a
                      model that was never there
MeshHasNoIdentity     a default-constructed Mesh. A numbering built from it
                      could not be bound to it, so a remesh could not be
                      detected
```

Both are reachable only through a hand-assembled `Mesh` — `generateVolumeMesh`
refuses an empty result — and both have a test. Two values, no third:
P17-ARCH-001's lesson about unreachable enumerators was applied deliberately
here, as it was in P17-DATA-001 and P17-MAT-001.

Per-degree-of-freedom refusals:

```text
a node the mesh does not have    NotFound, diagnostic naming "node:99"
an invalid handle                NotFound -- never the first node
a component cast from 7 or 9     InvalidArgument. REACHABLE, because
                                 DofComponent is a uint8_t enum and a switch
                                 with no default falls straight through such a
                                 value into a lookup past the end
an index outside 1 ..= 3N        NotFound, including DofIndex{} and 3N+1
```

## Storage

A `MeshDofMap` **copies** the node handles in — four bytes a node — rather than
borrowing the mesh. `StructuralModel` is move-only precisely because it borrows
and can dangle; this type avoids the hazard instead of guarding it, and is
freely copyable as a result. For a hundred thousand nodes that is 400 KB
against a numbering that cannot outlive its data.
