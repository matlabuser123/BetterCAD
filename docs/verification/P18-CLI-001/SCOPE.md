# P18-CLI-001 — scope

## In scope (the 27 items)

Inspect a thermal analysis; add and remove loads; add and remove boundary conditions; steady solve; transient solve; temperature, gradient and heat-flux summaries; the heat-balance report; the validation report; structured diagnostics; correct process exit codes; the stale-geometry, stale-mesh, missing-material, singular-model and invalid-load/BC failures; CLI/core equivalence; multi-process save/load/re-solve; no CLI-only thermal implementation; a fresh-binary proof; raw structured stdout that is clean; zero-match protection; an end-to-end scripted workflow; an adversarial review; the regression; the evidence.

## What was added

* **One entry in the thermal core** — `include/bettercad/thermal/ThermalAnalysisRun.hpp`, `src/thermal/ThermalAnalysisRun.cpp`, one source in `src/thermal/CMakeLists.txt` ([ADR-057](../../architecture/decisions/ADR-057-the-thermal-chain-has-one-production-entry-and-the-command-line-is-an-adapter-over-it.md)). It is additive: not one existing thermal source or header changed.
* **Twelve verbs** — `ThermalReports.cpp` (7 reports), `ThermalEdits.cpp` (5 edits), `ThermalOutput.hpp` (the finite-checking writer, the validation payload, the extremum helper); registered in `Cli.cpp`, `Commands.hpp`, `Edits.hpp`, `EditRegistry.cpp`; `BetterCAD::thermal` added to the CLI library's private links.
* **Tests** — `tests/thermal/ThermalAnalysisRunTests.cpp` (20), `tests/cli/ThermalCliTests.cpp` (85), `tests/cli/MiniJson.hpp` (the strict reader), `tests/cli/ThermalWorkflow.cmake` (74 processes + the strict re-parse), 12 process tests, the ownership gate with 102 violating fixtures (104 tests), exact-count zero-match filters.
* **A reference document** — `examples/models/thermal_bare_plate.bcad` (the plate with its material and mesh control and no analysis), beside the existing hand-written `thermal_plate.bcad`.
* ADR-057 and this evidence directory.

## What was not done, and why

* **No second thermal implementation anywhere.** The CLI contains no physics; the entry contains no arithmetic.
* **No change to any qualified thermal file.** The freeze lists every other `src`, `include` and `apps` file as byte-identical to the previous commit (the only modified shared CLI files are the four that register the verbs).
* **No `--json` on the edit verbs.** They print a line and save; the id is the caller's and is echoed, and `thermal-settings --json` reports it from a fresh process. Structured edit output would change shared infrastructure that P13–P17 qualify.
* **No generated ids.** Ids are the caller's, as `structural-load-add` and P18-CMD-001's commands have them.
* **No way to switch an existing analysis between steady and transient.** That is a whole-definition edit the command layer does not have, and the brief says not to change the mode behind a user's back. `thermal-analysis-add` creates either.
* **No edit of solver settings.** The tolerance and pivot floor are the data model's defaults; a CLI-chosen tolerance would be a second opinion about numerical acceptance.
* **No stale-geometry or stale-mesh *document* for the CLI.** A one-shot process regenerates and remeshes before it asks, so neither state can be presented to it; the core's refusals are exercised in process and the CLI's analogues (`regeneration_failed`, `mesh_generation_failed`) through the executable. See [FAILURE_PATHS.md](FAILURE_PATHS.md).
* **No change to `Mesher`, the Netgen adapter, the document format, the edit driver, `batch` or the unit catalogue.**
* **No Celsius.** A bare number is kelvin; `degC` is an unknown unit like any other the catalogue lacks.
* **No P18-REFMOD-001 and no P18-QUAL-001.** No reference-model suite, no qualification summary.
* **No concurrent-write or locking work.** "Multi-process" means sequential independent processes; collaboration is P22's.

## Decisions the owner may want to take (measured here, not changed)

1. **`relative_residual_tolerance` has no upper bound.** A hand-edited `1e300` loads and every analysis is then accepted (`ThermalData.cpp`: finite and positive). The acceptance threshold is user-editable canonical data.
2. **A `final_only` transient of more than one step can never be accepted**, because the balance of a backward-Euler step needs the states on both sides of it. The CLI therefore requires `--output` explicitly rather than choosing for the user.
3. **A 23 750-step transient is refused** (`balance_out_of_tolerance` at state 812) at a valid, fine time step.
4. **0 K and denormal temperatures are accepted** as prescribed and ambient values.
5. **`thermal-settings` needs a regenerable model** (to report `faceResolves`).
6. **A load or save failure inside the shared edit driver carries no machine code** for any edit family.
7. **The "Assembly edits" section of `help` now lists thermal (and structural, mesh, material) edits.**
