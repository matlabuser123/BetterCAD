# P16-SURF-001 — Engineering Surface Mesh

```text
STATUS:      see RESULT, below
TASK:        P16-SURF-001 -- engineering surface mesh
PHASE:       P16 -- Meshing
DATE:        2026-10-01
```

## Baseline

```text
branch        main
HEAD          9c755e843ac9e802f7d1d1c9494211889c8256c0
origin/main   9c755e843ac9e802f7d1d1c9494211889c8256c0   (HEAD == origin/main)
tree          7fd0286744deabb9b13e9f160965a253414cf40f
log           9c755e8 BetterCAD: enforce authoritative geometry for meshing
working tree  clean
build root    C:/Users/uqhas/AppData/Local/bc-build   (outside the synchronised folder)
compiler      GNU 16.1.0, C++23
OCCT          8.0.1
```

## Prerequisites

Verified before any production change:

```text
P16-ARCH-001   25/25 ticked, PASS marker   evidence at 9964f88
P16-DATA-001   26/26 ticked, PASS marker   evidence at 20b04b9
P16-GEOM-001   20/20 ticked, PASS marker   evidence at 9c755e8
```

Nothing re-decided. Mesh ownership, `NodeId`/`ElementId` semantics, the coordinate frame, the
authoritative geometry boundary, the stale-geometry policy, the surface-mesh role and the backend
boundary all come from those ADRs unchanged.

## Scope

```text
IN SCOPE      the engineering surface triangulation; node unification; structural
              validation (closure, manifoldness, orientation coherence,
              degeneracy, duplication); the two independent cross-checks

NOT IN SCOPE, and NOT STARTED
              P16-VOL-001, P16-SIZE-001, P16-QUALITY-001, P16-MAP-001 and later.
              No tetrahedra, no sizing beyond the two controls the kernel needs,
              no quality scoring, no facet-to-FaceName attribution.
```

## Display tessellation audit

Full record: [AUDIT.md](AUDIT.md).

```text
Is there a display tessellation path today?   NO. No renderer, no AIS_Shape
                                              anywhere in the repository.
The only triangulator                         geometry::triangulate(), whose one
                                              existing consumer is STL export.
```

So the separation this milestone owes cannot be demonstrated by watching two paths diverge —
there is no second path yet. It is established by construction instead, which is stronger.

## Display / engineering separation

The brief asks for no vague statement that the two are "logically separate". They are separate
because **there is no shared mutable state between them**, and the reason is one line of the
existing triangulator:

```cpp
// Mesh a copy: the kernel stores triangulations on the faces, and the
// body's faces are shared with every copy of the body. Meshing them in
// place would make results depend on earlier meshing requests.
BRepBuilderAPI_Copy copier(*shape, /*copyGeom=*/false, /*copyMesh=*/false);
```

| Question | Answer |
| --- | --- |
| Can viewer settings change the engineering mesh? | **No.** `copyMesh=false`, so the copy starts with no triangulation and the mesher must produce one from the controls it was given. |
| Can engineering settings change display semantics? | **No.** Only the copy is meshed; nothing is written to the authoritative faces. |
| Does either path reuse cached OCCT triangulation? | **Neither does.** The authoritative shape is never meshed. |
| If a cache were shared, how is contamination prevented? | The question does not arise — there is no cache to share. |

Tested as well as argued: `SurfaceMesh_IsUnaffectedByAnEarlierCoarseTriangulationOfTheSameBody`
runs a coarse pass first, then an engineering request with controls 100x finer, requires the
engineering result to follow the engineering controls, then runs another coarse pass and requires
the engineering result to be unchanged.

`SurfaceMesh_GenerationLeavesTheDocumentAndTheBodyUntouched` generates three times and checks the
document revision, the body's topology, the geometry revision, the CAD volume and the CAD surface
area are all unchanged. Triangulating is derived work, not a CAD edit.

## Engineering surface API

```cpp
Result<EngineeringSurfaceMesh> generateSurfaceMesh(const MeshableGeometry&, const SurfaceMeshControls& = {});
Result<EngineeringSurfaceMesh> surfaceMeshFor(const Document&, const Regenerator&, ObjectId,
                                             const SurfaceMeshControls& = {});
SurfaceValidation validateSurface(const Mesh&);
Area              surfaceArea(const Mesh&);
Volume            enclosedVolume(const Mesh&);
```

`generateSurfaceMesh` takes a `MeshableGeometry`, which is the point: the only way to obtain one
is `requireMeshableGeometry`, so it cannot be handed a stale body, a failed regeneration, a shape
with no solid, or geometry under a configuration override. **P16-SURF performs no body-validity
checks of its own** and has no second opinion about what is meshable.

`surfaceMeshFor` is the ordinary entry point so a caller cannot forget the preparation step, and
it returns P16-GEOM's refusals unchanged.

## Backend and OCCT adapter

ADR-033 had already decided where this work goes: *"P16-SURF-001 extends it additively — per-face
triangle grouping, so a higher layer can attribute triangles to faces — and does not move it."*

```text
kernel call        geometry::triangulate(), unchanged except for the addition below
added additively   geometry::MeshFace -- a triangle range per CAD face. It carries
                   NO FaceName: attributing a facet to a named face is
                   P16-MAP-001's, and a name here would start that milestone early
OCCT in meshing    NONE. src/meshing/ has no occt/ directory, and the architecture
                   check confirms 0 violations over 407 files
```

So no public P16 consumer can reach `Poly_Triangulation`, `Poly_Triangle`, `TopLoc_Location` or a
face-local node index — those appear only inside `core/geometry/occt/`.

## Node unification

**Exact coordinate equality in model space. No tolerance, and therefore none to tune.**

That is a topological identity here rather than an approximation: the kernel discretises a shared
edge **once** and both adjacent faces index that same discretisation, so the two faces' nodes on
that edge are the same numbers. The repository already depended on it — 15 watertightness
assertions across 11 files weld this way.

**The route not taken, recorded rather than dismissed.** OCCT's `PolygonOnTriangulation` would
make the correspondence topological by construction, and §9 names it first. It was not used
because its edge cases are where cracks come from: a periodic face's seam edge appears twice on
one face with two polygons, and a sphere's pole is a degenerate edge. Each needs separate handling
and each is a silent crack if wrong.

**What makes the simpler rule safe rather than hopeful:** the output is *proven* to close.
`generateSurfaceMesh` refuses unless the boundary-edge, non-manifold and orientation-conflict
counts are all zero, so **a welding failure cannot be reported as watertight** — the worst case is
a refusal. The seam was the live risk and the cylinder closes; the fallback stays recorded in the
source.

## Triangle connectivity, orientation and normals

```text
data model      P16-DATA's Mesh with Triangle3 elements. No parallel triangle
                representation -- arity, orientation convention, identity rules
                and immutability are already qualified
orientation     the right-hand rule on the STORED winding,
                n proportional to (p2-p1) x (p3-p1)
face reversal   face.Orientation() == TopAbs_REVERSED swaps b and c. Already in
                the triangulator, and heavily load-bearing: ignoring it fails
                23 of 30 tests
normals         DERIVED, never stored. A normal is computed from the winding when
                something needs one, so it cannot go stale against the positions
                it came from. No graphics shading normal exists to be mistaken for
                an engineering one
```

Per-face coherence is checked using the new `MeshFace` grouping: within one planar CAD face every
triangle normal must have a dot product of +1 with the first, and across the box's six faces there
must be exactly six distinct axis-aligned outward directions — so sharp edges keep distinct face
normals and nothing is smoothed.

## Structural validation

```text
boundaryEdgeCount         undirected edges used by exactly one triangle
nonManifoldEdgeCount      used by three or more
orientationConflictCount  two triangles traversing a shared edge the SAME way
degenerateTriangleCount   zero or non-finite area
duplicateTriangleCount    same three nodes in either winding
unusedNodeCount           nodes no triangle references
```

`watertight()` requires the **first three** to be zero, not just the first: a surface can have no
boundary edge and still be non-manifold, and it can be manifold and still carry a patch facing the
wrong way. Each has its own fixture, including one with no boundary edges and one non-manifold
edge, and one where two triangles share an edge traversed identically — which edge counting alone
cannot see.

Duplicates are detected on the **node set after unification**, never on coordinates, so a future
contact interface where two regions legitimately occupy the same place is not mis-merged. A
reversed duplicate counts.

## The two independent cross-checks

Beyond edge counting, every closed fixture is checked two further ways.

**Surface area** against the closed form, and against the kernel's own integration. CAD owns the
area; the triangle sum is a derived check.

**Enclosed volume**, `V = 1/6 sum dot(a, cross(b, c))`:

```text
its SIGN         detects a globally inward surface that is otherwise perfectly
                 manifold -- EnclosedVolume_IsNegativeForAnInwardOrientedClosed-
                 Surface reverses every winding of the box's surface, confirms it
                 is STILL watertight and coherent, and confirms the volume comes
                 out exactly negated
its INVARIANCE   under translation detects a crack, because the relation only
                 holds for a surface that genuinely closes
```

`generateSurfaceMesh` refuses a non-positive enclosed volume, so an inward boundary cannot be
returned as valid.

## Measured results

Figures captured from the built tests, not computed by hand and not estimated.

### Topology

| Fixture | Nodes | Triangles | CAD faces | Boundary | Non-manifold | Degenerate | Duplicate | Orientation conflicts | Watertight |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| SURF-BOX 20×30×50 | **8** | 12 | 6 | 0 | 0 | 0 | 0 | 0 | **yes** |
| SURF-BOX placed on XZ | 8 | 12 | 6 | 0 | 0 | 0 | 0 | 0 | **yes** |
| SURF-CYLINDER coarse | 52 | 100 | 3 | 0 | 0 | 0 | 0 | 0 | **yes** |
| SURF-CYLINDER default | 72 | 140 | 3 | 0 | 0 | 0 | 0 | 0 | **yes** |
| SURF-CYLINDER fine | 504 | 1004 | 3 | 0 | 0 | 0 | 0 | 0 | **yes** |
| SURF-TUBE Ro20/Ri12/h40 | 1008 | 2016 | **4** | 0 | 0 | 0 | 0 | 0 | **yes** |

The box welding 24 raw vertices to exactly **8** is the clearest single number here: those are its
corners, and a failure to unify would have left 24 nodes and an open surface.

### Area, against independent closed forms

| Fixture | Expected | Mesh area | Rel. error | Result |
| --- | --- | --- | --- | --- |
| Box, `2(ab+ac+bc)` = 6200 mm² | 6200 | **6200.00** | ~0 | **PASS**, planar is exact |
| Cylinder coarse, `2πrh+2πr²` = 2789.73 mm² | 2789.73 | 2776.37 | **−4.790e-3** | PASS, understates |
| Cylinder default | 2789.73 | 2782.76 | **−2.501e-3** | PASS, understates |
| Cylinder fine | 2789.73 | 2789.59 | **−5.110e-5** | PASS, understates |

Every curved error is **negative**, which is the only correct direction: an inscribed
triangulation cuts chords inside the arc, so it must understate. The fixture asserts the direction
and the monotone convergence (coarse < default < fine < exact), not an equality that would be
wrong.

### Surface-derived volume, against the CAD volume

| Fixture | Expected | From the surface | Rel. error | Result |
| --- | --- | --- | --- | --- |
| Box, `abc` = 30000 mm³ | 30000 | **30000.0** | ~0 | **PASS** |
| Box placed on XZ | 30000 | **30000.0** | ~0 | **PASS**, placement invariant |
| Cylinder fine, `πr²h` | 11309.73 | within 1e-3 | — | **PASS**, understates |
| Tube, `π(Ro²−Ri²)h` = 32169.9 mm³ | 32169.9 | **32166.6** | **−1.03e-4** | **PASS** |

The tube's number does the work of three checks at once: a capped cavity or a missing inner wall
would give the outer cylinder's 50265.5 mm³, **1.56x larger**.

## Transforms

```text
placement invariance   the same box on the XY and XZ planes: identical node and
                       triangle counts, identical area, identical enclosed volume,
                       and a DIFFERENT bounding box -- so it really moved
TopLoc_Location        applied exactly once, at the one place a node becomes a
                       model-space point. Dropping or double-applying it would
                       change the placed box's bounds and volume
translation            every node moved 137 mm: enclosed volume invariant to 1e-9,
                       area exact, still watertight
extreme translation    every node moved 137 METRES: a cancellation ratio near 1e10
                       consumes ~10 digits, and the volume still agrees to 1e-5
                       while the area -- which has no cancellation -- stays exact
```

## Holes, voids and curved faces

```text
hole wall            SURF-TUBE's inner wall: nodes at exactly r = 12 mm, and no
                     node on the axis, so the opening is not capped. Classified
                     geometrically IN THE TEST, because production face mapping is
                     P16-MAP-001's
inner normals        THE SHARPEST ORIENTATION TEST HERE. "Outward" means out of
                     the MATERIAL, not away from the origin, and on a tube's inner
                     wall those are OPPOSITE. The radial component must be
                     strongly negative inside and positive outside, and both sets
                     are counted so neither can be empty
cylindrical faces    every node is at r <= 12 within 1e-6 mm and some are at
                     exactly 12: tessellation NODES lie on the true surface even
                     though triangle interiors cut inside it, so the nodes are
                     what may be checked against the closed form
curved normals       not required to be equal -- each is checked against the LOCAL
                     outward direction at its own centroid
```

## Disconnected solids

**Supported, not refused.** ADR-032 gives a mesh one region per solid, so there is nothing to
reject. Each component must still close, which edge incidence enforces over the whole surface
without needing to know which component a triangle belongs to. P16-GEOM reports the solid count
for `P16-VOL-001` to make regions from.

## Stale-geometry integration

```text
stale after a profile edit   REFUSED. The old body is still there, valid and
                             closed; no triangles come from it
blocked regeneration         REFUSED, with P16-GEOM's own diagnostic
after regenerating           accepted, and the new geometry's volume is the
                             trapezoid 1/2(40+20)*30*50 = 45000 mm^3
```

Mutation-proved: disabling the currency check two layers down in `GeometryPreparation` fails
exactly the surface-mesh stale test, which is what shows the chain
`surfaceMeshFor -> requireMeshableGeometry -> isCurrent` is wired rather than bypassed.

## Mutation proof

| Mutation | Tests failed | Proves |
| --- | --- | --- |
| Ignore `TopAbs_REVERSED` | **23 of 30** | face-reversal handling is on the main line for every solid |
| Disable the currency guard | **1**, exactly the stale test | P16-SURF goes through P16-GEOM |
| Skip boundary-edge counting | **1**, exactly the open-patch test | the counter works — and see below |

**What the third mutation does NOT prove**, recorded rather than glossed: the real solids *are*
closed, so their boundary-edge count was already zero and removing the counter changes nothing
about them. Only `ValidateSurface_CountsBoundaryEdgesOfAnOpenPatch` detects it. That one test is
the entire guard on the watertightness claim.

Each mutation was reverted and the revert verified by `cmp` against a backup in the same call.

## Determinism

```text
repeated generation   5 repeats on box, cylinder and tube: identical node handles
                      and EXACT positions, identical oriented connectivity,
                      element handles, regions, bounds, validation counts, area
                      and enclosed volume
node numbering        ascending coordinate order, so it does not depend on face
                      traversal at all -- checked by asserting the positions come
                      back sorted
element ordering      sorted by the triangle's NODE-SET key while the stored
                      connectivity keeps its oriented winding (the brief's §52
                      separation). Sorting the connectivity itself would make the
                      order deterministic by destroying the orientation
```

Equality is compared on **content**, not with `operator==`: a `MeshId` is unique per mesh by
design (ADR-031), so two independently generated meshes are deliberately unequal however identical
their content. `SurfaceMesh_TwoMeshesOfTheSameBodyAreNeverTheSameMesh` pins that, because using
`==` here was finding F1.

## Files changed

```text
include/bettercad/core/geometry/Mesh.hpp     + MeshFace grouping (additive)
src/core/geometry/occt/OcctMesh.cpp          + records one MeshFace per face
include/bettercad/meshing/SurfaceMesh.hpp    new
src/meshing/SurfaceMesh.cpp                  new
src/meshing/CMakeLists.txt                   + the source
tests/meshing/SurfaceMeshTests.cpp           new -- 31 tests
tests/CMakeLists.txt                         + the test source
```

32 added lines across the four existing files, all additive.

## Adversarial review

Full record: [ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md).

```text
ATTACKS              22 from the brief, plus 4 raised there
FINDINGS             5
PRODUCTION DEFECTS   0
FIXED                5 -- all in this milestone's own tests
MUTATIONS            3 applied, 3 caught
```

The three findings worth reading are all cases where a looser assertion would have hidden the
problem: a determinism test that compared **identity** instead of content (and `MeshId` is unique
by design, which I had documented myself); a translation test that moved the body **137 metres**
instead of millimetres; and a capped-cavity guard that was **arithmetically impossible**, since a
tube of these radii is 64% of its outer cylinder and the guard demanded under 50%.

## Regression

Harness carried from P16-GEOM-001, including P16-DATA-001's delayed-expansion fix -- this
milestone's determinism filter again contains `|`, `(` and `)`.

Pre-freeze checks, all cleared **before** the expensive run:

```text
git diff --check                  clean
31 new tests, --repeat x5         155 executions, 100%, 32 s
integration run                   120/120 -- the surface tests, P16-DATA, P16-GEOM,
                                  STL EXPORT (which shares the triangulator this
                                  milestone modified) and the architecture checks
debug-shared-ext build            exit 0, 0 diagnostics, no DLL-boundary diagnostics
debug-shared-ext surface tests    30/30
architecture.layering             407 files, 0 violations, and NO OCCT in src/meshing/
```

| Preset | Clean build | Full ctest | Tests |
| --- | --- | --- | --- |
| `debug-ext` | 22m05s | 16m12s | **2931/2931** |
| `release-ext` | 24m16s | 15m02s | **2931/2931** |
| `debug-shared-ext` | 19m23s | 16m16s | **2931/2931** |
| repeat `release-ext` | — | 31 tests x5 | **31/31** |
| repeat `debug-ext` | — | 31 tests x5 | **31/31** |

```text
2931 = the 2900 P16-GEOM-001 qualified, plus this milestone's 31 new tests
9103 test executions (2931 x 3, plus 31 x 5 x 2)
0 failures
```

**0 compiler warnings in all six build and rebuild logs**, checked strictly for `warning:` and
`[-W`.

**The binaries tested are the binaries built**: each preset's second build exited 0 having
recompiled and relinked nothing.

This regression carried more weight than the previous milestones' because the change reaches
**layer 0**: `core/geometry/Mesh.hpp` is included transitively by features, assembly, drawing,
meshing and io, and the STL export suite exercises the same triangulator that gained per-face
grouping. Release and Debug-shared are where a surviving assumption about the triangulator would
have shown, and neither found one.

## Result

```text
RESULT:      PASS
HARNESS:     "Qualification passed: every stage exited 0."
ELAPSED:     1h51m50s (14:36:43 -> 16:28:33, 2026-10-01)
TESTS:       2931/2931 in each of three presets; 9103 executions; 0 failures
DETERMINISM: 31 tests x5 in release-ext and debug-ext; 0 failures
WARNINGS:    0 in all six build and rebuild logs
TREE:        the eight qualified tree IDs are identical before the first build and
             after the last test run
EVIDENCE:    qualification/qualification-times.txt and the per-preset logs
```

Qualified source trees, recorded before the first build and unchanged after the last test run:

```text
apps              7532b4b3748efaa1282874af618a6d41bcb87751
include           982907434a52c83d0b5497ef9c6076a1a48f93d0
src               a4d7fd2d1962d4ce45261aae903b8429810adcff
tests             70bdbe754e6fbe9265e71e503f788255208c9f00
examples          9187931fa6824e98d3da6296cdc40af6ca4b8607
cmake             7ed703a3031144d26063a1ff02996e7664e29524
CMakeLists.txt    13823e788ed3975792affa9ee4354ffce9479d80
CMakePresets.json 3229f0f89b236744d7d5c082bed47c28b718288e
```

`docs/` and `TODO.md` are outside the fingerprint, so the evidence and the TODO closeout that
follow this run do not affect it.

**Cross-preset equivalence.** The three presets agree on every figure this milestone reports:
the same 2931 tests pass in each, and the surface fixtures assert exact node counts, exact
triangle counts, exact positions and exact validation counts rather than tolerances, so a
Debug/Release divergence in the triangulator or in node unification would have failed rather
than drifted.

## Known limitations

```text
No sphere fixture. The cylinder and tube cover curved and periodic faces, and the
brief makes the sphere optional -- but a sphere has a DEGENERATE POLE EDGE, which
is the one geometry where exact-coordinate welding has an untested edge case.

TriangulationFailed has no fixture: no shape was found that the kernel accepts as a
valid solid and then fails to triangulate.

No display path exists to measure against. The separation is established by
construction rather than by observing two paths diverge; when a display path
arrives it will need its own test.

The boundary-edge counter is protected by exactly one test, because closed solids
cannot detect its removal.

Node unification is exact-coordinate, not topological. Validated per mesh rather
than guaranteed by construction; the PolygonOnTriangulation fallback is recorded in
the source.

Sizing is the two controls the kernel needs. Local sizing is P16-SIZE-001's.

No facet-to-FaceName attribution: P16-MAP-001's. The hole-wall test classifies
geometrically, in the test only.

No quality metrics: a sliver is a valid closed boundary here and P16-QUALITY-001's
to complain about.
```

## Revision

First issue, 2026-10-01.
