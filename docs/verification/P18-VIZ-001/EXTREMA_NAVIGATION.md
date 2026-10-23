# P18-VIZ-001 — Extrema and navigation

## The extreme

`thermalExtremeOf(field, Maximum | Minimum)` answers over a **complete** field only; a partial or empty one has no extreme and is refused (mutation `VIZ16`). The search is a strict-comparison scan in the
domain's own ascending order, so a tie resolves to the **first (lowest) index** — the lowest DOF for a node field, POST's first element for an element field. The extreme carries its value, its
index, its location (node or element) and the node **or** element identifier (never both: the other stays default).

Vectors typed in the tests, never read from the code under test:

| Test | Field | Expected |
|---|---|---|
| `HighestTemperatureNavigation` | 310, 350, 295, 350 K on nodes 7, 104, 9001, 12000 | 350 K at **node 104** (unknown 1), not node 12000 (unknown 3) |
| `LowestTemperatureNavigation` | the same | 295 K at node 9001 (unknown 2); the minimum is seeded from the first value and is never 0 |
| `HighestHeatFluxNavigation` | \|q\| = 5, 12, 7, 12 W/m² on tetrahedra 17..20 | 12 W/m² at element **18**, the first of the tie, not element 20; the lowest is element 17 |
| `ExtremaTieBreakDeterministic` | the same field asked 25 times; a uniform 320 K field | the identical extreme every time; for the uniform field the first node (7) wins for both kinds |

Mutations `VIZ09`, `VIZ10` (ties to the last entity) and `VIZ11` (seeded from zero) change those answers and are killed.

**The extreme is the stored value, not a colour.** It is taken from the field's values, not from a band index, a clamped manual range or a rendered pixel: with a manual range that cuts the field,
the extreme is still the true one (`ManualRangeClampsTheColourAndNeverTheValue`).

## Navigation

`thermalNavigationTarget(mesh, extreme)` answers *where that entity is*: the node's canonical position, or the centroid of the tetrahedron's four nodes, in SI metres. Navigation is a camera and a selection and
nothing else — it does not search, recompute or re-rank.

`NavigationLooksAtTheEntityThatHoldsTheExtreme`: the hottest node of the brief's field is node 104 at (1, 0, 0); the highest-flux tetrahedron of the four hand elements has its centroid at (10.25, 0.25, 0.25). An
extreme whose element or node the mesh does not hold (another mesh generation) names nothing here and is refused rather than guessed. Mutation `VIZ50` (the centroid weight) is killed.

## Sparse identifiers

Node 9001 is unknown 2 and element 18 is ordinal 1: every answer is found through the numbering and POST's element order. Nothing indexes an array by an identifier (gate rule 3).

## Not covered, stated

An extreme does not carry a mesh stamp: an extreme from a previous generation whose identifier still exists in the new mesh would navigate to *that* entity. The display state marks the result stale in that
situation and no field is available to produce an extreme from it; the observation is recorded in [ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md) (R9).
