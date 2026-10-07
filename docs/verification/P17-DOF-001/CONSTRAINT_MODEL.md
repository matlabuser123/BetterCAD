# P17-DOF-001 — the constraint model and the free-equation numbering

```text
SUBJECT:  which degrees of freedom are prescribed, which are unknown, and the
          compact numbering the reduced system is assembled against
```

## Three types, because there are three different facts

```text
MeshDofMap        which degrees of freedom EXIST. A function of the mesh alone
ConstraintSet     which of them are PRESCRIBED. A function of the model's
                  restraints, validated against one MeshDofMap
FreeEquationMap   which are UNKNOWN, and which row each is. A function of the
                  other two
```

The brief offers one combined type as an alternative and prefers the split; the
split is what was built, for two reasons that are not stylistic. One type
carrying all three would have to be **rebuilt when a restraint changed although
the numbering had not** — and a numbering of a hundred thousand nodes is the
expensive half. And it would let a caller ask "which equation is this" of an
object that had not yet been told what was constrained, which is a question
with a plausible wrong answer rather than no answer.

The dependency runs one way, and each answer has exactly one owner.

## `ConstraintSet` carries WHICH, not WHAT

It records that a degree of freedom is not an unknown. It carries **no
prescribed value**, and that is a decision:

```text
homogeneous and inhomogeneous restraints partition the unknowns IDENTICALLY,
so the numbering does not need the value
```

Inventing a value field here would fix a representation before `P17-BC-001`
defines what a restraint is. The type is called `Constraint` and not
`Restraint` for the same reason: a restraint is the user's modelling intent,
and this is its consequence for the index space.

## Order independence, by construction

The input is sorted into ascending `DofIndex`, so two callers listing the same
restraints in different orders build sets that compare **equal** — and the free
numbering derived from either is identical. A property of the representation,
not of a sorting step a future caller might skip.

Checked over **every permutation** of a four-element set rather than on one
shuffle, and the consequence is checked too:

```text
24 permutations of 4 prescribed DOFs
  ConstraintSet      equal, element for element, in all 24
  FreeEquationMap     equal, and freeDofs() equal, in all 24
```

`ConstraintSet_IsIndependentOfTheOrderConstraintsAreListedIn`, which asserts
the permutation count is 24 so a loop that stopped early could not pass.

## Duplicates are refused, not deduplicated

```text
DuplicateDof    the same (node, component) twice
```

Refused, and the reason is that this milestone **cannot know whether the
repetition is harmless**. `P17-BC-001` will build these sets from restraints,
and two restraints that both fix a node's z are harmless only if they prescribe
the same value — a question about data this type deliberately does not carry.
Accepting the duplicate now would decide it by silence.

The diagnostic names the offending degree of freedom rather than the fact of a
duplicate: `node:2 uy is constrained more than once`.

And the near miss is tested: the same **node** twice with different components
is not a duplicate, and a two-element set is built.

## The refusals, and that each is reachable

```text
NodeNotInMesh             a handle the mesh does not have, including NodeId{}.
                          Never resolved to the nearest node and never skipped:
                          a restraint the user asked for that silently did not
                          apply leaves a model under-constrained in a way
                          nothing reports
ComponentNotRecognised    static_cast<DofComponent>(9). Reachable because
                          DofComponent is a uint8_t enum
DuplicateDof              as above
```

All three reached by a test, each with its name asserted. No fourth value: a
"value conflict" enumerator was considered and **not** added, because nothing
could return it while the set carries no values.

## An empty set is accepted

A model with no restraints is a real model whose stiffness matrix is singular.
Refusing to **number** it would report a physics problem as a numbering
failure, and detecting an insufficiently constrained model is
`P17-SOLVE-001`'s, where the rigid-body modes are visible. This milestone's job
is to represent what was asked for faithfully, including nothing.

Measured: an empty set over a 3-node mesh gives `freeCount() == 9`,
`constrainedCount() == 0`, and every degree of freedom numbered
`value - 1`.

## `FreeEquationIndex`: zero-based, and with no invalid value

This is the one place the module departs from BetterCAD's handle convention,
and the departure is deliberate:

```text
DofIndex             an IDENTITY. 1-based, 0 invalid, so a default-constructed
                     one names no equation
FreeEquationIndex    a POSITION. It is the row and column an assembler indexes
                     K and F with, so the value that comes out must BE the row
```

A 1-based handle would put a `- 1` at every assembly site, and the one that was
forgotten would be an off-by-one **in the stiffness matrix** — a wrong answer
that looks like an answer.

So absence is `std::optional<FreeEquationIndex>` rather than a sentinel, which
is strictly stronger: a constrained degree of freedom has no equation and the
compiler makes the caller handle it. And there is **no default constructor**,
because an index that defaulted to row 0 would be the sentinel defect by
another route. Both enforced:

```text
STATIC_REQUIRE_FALSE(std::is_default_constructible_v<FreeEquationIndex>)
compile_fail.structdof.free-equation-default-constructed
```

A zero-based position is not a new idea in this module: `StructuralResult`
already keys its displacement array by a zero-based ordinal into
`mesh.nodes()`. It is the same idea, named.

## Compact numbering

`0 ..= freeCount() - 1`, assigned to the free degrees of freedom in ascending
`DofIndex` order. Gap-free, so the reduced system has exactly `freeCount()`
rows and an assembler can size its storage from one number.

Checked as a **set**, not by the formula: every equation number collected,
`size()` compared with `freeCount()`, first `0`, last `freeCount() - 1`. Plus
the exact assignment, so a renumbering is visible — with DOFs 1, 2, 3 and 8
constrained out of 12, DOF 4 is row 0, DOF 7 is row 3, DOF 9 is row 4 and DOF
12 is row 7.

## `isFree` and `isConstrained` cannot disagree

Only one of them is stored. A degree of freedom is free exactly when it has an
equation, and constrained exactly when it is **in range** and does not. The
partition is a property of the representation rather than an invariant two
containers must maintain between them.

Which makes a test asserting `isFree(d) != isConstrained(d)` nearly
tautological, so the test asserts the non-tautological version as well:

```text
for every DOF d:
    equations.isFree(d)        != equations.isConstrained(d)
    equations.isConstrained(d) == set.contains(d)      <- two objects compared
sums:
    freeSeen == freeCount(), constrainedSeen == constrainedCount()
    freeSeen + constrainedSeen == dofCount()
    constrainedSeen == set.size()
```

The second line compares two independently built objects, which is what the
property is actually for.

**A degree of freedom of another mesh is absent, not constrained.** `contains`
is false for it, and so are both `isFree` and `isConstrained` — because calling
an index from a different mesh "constrained" would make a stale index look like
a fixed one, which is the direction that silently removes equations.

## The binding check, in exactly one place

```cpp
if (map.stamp() != constraints.stamp()) {
    return makeError(ErrorCode::FailedPrecondition, ...);
}
```

`buildFreeEquationMap` is where the numbering and the restraints meet, so the
pair is checked once, there, rather than by a stamp comparison at every
assembly site that could be omitted. And it **cannot be forgotten**, because
there is no other way to obtain a `FreeEquationMap`.

Measured with identical node counts and identical numeric handles:

```text
two meshes, 3 nodes each, handles 1 2 3 in both
  firstMap.nodes() == secondMap.nodes()        true
  firstMap.stamp() != secondMap.stamp()        true
  buildFreeEquationMap(firstMap, foreignSet)   REFUSED, FailedPrecondition
  buildFreeEquationMap(secondMap, foreignSet)  accepted
```

The last line is what makes the refusal about the mismatch rather than about
the set.

## Storage

`O(freeCount)`, one `DofIndex` per unknown. The forward direction is a binary
search over it and the inverse is an index, so neither direction needs a table
over all `3N` degrees of freedom and neither needs a sentinel. For 300 000
degrees of freedom with a tenth constrained that is about 2.2 MB.

## Possession is the evidence, again

All three types have a private constructor and exactly **one** friend each —
the pattern `VolumeMesh`, `GeometryMeshMap`, `StructuralModel` and
`StructuralMaterial` already use (ADR-036). What possession proves here:

```text
MeshDofMap        this is the CANONICAL numbering of a mesh that exists: not a
                  map over nodes the mesh does not have, not in an order the
                  mesh does not use, not with a stamp from another mesh
ConstraintSet     validated against one numbering: no duplicate, no
                  out-of-range index, no unknown node
FreeEquationMap   built from a matching pair, so its rows are numbered against
                  a mesh its constraints actually describe
```

Three compile-fail cases enforce it —
`mesh-dof-map-constructed-directly`, `constraint-set-constructed-directly`,
`free-equation-map-constructed-directly` — so a hand-built map is a build
failure rather than a review finding.

## How a restraint will reach these nodes

Not by holding a `NodeId`. The path, tested end to end on the reference models:

```text
FaceName                               a CAD reference, P12-STREF-001's
  -> meshing::boundaryFacetsOf         P16's map; Unresolved is a STATUS
  -> meshing::boundaryNodesOf          ascending, derived from the facets
  -> structural::fullyFixedDofs        three NodalDofs per node
  -> buildConstraintSet                validated against the numbering
  -> buildFreeEquationMap              the rows
```

`fullyFixedDofs` does **no** validation of its own — `buildConstraintSet` does,
and a helper that silently dropped an unknown node would defeat it. Asserted:
`fullyFixedDofs` on a handle the mesh does not have passes straight through and
the set refuses it.

Measured on RM-MESH-01's start cap: a fully fixed face removes exactly three
equations per node of that face and no more, and **every node not on the face
keeps all three of its degrees of freedom** — the second half being what a
control that over-applied would fail.

On RM-MESH-04, whose inner and outer walls are swept by different circles: the
two node sets are **disjoint**, fixing both builds a set of
`(outer + inner) * 3`, and listing one wall **twice** is refused as a
duplicate rather than quietly deduplicated into the same set.
