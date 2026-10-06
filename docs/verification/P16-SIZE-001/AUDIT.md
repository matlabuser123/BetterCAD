# P16-SIZE-001 — Netgen sizing control surface, and its hidden defaults

```text
SUBJECT:  every backend parameter that can affect element size, read from the
          PINNED SOURCE rather than from the header's documentation
RESULT:   4 findings that each would have made BetterCAD's sizing silently
          wrong. 14 parameters pinned; 7 found to be dead.
DATE:     2026-10-02
SOURCE:   Netgen 6.2.2604 as built by deps/, nglib/nglib.cpp and
          libsrc/meshing/{meshclass,localh,meshing3,meshfunc}.cpp
```

The brief required this audit before any production code. It was worth it: the
obvious reading of the header is wrong in three separate ways.

## Finding 1 — `maxh` is NOT the global size control

The parameter named "Maximum global mesh size allowed" does not control
element size on BetterCAD's pathway.

```text
measured   a 40 mm block with Ng_Meshing_Parameters::maxh of 20, 10 and 5 mm
           produced the IDENTICAL mesh three times: 9 nodes, 12 tetrahedra,
           median edge 40 mm
```

Reading the source explains it. The volume mesher asks `Mesh::GetH(p)`:

```cpp
// libsrc/meshing/meshclass.cpp
double Mesh :: GetH (const Point3d & p, int layer) const {
    double hmin = hglob;
    if (lh) { double hl = lh->GetH(p); if (hl < hglob) hmin = hl; }
```

so the size at a point is `min(hglob, localh(p))`, where

```text
hglob      set ONLY by Mesh::SetGlobalH, i.e. by Ng_RestrictMeshSizeGlobal
localh     a tree built by CalcLocalH from the SURFACE ELEMENT sizes
maxh       consulted only for a per-domain maximum (meshfunc.cpp:73) and
           inside one local decision (meshing3.cpp:1171)
```

BetterCAD supplies the surface, so for a coarsely triangulated body `localh`
is as large as the body and `maxh` never gets a say.

**Consequence:** the global target is routed through
`Ng_RestrictMeshSizeGlobal`. `maxh` is set to the same value as well, because
it does participate in those two places.

## Finding 2 — a point restriction cannot refine a face

`Ng_RestrictMeshSizePoint` is the only local mechanism nglib offers besides a
box. A face's nodes lie on a surface, so restricting at each of them
constrains an infinitesimally thin **sheet**, and the mesher samples the size
function at candidate points almost none of which fall on it.

```text
measured   36 point restrictions at a cylinder's top disc (verified to land at
           z = 20 exactly) changed the mesh without refining the disc: the
           region went from 50 nodes to 40 and from mean edge 4.68 mm to
           5.85 mm -- COARSER than with no control at all
```

**Consequence:** a face becomes a **slab** — the bounding box of its nodes,
extended into the material — applied with `Ng_RestrictMeshSizeBox`.

## Finding 3 — the slab must extend inward, and be several elements deep

`Ng_RestrictMeshSizeBox` walks the box from its minimum corner in steps of
`h`:

```cpp
// nglib/nglib.cpp
for (double x = pmin[0]; x < pmax[0]; x += h)
  for (double y = pmin[1]; y < pmax[1]; y += h)
    for (double z = pmin[2]; z < pmax[2]; z += h)
      ((Mesh*)mesh) -> RestrictLocalH (Point3d (x, y, z), h);
```

So where the samples land depends on the corner, and a thin or badly placed
slab wastes them outside the material.

```text
measured, slab centred on the face and one target deep
           top disc    [18.5, 21.5] -> samples at 18.5 (inside) and 20.0
                       REFINED: 69 nodes in the region against 43
           bottom disc [-1.5, 1.5]  -> samples at -1.5 (OUTSIDE) and 0.0 (the
                       surface only)
                       NOT REFINED: 490 tetrahedra, FEWER than the 554 with no
                       control at all

measured, slab extended inward three targets deep
           top disc    72 nodes against 59      both directions, symmetric
           bottom disc 71 nodes against 52
```

**Consequence:** the slab is extended along the face's inward normal, three
target sizes deep. Both numbers are load-bearing and neither is a guess.

## Finding 4 — `RestrictLocalH` hardcodes the grading, bypassing `mparam`

```cpp
// libsrc/meshing/meshclass.cpp
void Mesh :: RestrictLocalH (const Point3d & p, double hloc, int layer) {
    if (hloc < hmin) hloc = hmin;                     // <-- SILENT CLAMP
    if (!lochfunc[layer-1]) {
        PrintWarning("RestrictLocalH called, creating mesh-size tree");
        GetBox (boxmin, boxmax);
        SetLocalH (boxmin, boxmax, 0.8, layer);       // <-- HARDCODED 0.8
    }
```

Two problems in four lines.

**The grading.** `Ng_GenerateVolumeMesh` calls `CalcLocalH(mparam.grading)`,
which builds the tree *only if one is absent* — and a local restriction has
already built it, with 0.8. So the grading depended on whether a local control
happened to exist, and merely adding one changed the whole mesh.

**The clamp.** A requested local size below `hmin` is raised silently. That is
precisely the "silent sizing mutation" BetterCAD must not do.

**Consequences:** `grading` is pinned to **0.8**, the value the restriction
path forces, so both paths agree. `minh` is pinned to **0**, which disables
the clamp.

## Every parameter `Transfer_Parameters` actually copies

`Ng_Meshing_Parameters::Transfer_Parameters()` is the only route from the
struct into the mesher. It copies fifteen fields, and all fifteen are now
set explicitly by `src/meshing/netgen/NetgenBackend.cpp`.

> **Count corrected by P16-QUAL-001 on 2026-10-06.** The list above is right
> and every field in it is set explicitly, which is what this audit set out to
> establish; the word "fourteen" is wrong, because the list has fifteen rows
> (`check_overlapping_boundary` wraps onto two lines). Checked from both ends:
> the installed `nglib.h` declares 22 fields, 7 of which nglib never transfers,
> and `NetgenBackend.cpp` makes exactly 15 `parameters.<field> =` assignments
> whose names match. See `docs/verification/P16-QUAL-001/BACKEND_DEFAULTS.md`.


```text
field                       default  BetterCAD  why
uselocalh                   1        1          GATES the local size function
                                                (meshing3.cpp reads it); local
                                                sizing silently does nothing
                                                without it
maxh                        1000     target     see Finding 1; not the primary
                                                driver but it does participate
minh                        0.0      0.0        see Finding 4: non-zero would
                                                silently clamp a local request
grading                     0.3      0.8        see Finding 4
elementsperedge             2.0      2.0        -> segmentsperedge. Surface
                                                side; BetterCAD supplies the
                                                surface, so inert here
elementspercurve            2.0      2.0        -> curvaturesafety. Likewise
                                                surface side and inert; CURVATURE
                                                control in BetterCAD is the
                                                surface layer's deflection
second_order                0        0          Tet4 only (ADR-031)
quad_dominated              0        0          Tet4 only
meshsize_filename           0        nullptr    no external size file
optsteps_2d                 3        3          surface side, inert
optsteps_3d                 3        3          volume optimisation: DOES affect
                                                the result
invert_tets                 0        0          the boundary is already oriented
invert_trigs                0        0          likewise
check_overlap               1        1          the backend's own input check
check_overlapping_boundary  1        1          likewise
```

### Seven fields the header declares and nglib never transfers

```text
fineness            closeedgeenable     closeedgefact
minedgelenenable    minedgelen          optsurfmeshenable      optvolmeshenable
```

These are **dead** on this pathway. `fineness` and `optvolmeshenable` in
particular read as though they would matter. Setting them would be theatre, so
BetterCAD does not, and records why here instead.

## Version stability

The protection against a future Netgen changing a default is that every
transferred field is assigned explicitly before generation. The values above
are Netgen 6.2.2604's own where BetterCAD has no reason to differ, which makes
them **BetterCAD's values now**: an upgrade that changed a default cannot
change what a BetterCAD document means.

The two places BetterCAD deliberately differs (`grading`, and the global
target going through `Ng_RestrictMeshSizeGlobal`) are differences it chose,
for the reasons above.

## What BetterCAD exposes, and what it does not

```text
GLOBAL TARGET SIZE   exposed, as Length. An upper bound the mesher aims at,
                     not a promise about every edge.
LOCAL TARGET SIZE    exposed, per FaceName, as Length.

MINIMUM SIZE         NOT exposed. N/A BY DECISION, not by omission: its only
                     observable effect through this backend is to silently
                     clamp other controls (Finding 4), so a canonical control
                     for it would be a control whose purpose is to make
                     another control lie. Pinned to 0.
MAXIMUM SIZE         NOT exposed, because the global target IS the maximum.
                     Two controls with unclear interaction is what the brief
                     warns against.
GROWTH RATE          NOT exposed, deferred. `grading` is real and transferred,
                     but its engineering meaning ("0 uniform ... 1 aggressive
                     local grading") is not statable as a quantity yet, and
                     the restriction path forces 0.8 regardless. Pinned.
CURVATURE CONTROL    NOT exposed HERE, and not deferred either -- it already
                     exists, in the right place. Netgen's curvaturesafety is
                     surface-side and inert because BetterCAD supplies the
                     boundary; curvature refinement is
                     SurfaceMeshControls::angularDeflection, which P16-SURF-001
                     qualified. Adding a second one would be the competing-state
                     failure.
```
