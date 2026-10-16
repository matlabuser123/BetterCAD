# P18-DATA-001 — units

```text
SUBJECT:  every quantity in the thermal data model, its SI unit, and how the
          units framework makes a swapped unit a compile error
RULE:     SI internally through Quantity<Dimension>. Absolute temperature
          only. No field is a bare double except two solver ratios.
```

## What core had, and what this milestone added

P15-UNITS-001 characterised the **material**: `ThermalConductivity` W/(m K),
`SpecificHeatCapacity` J/(kg K), `ThermalExpansionCoefficient` 1/K, with
`Temperature` K, `Energy` J, `Power` W, `Density` kg/m^3 and `Time` s already
present. P18-ARCH-001 recorded that the **field** and **boundary** quantities
did not exist. This milestone adds them, composed in `Dimension.hpp` so the
composition is the proof:

| Quantity | Dimension | Composed as | Unit | Literal | Catalog symbol |
| --- | --- | --- | --- | --- | --- |
| `TemperatureGradient` | Theta L^-1 | `temperature / length` | K/m | `_K_per_m` | `"K/m"` |
| `HeatFlux` | M T^-3 | `power / area` | W/m^2 | `_W_per_m2` | `"W/m^2"` |
| `HeatTransferCoefficient` | M T^-3 Theta^-1 | `heatFlux / temperature` | W/(m^2 K) | `_W_per_m2_K` | `"W/(m^2 K)"` |
| `VolumetricHeatSource` | M L^-1 T^-3 | `power / volume` | W/m^3 | `_W_per_m3` | `"W/m^3"` |

One unit per dimension, SI only: a thermal result is expressed in these and
in K, and no other scale exists for a consumer to pick by mistake. The unit
catalog grows from 54 to 58 entries; `FormatAndCatalogTests` carries the new
count with the reason.

## The relationships, asserted in the type system

`ThermalData_UnitsAreExplicitSi`:

```text
ThermalConductivity * TemperatureGradient        == HeatFlux        q = -k grad(T)
HeatTransferCoefficient * Temperature            == HeatFlux        q = h (T_amb - T)
HeatFlux * Area                                  == Power           a flux over a face
VolumetricHeatSource * Volume                    == Power           generation over a volume
Power * Time                                     == Energy
Density * SpecificHeatCapacity * Volume * Temperature == Energy     rho cp V T
HeatTransferCoefficient != ThermalConductivity                      per m^2 vs per m
HeatFlux != Power;  TemperatureGradient != Temperature
```

Each is a `static_assert` on `decltype`, so the dimensional consistency of
Fourier's law, convection and the balance is checked before P18-ELEM-001
writes a matrix entry.

## Every field, with its unit

| Field | Type | Unit | Validation |
| --- | --- | --- | --- |
| `SurfaceHeatFluxLoad::flux` | `HeatFlux` | W/m^2, **into the body positive** | finite; negative accepted (heat leaving) |
| `VolumetricHeatSourceLoad::generation` | `VolumetricHeatSource` | W/m^3 | finite; negative accepted (a sink) |
| `PrescribedTemperature::value` | `Temperature` | K | finite and >= 0 K |
| `ConvectionBoundary::coefficient` | `HeatTransferCoefficient` | W/(m^2 K) | finite and **> 0** |
| `ConvectionBoundary::ambient` | `Temperature` | K | finite and >= 0 K |
| `UniformInitialTemperature::value` | `Temperature` | K | finite and >= 0 K |
| `TransientSettings::timeStep` | `Time` | s | finite, > 0, <= endTime |
| `TransientSettings::endTime` | `Time` | s | finite, > 0 |
| `ThermalSolverSettings::relativeResidualTolerance` | `double` | ratio | finite, > 0 |
| `ThermalSolverSettings::pivotFloor` | `double` | ratio | finite, > 0 |
| `ThermalResult::temperatures()[i]` | `Temperature` | K | finite and >= 0 K |
| `TemperatureGradient3::{x,y,z}` | `TemperatureGradient` | K/m, order X Y Z frozen | finite |
| `HeatFlux3::{x,y,z}` | `HeatFlux` | W/m^2, **in the direction heat flows** (`q = -k grad T`) | finite; `magnitude()` derived |
| `HeatBalance::*` | `Power` | W, **into the body positive**, every term | finite |
| `HeatBalance::storedEnergyRate` | `std::optional<Power>` | W | present for transient, **absent** for steady -- no magic zero |
| `HeatBalance::normalizedImbalance()` | `std::optional<double>` | ratio | nullopt when every term is zero -- no magic zero |
| `ThermalResult::time()` | `std::optional<Time>` | s | present, finite, >= 0 for transient; absent for steady |

## The absolute-temperature policy

One function, `thermal::isAbsoluteTemperature(Temperature)`: finite and
`>= 0 K`. Every temperature that enters the model -- initial condition,
prescribed value, convection ambient, every nodal temperature in a result --
asks it, so the rule cannot drift between them.

```text
0 K            ACCEPTED   the limit of the scale; mathematically ordinary in
                          linear conduction; refusing it would need a threshold
                          with no physical basis
below 0 K      REFUSED    not a temperature
NaN, +-inf     REFUSED
in a RESULT    REFUSED    a field with a node below 0 K is not a solution of a
                          problem whose every datum was absolute; refusing turns
                          a wrong solve into a reported failure rather than a
                          displayed temperature
```

## Celsius

There is none, and `ThermalData_TemperaturesAreKelvinAndNoCelsiusUnitExists`
proves it as a fact about the whole catalog: exactly one unit has the
temperature dimension and its symbol is `"K"`; `findUnit("degC")`,
`findUnit("C")` and `findUnit("F")` are empty. An offset scale needs an affine
conversion the units framework does not have, and a "degC" that scaled like a
kelvin would read 20 degC as 20 K -- the exact mutation the brief names, and
it cannot be expressed.

## Component order and sign

`VectorComponent::X = 0, Y = 1, Z = 2`, `kVectorComponents` in that order,
and `component(which)` on both triples agrees with the named field for every
component (`ThermalData_VectorComponentOrderIsFrozen`). The order is frozen so
that P18-POST-001's recovery, any report and any renderer index the same
thing; a module that chose its own would swap two components and produce a
flux wrong in direction only, which no magnitude check would catch.

Heat flux **points where heat flows**: `q = -k grad(T)`, down the gradient.
Heat **into the body is positive** for a flux load and for every balance term,
so a load's sign and its appearance in the balance agree, and a steady solve
satisfies `applied + generated + convection + reaction = 0`. A transient one
satisfies it with the stored-energy rate subtracted.
