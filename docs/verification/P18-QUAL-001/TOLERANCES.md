# P18-QUAL-001 - tolerance inventory and ownership (brief sections 83-90)

**Verdict: every numeric threshold of the phase has exactly one owner, in `src/thermal` or `include/bettercad/thermal`; the CLI, the viewer and the persistence layer define none.**
Inventory: `audit/AUDIT_B_*` section B11 (grep of `src/thermal`, `include/bettercad/thermal`, `src/linalg`, `include/bettercad/linalg`, `apps/bettercad_cli`, `src/renderer`, `include/bettercad/renderer`
for `Tolerance`, `1e-`, `epsilon`, `constexpr`, `kMax`, `pivotFloor`). The **tolerance fingerprint** frozen for the run (`FREEZE.md`) is the hash of the 24 named constants and settings defaults extracted
from those directories by pattern, in file order - a change to any constant moves it.

| constant | owner | value | unit and scale | tested and pinned by |
|---|---|---|---|---|
| `kDegeneracyNoiseFactor` | `ThermalElement.cpp` | 16; bound `B = 16 eps |e1||e2||e3|` | m^3, homogeneous of degree 3 (scale-free) | `ThermalElement_NearDegeneratePolicy` (three scales; the factor mirrored as a literal), `_RoundingNoiseIsBelowTheDegeneracyBound` |
| `kSymmetryTolerance` | `ThermalSystem.cpp` (anonymous) | 1e-13 | relative, `max |Aij - Aji| <= tol max |A|` | `ThermalAssembly_GlobalKSymmetric`, `_SparseSymmetryCheckSeesAMissingTranspose` (literal) |
| `kEffectiveSymmetryTolerance` | `ThermalTransient.cpp` (anonymous) | 1e-13 | relative | `ThermalTransient_EffectiveMatrixRefusals` |
| `kAnchorTolerance` | `ThermalSolve.hpp` | 1e-10 | relative, `|row sum| > tol sum |row|` | `ThermalSolve_AnchorToleranceSeparatesRoundingFromAnchoring` (the range `1e-14 < tol < 1e-8`; a weak 1e-6 anchor accepted, 3e-16 noise refused) |
| `ThermalSolverSettings::relativeResidualTolerance` | `ThermalData.hpp` (canonical, user-editable, persisted) | 1e-9 | dimensionless backward error `||r|| / (||K|| ||T|| + ||b||)` | `ThermalData_SolverSettingsAreValidatedFiniteAndPositive`, `ThermalCommand_SolverSettingsUndoRedoExact` (pins the default), `ThermalValidation_ResidualIsJudgedAgainstTheCanonicalToleranceAndNoOther`; `architecture.validation-ownership` forbids any other 1e-9 to 1e-16 literal in the validator |
| `ThermalSolverSettings::pivotFloor` | `ThermalData.hpp` | 1e-12 | dimensionless `min |D| / max |D|` | `ThermalSolve_FactorisationSingularityIsIndependentOfAnchoring`, `ThermalValidation_RejectsAPivotRatioBelowTheCanonicalFloor`; the default is now pinned by `ThermalSolve_TheSolverSettingDefaultsAreTheKernelDefaults` |
| `linalg::SymmetricSolveSettings` defaults | `SymmetricSolve.hpp` | 1e-9, 1e-12 | same | the same new test fails the day the kernel's, the thermal record's or (by the third copy in `StructuralData.hpp`) the structural record's value moves alone |
| `kTimeGridTolerance` | `ThermalTransient.hpp` | 1e-9 | relative on `t_end / dt` | `ThermalTransient_TimeGridFloatingPointRatioIsAnInteger`, `_TimeGridToleranceBoundary`, `_TimeGridSaysWhichSpanItIntegrates` |
| `kMaxTimeSteps`, `kMaxStoredValues` | `ThermalTransient.hpp` | 1 000 000, 50 000 000 | counts; resource guards, not accuracy | `ThermalTransient_TimeGridResourceGuardNeverEnlargesTheStep`, `_AlgebraRefusals` |
| `kBalanceAbsoluteToleranceW`, `kBalanceRelativeTolerance`, `kBalanceScaleFloorW` | `ThermalBalance.hpp` | 1e-12 W, 1e-13, 1e-12 W | W; relative to gross plus arithmetic scale | `ThermalBalance_TolerancePolicyConstantsArePinned`, `_MeasuredTolerancePolicy`, `_LargeCancellationScale`; mutation QP28 (a thousand times looser) killed |
| `kThermalRadiusRatioFloor`, `kThermalAspectRatioCeiling`, `kThermalGradientAccuracy` | `ThermalValidation.hpp` | 7.0e-6, 700, 1.0e-9 | dimensionless mesh-quality bounds, from measured laws | `ThermalValidation_PolicyConstantsArePinned`, `_TheBoundsGuaranteeTheStatedGradientAccuracy`, `_TheEnvelopeBoundsAreTheMeasuredWorstCase`, and now `_AMeshAtTheEdgeOfTheAcceptedBoundsStillSolvesAndBalances` |
| mesh-quality WARNING bounds (3.0e-4, 81, 6.0e-3, 3.125) | `ThermalValidation.cpp` (anonymous literals) | | dimensionless and radians | `ThermalValidation_PolicyConstantsArePinned`; the numbers are repeated in the header comment table |
| exact-equality "tolerances" | `ThermalDof.cpp`, `ThermalBoundary.cpp`, `ThermalSolve.cpp`, `ThermalTransient.cpp` | `==` | K | `ThermalBC_ConflictUsesExactCanonicalEquality`, `ThermalSolve_ConstraintViolationIsAHardFailure`; intentional: no tolerance on user intent |

## Ownership outside `src/thermal`

| layer | what it defines | gate |
|---|---|---|
| CLI | no tolerance, epsilon, threshold or default; the help text gives the example `1e5W/m^3`; it prints the tolerances it reads from the canonical definition | `architecture.thermalcli-ownership` rule 4 (no floating literal in any `Thermal*` CLI file), rule 17 (no `Tolerance`, `pivotFloor`, `kBalance`, `kThermal` in the run entry), their negative fixtures |
| renderer | only display constants: `kThermalArrowBarbFraction` 0.25, `kThermalArrowBarbRadians`, the colour ramp stops | `architecture.thermalviz-ownership` (physics names and unit literals forbidden) |
| persistence | writes and reads the canonical pair (`relative_residual_tolerance`, `pivot_floor`), defines none | `ThermalAnalysisFileTests` (a file-loaded 1e-8 and 1e-12 round-trips), `ThermalReference_Persistence_SaveLoadReSolve` |
| P16 | measures, states no opinion: `reportOnlyThresholds()` classifies nothing | `ThermalValidation_PolicyIsAcceptedByP16sOwnPolicyChecker`; `src/meshing` contains no `thermal` |
| the reference suite | its own comparison tolerances (`kClassA`, `kClassB`, `Tolerance{...}`), which judge production and are not production's | the **reference tolerance fingerprint** of P18-REFMOD-001, unchanged |

## What this milestone found and did

* **D6** the symmetry tolerance and the effective-matrix tolerance are two literals with one value; the three copies of the solver defaults are three places. Both are anonymous or private, so the
  defaults are now pinned together by a test and the symmetry pair is recorded (`LIMITATIONS.md`). No value was changed; **no tolerance was loosened, no diagnostic downgraded.**
* **D-E** the solver gate (1e-9) is four orders looser than the balance gate (|B| <= 1e-12 W + 1e-13 scale): measured at the last accepted shape of both degeneration families the solve error is 2.6e-13 and
  7.8e-13 and the worst balance residual 4.8e-11 and 0, so a solve inside the solver gate is not refused by the balance in practice (`BALANCE.md`).
* **D3** the anchor classifier is row-relative; the cliff between the tolerance and the noise is documented, not probed (`STEADY_SOLVER.md`).
* The balance formula `abs + rel (gross + arithmetic)` is written inline in three places; the verdict itself calls `balanceWithinTolerance`. Recorded.
