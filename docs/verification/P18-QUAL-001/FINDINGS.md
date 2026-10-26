# P18-QUAL-001 - findings of the final audit and what became of each

The audit (`audit/`) read the whole phase against the brief and the 33 adversarial questions of the TODO. It found **no wrong number**: no temperature, gradient, heat flux or balance that
a test or a payload shows to be wrong. It found **three defects that could have produced a wrong or misleading outcome at a boundary** - a file that could not be read back, a mesh
metric nobody had judged, a stale result labelled current by omission - and fixed all three by the discipline the TODO sets: reproduce, regression test, root cause, general fix, requalify the
affected milestone, rerun P18-QUAL. Every other item is recorded with its reason.

Reproduction against the old code is recorded in `repro-old-code.txt`: with `ThermalAnalysisJson.cpp` and `ThermalValidation.cpp` reverted to `HEAD`, the two new regression tests fail (2 of 2
test cases, 11 failed assertions: five persistence sections, six validation assertions); with the fixes they pass.

## Defects fixed

| id | where | the defect | the fix | the test that pins it |
|---|---|---|---|---|
| **D-A** | `src/io/json/ThermalAnalysisJson.cpp` | The writer emitted a file the reader refuses: a `FaceName` with the invalid feature handle (the data-model `validate` admits it), or a `GeometryReference` identifier above the file cap (2^53 - 1). It saved successfully and then never loaded - the failure the writer's own comment says it must not allow. Reachable through the data-model API, not through commands or the CLI. | `thermalAnalysisToJson` applies a `faceWritable` check to every load target and every condition face; it refuses by name and `saveDocument` creates no file. | `ThermalPersistence_WhatTheWriterEmitsTheReaderAccepts` (six sections) |
| **D-B** | `include/bettercad/renderer/ThermalInspection.hpp` | `inspectThermalNode` and `inspectThermalElement` defaulted their `resultState` to `Current`, and the inspection records defaulted the field to `Current`: a caller that forgot the argument would have labelled a stale result current. | The parameter has no default in either function; the records default to `NoAnalysis`; all 31 call sites state the state they mean. | compile-fail `thermalviz.an-inspection-must-state-the-result-state` ("too few arguments") |
| **D-F** | `src/thermal/ThermalValidation.cpp` | `classifyThermalMesh` skipped a classifiable metric whose P16 summary was absent or had count 0, and never compared a summary's count with the mesh's tetrahedron count: a P16 regression would have accepted a mesh with an element nobody judged. Unreachable today (P16 always summarises every metric over every tetrahedron), but the validator did not defend the invariant. | a structural `Error` finding `MeshHasInvalidElements` naming the metric, emitted whenever `tetCount > 0` and a metric has no summary or a short one. The shared helper `reportWithOneSummary` now fills all four metrics benignly. | `ThermalValidation_AMissingOrShortQualitySummaryIsNotAPass` |

## Concerns examined and closed without a production change

| id | concern | disposition |
|---|---|---|
| **D-E** | the solver residual gate (1e-9) is four orders looser than the balance gate; no test drove a solve at the edge of the accepted element shapes through the balance | measured, not assumed: both degeneration families taken to the last shape the classifier accepts; solve error 2.6e-13 and 7.8e-13, worst balance 4.8e-11 and 0. `ThermalValidation_AMeshAtTheEdgeOfTheAcceptedBoundsStillSolvesAndBalances`. Side finding: the aspect ceiling of 700 is never the binding bound (stretching reaches the radius-ratio floor at aspect 265). |
| **D-G** | `PASS_WITH_WARNINGS` through the CLI was never exercised (only an information finding) | a severity-warning finding cannot be produced by a small real document; the hand-built report goes through the production writer: `ThermalCli_ValidationJsonCarriesAWarningReportWithEveryOptionalFieldItHolds` (also closes the untested optional fields of C7.7a). `thermal-balance` added to the missing-material loop. |
| **D-H** | the fresh-binary proof records identity but not freshness | the freshness half is done at the harness, where it can be done without a file-system-timestamp hazard inside a six-hour run: `postrun-qual.ps1` (`FRESH_BINARY.md`). |
| **A G1** | the non-undo path of the two currentness regimes (typing the old value back) was pinned by no test | `P18Qualification_AManualEditBackFollowsTheSameTwoRegimesAsUndo`. |
| **B 9.5, 9.8** | the disconnected-component rule and the in-solve residual gate were proven on bare algebra / through a sibling path only | `ThermalSolve_ADisconnectedMeshWithAnUnanchoredComponentFailsEndToEnd` (the regions are four, not two - the unit tetrahedron has exact-zero couplings, and an exact zero does not connect), `_AnUnmeetableToleranceRefusesTheSolveItself`. |
| **A 51.2, 50.5, A5.5** | possession of a `ThermalModel`, the structural -> thermal direction, the dimension of the transient sum | `P18Qualification_PossessionOfAThermalModelIsTheOnlyProof`, the valid architecture tree, `P18Qualification_TheThermalTimeConstantIsATimeAndTheTransientSumIsDimensionallyLegal`. |
| QP15 | a mutant that survives | the unreachable `WithoutFacets` branch; see `MUTATION.md`. |

## Recorded, with the reason, in `LIMITATIONS.md`

D-C (the viewer draws an overlay with incomplete targets), D-D (default `FinalOnly` can never be accepted beyond one step), D-I (the CLI arithmetic gate is bypassable), D-J (dead `ThermalResult` code),
D-K (the transient balance starts from the resolved initial state), D-L (TV_POST does not recompute `q`), D1 (two currentness regimes), D2 (`NamedBoundarySet` is not a thermal target), D3 (a held `ThermalModel` is a convention),
D4 (a mesh identity is a stamp plus a node count), D5 (documentation drift in three ADRs), D6 (regex gates on text), D7 (parser last-wins, the `FaceName{}` asymmetry), D8 (one type for temperature and temperature difference),
D9 (the report is a mutable aggregate), D10 (duplicated numeric constants), D11 (ungated absences), the time-grid snap, the transient absolute residual, the anchor-classifier cliff, the dense-allocation guard.

## Requalification of the affected milestones

D-A is P18-PERSIST-001's, D-B is P18-VIZ-001's, D-F is P18-VALID-001's. The TODO asks for those milestones to be requalified and P18-QUAL rerun. The three-preset qualification of this milestone runs the
**complete** suite of each of them (unit, compile-fail, architecture, process and workflow tests) on a clean build of each preset, plus five repetitions of the thermal, reference and CLI groups in Release
and in Debug, which is stricter than a subset run of the three milestones. The harness for it is the one every milestone since P16-SIZE-001 has used, byte-identical.

## Found by the second, independent reviewer (`ADVERSARIAL_REVIEW.md`) and NOT changed

| id | finding | why it was not fixed here |
|---|---|---|
| **F1** | **the balance gate refuses correct transient runs once the time step is small** (reproduced: the committed `ThermalTransientSlab`, 40 x 40 x 100 mm, is accepted at dt = 0.01 s and refused at dt = 0.003, 0.001, 0.0003 s with `balance_out_of_tolerance`; the independent physical residual stays at 1e-15). The cause is a missing rounding term, `eps |T| |C| / dt`, in the transient arithmetic scale. | The only fix *loosens* a tolerance in the small-step regime. A tolerance is never loosened by the party qualifying it; the exact change is specified and the decision is the owner's. It errs on the safe side. **This is the most important open item of the phase.** |
| F2 | absurd but algebraically perfect results (1e9 K from an h = 1e-6 anchor, a prescribed 1e16 K) are accepted with no finding | a new bound would be a new refusal or warning: owner decision |
| F3 | the residual tolerance and the pivot floor are user intent with no upper bound (a tolerance of 1 passes and is echoed) | the tolerance is canonical, editable and persisted by ADR-053; owner decision |
