# P16-VOL-001 — reference cases

```text
SUBJECT:  the fixtures the milestone is validated against, what each one is
          FOR, and what was measured
RESULT:   4 reference cases plus 2 scale cases. Every expected value is closed
          form and was evaluated by hand.
DATE:     2026-10-02
```

Each case exists to catch something a different case cannot. A suite of four
boxes would pass while the mesher filled voids, mis-oriented elements and
re-triangulated boundaries.

## Case 1 — the reference tetrahedron

```text
fixture     4 points, 4 outward-wound triangles, 10 mm legs, at the seam
            (no CAD, so a failure is the backend's or the adapter's)
expected    exactly 1 tetrahedron, exactly 4 nodes
measured    1 tetrahedron, 4 points, all four nodes named once
```

**What only this case catches.** A tetrahedron is the one input whose correct
answer is a single element and no new nodes, so it is the only fixture where
"the backend refined geometry it was asked to fill" is visible as a count. It
also establishes that boundary nodes are preserved bit for bit, which is what
makes the conformity check's exact-position matching legitimate rather than
hopeful.

## Case 2 — a box

```text
fixture     20 x 30 x 40 mm extruded rectangle
expected    V = 20*30*40 = 24000 mm^3 = 2.4e-5 m^3, BY HAND
measured    9 nodes, 12 tetrahedra, 12 boundary triangles
            tetrahedral volume  2.4e-05 m^3
            boundary volume     2.4e-05 m^3
            CAD volume          2.4e-05 m^3
```

All three agree. The 9 nodes are the 8 corners plus one interior node, and the
12 boundary triangles are exactly the 12 the surface mesher produced — the
conformity counts are 0 unmatched in both directions.

**What only this case catches.** A box has planar faces only, so the faceted
boundary **is** the body and equality with the CAD volume is the right
expectation. Every curved fixture can only be held to a one-sided bound, so
this is the one case that can check volume recovery at all.

It is also the fixture that exposed the orientation defect: 12 tetrahedra, 12
of them negative, before the adapter's node-order translation.

## Case 3 — a cylinder

```text
fixture     r 10 mm, h 25 mm, meshed at two surface deflections
expected    V = pi r^2 h = 7.85398e-6 m^3, BY HAND
asserted    tetrahedral volume < CAD volume, at BOTH deflections
            and the error at 2e-5 m deflection < the error at 5e-4 m
measured    coarse (5e-4 m):  149 nodes, 577 tetrahedra, 7.81417e-06 m^3
            fine   (2e-5 m):  274 nodes, 1121 tetrahedra, 7.84373e-06 m^3
            CAD volume                                    7.85398e-06 m^3

            error  coarse 3.98e-08 m^3  (0.51% low)
                   fine   1.03e-08 m^3  (0.13% low)
            Both below, and the error falls by a factor of ~3.9 as the
            deflection tightens by 25x.
```

**What only this case catches.** A chord lies inside the arc, so a faceted
cylinder is strictly smaller than the real one — and that direction is a fact
about geometry, not a tolerance. Asserting convergence rather than a fixed
margin is what would catch a mesher that filled the wrong shape entirely: a
fixed tolerance can be satisfied by being wrong by a constant.

The exact identity (tetrahedra == boundary's enclosed volume) still holds at
every deflection, because the tetrahedra tile whatever polyhedron the boundary
describes.

## Case 4 — a hollow tube, whose void must stay void

```text
fixture     Ro 20 mm, Ri 12 mm, h 30 mm
expected    V = pi (Ro^2 - Ri^2) h = 2.4127e-5 m^3, BY HAND
            a mesher that FILLED the bore would report pi Ro^2 h = 3.7699e-5,
            which is 1.56x larger
asserted    no element CENTROID lies inside the bore
            volume within 2% of the annulus, from below
            volume nearer the annulus than the filled bore
measured    257 nodes, 513 tetrahedra, 448 boundary triangles
            volume            2.41021e-05 m^3
            annulus, by hand  2.41274e-05 m^3   (0.105% above the mesh)
            filled bore       3.76991e-05 m^3   (56% above the mesh)
            elements with a centroid inside the bore: 0
```

**What only this case catches.** Everything else in the suite would pass while
the mesher filled internal voids. The surface here is two shells — an outer
wall and an inner one whose normals point *toward* the axis, because outward
from the material there is inward in space — so it also exercises a boundary
that is not simply connected.

Checked on **centroids**, not nodes: nodes legitimately sit on the inner wall,
so a node-based test would either be vacuous or fail on a correct mesh. A
centroid inside the bore means material was invented.

## Case 5 — an invalid surface, which must FAIL

```text
fixture (seam)      the reference tetrahedron with one face removed
expected            refusal, NOT an empty successful mesh
measured            nglib returns NG_OK and zero tetrahedra; the adapter turns
                    that into VolumeBackendFailure::NoTetrahedra

fixture (domain)    a hand-built 3-triangle open patch whose SurfaceValidation
                    report claims it is perfect
expected            refusal
measured            refused: generateVolumeMesh recomputes the validation
                    rather than trusting the report
```

**What only this case catches.** The two different ways an open surface can
arrive — through the backend, and through a caller who assembled an
`EngineeringSurfaceMesh` by hand. The second is the route a viewer
tessellation would have to take, and before the adversarial review it was open.

Netgen prints `Meshing of domain 1 failed with error: Stop meshing since too
many attempts in domain 1` while the first of these runs. That is expected
output, not a problem: it is the backend being honest in the only channel it
has.

## Cases 6 and 7 — scale, because SI metres reach the backend

BetterCAD is SI internally, so a 20 mm box arrives at Netgen as 0.02. Netgen is
unit-agnostic but carries some absolute defaults, so behaviour at small scale is
a real question. Both tests are written so that an **explicit refusal would also
pass** — the requirement is that the outcome is one of two named things, never a
mesh that is quietly wrong.

```text
1 mm cube                       meshed. Volume recovered to 1e-9 relative.
0.4 mm wall on a 40 mm plate    meshed. Aspect ratio 100. Volume recovered to
                                1e-9 relative, validation clean, boundary
                                conforming.
```

Both succeeded, so the documented answer for small features is **supported**
rather than **explicit failure** — at these sizes. Nothing here claims a lower
bound, because none was searched for.

## What is deliberately absent

```text
a multi-solid case producing a MESH    refused by design; the test asserts the
                                       refusal and that P16-GEOM-001 accepts
                                       the same body, so the decision is this
                                       layer's and not an accident
a sphere                               P16-SURF-001 records the same gap: a
                                       degenerate pole edge is the one geometry
                                       where exact-coordinate node unification
                                       is untested, and it is still untested
a quality fixture                      slivers and aspect ratios are
                                       P16-QUALITY-001's. A thin tetrahedron is
                                       data-valid here, deliberately.
```
