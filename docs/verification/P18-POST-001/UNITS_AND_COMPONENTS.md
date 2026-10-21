# P18-POST-001 — units and component order

```text
SUBJECT:  the units of each recovered quantity, and the order of the components, as the compiler and the tests fix them
```

## Units

| Quantity | Type | Dimension (checked) | SI unit | Where it is fixed |
| --- | --- | --- | --- | --- |
| nodal temperature | `Temperature` | temperature | K | the solver's own type, copied |
| temperature gradient | `TemperatureGradient3` of `TemperatureGradient` | temperature / length | K/m | `static_assert(TemperatureGradient::dimension == temperature / length)` |
| thermal conductivity (input) | `ThermalConductivity` | power / (length · temperature) | W/(m K) | P18-DATA / P18-MAT |
| heat flux | `HeatFlux3` of `HeatFlux` | power / area | W/m² | `static_assert(HeatFlux::dimension == power / area)` |
| heat-flux magnitude | `HeatFlux` | power / area | W/m² | same type as a component |
| time of a transient state | `Time` | time | s | the solution's own |

`static_assert(std::is_same_v<decltype(ThermalConductivity{} * TemperatureGradient{}), HeatFlux>)` states that Fourier's law is an equation between heat
fluxes: the product is the `HeatFlux` type itself, not a convertible one. Everything is SI; there is no unit conversion anywhere in the recovery. A
geometry in millimetres reaches the module already in metres (P15 / P16 own that conversion), so a gradient is per metre. PM10b (a stray `1e-3`
on the gradient, "per millimetre") is killed by 19 cases.

None of the three derived types converts to another. The compile-fail cases `A_GRADIENT_IS_NOT_A_HEAT_FLUX`, `A_TEMPERATURE_IS_NOT_A_HEAT_FLUX`,
`A_HEAT_FLUX_IS_NOT_A_TEMPERATURE_GRADIENT`, `A_MAGNITUDE_IS_NOT_A_TEMPERATURE`, `A_BARE_DOUBLE_IS_NOT_A_CONDUCTIVITY` and
`A_BARE_DOUBLE_IS_NOT_A_TEMPERATURE` show each as a compile error; `A_NODE_ID_IS_NOT_AN_ELEMENT_ID` does the same for the two keys.

## Component order

```text
gradient   [dT/dx, dT/dy, dT/dz]      components.x, .y, .z
flux       [qx, qy, qz]
magnitude  |q| = hypot(qx, qy, qz) >= 0
```

The order is the order of `TemperatureGradient3` / `HeatFlux3`, which is the global frame of the mesh. `descriptorOf` records it
(`componentNames`: `dT/dx`, `dT/dy`, `dT/dz`; `qx`, `qy`, `qz`; `|q|`; `T`) and the test `FieldDescriptorsStateLocationComponentsAndUnits` compares each
descriptor with the table in RESULT_MODEL.md, the dimension with the type's, and the unit symbol with `K`, `K/m`, `W/m^2`, `W/m^2`.

| Order claim | Proved by | Broken by (killed) |
| --- | --- | --- |
| x is first | `T = x` gives `(1, 0, 0)` | PM04 (x/y swapped), 12 cases |
| y is second | `T = y` gives `(0, 1, 0)` | PM03, PM04 |
| z is third | `T = z` gives `(0, 0, 1)` | PM03 (y/z swapped), 15 cases |
| the three together, with signs | the affine patch `(7.2, -3.5, 11.4)` and the flux `(-36, 17.5, -57)` | all of the above, and PM05/PM06 for the sign |
| element order is `mesh.tetrahedra()` | `ElementResultsAreKeyedByOrdinal…`, `PreservesTetLocalNodeOrder…` | PM29/PM29b (raw id as index) |
| state order is the solution's | `TransientHistoryKeepsTheSolutionsOrder…`, `TransientRecoveryOnTheBlock…` | PM26 (reversed), PM27 (time of state 0), PM40 |

No component is converted, rotated, normalised or negated beyond the sign of Fourier's law.
