# P16-MAP-001 — the audit this milestone rests on

```text
SUBJECT:  what BetterCAD already guarantees about geometry references, what
          provenance already exists, and what therefore did NOT have to be
          built
DATE:     2026-10-03
```

Read before any production code was written. **The headline is that almost
everything this milestone needs was already there and deliberately left for
it**, and that the one thing it needed and did not have — a checkable link from
a triangle group to a CAD face — was a five-line change rather than a new
subsystem.

## 1. The stable-reference infrastructure

```text
reference         domain  identity source            survives
                                                      simple    topology-
                                                      regen     preserving   topology-
                                                                edit         changing edit
FaceName          face    the feature that            yes       yes          ONLY where the
                          GENERATED it, its role                              kernel's history
                          there, the profile entity                           carries the name
                          that swept it, and the
                          chain of pattern/mirror
                          copies -- never an index
FaceSignature     planar  the supporting plane and    yes       yes, while   no, once the
                  face     the side the material is              the plane    plane moves
                           on                                    does not
                                                                 move
geometry::        face    position in TopExp_         NO -- and it is documented as
listFaces() order        Explorer order               carrying no meaning
```

**Ambiguity handling.** `FaceName` is many-to-many by design, and `Faces.hpp`
says why: *"a face split in two carries its name on both parts, faces merged
into one carry all their names, a face the operation removed carries none."* So
one name may legitimately denote several faces, and the honest answer to such a
query is their union with the multiplicity reported — not a choice between
candidates. `FaceSignature` genuinely is ambiguous for two coplanar faces
facing the same way, and `Faces.hpp` says so: *"a reference alone does not tell
them apart. Users of references say how they choose."*

**Current users.** `FaceName` is what hole features, chamfers, drawing
dimensions, stable drawing references and — decisively for this milestone —
`P16-SIZE-001`'s local sizing already use.

**Conclusion.** `FaceName` is the canonical geometry reference. P16-MAP adds
none, and `ADR-032` had already decided this: *"Local sizing, boundary
conditions, loads and restraints name CAD geometry — a FaceName."*

## 2. The selection model

A face is selected today by **a `FaceName`**, resolved with
`geometry::findNamedFaces(body, name)`, which matches names **by value** against
the names the kernel's history carried onto the body's faces. A planar face may
alternatively be selected by `FaceSignature` through
`geometry::findFaces(body, signature)`, which matches **geometrically**.

Nothing holds a `TopoDS_Shape`, a subshape index or a topology path outside the
OCCT adapter.

**P16-MAP consumes `FaceName`**, and only `FaceName`. Not because the geometric
route is unavailable, but because it is the one with documented ambiguity: a
mapping that resolved through a signature would inherit that ambiguity for no
gain, since every face a user can target by name is already nameable.

## 3. What `FaceRole` can and cannot name

```text
StartCap          an extrude's face on its sketch plane
EndCap            its face at its depth
Side              the face swept by ONE PROFILE ENTITY -- so each line of a
                  rectangle, and each circle of a profile, names its own face
HoleBottom        a blind hole's flat bottom
CounterboreFloor  a counterbored hole's flat floor
SpotfaceFloor     a spotfaced hole's flat seat
Chamfer           the face a chamfer cuts, by the chamfer's own edge-selection ID
```

**There is no `HoleWall`.** This is the audit's one consequential gap, and it
is examined in §7.

## 4. Surface provenance — ALREADY THERE, and left here on purpose

`geometry::Mesh` carries

```cpp
struct MeshFace { std::size_t firstTriangle; std::size_t triangleCount; };
std::vector<MeshFace> faces;   // one entry per CAD face
```

added by `P16-SURF-001`, whose own comment says it: *"It deliberately carries
NO FaceName: attributing a facet to a named face is P16-MAP-001's."* And
`TODO.md` recorded the same, under the candidates: *"P16-MAP-001 — P16-SURF
left per-face triangle groups for it."*

So the kernel's per-face grouping survives into the geometry layer. What it did
not have was a **checkable correspondence to a CAD face**, because
`triangulate` explored a `BRepBuilderAPI_Copy` of the body while `listFaces`
explored the original, and nothing said the two traversals agreed.

That gap is closed in §8.

**What is NOT recorded and was not added.** The surface mesh unifies nodes by
position and then re-sorts triangles by node-set key for determinism, both of
which destroy the grouping unless it is carried through them. It now is.
Reconstructing it afterwards was rejected: see MAPPING_CONTRACT.md.

## 5. Volume-boundary provenance — CASE A, and it is ENFORCED

The decisive finding of the audit.

`generateVolumeMesh` already computes the boundary from the tetrahedra's own
connectivity and compares it with the input surface **by exact position, in
both directions**:

```text
unmatched boundary faces  == 0   required
unmatched surface triangles == 0 required
otherwise -> VolumeMeshFailure::BoundaryNotConforming
```

and `keyOf` is exact — `PositionKey{x.si(), y.si(), z.si()}`, no quantisation.

So:

```text
volume boundary facet  ==  P16-SURF triangle      (the same three positions)
```

is not an assumption about Netgen. It is a **gate the volume mesher already
refuses to pass without**. If a backend ever retriangulated the boundary,
`generateVolumeMesh` would fail outright rather than quietly producing a
different one.

Two consequences:

- attribution propagates from the surface to the volume boundary by exact
  position, with **no tolerance anywhere in the chain**;
- the volume mesh already stores those boundary triangles as `Triangle3`
  elements with the tetrahedra's winding, so **a boundary facet's identity is
  an existing `ElementId`**. No new strong ID was added, which is what the
  brief asked: do not add one for symmetry when `ElementId` already is a
  robust current-mesh identity.

## 6. Netgen boundary markers — NOT NEEDED, and not used

nglib does expose surface-element handling, and the audit asked whether input
markers survive volume generation. **The question is moot**, because §5 makes
the boundary positionally identical to the surface BetterCAD supplied, so the
attribution is already known without asking the backend anything.

Deciding not to use them is the stronger position:

```text
a backend marker is a transient integer with no CAD meaning
whether it survives is undocumented behaviour of a pinned third party
it would have to be audited again at every Netgen upgrade
and it would put a backend concept inside the attribution chain
```

`src/meshing/netgen/NetgenBackend.cpp` is unchanged by this milestone. No
marker is assigned, read or persisted.

## 7. The hole wall — the audit's one real gap

`cutHole` builds the hole as a revolved cutter and names its faces through a
`SweptFaceNamer`:

```cpp
if (swept.segment == bottomSegment && blind)  return namer(HoleFace::Bottom);
if (swept.segment == 2 && counterbore)        return namer(HoleFace::CounterboreFloor);
if (swept.segment == 2 && spotface)           return namer(HoleFace::SpotfaceFloor);
return std::nullopt;                       // <- the WALL lands here
```

and `enum class HoleFace { Bottom, CounterboreFloor, SpotfaceFloor }` has no
wall. A cylindrical face also has no `FaceSignature`, because signatures are
planar only.

**So a drilled hole's wall has no canonical reference of any kind today.**
Measured on the reference fixture:

```text
BORED 40x30x10 mm, 12 mm through hole:  7 CAD faces, 160 boundary facets
  [0..2] plane      2 facets each   side/entity:5,8,6
  [3]    plane     40 facets        start_cap
  [4]    plane     40 facets        end_cap
  [5]    plane      2 facets        side/entity:7
  [6]    cylinder  72 facets        NO NAME        <- the hole wall
```

Note what is NOT wrong: the wall is **fully mapped**, its 72 facets answer the
reverse query, and the mapping is complete. What it cannot be is the *target*
of a forward query, because there is nothing to ask with.

**ADR-032 assumed otherwise**, and the discrepancy is worth recording: it says
*"A hole's wall is boundary with a FaceName like any other face."* That holds
for a hole formed by a **profile** — a tube's bore is swept by the profile's
inner circle and is an ordinary `Side` face, which the fixtures confirm — and
not for one formed by `cutHole`.

**What this milestone did about it: nothing, deliberately.** Adding
`HoleFace::Wall` and `FaceRole::HoleWall` would be a capability change to
`P12-STREF`'s naming, reaching `core/Id.hpp`'s role vocabulary, the geometry
adapter, the hole feature and the persisted JSON mapping. It is not
correspondence work, `TODO.md` does not authorize it here, and the brief's own
rules say to report what the infrastructure does rather than extend it quietly.
So:

```text
hole-wall FORWARD selection   demonstrated on the tube, whose bore IS named
hole-wall REVERSE mapping     demonstrated on the drilled plate, which has no
                              name to offer and says so with an empty name list
the gap                       recorded, with its exact cause and the four files
                              a fix would touch
```

## 8. The one change the audit forced

`triangulate` meshes a **copy** of the body, because the kernel caches
triangulations on faces and meshing the authoritative ones would make results
depend on earlier requests. `listFaces` explores the **original**. Nothing
connected the two traversals, so `Mesh::faces[i]` corresponded to
`listFaces()[i]` only by the copier happening to preserve face order.

**First attempt, and why it was wrong.** Iterating the original and fetching
each face's copy with `copier.ModifiedShape(face)` to read its triangulation.
That compiled and broke every surface mesh: `ModifiedShape` returns the copy
**without the original's orientation inside the shell**, so `face.Orientation()
== TopAbs_REVERSED` came out wrong for half the faces, their windings flipped,
and the surface stopped closing. Caught by `P16-SURF-001`'s own tests within a
minute of making the change.

**What was done instead.** The triangulation is read from the copy's own
exploration exactly as before — so the orientation, the location and every
qualified behaviour are untouched — and the original is explored *in lockstep*
purely to verify the pairing through the copier's history:

```cpp
if (!copier.ModifiedShape(originals.Current()).IsSame(faces.Current())) {
    return makeError(ErrorCode::Internal, "triangulation: the copy's faces are not in "
                     "the body's order, so a face group could not be attributed ...");
}
```

`IsSame` compares identity and ignores orientation, which is exactly the
comparison wanted. The index contract is now **checked on every call** instead
of assumed, and both lists are also required to have the same length.

This is a guard against a future OCCT change rather than a live path: the
pairing is correct today, so the check never fires. It is labelled as such in
the source and in the known limitations, rather than presented as a tested
branch.
