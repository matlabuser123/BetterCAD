# P18-QUAL-001 - independent analytical validation (brief sections 79-82)

**Rule: a reference value that production computed is not a reference.** The qualification confirms the analytical validation in three independent ways. None shares code with BetterCAD.

## 1. The in-process oracle (carried, re-run on three presets)

`tests/reference/ThermalAnalytic.hpp` includes `<algorithm> <array> <cmath> <cstddef> <numbers> <optional> <utility> <vector>` and nothing else (the whitelist gate
`ThermalReference_Independence_OracleIncludesNoProductionCode` proves it, and `_TheGateCanFail` proves the gate fails on fifteen violating samples). It holds the closed forms and an independent dense
Galerkin FE solver and a dense backward-Euler integrator (`P18-REFMOD-001/ANALYTICAL_ORACLES.md`). The 47 in-process cases judge production against it; production equals the independent FE solve
to 9.7e-13 to 3.8e-12 K on the four committed real meshes and the transient integrator to 2.3e-13 K over 11 states. The matrix per preset is `REFERENCE_MODELS.md`.

## 2. The fresh-process verifier written for this milestone

`qualification/verify-independent.ps1` is a PowerShell script (Python is not installed on this machine). It reads the **raw stdout bytes** that real `bettercad-cli` processes wrote for the committed
reference documents - the payloads the workflow leaves in each preset's own build tree - with a JSON parser that shares nothing with the CLI's writer, and compares them with closed forms
computed in the script from hand constants (the geometry and the material of `RM-THERM-02`, `-02b`, `-05`, `-08`). Result on each preset: `qualification/verify-<preset>.txt`.

| model | closed form (computed in the script) | compared |
|---|---|---|
| RM-02 steady slab, 500 x 500 x 1000 mm, k = 8, 400 K / 300 K | `Q = k A dT / L = 200 W`, `q = 800 W/m^2`, `T(z) = 400 - 100 z` | min and max temperature, the largest and smallest element |q|, the flux components of the largest element, the sum of the two cap reactions (0), the balance terms, a second fresh process reproducing the first |
| RM-02b surface flux, 50 x 50 x 100 mm, k = 5, +500 W/m^2 | `Q = q'' A = 1.25 W`, `T_end = 300 + (q''/k) L = 310 K` | max temperature, max flux, `surfaceHeatInto = +1.25 W`, `dirichletReactionInto = -1.25 W` |
| RM-05 conduction in series with convection, A = 0.2 m^2, L = 0.5 m, k = 10, h = 20, 400 K / 300 K | `Rcond = Rconv = 0.25 K/W`, `Q = 200 W`, `Ts = 350 K`, `q = 1000 W/m^2` | surface temperature, max flux, `+200 W` reaction, `-200 W` convection, the convecting area |
| RM-08 lumped transient, 40 mm copper cube, h = 25, T0 = 400 K, T_inf = 300 K, dt = 100 s, 46 steps | `tau = rho c V / (h A) = 919.893 s`; backward Euler `T[n+1] = (T[n] + (dt/tau) T_inf) / (1 + dt/tau)` | the mean of the stored minimum and maximum against the recurrence at all 47 stored states (tolerance 0.05 K; measured worst 0.0175 K), the state count and the last stored time |

**The verifier can fail.** `postrun-qual.ps1` copies the payloads, doctors one number (the reaction of RM-05 to 201.5 W), runs the verifier and requires a non-zero exit; the doctored run reports `FAIL` and
`checks failed: 1` (`qualification/verify-selftest.txt`).

## 3. The CLI-against-closed-form tests (carried)

`ThermalRefmodCli_SteadySlabAgreesWithFourier`, `_ConvectionAgreesWithSeriesResistance`, `_TransientAgreesWithBackwardEuler`, `_SurfaceFluxAndTheHeldCapTransientAgree`, `_EditedModelGivesTheNewAnswer`
compare a real executable's printed numbers (tolerance 1e-9 for the text round trip) with the oracle, and the same model run in process through the core: an equivalence in addition to, never instead of,
the comparison with the closed form.

## The TODO items this evidences

| TODO item | evidence |
|---|---|
| Independent analytical validation | sections 1, 2 and 3 |
| Steady residual PASS | the residual gate in every accepted payload (`normalizedResidual <= 1e-9`; measured 0 to 5.2e-17 on the slab and the series model); `ThermalSolve_*`; mutation QP10 |
| Heat balance PASS | `worstNormalizedResidual` 1.9e-15 and 2.5e-15 on the slab and the series model; the verifier's balance terms; mutation QP11, QP28 |
| Thermal patch test PASS | `ThermalReference_RM01_*` (8): an affine field on a real mesh with an interior node is reproduced exactly |
| Slab conduction PASS | `ThermalReference_RM02_*`, the verifier's RM-02 rows |
| Convection benchmark PASS | `ThermalReference_RM05_*`, the verifier's RM-05 rows |
| Transient benchmark PASS | `ThermalReference_RM08_*`, the verifier's RM-08 rows |
| Singular thermal-model failure PASS | `ThermalReference_RM09_*` (4), `ThermalCli_SingularSteadyFails` |
| Stale-mesh failure PASS | `ThermalReference_RM10_*`, `ThermalViz_SameCountsNewMeshStillStale`, `P18Qualification_EveryDependencyEditStalesExactlyTheObjectsItReaches` |
