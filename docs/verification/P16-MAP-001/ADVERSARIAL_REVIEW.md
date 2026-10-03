# P16-MAP-001 — adversarial review

```text
SUBJECT:  an attempt to disprove the milestone before calling it complete
DATE:     2026-10-03
METHOD:   the brief's 24 attacks, answered one at a time against the final
          diff; then mutation testing on the four files the provenance chain
          runs through.
```

**Two production defects found and fixed, one test gap found by mutation and
closed, one ADR discrepancy recorded, six limitations carried.** None of the
four was found by reading the diff: two came from making a change and watching
the existing suite, and two from deleting a line and finding that nothing
complained.

---

## Finding 1 — `ModifiedShape` loses a face's orientation

**Severity: real, and it would have broken every surface mesh. Fixed.**

To make `geometry::Mesh::faces[i]` provably the i-th face of `listFaces()`, the
first attempt iterated the **original** shape's faces and fetched each one's
copy with `copier.ModifiedShape(face)` to read its triangulation from.

That compiled and broke the surface mesher outright. `BRepBuilderAPI_Copy`'s
`ModifiedShape` returns the copied subshape **without the orientation it has
inside the shell**, so `face.Orientation() == TopAbs_REVERSED` came out wrong
for the reversed faces, their triangle windings were not flipped where they
should have been, and the surface stopped closing coherently —
`SurfaceMeshFailure::NotAValidBoundary`.

Caught by `P16-SURF-001`'s own tests within a minute. Recorded because the
mistake is attractive: `ModifiedShape` is exactly the right API for *identity*
and exactly the wrong one for *reading a face in its shell*.

**Fix.** The triangulation is read from the copy's own exploration, unchanged
from the qualified code, so orientation, location and every qualified behaviour
are untouched. The original is explored in lockstep purely to **verify** the
pairing through the copier's history, with `IsSame`, which compares identity
and ignores orientation. The index contract is now checked on every call, and
the two explorations are required to have equal length.

---

## Finding 2 — a feature's own edit did not look stale... and does

**Severity: a test defect, not a production one, but it took a real
investigation to tell which. Recorded because the conclusion matters.**

`Map_RefusesAMeshOfADifferentFeature` was written with two `Block` fixtures in
**two separate documents** and expected the mismatch to be refused. It was not.

The cause is not a bug: `GeometryRevision` mixes **ObjectIds and per-object
revision counters**, both of which are per document. Two structurally identical
documents therefore hash identically, and `VolumeMesh::source()` is an
`ObjectId` that is also per document — so both are `object:2` and nothing tells
them apart.

**Within a document the check is exact**, which is what it exists for, and that
is now what the test exercises: two extrudes in one document have different
`ObjectId`s and the mismatch is refused. The cross-document case is recorded in
the limitations with its cause, rather than left as a test that asserts a
guarantee the design does not make.

The mesh **generation** stamp does protect every query: `MeshStamp::mesh` comes
from a process-local counter, so `mesh.owns(map.meshStamp())` is globally
unique and a map can never answer about a mesh it was not built for, in any
document.

---

## Finding 3 — ADR-032 assumes hole walls are named; they are not

**Severity: a documentation discrepancy with a real consequence. Recorded, not
silently worked around.**

ADR-032 says *"A hole's wall is boundary with a FaceName like any other
face."* Measured on the reference fixture, a `cutHole` wall carries **no
name**: `cutHole`'s namer answers for `HoleFace::{Bottom, CounterboreFloor,
SpotfaceFloor}` and returns `std::nullopt` for the wall segments, and a
cylindrical face has no `FaceSignature` either.

The ADR's claim does hold for a hole formed by a **profile** — a tube's bore is
swept by the profile's inner circle and is an ordinary `Side` face, confirmed
by the fixtures — so the sentence is true of the case the ADR probably had in
mind and false of a drilled hole.

**What was done.** Nothing to the naming infrastructure: extending it is
`P12-STREF` capability work that `TODO.md` does not authorize here. What was
done instead is to make the gap visible and harmless —

```text
the wall is fully mapped, and its facets answer the reverse query
the reverse answer carries an EMPTY name list, not a nearby face's name
the report counts it: unnamedFaceCount == 1
hole-wall FORWARD selection is demonstrated on the tube, where a bore IS named
```

— and to record the four files a fix would touch. `REFERENCE_AUDIT.md` §7 has
the detail.

---

## Mutation testing

Fifteen plausible mistakes, applied one at a time across the four files the
provenance chain runs through — the surface mesh, the volume mesh, the mapping
and its header — each followed by a rebuild and the **whole** `[meshing]`
suite, because an attribution mistake can break the surface or volume contracts
the mapping rests on and that is a kill worth seeing.

Harness, mutation list and raw log in `qualification/mutation/`.

```text
MUTATION                                                                       VERDICT
provenance: every triangle attributed to the first CAD face [SurfaceMesh.cpp]  killed
provenance: attribution dropped, so no facet has a CAD source [SurfaceMesh.cpp] killed
provenance: the surface's attribution not carried to the boundary [VolumeMesh.cpp] killed
currency: the geometry revision ignored when the map is built [GeometryMeshMap.cpp] killed
currency: the mesh generation ignored, so an old map answers a new mesh [GeometryMeshMap.cpp] killed
currency: a map accepted for a different feature [GeometryMeshMap.cpp]         killed
unresolved: an unresolvable reference reported as Resolved with no facets [GeometryMeshMap.cpp] killed
attribution: every facet pushed onto the first CAD face's list [GeometryMeshMap.cpp] killed
interior: a tetrahedron's interior face resolved to the first boundary triangle [GeometryMeshMap.cpp] killed
sets: a set may list the same face twice [GeometryMeshMap.cpp]                 killed
sets: the selector's own validity not checked [GeometryMeshMap.cpp]            killed
determinism: derived facet lists neither sorted nor deduplicated [GeometryMeshMap.cpp] killed
region: a boundary triangle accepted as a volume element [GeometryMeshMap.cpp] killed
nodes: a handle that is not a boundary triangle silently skipped [GeometryMeshMap.cpp] killed
report: an empty mapping reported as complete [GeometryMeshMap.hpp]            killed

15 applied, 15 KILLED BY A FAILING TEST, 0 killed only by the compiler,
0 survived.
```

Raw log: `qualification/mutation/results.txt`.

### What the first run found, and what became of it

The run above is against the committed tree. An earlier run, over seventeen
mutations, produced **three survivors and two compiler-only kills**, and
sorting out which were gaps and which were not is where the value was.

```text
SURVIVOR  a map accepted for a different feature
          A REAL TEST GAP, and Finding 4 below. Fixed; now killed by a test.

SURVIVOR  a facet with several owning tetrahedra resolved to the first
SURVIVOR  a CAD face with no facet no longer counted
          NOT gaps: both branches are unreachable through the public API.
          Diagnosed, documented, and removed from the list, because a mutation
          with a predetermined verdict reports a gap that is not one. See
          "Guards that cannot fire" below.

COMPILER  the mesh generation ignored
COMPILER  the selector's own validity not checked
          `if (false)` left a name unused under -Werror, so the mutant did not
          build, which shows the code would not compile rather than that a test
          would notice. Both rewritten as `&& false` so every name stays used,
          and both are now killed by a test.
```

One further guard was never in the list, for the same reason as the two
removed: deleting `triangulate`'s lockstep pairing check (Finding 1's fix)
would survive, because the pairing is correct today and the check exists to
notice a future OCCT change.

---

## Finding 4 — a check whose only contribution was a diagnostic nothing read

**Severity: a test gap, found by mutation. Closed.**

`buildGeometryMeshMap` refuses a mismatched pair in two steps: the geometry's
`source` against the mesh's, then the revisions. Deleting the **source** check
left the whole suite green.

The reason is instructive, and it is not that the check is useless. Within one
document a different feature already hashes to a different `GeometryRevision`,
because `ObjectId`s are mixed into it, so the second check refuses the same
cases. The source check's unique contribution is the **diagnostic**: it says
*which two things did not match*, by name, where a revision mismatch can only
report two opaque integers. And `Map_RefusesAMeshOfADifferentFeature` asserted
only that the refusal happened.

**Fix.** The test now requires the message to name **both** features and to say
what did not match. The check's actual contribution is what is tested, and the
mutation is killed.

This is the same shape as `P16-QUALITY-001`'s finding 3, and the pattern is
worth naming: **a correct check whose removal no test notices is not
necessarily dead code. Sometimes its value is the diagnostic, and the
diagnostic is what needs asserting.**

---

## Guards that cannot fire, and why they stay

Three branches cannot be reached through the public API. Each states an
invariant and would give a precise diagnostic if a future change broke it, so
deleting them to make a mutation table look tidy would be the wrong trade.
Naming them is the alternative to pretending they are covered.

```text
triangulate's lockstep pairing check
    `copier.ModifiedShape(original).IsSame(copy)` verifies that
    BRepBuilderAPI_Copy preserves face order. It does, so the check never
    fires. It is a guard against a FUTURE OCCT change, the equivalent of a
    static_assert about a pinned third party, and it is what makes the index
    contract checked rather than assumed. See Finding 1.

owningTetrahedraOf's "exactly one owner" branch
    a boundary facet is by definition a face of exactly one tetrahedron, and
    the only way to obtain a Triangle3 handle is from a VolumeMesh, whose
    triangles are its boundary. So "two owners, therefore interior" cannot
    occur. It stays because the alternative -- resolving to whichever
    tetrahedron came first -- is exactly what the brief forbids, and because a
    future mesh source holding interior surface elements would need it.

the facesWithoutFacets counter
    triangulate refuses a face it could not mesh, so every CAD face of a
    meshable body has at least one triangle. The counter exists so that an
    attribution chain which LOST a face is reported instead of passing as
    complete. The PREDICATE that consumes it is tested directly, by
    constructing the report, so the behaviour is covered even though the
    counting is not reachable.
```

---

## The brief's 24 attacks

**Can a viewer triangle ID become canonical boundary identity?** No — there is
no viewer triangle anywhere in the chain. `geometry::triangulate`'s output is a
triangle soup for display and export; the engineering surface is built from it
with unified nodes, and the mapping is keyed on `ElementId`s of that mesh.
`Map_IsUnchangedByADisplayTriangulation` asks the kernel for a 5 mm-deflection
tessellation between two mappings and requires the map to be byte-identical.

**Can a Netgen boundary tag leak into saved geometry identity?** No tag is
assigned, read or persisted; `NetgenBackend.cpp` is unchanged by this
milestone. The conformity gate makes the backend's own account of its surface
unnecessary, which is why there was nothing to leak.

**Can `NodeId` be treated as permanent face identity?** Node sets are
*derived*, from facets, which are derived from the geometry. There is no API
that accepts a `NodeId` as a selection, and `BoundarySetId`'s documentation
states that the mesh entities a set resolves to are not its identity.

**Can `ElementId` survive a remesh incorrectly?** Every query begins with
`mesh.owns(map.meshStamp())`, and a stamp comes from a process-local counter.
`MapRemesh_AMapIsRefusedAgainstADifferentMeshGeneration` crosses a coarse map
with a fine mesh and requires `mapping_stale`.

**Can coarse→fine remesh leave a named set pointing at old facet IDs?** A set
stores `FaceName`s and nothing else.
`BoundarySet_KeepsItsIdentityAndIntentAcrossARemesh` resolves one set against
both meshes and requires the id and the intent to be identical **and the facet
sets to differ**.

**Can a topology-changing edit silently rebind to the nearest face?** A blind
hole's bottom is drilled away and the reference comes back `Unresolved` with an
empty face list — `MapTopologyChange_ADeletedFaceBecomesUnresolvedAndIsNever
Rebound`. There is no geometric fallback to rebind *with*.

**Can two coincident CAD faces be merged because coordinates match?** No
coordinate is consulted. The nearest live case is a tube's two coaxial
cylinders, distinguished only by radius: `MapTube_OuterWallAndBoreStayDistinct`
requires disjoint facet sets and a reverse answer naming the bore and not the
wall.

**Can a cylindrical side selection include end-cap facets?**
`MapCylinder_LateralFaceExcludesTheCapsAndTheCapsExcludeTheWall` checks both
directions, requires the three sets to be pairwise disjoint and to cover the
boundary exactly, and additionally requires every wall facet's normal to be
radial (`|n_z| < 1e-6`) and every cap facet's to be vertical.

**Can a hole-wall selection include outer-wall facets?** See the tube case
above, plus `BoundarySet_OfABoreMapsOnlyToTheBore`.

**Can the inner hole-wall normal confuse mapping?** The mapping never uses a
normal. The *test* checks the thing that would be confused:
`MapTube_BoreIsSelectableAndMapsOnlyToTheBore` requires each bore facet's
outward normal to point **toward the axis**, because out of the material on a
bore is into the void — `dot(normal, centroid) < 0`.

**Can transformed geometry map in the old coordinate frame?**
`MapTransform_FollowsTheBodyIntoItsNewFrame` maps the same block on the XY and
XZ planes and checks every facet against the kernel's `FaceInfo` **in the
transformed frame**, then requires the two bodies' end-cap normals to point
along different axes (+z and −y) so a stale frame cannot pass.

**Can a stale mesh still answer current mapping queries?** No: the map cannot
be built at all. `Map_RefusesAMeshBuiltFromAnEarlierGeometryRevision` edits the
block's depth and requires the construction to be refused.

**Can an interior Tet face acquire a CAD boundary face?**
`MapFacet_EveryTetrahedronFaceIsEitherBoundaryOrExplicitlyInterior` asks all
four faces of all twelve tetrahedra and requires every answer to be either a
boundary facet or `no_boundary_correspondence` naming it *interior* — and
requires the boundary count to equal the mesh's own, which also proves this
layer's winding table agrees with `tetrahedralBoundary`'s.

**Can a valid CAD face map to zero facets and still PASS?** No:
`facesWithoutFacets` is counted, an issue names the face, and `complete()`
requires it to be zero. `MapReport_AnEmptyMappingIsNotComplete` pins the
predicate, including that a report with no facets at all is not complete.

**Can boundary facets remain unmapped without the report failing?**
`unmappedFacetCount` is computed by walking the mesh's triangles and asking
whether each was attributed; `complete()` requires zero.

**Can one facet map to two CAD faces silently?** `facetsWithSeveralFaces` is
counted from the per-face lists — so the inversion is checked rather than
trusted — and `complete()` requires zero.
`MapBox_PartitionsTheBoundaryWithNoGapAndNoOverlap` independently requires the
six faces' facet sets to be pairwise disjoint and to union to every boundary
facet.

**Can unordered containers change mapping order?** Nothing is built from a hash
container. The attribution is a `std::map<ElementId, std::size_t>`, faces are
in `listFaces` order, every derived list is sorted and deduplicated, and the
report's issues are sorted by a total key. `Map_IsIdenticalOnRepeatedConstruc
tion` compares whole maps five times over.

**Can local sizing use a different reference system from P16-MAP?** Both take a
`FaceName` and both resolve it with `geometry::findNamedFaces`.
`MapLocalSize_UsesTheSameCanonicalReferenceAsSizing` requires the value sizing
recorded and the value the mapping resolved to be the same object. Their
*resolution mechanisms* do differ, and that is recorded below.

**Can named boundary sets persist facet IDs instead of geometry intent?**
`NamedBoundarySet` has three fields: an id, a name and `std::vector<FaceName>`.
There is nowhere to put a facet.

**Can material changes alter correspondence?**
`Map_IsUnchangedByMaterialAssignment` creates a material, assigns it,
regenerates, and requires the map to be unchanged.

**Can quality evaluation mutate correspondence?**
`Map_IsUnchangedByEvaluatingMeshQuality` runs the full quality evaluation
between two mappings and requires equality. P16-QUALITY's own compile-failure
cases independently prove it cannot mutate a mesh.

**Can future P17 need Netgen APIs to resolve boundary sets?** No: a
`GeometryMeshMap` is a value built from a `MeshableGeometry` and a
`VolumeMesh`, and every query is answered from it. Nothing in the public
mapping API mentions a backend.

**Can P16 accidentally claim semantic face persistence that only P21 should
provide?** The only thing that resolves a reference is a by-value name match
against what the kernel's history carried. There is no same-normal,
similar-area or nearest-centroid rule to make such a claim with, and the
mutation that would introduce one (attributing every triangle to the first CAD
face) is killed by 105 assertions.

---

## The lifecycle's questions

**What did we assume?** That `Mesh::faces[i]` corresponds to `listFaces()[i]`.
That was the audit's one open question, and rather than assume it the pairing
is now **verified on every call** through the copier's history. See Finding 1.

**What case is missing?** Edge mapping, deliberately and with the reason
written down (`MAPPING_CONTRACT.md`): there is no CAD-edge provenance in the
per-face triangulation path, P17 has stated no edge requirement, and the only
alternative implementation would be the nearest-edge heuristic the brief
forbids.

**Could this pass its tests and still be geometrically wrong?** The tests judge
the mapping **geometrically**, from the kernel's own `FaceInfo`, while the
mapping decides by provenance — so the two are independent. The strongest of
them is the complete-partition test: every boundary facet attributed exactly
once, the union equal to the whole boundary, every facet on the face it claims.

**Are the expected values independent?** They come from the kernel's surface
descriptions and from closed-form geometry (x = 30 mm, r = 6 mm, normals along
axes), never from the mapping.

**Did we weaken a test or move a tolerance?** No tolerance was changed, and the
mapping has none to change. The tests use one epsilon, `1e-9 m`, with its
source stated: P16-SURF-001 measured triangulation nodes on a cylinder at the
true radius within that figure, and planar faces are exact.

**Is there hidden global state?** No. Every entry point is a pure function of
its arguments.

**Can save/load or undo/redo change the result?** Nothing here is persisted. A
map is derived from a geometry and a mesh, and a mesh is never written to a
document file. `NamedBoundarySet` is value-semantic and comparable
specifically so that `P16-CMD-001` and `P16-PERSIST-001` can be added without
this type changing.

**Can a parameter change leave stale geometry?** The map cannot be built from
stale geometry: `geometryMeshMapFor` goes through `requireMeshableGeometry` and
returns its refusal unchanged, and `buildGeometryMeshMap` additionally compares
the revisions.

**Can a stable reference bind to the wrong face?** Only by a geometric rule,
and there is none.

**Could Debug and Release differ?** Nothing depends on an unordered container,
timing, a random seed, thread scheduling, path order or locale. The full
three-preset qualification is the answer.

**Can a failure leave partial state committed?** `buildGeometryMeshMap` returns
a `Result`; a refusal returns no map at all, so there is no partially current
map to publish or cache. There is no cache: the map is cheap to rebuild and
keyed by nothing, which is simpler than a cache keyed by revision and stamp
would be.

**Did we cross an architectural boundary or widen the scope?**
`src/meshing/GeometryMeshMap.cpp` includes its own header and the standard
library. The header reaches `core` and its own module. No OCCT, no Qt, no
backend header. `architecture.layering` passes. The geometry-layer change is
inside `src/core/geometry/occt/`, where OCCT is already contained.

Scope beyond the mapping itself: a verified index contract in `triangulate`;
per-face triangle groups on the engineering surface; facet attribution on the
volume mesh; `Mesh::findTriangle`/`findTetrahedron`, the siblings of the
existing `findNode`; and `BoundarySetId` in `core/Id.hpp`. Each is used by this
milestone. No commands, no persistence, no CLI, no GUI, no FEA.

---

## Carried limitations

```text
A DRILLED HOLE'S WALL HAS NO CANONICAL REFERENCE
    cutHole names a hole's flat faces only, and a cylindrical face has no
    FaceSignature, so the wall cannot be the target of a forward query. It is
    fully mapped and answers in reverse with an empty name list, and the report
    counts it. A fix is P12-STREF capability work touching HoleFace, FaceRole,
    OcctHole.cpp and the JSON role mapping. ADR-032's text assumes otherwise;
    see Finding 3.

TWO DOCUMENTS CANNOT BE TOLD APART
    ObjectIds and revision counters are per document, so two structurally
    identical documents produce the same source and the same GeometryRevision.
    Within a document the check is exact. Every QUERY is protected regardless,
    by the globally unique mesh stamp. See Finding 2.

EDGE MAPPING IS NOT IMPLEMENTED
    No CAD-edge provenance survives the per-face triangulation path, and P17
    has stated no edge requirement. The decision and both rejected
    implementations are recorded in MAPPING_CONTRACT.md.

P16-SIZE-001 STILL RESOLVES A FACE GEOMETRICALLY
    Both use the same canonical FaceName and the same findNamedFaces, so the
    reference contract is shared and tested. But sizing then selects mesh nodes
    by testing them against the face's plane or cylinder, which its own
    limitations already record as unable to distinguish coplanar faces facing
    the same way. Re-basing sizing on this milestone's provenance would be a
    change to qualified sizing behaviour and is a scope decision, not this
    milestone's to take.

THREE GUARDS CANNOT FIRE THROUGH THE PUBLIC API
    Named and examined above. Kept deliberately, excluded from the mutation
    list deliberately, and not counted as tested branches.

NO SANITIZER COVERAGE
    This MinGW ships no libasan or libubsan. Carried.
```
