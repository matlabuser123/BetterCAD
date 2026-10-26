# P18-REFMOD-001 — Rigid-transform invariance

**Item 12 (RM-THERM-06).** Heat conduction in an isotropic body does not know where the body is or which way it faces. Move a body rigidly and every *scalar* (temperature, heat rate, stored energy, the conduction integral `k ∫|∇T|² dV`) must be unchanged, and every *vector* (gradient, flux) must be the original rotated by the same rotation. Three cases, and the model is a poor one on purpose.

## The transform

`ThermalTransformedBase` is a 90 × 55 × 24 mm block (k = 15 W/(m K)) in the world frame. `ThermalTransformedPlaced` is the same intent with the sketch placed on the frame of P16's RM-MESH-06 reference model: an oblique frame that is neither axis-aligned nor a permutation, so no component can be "correct by luck".

* Rotation `R` with columns `X, Y, N` (the sketch's axes and normal): `R[0][0] = 2/3`, `R[2][0] = −1/3` (the sketch's x axis has a **negative** z component), `det R = 1`, orthogonality error `0` (`RM06_TheTransformIsARigidMotion`, class A).
* Translation `t = (0.037, −0.019, 0.023) m`.
* Both volumes equal `0.09 × 0.055 × 0.024 m³` by the oracle's determinants (`1e-12` relative).

## What is checked

| Check | Expected | Measured |
|---|---|---|
| Temperature at every node, base | `400 − 100 z/0.024` | within `kClassB` |
| Temperature at every node, placed | the same closed form **in the rotated frame**, `x_local = Rᵀ(x − t)` | within `kClassB` |
| Gradient, base | `[0, 0, −100/0.024]` K/m | `≤ 1e-6` |
| Gradient, placed | `R · g_base` **component by component** (a magnitude alone would hide a swapped or sign-flipped component) | `3.4·10⁻¹² K/m` |
| Flux, placed | `−k R g_base` | `5.1·10⁻¹¹ W/m²` |
| Heat rate through the held caps | `k A ΔT / L = 309.375 W` in both | `1e-9` |
| Reactions of a sourceless slab | sum to 0 | `1e-9` |
| Conduction integral `k ∫|∇T|² dV` | identical in both, and equal to `k V |g|²` | `1e-9` relative |
| Boundary heat | identical | difference `3.4·10⁻¹³ W` |
| Mesh | 8 boundary nodes + 1 interior node, 12 boundary triangles, 12 tets in **both** | identical counts |

## The non-axis-aligned affine field (the real test)

A slab held top and bottom is invariant under many *wrong* transforms (anything that preserves z). So `RM06_ANonAxisAlignedAffineFieldRotates` imposes `T = 300 + b·x` with a generic `b` on every boundary node of **both** meshes (through the algebraic seam, since no canonical boundary condition can say "a different temperature at each node") and requires:

* the interior node lands on the plane in both (`kClassB`) — an interior unknown that is solved, not imposed;
* gradient in the placed mesh `= R b` and flux `= −k R b`, element by element (`kClassB`): measured **3.76·10⁻¹² K/m** and **5.64·10⁻¹¹ W/m²**; `|g_placed| = |b|` to `1e-8`;
* the total heat exchanged at the boundary is the same in both (`1e-9`): measured difference **3.4·10⁻¹³ W**, summed `|reaction|` 1.69290000000019 vs 1.69289999999993 W.

## A finding about the mesher that the test is built around

The placed mesh is **not an exact rigid image of the base mesh.** Netgen places the interior node from an axis-aligned size tree in the *world* frame, so the interior node of the placed body sits **0.27 µm** from the image of the base's. (Boundary nodes are a rigid image to round-off.) A test that paired every node with its image would fail for a reason that is not a thermal defect, and one that tolerated the gap silently would hide it. So correspondence is restricted to **boundary nodes** (where it is exact), the interior node is checked against the closed form at *its own* position, and the offset is recorded here. This is a P16 observation (mesh generation is frame-dependent at the micron level), not a P18 one: the physics is invariant, which is what the test shows.

## Not covered, on purpose

Reflections (det −1) are not rigid motions and are not claimed; the oracle's `determinant` check is what makes the test fail if a "placement" with `det R = −1` were ever supplied. Scaling is covered by P18-ELEM-001/POST-001 (`ThermalElement_GeometryScaling`, `ThermalPost_GeometryScaling`).
