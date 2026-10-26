# P18-QUAL-001 - persistence audit (brief sections 63-66)

**Verdict: the file holds intent and nothing else; what the writer emits the reader accepts; every malformed input is refused by name.**
Row-by-row: `audit/AUDIT_C_post_balance_validation_viz_commands_persistence_cli.md` section C6 (14 rows, 14 COVERED) and ADR-056 in `audit/AUDIT_A_*` (14 clauses, all COVERED).

| property | tests |
|---|---|
| one object type `thermal-analysis`; no schema bump; a pre-P18 document loads and simply holds no analysis; a future schema is refused, not guessed | `ThermalPersistence_APreP18DocumentLoadsAndSimplyHasNoThermalAnalyses`, `_AFutureSchemaVersionIsRefusedNotGuessed` |
| the section is the definition and nothing else: seven members (`mesh`, `mode`, `loads`, `boundary_conditions`, `initial_temperature`, `solver`, `transient`); no derived key can be written | `ThermalPersistence_NoDerivedStateReachesTheFile` (46 forbidden key prefixes, after a steady and a transient solve), `_TheGoldenSectionsHaveFixedBytes` (an FNV hash of the exact bytes); mutation QP21 (the writer adds a derived key) killed by **101** tests |
| bytes before the solve equal bytes after the solve, steady **and** a long transient, after POST, BALANCE, validation and VIZ | `ThermalPersistence_TheFileIsIdenticalWhateverASteadySolveHasDone` (eight states: solved, remeshed, stale, failed), `_TheFileIsIdenticalWhateverALargeTransientSolveHasDone` (at least 200 states; the result is more than 100 times larger than the file), `ThermalReference_Persistence_SaveLoadReSolve` (six reference models); and the final-gate case over all 20 reference documents |
| save, load, save is byte-identical and a fixed point; doubles round-trip bit for bit (subnormal, `DBL_MAX`, `0.1 + 0.2`, `-0.0`) | `ThermalPersistence_SaveLoadSaveIsByteIdenticalAndAFixedPoint`, `_ExtremeAndAwkwardFiniteValuesSurviveBitForBit`, `_TheDecimalPointDoesNotDependOnTheLocale` |
| malformed input is refused by name and rejection is atomic | `_RejectsWrongFieldTypes`, `_RejectsMalformedThermalIntent`, `_RejectsInvalidValuesRatherThanCorrectingThem` (`dt` zero, negative, greater than the end time; `1e999`; `NaN` and `Infinity` tokens), `_RejectsDuplicateIdentitiesRatherThanMerging` ("the load id 5 is used twice", with its path), `_ACorruptFileLeavesTheOpenDocumentAlone` |
| a persisted fake `validated` / `current` / `result` has no authority: every unknown key is refused | `ThermalPersistence_FakeResultsAndFakeStatusCannotBecomeAuthority` (17 injected keys at the data level, three more inside a load record) |
| an unresolved face reference round-trips exactly and is never rebound; a well-formed reference to a face that does not exist **loads** (intent), and the solve refuses it | `_AnUnresolvedReferenceIsPreservedAndNotRebound`, `_GeometryReferencesSurviveExactlyInEveryForm` (eight selector forms, loads and conditions), `_AnUnresolvedReferenceStillRefusesTheSolveAfterLoading` |
| the material is P15's and is never copied into the analysis | `ThermalPersistence_TheMaterialStaysP15sAndIsNotCopiedIntoTheAnalysis`, `architecture.thermalpersist-ownership.material-copy` |
| a loaded analysis is intent only: no mesh, no numbering, no result, nothing current; a re-solve runs the normal chain and reproduces the answer | `ThermalPersistence_ALoadedAnalysisHasIntentAndNoDerivedState`, `_ASteadyDestroyAndLoadRoundTripReSolvesEquivalently`, `_ATransientDestroyAndLoadRoundTripReSolvesEquivalently` |
| the gate: ten rules, 22 negative fixtures | `architecture.thermalpersist-ownership` and its fixtures |

## Defect found and fixed by this milestone: D-A, the writer emitted a file the reader refuses

The code's own comment says "the writer must not emit what the reader refuses". Two classes of target broke that rule, both reachable through the data-model API
(`ThermalAnalysis::create`, `setDefinition`) rather than through commands or the CLI:

1. A `FaceName` whose **feature is the invalid handle**. The data-model `validate` admits it; the commands and the reader refuse it. The writer checked only the load and
   condition identities, so such a definition **saved successfully and then never loaded**.
2. A `GeometryReference` sub-identifier **above the file cap** (2^53 - 1): the feature, the selector's entity, the `along` / `chamfer_edge` identifiers and the copy-step features.
   `faceSelectorToJson` wrote them; the reader's `readId` capped them.

`thermalAnalysisToJson` now applies a `faceWritable` check to every load target and every condition face: it refuses an invalid feature handle and any identifier above the cap,
**by name** (the message names the load or condition and the identifier) and `saveDocument` creates no file. `ThermalPersistence_WhatTheWriterEmitsTheReaderAccepts` drives
six sections - no feature (load), no feature (condition), a feature id of 2^60, a selector entity of 2^60, a copy-step feature of 2^60, and the ordinary targets (start cap, end cap, side 0, side 1)
written and read back with the target compared for equality. Against the old writer 5 of its sections fail (`repro-old-code.txt`); with the fix it passes. Mutations on the writer
were taken after the fix, so they attack the corrected code.

## Recorded

* The JSON parser collapses a repeated key (last wins) for **every** object type of the format, not only this one. Making it strict would change how every document loads; it is an owner decision across the
  whole format, pinned by `ThermalPersistence_ARepeatedKeyInTheFileIsLastWinsAsEverywhereInTheFormat`. Duplicate **identities** - the brief's rule - are refused.
* The data-model `validate` accepts `FaceName{}`; the commands and the file refuse it. With D-A fixed, the *file* no longer disagrees with itself, but the data-model/commands asymmetry remains (owner decision, D7 in `audit/AUDIT_A`).
* `-0.0` and `+0.0` compare equal, so a no-op edit leaves the old zero's bits (tested and intended); two histories that reach equal definitions can write different bytes if a zero's sign differs.
* Two checks can pass without exercising their claim: `..ACorruptFileLeavesTheOpenDocumentAlone` is atomic by construction (the loader never receives the open document) and the locale test degrades to a `WARN` when no comma-decimal locale is installed.
* A duplicated cap constant: `kLargestFileId` in `ThermalEdits.cpp:119` and `kMaxFileId` in the io layer.
