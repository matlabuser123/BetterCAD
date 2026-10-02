# P16-SIZE-001 — backend mapping

```text
SUBJECT:  exactly how canonical BetterCAD sizing intent becomes Netgen calls
RESULT:   one translation layer, in one file. No ambiguity about units or
          direction.
DATE:     2026-10-02
```

## The dependency direction

```text
MeshSizingControls              canonical, Length, persistable, value-semantic
        |                       include/bettercad/meshing/MeshSizing.hpp
        |  resolveSizing(body, surface, controls)
        v
ResolvedSizing                  still BetterCAD types: Length, Point3D
        |                       global bound + point restrictions + slabs
        |  VolumeBackendRequest
        v
NetgenBackend.cpp               THE ONLY FILE THAT NAMES A NETGEN SYMBOL
        |
        v
nglib
```

Nothing above `NetgenBackend.cpp` names a Netgen parameter, and nothing below
the seam sees a `FaceName`, a `NodeId` or a `MeshSizingControls`. ADR-033's
containment is enforced by `tests/architecture/CheckLayering.cmake` rule 5,
not asserted.

## Global target

```text
BetterCAD control   MeshSizingControls::globalTargetSize, type Length
                    (or BetterCAD's default when absent -- see below)
canonical units     SI metres internally; Length is dimensioned, so 10 mm and
                    0.01 m are the SAME VALUE and compare equal
Netgen control      Ng_RestrictMeshSizeGlobal(mesh, h)  -> Mesh::SetGlobalH
                    AND Ng_Meshing_Parameters::maxh
conversion          h = targetSize.si(), i.e. metres, no scaling
```

**Why both.** `Mesh::GetH` returns `min(hglob, localh(p))`, and only
`SetGlobalH` writes `hglob`; `maxh` is consulted for a per-domain maximum and
one local decision. Setting `maxh` alone did nothing at all — see
AUDIT.md Finding 1. `Ng_RestrictMeshSizeGlobal` is the one that works.

**Units are checked by test, not by inspection.**
`Sizing_IsUnitSafeSoTenMillimetresEqualsAHundredthOfAMetre` compares the two
spellings as intent and asserts the SI value is 0.01, and
`SizeGlobal_IsScaleRobust` meshes a 4 mm body at 1 mm and a 40 mm body at
10 mm and requires the median edge to differ by roughly ten, not by a
thousand.

## BetterCAD's default global target

A document with no sizing settings gets **the bounding-box diagonal of its
surface**, not Netgen's `maxh = 1000`.

```text
defaultGlobalTargetSize(bounds) = |max - min|
```

The decision the brief required is therefore **A, an explicit BetterCAD
default** — and deliberately not a constant. A constant in metres cannot serve
a 1 mm body and a 1 m body: one value is finer than the geometry and the other
coarser than it. The diagonal is scale-relative, imposes no practical
restriction (an element cannot exceed the body), and is **a number BetterCAD
chose**. `ResolvedSizing::globalIsDefault` records that it was used, so a
reader never has to wonder.

## Local target

```text
BetterCAD control   LocalMeshSizing{FaceName face, Length targetSize}
reference           FaceName = {ObjectId feature, FaceSelector role...} --
                    BetterCAD's persistent face identity (P12-STREF-001,
                    ADR-024). NEVER a NodeId, ElementId, viewer triangle index
                    or backend entity tag.
resolution          geometry::findNamedFaces(body, name) -> FaceInfo, whose
                    planar `signature` or `cylinder` says where the face is
region              the surface nodes ON that face, then their bounding box
                    extended INWARD along the face normal by 3 x targetSize
Netgen control      Ng_RestrictMeshSizePoint for each node, AND
                    Ng_RestrictMeshSizeBox for the slab
conversion          metres throughout; h = targetSize.si()
```

**Why a slab and not just points.** A face's nodes lie on a surface, so point
restrictions constrain a sheet the mesher barely samples: measured, 36 point
restrictions on a cylinder's top disc left the region *coarser* than no
control at all. **Why inward, and why three deep.** `Ng_RestrictMeshSizeBox`
walks from the box's minimum corner in steps of `h`, so a centred one-deep
slab put its first sample outside the material — which made a top face refine
and the opposite bottom face not. Both numbers come from measurement; see
AUDIT.md Findings 2 and 3.

The point restrictions are kept as well. They cost one call each, they
constrain the face surface exactly, and `RestrictLocalH` takes the smaller of
any two restrictions so they cannot conflict with the slab.

## What local sizing does NOT do, stated plainly

**It refines the volume, not the boundary.** The boundary is P16-SURF-001's,
fixed and validated before nglib is called, and nglib only fills it. So a
local control makes the tetrahedra near a face smaller; it does not
re-triangulate that face.

That is the approved architecture working as intended, and it is why the
Netgen OCC front end stays off: switching it on to obtain face-local surface
sizing would bypass the validated boundary, which ADR-033 forbids and which
this milestone does not do.

A consequence worth knowing: on a body whose boundary is coarse, a global
target has little room to act, because a tetrahedron cannot be smaller than
the triangles it must conform to. OCCT triangulates a **planar** face with two
triangles whatever the deflection, so a 40 mm block's boundary is irreducibly
40 mm. `SizeGlobal_CannotBeFinerThanTheBoundaryItMustConformTo` pins this so
that a future reader measuring a block and finding no effect learns why from
the suite rather than from a debugger.

## Precedence

```text
local overrides global in its region       because every backend restriction is
                                           a MAXIMUM and RestrictLocalH keeps
                                           the smaller
two controls on the SAME face              REFUSED by validate() as
                                           DuplicateFaceControl: two sizes for
                                           one face is a modelling mistake the
                                           author should see
two controls on DIFFERENT faces that
overlap                                    the smaller size wins at the shared
                                           nodes
```

**Order independence is by construction, not by rule.** The minimum of a set
does not depend on the order its members arrived in, so there is no insertion
order for the backend to honour or ignore. The canonical resolution also
*stores* its restrictions in a `std::map` keyed on position and sorts its
slabs, so the list handed to the backend is byte-identical however the
controls were written. `SizeLocal_ResolutionDoesNotDependOnControlOrder`
asserts the resolved lists are equal and the meshes identical for two
insertion orders.

## Validation, before anything expensive

`resolveSizing` runs `validate(controls)` first and returns
`InvalidArgument` if it fails, so there is no path on which an invalid
canonical request reaches geometry resolution, let alone the backend.

```text
checked without geometry   non-finite size (checked BEFORE non-positive, so a
                           NaN is reported as not-a-number rather than as
                           not-positive), non-positive size, malformed face
                           selector, two controls on one face
checked against geometry   the face resolves (else Unresolved), and is a kind
                           that can become a region (else Unsupported)
on any unresolved control  THE MESH IS REFUSED, not generated with the
                           remaining controls. A partially applied sizing
                           request would produce a mesh that is not the one
                           asked for while reporting success.
```
