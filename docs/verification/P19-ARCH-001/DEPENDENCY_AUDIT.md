# P19-ARCH-001 - dependency audit (what P19 can rely on, and what it cannot)

Read before any decision was written. Every row cites the source; nothing is from memory of an ADR.

## P16 meshing - can the Tet4 mesh serve as a finite-volume mesh?

| need | P16 provides | gap |
|---|---|---|
| cells | `VolumeMesh` / `Mesh::tetrahedra()`: Tet4, validated, positively oriented | none; **a cell is a tetrahedron** |
| boundary faces | boundary triangles with the winding facing out of their owning tetrahedron; `tetrahedralBoundary(mesh)` derives them from connectivity alone ("the independent check") | none |
| internal faces / owner-neighbour | not provided: P16 stores no face-to-cell adjacency | **derived by P19-TOPO-001** from the tetrahedra, keyed to the mesh stamp |
| face areas, normals, centroids, cell volumes | `signedVolume` exists; no face metrics | derived by P19-GEOM-001 |
| CAD face -> boundary face | `GeometryMeshMap`, `boundaryFacetsOf(map, FaceName)`, a `BoundaryFacetSet` with a cardinality (`fullyResolved()`), no nearest-face fallback | none; the same mechanism thermal and structural use |
| currency | `Mesher::currency(document, control)`: exact (sizing intent + geometry revision); `geometryIneligibility` | none |
| quality | Tet4 metrics (radius ratio, aspect, dihedral angles) | finite-volume metrics (non-orthogonality, skewness) are not P16's; P19-MESH-001 measures them |

**Answer to the critical question:** yes, in principle, through a derived topology and with no second mesh identity.
Whether tetrahedral cells give a *qualified* result is an empirical question for P19-MESH-001 and P19-VALID-001, and
"this mesh is refused" is an allowed answer.

## P15 materials - what is a fluid?

| need | P15 provides | gap |
|---|---|---|
| density | `MechanicalPropertyKind::Density`; `features::requireDensity(document, id)` (missing, non-finite or non-positive refused, material named) | none |
| dynamic viscosity | the **unit types** exist (`DynamicViscosity`, `KinematicViscosity`, `Pa_s`, `_Pa_s`); **no material property carries one** | **P19-FLUID-001 adds the property** |
| a CFD consumer | `ConsumerKind` has no CFD value; its comment says P19 is "deliberately ABSENT" and "a CFD consumer will also need a dynamic-viscosity property, which no material carries yet" | P19-FLUID-001 adds `{ Density, DynamicViscosity }` |
| a "fluid" marker | none | a fluid is a material that carries a viscosity; no CFD-side flag |
| assignment | one document-wide material assignment (`requireEffectiveMaterial`) | a CFD analysis therefore has one fluid per document in v1 |

## Units

`core/units` has `Density`, `Pressure`, `Velocity`, `Force`, `Mass`, `DynamicViscosity` and `KinematicViscosity`. A mass
flux (kg/s) and a volumetric flow (m^3/s) may need dimensions of their own; checked at P19-DATA-001.

## Solvers

`BetterCAD::linalg` (layer 5) holds the generic symmetric sparse solve, shared by structural and thermal. **A finite-volume
momentum matrix is not symmetric** (convection), so the existing symmetric kernel does not serve it as it stands. That is a
real finding for P19-LINSOLVE-001: the pressure-correction system is symmetric and can reuse it, the momentum system needs
a non-symmetric kernel, and it belongs in `linalg` (the generic layer), not in `cfd`. Recorded here so it is not
discovered at the first benchmark.

## Gaps this milestone found, and where each goes

| gap | goes to |
|---|---|
| no viscosity, no CFD consumer kind | P19-FLUID-001 |
| no internal-face topology | P19-TOPO-001 |
| no face geometry | P19-GEOM-001 |
| tetrahedral FV quality unknown | P19-MESH-001, P19-VALID-001 |
| the generic solver is symmetric-only | P19-LINSOLVE-001 |
| `final-gate` question of the two currentness regimes (P18-QUAL-001 D1) | applies to P19 the day the owner decides it |
