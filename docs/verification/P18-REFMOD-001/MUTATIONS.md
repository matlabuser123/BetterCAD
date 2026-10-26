# P18-REFMOD-001 — Mutation campaign

**29 probes, 29 killed.** Each probe edits one file, rebuilds `bettercad_tests` (release-ext), runs the thermal reference suite (`[refmod][thermal]`) and **restores the file** (the driver compares the file before and after; every file was restored byte for byte, and the pre-campaign SHA-256 of every production file was re-read at the end). A probe is *killed* when at least one reference case fails. Driver: `qualification/mutation-driver-ref.ps1` + `mutate-ref.ps1`; the raw per-probe logs are in `qualification/mutations/`.

The campaign was run twice: the first 26 probes (RP01–RP14, RM01–RM07, RO01–RO05) against the suite as first written, then — after the independent review found that a lumped-matrix error and a surface-load error would have passed — three more (RP15–RP17) against the extended suite. **RP15, RP16 and RP17 were designed to be survivors of the first suite; they were not run against it** (the review had already shown they would survive by reading the tests), so they are evidence that the *additions* work, not that the first suite did.

Three probes were VOID on their first run and were rewritten, not counted as kills: **RP04** (the link failed with *Permission denied* — the test executable was still open; re-run unchanged and killed), **RP08** (`anchoredRow[row] = true;` left two variables set-but-unused, so `-Werror` refused the mutant; rewritten as `… || true`), **RO04** (dropping `conductivity` from the independent stiffness left a parameter unused; rewritten as a factor 0.5).

## Production probes — a physical law, a sign, a factor moved

| ID | Mutation | Killed by (cases) |
|---|---|---|
| RP01 | Fourier: the **x** component of the heat flux loses its minus sign | `RM01_PatchOnAMeshWithAnInteriorNode`, `RM06_ANonAxisAlignedAffineFieldRotates`, `RM06_ScalarsInvariantVectorsRotated` (3). **Note:** the slab, convection and surface-flux flux is along z, so only the patch and the rotated bodies exercise the x component — a mutation of `.z` would be seen by many cases, one of `.x` by these three |
| RP02 | volumetric generation shared among 3 nodes instead of 4 | `RM04` ×3, `RM07_MappingPreservedAndTheSolveIsValid`, `RM07_TheTrendIsMeasuredNotAssumed`, `RM08_FinalPartialStep`, `RM08_UniformGenerationTransient` (7) |
| RP03 | convection stiffness `h A / 6` instead of `/12` | `RM05` ×2, `RM08` ×3 (5) |
| RP04 | convection load sees an ambient 5 K warmer | `RM05` ×2, `RM08` ×3 (5) |
| RP05 | consistent capacity off-diagonal `ρ c V/10` instead of `/20` | `RM08` ×5 (5) |
| RP06 | the partial last step is a full nominal step | `RM08_FinalPartialStep`, `RM08_UniformGenerationTransient` (2) |
| RP07 | the final stored time is the nominal grid time (1.2), not the end time | the same two (2) |
| RP08 | every row declared anchored — the datum check disabled | `RM09` ×3 (3) |
| RP09 | the validator never reports a stale derived object | `RM09_AFailedSolveLeavesTheOldResultStale`, `RM10_SameCountsAndSameIdsStillStale`, `RM10_RemeshOfAnUneditedModelStillStalesTheOldResult` (3) |
| RP10 | the reaction of a prescribed unknown has the opposite sign | `Determinism` ×2, `Persistence_SaveLoadReSolve`, `RM02`, `RM04` ×2, `RM05` ×2, `RM07` ×2 (10) |
| RP11 | the heat through a convecting face has the opposite sign | `Determinism` ×2, `Persistence`, `RM05` ×2, `RM08` ×4 (9) |
| RP12 | an unresolved face reference is not reported as unresolved | `RM10_UnresolvedTargetsAreRefusedBeforeAssembly` (1) |
| RP13 | a stale mesh is accepted at the model gate | `RM10_StaleMeshRejected` (1) |
| RP14 | the conduction matrix is **0.1 %** too stiff (tolerance sensitivity) | `RM01` ×3, `RM02`, `RM04` ×2, `RM05` ×2, `RM06` (9) |
| RP15 | a surface heat flux shared among 2 nodes instead of 3 | `RM02_SurfaceHeatFluxIntoAHeldSlab` (1) — *before this case existed no test would have failed* |
| RP16 | the consistent capacity matrix replaced by a lumped (diagonal) one with the same row sums | `RM08_NonUniformTransientAgainstIndependentBackwardEuler` (1) — *the lumped cube cannot see it* |
| RP17 | the Robin face matrix replaced by a lumped one with the same row sums | the same case (1) |

## Reference-model probes — a document that does not say what its test thinks

| ID | Mutation | Killed by |
|---|---|---|
| RM01 | the slab holds its caps the wrong way round | `RM02_SlabAtThreeMeshLevels` (also 2608 failed assertions) |
| RM02 | the convection model sits in an ambient 10 K warmer than declared | `RM05` ×2 |
| RM03 | the lumped cube leaves one face insulated | `RM08_LumpedTemperature…`, `RM08_TimeStepRefinement` |
| RM04 | the generation box generates twice the declared heat | `RM04_GenerationResultant…`, `RM04_ProductionEqualsAnIndependentSolve…` |
| RM05 | the "unresolved" held temperature targets a real face | `RM10_UnresolvedTargetsAreRefusedBeforeAssembly` |
| RM06 | the "singular" model is given a held temperature | `RM09_NoArbitraryFiniteSolutionAccepted`, `RM09_PureNeumannSteadyFails` |
| RM07 | the "balanced" model is not balanced (net heat in) | `RM09_BalancedNeumannStillMissingDatum` — *the first version of that test hard-coded ±1000 W/m² and survived this probe; it now reads the loads from the model* |

## Oracle probes — an expected value that stopped being the physics

| ID | Mutation | Killed by |
|---|---|---|
| RO01 | the slab flux uses `Th + Tc` | `RM02_SlabAtThreeMeshLevels` |
| RO02 | the lumped time constant is halved | `RM08` ×3 |
| RO03 | the series heat rate forgets the convection resistance | `RM05` ×2 |
| RO04 | the independent FE stiffness is half what it should be | `RM01_ElementMatrixIndependentOracle`, `RM04` ×2 |
| RO05 | the oracle header includes a BetterCAD header | `Independence_OracleIncludesNoProductionCode` |

## Tolerance sensitivity, read off the table

* **RP14** shows the Class B/C margins (4–7 orders inside the measured error) are not hiding a *physical* error: a 0.1 % stiffness error is seen by nine cases.
* The smallest error the suite is **blind** to is not measured by this campaign; a factor-1.0001 probe was not run. The in-process bounds that matter are `1e-9` (Class B/C) and the Class D scale bounds; the CLI's `0.02 K` transient comparison is looser than any of them and is a regression guard, not a detector.

## Honest gaps

* **No probe of the load-vector `/4.0` of the *surface* load on an accepted document before RP15's case existed**, which is the point of RP15.
* Probes ran against the in-process suite only. The process-level checks (`refmod.thermal.*`, the workflow) were exercised once, unmutated.
* The "validator never reports a stale object" probe (RP09) is not killed by `RM10_StaleMeshRejected`, which is the *model-gate* case (RP13 kills that). Staleness is judged in several places by comparing one mesh identity; a mutation of a single prepared object is masked by the others, so the probes mutate the point where the verdict is made.
* `GenerationSlab` and the 364 K parabola are not asserted against any mesh (`ANALYTICAL_ORACLES.md`).
