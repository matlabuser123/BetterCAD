# P18-CLI-001 — the failure paths

```text
Every row is a REAL document state, driven through the CLI (in process, and again through the executable in separate processes by
tests/cli/ThermalWorkflow.cmake). None is a hand-assembled diagnostic object.

stdout on a refusal      EMPTY          (the stream discipline: a command that cannot answer prints nothing to stdout)
code                     on stderr      "bettercad-cli <command>: <code>: <message>"   one line
file on disk             BYTE-IDENTICAL (checked by SHA-256 / direct byte comparison after every refusal)
```

## The matrix

| Fixture | Which core rule refuses | CLI diagnostic code | exit | payload on stdout | solver reached? |
| --- | --- | --- | --- | --- | --- |
| a valid steady analysis | — | — | 0 | yes | yes |
| a valid transient analysis | — | — | 0 | yes | yes |
| **a missing material** | `TV_MATERIAL` / `requireThermalModel` | `material_missing` | 1 | no | **no** |
| **a pure-Neumann steady model** | `TV_WELL_POSED` (no datum), the solve's anchoring check | `no_temperature_datum` | 1 | no | refused before the factorisation |
| **an unresolved LOAD target** | `TV_LOADS` (P18-LOAD) | `loads_invalid` ("Nothing nearby is substituted") | 1 | no | **no** |
| **an unresolved CONDITION target** | `TV_BCS` (P18-BC) | `boundary_conditions_invalid` | 1 | no | **no** |
| **two different temperatures on one face** | `TV_BCS` | `boundary_conditions_invalid` | 1 | no | **no** |
| a transient model on a material without ρ or cp | `TV_MATERIAL` per mode | `material_incomplete_for_mode` | 1 | no | **no** |
| a transient with `final_only` output and more than one step | `TV_BALANCE` (the balance needs every step) | `balance_missing` — "solve with the EveryStep output policy to balance each step" | 1 | no | yes, then the balance refused |
| a mesh that cannot be generated | P16's generation | `mesh_generation_failed` | 1 | no | no |
| the same four, through `thermal-validate` | the same rules | the first error finding's own code | **1** | **yes — the report** | as above |
| the same, through `thermal-balance` | the same rules | the first error finding's own code | **1** | yes if the chain reached the balance | as above |
| no thermal analysis | — | `no_thermal_analysis` | 1 | no | no |
| two thermal analyses | — | `ambiguous_thermal_analysis` | 1 | no | no |
| no meshing control | — | `no_mesh_control` | 1 | no | no |
| the file does not exist / is not a document | io | `document_not_loaded` | 1 | no | no |
| a feature the document lacks in a `face:` | — | `bad_face_reference` | 1 | no | no |
| a duplicate load or condition id | P18-CMD | `duplicate_id` | 1 | no | no |
| an id that is not there (remove) | P18-CMD | `id_not_found` | 1 | no | no |
| a value the validator refuses (h ≤ 0, T < 0 K, id 0, dt ≤ 0 …) | P18-DATA `validate` through the command | `invalid_value` | 1 | no | no |
| a second analysis | — | `thermal_analysis_exists` | 1 | no | no |
| unreadable command lines (`nan`, `inf`, `20degC`, `--node`, missing id, unknown option, two kinds at once …) | the CLI cannot read them | none, by design | **2** | no | no |

**Which of the three exit codes is the CLI's and which is the model's** is decided in one place for every edit in the program (`fromParse`): *InvalidArgument* and *DimensionMismatch* from the CLI's own parsing are exit 2; everything the model refuses is exit 1. The thermal edits add one thing: the codes above, derived from the core's `ErrorCode`, so a pipeline can branch on them.

## What the matrix is careful about

**The offending entity is not dropped.** An unresolved load or condition is accepted as canonical intent, is *reported* by `thermal-settings` with `faceResolves: false` and its reference exactly as written, and is *refused* by every result command by name. The file is never rewritten to remove it or to point it at a neighbouring face.

**No CLI default is substituted.** There is no default material, no default ambient, no default initial temperature, no default time step and no default tolerance anywhere in the CLI. The missing-material row checks that the refusal comes from the material stage and that the document still has no material afterwards. The pure-Neumann row checks that no result exists to have been "regularised".

**No flag bypasses validation.** `--force`, `--ignore-validation`, `--ignore-stale`, `--auto-pin`, `--regularize`, `--default-material`, `--skip-validation`, `--no-validate` and `--tolerance` are each a usage error (exit 2), and none appears in the help of any thermal verb.

## Stale geometry and stale mesh: not presentable to a one-shot process, and why

The brief asks for a stale-geometry and a stale-mesh failure driven through the CLI, and for a "same-count, new-source" and a "same-id, new-source" case. **None of those states can be handed to this CLI**, and the brief itself says not to claim impossible persistence behaviour:

```text
every CLI process       loads the document
                        REGENERATES the geometry (and refuses with regeneration_failed / regeneration_blocked / dependency_cycle if that fails)
                        GENERATES the mesh from the document's current intent (and refuses with mesh_generation_failed if that fails)
                        then runs the analysis
```

Geometry is current by the time anything asks, and the mesh was built in the same process from that geometry. Nothing persists a mesh (P16-PERSIST-001) or a result (ADR-049, P18-PERSIST-001), so there is no window in which a stale mesh could be handed to the solve, and no result held by one process that another could mistake for current. This is the conclusion P17-CLI-001 reached for the structural CLI, recorded in its `FAILURE_MATRIX.md`.

What *is* tested, and where:

| The brief's case | Where it is exercised | How |
| --- | --- | --- |
| stale geometry | `ThermalRun_ARunOfAnEditedGeometryIsStaleAndTheEditedGeometryIsNotRunOnTheOldMesh` | the extrusion depth is edited and regenerated; **the mesh is not rebuilt**; the entry stops at `model`, the report says `mesh_stale`, nothing is published, and an earlier run's report is no longer current |
| stale mesh, same counts, same node ids | `ThermalRun_ARemeshWithTheSameCountsAndIdsStillMakesTheOldRunStale` | the same body is meshed again (same node and tetrahedron counts); the old report is no longer current although every count and id agrees, and a fresh run is |
| result of an older document after an edit | `ThermalRun_AnEditAfterARunMakesItStaleAndAFailedRerunPublishesNothing` | a conflicting condition is added; the failed re-run has no solution, no recovered field and no balance, and does not make the first run current again |
| the CLI's own analogue | `ThermalCli_AMeshThatCannotBeGeneratedIsTheCliAnalogueOfAStaleMesh` | a sizing reference that names no face: exit 1, `mesh_generation_failed`, nothing on stdout, for four commands |
| multi-process independence | `cli.thermal.workflow` | two fresh processes on one file print byte-identical payloads; an edit by a third changes the answer; removing it restores the original answer byte for byte |

Faking a stale-mesh document for the CLI would have meant asserting that a formatter prints a hand-built report, which tests the formatter and not the gate.

## Load atomicity

Every refused mutation is checked for **byte equality of the file**, not only for its exit code, in process (`ThermalCli_InvalidLoadFailsAtomically`, `…InvalidBoundaryConditionFailsAtomically`, `…AnalysisModeCompleteness…`, `…AnalysisAddUsageErrors`) and through the executable (workflow step group 17: ten refused commands, then one SHA-256 comparison). A `batch` script whose second line fails leaves the file exactly as the first successful batch left it.
