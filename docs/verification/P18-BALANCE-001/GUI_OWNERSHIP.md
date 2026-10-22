# P18-BALANCE-001 — who computes the accounting (GUI ownership)

```text
SUBJECT:  reaction heat, category totals, stored-energy rate and the balance residual are computed once, in the thermal module
```

```text
core owns the reaction heat:          YES   (reactionHeats, from the full residual)
core owns the category totals:        YES   (steadyHeatBalance / transientStepHeatBalance)
core owns the stored-energy rate:     YES   (storedEnergyChange, divided by the actual step)
core owns the balance residual:       YES   (HeatBalance::netImbalance(), one formula)
GUI recomputes any of these:          NO
```

## The rule

The residual `K T - Q`, the reactions read from it, the heat each convection face exchanges, `1^T C dT` and the sums that make the balance are computed in ONE place:
`src/thermal` + `include/bettercad/thermal`. A viewer, a report, a command or an exporter asks for a `ThermalBalanceResult` and reads it. A second computation would be a
second authority that drifts from the first: a different sign, a reaction defined as whatever closes the books, a nominal step where the actual one belongs.

## The gate: `tests/architecture/CheckBalanceOwnership.cmake`

Run as `cmake -DSOURCE_DIR=<root> -P CheckBalanceOwnership.cmake`, over the source text with `//` comments removed.

| Rule | Checks | Fails with |
| --- | --- | --- |
| 1 | no file under `apps/`, `src/`, `include/`, `examples/` **outside** `src/thermal` and `include/bettercad/thermal` names `steadyFullResidual`, `transientFullResidual`, `reactionHeats`, `convectionHeatOf`, `steadyHeatBalance`, `transientStepHeatBalance`, `residualFrom`, or calls `storedEnergyChange(` | "GUI or other non-thermal code recomputes the heat accounting (<name>); consume the core result instead." |
| 2 | `ThermalBalance.cpp` / `.hpp` contain no `computeTet4`, Jacobian, `integrateConvection`, `integrateSurfaceHeatFlux`, `integrateVolumetricHeatSource`, `computeTri3Convection`, `facetNodalHeat`, `elementNodalHeat`, `solveSteady`, `solveTransient`, `solveThermal`, `effectiveMatrix`, `reduceThermalSystem` | "rewrites what another milestone owns (<name>)" |
| 3 | `ThermalBalance.cpp` reaches the convection exchange through `residualFrom` and the load totals through `heatRate` | "does not take the convection exchange from residualFrom" / "… the load totals from the prepared contributions" |

Callers of the source-bound entry points (`recoverSteadyBalance`, `recoverTransientBalance`) are not flagged, and neither is reading the result's `storedEnergyChange` MEMBER (a viewer
displays the number; calling the function would recompute it).

## Evidence

```text
real tree                          Balance ownership check passed: 497 files outside the thermal module scanned, 0 violations
fixture balance-valid               passed: 3 files outside the thermal module scanned, 0 violations                       exit 0
fixture balance-gui-recomputes      FAILED: 1 violation: apps/app/Viewer.cpp: ... recomputes the heat accounting (steadyFullResidual)   exit 1
fixture balance-reintegrates        FAILED: 1 violation: src/thermal/ThermalBalance.cpp: rewrites ... (solveTransient)                  exit 1
fixture balance-lacks-attribution   FAILED: 2 violations: no residualFrom, no heatRate                                                  exit 1
```

Five ctest entries run it (`architecture.balance-ownership` on the real tree and `.valid`, `.gui-recomputes`, `.reintegrates`, `.lacks-attribution` on the fixtures). A gate that always passed would be
vacuous; the three failing fixtures are what prove it can fail, and the valid fixture proves a legitimate consumer — one that names a forbidden function in a comment, calls the entry point and reads
a member called `storedEnergyChange` — is not flagged.

## Formula ownership

| Formula | Owner | Elsewhere (audited) |
| --- | --- | --- |
| `q'' A`, `q''' V` | P18-LOAD-001 | none: the balance reads `heatRate` |
| `Kconv`, `Qconv`, `Kconv T - Qconv` | P18-BC-001 | none: the balance sums `residualFrom` |
| `K`, `C`, `Q` | P18-ASSEMBLY-001 | none |
| `T`, the grid, the states | P18-SOLVE-001 / P18-TRANSIENT-001 | none: no time integration is run |
| `grad T`, `q = -k grad T` | P18-POST-001 | none |
| `r = K T - Q`, the reactions, `1^T C dT`, the sums | **P18-BALANCE-001** | none outside `ThermalBalance.cpp`; the tests carry their own dense oracles |
| colour, range, legend, component choice | P18-VIZ-001 | view state; never in the balance's source |
| acceptance of the analysis | P18-VALID-001 | `status` is a numerical self-consistency, not a verdict |

## Existing GUI

No application code in this tree reads a thermal result yet, so the real-tree scan has nothing to flag; the gate is a standing constraint on P18-VIZ-001 and P18-CMD-001. They receive a
`ThermalBalanceResult` and cannot add a recomputation without turning five tests red.
