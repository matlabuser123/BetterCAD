# P17-BC-001 — mutation protection

```text
14 probes applied, 12 KILLED, 2 SURVIVED
```

## Method

Each probe restores the five production files from hash-verified pristine
copies, applies **one** verified literal substitution (the harness dies if the
anchor is missing or ambiguous), rebuilds, runs the combined P17-BC and
P17-LOAD selection, and restores. The run **ends with a restore build**: a
restored source is not a restored binary, and P17-DOF-001 lost an hour to a
stale mutant binary reporting a false `--repeat` failure.

```text
filter          StructuralBC_|StructuralLoad_
selected        55 tests, counted with ctest -N before the run
```

**Both milestones' suites run on every probe**, because
`src/structural/StructuralTarget.cpp` is shared. A mutation of the shared
resolver has to be killed by both, and M3 is: five failures drawn from P17-BC
and P17-LOAD together.

```text
harness   .../scratchpad/bcmut/run.sh
log       .../scratchpad/bcmut/results.txt
```

The harness was pointed at the **final** production files. An earlier partial
run (M1–M5) was discarded when the single-resolution refactor landed in
`StructuralConstraints.cpp`, so the evidence below describes the file that
ships rather than a superseded draft.

## Results

```text
#    Mutation                                        Result     Killed by
---------------------------------------------------------------------------------
M1   persist a node array in the restraint           KILLED     the compiler
M2   persist a DOF array in the restraint            KILLED     the compiler
M3   use an unresolved target anyway                 KILLED     5 of 55
M4   constrain only the first four mapped nodes      KILLED     3 of 55
M5   do not deduplicate the node union               KILLED     1 of 55
M6   do not deduplicate the prescribed DOF union     KILLED     2 of 55
M7   constrain all three components regardless       KILLED     8 of 55
M8   swap uy and uz when prescribing                 KILLED     4 of 55
M9   do not check the numbering binding              KILLED     1 of 55
M10  describes() by MeshStamp alone                  SURVIVED   inert here
M11  accept an empty resolved node set               SURVIVED   unreachable
M12  accept a duplicate RestraintId                 KILLED     2 of 55
M13  accept an empty component mask                  KILLED     2 of 55
M14  leave the restraints out of the revision        KILLED     2 of 55
```

### The compile-time kills

M1 and M2 add a `std::array<int, 4>` and a `std::array<unsigned long long, 4>`
to `StructuralRestraint`'s private members — the two mutations brief section
139 names first. Both fail to compile, and the error is the assertion that
exists for exactly this:

```text
StructuralBCTests.cpp:365: static assertion failed: a canonical restraint
holds an identity, a CAD target and a component mask -- nothing else. A node
list, a facet list or a DOF index added to it breaks this
```

A `sizeof` mirror is a cheap instrument, and these are what make it a real one:
the authority rule is enforced at build time rather than by a reviewer reading
a struct.

### The behavioural kills

```text
M3   if (!set->fullyResolved())  ->  if (false && ...)
     2654 StructuralBC_RefusesAnUnresolvedTargetWithNoNearestFaceFallback
     2655 StructuralBC_RefusesTheDrilledHoleWallWithNoGeometricFallback
     2657 StructuralBC_ReportsEveryProblemThroughTheSameOrderedChecks
     2714 StructuralLoad_RefusesAnUnresolvedTargetRatherThanTreatingItAsZero
     2716 StructuralLoad_RefusesTheDrilledHoleWallWithNoGeometricFallback
     The shared resolver, killed from both sides.

M4   nodes->resize(min(size, 4)) before the component loop
     2636 StructuralBC_ConstrainsEveryNodeOfACurvedReferenceWall
     2641 StructuralBC_FollowsTheNodeCountOfARefinedCurvedFace
     2655 StructuralBC_RefusesTheDrilledHoleWallWithNoGeometricFallback
     THE PROBE THAT JUSTIFIES MOVING THAT TEST. On the block fixture a planar
     face carries exactly four nodes, so this mutation would have changed
     nothing there -- it is killed only by the CURVED reference faces, which
     is the defect D2 in the adversarial review and the reason the corners
     claim lives on RM-MESH-04.

M5   drop the node-union unique()
     2660 StructuralBC_UnionsOverlappingRestraintsWithoutRefusingThem
     The two adjacent faces sharing two edge nodes.

M6   drop the prescribed-DOF unique()
     2644 StructuralBC_IsIndependentOfTheOrderRestraintsAreGivenIn
     2660 StructuralBC_UnionsOverlappingRestraintsWithoutRefusingThem
     P17-DOF's buildConstraintSet refuses the repeat, so an overlap that is
     lawful becomes a failure -- which is exactly the false-duplicate case
     brief section 68 warns about.

M7   constrain every component regardless of the mask
     eight failures, including every exact-count test and the transform case.
     The broadest kill in the set, which is what a both-ways semantic check
     buys.

M8   swap uy and uz when pushing the NodalDof
     2635, 2636, 2641, 2658 -- the four tests that read back which component
     is held at each node, rather than only how many are.

M9   skip numbering.describes(mesh)
     2652 StructuralBC_RefusesANumberingBuiltForADifferentMesh

M12  accept a duplicate RestraintId
     2656, 2657

M13  accept an empty component mask
     2653, 2657

M14  make StructuralAnalysisDefinition::operator== ignore the restraints,
     so setDefinition short-circuits and a restraint edit moves nothing
     2630 StructuralBC_ARestraintEditStalesTheResultAndNotTheMesh
     2631 StructuralBC_ARestraintLivesInTheDefinitionAndNotInAParallelCounter

     THIS PROBE FOUND A TEST DEFECT. On the first run it killed only 2631: the
     `setRestraints` helper returned `setDefinition(...).has_value()`, which is
     true whenever the CALL succeeded, so `Document::modifyObject` bumped the
     revision on every call -- including a no-op. The currentness test would
     therefore have passed even if the restraints were not in the definition at
     all. The helper now returns the change flag, a control section asserts
     that re-setting identical restraints changes NOTHING, and every edit
     asserts that it DID change something. M14 was re-run against the corrected
     test and now kills both. Recorded as defect D4 in
     ADVERSARIAL_REVIEW.md.
```

### The two survivors

```text
M10  PreparedRestraints::describes() reduced to mesh.owns(mesh_), dropping the
     node-count half.

     INERT HERE, and for a reason that is architectural rather than lucky. A
     MeshStamp identifies the BUILDER and not the snapshot, so two snapshots of
     one builder share a stamp with different node counts -- which is why
     MeshDofMap::describes checks both. But production builds one mesh per
     generation from a local builder, so the ambiguity cannot arise through any
     path a test can reach, and every stamp this milestone compares differs
     between generations anyway.

     Where the count check IS load-bearing is MeshDofMap, and it was probed
     there: P17-DOF-001's M9 and M10 both killed its removal, and that
     milestone's evidence records the production defect it fixed -- a
     ConstraintSet from a larger snapshot accepted against a smaller, numbering
     every DOF as free. The check here mirrors it so the two types answer the
     same question the same way, and it costs O(1).

M11  if (nodes->empty()) disabled.

     UNREACHABLE. P16 reports a face it attributed no facet to as UNRESOLVED,
     which M3 covers, and facets that carry no node cannot arise from a valid
     mesh. `StructuralConstraints.hpp` already records both
     `TargetWithoutFacets` and `TargetWithoutNodes` as untested-but-kept: a
     mapping that lost a face must be REPORTED and not passed over, because a
     restraint the user asked for that silently covered nothing leaves a model
     under-constrained with nothing saying so.

     The survival is the measurement that confirms the header's claim rather
     than a gap in the tests. This is the same disposition P17-LOAD-001's M13
     and P17-ELEM-001's M7 and M8 were recorded with.
```

## Two of the brief's mutations are not expressible

Recorded with the reason rather than reported as killed:

```text
"make component restraint face-normal based"
    there is no normal in this milestone's code to read and no face-local
    basis in the schema to select. Facet geometry is never loaded at all -- a
    restraint needs the facets' NODES. The convention is instead MEASURED, on
    RM-MESH-06's transformed body, where a global ux restraint constrains the
    same nodes although the face's normal has rotated

"accept stale mapping"
    the gate is requireStructuralModel's (ADR-036) and there is no second one
    here to disable. Possession of a StructuralModel IS the evidence; disabling
    it would be a mutation of P17-ARCH-001, which has its own probes.
    boundaryNodesOf re-checks the map against the mesh on its own account
```

Two more of the brief's list are covered by probes under different names:

```text
"use nearest face on unresolved reference"           M3
"use only CAD vertices instead of all mapped nodes"  M4
"forget to deduplicate shared facet nodes"           M5, M6
"constrain all XYZ when only Ux requested"           M7
"swap Uy/Uz"                                         M8
"reuse M1 prepared constraints on M2"                M9, M10
"treat empty target as zero constraints"             M11
"mark P16 mesh stale on restraint edit"              not expressible -- there
                                                     is no code here that
                                                     could mark a mesh stale,
                                                     and that absence is what
                                                     StructuralBC_ARestraintEdit
                                                     StalesTheResultAndNotThe
                                                     Mesh measures through
                                                     Mesher::currency
"omit restraint revision from result currentness"    M14
```

## Restoration verified

```text
restore build OK
100% tests passed out of 55, 75.42 s
```

The five production files were compared against their pristine hashes after the
run, and the qualification freeze re-reads them. The one source change made
after the probes is the D4 test fix above, in two test files; M14 was re-run
against it and the full three-preset qualification ran after it.
