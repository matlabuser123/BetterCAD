# P18-BALANCE-001 — attribution of the heat

```text
SUBJECT:  which number comes from where, how omission and double counting are made impossible, and what is reported per load, per condition and per unknown
```

## Sources of the categories

| Category | Detail reported | Taken from | Not taken from |
| --- | --- | --- | --- |
| surface heat | one `LoadHeat{load, heatInto}` per surface load, in the prepared loads' order | `PreparedThermalLoads::contributions()`, `kind == SurfaceHeatFlux`, `integrated.heatRate` | the CAD face area × `q''` (no geometry is read); the global `Q` |
| generation | one per volumetric load | the same, `kind == VolumetricHeatSource` | the element volumes; the global `Q` |
| convection | one `ConvectionHeat{condition, area, heatOut, heatInto}` per convection condition, in the prepared boundary's order | `PreparedConvectionBoundary::facets[*].term.residualFrom(T)` | a re-integration of `h`, `A`, `Tinf`; `sum Qconv` |
| reaction | one `ReactionHeat{dof, heatInto}` per prescribed unknown, ascending | the full residual `r` at the constraint map's unknowns | the other categories (never the remainder) |
| storage | `dE = 1^T C dT`, rate `dE/dt` | `C` and the two states of the step | the output interval |

No unordered container is used; every order is an upstream canonical order (the definition's load and condition order, P16's facet order, `ThermalDofIndex`).

## Same target, different loads

Two loads with the same value (and so possibly the same target) both count: nothing deduplicates by target, because the canonical load list is the user's intent and P18-LOAD-001 already
treats superposition as the semantics (`MultipleSurfaceLoadsAreEachCounted`). Negative values are kept (`SignConvention`: -20 W stays -20 W; `SteadyAppliedHeatAndReaction`: -9 W extracted).

## The three double-counting traps and what stops each

| Trap | Why it is tempting | What stops it |
| --- | --- | --- |
| `sum(Q) + explicit categories` | `Q` and the categories both "contain" the loads | the categories are parallel views and the residual uses `Q` once; `GlobalHeatInputIsNotAddedToTheCategories` shows the double count (100 W) the correct residual (0) does not have |
| the generation counted again because it is "already in `Q`" | the generation total is also in the residual | the generation is reported from the load attribution only; the residual enters through the reaction (prescribed rows) and the imbalance (free rows), never as a category |
| the same Dirichlet reaction counted once per boundary condition | several canonical conditions can name one unknown | the unit is the unique prescribed unknown; the tests with a duplicate identical condition show 270 W once |

## The attribution check

`sum_i Q_i = sum loads + sum Qconv` (within the policy) is verified for every state before any residual is formed:

```text
loads omitted      Q carries 100 W, no load presented       -> attribution_missing ("the heat input of the system is 100 W but the loads ... account for 0 W")
load invented      7 W of generation that is not in Q       -> attribution_missing
condition omitted  its ambient source is in Q, not presented -> attribution_missing
facet count        the conditions' facets are not the ones the system was assembled with (seam)    -> attribution_missing
```

so an incomplete or surplus category can never be reported as a balanced model. The identity needs no temperature; it is the check that the books being kept are the books
of THIS system.

At the model level the sets are compared with the system's source before the seam is entered: the prepared loads' list equals `system.source().loads` (`source_mismatch` otherwise), and the prepared boundary is
required to be current for the SAME conditions as the solution, so its convection conditions are the system's (a boundary prepared for other conditions is refused as stale before its constraints are compared with the solution's).

## What is deliberately not attributed

| Not reported | Reason |
| --- | --- |
| a reaction per boundary condition | two conditions may prescribe one unknown at one temperature; an equal split would be invented and an "owner" arbitrary (REACTION_HEAT.md) |
| heat per CAD face for a prescribed face | the reaction belongs to unknowns; a face's reaction is the sum over its unknowns, which a consumer can form (the tests do) |
| the heat of an individual element | the balance is global; element fields are P18-POST-001's |

## Upstream contracts: nothing extended

The audit asked whether P18-LOAD, P18-BC or P18-TRANSIENT had to expose more. They did not: `PreparedThermalLoads` already kept surface and generation apart
("so P18-BALANCE-001 can attribute them"), `Tri3Convection` already published `residualFrom` and the area, `PreparedThermalBoundary` already exposed the constraints and
the convection conditions, and the transient history keeps the grid. No qualified file was changed (FREEZE.md compares them by blob).
