# P16-MAP-001 — Geometry ↔ Mesh Correspondence / Regions

```text
STATUS:   PASS
TASK:     P16-MAP-001 -- correspondence between current CAD geometry and the
          current generated mesh, and the solver-facing regions built on it
PHASE:    P16 -- Meshing
DATE:     2026-10-03
```

A load or a restraint is a statement about **a face of a part**, and the mesh
it is eventually applied to is derived state that a remesh replaces wholesale.
So canonical intent names CAD geometry and the mesh entities are derived from
it, every time:

```text
load / restraint
    -> FaceName or NamedBoundarySet        canonical, persistable
    -> GeometryMeshMap against the current mesh
    -> current boundary facets / nodes / elements
    -> P17 assembly
```

A solver requirement stored as "node 174" is wrong the moment the mesh
changes, and nothing would say so. That is the whole reason this milestone
exists.

## Documents

```text
REFERENCE_AUDIT.md     what BetterCAD already guaranteed, what provenance
                       already existed, and the one change the audit forced
MAPPING_CONTRACT.md    the chain, the three candidate mechanisms and why two
                       were rejected, the states, the identity table, the edge
                       decision, and the P21 boundary
MAPPING_RESULTS.md     the measured matrices: coverage, bidirectional,
                       remesh, topology change, local sizing, determinism
ADVERSARIAL_REVIEW.md  the brief's 24 attacks answered; 2 production defects,
                       1 test gap found by mutation, 1 ADR discrepancy, the
                       three guards that cannot fire, 6 limitations, and the
                       mutation table
qualification/         the logs, and the mutation harness
```

## Baseline

```text
branch        main
HEAD at start b5f9137  BetterCAD: define and validate mesh quality metrics
tree          3c6a58028fc0958f2ece6c49582dd79493ee5854
origin/main   b5f9137  (HEAD == origin/main)
working tree  clean
compiler      GNU 16.1.0 (MinGW, WinLibs POSIX UCRT), C++23
backend       Netgen 6.2.2604 -- UNCHANGED by this milestone
build root    C:/Users/uqhas/AppData/Local/bc-build  (outside OneDrive)
```

## Prerequisites

Verified from the committed evidence, not assumed:

```text
P16-ARCH-001      25/25 ticked, RESULT PASS      evidence at 9964f88
P16-DATA-001      26/26 ticked, RESULT PASS      evidence at 20b04b9
P16-GEOM-001      20/20 ticked, RESULT PASS      evidence at 9c755e8
P16-SURF-001      21/21 ticked, RESULT PASS      evidence at 4005815
INFRA-NETGEN-001  RESULT PASS                    evidence at eaf7da4
P16-VOL-001       19/19 ticked, RESULT PASS      evidence at e9fd82c
P16-SIZE-001      21/21 ticked, RESULT PASS      evidence at 1ec54fc
P16-QUALITY-001   19/19 ticked, RESULT PASS      evidence at b5f9137
```

`P16-QUALITY-001` was checked specifically, because the brief makes this
milestone BLOCKED without it: 19/19, `RESULT: PASS`, 3071/3071 tests in three
presets, 0 stages failed.

## Scope

Implemented: the correspondence itself, in both directions; the canonical
reference it resolves; the facet, node, element and region sets derived from
it; the named-boundary-set foundation; and the audit that decided the
mechanism.

Not implemented, deliberately:

```text
edge mapping                  no CAD-edge provenance survives the per-face
                              triangulation path, and P17 has stated no edge
                              requirement. The decision and both rejected
                              implementations are written down.
a hole-wall FaceRole          extending P12-STREF's naming is capability work
                              TODO.md does not authorize here. The gap is
                              measured and recorded instead.
multi-region semantics        P16 meshes one solid; more is refused upstream.
commands / undo               P16-CMD-001. The types are value-semantic and
                              comparable so that it needs no change here.
persistence                   P16-PERSIST-001. Nothing holds a pointer.
CLI verbs                     P16-CLI-001. The core API is backend-neutral.
visualisation                 P16-VIZ-001.
anything FEA                  P17.
```

## The architecture was already decided

`ADR-032` — *"A mesh boundary names a CAD face, and a selection is never a mesh
entity"* — settled the reference type, the many-to-many relation, the
unnamed-face rule and the orientation convention before this milestone started.
**No new ADR was written**, because there was no new architecturally
significant decision to record and ADR noise is worse than none.

What ADR-032 left open was the **attribution mechanism**, and that is
`MAPPING_CONTRACT.md`'s subject. Three candidates were compared; generation
provenance was chosen; backend markers and query-time geometric classification
were rejected, each with the condition that would have made it right.

One refinement of the ADR's letter, in substance consistent with it: the ADR
describes the mesh carrying *"that face's `FaceName` set"*. What is carried in
the mesh is the face's **index**, and the names are joined when the map is
built. That keeps document types out of the mesh — ADR-031's spirit — and gives
the map the whole `FaceInfo` as well. The ADR's actual prohibition, a facet
field of type `optional<FaceName>`, is respected: `MappedFace::names` is a set,
per face.

## What P16 guarantees, and what it does not

```text
P16 GUARANTEES
    the CURRENT regenerated CAD geometry reference
        <-> the CURRENT generated mesh entities
    under BetterCAD's existing stable-reference semantics (P12-STREF-001).

P16 DOES NOT GUARANTEE
    that an arbitrary topology-changing edit preserves a face's semantic
    identity. That is P21 -- Semantic Topology.
```

Both halves are tested. Where the kernel's history carries a name through an
operation, a reference survives: the block's `end_cap` follows the face when
the block gets taller, and a bored plate's caps keep their names on the annuli
they became. Where it does not, the reference is **`Unresolved`** and binds to
nothing: a blind hole's `HoleBottom`, after the hole is drilled through.

There is no same-normal, similar-area or nearest-centroid rule anywhere.
**The failure guarded against is not the inability to preserve an arbitrary
face; it is pretending it was preserved.**

## The attribution chain has no tolerance

```text
FaceName
    | findNamedFaces -- matched BY VALUE against the names the kernel's
    | history carried. No geometry consulted.
CAD face i of listFaces(body)
    | the kernel triangulates face by face
geometry::Mesh::faces[i]
    | carried through node unification and the deterministic re-sort
EngineeringSurfaceMesh::faceTriangles[i]
    | carried by EXACT POSITION
VolumeMesh::boundarySourceFaces
    | inverted, and the inversion CHECKED
GeometryMeshMap::faces()[i].facets
```

The positional step is exact because `generateVolumeMesh` **already refuses** a
result whose boundary is not positionally identical, in both directions, to the
surface it was given — with an exact position key and no quantisation. So the
carry is a lookup, not a match, and if a backend ever retriangulated its
boundary the volume mesher would fail outright rather than quietly produce a
different one.

Consequences worth stating plainly:

```text
no tolerance exists in the mapping, so none can be wrong or tuned
two coincident CAD faces stay distinct, which no geometric rule manages
any surface kind maps -- cone, sphere, torus, B-spline -- because the
  mechanism never looks at the surface
no Netgen marker is assigned, read or persisted; the backend is unchanged
```

## The audit found almost everything already there

`P16-SURF-001` had already added per-face triangle groups to
`geometry::Mesh` *for this milestone*, saying so in its own comment, and
`P16-GEOM-001` had already recorded that it does not reconstruct the shape
because *"it would break the subshape correspondence that P16-MAP-001 needs"*.

**The one gap was a checkable link from a triangle group to a CAD face.**
`triangulate` meshes a copy of the body — the kernel caches triangulations on
faces, so meshing the authoritative ones would make results depend on earlier
requests — while `listFaces` explores the original, and nothing said the two
traversals agreed.

It is now checked on every call, through the copier's own history. The first
attempt at the fix broke every surface mesh and is Finding 1 of the adversarial
review.

## States: two, and the absence of a third is a finding

```text
Resolved      the reference names at least one face of the current body
Unresolved    it names none. The reference is KEPT and reported
```

```text
Ambiguous     NOT REACHABLE through this reference type. A FaceName is matched
              by value, and a name on several faces denotes all of them -- a
              union with a reported multiplicity, not a choice between
              candidates. A geometric reference genuinely can be ambiguous,
              which is one more reason the mapping does not use one.
Unsupported   NOT REACHABLE either. Attribution needs no surface kind, so the
              state P16-SIZE-001 needs for a face that cannot become a sizing
              region has no analogue here.
```

Neither was added: an enumerator that cannot occur is a state nothing can
test.

**`Resolved` with zero facets is not `Unresolved`.** A face that produced no
facet is counted as `facesWithoutFacets` with an issue naming it, and
`complete()` requires that count to be zero — so an unmeshed face is
distinguishable from a deleted one and still fails the completeness gate.

## The hole wall: the one measured gap

```text
TUBE   r12/r6 h20 mm   the bore is swept by the profile's INNER CIRCLE,
                       so it is an ordinary `Side` face -- named, selectable,
                       mapped, and distinguished from the outer wall although
                       the two are coaxial and differ only in radius
BORED  40x30x10 mm     the drilled wall carries NO NAME: cutHole's namer
                       answers for a hole's flat faces only, and a cylindrical
                       face has no FaceSignature either
```

So hole-wall **forward selection** is demonstrated where the infrastructure
supports it, and the drilled case is reported rather than papered over: the
wall's 72 facets are fully mapped, the reverse query answers with an **empty
name list** instead of a nearby face's name, and the report counts the unnamed
face.

`ADR-032` assumes the opposite — *"A hole's wall is boundary with a FaceName
like any other face"* — which is true of the profile case and false of the
drilled one. Recorded as Finding 3, with the four files a fix would touch.
Extending the naming is `P12-STREF` capability work and is a scope decision.

## Identity

```text
FaceName            canonical. Persistable. Never a mesh entity.
BoundarySetId       canonical. A set's identity; its DISPLAY NAME is not, so
                    two sets may be called "fixed" and stay distinct.
listFaces() index   a CORRELATION HANDLE for one map on one body. Not
                    persisted, not comparable between maps.
ElementId / NodeId  mesh-local, for ONE generation. A remesh invalidates them.
RegionId            mesh-local (ADR-032). A region's CAD identity is its
                    feature.
Netgen markers      do not exist in this chain.
viewer triangles    do not exist in this chain.
```

**Nothing a remesh invalidates is ever canonical.** A boundary facet is named
by the `ElementId` the volume mesh already gives its boundary triangles — no
new strong ID was introduced, because `ElementId` is already a robust
current-mesh identity and adding one for symmetry is what the brief warns
against.

## Queries

```text
FaceName            -> current boundary facets        the P17 primitive
several FaceNames   -> their union, deduplicated
boundary facet      -> its CAD face and that face's names (possibly empty)
TetrahedronFace     -> its CAD face, or "interior", which is how the interior
                       case is asked about at all
facets              -> NodeIds, ascending, deduplicated
facets              -> owning Tet4, exactly one per facet, CHECKED
region              -> every Tet4; and element -> the region's CAD identity
CAD face            -> engineering SURFACE triangles, from the same provenance
NamedBoundarySet    -> resolved against whatever mesh is current
```

Every one begins with the currency check, so a stale map never gets as far as
classifying a reference.

## This layer observes

A map is built from a geometry and a mesh and answers questions. It does not
heal CAD, regenerate a feature, move a node or re-triangulate anything.
Enforced by the type system rather than by this paragraph — five
compile-failure cases:

```text
compile_fail.geometrymeshmap.build-map-from-a-mutable-geometry
compile_fail.geometrymeshmap.query-through-a-mutable-mesh
compile_fail.geometrymeshmap.fabricate-a-map
compile_fail.geometrymeshmap.write-through-a-mapped-face
compile_fail.geometrymeshmap.reach-the-mesh-through-the-map
```

`GeometryMeshMap`'s default constructor is private, as `VolumeMesh`'s is:
possessing a map is the evidence that a geometry and a mesh were checked to be
the same pair, and a default-constructed one would be a map of nothing a caller
could hand on as though it meant something.

## Results

Full matrices in [MAPPING_RESULTS.md](MAPPING_RESULTS.md).

```text
fixture                       CAD    boundary  mapped  unmapped  ambiguous  w/o
                             faces    facets                              facets
BOX 30x20x10 mm                  6        12      12        0         0        0
CYLINDER r8 h20 mm               3       140     140        0         0        0
TUBE r12/r6 h20 mm               4       288     288        0         0        0
BORED 40x30x10, 12 mm hole       7       160     160        0         0        0
BOX on the XZ plane              6        12      12        0         0        0
```

```text
COVERAGE        the union of the faces' facet sets EQUALS every boundary facet,
                and the pairwise intersections are empty -- at facet-identity
                level, on the box, the cylinder and the tube
BIDIRECTIONAL   exhaustive on the box: every facet of every face answers with
                that face. 0 mismatches.
REMESH          the same FaceName value resolves against a coarse and a fine
                mesh, the facet sets differ, both are geometrically correct,
                and a cross-generation query is REFUSED
TOPOLOGY        a drilled-away HoleBottom becomes Unresolved, keeps its place
                in the report, and binds to nothing
SIZING          the value sizing resolved and the value the mapping resolved
                are the same FaceName; the refined face's owning tetrahedra
                are finer than the opposite face's
INDEPENDENCE    a material assignment, a full quality evaluation and a 5 mm
                display tessellation each leave the whole map identical
```

## Determinism

The whole `GeometryMeshMap` compared as a value — face order, names,
signatures, facet lists, the report and its issue order — identical over five
constructions, as are the derived facet and node sets. Nothing is built from a
hash container.

## Mutation testing

Fifteen plausible mistakes across the four files the provenance chain runs
through, each followed by a rebuild and the whole `[meshing]` suite.

```text
15 applied, 15 KILLED BY A FAILING TEST, 0 killed only by the compiler,
0 survived.
```

An earlier run over seventeen produced three survivors and two compiler-only
kills, and sorting out which were gaps is where the value was: **one was a real
test gap** (a check whose only contribution was a diagnostic nothing read, now
Finding 4), two were branches unreachable through the public API, and the two
compiler kills were rewritten so a test has to catch them. Full account in
[ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md).

## Regression

Full qualification, `qualification/run-qualification.cmd`, which only supplies
the three variables; `qualify.cmd` and `verify-harness.cmd` beside it are
carried **byte for byte unchanged** from `P15-QUAL-001`, verified by `diff`.
Each preset is configured, has **every** build output removed, is rebuilt with
warnings as errors, and runs CTest only after a successful build -- unfiltered.

```text
preset             build   warnings  ctest                    build time
debug-ext          exit 0  0         3121/3121 passed (100%)  14 min
release-ext        exit 0  0         3121/3121 passed (100%)  17 min
debug-shared-ext   exit 0  0         3121/3121 passed (100%)  14 min

repeat release-ext   414 tests selected, x5, 414/414 passed, exit 0   59 min
repeat debug-ext     414 tests selected, x5, 414/414 passed, exit 0   60 min

stages failed: 0          qualify.cmd exit 0
started 02:32:29   finished 06:00:12   3 h 27 m 43 s   2026-10-03
```

```text
test count   3071 (P16-QUALITY-001) -> 3121, exactly +50:
             45 in tests/meshing/GeometryMeshMapTests.cpp
              5 in tests/compile_fail/GeometryMeshMapMisuse.cpp
executions   3121 x 3 presets + 414 x 5 x 2 repeat presets = 13 503
```

The no-op rebuild after each build had nothing to do but re-check the git
revision, which is what proves the binaries CTest ran are the ones just built.
The single `error` string in each build log is the filename `Error.cpp.obj`,
not a diagnostic.

Two notes on the figures. Every preset rebuilt **587 objects** rather than a
handful, because `core/Id.hpp` gained one alias and every translation unit
includes it -- which is also why this run is an hour longer than
`P16-QUALITY-001`'s. And the repeat stages took an hour each because the
414-test blast radius includes the compile-failure groups, every one of which
drives a build of the tree under a `RESOURCE_LOCK` and therefore cannot run in
parallel with its siblings.

`debug-shared-ext` matters here for a specific reason: `GeometryMeshMap`,
`MappedFace`, `BoundaryFacetSet`, `NamedBoundarySet` and the report all cross a
DLL boundary in that preset, and the map's private default constructor plus its
friend declaration are exactly the shape an export mistake breaks. It was also
built and run against the mapping tests **before** the freeze, so a DLL defect
could not have been discovered only after a three-hour run.

### The blast radius

```text
unit.Map*  unit.Boundary*         the subject
unit.Mesh* unit.Surf* unit.Vol*   the rest of the meshing module: the data
unit.Tet*  unit.Size* unit.Geom*   model, geometry preparation, the surface
unit.Quality* unit.Netgen*         mesh, the volume mesh, sizing, quality and
                                   the backend
unit.Export* unit.Step*           the OTHER callers of geometry::triangulate --
                                   src/io/ModelExport.cpp is the only
                                   production one besides the surface mesher
compile_fail.*                    every group, because core/Id.hpp changed and
                                   they all compile against core headers
architecture.*                    what would notice a containment or layering
                                   violation
                                                              414 tests
```

Chosen from what could break rather than from proximity. This milestone changes
a file **outside** the meshing module -- `triangulate` now verifies the pairing
between the body's faces and the meshed copy's -- so its two production callers
are in the set, and `core/Id.hpp`'s additive alias is why every
compile-failure group is.

### Cross-preset equivalence

Asserted rather than compared after the fact: the mapping tests assert exact
counts -- `cadFaceCount == 6`, `unnamedFaceCount == 1`, the complete-partition
equality, the bidirectional mismatch count of zero -- and **those assertions
ran in all three presets**. So the same CAD references resolve, the same
coverage counts come out and the same unresolved behaviour holds in Debug,
Release and Debug-shared. Raw mesh IDs are not compared between presets, which
is the limitation `P16-VOL-001` recorded and this milestone inherits unchanged:
nothing exports a mesh.

### Qualified tree == committed tree

```text
apps              7532b4b3748efaa1282874af618a6d41bcb87751
include           398e477fac0c980666be34161ff021f09c5da845
src               f4367340b894dc57fe76096348db6bf0725d4c9a
tests             cb4056bfa1bd1e84780c8d29711207eec2bdf38a
examples          9187931fa6824e98d3da6296cdc40af6ca4b8607
cmake             5a382115d7ed7775189d40b55e40f318eaae6cc7
CMakeLists.txt    3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd
CMakePresets.json 3229f0f89b236744d7d5c082bed47c28b718288e
```

Identical before and after the run, and identical to the fingerprint recorded
independently at the freeze at 02:32:14 -- three trees moved from `HEAD`
(`include`, `src`, `tests`) and five did not. These are the trees that were
committed.

### Pre-freeze checks

Run **before** the freeze, cheapest first, because all three of P15's voided
qualifications came from doing a cheap check after an expensive one:

```text
git diff --check, new files staged with add -N   clean. The mutation harness's
                                                 transient pristine/, build.log
                                                 and run.log were removed
                                                 rather than committed.
the 50 new tests x5 repeats, debug-ext           50/50 passed, 186 s
the shared build, debug-shared-ext               built clean, 45/45 mapping
                                                 tests passed
the full debug-ext suite                         3120/3121 -- see below
```

The one pre-freeze failure was `cli.new.unicode-path`, already diagnosed during
`P16-QUALITY-001`: the CLI writes the name correctly and only the stdout regex
fails to match when the console is not on code page 65001. `qualify.cmd` begins
with `chcp 65001`, and it passed in all three presets of the qualification. A
property of running CTest from a bash shell, not a regression, and nothing this
milestone touches.

## Known limitations

```text
A DRILLED HOLE'S WALL HAS NO CANONICAL REFERENCE
    cutHole names a hole's flat faces only, and a cylindrical face has no
    FaceSignature, so a drilled wall cannot be the target of a forward query.
    It is fully mapped, answers in reverse with an empty name list, and the
    report counts it. A fix is P12-STREF capability work touching HoleFace,
    FaceRole, OcctHole.cpp and the JSON role mapping -- and a bolt hole is
    exactly the surface a P17 restraint wants, so it is worth deciding. ADR-032
    assumes otherwise; see ADVERSARIAL_REVIEW.md Finding 3.

TWO DOCUMENTS CANNOT BE TOLD APART
    ObjectIds and revision counters are per document, so two structurally
    identical documents produce the same source and the same GeometryRevision.
    Within a document the check is exact, and every QUERY is protected
    regardless by the globally unique mesh stamp. Finding 2.

EDGE MAPPING IS NOT IMPLEMENTED
    No CAD-edge provenance survives the per-face triangulation path, and P17
    has stated no edge requirement. The decision and both rejected
    implementations are recorded in the header and in MAPPING_CONTRACT.md.

P16-SIZE-001 STILL RESOLVES A FACE GEOMETRICALLY
    Both use the same canonical FaceName and the same findNamedFaces, so the
    reference contract is shared and tested. But sizing then selects mesh nodes
    by testing them against the face's plane or cylinder, which its own
    limitations already record as unable to distinguish coplanar faces facing
    the same way. Re-basing it on this milestone's provenance would remove that
    limitation AND change qualified sizing behaviour, so it is a scope
    decision.

THREE GUARDS CANNOT FIRE THROUGH THE PUBLIC API
    triangulate's pairing check, owningTetrahedraOf's multi-owner branch and
    the facesWithoutFacets counter. Kept deliberately, named in
    ADVERSARIAL_REVIEW.md, and not counted as tested branches.

CROSS-PRESET MESH DETERMINISM UNASSERTED
    Carried unchanged from P16-VOL-001: nothing exports a mesh to compare. The
    mapping's SEMANTICS are asserted in all three presets, as above.

NO SANITIZER COVERAGE
    This MinGW ships no libasan or libubsan. Carried.
```

## Result

```text
RESULT:   PASS
TODO:     20/20 ticked.
NEXT:     P16-VIZ-001 -- Mesh Visualisation / Inspection. Not started, and not
          authorized by this document.
```

## Revision

First issue, 2026-10-03.
