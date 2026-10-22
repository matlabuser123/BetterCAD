# P18-VALID-001 — mutation testing

```text
PROBES:      78   (75 in the first campaign, 3 added in the re-run; 6 of the 75 were re-run because the first run did not give a clean kill)
RESULT:      78 KILLED   (72 by a failing test, 6 by an abort of the mutated program)
SURVIVORS:   0 at the end; 3 in the first campaign, all three test gaps, closed
VOID:        0 at the end; 2 in the first campaign (a mutation that did not compile), corrected
SOURCE:      restored after every probe; SHA-256 of ThermalValidation.cpp and ThermalValidation.hpp equal before the campaign, after it and after the re-run
FILTER:      the whole family, [thermal][validation]   (130 cases in the first campaign, 138 in the re-run)
```

A mutation is a one-line change of the production source that removes or inverts one guard. The program is rebuilt and the whole validation family is run; the verdict is read from the
test output (never from an exit code): **KILLED** when at least one case fails, **SURVIVED** when everything passes. A mutated program that aborts on a library assertion before it
prints a summary is classed **KILLED (abort)**: it was detected, and the abort is the proof that the removed guard protects against undefined behaviour. The driver, the re-run
script, the single-probe harness, the progress files and the hashes are in `qualification/mutations/`.

## The brief's list of mutations, and the probes that carry each

| Brief | Probes | Carried by |
| --- | --- | --- |
| accept stale source if dimensions match | VAL19–VAL25, VAL67–VAL69 | `RejectsSameCountsNewMeshSource`, the currentness matrices |
| accept same IDs as same mesh generation | VAL20, VAL24, VAL25, VAL26, VAL28, VAL76 | `RejectsSameIdsNewMeshSource` |
| invert fatal/warning mesh-quality severity | VAL01, VAL02, VAL05 | `RejectsAnInvertedTetrahedron`, `RejectsStaleMesh`, `ThermalMeshQualityReject` |
| require ρ/cp for steady / omit it for transient | VAL13 / VAL14 | `SteadyRequiresConductivityOnly` / `TransientRequiresConductivityDensitySpecificHeat` |
| require at least one load / one boundary condition | VAL15 / VAL16 | `NoLoadsIsNotAnError`, `TransientPureNeumannModelPassesEndToEnd` |
| allow pure-Neumann steady / reject pure-Neumann transient | VAL29 / VAL30 | `SteadyPureNeumannModelFailsEndToEnd` / `TransientPureNeumannModelPassesEndToEnd` |
| skip the residual gate | VAL64, VAL65, VAL34, VAL35, VAL59, VAL60 | `APerturbedFreeTemperatureFailsTheResidualAlone`, `ABalanceThatCancelsDoesNotRescueAFailedResidual` |
| skip the balance gate | VAL66, VAL61, VAL38, VAL39 | `ACorruptedBalanceFailsTheBalanceAlone` |
| use residual OR balance | VAL64 + VAL66 | each gate is wired alone and each has its own case in which the other one passes (`ABalanceThatCancels…`, `AResidualThatPasses…`); an OR would let one of those pass |
| treat N_free = 0 as failure | VAL31 | `FullyConstrainedSteadyPasses`, `FullyPrescribedSteadyModelPassesOnARealMesh` |
| skip `isfinite(T)` / `isfinite(q)` | VAL42, VAL63 / VAL62 | `ANotANumberTemperatureFailsTheFinitenessGateAndTheResidual` / `ANotANumberHeatFluxFailsThePostGate…` |
| skip POST currentness / BALANCE currentness | VAL24, VAL68 / VAL25, VAL69 | the currentness matrices |
| GUI overrides report severity | no probe: there is no field to mutate | 33 compile-fail cases (`A_FINDING_HAS_NO_OVERRIDE`, `THERE_IS_NO_SEVERITY_SETTER`, …) and the `validation-gui-decides` gate fixture |
| duplicate balance tolerance in VALID | VAL38, VAL52 | `BalanceIsJudgedByBalancesOwnTolerancePolicy`, `TransientResidualsUseTheCanonicalToleranceToo`, and the `validation-second-tolerance` gate fixture |
| unordered map for validation rules | VAL58 (ordering) | `ReportOrderingAndContentsAreDeterministic`, and the `validation-unordered` gate fixture |
| overall status stored independently of the checks | VAL71, VAL75 | `requireWellFormed` on every report; compile-fail `THE_REPORT_HAS_NO_STORED_STATUS` |

## All probes

| Probe | Mutation | First run | Final | Evidence |
| --- | --- | --- | --- | --- |
| VAL01 | an inverted/degenerate tetrahedron is downgraded to a warning | KILLED | **KILLED** | 2 of 130 cases failed |
| VAL02 | a stale mesh is downgraded to information | KILLED | **KILLED** | 4 of 130 cases failed |
| VAL03 | a value on a bound is on the BAD side (non-strict comparison) | KILLED | **KILLED** | 1 of 130 cases failed |
| VAL04 | a non-finite worst metric is no longer an error | KILLED | **KILLED** | 1 of 130 cases failed |
| VAL05 | the failure bound is judged against the warning bound (warnings become errors) | no summary (abort) | **KILLED (abort)** | aborted: `optional::operator*` assertion (the mutated comparison dereferences an absent failure bound) |
| VAL06 | the aspect-ratio ceiling is removed | KILLED | **KILLED** | 6 of 130 cases failed |
| VAL07 | the radius-ratio floor is removed | KILLED | **KILLED** | 8 of 130 cases failed |
| VAL08 | the count of elements past the failure bound is reported as 1 | KILLED | **KILLED** | 1 of 130 cases failed |
| VAL09 | the worst value is taken from the wrong end of the summary | KILLED | **KILLED** | 2 of 130 cases failed |
| VAL10 | a structurally invalid mesh is not reported | KILLED | **KILLED** | 2 of 130 cases failed |
| VAL11 | invalid elements are not reported | SURVIVED | **KILLED** | re-run VAL11r after the test was added or the probe corrected: 1 of 138 cases failed |
| VAL12 | a contradictory reporting policy is silently ignored | KILLED | **KILLED** | 1 of 130 cases failed |
| VAL13 | a steady analysis requires rho and cp | KILLED | **KILLED** | 2 of 130 cases failed |
| VAL14 | a transient analysis does not require rho and cp | KILLED | **KILLED** | 2 of 130 cases failed |
| VAL15 | at least one load is required | KILLED | **KILLED** | 15 of 130 cases failed |
| VAL16 | at least one boundary condition is required | KILLED | **KILLED** | 3 of 130 cases failed |
| VAL17 | an unresolved load is not reported | KILLED | **KILLED** | 2 of 130 cases failed |
| VAL18 | an unresolved or conflicting boundary condition is not reported | KILLED | **KILLED** | 4 of 130 cases failed |
| VAL19 | a stale assembled system is accepted | KILLED | **KILLED** | 4 of 130 cases failed |
| VAL20 | a numbering of another mesh generation is accepted | KILLED | **KILLED** | 1 of 130 cases failed |
| VAL21 | stale prepared loads are accepted | KILLED | **KILLED** | 2 of 130 cases failed |
| VAL22 | a stale prepared boundary is accepted | KILLED | **KILLED** | 2 of 130 cases failed |
| VAL23 | a stale steady solution is accepted | KILLED | **KILLED** | 3 of 130 cases failed |
| VAL24 | a stale steady recovered result is accepted | KILLED | **KILLED** | 3 of 130 cases failed |
| VAL25 | a stale steady balance is accepted | KILLED | **KILLED** | 3 of 130 cases failed |
| VAL26 | a solution from another assembled system is accepted | SURVIVED | **KILLED** | re-run VAL26r after the test was added or the probe corrected: 1 of 138 cases failed |
| VAL27 | a steady solution for a transient analysis is not a mode mismatch | not applied (find text) | **KILLED** | re-run VAL27r after the test was added or the probe corrected: 1 of 138 cases failed |
| VAL28 | recovered fields of another solution are accepted | SURVIVED | **KILLED** | re-run VAL28r after the test was added or the probe corrected: 1 of 138 cases failed |
| VAL29 | a pure-Neumann steady model is accepted | KILLED | **KILLED** | 7 of 130 cases failed |
| VAL30 | the steady datum rule is applied to a transient | KILLED | **KILLED** | 7 of 130 cases failed |
| VAL31 | no free equation (fully constrained) is a failure | did not compile | **KILLED** | re-run VAL31r after the test was added or the probe corrected: 2 of 138 cases failed |
| VAL32 | a missing transient capacity is not noticed | no summary (abort) | **KILLED (abort)** | aborted: `optional::operator->` assertion (the absent capacity is dereferenced) |
| VAL33 | a zero capacity on the diagonal is accepted | KILLED | **KILLED** | 1 of 130 cases failed |
| VAL34 | the recorded physical step residual is not judged | KILLED | **KILLED** | 1 of 130 cases failed |
| VAL35 | the recorded reduced step residual is not judged | KILLED | **KILLED** | 1 of 130 cases failed |
| VAL36 | the nominal step size is used where the actual one belongs | KILLED | **KILLED** | 4 of 130 cases failed |
| VAL37 | no step is ever re-evaluated independently | KILLED | **KILLED** | 5 of 130 cases failed |
| VAL38 | the balance tolerance is not applied to the numbers | KILLED | **KILLED** | 6 of 130 cases failed |
| VAL39 | a balance state that records itself out of tolerance is accepted | KILLED | **KILLED** | 1 of 130 cases failed |
| VAL40 | a non-finite balance term falls through to the comparison | KILLED | **KILLED** | 1 of 130 cases failed |
| VAL41 | the number of balance states is not checked | KILLED | **KILLED** | 1 of 130 cases failed |
| VAL42 | the finiteness of the temperatures is not checked | KILLED | **KILLED** | 4 of 130 cases failed |
| VAL43 | a state of the wrong size is read anyway | KILLED | **KILLED** | 1 of 130 cases failed |
| VAL44 | recovered temperatures are not compared with the solver's | KILLED | **KILLED** | 1 of 130 cases failed |
| VAL45 | a missing element result is not a cardinality failure | did not compile | **KILLED (abort)** | first run did not compile (unused variable under `-Werror`); corrected probe VAL45r aborted: `vector::operator[]` assertion, the guarded out-of-range read |
| VAL46 | a recorded solve failure is ignored | no summary (abort) | **KILLED (abort)** | 7 checks failed, then aborted: `vector::operator[]` assertion |
| VAL47 | an absent solution is accepted | no summary (abort) | **KILLED (abort)** | aborted: `vector::back()` assertion on an empty history |
| VAL48 | the history contract is not enforced | KILLED | **KILLED** | 1 of 130 cases failed |
| VAL49 | the steady pivot floor is ignored | KILLED | **KILLED** | 2 of 130 cases failed |
| VAL50 | the transient pivot floor is ignored | KILLED | **KILLED** | 1 of 130 cases failed |
| VAL51 | a transient balance is expected to have one state | KILLED | **KILLED** | 6 of 130 cases failed |
| VAL52 | a second residual tolerance in the transient check | KILLED | **KILLED** | 2 of 130 cases failed |
| VAL53 | a failed transient setup no longer blocks the derived rules | KILLED | **KILLED** | 1 of 130 cases failed |
| VAL54 | a source-compatibility failure no longer blocks well-posedness | KILLED | **KILLED** | 1 of 130 cases failed |
| VAL55 | a well-posedness failure no longer blocks the solve rules | no summary (abort) | **KILLED (abort)** | 2 checks failed (`SolverResidual` not skipped, wrong blocker), then stopped |
| VAL56 | a report under another policy is still current | KILLED | **KILLED** | 1 of 130 cases failed |
| VAL57 | the recorded attribution check is skipped | KILLED | **KILLED** | 2 of 130 cases failed |
| VAL58 | the findings are not ordered by rule first | KILLED | **KILLED** | 21 of 130 cases failed |
| VAL59 | the steady residual is judged against a tolerance 1e12 too loose | KILLED | **KILLED** | 6 of 130 cases failed |
| VAL60 | the transient step is re-evaluated against a tolerance 1e15 too loose | KILLED | **KILLED** | 5 of 130 cases failed |
| VAL61 | the balance attribution is not wired into the decision | KILLED | **KILLED** | 1 of 130 cases failed |
| VAL62 | the recovered fields are not judged | KILLED | **KILLED** | 1 of 130 cases failed |
| VAL63 | the temperature finiteness is not wired into the decision | KILLED | **KILLED** | 1 of 130 cases failed |
| VAL64 | the steady residual verdict is not wired into the decision | KILLED | **KILLED** | 3 of 130 cases failed |
| VAL65 | the transient residual verdict is not wired into the decision | KILLED | **KILLED** | 1 of 130 cases failed |
| VAL66 | the balance verdict is not wired into the decision | KILLED | **KILLED** | 2 of 130 cases failed |
| VAL67 | a stale transient solution is accepted | KILLED | **KILLED** | 1 of 130 cases failed |
| VAL68 | stale transient recovered fields are accepted | KILLED | **KILLED** | 1 of 130 cases failed |
| VAL69 | a stale transient balance is accepted | KILLED | **KILLED** | 1 of 130 cases failed |
| VAL70 | the FinalOnly explanation is dropped | KILLED | **KILLED** | 1 of 130 cases failed |
| VAL71 | a skipped rule counts as a pass in the overall status | KILLED | **KILLED** | 1 of 130 cases failed |
| VAL72 | accepted() refuses a warning | KILLED | **KILLED** | 2 of 130 cases failed |
| VAL73 | solveAccepted() ignores the temperature finiteness | KILLED | **KILLED** | 1 of 130 cases failed |
| VAL74 | errorCount() counts warnings too | KILLED | **KILLED** | 4 of 130 cases failed |
| VAL75 | a warning makes the overall status Fail | KILLED | **KILLED** | 2 of 130 cases failed |
| VAL76 | a balance of another solution is accepted | (new in the re-run) | **KILLED** | 1 of 138 cases failed |
| VAL77 | a history from another assembled system is accepted | (new in the re-run) | **KILLED** | 1 of 138 cases failed |
| VAL78 | prepared loads of another list than the system are accepted | (new in the re-run) | **KILLED** | 1 of 138 cases failed |

## The probes that did not give a clean kill the first time

* **VAL11, VAL26, VAL28 survived.** Each guarded a diagnostic that the tests reached only through another path: `invalidElements > 0` was exercised through the structural and the non-finite
  findings, which report the same condition; the system–solution and solution–post relations were asserted as "a `SourceMismatch` exists", which the prepared-loads comparison also
  produces. They were test gaps, not equivalent mutants, and they are closed by `AnInvalidElementCountFromP16IsAnErrorOnItsOwn` and by per-subject assertions (`mismatchOn`) for the
  solution, the prepared loads, the post result, the balance and a transient history. Three probes the first campaign lacked for the same relations (VAL76, VAL77, VAL78) were added and killed.
* **VAL27 was not applied** (its find text did not match when the driver ran) and **VAL31, VAL45 did not compile** (a quoting error that cut a string literal; an unused variable
  under `-Werror`). All three were corrected and re-run: VAL27 and VAL31 are killed by tests that already existed; VAL45 aborts in `vector::operator[]`.
* **VAL05, VAL32, VAL46, VAL47, VAL55 gave no summary line.** The mutated program stopped on a `_GLIBCXX_ASSERTIONS` check (an absent `optional` or an empty `vector` dereferenced) or, for
  VAL55, after two failed checks. A program that aborts has been detected; they are recorded as **KILLED (abort)** and the reason is in the table.

## What was not mutated, and why

* The branch that reports `MeshMissing` when the mesh is current but holds no quality report: P16 guarantees a quality report for a held mesh and `requireThermalModel` checks it, so
  the branch is a defence against a bug upstream and cannot be reached from a test without breaking P16. It is kept, commented as unreachable, and has no probe.
* Message text, which carries no verdict.
* Constants of the thermal mesh policy other than the two failure bounds and the strictness of the comparison: the warning bounds are pinned by `PolicyConstantsArePinned` and by the
  policy fingerprint, and the measured laws that fix them are reproduced by `ThermalValidationLawTests.cpp`.
