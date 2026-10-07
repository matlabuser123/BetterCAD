# P17-DOF-001 — DOF Numbering / Constraint Model

```text
STATUS:   PASS
MILESTONE: P17-DOF-001, the fourth milestone of P17 — Structural FEA
SCOPE:    the index space every later P17 milestone assembles against. No
          element stiffness, no load assembly, no restraints from CAD
          geometry, no sparse matrix, no solver.
```

## Baseline

```text
HEAD at start      9e65b5117a6d2dc429df5890a8bdf5888b9556e9
origin/main        9e65b5117a6d2dc429df5890a8bdf5888b9556e9
working tree       clean, 0 porcelain lines
P17-ARCH-001       PASS, 20/20     P17-DATA-001   PASS, 19/19
P17-MAT-001        PASS, 14/14     P16            QUALIFIED
and the eight paths still fingerprinted P17-MAT-001's qualified component
list, so every predecessor was demonstrably qualified on THIS tree
```

## What the audit decided before any code was written

The brief says not to assume `NodeId.value == vector index` unless P16
guarantees it. **P16 guarantees the opposite**, and P17-DATA-001 had already
chosen the alternative:

```text
Mesh::findNode        "BY IDENTITY, with a binary search ... never
                       nodes_[id.value()]. Handles may be sparse (1, 4, 10)"
Mesh                  "nodes are stored and enumerated in ascending NodeId ...
                       Nothing here is an unordered container"
StructuralResult      "displacements DENSE, parallel to mesh.nodes(). Entry i
                       is the displacement of mesh.nodes()[i]"
```

So the node ordinal was **adopted, not invented**. A DOF map with its own order
would make a solver write node i's answer into node j's slot — plausible
numbers, wrong model — and
`MeshDofMap_UsesTheSameNodeOrdinalAsAStructuralResult` compares the two through
the result's **own lookup**, so a future divergence fails the suite rather than
going unnoticed.

`DofComponent`, `kDofComponents`, `DofIndex` and `NodalDof` all already existed.
They are used, not redefined. The only new index type is `FreeEquationIndex`,
which nothing upstream had a reason to name.

Full audit in [DOF_MODEL.md](DOF_MODEL.md).

## What this milestone adds

```text
MeshDofMap        which degrees of freedom EXIST: 3N, interleaved per node,
                  numbered from the mesh's own enumeration, bound to its
                  MeshStamp AND node count
ConstraintSet     which are PRESCRIBED: ascending, unique, order-independent
                  by construction, and carrying no prescribed VALUE
FreeEquationMap   which are UNKNOWN, and which row of the reduced system each
                  is: compact 0 .. Nfree-1
FreeEquationIndex a row, not an identity: zero-based, no invalid value,
                  absence is std::optional
```

[CONSTRAINT_MODEL.md](CONSTRAINT_MODEL.md) and
[ADR-037](../../architecture/decisions/ADR-037-the-solver-index-space-is-derived-from-the-mesh-enumeration-and-interleaved-per-node.md),
which records the four permanent decisions and the eight rejected alternatives.

## The numbering, frozen

```text
    DofIndex value  =  kDofsPerNode * k + c + 1

    node ordinal 0 -> 1, 2, 3        (Ux, Uy, Uz)
    node ordinal k -> 3k+1, 3k+2, 3k+3
```

Interleaved per node, because an element's stiffness couples the degrees of
freedom of **its own four nodes** — twelve indices from four short runs, rather
than three regions `N` apart. Asserted **with its negation**: for a five-node
mesh the second node's Ux is asserted to be 4 and asserted **not** to be 2,
because the two conventions agree at ordinal 0 and a formula check there would
pass under either.

The brief's own sparse fixture is a test: nodes 3, 1000 and 9 000 000 give
**9** degrees of freedom, and the test asserts both the 9 and that the count is
**not** 27 000 003.

## The production defect this milestone found in itself

A `MeshStamp` identifies the **builder, not the snapshot**. `MeshBuilder` sets
it in its constructor and `build()` is documented as a snapshot, so two
snapshots of one builder share a stamp and may differ in size. The first draft
of `buildFreeEquationMap` compared stamps and nothing else:

```text
5-node snapshot and 10-node snapshot, ONE stamp
  constraint set prescribing DofIndex 30, built against the larger
  buildFreeEquationMap(smallMap, set)   ACCEPTED
  freeCount()                           15   <- every DOF free
  constrainedCount()                    0    <- from a set of size 1
```

**A restrained model assembled as an unrestrained one.** Found by reading
`MeshBuilder` during the adversarial review, not by a failing test. Fixed with
a range check on the set's last index and a node-count check in `describes()` —
both O(1), both confirmed load-bearing by mutation probes M9 and M10.
Production cannot reach the ambiguity today, which the header says, and the
check was added anyway because a numbering bindable to the wrong snapshot is
the class of defect this milestone exists to prevent.

[ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md) finding F1.

## Tests

```text
tests/structural/StructuralDofTests.cpp              27 ctest entries
  three DOFs per node, the union exactly 1..3N as a SET
  a node's three are consecutive, and NOT component-blocked
  indexOf and dofAt are exact inverses over EVERY DOF of three fixtures
  sparse handles numbered densely; every absent handle resolves to nothing
  ordinals follow the mesh's own enumeration, element for element
  the same ordinal a StructuralResult uses, via the result's own lookup
  an empty mesh and a mesh with no identity are refused
  a missing node, an invalid handle, an unrecognised component, an
    out-of-range index are each refused
  bound to its mesh; a COPY of a mesh is still the same mesh
  identical over 16 rebuilds
  prescribed DOFs sorted canonically; ALL 24 permutations agree
  a duplicate refused with the DOF named; the same NODE twice is not one
  an empty constraint set accepted
  free equations compact 0..Nfree-1, checked as a set and by exact assignment
  every DOF free xor constrained, cross-checked against the ConstraintSet
  a constrained DOF has NO row, and the first free one owns row 0
  a set from another mesh refused; a set that OUTGROWS the numbering refused
  both extremes: nothing constrained, everything constrained
  a remesh invalidates the numbering and the constraints built on it
  the solver-local types are distinct, and the three maps unconstructible
  the index space cannot overflow, and a 20 000-node mesh is numbered
  the component lookup is cyclic over a flat Tet4 element position

tests/reference/StructuralDofReferenceTests.cpp       4 ctest entries
  RM-MESH-01/03/04/07 numbered and round-tripped: 24, 243, 783 and 183 DOFs
  a named CAD face -> facets -> nodes -> constraints -> rows, and every node
    NOT on the face keeps all three of its DOFs
  a tube's two walls are disjoint; listing one twice is refused
  deterministic across a remesh: same handles, different MeshId, refused

tests/compile_fail/StructuralDofMisuse.cpp            9 cases
  a row is not a DOF, in both directions; not an integer; not a node;
  not default-constructible; and none of the three maps can be fabricated
```

## Zero-match protection

Counted with `-N` **before** every run:

```text
MeshDofMap_|ConstraintSet_|FreeEquationMap_|StructuralDof_   31
compile_fail.structdof                                        9
architecture.                                                14
```

## Mutation protection

```text
PROBES    10
KILLED    10   (9 at run time, M7 at COMPILE time)
SURVIVED   1 on the first pass -- M7, resolved by making the branch reachable
```

Every central claim has a probe behind it: handle arithmetic (M5, killed by
four tests), the one-based numbering (M1), interleaving (M2), the inverse (M6),
remesh binding (M3, M9, M10), duplicate refusal (M4), the empty mesh (M8), the
cyclic lookup (M7). [MUTATION_PROTECTION.md](MUTATION_PROTECTION.md).

## Determinism

No unordered container, no `static` storage, no `mutable`, no `thread_local`,
and — unusually for this codebase — **no floating-point comparison at all**:
the module is integer arithmetic over handles and ordinals, and never reads a
node position. [DETERMINISM.md](DETERMINISM.md).

**Cross-preset equivalence**, which is where a reordered comparison or a folded
constant would show:

```text
[dof]          debug-ext         All tests passed (24771 assertions in 32 cases)
               release-ext       All tests passed (24771 assertions in 32 cases)
               debug-shared-ext  All tests passed (24771 assertions in 32 cases)

[structural]   all three         All tests passed (26762 assertions in 81 cases)
```

**32** cases for 31 new tests: the thirty-second is P17-DATA-001's own
`StructuralData_ADofIndexIsSolverLocalAndNotADocumentIdentity`, which already
carried the `[dof]` tag.

## Adversarial review

```text
QUESTIONS                 29  (25 from the brief, 4 of my own)
FINDINGS                   4
PRODUCTION DEFECTS         1  F1, fixed in this milestone
TEST DEFECTS OF MINE       2  F2, twice in one file: asserting a mesh DENSITY
                              that P16 owns and has measured otherwise
HARNESS DEFECTS            1  F3, a restored source is not a restored binary
GATE-BLOCKING              0
```

## Regression

Three presets, each configured, **cleaned**, rebuilt and run unfiltered, then
two repeat stages. One uninterrupted detached run, 17:37:15 to 20:28:06,
**2 h 50 min 51 s**, 17 stages, `qualify.cmd exit 0`.

```text
PRESET            CONFIGURE  CLEAN  BUILD  NO-OP REBUILD  CTEST
debug-ext              0       0      0         0           0   3493/3493
release-ext            0       0      0         0           0   3493/3493
debug-shared-ext       0       0      0         0           0   3493/3493

REPEAT (5x each of 765 selected tests, back to back)
release-ext            0                            765/765   666.61 s
debug-ext              0                            765/765   704.36 s

warnings, all three clean builds   0   (-Werror and 22 warning flags,
                                        609 objects each)
no-op rebuilds                     0 compiles, 0 links in every preset
shared build                       10 DLLs
```

**3493** is P17-MAT-001's 3453 plus this milestone's 31 unit entries and 9
compile-fail cases; both counts were taken independently and reconciled rather
than one derived from the other.

**609** objects is 605 plus exactly four new translation units, confirmed by
name in the build log: `StructuralDof.cpp`, `StructuralDofTests.cpp`,
`StructuralDofReferenceTests.cpp` and the `StructuralDofMisuse.cpp` control.

**765** repeated tests is 751 plus 14. The inherited P17-MAT-001 filter covered
only **17 of this milestone's 31** tests — `StructuralDof_*` matched
`unit\.Structural` and `MeshDofMap_*` matched `unit\.Mesh` by accident, while
`ConstraintSet_*` and `FreeEquationMap_*` matched nothing. Three terms were
added before the launch and the selection re-counted **from inside**, not by
reading the filter. A determinism stage run on the inherited set would have
passed while claiming a set it had not covered.

## Known limitations

```text
Nothing calls this code yet. There is no stiffness matrix, no load, no
  restraint from CAD geometry, no assembly and no solve, so the numbering's
  only consumers today are its own tests and the reference integration.
  P17-ELEM-001 onwards are not authorized.

A ConstraintSet carries no prescribed VALUE. Homogeneous and inhomogeneous
  restraints partition the unknowns identically, so the numbering does not need
  one; P17-BC-001 owns the restraint payload. A "value conflict" enumerator was
  considered and NOT added, because nothing could return it.

An insufficiently constrained model is NOT detected here. An empty constraint
  set is accepted deliberately: a model with no restraints is a real model with
  a singular stiffness matrix, and the rigid-body modes are visible to
  P17-SOLVE-001, not to a numbering.

MeshDofMap::describes() and buildFreeEquationMap() distinguish two snapshots of
  one MeshBuilder by NODE COUNT, which is sufficient because a builder only
  grows and its handles are strictly increasing. It is not a general snapshot
  identity, and a MeshStamp that named the snapshot would be a better fix --
  but that is P16's type and changing it is not this milestone's.

The reference meshes are small: RM-MESH-01 is 8 nodes, and P16's own sizing
  table records the same 8 at 30, 20 and 10 mm. The scale claim therefore rests
  on a 20 000-node synthetic mesh and on a static_assert over the index-space
  bound, not on a reference model.

This MinGW toolchain has no ASan/UBSan. Inherited and recorded.
```

## The qualified tree is the committed tree

```text
| WHEN                              | WHOLE FINGERPRINT                        |
| frozen, before the first configure | 9301b6431f9802c2f6927dba18f2e192a568a890 |
| recorded by the harness after the  | 9301b6431f9802c2f6927dba18f2e192a568a890 |
|   last test of the last preset     |                                          |
| recomputed before the commit       | 9301b6431f9802c2f6927dba18f2e192a568a890 |
```

Component for component at all three readings:

```text
apps               b49722470580d4fe6c2bb2b3594bb3885eee10db
include            9819575fa997ba59acdfee42542dac960938bd40
src                30569b8bfb94824eecb72605d3b4f08d93f2c4d3
tests              6e13b938ba8b513a12a373f6a1c0eae81d590f45
examples           75ce1c6852e03011f350d6806f4e8d5f3207d63e
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e
```

Exactly three paths moved from P17-MAT-001's, and they are the three this
milestone touches; the other five are byte-identical, which is the check a
milestone that had quietly edited `cmake/` or `CMakePresets.json` would fail.

The whole value is a function of the eight paths **plus the base tree HEAD
pointed at**, which was `9e65b51` throughout. The invariant that survives a
moving HEAD is the component list, checkable against the published tree in one
command:

```bash
git fetch origin && for p in apps include src tests examples cmake CMakeLists.txt CMakePresets.json; do echo "$p $(git rev-parse "origin/main^{tree}:$p")"; done
```

What moved after the freeze: `docs/verification/P17-DOF-001/`,
`docs/architecture/decisions/ADR-037-*.md`, `TODO.md`, `ROADMAP.md` and
`README.md` — all documentation, none inside the fingerprint, none configured,
compiled, linked or read by a test.

[FREEZE.md](FREEZE.md) records the pre-freeze checks and the harness
provenance: `qualify.cmd` is byte-identical at
`d313a64070718c44fae290ac042fe259d1a03c8b`, unchanged since P16-SIZE-001 and
now fourteen milestones in a row, hashed against the previous milestone's copy
rather than assumed.

## Result

```text
RESULT:   PASS
TESTS:    3493/3493 in debug-ext, release-ext and debug-shared-ext, each from
          clean; 0 warnings over 609 objects; 765 x 5 repeats in two presets;
          17 stages, 0 failed
MUTATION: 10 probes, 10 killed
TREE:     9301b6431f9802c2f6927dba18f2e192a568a890, identical at all three
          readings
EVIDENCE: this directory, plus ADR-037
```

## Revision

First issue, 2026-10-07.
