# P18-PERSIST-001 — backward compatibility, and what was and was not tested

```text
SCHEMA VERSION:  2, unchanged. The first BetterCAD document containing P18 thermal intent is version 2.
PRE-P18 FILES:   load unchanged; thermal analyses = none; re-saving invents none.
EARLIER P18 SCHEMA: there is none. Thermal analyses could not be saved before this milestone, so no thermal file exists to migrate.
FUTURE VERSIONS: refused, not guessed.
```

## Why the version does not move

`DocumentFile.hpp` carries the rule (P13-PERSIST-001): **bump only for a change an existing reader would get wrong.** Adding an object type is additive, and both directions fail safely without the number doing any work:

| | Behaviour |
| --- | --- |
| a new reader, an old file | loads: an absent thermal section means "no thermal analyses", which is what an older document meant by not having one |
| an old reader, a new file | refuses **by name**: "unknown object type 'thermal-analysis'" — never a silent skip, never a guess |

The comment makes the point against itself: "the schema has grown a great deal since this was written … and the number has correctly never moved."

## What was tested — and only that

The brief asks for real files, not synthesized history. The corpus is the three documents that predate P18 and are in the repository, **which the tests read from `examples/models/`** (`BETTERCAD_EXAMPLE_MODELS_DIR`):

| File | Predates P18 | Contains | Result |
| --- | --- | --- | --- |
| `meshed_plate.bcad` | yes (P16) | geometry, a mesh control | loads; `thermalAnalysisIds` empty; re-save has no `thermal-analysis`; version 2 |
| `analysed_plate.bcad` | yes (P17) | geometry, a mesh control, material, a **structural** analysis | same |
| `plate.bcad` | yes | geometry | same |

Each is asserted to contain no `thermal-analysis` text *before* it is loaded, or the test would prove nothing. The blobs of all three are part of the backward-compatibility fingerprint in [FREEZE.md](FREEZE.md), so a fixture silently replaced by a P18 file would change the fingerprint.

`AThermalAndAStructuralAnalysisShareOneFileWithoutInterfering` is the compatibility test in the other direction: the P17 analysis next to a P18 one round-trips, and the thermal object's bytes do not depend on the structural one being there.

## What was NOT tested, because it does not exist

* **A migration from an earlier P18 schema.** There was none. No migration code was written, and the brief's "do not invent fake compatibility" is respected: nothing in the tree claims to read a file that never existed.
* **Migration of ids or defaults.** There is no legacy format lacking ids, so the brief's "deterministic migration ids" question has no subject. A missing required field in a current file is **refused**, never given a default (`RejectsMalformedThermalIntent`: a missing `mesh`, `mode`, `solver`, a solver field, a load `id`, a flux, an ambient).

## Future versions

`AFutureSchemaVersionIsRefusedNotGuessed` changes `"version": 2` to `3` in a real saved file and requires a refusal naming the version. The number the first P18 file carries is asserted to be `2`.

## Unknown fields

The format's policy is strict rejection, so the thermal section follows it: a key the build does not know is refused by name, at the record, solver, transient and face levels alike ([MALFORMED_INPUTS.md](MALFORMED_INPUTS.md)). A harmless unknown field is therefore **not** silently dropped, and a *generated* one (`temperature`, `heat_flux`, `balance`, `validated`, `current`, `K`, `C`, `Q`, `dof_map`, `prepared_loads`, `solution` …) can never become anything: `FakeResultsAndFakeStatusCannotBecomeAuthority` refuses seventeen such keys at the top level, each naming the key, and three inside a record.

## Corrupted current-version files get no defaults

A current-version file with a missing required field is refused — the migration/default path that a version bump would define does not apply to a file that claims to be current. `solver` in particular is always written and wholly required.
