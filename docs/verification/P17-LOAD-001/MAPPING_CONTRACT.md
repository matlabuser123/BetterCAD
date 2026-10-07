# P17-LOAD-001 — the P16 mapping contract, audited

```text
SUBJECT:  what P16 actually provides, read from the headers and the sources
          before any load code was written -- and the two things the brief
          assumes that do not exist
```

## The authority matrix

| Concept | Actual type | Canonical? | Persisted? | Mesh-local? | P17-LOAD uses it as |
| --- | --- | --- | --- | --- | --- |
| `LoadId` | `Id<LoadIdTag>`, `core/Id.hpp` | yes | yes | no | the load's identity, from P17-DATA-001. There is no second load identifier |
| `FaceName` | `{ObjectId, FaceSelector}`, `core/document/References.hpp` | **yes** | yes | no | **the only canonical load target** |
| `NamedBoundarySet` | `{BoundarySetId, name, vector<FaceName>}` | yes | yes | no | available and **not used yet** — see below |
| `GeometryMeshMap` | `meshing` | no | no | yes | the resolver, consulted on every preparation |
| `BoundaryFacetSet` | `{requested, facets}` | no | **no** | yes | derived, discarded after integration |
| `ElementId` (facet) | `meshing::ElementId` | no | **never** | yes | derived; appears in no load record |
| `NodeId` | `meshing::NodeId` | no | **never** | yes | derived for face loads; carried ONLY by the explicitly mesh-local `NodalForceLoad`, with its `MeshStamp` |
| nodal force field | `PreparedLoads` | no | no | yes | derived and disposable |

`grep` over the canonical schema confirms the third and fourth rows: the only
non-comment `NodeId` in `StructuralLoad.hpp` is `NodalForceLoad::node`, and
there is no `ElementId` at all. Asserted at compile time as well — see
`LOAD_SCHEMA.md`.

## The two states the brief assumes, which do not exist

```cpp
enum class MappingState : std::uint8_t {
    Resolved,
    Unresolved,
};
```

Two values, and the header says why each alternative is absent:

> a `FaceName` ... can **be ambiguous, which P12-STREF-001 documents, and that
> is exactly why this layer does not map through one.**
>
> `Unsupported` does not appear either. P16-SIZE-001 needs it because only a
> planar or cylindrical face can become a sizing region; attribution needs no
> surface kind at all, so **a cone, a sphere, a torus and a B-spline map
> exactly as a plane does.**

So the brief's §73 ("if P16 returns `Ambiguous`, load preparation fails") and
its `LoadTargetAmbiguous` and `LoadTargetUnsupported` diagnostics describe
states that **cannot occur**. `LoadProblem` therefore has neither, and that is
the same discipline P17-ARCH-001 applied when it found three unreachable values
in its own first draft and deleted them rather than shipping them as
placeholders.

What exists instead, and is reachable:

```text
TargetUnresolved       the reference names no face of the current body. This is
                       also where an unnamed face lands -- including a drilled
                       hole's wall
TargetInvalid          the FaceSelector is malformed on its own terms (core's
                       validate(FaceSelector))
TargetWithoutFacets    resolved, and the mapping gave it no facet
```

## Stale mapping: refused upstream, not here

The brief's §16 requires that a mapping belonging to `G1/M1` be rejected when
the analysis is on `G2/M2`. It is — by `requireStructuralModel`, and
**possession of a `StructuralModel` is the proof** (ADR-036):

```text
the control exists
the body is eligible -- regenerated, current, a solid, non-empty, valid
a mesh is held and its currency is Current
the mapping and the quality report came from THAT SAME MESH, in one lookup
the material resolves
```

`prepareStructuralLoads` takes a `StructuralModel`, so a stale map cannot be
presented to it and a `LoadProblem::Stale` would be a value nothing returns.
`InputProblem::MeshStale` is where that refusal lives and P17-ARCH-001 tests
it.

That is why the load module performs **no** currency check of its own: adding
one would be a second gate for a condition the type system already excludes.

## The resolution path

```text
FaceName
  -> meshing::boundaryFacetsOf(map, face)      Result<BoundaryFacetSet>
       fails ONLY for a malformed selector; an unresolvable reference is a
       STATUS it reports, which is why TargetUnresolved is read from
       fullyResolved() and not from the Result
  -> set.facets                                ascending, deduplicated
  -> mesh.findTriangle(id)->nodes              three handles, P16's winding
  -> mesh.findNode(...)->position              coordinates
  -> facetAreaVector / facetNodalForce         integration
```

**Nothing in that chain classifies geometry.** Searched over
`src/structural/StructuralLoad.cpp`: no `tolerance`, no `nearest`, no
`distance`, no `centroid`, no normal-similarity test, no surface-kind test.
Facet geometry is read only after `boundaryFacetsOf` has answered, and then
only to integrate over it.

## Facet orientation: traced, not assumed

Pressure needs an outward normal, and `Mesh.hpp` is explicit that the data
layer does **not** canonicalise triangle winding — "whether a given triangle
faces out of the material is decided against the CAD face in P16-MAP-001". So
the guarantee had to be traced. It is, through three places:

```text
src/meshing/VolumeMesh.cpp, kTetFaces
    "With a positive signed volume on (n0,n1,n2,n3), these windings give
     outward normals. They are written out rather than generated, because an
     index permutation produced by a loop is exactly the kind of thing that is
     wrong in one of four cases and still passes a test that only counts
     faces."

tetrahedralBoundary
    "The stored value keeps the outward winding of the first tetrahedron that
     claimed it."

generateVolumeMesh
    "The boundary triangles go into the mesh with the winding the tetrahedra
     imply."

MeshValidation
    refuses any tetrahedron whose signed volume is not positive
```

So a `VolumeMesh`'s boundary triangles **are** outward-wound, as a consequence
of four separate facts rather than one documented promise. Because it is
derived rather than stated, it is **checked**:
`StructuralLoad_BoundaryFacetWindingIsOutwardOfTheOwningTetrahedron` walks
every boundary facet of a meshed block, finds the tetrahedron that owns it, and
requires the area vector's dot product with the vector to the opposite node to
be **negative** — that is, the facet faces away from the material. If the chain
ever breaks, every pressure in the system has the wrong sign, and the failure
is P16's and visible.

## `NamedBoundarySet`: available, and deliberately not used yet

P16 provides `NamedBoundarySet`, `resolveBoundarySet` and `ResolvedBoundarySet`,
and the brief's §14 and §71 ask for a load that targets one. It is **not** used
by this milestone, and that is a scope decision rather than an oversight:

```text
a NamedBoundarySet lives in MeshControlDefinition::boundarySets, which is
  MESHING intent. A load that targeted one would make the structural analysis
  depend on a set the mesh control owns, and editing the mesh control's named
  sets would then change the load -- a coupling no milestone has authorized
a set is a UNION of FaceNames, so targeting one is exactly equivalent to
  several loads on the member faces, which superpose. Nothing is unreachable
  without it
the reference models carry no committed named set for a structural purpose
  (P16's own sets are meshing-side), so there is no fixture to qualify it
  against
```

Recorded as a known limitation with its reason. `P17-BC-001` is the milestone
with the natural need — a restraint on "fixed_end" is the example P16's own
header gives — and it can decide where the canonical set should live.

## The drilled-hole wall

`cutHole` names a hole's flat faces and **not** its cylindrical wall
(P16-MAP-001's recorded limitation; `MappedFace::names` "MAY BE EMPTY, and that
is a real state"). So there is no `FaceName` for that wall and it cannot be a
load target.

What P17-LOAD does about it: **nothing**, which is the correct answer. A
reference aimed at an unnamed face resolves to no face and the load is refused
as `TargetUnresolved`. There is no nearest-face fallback, no cylinder
detection, no centroid test and no facet-index persistence — searched and
confirmed.

And the refusal is tested against its own control: RM-MESH-03's hole wall **is**
nameable, because its circle is in the profile sketch, so the same geometric
kind succeeds when the naming chain gave it a reference. That is what makes the
refusal a P16 limitation rather than a P17 defect.

```text
target                              a Side face of an entity the profile
                                    does not contain
canonical FaceName available        NO
P17 geometric fallback              NO
result                              TargetUnresolved, refused
the same kind of wall, named        RM-MESH-03's holeWall() -- ACCEPTED,
                                    with facets and a positive area
```
