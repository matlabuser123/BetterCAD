# P18-DATA-001 — the analysis state model and the result lifecycle

```text
SUBJECT:  where one thermal analysis stands, derived from source identity and
          never stored; and what a held result may become
```

## States

Derived, never stored. Every value is a function of the document, the current
mesh state and what the slot holds, so there is no cached flag to fall out of
agreement with the thing it describes -- the choice P16 made for
`MeshCurrency` and P17 for its `AnalysisState`.

| State | Brief's name | Conditions | Result held | Result current | Reached from |
| --- | --- | --- | --- | --- | --- |
| `NoAnalysis` | no analysis | no thermal analysis with that id | — | — | — |
| `InputsUnavailable` | **defined** | the analysis exists; `requireThermalModel` refuses (ineligible body, no mesh, stale mesh, failed generation, no material, no usable k) | maybe | no | creation; any state on a geometry or mesh change |
| `Ready` | ready | inputs resolve; nothing held | no | — | `InputsUnavailable` once a mesh is current; any state after `reset()` |
| `SolvedCurrent` | solved current | a result whose source equals the current source | yes | yes | `Ready` or `SolvedStale` or `SolveFailed` on `recordSuccess` |
| `SolvedStale` | solved stale | a result exists; at least one source field differs | yes | no | `SolvedCurrent` on any dependency change |
| `SolveFailed` | failed | the last attempt failed | maybe | **no** | any state on `recordFailure` |

Every state is reached by a section of
`ThermalData_TheStateMachineReachesEveryStateItDeclares`, including "failed
with no result", "failed over a surviving stale result" and "failed over a
surviving current result". There is no state the code cannot produce.

**"Defined" is `InputsUnavailable`.** A definition that exists but cannot be
computed from is exactly that state, and `thermalInputProblem` names which of
the seven reasons applies. "Incomplete" definitions do not exist in the
document at all: `validate(definition)` refuses a transient analysis without
an initial condition or time settings, and a steady one that carries them, so
the only way for an analysis to be "not ready" is for its inputs not to
resolve.

## The result lifecycle

```text
                recordSuccess            recordFailure
   NO RESULT  ───────────────▶ CURRENT  ───────────────▶ FAILED, no result
      ▲                          │   ▲                        │
      │ reset                    │   │ recordSuccess          │ recordSuccess
      │              dependency  │   │                        ▼
      │                 moved    ▼   │                     CURRENT
      └──────────────────────  STALE ────────────────────▶ FAILED, stale
                 (historical)       recordFailure            result remains
                                                              │
                                              recordSuccess   ▼
                                                           CURRENT
```

`ThermalResultSlot` is the type that holds a result with these transitions:

| Operation | Result | Failure | What it may NOT do |
| --- | --- | --- | --- |
| `recordSuccess(result)` | replaced | cleared | — |
| `recordFailure(error)` | **untouched**: same object, same source, same currency | set | replace, clear or re-stamp the result |
| `reset()` | cleared | cleared | — |

**A failure is never a current result.** `analysisState` answers
`SolveFailed` whenever a failure is held, before it looks at the result.
**A failure never resurrects a stale result.** `recordFailure` does not touch
`result_`, so the source the stale result carries is the one it was computed
with, and `resultCurrency` still says `Stale`. Both are measured by
`ThermalData_FailedAttemptIsNotCurrent` and
`ThermalData_FailedAttemptDoesNotResurrectStaleResult`, the latter running
the full sequence solve -> remesh -> fail -> fail again -> succeed and
checking the held source at each step.

**`SolveFailed` takes precedence over a surviving stale result**, mirroring
`MeshCurrency::GenerationFailed`: reporting that result as merely stale would
hide that the attempt to replace it failed.

## Why the slot is in this milestone

P17 defined `analysisState(... lastResult, lastFailure)` in its DATA milestone
and the service that holds them in SOLVE. The brief for this milestone asks
that the lifecycle -- failed with no result, failed while a stale result
remains, failure never resurrecting -- be *proved* now, before any solver
exists. Proving it needs the holder with its transitions, so the holder is
here: forty lines, no document access, no cached currentness. P18-SOLVE-001
will own one per analysis.
