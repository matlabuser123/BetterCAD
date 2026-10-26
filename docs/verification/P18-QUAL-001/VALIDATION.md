# P18-QUAL-001 - validation audit (brief sections 49-52)

**Verdict: one place decides acceptance; the report is derived; a skipped rule is never a pass; the thermal mesh bounds are measured, not chosen.**
Row-by-row: `audit/AUDIT_C_post_balance_validation_viz_commands_persistence_cli.md` section C3 (10 rows: 9 COVERED, 1 PARTIAL, now closed) and ADR-053 in `audit/AUDIT_A_*` (14 clauses, all COVERED).

| property | where | tests |
|---|---|---|
| `validateThermalAnalysis` is the only place a thermal analysis is accepted | `ThermalValidation.cpp`; gate `CheckValidationOwnership.cmake` (6 rules, each with a negative fixture) | `architecture.validation-ownership` and its fixtures `gui-decides`, `reruns`, `second-tolerance`, `lacks-owners`, `unordered`, `second-owner`; `ThermalRun_AcceptedIsACompletedChainAndTheValidatorsVerdict` |
| the report is derived: status, acceptance and counts are computed on every call; nothing stores "valid" | `status()` / `accepted()` / `solveAccepted()` / `inputsAccepted()` | `ThermalValidation_StatusAndCountsAreDerivedFromTheChecksAndFindings`, compile-fail `the-report-has-no-stored-status`, `-no-valid-flag`, `-no-is-validated-flag`, `-no-status-setter` |
| severity is a function of the finding's code only | `severityOf(code)` | `ThermalValidation_SeverityIsAFunctionOfTheCodeAndNothingElse` (exact warning list, exact information list, eleven named fatal codes), compile-fail `a-finding-has-no-override` |
| a skipped rule is never a pass and names its blocker | `status()`: `Skipped` => `Fail` | `ThermalValidation_ASkippedRuleIsNeverAPass`, `_ARecoveryOrABalanceThatWasNeverPresentedFailsItsOwnRule` |
| the rules run in a deterministic order (21 rules; transient setup is `NotApplicable` for steady) | `kValidationRules` | `ThermalValidation_EveryRuleHasAStableIdAndGroupInTheOrderItRuns`, `_ReportOrderingAndContentsAreDeterministic`, `_PrimaryFailureReasonDoesNotDependOnTheOrderOfTheDocument` |
| tolerances are consumed, not copied: one residual tolerance (the canonical setting) and one balance policy | rules 3 and 4 of the gate | `ThermalValidation_ResidualIsJudgedAgainstTheCanonicalToleranceAndNoOther`, `_TransientResidualsUseTheCanonicalToleranceToo`, `_BalanceIsJudgedByBalancesOwnTolerancePolicy` |
| **mesh bounds from measured laws**: radius ratio `3r/R >= 7.0e-6`, aspect `<= 700`, gradient accuracy `1e-9`; dihedral angles have no failure bound; warnings at the qualified envelope | `ThermalValidation.hpp:94,106,109` | `ThermalValidation_PolicyConstantsArePinned`, `_MeasuresHowGradientAccuracyDegradesWithElementShape`, `_TheBoundsGuaranteeTheStatedGradientAccuracy`, `_TheEnvelopeBoundsAreTheMeasuredWorstCase`, `_AcceptsEveryQualifiedReferenceMeshWithoutAFinding`, `_ComparesStrictlySoAValueOnTheBoundIsOnTheGoodSide` |
| P16 measures and states no opinion; thermal thresholds live only in thermal | `meshing::reportOnlyThresholds()` classifies nothing; `src/meshing` contains no `thermal` | `ThermalValidation_PolicyIsAcceptedByP16sOwnPolicyChecker`, `_RejectionByThermalPolicyDoesNotFailP16GlobalMeshQuality` |
| independence: a bad residual and a good balance fail; a good residual and a bad balance fail | `judgeSolveEvidence` is public so corrupted data meets the same decision | see `BALANCE.md` |
| a stale report is not current; edit-back does not revive it | `isCurrent(report, ...)` compares `ThermalResultSource` (analysis id and **revision**) and the policy fingerprint | `ThermalValidation_EveryEditStalesExactlyWhatDependsOnIt`, `_InputEditStalesTheReportButAnUnrelatedEditDoesNot`, `ThermalRun_AnEditAfterARunMakesItStaleAndAFailedRerunPublishesNothing` |
| the validator is read-only | const-reference API; gate rule 2 forbids solve/assemble/recover/balance tokens | `ThermalValidation_ValidationIsReadOnly`, `_FailedResolveDoesNotRevalidateOldResult` |

## Defect found and fixed by this milestone: D-F, an unjudged mesh metric passed silently

`classifyThermalMesh` skipped a classifiable quality metric whose P16 summary was absent or had `count == 0`, and never compared a summary's `count`
with the mesh's `tetCount`. P16 always summarises every metric over every tetrahedron, so the situation cannot arise today - but the validator did not
defend the invariant, so a P16 regression would have **accepted a mesh with an element nobody judged**. The classifier now emits a structural `Error`
finding (`MeshHasInvalidElements`, naming the metric: "the {} was measured for {} of the mesh's {} element(s), so the mesh cannot be judged") when
`quality.tetCount > 0` and any of the four classifiable metrics has no summary or a summary over fewer elements.
`ThermalValidation_AMissingOrShortQualitySummaryIsNotAPass` covers an absent summary, a count of zero, a short count (all four metrics) and the
no-element mesh (not accused of an unmeasured metric - other rules refuse an empty mesh). It **fails against the previous classifier**: with
`ThermalValidation.cpp` and `ThermalAnalysisJson.cpp` both reverted to `HEAD`, this test and the persistence test of finding D-A both fail (2 of 2 test cases,
11 failed assertions; `repro-old-code.txt`, `FINDINGS.md`). The shared test helper
`reportWithOneSummary` now fills all four metrics with benign values so the metric under test is the only one that can fail.

## Notes

* A severity-WARNING finding exits 0 through the CLI and writes `PASS_WITH_WARNINGS`; a small real document cannot produce one, so the payload is covered by
  `ThermalCli_ValidationJsonCarriesAWarningReportWithEveryOptionalFieldItHolds` (a hand-built report through the production writer; every optional field).
* `ThermalValidationReport` is a plain aggregate with public mutable `checks` and `findings`; `status()` re-derives from them, and the tests mutate them to prove it
  (D9, `LIMITATIONS.md`). `accepted()` ignores the report's currency by design; a consumer must also call `isCurrent`.
* **D-D** the default `FinalOnly` transient output cannot be accepted beyond one step (`TRANSIENT_SOLVER.md`).
