# P18-PERSIST-001 — malformed files, invalid values, duplicate identities, atomic load

```text
68 refusal call sites in 65 sections (several loop over a list of inputs), each asserting the diagnostic NAMES THE FIELD (".loads[0].flux", not "flux").
Every refusal carries the path of the offending field; none crashes; none partly loads.
```

## Where each rule lives

| Rule | Owner | Why there |
| --- | --- | --- |
| structure (an object, an array, a string, a number) and unknown keys | `requireObject`, `readNumber`, `readId`, `requireField` — format-wide | the one strict reader every object type uses |
| unknown enumerator | the key tables in `ThermalAnalysisJson.cpp`; never mapped to a default | an unknown future value must not be guessed |
| which keys a kind may carry | `ThermalAnalysisJson.cpp` | `requireObject` can only allow the union of keys |
| a record's value (finite, ≥ 0 K, h > 0, …) | `thermal::validate(...)`, **the core's own**, called not copied | one rule, one place |
| duplicate identities | `ThermalAnalysisJson.cpp` (a file rule; `validate(definition)` repeats it later) | the file can name the **path** of the second occurrence |
| the mode-complete rule | `ThermalAnalysis::create` | the one rule no per-record check can express |
| the invalid feature handle | `ThermalAnalysisJson.cpp` (`readFace`) | matches the command layer |
| an identifier above the file cap | the **writer** (and `readId` on read) | the writer must not emit what the reader refuses |

## Structure and types

| Input | Refused with |
| --- | --- |
| `data` that is `[]`, `3`, `null`, `"x"`, `true` | "…data: expected an object" |
| `data: {}` | the first missing required key: `…data.mesh` |
| a top-level key this build does not know (`ordering`) | unknown key, by name |
| missing `mesh` / `mode` / `solver` | the field, by path |
| `mesh` as a string, a float, `-1`, `null` | `…data.mesh` |
| `mode` as a number | `…data.mode` |
| `mode: "nonlinear"` | "unknown analysis mode 'nonlinear'" |
| `loads` not an array; a load that is not an object | `…data.loads`, `…loads[0]` |
| a load of unknown `type` (`radiation`) | "unknown load type 'radiation'" |
| a condition of unknown `type` | "unknown boundary condition type 'radiation'" |
| a surface flux with no `flux`; with no `face` | `…loads[0].flux`, `…loads[0].face` |
| a volumetric source carrying `face` or `flux`; a surface flux carrying `generation`; a prescribed temperature carrying `coefficient` or `ambient`; a convection carrying `temperature` | "a … does not carry this", naming the key |
| a prescribed temperature with no `temperature`; a convection with no `ambient` | the field, by path |
| a load with no `id` | `…loads[0].id` |
| a coefficient given as `"twenty-five"`, an ambient as `true`, a value as `null`, a number as `[25.0]` | the field, by path — **never parsed from a string** |
| an id as `"7"`, `7.5`, `-1`, `9007199254740993`, `18446744073709551616`, `1e30` | `…loads[0].id` |
| the largest permitted id (2^53 − 1) | accepted |
| `solver` as a string or `null`; `transient` as a string; `initial_temperature`, `time_step`, `end_time` as strings | `…solver`, `…transient`, `…initial_temperature`, `…transient.time_step`, `…transient.end_time` |
| a solver or transient section missing a field; an unknown field in either | the field, by name — **never defaulted** |
| a truncated file | a parse error |

## Invalid values (refused by the core's validators; never corrected)

| Input | Refused |
| --- | --- |
| a convection coefficient of `0`, `-25`, `-1e-300` | `InvalidArgument` at `…boundary_conditions[0]` — **never `abs()`'d** |
| a held, ambient or initial temperature of `-273.15`, `-1`, `-0.5` K | `InvalidArgument`; the Celsius mistake is the one caught |
| `0.0` K | **accepted** — the limit of the scale, the data model's policy |
| a load or generation of `1e999`; of `NaN`, `Infinity`, `-Infinity`, `nan` | a **parse error** ("number overflow"; not JSON) — an infinity never becomes a number for the reader or the validator to see |
| a negative flux or generation; a zero one | **accepted** — a sink is ordinary canonical intent |
| a solver tolerance or pivot floor of `0` or negative | `InvalidArgument`, naming the solver section |
| `time_step` `0`, negative, or longer than `end_time`; `end_time` `0` or negative | `InvalidArgument`, naming the transient section — **never replaced by a default** |
| an unknown output policy (`sometimes`); a missing one; an unknown extra field (`times`) | the field, by name |
| a transient analysis without an initial temperature, or without transient settings | "a transient analysis needs an initial temperature" / "…a time step and an end time" |
| a steady analysis carrying either | "a steady analysis has no initial condition" / "…has no time step or end time" |
| a face whose feature handle is the invalid one | "a target must name a feature" |

## Duplicate identities

| Input | Outcome |
| --- | --- |
| two loads with one `ThermalLoadId` | refused: "the load id 5 is used twice in this analysis", at `…loads[1].id` |
| two conditions with one `ThermalBoundaryConditionId` | refused: "the boundary condition id 2 is used twice…", at `…boundary_conditions[1].id` |
| two analyses with one object id | refused by the document: "object:5 is already in use" |
| a load id and a condition id with the same number | **accepted** — different strong types |
| two loads with the **same target and value** but different ids | **accepted and both preserved** — duplicate physical intent is not a duplicate identity; whether two loads on one face are sensible is the validator's question |
| two conflicting prescribed temperatures on one face | **accepted and both preserved** — `P18-BC` reports the conflict when it prepares them |

Neither first-wins, last-wins, merged nor regenerated: a duplicate identity is refused, and the destination document is unchanged.

## Repeated JSON keys: last wins — format-wide, pinned, an owner decision

`{"flux": 1.0, "flux": -1.0}` loads as −1.0. The parser collapses a repeated key before any reader sees it, exactly as for every other object type in the format. Making the parser strict would change how every document loads and is not this milestone's to decide; `ARepeatedKeyInTheFileIsLastWinsAsEverywhereInTheFormat` pins the behaviour so it is recorded and cannot change silently. Recorded as finding M2 of [ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md) and as an observation for the owner in the README.

## Atomic load

```cpp
static_assert(std::is_same_v<decltype(io::documentFromJson(std::declval<std::string_view>())), Result<Document>>);
static_assert(std::is_same_v<decltype(io::loadDocument(std::declval<const std::filesystem::path&>())), Result<Document>>);
```

The loaders build and return a **new** `Document`; no entry point fills an existing one, so a failed load cannot have changed the document the user has open — by construction. The two `static_assert`s pin that shape (adding an in-place loader would have to change the test). `ACorruptFileLeavesTheOpenDocumentAlone` then corrupts a file where a half-publishing reader would leave something behind and requires the open document to be byte-for-byte unchanged, its analysis revision unchanged, and the **same diagnostic** from a second attempt:

* a late wrong-typed field (the third load of three);
* a late duplicate id (the first and the third load share one);
* a late malformed reference (the second of two conditions);
* late invalid transient settings;
* a truncated file;
* a good analysis followed by a broken object;
* a broken object followed by a good analysis.

## The identifier cap, on the writer

`AnIdentifierAboveTheFileCapIsRefusedOnSaveRatherThanWrittenAndLostOnLoad`: `validate` asks only that an identity be valid, and `readId` caps a file identifier at 2^53−1, so a load id of 2^53, or a condition id of 2^60, used to save and then never load. The writer now refuses it with "the load id 9007199254740992 exceeds the largest identifier a file can hold…", writes no file, and the largest permitted id round-trips. Found by the independent review (M1).
