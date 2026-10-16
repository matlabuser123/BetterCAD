# P18-MAT-001 — what each mode requires

```text
STEADY      K T = Q                 needs k      only
TRANSIENT   C dT/dt + K T = Q       needs k, rho, cp   because C is built from rho cp
```

## The independent argument

Not taken from production code. The conductivity matrix is

```text
Ke = integral  grad(N)^T  k  grad(N)  dV          [W/(m K)] [1/m] [1/m] [m^3] = W/K
```

and depends on `k` alone. The capacity matrix is

```text
Ce = integral  rho cp  N^T N  dV                  [kg/m^3] [J/(kg K)] [m^3] = J/K
```

and depends on the PRODUCT `rho cp`, so it needs both: a density alone has
dimension kg/m³ and a specific heat alone J/(kg K), and neither is an energy
per kelvin. The product's dimension, J/(m³ K), is asserted in the type
system (`ThermalMaterial_VolumetricHeatCapacityIsDerivedNotStored`):

```text
Density * SpecificHeatCapacity   ==   Energy / (Volume * Temperature)
```

A steady field has no capacity matrix, so requiring `rho` or `cp` for it
would block a solvable problem -- the over-requirement P15's own table comment
warns against. Volumetric heat generation, W/m³, needs no density either: it
is heat per volume, not per mass.

## Where the policy lives

ONE place: `materials::requiredProperties(consumerFor(mode))`, P15's table,
read by the resolver:

```cpp
const PropertyRequirement required = requiredProperties(consumerFor(mode));
const bool needsDensity      = find(required.mechanical, Density)             != end;
const bool needsSpecificHeat = find(required.thermal, SpecificHeatCapacity)   != end;
```

`ThermalMaterial_TheModeChoosesP15sConsumerAndNothingElseDecidesIt` asserts
the exact vectors. A GUI, a CLI or P18-VALID-001 that wants the policy asks
`thermalMaterialCompleteness(document, mode)` or `thermalCapability`, and
never writes `if (transient) ...` of its own.

## The mode matrix, measured

| Property state | Steady | Transient | Test |
| --- | --- | --- | --- |
| k only | **PASS** | FAIL (density, specific heat) | `SteadyNeedsNeitherDensityNorSpecificHeat`, `TransientNamesEveryMissingPropertyDeterministically`, `CapabilityMatrix` |
| k + rho | **PASS** | FAIL (specific heat) | `SpecificHeatIsRequiredOnlyByTransient`, `CapabilityMatrix` |
| k + cp | **PASS** | FAIL (density) | `DensityIsRequiredOnlyByTransient`, `CapabilityMatrix` |
| k + rho + cp | **PASS** | **PASS** | `ResolveTransientProperties`, `CapabilityMatrix` |
| rho + cp, no k | FAIL (thermal conductivity) | FAIL (thermal conductivity) | `MissingConductivityFailsBothModes`, `CapabilityMatrix` |
| nothing | FAIL | FAIL | `CapabilityMatrix`, `ReachesEveryMaterialProblemItDeclares` |
| k = 0, k < 0, NaN, ±inf (with the rest valid) | unrepresentable | unrepresentable | `P15RefusesAnUnusableValueBeforeItCanReachTheDocument` |

"FAIL (x)" names what the diagnostic says; a steady failure never mentions a
density or a specific heat, and a transient failure for a k + cp material
never mentions the specific heat (asserted with negative substring matches).

## The capability query

```cpp
struct ThermalCapability { bool steady; bool transient; };
Result<ThermalCapability> thermalCapability(const Document&, MaterialId);
```

Derived from P15's `materialCompleteness` for the two thermal consumers at
the moment of the call; nothing stores either flag. `CapabilityMatrix`
asserts that the capability and the resolver agree on every row, which is
what makes the former safe to show a user before the latter is called.

## The integration with the state model

`currentResultSource` (P18-DATA-001) now calls `resolveThermalMaterial` for
the analysis's mode. Measured
(`ATransientAnalysisOnAConductivityOnlyMaterialIsNotReady`):

```text
k-only material, steady analysis       Ready
k-only material, transient analysis    InputsUnavailable -- requireThermalModel
                                       accepts it (ADR-051), the resolver refuses
                                       it, the source cannot be computed
+ rho and cp through P15's setters     Ready, same mesh stamp, mesh Current
```
