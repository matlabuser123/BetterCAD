# P16-REFMOD-001 — the reference suite contract

```text
SUBJECT:  what each model is, what it is for, and what it is expected to do
SOURCE:   examples/reference_models/MeshReferenceModels.hpp  (the declaration)
          examples/reference_models/MeshModels.cpp           (the builders)
          tests/reference/MeshModelsTests.cpp                (the gates)
          tests/reference/MeshTestSupport.hpp                (the shared checks)
```

**Eight mandatory model IDs in nine documents.** RM-MESH-06 is a pair — a body
and the same body rigidly transformed — because comparing the two *is* the case.
P15's catalog does the same thing, with three entries for RM-MAT-04.

Every model is built only through the public document, parameter, sketch,
feature and meshing APIs, in a library the architecture test checks "like any
client". Every one carries its meshing intent as a `MeshControl` document
object, **including RM-MESH-08** — without one, its headless refusal would name
a missing control rather than the broken body, which is precisely the defect
P16-CLI-001's mutation M6 found.

## The contract every model states

```text
model ID            RM-MESH-nn
document name       the .bcad's document name
file stem           what the runner writes with --out
purpose             one line: the defect this model makes impossible to hide
expected outcome    a mesh, or an explicit refusal
analytic volume     the closed form, declared -- and independently re-derived
                    from the model's own parameters by the suite
sizing controls     global target, and any face-local target
boundary regions    the named sets, each with its own BoundarySetId
main parameter      what the model-change gate changes, and to what
```

## RM-MESH-01 — rectangular block

```text
geometry      120 x 70 x 35 mm, one corner at the origin, extruded +Z from XY
analytic      V = abc = 294000 mm^3                              EXACT
sizing        global 20 mm; surface deflection at the layer's default
regions       fixed_end  (start cap, z = 0)
              loaded_end (end cap, z = 35)
outcome       a mesh
main param    block_a, 120 -> 150 mm  (V = 367500 mm^3)
```

**Three different edge lengths deliberately.** A cube would let an axis
permutation, a transposed bounding box or a swapped sketch axis pass every other
check in the suite. The suite asserts `a != b != c` rather than trusting the
builder.

It is the only model whose **six faces are all nameable and all planar**, which
makes it the one that proves a complete boundary partition face by face — and
the one where mesh volume and CAD volume must agree to arithmetic accumulation
rather than to an approximation bound, because its facets tile the exact solid.

## RM-MESH-02 — cylinder

```text
geometry      r = 25 mm, h = 60 mm, axis +Z from z = 0
analytic      V = pi r^2 h = 117809.72450961724 mm^3
sizing        global 12 mm; surface deflection 0.25 mm, angular 20 deg
regions       bottom_cap, top_cap, lateral_wall
outcome       a mesh, BELOW the analytic volume
main param    cyl_h, 60 -> 75 mm
```

**h != 2r on purpose.** At h = 50 the body would be as tall as it is wide, and a
product that transposed the radius and the height would give the same volume and
the same bounding box.

Curved, so this is where the convergence study lives: the suite varies the
**surface deflection** across three levels, because for a curved body the volume
error is the chord error of the boundary and the volume target cannot touch it.

## RM-MESH-03 — plate with a through-hole

```text
geometry      100 x 60 x 12 mm plate, 15 mm radius hole at (50, 30), through
analytic      V = LWt - pi r^2 t = 63517.699835307561 mm^3
sizing        global 12 mm; surface deflection 0.25 mm
regions       hole_wall      (the inner cylindrical face)
              clamped_faces  (two opposite outer sides -- a two-face set)
outcome       a mesh, ABOVE the analytic volume
main param    plate_t, 12 -> 16 mm
```

**The hole is an inner loop of the profile sketch, not a drilled feature, and
that is forced rather than chosen.** `cutHole` names a hole's flat faces and not
its cylindrical wall, so a drilled hole's wall carries no `FaceName` and could
not be the target of a forward query. Swept by a profile circle, the wall is
`FaceRole::Side` of that circle. Measured: every CAD face of this model is
named, `unnamed_faces 0`.

The hole is 13.35% of the body, and the mesh must come out **above** the analytic
volume because an inscribed chord polygon removes less material than the true
circle. That is the opposite direction from RM-MESH-02's error, on the same
geometric primitive — a check no single-sided tolerance makes.

## RM-MESH-04 — hollow tube

```text
geometry      Ro = 30 mm, Ri = 18 mm, h = 45 mm
analytic      V = pi(Ro^2 - Ri^2) h = 81430.081581047431 mm^3
sizing        global 10 mm; surface deflection 0.25 mm
regions       outer_wall, inner_wall, bottom_annulus, top_annulus
outcome       a mesh; the void preserved
main param    tube_h, 45 -> 60 mm
```

The bounding cylinder would be 127234.50 mm^3, so filling the bore is a **56.25%**
error. The two walls are swept by **different circles**, so they carry different
names — which is the property that makes them separable at all, and a mapping
that confused them would put a pressure load on the wrong surface.

## RM-MESH-05 — thin feature

```text
geometry      120 x 80 x 1.5 mm plate: 80 times longer than it is thick
analytic      V = abc = 14400 mm^3                                EXACT
sizing        global 20 mm
regions       broad_face
outcome       EITHER a structurally valid mesh whose shape metrics are
              materially worse than RM-MESH-01's, OR an explicit refusal
main param    thin_t, 1.5 -> 3.0 mm
```

**Challenging and deliberately not pathological.** 1.5 mm is seven orders of
magnitude above the kernel's confusion tolerance, so the geometry is sound and
only the *discretisation* is hard — the brief forbids geometry below model
tolerance "merely to force failure". A 20 mm global target on a body 1.5 mm
thick imposes nothing through the thickness, so the boundary governs, which is
the condition that makes this model hard. Stating a target finer than the
thickness would have been quietly fixing the model's own difficulty.

Planar, so its volume is exact even in the hard case: this model's difficulty is
element SHAPE, and the suite keeps the two apart.

## RM-MESH-06 — transformed asymmetric solid

```text
geometry      90 x 55 x 24 mm, built TWICE:
              MeshTransformedBase    on the XY plane at the origin
              MeshTransformedPlaced  on a rotated and translated frame
analytic      V = abc = 118800 mm^3, invariant
transform     X = ( 2/3,  2/3, -1/3)
              Y = (-1/3,  2/3,  2/3)      X x Y = N, every axis moves
              N = ( 2/3, -1/3,  2/3)
              t = (37, -19, 23) mm
sizing        global 16 mm on BOTH, so the only difference is the placement
regions       datum_face, first_side -- the same canonical intent in both
outcome       two meshes, with the invariants below
main param    base_a / placed_a, 90 -> 110 mm
```

**There is no transform feature in BetterCAD.** `features/` has no move or
placement feature; P13-XFORM-001 is about *component* placements and its scope
says "no transformed bodies"; and meshing an assembly occurrence is a later
milestone's. So the transform is applied by building the profile on a frame made
with `Frame3D::fromAxes`, which stores the axes unchanged after checking
orthonormality within 1e-12 — a sketch there maps local `(u, v, w)` to
`origin + uX + vY + wN`, which is exactly `R p + t`.

**The triad is written out in thirds, not derived from an angle**, so the frame
holds the same doubles the suite's own transform uses, and the suite verifies
the three norms, the three dot products and the handedness by hand before using
it. It is not a rotation about a coordinate axis, so an axis-permutation defect
cannot survive it.

**Its own base block, not RM-MESH-01's**, so neither model depends on the other
having been built.

## RM-MESH-07 — local refinement

```text
geometry      100 x 60 x 40 mm block
analytic      V = abc = 240000 mm^3                               EXACT
sizing        global 20 mm, local 6 mm on the y = 0 side face (F)
regions       refined_face (F), coarse_face (the y = 60 side, G)
outcome       a mesh refined at F and not at G
main param    local_a, 100 -> 120 mm
```

**Two named regions, including the one with no control.** "The target is finer"
is only half the claim; the other half is that the rest of the body was left
alone, and a control that refined everything would satisfy the first half
perfectly. The 3.3x ratio between global and local is deliberate: too close and
the refinement is lost in the grading Netgen inserts between regions, too far
and the mesher has to subdivide the whole body to get from one size to the
other.

The sizes are **not** document parameters, because
`MeshSizingControls::globalTargetSize` is a `Length` and P16 has no
parameter-driven mesh size. A reader takes them from the control's own
definition, which is where the canonical intent lives.

## RM-MESH-08 — invalid geometry

```text
geometry      three sides of an 80 x 50 mm rectangle, extruded 20 mm
              -- the fourth side is deliberately absent
analytic      none. The model has no body, and the declared volume is the
              ABSENCE of one, not a fabricated zero
sizing        global 20 mm (never used)
regions       intended_wall (never resolves)
outcome       AN EXPLICIT REFUSAL, and nothing published
main param    open_l (unused: the model never regenerates)
```

**The sketch solves; the extrude fails**, with the feature layer's own words —
*"the profile is open: an edge ends at (0, 0) mm without a neighbour"*. So the
document loads, regenerates deterministically, and reports a failed body every
time, in every configuration and after every save and load.

**Why not an open shell**, which is the other input the brief offers.
`GeometryIneligibility::NotASolid` is where an open shell lands, "and lands here
on TOPOLOGY" — but no feature in this repository produces a non-solid body, so
that state is not reachable from a committed model. A failed body is the brief's
own alternative, and it is reachable, deterministic and honestly diagnosed.

**And it carries a `MeshControl`**, so the refusal is about the body.

## What is committed, and what is not

```text
COMMITTED, as source
  the builders and the catalog      examples/reference_models/Mesh*
  the analytical oracles            tests/reference/Analytic.hpp
  the gates                         tests/reference/MeshModelsTests.cpp
  the shared checks                 tests/reference/MeshTestSupport.hpp

NOT COMMITTED
  the nine .bcad documents          written by the runner with --out, into the
                                    build tree, as a ctest fixture
  any generated mesh                never written to a file at all
  any golden mesh blob              none exists
```

**The builders are the canonical definitions**, so the suite can be recreated
from source with nothing else. That is also what P15-REFMOD-001 did: its eight
documents are produced by its CLI fixture rather than checked in, unlike the
P11/P12 parts and the P13 assemblies under `examples/models/reference/`, which
predate that practice.

No opaque output is frozen as an expected result. The expectations are
closed-form volumes, semantic counts, and a canonical fingerprint that is the
`MeshControlDefinition` itself — which is what the brief asks for in preference
to golden mesh files, and which has the further property that a reader can check
an expectation by arithmetic rather than by diffing a blob.

## RM-MESH-09 — configuration-driven geometry: DEFERRED

```text
STATUS:  deferred, with the reason, and not as an omission
```

The brief permits a configuration case "only if the existing system guarantees a
safe, current regenerated geometry path", and says that if the current contract
deliberately blocks such meshing, the suite should document the deferral.

**The contract blocks it, deliberately and first.**
`GeometryIneligibility::ConfigurationOverrideActive` is the *first* check
`requireMeshableGeometry` makes, before the object is even looked up:

> *"A configuration with parameter overrides is active. The carried regeneration
> defect means the bodies in hand are the base configuration's, so nothing here
> can be trusted to describe the active one."*

So "configuration A -> geometry A -> mesh A" is not a workflow P16 supports: it
is a refusal P16-GEOM-001 was built to make. A reference model that exercised it
would either assert the refusal — which `GeometryPreparationTests.cpp` already
does, as a unit of the milestone that owns it — or reproduce the historical
stale-configuration bug as a supported workflow, which the brief forbids in as
many words.

Adding RM-MESH-09 is therefore a scope decision about the *configuration*
contract, not about reference models, and it is not taken here.
