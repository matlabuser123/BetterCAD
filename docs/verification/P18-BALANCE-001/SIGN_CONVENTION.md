# P18-BALANCE-001 — the sign convention

```text
SUBJECT:  one signed convention for every heat rate in the accounting, frozen by P18-DATA-001 and obeyed here
```

## The convention, stated once

```text
POSITIVE  = heat entering the thermal domain (added to the solid)
NEGATIVE  = heat leaving the thermal domain (removed from the solid)
UNIT      = W for every heat rate; J for an energy; W for the rate at which energy is stored
```

This is the convention `HeatBalance` (P18-DATA-001, `ThermalData.hpp`) already carries — "every term positive INTO the body" — and
that P18-LOAD-001 and P18-BC-001 documented (`SIGN_CONVENTION.md` of P18-BC-001: "every term positive INTO the body; convection negative when
the body is hotter"). P18-BALANCE-001 adds no second convention and no transformation: the five terms of its result are the five fields of
that type, with the sign they were declared with.

| Term | Field | Definition | Positive when | Source of the number |
| --- | --- | --- | --- | --- |
| surface heat | `appliedSurfaceHeat` | `sum q'' A` over the surface loads | the load heats the body (`q'' > 0`) | P18-LOAD-001 `contributions()[i].integrated.heatRate` |
| generation | `generatedHeat` | `sum q''' V` over the volumetric loads | the load generates heat (`q''' > 0`); a sink is negative | the same |
| convection | `convectionHeat` | `- sum (Kconv T - Qconv)` over the faces | the body is colder than its ambient (it gains heat) | P18-BC-001 `Tri3Convection::residualFrom` at the solved `T` |
| reaction | `prescribedTemperatureReaction` | `+ sum r_c` over the prescribed unknowns | the constraints supply heat | the full residual, below |
| storage | `storedEnergyRate` | `1^T C (T[n+1] - T[n]) / dt` | the body warms | `C` and the two states; transient only |

```text
steady       B = Q_surface + Q_generation + Q_convection + Q_reaction                       ~ 0
transient    B = Q_surface + Q_generation + Q_convection + Q_reaction - dE/dt               ~ 0
```

`B` is computed in exactly one place, `HeatBalance::netImbalance()`, from those five fields; no second formula can diverge from it.

## Distinct quantities that must not be confused

The words "heat flux" and "heat" cover five different things in this milestone. They are related and **not** interchangeable:

| Name | Symbol | Unit | What it is | Who owns it |
| --- | --- | --- | --- | --- |
| applied scalar surface heat input | `q''` | W/m² | a canonical load value; positive INTO the body; no normal is involved | P18-DATA / P18-LOAD |
| conductive vector heat flux | `q = -k grad T` | W/m² | where heat flows INSIDE the solid; points toward lower temperature | P18-POST-001 |
| outward convection heat rate | `Q_conv,out = sum (Kconv T - Qconv)` | W | what a face LOSES to its ambient; positive when the body is hotter | P18-BC (the terms), P18-BALANCE (the sum) |
| Dirichlet reaction heat rate | `R_c = + r_c` | W | what a prescribed temperature SUPPLIES; positive supplies, negative removes | P18-BALANCE |
| balance quantity | `B` | W | the signed sum above, positive INTO the domain | P18-BALANCE |

A face's convection appears in the balance as `Q_convection = - Q_conv,out`, so the one place a sign is inverted is named, tested
(`ConvectionSignAboveAmbient`, `ConvectionSignBelowAmbient`) and visible in the result (`ConvectionHeat` carries both `heatOut` and `heatInto`).

## Sign table (what the tests assert)

| Situation | Surface | Generation | Convection | Reaction |
| --- | --- | --- | --- | --- |
| a 100 W flux load on a face | +100 | | | |
| a 20 W sink | | -20 | | |
| a body at 310 K in 300 K air, `hA = 4 W/K` | | | -40 (out 40) | |
| a body at 290 K in 300 K air | | | +40 (out -40) | |
| the hot end of a prescribed 400 K / 300 K bar (`g = 2`) | | | | +200 |
| the cold end | | | | -200 |
| a 300 K boundary against 100 W applied | +100 | | | -100 |

## What does not depend on orientation

* A facet's winding cannot flip a surface load (the sign is a property of the scalar, not of a normal — P18-LOAD-001).
* A facet's winding cannot flip a convection exchange (`Tri3Convection` is a scalar-area term — P18-BC-001; the balance takes its entries as they are).
* A reaction's sign follows the energy (hot supplies, cold removes), not a CAD face's outward direction: it is the sign of an equation residual.

## Kelvin and Celsius

A temperature DIFFERENCE is the same number in K and °C, so a transient storage balance, a convection exchange `h A (T - Tinf)` and a conduction
heat `g (T1 - T2)` cannot distinguish a Celsius offset. The absolute temperature was qualified by P18-DATA (the one `isAbsoluteTemperature` rule),
P18-SOLVE and P18-POST (the nodal temperature, bit for bit, in K). The balance does not prove the unit and does not claim to.
