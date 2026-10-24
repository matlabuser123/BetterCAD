# P18-CMD-001 — Thermal Commands and Undo / Redo

```text
MILESTONE:  P18-CMD-001                                    the fifteenth milestone of P18 - Thermal Analysis
BASELINE:   HEAD 6b675ec (P18-VIZ-001 record), origin == HEAD, working tree clean, 5269 tests
ADDS:       ten undoable commands that edit canonical thermal intent, with exact undo and redo, atomic failure and correct redo invalidation
CHANGES:    nothing qualified (322 files of the thermal, meshing, structural, linalg, core and renderer modules and the apps are byte-identical to HEAD, except one line in
            src/thermal/CMakeLists.txt, compared by blob in FREEZE.md)
RESULT:     5432/5432 x 3 presets, 2182 x 2 in two, 0 warnings over 725 objects, 17 stages exit 0, 6h35m56s
NOT STARTED P18-PERSIST-001, P18-CLI-001, P18-REFMOD-001, P18-QUAL-001
```

## What this milestone is

> **THE UNDO HISTORY HOLDS CANONICAL THERMAL INTENT AND NEVER A GENERATED SOLVER OR RESULT ARRAY. A COMMAND EDITS ONE ANALYSIS DEFINITION; EVERY DERIVED OBJECT ANSWERS THROUGH ITS OWN CURRENTNESS.**

By the end of P18-VIZ-001 a thermal analysis can be defined, meshed, solved, recovered, balanced, validated and drawn, and nothing can be *changed* with an undo history. The audit found the command framework
already in the core (`Command`, `CommandHistory`, `AddObjectCommand`, `Document::modifyObject`, the monotonic revisions) and the pattern in P17-CMD-001, so this milestone is the ten commands on it — **no new
framework, no new invalidation mechanism, no new identity type.**

```text
CreateThermalAnalysisCommand                 wraps AddObjectCommand: the same ThermalAnalysisId on every redo
ThermalAnalysisEditCommand                   the base: stage -> apply -> record; undo/redo ASSIGN a stored definition and never invert
  Add / Edit / Remove ThermalLoadCommand
  Add / Edit / Remove ThermalBoundaryConditionCommand
  SetInitialTemperatureCommand               refused for a steady analysis, with a diagnostic naming the mode
  SetThermalSolverSettingsCommand
  SetTransientSettingsCommand                refused for a steady analysis
```

## The result in one table

| Gate | Result |
| --- | --- |
| three presets, the unfiltered ctest | **5432/5432** in `debug-ext`, `release-ext` and `debug-shared-ext` (the previous milestone had 5269; this adds 95 + 52 + 16) |
| the repeat set | 2182 tests x 2 presets, all pass |
| warnings | 0 over 725 objects per preset, `-Werror` |
| no-op rebuilds | 1 edge each (nothing rebuilt) |
| fingerprints | the eight-path fingerprint at the freeze == before the run == after it |
| stages | 17, all exit 0, 6h35m56s |
| `CMDFP` determinism line | identical in all three presets |
| unit cases | **95** `ThermalCommand_*` |
| compile-fail | **52** cases (+ a control that must compile) |
| architecture gate | **16** tests: the real tree and 15 fixtures that each fail one rule |
| zero-match guard | 14 exact-count filters |
| mutation probes | **44** production + **14** gate, all killed; 3 survivors on the first run each produced a test or a fixture |
| independent review | 7 defects, 7 weak tests, 6 gate gaps, 10 unpinned behaviours — each with a disposition; 0 atomicity holes |
| qualified modules changed | **0** |

### Attempts

The run that is recorded is the third. **Attempt 1** was stopped by the author after `debug-ext` reported 11 failures out of 5432 (the ten `gui.*` tests and `infra.deploy.location-independent`): Windows Smart App Control
(Code Integrity events 3033/3077) blocked the freshly built unsigned `bettercad.exe`; the cause was measured, the setting is the owner's, and the owner turned it off. **Attempt 2** passed `debug-ext` and `release-ext`
(5432/5432 each) and then could not LINK `bettercad_tests` on `debug-shared-ext`: a test read the static member `ThermalAnalysis::kTypeName`, which the thermal DLL does not export. That is a defect of this
milestone's own test, found by the shared-library preset exactly as that preset is meant to find it; it was fixed (one line, in a test), the tree re-frozen ([FREEZE.md](FREEZE.md)) and everything run again.
**Attempt 3** is the recorded one. The logs of attempts 1 and 2 are in `qualification/attempt1/` and `qualification/attempt2/`.

## Documents

| Document | What it holds |
| --- | --- |
| [COMMAND_MODEL.md](COMMAND_MODEL.md) | the architecture audit; the ten commands; what is deliberately not there |
| [CANONICAL_VS_DERIVED.md](CANONICAL_VS_DERIVED.md) | what the history may and may not hold, proved at runtime, at compile time and by the gate; reachability measured |
| [UNDO_REDO.md](UNDO_REDO.md) | exact undo and redo; content versus identity; the checked stack discipline; signed zero |
| [ATOMICITY.md](ATOMICITY.md) | atomic by construction; every refusal and its code; redo invalidation; the no-op policy |
| [GEOMETRY_REFERENCE.md](GEOMETRY_REFERENCE.md) | the `FaceName` is stored as given, never resolved; the one rule the command layer adds |
| [CURRENTNESS.md](CURRENTNESS.md) | the measured invalidation matrix and the **two currentness regimes** |
| [TYPE_SAFETY.md](TYPE_SAFETY.md) | the 52 compile-fail cases |
| [HISTORY_MEMORY.md](HISTORY_MEMORY.md) | the payload is two definitions, independent of mesh and result size |
| [DETERMINISM.md](DETERMINISM.md) | repeatability, the `CMDFP` fingerprint across presets |
| [ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md) | the independent review and its dispositions; observations O1–O5; twenty-one answered questions |
| [MUTATIONS.md](MUTATIONS.md) | 44 + 14 probes, the survivors and what closed them |
| [SCOPE.md](SCOPE.md) | what is in, what is not, the `TODO.md` ordering note |
| [FREEZE.md](FREEZE.md) | the candidate, the fingerprints, the re-freeze |
| `qualification/` | the run scripts, the post-run analysis, the mutation harness and its logs, the source audit, the logs of every attempt |
| ADR-055 | `docs/architecture/decisions/ADR-055-thermal-commands-edit-one-analysis-definition-and-the-history-holds-intent-only.md` |

## Decisions

| # | Decision | Why |
| --- | --- | --- |
| D1 | one edit base; every command is a function from the previous definition to the next | capturing, applying, undoing and redoing are written once, where an atomicity mistake would hide |
| D2 | undo and redo **assign** a stored definition | nothing is inverted, so nothing drifts; no number is formatted and parsed |
| D3 | records are found by **stable id**, never by position; an edit replaces the whole record in place | a multi-field edit lands entirely or not at all, and the user's order survives |
| D4 | a command changes the definition through `Document::modifyObject` / `setDefinition` and runs no solve | every derived layer already answers through its own currentness; a second opinion would be a second authority |
| D5 | undo/redo **check** that the analysis is as the command left it | an assignment of a whole definition would otherwise discard an out-of-history edit silently |
| D6 | the invalid feature handle is refused at the command boundary; a valid handle naming no feature is accepted | an invalid handle is not a target; whether a face resolves is a question about a mesh |
| D7 | a no-op succeeds, moves no revision and is recorded | P15, P16 and P17 behave so; one module must not differ |
| D8 | the two currentness regimes are **recorded and left as they are** | unifying them would change qualified P18 semantics |

## Findings for the owner

1. **Two currentness regimes** (O1). The derived chain compares its recorded intent *by value*; the validation report compares the analysis *revision*, which is monotonic. After an undo that restores identical
   intent, a post result / solution / balance is current again (and a fresh solve gives bit-identical arrays — tested), while the validation report stays stale. A viewer therefore shows a current result with a stale
   badge until the analysis is validated again. Not changed.
2. **The data model accepts `FaceName{}`** (O2): `validate(ThermalLoad / ThermalBoundaryCondition)` checks the selector and not the feature handle. Untouched; the commands refuse it themselves.
3. **A create accepts a dangling mesh-control handle** (O3), as P17-CMD-001 does. Pinned, not changed.
4. **No step-count bound in `validate(TransientSettings)`** (O4): a resource question for P18-TRANSIENT.
5. **No Qt panel** (O5): the application has no thermal panel and no command wiring; the gate fails a future one that edits an analysis around the commands.
6. **`TODO.md` ordering.** The brief asks for the Future Phases section to be the end of `TODO.md`; in the repository (as at HEAD and at every earlier milestone) three sections follow it. The order was kept.
7. **Environment.** Smart App Control blocks freshly built unsigned executables on this machine (above). The Code Integrity log also shows blocks of `bettercad-cli.exe`, so it may be part of the cause of the earlier intermittent `cli.*` failures (not proven here). The owner has switched it off.
