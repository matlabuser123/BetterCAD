# P16-VOL-001 — architecture

```text
SUBJECT:  how the volume mesher is built, and what it deliberately does not add
RESULT:   one new seam, one new domain type, one new validation kind.
          The Tet4 element, its orientation convention, element validation and
          mesh identity are REUSED from P16-DATA-001, unchanged.
DATE:     2026-10-02
```

## The pipeline

```text
authoritative CAD geometry
        |                   requireMeshableGeometry   P16-GEOM-001
        |                   configuration override -> exists -> regeneration
        |                   state -> CURRENCY -> body -> solid -> volume
        v
MeshableGeometry            source, body, solidCount, volume, revision
        |
        |                   generateSurfaceMesh       P16-SURF-001
        |                   watertight, manifold, coherently oriented, or REFUSED
        v
EngineeringSurfaceMesh
        |
        |                   generateTetrahedra        the ADR-033 seam
        |                   Point3D + 0-based indices. No backend type crosses.
        v
VolumeBackendMesh           points, Tet4 connectivity
        |
        |                   generateVolumeMesh        P16-VOL-001
        |                   handles, validation, conformity, volume recovery
        v
VolumeMesh                  constructible only by the line above
```

## What was NOT written, because it already existed

This is the largest fact about the milestone, and it is the reason the diff is
small. P16-DATA-001 had already qualified everything the brief's data-model
sections describe:

```text
asked for                         already present, reused unchanged
---------------------------------  ----------------------------------------
Tet4 element with 4 node refs       meshing::Tetrahedron
NodeId / ElementId, mesh-local      MeshIds.hpp, ADR-031, with compile-fail
                                    proof that they are not CAD identities
V = 1/6 det(p2-p1, p3-p1, p4-p1)    meshing::signedVolume, exactly this
positive volume is valid            the convention, with no abs() anywhere
reject negative volume              MeshIssueKind::InvertedTetrahedron
reject zero volume                  MeshIssueKind::DegenerateTetrahedron
reject missing node reference       MeshIssueKind::MissingNodeReference, and
                                    MeshBuilder refuses it at add time
reject duplicate node in element    MeshIssueKind::RepeatedNodeReference, and
                                    MeshBuilder refuses it at add time
```

Writing a second `VolumeMesh`/`Tet4Element` pair beside these would have been
the duplicate-rule defect P16-GEOM-001 was already bitten by, where two
definitions of "is this body eligible" had drifted apart. One definition of
"is this tetrahedron valid" serves every producer, and the volume mesher is
just another caller of it.

**The one data-model gap was duplicate detection**, which P16-DATA-001
deferred with a documented reason. See VALIDATION.md.

## The three things this milestone adds

### 1. The backend seam — `VolumeBackend.hpp`

ADR-033: "No backend type appears in any BetterCAD API." The seam is
`Point3D` and `std::array<std::uint32_t, N>`, nothing else.

**Indices, not handles, and that is deliberate.** A backend knows nothing of
mesh identity and must not: handles belong to one generation of one mesh
(ADR-031), `MeshBuilder` assigns them, and letting a third-party library
choose them would hand it an invariant it cannot keep. So the backend cannot
produce a handle, valid or otherwise, and the mesher converts indices to
handles on the way out.

Containment is enforced, not asserted: `tests/architecture/CheckLayering.cmake`
rule 5 confines backend headers to `src/meshing/<backend>/` and two fixtures
prove it fires.

### 2. The domain layer — `VolumeMesh.hpp`

`VolumeMesh`'s constructor is **private** and `generateVolumeMesh` is its only
friend. Possessing one is the evidence that the mesh passed validation,
conformity and volume recovery, because there is no other way to obtain one.
Three compile-fail cases prove it:

```text
compile_fail.volumemesh.default-construct-volume-mesh
compile_fail.volumemesh.volume-mesh-from-plain-mesh
compile_fail.volumemesh.mutate-volume-mesh
```

This is ADR-030's mechanism — "enforced by the type system, not by
documentation" — applied to the only mesh kind a solver can currently be
offered. ADR-030's general `ValidatedMesh`, one token covering surface meshes
too, remains open; naming it belongs with the second kind of mesh that needs
it, not here.

`generateVolumeMesh` takes a `MeshableGeometry` rather than a loose volume and
feature id. That type already carries the source, the kernel volume, the
geometry revision and the solid count — all four of which are needed, and all
four of which are then guaranteed to describe the same body. Assembling them
from separate arguments is how a mesh ends up stamped with another feature's
revision; the adversarial review found exactly that hazard in the first
version of this API.

### 3. One new validation kind — `DuplicateTetrahedron`

In `validate()`, with the other whole-mesh check. See VALIDATION.md for why
there and not in P16-QUALITY-001.

## Decisions taken, and what they rejected

### Multiple solids are REFUSED

```text
A  support disconnected solids, one region each      REJECTED for now
B  refuse them explicitly                            CHOSEN
```

ADR-032 gives a mesh "one region per solid" and that remains the eventual
design. This milestone does not implement it, because `P16-SURF-001`
triangulates a whole body into a single region and separating solids is not
its job either. The options were therefore: mesh a two-solid body as one
region, which produces a mesh whose region is a lie; mesh only the first solid,
which is worse because it is silent; or refuse. Refusing is the only one with
no undefined behaviour, which is what the brief asks for.

`P16-GEOM-001` deliberately **accepts** multi-solid bodies and reports the
count, so this is a decision of this layer and not an accident of the one
below — a test asserts both halves of that.

No ADR: ADR-032 already states the eventual design and this does not
contradict it. An ADR recording "not yet" would be noise.

### Orientation is translated at the adapter, not canonicalised per element

Netgen orders a tetrahedron's nodes so that `det(p1-p0, p2-p0, p3-p0)` is
**negative** — the opposite of BetterCAD's convention. Measured, not assumed: a
20x30x40 box came back as 12 tetrahedra, 12 of them negative.

```text
A  abs() in signedVolume                      REJECTED. Destroys the only
                                              evidence that a generator
                                              produced an inverted element.
B  measure each element's sign and reorder     REJECTED. Cannot distinguish
   the ones that come out negative             "their convention" from "their
                                              bug", so it silently repairs a
                                              genuinely inverted element.
C  one fixed odd permutation at the adapter    CHOSEN.
```

C is what an adapter is for: ADR-033 says "the Mesh a backend produces is
BetterCAD's Mesh, translated at the boundary", and a node-ordering convention
is part of the representation being translated. Because the permutation is
fixed and unconditional, an element Netgen got *wrong* still arrives negative
and is still refused by `validate()` — the evidence survives.

The uniformity C depends on is pinned by a test
(`VolumeBackend_ReturnsEveryElementInBetterCadsOrientation`), so a future
Netgen that changed convention fails loudly instead of producing a mesh of
uniformly inverted elements.

**A single swap, because a swap is an ODD permutation.** Reversing all four
nodes would not work: `(0,1,2,3) -> (3,2,1,0)` is `(0 3)(1 2)`, two
transpositions, an even permutation that leaves the signed volume exactly as it
was. That mistake looks like a fix and changes nothing, and it is recorded here
because it has been made before in this repository.

### The surface's own validation report is not trusted

`SurfaceValidation` is a plain struct, so a caller can set every count to zero
and present a garbage mesh as a perfect boundary. `generateVolumeMesh`
therefore re-runs `validateSurface` over the triangles and uses what it finds,
and recomputes the enclosed volume the same way.

That is the difference between a gate and a label, and it is what makes
"a viewer tessellation cannot enter the solver path" structural rather than a
matter of which type name was used. A test presents a three-triangle open patch
with a clean report and watches it be refused.

## Layering

Nothing moved. `meshing` is layer 4; the backend adapter is a translation unit
inside it; `nglib` appears in exactly one file. `architecture.layering` passes
over 412 files with 0 violations.
