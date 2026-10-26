# RM-THERM-05 — Conduction in series with convection

**Model.** `ThermalConvection`: 400 × 500 × 500 mm (A = 0.2 m², L = 0.5 m along z), k = 10 W/(m K); the start cap held at 400 K, the end cap convecting to a 300 K ambient with h = 20 W/(m² K); the sides insulated.

**Resistance network (hand).** `Rcond = L/(kA) = 0.5/(10 × 0.2) = 0.25 K/W`, `Rconv = 1/(hA) = 1/(20 × 0.2) = 0.25 K/W`, `Qdot = (400 − 300)/0.5 = 200 W`, surface temperature `Ts = 300 + 200 × 0.25 = 350 K` (and `400 − 200 × 0.25 = 350 K` the other way, `kClassA`), `q = 1000 W/m²`, `dT/dz = −100 K/m`.

## `RM05_ResistanceNetwork`

* A and L **measured from the mesh** equal the declared (`kClassA`).
* Every node on the convecting cap is at **350 K** (`kClassB`); every element has gradient `(0, 0, −100)` (`≤ 1e-8`) and flux `(0, 0, 1000)` (`≤ 1e-7`).
* **Signs and units of the heat accounting (heat INTO the body positive):** the convecting face carries **−200 W** (`heatOut = +200`, `heatInto = −200`), the held cap supplies **+200 W**, the convecting area is `0.2 m²`, and the two sum to zero; balanced, normalised residual **2.5·10⁻¹⁵**.

## `RM05_HeatGainSubcase` — the sign test

The same body with the held cap at **280 K** and the same 300 K ambient: `Qdot = (280 − 300)/0.5 = −40 W`, `Ts = 300 − 40 × 0.25 = 290 K`. Heat now flows *into* the body through the convecting face (**+40 W**) and the held cap *removes* it (**−40 W**); the profile is `280 + 10 z/0.5` (`kClassB`) and the flux points toward the held cap. A sign error in `q = h(T∞ − T)` or in the reaction passes the cooling case by symmetry of one of the two errors and fails here.

**CLI.** `thermal-balance` prints reaction +200 W, convection −200 W, area 0.2 m²; `thermal-solve` min 350 K, max 400 K — compared with this closed form in `CLI_VALIDATION.md`. Mutations RM02 (ambient moved 10 K), RO03 (series heat rate forgets Rconv), RP03/RP04 (convection stiffness / load), RP11 (convection sign) probe it.
