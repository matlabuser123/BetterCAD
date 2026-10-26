# P18-REFMOD-001 — Analytical oracles and how they stay independent

**Rule of the milestone:** a reference value that production computed is not a reference. Every expected number in `tests/reference/Thermal*ReferenceTests.cpp` comes from one of three places — a *hand value* written into the test with its arithmetic beside it, a *closed form* in `tests/reference/ThermalAnalytic.hpp`, or the *independent dense FE solver* in the same header. Production supplies only the state that is being judged.

## The header

`tests/reference/ThermalAnalytic.hpp` includes `<algorithm> <array> <cmath> <cstddef> <numbers> <optional> <utility> <vector>` and **nothing else**. It declares `namespace bettercad::test::analytic::heat` and uses no BetterCAD type: temperatures are `double` kelvin, lengths `double` metres.

| Oracle | Closed form | Used by |
|---|---|---|
| `Affine`, `Tet` | `T = T0 + b·(x − x0)`; barycentric weights, shape gradients from the 4 × 4 inverse of `[1 x y z]`, signed volume, `Ke = k V GᵀG`, `q = −k b` | RM-01, RM-06 |
| `Slab` | `Q = k A (Th − Tc)/L`, `q = k ΔT/L`, `dT/dz = −ΔT/L`, `T(z)` | RM-02 |
| `CompositeWall` | `R_i = L_i/(k_i A)`, `Q = ΔT/ΣR`, interface temperatures, per-layer gradients, flux continuity | RM-03 (verified against hand arithmetic only — no production counterpart exists) |
| `GenerationSlab` | slab of thickness 2a, both faces at Ts, uniform q‴: `T(x) = Ts + q‴(a² − x²)/(2k)`, `dT/dx = −q‴x/k` | RM-04 — **declared but not asserted against the box**: `ThermalGeneration` is exactly this problem (peak 364 K), but its 9-node mesh cannot represent the parabola, so the box is judged on the resultant `q‴V` and on equality with the independent FE solve. `GenerationSlab` is kept as the independently-derived reference and is currently used by no test |
| `GenerationTube` | `T(r)` log-law solution of `(1/r)(r T')' = −q‴/k` with the outer wall held and the inner wall adiabatic; `Q = q‴π(ro²−ri²)L` | RM-04 (`T(ri) = 348.4112 K` checked against a 5-digit hand value) |
| `cubeGenerationRise`, `cubeGenerationGradient` | triple Fourier series of `−kΔT = q‴` on a cube with all faces at 0 (`64 q‴ a²/(k π⁵) Σ…`); checked against the classic centre rise `0.0562 q‴a²/k` (0.5 %) | RM-04 spatial convergence |
| `ConvectionSlab` | `Rcond = L/(kA)`, `Rconv = 1/(hA)`, `Q = (Th − T∞)/(Rcond + Rconv)`, `Ts = T∞ + Q Rconv = Th − Q Rcond` | RM-05 |
| `Lumped` | `Bi = hLc/k`, `τ = ρ c V/(h A)`, continuous `T(t) = T∞ + (T0 − T∞)e^{−t/τ}`, and the **backward-Euler** recurrence `T_{n+1} = (T_n + (dt/τ)T∞)/(1 + dt/τ)` with per-step sizes (so a shortened last step is honoured) | RM-08 |
| `insulatedGenerationTemperature` | `T = T0 + q‴ t/(ρ c)` — exact for backward Euler (constant source, uniform field) | RM-08 |
| `solveTransient` (+ `RobinFace`) | independent dense **backward-Euler** integrator: consistent capacity `ρ cp V (1 + δij)/20`, Robin face matrix `h A (1 + δij)/12` and load `h T∞ A/3`, held nodes eliminated, initial field with the held nodes at their held value, per-step sizes | RM-08 non-uniform transient (held cap, two different convections) |
| `solveConduction` (+ `HeatMesh`, `HeatSolution`) | independent dense Galerkin FE: own shape gradients, own `Ke`, own Dirichlet elimination, reactions `(K T − Q)` at held nodes; no production kernel reachable | RM-04 (curved real meshes), RM-09 (what a pin would have to do) |

## What "independent" means here, and the evidence

1. **No shared code.** The header has no include of the project. `ThermalReference_Independence_OracleIncludesNoProductionCode` reads the file as text and fails on a quoted include, a `<bettercad/…>`, `<thermal/…>` or `<catch2/…>` include, or any of the names `bettercad`, `thermal::`, `meshing::`, `units::`, `Temperature::`, `ThermalConductivity` outside comments (the oracle's own namespace declaration is the one permitted mention).
2. **The gate can fail.** `…Independence_TheGateCanFail` runs the scanner over three clean samples (standard includes only; a header whose *comments* merely name what it avoids; a string literal containing `//`) and fifteen violating ones — a quoted include, `<bettercad/…>`, `<BetterCAD/…>` (letter case), `<ThermalReferenceModels.hpp>`, `<reference/…>`, `<support/…>`, `<catch2/…>`, the digraph `%:include`, a macro include, a backslash continuation, `thermal ::` with a space, `Thermal::`, a `bettercad::` type, an unqualified production function after a using-declaration — and requires the verdict each time. The scanner is a **whitelist** (after comments are removed — by a hand-written scanner that understands string literals — the only directives allowed are `#pragma once` and `#include <std-header>` from a fixed list), not the blacklist of the first version, which the review showed evadable. Mutation probe **RO05** adds a BetterCAD include to the real header and is killed.
3. **Oracles are tested against hand values before they judge production.** `RM01_FixturesAreWhatTheyClaimToBe`, `RM03_ClosedFormAgainstHandArithmetic`, `RM02` (slab by hand: 200 W, 800 W/m², −100 K/m), `RM05_ResistanceNetwork` (0.25 + 0.25 K/W, 200 W, 350 K), `RM08_TheLumpedAssumptionHolds` (`V = 6.4·10⁻⁵ m³`, `A = 9.6·10⁻³ m²`, `ρcV = 220.7744 J/K`, `τ = 919.893 s`) all compare the oracle to numbers worked out on paper.
4. **Oracles are mutation-tested.** Probes RO01–RO04 each break one oracle formula (slab flux, lumped τ, series heat rate, FE stiffness); every one is caught because the production result no longer agrees with it.
5. **Measured geometry, not declared geometry.** Where a test needs a length, area or volume it measures it from the mesh's own node coordinates (`extentOf`, `meshVolume` by the oracle's determinants) and *then* compares that with the declared value, so a builder that drifted from the constants it documents would fail rather than silently agree with itself.
6. **Production vs oracle, never production vs production.** The independent FE solve is the check that settles "solver or mesh?": production equals it to 9.7·10⁻¹³ … 3.8·10⁻¹² K on the 4 committed real meshes (the slab and three tubes), and the transient integrator to 2.3·10⁻¹³ K over 11 states, so the 3–4 K error against the closed form on the tube is the *mesh's* (`AUDIT.md` §3).

## The one place a reference value is production's

The CLI comparison (`ThermalCliReferenceTests.cpp`) also compares the CLI's numbers with the **same model run in process through the core** — CLI against core. That is an *equivalence* check, in addition to (never instead of) the comparison with the closed form in the line above it.
