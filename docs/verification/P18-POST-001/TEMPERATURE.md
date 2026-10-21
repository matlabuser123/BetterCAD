# P18-POST-001 — the nodal temperature

```text
SUBJECT:  the recovered temperature: Kelvin, the solver's value bit for bit, keyed through the numbering
```

## What is recovered

The nodal temperature of a post result is the accepted solution's vector, copied, in K, parallel to `ThermalDofIndex`. There is no
interpolation, no smoothing, no averaging and no unit conversion. Recovery adds only checks:

| Check | Where | Problem named |
| --- | --- | --- |
| the vector has `Ndof` entries | `checkTemperatures`, before any element | `temperature_cardinality_mismatch` |
| every entry is finite (the unknown is named) | same | `non_finite_temperature_result` |
| every entry is `>= 0 K` (`isAbsoluteTemperature`) | same | `temperature_below_absolute_zero` |

The third is the one absolute-temperature rule of the thermal module. P18-SOLVE-001 and P18-TRANSIENT-001 deliberately accept any
finite field (recorded as their limitation, "the policy belongs to P18-POST-001"); this milestone applies it: a transient history whose
late state has cooled below 0 K (a strong sink in an insulated body) is returned by the solver and **refused whole** by the recovery,
naming the first state that failed (`ThermalPost_ALateStateBelowAbsoluteZeroFailsTheWholeHistory`: the state-5-of-11 case; state 5 is
named and no earlier state is published). `0 K` itself is an absolute temperature and is accepted.

## Kelvin, proved on its own

A Celsius offset cancels in every gradient, so no flux test can see one. The temperature is therefore checked directly
(`ThermalPost_NodalTemperatureRemainsKelvinAndFollowsTheSolver`): a slab solved with 350 K and 280 K prescribed recovers
**350 K and 280 K** (the test looks for exactly those values, and rejects 76.85 / 6.85 and 623.15 / 553.15), every entry is
`bit_cast`-equal to the solver's, and the extremes are within `[280, 350]`. The mutation PM09 (the stored temperatures minus 273.15)
is killed by three cases: this one, `ElementResultsAreKeyedByOrdinalAndNotByRawElementId` (which compares every node with the solver's) and
`SteadySlabOnTheBlockFollowsFourierLaw`. No gradient or flux test is among them, which is the point of testing the temperature on its own.

| Quantity | Source value | Recovered | Equal? |
| --- | --- | --- | --- |
| prescribed hot face | 350 K | 350 K | bit for bit |
| prescribed cold face | 280 K | 280 K | bit for bit |
| every free unknown | solver | same | bit for bit (`std::bit_cast<uint64_t>`) |
| "76.85" or "6.85" (Celsius) | — | not present | — |

## Offset and difference

| Transformation of the nodal field | gradient | flux | test |
| --- | --- | --- | --- |
| `T + c` (c = 123.456 K) | unchanged (<= 1e-10) | unchanged | `TemperatureOffsetLeavesGradientAndFluxUnchanged` |
| `Tref + a (T - Tref)` (a = 3, Tref = 300 K) | x3 (<= 1e-8) | x3 | `TemperatureDifferenceScalingIsLinear` |
| constant 325.7 K, 162 jittered tetrahedra | `3.22e-13` K/m | `1.61e-12` W/m² | `ConstantTemperatureZeroGradient` |

The constant field is not 0 K, so an absolute-temperature term anywhere in the gradient would show; the largest component is 1e-12, the
rounding of `G Te` with `Te ~ 326`, and the test's bound is `1e-11` K/m.

## Keying

The temperature is found by `temperatureOf(state, map, NodeId)`: the numbering resolves the node to a `ThermalDofIndex`, which indexes the
snapshot. A numbering of another mesh is refused (message "another numbering", PM33 kills the removal of that check); an unknown node and
a state out of range are refused (PM34). The sparse test uses nodes 7, 42, 104 and 9001, whose unknowns are 0..3 in ascending-id order, so
`T(104) = 301 K` is entry 2 of the vector, not entry 104 (nor 103).
