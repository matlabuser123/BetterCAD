# P19-FLUID-001 - the fluid model, item by item

Each checklist item, where it is answered, and the test that holds it.

| checklist item | answer | held by |
|---|---|---|
| P19 fluid-property requirements | `ConsumerKind::IncompressibleFlow` requires `{ Density }` and `{ DynamicViscosity }` and nothing thermal, in P15's one requirement table (ADR-062 §2) | `Provenance_ConsumerRequirementsAreExactAndDoNotOverreach`; `FluidProperty_TheFlowConsumerRequiresDensityAndViscosityAndNothingThermal`; `CfdInput_RefusesADensityWithNoViscosity` |
| density rho | the material's ONE density, `MechanicalProperties::density`, kg/m^3; no second density anywhere | P15's existing density tests; `CfdFluid_ResolvesDensityAndViscosityThroughP15` |
| dynamic viscosity mu | `ThermalProperties::dynamicViscosity`, `MaterialProperty<DynamicViscosity>`, Pa s = kg/(m s), Known or Unknown, never a zero for "unknown" (ADR-062 §1) | `FluidProperty_TheViscosityIsDynamicInPascalSecondsAndTheKinematicIsDerived` |
| kinematic viscosity derivation | `nu = mu / rho`, m^2/s, computed by `CfdFluidProperties::kinematicViscosity()` / `CfdFluid::kinematicViscosity()` on every call; no field, no file key, no CLI code | the same; `CfdFluid_HoldsNothingButTheIdentityTheRevisionAndTheTwoValues` (size pin); `FluidProperty_TheFileStores...` (no "kinematic" in the file) |
| validate rho > 0 | P15 at entry (`validate(MechanicalProperties)`) and in `requireIncompressibleFlowProperties` | P15's density tests; `FluidProperty_IncompressibleFlowNamesEveryGapAtOnce` |
| validate mu > 0 | P15 at entry (`validate(ThermalProperties)`: finite, > 0, not Derived, reference temperature checked) and in the require functions | `FluidProperty_AViscosityIsRefusedAtEntryUnlessFiniteAndPositive` (0, negative, NaN, +inf, -inf, Derived refused; any positive accepted; a refused edit changes nothing); `FluidProperty_AReferenceTemperatureIsRecordedAndChecked`; `FluidProperty_HasDynamicViscosityAnswersOnlyForAUsableValue` |
| reject NaN / Inf | the same gate; such a value cannot be in a document, so CFD never meets one | the same; `CfdFluid_AnUnusableValueCannotReachTheResolution` |
| reject missing fluid | `resolveCfdFluid` refuses "no material assigned" (P15's diagnostic); the boundary reports `NoMaterialAssigned` | `CfdFluid_RefusesAMissingFluidAndThereIsNoDefaultAir`; `CfdFluid_AFluidDeletedAfterASolveIsMissingNotRemembered`; `CfdInput_RefusesWhenTheDocumentNamesNoMaterial` |
| no default air | there is no fluid value, table, library entry or fallback in the code; the built-in library holds metadata only and no fluid (ADR-028); a refusal writes nothing to the document | `CfdFluid_RefusesAMissingFluidAndThereIsNoDefaultAir` (object and material counts unchanged) |
| preserve fluid material ID | `CfdFluid::id()`, `CfdModel::materialId()`, `CfdResultSource::fluid` | `CfdFluid_ResolvesDensityAndViscosityThroughP15`; `CfdFluid_AssigningAnotherFluidStalesTheResultForTheFluid` |
| preserve revision / generation / fingerprint | `CfdFluid::revision()` = the material's document revision at resolution, carried into `CfdResultSource::fluidRevision`; part of `CfdFluid` equality | `CfdFluid_ResolutionIsDeterministicAndReadOnly` (same material at two revisions with equal values is two fluids) |
| fluid edit stales CFD result | the revision moves on any effective edit; the result is stale for `fluid` alone | `CfdFluid_AViscosityEditStalesTheResultAndNothingElse`; `CfdData_AFluidEditStalesTheResultAndNotTheMesh` (density) |
| fluid edit does not stale geometry unnecessarily | the geometry revision and the mesh stamp do not move; the mesh stays Current; an equal value re-entered moves nothing | `CfdFluid_AViscosityEditStalesTheResultAndNothingElse`; `CfdFluid_ReEnteringTheSameValueStalesNothing` |
| no duplicate CFD-only material database | `CfdFluid` holds an id, a revision and two values P15 handed over (size pinned); every rule, message and table is P15's | `CfdFluid_HoldsNothing...`; `CfdFluid_IsP15sCompletenessReportForIncompressibleFlow` (identical to P15's report); P19-DATA-001's CFD-side `validate(CfdFluidProperties)` retired (ADR-062 §3) |
| SI units explicit | the types: `Density`, `DynamicViscosity`, `KinematicViscosity`; the file in Pa s; the CLI's bare number in Pa s | `FluidProperty_TheViscosityIsDynamic...`; `MaterialCli_Viscosity_IsSetInItsUnitAndReadBackByTheCore` |
| determinism | resolution is a pure read: the same document gives an equal `CfdFluid`, writes nothing, moves no revision | `CfdFluid_ResolutionIsDeterministicAndReadOnly` |

## Recorded, not hidden

- **A conductivity edit also stales a flow result** (`CfdFluid_AnEditOfAPropertyAFlowDoesNotReadStillMovesTheRevision`).
  The source tracks the material's revision, not a per-property one: the conservative
  side, and the same choice P18-MAT-001 recorded for conduction.
- **The viscosity lives in P15's "thermal" group** and so appears under "thermal" in
  the file and in a CLI report. That is the cost of one home for it (ADR-062 §1).
- **An older build refuses a file carrying the new key**, by name, which is its
  existing rule for unknown keys.
