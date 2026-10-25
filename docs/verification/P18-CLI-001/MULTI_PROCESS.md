# P18-CLI-001 — multi-process save / load / re-solve

## The claim

A thermal analysis built by the CLI, one process per edit, is complete in the **file alone**. A later process, started from nothing, loads that file, runs the whole chain again and reaches the same answer. Nothing a previous process held in memory — a pointer, a cached mesh, a solver state, a singleton — is needed, and no thermal result is persisted.

## Where it is proved

`tests/cli/ThermalWorkflow.cmake`, run as ctest `cli.thermal.workflow` against the real `bettercad-cli` executable of each preset. It uses `execute_process`, so every step is its own operating-system process; there is no shared address space to leak through. The script has **74 processes** (asserted exactly; a script that skipped steps and exited 0 fails). Each one's exit code, raw stdout/stderr byte counts and stdout SHA-256 are written to `ledger.txt`, and its stdout and stderr are kept as files under `payloads/`.

| What a fresh process is shown | Step(s) | Assertion |
|---|---|---|
| The edits of four earlier processes (`thermal-analysis-add`, `thermal-load-add`, two `thermal-boundary-add`) | 6 | `thermal-settings` reports mode, 1 load, 2 boundary conditions, the stable ids, the canonical face references and `K` unit |
| No result is stored | 6 | `resultStored` is false |
| A solve writes nothing | 7–9 | SHA-256 of the document is identical after validate, solve, the five summaries and `mesh-info` |
| A second process re-solves the same file | 10 | the whole `--json` payload is **byte-identical** to the first process's |
| An edit invalidates and nothing stale survives | 11 | adding a load in one process changes the next process's peak temperature |
| A removal restores | 12 | removing the load by id restores the original payload byte for byte, and the document bytes |
| A rejected edit leaves the file alone | 17 | every refused add/remove (duplicate id, unknown id, invalid value, usage faults) leaves the SHA-256 unchanged |
| A transaction is atomic | batch steps | a `batch` with one bad line writes nothing |
| The same analysis built by `batch` | batch steps | its solve payload equals the one built step by step |
| Independent of working directory and path | two steps | same payload from another cwd and from a path containing a space |
| Transient | transient block | built through the CLI, solved twice in two processes, byte-identical payloads |

The in-process counterparts (`ThermalCli_SaveLoadResolveAgreesAcrossFreshRuns`, `ThermalCli_AnEditChangesTheNextSolveAndNothingStaleSurvives`, `ThermalCli_SolveDoesNotModifyCanonicalBytes`) prove the same with the document round-tripped through `io::saveDocument` / `io::loadDocument` and compared with the core run on the reloaded copy.

## What "multi-process" does not mean

Sequential, independent processes. There is no concurrent-writer protocol, no locking and no merge: collaboration is P22's. Two processes editing the same file at once are outside this milestone.

## The persisted-result boundary

`thermal-settings --json` reports `resultStored: false` because the document schema has no field for a thermal result. P18-PERSIST-001 qualified that boundary; this milestone does not add a second route around it. The CLI offers no `--save-result` and no cache.
