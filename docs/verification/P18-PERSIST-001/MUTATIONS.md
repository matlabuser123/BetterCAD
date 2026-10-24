# P18-PERSIST-001 — mutation campaign

```text
41 PRODUCTION MUTANTS  -> 41 killed. 1 SURVIVED on the first run (PER23) and led to a sharper assertion.
16 GATE MUTANTS        -> 16 killed. 1 SURVIVED on the first run (PGATE01) and led to a new fixture.
Every probe restored its file: the sha256 of ThermalAnalysisJson.cpp and DocumentJson.cpp is identical before the campaign and after it.
```

## Method

A mutation is a one-line change to the code under test; a suite that is worth anything fails when the code is wrong. [mutation-driver-persist.ps1](qualification/mutations/mutation-driver-persist.ps1) reads each
`Find`/`Replace` from files (so double quotes survive a native command line), applies it with [mutate-t.ps1](qualification/mutations/mutate-t.ps1), rebuilds `bettercad_tests` (`debug-ext`, `-Werror`),
runs `ThermalPersistence_*` **and the two sibling persistence suites that share the dispatch** (`StructuralPersistence_*`, `MeshingPersistence_*` — 112 cases), reads the verdict from the test output and
restores the file. `KILLED` = a case fails, or the runner crashes after failed assertions; `SURVIVED` = "All tests passed"; `VOID` = the mutant did not compile (none were void: every probe was written to keep
its variables alive under `-Werror`, the lesson of P17-PERSIST-001 and P18-CMD-001). Nothing under `src`, `include`, `tests`, `apps` or `cmake` was edited while a campaign ran.

**Restore proof:** `C7CF7C00…B01F3` (`ThermalAnalysisJson.cpp`) and `4C91BFFF…BDC2A8` (`DocumentJson.cpp`) before the campaign
([persist-premutation-hashes.txt](qualification/mutations/persist-premutation-hashes.txt)) and again after the last pass; each probe's own line says `restored`.

## The brief's mutations, and what happened to each

| The brief asks to mutate | Result |
| --- | --- |
| serialize K, C, Q, T, a transient history, heat flux, balance | **IMPOSSIBLE TO WRITE, recorded rather than faked.** The writer's only argument is the analysis object, which has no such member; a result does not live on any document object. Expressing the mutation means changing the writer's signature and every caller. The nearest honest probes are the ones that put a *non-canonical* value into the file: the analysis **revision** (PER14, killed) and an extra key |
| serialize "current=true" | the same: no source for it. The reader's side — a `current`/`validated` key accepted and ignored — is PER35 (the strict key list loosened), killed |
| serialize mesh facet ids with the GeometryReference / replace the reference with resolved facets | the writer has no mesh. The rebind mutations are PER13 (a face rebound to the end cap on write) and PER36 (on read), both killed |
| drop the unresolved GeometryReference | PER13 and PER36 (an unresolved reference becomes a resolvable one) |
| regenerate `LoadId` on load | PER19, PER20 — killed |
| accept duplicate ids by last-write-wins | PER21, PER22 — killed |
| partially commit during parsing | **IMPOSSIBLE**: no entry point fills an existing document (pinned by `static_assert`) |
| save temperature after converting to Celsius | PER05 (held), PER29 (initial, on read) — killed |
| serialize `dt` using formatted UI text | the writer writes a typed double; PER08 (ms) and PER30 (ms on read) — killed |
| iterate an unordered map | there is no map; PER11, PER12 (reverse order), PER40, PER41 (key order) — killed |
| write a timestamp / solver statistics | PER14 (a revision) — killed; gate rule 6 |
| auto-solve during load | the loader has no solver; `ALoadedAnalysisHasIntentAndNoDerivedState` |
| accept a fake temperature array as a result | PER35 — killed |
| default an unknown load type to heat flux | PER28 (unknown enumerator read as the first) — killed |
| default a missing current-version field | the solver section is wholly required: `RejectsMalformedThermalIntent`; PER25 (validator dropped), PER28 |
| sort / canonicalize output times | there is no output-time vector in the data model |

## The production mutants

| Id | Mutation | First run | Final |
| --- | --- | --- | --- |
| PER01 | the pivot floor is not written | same | killed: 31 of 112 cases fail |
| PER02 | the residual tolerance is not written | same | killed: 31 of 112 cases fail |
| PER03 | a surface flux is written as its absolute value | same | killed: 14 of 112 cases fail |
| PER04 | a generation is written as its absolute value | same | killed: 1 of 112 cases fail |
| PER05 | a held temperature is written in Celsius | same | killed: 14 of 112 cases fail |
| PER06 | a convection coefficient is written in kW/(m^2 K) | same | killed: 15 of 112 cases fail |
| PER07 | the ambient is written from the coefficient | same | killed: 14 of 112 cases fail |
| PER08 | the time step is written in milliseconds | same | killed: 13 of 112 cases fail |
| PER09 | the end time is written in minutes | same | killed: 12 of 112 cases fail |
| PER10 | the initial temperature is not written | same | killed: 12 of 112 cases fail |
| PER11 | the loads are written in reverse order | no verdict line (the runner crashed; verdict logic extended) | killed: assertions fail and the runner crashes |
| PER12 | the conditions are written in reverse order | same | killed: assertions fail and the runner crashes |
| PER13 | a load face is rebound to the end cap on the way out | same | killed: 13 of 112 cases fail |
| PER14 | the writer adds the analysis revision to the file | same | killed: 32 of 112 cases fail |
| PER15 | empty load and condition arrays are always written | same | killed: 1 of 112 cases fail |
| PER16 | the load kind keys are swapped | same | killed: 10 of 112 cases fail |
| PER17 | the mode keys are swapped | same | killed: 11 of 112 cases fail |
| PER18 | the output policy keys are swapped | same | killed: 3 of 112 cases fail |
| PER19 | a load id is regenerated as id + 1 on load | same | killed: 16 of 112 cases fail |
| PER20 | a condition id is regenerated as id + 1 on load | same | killed: 12 of 112 cases fail |
| PER21 | a duplicate load id is accepted (last write wins in the vector) | same | killed: 1 of 112 cases fail |
| PER22 | a duplicate condition id is accepted | same | killed: 1 of 112 cases fail |
| PER23 | the core load validator is not called on read | SURVIVED | killed: 1 of 48 cases fail |
| PER24 | the core condition validator is not called on read | same | killed: 1 of 112 cases fail |
| PER25 | the core solver-settings validator is not called on read | same | killed: 1 of 112 cases fail |
| PER26 | the core transient-settings validator is not called on read | same | killed: 1 of 112 cases fail |
| PER27 | the keys a kind must not carry are no longer refused | same | killed: 1 of 112 cases fail |
| PER28 | an unknown enumerator is read as the first one | same | killed: 2 of 112 cases fail |
| PER29 | the initial temperature is read as Celsius | same | killed: 9 of 112 cases fail |
| PER30 | the time step is read as milliseconds | same | killed: 10 of 112 cases fail |
| PER31 | a surface flux sign is flipped on read | same | killed: 17 of 112 cases fail |
| PER32 | a generation is read as its absolute value | same | killed: 1 of 112 cases fail |
| PER33 | the two solver tolerances are swapped on read | same | killed: 14 of 112 cases fail |
| PER34 | the output policy is always read as final-only | same | killed: 10 of 112 cases fail |
| PER35 | the strict key list is loosened so generated keys are accepted and ignored | same | killed: 1 of 112 cases fail |
| PER36 | a load face is rebound on read (the nearest-face repair) | same | killed: 17 of 112 cases fail |
| PER37 | the reader no longer dispatches thermal-analysis | same | killed: 36 of 112 cases fail |
| PER38 | the invalid feature handle is accepted as a target | same | killed: 1 of 112 cases fail |
| PER39 | the writer no longer refuses an identifier above the file cap | same | killed: 1 of 112 cases fail |
| PER40 | the writer emits mode before mesh (key order) | same | killed: 2 of 112 cases fail |
| PER41 | the writer emits the solver section before the loads (key order) | same | killed: 2 of 112 cases fail |

## The one that survived, and what closed it

**PER23** removes the reader's own call of `validate(*load)`. It survived because `ThermalAnalysis::create` validates every load again when the definition is assembled, so the same *rejection* happens either
way — the difference is only **where the diagnostic points**: the reader's call names `…loads[0]`, the definition-level call names only the analysis. The assertion on the invalid-id case was `ContainsSubstring("ThermalLoadId")`,
which both messages contain. It now also requires `.loads[0]`, and PER23 is killed. (PER24, the same removal for conditions, was already killed, because an invalid condition is tested at a path.)
This is exactly the L2 finding of the independent review — "weak substring checks" — found a second time by a mutation.

**PER11 and PER12** (loads and conditions written in reverse order) killed the runner outright after failed assertions: the cause of the crash was not investigated further (the failing assertions are lost with the runner's buffered output). With the thermal suite alone, PER11 is killed the same way. Recorded as
"killed: the runner crashes"; a crash is a failed run.

## The gate mutants

The ownership gate runs against the real tree, one valid fixture and 22 fixtures that each break exactly one rule. A rule that is disabled or narrowed must make a fixture test fail; if none does, the rule is not pinned.
Driver: [mutations/gate-mutation-driver-persist.ps1](qualification/mutations/gate-mutation-driver-persist.ps1).

| Id | Rule disabled or narrowed | Verdict | Fixture test(s) that fail |
| --- | --- | --- | --- |
| PGATE01 | rule 1: a derived-chain header may be included | KILLED (first run: SURVIVED) | vendored-layer-include |
| PGATE02 | rule 1: a third-party header may be included | KILLED | includes-third-party |
| PGATE03 | rule 1: any quoted project header may be included | KILLED | other-project-include |
| PGATE04 | rule 1: a space after the # hides an include | KILLED | spaced-include |
| PGATE05 | rule 2: derived work may be named | KILLED | derived-name, current-check |
| PGATE06 | rule 3: a mesh identity may be named | KILLED | mesh-identity |
| PGATE07 | rule 4: a material property may be named | KILLED | material-copy, material-id |
| PGATE08 | rule 5: a number may be parsed from text | KILLED | parses-text |
| PGATE09 | rule 6: a non-deterministic construct is allowed | KILLED | unordered, timestamp, stale-flag, mutable |
| PGATE10 | rule 9: the reader may resolve and look up | KILLED | resolves |
| PGATE11 | rule 10: a unit may be converted | KILLED | units |
| PGATE12 | rule 7: the owners need not be used at every place that needs them | KILLED | missing-face-encoding, weak-strictness |
| PGATE13 | rule 7: the strict-key reads are counted as one | KILLED | weak-strictness |
| PGATE14 | rule 8: another thermal header may be included in the io module | KILLED | lookalike-include, io-includes-solver |
| PGATE15 | rule 8: a lookalike of the analysis header is allowed (unanchored) | KILLED | lookalike-include |
| PGATE16 | rule 8: the dispatch may name derived work | KILLED | dispatch-derived |

**PGATE01** survived the first run: the first branch of rule 1 (a forbidden derived-chain header) is *shadowed* by a later branch that refuses every `bettercad/` include that is not under `core`, so the
fixtures with a solver header failed either way and the first branch could be removed unseen. It is not redundant for an include whose path merely *contains* a forbidden layer (`<vendor/bettercad/renderer/Renderer.hpp>`)
— which only that branch catches. The fixture `vendored-layer-include` is that include; with it, removing the branch fails the gate. (Its expected message had to span a line wrap in CMake's error output; the
first version of the test's regex did not, which is why the test was run un-mutated before the mutant was rerun — a failure that is also present without the mutation is not a kill.)

## Honest limits

* The campaign mutates `ThermalAnalysisJson.cpp` and one line of `DocumentJson.cpp`. `ObjectJson.hpp` (two declarations) is not mutated: a changed declaration does not compile against its callers.
* A mutant equivalent to the original would survive; there were none to classify. PER23 looked like one — the same rejection happens through the definition — until the diagnostic's path was asked for.
* The mutation filter includes the P16 and P17 persistence suites, so a mutation of the shared path would also have shown there. (PER11 and PER23 were re-run with the thermal suite alone, to see what the thermal tests alone do.)
