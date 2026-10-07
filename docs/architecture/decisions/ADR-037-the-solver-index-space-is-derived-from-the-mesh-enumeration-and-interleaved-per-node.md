# ADR-037 — The solver's index space is derived from the mesh enumeration, interleaved per node, and a row of the reduced system is a different type

```text
STATUS:    Accepted
DATE:      2026-10-07
MILESTONE: P17-DOF-001
TOUCHES:   ADR-031 (a mesh node is a handle, not an identity),
           ADR-034 (linear-static Tet4 on one solid),
           ADR-036 (an analysis input is validated once)
```

## Context

Everything P17 builds after this point indexes the same space.
`P17-ELEM-001` writes a 12x12 element stiffness matrix whose rows are degrees
of freedom; `P17-ASSEMBLY-001` scatters it into a global matrix;
`P17-SOLVE-001` solves the reduced system; `P17-POST-001` reads displacements
back out by node. If the convention is wrong, or is quietly different in two
places, the result is not a crash — it is a stress field that looks plausible
and is wrong, which is the failure mode this phase is organised against.

So the convention is permanent in a way an ordinary data structure is not, and
reversing it later would be a breaking change to four milestones at once. That
is what puts it here rather than in a comment in one header.

Three things had to be decided, and each had a real alternative.

## Constraints

- **A `NodeId` is not an index.** ADR-031 and `Mesh` are explicit: handles may
  be sparse, and `Mesh::findNode` is a binary search "never
  `nodes_[id.value()]`" because of it.
- **`StructuralResult` already keys its displacement array** "parallel to
  `mesh.nodes()`. Entry i is the displacement of `mesh.nodes()[i]`"
  (P17-DATA-001). Whatever is decided here has to agree with that or a solver
  writes node i's answer into node j's slot.
- **No solver identity may be persisted or survive a remesh.** P17-DATA-001 put
  `DofIndex` outside `core/Id.hpp` for exactly this reason.
- `DofComponent`, `kDofComponents`, `DofIndex` and `NodalDof` already exist.
  This milestone may use them; it may not define rivals.

---

## Decision 1 — the node ordinal is the mesh's own enumeration index

### Candidates

**A. Handle arithmetic.** `dof = kDofsPerNode * nodeId.value() + component`.
The obvious formula, and the one the brief warns about by name.

**B. The mesh's enumeration index** — `k` is the position of the node in
`mesh.nodes()`, which `Mesh` guarantees is dense and ascending by `NodeId`.
**Chosen.**

**C. A sorted copy of the handles, sorted here.** Equivalent to B today, since
`Mesh` is already ascending.

### Comparison

| | A handle arithmetic | B mesh enumeration | C sort here |
| --- | --- | --- | --- |
| correctness on a sparse mesh | **wrong** | correct | correct |
| size of the system | `3 * max(handle)` | `3N` | `3N` |
| agrees with `StructuralResult` | no | **yes, by construction** | by coincidence |
| determinism | yes | yes, P16 guarantees it | yes |
| number of definitions of "the order" | 1 | **1** | **2** |
| cost | O(1) lookup | O(log N) lookup | O(log N) + a sort |

A is rejected on correctness and it is not a hypothetical: a mesh whose nodes
are 3, 1000 and 9 000 000 has **9** degrees of freedom, and A would build a
system with **27 000 003** rows of which nine were used. Nothing the current
backend produces is sparse, which is precisely why the defect would have shipped
— it would have been correct for every mesh anyone tested.

C is rejected for the reason that matters more than cost: it would be a
**second definition of the node order** sitting beside `Mesh`'s. The two would
agree on the day it was written, and the one that later changed would not
announce itself. Copying the mesh's own enumeration means there is one order in
the system, and `StructuralResult`'s array and the DOF numbering are the same
convention rather than two conventions that match.

The agreement with `StructuralResult` is asserted, not assumed:
`MeshDofMap_UsesTheSameNodeOrdinalAsAStructuralResult` compares the DOF map's
ordinal against the result's **own lookup**, so the two are checked through
different APIs and a future divergence fails the suite.

---

## Decision 2 — the numbering is interleaved per node

### Candidates

**A. Interleaved.** `node ordinal k -> 3k+1, 3k+2, 3k+3`. A node's three
degrees of freedom are adjacent. **Chosen.**

**B. Blocked by component.** All the Ux first, then all the Uy, then all the
Uz: `ux of node k -> k+1`, `uy of node k -> N+k+1`.

Both are standard, both are correct, and the choice is permanent.

### Comparison

| | A interleaved | B component-blocked |
| --- | --- | --- |
| indices one element writes | 12 drawn from **4 short runs** | 12 scattered across 3 regions `N` apart |
| assembled bandwidth | follows element connectivity | inflated by `N` |
| element-loop locality | good | poor |
| matches `[ux1 uy1 uz1 ux2 ...]` element vectors | **yes** | no |
| convenience of a component-wise view | needs a stride | contiguous |

A is chosen because an element's stiffness contribution couples the degrees of
freedom of **its own four nodes**, so the access pattern of every assembly and
every element loop is nodal. B's only advantage is a contiguous per-component
slice, and nothing in the authorized scope wants one: a displacement result is
reported per node (`Translation3D`), not per component.

A also matches the ordering a 12-component Tet4 element vector is
conventionally written in, which is what `P17-ELEM-001` will build — so the
same index arithmetic serves both, and `componentAt` is the cyclic lookup over
a flat element position rather than two separate mappings.

The decision is enforced rather than documented:
`MeshDofMap_NumbersANodesThreeDofsConsecutivelyRatherThanInComponentBlocks`
asserts the convention **and its negation** — for a five-node mesh the second
node's Ux is asserted to be 4 and asserted **not** to be 2 — because the two
conventions agree at node ordinal 0 and a formula check there would pass under
either.

---

## Decision 3 — a row of the reduced system is a different type from a degree of freedom

### Candidates

**A. One type.** `DofIndex` names both a degree of freedom and a row of the
reduced system, with the solver remembering which it holds.

**B. Two types, both 1-based identities**, matching every other handle in
BetterCAD, with a `row()` accessor returning `value() - 1`.

**C. Two types, `DofIndex` a 1-based identity and `FreeEquationIndex` a
0-based position with no invalid value.** **Chosen.**

### Comparison

| | A one type | B two, both 1-based | C two, position 0-based |
| --- | --- | --- | --- |
| confusing a DOF with a row | **compiles** | refused | refused |
| caught by a test on an unrestrained model | no — the two agree there | n/a | n/a |
| `- 1` at every assembly site | no | **yes** | no |
| a forgotten `- 1` | n/a | off-by-one **in the stiffness matrix** | impossible |
| representing "this DOF has no row" | a sentinel | a sentinel | **`std::optional`** |
| consistency with BetterCAD handles | yes | yes | **departs, deliberately** |

A is rejected because the confusion it permits is invisible to the obvious
test: for a model with **no restraints** a degree of freedom and its row agree
apart from the one-based offset, so a test on a free-floating body passes while
every restrained model is wrong. The compile-fail case
`dof-index-as-free-equation` exists for that direction specifically.

B is rejected because a `FreeEquationIndex` **is** the row and column an
assembler indexes `K` and `F` with. A 1-based handle puts a `- 1` at every
assembly site, and the one that is forgotten is an off-by-one in the stiffness
matrix — again a plausible wrong answer rather than a failure.

C's departure from the 1-based convention is the point, and it is narrow: a
`DofIndex` is an **identity** and keeps the convention; a `FreeEquationIndex`
is a **position**. Positions are already 0-based in this module —
`StructuralResult` keys its displacement array by a 0-based ordinal into
`mesh.nodes()` — so this names an existing idea rather than introducing one.

Having no invalid value lets absence be `std::optional`, which is strictly
stronger than a sentinel: a constrained degree of freedom has no equation, and
the compiler makes the caller handle it. The default constructor is **deleted**,
because an index defaulting to row 0 would be the sentinel defect by another
route.

---

## Decision 4 — three types, not one

`MeshDofMap` (which degrees of freedom exist), `ConstraintSet` (which are
prescribed), `FreeEquationMap` (which are unknown, and their rows). The
alternative was one `DofSystem` carrying all three.

Rejected for two reasons, neither stylistic. A combined type would be **rebuilt
when a restraint changed although the numbering had not** — and the numbering of
a hundred thousand nodes is the expensive half. And it would let a caller ask
"which equation is this" of an object that had not yet been told what was
constrained: a question with a plausible wrong answer rather than no answer.

Each of the three has a private constructor and exactly one friend, which is
ADR-036's device applied one layer down. What possession proves:

```text
MeshDofMap        the CANONICAL numbering of a mesh that exists
ConstraintSet     validated against one numbering: no duplicate, no
                  out-of-range index, no unknown node
FreeEquationMap   built from a matching pair, so its rows are numbered against
                  a mesh its constraints actually describe
```

## Consequences

- `P17-ELEM-001` writes its 12-component element vector in the order
  `[ux1 uy1 uz1 ux2 ... uz4]` and may use `componentAt` over a flat position.
- `P17-ASSEMBLY-001` sizes the reduced system from `FreeEquationMap::freeCount()`
  and indexes it with `FreeEquationIndex::value()` directly, with no offset.
- `P17-BC-001` produces `NodalDof`s from CAD references and never stores an
  index. A restraint resolved after a remesh gives different indices and the
  same physical restraint, which is the behaviour the whole split is for.
- A remesh invalidates every numbering and every set built on one. The
  refusal is by `MeshStamp`, **plus the node count** — because `MeshBuilder`
  sets its stamp in its constructor and `build()` is a snapshot, so a stamp
  identifies the builder and not the snapshot. That was found during
  P17-DOF-001's adversarial review and is recorded there as F1.
- The index space cannot overflow for any representable mesh, proved by
  `static_assert` rather than guarded by a branch: a `DofIndex` is 64-bit and a
  `NodeId` 32-bit, so `3 * (2^32 - 1) + 1 < 2^64`.

## Rejected, for the record

```text
dof = 3 * nodeId.value() + component        correct for every mesh today and
                                            wrong for the first sparse one
a second sorted copy of the node order      a second definition of the order
component-blocked numbering                 inflates the bandwidth of every
                                            assembled matrix by N
one index type for DOFs and rows            the confusion is invisible on an
                                            unrestrained model
a 1-based FreeEquationIndex                 a forgotten "- 1" is an off-by-one
                                            in the stiffness matrix
a sentinel for "no equation"                std::optional makes the compiler
                                            check it
one combined DofSystem                      recomputes the expensive half when
                                            the cheap half changes
a prescribed VALUE on ConstraintSet         fixes a restraint representation
                                            before P17-BC-001 defines one
```

Evidence: [docs/verification/P17-DOF-001/](../../verification/P17-DOF-001/README.md).
