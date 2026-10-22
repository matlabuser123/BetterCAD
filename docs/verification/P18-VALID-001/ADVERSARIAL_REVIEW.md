# P18-VALID-001 — adversarial review

```text
QUESTIONS:  35 (the brief's list, verbatim in meaning)
METHOD:     for each, the attack that would make the answer "yes", the test that shows it does not, and the mutation that proves the test would notice
DEFECTS:    found in the review itself: 3 test gaps (all closed), 1 missing code (closed); production defects that changed a verdict: 0
```

Each answer names the test that carries it. A mutation column names the probe of MUTATIONS.md that removes the guard; "KILLED" means the named tests fail when it is removed.

| # | Attack | Answer | Evidence | Mutation |
| --- | --- | --- | --- | --- |
| 1 | P16 mesh quality becomes thermal acceptance authority | **No.** `reportOnlyThresholds()` classifies nothing and P16 is untouched; the thermal bounds live in `thermalQualityThresholds()` and are applied by `classifyThermalMesh` | ownership gate rules 1 and 2; FREEZE.md untouched list; `ThermalMeshQualityReject`, `ThermalMeshQualityWarning` | VAL05–VAL07 |
| 2 | VALID recomputes mesh metrics differently from P16 | **No.** It reads P16's per-metric summaries and per-element records and calls `meshing::direction` and `unitOf`; no metric formula exists in the file | gate rule 2 (`evaluateTetQuality`, `evaluateMeshQuality` forbidden); `ReusesP16Metrics` | — (gate fixture `validation-reruns`) |
| 3 | An inverted tetrahedron is warning-only | **No.** Structural invalidity is an Error of rule `TV_MESH_STRUCTURAL`; no sign flip, no `|detJ|` | `RejectsAnInvertedTetrahedron`, `RejectsADegenerateTetrahedron` | VAL01 killed |
| 4 | A stale mesh passes because the counts match | **No.** Currentness is identity (`MeshStamp`, geometry revision, intent), never sizes | `RejectsSameCountsNewMeshSource` (same nodes, same tetrahedra, same NodeIds, different generation), `RejectsStaleMesh` | VAL02, VAL19–VAL25 |
| 5 | A new mesh with the same NodeIds accepts the old result | **No.** Each derived object is compared by source; the old post result and balance are named stale one by one | `RejectsSameIdsNewMeshSource` (post alone, balance alone, solution alone) | VAL20, VAL26, VAL28, VAL76 |
| 6 | Old T is combined with a new material | **No.** The solution's source carries the material revision | `EveryEditStalesExactlyWhatDependsOnIt` (material edit row), `EveryTransientEditStalesTheHistory` (specific-heat edit) | VAL19–VAL25, VAL67–VAL69 |
| 7 | A steady analysis wrongly requires ρ and cp | **No.** The material rule takes the mode | `SteadyRequiresConductivityOnly` | VAL13 killed |
| 8 | A transient accepts a missing ρ or cp | **No.** | `TransientRequiresConductivityDensitySpecificHeat` | VAL14 killed |
| 9 | A model with no load is rejected because the load count is 0 | **No.** `NoLoadsConfigured` is Information | `NoLoadsIsNotAnError`, `AZeroMagnitudeLoadIsValid` | VAL15 |
| 10 | A transient insulated model is rejected because the BC count is 0 | **No.** `NoBoundaryConditionsConfigured` is Information; the capacity makes it well posed | `TransientPureNeumannModelPassesEndToEnd` | VAL30 killed |
| 11 | A steady Robin-only model is rejected because there is no Dirichlet | **No.** A positive convection coefficient anchors | `SteadyRobinOnlyModelPassesEndToEnd`, `SteadyRobinOnlyPasses` (rounding is not an anchor) | VAL29 killed |
| 12 | A pure-Neumann steady model passes because the solver returned finite arbitrary T | **No.** It fails `NoTemperatureDatum` before any solve is judged | `SteadyPureNeumannModelFailsEndToEnd`, `SteadyPureNeumannFails`, `TheValidatorAndTheSolverAgreeOnWhatIsSingular` | VAL29 killed |
| 13 | A pure-Neumann transient is rejected by the steady singularity policy | **No.** The rule is chosen by mode | `TransientPureNeumannModelPassesEndToEnd`, `TransientPureNeumannPasses` | VAL30 killed |
| 14 | Disconnected, anchored components are rejected unconditionally | **No.** Only an unanchored region is refused, and only in a steady analysis | `DisconnectedAllAnchoredSteadyPasses`, `DisconnectedUnanchoredSteadyFails`, `TransientDisconnectedComponentsWithCapacityPass` | VAL29 |
| 15 | Solver success bypasses residual acceptance | **No.** The residual is recomputed from the system and the temperatures by the canonical qualified function; a success flag is not an input | `APerturbedFreeTemperatureFailsTheResidualAlone` (finite, plausible, wrong) | VAL64, VAL65 |
| 16 | A balance PASS rescues a residual FAIL | **No.** Independent gates, no `OR` | `ABalanceThatCancelsDoesNotRescueAFailedResidual`: \|B\| 6.2e-14 W (`balanced`) with a free residual of 1.545e-4 against 1e-9 → overall FAIL | VAL64 |
| 17 | A residual PASS rescues a balance FAIL | **No.** | `ACorruptedBalanceFailsTheBalanceAlone`, `AResidualThatPassesDoesNotRescueABalanceThatFailsOnTheModel` | VAL66 |
| 18 | The constrained residual is mistaken for the free residual | **No.** A prescription that is not held is its own code `ConstraintNotHeld`; the residual is the reduced free residual | `RejectsAPrescribedTemperatureThatIsNotHeld` | — (a seam test; no probe) |
| 19 | N_free = 0 is treated as a residual failure | **No.** Nothing to anchor, nothing to solve: the model passes | `FullyConstrainedSteadyPasses`, `FullyPrescribedSteadyModelPassesOnARealMesh`, `FullyConstrainedSteadyResidual…` | VAL31r |
| 20 | A NaN metric falls through the comparisons into PASS | **No.** A non-finite worst metric is an Error before any comparison | `ANotANumberMetricIsInvalidAndNeverFallsThroughAComparison` (NaN, +inf, −inf, all four metrics) | VAL04 killed |
| 21 | A non-finite flux is ignored because the temperature is finite | **No.** The recovered fields are their own rule | `ANotANumberHeatFluxFailsThePostGateEvenWhenTheTemperatureAndTheBalanceAreFine`, `RejectsNonFiniteGradient`, `RejectsNonFiniteHeatFlux` | VAL62 |
| 22 | POST is stale while the solve is current and still gets full acceptance | **No.** `RejectsSameIdsNewMeshSource` presents a fresh solution with the old post alone | same | VAL24, VAL28 |
| 23 | BALANCE is stale and its old good metric still counts | **No.** | same, balance alone | VAL25, VAL76 |
| 24 | A warning becomes fatal in a GUI while the core says pass | **No.** Severity is `severityOf(code)`; the finding has no field a viewer could write, and the status is derived | compile-fail `A_FINDING_HAS_NO_DISPLAY_SEVERITY`, `A_FINDING_HAS_NO_OVERRIDE`, `THERE_IS_NO_SEVERITY_SETTER`; gate rule 1 | VAL71, VAL75 |
| 25 | Fatal validation is downgraded to a warning by the UI | **No.** Same | same | VAL72–VAL75 |
| 26 | The validator reruns the solver and produces a second result | **No.** It takes views of derived objects; it has no solver to call | `ValidationIsReadOnly` (stamp, revision, system, solution, fields and balance unchanged by three validations); compile-fail `THE_DERIVED_STATE_HAS_NO_SOLVER`; gate rule 2 | — |
| 27 | The validator reimplements load, Robin or flux formulas | **No.** | gate rule 2 forbids the names of every one; `validation-reruns` fixture | — |
| 28 | Acceptance depends on unordered diagnostic iteration | **No.** Fixed rule order, stable sort on a key without addresses, no unordered container | `ReportOrderingAndContentsAreDeterministic`, `RepeatedReportsOfEveryClassAreStable`, `PrimaryFailureReasonDoesNotDependOnTheOrderOfTheDocument`; gate rule 5 | VAL58 |
| 29 | A report stays current after a load, material or mesh mutation | **No.** Currentness is the source plus the policy fingerprint | `InputEditStalesTheReportButAnUnrelatedEditDoesNot`, `isCurrent` in the stale-mesh tests | VAL56 |
| 30 | A visualisation colour map invalidates validation, or the reverse | **No.** The report source has no display field; P18-VIZ-001 is not started | compile-fail `THE_REPORT_SOURCE_HAS_NO_DISPLAY_SETTING` | — |
| 31 | A tolerance exists independently in SOLVE, BALANCE and VALID with conflicting values | **No.** Three policies, three owners, consumed not copied | GUI_OWNERSHIP.md table; gate rule 3; `ResidualIsJudgedAgainstTheCanonicalToleranceAndNoOther`, `BalanceIsJudgedByBalancesOwnTolerancePolicy` | VAL38, VAL52, VAL59, VAL60 |
| 32 | The mesh thresholds are guessed | **No.** They are inverted from measured laws with a stated margin | `MeasuresHowGradientAccuracyDegradesWithElementShape`, `TheBoundsGuaranteeTheStatedGradientAccuracy`, MESH_ACCEPTANCE.md | VAL06, VAL07 |
| 33 | The warning/fail boundary compares inconsistently (`>` against `>=`) | **No.** One function, strict on both bounds, and a value exactly on a bound is on the good side | `ComparesStrictlySoAValueOnTheBoundIsOnTheGoodSide` | VAL03 killed |
| 34 | Zero targeted tests still report PASS | **No.** The zero-match guard fails the build of the test plan when a filter finds fewer tests than its floor | `tests/cli/ZeroMatchGuard.cmake` (138 for the family, 15 group floors, 33 compile-fail, 8 architecture) | — |
| 35 | An overall verdict is stored independently of its checks | **No.** | compile-fail `THE_REPORT_HAS_NO_STORED_STATUS`, `…NO_VALID_FLAG`, `…NO_STATUS_SETTER`; `requireWellFormed` re-derives the status on every report any test produces | VAL71, VAL75 |

## Findings

Findings are things the review found wrong or unproved. None changed a verdict a user could have received; three were gaps in the proof and one was a gap in the model.

| # | Finding | Kind | Resolution |
| --- | --- | --- | --- |
| F1 | A balance could be handed to the validator that accounted for the right total and the wrong categories (one load too few, one convection condition too many) and still read as closed | **Model gap** | `BalanceAttributionMismatch` (the 50th code) and `checkBalanceAttribution`: the categories the balance accounted for must equal the categories the system was made from. Test: `ABalanceMissingACategoryFailsTheAttributionOnTheModel` |
| F2 | Mutation VAL11 (a P16 `invalidElements` count that is not reported) survived: the count was only exercised through the structural and non-finite paths, which report the same condition | **Test gap** | `AnInvalidElementCountFromP16IsAnErrorOnItsOwn` |
| F3 | Mutations VAL26 and VAL28 survived: the system–solution and solution–post relations were asserted only as "some `SourceMismatch` exists", which another subject also produced | **Test gap** | `mismatchOn(report, subject)` and assertions per subject for the solution, the prepared loads, the post result, the balance and a transient history |
| F4 | Five codes had no test that reaches them (`ControlNotFound`, `GeometryIneligible`, `MeshGenerationFailed`, `SystemMissing`, `SystemNotFinite`), and `InitialConditionOverriddenByPrescription` and `PostMissing` were only listed in a table | **Test gap** | eight tests (see README.md); every code in the enumeration now has an end-to-end or seam test that produces it |

Not a finding but recorded: the brief asks for Future Phases to be the last section of `TODO.md`. In the file as it stands (and at `HEAD`) three sections follow it. The order at `HEAD` is kept; reordering the file was not part of this milestone.

## What was not attacked, and why

* Multi-solid models: P16 meshes are single-solid (carried constraint 4); the disconnected-region rules are shown on synthetic systems through the same function the model-level decision calls.
* Timing: the validator is not measured for speed beyond the full-regression times; the brief sets no performance requirement for it.
* A hostile `ThermalDerivedState` built by hand from freed memory is outside the contract: the views are borrowed for the duration of the call, as the header states.
