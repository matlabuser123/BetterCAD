# P16-SURF-001 — display tessellation audit

Done before writing any surface-meshing code. Every claim names the file it came from.

## 1. Every path in the repository that triangulates

Searched for `BRepMesh_IncrementalMesh`, `Poly_Triangulation`, `AIS_Shape`, `deflection`,
`angular`.

| Code path | Purpose | Settings | Lifetime | Validation | Authority | Reusable backend? |
| --- | --- | --- | --- | --- | --- | --- |
| `geometry::triangulate()` — `core/geometry/Mesh.hpp`, `occt/OcctMesh.cpp` | the only triangulator | `MeshOptions{linear, angular}`, caller-supplied | **transient**: meshes a copy, returns a value, keeps nothing | none in production; tests check closure | derived | **yes, and reused** |
| `io::exportStl` — `src/io/ModelExport.cpp` | STL output | via `triangulate()`, from CLI `--tolerance --angle` | transient | STL tests check watertightness | derived, an exchange format | n/a |
| `meshing::generateSurfaceMesh` — NEW | the engineering surface | `SurfaceMeshControls`, this milestone's own | a value, held by the caller | **structural validation, and it gates the result** | derived | — |
| `AIS_Shape` | — | — | — | — | — | **does not appear in the repository** |
| `src/renderer/` | — | — | — | — | — | **one file: `.gitkeep`** |

```text
Is there a display tessellation path today?    NO. There is no renderer and no
                                              AIS_Shape anywhere.
```

So the separation this milestone must establish is not between two existing paths; it is a
contract that will hold **when** a display path arrives. That makes the structural argument
below more important than a behavioural one, because there is no second path to measure
against yet.

## 2. Why display/engineering contamination is structurally impossible

`OcctMesh.cpp`, with its own comment:

```cpp
// Mesh a copy: the kernel stores triangulations on the faces, and the
// body's faces are shared with every copy of the body. Meshing them in
// place would make results depend on earlier meshing requests.
BRepBuilderAPI_Copy copier(*shape, /*copyGeom=*/false, /*copyMesh=*/false);
```

Three consequences, and none of them is a policy anyone has to remember:

```text
can a cached Poly_Triangulation be READ as the engineering mesh?
    NO. copyMesh=false, so the copy starts with no triangulation at all and the
    mesher must produce one from the controls it was given.

can engineering meshing WRITE a triangulation onto the authoritative faces?
    NO. Only the copy is meshed. The authoritative shape is never touched.

can a viewer's deflection reach the engineering mesh, or vice versa?
    NO, because neither can see the other's triangulation. They cannot share a
    cache even by accident.
```

This answers §6, §7, §41, §42, §43 and §44 together, and it answers them by construction
rather than by discipline. §71 asks for no vague statement that the two are "logically
separate" — they are separate because there is no shared mutable state between them.

The cost is a shape copy per request, which is the price of that guarantee and is paid once
per mesh rather than per solve.

## 3. Face orientation: already correct, and load-bearing

`OcctMesh.cpp`:

```cpp
// Node order follows the surface parametrization; a reversed face
// points the other way.
const bool reversed = face.Orientation() == TopAbs_REVERSED;
...
if (reversed) { std::swap(b, c); }
```

§12 calls this a likely bug source. It was already handled — and the mutation test shows how
much depends on it: **ignoring `TopAbs_REVERSED` fails 23 of 30 tests.** A box has reversed
faces, so this is the main line for every solid here rather than an exotic corner.

## 4. `TopLoc_Location`: applied exactly once

```cpp
const gp_Trsf transform = location.Transformation();
for (int i = 1; i <= triangulation->NbNodes(); ++i) {
    mesh.vertices.push_back(occt::pointFromModel(triangulation->Node(i).Transformed(transform)));
}
```

Once, per face, at the point the node is read. There is no second application and no path
that reads `triangulation->Node(i)` without it. §13's "dropped" and "applied twice" are both
excluded by there being exactly one place a node becomes a model-space point.

## 5. What was missing, and is this milestone's work

```text
conforming topology   geometry::Mesh stores vertices PER FACE: a vertex on a
                      shared edge appears once for each face. Geometrically
                      watertight, NOT topologically conforming. A solver boundary
                      needs shared nodes.
identity              no NodeId, no ElementId: raw uint32 indices into a vector.
validation            none in production. Closure was checked only by tests, with
                      a test-only helper.
orientation proof     nothing checked that the surface as a whole faces outward.
per-face grouping      no way to tell which triangles came from which CAD face.
```

The last one was added here, additively, as ADR-033 said it would be: *"P16-SURF-001 extends
it additively — per-face triangle grouping, so a higher layer can attribute triangles to
faces — and does not move it."* `MeshFace` carries a triangle range and **no `FaceName`**:
attributing a facet to a named face is `P16-MAP-001`'s, and a name here would start that
milestone early.

## 6. Node unification: the rule, and why this one

**Exact coordinate equality in model space.** No tolerance, and therefore none to tune.

Why that is a topological identity here rather than an approximation: the kernel discretises
a shared edge **once**, and both adjacent faces index that same discretisation, so the two
faces' nodes on that edge are the same numbers — not nearly the same. The repository already
depended on this before P16: `tests/support/MeshAnalysis.hpp` welds by exact coordinate and
**15 watertightness assertions across 11 files** pass that way, covering chamfers, fillets,
holes, lofts, revolves, sweeps, mirrors and both pattern kinds.

### The route not taken, and why it is recorded rather than dismissed

OCCT's `BRep_Tool::PolygonOnTriangulation(edge, triangulation, location)` gives the node
indices of an edge within a face's triangulation, which would make the correspondence
topological **by construction** — the stronger rule, and the one §9 names first.

It was not used because its edge cases are exactly where cracks come from:

```text
a seam edge      a periodic face (every cylinder) has a closing edge that appears
                 TWICE on one face, with two polygons on the same triangulation.
                 The three-argument accessor returns one of them.
a degenerate edge a sphere's pole collapses to a point.
```

Each needs separate handling, and each is a silent crack if it is wrong. The rule chosen is
simple enough to be obviously right.

### What makes the simpler rule safe rather than hopeful

**The output is proven to close, and a mesh that does not is refused.**
`generateSurfaceMesh` runs `validateSurface` and returns a failure unless the boundary-edge
count, the non-manifold count and the orientation-conflict count are all zero. So a welding
failure cannot be reported as watertight — the worst case is a refusal, never a cracked
surface presented as a solver boundary.

That was a live risk, not a theoretical one: a cylinder's seam is the case where exact
welding had to work, and the cylinder fixture passed on the first run. The `PolygonOnTriangulation`
route stays recorded in `SurfaceMesh.cpp` as the fallback if a shape is ever found that exact
welding cannot close.

## 7. Determinism: what the kernel gives, and what this layer adds

```text
from the kernel   parameters.InParallel = false  // deterministic, already set
                  TopExp_Explorer face order: stable for a given shape, but
                  "architecture-dependent" by the brief's own caution
added here        NodeIds are assigned in ASCENDING COORDINATE ORDER, so the
                  numbering depends on the geometry and not on traversal.
                  Triangles are sorted by their NODE-SET key while the stored
                  connectivity keeps its original oriented winding -- §52's
                  separation, because sorting the connectivity itself would make
                  the order deterministic by destroying the orientation it exists
                  to carry.
```

So public identity does not depend on face traversal order at all, which is what the
cross-preset check then confirms rather than assumes.

## 8. Tolerances

```text
node unification     EXACT equality. No tolerance.
degeneracy           area exactly zero, or not finite. No tolerance. A THIN
                     triangle is valid here and is P16-QUALITY-001's business.
closure              integer edge counts. No tolerance.
outward orientation  the SIGN of the enclosed volume. No tolerance.
curved-surface area  a tolerance, and it belongs to the TEST rather than the
                     code: a triangulated curved surface understates the true
                     area, so the fixtures require convergence from below instead
                     of equality.
```

No `surfaceEpsilon` was introduced, so §54's prohibition is met by not needing one.

## 9. Graphics normals

```text
smoothed vertex normals anywhere in the repository?   NONE -- there is no
                                                     renderer to have any.
normals stored on the engineering mesh?               NONE. A normal is derived
                                                     from the stored winding when
                                                     something needs one, so it
                                                     cannot go stale against the
                                                     positions it came from.
```

Nothing shading-related can become the engineering orientation authority, because nothing
shading-related exists and the mesh has no normal field for it to occupy.
