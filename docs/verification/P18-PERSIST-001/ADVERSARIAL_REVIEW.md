# P18-PERSIST-001 — adversarial review

```text
TWO REVIEWS: the author's own pass while building, then an INDEPENDENT read-only review by a separate agent that was given the requirements and the code and not the conclusions.
The independent review found no high-severity defect: the reader and writer closely follow the P17 precedent.
It found 4 medium findings, 6 low ones, 8 holes in the architecture gate and a list of vacuous or overclaiming tests. Every one has a disposition below.
```

## Part 1 — the independent review, finding by finding

| # | Finding (severity as reported) | Disposition | Pinned by |
| --- | --- | --- | --- |
| M1 | **medium** — the writer can emit an id the reader refuses: `validate` only asks that an identity be valid, `readId` caps a file id at 2^53−1, so a load id of 2^53 or a condition id of 2^60 saves and then never loads; the comment said it could not | **FIXED**: the writer refuses any id above the cap, by name, before writing; no file is written; the largest permitted id round-trips | `AnIdentifierAboveTheFileCapIsRefusedOnSaveRatherThanWrittenAndLostOnLoad`; mutation PER39 |
| M2 | **medium** — duplicate JSON keys are silently last-wins (`{"flux": 1, "flux": -1}`), and nothing pins it | **RECORDED AND PINNED, not changed.** It is format-wide: the parser collapses a repeated key before any reader sees it, for every object type. A strict parser would change how every document loads; that is the owner's decision, not one object type's. Duplicate **identities** (the brief's rule) are refused | `ARepeatedKeyInTheFileIsLastWinsAsEverywhereInTheFormat` |
| M3 | **medium** — the currentness test does not prove its title: `ThermalResultSource` has no document identity, and an equal analysis revision could alias | **MEASURED, test strengthened, not a defect.** The copy is edited until its revision **equals** the original's while its content differs; the original's result is stale for it, for exactly the **mesh** (a result is bound to the stamp of the mesher that made it) and the **loads** (the derived chain compares canonical intent **by value** — the first of the two regimes recorded by P18-CMD-001). An *unedited* copy is stale for the mesh alone. The revision alias cannot occur because the revision is not what the chain compares | `AResultOfOneDocumentDoesNotAttachToACopyThatDiffers` |
| M4 | **medium** — "cross-preset identical" had no golden bytes: swapping the `mesh` and `mode` insertions would pass every test | **FIXED**: the hand-written reference file is loaded and re-saved and must equal itself character for character (thermal section and whole file); two FNV constants pin the golden sections' exact bytes in the source | `TheHandWrittenFileIsExactlyWhatTheWriterProduces`, `TheGoldenSectionsHaveFixedBytes`; mutations PER40, PER41 |
| L1 | `ACorruptFileLeavesTheOpenDocumentAlone` cannot fail: `loadDocument` takes no reference to the open document | **TEST REWRITTEN honestly**: atomicity is by construction, and two `static_assert`s on the loader signatures pin that shape; the runtime half now also covers a good analysis followed by a broken object and the reverse, and a repeat load giving the same diagnostic | same test |
| L2 | overclaiming tests: "data that is not an object" passed `{}`; the reference-file test stopped at preparation; the fake-result test's `ContainsSubstring("data")` matches every message; many checks used `"id"`, `"mode"`, `"face"`; the 300-character compare; the locale test is vacuous when no locale exists | **ALL FIXED**: non-object `data` tested with `[]`, `3`, `null`, `"x"`, `true` and the message asserted; the reference file now meshes, solves 20 states, recovers, balances and validates; the fake-result test asserts the injected key is named; 21 substring checks now name the full path (`.loads[0].flux`); the whole thermal object is compared; the locale test tries eight locale names (and still says so, honestly, if none is installed) | `RejectsMalformedThermalIntent`, `TheReferenceFileRegeneratesMeshesAndSolves`, `FakeResults…` |
| L3 | per-kind shape and type gaps (convection+temperature, prescribed+ambient, volumetric+flux; prescribed missing temperature; strings for `initial_temperature`, `time_step`, `end_time`; `solver`/`transient` non-objects and `null`; `mesh` as a string or float) | **ADDED** | `RejectsMalformedThermalIntent` |
| L4 | dangling references load and this is unpinned; `face.feature: 0` | **PINNED, and one rule added**: a dangling mesh control and a control of another kind load (the document checks no dependencies on load, for any type) and are refused later by `requireThermalModel`; a face of a nonexistent feature loads as unresolved intent; **`face.feature: 0` is refused** as a record that names nothing, matching the command layer | `DanglingTargetsAreIntentAndAnInvalidFeatureHandleIsNot`; mutation PER38 |
| L5 | duplicate-id checks are O(n²), twice (the reader and `validate(definition)`) | **RECORDED, not changed.** 3000 loads + 3000 conditions round-trip in the unit suite; a hand-written file with ~10⁵ records would be slow. `validate(definition)` is qualified P18-DATA code and was not touched | |
| L6 | fixture metadata said `6061-T6` while the material is S355 | **FIXED** | |

### Gate holes (`CheckThermalPersistenceOwnership.cmake`)

| # | Hole | Disposition |
| --- | --- | --- |
| G1 | only `ThermalAnalysisJson.cpp` scanned for rules 2–7; the dispatch could hold derived state | **FIXED**: the dispatch and the shared header are scanned for derived names (fixture `dispatch-derived`) |
| G2 | `#  include <…>` (a space after `#`) bypassed rules 1 and 8; the allow-match for the analysis object header was unanchored | **FIXED**: `#[ \t]*include`; anchored allow-match (fixtures `spaced-include`, `lookalike-include`) |
| G3 | no rule forbade resolution in the reader (a `const Document&`, `findObject`) | **FIXED**: rule 9 (fixture `resolves`) |
| G4 | rule 4 missed `MaterialId`, `capacity`, `diffusivity`, `rho`, `emissivity`, `Material` | **FIXED** (fixture `material-id`) |
| G5 | no units or number-formatting rule | **FIXED**: rule 10 — `273.15`, `Celsius`, `1000.0`, `.in(`, `to_string`, `printf`, `fixed`, `setprecision` (fixture `units`) |
| G6 | rule 7 was presence-only | **FIXED**: it counts — every owner used at least as many times as there are places that need it (two face writes, five strict-key reads, five core validator calls, eight number reads); fixture `weak-strictness` removes four strict-key reads and keeps one |
| G7 | the stderr regex also matched "11 violation(s)"; no fixtures for `mutable`, `isCurrent` | **FIXED**: anchored on `check failed: 1 violation\(s\):`; fixtures `mutable`, `current-check` |
| G8 | `NoDerivedStateReachesTheFile` checks the writer's own output, which cannot produce those keys | **ACCEPTED**: it is a regression net; the strong evidence is the structural argument, the reader's rejection of every such key, and the gate |

Remaining known limitation of the gate: the comment stripper removes `//` to the end of the line, including a `//` inside a string literal, so code after `"a//b";` on one line would be hidden. The persistence file contains no `//` inside a string; the source audit prints the same counts through the same stripper.

## Part 2 — observations (not defects), for the owner

* **O1 — repeated JSON keys are last-wins, format-wide** (M2 above).
* **O2 — the data model accepts `FaceName{}`** (found by P18-CMD-001); the commands and the file refuse it, `validate` does not.
* **O3 — dangling targets load**: a thermal analysis may name a meshing control that does not exist, or one of another kind; the document does not check dependencies on load for any object type. Refused later, with a diagnostic, by the derived chain.
* **O4 — object names are validated identifiers**, so a Unicode or escaped analysis name is refused by the document (four of five probes), identically each time.
* **O5 — no `NamedBoundarySet`, no output-time schedule, no material reference** in the P18 data model; the brief's items about them have no subject ([AUDIT.md](AUDIT.md)).
* **O6 — the first P18 file is version 2**; there is no earlier P18 schema to migrate.

## Part 3 — the author's questions, answered

| Question | Answer |
| --- | --- |
| Can K, C or Q reach the file? | No: not a member of any document object; not declared where the writer is written; gate rule 2 |
| Can T or a transient history? | No: the same; a 3.9 MB result leaves a 5251-byte file unchanged |
| Can grad(T), q, balance or validation? | No: the same; an injected key is refused by name |
| Can generic serialization deep-serialize derived state? | No: `objectToJson` dispatches on type and each type's writer writes only that type's fields; a result is not on any document object |
| Can a solve, POST, BALANCE, VALID or VIZ change the saved bytes? | No — eight states compared byte for byte |
| Can a large transient result enlarge the file? | No |
| Can mesh-local ids be saved as targets? | No; `node`/`element`/`facet`/`dof` keys are refused |
| Can a reference be replaced by its resolved entities, dropped or rebound? | No: the reader has no document, mesher or regenerator; preserved in all eight forms |
| Can stable ids be regenerated on load? | No (mutations PER19, PER20 killed) |
| Can duplicate ids be accepted first/last-wins? | No (PER21, PER22 killed) |
| Can a malformed file partially mutate the document? | No: by construction, pinned by signature |
| Can a late error leave earlier records imported? | No: five late-error shapes, same diagnostic twice |
| Can negative q″ or q‴ be abs()-corrected? h? | No (PER03, PER04, PER32 killed); h ≤ 0 is refused, never `abs`'d |
| Can an invalid dt silently become a default? | No: refused, naming the section |
| Can temperatures be saved as Celsius? Time in ms? | No (PER05, PER29, PER08, PER30 killed); gate rule 10 |
| Can OCCT millimetres leak? | No: there is no length in the section |
| Can doubles round-trip through display strings? | No: typed doubles; the formatter is the format's |
| Can locale alter the decimal point? | No — pinned when a comma locale is installed |
| Can unordered iteration, timestamps, solver statistics or the revision alter bytes? | No (gate rule 6; PER14 killed) |
| Can load run the solver? | No: the loader has no solver; a fresh mesher holds nothing after a load |
| Can a fake temperature array or "validated=true" become authority? | No: 17 keys refused by name |
| Can an unknown load or condition type map to a default? | No (PER28 killed) |
| Can a pre-P18 file fail for lacking a thermal section? | No: three real files load |
| Can compatibility invent defaults for a corrupted current file? | No: required fields stay required |
| Can save/load/save change bytes each cycle? | No: a fixed point |
| Can k/rho/cp be duplicated into the section? | No: gate rule 4; the P15-authority test |
| Can an old result become current because ids match? | No: [ROUND_TRIP.md](ROUND_TRIP.md) |
| Can zero targeted tests still report PASS? | No: the zero-match guard holds the total (48), nine exact-name groups and the gate (24) |
