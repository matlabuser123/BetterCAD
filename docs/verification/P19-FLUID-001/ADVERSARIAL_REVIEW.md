# P19-FLUID-001 - adversarial review

Three attacks were made before the freeze:

- an **independent reviewer**, given the checklist, the ADRs and the code but not
  the author's conclusions;
- **eighteen mutation probes** across P15's validation and requirement table, the
  feature functions, the file, the CLI and CFD's resolution and source record;
- the **author's own questions**.

All findings were disposed of before the freeze. None is left as a defect in the code.

## 1. The independent reviewer (read-only)

The reviewer found **no blocker and no major finding**: eight minor findings and three
nits. It verified as sound:

- **Every branch on the two enums has the new case.** For `ThermalPropertyKind`:
  validation, availability, provenance clearing, removal, presence, the file key
  table, writer and reader, and the CLI table, format, unit and setter. For
  `ConsumerKind`: the roster, the name, the requirement row and the CLI table. No
  hand-sized table was missed, and everything else is generic.
- **The P15 sources agree with each other**: the requirement row, the requirement
  function, `isAvailable` and the CFD category logic.
- **There is no default air or water** anywhere, and nu is never stored.
- **A viscosity edit, a reassignment and a removal** each reach the result through
  the material revision or the id, with no ABA through undo.
- **The file**: optional key, SI, refused at load if unusable, older files load.
- **The compile-fail regex** cannot match the control.
- **Every count** in ADR-062 and the guard.

| # | finding | disposition |
|---|---|---|
| 1 minor | ADR-062 and the header said CFD restates nothing, but P19-DATA-001's `validate(CfdFluidProperties)` restated P15's "finite and > 0" for rho and mu, with no production caller | **fixed by removal**: the function and its test assertions are gone, the header records why, and ADR-062 §3 records the retirement |
| 2 minor | `cfdFluidCompleteness` can report `Invalid` (P15 adds material-level inconsistencies for every consumer) while `resolveCfdFluid` accepts the material; the header said "nothing a flow does not require" | **recorded**: the header and ADR-062 §3 now say the report is P15's, unreinterpreted, including its material-level findings, as P18's thermal report is. That is P15's policy for every consumer and not re-decided for one |
| 3 minor | a stale doc in `CfdAnalysis.hpp` still listed a missing viscosity as "not here yet" | **fixed** |
| 4 minor | the viscosity's reference-temperature check was untested | **fixed**: `FluidProperty_AReferenceTemperatureIsRecordedAndChecked` (recorded at 293.15 K, refused at -1 K) |
| 5 minor | `hasDynamicViscosity` had no test | **fixed**: `FluidProperty_HasDynamicViscosityAnswersOnlyForAUsableValue` |
| 6 minor | the CLI display unit was not pinned ("mPa s" contains "Pa s") | **fixed**: the exact text `0.001002 Pa s` is asserted, and `mPa s` is asserted absent |
| 7 minor | provenance clearing was tested in one direction only | **fixed**: `FluidProperty_AnEditOfAnotherPropertyKeepsTheViscositysCitation` |
| 8 minor | "reject missing fluid" was not tested for a fluid deleted after a solve | **fixed**: `CfdFluid_AFluidDeletedAfterASolveIsMissingNotRemembered`, which yields `NoMaterialAssigned`, no current source, and `InputsUnavailable` |
| 9 nit | the failure category was assigned by elimination | **fixed**: the thermal row is walked too, and a disagreement between P15's function and table becomes a named internal error instead of being mislabelled |
| 10 nit | a failed preparation resolves the fluid twice | **recorded**: correct, because both reads are deterministic over a const document, and only on the failure path |
| 11 nit | wording: "which a flow needs", and the CMake comment about not reaching into core/materials | **fixed**: the message says "incompressible flow", and the comment says cfd reads only P15's own table and availability rule |

## 2. Mutation probes

Each probe edits one file, rebuilds debug-ext (the tests and the CLI), runs the CFD, fluid and P15 material suites and the `cli.material.*` process tests through CTest (259 tests), and restores the file byte for byte, checked after every probe.

| id | the mutant | verdict | killed by |
|---|---|---|---|
| FP01 | the flow row does not require a viscosity | KILLED | `cli.material.completeness.incompressible-flow`, `CfdFluid_IsP15sCompletenessReportForIncompressibleFlow`, `CfdInput_RefusesADensityWithNoViscosity`, `FluidProperty_TheFlowConsumerRequiresDensityAndViscosityAndNothingThermal`, `MaterialCli_Viscosity_IsSetInItsUnitAndReadBackByTheCore`, `Provenance_ConsumerRequirementsAreExactAndDoNotOverreach` |
| FP02 | a viscosity is always available | KILLED | `cli.material.completeness.incompressible-flow`, `CfdFluid_IsP15sCompletenessReportForIncompressibleFlow`, `CfdInput_RefusesADensityWithNoViscosity`, `FluidProperty_TheFlowConsumerRequiresDensityAndViscosityAndNothingThermal`, `MaterialCli_Viscosity_IsSetInItsUnitAndReadBackByTheCore`, `Provenance_ReportsAMissingPropertyRatherThanEverFillingIt`, `Provenance_TheGeneralReportListsEverythingAndGatesNothing`, `Provenance_TheRequirementsTableAgreesWithTheFunctionsConsumersActuallyCall` |
| FP03 | a non-positive viscosity is accepted at entry | KILLED | `CfdFluid_AnUnusableValueCannotReachTheResolution`, `FluidProperty_AViscosityIsRefusedAtEntryUnlessFiniteAndPositive`, `FluidProperty_TheFileStoresTheViscosityInSiAndOlderFilesStillLoad`, `MaterialCli_Viscosity_IsSetInItsUnitAndReadBackByTheCore` |
| FP04 | the viscosity's reference temperature is not checked | KILLED | `FluidProperty_AReferenceTemperatureIsRecordedAndChecked` |
| FP05 | a Derived viscosity is accepted | KILLED | `FluidProperty_AViscosityIsRefusedAtEntryUnlessFiniteAndPositive` |
| FP06 | hasDynamicViscosity trusts any value | KILLED | `FluidProperty_HasDynamicViscosityAnswersOnlyForAUsableValue` |
| FP07 | the flow function hands over a default viscosity | KILLED | `CfdFluid_AnEditOfAPropertyAFlowDoesNotReadStillMovesTheRevision`, `CfdFluid_AssigningAnotherFluidStalesTheResultForTheFluid`, `CfdFluid_AViscosityEditStalesTheResultAndNothingElse`, `CfdFluid_ResolvesDensityAndViscosityThroughP15`, `CfdInput_AcceptsACurrentMeshWithAUsableDensity`, `FluidProperty_IncompressibleFlowNamesEveryGapAtOnce` |
| FP08 | a viscosity edit keeps its stale citation | KILLED | `FluidProperty_AViscosityEditDropsItsOwnProvenanceAndNoOther` |
| FP09 | every thermal edit drops the viscosity citation | KILLED | `FluidProperty_AnEditOfAnotherPropertyKeepsTheViscositysCitation` |
| FP10 | the file writes the viscosity in mPa s | KILLED | `FluidProperty_TheFileStoresTheViscosityInSiAndOlderFilesStillLoad`, `MaterialCli_Viscosity_IsSetInItsUnitAndReadBackByTheCore`, `MaterialFile_AFailedLoadLeavesTheCallersDocumentUntouched`, `MaterialFile_RejectsMalformedMaterialsAndLeavesNothingBehind`, `MaterialFile_RoundTripsARichDocumentTwiceWithoutDrift`, `MaterialFile_RoundTripsEveryFieldOfARichMaterial`, `MaterialFile_SerializesTheSameStateToTheSameBytes` |
| FP11 | the file reader drops the viscosity | KILLED | `FluidProperty_TheFileStoresTheViscosityInSiAndOlderFilesStillLoad`, `MaterialCli_Viscosity_IsSetInItsUnitAndReadBackByTheCore`, `MaterialFile_AFailedLoadLeavesTheCallersDocumentUntouched`, `MaterialFile_RejectsMalformedMaterialsAndLeavesNothingBehind`, `MaterialFile_RoundTripsARichDocumentTwiceWithoutDrift`, `MaterialFile_RoundTripsEveryFieldOfARichMaterial`, `MaterialFile_SerializesTheSameStateToTheSameBytes` |
| FP12 | a bare CLI number is mPa s | KILLED | `MaterialCli_Viscosity_IsSetInItsUnitAndReadBackByTheCore` |
| FP13 | the CLI shows the viscosity in mPa s | KILLED | `MaterialCli_Viscosity_IsSetInItsUnitAndReadBackByTheCore` |
| FP14 | a density gap is reported as a viscosity problem | KILLED | `CfdFluid_RefusesAMaterialMissingEitherPropertyNamingEveryGap`, `CfdInput_RefusesAMaterialThatHasNoDensity` |
| FP15 | the fluid carries no revision | KILLED | `CfdData_AFluidEditStalesTheResultAndNotTheMesh`, `CfdData_TheSourceIsPopulatedFromTheDocumentAndNotDefaulted`, `CfdFluid_AnEditOfAPropertyAFlowDoesNotReadStillMovesTheRevision`, `CfdFluid_AViscosityEditStalesTheResultAndNothingElse`, `CfdFluid_ResolutionIsDeterministicAndReadOnly`, `CfdFluid_ResolvesDensityAndViscosityThroughP15` |
| FP16 | a viscosity gap is reported as a density problem | KILLED | `CfdFluid_RefusesAMaterialMissingEitherPropertyNamingEveryGap`, `CfdInput_RefusesADensityWithNoViscosity` |
| FP17 | the source records no fluid revision | KILLED | `CfdData_AFluidEditStalesTheResultAndNotTheMesh`, `CfdFluid_AnEditOfAPropertyAFlowDoesNotReadStillMovesTheRevision`, `CfdFluid_AViscosityEditStalesTheResultAndNothingElse` |
| FP18 | requireDynamicViscosity accepts a non-positive value | SURVIVED | none: see below |

**17 of 18 killed; FP18 survived and is recorded as an equivalent mutant.** FP18 removes `requireDynamicViscosity`'s refusal of a present but non-finite or non-positive viscosity. No document can hold such a value: P15 refuses it at entry (`validate(ThermalProperties)`) and at file load, and FP03 shows that removing that entry check IS caught. So the branch is defence-in-depth behind an invariant the tests pin elsewhere, and no test can reach it without first breaking that invariant. P18-MAT-001 recorded the same for conduction ("through a path P15 does not currently allow"). Every file was restored. The script is `qualification/mutation/mutate-fluid.ps1`, the verdicts are in `progress.txt` and each probe's CTest output is in `FP??-tests.log`.

## 3. The author's questions

| question | answer |
|---|---|
| Can CFD hold a fluid value P15 does not? | No: `CfdFluid` has a private constructor and one friend, and holds an id, a revision and the two values P15 handed over (size pinned). |
| Can a fluid be defaulted? | No: no value, table or library entry exists, and a missing or deleted fluid is `NoMaterialAssigned`. |
| Can an unusable viscosity reach a solve? | No: P15 refuses it at entry and at file load, and the CFD resolution of a document is unchanged by each refused edit. |
| Can a viscosity edit leave a result current? | No: the material revision moves, and the result is stale for `fluid` alone. |
| Does a viscosity edit remesh? | No: the geometry revision and the mesh stamp do not move. |
| Is nu stored anywhere? | No: no field, no file key, no CLI code; it is computed on each call. |
| Can the requirement table and the runtime function disagree? | Not unnoticed: P15's cross-check covers the new consumer, and a disagreement inside the CFD resolver is a named internal error. |
| Can an old document still be read? | Yes: a document saved before this milestone loads with the viscosity Unknown. |
