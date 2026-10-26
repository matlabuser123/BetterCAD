# RM-THERM-06 — Rigid-transform invariance

Full account: **`RIGID_TRANSFORM.md`**. Summary.

* **Documents:** `ThermalTransformedBase` (90 × 55 × 24 mm, k = 15, caps 400 / 300 K) and `ThermalTransformedPlaced` (the same intent on RM-MESH-06's oblique frame: `R[0][0] = 2/3`, `R[2][0] = −1/3`, `det R = 1`, `t = (0.037, −0.019, 0.023) m`).
* **Tests (3):** `TheTransformIsARigidMotion` (class A: `det R = 1`, orthogonality `≤ 1e-15`, volumes equal); `ScalarsInvariantVectorsRotated` (temperatures against the closed form in each frame; gradient and flux of the placed body `= R ·` the base's and `−k R ·` the base's, **component by component**; `Qdot = 309.375 W` in both; `k∫|∇T|²dV` identical); `ANonAxisAlignedAffineFieldRotates` (a generic affine field imposed on the boundary nodes of both meshes through the algebraic seam: the interior node lands on the plane, gradient `= R b`, flux `= −k R b`, boundary heat identical).
* **Measured:** gradient vs `R b` **3.8·10⁻¹² K/m**, flux **5.6·10⁻¹¹ W/m²**, boundary heat difference **3.4·10⁻¹³ W**, interior node of the placed mesh **0.27 µm** from the image of the base's (asserted `< 10 µm`; the cause — the mesher's frame dependence — is a hypothesis, not established; correspondence is therefore restricted to the 8 boundary nodes, whose count is asserted so the sums cannot be empty — documented, not hidden).
