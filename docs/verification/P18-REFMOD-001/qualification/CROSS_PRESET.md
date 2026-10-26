# P18-REFMOD-001 — The three presets against one another

Source: `postrun-summary.txt` (written by `postrun-ref.ps1` from each preset's own build tree after the qualification; raw ledgers, payloads and evidence lines are under `workflow-<preset>/`).

| Question | debug-ext | release-ext | debug-shared-ext |
|---|---|---|---|
| ctest | **5789 / 5789** (4569.6 s) | **5789 / 5789** (4254.5 s) | **5789 / 5789** (4503.8 s) |
| warnings / errors over the clean build | 0 / 0, 742 objects | 0 / 0, 742 objects | 0 / 0, 742 objects |
| no-op rebuild after the build | 1 edge (nothing rebuilt) | 1 | 1 |
| `[refmod][thermal]` (47 cases, 63 168 assertions) | all pass | all pass | all pass |
| fresh-binary ledger | CLI, runner **and tests** binaries: absolute path inside the build tree, size, SHA-256; 0 project DLLs beside them (static) | the same; 0 | the same; **11 / 11 / 12** project DLLs resolved beside the CLI / runner / tests binaries |
| workflow | 58 processes, every exit code asserted | 58 | 58 |
| numeric comparison with the closed forms | 6 cases, 666 assertions pass; strict re-parse of 32 payloads pass | same | same |
| repeat (`--repeat until-fail:5`, 2547 tests) | 100 % (1921 s) | 100 % (1651 s) | — (not in the repeat set, as in every earlier milestone) |

## Identity of results across presets

* **Workflow processes** (exit code, stdout byte count and SHA-256 of the 57 CLI processes, with each preset's build root removed from the line): **57 of 57 identical** for debug-ext vs release-ext and for debug-ext vs debug-shared-ext. **0 differ in exit code.** (The runner's own line is excluded: it prints timings.)
* **JSON payloads** (32 non-empty): same key sets, same text and discrete fields, **0 numeric fields differ in a single bit** (worst relative difference 0).
* **The 20 documents the runner wrote** (and the edited copy of the stale-mesh model): **SHA-256 identical in all three presets**.
* **The 72 evidence lines of the reference suite** (every measured number quoted in this directory's documents — slab reaction, tube errors, cube series, band sizes, lumped errors, …): **identical to the last digit** in all three presets.
* The mesher-pinned assertions the review worried about (9 nodes / 12 tets, `tb.tets == 0`, the node-count ordering, the 0.27 µm frame offset) hold in all three presets; none flipped.

So the thermal chain, the P16 mesher it sits on and the CLI that prints it give **bit-identical results at -O0, at -O3 and when linked as shared libraries.**
