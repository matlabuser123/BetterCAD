# RM-THERM-09 — Singular system: pure Neumann, no temperature datum

**A correct answer here is a refusal.** A steady conduction problem whose every boundary is insulated or flux-driven determines the temperature only up to an additive constant. Any finite field returned would be an *invention* — by a pinned node, a weak spring, a pseudo-inverse or a tiny diagonal. The chain must refuse, **say why**, and publish nothing.

## Documents

* `ThermalSingular` — 60 × 40 × 25 mm, uniform generation 10⁵ W/m³ (6 W with nowhere to go), no boundary condition at all.
* `ThermalSingularBalanced` — the same block, **+1000 W/m² in at the start cap and −1000 W/m² out at the end cap**. The caps have the same area, so the net heat is zero and the equations are *consistent* — and the level is still undetermined. This is the case a solver is tempted to accept ("the residual is fine").

## Tests (4)

| Case | Shows |
|---|---|
| `PureNeumannSteadyFails` | stopped at **`Solve`** (not earlier, not as a numerical symptom); failure message contains *datum*: *"the steady model has no absolute temperature datum: none of its 1 independent region(s) is tied to a prescribed temperature or a convection face, so the constant temperature is in the null space and the solution is not unique…"*; finding `no_temperature_datum`; rule `WellPosed` = **Fail**, `Loads` and `MeshCurrent` = **Pass** (refused as *underdetermined*, not as malformed); **no** steady solution, transient solution, recovered fields or balance published |
| `NoArbitraryFiniteSolutionAccepted` | with the independent solver the one thing here that may pin a node: pinned at 300 K and at 500 K, the held node removes the whole **6 W** (−5.9999999999999281 / −5.9999999999998428 W) *whatever its temperature*, and the two pinned fields differ by **exactly 200 K at every node** — the shape is determined, the level is the pin's invention. And production published nothing |
| `BalancedNeumannStillMissingDatum` | stopped at `Solve`, `no_temperature_datum`; the net heat is **read from the model's own declared loads** (flux × the area of the face each names, areas measured from the mesh's boundary triangles) and is **0 W** — so the case is the consistent-but-underdetermined one it claims to be; a family of solutions exists (K·const = 0: pinned at 300 K and at 500 K the sourceless solution is constant and the two differ by 200 K everywhere) |
| `AFailedSolveLeavesTheOldResultStale` | the **lifecycle**: R1 on a valid model is current; the definition is edited into a singular one (the held caps removed, generation added) → R1 is **stale** (`SourceCompatibility` = Fail); the new run **fails with nothing published**; R1 is *still* stale after the failed attempt. **What this proves, exactly:** the validator's verdict on R1 is a pure function of (document, R1) and the chain keeps no result store, so "a failure does not revive the old result" holds by construction here; the test pins that the verdict is *stale* both before and after the failed run and that the failed run publishes nothing. A stateful holder of "the current result" (a GUI or command layer) is outside this suite and is covered by P18-VIZ/CMD |

## What it deliberately does not do

It does **not** pin a node, add a spring, regularise, or accept a "minimum-norm" field, and neither does production (mutation **RP08** — disabling the anchoring check — is killed). The refusal's wording is part of the contract (*datum*), because a user reading "singular matrix" has not been told what to fix; the message tells them to prescribe a temperature or add a convection condition.

**CLI.** The same two documents through the real binary: every one of `thermal-solve/-temperature/-gradient/-heat-flux/-balance` exits **1 with nothing on stdout** and `no_temperature_datum` on stderr; `thermal-validate` exits 1 with `accepted: false`, `stoppedAt: "solve"` (`CLI_VALIDATION.md`).
