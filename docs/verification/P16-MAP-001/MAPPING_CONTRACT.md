# P16-MAP-001 — the correspondence contract

```text
SUBJECT:  what the mapping guarantees, how it decides, and where its limits are
DATE:     2026-10-03
```

## The chain, and that it has no tolerance

```text
FaceName                              canonical, persistable, never a mesh entity
    | geometry::findNamedFaces, matched BY VALUE against the names the
    | kernel's history carried
CAD face i of listFaces(body)
    | the kernel triangulates face by face
geometry::Mesh::faces[i]              {firstTriangle, triangleCount}
    | carried through node unification and the deterministic re-sort
EngineeringSurfaceMesh::faceTriangles[i]   surface Triangle3 ElementIds
    | carried by EXACT POSITION -- see below
VolumeMesh::boundarySourceFaces       facet ElementId -> CAD face index
    | inverted, and the inversion checked
GeometryMeshMap::faces()[i].facets    current boundary facets of CAD face i
```

**Not one step uses a tolerance, a distance or a normal comparison.** The last
link is exact because `generateVolumeMesh` already refuses a result whose
boundary is not positionally identical to the surface it was given, in both
directions, with an exact position key. So the carry is a lookup, not a match.

That is the whole reason two coincident CAD faces stay distinct and the reason
there is no epsilon to defend.

## Why provenance, and what was rejected

Three candidates. ADR-032 had already chosen the reference type; what remained
open was the attribution mechanism.

```text
A  GENERATION PROVENANCE                                          CHOSEN
   The kernel triangulates per face, so record which group each
   triangle came from and carry it.
   + exact: no tolerance exists to be wrong
   + tells two coincident faces apart, which no geometric rule can
   + works for ANY surface kind -- cone, sphere, torus, B-spline --
     because it never looks at the surface
   + no backend involvement at all
   - needs the grouping carried through two transformations that
     would otherwise destroy it, and a checkable link from a group
     to a CAD face (REFERENCE_AUDIT.md section 8)

B  BACKEND BOUNDARY MARKERS                                     REJECTED
   Tag the surface elements handed to Netgen and read the tags
   back off the boundary elements it returns.
   - puts a backend concept inside the attribution chain, which
     ADR-033 contains for exactly this reason
   - rests on undocumented behaviour of a pinned third party, to be
     re-audited at every upgrade
   - and it is unnecessary: the conformity gate already makes the
     boundary positionally identical to BetterCAD's own surface, so
     the answer is known without asking the backend

C  GEOMETRIC CLASSIFICATION AT QUERY TIME                       REJECTED
   Resolve the FaceName to a FaceInfo and test each facet against
   its plane or cylinder.
   - needs a tolerance, and the tolerance is the answer
   - CANNOT tell two coincident CAD faces apart, which matters for
     future contact and interface models
   - at a sharp edge a facet's vertices satisfy BOTH faces' planes
   - only planar and cylindrical faces have anything to test against
   = this is what P16-SIZE-001 does, and its own known limitations
     record the consequence: "coplanar faces facing the same way are
     not distinguished"
```

**What would have made B right:** a backend that retriangulated its boundary.
Then the volume boundary would not be the surface BetterCAD supplied, the
positional carry would be impossible, and marker propagation would be the only
exact route left. `generateVolumeMesh` would also have to stop refusing a
non-conforming boundary, so this is a visible, gated change rather than a quiet
drift.

**What would have made C right:** nothing, for identity. It stays the right
tool for a *geometric* selection, which is what `FaceSignature` is for.

## States

```text
MappingState::Resolved      the reference names at least one face of the
                            current body
MappingState::Unresolved    it names none. The reference is KEPT and reported
```

**Two states, and the absence of a third is a finding rather than an
omission.**

```text
Ambiguous    NOT REACHABLE through this layer's reference type. A FaceName is
             matched by value, and a name on several faces denotes all of them
             -- a union with a reported multiplicity, not a choice between
             candidates. A geometric reference genuinely can be ambiguous,
             which is one more reason the mapping does not use one.
Unsupported  NOT REACHABLE either. P16-SIZE-001 needs it because only a planar
             or cylindrical face can become a sizing region; attribution needs
             no surface kind, so a cone, a sphere, a torus and a B-spline map
             exactly as a plane does.
```

An enumerator that cannot occur would be a state nothing can test, so neither
was added.

### Resolved with zero facets is not the same as Unresolved

A reference that resolves to a CAD face which produced no facet comes back
`Resolved` with an empty facet list, and the **report** counts it:
`facesWithoutFacets`, with a `FaceWithoutFacets` issue naming the face.
`complete()` requires that count to be zero. So the distinction is kept —
a deleted face and an unmeshed face are different answers — and an unmeshed
face still fails the completeness gate rather than passing quietly.

## Diagnostics

```text
GeometryReferenceInvalid       the FaceSelector is malformed on its own terms;
                               core's own validate(), not a second opinion
GeometryReferenceUnresolved    the reference names no face of the current body
MappingStale                   the mesh was not built from the geometry that is
                               current now, or is a different generation from
                               the map's
MeshFacetInvalid               an element handle that is not a boundary
                               triangle of this mesh, including one that names
                               a tetrahedron, and an out-of-range face ordinal
NoBoundaryCorrespondence       a tetrahedron face that is interior, or a
                               boundary triangle the chain carried no
                               attribution for
FaceWithoutFacets              a CAD face the mesh gave no facet
FacetAttributedTwice           a facet attributed to more than one CAD face
```

Structured, with the reference, element or face index the issue is about;
`message` is a fallback for logs, not the payload.

### Precedence

**Currency first, always.** Every query begins with the currency check, so a
stale map never gets as far as classifying a reference. Asking whether a
reference resolves against a mesh that is not about the current model would
produce an answer that looks current.

## Identity: what is what

```text
FaceName            canonical. Persistable. Never a mesh entity.
BoundarySetId       canonical. The identity of a named set; its DISPLAY NAME is
                    not, so two sets may share a name and stay distinct.
listFaces() index   a CORRELATION HANDLE for one map on one body. Not
                    persisted, not comparable between maps, and listFaces
                    documents that the kernel's order carries no meaning.
ElementId (facet)   mesh-local. Valid for ONE generation of one mesh. A remesh
                    invalidates it.
NodeId              mesh-local, the same.
RegionId            mesh-local (ADR-032). A region's CAD identity is the
                    feature whose body it is.
Netgen markers      do not exist in this chain.
viewer triangles    do not exist in this chain.
```

**Nothing that a remesh invalidates is ever canonical.** A boundary set stores
`FaceName`s; its facets, nodes and owning elements are recomputed for whatever
mesh is current.

## Boundary facet identity

A boundary facet **is** a `Triangle3` element of the volume mesh, named by its
existing `ElementId`. No new strong ID was introduced: the volume mesh already
stores its boundary triangles with the windings the tetrahedra imply, and
`ElementId` is already a robust current-mesh identity resolved by binary search
over ascending storage, never by indexing.

For completeness a facet can also be named as `TetrahedronFace{tetrahedron,
ordinal}` — which is how the **interior** case is asked about at all, since an
interior face has no `ElementId`. Membership is decided by the mesh's own
connectivity, with the same four-face winding table `tetrahedralBoundary` uses,
and the agreement between the two tables is tested exhaustively rather than
asserted.

## Volume region

```text
P16 meshes ONE SOLID: generateVolumeMesh refuses a body holding more, so there
is one region, and there is no second one to name.
```

`MappedRegion` carries the region's **CAD identity** (the feature's `ObjectId`)
and its mesh-local `RegionId` as separate things, plus every `Tet4` of the
mesh. `regionSourceOf` answers element → CAD identity, and refuses a boundary
triangle, which is not a volume element.

It carries **no material**. P15 assigns materials to a document; combining a
material with a geometric region is P17's, and doing it here would make the two
one object.

Disconnected solids need no multi-region semantics here, because the geometry
is refused upstream.

## Edge mapping — FOUNDATION ONLY, with the reason

**Not implemented, and not because it was awkward.**

The surface mesh is generated from a *per-face* triangulation. A node on an
edge shared by two faces is unified by position, so it is shared — but **which
CAD edge it lies on is recorded nowhere**, because the kernel's per-face
triangulation does not report edge discretisation through this path. So an edge
mapping today would have exactly two possible implementations:

```text
(a) add edge provenance to the geometry layer -- a real change to the adapter
    and to geometry::Mesh, for a consumer that does not exist yet
(b) decide a mesh edge lies on a CAD edge because it is NEAR one -- which the
    brief forbids, and rightly: it is geometric classification with a tolerance
```

And P17 has stated no edge requirement: a line load, a symmetry edge and
edge-local refinement are all listed as *possible future* uses. Building (a)
now would be speculative, and (b) would be wrong.

**What exists as the foundation:** the node sets a face selection derives are
already the mechanism an edge selection would use, and the reference type
(`FaceName`'s edge equivalent) is `P12-STREF`'s to define. Recorded here so the
decision is explicit rather than silently absent.

## Node and element sets — IMPLEMENTED, derived

```text
boundaryNodesOf(map, mesh, facets)        -> ascending, deduplicated NodeIds
owningTetrahedraOf(map, mesh, facets)     -> ascending, deduplicated ElementIds
regionOf(map, mesh)                        -> every Tet4 of the one region
```

All derived from the facets, which are derived from the geometry. **There is no
independent geometric node selector**, so a node set cannot drift from the face
it is supposed to describe.

`owningTetrahedraOf` requires **exactly one** owner per facet and says so when
there is not: none means the facet is not a face of the volume at all, and two
means it is interior. Neither is resolved to whichever came first.

## Named boundary sets

```text
NamedBoundarySet { BoundarySetId id; std::string name; std::vector<FaceName> faces; }
    resolve -> ResolvedBoundarySet { id, name, BoundaryFacetSet mapping }
```

```text
CANONICAL          the id, the name, and the geometry selection
DERIVED            the facets, recomputed for whatever mesh is current
THE NAME IS NOT    the identity, so two sets may be called "fixed" and stay
                   distinct, and a set may be renamed and stay the same set
SEVERAL FACES      allowed; the set is their union, deduplicated and ascending
TWO SETS, ONE FACE allowed: two different physics legitimately refer to one
                   surface. Deduplicating a user's intent because the resolved
                   facets are equal would be deciding for them
ONE SET, ONE FACE  listed twice is REFUSED: it would make the set's own size
                   depend on how often it was written
UNRESOLVABLE       the set keeps its id, its name and every reference, and
                   reports Unresolved per reference. It does not disappear and
                   it is never rebound
```

No set algebra beyond the union. Intersections and differences are not needed
by anything and were not built.

## What P17 may rely on

```text
ALLOWED as canonical intent      FaceName
                                 NamedBoundarySet (id + name + FaceNames)
DERIVED, recompute per mesh      boundary facet ElementIds
                                 NodeIds
                                 owning Tet4 ElementIds
                                 the region's Tet4 ElementIds
FORBIDDEN as permanent intent    raw Netgen tags -- which do not exist here
                                 viewer triangle indices -- likewise
                                 NodeIds stored as a CAD boundary identity
                                 ElementIds stored across a remesh
```

And the flow P16-MAP exists for:

```text
load / restraint
    -> NamedBoundarySet or FaceName
    -> GeometryMeshMap against the current mesh
    -> current facet / node / element set
    -> P17 assembly
```

No step of that needs a backend API, and no step stores a handle a remesh
would invalidate.

## The P21 boundary

```text
P16 GUARANTEES
    the CURRENT regenerated CAD geometry reference
        <-> the CURRENT generated mesh entities
    under BetterCAD's existing stable-reference semantics (P12-STREF-001).

P16 DOES NOT GUARANTEE
    that an arbitrary topology-changing edit preserves a face's semantic
    identity.
```

Where the kernel's history carries a name through an operation, a reference
survives, and that is tested. Where it does not, the reference is `Unresolved`
and binds to nothing, and that is tested too. **There is no heuristic** — no
same-normal, similar-area, nearest-centroid rule — and that absence is the
point: such a rule would be a claim about permanent identity that only P21 can
make.

The failure this milestone guards against is not the inability to preserve an
arbitrary face. It is **pretending it was preserved.**
