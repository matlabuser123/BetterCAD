# P18-POST-001 — mutation probes

```text
SUBJECT:  43 distinct probes of src/thermal/ThermalPost.cpp, each applied to the production file, rebuilt, run against the 40 POST cases, and restored
RESULT:   42 killed, 1 void, 0 survived at the end; 3 survived the first pass and were closed by test additions, then killed on rerun
```

## Method

`mutate-t.ps1` (the harness every P18 milestone since P18-TRANSIENT-001 has used, unchanged) takes a name, a find string, a replacement and a Catch filter
(`[thermal][post]`, 40 cases): it applies the replacement to the file (refusing if the find text is not present), builds `bettercad_tests` in debug-ext, runs the
tests with a 240 s timeout, restores the original bytes in a `finally` block, and logs `VERDICT: KILLED | SURVIVED` from the **test output** (never from
`Start-Process`'s exit code, which can be empty). `BUILD FAILED` is VOID. The ids `PM01`–`PM40` are new for this milestone and collide with no earlier scratch log.

* The production file's SHA-256 before the first probe and after the last is the same (`B1EDADDE…05EE8`, in `qualification/mutations/post-premutation-hashes*.txt`).
* The probes ran against the source **before** two whitespace-only edits (the continuation line of the kernel's signature re-aligned in the header and the source, and a
  blank line between two test cases), neither of which changes a token; both are in the frozen tree.

## What the probes cover

| Family | Ids |
| --- | --- |
| the gather (sorted order, raw `NodeId`) | PM01, PM02, PM02b |
| the components (swaps) | PM03, PM04 |
| Fourier's law (sign, missing `k`, `k²`, one component) | PM05, PM06, PM07, PM08, PM30 |
| temperature in Celsius, a unit factor, the magnitude | PM09, PM10 (void), PM10b, PM11, PM12 |
| refusals (invalid element, non-finite gradient / flux / magnitude / temperature, below 0 K, cardinality, conductivity, numbering) | PM13–PM21, PM39, PM40 |
| currentness (mesh stamp vs counts, solution currentness, material cross-check, mode checks, stale reasons) | PM22–PM25, PM31, PM35–PM38 |
| transient order and times | PM26, PM27, PM28 |
| keying (raw element id, numbering of another mesh, state range) | PM29, PM29b, PM33, PM34 |
| smoothing (each element averaged with its predecessor) | PM32 |

## The table

| Id | Mutation | Verdict | Result | Cases that failed |
| --- | --- | --- | --- | --- || PM01 | the local node order is sorted by unknown index (not the tetrahedron's own order) | KILLED | 18 of 40 failed | AFailedRecoveryNeverMakesAnOldResultCurrent, AffineFieldGradientAndFourierFlux, FourierLawSign, GeneralTetIndependentGradientOracle, HeatFluxMagnitudeIsEuclideanAndKee... |
| PM02 | temperatures are read through the raw NodeId (minus one) and not through the thermal numbering | KILLED (assertion abort) | process aborted | (libstdc++ bounds assertion fired inside the suite) |
| PM02b | temperatures are read through the raw NodeId (minus one), clamped so the read stays in range | KILLED | 2 of 40 failed | NodalTemperatureUsesThermalDofOrderForSparseNodeIds, PreservesTetLocalNodeOrderInAMeshWhoseHandlesAreNotSorted |
| PM03 | the y and z gradient components are swapped | KILLED | 15 of 40 failed | AffineFieldGradientAndFourierFlux, GeneralTetIndependentGradientOracle, HeatFluxMagnitudeIsEuclideanAndKeepsTheVector, LargeMeshRecoveryIsExactForAnAffineFieldAndCountsMatch, Ther... |
| PM04 | the x and y gradient components are swapped | KILLED | 12 of 40 failed | AffineFieldGradientAndFourierFlux, FourierLawSign, GeneralTetIndependentGradientOracle, HeatFluxMagnitudeIsEuclideanAndKeepsTheVector, LargeMeshRecoveryIsExactForAnAff... |
| PM05 | the Fourier sign is dropped: q = +k grad(T) | KILLED | 17 of 40 failed | AffineFieldGradientAndFourierFlux, ConvectionIsNotPartOfTheConductiveFlux, FineCylinderRecoverySmoke, FourierLawSign, GeneralTetIndependentGradientOracle, ... |
| PM06 | the sign is dropped on the z component only | KILLED | 14 of 40 failed | AffineFieldGradientAndFourierFlux, ConvectionIsNotPartOfTheConductiveFlux, FineCylinderRecoverySmoke, GeneralTetIndependentGradientOracle, HeatFluxMagnitudeIsEuclidean... |
| PM07 | the conductivity is omitted: q = -grad(T) with k = 1 | KILLED | 13 of 40 failed | AffineFieldGradientAndFourierFlux, ConductivityScalingScalesTheFluxAndNotTheGradient, ConvectionIsNotPartOfTheConductiveFlux, FourierLawSign, GeneralTetIndependentGrad... |
| PM08 | the conductivity enters squared on y: q_y = -k^2 grad_y | KILLED | 12 of 40 failed | AffineFieldGradientAndFourierFlux, ConductivityScalingScalesTheFluxAndNotTheGradient, ConvectionIsNotPartOfTheConductiveFlux, GeneralTetIndependentGradientOracle, Heat... |
| PM09 | the stored steady temperatures are in Celsius | KILLED | 3 of 40 failed | ElementResultsAreKeyedByOrdinalAndNotByRawElementId, NodalTemperatureRemainsKelvinAndFollowsTheSolver, SteadySlabOnTheBlockFollowsFourierLaw |
| PM10 | the gradient is computed per millimetre (a stray 1e-3) | VOID (equivalent) | 0 failed | (none: the mutated value is computed and discarded) |
| PM10b | the gradient is computed per millimetre (a stray 1e-3 applied for real) | KILLED | 19 of 40 failed | AFailedRecoveryNeverMakesAnOldResultCurrent, AffineFieldGradientAndFourierFlux, FourierLawSign, GeneralTetIndependentGradientOracle, HeatFluxMagnitudeIsEuclideanAndKee... |
| PM11 | the magnitude is the L1 norm | KILLED | 5 of 40 failed | AffineFieldGradientAndFourierFlux, ElementResultsAreKeyedByOrdinalAndNotByRawElementId, FineCylinderRecoverySmoke, HeatFluxMagnitudeIsEuclideanAndKeepsTheVector, Rotat... |
| PM12 | the magnitude is the largest component only | KILLED | 5 of 40 failed | AffineFieldGradientAndFourierFlux, ElementResultsAreKeyedByOrdinalAndNotByRawElementId, HeatFluxMagnitudeIsEuclideanAndKeepsTheVector, RefusesNonFiniteDerivedValues, R... |
| PM13 | an invalid element is skipped instead of failing the whole recovery | KILLED | 1 of 40 failed | OneInvalidTetrahedronFailsTheWholeRecoveryAndIsNeverSkipped |
| PM14 | the non-finite gradient gate is removed | KILLED | 1 of 40 failed | RefusesNonFiniteDerivedValues |
| PM15 | the non-finite flux gate is removed | KILLED | 1 of 40 failed | RefusesNonFiniteDerivedValues |
| PM16 | the non-finite magnitude gate is removed | KILLED | 1 of 40 failed | RefusesNonFiniteDerivedValues |
| PM17 | a non-finite nodal temperature is accepted | KILLED | 1 of 40 failed | RefusesWhatItCannotRecoverAndPublishesNothing |
| PM18 | a temperature below absolute zero is accepted | KILLED | 2 of 40 failed | ALateStateBelowAbsoluteZeroFailsTheWholeHistory, RefusesWhatItCannotRecoverAndPublishesNothing |
| PM19 | the temperature count is not checked against the numbering | KILLED (assertion abort) | process aborted | (libstdc++ bounds assertion fired inside the suite) |
| PM20 | a conductivity of zero or less is accepted | KILLED | 1 of 40 failed | RefusesWhatItCannotRecoverAndPublishesNothing |
| PM21 | the numbering is not checked against the mesh | KILLED | 1 of 40 failed | RefusesWhatItCannotRecoverAndPublishesNothing |
| PM22 | a remesh with the same counts is accepted as the same mesh (stamp ignored) | KILLED | 1 of 40 failed | RemeshStalesTheResultEvenWhenCountsAndIdsAgree |
| PM23 | the solution is not required to be current at the model level (steady) | KILLED | 1 of 40 failed | RecoveryIsRefusedForEveryStaleInput |
| PM24 | the solution is not required to be current at the model level (transient) | KILLED | 1 of 40 failed | TransientRecoveryOnTheBlockKeepsOrderAndFollowsTheSolution |
| PM25 | a model older than a material edit is paired with the new conductivity (cross-check removed) | KILLED | 1 of 40 failed | RecoveryIsRefusedForEveryStaleInput |
| PM26 | the transient states are published in reverse order | KILLED | 3 of 40 failed | ConvectionIsNotPartOfTheConductiveFlux, TransientHistoryKeepsTheSolutionsOrderAndSeparatesStorageFromFlux, TransientRecoveryOnTheBlockKeepsOrderAndFollowsTheSolution |
| PM27 | every transient state carries the first state's time | KILLED | 2 of 40 failed | TransientHistoryKeepsTheSolutionsOrderAndSeparatesStorageFromFlux, TransientRecoveryOnTheBlockKeepsOrderAndFollowsTheSolution |
| PM28 | a bad late state is reported as state 0 | KILLED | 1 of 40 failed | ALateStateBelowAbsoluteZeroFailsTheWholeHistory |
| PM29 | an element is looked up by its raw id (minus one) and not by its ordinal | KILLED (assertion abort) | process aborted | (libstdc++ bounds assertion fired inside the suite) |
| PM29b | an element is looked up by its raw id (minus one), clamped to stay in range | KILLED | 1 of 40 failed | ElementResultsAreKeyedByOrdinalAndNotByRawElementId *(first pass SURVIVED: a test gap; closed and rerun)* |
| PM30 | a zero gradient gives negative zero (the flux is written -(k g)) | KILLED | 1 of 40 failed | HeatFluxMagnitudeIsEuclideanAndKeepsTheVector |
| PM31 | a steady source records itself as transient | KILLED | 4 of 40 failed | AFailedRecoveryNeverMakesAnOldResultCurrent, NodalTemperatureRemainsKelvinAndFollowsTheSolver, RecoveryIsRefusedForEveryStaleInput, RemeshStalesTheResultEvenWhenCountsAndIdsAgree |
| PM32 | the result stores nodal temperatures by averaging element values (nodal smoothing of the gradient: each element gets the mean of itself and its predecessor) | KILLED | 3 of 40 failed | ConvectionIsNotPartOfTheConductiveFlux, GeneralTetIndependentGradientOracle, TransientRecoveryOnTheBlockKeepsOrderAndFollowsTheSolution |
| PM33 | the temperatureOf lookup ignores the numbering the result belongs to | KILLED | 1 of 40 failed | ElementResultsAreKeyedByOrdinalAndNotByRawElementId |
| PM34 | the state index of temperatureOf is not range-checked | KILLED (assertion abort) | process aborted | (libstdc++ bounds assertion fired inside the suite) |
| PM35 | a held steady result is judged current even when the model changed (stale reasons ignored) | KILLED | 3 of 40 failed | AFailedRecoveryNeverMakesAnOldResultCurrent, RecoveryIsRefusedForEveryStaleInput, RemeshStalesTheResultEvenWhenCountsAndIdsAgree |
| PM36 | a held transient result is judged current even when the model changed (stale reasons ignored) | KILLED | 1 of 40 failed | TransientRecoveryOnTheBlockKeepsOrderAndFollowsTheSolution |
| PM37 | a held steady result may be asked about with the transient settings (mode check removed) | KILLED | 1 of 40 failed | TransientRecoveryOnTheBlockKeepsOrderAndFollowsTheSolution *(first pass SURVIVED: a test gap; closed and rerun)* |
| PM38 | a held transient result may be asked about without the transient settings (mode check removed) | KILLED | 1 of 40 failed | RecoveryIsRefusedForEveryStaleInput |
| PM39 | the recovery does not refuse a solution whose conductivity is invalid at the seam (seam check skipped) | KILLED | 2 of 40 failed | ALateStateBelowAbsoluteZeroFailsTheWholeHistory, ElementResultsAreKeyedByOrdinalAndNotByRawElementId *(first pass SURVIVED: a test gap; closed and rerun)* |
| PM40 | the history is checked only through the first state (late states are not pre-checked) | KILLED | 1 of 40 failed | ALateStateBelowAbsoluteZeroFailsTheWholeHistory |

## The survivors, and what closed them

| Probe | What it showed | Closed by |
| --- | --- | --- |
| **PM10** | Not a production survivor: the probe computed the scaled gradient and discarded it (`(void)gradientMm`), so the mutated program is the original. It is **void by construction** and is replaced by **PM10b**, which applies the factor to the gradient that is used (killed by 19 cases). | PM10b |
| **PM29b** | The element lookup used the raw id minus one, clamped. The two-tetrahedron fixture (ids 2 and 3) looked up only the *last* tetrahedron by id, for which "raw id − 1, clamped to the last ordinal" is the right answer by accident, and compared flux and magnitude for the first only with each other. | `ElementResultsAreKeyedByOrdinal…` now looks up **every** tetrahedron through all three accessors and compares each with the array at its ordinal, and requires the two tetrahedra's gradients and magnitudes to differ. PM29b killed. |
| **PM37** | A *transient* result asked about with the *steady* overload was refused anyway (the stale reasons differ by mode), so removing the explicit mode check changed only the message. | `TransientRecoveryOnTheBlock…` asserts the refusal says "transient history" and that `isCurrent` is false. PM37 killed. |
| **PM39** | The conductivity rule at the **solution seam** was unobserved: the kernel has its own check, and a conductivity of 0 gives an all-zero flux that looks valid, while NaN was caught later by the flux gate under another name. | `ElementResultsAreKeyed…` (steady seam: 0, -1, NaN, inf) and `ALateStateBelowAbsoluteZero…` (transient seam: 0, -45, NaN) require `thermal_conductivity_invalid`. PM39 killed. |

The four probes that end with "assertion abort" (PM02, PM19, PM29, PM34) read outside a vector and stopped on libstdc++'s bounds assertion (`_GLIBCXX_ASSERTIONS`, on in
the debug presets); the log ends mid-run with the assertion text and no summary line. They are detections, not survivors: the suite did not report success. PM02 and PM29
were additionally re-posed with a clamped index (PM02b, PM29b) so that a clean per-case count exists.

## Honest notes

* The count of cases that fail is a property of the probe, not a ranking: a probe killed by exactly one case (PM13–PM17, PM20–PM25, PM28, PM30, PM33, PM36, PM38, PM40) is
  killed by the case written for that refusal, which is what a precise test should do.
* No tolerance was loosened and no test was weakened to kill a probe. The three additions above only add assertions.
* Mutations of `ThermalPost.hpp` (inline, `static_assert`s) are covered by the compile-fail group (31 cases, `qualification/ctest-*.log`), which is itself a negative test of the
  header.
* The ownership gate was exercised by its fixtures (GUI_OWNERSHIP.md): the "GUI recomputation" mutant is a file in `recovery-gui-recomputes`, and the gate fails on it.
* A mutation not run: replacing `return material->conductivity();` by `return model.conductivity();` in `currentConductivity`. It is **equivalent**, because the line before it has
  already required the two to be equal.