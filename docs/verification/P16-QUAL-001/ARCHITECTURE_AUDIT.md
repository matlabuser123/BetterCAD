# P16-QUAL-001 — architecture, authority and identity audit

```text
SUBJECT:  whether the final P16 architecture is the one the ADRs decided, and
          whether the CAD/mesh authority boundary can be reversed anywhere
METHOD:   searched for the FAILURE MODE rather than the promise. A grep that
          finds the reassuring comment proves nothing; a grep that finds no
          occurrence of the forbidden construct does
```

## The pipeline, as the committed tree implements it

```text
authoritative CAD model          Document: features, parameters, sketches
        |
        v  Regenerator
regenerated authoritative        requireMeshableGeometry -- the ONE geometry
geometry                         boundary; refuses stale, failed, blocked,
        |                        non-solid, zero-volume, and a configuration
        |                        override, in that precedence order
        v  generateSurfaceMesh
engineering surface mesh         closed, manifold, coherently oriented, or it
        |                        does not exist
        v  generateTetrahedra
volume meshing backend           Point3D + Length + 0-based indices only
        |
        v  generateVolumeMesh
canonical BetterCAD Tet4 mesh    validated, conformity-checked, volume-
        |                        recovered, or REFUSED
        v
quality / mapping / visualisation / P17
```

**The single approved path is measured, not asserted.**

```text
generateTetrahedra called outside a backend adapter   1 place
                                                      src/meshing/VolumeMesh.cpp:283
EngineeringSurfaceMesh constructed outside meshing    0 places
nglib / Ng_* outside src/meshing/netgen/              0 files
```

So there is no route to a tetrahedralisation that skips the validated surface,
and `CheckLayering.cmake`'s Rule 5 fails the build if a backend header appears
outside an adapter directory.

## The authority matrix

```text
| DATA                            | CANONICAL? | WHERE IT LIVES / WHY NOT       |
| CAD geometry                    | YES        | Document, features, parameters |
| meshing settings                | YES        | MeshControl::definition().mesh |
| local sizing intent             | YES        | LocalMeshSizing, keyed by FaceName |
| GeometryReference intent        | YES        | FaceName -- persisted, undoable |
| boundary / region intent        | YES        | NamedBoundarySet, with its own id |
|                                 |            |                                 |
| engineering surface triangles   | NO         | inside generateVolumeMesh; no
|                                 |            | document object holds one       |
| volume nodes                    | NO         | Mesher::Held::mesh, keyed by control |
| Tet4 connectivity               | NO         | same                            |
| boundary facets                 | NO         | derived per generation; ElementId
|                                 |            | is invalidated by every remesh  |
| quality report                  | NO         | Mesher::Held::quality           |
| mapping cache                   | NO         | Mesher::Held::map; constructible
|                                 |            | only by buildGeometryMeshMap    |
| render cache                    | NO         | renderer::MeshScene, revision-bound |
| Netgen internal state           | NO         | does not outlive the adapter call |
```

### The matrix as one executable statement

A table is a claim. `P16Qualification_GeneratingAMeshLeavesTheCanonicalDocumentByteIdentical`
is the check: save the document, generate a mesh and its map and its quality
report, save again, and require the bytes to be **identical** — with the object
count and the document revision unchanged too, so generation is shown not to be
an edit.

```text
RM-MESH-03: mesh generated (126 tets, map complete, quality valid)
            document bytes before == after
            object count unchanged, document revision unmoved
```

And `P16Qualification_TheSavedDocumentIsIndependentOfMeshDensity` is the
stronger form the brief asks for, asserted as byte identity rather than
"essentially independent":

```text
RM-MESH-02 at 24 mm   114 nodes,  361 tets
RM-MESH-02 at  6 mm   376 nodes, 1977 tets
saved document        3347 bytes EITHER WAY

a node array for the fine mesh alone would be 376 x 3 x 8 = 9024 bytes,
nearly three times the whole file -- which is why byte identity is the
decisive form of this check
```

### Structural impossibilities, by construction rather than by discipline

```text
VolumeMesh          private constructor; generateVolumeMesh is its only friend
GeometryMeshMap     private constructor; buildGeometryMeshMap is its only friend
Mesh                immutable by API -- every accessor const, returns
                    std::span<const T>, build() returns by value
MeshControlDefinition   has no member that could hold a result
```

So "a mesh cannot become canonical" is not a rule someone must remember: there
is no default-constructible `VolumeMesh` to put in a document, and no field to
put it in.

## Mesh identity

```text
| IDENTITY DOMAIN     | TYPE                      | PERSISTENT? | SURVIVES REMESH? |
| ObjectId            | Id<Tag>, 64-bit           | YES         | yes              |
| MeshControlId       | Id<MeshControlIdTag>      | YES         | yes              |
| BoundarySetId       | Id<BoundarySetIdTag>      | YES         | yes              |
| FaceName            | ObjectId + FaceSelector   | YES         | yes, or reports
|                     |                           |             | Unresolved       |
| NodeId              | standalone uint32 class   | NO          | NO               |
| ElementId           | standalone uint32 class   | NO          | NO               |
| RegionId            | standalone class          | NO          | NO               |
| backend marker      | does not exist            | n/a         | n/a              |
| viewer primitive id | renderer-local            | NO          | NO               |
```

**The domains are distinct in the type system, not by convention.**

```text
NodeIdTag / ElementIdTag / RegionIdTag in core/Id.hpp      0 occurrences
NodeId or ElementId in src/io or include/bettercad/io      0 occurrences
NodeId or ElementId in MeshControlDefinition                0 -- no such member
NodeId or ElementId in any command's API                    0 occurrences
NodeId or ElementId in FaceName                             0 -- it is ObjectId
                                                               + FaceSelector
```

`NodeId` and `ElementId` are 32-bit handle classes with no conversion to an
integer, to each other, or to `ObjectId`; fifteen compile-fail cases pin that.
A handle is meaningful only with the `MeshStamp` it came from, and `Mesh::owns()`
is how a holder checks.

Counted from `tests/compile_fail/CMakeLists.txt` rather than quoted from an
earlier milestone, because an inflated figure is as bad as a wrong one:

```text
meshids           15   the handle types themselves -- every conversion among
                       NodeId, ElementId, RegionId and ObjectId, both ways
meshcmd           13   a command API that mentions a node, an element, or a
                       definition holding either
geometrymeshmap    5
meshview           5
meshquality        4
volumemesh         3
                  --
                  45   mesh-related cases, of 176 in the whole suite
```

**Can `ElementId` survive a remesh when it should not?** The question is
answered by what can store one. Nothing persistable can: not the file format,
not a control, not a command, not a face reference. A `NamedBoundarySet` holds
`FaceName`s and resolves to facets *per mesh*, so a remesh produces new handles
for the same intent — which `BoundaryFacetSet` reports rather than silently
reusing.

## Element orientation and validity

Computed in the test from the node coordinates — a determinant per element, not
a production call — over every valid reference model:

```text
positive      1751
zero             0
negative         0
non-finite       0
```

**There is no `abs()` on a signed volume anywhere in the meshing module.**
Searched for, not assumed: `abs(`/`fabs(` applied to a volume returns zero
matches in `src/meshing/` and `include/bettercad/meshing/`. The convention is
stated at `signedVolume`'s declaration and the sign is what distinguishes an
inverted element from a correct one.

Refusal is structural: `generateVolumeMesh` runs `validate(mesh)` at
`VolumeMesh.cpp:414` and returns `InvalidMesh` at 418 **before** summing the
volume at 431, so a mesh with a zero or negative element cannot come into
existence — which is also the proof that the one surviving mutation in
P16-REFMOD-001's mutation set (`abs()` on the volume sum) is unobservable
rather than untested.

```text
MeshIssueKind covers   NonFiniteCoordinate, MissingNodeReference,
                       RepeatedNodeReference, DegenerateTriangle,
                       DegenerateTetrahedron, InvertedTetrahedron,
                       DuplicateTetrahedron, NodeSharedBetweenRegions,
                       MissingRegion, EmptyMesh
```

An EMPTY mesh is structurally invalid (`EmptyMesh`), so a zero-element result
cannot be reported as a pass for want of bad elements.

## The backend's return code is never evidence

```text
VolumeBackendFailure::NoTetrahedra     a DISTINCT failure, not folded into
                                        GenerationFailed
NetgenBackend.cpp:372-379               reads Ng_GetNP and Ng_GetNE and refuses
                                        when either is <= 0, whatever NG_OK said
```

And it is tested against the real behaviour rather than a stub:
`VolumeBackend_RefusesAnOpenSurfaceRatherThanReportingSuccess` feeds nglib a
tetrahedron with one face removed; nglib returns `NG_OK` with zero elements, and
the adapter turns that into a failure.

> **What this leaves unverifiable, stated rather than implied.** The
> zero-element path cannot be reached *from a document*, because
> `requireMeshableGeometry` refuses a non-solid before a surface is ever built —
> so the CLI cannot be made to exhibit it. The guard is exercised at the backend
> seam, which is where it lives. The CLI's non-zero exit on a generation failure
> is covered by the paths that are reachable, including RM-MESH-08's five
> queries.

## GUI authority

```text
canonical nodes in apps/bettercad/        none
Tet arrays                                none
quality thresholds of its own             none -- it calls reportOnlyThresholds()
mapping semantics                         none -- it calls geometryMeshMapFor
mesh currentness logic                    none -- Mesher::currency answers it
```

The GUI calls the production entry points, hands the results to
`renderer::MeshScene::adopt`, and has `dropMesh()` to discard derived state. It
defines no formula and no threshold.

> **One recorded gap, not an authority violation.** `MainWindow.cpp` calls
> `volumeMeshFor(document, regenerator, feature, {})` — **default** controls,
> not the document's `MeshControl`. P16-CMD-001 recorded "wiring the GUI to a
> `MeshControl`" as explicitly `OUT` of scope, with its reason ("needs GUI undo,
> which TODO.md does not authorize here") and a cross-reference to its own
> finding A3. The GUI's mesh is still derived state and it still owns nothing
> canonical; what it does not yet do is honour the document's meshing intent.
> Carried into P16's known limitations.
