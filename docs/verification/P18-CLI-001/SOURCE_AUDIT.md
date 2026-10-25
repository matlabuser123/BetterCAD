# P18-CLI-001 — source audit: there is no thermal physics in the CLI

`qualification/source-audit.ps1` reads the five files this milestone adds (comments, string literals and `#include` lines removed first) and counts, per file, the tokens of every thing the CLI must not do. Its output is `qualification/source-audit.txt`. The audit's patterns are deliberately **looser** than the ownership gate's, so a non-zero row is a prompt to read, not a violation; each one is explained here.

## The result

| Question | `ThermalReports.cpp` | `ThermalEdits.cpp` | `ThermalOutput.hpp` | `ThermalAnalysisRun.cpp` / `.hpp` |
|---|---|---|---|---|
| Tet4 matrices, shape gradients, `Ke`, `Ce` | 0 | 0 | 0 | 0 / 0 |
| a floating-point literal | **0** | **0** | **0** | **0 / 0** |
| `abs`, `clamp`, `min`, `max`, `round` | 0 | 0 | 0 | 0 / 0 |
| `sqrt`, `pow`, `exp`, `log` | 0 | 0 | 0 | 0 / 0 |
| a material property (k, ρ, cp) | 0 | 0 | 0 | 0 / 0 |
| Celsius / Fahrenheit / 273.15 | 0 | 0 | 0 | 0 / 0 |
| `ValidationStatus` / `CheckStatus` / `BalanceStatus` | 0 | 0 | 0 | 0 / 0 |
| an id's `value()` used as an index | 0 | 0 | 0 | 0 / 0 |
| a file write, `ofstream`, `saveDocument` | 0 | 0 | 0 | 0 / 0 |
| a direct write to a process stream | 0 | 0 | 0 | 0 / 0 |

## The non-zero rows, read

| Row | Count | What matched | Why it is not physics |
|---|---|---|---|
| Reports · "transient integration" | 1 | `const thermal::TransientThermalSolution& transient = *run.transient;` | a **reference to the core's result object**, read for its stored times and step count; nothing is integrated |
| Reports · "validation policy" | 7 | `kThermalSettingsUsage`, `kThermalValidateUsage`, … (the audit's `kThermal` pattern) | the seven **usage-string constants** of the verbs, defined in `Commands.hpp` |
| Reports · "meshing/writing" | 2 | `prepared.regenerator.regenerate(…)` (the audit's `generate(` matches inside `regenerate(`) and `mesher.generate(…)` | the one-shot process must regenerate the model and mesh it **before** asking the core; this is the same step every P16/P17 report performs. The thermal *entry* does neither (its row is 0, and the gate fails it if it ever does) |
| Edits · "Robin / convection" | 1 | `thermal::ConvectionBoundary{.face = *face, .coefficient = *coefficient, .ambient = *ambient}` | the **definition** the user typed, handed to `AddThermalBoundaryConditionCommand`; it is data (two quantities and a face), not a matrix |
| Entry · "load/boundary", "assembly", "steady", "transient", "recovery", "balance" | 4, 1, 2, 2, 4, 2 | the **calls of the chain** in `ThermalAnalysisRun.cpp`, and the declarations of its stages in the header | this file's job is to call each link once, in order; the gate pins that each is called exactly once and that none of them is done here |

## The owners used

Each thing the CLI must delegate is delegated, at least as many times as there are places that need it (the gate checks the same numbers, `accepted()` and `sawNonFinite()` **exactly**). `source-audit.txt` lists `runThermalAnalysis` ×1, `accepted()` ×5, `thermalAnalysisIds`, `checkFaceName` ×4, `nodeOf`, `ordinalOrThrow` ×3, `validationJson` ×3, `sawNonFinite()` ×6; every P18-CMD command object (`Add/RemoveThermalLoadCommand`, `Add/RemoveThermalBoundaryConditionCommand`, `CreateThermalAnalysisCommand`); `parseFaceReference` ×2; `parseQuantity`; `executeEdit` ×6.

## The includes

The thermal headers the CLI includes are `ThermalAnalysisObject`, `ThermalAnalysisRun`, `ThermalBalance`, `ThermalData`, `ThermalPost`, `ThermalValidation` and `ThermalCommands`: the canonical data, the single entry, and the **result and report types**. It includes none of `ThermalElement`, `ThermalSystem`, `ThermalLoadVector`, `ThermalBoundary`, `ThermalSolve`, `ThermalTransient`, `ThermalDof` (the gate's rule 1 fails the build otherwise), nothing from `linalg`, `structural` or `renderer`, and no Qt header.

(The *entry's header* includes the chain's headers because `ThermalAnalysisRun` **holds** the optionals of the chain's products; that is the entry's job and it lives in the thermal module, below the CLI.)
