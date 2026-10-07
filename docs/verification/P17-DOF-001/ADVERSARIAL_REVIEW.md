# P17-DOF-001 — adversarial review

```text
SUBJECT:  an attempt to disprove the degree-of-freedom numbering and the
          constraint model before a stiffness matrix is built on them
QUESTIONS: 25 from the brief, plus 4 of the reviewer's own
FINDINGS: 4 -- 1 PRODUCTION DEFECT, 2 test defects of mine, 1 harness defect
PRODUCTION DEFECTS: 1, found and fixed in this milestone
GATE-BLOCKING: 0
```

## Findings

### F1 — a `MeshStamp` identifies the BUILDER, not the snapshot (PRODUCTION DEFECT, FIXED)

The binding check in the first draft of `buildFreeEquationMap` was a stamp
comparison and nothing else, on the reasoning that a stamp names one mesh. It
does not. `MeshBuilder` sets its stamp **in its constructor**:

```cpp
MeshBuilder::MeshBuilder(std::uint32_t generation) {
    mesh_.stamp_ = MeshStamp{nextMeshId(), generation};
}
Mesh MeshBuilder::build() const { return mesh_; }
```

and `build()` is documented as "a snapshot, which is what makes a mesh a value
rather than a handle to a living object". So two snapshots of one builder carry
**the same stamp and different node counts**.

Found by reading `MeshBuilder` while answering the brief's question about
remesh invalidation — not by a failing test. What it let through:

```text
MeshBuilder b;  5 nodes  -> Mesh small   stamp S, 15 DOFs
                10 nodes -> Mesh large   stamp S, 30 DOFs

ConstraintSet over largeMap prescribing DofIndex 30
buildFreeEquationMap(smallMap, that set)
    stamps match          -> ACCEPTED
    the loop over 1..15 never meets index 30
    freeCount()           == 15        <- every DOF free
    constrainedCount()    == 0         <- from a set of size 1
```

**A restrained model assembled as an unrestrained one**, silently. And the
sharper form: a set of 30 constraints against a 15-DOF numbering made the
reservation `dofCount() - constraints.size()` an unsigned subtraction below
zero, which would throw `std::length_error` out of a function whose contract is
to return a `Result`.

Fixed in two places, both O(1):

```text
buildFreeEquationMap   also refuses a set whose LAST index -- the set is
                       ascending -- is not in the numbering's range. This also
                       makes the reservation safe, because a set that is
                       ascending, unique and in range cannot be larger than the
                       range
MeshDofMap::describes   also compares the node count. That closes the same hole
                       at the mesh boundary, and it is sufficient for every pair
                       that can arise: a builder only grows and its handles are
                       strictly increasing, so two snapshots whose nodes differ
                       always differ in count, and two with the same count hold
                       the same nodes
```

**Production cannot reach the ambiguity today** — `generateVolumeMesh` uses a
local `MeshBuilder builder;` and builds once — and that is said in the header so
the check does not read as dead weight. It was not left as a documented
limitation because a numbering that can be bound to the wrong snapshot is
precisely the class of defect this milestone exists to prevent.

Regression: `FreeEquationMap_RefusesAConstraintSetThatOutgrowsTheNumbering`,
four sections, and mutation probes M9 and M10 confirm each half of the fix is
load-bearing.

### F2 — I asserted a mesh density I do not own (TEST DEFECT, FIXED)

`StructuralDof_NumbersTheReferenceMeshesAndScalesToThem` asserted
`map.nodeCount() > 100` for RM-MESH-01. It failed: **8**.

A 120 x 70 x 35 mm block is tetrahedralised on its corners alone, and P16's own
committed RM-MESH-01 sizing table records the same 8 nodes and 6 tetrahedra at
30, 20 **and** 10 mm, because a global target is an upper bound and does not
subdivide a planar box interior. So the assertion was about P16's qualified
behaviour, not about this milestone's.

The second draft then did it **again**: a "refined" section asserting a 6 mm
target would give more than 8 nodes. Also 8. Twice in one file, which is why it
is recorded as a finding rather than a typo.

What replaced it: the measured counts, stated (24, 243, 783 and 183 degrees of
freedom over the four models, every one round-tripped), and a section that
asserts the property this milestone **is** entitled to — that the numbering is
a pure function of the mesh at three sizing levels of RM-MESH-03, whatever the
mesher produces. The scale claim lives where it is honest: a 20 000-node
synthetic mesh, and the `static_assert` on the index-space bound.

### F3 — a restored source is not a restored binary (HARNESS DEFECT, FIXED)

The mutation harness restored its sources, verified the restoration by sha1,
printed matching hashes — and the next `ctest --repeat` run failed in the F1
regression test, reading exactly like the F1 fix not working. It was still
running the **M10 mutant binary**: restoring a source does not relink the test
executable.

Located by running the same filter in `debug-shared-ext`, which had just been
built from the restored tree and passed 31 of 31 at the same moment. Both
scripts now end with a restore build. Recorded in `MUTATION_PROTECTION.md`
because a verdict produced by a stale artifact is indistinguishable from a real
one, and this one pointed at working code.

### F4 — a surviving mutation that was not a missing test (RESOLVED)

M7 — removing the modulo from `componentAt` — survived. The branch was
unreachable because every caller passed an already-reduced offset. The
resolution was neither to delete the code nor to contrive a test for a
defensive guard, but to notice that the modulo **is** the cyclic lookup over a
flat element-vector position that `P17-ELEM-001` needs, and to fix all twelve
positions of a Tet4 vector here, where the convention lives. See
`MUTATION_PROTECTION.md`; M7 is now killed at compile time.

## The brief's 25 questions

**Can a DOF index be computed from a raw `NodeId` value?** No, and verified by
search rather than asserted: the only textual occurrence of
`3 * nodeId.value()` in the module is the comment explaining why it is absent.
`nodeOrdinal` is a binary search over the mesh's ascending handles, and mutation
M5 — replacing it with `node.value() - 1` — is killed by seven tests.

**Can a sparse `NodeId` set break the numbering?** No. The brief's own fixture
is a test: nodes 3, 1000 and 9000000 give **9** degrees of freedom, and the test
asserts both the 9 and that the count is not 27 000 003. Every handle a dense
numbering would have produced — 1, 2, 4, 999, 1001, 8999999, 9000001 — is
asserted to resolve to nothing.

**Can `3N` overflow?** No, and it is a compile-time proof rather than a runtime
branch: a `DofIndex` is 64-bit, a `NodeId` is 32-bit and a mesh's handles are
strictly increasing, so `3 * (2^32 - 1) + 1 = 12 884 901 885 < 2^64`. The
header carries the `static_assert`; the test carries it **and** the reason
`DofIndex` is not 32-bit, since `3 * (2^32 - 1) > 2^32 - 1`. All DOF arithmetic
is widened before multiplying, so the `size_t` side cannot overflow on a 32-bit
platform either.

**Can the numbering differ between Debug and Release?** No unordered container
appears in the module — the only occurrence of the word is the comment saying
why — and the numbering is derived from `Mesh`'s enumeration, which P16
guarantees is "ascending NodeId ... so the same construction gives the same
enumeration in Debug, Release and Debug-shared". Measured: all 31 tests give
identical results in `debug-ext`, `release-ext` and `debug-shared-ext`, at the
same assertion count.

**Can a node be numbered twice?** No, and it is checked as a **set** rather than
by trusting the formula: the union of every node's three indices is collected
into a `std::set`, its size compared with `3N`, its minimum asserted 1 and its
maximum `3N`. A collision would shrink the set.

**Can a node be missed?** The same assertion answers it from the other side: a
missed node would also shrink the set below `3N`.

**Can a DOF index be reused across two nodes?** Covered by the same set, and by
the exhaustive round trip: `dofAt(indexOf(d)) == d` for every degree of freedom
of three fixtures, and `indexOf(dofAt(i)) == i` for every index. Two things
numbered the same would break one direction.

**Is the component ordering fixed and documented?** Yes, and it is
`kDofComponents`, which P17-DATA-001 already defined as "the order a node's
degrees of freedom are numbered". `kDofsPerNode` is that array's `size()`, and
`componentAt` / `offsetOf` are functions over it, so a reordering cannot leave
two definitions disagreeing.

**Is the per-node interleaving fixed, and tested?** Yes, and the test asserts
the convention **and its negation** — for a 5-node mesh the second node's Ux is
asserted to be 4 and asserted **not** to be 2, and the fifth node's Ux to be 13.
Without the negation, component blocking would satisfy a formula check at node
ordinal 0, where the two agree. Mutation M2 confirms.

**Could a later milestone silently change the ordering?** It would have to
break M2's test and the three negated assertions. The reason for the choice —
element-loop locality and assembled bandwidth — is recorded beside it, so a
change would be a decision rather than an accident.

**Can a constraint name a node that does not exist?** No;
`ConstraintProblem::NodeNotInMesh`, with the handle in the diagnostic
(`node:77`). Never resolved to the nearest node and never skipped.

**Can an invalid `NodeId` be treated as node zero or as the first node?** No.
`NodeId{}` is invalid, `nodeOrdinal` returns nullopt for it before searching,
and a constraint carrying it is `NodeNotInMesh`.

**Can the same DOF be constrained twice?** It is **refused**, not deduplicated,
and the reason is recorded: `P17-BC-001` will build these sets from restraints,
and two restraints fixing the same z are harmless only if they prescribe the
same value — a question about data this type deliberately does not carry.
Accepting it now would decide it by silence. The diagnostic names the degree of
freedom: `node:2 uy is constrained more than once`. Mutation M4 confirms.

**Can an out-of-range DOF be constrained?** Two answers, because there are two
routes. Through a `NodalDof` the component is validated —
`static_cast<DofComponent>(9)` is `ComponentNotRecognised`, reachable because
`DofComponent` is a `uint8_t` enum and a switch with no default falls straight
through such a value into a lookup past the end. Through a `ConstraintSet` built
against a larger numbering it **was** possible, and that is F1.

**Can the free numbering have a gap?** No, and it is checked as a set: every
equation number collected, `size()` compared with `freeCount()`, first `0`, last
`freeCount() - 1`. Plus the exact assignment — with DOFs 1, 2, 3 and 8
constrained out of 12, DOF 4 is row 0 and DOF 12 is row 7 — so a renumbering is
visible rather than merely counted.

**Can a DOF be both free and constrained?** It cannot be represented: only
`freeDofs_` is stored, and `isConstrained` is `contains && !isFree`. Which makes
the obvious assertion nearly tautological, so the test asserts the version that
is not — `isConstrained(d) == set.contains(d)` for every `d`, comparing two
independently built objects — and the four sums.

**Can a DOF be neither?** For an index of this numbering, no, by the same
construction. For an index that is **not** of this numbering, both are false,
and that is deliberate: a degree of freedom of another mesh is absent, not
constrained, because calling it constrained would make a stale index look like
a fixed one and silently remove an equation.

**Can equation numbering depend on constraint insertion order?** No, and it is
checked over **all 24 permutations** of a four-element set rather than on one
shuffle — the `ConstraintSet` equal element for element in all 24, and the
`FreeEquationMap` and its `freeDofs()` equal in all 24. The permutation count is
asserted to be 24 so a loop that stopped early could not pass.

**Can a DOF map outlive its mesh?** It does not reference one. The node handles
are copied in — four bytes a node — so there is no pointer to dangle.
`StructuralModel` is move-only because it borrows; this type avoids the hazard
rather than guarding it, and is freely copyable as a result.

**Can a remesh leave a stale numbering valid?** No. Measured through the
ordinary pipeline on a real model: the same document remeshed gives the **same
node count and the same node handles** and a different `MeshId`, so
`describes()` is false, the maps compare unequal, and the stale `ConstraintSet`
is refused against the fresh numbering with `FailedPrecondition` — even though
every handle in it is still a node of the new mesh, which the test asserts
before trying. Mutation M3 confirms.

An audit finding behind this: `generateVolumeMesh` always builds with
`MeshBuilder builder;`, so **`MeshStamp::generation` is always 0 in production**
and the `MeshId` is what distinguishes generations. The brief's adversarial case
is defeated by the id, not by the generation counter.

**Can a solver identity be persisted?** `grep` across `src/io/`,
`include/bettercad/io/` and `apps/` for `DofIndex`, `FreeEquationIndex`,
`MeshDofMap`, `ConstraintSet` and `FreeEquationMap`: **0** occurrences. Neither
index type is in `core/Id.hpp` (0 occurrences there either), and nine
compile-fail cases enforce that neither converts to a document identity, a
node, or an integer.

**Can a `FreeEquationIndex` be confused with a `DofIndex`?** No, in both
directions, and the compile-fail cases are chosen for the mistake that would
actually be made — `takesEquation(dof)` is listed as the more dangerous
direction because for an **unrestrained** model the two agree apart from the
one-based offset, so a test on a free-floating body would not catch it.

**Can either be constructed from a raw integer?** No; `fromValue` is the only
way in. And `FreeEquationIndex` has **no default constructor**, because an
index defaulting to row 0 would be the sentinel defect by another route —
absence is `std::optional`, which the compiler makes the caller handle.

**Can a numbering, a constraint set or a free-equation map be fabricated?** No.
Each has a private constructor and exactly one friend, and three compile-fail
cases prove a direct construction is a build failure. What possession proves is
written out in `CONSTRAINT_MODEL.md`.

**Can zero targeted tests run while a gate claims PASS?** Checked with `-N`
before every run: `MeshDofMap_|ConstraintSet_|FreeEquationMap_|StructuralDof_`
selects **31**, `compile_fail.structdof` **9**, `architecture.` **14**. The
whole suite is **3493**, reconciled from both ends: P17-MAT-001's 3453 plus 31
plus 9 is 3493, and the two counts were taken independently rather than one
derived from the other.

## Four of the reviewer's own

**Is the three-type split justified, or is it ceremony?** The brief prefers it
and offers one combined type as the alternative, so the question is whether the
preference has a reason here. Two: a combined type would be rebuilt when a
restraint changed although the numbering had not — and the numbering of a
hundred thousand nodes is the expensive half — and it would let a caller ask
"which equation is this" of an object not yet told what was constrained, which
is a question with a plausible wrong answer rather than no answer.

**Should `buildMeshDofMap` have taken a `StructuralModel` instead of a `Mesh`?**
This was the sharpest question, because ADR-036's whole device is that
possession of a `StructuralModel` is the evidence a solve is safe, and a
function taking a bare `Mesh` looks like a way around it. The answer is that a
numbering is not a solve: it is combinatorics over node identity and cannot
produce a wrong engineering answer by itself. `P17-ASSEMBLY-001` and
`P17-SOLVE-001` take a `StructuralModel` **and** a `MeshDofMap`, and the stamp
binding proves the two agree, so the gate stays where ADR-036 put it. Taking a
`Mesh` is also what makes the sparse-handle fixtures possible, since no
pipeline produces a sparse mesh and `VolumeMesh` cannot be hand-built. The
reasoning is in the header so it reads as a decision.

**Is `FreeEquationIndex` being zero-based a second convention in one codebase?**
It is a departure, and the alternative was considered: a 1-based handle with a
`row()` accessor. Rejected because a `FreeEquationIndex` **is** the row an
assembler indexes `K` and `F` with, so a 1-based handle would put a `- 1` at
every assembly site and the one that was forgotten would be an off-by-one in
the stiffness matrix — a wrong answer that looks like an answer. And it is not
in fact a new idea in this module: `StructuralResult` already keys its
displacement array by a zero-based ordinal into `mesh.nodes()`. The asymmetry
with `DofIndex` is stated in the header with the reason, which is the part that
makes it a decision rather than drift.

**Did this milestone duplicate anything P16 or P17-DATA already owned?** The
check that mattered: the node ordinal. `StructuralResult` had already fixed it
— "parallel to mesh.nodes(). Entry i is the displacement of mesh.nodes()[i]" —
so a DOF map with its own order would have made a solver write node i's answer
into node j's slot. The ordinal was **adopted**, not invented, and
`MeshDofMap_UsesTheSameNodeOrdinalAsAStructuralResult` compares the two through
two different APIs so a future divergence breaks the build. `DofComponent`,
`kDofComponents`, `DofIndex` and `NodalDof` were all already defined by
P17-DATA-001 and are used rather than redefined; the only new type is
`FreeEquationIndex`, which nothing upstream had a reason to name.

## Result

```text
QUESTIONS:                      25 + 4 = 29
FINDINGS:                       4
PRODUCTION DEFECTS:             1  (F1 -- a MeshStamp identifies the builder,
                                    not the snapshot; found by reading, fixed,
                                    regression-tested, and both halves of the
                                    fix confirmed by mutation)
TEST DEFECTS OF MINE:           2  (F2 twice in one file -- asserting a mesh
                                    density P16 owns; F4's resolution)
HARNESS DEFECTS:                1  (F3 -- a restored source is not a restored
                                    binary)
GATE-BLOCKING:                  0
VERDICT:                        PASS
```
