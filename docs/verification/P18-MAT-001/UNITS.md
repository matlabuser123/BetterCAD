# P18-MAT-001 — units

```text
k     ThermalConductivity      W/(m K)      P15's type, P15's unit
rho   Density                  kg/m^3       P15's type, P15's unit
cp    SpecificHeatCapacity     J/(kg K)     P15's type, P15's unit
rho cp VolumetricHeatCapacity  J/(m^3 K)    the product's own dimension, named
                                            in thermal/; no new unit, literal
                                            or catalog entry
```

## No conversion in P18

Every value crosses from P15 to P18 as a `Quantity` already in SI. The three
unit traps the brief names are converted AT THE LITERAL -- `7.8_g_per_cm3` IS
7800 kg/m³ before it reaches a material -- and the resolver returns the
canonical number unchanged. There is no `/ 1000`, no `* 1000` and no
`in(units::...)` anywhere in `src/thermal/`.

## The units table, measured

| Property | Canonical unit | Test input | Resolved value | Not the trap | PASS |
| --- | --- | --- | --- | --- | --- |
| k | W/(m K) | `45_W_per_m_K` | 45 | not 0.045 (a mm reading) | ✓ |
| rho | kg/m³ | `7800_kg_per_m3` | 7800 | not 7.8 (a g/cm³ reading) | ✓ |
| cp | J/(kg K) | `500_J_per_kg_K` | 500 | not 0.5 (a kJ reading) | ✓ |
| k, rho, cp | SI | 42.75, 7853.25, 486.5 | exactly those, each in its own field | no field swap | ✓ |
| rho cp | J/(m³ K) | 7853.25 × 486.5 | 3 820 606.125 | — | ✓ |
| equivalence | — | `0.5_kJ_per_kg_K` vs `500_J_per_kg_K`; `7.8_g_per_cm3` vs `7800_kg_per_m3` | equal | — | ✓ |

Tests: `UnitsAreCanonicalAndEquivalentInputsAgree`, `ResolveTransientProperties`,
`VolumetricHeatCapacityIsDerivedNotStored`.

## In the type system

```text
decltype(ThermalMaterial::conductivity())         ThermalConductivity
decltype(ThermalCapacity::density)                Density
decltype(ThermalCapacity::specificHeatCapacity)   SpecificHeatCapacity
double -> ThermalConductivity                     not convertible
Density * SpecificHeatCapacity                    == VolumetricHeatCapacity
Energy / (Volume * Temperature)                   == VolumetricHeatCapacity
VolumetricHeatCapacity                            != SpecificHeatCapacity, != Density
```

A field swap between `rho` and `cp` is a compile error (mutation MAT09 did
not compile); a swap that compiled would be caught by the distinct values.

## Temperature is not a material value here

Constant properties (ADR-049, ADR-027's `Constant` law): no ambient, no
reference temperature and no `k(T)` enter resolution. `MaterialProperty`'s
optional `referenceTemperature` is a RECORD of where a number was measured
and is neither read nor validated by P18.
