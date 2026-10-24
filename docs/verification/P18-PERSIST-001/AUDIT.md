# P18-PERSIST-001 — the persistence architecture audit

```text
SUBJECT:  what already exists, what P18 may reuse, and the questions the brief asked me to DECIDE that the project had already decided
RAN:      before any production code was written
RESULT:   P18 persistence adds ONE OBJECT TYPE to the existing document format, exactly as P17-PERSIST-001 did for the structural analysis.
          No second file, no version bump, no migration code, no new validation framework, no new reference encoding, no new determinism mechanism.
```

## The capability table

| Capability | Existing type / API | Where | Reusable for P18? | Decision |
| --- | --- | --- | --- | --- |
| document file format | `.bcad`, transparent JSON, `nlohmann::ordered_json`, 2-space indent, trailing newline | `io/DocumentFile.hpp`, `json/JsonReader.cpp` (`dumpJson`) | yes | reuse unchanged |
| schema version | `kDocumentFormatVersion = 2`, `kOldestReadableDocumentVersion = 1` | `io/DocumentFile.hpp` | yes | **no bump** — see below |
| object dispatch | `objectToJson` (a `dynamic_cast` chain) and `objectFromJson` (a string-literal chain) | `json/DocumentJson.cpp` | yes | add one branch each way |
| strict reading | `requireObject(value, path, allowedKeys)` | `json/JsonReader.hpp` | yes | **unknown key = reject** |
| path-named errors | `parseError`, `atPath`, `childPath`, `indexPath` | same | yes | every refusal names a path |
| ID reading | `readId`, capped at `kMaxFileId = 2^53 - 1` | same | yes | reuse; the **writer** now refuses what the reader would (finding M1) |
| face reference | `faceNameToJson` / `faceNameFromJson` | `json/ObjectJson.hpp` | **yes** | reuse — a second encoding would be a second contract |
| atomic save | temp file + `rename` | `io/FileIo.cpp` | yes | reuse unchanged |
| atomic load | `documentFromJson` builds a local `Document` and returns it | `io/DocumentFile.cpp` | yes | atomic **by construction**; pinned by `static_assert` on the loader signatures |
| writer failure path | `objectToJson` returns `Result<Json>` | `json/DocumentJson.cpp` | yes | the thermal writer uses it for the one defence-in-depth refusal |
| material persistence | `materialToJson`, P15 (thermal conductivity, specific heat, density are P15 properties) | `json/MaterialJson.cpp` | n/a | **not touched** — P18 stores no material (P18-MAT-001) |
| derived-state exclusion | structural: a `ThermalAnalysisDefinition` has seven members and none is derived | `thermal/ThermalAnalysisObject.hpp` | yes | same argument as P16 and P17 |
| determinism | insertion-ordered keys, the user's record order, shortest-round-trip doubles | same | yes | same mechanism |
| golden fixture | `examples/models/analysed_plate.bcad` + a test that reads it | `tests/io/StructuralAnalysisFileTests.cpp` | yes | add `thermal_plate.bcad` and its tests |
| test layout | `tests/io/<Thing>FileTests.cpp`, one target | `tests/CMakeLists.txt` | yes | one new file, one line |

## The questions the brief asked, and where the project had already answered them

| The brief asks | The answer in the tree |
| --- | --- |
| What is the canonical document? What serializes? | The document is its objects; an object's `data` is what its `*ToJson` writes. A `ThermalAnalysis` object was **not savable at all** before this milestone: `objectToJson` returned "objects of type 'thermal-analysis' cannot be saved". So no existing file can contain one, and nothing needs migrating. |
| Where do derived caches live? | In the caller: `ThermalPostResult`, `ThermalBalanceResult`, solutions and systems are returned by their producing functions and held by the caller (`vztest::Derived` in the tests). None is a member of a document object. |
| JSON, YAML, binary? | Transparent JSON. |
| How are IDs, variants, GeometryReference, enums, floats, versions handled? | IDs as integers capped at 2^53-1; variants by an explicit `"type"` string key; `FaceName` by `faceNameToJson`; enums by stable string keys (never integers, never `toString`); floats by nlohmann's shortest round-trip formatter, locale-independent; one global integer version. |
| Unknown-field policy? | **Strict rejection** (`requireObject`'s allow-list). A fake `"temperature"` array in a thermal section is refused by name. |
| How are migrations implemented? | None has ever been needed: the version "has correctly never moved" (the comment in `DocumentFile.hpp`). |
| Is loading transactional? | Yes, by construction: no entry point fills an existing document. |
| Timestamps / statistics / paths in the file? | None are written by any object type; the only per-document varying value is the header's document UUID, which a `Document` keeps from its file. |

## Decisions the audit settled

1. **No version bump.** `DocumentFile.hpp` carries the rule as a long comment (P13-PERSIST-001): bump only for a change an existing reader would get *wrong*. Adding an object type is additive, and both directions fail safely: a new reader loads an old file (an absent section means "none"), and an old reader refuses a new file *by name* ("unknown object type 'thermal-analysis'") — never a silent skip. The **first BetterCAD document containing P18 thermal intent is therefore version 2**, the version every document since P13 has had.
2. **Unknown fields are rejected**, as everywhere in the format. The brief offered both policies and said to follow the existing one.
3. **Validation lives in the core**, called and not copied: `validate(ThermalLoad)`, `validate(ThermalBoundaryCondition)`, `validate(ThermalSolverSettings)`, `validate(TransientSettings)`, `validate(UniformInitialTemperature)` and — through `ThermalAnalysis::create` — `validate(ThermalAnalysisDefinition)`, which unlike P17's covers the records, the duplicate identities and the mode-complete rule. The reader calls the per-record validators itself only so that a refusal names the record's **path**.
4. **Order is preserved.** `ThermalAnalysisDefinition` declares that the loads' and conditions' order is the user's and is persisted as given (the opposite of P16's sorted local sizing controls).
5. **Units are fixed SI and named by the key**, not by a unit string: `flux` is W/m², `generation` W/m³, `temperature`/`ambient`/`initial_temperature` K, `coefficient` W/(m² K), `time_step`/`end_time` s. The writer reads `.si()`, the reader calls `fromSi`; no conversion factor exists in the file to be wrong.
6. **P15 stays the sole material authority.** The thermal section has no material, conductivity, density or specific-heat key; the analysis names a *meshing control*, the control names a body, and the body's material is the document's own P15 assignment. Re-solving a loaded document uses that material (`TheMaterialStaysP15sAndIsNotCopiedIntoTheAnalysis`).

## Brief items that do not exist in this data model, stated rather than faked

| The brief says | What P18-DATA actually has | Consequence |
| --- | --- | --- |
| `NamedBoundarySet` as a second `GeometryReference` variant | Only `FaceName`. A named boundary set is a P16 *mesh* concept | no such variant to persist; every selector form of `FaceName` is round-tripped |
| an explicit output-time schedule (`[0.0, 0.25, 0.7, 1.0]`) | `TransientSettings { timeStep, endTime, output: FinalOnly \| EveryStep }` — the schedule is *derived* by P18-TRANSIENT's `TimeGrid` | the three canonical fields are persisted; the grid is never stored (`ATransientWithAShortenedLastStepReSolvesEquivalently`: 0.3 s into 1.0 s) |
| a material reference inside the analysis | none (P18-MAT-001) | nothing to persist |
| an earlier P18 schema version | none existed: thermal analyses could not be saved | no migration to write; "backward compatibility" means pre-P18 documents |
| Unicode / escaped analysis names | object names are validated identifiers; of five probes (`Study2`, `Thermal α`, a quoted/backslashed name, `plate thermal 2`, `café`) only `Study2` is a valid name | the name round-trips exactly; the others are refused by the document itself, identically each time (`ObjectNamesRoundTrip…`) |

## What the audit found that is NOT there

```text
toJson(ThermalPostResult)            does not exist
serialize(ThermalSystem)             does not exist
any thermal serializer at all        did not exist before this milestone
```

Searched across `src/thermal/` and `include/bettercad/thermal/` for `serializ`, `toJson`, `fromJson`, `nlohmann` and `ordered_json`: the one match is a comment in `src/thermal/CMakeLists.txt` saying the module sits "BELOW IO, which will serialize its analysis intent". There is no result serializer — or any other — to keep out of the save path.
