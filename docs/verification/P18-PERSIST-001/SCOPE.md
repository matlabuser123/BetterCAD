# P18-PERSIST-001 — scope

## In scope (the 22 items)

Define the P18 schema; persist the analysis definition, the loads, the boundary conditions, the `GeometryReference`s, the initial condition, the steady solver settings and the transient settings; preserve units;
preserve unresolved references explicitly; exclude the thermal solver and result arrays; reject malformed files, invalid values and duplicate ids; backward compatibility; deterministic serialization; full round trip;
save / load / re-solve; compare the re-solved semantics; adversarial review; regression; evidence.

## What was added

* `src/io/json/ThermalAnalysisJson.cpp` — the writer and the reader (new).
* `src/io/json/ObjectJson.hpp` (two declarations and one include), `src/io/json/DocumentJson.cpp` (one `dynamic_cast` branch and one string-literal branch), `src/io/CMakeLists.txt` (one source, one private link).
* `tests/io/ThermalAnalysisFileTests.cpp` — **48** cases; `examples/models/thermal_plate.bcad` — a hand-written reference document.
* `tests/architecture/CheckThermalPersistenceOwnership.cmake` with **22** violating fixtures and one valid one (**24** tests), and **11** zero-match filters.
* ADR-056 and this evidence directory.
* One qualified test **changed**: `ThermalData_SavingADocumentWithAThermalAnalysisIsRefusedUntilPersistenceExists` pinned the refusal this milestone removes ("a known refusal replaced rather than a surprise", as its own comment says). It is renamed `…WritesItsIntentAndOnlyThat` and now asserts that the document saves, carries a `thermal-analysis` object, and loads back to an equal definition.

## What was not done, and why

* **No `NamedBoundarySet` target, no output-time schedule, no material reference.** None exists in the P18 data model ([AUDIT.md](AUDIT.md)).
* **No migration code.** No earlier P18 schema existed; thermal analyses could not be saved before this milestone. Backward compatibility means the pre-P18 documents in the repository, which are tested.
* **No schema version bump.** Adding an object type is additive and fails safely in both directions ([BACKWARD_COMPATIBILITY.md](BACKWARD_COMPATIBILITY.md)).
* **No change to the JSON parser.** A repeated key is last-wins for every object type; making it strict is a format-wide decision for the owner ([MALFORMED_INPUTS.md](MALFORMED_INPUTS.md)).
* **No change to `validate(ThermalAnalysisDefinition)`**, which accepts `FaceName{}` and is O(n²) in the number of records. Both are qualified P18-DATA behaviour; the file refuses the invalid handle itself.
* **No persistence of the undo/redo history.** The format does not persist history for any object type; `SerializationIsByteIdentical…` shows that the same final state reached through commands, an undo and a redo gives the same bytes.
* **No CLI verb, no Qt panel, no reference-model suite.** P18-CLI-001, P18-REFMOD-001 and P18-QUAL-001 are not started.
* **No change to any other qualified file.** The freeze lists every other `src`, `include` and `apps` file as byte-identical to the previous commit.

## The checklist item the brief and the repository word differently

The brief asks that the Future Phases section be the absolute end of `TODO.md`. In the repository (as at HEAD and at every earlier milestone) three sections follow it. The order was kept and is reported, not changed.
