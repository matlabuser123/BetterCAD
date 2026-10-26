# P18-REFMOD-001 — The reference suite

Ten reference models (RM-THERM-01 … 10), **twenty documents**, **47 in-process test cases** (`unit.ThermalReference_*`, tags `[refmod][thermal][rmNN|persistence|determinism|independence|intent]`), 6 hidden CLI-comparison cases (`ThermalRefmodCli_*`, run by the workflow script), **17 fresh-process ctest tests** (`refmod.thermal.*`).

## Where things live

| What | Path |
|---|---|
| Document builders (the intent; no expected value) | `examples/reference_models/ThermalReferenceModels.hpp`, `ThermalModels.cpp`, `ThermalCatalog.cpp` |
| Evidence generator + CLI fixture (`--thermal [--out dir]`; exit code is an assertion) | `examples/reference_models/main.cpp` |
| **The oracle** — closed forms and an independent dense FE solver; std-only, includes no BetterCAD header | `tests/reference/ThermalAnalytic.hpp` |
| Shared fixtures (`Run`, tolerances, evidence) | `tests/reference/ThermalReferenceSupport.hpp`, `ThermalPatchSupport.hpp` |
| Tests | `tests/reference/Thermal{Element,Steady,TransformRefine,Transient,Failure,Persistence,Independence,Cli}ReferenceTests.cpp` |
| Two-binary workflow | `tests/cli/ThermalReferenceWorkflow.cmake` |

## The documents

15 solve (and are re-judged **stale** after an edit of their main dimension by the runner); 5 must be refused. Two of the fifteen (`ThermalSurfaceFlux`, `ThermalTransientSlab`) were **added after the independent review** found that no accepted document exercised a surface load, a held node in a transient, a non-uniform transient field, or two different convections (`ADVERSARIAL_REVIEW.md` finding 1).

| Document | Model | Mesh (measured) | Role |
|---|---|---|---|
| `ThermalPatch` | 40 × 30 × 20 mm, k 5, 400/300 K caps | 9 nodes / 12 tets, **1 interior node** | RM-01 affine patch on a real mesh (interior node is what makes it a patch test) |
| `ThermalSlab` | 500 × 500 × 1000 mm, k 8, 400/300 K caps, sides insulated | 9 / 32 / 449 nodes at three face sizes | RM-02, persistence, CLI |
| *(none)* | composite wall | — | RM-03 limitation: closed form + refusal tests (`AUDIT.md` §2) |
| `ThermalSurfaceFlux` | 50 × 50 × 100 mm, k 5, start cap held 300 K, **+500 W/m² through the end cap** | 9 nodes | RM-02 variant: the only accepted document with a surface load; `Qdot = 1.25 W`, `T(z) = 300 + 100 z`, flux −500 W/m² |
| `ThermalGeneration` | 80 × 80 × 160 mm box, q‴ 2·10⁵ W/m³, **both caps held at 300 K, sides insulated** (an exact 1D parabola, peak 364 K, which a 9-node mesh cannot represent) | 9 nodes | RM-04 resultant `q‴V = 204.8 W` and conservation |
| `ThermalTube` | ri 20, ro 40, L 6 mm, q‴ 3·10⁶ W/m³, outer wall 300 K | 180 / 229 / 507 nodes | RM-04 on a **curved** real mesh: production == independent FE |
| `ThermalConvection` | 400 × 500 × 500 mm, k 10, h 20, hot cap 400 K, ambient 300 K | 9 nodes | RM-05 resistance network |
| `ThermalTransformedBase` / `…Placed` | 90 × 55 × 24 mm, k 15; the second under a rigid rotation + translation | 9 / 9 nodes | RM-06 |
| `ThermalLocalBaseline` / `…Target` / `…Mirror` | 40 × 60 × 100 mm beam; 5 mm control on the start cap / end cap / none | 9 / 78 / 60 nodes | RM-07 |
| `ThermalLumped` | 40 mm cube, k 400, ρ 8960, cp 385, h 25 on six faces, 400 K → 300 K ambient | 9 nodes | RM-08, persistence, CLI |
| `ThermalTransientSlab` | 40 × 40 × 100 mm, k 50, ρ 7800, cp 500; start cap **held 400 K**, end cap convecting (h 100, 300 K), one side convecting with **different** numbers (h 10, 350 K); initial 300 K; dt 50 s to 500 s | 9–12 nodes | RM-08: consistent capacity, a held node in a transient, two convections, a non-uniform field — against an independent dense backward-Euler integrator (agreement 2.3·10⁻¹³ K) |
| `ThermalInsulatedGeneration` | the same cube, no condition, q‴ 10⁶ W/m³ | 9 nodes | RM-08 exact linear heating |
| `ThermalSingular` | 60 × 40 × 25 mm, generation, **no datum** | 9 nodes | RM-09 refusal |
| `ThermalSingularBalanced` | the same, ±1000 W/m² on the two caps (net 0) | 9 nodes | RM-09 refusal (the case a solver is tempted to accept) |
| `ThermalStaleMesh` | 50 × 40 × 30 mm, `stale_l` | 9 nodes / 12 tets | RM-10 |
| `ThermalUnresolvedLoad` / `…Temperature` / `…Convection` | a *valid* model plus one target on a face no entity sweeps (entity 9991) | 9 nodes | RM-10 refusal before assembly |

## The 24 TODO items

| # | Item | Shown by |
|---|---|---|
| 1 | Define reference suite | this file; `RM-THERM-*.md` |
| 2 | Single-Tet patch | `RM01_*` (8 cases), `RM-THERM-01.md` |
| 3 | Constant-gradient | `RM01_ConstantGradient`, `RM01_PatchOnAMeshWithAnInteriorNode`, `RM06_ANonAxisAlignedAffineFieldRotates` |
| 4 | Fourier-law | `RM01_FourierFlux`, `RM02`, `RM05` (q = −k ∇T, component by component) |
| 5 | 1D slab | `RM02_SlabAtThreeMeshLevels` |
| 6 | Composite-wall | `RM03_*` (3): **recorded as unsupported**, not faked — `RM-THERM-03.md` |
| 7 | Volumetric generation | `RM04_*` (3), `RM08_UniformGenerationTransient` |
| 8 | Convection benchmark | `RM05_ResistanceNetwork`, `RM05_HeatGainSubcase` |
| 9 | Heat-balance | every accepted model checks the balance terms against the oracle; `RM08_EnergyBalance` |
| 10 | Temperature agreement | RM-01…08 against closed forms; CLI vs closed forms |
| 11 | Heat-flux agreement | RM-01, 02, 05, 06; CLI `thermal-heat-flux` |
| 12 | Rigid-transform invariance | `RM06_*` (3) — `RIGID_TRANSFORM.md` |
| 13 | Mesh-refinement behaviour | `RM02` (3 levels), `RM04_SpatialRefinementOnStructuredMeshes`, `RM08_TimeStepRefinement` — `MESH_REFINEMENT.md` |
| 14 | Local-refinement behaviour | `RM07_*` (3) — `LOCAL_REFINEMENT.md` |
| 15 | Transient analytical benchmark | `RM08_*` (7) |
| 16 | Singular-system failure | `RM09_*` (4) |
| 17 | Stale-mesh failure | `RM10_StaleMeshRejected`, `…SameCountsAndSameIds…`, `…RemeshOfAnUneditedModel…` |
| 18 | Unresolved-reference failure | `RM10_UnresolvedTargetsAreRefusedBeforeAssembly` (×3 targets) |
| 19 | Save/load/re-solve | `Persistence_*` (2) — `PERSISTENCE.md` |
| 20 | CLI validation | `refmod.thermal.*` (13), `ThermalRefmodCli_*` (5) — `CLI_VALIDATION.md` |
| 21 | Independent analytical validation | `ThermalAnalytic.hpp` + `Independence_*` (2) — `ANALYTICAL_ORACLES.md` |
| 22 | Determinism PASS | `Determinism_*` (2) — `DETERMINISM.md` |
| 23 | Three-preset regression PASS | `qualification/` (see README) |
| 24 | Evidence recorded | this directory |
