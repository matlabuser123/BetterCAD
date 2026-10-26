# RM-THERM-04 — Uniform volumetric generation

Three documents/cases, because one mesh cannot show everything and the mesher limits what a *real* mesh can show (`AUDIT.md` §3).

## (a) The box: the resultant and conservation — `ThermalGeneration`

80 × 80 × 160 mm, k = 10 W/(m K), q‴ = 2·10⁵ W/m³, **both caps held at 300 K, the four sides insulated** — an exact 1D parabola along z with peak `300 + q‴a²/(2k) = 364 K` (a = 80 mm). The mesh is 9 nodes (a box) and cannot represent a parabola, so the *peak* is not asserted; the **resultant** is exact:

* volume by the oracle's determinants `1.024·10⁻³ m³` (`{0, 1e-12}`); extents `0.08 × 0.08 × 0.16 m` measured from the mesh;
* `Qgen = q‴ V = 2·10⁵ × 1.024·10⁻³ = 204.8 W` by hand; the balance's `generatedHeat` and the load's own heat equal it (`{0, 1e-12}`; measured 204.80000000000004);
* the held faces remove exactly that: `Σ reactions = −204.8 W` (measured −204.8000000000001, `{1e-9, 1e-11}`); balanced `≤ 1e-12`;
* production equals the **independent FE solve** on the same mesh: difference **0**.

## (b) The tube on real P16 meshes — `ThermalTube`

ri = 20, ro = 40, L = 6 mm, k = 10, q‴ = 3·10⁶ W/m³, **outer wall** held at 300 K, inner wall and ends insulated. Closed form `T(r) = Ts + q‴(ro² − r²)/(4k) − q‴ ri² ln(ro/r)/(2k)`: `T(ri) = 348.4112 K` (hand value to 5 digits, `5e-5`), `Qgen = q‴ π (ro² − ri²) L = 67.86 W`.

Run at deflections 2 / 0.1 / 0.02 mm (180 / 229 / 507 nodes): **production equals the independent FE on every mesh (9.7·10⁻¹³ / 1.4·10⁻¹² / 3.8·10⁻¹² K)**; `Σ reactions = −q‴ V_mesh` and `generatedHeat = q‴ V_mesh` for the *mesh's* volume (so the chord approximation of the curved wall is not mistaken for a heat error), while the volume deficiency against the true annulus falls 5.1·10⁻³ → 1.1·10⁻³ → 2.2·10⁻⁴. The temperature itself is 3.8–4.0 K RMS from the closed form on all three (hottest node 354.94 / 354.09 / 353.85 K against 348.411 K): the **mesh** cannot represent the log profile, not the solver — which is what the equality with the independent solve proves.

## (c) Spatial convergence — structured Kuhn meshes through the production algebra

A 100 mm cube, all faces held at 300 K, q‴ uniform, N = 4 / 8 / 16 (125 / 729 / 4913 nodes), against the Fourier-series solution: max error 4.743 → 1.296 → 0.333 K (order 1.87, 1.96), RMS 2.228 → 0.640 → 0.166 K, gradient RMS 620.9 → 340.0 → 179.1 K/m (order ≈ 0.9). Every expected number is the oracle's (`cubeGenerationRise`), the classic centre rise `0.0562 q‴a²/k` is checked to 0.5 %, and the six held faces remove exactly `q‴ a³ = 1000 W` (`{1e-8, 1e-11}`).

## The brief's gate, stated honestly

"Maximum temperature converges (reproduced) with refinement." For the **P18 stack** it does — (c). For **meshes P16 can make** it does not (§(b), `AUDIT.md` §3, owner findings F-R1/F-R2), and the suite says so rather than loosening a tolerance or hiding the case. Conservation (resultants and reactions) holds on every mesh regardless.
