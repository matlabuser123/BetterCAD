# P18-QUAL-001 - thermal reference models re-run (brief sections 76-78)

All ten reference models (RM-THERM-01 to 10, twenty documents) were re-run on all three presets by the full unfiltered CTest and again, individually, through the XML reporter of each preset's own
test executable. No reference test source, oracle, document builder or runner was changed by this milestone (`git status` lists nothing under `tests/reference` or `examples`); the shared
registration files `tests/CMakeLists.txt` and `tests/cli/ZeroMatchGuard.cmake` gained only this milestone's own entries.

| model | what it establishes | cases | debug-ext | release-ext | debug-shared-ext |
|---|---|---:|---|---|---|
| RM-THERM-01 | single-tetrahedron and interior-node affine patch; constant gradient; Fourier flux; negative orientation refused, not corrected | 8 | PASS | PASS | PASS |
| RM-THERM-02 | 1-D steady slab at three mesh levels; surface-flux variant | 2 | PASS | PASS | PASS |
| RM-THERM-03 | the composite wall P18 v1 cannot represent: recorded as a limitation, closed form and refusals tested | 3 | PASS | PASS | PASS |
| RM-THERM-04 | volumetric generation: resultant `q'''V`, conservation, curved real meshes against the independent FE solve, spatial refinement | 3 | PASS | PASS | PASS |
| RM-THERM-05 | conduction in series with convection; the heat-gain sign subcase | 2 | PASS | PASS | PASS |
| RM-THERM-06 | rigid-transform invariance | 3 | PASS | PASS | PASS |
| RM-THERM-07 | local refinement | 3 | PASS | PASS | PASS |
| RM-THERM-08 | lumped transient, final partial step, non-uniform transient against an independent backward-Euler integrator, energy balance, time-step refinement, insulated generation | 8 | PASS | PASS | PASS |
| RM-THERM-09 | singular (pure-Neumann) models refused; no arbitrary finite solution accepted | 4 | PASS | PASS | PASS |
| RM-THERM-10 | stale mesh (also with the same counts and ids), unresolved load / temperature / convection targets refused before assembly | 4 | PASS | PASS | PASS |
| persistence, determinism, independence, intent | save/load/re-solve; two runs identical; the oracle includes no production code; the documents hold intent only | 2, 2, 2, 1 | PASS | PASS | PASS |
| **in-process total** | `[refmod][thermal]` | **47 cases** | 47/47 | 47/47 | 47/47 |
| fresh-process | `refmod.thermal.*` (17 ctest tests, incl. the two-binary workflow of 58 processes), 6 hidden CLI-comparison cases run by the workflow | 17 | PASS | PASS | PASS |

## The TODO's named benchmarks

| TODO item | model and cases | verdict |
|---|---|---|
| Steady residual PASS | every accepted model: `normalizedResidual <= 1e-9` (measured 0 to 5.2e-17 on RM-02 / RM-05) | PASS |
| Heat balance PASS | every accepted model: `balanced`, worst normalised residual 1.9e-15 (RM-02), 2.5e-15 (RM-05) | PASS |
| Thermal patch test PASS | RM-THERM-01 (an affine field is reproduced exactly on a real mesh with an interior node) | PASS |
| Slab conduction PASS | RM-THERM-02: 200 W, 800 W/m^2, `T(z) = 400 - 100 z` at 9, 32 and 449 nodes | PASS |
| Convection benchmark PASS | RM-THERM-05: 350 K surface, +-200 W, 1000 W/m^2; the 280 K subcase gives +-40 W | PASS |
| Transient benchmark PASS | RM-THERM-08: lumped decay (tau 919.893 s) within 0.0175 K of backward Euler at 47 stored states; the non-uniform case within 2.3e-13 K of the independent integrator | PASS |
| Singular thermal-model failure PASS | RM-THERM-09: both singular documents refused with `no_temperature_datum` by all five result commands and the validator | PASS |
| Stale-mesh failure PASS | RM-THERM-10: refused, also with identical counts and ids; an unedited remesh still stales the old result | PASS |

The per-document results are in `qualification/thermal-refmod-workflow-<preset>/` (the ledger of the 58-process workflow, with every payload byte-for-byte) and `qualification/evidence-<preset>.txt`
(72 evidence lines per preset, identical across presets). The independent re-check of four of the models from raw payloads is `ANALYTICAL_VALIDATION.md`.
