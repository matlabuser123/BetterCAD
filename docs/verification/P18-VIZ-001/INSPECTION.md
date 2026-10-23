# P18-VIZ-001 — Inspection

Two inspections: a **node** (temperature) and an **element** (gradient, heat flux and their magnitudes). Both are *reads*: every field of the returned record is a stored member or a documented
presentation of one.

## Node temperature

`inspectThermalNode(mesh, map, post, state, node, resultState)` returns the node's identity, its compact DOF index, its canonical position, the stored temperature (K, unrounded), the state index and the
state's stored time (none for a steady result), and the result state it was inspected under. The temperature is found **through the numbering** (`ThermalDofMap::dofOf`, `post.temperatureOf(state, map, node)`) —
never `node.value()` as an index.

* Refused: a result that does not describe the mesh and the numbering (another generation, or another count); a node the mesh does not hold; a state the result does not hold; a result state with nothing
  to inspect (`NoAnalysis`, `InputsUnavailable`, `NoResult`).
* `NodeTemperatureInspection` — on a real slab with 400 K and 300 K held, every node of the mesh is inspected; its identity, DOF index and canonical position match, its temperature equals the solver's own
  vector entry bit for bit (found through the numbering by the test) and lies between the two held values, a steady result has no time, and an unknown node, an unstored state and a result with nothing
  to inspect are refused.
* `SparseNodeIdInspection` — NodeIds 7, 104, 9001, 12000 on one tetrahedron: numbering 0..3, node 9001 is unknown 2, and its temperature is the third stored value. A raw-id index would read out of range
  or the wrong node. Mutation `VIZ44` (raw id as an index) is killed.

## Element gradient and flux

`inspectThermalElement(mesh, post, state, element, resultState)` returns the element's identity and **ordinal** (`ordinalOf`, never the raw id), its four nodes, the stored gradient (K/m), its display norm, the
stored heat-flux vector (W/m², **not sign-flipped and not normalised**), POST's stored |q|, the state and its time.

* `ElementThermalInspection` — every tetrahedron of a real mesh is inspected; identity, ordinal, nodes and the three stored fields equal POST's arrays; the display norm equals a hand-written norm; the flux
  is −k·∇T with the slab's k = 45 W/(m·K) (so it was not re-derived with another sign) and points down the gradient.
* `SparseElementIdInspection` — tetrahedra with ids 17..20 (sixteen boundary triangles come first) and ordinals 0..3; the gradients 3-4-0 (norm 5), 0-12-0, 2-3-6 (norm 7) and 0-0-12 come back for the
  right ids. An id that is a boundary triangle, an id that is nowhere, and a state that does not exist are refused. Mutations `VIZ45` (ordinal = raw id) and `VIZ46`/`VIZ47` (component instead of norm, flipped flux) are
  killed.
* `PickMapsARenderTriangleToTheEngineeringElement` — the viewer's pick of a render triangle resolves to the engineering element the render-index bridge names, and from there to the inspection.

## What the caller states

`resultState` is what the application knows about the currentness of the result (`ThermalDisplayState::result`). The inspection records it and refuses a state with nothing to inspect; it **cannot verify it** (it
holds no document). The parameter defaults to `Current` for hand-built posts in tests. The independent review flagged this default as a trap for a future caller; it is accepted and recorded here and in
[ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md) (finding R7): the GUI that wires this layer takes the state from `thermalDisplayState`, never from the default.

## Stale inspections say so

`ThermalNodeInspection::resultState` and `ThermalElementInspection::resultState` carry `Stale` or `SolveFailed` when that is what the caller states, so a stale value is never presented as a current one
(`StaleResultHasExplicitState`).

## Navigation

See [EXTREMA_NAVIGATION.md](EXTREMA_NAVIGATION.md).
