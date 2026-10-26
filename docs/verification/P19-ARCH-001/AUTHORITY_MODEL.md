# P19-ARCH-001 - the authority model

One authority per fact. Everything that can be recomputed from an authority is derived and never persisted.

| fact | sole owner | derived views (never authority) | how it is enforced |
|---|---|---|---|
| the fluid region's shape | **CAD geometry** (P15 features, `Regenerator`) | the P16 mesh, the finite-volume topology, every face area and cell volume | `requireCfdModel` asks P16's `geometryIneligibility`; no CFD-side geometry is ever built from a mesh (ADR-058) |
| the fluid's properties | the **P15 material assignment** of the document (one per document in v1; the body the control names is its flow region) | the density and the material id held by value in `CfdModel`; later the viscosity | density through `features::requireDensity`; viscosity through a P15 property that P19-FLUID-001 adds; no CFD fluid table, no default (ADR-028, ADR-060) |
| meshing intent | P16 `MeshControl` | the mesh, its stamp, its quality, the geometry/mesh map | `Mesher::currency` against the document as it is now |
| the CFD analysis definition: mode, boundary conditions, initial conditions, gravity, steady/transient/convergence/linear-solver/output settings | the **CFD analysis object** (P19-DATA-001) | everything below | holds `GeometryReference`s and typed physical quantities only |
| cell, face and boundary-face numbering, owner/neighbour topology, face geometry | derived from the P16 mesh, keyed to its stamp (P19-TOPO-001, P19-GEOM-001) | `CellIndex`, `FaceIndex`, `BoundaryFaceIndex` | compact, distinct strong types; none converts to `ElementId` or `NodeId`; no array sized by a maximum id |
| matrices, sources, factorisations | none: computed on every solve | all of them | never persisted, never in history |
| pressure, velocity, mass flux, gradients, turbulence fields | none: computed | all of them | keyed to the source they were computed from |
| residual histories, mass imbalance, forces, pressure drop, flow split | none: computed | all of them | recovered from the fields in one place (P19-POST-001, P19-INTEGRALS-001, P19-MASSBAL-001) |
| acceptance of an analysis | **`validateCfdAnalysis`** (P19-VALID-001), one function | the CLI exit status, the GUI badge | the ownership gate P19-VALID-001 adds, following ADR-053 |
| what the viewer draws | derived, read-only | render buffers | ADR-054's rules, applied by P19-VIZ-001 |

## What may never become canonical

Cells, faces, cell centres, face normals, DOF maps, pressure or velocity arrays, mass fluxes, gradients, residual
vectors, matrices, factorisations, forces, pressure-drop results, flow-split results and visualisation buffers. A
boundary condition names a CAD face and never a boundary face; an unresolved reference is a refusal and is never
re-bound to a nearby face.

## What this milestone's boundary can and cannot say

`CfdModel` proves: the control exists; the body is eligible; a mesh is held and current; the map and quality report
came from that mesh in one lookup; the material resolves with a usable **density**. It does **not** prove a viscosity,
a mesh fit for finite volumes, resolved boundary conditions, a well-posed system, a converged solve or conserved
mass; those are later gates in a staged validation entry. This is stated in the header, in ADR-060 and in a test that
is designed to fail the day a CFD consumer kind appears.

## The one new gap this architecture exposes

**P15 has no viscosity and no CFD consumer kind** (`Completeness.hpp` says so in a comment). The decision is that the
fluid stays a P15 material and P15 grows the property, in its own vocabulary, in P19-FLUID-001. The alternative - a
CFD-only fluid record - is the second material database ADR-028 forbids.
