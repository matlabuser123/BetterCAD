# P19-MESH-001 — Audit of P16 for finite-volume use

The question: **can P16's Tet4 mesh directly serve as the P19 finite-volume mesh?**
This file records what was read in P16 and what each finding decided. Every claim
names the code it rests on. The audit changed no P16 file; the freeze shows
`include/bettercad/meshing` and `src/meshing` identical to HEAD.

## What a finite-volume method needs, and where P16 stands

| FV requirement | P16 | Where | Decision |
|---|---|---|---|
| Cells with a positive volume | Every tetrahedron is checked: `validate()` reports `InvertedTetrahedron` / `DegenerateTetrahedron`, and `generateVolumeMesh` fails with `VolumeMeshFailure::InvalidMesh` | `src/meshing/MeshValidation.cpp:184-195`, `src/meshing/VolumeMesh.cpp:414-419` | Relied on, and **re-checked** by the gate (`NonPositiveCellVolume`), because `MeshBuilder` accepts any hand-built mesh |
| Finite coordinates in SI | `Point3D` of `Length` (SI metres); `MeshBuilder::addNode` refuses a non-finite coordinate | `src/meshing/Mesh.cpp:162-164` | Relied on; a finite but overflowing volume is still refused |
| Deterministic order | Nodes in ascending `NodeId`, triangles and tetrahedra in ascending `ElementId` ("ORDERING IS DEFINED AND DETERMINISTIC") | `include/bettercad/meshing/Mesh.hpp:157-158` | The owner rule (first claimant in cell order) and the census rest on it |
| One identity per generation | `MeshStamp { MeshId, generation }`; `Mesh::owns(stamp)` | `include/bettercad/meshing/Mesh.hpp` | The contract binds to a stamp, never to counts |
| Boundary faces wound outward | `tetrahedralBoundary` emits each once-used face in the winding of its table, outward of a positive tet | `src/meshing/VolumeMesh.cpp:146-173` | **Re-checked geometrically** by the gate (`InwardBoundaryFace`). `BoundaryNotConforming` compares positions and does not establish winding |
| Interior faces, owner, neighbour | **Not held anywhere** | — | Derived by the gate's face census; built compactly by P19-TOPO-001 |
| A boundary triangle's owning cell | **Not held**: `Triangle` has nodes and a region only. Derivable via `owningTetrahedraOf` | `include/bettercad/meshing/GeometryMeshMap.hpp:391` | Derived by the census; ADR-058's sentence corrected (ADR-063 errata) |
| A face shared by at most two cells | **Not checked**: `tetrahedralBoundary` counts uses and emits only faces used once, so a face used three times is silently dropped | `src/meshing/VolumeMesh.cpp:167-171` | Gate refusal `NonManifoldFace` |
| A shared face traversed both ways | **Not checked combinatorially** | — | Gate refusal `InconsistentFaceOrientation` (added after the independent review) |
| One face-connected set of cells | **Not checked** | — | Gate refusal `DisconnectedCells` |
| Each boundary facet attributed to exactly one CAD face | **Reported, not enforced**: `GeometryMeshMappingReport::complete()` exists; nothing refuses an incomplete map | `include/bettercad/meshing/GeometryMeshMap.hpp:185-215` | Gate refusal `UnattributedBoundary`; **no nearest-face fallback** |
| Names for boundary faces | `MappedFace::names` (`FaceName`s), possibly empty: a drilled hole's wall has none | `GeometryMeshMap.hpp:151-182`; `MapBored_DrilledHoleWallIsMappedInReverseAndCarriesNoName` | Counted as `unnamedPatches`; P19-BC-001 must refuse (ADR-058: every facet one condition) |
| One solid | `generateVolumeMesh` refuses `geometry.solidCount != 1` (`MultipleSolids`) | `src/meshing/VolumeMesh.cpp:182-189` | A fluid domain is one solid; ADR-032's "one region per solid" corrected (ADR-063 errata) |
| FV quality metrics | **None**: P16's quality report is finite-element (aspect ratio, dihedral angles) | `include/bettercad/meshing/MeshQuality.hpp` | Defined in ADR-063 §6, measured here, computed by P19-GEOM-001, judged by P19-VALID-001 |

## The face table

The local face table appears **three times**:

1. `src/meshing/VolumeMesh.cpp`, file-local, used by `tetrahedralBoundary`;
2. `src/meshing/GeometryMeshMap.cpp:20-25`, file-local, which says it is "copied rather than shared" and pinned by a P16 test;
3. `include/bettercad/cfd/CfdMesh.hpp`, public, with the opposite-vertex table.

All three read `{0,2,1}, {0,1,3}, {1,2,3}, {0,3,2}`. The opposite vertices are
`{3, 2, 0, 1}`. They are **not** `3 - k`; the first draft of the gate assumed that
formula, and every P16 mesh failed until it was corrected. P16's copies were not
made public, because that would modify P16. `CfdMesh_TheFaceTableIsTheOneP16WindsItsBoundaryWith`
pins CFD's copy against P16's own output.

## Answer

**Yes, directly, with no adapter.** A cell is a P16 tetrahedron, a boundary face is a
P16 boundary triangle, and a patch is a CAD face from P16's map. What P16 does not
guarantee is proved by a staged gate (`requireCfdMesh`), which all eight meshable
P16 reference bodies satisfy, and a drilled body too.

Measured quality is a separate matter. It is poor by finite-volume standards:
finding F1 in ADR-063 §6 and in the README.

## What was not changed

No file under `include/bettercad/meshing`, `src/meshing`, `include/bettercad/structural`,
`include/bettercad/thermal` or their sources. The shared mesh-seam extraction ADR-060
proposed was **not** done (ADR-063 §7, owner decision D2).
