# ADR-063 — The P16 Tet4 mesh is the finite-volume mesh, and a staged gate proves the contract

```text
STATUS:    Accepted
DATE:      2026-10-28
MILESTONE: P19-MESH-001
TOUCHES:   ADR-032 (a boundary names a CAD face; errata below), ADR-033 (the
           mesher behind a boundary), ADR-058 (the critical question, answered;
           errata below), ADR-060 (the input boundary this gate is staged after;
           the shared mesh seam it proposed, decided below)
```

## Context

ADR-058 answered P19-MESH-001's critical question in principle. It said a cell is
a P16 tetrahedron, and the interior topology and finite-volume metrics are new,
derived work. It also said that "if a qualified result requires another cell type,
the answer is an adapter from the P16 mesh, never a parallel representation". This
milestone qualifies that answer. It also fixes the definitions P19-TOPO-001,
P19-GEOM-001 and P19-VALID-001 must build on.

The audit of P16 (`docs/verification/P19-MESH-001/P16_AUDIT.md`) found that P16
provides most of what a finite-volume method needs. It also found exactly where it
does not:

```text
P16 PROVIDES                                          WHERE
cells: Tet4, every one with signed volume > 0         validate(): InvertedTetrahedron / DegenerateTetrahedron
                                                      -> VolumeMeshFailure::InvalidMesh
coordinates in SI metres (Point3D of Length)          Mesh; MeshBuilder refuses a non-finite one
boundary Triangle3 wound out of the solid             tetrahedralBoundary + its face table, on positive
                                                      cells (BoundaryNotConforming compares positions,
                                                      not winding)
each boundary facet attributed to a CAD face          GeometryMeshMap, by exact position
deterministic storage, ascending ids                  Mesh ("ORDERING IS DEFINED AND DETERMINISTIC")
one identity per generation                           MeshStamp { MeshId, generation }

P16 DOES NOT PROVIDE                                  CONSEQUENCE
interior faces, owner, neighbour                      derived here; built compactly by TOPO-001
an owner field on a Triangle3                         derived (owningTetrahedraOf, or this gate)
refusal of a face shared by >= 3 tetrahedra           tetrahedralBoundary drops it silently
a check that a shared face is traversed both ways     two positive cells folded onto one side pass
any face-connectivity check                           two pieces joined at a vertex mesh "fine"
ENFORCEMENT of attribution completeness               the map REPORTS gaps; nothing refuses them
finite-volume quality metrics                         defined here, computed by GEOM-001
```

`MeshBuilder` also accepts a hand-built mesh that breaks every one of these
properties. That is correct for a builder, and it is how each refusal below is
demonstrated.

## Decision 1 — Yes: the P16 mesh serves directly, through a staged gate, and there is no adapter

**A finite-volume cell is one P16 tetrahedron. A boundary face is one P16
boundary Triangle3. A patch is one CAD face of the fluid body, as P16's
`GeometryMeshMap` attributes facets to it.** No second mesh, no second geometry,
no copied coordinates and no adapter that rebuilds anything. What P16 does not hold
(interior faces, owner and neighbour, metrics) is derived from its connectivity,
keyed to its stamp, and never persisted (ADR-058's DERIVED list).

What P16 does not guarantee is not assumed. It is proved by a gate:

```text
inspectCfdMesh(const Mesh&, const GeometryMeshMappingReport&) -> CfdMeshInspection
cfdMeshRefusal(const CfdMeshInspection&, MeshControlId)       -> Error (what the gate returns)
requireCfdMesh(const CfdModel&)                                -> Result<CfdMeshContract>
cfdMeshProblem(const CfdModel&)                                -> optional<CfdMeshProblem>
```

- **Staged after the input boundary, not inside it** (ADR-053's shape, as
  `requireThermalModel` follows `requireThermalInput`). `requireCfdMesh` takes a
  `CfdModel`, so the mesh is already current, eligible and paired with its map
  (ADR-060). The gate adds the finite-volume conditions and does not re-ask those.
  ADR-060's seven input problems are unchanged.
- **Possession is the proof.** `CfdMeshContract` has a private constructor and one
  friend. It is a value that borrows nothing: it names the mesh by `MeshStamp` and
  the geometry by `GeometryRevision`. `describes(mesh)` compares stamps, never
  counts. A remesh of the same body gives an identical census and identical
  `NodeId`s, and the test shows a contract for one generation does not describe the
  next.
- **`inspectCfdMesh` and `cfdMeshRefusal` are the same code, on a plain mesh.** A
  `VolumeMesh` cannot be built by hand, so a gate that took only one could never be
  shown an inverted cell. The inspection takes a plain `Mesh` and a plain report,
  and the refusal turns a failed inspection into the error. Together they are
  exactly what `requireCfdMesh` runs.
- **The map is paired with the mesh by the `CfdModel`, not by this gate.** Both
  come from one held entry (ADR-060). The inspection compares only the facet
  counts, and the tests assert that the model's map was built for the model's
  mesh.

The adapter ADR-058 allowed for is **not needed**. Every refusal below is a
property P16's own reference meshes already satisfy, measured on all eight of them.
Quality is a separate question (Decision 6).

## Decision 2 — The definitions

All quantities are in SI (metres). `p_i` are node positions, read from P16 and
never copied into a CFD-side geometry.

```text
CELL            one Tetrahedron (Tet4). The only cell type in v1. Cells are in the mesh's
                order: ascending ElementId. A cell's identity is its ElementId for a
                diagnostic, and a compact CellIndex in TOPO-001 (ADR-058).
CELL VOLUME     V_P = meshing::signedVolume(p0, p1, p2, p3), REQUIRED > 0 and finite.
                Never an absolute value: a negative volume is an inverted cell.
CELL CENTRE     C_P = (p0 + p1 + p2 + p3) / 4 -- the centroid, exact for a tetrahedron.

FACE            a node triple. Its IDENTITY is the sorted triple of NodeIds; two cells
                share a face iff they produce the same sorted triple.
LOCAL FACES     kTetFaces = {0,2,1}, {0,1,3}, {1,2,3}, {0,3,2}; the vertex opposite each is
                kTetFaceOpposite = {3, 2, 0, 1}. Each is wound so that its right-hand
                normal points OUT of a positive tetrahedron. It is the same table P16 winds
                its boundary with, pinned by a test against tetrahedralBoundary.
INTERIOR FACE   claimed by exactly two cells, which traverse it in OPPOSITE directions
                (each winds it out of itself, so they lie on opposite sides of it).
BOUNDARY FACE   claimed by exactly one cell, and covered by exactly one boundary Triangle3.

OWNER           interior: the claimant that comes FIRST in cell order (the lower
                ElementId, hence the lower CellIndex). Boundary: the one claimant.
NEIGHBOUR       interior: the other claimant. Boundary: none.
ORIENTATION     every face is wound by its OWNER's local table, so its normal points OUT
                of the owner: from owner to neighbour on an interior face, and out of the
                fluid on a boundary face. This is the direction in which the mass flux
                phi is positive (ADR-058).
FACE CENTRE     C_f = (a + b + c) / 3, the centroid of the triangle.
AREA VECTOR     S_f = 1/2 (b - a) x (c - a), with (a, b, c) in the owner's winding.
                |S_f| is the area and n_f = S_f / |S_f| the unit normal. A positive cell
                has no zero-area face, so a face of a cell that passed has |S_f| > 0.
```

The boundary winding is checked, not assumed. A boundary Triangle3 must agree with
the owner-outward winding, which the gate tests geometrically: the triangle's normal
must point away from the owner's opposite vertex. A triangle wound into its cell is
refused.

## Decision 3 — Patches and named boundaries

```text
PATCH           one CAD face of the fluid body: one MappedFace of the GeometryMeshMap.
                Its boundary faces are the facets the map attributes to it (exact
                position, P16). Every boundary face is on exactly one patch: the gate
                requires the map to be complete.
PATCH HANDLE    MappedFace::index is a correlation handle for ONE map, never an identity,
                never persisted (P16's own rule).
NAMED BOUNDARY  a boundary condition names a GeometryReference (ADR-032, ADR-058). It
                resolves to faces through P16 alone: FaceName -> boundaryFacetsOf(map, name).
                One name may cover several patches, and one patch may carry several names.
UNNAMED PATCH   a mapped CAD face with no FaceName. Counted in the contract
                (unnamedPatches), not refused here: it is a fact about naming, not about
                the mesh. ADR-058's rule that EVERY boundary facet has exactly one
                condition means P19-BC-001 refuses an analysis in which one exists.
```

**No nearest-face fallback.** Attribution is P16's exact-position mapping. A facet
the map attributed to no CAD face, or to several, or a CAD face given no facet, is
refused by name (`UnattributedBoundary`). Assigning such a facet to the "nearest"
face is the silent rebind ADR-032 forbids. A complete report that counts a
different number of facets than the mesh holds is refused too.

## Decision 4 — Source binding

```text
GEOMETRY SOURCE   the GeometryRevision the CfdModel's mesh was built from; recorded in
                  the contract (geometry()).
MESH SOURCE       the mesh the CfdModel holds for its meshing control (ADR-060: one held
                  lookup, current, paired with its map); recorded as its MeshStamp
                  (mesh()). describes(mesh) is mesh.owns(stamp).
```

The contract is never persisted and never in history. A remesh, a geometry edit or
a new generation makes it describe nothing current. Whatever TOPO-001 builds on it
must check the stamp, not trust a count.

## Decision 5 — The refusals

`CfdMeshProblem`, reported in this order. Every finding is reported (with a count
and the first occurrence, named by its P16 handle), not just the first:

```text
EmptyMesh                    no cells
NonPositiveCellVolume        signed volume <= 0 or not finite
NonManifoldFace              a face claimed by >= 3 cells (no third side for a flux)
InconsistentFaceOrientation  two cells traverse their shared face the same way: folded
                             onto one side of it (checked exactly, on node order)
DisconnectedCells            the cells are not ONE face-connected set
InvalidFaceOwnership         a boundary triangle that is a face of no cell, lies on a face
                             two or more cells share, or claims a face another triangle
                             already claims
InwardBoundaryFace           a boundary triangle wound into its owner, or degenerate
                             against it (no outward side)
UnclosedDomain               a face one cell owns that no boundary triangle covers
UnattributedBoundary         the map's report is incomplete, or counts a different number
                             of facets than the mesh holds
```

A count is the number of occurrences. For `DisconnectedCells` it is the pieces
beyond the first. For `UnattributedBoundary` it is the defective facets and faces
the report counts, or 1 for a count mismatch.

The checklist's "non-manifold volume topology" is the three topology refusals:
`NonManifoldFace`, `InconsistentFaceOrientation` and `DisconnectedCells`. **The
finite-volume definition of manifold is face-based**, because fluxes cross faces
and nothing else:

- a face has one owner and at most one neighbour, on its other side;
- the cells form one face-connected set, so one flow has one pressure datum
  (ADR-058).

The independent review found the orientation refusal missing from the first draft.
Two positive cells sharing a face and lying on the same side of it passed every
other check.

Two cells that touch only at a vertex or along an edge, or not at all, are two flows
and are refused. A vertex or edge pinch that leaves the cells face-connected is
**not** refused, because no finite-volume quantity crosses it. That is recorded
here as a decision, not an oversight. Edge-only adjacency inside a face-connected
mesh is shown accepted: a three-cell fan around one edge whose end cells share only
that edge. A true boundary pinch, where the boundary surface is non-manifold at a
vertex, needs a larger ring of cells and is not shown by a fixture. The gate has no
vertex-link check for it to exercise.

The checklist's "unclosed fluid domain" is `UnclosedDomain`. Every face with one
owner must be covered by a boundary triangle, and therefore by a patch and a name.
Otherwise a flux would leave through a face no boundary condition can reach.

**Not refused here:**

- a positive-volume sliver (a quality question, Decision 6);
- an unnamed patch (P19-BC-001);
- anything the input boundary already decided (ADR-060).

## Decision 6 — The quality metrics, defined here, computed by GEOM-001, judged by VALID-001

```text
d                 C_N - C_P (interior), C_f - C_P (boundary)
NON-ORTHOGONALITY theta_f = arccos( S_f . d / (|S_f| |d|) ), in degrees; interior and
                  boundary faces
SKEWNESS          INTERIOR faces only: |C_f - x_f| / |d|, where x_f = C_P + lambda d is
                  the point where the line C_P -> C_N crosses the face plane:
                  lambda = ((C_f - C_P) . S_f) / (d . S_f). (With the boundary d it is
                  identically zero, so it is not defined there.)
ORTHOGONAL DIST.  d . n_f
VOLUME RATIO      max(V_P, V_N) / min(V_P, V_N) across an interior face
```

**One bound is a theorem of the contract, and is asserted.** Take an interior face of
a mesh that holds a contract.

1. The owner and the neighbour are positive, and they traverse the face in opposite
   directions (`InconsistentFaceOrientation` refuses otherwise).
2. Each winds the face outward of itself, so their outward normals are `S_f` and
   `-S_f`.
3. A positive cell's opposite vertex lies on the inner side of its outward normal.
   So the owner's vertex is on the `-S_f` side and the neighbour's is on the `+S_f`
   side.
4. A centroid lies a quarter of the way from the face toward the cell's opposite
   vertex.

So `(C_P - C_f) . S_f < 0 < (C_N - C_f) . S_f`. Therefore `S_f . d > 0` and
`theta_f < 90` degrees, and `lambda` is well defined. Without step 1 this is false:
the folded pair in `CfdMesh_RefusesTwoCellsFoldedOntoOneSideOfTheirFace` has
`S_f . d < 0`.

**Measured on every P16 reference mesh** (debug-ext; the test prints it):

```text
model                   interior faces   theta max   theta mean   skewness max
RM-MESH-01 Block                   6        64.09        47.56         0.5376
RM-MESH-02 Cylinder              946        89.28        57.89         4.6077
RM-MESH-03 PlateWithHole         172        84.07        56.70         6.5099
RM-MESH-04 Tube                 1472        85.20        55.27         3.7138
RM-MESH-05 ThinPlate              18        88.81        66.48         0.8206
RM-MESH-06 TransformedBase        18        66.00        47.04         0.8206
RM-MESH-06 TransformedPlaced      18        66.00        47.04         0.8206
RM-MESH-07 LocalRefinement       528        75.58        31.14         1.3109
```

**Finding F1 (for P19-VALID-001 and the owner): by finite-volume standards, P16's
reference meshes are poor.** They were built to qualify a finite-element mesher, at
deliberately coarse sizes. Mean non-orthogonality of 31–66 degrees and maxima near
90 mean three things. A non-orthogonal correction is mandatory in the
discretisation, not an option. VALID-001 may refuse some of these meshes at the
thresholds it sets. And a CFD benchmark will need a mesh made finer through P16's
own sizing controls. If finer sizing is not enough, the remedy is a
finite-volume-quality option **inside P16**, decided by the owner. It is **never** a
CFD-side remesh or a second mesh. No threshold is set in this milestone, and none
is implied by these numbers. TransformedBase and TransformedPlaced give the same
figures, as rigid-motion invariance requires.

## Decision 7 — The shared mesh seam ADR-060 proposed is not extracted

ADR-060 proposed extracting the shared "control, geometry, currency, one held
lookup" seam from three boundaries (structural, thermal, CFD) into `meshing`, in
this milestone. **It is not extracted.** The extraction would modify P16, P17 and
P18, all qualified. This milestone changes no part of the input boundary, so the
extraction would be the only reason to touch them. The risk it removes (three
copies drifting) is already guarded: ADR-060's walk test requires all three
boundaries to name the same reason at every mesh and geometry state. **Recorded as
owner decision D2:** extract in a dedicated, qualified refactoring milestone, or
keep the three copies under the walk test.

The face table is also copied: two file-local copies in P16 and the public one in
`cfd`. Making P16's public would also modify P16. The CFD copy is pinned against
P16's output (`tetrahedralBoundary` of one tetrahedron) face for face and node for
node, so a drift fails a test.

## Errata

- **ADR-058, "The mesh"** says a P16 mesh carries "boundary triangles with their
  owning tetrahedron". A `Triangle` holds three nodes and a region, and **no owner**.
  The owning tetrahedron is derived (P16's `owningTetrahedraOf`, or this gate's
  census). ADR-058's own next clause says as much. The sentence is corrected here
  and not edited in place.
- **ADR-032** says "a multi-solid body meshes as one mesh with one region per
  solid". P16 as implemented **refuses** a body with more than one solid
  (`VolumeMeshFailure::MultipleSolids`, checked in `generateVolumeMesh`), and its
  comment calls one region per solid "the eventual design". So in v1 a CFD fluid
  domain is exactly one solid. A multi-region fluid domain is not reachable. If it
  became reachable as ADR-032 describes it (regions share no node), this gate would
  refuse it as `DisconnectedCells`. That is the right answer until a later ADR
  defines how regions of one fluid are joined.

## Consequences

- P19-TOPO-001 builds `CellIndex`/`FaceIndex`/`BoundaryFaceIndex` from a mesh that
  holds a `CfdMeshContract`, with the owner rule and orientation above. Its own
  refusals (duplicate interior face, missing neighbour, a boundary face with two
  owners, an interior face with one owner) are consequences of this contract.
  TOPO-001 decides whether to re-assert them. If it does, it must use these
  definitions.
- P19-GEOM-001 computes Decision 6's quantities and must agree with the
  independent computation in `CfdMesh_TheReferenceMeshQualityIsMeasuredNotJudged`.
- P19-BC-001 resolves names through P16's map and must refuse an unnamed patch,
  because ADR-058 requires a condition on every facet.
- P19-VALID-001 sets the thresholds and must face finding F1.

## Verification

`tests/cfd/CfdMeshTests.cpp` (`[cfd][mesh]`, 20 cases).

- Every P16 reference mesh satisfies the contract, with a census that agrees with
  P16's independent `faceIncidence`.
- A drilled body's unnamed wall is counted as a patch, not refused.
- Each of the nine refusals is shown on a mesh built to fail it. That includes the
  vertex, edge, no-contact and three-piece cases of `DisconnectedCells`, an
  accepted edge-only fan, the fold, and the ownership cases. The triangle on an
  interior face, the triangle on a three-way face and the duplicate triangle are
  each wound inward, so a gate that did not stop at the ownership refusal would
  report a second problem. (A triangle on no face has no cell to be wound into.)
- Also covered: seven findings at once in order, with counts; the refusal's exact
  text; binding by stamp across a remesh; the unforgeable proof (compile-time);
  determinism across two builds of the same mesh; the face table against P16's;
  and the quality measurement.

Evidence is in `docs/verification/P19-MESH-001/`.
