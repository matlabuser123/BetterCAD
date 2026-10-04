# P16-VIZ-001 — Mesh Visualisation / Inspection

```text
STATUS:   PASS
TASK:     P16-VIZ-001 -- mesh visualisation and inspection
PHASE:    P16 -- Meshing
DATE:     2026-10-04
```

**Previously BLOCKED on 2026-10-03** at `e7d90e9`, 0/18, with no production
code: BetterCAD had no 3D viewport, no CAD display and no selection system, and
the OCCT dependency was built without its visualization driver.
`INFRA-VIEWER-001` was authorized out of band to supply them and qualified on
2026-10-04 at `d9c8d53`. This milestone was then authorized and is the work
that the viewport was built for.

The original audit is kept unchanged as [VIEWER_AUDIT.md](VIEWER_AUDIT.md); it
describes a repository that no longer exists. The re-audit against what
`INFRA-VIEWER-001` built is [VIEWER_AUDIT_PART2.md](VIEWER_AUDIT_PART2.md).

## Documents

```text
VIEWER_AUDIT.md          the 2026-10-03 audit, when there was no viewport:
                         why this milestone was blocked
VIEWER_AUDIT_PART2.md    the re-audit at d9c8d53: what to reuse, what to
                         extend, why MeshVS_Mesh is refused, and the one gap
                         in the canonical data
VISUAL_STATE_MODEL.md    current / stale / missing / generation failed: where
                         each is decided and how each is made visible
SELECTION_LINKAGE.md     a pixel to an ElementId, and CAD <-> mesh in both
                         directions
ADVERSARIAL_REVIEW.md    3 production defects, 4 tests that could not fail,
                         the brief's attack list answered, the mutation table,
                         the limitations carried
FREEZE.md                the identity of the tree that was qualified, and
                         the gates cleared before the freeze
qualification/           the logs, and the mutation harness
```

## Baseline

```text
branch        main
HEAD at start d9c8d53  BetterCAD: qualify the OCCT visualization toolchain
                       and add a 3D viewport
origin/main   d9c8d53  (HEAD == origin/main, working tree clean)
compiler      GNU 16.1.0 (MinGW, WinLibs POSIX UCRT), C++23
Qt            6.11.2
OCCT          8.0.1, pinned at V8_0_1, with Visualization + OpenGL
Netgen        6.2.2604
build root    C:/Users/uqhas/AppData/Local/bc-build  (outside OneDrive)
```

## Prerequisites, verified before any production code

Every predecessor's committed evidence was read, not assumed. `P16-MAP-001`
being unqualified is an explicit stop condition for this milestone, so it was
checked first and in detail — its `MAPPING_RESULTS.md` records exhaustive
bidirectional coverage on the box, the cylinder and the tube, a remesh case,
and a cross-generation query that is refused.

```text
P16-ARCH-001     [x]   24 evidence files committed
P16-DATA-001     [x]   23
P16-GEOM-001     [x]   23
P16-SURF-001     [x]   23
INFRA-NETGEN-001 [x]   26   PASS -- Netgen v6.2.2604 is QUALIFIED
P16-VOL-001      [x]   27   PASS
P16-SIZE-001     [x]   25   PASS
P16-QUALITY-001  [x]   33   PASS
P16-MAP-001      [x]   33   PASS
INFRA-VIEWER-001 [x]   33   QUALIFIED
```

## The architecture

What the brief required, and what was built:

```text
canonical P16 mesh state            meshing::Mesh, VolumeMesh,
                                    GeometryMeshMap, MeshQualityReport
        |
        v
read-only visualisation adapter     include/bettercad/renderer/MeshView.hpp
                                    include/bettercad/renderer/MeshInspection.hpp
                                    include/bettercad/renderer/MeshScene.hpp
                                    NO Qt, NO OCCT -- testable headlessly
        |
        v
viewer/render representation        src/renderer/occt/OcctMeshPresentation.cpp
                                    one batched AIS object per mesh
        |
        v
selection / inspection UI           apps/bettercad/MeshInspectorPanel.*
                                    apps/bettercad/MainWindow.*
```

**The split at the top is where the engineering content is, and it is
deliberately viewless.** `MeshView`, `MeshInspection` and `MeshScene` contain
no Qt and no OCCT, so counts, identity translation, the revision guards,
quality navigation, both directions of the CAD linkage, determinism and
non-mutation are all asserted without a display — 38 test cases that run on a
machine with no OpenGL at all. What a view adds is drawing, and that is tested
separately.

## What the GUI owns, and what it does not

```text
CANONICAL (the Document and the meshing module)
    nodes, connectivity, NodeId, ElementId, RegionId
    the boundary -- which IS the VolumeMesh's Triangle3 elements
    quality metrics, classifications and the threshold policy
    the geometry <-> mesh mapping, and named boundary sets
    mesh currentness, through GeometryRevision

GUI-DERIVED (and all of it disposable)
    vertex, triangle and edge buffers        keyed to a MeshStamp
    render index -> NodeId / ElementId       two lookup tables
    highlight state, visibility, style, camera, selection mode
```

The derived side is reconstructable from the canonical side and from nothing
else: a `MeshView` is built from a `meshing::Mesh` and holds no reference to
it afterwards, which is why a stale cache can be detected rather than silently
reinterpreted.

### No second source of truth, proved two ways

**Structurally.** A finished `meshing::Mesh` has **no mutator at all** —
`addNode` and its siblings are on `MeshBuilder` — so the GUI cannot change a
mesh even through a non-const reference, even by mistake.
`compile_fail.meshview.mutate-a-mesh` is that claim as a build failure, and
`compile_fail.meshview.reach-a-mesh-through-a-view` shows the render cache has
no mesh to read engineering data out of.

**At runtime.** Whole-mesh fingerprints — stamp, every node, every triangle,
every tetrahedron — compared before and after building views, rendering,
picking, highlighting, switching style, toggling visibility, navigating
quality and inspecting every element.

## The render cache, and the revision rule

```text
DERIVED        from a meshing::Mesh, by arithmetic and two lookup tables
DISPOSABLE     never updated in place; there is no API to do so
READ-ONLY      from an engineering point of view: it holds no identity it did
               not get from the mesh
REVISION-KEYED MeshView::stamp(), and describes(mesh) asks Mesh::owns
```

A changed mesh means a NEW `MeshView`. The old one answers
`describes(mesh) == false` rather than reinterpreting its indices against
different geometry — ADR-031's refusal, applied to rendering. Everything that
pairs a cache with a mesh goes through one function so the check cannot be
present on three paths and missing on the fourth.

`MeshScene` takes it further: a mesh, its mapping and its quality report can
only be adopted **together and only if all three carry the same stamp**. After
that, nothing inside has to re-check, and a caller cannot assemble a
mismatched set at all.

## Flat shading, and why it is not a style choice

The facet array is non-indexed — three vertices per facet, each carrying that
facet's own normal. Averaging normals at shared vertices would smooth the
facets into a curved-looking surface, which is precisely what an engineering
mesh inspection must not do: **the user is looking AT the faceting.** It also
makes render triangle *i* the *i*-th facet of the view, so the selection index
needs no second mapping.

## Interior inspection: implemented, not deferred

The checklist marks it optional. It is implemented, as the cheapest honest
form: `MeshView::tetrahedraOf` builds the four faces of the chosen tetrahedra
as a view of their own, so a user can see inside without a clipping plane and
without the whole volume becoming geometry.

The four face windings are a property, so they are tested as one rather than
asserted in a comment: for a positively oriented Tet4 — the only kind P16
accepts — each face's right-hand normal must point away from the vertex that
face omits. A wrong winding would light a face from inside and show a hole.

```text
MeshView_TetFaceWindingsPointAwayFromTheOppositeVertex
```

## The tests

```text
HEADLESS -- no view, no driver, no GL                              cases
  MeshViewTests.cpp          the render cache, identity, edges,      13
                             interior faces, determinism, non-mutation
  MeshInspectionTests.cpp    state, quality binding, both linkage    17
                             directions, named sets, refusals
  MeshSceneTests.cpp         generation agreement, node and element   9
                             inspection, face listing
NEEDS A GL IMPLEMENTATION
  MeshDisplayTests.cpp       drawing, styles, highlight, picking,    13
                             visibility, staleness, frames, overlay
  ViewerTests.cpp            carried from INFRA-VIEWER-001           24
BUILD FAILURES
  MeshViewMisuse.cpp         render index vs identity, mutation       5
GUI, under the offscreen platform plugin
  gui.mesh.*                 one CTest case per claim                10
```

Measured by **state** and by **coverage** — how many pixels differ from the
background — never by screenshots and never by pixel equality between
machines. `ToPixMap` returning true proves nothing: a view that drew nothing
returns success and a background-coloured image.

## The three production defects

Full account in [ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md).

```text
1  THE MESH WAS A THOUSAND TIMES TOO SMALL. MeshView positions are SI metres;
   OCCT model space is millimetres. Displayed ALONE it looked perfect, because
   FitAll frames the only thing in the scene. Beside its own CAD solid it was a
   sub-pixel speck -- 0 covered pixels for a mesh that was demonstrably
   displayed. The clue was one box measuring 32674 pixels as a mesh and 54120
   with the solid added: one silhouette, two scales.

2  A SELECTION SURVIVED A REMESH. The panel cleared its details when the mesh
   went away and not when it was replaced, so an element's numbers stayed on
   screen against a mesh in which that ElementId is a different element --
   internally consistent and completely wrong.

3  THE MESH COMMAND COULD NOT MESH AN EDITED MODEL. P16-GEOM-001 refuses to
   mesh stale geometry, correctly; the window never regenerated. It now does,
   which is what "mesh this" means, and reports a regeneration failure as the
   mesh's failure.
```

**And four tests that could not fail**, three of them written in this
milestone and passing. A box cannot distinguish two tessellations because a
plane is exactly representable; a coarse tessellation request is a no-op
because OCCT does not coarsen an existing triangulation; a block's boundary
cannot refine; and a highlight on every other facet by index may land entirely
on hidden faces. The assertions were right, the code was right, and the tests
were worth nothing until their fixtures could express the difference.

## Known limitations

Carried, and listed in full in the adversarial review: no clipping or section
plane (interior inspection needs neither), no element labels in the 3D scene
(FreeType is off, so they are in the panel), no quality heatmap, the display
tests need a GL implementation, one body per document in the GUI, and a pick at
a non-unit device pixel ratio is still reasoned rather than clicked.

## Revision

First issue, 2026-10-04.

## Regression

Three presets, each cleaned, configured, built from scratch, rebuilt to prove
nothing was left to do, and run in full. Harness `qualification/qualify.cmd`,
carried byte for byte from `P15-QUAL-001`
(`git hash-object d313a64070718c44fae290ac042fe259d1a03c8b`); invocation
`qualification/run-qualification.cmd`.

```text
                   configure  clean  build        rebuild  full suite
debug-ext              0        0      0 602/602     0     0  3219/3219
release-ext            0        0      0 602/602     0     0  3219/3219
debug-shared-ext       0        0      0 602/602     0     0  3219/3219

compiler diagnostics   0 errors, 0 warnings, 0 FAILED, under -Werror,
                       in all three builds

determinism, 301 tests x5
  release-ext          100% of 301     3266 s
  debug-ext            100% of 301     3495 s

qualify.cmd exit 0 -- "every stage exited 0", 0 stage(s) failed
ran  2026-10-04 14:45:59  ->  18:24:57
```

9657 test executions across the three presets, plus 3010 in the determinism
stages, with no failure anywhere. The suite grew from 3151 tests to 3219.

The no-op rebuild log is `[1/8] Checking git revision` in each preset --
byte-identical in shape to `INFRA-VIEWER-001`, `P16-MAP-001` and
`P16-QUALITY-001`, which is this project's signature for "nothing left to
build": the git-revision command always reruns, leaves its output unchanged,
and Ninja prunes everything downstream.

## The qualified tree is the committed tree

Recorded independently before the first build, and again by the harness after
the last test run. All three agree.

```text
apps               5b2059dc47dd2db91e3fc6a1c6b361074c15a0a6
include            a16629454910ea4cd2d120bd0f0087690a2d9836
src                3d7a140bdbcf906d28eeace4bf5ed43f825242c8
tests              1806573a4638121d8c32bf125ad6a305933249bc
examples           9187931fa6824e98d3da6296cdc40af6ca4b8607
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e

whole fingerprint  6103d53414a0ccdfa87d7edf0c6f62d921e1b535
```

Details, and the gates cleared before the freeze, in [FREEZE.md](FREEZE.md).

## Result

```text
TASK:            P16-VIZ-001 -- mesh visualisation and inspection
IMPLEMENTATION:  a read-only visualisation adapter with no Qt and no OCCT
                 (MeshView, MeshInspection, MeshScene); one batched OCCT
                 presentation per mesh with per-primitive selection; a Qt
                 inspector panel and the mesh plumbing in the main window
TESTS:           78 renderer cases / 4198 assertions, of which 39 are
                 headless; 5 compile-failure cases; 10 GUI claims; and
                 3219 tests in each of three presets
VALIDATION:      counts taken from the canonical mesh, never from a backend
                 figure; positions compared with the canonical node EXACTLY;
                 edges counted a second way by a different container; the
                 tetrahedron face windings proved as a property; the
                 mesh-over-CAD overlay proved by silhouette equality on three
                 fixtures; both linkage directions through P16-MAP-001's own
                 API with no geometric search anywhere
ADVERSARIAL:     3 production defects and 6 test gaps found and fixed;
                 19/19 mutations killed BY A TEST, 0 survived, 0 compiler-only
RESULT:          PASS
EVIDENCE:        this directory; qualification/qualification-times.txt
TODO:            18 checkboxes ticked
```

### The gate, clause by clause

```text
engineering surface mesh inspectable        MeshView::surfaceOf, displayed and
                                            picked
volume-mesh boundary inspectable            the VolumeMesh's own Triangle3
                                            elements; no interior face can
                                            reach the buffer
wireframe available                         deduplicated edges, and a style
nodes inspectable                           canonical position with its unit,
                                            boundary or interior, and the CAD
                                            face behind its facet
Tet elements inspectable                    nodes, SIGNED volume, and the
                                            report's own metrics
ElementId visible in inspection             in the panel; FreeType is off, so
                                            not in the 3D scene
quality report inspectable                  the report's categories and
                                            per-metric worst, never the GUI's
worst-element navigation works              per metric, from the report
mapped boundary regions highlight           by FaceName, by face index, and by
                                            named boundary set
CAD -> mesh linkage works                   including an UNNAMED hole wall,
                                            which only the index path reaches
mesh -> CAD linkage works                   sourceFaceOf, reporting unnamed
                                            faces as unnamed
visibility toggle without modifying mesh    hidden is not deleted
current mesh visually explicit              one state label, from the
                                            document's geometry revision
stale mesh visually explicit                a distinct colour, asserted as a
                                            pixel difference
failed remesh cannot look current           GenerationFailed is its own state
render cache tied to mesh revision          MeshStamp, and describes()
old cache not reused after remesh           refused, with both generations
                                            named in the message
GUI duplicates no canonical authority       a finished Mesh has NO mutator,
                                            proved as a build failure
rendering and inspection are read-only      whole-mesh fingerprints
transformed meshes in the correct frame     three fixtures, including XZ
hole-wall linkage PASS                      every facet within 6.1 mm of the
                                            bore axis
determinism PASS                            byte-identical buffers; 301 tests
                                            x5 in Release and in Debug
adversarial review PASS                     see ADVERSARIAL_REVIEW.md
mutation protection PASS                    19/19 killed by a test
GUI regression PASS                         the full suite in all three presets
Debug / Release / Debug-shared PASS         3219/3219 each
0 unexpected warnings                       0 warnings, all three builds
evidence complete                           this directory
```

Interior element inspection is **implemented**, not deferred:
`MeshView::tetrahedraOf` builds the four faces of the chosen tetrahedra as a
view of their own, which needs no clipping plane and does not turn the whole
volume into geometry.

## Revision

First issue, 2026-10-04.
