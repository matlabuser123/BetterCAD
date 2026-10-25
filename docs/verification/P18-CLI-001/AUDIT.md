# P18-CLI-001 — the audit that came before the code

```text
baseline        HEAD 2ffba66f6676fe459de225ea0839b21b40fa8681   tree f82824e07a87b6fc15183ae015cfe8c3dba7b6e1   origin/main == HEAD
working tree    clean except .claude/
`bettercad`     not on PATH ("which bettercad" finds nothing); the executable is `bettercad-cli`, target bettercad_cli,
                built to <build>/bin/bettercad-cli.exe by every preset
```

The brief says, twice, to follow the existing CLI's naming and conventions and to invent none. So the audit came first, and it answered nearly every question the brief asks.

## What the CLI already decided (and where)

| Question the brief asks | The tree's answer | Where |
| --- | --- | --- |
| command naming | `<family>-<noun>` for a report, `<family>-<noun>-add` / `-remove` for an edit: `structural-settings`, `structural-solve`, `structural-load-add`, `mesh-local-add` | `Cli.cpp` `kCommands`, `StructuralEdits.cpp` |
| document argument | the first positional argument, always a path | every command |
| structured output | `--json` on a report; `JsonValue` (insertion order, two-space indent, trailing newline); a quantity is `{"value","unit"}`, never a bare number | `StructuredOutput.hpp` |
| streams | a command that cannot answer prints **nothing** to stdout and `<program> <command>: <code>: <message>` to stderr | `failure()` in `Cli.cpp` |
| exit codes | **three**: 0 success, 1 the command ran and the answer is no, 2 the command line was invalid | `Cli.hpp`; `help` prints the contract |
| a report that is the answer to an acceptance question | prints its payload **and** exits 1 when it rejects | `structural-validate` |
| an edit | a function over an already-loaded document; one registration makes it a single-shot command **and** a `batch` verb; nothing is written unless it succeeded | `Edits.hpp`, `EditRegistry.cpp`, ADR-009 |
| a face | the shared `face:<feature>:<role>[:<entity>]` grammar, never a node or a facet | `parseFaceReference` in `Selectors.cpp` |
| a quantity | the one dimension-generic parser with the one unit catalogue; a bare number is SI; **there is no Celsius** | `parseQuantity` in `Arguments.hpp`, `UnitCatalog.cpp` |
| backend chatter on stdout | contained in the Netgen adapter (`SilentBackendScope`), not in any consumer | `src/meshing/netgen/NetgenBackend.cpp`, P16-CLI-001, P17-CLI-001 `BACKEND_STDOUT.md` |
| fresh-binary proof | `$<TARGET_FILE:bettercad_cli>` — an absolute build-tree path, never a bare name | the structural workflow and process tests |
| zero-match protection | `ctest --preset` has `noTestsAction: error`; `ZeroMatchGuard.cmake` asserts a discovered **count** | `tests/cli/ZeroMatchGuard.cmake` |

So this milestone adds **no new command style, no new output envelope, no new unit semantics and no new exit-code policy.** It adds twelve verbs in the existing shape.

## What the thermal core offered

All of the P18 chain is committed and qualified, and the prerequisite gate was read from the repository rather than from the TODO labels:

```text
P18-ARCH-001 .. P18-PERSIST-001   sixteen directories under docs/verification, each with README.md and FREEZE.md; each TODO block records its commit
P15 / P16 / P17                   QUALIFIED (P15-QUAL-001, P16-QUAL-001, P17-QUAL-001 directories present; the TODO status block says so)
P17-CLI-001                       committed and qualified: the pattern this milestone follows
```

**The one gap was real.** `structural::solveStructuralAnalysis` is the production entry a structural CLI calls. The thermal chain had no equivalent: the order of its ten qualified links existed only in `tests/renderer/ThermalVizTestSupport.hpp`. A CLI that composed them would have been a second copy of the order in the layer that must hold none of it. [ADR-057](../../architecture/decisions/ADR-057-the-thermal-chain-has-one-production-entry-and-the-command-line-is-an-adapter-over-it.md) records the decision: one new, additive file in the thermal module, `runThermalAnalysis`, tested bit for bit against the test support's independently written chain.

## What this milestone decided that the project had not

* **Ids are the caller's.** `structural-load-add <id>` takes the id; `ThermalLoadId` and `ThermalBoundaryConditionId` are user-chosen in P18-CMD-001 too. The brief asks the response to report "the resulting id"; the id is the one the user gave, echoed in the response line, and `thermal-settings --json` reports it from a fresh process. A generated id would have needed a new allocation rule the command layer does not have.
* **One thermal analysis per document.** Two are refused by name (`ambiguous_thermal_analysis`), exactly as P17 refuses two structural ones; the brief's "do not pretend an arbitrary selector exists" applies.
* **`thermal-validate` runs the analysis.** The validation report's `status()` treats a skipped rule as a failure, and everything after the inputs is skipped until a result exists; a report taken before a solve could only say FAIL. So validating a result means producing one, and the payload says how far the chain got (`stoppedAt`).
* **Stale geometry and a stale mesh are not presentable to a one-shot process** (see [FAILURE_PATHS.md](FAILURE_PATHS.md)). This is the same conclusion P17-CLI-001 reached and recorded.

## Things the audit confirmed would have been mistakes

* A `--json` flag on the edit verbs. The edit driver prints a line and saves; adding structured output to it would have changed shared infrastructure that P13–P17 qualify. The id is echoed in the line and verified through `thermal-settings --json`.
* Reporting `solve_failed` for every refusal. Found by the first CLI test of a `final_only` transient, and fixed in the entry (ADR-057 §4).
* Comparing numbers as text in the scripted workflow. CMake's `string(JSON)` re-prints a number through a double at 17 digits (`0.3` comes back as `0.29999999999999999`); the script compares numbers as numbers and whole payloads as raw text.
