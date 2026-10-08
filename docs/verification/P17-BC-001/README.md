# P17-BC-001 — Structural Restraints

```text
STATUS:    PASS
MILESTONE: P17-BC-001, the seventh milestone of P17 — Structural FEA
SCOPE:     canonical zero-displacement restraint intent and its resolution
           into the constrained degree-of-freedom set of the current mesh.
           Prescribed non-zero displacement deliberately deferred. No
           constraint APPLICATION, no assembly, no solver, no reactions, no
           persistence, no CLI, no GUI.
```

## Baseline

```text
HEAD at start      caf0f1e52ec82a8daf39711487bcf584764d3ace
origin/main        caf0f1e52ec82a8daf39711487bcf584764d3ace
working tree       clean apart from TODO.md, which carried this milestone's
                   own authorization
P17-ARCH-001  20/20   P17-DATA-001  19/19   P17-MAT-001  14/14
P17-DOF-001   12/12   P17-ELEM-001  18/18   P17-LOAD-001  18/18
P16  QUALIFIED (P16-QUAL-001, 2026-10-06)
and the eight fingerprinted paths still carried P17-LOAD-001's qualified
component list, so every predecessor was demonstrably qualified on THIS tree
```

`TODO.md` authorized the milestone before any production file was touched:
*"P17-BC-001 — Structural Restraints. AUTHORIZED 2026-10-08 by an explicit
scope decision. In progress."*

## What the audit found, and what it removed from the brief

Most of the design work was establishing that four of the brief's requirements
describe work **already done elsewhere**, or states that **do not exist**.

```text
deduplicate the node set       ALREADY DONE. meshing::boundaryNodesOf ends
                               with sort() then unique(), and also re-checks
                               the map against the mesh. So brief sections 18
                               (deduplicate), 19 (deterministic order), 112
                               (shared facet nodes) and 114 (efficient
                               deduplication) are ONE CALL, and writing any of
                               it again would have been a second definition of
                               the node set

MappingState::Ambiguous        ABSENT, deliberately. P16's header: a FaceName
                               "can be ambiguous, which P12-STREF-001
                               documents, and that is exactly why this layer
                               does not map through one". Brief section 101 is
                               conditional and the condition is false

MappingState::Unsupported      ABSENT. "attribution needs no surface kind at
                               all". An unsupported topology does not exist in
                               the mapping layer; an unattributed FACE does,
                               and it is Unresolved

a stale-mapping check          UNREACHABLE here. requireStructuralModel
                               already refuses a stale mesh and proves the map
                               came from the same lookup (ADR-036), and
                               boundaryNodesOf checks it again. A third gate
                               would be a branch nothing could take
```

So `RestraintProblem` has **seven** values, not the eleven the brief sketches,
and there is no `Conflict` among them: in this scope every prescribed value is
zero, so two restraints on one degree of freedom are redundant rather than
contradictory. The brief's own instruction was not to invent fake conflict
cases, and [CONSTRAINT_VALIDATION.md](CONSTRAINT_VALIDATION.md) records what a
real conflict will be once prescribed displacement exists, and why adding it
costs nothing then.

This is the fifth milestone running where the audit found the work already
done and the milestone became reuse plus proof. It is recorded as a pattern
because it keeps paying: P17-DOF found `StructuralResult` had already fixed the
node ordinal; P17-ELEM found P15's `shearModulus` *is* μ; P17-LOAD found
`MappingState` has only two values; and here one call to `boundaryNodesOf`
satisfied four mandatory requirements.

## The authority chain

```text
canonical restraint intent   RestraintId + FaceName + component mask
          |
P16's GeometryMeshMap        structural::resolveFaceTarget
          |
current boundary facets      ascending ElementIds of THIS mesh, derived,
                             discarded -- only a COUNT is kept, for diagnostics
          |
unique current NodeIds       meshing::boundaryNodesOf, already sorted and
                             deduplicated
          |
DofIndex per component       MeshDofMap::indexOf -- and there is no
                             3 * nodeId anywhere in this milestone
          |
ConstraintSet                P17-DOF's own type, ascending and unique
```

**Nothing mesh-local is canonical**, and it is a compile-time assertion rather
than a review: a mirror struct fixes the permitted members of
`StructuralRestraint`, so an added field changes the `sizeof`, and the record is
constructible from neither a `NodeId`, an `ElementId`, a `DofIndex`, a
`MeshStamp` nor a `ConstraintSet`. Six build-failure cases enforce it, and the
two mutations that add a node list or a DOF list to the record are killed **at
compile time** by that assertion.

Full matrix and the measured mapping table:
[MAPPING_CONTRACT.md](MAPPING_CONTRACT.md).
Schema and the deferral: [RESTRAINT_SCHEMA.md](RESTRAINT_SCHEMA.md).

## What was built

```text
NEW   include/bettercad/structural/StructuralTarget.hpp       the shared face-target resolution
NEW   src/structural/StructuralTarget.cpp
NEW   include/bettercad/structural/StructuralRestraint.hpp    canonical intent
NEW   include/bettercad/structural/StructuralConstraints.hpp  the derived constrained set
NEW   src/structural/StructuralConstraints.cpp

CHANGED  src/structural/StructuralLoad.cpp                    onto the shared resolver
CHANGED  include/bettercad/structural/StructuralAnalysisObject.hpp   the restraints field
CHANGED  src/structural/CMakeLists.txt

NEW   tests/structural/StructuralBCTests.cpp                  24 cases
NEW   tests/reference/StructuralBCReferenceTests.cpp           8 cases
NEW   tests/compile_fail/StructuralRestraintMisuse.cpp         6 cases
CHANGED  tests/structural/StructuralDataTests.cpp              the definition mirror, + 1 member
CHANGED  tests/CMakeLists.txt, tests/compile_fail/CMakeLists.txt
```

`src/structural/StructuralLoad.cpp` is production code in an already-qualified
milestone. [REQUALIFICATION.md](REQUALIFICATION.md) records why it changed, that
behaviour and diagnostics are unchanged, and the reruns that re-establish
P17-LOAD-001 — plus the dated forward note added to that milestone's own
evidence, whose `FREEZE.md` fingerprint was left exactly as recorded.

## No ADR, and that is a decision

Every architectural question this milestone could have raised was already
answered, and writing an eighth structural ADR would have been noise:

```text
the index space            ADR-037, which already states what this milestone
                           does: "P17-BC-001 produces NodalDofs from CAD
                           references and never stores an index. A restraint
                           resolved after a remesh gives different indices and
                           the same physical restraint, which is the behaviour
                           the whole split is for"
the stale-input gate       ADR-036 -- possession of a StructuralModel
a mesh boundary names a
  CAD face                 ADR-032
where duplicates are
  normalised               pre-decided by ConstraintSet's own header, which
                           named P17-BC-001 and said accepting a duplicate
                           "would decide it by silence"
```

The one decision left open — restraint target kind — is recorded in
[RESTRAINT_SCHEMA.md](RESTRAINT_SCHEMA.md) and
[MAPPING_CONTRACT.md](MAPPING_CONTRACT.md) with the rejected alternatives
(`NamedBoundarySet`, which would couple structural intent to meshing intent;
edge and vertex targets, which P16 provides no canonical reference for).

## Tests

```text
tests/structural/StructuralBCTests.cpp            24 cases
    schema and compile-time authority                3
    resolving a target to the current node set       5
    overlap, duplication and the union               5
    failure paths                                    4
    remesh and determinism                           2
    currentness and independence                     4
    the ordered diagnostics                          1

tests/reference/StructuralBCReferenceTests.cpp     8 cases
    RM-MESH-01  all five component combinations on one stable planar face
    RM-MESH-01  two opposing faces, union is the exact sum
    RM-MESH-04  a curved wall: 72 mapped nodes, all constrained
    RM-MESH-06  a rigid transform does not rotate the component convention
    RM-MESH-07  local sizing changes the body, not a planar face's node set
    RM-MESH-02  a curved face's node count DOES follow refinement: 72/100/200
    RM-MESH-03  the unnamed hole wall refused, the named bore restrained
    RM-MESH-04  the free-equation partition: 567 + 216 = 783

tests/compile_fail/StructuralRestraintMisuse.cpp   6 cases

ctest -N -R "StructuralBC_|structbc"   Total Tests: 38
```

The selection was **counted before it was trusted**, because P17-DOF-001 found
a real instance of the opposite: its inherited repeat filter selected 751 tests
and covered only 17 of that milestone's 31.

### Every claim is checked both ways

A restraint has no force or moment to conserve, so there is no analytical
integral here and the arithmetic is integer: a face with `N` unique mapped
nodes restrained in `k` components gives exactly `k N` constrained degrees of
freedom. The real claims are about authority and completeness, and completeness
needs both directions at once:

```text
for every node of the mesh
    on the target      it holds EXACTLY the requested mask
    off the target     it holds NOTHING
and  constraints().size() == target nodes x components
```

A test asking only "is every target node constrained?" would pass a path that
constrained the whole mesh; one asking only "is every constrained node on the
target?" would pass a path that constrained a single corner. The expected
target set is assembled **in the test** from P16's facet mapping, facet by
facet, with its own deduplication — never from `boundaryNodesOf`, which is the
production path.

## Four defects this milestone's own review found

All four were mine, all four are fixed, and all four are the same family of
mistake: an instrument that could not see what it claimed to measure.

```text
D1  the remesh binding assertion compared two booleans for EQUALITY, so it
    would have passed if a prepared set described no mesh at all. Now a
    conjunction: the old set does NOT describe the new mesh, and the new one
    does

D2  "do not constrain only the CAD corners" was vacuous on a box and CANNOT be
    made otherwise -- a planar box face carries exactly its four corners at any
    sizing, so the correct answer and the defective one coincide. Moved to
    RM-MESH-04's curved inner wall, 72 nodes, premise asserted first

D3  the refinement comparison measured the same mesh three times: RM-MESH-07's
    planar refined face reported 2 facets and 4 nodes at 12, 6 and 3 mm while
    the body went from 14 to 61 to 64 nodes. The genuine claim moved to
    RM-MESH-02's cylindrical wall, 72/100/200, with the levels asserted to
    DIFFER before anything is compared

D4  the currentness test proved only that modifyObject bumps a revision. The
    fixture helper returned setDefinition(...).has_value(), which is true
    whenever the CALL succeeded -- so the revision moved on a no-op and the
    test would have passed even if the restraints were not in the definition
    at all. FOUND BY A MUTATION PROBE: M14 killed one of the two tests that
    should have failed, and the gap was the evidence. The helper now returns
    the change flag, a control section asserts that re-setting identical
    restraints changes nothing, and M14 was re-run and kills both
```

D2 and D3 are the fourth and fifth instances of this family across P17 —
P17-ELEM-001 asserted a mesh density twice and P17-LOAD-001 wrote a vacuous
refinement test twice — so each is recorded with the instrument's blind spot
stated on the fixture itself rather than quietly corrected. D4 is the same
family wearing different clothes, and it is the clearest argument in this
milestone for running the probes: nothing in a passing suite could have told me.

[ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md) carries all twenty-five attacks.

## Measured results

```text
Model              Target          Facets  Nodes  Components  Constrained DOFs
-----------------------------------------------------------------------------
RM-MESH-01         end cap              2      4  ux / uy / uz               4
RM-MESH-01         end cap              2      4  ux+uy                      8
RM-MESH-01         end cap              2      4  fixed                     12
RM-MESH-01         both caps          2+2    4+4  ux                         8
RM-MESH-02         wall @ 0.400 mm     72     72  uz                        72
RM-MESH-02         wall @ 0.100 mm    100    100  uz                       100
RM-MESH-02         wall @ 0.025 mm    200    200  uz                       200
RM-MESH-04         inner wall          72     72  ux                        72
RM-MESH-04         bottom annulus      >0     72  fixed                    216
RM-MESH-06 base    datum face           2      4  ux                         4
RM-MESH-06 placed  datum face           2      4  ux                         4
RM-MESH-07 12/6/3  refined face         2      4  fixed                     12
RM-MESH-03         unnamed wall         -      -  fixed              REFUSED
```

```text
N_free + N_constrained = N_total
    block fixture       45 + 12 = 57
    RM-MESH-04         567 + 216 = 783
fully fixed body        N_free = 0, N_constrained = N_total
no restraints           N_free = N_total, N_constrained = 0, and preparation
                        SUCCEEDS -- detecting an under-constrained model is
                        P17-SOLVE-001's, which is where the rigid-body modes
                        are visible
```

Remesh, invalidation and determinism:
[REMESH_VALIDATION.md](REMESH_VALIDATION.md).
The hole-wall limitation: [UNSUPPORTED_TARGETS.md](UNSUPPORTED_TARGETS.md).

## Invalidation

```text
Change                          Geometry  Mesh    Old result
component edit                  current   current STALE (Analysis)
target edit                     current   current STALE (Analysis)
add a restraint                 current   current STALE (Analysis)
remove a restraint              current   current STALE (Analysis)
re-set the identical restraints current   current current
```

The last row is a **control**, not a courtesy: without it the four above prove
only that `Document::modifyObject` bumps a revision. Defect D4.

**With no new mechanism.** The restraints are a field of
`StructuralAnalysisDefinition`, so an edit moves the owning object's revision,
which moves `analysisRevision`, which `StructuralResultSource` already carried
and `staleReasons` already compared. A restraint edit stales the **result** and
not the mesh — a restraint names a `FaceName`, not a mesh control, so it is not
in the mesh's dependency chain at all. Asserted directly through P16's own
`Mesher::currency`.

## Mutation protection

```text
14 probes, 12 KILLED, 2 SURVIVED
    M1, M2    killed at COMPILE TIME by the schema assertion
    M3        the shared resolver, killed from both P17-BC and P17-LOAD
    M7        the broadest kill: 8 of 55
    M14       found defect D4
    M10, M11  survived; both inert, both explained, and the removal M10 makes
              WAS killed where it is load-bearing -- by P17-DOF-001's own
              probes on MeshDofMap
```

See [MUTATION_PROTECTION.md](MUTATION_PROTECTION.md).

## Qualification

```text
3583/3583 in debug-ext, release-ext and debug-shared-ext, each from clean
0 warnings over 620 objects in each preset
634 x until-fail:5 in release-ext and in debug-ext
17 stages, 0 failed, qualify.cmd exit 0, 3 h 10 min
no-op rebuild: 0 Building or Linking lines in all three presets
the eight component hashes IDENTICAL before the first build and after the
last test run
```

`qualify.cmd` is byte-identical at `d313a640`, unchanged since P16-SIZE-001 and
now seventeen milestones in a row. The four pre-freeze checks, the exact
invocation and the counted selections are in [FREEZE.md](FREEZE.md).

## Known limitations

```text
prescribed non-zero displacement      deferred, deliberately, with no field in
                                      the schema for one
edge and vertex restraints            no canonical reference exists in P16
normal / tangential / frictionless
  / roller supports                   would each need a face-local basis; none
                                      exists, so none can be reached by
                                      accident
a drilled hole's cylindrical wall     has no FaceName, so it cannot be a
                                      restraint target. Refused explicitly; no
                                      geometric fallback
named boundary sets as targets        not supported; would couple structural
                                      intent to meshing intent
restraint persistence                 not implemented -- the restraints live in
                                      a document object, which is how `io` will
                                      serialize them
a planar box face                     carries exactly its four CAD corners at
                                      any sizing, which bounds what any test on
                                      box geometry can show
```

## Result

```text
RESULT:   PASS
TESTS:    3583/3583 in debug-ext, release-ext and debug-shared-ext, each from
          clean; 0 warnings over 620 objects; 634 x 5 repeats in two presets;
          17 stages, 0 failed
          +38 over P17-LOAD-001: 24 unit, 8 reference, 6 compile-fail
MUTATION: 14 probes, 12 killed (2 at compile time), 2 inert and explained
REVIEW:   25 attacks, 4 defects found and fixed, one of them by a probe
TREE:     60e01a988ee8302740bfa8f147e28fc0e888c74e, and the eight component
          hashes identical at both readings
EVIDENCE: this directory
TODO:     updated on PASS
```

## Revision

First issue, 2026-10-08.
