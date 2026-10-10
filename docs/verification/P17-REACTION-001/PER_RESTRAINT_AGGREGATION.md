# P17-REACTION-001 — per-restraint attribution, and the shared degree of freedom

```text
RESULT: PASS

POLICY    unique ownership plus one shared aggregate (ADR-041, Decision 4)
IDENTITY  sum over restraints of ownedForce + sharedForce == totalForce
          exactly, and the same for moments about one origin
```

## The problem the policy exists for

Two restraints can lawfully reach the same degree of freedom — overlapping
faces share edge nodes, and `PreparedRestraints` already documents the
constrained set as "a UNION, NOT A LIST" for that reason. The **physical
reaction at a shared degree of freedom exists once**, and it cannot be
partitioned between the restraints without inventing a convention.

Two failure modes follow, and both are real:

```text
global double-counting     iterating the per-restraint DOF lists and summing
                           counts the shared edge TWICE, so the global total
                           overshoots and equilibrium fails by exactly the
                           edge's share
false additivity           reporting the shared DOF under every contributing
                           restraint and expecting a reader to know the
                           summaries do not add
```

## What was rejected

**Splitting a shared reaction between its restraints.** Rejected outright. Any
split is arbitrary; a 50/50 split is arbitrary *and* looks principled, which is
worse.

**Policy B — report under every restraint and warn.** Rejected as the primary
model. A structure whose correct use depends on a reader noticing a warning
will eventually be summed by a CLI or a GUI, and the result will be wrong by
exactly the overlap.

## The selected policy

```text
per restraint
    ownedDegreesOfFreedom    DOFs only this restraint constrained
    ownedForce, ownedMoment  the reaction on them.  ADDITIVE
    sharedDegreesOfFreedom   DOFs it constrained that another also did
    sharedForce, sharedMoment the reaction on them. NOT additive -- the same
                             physical reaction appears in every summary that
                             shares it

globally
    sharedDegreesOfFreedom   how many DOFs more than one restraint reached
    sharedForce, sharedMoment the reaction on them, counted ONCE
```

so that

```text
sum_r ownedForce(r) + sharedForce == totalForce
```

holds **exactly**. Additivity is a property of the decomposition, not a rule a
caller must remember, and `sharedDegreesOfFreedom != 0` is the explicit signal
that a summary has a non-additive part.

## The provenance this needed, and the P17-BC change it cost

`RestraintResolution` recorded only counts:

```text
restraint, facets, nodes, components, degreesOfFreedom
```

Counts cannot attribute a reaction. The two ways to get identity:

```text
re-resolve each restraint's target after the solve
    REJECTED. Re-running the geometry mapping and ASSUMING it reproduces the
    set the solve used is precisely how a source mismatch hides: if the mesh or
    the mapping moved, the re-resolution would quietly attribute reactions to
    the wrong degrees of freedom

retain the mapping the solve actually used
    SELECTED, and it keeps a value already computed
```

The per-restraint loop in `prepareStructuralRestraints` already called
`numbering.indexOf(NodalDof{node, component})` for every degree of freedom it
resolved, verified the answer, and **discarded the index**. The change retains
it:

```text
RestraintResolution gains
    std::vector<DofIndex> constrained;   ascending, unique within the restraint
invariant
    constrained.size() == degreesOfFreedom
```

It is sorted and uniqued even though the construction order already produces
ascending indices — so the contract does not depend on that reasoning holding
after a future renumbering, and a duplicate cannot silently double a reaction.

**Derived state, never persisted**, which is the status the rest of
`PreparedRestraints` already has. A `DofIndex` is solver-local: it means a row
of one numbering of one mesh, and storing one as intent is the defect
P17-DATA-001 exists to prevent.

### Requalification

```text
milestone          shared code changed?   requalification
---------------------------------------------------------------------------
P17-BC-001         YES -- StructuralConstraints.hpp and .cpp
                                          OWED AND PAID. Its 32 tests were run
                                          before the freeze (32/32), run
                                          unfiltered in all three presets, and
                                          run 5x in two via unit\.Structural
P17-DOF-001        NO                     not owed; in the repeat set anyway
P17-LOAD-001       NO                     not owed; likewise
P17-ASSEMBLY-001   NO                     not owed; likewise
P17-SOLVE-001      NO                     StructuralSolve.cpp UNTOUCHED. This
                                          milestone consumes fullResidual()
P17-POST-001       NO                     not owed; likewise
P16                NO                     not owed
```

## The disjoint case: additivity, with the premise asserted

Two side faces at `x = 0` and `x = 40 mm`. They do not touch, so they share no
node.

```text
sharedDegreesOfFreedom                            0        asserted first
resolved[0].dof + resolved[1].dof == unique       yes      asserted
ownedForce(R1) + ownedForce(R2) + sharedForce     == totalForce, 1e-9 rel
ownedMoment(R1) + ownedMoment(R2) + sharedMoment  == reaction moment, 1e-9
each restraint's ownedDegreesOfFreedom            > 0
each restraint's |ownedForce|                     > 1 N      neither is a token
```

**The disjointness is asserted before the additivity**, because with an overlap
the additivity claim would be the wrong claim entirely — a vacuous-instrument
guard.

### The split is measured, not predicted

The two supports carry different loads, and nothing hardcodes a 50/50. Reaction
sharing depends on stiffness, geometry and load position; the end-cap load is
not symmetric between these two faces. The magnitudes are printed and the test
asserts only what statics determines:

```text
R_A + R_B + F_external = 0      exact statics, asserted
R_A = R_B                       NOT asserted
```

**An analytically predicted split is not claimed**, and the reason is recorded
rather than glossed: it would need a fixture whose *mesh* is provably symmetric,
and P16 does not guarantee that a symmetric body meshes symmetrically. What is
claimed is the statics that holds regardless — the sum — plus the fully
constrained case where `R = -F` exactly.

## The overlapping case: the double-count adversarial test

Two side faces that **meet** along an edge, each fully fixed. The edge's nodes
are constrained twice in the restraint lists and once in the union.

```text
listed across the two restraints          > unique          asserted
sharedDegreesOfFreedom                    > 0               asserted
listed - unique == sharedDegreesOfFreedom                   asserted
```

**The overlap is asserted first, and it is the most important guard in the
file:** if the faces did not share an edge, every assertion below would hold
trivially and the double-count protection would be untested.

```text
global force equilibrium                  eta_F <= 1e-12    PASS
reaction F_z == -1800 N                   1e-9 rel          PASS
owned(R1) + owned(R2) + shared == total   1e-9 rel          PASS
owned(R1) + owned(R2) + shared DOF count == unique          PASS
sharedDegreesOfFreedom(R1) == sharedDegreesOfFreedom(R2)    PASS
```

Global equilibrium holding is the decisive assertion: a summation over the
per-restraint lists would overshoot by the shared edge and break it.

### The non-additivity is demonstrated, not warned about

```text
naive = owned(R1) + shared(R1) + owned(R2) + shared(R2)
naive - totalForce == sharedForce        measured, 1e-9 rel
|sharedForce| > 0                        so a caller who summed would be
                                         visibly wrong
```

The overshoot is exactly one copy of the shared aggregate — measured, so the
non-additivity is a demonstrated fact rather than a sentence in a comment.

## Determinism of the aggregation

```text
constrained degrees of freedom   ascending by DofIndex (ConstraintSet's own
                                 order, unique by construction)
nodes                            ascending by NodeId
restraints                       the order they were given to
                                 prepareStructuralRestraints
within a restraint               ascending by DofIndex
the multiplicity table           a SORTED VECTOR, looked up by binary search
                                 -- not a hash map, because the multiplicities
                                 decide which bucket each reaction joins and
                                 those buckets are summed
```

No unordered container appears in a numerically significant traversal. Five
repeats are **bitwise** identical over every channel including both balances,
and the restraint order is asserted.

## Mutation protection

```text
probe                                        result
-------------------------------------------------------------
M15 shared DOF counted per restraint         KILLED
    (`counts == 1` -> `counts >= 1`, so a shared DOF enters
     every owner's ADDITIVE bucket)
M16 shared aggregate double-added            KILLED
    (`counts < 2` -> `counts < 1`, so every DOF joins the
     shared aggregate as well as an owned bucket)
M17 multiplicity always one                  KILLED
    (so nothing is ever classified as shared)
```

Each is killed by the identity assertions above, which is what makes them
assertions about the decomposition rather than about one fixture.
