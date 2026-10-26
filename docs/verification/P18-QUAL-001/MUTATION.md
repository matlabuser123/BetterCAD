# P18-QUAL-001 - mutation campaign (brief sections 98-102)

**28 production mutants, 27 killed, 1 survivor with a recorded reason. No mutant is left unexplained.**

Method (the one the previous reference milestone used, extended): one production file at a time is edited at one place, `bettercad_tests` is rebuilt in `release-ext`, and
every test of the thermal stack is run through CTest (`^unit\.(Thermal|P18Qualification|Linalg)`, 1198 cases, `-j 6`); the file is **always restored byte for byte**
(the driver compares the file after the probe with the file before it and prints `restored`; every probe line below reads `restored`). A probe is KILLED if any test
fails, SURVIVED if none does, and VOID if the mutant does not compile (the build is `-Werror`, so a mutant that leaves a variable unused is void and is re-posed, not counted).

Scripts (kept beside the other build scripts, ASCII only): `mutation-driver-qual.ps1` (probes QP01-QP25), `mutation-driver-qual2.ps1` (the re-posed and added probes
QP16, QP23, QP26-QP28), `mutate-ref.ps1` (apply, build, run, restore). Each probe's find and replace text is in the driver; the per-probe logs are `mutation-QPnn*.log`.

| id | what the mutant breaks | verdict | tests failed | first killing cases |
|---|---|---|---|---|
| QP01 | an inverted tetrahedron is admitted (the sign of detJ is not checked) | KILLED by tests | 9 tests failed | ThermalAssembly_SeamRefusals, ThermalElement_InvertedTetRejected, ThermalElement_NearDegeneratePolicy |
| QP02 | the shape gradients use J^-1 instead of J^-T (a transposed Jacobian) | KILLED by tests | 69 tests failed | ThermalAssembly_LocalOrderIsPreservedUnderEvenPermutation, ThermalAssembly_SmallSystemMatchesIndependentDenseOracle, ThermalAssembly_SparseNodeIds |
| QP03 | the consistent capacity matrix is replaced by the one-point rank-1 (centroid) matrix | KILLED by tests | 16 tests failed | ThermalAssembly_EnergyIdentities, ThermalAssembly_SingleTetCapacityAnalytical, ThermalAssembly_SingleTetCompositeSystemAnalytical |
| QP04 | duplicate sparse conductance contributions OVERWRITE instead of summing | KILLED by tests | 234 tests failed | P18Qualification_AFailedRegenerationNeverYieldsACurrentResult, P18Qualification_EveryDependencyEditStalesExactlyTheObjectsItReaches, P18Qualification_RunningTheChainLeavesEveryReferenceDocumentByteIdentical |
| QP05 | the Robin source Qconv is omitted from the heat vector | KILLED by tests | 120 tests failed | P18Qualification_RunningTheChainLeavesEveryReferenceDocumentByteIdentical, P18Qualification_TheTransientEditsAndTheCapacityStaleTheirOwnObjects, ThermalAssembly_BlockHeatResultantsPreserved |
| QP06 | the prepared heat loads are omitted from the heat vector | KILLED by tests | 155 tests failed | P18Qualification_AFailedRegenerationNeverYieldsACurrentResult, P18Qualification_EveryDependencyEditStalesExactlyTheObjectsItReaches, P18Qualification_RunningTheChainLeavesEveryReferenceDocumentByteIdentical |
| QP07 | the sign of a surface heat flux is flipped (q'' > 0 no longer heats the body) | KILLED by tests | 26 tests failed | ThermalAssembly_BlockHeatResultantsPreserved, ThermalAssembly_HeatResultantsPreserved, ThermalAssembly_SmallSystemMatchesIndependentDenseOracle |
| QP08 | the z component of the heat flux is +k grad T (Fourier sign lost) | KILLED by tests | 28 tests failed | ThermalCli_HeatFluxSummary, ThermalPost_AffineFieldGradientAndFourierFlux, ThermalPost_ConvectionIsNotPartOfTheConductiveFlux |
| QP09 | the anchoring check is disabled (a singular steady model is not refused) | KILLED by tests | 22 tests failed | ThermalCli_SingularSteadyFails, ThermalReference_RM09_AFailedSolveLeavesTheOldResultStale, ThermalReference_RM09_BalancedNeumannStillMissingDatum |
| QP10 | the independent residual gate is skipped | KILLED by tests | 10 tests failed | Linalg_ResidualIsEvaluableWithoutASolve, ThermalSolve_BadSolutionFailsResidual, ThermalSolve_ResidualToleranceIsScaleAware |
| QP11 | the validator no longer recomputes the heat-balance tolerance | KILLED by tests | 6 tests failed | ThermalValidation_ACorruptedBalanceFailsTheBalanceAlone, ThermalValidation_AResidualThatPassesDoesNotRescueABalanceThatFailsOnTheModel, ThermalValidation_BalanceIsJudgedByBalancesOwnTolerancePolicy |
| QP12 | the validator never reports a stale derived object | KILLED by tests | 13 tests failed | P18Qualification_AFailedRegenerationNeverYieldsACurrentResult, P18Qualification_EveryDependencyEditStalesExactlyTheObjectsItReaches, P18Qualification_TheTransientEditsAndTheCapacityStaleTheirOwnObjects |
| QP13 | a DOF map describes any mesh with the same node COUNT (the stamp is not compared) | KILLED by tests | 20 tests failed | P18Qualification_AFailedRegenerationNeverYieldsACurrentResult, P18Qualification_EveryDependencyEditStalesExactlyTheObjectsItReaches, ThermalAssembly_FailureOrderIsDeterministic |
| QP14 | an unresolved face reference is not reported as unresolved | KILLED by tests | 16 tests failed | ThermalBC_AFailedPreparationPublishesNothing, ThermalBC_DiagnosticsAreDeterministic, ThermalBC_NoNearestFaceRebind |
| QP15 | a target that resolved to zero facets is accepted | SURVIVED | 100% tests passed |  |
| QP16 | a steady analysis demands rho and cp (the mode-specific requirement is lost) | KILLED by tests (earlier attempt: VOID) | 203 tests failed | P18Qualification_AFailedRegenerationNeverYieldsACurrentResult, P18Qualification_EveryDependencyEditStalesExactlyTheObjectsItReaches, P18Qualification_RunningTheChainLeavesEveryReferenceDocumentByteIdentical |
| QP17 | the shortened last step is a full nominal step | KILLED by tests | 12 tests failed | ThermalBalance_TransientFinalPartialStepUsesActualDt, ThermalBalance_TransientInsulatedGenerationWithAPartialLastStep, ThermalCli_TransientFinalPartialStep |
| QP18 | the recovered fields no longer notice that the material was edited | KILLED by tests | 1 tests failed | ThermalPost_RecoveryIsRefusedForEveryStaleInput |
| QP19 | the Robin stiffness is h A / 6 instead of h A / 12 | KILLED by tests | 52 tests failed | ThermalAssembly_BlockNullModeAndCapacityConservation, ThermalAssembly_ConvectionFacetOutsideAnyElementExtendsThePattern, ThermalAssembly_ConvectionLiftsTheConstantMode |
| QP20 | a command the document refuses is reported as executed (it would enter the history) | KILLED by tests | 1 tests failed | ThermalCommand_ADefinitionTheDocumentRefusesLeavesTheCommandUnexecuted |
| QP21 | the canonical analysis writer adds a derived key (temperatures) | KILLED by tests | 101 tests failed | P18Qualification_RunningTheChainLeavesEveryReferenceDocumentByteIdentical, ThermalCli_ACommandLineFaultHasOneExitCodeWhateverTheDocumentHolds, ThermalCli_AddBoundaryConditionCoreEquivalence |
| QP22 | the CLI exits 0 whatever the verdict | KILLED by tests | 8 tests failed | ThermalCli_AnalysisFailureExitCode, ThermalCli_ANonFiniteMeasurementInTheReportKeepsAParseablePayload, ThermalCli_ARejectedBalanceIsStillPrintedAndTheExitStatusSaysNo |
| QP23 | the viewer sends metres to the kernel without the millimetre conversion | KILLED by tests (earlier attempt: SURVIVED) | 3 tests failed | ThermalViz_AnOverlayInMetresCoincidesWithAFortyMillimetreSolid, ThermalViz_SIMetreToOCCTMillimetre, ThermalViz_ThermalOverlaysReachTheScreenInTheirOwnColours |
| QP24 | the DOF index is the NodeId value, not the dense rank | KILLED by tests | 477 tests failed | P18Qualification_AFailedRegenerationNeverYieldsACurrentResult, P18Qualification_EveryDependencyEditStalesExactlyTheObjectsItReaches, P18Qualification_RunningTheChainLeavesEveryReferenceDocumentByteIdentical |
| QP25 | a prepared load list is current when the node COUNT agrees (the mesh identity is not compared) | KILLED by tests | 6 tests failed | P18Qualification_EveryDependencyEditStalesExactlyTheObjectsItReaches, ThermalLoad_GeometryEditStalesPreparedLoad, ThermalLoad_RemeshInvalidatesPreparedLoad |
| QP26 | the nonzero Dirichlet coupling term Kfc Tc is dropped from the reduced right-hand side | KILLED by tests (earlier attempt: VOID) | 205 tests failed | P18Qualification_AFailedRegenerationNeverYieldsACurrentResult, P18Qualification_EveryDependencyEditStalesExactlyTheObjectsItReaches, P18Qualification_RunningTheChainLeavesEveryReferenceDocumentByteIdentical |
| QP27 | two different prescribed temperatures on one unknown are silently merged (first wins) | KILLED by tests | 13 tests failed | ThermalBC_ConflictCountsEachUnknownOncePerPair, ThermalBC_ConflictingTemperatureRejected, ThermalBC_ConflictReportIsSummarisedAndOrderIndependent |
| QP28 | the heat-balance tolerance is a thousand times looser | KILLED by tests | 4 tests failed | ThermalBalance_MeasuredTolerancePolicy, ThermalBalance_TolerancePolicyConstantsArePinned, ThermalCli_ARejectedBalanceIsStillPrintedAndTheExitStatusSaysNo |

## The survivor: QP15, a target that resolved to zero facets is accepted

The mutated branch (`ThermalTarget.cpp`, `if (set->facets.empty())`) is the **defensive** `TargetProblem::WithoutFacets` check. It is unreachable through any P16 product:
`boundaryFacetsOf` returns an empty set only for a face the mapping reports as `FaceWithoutFacets`, which makes the mapping report incomplete and the mesh not
describe the model, so `requireThermalModel` refuses the model before a target is ever resolved. The three production branches that consume it
(`ThermalTarget.cpp:52-59`, `ThermalLoadVector.cpp:528-537`, `ThermalBoundary.cpp:603-610`) can be reached only with a hand-made mapping, and the public API offers no way to make one.
The sibling seams are tested (`ThermalLoad_EmptyTargetIsNotAZeroLoad`, `ThermalBC_EmptyMappedTargetFails`, `ThermalBC_ConvectionSeamRefusals`).
This is an equivalent-in-practice mutant, recorded in `LIMITATIONS.md`; the structural module records the same branch as untested for the same reason.

## Probes that were re-posed, and why

* **QP16** first posed as `if (true)`: the build is `-Werror` and the now-unused variables made it VOID. Re-posed as `if (true || needsDensity || needsSpecificHeat)`; killed by 203 tests.
* **QP23** first posed on the P16 *mesh* presentation (`OcctMeshPresentation.cpp`), which is outside the thermal suite and is covered by P16's `MeshDisplay_*` tests, so the thermal suite did not
  fail. The thermal overlays use the identical wrapper in `OcctOverlayPresentation.cpp`; re-posed there and killed by three viewer cases that compare the overlay with a 40 mm solid on screen.
* **QP26** first posed as `out.beff_[equation] = out.qf_[equation];` and void for the same reason (`coupled` unused); re-posed as `- 0.0 * coupled`; killed by 205 tests.

## Why these 28

They attack every stage of the chain at the property the brief names: the element (inverted tetrahedron admitted, transposed Jacobian, rank-1 capacity), the loads (a sign, an omitted contribution), the
boundary conditions (Robin stiffness h A / 6, the Robin source omitted, two temperatures merged), assembly (overwrite instead of add), the solve (the Dirichlet coupling term dropped, the anchoring check
disabled, the independent residual gate skipped), the transient (a full nominal last step, rho and cp demanded of a steady run), recovery (the Fourier sign, a material edit not noticed), the validator (the
balance tolerance not recomputed, a thousand times looser, the stale branch removed), currentness (a DOF map that ignores the stamp, a prepared load list that ignores the mesh identity), the commands (a
refused command reported as executed), persistence (a derived key written), the CLI (the exit status ignores the verdict) and the viewer (a missing unit conversion).

A mutant that dies only because the suite is large is not evidence of anything; the **first killing cases** column names, for each, the narrow tests that carry it (for example QP18, a material edit not
noticed by recovery, is killed by exactly one case, `ThermalPost_RecoveryIsRefusedForEveryStaleInput`, which is the case that owns that property).