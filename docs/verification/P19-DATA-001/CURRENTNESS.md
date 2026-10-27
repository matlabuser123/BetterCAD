# P19-DATA-001 - source, staleness, the accepted result and the failed attempt

## Source and provenance metadata

`CfdResultSource` is a record of identities and revisions, not a hash of bytes.
Every field is compared by `= default` equality. `staleReasons` reports
**every** field that differs, in a fixed order:

| field | read from | moves when | stale reason |
|---|---|---|---|
| `body` | the meshing control's body | the control is pointed at another body | `body` |
| `control` | the analysis definition | the analysis names another control | `mesh_control` |
| `geometry` | P16's `geometryRevision` (via `requireCfdModel`) | a CAD edit | `geometry` |
| `mesh` | the held mesh's `MeshStamp` | **any** regeneration, even with identical counts and NodeIds | `mesh` |
| `fluid`, `fluidRevision` | the material `requireCfdModel` resolved, and its revision | the assignment changes, or the material is edited (density today, viscosity from P19-FLUID-001) | `fluid` |
| `analysis`, `analysisRevision` | the `CfdAnalysis` object | any effective `setDefinition`, **and a rename** (core's `Document::rename` moves the revision) | `analysis` |
| `definition` | `fingerprint(definition)` | any change of content | `definition` |
| `mode` | the definition | steady and transient swap | `mode` |

`currentResultSource` builds the record by calling `requireCfdModel`, so every gate
of ADR-060 is inherited rather than re-asked. It then refuses a definition whose
pressure datum is **missing** (by name, `pressure_datum_missing`, through
`pressureDatumProblem`), and reads each field off the document. `CfdData_TheSourceIsPopulatedFromTheDocumentAndNotDefaulted` proves each
field is read, not defaulted. `CfdData_StaleReasonsAreSensitiveToEveryField`
mutates each field alone (ten rows) and then several at once.

## Stale and current

**Current exactly when the source computed now equals the recorded one.** No array,
count, metadata value or address is read:

* the same body remeshed with the same settings gives identical NodeIds and
  tetrahedron counts and a different stamp, so the result is **stale, reason `mesh`**
  (`CfdData_SameIdsNewMeshGenerationIsStale`);
* two results with different metadata on one source are both current, and two with
  identical metadata on two generations are one stale and one current
  (`CfdData_CurrentnessComparesSourceIdentityNeverMetadata`);
* every kind of definition edit makes the result stale for `analysis` and
  `definition`, and a mode switch adds `mode` (`CfdData_EveryDefinitionEditMakesTheResultStale`);
* a density edit stales the result for `fluid` **only**. The mesh stays current,
  and neither the analysis revision nor the definition moves
  (`CfdData_AFluidEditStalesTheResultAndNotTheMesh`);
* a quality-threshold edit (presentation) leaves the mesh **and** the result current
  (`CfdData_AViewerOnlyChangeDoesNotStaleAResult`);
* a CAD edit makes the inputs unavailable: no current source exists, and the
  surviving result is offered as nothing (`CfdData_AGeometryEditMakesInputsUnavailable`).

**The regime.** If a value is edited and typed back, the definition's fingerprint
returns to what the result recorded but the revision has moved twice. The result
stays **stale, with the single reason `analysis`**
(`CfdData_AnEditTypedBackKeepsTheResultStale`). This is the revision-keyed regime
P18's result uses. A rename does the same: the fingerprint is unchanged, the
revision moves, and the result is stale for `analysis` alone
(`CfdData_ARenameStalesTheResultByRevisionOnly`). Whether to key results by content
instead is P18-QUAL-001's open owner decision D1. ADR-061 does not take it, and
both tests fail first if the regime is changed without that decision.

## The current accepted result and the latest failed attempt

```text
CfdResultSlot
  result()            the current ACCEPTED result: entered only by recordSuccess(CfdResult);
                      a CfdResult is built only by CfdResult::create, which requires a
                      source naming the mesh, >= 1 iteration, norms that are norms, and the
                      mode's time fields
  failure()           the LATEST FAILED ATTEMPT: CfdSolveFailure {stage, error, source?, iterations?}
  recordSuccess(r)    replaces the result, clears the failure
  recordFailure(f)    holds f; the result is KEPT UNTOUCHED (same source, same metadata)
  reset()             nothing held
```

A failure cannot be recorded as a success (`A_FAILURE_IS_NOT_A_RESULT`; no
conversion between the types). A result cannot be built without the factory
(`A_RESULT_CANNOT_BE_BUILT_WITHOUT_ITS_FACTORY`) or written after it
(`A_RESULT_SOURCE_CANNOT_BE_REWRITTEN`, `A_RESULT_METADATA_IS_READ_ONLY`).

## The state machine (derived, never stored)

```text
NoAnalysis          no CFD analysis has that id (an id of another kind of object included)
InputsUnavailable   requireCfdModel refuses (nothing meshed, a stale mesh, an ineligible body, no fluid,
                    no density), or nothing fixes the pressure
Ready               inputs resolve, nothing held
SolvedCurrent       a result is held and its source equals the current one
SolvedStale         a result is held and some field differs
SolveFailed         a failure is held, whatever result survives, current or stale
```

`CfdData_TheStateMachineReachesEveryStateItDeclares` reaches all six. That includes
three separate causes of `InputsUnavailable`: nothing meshed, no fluid, and a
missing pressure datum, which becomes `Ready` the moment a reference pressure is
added. It also reaches `SolveFailed` over no result, over a current result and
over a stale one.

**The state outranks the result's currency.** While a failure is held the analysis
is `SolveFailed`, even if the surviving result's source equals the current one. In
that case `resultCurrency` says `Current`, which is about that result's inputs, not
about the analysis. The failure's source is diagnostic and is not compared. Only a
success or a reset clears the state. This is P18's rule, kept, and the independent
review (finding 10) asked for it to be stated. `CfdData_FailedAttemptDoesNotResurrectStaleResult`
runs the sequence solve, remesh, fail, fail again, succeed. It checks after every
step that the stale result is the same object with the same source and metadata,
and that only the success makes the analysis current again.
