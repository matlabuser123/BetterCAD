# P18-CLI-001 — exit codes

```text
0   Success      the command did what it was asked, and for a result command the analysis was ACCEPTED
1   Failure      the command ran and the answer is no: a refused edit, an analysis that is not accepted, a document that will not load
2   UsageError   the command line could not be read: nothing was attempted
```

**Three, not six — and that is the audit's finding rather than a choice.** `Cli.hpp` has defined exactly these three since P10 and `help` prints the contract ("Exit status: 0 success, 1 failure, 2 invalid command line."). The brief asks to distinguish categories "if the project already has them"; it has these, so this milestone invents no thermal-specific number. **The granularity lives in the diagnostic CODE**, which is the earlier milestones' design: "the code is the thing a pipeline matches, and only the code is an interface".

## The rule, stated once

**Exit 2 means "the CLI could not read what it was given". Exit 1 means "the model refused what it was asked".** For an edit that is `fromParse`'s rule (`EditSupport.hpp`): *InvalidArgument* and *DimensionMismatch* raised by the CLI's own parsing are 2, everything the model refuses is 1. For a result command the whole decision is `ThermalAnalysisRun::accepted()` and nothing else.

## The success criterion is the validator's, not the solver's

`thermal-solve` and the three summaries print nothing unless `run.accepted()`, and exit 1 otherwise. `accepted()` is `!stoppedAt.has_value() && validation.accepted()`: the chain completed **and** P18-VALID, which re-judges the residual, the finiteness of every temperature and every recovered field, and the heat balance independently of the links that produced them, accepts. A linear solver that returned success is not an input. The reports never name a `ValidationStatus` enumerator (the ownership gate fails one that does), so a warning-only analysis exits 0 by construction and an analysis with a skipped mandatory rule exits 1.

`thermal-validate` and `thermal-balance` are themselves the answer to a question about acceptance, so — as `structural-validate` does — they **print their payload and exit 1** when the analysis is not accepted. `thermal-balance` prints whenever the chain reached the balance, so an out-of-tolerance step is *shown*, not omitted; if the chain stopped earlier there is no balance to print and it behaves like the other result commands.

## The table (each row measured by running the command: in process, and again as a separate process by the workflow)

| Case | Diagnostic code | stdout | Expected | PASS |
| --- | --- | --- | --- | --- |
| a valid steady solve / summaries / balance / validate | — | payload | 0 | ✓ |
| a valid transient solve / summaries / balance / validate | — | payload | 0 | ✓ |
| an inspection (including one whose targets do not resolve) | — | payload, `faceResolves: false` | **0** | ✓ |
| an information-only finding (`transient_without_datum`) | — | payload, finding kept | 0 | ✓ |
| a valid edit | — | one line | 0 | ✓ |
| no command / no document / two documents | — | empty | 2 | ✓ |
| an unknown option (`--format`, `--force`, `--tolerance`, `--node`, `--facet`) | — | empty | 2 | ✓ |
| `--flux nan`, `--flux inf`, `--flux abc`, `--temperature nan`, `--temperature 20degC` | — | empty | 2 | ✓ |
| a unit of the wrong dimension (`1e5K` for a generation, `10W` for a coefficient) | — | empty | 2 | ✓ |
| no kind / two kinds / a face on a volumetric load / no face on a surface flux | — | empty | 2 | ✓ |
| a convection without `--ambient` / an ambient on a prescribed temperature | — | empty | 2 | ✓ |
| an id that is not a number | — | empty | 2 | ✓ |
| `--mode dynamic`, `--output sometimes` | — | empty | 2 | ✓ |
| the file does not exist / is not a document | `document_not_loaded` | empty | 1 | ✓ |
| a feature the document lacks in `face:` | `bad_face_reference` | empty | 1 | ✓ |
| no thermal analysis / two / no meshing control | `no_thermal_analysis` / `ambiguous_thermal_analysis` / `no_mesh_control` | empty | 1 | ✓ |
| a duplicate id / an id that is not there / a value the validator refuses | `duplicate_id` / `id_not_found` / `invalid_value` | empty | 1 | ✓ |
| a second analysis | `thermal_analysis_exists` | empty | 1 | ✓ |
| a transient analysis missing its initial temperature, step or end time; a steady one given them | the data model's own sentence, `invalid_value` | empty | 1 | ✓ |
| a missing material | `material_missing` | empty | 1 | ✓ |
| a pure-Neumann steady model | `no_temperature_datum` | empty | 1 | ✓ |
| an unresolved load / condition; conflicting temperatures | `loads_invalid` / `boundary_conditions_invalid` | empty | 1 | ✓ |
| a transient on a material without ρ or cp | `material_incomplete_for_mode` | empty | 1 | ✓ |
| a `final_only` transient of several steps | `balance_missing` | empty | 1 | ✓ |
| a mesh that cannot be generated | `mesh_generation_failed` | empty | 1 | ✓ |
| **the same refusals through `thermal-validate`** | the first error finding's code | **payload** | **1** | ✓ |
| residual above tolerance / balance out of tolerance / non-finite result | `solve_failed` / `balance_out_of_tolerance` / `temperature_not_finite` … (the validator's) | empty (validate: payload) | 1 | reached by the core's own seams; see below |

**Residual failure, balance failure and a non-finite result (the brief's §57–59)** are decided by the validator's rules `TV_SOLVER_RESIDUAL`, `TV_BALANCE` and `TV_RESULT_FINITE` and reach the exit status through `accepted()`; they are not reachable through a real document because the earlier links refuse first (the solve itself rejects a residual above the canonical tolerance, and the balance is within tolerance whenever it is formed), and P18-VALID-001 shows each one to the validator as plain data, because a typed result cannot be corrupted. Faking one for the CLI would test the formatter. What the CLI tests *do* establish, with a real document, is the property that matters: an overflowing load (1e300 W/m²) makes the solve refuse, the validation report that explains it is strict JSON with no `nan` token, and its exit status agrees with its `accepted` field (`ThermalCli_ANonFiniteMeasurementInTheReportKeepsAParseablePayload`).

## The exit code is never derived from text

```text
if (message.contains("stale")) ...        does not appear anywhere
```

`runThermalSolve`, `…Temperature`, `…Gradient`, `…HeatFlux` return the `ExitCode` the shared `analyse` helper chose; `runThermalValidate` and `runThermalBalance` return `run.accepted() ? Success : Failure`. The probes that change either — a validate that always succeeds (CR07), a balance that always succeeds (CR08), a success that means "a solution exists" (CR09), result commands that never refuse (CR10), an edit refusal swallowed (CE04) — are each killed (see [MUTATIONS.md](MUTATIONS.md)).
