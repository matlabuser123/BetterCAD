# P19-DATA-001 - canonical and derived

The checklist's two lists, item by item, with where each one lives and what stops
it from crossing the line.

## Canonical intent (document state; undone, redone, revisioned; persisted by P19-PERSIST-001)

| item (TODO) | type | where |
|---|---|---|
| analysis definition | `CfdAnalysisDefinition`, held by the document object `CfdAnalysis` ("cfd-analysis") | `CfdAnalysisObject.hpp` |
| fluid reference | **the document's P15 material assignment**, not a field (ADR-060, ADR-061 §1) | P15; recorded in the result's source as `fluid` + `fluidRevision` |
| boundary-condition definitions | `CfdBoundaryCondition`: `CfdBoundaryConditionId` + one of `VelocityInlet`, `PressureInlet`, `PressureOutlet`, `NoSlipWall`, `MovingWall`, `SlipWall` | `CfdData.hpp` |
| initial conditions | `CfdInitialCondition` (uniform velocity and pressure), in both modes, no identity (ADR-061 §3) | `CfdData.hpp` |
| gravity setting | `std::optional<CfdGravity>` (a vector; absence = none) | `CfdData.hpp` |
| pressure datum | `std::optional<CfdReferencePressure>` (value at a point), required iff nothing fixes p | `CfdData.hpp`, `pressureDatumProblem` |
| steady / transient settings | `CfdSteadySettings` xor `CfdTransientSettings`, by mode | `CfdData.hpp` |
| solver settings | `CfdIterationSettings`, `CfdConvergenceSettings`, `CfdLinearSolverSettings` | `CfdData.hpp` |
| output settings | `CfdOutputSettings` (residual history); time sampling in the transient settings | `CfdData.hpp` |
| GeometryReferences | `FaceName` in every boundary condition, nothing mesh-local | core `References.hpp` |

**How the list is held closed.** `CfdData_TheDefinitionHoldsOnlyIntent` pins the
definition twice:

- **by size**, against a mirror of its twelve members in order;
- **by count**, with a structured binding that names exactly twelve members.

The count is needed because a small member can hide in the seven padding bytes
after `mode`, where a size check cannot see it (review finding 4). Either check
fails to compile until it is extended, and extending it is where a new field is
declared canonical.

`CfdData_TheFingerprintCoversEveryField` pins every record the fingerprint walks by size,
and by member count wherever the members are public (a structured binding):

- every boundary-condition payload (a `FaceName` and typed values);
- the `CfdBoundaryCondition` class itself (an id and the variant, nothing else, so
  no cache of resolved facets can be added, review finding 3), by size only because
  its members are private; it has no interior gap, and GCC does not reuse a
  member's tail padding, so any added member grows it;
- the initial condition, the reference pressure, gravity and every settings record
  (the steady, linear-solver and output settings, one member each with no padding,
  by size only);
- core's `FaceName`, `FaceSelector` and `FaceCopy`.

## Must NOT become canonical

| item (TODO) | status | what stops it |
|---|---|---|
| cells, faces, cell centres, face normals | not defined in this milestone (P19-TOPO-001, P19-GEOM-001) | the size and count pins; `THE_DEFINITION_HAS_NO_CELLS` |
| DOF maps | not defined (P19-DOF-001) | the size and count pins |
| pressure arrays | not defined; never a definition member | `THE_DEFINITION_HAS_NO_PRESSURE_FIELD`; the size and count pins |
| velocity arrays | likewise | `THE_DEFINITION_HAS_NO_VELOCITY_FIELD`; the size and count pins, including the initial condition's two members |
| mass fluxes | likewise | `THE_DEFINITION_HAS_NO_MASS_FLUX` |
| gradients, residual vectors, matrices, factorisations | not defined | the size and count pins |
| forces, pressure-drop results, flow-split results | not defined (P19-FORCES-001) | the size and count pins; the output settings select no field and name no result |
| visualisation buffers | not defined | `cfd` cannot include `renderer` (layering) |
| **results** | `CfdResult`, a separate header the definition's header does not include | `THE_DEFINITION_HAS_NO_RESULT`; the size and count pins |
| **the resolved fluid** | `CfdFluidProperties`, a derived value type | `THE_DEFINITION_HAS_NO_FLUID`; the size and count pins |

The derived values this milestone does define are **values with units, not
state**: `Velocity3`, `Acceleration3`, `CfdFluidProperties` (nu derived, never
stored), `CfdResiduals`, `CfdMassBalance`, `CfdResultMetadata`. None of them is a
member of anything canonical.

## Command history and the file

The history stores commands, never document snapshots: an added analysis comes back
on redo with the same id and an equal definition, and its fingerprint is equal
(`CfdData_AddUndoRedoRestoresTheAnalysisExactly`). Nothing in the definition can
hold a field, so no undo payload can. The file format is P19-PERSIST-001's. Until
it exists, saving a document with a CFD analysis is **refused by name**
(`"objects of type 'cfd-analysis' cannot be saved"`), not silently dropped
(`CfdData_SavingADocumentWithACfdAnalysisIsRefusedByName`), the same honest
interim P18-DATA-001 pinned.
