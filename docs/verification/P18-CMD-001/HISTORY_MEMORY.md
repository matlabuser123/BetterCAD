# P18-CMD-001 — history size does not depend on the mesh or the result

```text
A command's whole payload is two ThermalAnalysisDefinitions. Its size is the number of loads and conditions.
It is not O(Nnodes), not O(Ntet), not O(nnz(K)) and not O(the length of a result history).
```

## The structural claim

`ThermalAnalysisEditCommand` holds `std::optional<ThermalAnalysisDefinition> before_, after_` and a `bool`; each concrete command adds one canonical record or id (a `ThermalLoad`, a `ThermalBoundaryCondition`,
a `ThermalLoadId`, a `UniformInitialTemperature`, a `ThermalSolverSettings`, a `TransientSettings`). A `ThermalAnalysisDefinition` is a mesh-control handle, a mode, two short vectors of canonical records, at most one
`double` of initial temperature, two ratios and at most one three-field transient setting. **It has no array whose length depends on a node or element count**, and `ThermalData_TheDefinitionHoldsOnlyIntent`
(a mirror struct and a size equality) breaks the build if one is added. `CreateThermalAnalysisCommand` holds one `AddObjectCommand`, whose prototype is a clone of the new analysis — again a definition.

There is no document snapshot: `CommandHistory` stores command objects. So the generic framework deep-copies nothing, and the brief's "generic document copy trap" (section 91) does not arise.

## The measurement

`HistorySizeIndependentOfThermalResultSize` runs the **same** edit (a face flux changed from 5000 to 6000 W/m², with a held and a convecting face) on two documents solved through the whole chain:

| | tiny mesh (default) | fine mesh (20 mm body, 2 mm local size) |
| --- | --- | --- |
| nodes | small | > 4× |
| tetrahedra | small | > 10× |
| result values held by the solved post (temperatures, gradients, fluxes) | small | > 10× |
| the command's `before()` fingerprint | **equal** | **equal** |
| the command's `after()` fingerprint | **equal** | **equal** |
| loads in `before()`, conditions in `after()` | 1 and 2 | 1 and 2 |

The three growth factors are asserted (so the comparison is not vacuous: the fine document really is far larger), and the command's two definitions are shown **equal** across the two. The run prints
`history payload: 1 loads + 2 conditions in both; result values … vs …`. Nothing in a payload could vary with the mesh, because nothing in a payload refers to one.

## What is not asserted, and why

No allocation counter is used: a byte count of a heap is brittle across presets and standard libraries, and the property is exactly "the payload is these two definitions", which the type of `before()`/`after()`
(`const std::optional<ThermalAnalysisDefinition>&`) and the equality above state directly.

## Remesh

A payload that held mesh handles would be invalid after a remesh. `HistorySurvivesARemesh` shows the opposite: the command is undone and redone **after** a remesh with its two definitions unchanged
([CURRENTNESS.md](CURRENTNESS.md), [GEOMETRY_REFERENCE.md](GEOMETRY_REFERENCE.md)).
