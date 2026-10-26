# RM-THERM-02 — One-dimensional steady slab

**Model.** `ThermalSlab`: 500 × 500 × 1000 mm, k = 8 W/(m K), start cap (z = 0) held at 400 K, end cap (z = 1 m) held at 300 K, the four sides left insulated (the natural condition).

**Closed form.** `Qdot = k A ΔT/L = 8 × 0.25 × 100 / 1 = 200 W`, `q = k ΔT/L = 800 W/m²` along +z, `dT/dz = −100 K/m`, `T(z) = 400 − 100 z`. All by hand in the test (`kClassA`) before they judge anything.

**Test.** `RM02_SlabAtThreeMeshLevels` runs the document at three face sizes (default, 100 mm, 50 mm) → 9 / 32 / 449 nodes, 12 / 122 / 2456 tets. For each level:

* extents and cross-section **measured from the mesh** (`0.5 × 0.5 × 1.0 m`, area `0.25 m²`) and compared with the declared geometry;
* every nodal temperature equals `T(z)` (`≤ 1e-9`; measured worst 0 / 2.8·10⁻¹³ / 8.0·10⁻¹³ K);
* **every element** has gradient `(0, 0, −100)` (`≤ 1e-8`) and flux `(0, 0, 800)` (`≤ 1e-7`); `q_z A` equals `Qdot`;
* reactions: the hot cap supplies **+200 W** and the cold cap **−200 W** (sign: heat *into* the body), summing to 0;
* the balance terms for surface heat, generation and convection are all exactly `0`; balanced, normalised residual `≤ 1e-12` (measured 2·10⁻¹⁵ / 3·10⁻¹⁴ / 1.9·10⁻¹³).

## The surface-flux variant — `RM02_SurfaceHeatFluxIntoAHeldSlab`

`ThermalSurfaceFlux` (50 × 50 × 100 mm, k = 5): the start cap held at 300 K and **+500 W/m² into the body through the end cap**. By hand: `Qdot = q'' A = 500 × 0.0025 = 1.25 W`, `dT/dz = +q''/k = +100 K/m`, `T(z) = 300 + 100 z` (310 K at the end cap), flux `−k dT/dz = −500 W/m²` (toward the held cap). Checked: every node on the line (`kClassB`), the end-cap nodes at 310 K (≥ 4 of them, asserted), every element's gradient `(0, 0, 100)` and flux `(0, 0, −500)`, and the **signs**: the surface load carries **+1.25 W** into the body, `appliedSurfaceHeat = +1.25 W`, the held cap **−1.25 W**; generation and convection terms exactly 0; balanced. Added after the review: before it, no accepted document exercised a surface load (a `/3 → /2` error in the surface load vector — mutation **RP15** — passed every test; it is now killed).

**What it shows.** The linear field is reproduced exactly on every mesh the mesher can make, with the right sign and units; refinement changes nothing, as it must for an exact field (`MESH_REFINEMENT.md` §1). Also persisted and CLI-checked (`PERSISTENCE.md`, `CLI_VALIDATION.md`: the CLI prints 800 W/m² in every element).

**Mutations that reach it:** `MUTATIONS.md` (RM01 caps swapped; RO01 oracle slab flux; and the production probes whose killing cases are listed there).
