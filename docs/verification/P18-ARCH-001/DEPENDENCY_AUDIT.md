# P18-ARCH-001 — what P18 consumes, and what it was therefore not allowed to build

```text
SUBJECT:  the public P15, P16 and P17 interfaces a thermal analysis is
          permitted to use, and the duplication that was avoided by auditing
          before writing
METHOD:   read the public headers in the committed tree at a3b721b; every
          name below was found in the code, not inferred from the phase plan
```

## The headline

**The audit changed the milestone, as P17's did.** P15 built this phase's
material boundary *for this consumer, by name*, and P17 built the device that
closes P16's caller gap and then had it verified through sixteen milestones:

```text
features::requireThermalConductivity        "What steady-state conduction needs
                                            from a material: its thermal
                                            conductivity ... never returns a
                                            default conductivity"
features::requireTransientConductionProperties
                                            "What transient conduction needs:
                                            density, specific heat capacity and
                                            thermal conductivity, all concrete
                                            ... EVERY missing input" at once
ConsumerKind::ThermalSteady,                P15's completeness requirements,
  ThermalTransient, ThermoMechanical        already split by consumer
ADR-027's law contract                      PropertyLawKind, OutOfRangeBehaviour,
                                            temperatureTableInvariants(): the
                                            shape a temperature-dependent k
                                            will take, fixed before P18 exists
ADR-036 / StructuralModel                   possession is the proof -- the
                                            device, with its ordering argument
                                            and its enum-honesty rule
```

So the material resolution this milestone was asked to architect is two
function calls, their validation is inside them, and the input-boundary
device is already designed, implemented, qualified and audited. What was left
to build is the thermal instance of that device, in a module placed where the
roadmap's coupling can reach it.

## P15 — material consumption

| P18 needs | The qualified API | Layer |
| --- | --- | --- |
| The document's material | `features::requireEffectiveMaterial(document)` | `features` 20 |
| `k` | `features::requireThermalConductivity(document, id)` | `features` 20 |
| `k`, `rho`, `cp` for a transient | `features::requireTransientConductionProperties(document, id)` | `features` 20 |
| the density alone | `features::requireDensity(document, id)` -- the ONE density, which lives with the mechanical properties | `features` 20 |
| the quantities themselves | `ThermalConductivity`, `SpecificHeatCapacity`, `Density`, `Temperature`, `Power`, `Energy`, `Time` | `core` 0 |
| the literals | `_W_per_m_K`, `_J_per_kg_K`, `_kJ_per_kg_K`, `_kg_per_m3`, `_K`, `_W` | `core` 0 |
| what a consumer requires | `materials::ConsumerKind::ThermalSteady`, `ThermalTransient` | `core` 0 |

### The validation is already there, measured

Read out of `src/features/material/Materials.cpp` and
`src/core/materials/ThermalProperties.cpp`:

```text
no thermal conductivity                 -> refused, "has no thermal conductivity,
                                           which conduction needs"
k not finite, or k <= 0                 -> refused, "not a usable value"
cp not finite, or cp <= 0               -> refused at storage (checkPositive)
no density / no cp for a transient      -> refused, every gap in one diagnostic
a DERIVED thermal property              -> refused at storage (checkNotDerived)
a reference temperature below 0 K       -> refused at storage
```

So `P18-MAT-001`'s "require k > 0", "require cp > 0", "reject non-finite
values" and "reject missing density / specific heat for transient" boxes are
satisfied by consumption, exactly as P17-MAT-001's were. It will have work to
do -- the per-mode question, and the stale-on-edit proof -- but not this.

### What P15 does NOT have, and P18 will add in core

```text
heat flux             W/m^2       not a Quantity alias today
heat-transfer coeff.  W/(m^2 K)   not a Quantity alias today
volumetric heating    W/m^3       not a Quantity alias today
temperature gradient  K/m         not a Quantity alias today
```

`Units.hpp` has `Power`, `Energy`, `Temperature`, `ThermalConductivity` and
`SpecificHeatCapacity`; the four derived quantities above are
`P18-DATA-001`'s and `P18-LOAD-001`'s to add as dimensioned aliases, which
is a `core` change that must be made the way P15 made its own -- with the
dimension exponents stated and a compile-fail case per confusion. This
milestone adds none, because it has no load or flux to carry.

### The duplication that was avoided

ADR-028 forbids it and names this phase: *a downstream solver must not
maintain a second authoritative material database.* So:

```text
src/thermal/     contains no conductivity, no default, no material table
ThermalModel     holds ThermalConductivity BY VALUE -- one number that is a
                 derived view of P15's canonical data, obtained through the one
                 contract, and recomputed on every prepare
```

A `struct ThermalMaterial { double k; double rho; double cp; };` would have
been the forbidden thing. It does not exist.

## P16 — mesh, mapping and quality consumption

The same table as P17-ARCH-001's, consumed through the same functions, for
the same reasons -- `geometryIneligibility`, `geometryRevision`,
`Mesher::currency` + `describesTheModel`, `Mesher::mesh/map/quality` from one
lookup, mesh validity NOT re-derived, `findMeshControl` for the body. Nothing
reaches a backend: rule 5 and `architecture.checker.thermal-backend-leak`.

## P17 — what is reused, what is reachable, and what is not

```text
REUSED BY DESIGN    the possession-is-the-proof device (ADR-036), the check
                    order (geometry first), the enum-honesty rule (no
                    InputProblem nothing can return), the staged-validation
                    shape (ADR-042) adopted from the start rather than
                    forecast and amended
NOT REACHABLE       anything in include/bettercad/structural/: thermal is at
                    layer 45 and structural at 50 (ADR-050). In particular
                    structural::solveSymmetricSparse, SolveProblem,
                    ResidualMetrics, StiffnessMatrix, MeshDofMap,
                    ConstraintSet, FreeEquationMap, Tet4Element's kinematics
WHY NOT 55          the roadmap's coupling is structural READING a thermal
                    result; strictly-lower puts the reader above the read
WHAT THAT COSTS     P18-SOLVE-001 reuses P17's sparse solve by EXTRACTION to a
                    module at or below 45, or by an independent private Eigen
                    link; a hand copy is refused (ADR-050)
WHAT ELSE IS SHAPED THE SAME
                    P18-DOF-001 will want P17's MeshDofMap shape with one DOF
                    per node; P18-ELEM-001 will want Tet4Element's shape
                    gradients and signed-volume refusal. Each is a candidate
                    for the same extraction, and each milestone decides with
                    its audit
```

## Layer consistency of the above

Every consumed interface is strictly below 45:

```text
core        0     Result, Error, ObjectId, MeshControlId, units, literals,
                  ThermalProperties, ConsumerKind
features   20     Regenerator, requireEffectiveMaterial,
                  requireThermalConductivity,
                  requireTransientConductionProperties, requireDensity
meshing    40     Mesher, VolumeMesh, GeometryMeshMap, MeshQualityReport,
                  GeometryPreparation, MeshIds, findMeshControl
```

and `bettercad_thermal` links exactly `BetterCAD::core`, `BetterCAD::features`,
`BetterCAD::meshing`. No `structural`, no `io`, no `renderer`, no Qt, no
Eigen, no backend.

## Searches run before qualification

```text
#include <bettercad/structural/ under src/thermal, include/bettercad/thermal   0
#include <bettercad/thermal/ under src/meshing, src/structural                 0
nglib / netgen / OCCT / Qt headers in src/thermal/                              0
a conductivity, density or specific-heat store in src/thermal/                  0
Eigen in include/bettercad/thermal/                                             0
"thermal" in src/, include/, apps/ before this milestone                        P15's
                                                                                headers and
                                                                                the CLI's
                                                                                material
                                                                                vocabulary
                                                                                only
```

The last line is the measurement that there was no thermal analysis code to
inherit or to conflict with: every prior mention is P15 describing the
material data this phase consumes.
