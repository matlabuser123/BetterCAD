# P18-REFMOD-001 — Thermal Reference Models

**PASSED 2026-10-26.** Qualified against `b274df2` plus the working tree (committed: see `TODO.md`): **5789/5789 tests in each of three presets**, 2547 × 5 repeats in two, **0 warnings over 742 objects per preset**, 17 stages exit 0, 6 h 15 min 48 s, the source fingerprints at the freeze == before == after, first attempt (the suite itself was extended once, *before* the freeze, after the independent review).

Ten reference models give the thermal chain something to be wrong *against*: closed forms, hand arithmetic and an independent dense finite-element solver that shares no code with production. They found **no production defect**, so no production file changed (`src`, `include`, `apps`: 520 files, byte-identical to HEAD) — and the suite was shown, by 29 mutations, to be able to find one.

## What was built

| | |
|---|---|
| **20 reference documents** (`examples/reference_models/Thermal*`) | patch, slab, surface-flux slab, generation box, tube, convection, transformed base/placed, local-refinement baseline/target/mirror, lumped cube, insulated generation, held-cap transient slab, singular, singular-balanced, stale-mesh, 3 unresolved-target documents. 15 solve; 5 must be refused. The builders contain **no expected value**. `--thermal [--out dir]` in the example runner builds, saves, meshes, solves, judges and re-judges each stale after an edit; its exit code is an assertion |
| **An independent oracle** (`tests/reference/ThermalAnalytic.hpp`) | closed forms (slab, composite wall, generation, tube, Fourier-series cube, resistance network, lumped, backward Euler) plus a dense steady solver and a dense **transient** integrator; std-only, and a whitelist gate fails if it ever includes production code |
| **47 in-process cases** (63 168 assertions) | `ThermalReference_RM01 … RM10`, `Persistence`, `Determinism`, `Independence`, `Intent` — `unit.ThermalReference_*` |
| **17 fresh-process tests + a 58-process two-binary workflow** | `refmod.thermal.*`; the real runner writes the documents, the real CLI loads and solves them, 6 hidden cases compare every number with the closed forms |
| **Zero-match groups** | 19 new filters in `tests/cli/ZeroMatchGuard.cmake`, one per reference model and per cross-cutting group |

## Result

| Gate | Result |
|---|---|
| Tests, three presets (debug-ext / release-ext / debug-shared-ext) | **5789/5789 each** (5725 before this milestone + 64 new) |
| Repeats (`--repeat until-fail:5`, 2547 tests) | release-ext and debug-ext **100 %** |
| Warnings, errors | **0 / 0** over 742 objects per preset, `-Werror` |
| New tests | 47 reference cases, 17 process tests (13 + 4 for the two documents added after the review), 7 hidden CLI cases |
| Scripted workflow | **58 real processes** per preset, every exit code asserted, 32 payloads parsed twice, 3 binaries proven fresh |
| Mutation campaign | **29 probes, 29 killed** (17 production, 7 reference-model, 5 oracle) — `MUTATIONS.md` |
| Independent adversarial review | 0 critical, 17 findings (4 HIGH): 12 fixed in code/tests/docs, 3 partly, 2 recorded — `ADVERSARIAL_REVIEW.md` |
| Cross-preset | 57/57 workflow process lines identical, 32 payloads **bit-identical**, 20 documents SHA-identical, 72 evidence lines identical to the last digit — `qualification/CROSS_PRESET.md` |
| Freeze | scratch tree `ba86e2f44dd023529775d4955108f90726f16b9c`; four milestone fingerprints equal before and after — `FREEZE.md` |
| Nothing qualified changed | 520 of 520 `src`/`include`/`apps` files byte-identical to HEAD |

## What the reference models say about the thermal chain

* **The P18 stack is correct against independent oracles.** Exact fields are exact (slab 0 … 8·10⁻¹³ K on 9 / 32 / 449 nodes, reactions ±200 W, flux 800 W/m²); production equals an independent dense FE solve on every real mesh (≤ 3.8·10⁻¹² K) and an independent backward-Euler integrator over 11 states of a non-uniform transient (2.3·10⁻¹³ K); the convection network gives 350 K and ±200 W with the right signs in both heat-loss and heat-gain; rigid motion rotates gradients and fluxes component by component (3.8·10⁻¹² K/m); the lumped body follows backward Euler to 0.0084 K and is first order in dt (0.940, 0.964); a shortened last step lands on 1.0 s exactly.
* **Refusals are real.** A pure-Neumann steady model — including the net-zero-flux one — is refused as missing a datum, at the solve stage, with nothing published; a stale mesh is refused at the model stage; **a remesh with the same counts and the same ids, with and without an edit, still makes the old result stale**; an unresolved load, held temperature or convection is refused before assembly with no nearest-face rebinding.
* **Save → load → re-solve is bit-identical** (six documents; the file is byte-identical before and after solving), and results are bit-identical across runs, across processes and across the three presets.

## Owner findings (none is a P18 defect)

* **F-R1.** P16 cannot refine a smooth field on a box, rod or tube: no interior nodes beyond a skin (tube 160–4200 nodes: hottest node 351.6–358.2 K against 348.411 K, not converging, while production equals an independent solve to 4·10⁻¹² K). Mesh convergence of the *thermal stack* is therefore shown on structured meshes (cube: order 1.87, 1.96); on P16 meshes only equality with the independent solve and a bounded error are asserted. The brief's gate "maximum temperature converges with refinement" is **met for the stack and not meetable on P16 meshes** (`MESH_REFINEMENT.md`, `AUDIT.md` §3).
* **F-R2.** P16 local sizing does not improve gradient accuracy near a held boundary (refined sides +4.7 % / +4.1 %, unrefined −5.5 % / −2.3 %): the brief's "not systematically worse" is **not met**. The size effect is real (band mean 11.3 mm against 55.4 mm). (`LOCAL_REFINEMENT.md`)
* **F-R3.** P18 v1 cannot represent a composite wall (one material per document, one solid per mesh). RM-THERM-03 is recorded as such: closed form verified by hand, both refusals demonstrated, no stand-in built (`RM-THERM-03.md`).

## Limits recorded

* Only the `every_step` output policy is run in the reference documents; no *unbalanced* result is built to test the balance classifier negatively (P18-TRANSIENT / P18-BALANCE cover both).
* The Class E regression guards were fixed from measurements and four were redesigned after one (`TOLERANCES.md`); the CLI's transient comparison tolerance (0.02 K) is a guard, not a detector.
* The exploratory mesh sweeps of `AUDIT.md` §3 (160–4200 nodes) are not reproducible from committed code (the builders' `faceTarget` / `outerWallTarget` knobs exist, the sweep driver does not).
* `ThermalPatch`'s analysis (400 / 300 K caps) is exercised but its numbers are not pinned by the intent test; `GenerationSlab` (the 364 K parabola) is not asserted against any mesh.
* The mutation campaign ran against the in-process suite; the process-level tests were exercised once, unmutated.

## Where each of the 24 TODO items is shown

See `REFERENCE_SUITE.md` for the table (item → cases → document). The per-model accounts are `RM-THERM-01.md … RM-THERM-10.md`; the cross-cutting ones are `ANALYTICAL_ORACLES.md`, `TOLERANCES.md`, `MESH_REFINEMENT.md`, `LOCAL_REFINEMENT.md`, `RIGID_TRANSFORM.md`, `PERSISTENCE.md`, `CLI_VALIDATION.md`, `DETERMINISM.md`; the verification of the verification is `MUTATIONS.md`, `ADVERSARIAL_REVIEW.md`, `FREEZE.md` and `qualification/`.

## NEXT

**P18-QUAL-001 — Thermal Analysis Final Qualification.** Not started, and not authorized by this passing: the owner starts it.
