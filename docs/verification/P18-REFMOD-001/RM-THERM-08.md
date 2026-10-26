# RM-THERM-08 — Transient analytical benchmark

**Model.** `ThermalLumped`: a 40 mm cube, k = 400 W/(m K), ρ = 8960 kg/m³, cp = 385 J/(kg K), **all six faces** convecting (h = 25 W/(m² K), ambient 300 K), initial 400 K, backward Euler, dt = 100 s to 4600 s.

**The lumped assumption is checked, not assumed (`RM08_TheLumpedAssumptionHolds`).** `V = 6.4·10⁻⁵ m³` and `A = 9.6·10⁻³ m²` are **measured from the mesh**; `Lc = V/A = 6.667 mm`; `Bi = h Lc/k = 4.1667·10⁻⁴ ≪ 0.1`; `ρ cp V = 220.7744 J/K` and `τ = ρ cp V/(h A) = 919.893 s` by hand.

## Tests (8: the six below, `TheLumpedAssumptionHolds` above and the non-uniform case after the table)

| Case | Shows | Measured |
|---|---|---|
| `LumpedTemperatureFollowsTheAnalyticalDecay` | (1) the volume-weighted mean `T̄` equals the **backward-Euler recurrence** to within `Bi·ΔT0` (0.0417 K); (2) against `exp(−t/τ)` within the first-order bound `1.10 (r/2)/e·ΔT0 + Bi ΔT0`; (3) the nodal spread within `Bi ΔT d²/(2 Lc²)` (derived, not fitted); (4) `T̄` falls monotonically and never crosses the ambient; stored times `0, 100, …, 4600` exactly | `\|T̄ − T_BE\|` **0.0084 K**; vs exponential **1.92 K** (bound 2.2); spread **0.10 K** (bound 0.19); final `T̄` 300.8687 K, BE 300.8678 K, exp 300.6734 K |
| `TimeStepRefinement` | dt = 200 / 100 / 50: error vs the exponential at the common stored times falls first-order | 3.682 → 1.919 → 0.984 K, orders **0.940, 0.964** |
| `EnergyBalance` | every step: convection into the body = stored-energy rate (both negative); stored-energy rate = `ρ cp V ΔT̄/dt` (independent); the run: Σ dt·convection = `ρ cp V (T̄N − T0)`; the balance's own integrated stored energy equals it | energy lost **−21885.649378440234 J**, external −21885.649378444647 J, worst normalised residual 2.07·10⁻¹² |
| `SteadyLimit` | 20 τ (18 400 s, dt 200 s): the body is at the ambient | `T̄ − T∞` **1.3846·10⁻⁶ K** vs the BE closed form 1.3791·10⁻⁶ K |
| `FinalPartialStep` | dt = 0.3 s, end 1.0 s on the insulated-generation cube: three whole steps and a **shortened last step of 0.1 s**; the last stored time is **exactly 1.0** (not 1.2); each balance step uses its **actual** size; the stored-energy rate equals `q‴V = 64 W` at the shortened step | last time 1; last step size 0.10000000000000009; storage 63.999999999999417 W |
| `UniformGenerationTransient` | insulated cube, q‴ = 10⁶ W/m³ from 300 K: `T = 300 + q‴ t/(ρ cp)` — **exact** for backward Euler | `\|T − line\|` ≤ 4.5·10⁻¹³ K at dt = 0.25, 0.3 (partial), 0.5 |

## `RM08_NonUniformTransientAgainstIndependentBackwardEuler` (added after the review)

The lumped cube cannot tell a **consistent** capacity matrix from a lumped one (the bulk temperature, the stored energy and the balance depend only on row sums), cannot tell one convection condition from another (all six are identical), and has no held node. `ThermalTransientSlab` (40 × 40 × 100 mm, k 50, ρ 7800, cp 500) has the start cap **held at 400 K**, the end cap convecting (h 100, 300 K), one side face convecting with **different** numbers (h 10, 350 K), starts uniform at 300 K and develops a gradient along z (final field 369.8 … 400 K). The independent dense integrator (`solveTransient`: consistent capacity, Robin face matrix, held nodes) reproduces the production states **at all 11 stored times to 2.3·10⁻¹³ K**; the held nodes are at 400 K in every state including t = 0 (a prescription overrides the initial condition); the first step's stored-energy rate and the held-cap reaction are positive. Mutations **RP16** (consistent → lumped capacity, same row sums) and **RP17** (Robin face matrix → lumped, same row sums), which survived the original suite, are killed by this case alone.

**Not covered (recorded):** a non-default output policy (`final_only`) with a shortened last step (only `every_step` is run in the reference documents; P18-TRANSIENT covers `final_only`); and a negative control for the balance classifier (no reference builds an *unbalanced* result; P18-BALANCE's own tests do).

**Not claimed:** the test does not pretend the 1.92 K difference from the *exponential* is spatial error; it is the integrator's first-order error (it halves with dt) and the bound is the theory's. The discrete oracle (the backward-Euler recurrence) is the tight one.
