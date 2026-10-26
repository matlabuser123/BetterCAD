# RM-THERM-07 — Local mesh refinement

Full account: **`LOCAL_REFINEMENT.md`**. Summary.

* **Documents:** `ThermalLocalBaseline` (9 nodes / 12 tets), `ThermalLocalTarget` (5 mm on the start cap: 78 / 369), `ThermalLocalMirror` (5 mm on the end cap: 60 / 271) — a 40 × 60 × 100 mm beam, k = 2, q‴ = 10⁵ W/m³, both caps held at 300 K.
* **Tests (3):** `RefinementIsMeasuredInTheVolumeBand` (the 10 mm *volume* band beside each cap, size = edge of the regular tetrahedron of equal volume; boundary-triangle counts are 12 in all three and prove nothing); `MappingPreservedAndTheSolveIsValid` (cap faces still resolve, generated heat 24 W, caps remove −12 W each, balanced); `TheTrendIsMeasuredNotAssumed`.
* **Measured:** start band mean size **11.3 mm** (target, 74 tets) against **55.4 mm** unrefined; end band **15.7 mm** (mirror, 31 tets). The gradient RMS near the refined cap changed **+4.7 % / +4.1 %** against the baseline — **no accuracy gain**. Asserted within ±10 % (a post-measurement guard; the unrefined sides moved −5.5 % / −2.3 %, so "not systematically worse" is not met); the lack of improvement is the finding (**F-R2**).
