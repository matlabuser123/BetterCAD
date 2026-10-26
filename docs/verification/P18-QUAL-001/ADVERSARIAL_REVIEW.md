# P18-QUAL-001 - final adversarial review (brief sections 103-110, the TODO's 33 questions)

Two independent attacks were made on the integrated system, both **before** the freeze:

1. the three-part read-only audit (`audit/`), which attacked the code with the brief and the 33 questions (330 rows), and
2. a **second, independent reviewer** (a fresh agent that was given the scope, the design summary and the questions, and *not* the audit's conclusions, the findings or the fixes). It was read-only
   with respect to the repository, worked on copies of the reference documents in its own scratch directory, and drove the built `bettercad-cli.exe` and the built test executable. It made 125 tool calls.
   Its report is reproduced in substance below, with this milestone's disposition of each finding. **Where the two disagree the stricter reading is taken.**

## The second reviewer's findings and their disposition

| id | severity | claim | verified by this milestone | disposition |
|---|---|---|---|---|
| **F1** | major | **The balance gate rejects physically valid, residual-clean transient runs once the time step is small**: a 1 m, 0.5 x 0.5 m steel bar (k 8, rho 7800, cp 500) is accepted at dt 10, 30 and 100 s and refused at dt 3, 1, 0.3 and 0.1 s with `balance_out_of_tolerance` (first residual 2.5e-8 W against a policy of 5.8e-9 W); a short final step (0.1 s of a 1000.1 s run) is refused the same way. The independent physical residual stays at about 1e-15; only the balance fires. Cause: the step's stored-energy rate is `C (T1 - T0) / dt` and the rounding of the absolute temperature (about 7e-14 K at 300 K) is amplified by `C / dt`; the arithmetic scale of the policy (`ThermalBalance.hpp`) carries `|C||dT|/dt`, which has no term for `eps |T|`. | **Reproduced independently** on the committed `ThermalTransientSlab` (a 40 x 40 x 100 mm part): `thermal-validate` is accepted for dt = 50, 5, 0.5, 0.1, 0.01 s (worst normalised balance residual 1.7e-14, 1.1e-14, 2.9e-14, 9.9e-14, 1.1e-12) and **refused at dt = 0.003, 0.001, 0.0003 s** (2.9e-12, 5.6e-12, 2.0e-11, against 1e-13): the residual grows as `C / dt`. | **NOT changed. Recorded as the first item of the next phase's debt, with the owner's decision required.** It errs on the safe side (a correct run is refused; no wrong run is accepted), so it does not make a P18 result untrue. The only fix is to add a rounding term to the transient arithmetic scale, which *loosens* the balance gate in the small-step regime, and this milestone's rule is that a tolerance is never loosened by the party qualifying it. The proposed fix is exact: add `eps * sum |C_ij| |T_j| / dt` to `arithmetic` in `transientStepHeatBalance` (`ThermalBalance.cpp:504`), re-measure the qualified references (the margin is only larger), and pin it with a slab at dt 0.1 s `every_step` expected accepted. Workaround today: a larger step. |
| **F2** | major | Absurd but algebraically perfect results are accepted with no finding: a Robin anchor `h = 1e-6` with a volumetric source gives `Tmax = 1.0e9 K` (exit 0, PASS); a prescribed temperature of `1e16 K` is accepted; `k = 1e-300` is accepted. The only decider is the row-relative anchor tolerance 1e-10, so a model just above it is "well posed" with a billion kelvin. | read (`isAbsoluteTemperature` checks only finite and >= 0) | **Recorded, owner decision**: a documented upper bound on any input or output temperature, as a *warning* (an error would be a new refusal). Not a wrong number: every such result satisfies the equations. `LIMITATIONS.md`. |
| **F3** | major | The residual tolerance and the pivot floor are user intent with no upper bound: `relative_residual_tolerance` of `1` or `1e300` gives PASS and the metrics echo it. Gate 6 is vacuous in a file that says so. | read (`validate(ThermalSolverSettings)` requires finite and positive) | **Recorded, owner decision** (cap in `validate`, or a `solver_tolerance_loose` warning). The tolerance is canonical, user-editable and persisted by ADR; the validator honours it and prints it. |
| F4 | minor | No discrete-maximum-principle check: a source-free transient leaves `[min, max]` of its data by 6% (consistent-mass undershoot; ADR-049 defers the capacity choice to ELEM). | accepted as a known property of the consistent mass matrix | recorded |
| F5 | minor | No discretisation-adequacy signal: the shipped `thermal_generation.bcad` (one free node) is PASS with `Tmax = 310.7 K` against the closed form 364 K. | known, stated in ADR-053 ("not a discretisation-error bound") and `RM-THERM-04` | recorded; the reference suite judges that model on the resultant and on equality with the independent FE solve, not on the parabola |
| F6 | minor | `worstBalanceResidual` in the validation metrics is the *normalised* (dimensionless) residual, with no unit and a name that suggests watts | read | recorded; a rename changes the payload schema (a CLI milestone) |
| F7 | minor | The policy fingerprint is a hand-written description and omits `kAnchorTolerance`, `kTimeGridTolerance`, `kMaxTimeSteps`, `kMaxStoredValues` and the degeneracy factor: changing one changes acceptance but not the fingerprint, so `isCurrent(report)` still calls an old report current | read (`thermalValidationPolicyText`, `ThermalValidation.cpp:442-473`) | recorded. Changing the text changes the fingerprint printed in every payload and pinned in many tests; deferred with F1 so the policy changes once |
| F8 | minor | The balance formula is re-stated in `ThermalValidation.cpp:952` (without the `abs`) and in `ThermalBalance.cpp:698`; the telescoping check `IntegratedEnergy.endpoints` vs `.stored` is never judged by the validator | read | recorded (D10) |
| F9 | minor | Architecture checkers can be bypassed: (i) `CheckValidationOwnership` rule 4 tests only that a *name* appears in `ThermalValidation.cpp`, and the policy text strings contain those names; (ii) every checker except the CLI one strips `//` comments from unmasked text, so a `"//"` literal hides a later violation; (iii) the tolerance literal regex misses `1E-9`, `1e-08` and everything from 1e-8 to 1e-3; (iv) the viz rules scan only `Thermal*` renderer files; (v) the CLI float-literal regex misses `1.`, `2.f` and hex floats, and only `Thermal*` CLI files are globbed | read | recorded under D6 / D-I; these are the real limit of the ownership gates |
| F10 | info | ADR text out of date: ADR-049 (one translation unit; "never a warning beside a number" against `PASS_WITH_WARNINGS`; a Celsius conversion "at the boundary" against a CLI that refuses `25C`), ADR-057 (17 rules and 73 fixtures; it is 21 and 103), `ThermalResult.hpp` ("the only currentness answers in P18") | read | recorded (D5, D-J) |
| F11 | minor | Eleven read test cases would survive a mutation: `ThermalReference_RM01_FourierFlux` multiplies the production gradient by `-k` itself; `ThermalAssembly_GlobalKSymmetric` asserts only that the symmetry error is zero (a zero matrix survives); `ThermalRun_ModeIsTheDefinitionsAndOnlyItsSolutionIsBuilt` asserts only `has_value`; `ThermalBC_ConvectionAmbientEquilibrium` survives a common factor on both Robin terms; `ThermalLoad_PreparationIsIndependentOfBoundaryConditionsAndMaterial` ends in `CHECK(true)`; the possession test checks one constructor signature; the two bit-fingerprint cases compare a run with itself; `ShapeGradientPartitionOfUnity` survives any zero-row-sum G | read | **Not strengthened here**; each property has a second, stronger test (flux sign: `ThermalPost_*` and mutation QP08, killed by 28 tests; Robin factors: `ThermalBC_SingleTriangleConvection*Analytical` and QP19, killed by 52; K and C scale: the dense-oracle cases and QP02/QP04). The weak cases are listed so a later hardening can target them. |

## Answers to the TODO's 33 questions

| # | question | answer | evidence |
|---|---|---|---|
| 1 | Can P18 solve a stale mesh? | no | `requireThermalModel` (`InputProblem::MeshStale`), `ThermalReference_RM10_*`, `ThermalValidation_RejectsStaleMesh` |
| 2 | geometry that failed regeneration? | no | `P18Qualification_AFailedRegenerationNeverYieldsACurrentResult`, `ThermalInput_RefusesAfterAFailedGenerationOfEligibleGeometry` |
| 3 | a conductivity edit leaves an old result current? | no | currentness matrix row 2 |
| 4 | density/cp edits leave an old transient result current? | no | `P18Qualification_TheTransientEditsAndTheCapacityStaleTheirOwnObjects` |
| 5 | a thermal-load edit? | no | row 3 |
| 6 | a thermal-BC edit? | no | rows 4 (prescribed) and the transient convection row |
| 7 | NodeId permanent load identity? | no | search S1 = 1, provenance only; compile-fail `thermalids.*` |
| 8 | ElementId permanent intent? | no | same |
| 9 | a boundary silently move to another CAD face? | no | `ThermalBC_NoNearestFaceRebind`, `ThermalPersistence_AnUnresolvedReferenceIsPreservedAndNotRebound` |
| 10 | an unresolved BC bind to a nearby face? | no | the reviewer: `side:99`, a sketch feature, `hole_bottom` all fail with "Nothing nearby is substituted" |
| 11 | an inverted Tet enter assembly? | no | `ThermalElement_InvertedTetRejected`, `ThermalAssembly_SeamRefusals`; mutation QP01 |
| 12 | a zero-volume Tet generate finite-looking matrices? | no | `ThermalElement_CoplanarTetRejected`, `_NearDegeneratePolicy` |
| 13 | conductivity units confused? | no | typed quantities; the reviewer: `500W/mm^2`, `kW/m^2`, `25C`, `80F` all exit 2 |
| 14 | K lose symmetry? | no | natural symmetry of `Ke`; the global gate refuses, never repairs |
| 15 | duplicate sparse contributions overwrite? | no | `ThermalAssembly_TwoTetDuplicateContributionsSum`; mutation QP04 killed by 234 |
| 16 | a free null mode appear solved? | no | `ThermalSolve_PureNeumannSingular`, RM-09; the reviewer: flux pair and generation with no BC exit 1 `no_temperature_datum`; mutation QP09 |
| 17 | the solver return success with a large residual? | no, except that the *user* may set the tolerance arbitrarily high (F3) | `ThermalSolve_BadSolutionFailsResidual`, `_AnUnmeetableToleranceRefusesTheSolveItself`; mutation QP10 |
| 18 | heat balance fail while the solve is accepted? | no | the validator judges both; `ThermalValidation_ABalanceThatCancelsDoesNotRescueAFailedResidual`; but see F1 for the reverse |
| 19 | surface heat flux fail to preserve total heat rate? | no | `ThermalLoad_SurfaceHeatRateConserved`; the verifier's `+1.25 W` |
| 20 | volumetric generation fail to preserve q'''V? | no | `ThermalLoad_VolumeHeatRateConserved`, RM-04 (204.8 W) |
| 21 | convection use an incorrect sign? | no | `ThermalBC_ConvectionAboveAmbientHeatLoss` / `Below`, RM-05 heat-gain subcase; mutation QP19 |
| 22 | a rigid transform change temperature or heat-flux magnitude? | no | `ThermalReference_RM06_*` |
| 23 | GUI recalculate heat flux differently from core? | no | `ThermalViz_ConsumesQualifiedHeatFlux` (bitwise); gate `gui-recomputes` (name-based, see F9) |
| 24 | visualisation units mutate model coordinates? | no | `ThermalViz_DoesNotMutateMeshDuringUnitConversion`; mutation QP23 |
| 25 | undo restore matrices/results instead of intent? | no | `ThermalCommand_HistorySizeIndependentOfThermalResultSize`, compile-fail `thermalcmd.*` |
| 26 | save/load trust old arrays? | no | `ThermalPersistence_FakeResultsAndFakeStatusCannotBecomeAuthority`; mutation QP21 |
| 27 | the CLI implement separate Tet4 logic? | no | `architecture.thermalcli-ownership` (102+ fixtures); `ThermalCli_SteadyCoreEquivalence` (bitwise) |
| 28 | the CLI return zero on a singular solve? | no | `ThermalCli_SingularSteadyFails`; mutation QP22 |
| 29 | mesh-quality thresholds differ between GUI and CLI? | no | one `thermalQualityThresholds()`, in the validator; neither consumer holds a number |
| 30 | transient time-step changes fail to stale the old result? | no | the transient matrix (time step 50 -> 25 s) |
| 31 | save-before-solve and save-after-solve differ? | no | `ThermalPersistence_TheFileIsIdenticalWhatever...`; the 20-document byte-identity case; the reviewer: the hash of the file is identical after every verb |
| 32 | stale binaries produce qualification evidence? | no | clean + rebuild + no-op rebuild per preset, and `postrun-qual.ps1` (`FRESH_BINARY.md`) |
| 33 | a zero-test filter pass qualification? | no | `ZeroMatchGuard.cmake` (minimum counts), the preset's `noTestsAction: error`, and the per-group table of `postrun-qual.ps1` |
| - | source change after final qualification? | no | the frozen eight-path fingerprint is read back after the run and equals the committed tree (`FREEZE.md`) |

## What the reviewer did not reach

It did not read the Tet4 element, the face-to-facet mapping, the DOF map, the renderer internals or the qualification evidence documents; those were the audit's. It could not exercise held results across edits
through the CLI (every process regenerates and remeshes, ADR-057), so Q1 was read, not run; the library paths are covered by the `P18Qualification_*` and `ThermalRun_*` cases. It raised two latent
points: revisions restart at 1 on every load, so a result held across a reload-and-reedit is protected only by the process-unique mesh stamp (a future mesh cache keyed by anything else would reopen it); and
`FinalOnly` with more than one step skips the independent re-evaluation and judges the integrator's recorded residual, harmless only because the balance gate refuses it.
