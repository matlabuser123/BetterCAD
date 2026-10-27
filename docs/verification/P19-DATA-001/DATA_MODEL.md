# P19-DATA-001 - the data model

Each checklist item, the type that answers it, and the rule that type enforces.

## Identities (`include/bettercad/core/Id.hpp`)

| item | decision |
|---|---|
| `CfdAnalysisId` | a document-object identity ("cfd analysis"), widens to `ObjectId`; not an `AnalysisId`, not a `ThermalAnalysisId` |
| `CfdBoundaryConditionId` | a member identity ("cfd boundary condition"), does **not** widen; not a `ThermalBoundaryConditionId`, not a `RestraintId` |
| `CfdInitialConditionId` | **not defined**: one uniform initial condition per analysis, so an identity would distinguish nothing (ADR-061 §3) |

None converts to another, to an integer, to a `NodeId` or to an `ElementId`. That
is asserted by `static_assert` in `CfdData_StrongIdsAreDistinct` and
`_NoMeshHandleCanActAsACfdIdentity`, and by eleven conversion cases plus the
integer case in `compile_fail.cfdids`.

## The definition (`CfdAnalysisObject.hpp`)

```text
mesh                MeshControlId          the control whose current mesh is analysed; its body is the fluid region
mode                CfdAnalysisMode        Steady | Transient (an enum, never a bool)
boundaryConditions  vector, user order     ids unique; order is canonical and fingerprinted
initialCondition    CfdInitialCondition    both modes
referencePressure   optional               required iff no condition fixes p; overdetermined is refused
gravity             optional<CfdGravity>   absence = no body force; a zero vector is refused; carries
                                           the reference height boundary pressures are prescribed at
iteration           relaxation U, p        each in (0, 1]; defaults 0.7, 0.3
convergence         tolerances, divergence tolerances finite > 0; divergence factor > 1
linearSolver        one ratio              finite > 0
steady              optional               REQUIRED for Steady, FORBIDDEN for Transient
transient           optional               REQUIRED for Transient, FORBIDDEN for Steady
output              residual history       FinalOnly | EveryIteration
```

`validate(definition)` checks each record through its own validator, then the
unique ids, the datum and mode-completeness. It does not consult the document.
`CfdAnalysis::create` and `setDefinition` refuse anything it refuses, and a
`setDefinition` with an equal definition returns false and moves no revision.

| checklist item | type | validation |
|---|---|---|
| analysis mode | `CfdAnalysisMode` | the enumerators only; `THE_MODE_IS_NOT_A_BOOL` |
| fluid model | `CfdFluidProperties` {rho, mu}, derived; nu = mu / rho computed | both finite and positive |
| reference pressure policy | `CfdReferencePressure` {value, `Point3D` location}; `pressureDatumProblem` | finite; overdetermined refused by the definition; missing is storable but makes the analysis `InputsUnavailable` (`currentResultSource` refuses it by name) |
| gravity policy | `CfdGravity` {`Acceleration3`, `Length referenceHeight`}: boundary pressures are prescribed at the reference height and vary hydrostatically along a face (ADR-061 §2) | vector finite and not zero; height finite |
| steady settings | `CfdSteadySettings` {maxIterations} | at least 1 |
| transient settings | `CfdTransientSettings` {step, end, per-step budget, sampling} | 0 < step <= end, finite; budget >= 1; known sampling |
| nonlinear-iteration settings | `CfdIterationSettings` {velocity, pressure relaxation} | each in (0, 1], NaN refused first |
| convergence settings | `CfdConvergenceSettings` {momentum, continuity tolerance, divergence factor} | finite > 0; factor > 1 |
| output settings | `CfdOutputSettings` {residual history} (+ transient sampling) | known policy |

## Boundary conditions (`CfdData.hpp`)

| kind | payload | own-terms rule |
|---|---|---|
| `velocity_inlet` | `FaceName`, `Velocity normalSpeed` | finite and **> 0** (into the domain): zero is a wall, negative an outflow |
| `pressure_inlet` | `FaceName`, `Pressure staticPressure` (gauge) | finite, either sign |
| `pressure_outlet` | `FaceName`, `Pressure staticPressure` (gauge) | finite, either sign |
| `no_slip_wall` | `FaceName` | the face selector |
| `moving_wall` | `FaceName`, `Velocity3 velocity` | finite and **not zero**: a wall at rest is a no-slip wall |
| `slip` | `FaceName` | the face selector |

Every kind also requires a valid id, a selector that core's `validate(FaceSelector)`
accepts, a known `FaceRole` and a valid feature id. Core checks neither of the last
two, and an out-of-range role would let two different conditions write the same
canonical text (review finding 5). `fixesPressure()` is true for exactly the two pressure kinds. `kind()`
is the variant's index; `CfdData_TheKindIsTheAlternativeHeld` holds the enum and the
alternatives in the same order. Whether a face resolves, every boundary facet
has one condition, an inflow points inward on every facet, a wall velocity is
tangential, or two conditions claim one facet are all questions about a mesh. They
belong to P19-BC-001, which refuses each by name.

## Units

SI through the units framework: velocity m/s, pressure Pa (gauge), acceleration
m/s^2, the reference location in m (`Point3D` of `Length`), time s, density kg/m^3,
dynamic viscosity Pa s, and kinematic viscosity m^2/s derived only. The only bare
doubles are ratios (relaxation, tolerances, normalised residuals, mass-balance
measures). `Velocity3` and `Acceleration3` are two instances of one template and
two types, so a gravity vector cannot be passed as a wall velocity
(`GRAVITY_IS_NOT_A_VELOCITY`, `A_WALL_VELOCITY_IS_NOT_AN_ACCELERATION`), and a pressure
cannot be typed as an inflow speed (`A_PRESSURE_IS_NOT_A_VELOCITY`,
`A_VELOCITY_IS_NOT_A_PRESSURE`). Component order is frozen x, y, z, a copy of
thermal's because cfd cannot include thermal.

## The result side (`CfdResult.hpp`)

```text
CfdResultSource    body, control, geometry revision, mesh stamp, fluid + revision,
                   analysis + revision, definition fingerprint, mode
CfdResultMetadata  outer iterations (>= 1), final residuals, mass-balance measures,
                   time and step (transient only)
CfdResult          source + metadata; immutable; only CfdResult::create builds one;
                   the fields arrive with P19-DOF-001 (ADR-061 §6)
CfdSolveFailure    stage (9 named), error, optional source, optional iterations
CfdResultSlot      the current accepted result (at most one) + the latest failed
                   attempt (at most one)
```

How currentness, staleness and the state machine work is in
[CURRENTNESS.md](CURRENTNESS.md). The fingerprint is in
[FINGERPRINT.md](FINGERPRINT.md).
