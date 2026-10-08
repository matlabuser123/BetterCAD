# P17-BC-001 — overlap, duplication, conflict and diagnostics

## The union, and why it is a union

```text
C_total = C_1 union C_2 union ... union C_n
```

A degree of freedom constrained by two restraints is constrained once. That is
not an optimisation, it is what the set means: `ConstraintSet` records which
degrees of freedom are not unknowns, and "not an unknown" is not a quantity
that can be held twice.

**P17-DOF's `buildConstraintSet` REFUSES a repeated `DofIndex`**, deliberately,
and its own header says why:

> REFUSED RATHER THAN DEDUPLICATED, because this milestone cannot know whether
> the repetition is harmless. P17-BC-001 will build these from restraints, and
> two restraints that both fix a node's z are harmless only if they prescribe
> the same value — a question about data this type deliberately does not carry.

So the deduplication happens **here**, where the question can be answered: in
this scope every prescribed value is zero, so a repetition is demonstrably
harmless. `prepareStructuralRestraints` sorts and uniques the accumulated
`NodalDof` list before handing it over, which is brief section 68's distinction
implemented exactly:

```text
duplicate arising from overlapping physical restraint   normalised
malformed duplicate identity in the canonical collection  diagnosed
```

## Overlap table

Measured on the 40 x 30 x 20 mm block fixture, whose end cap carries 4 mapped
nodes and whose side 0 carries 4, sharing 2 along their common edge.

```text
Case                                      Records  Sum of records  Final set  PASS
-----------------------------------------------------------------------------------
same face, ux and uy (two records)              2         4 + 4=8          8  PASS
same face, ux / uy / uz (three records)         3      4+4+4=12          12  PASS
  identical to one fixed support on that face                       12 == 12  PASS
same face, fixed + redundant ux                 2        12 + 4=16         12  PASS
two different ids, same face, same ux           2         4 + 4=8          4  PASS
two adjacent faces (end cap + side 0), uz       2         4 + 4=8          6  PASS
two opposing faces (both caps), ux              2         4 + 4=8          8  PASS
```

Three of those rows are the point:

```text
fixed + redundant ux    the sum EXCEEDS the set (16 > 12) and the set is
                        right. `resolutions()` records 16 so a reader can see
                        which restraint reached what; `constraints()` records
                        12 because that is how many unknowns were removed

adjacent faces          6, not 8: the 2 shared edge nodes are counted once.
                        Those nodes are NOT off-target -- they belong to both
                        faces -- and the test asserts the intersection is
                        non-empty BEFORE relying on it

opposing faces          8, the exact sum, because the two caps share no node.
                        Asserted empty-intersection first, which is what makes
                        this row the complement of the one above
```

And the equivalence brief section 88 asks for holds through the whole pipeline,
not just in the mask algebra: three single-component restraints on one face
produce a `ConstraintSet` that compares **equal** to the one a single fixed
support produces.

## Conflict policy

```text
TODAY                        there is no conflict to have
```

Every restraint in this scope prescribes zero. Two restraints on one degree of
freedom therefore cannot disagree numerically: they are **redundant**, which is
lawful. `RestraintProblem` has no `Conflict` value, and inventing one to fill a
checkbox would have been a branch nothing could take — the brief's own
instruction was not to invent fake conflict cases.

```text
ONCE PRESCRIBED DISPLACEMENT EXISTS, a conflict is exactly this:

    two restraints constrain the same DofIndex
    AND the prescribed values differ

and the policy that will be needed is REFUSE, with both RestraintIds and the
two values named. Not "last wins" (order-dependent, so the answer would depend
on the user's list order), not "average" (physically meaningless), not
"tolerance-matched" (two values that differ by 1e-15 are the same intent, but
choosing the tolerance is choosing the answer).

WHAT MAKES THAT CHEAP TO ADD: the accumulation already passes through one
place -- `out.prescribed` in `prepareStructuralRestraints` -- and already
deduplicates there. A value field turns the dedup into a group-and-compare at
the same point. No other code moves.
```

That is recorded as a forward decision, not as implemented behaviour.

## Duplicate identity

Two records carrying one `RestraintId` is **malformed**, not an overlap: one of
them would be ignored and nothing below could tell which. Refused first, before
any mesh work, so the diagnostic is about the collection and not about a face:

```text
restraint:4 appears more than once, so one record would be ignored
```

Two records with **different** ids on the identical target and component are
lawful and are unioned — a user may legitimately keep two named supports that
happen to coincide.

## Check order

One shared pass (`run()` in `StructuralConstraints.cpp`) backs both
`prepareStructuralRestraints` and `structuralRestraintProblem`, so the two
cannot drift apart. The order is deliberate:

```text
1  numbering.describes(mesh)      the PAIR must agree before any index means
                                  anything -- and this is why even an EMPTY
                                  restraint set is refused against a numbering
                                  for another mesh: the published value carries
                                  this model's MeshStamp and a ConstraintSet
                                  carrying the numbering's, and an empty result
                                  whose two halves named different meshes would
                                  be a lie
2  duplicate RestraintId          about the collection, needs no mesh
3  empty component mask           about the record, needs no mesh
4  per restraint: resolve target   P16's three checks, in P16's order
5  per restraint: resolve nodes    boundaryNodesOf, which checks the map again
6  per restraint: empty node set
7  node + component -> DofIndex    P17-DOF's indexOf
8  union, sort, unique
9  buildConstraintSet              P17-DOF's, which sorts and validates again
```

**Atomic.** Nothing touches a `PreparedRestraints` until step 9 has succeeded;
`PreparedRestraints` has no default constructor at all, so a partially built
one is unrepresentable rather than merely discouraged.

## Diagnostics matrix

```text
Scenario                    Expected problem                  Code              Published?  PASS
--------------------------------------------------------------------------------------------------
duplicate RestraintId       DuplicateRestraintId              InvalidArgument   no          PASS
empty component mask        NoComponents                      InvalidArgument   no          PASS
malformed selector          TargetInvalid                     InvalidArgument   no          PASS
unresolved target           TargetUnresolved                  NotFound          no          PASS
unsupported target          TargetUnresolved                  NotFound          no          PASS
  (drilled-hole wall -- the same value, because an unattributed face IS
   unresolved; see UNSUPPORTED_TARGETS.md)
numbering for another mesh  NumberingIsForADifferentMesh      FailedPrecondition no         PASS
missing mesh                refused by requireStructuralModel  --               no          PASS
  (no StructuralModel exists, so there is nothing to prepare against. ADR-036's
   gate, inherited rather than re-asked)
stale mapping               refused by requireStructuralModel  --               no          PASS
  (same gate. boundaryNodesOf checks the map against the mesh a second time)
ambiguous target            NO SUCH STATE                      --               --          n/a
  (P16's MappingState has only Resolved and Unresolved)
resolved, no facets         TargetWithoutFacets               FailedPrecondition no         UNTESTED
resolved, no nodes          TargetWithoutNodes                FailedPrecondition no         UNTESTED
```

**Nothing is ever published on a failure.** Asserted for every reachable row:
`structuralRestraintProblem` reports the problem and
`prepareStructuralRestraints` returns no value, both checked in
`StructuralBC_ReportsEveryProblemThroughTheSameOrderedChecks`.

### The two untested values, and why they are kept

```text
TargetWithoutFacets   P16 reports a face it attributed no facet to as
                      UNRESOLVED, so a resolved face with an empty facet set
                      cannot be constructed through the production path. The
                      mutation probe that disables the check SURVIVES, and
                      that survival is recorded rather than hidden

TargetWithoutNodes    same: facets that carry no node cannot arise from a
                      valid mesh
```

Both are kept because a mapping that lost a face must be **reported** and not
passed over: a restraint the user asked for that silently covered nothing
leaves a model under-constrained with nothing saying so. Their `toString`
values are asserted, so they are named rather than merely present.

## What this milestone does NOT decide

```text
whether the model is sufficiently constrained     P17-SOLVE-001
  a free Tet4 body has six rigid-body modes; one ux restraint on one face
  leaves five, and preparation SUCCEEDS. The eigenstructure is visible where
  the matrix is, and deciding it here would be overreach (brief section 67)

how a constraint is APPLIED                       P17-ASSEMBLY-001 / P17-SOLVE-001
  no stiffness row is zeroed, no diagonal is set to one, no penalty stiffness
  is added and no right-hand side is touched. Searched: this module contains
  no matrix, no penalty and no right-hand side of any kind

the free-equation numbering                       P17-DOF-001
  `buildFreeEquationMap` is called by a TEST, to prove interoperability.
  N_free + N_constrained = N_total is recorded on the block fixture
  (45 + 12 = 57) and on RM-MESH-04 (567 + 216 = 783)
```
