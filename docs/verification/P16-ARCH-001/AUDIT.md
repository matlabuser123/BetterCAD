# P16-ARCH-001 — repository audit

Done before any architectural decision and before any meshing code, because the brief
forbids choosing a library first and because every audit in P15 found the work already
partly done.

Everything here is a statement about **this tree**, at `11b6c76`. Where a claim could not
be established from the repository it says so.

## 1. Existing tessellation and triangulation

Full-tree search for `triangulat|tessellat|BRepMesh|Poly_Triangle|Poly_Array1OfTriangle|
Deflection` over `src/ include/ apps/ examples/`.

| Component | Purpose | OCCT API | Persistent or derived? | Engineering-grade? | Display-only? | Reusable in P16? |
| --- | --- | --- | --- | --- | --- | --- |
| `geometry::triangulate()` — `core/geometry/Mesh.hpp`, `occt/OcctMesh.cpp` | surface triangulation of a body | `BRepMesh_IncrementalMesh` | **derived, transient** — meshes a *copy*, so nothing is cached on the body | **surface only**; watertight and outward-oriented, but not a volume mesh and carries no quality metrics or geometry mapping | no — it has no display consumer at all | **as an input candidate**, after node welding; not as an engineering mesh |
| `io::exportStl` — `src/io/ModelExport.cpp` | STL output | via `triangulate()` | derived | no — an exchange format | no | no |
| `geometry::HiddenLine` — `occt/OcctHiddenLine.cpp` | drawing edge projection | HLR, **curve sampling to a chord deflection**, not surface triangulation | derived | n/a | n/a | no — different problem |
| `drawing::Section` | section geometry | no triangulation | derived | n/a | n/a | no |
| `src/renderer/` | — | — | — | — | — | **the directory contains one file: `.gitkeep`** |

**`triangulate()` has exactly one production consumer: STL export.** Nothing else in
`src/`, `include/` or `apps/` calls it, and there are **no other `BRepMesh` or
`Poly_Triangulation` users anywhere in the tree.**

### The vertex-duplication finding, measured rather than assumed

`Mesh` stores vertices **per face**: `OcctMesh.cpp` keeps a running `offset` and appends
each face's nodes, so a vertex on an edge shared by two faces appears twice with identical
coordinates. The data structure is therefore a *non-conforming* triangle soup.

It is nonetheless **geometrically** watertight, and the repository already proves it far more
broadly than a spot check. `tests/core/geometry/MeshTests.cpp:44` asserts
`surface.watertight()` and `surface.vertices == 8` for a box. Those figures come from
`tests/support/MeshAnalysis.hpp`, whose `checkSurface()` *"merges vertices with identical
coordinates and checks the edge structure"* and reports `vertices = distinct positions`. So a
box's 24 stored vertices weld to 8, with 0 open edges and 0 bad edges.

**15 watertightness assertions across 11 test files**, counted rather than estimated: the
triangulator and STL tests, and the file round-trip tests for chamfers, fillets, holes, lofts,
revolves, sweeps, mirrors and both pattern kinds. So the claim is not "it worked on a box" — it
holds across the feature set, welded by exact coordinate equality every time.

That is still an observation over a fixture set and not a proof: exact-coordinate welding works
because OCCT emits bit-identical coordinates for a shared node on these shapes. The evidence is
strong, broad, and not a general guarantee.

Both facts matter to P16 and they are different facts:

```text
geometrically watertight and outward-oriented   yes -- 15 assertions, 11 files
topologically conforming as stored              NO -- shared edges duplicate vertices
```

A volume mesher needs the second, and the gap between the two rows is exactly the work
`P16-SURF-001` has to do.

## 2. OCCT meshing actually invoked

```text
Does BetterCAD call BRepMesh_IncrementalMesh?   yes, in exactly one place
Where?                                          src/core/geometry/occt/OcctMesh.cpp:50
Deflection settings?                            MeshOptions{ linear 0.1 mm, angular 20 deg }
User-visible?                                   yes -- CLI `export-stl --tolerance --angle`
Display/rendering settings?                     no; there is no renderer
Cached on the TopoDS_Shape?                     NO -- it meshes a BRepBuilderAPI_Copy
                                                deliberately, so results cannot depend on
                                                earlier meshing requests
Invalidated on regeneration?                    not applicable -- nothing is retained
Normals / orientation inspected?                orientation yes: a TopAbs_REVERSED face has
                                                its triangle winding swapped
Watertightness checked?                         not in production; only in tests
Volume conservation checked?                    not in production; the STL tests check it
```

**OCCT triangulation in this tree serves export only.** Its status is not upgraded by
assumption: there is no renderer to have made it a display mesh, and no analysis consumer
to have made it an engineering mesh.

## 3. Existing engineering mesher

Searched for `Tet|Tetra|Element|Node|VolumeMesh|FEMMesh|FEAMesh|gmsh|netgen|tetgen|mmg|
salome|medcoupling|vtkUnstructuredGrid`.

```text
Does BetterCAD already contain an engineering mesher?     NO
```

No tetrahedral, hexahedral, prism or finite-element mesh exists. No node/element
connectivity type exists. `NodeId` and `ElementId` do not exist.

## 4. OCCT's own volume-meshing capability

OCCT **8.0.1** (from `Standard_Version.hxx`: `OCC_VERSION_COMPLETE "8.0.1"`).

```text
headers matching tet/tetra in the OCCT include tree   14, ALL false positives
                                                      (DateTime, Trihedron, Tangence)
the Poly_* family                                      entirely SURFACE: triangulations,
                                                       polygons, coherent triangulation
toolkits                                               TKMesh, TKXMesh -- surface meshing
```

```text
Does OCCT 8.0.1 ship a volume/tetrahedral mesher?      NO
```

Two OCCT headers are directly relevant and were read rather than guessed at:

* **`Poly_MeshPurpose`** — OCCT itself distinguishes
  `Poly_MeshPurpose_Calculation` *("mesh for algorithms")* from
  `Poly_MeshPurpose_Presentation` *("mesh for presentation (LODs usage)")*, and a shape may
  carry several triangulations with different purposes. **The display-versus-engineering
  boundary this milestone must define is one the kernel already models.**
* **`Poly_MergeNodesTool`** — looks like the tool that would weld the duplicated nodes into
  a conforming surface, but its own header says *"Auxiliary tool for merging triangulation
  nodes **for visualization purposes** … but split the ones on sharp corners at specified
  angle."* Splitting at sharp corners is the opposite of watertight. **It is not a route to
  an engineering surface mesh.**

## 5. Third-party dependencies

Every third-party library the build declares, in full: `Catch2`, `Eigen3`, `nlohmann_json`,
`OpenCASCADE`, `Qt6` (plus `Git`, a tool rather than a library).

Searched for `gmsh|netgen|tetgen|mmg|cgal|salome|medcoupling|vtk` across `cmake/`,
`deps/CMakeLists.txt`, `CMakeLists.txt` and `CMakePresets.json`. **One hit, and it points the
other way:** `deps/CMakeLists.txt:138` passes `-DUSE_VTK:BOOL=OFF` to OCCT's own build. So there
is not even an indirect visualization-toolkit path to mistake for a mesher.

```text
a mesher among BetterCAD's dependencies    NONE, direct or indirect
```

`Eigen3` is already a dependency, which matters to P17 rather than to P16.

## 6. Geometry validity infrastructure

```text
Body::isValid()        BRepCheck_Analyzer, wrapped; an EMPTY body is not valid;
                       a kernel exception is caught and reported as invalid
Body::isEmpty()
Body::topology()       TopologySummary{ solids, shells, faces, edges, vertices }
Body::massProperties() fails with FailedPrecondition for an empty body
```

`BRepCheck_Analyzer` is used in exactly two places (`OcctBody.cpp`, `OcctSweeps.cpp`) and
`ShapeFix` / `ShapeAnalysis` are **not used at all** — the tree does no shape healing.

**P16 reuses `Body::isValid()` and `TopologySummary` and defines no second validity
notion.**

## 7. Stable geometry references

The vocabulary is in `core/document/References.hpp`, and it is **generative, not
positional**:

```text
FaceName { ObjectId feature; FaceSelector face; }
FaceSelector { FaceRole role; optional<EntityId> entity, along; optional<SketchId>
               alongSketch; optional<ChamferEdgeId> edge; vector<FaceCopy> copies; }
FaceRole     StartCap EndCap Side HoleBottom CounterboreFloor Chamfer SpotfaceFloor
```

A face is named by **the feature that generates it, its role there, the profile entity that
sweeps it, and the chain of pattern/mirror copies made of it** — never by index. A
chamfer's face names the chamfer's edge *selection* by stable ID, *"not its position in the
chamfer's list, which reordering would change under a stored reference (ADR-024)"*.

Resolution is `features::checkFaceName()`, whose failures are structured: `NotFound` when
the feature is gone, when the profile entity is gone, or when **no face carries the name
because the feature's own operation removed it**; `InvalidArgument` for a malformed
selector.

```text
Can a CAD face be referred to persistently today?       YES -- FaceName
Does the reference survive regeneration?                YES -- it is resolved at use, and
                                                        names how the face was MADE
Does it survive a topology-changing edit?               NOT ALWAYS -- it becomes
                                                        explicitly unresolved, which is a
                                                        reported state and not a silent
                                                        rebind
Can local mesh sizing safely target it?                 YES, with an explicit unresolved
                                                        state -- the same shape P15 used
                                                        for a material assignment
Is the residue a P21 problem?                           YES: preserving semantic face
                                                        identity ACROSS a topology change
                                                        is P21, and P16 must not claim it
```

## 8. Configuration and regeneration — the carried defect

Root cause, quoted from the guard P15-MASS-001 already wrote:

> A configuration override changes a parameter's EFFECTIVE value without changing the
> parameter object, so the regenerator does not mark the features that read it dirty and
> their bodies are still the base configuration's.

The existing guard, in `src/features/material/MassProperties.cpp`, refuses rather than
answering:

```cpp
if (const std::optional<ConfigurationId> active = document.activeConfiguration()) {
    const Configuration* configuration = document.configurations().find(*active);
    if (configuration != nullptr && !configuration->overrides().empty()) {
        return makeError(ErrorCode::FailedPrecondition, ...);
    }
}
```

**P16 reuses this exact shape.** It is not a new mechanism and not a new definition of
staleness.

### Change-tracking mechanisms that already exist

```text
Document::revision()            increments on every effective change
Document::revisionOf(ObjectId)  per-object revision
Document::isDirty()             revision != cleanRevision
Regenerator                     "finds the items whose revision changed since they were
                                last built", marks those and everything downstream dirty
NodeState                       per-item regeneration state
```

**Stale-mesh detection needs no invented hashing.** A generated mesh can record the
document revision and the revisions of the objects it was built from, which is the same
information the regenerator already uses.

## 9. Module layering, as enforced

`tests/architecture/CheckLayering.cmake` fails the build on a violation:

```text
core 0   sketch 1   features 2   assembly 3   drawing 4   io 5   renderer 6   scripting 6
Qt allowed only in    ^(apps/bettercad|src/renderer)/
OCCT allowed only in  ^src/(.+/)?occt/
```

The table's own comments record the precedent for inserting a module: when `assembly` and
then `drawing` were added, *"there is no number between them and io moves up with
everything above it"* (ADR-006, ADR-015).

A second precedent matters more: *"Projection and hidden-line removal are NOT here: they
are kernel work and live in core/geometry behind the occt adapter."* **Surface
triangulation already follows that rule** — `Mesh.hpp` is in `core/geometry` and
`OcctMesh.cpp` is behind the adapter.

## 10. Canonical-versus-derived is already architecture

`ARCHITECTURE.md` already says:

> Geometry, render meshes, drawings, **simulation meshes** and analysis results are derived
> representations of that model. That distinction is the basis of everything below.

and its system overview already lists `Simulations [future]` as a Document-level concept
beside Parameters, Sketches, Features, Bodies and Assemblies.

**P16's central invariant is pre-existing architecture.** This milestone details it; it does
not introduce it.

## 11. One part, one document

P15-ASSIGN-001 established, and P15-QUAL-001 re-verified, that a `Document` holds exactly
one optional `MaterialId`, that `ComponentDefinition` has **no** material field, and that an
assembly places occurrences of part definitions **inside one document** because
cross-document part references need ADR-003 and are deferred.

```text
Can one current BetterCAD part carry multiple engineering materials?   NO
```

## Consequences for the architecture

1. There is **no engineering mesher and no volume mesher** — neither in BetterCAD nor in
   OCCT 8.0.1. An external backend is required, and its selection is licence-gated.
2. There is **no display tessellation** to be confused with an engineering mesh today, but
   the boundary must still be defined, because P16-VIZ-001 will create one — and OCCT's own
   `Poly_MeshPurpose` gives the vocabulary.
3. The surface triangulator is a **candidate input**, not an engineering mesh, and the gap
   is precise and measurable: conforming connectivity.
4. Every supporting mechanism P16 needs already exists — validity (`Body::isValid`), stable
   face references (`FaceName`), staleness (`Document::revision` + the configuration guard),
   derived-state ownership (`Regenerator` holds bodies; the Document holds features).

---

# Findings that changed the architecture

Four things the audit turned up that a design started from a filename would have missed.
Each is checkable in the tree.

## A. BetterCAD has no licence, so a backend licence cannot be evaluated in isolation

`LICENSE`, in full:

> Copyright (c) 2026 BetterCAD contributors. All rights reserved.
> **No license has been chosen for BetterCAD yet.** Until one is added to this file, no
> permission is granted to use, copy, modify or distribute this software.

`README.md` §License agrees: *"Not yet chosen."*

Every third-party component BetterCAD ships today is **weak-copyleft, dynamically linked**:
OCCT LGPL-2.1-with-exception, Qt LGPL-3, Catch2 BSL-1.0 (tests only).

The consequence is the single most important fact in this milestone:

```text
choosing a GPL or AGPL mesher does not merely add a dependency.
It DECIDES BetterCAD's own unchosen licence, or forbids distribution.
```

That is a project-ownership decision, not an engineering one. It is exactly the class of
thing `CLAUDE.md` says is "a scope decision, not Claude's to make". So this milestone
defines the backend **boundary and the admission rule** — which is what the P16 gate asks
for ("backend boundary explicit") — and does not silently bind the project to a licence.

## B. The OCCT containment rule would NOT catch a mesh backend

`CheckLayering.cmake` rule 1 detects an OCCT header **by its file extension**:

```cmake
if(header MATCHES "\.hxx$" AND NOT file MATCHES "${occt_allowed_regex}")
```

Rule 3 only inspects headers matching `^bettercad/([^/]+)/`. Rule 2 only matches Qt's
`Q[A-Z]...` shape.

Every candidate backend's entry header is a `.h`:

```text
nglib.h  nginterface.h  netgen/...      Netgen
tetgen.h                                TetGen
gmsh.h                                  Gmsh
CGAL/...                                CGAL
mmg/...  libmmg*.h                      MMG
```

**None of them matches any of the four rules.** A backend header could be included from
`src/features/`, from a public header in `include/`, or from the GUI, and the architecture
test would pass. P16's invariant *"Backend-specific behaviour must remain behind a meshing
interface"* is therefore **unenforceable as the tree stands**, and an ADR asserting it would
be prose.

This milestone closes that hole, because an invariant that only a reviewer enforces is one
the next milestone breaks.

## C. `bettercad::Id` is the wrong template for a mesh node, and its own docs say so

`include/bettercad/core/Id.hpp` states the contract of every `Id<Tag>`:

> "IDs are identities, never container indices: they stay stable when other objects are
> added or removed, and they are **persisted with the document**."

and `IdAllocator` exists so that "a stale reference can never silently resolve to a newer
item".

A mesh node is the opposite on all three counts. Set the two side by side:

```text
                        bettercad::Id<Tag>        a mesh node
stable across rebuild   yes, that is the point    NO -- remeshing may invalidate it
persisted               yes                       NO -- nodes are never persisted
an index                never                     effectively yes -- dense, 0..n-1
allocated by            IdAllocator, never reused  the mesher, reused every generation
```

`NodeId = Id<NodeIdTag>` is the obvious move and it is a lie: it would put a volatile,
non-persistent, index-like handle behind a type whose documented promise is the reverse, and
`IdAllocator`'s never-reuse guarantee — the mechanism that makes a stale CAD reference safe —
would be silently absent for meshes. ADR-031 rejects it explicitly.

## D. `FaceId` exists, is unused, and must not be adopted for mesh mapping

```text
grep -rn "\bFaceId\b" include/ src/   ->   one hit: the alias's own definition
```

`FaceId`, `EdgeId` and `VertexId` are **declared and never used anywhere**, and their comment
says why:

> "Topology IDs; unique within their body. **Persistent naming across regenerations is future
> work** (semantic topology naming)."

They are a P21 placeholder. They look exactly like the right type for "which CAD face does
this boundary facet belong to", and using them would give mesh→geometry mapping an identity
with no defined behaviour across the regeneration that mapping exists to survive.

**The mapping uses `FaceName`**, which is implemented, generative and structured in failure.
ADR-032 records this.
