# P19-DATA-001 - adversarial review

Three attacks were made before the freeze:

- an **independent reviewer**, given the checklist, the ADRs and the code but not
  the author's conclusions;
- **twenty mutation probes** of the validators, the canonical text, the stale
  reasons, the datum rule and the lifecycle;
- the **author's own questions**.

All findings were disposed of before the freeze. None is left as a defect in the code.

## 1. The independent reviewer (read-only; read the source, not the docs)

The reviewer found **no blocker**: three major findings, ten minor and one
suspected. They verified as sound:

- NaN, infinity and -0.0 handling in every validator, and every inclusive and
  exclusive boundary;
- the datum truth table and mode completeness;
- that `operator==` and the canonical text agree apart from finding 5;
- `staleReasons` against all ten source fields;
- `currentResultSource` reading each field from the right place;
- the slot leaving a held result untouched;
- the private factory;
- that the definition's header does not reach the result;
- that excluding the fluid from the definition is sound under ADR-060;
- that not refusing a missing datum at definition level is defensible;
- the compile-fail regexes;
- every count the documents gave at the time.

| # | finding | disposition |
|---|---|---|
| 1 major | Under gravity, a pressure boundary had **no reference height**. ADR-058 makes it canonical, and ADR-061's draft wrongly claimed the reference point covered it. A uniform outlet pressure on a non-level face contradicts hydrostatics | **fixed**: `CfdGravity { acceleration, referenceHeight }`, required whenever gravity is present. A static-pressure boundary prescribes the gauge pressure at that height and varies hydrostatically along its face (ADR-061 §2). In a closed cavity the height is inert; that is recorded as the one inert canonical value, not made conditional |
| 2 major | the zero-match guard was not extended | **fixed**: `unit\.CfdData_` 39, the four fingerprint and regime cases by name, `compile_fail\.cfdids\.` 28 |
| 3 major | the records nested in the definition were not size-pinned. A resolved-facet cache added to `CfdBoundaryCondition` would survive every test, be compared by `==`, never be written to the text, and be derived state inside intent | **fixed**: mirrors for the `CfdBoundaryCondition` class (id + variant), `CfdReferencePressure`, `CfdGravity` and `FaceCopy` |
| 4 minor | a size mirror cannot see a member hidden in padding (seven bytes after `mode`) | **fixed**: a structured binding that names exactly the members (twelve for the definition, ten for the source, five for the metadata, and every nested record with public members), so any added member, of any size, is ill-formed. The `CfdBoundaryCondition` class and the three one-member settings records are pinned by size only, which suffices because none has padding to hide a member in |
| 5 minor | an out-of-range `FaceRole` passes core's selector check and writes "unknown", so two unequal definitions could share a text. An invalid feature id passes too | **fixed**: `cfd::validate(CfdBoundaryCondition)` refuses both; tested |
| 6 minor | text mutations survived: dropping x/y of the reference location, or a copy's instance or feature | **fixed**: five new truth-table rows (40 in all) and a second golden case covering every kind, the transient lines, the copies, the path edge, the reference pressure and a chamfer edge |
| 7 minor | the unknown-mode test passed even with the mode check deleted | **fixed**: the message is checked, on both a steady-shaped and a transient-shaped definition |
| 8 minor | some `is_constructible` asserts proved nothing, and the docs cited them as guards | **removed**; the size and count pins are cited instead |
| 9 minor | a definition with a **missing** datum reported `Ready` | **fixed**: `currentResultSource` refuses it by name (`pressure_datum_missing`), so the analysis is `InputsUnavailable` and becomes `Ready` when a reference is added; tested |
| 10 suspected | after a failure on an unchanged source, the state says `SolveFailed` while `resultCurrency` says `Current` | **recorded as intended** (P18's rule): the state outranks the result's currency, and the failure's source is diagnostic. Stated in `CfdResult.hpp` and ADR-061 |
| 11 minor | "inflow at an angle is not in v1" cited ADR-061, which did not say so | **recorded**: ADR-061 §2a narrows the velocity inlet to a uniform normal speed |
| 12 minor | a rename moves the revision and stales the result | **recorded and pinned**: `CfdData_ARenameStalesTheResultByRevisionOnly` |
| 13 minor | `THE_DEFINITION_HAS_NO_FLUID` only proves there is no member named `fluid` | **softened** in ADR-061: the size and count pins are what close the list; the case proves the likeliest name is absent |
| 14 minor | the roster-ordering test could not fail (the document's map is keyed by id) | **corrected**: the test now re-inserts an earlier id last, as redo does, and the comment says the sort is a second guarantee |
| 15 minor | the number of transient steps is not bounded at the data level | **recorded** for P19-TRANSIENT-001 (ADR-061 consequences), as P18 bounds its own in its solver |

## 2. Mutation probes

Each probe edits one file, rebuilds debug-ext, runs `^unit\.(CfdData_|CfdInput_)` through CTest (53 cases), and restores the file byte for byte (checked after every probe).

| id | the mutant | verdict | killed by |
|---|---|---|---|
| DP01 | a zero inflow speed is accepted | KILLED | `CfdData_BoundaryConditionsAreValidatedOnTheirOwnTerms` |
| DP02 | a NaN inflow speed is accepted | KILLED | `CfdData_BoundaryConditionsAreValidatedOnTheirOwnTerms`, `CfdData_TheDefinitionIsValidatedPerMode` |
| DP03 | a moving wall at rest is accepted | KILLED | `CfdData_BoundaryConditionsAreValidatedOnTheirOwnTerms` |
| DP04 | a velocity relaxation above 1 is accepted | KILLED | `CfdData_SolverSettingsAreValidated` |
| DP05 | a divergence factor of 1 is accepted | KILLED | `CfdData_SolverSettingsAreValidated` |
| DP06 | an unknown face role is accepted | KILLED | `CfdData_BoundaryConditionsAreValidatedOnTheirOwnTerms` |
| DP07 | a pressure inlet does not fix the pressure | KILLED | `CfdData_TheKindIsTheAlternativeHeld`, `CfdData_ThePressureDatumIsRequiredIfAndOnlyIfNoBoundaryFixesIt` |
| DP08 | the gravity reference height is not checked | KILLED | `CfdData_InitialConditionReferencePressureAndGravityAreValidated` |
| DP09 | an overdetermined datum is accepted | KILLED | `CfdData_ThePressureDatumIsRequiredIfAndOnlyIfNoBoundaryFixesIt` |
| DP10 | a steady definition may carry transient settings | KILLED | `CfdData_TheDefinitionIsValidatedPerMode` |
| DP11 | the reference location is not written | KILLED | `CfdData_TheFingerprintCoversEveryField`, `CfdData_TheSecondGoldenCoversEveryKindAndTheTransientLines` |
| DP12 | the gravity reference height is not written | KILLED | `CfdData_TheFingerprintCoversEveryField`, `CfdData_TheFingerprintIsAGoldenFunctionOfTheText`, `CfdData_TheSecondGoldenCoversEveryKindAndTheTransientLines` |
| DP13 | -0.0 is not written as 0 | KILLED | `CfdData_TheFingerprintIgnoresIdentityAndEverythingButMeaning` |
| DP14 | a copy's instance is not written | KILLED | `CfdData_TheFingerprintCoversEveryField`, `CfdData_TheSecondGoldenCoversEveryKindAndTheTransientLines` |
| DP15 | a fluid revision change is not a stale reason | KILLED | `CfdData_AFluidEditStalesTheResultAndNotTheMesh`, `CfdData_StaleReasonsAreSensitiveToEveryField` |
| DP16 | an equal definition moves the revision | KILLED | `CfdData_ANoOpEditDoesNotMoveTheRevision` |
| DP17 | a missing pressure datum is Ready | KILLED | `CfdData_TheStateMachineReachesEveryStateItDeclares` |
| DP18 | a failure discards the held result | KILLED | `CfdData_FailedAttemptDoesNotResurrectStaleResult` |
| DP19 | a failure does not outrank a held result | KILLED | `CfdData_FailedAttemptDoesNotResurrectStaleResult`, `CfdData_TheStateMachineReachesEveryStateItDeclares` |
| DP20 | the source carries no fingerprint | KILLED | `CfdData_EveryDefinitionEditMakesTheResultStale`, `CfdData_TheSourceIsPopulatedFromTheDocumentAndNotDefaulted` |

**20 of 20 killed**, none void, every file restored. The script is `qualification/mutation/mutate-data.ps1`, the verdicts are in `progress.txt` and each probe's CTest output is in `DP??-tests.log`. Probes DP06 and DP11 to DP14 target fixes the independent review asked for; each is killed by a test added for that finding.

## 3. The author's questions

| question | answer |
|---|---|
| Can a pressure, velocity or flux array enter the definition? | No: the definition's members are pinned by size and by count, and the compile-fail group proves `pressure`, `velocity`, `massFlux`, `cells` and `result` do not exist. |
| Can the fluid be named twice? | No: the definition has no fluid; the source records the material the boundary resolved. |
| Can a boundary condition be keyed by a mesh entity? | No: each payload is a `FaceName` and typed values, pinned by size and count; a `NodeId` or `ElementId` does not convert to its id or construct it. |
| Can a stale result be made current by rewriting it? | No: the source and metadata are const references and the constructor is private (four compile-fail cases). |
| Can a failed attempt erase or refresh the last good result? | No: `recordFailure` leaves it untouched (same source, same metadata), and the state says `SolveFailed` until a success. |
| Can two equal definitions have different fingerprints, or two different ones the same? | Not for valid definitions: -0.0 is written as 0, and every field (40 rows) changes the text. An out-of-range role is refused, and the digest is checked by an independent FNV-1a. |
| Does an edit typed back silently revive a result? | No: the result stays stale for `analysis`, as P18's does. Changing that is the open owner decision D1. |
| Is a definition with nothing fixing the pressure solvable? | It is storable but `InputsUnavailable`, never `Ready`. |
| Can a unit be confused? | No: m/s, Pa, m/s^2 and m are distinct types, and `Velocity3` and `Acceleration3` are distinct instances (four compile-fail cases). |

## 4. The reviewer's second pass

After the fixes, the reviewer re-read the source (still read-only). All 15 dispositions were **verified**: every binding counted against its type, both goldens checked line by line by hand against the code's format, and the new guard counts matched the TEST_CASE and registration counts. There was **no new code defect**. Six documentation wordings were then corrected:

- A: the inert reference height is the only value inert *by the boundary set*, not the only inert value.
- B: the count pins cover records with public members; the class and three one-member records are pinned by size only.
- C: the cavity part of the second golden is checked by substring, not as a full independent text.
- D: the reference height is always carried, defaulting to 0 m; requiring it is P19-CMD-001's job.
- E: SolveFailed outranks every state but NoAnalysis.
- F: ADR-061 now answers seven questions, not six.
