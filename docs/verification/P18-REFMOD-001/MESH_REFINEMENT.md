# P18-REFMOD-001 — Mesh-refinement behaviour

**Item 13.** What refinement does to the thermal answer, measured on four families, and — separately — what it does *not* do on the meshes P16 can produce. The distinction matters: the first tells you the P18 stack converges; the second tells you what a user of the mesher will actually see.

## 1. Exact fields do not change with the mesh (RM-THERM-02, class B)

A linear tetrahedron reproduces the slab's linear profile exactly, so the answer must be the same on every mesh, and any difference is a defect.

| Face size | Nodes | Tets | worst `|T − T(z)|` | hot-cap reaction | cold-cap reaction | normalised balance |
|---|---|---|---|---|---|
| (default) | 9 | 12 | 0 | +199.99999999999932 W | −200.00000000000011 W | 1.99·10⁻¹⁵ |
| 100 mm | 32 | 122 | 2.8·10⁻¹³ K | +199.99999999999164 W | −200.00000000000426 W | 3.15·10⁻¹⁴ |
| 50 mm | 449 | 2456 | 8.0·10⁻¹³ K | +199.99999999996254 W | −200.00000000003695 W | 1.86·10⁻¹³ |

`Qdot = k A ΔT/L = 8 × 0.25 × 100 / 1 = 200 W` on all three, the gradient is `−100 K/m` and the flux `800 W/m²` in **every element** of every mesh (`≤ 1e-8`, `≤ 1e-7`). The meshes are asserted to be different (9 < 32 < 449). The error does not *shrink* with refinement because there is no discretisation error to remove: it stays at round-off (≤ 10⁻¹² K) on every level.

## 2. A smooth field: convergence on structured meshes (RM-THERM-04 cube, class E guard)

A 100 mm cube, uniform generation, all six faces held at 300 K, against the triple Fourier series (`ThermalAnalytic.hpp: cubeGenerationRise`). Hand-built Kuhn tetrahedral meshes of N × N × N cells are pushed through the **production** assembly, constraint map, steady solve and recovery (the algebraic seams), because P16 cannot produce such meshes (§3).

| N | Nodes | max error (K) | ratio | RMS error (K) | gradient RMS error (K/m) |
|---|---|---|---|---|---|
| 4 | 125 | 4.743 | | 2.228 | 620.9 |
| 8 | 729 | 1.296 | 3.66 | 0.640 | 340.0 |
| 16 | 4913 | 0.333 | 3.90 | 0.166 | 179.1 |

The temperature converges at **order 1.87 then 1.96** (theory: 2 for a linear element), the gradient at about 0.9–1.0 (theory: 1). Gates: each level `< previous / 2.83` for the temperature error, the gradient never worse and the finest pair `< previous / 1.87`; the finest level inside 1 % (RMS) and 5 % (max) of the centre rise. The independent series, not production, supplies every expected number.

## 3. What P16 can actually mesh (RM-THERM-04 tube, class C + E)

The curved tube (ri 20 / ro 40 / L 6 mm, q‴ 3·10⁶ W/m³, outer wall held) on **real P16 meshes** at three deflections:

| Deflection | Nodes | Tets | production vs independent FE | Volume deficiency | RMS vs closed form | Hottest node (exact 348.411 K) |
|---|---|---|---|---|---|---|
| 2 mm | 180 | 399 | **9.7·10⁻¹³ K** | 5.1·10⁻³ | 3.99 K | 354.94 K |
| 0.1 mm | 229 | 404 | **1.4·10⁻¹² K** | 1.1·10⁻³ | 3.80 K | 354.09 K |
| 0.02 mm | 507 | 883 | **3.8·10⁻¹² K** | 2.2·10⁻⁴ | 3.79 K | 353.85 K |

Production **equals an independent dense FE solve on the same mesh to 4·10⁻¹² K**: the thermal stack is right. The 3.8–4.0 K error against the closed form **does not fall** with refinement because P16 places no interior node between the walls (measured 160–4200 nodes, deflection 0.5–0.02 mm × outer-wall size 8–2 mm: hottest 351.6–358.2 K throughout; `AUDIT.md` §3). The volume deficiency does fall (chord approximation of the wall), and the test asserts that, plus the generated heat and the reactions, which depend on the volume and not on the interior resolution (`Σ reactions = −q‴V` to `1e-9`).

**This is reported, not hidden:** a statement "the maximum temperature converges with mesh refinement" is *true for the P18 stack* (§2) and *false for the meshes P16 produces* (§3). The suite asserts the first on structured meshes and, for the second, only what is true: equality with the independent solve and a bounded error.

## 4. Time-step refinement (RM-THERM-08, class D)

Lumped cube, dt = 200 / 100 / 50 s, error against `T∞ + (T0 − T∞) e^{−t/τ}` at the times all three runs store:

| dt (s) | steps | worst error (K) | observed order |
|---|---|---|---|
| 200 | 23 | 3.682 | |
| 100 | 46 | 1.919 | 0.940 |
| 50 | 92 | 0.984 | 0.964 |

First order, as backward Euler is (gate: ratio ≥ 1.74 per halving, and `< 2.5` so a second-order result — which would mean the error is not the integrator's — also fails).
