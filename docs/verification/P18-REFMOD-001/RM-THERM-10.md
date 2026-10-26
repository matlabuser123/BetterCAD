# RM-THERM-10 — Stale mesh and unresolved references

**A correct answer here is a refusal that names the cause, and that never substitutes something nearby.**

## Stale mesh (documents `ThermalStaleMesh`: 50 × 40 × 30 mm, parameter `stale_l`, 9 nodes / 12 tets)

| Case | Scenario | Result |
|---|---|---|
| `StaleMeshRejected` | R1 on mesh M1. `stale_l` 30 → 36 mm, the model **regenerated but not remeshed**. | currency `StaleGeometry`; the run stops at the **`Model`** stage — before the dof map, the loads, the boundary, the assembly (`dofMap` and `system` absent); finding `mesh_stale`; rule `MeshCurrent` = Fail; nothing published; R1 not current |
| `SameCountsAndSameIdsStillStale` | **The adversarial case.** The model is edited *and remeshed*: M2 has exactly the same node and tetrahedron **counts (9 / 12) and the same numeric NodeIds and ElementIds** as M1 (asserted). | R1 is **stale** (`SourceCompatibility` = Fail); the report carries seven `source_stale` errors — the numbering, the prepared loads, the prepared boundary, the assembled system, the solved temperatures, the recovered fields, the balance — each naming *mesh, geometry changed*. A fresh run is accepted and gives the new physics: peak flux **16 666.67 → 13 888.89 W/m²** (`k ΔT/L`, L = 30 → 36 mm) |
| `RemeshOfAnUneditedModelStillStalesTheOldResult` | **The pure form of the trap.** Nothing about the model is edited and the model is **not regenerated** (`Run::remeshOnly()` calls only the mesher — added after the review, which noted that the first version also regenerated the model); the mesh is simply generated again (same counts, same ids). | R1 is **stale** (`SourceStale`) — only the mesh object's own identity distinguishes the two, so a check by counts, ids or geometry revision would pass it as current. The fresh run is accepted and gives the same temperatures (`≤ 1e-9`): the refusal was about *provenance*, not about the numbers |

## Unresolved references (`ThermalUnresolvedLoad` / `…Temperature` / `…Convection`)

Each is a *valid* model (a held 400 K start cap, a convecting end cap) **plus one target on a face no entity sweeps** (entity 9991 — well formed, resolves to nothing), so that the refusal is about that target and not about a missing datum.

| Target | Stops at | Finding | Before assembly |
|---|---|---|---|
| surface load | `Loads` | `loads_invalid` | no system, and (loads) no boundary prepared |
| held temperature | `Boundary` | `boundary_conditions_invalid` | no system |
| convection | `Boundary` | `boundary_conditions_invalid` | no system |

For all three: the finding's `cause` is **`target_unresolved`**; nothing published (no solution, fields or balance); **no nearest-face rebinding** — `boundaryFacetsOf(map, missing)` is `Unresolved` with **no facets at all**, while the document's real faces resolve to their two triangles each; the canonical definition is **bit for bit what it was** after the failed run; and *without* the unresolvable target the very same document no longer fails for that reason (the refusal is about that target alone). The message says *"Nothing nearby is substituted"* (checked through the CLI too).

**Mutations:** RP13 (a stale mesh is accepted at the model gate) and RP12 (an unresolved reference is not reported as unresolved) are each killed by exactly the case that targets them; RP09 (the validator never reports a stale object) is killed by three; RM05 (the "unresolved" model targets a real face) is killed.

**Why the duplication of layers is not a gap.** Staleness is judged in six places (numbering, loads, boundary, system, solution, post/balance), all by comparing the same mesh stamp. A mutation of one layer is masked by the others — by design — which is why probe RP09 mutates the *validator's reporting* (the single place the verdict is made) and RP13 the model gate, rather than one prepared object.
