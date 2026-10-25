# P18-CLI-001 — the architecture, and the boundary the CLI must not cross

```text
argv
 -> parse typed options                    Arguments.cpp  (the one parser; options are declared per command, an unknown one is exit 2)
 -> load the canonical document            io::loadDocument                       (P18-PERSIST-001's reader; atomic: a failed load returns no document)
 -> regenerate the model                   features::Regenerator                  (a failed feature is reported, never skipped)
 -> EDIT      build a P18-CMD command object   -> the edit spine executes it -> save atomically only if it succeeded
    REPORT    mesh (P16) -> thermal::runThermalAnalysis -> format what the core produced
 -> exit with the core's verdict           accepted()  ->  0 / 1  ;  an unreadable command line -> 2
```

The CLI is an adapter. Everything with an engineering meaning is on the right of an arrow and in a qualified module.

## Who owns what (the brief's §218)

| | owned by the CLI? | by |
| --- | --- | --- |
| Tet4 formulation, `Ke`, `Ce` | **NO** | P18-ELEM-001 |
| load integration (`q″A/3`, `q‴V/4`) | **NO** | P18-LOAD-001 |
| boundary integration (prescribed T, Robin `h A / 12`) | **NO** | P18-BC-001 |
| assembly | **NO** | P18-ASSEMBLY-001 |
| steady solve `K T = Q` | **NO** | P18-SOLVE-001 |
| transient integration `(C/Δt + K) Tⁿ⁺¹ = …` | **NO** | P18-TRANSIENT-001 |
| `grad(T) = G Te`, `q = −k grad(T)`, `|q|` | **NO** | P18-POST-001 |
| reactions, heat balance | **NO** | P18-BALANCE-001 |
| validation policy, thresholds, severities, the rule order | **NO** | P18-VALID-001 |
| currentness | **NO** | P18-DATA-001 / P18-VALID-001 (`isCurrent`, `staleReasons`) |
| `GeometryReference` resolution | **NO** — the CLI neither resolves nor rebinds; it only *reports* whether a reference resolves | P18-LOAD/BC through the chain; `features::checkFaceName` for the report |
| persistence format | **NO** | P18-PERSIST-001 |
| material properties | **NO** — no file of the CLI names one | P15 through P18-MAT-001 |
| the order of the chain | **NO** | `runThermalAnalysis` (ADR-057) |
| command-line parsing, face grammar, unit catalogue | yes (shared, pre-existing) | `Arguments`, `Selectors`, `UnitCatalog` |
| selecting an extremum, formatting, exit status | **yes** — and nothing else | `ThermalReports.cpp` |

## The two directions of the boundary are both enforced

**Upwards** (the CLI must not reach below the single entry): `CheckThermalCliOwnership.cmake` fails a CLI file that includes a header of the derived chain, names a link of it, names a material property, holds a floating-point literal, decides on a residual, a balance or a status, takes `abs`/`clamp`/`min`/`max`, subscripts with an id's `value()`, accepts `--node`/`--facet`/`--element`/`--tet`/`--dof`, writes a file or parses document JSON, converts a unit, writes to a process stream, does arithmetic on a quantity, or lets a double bypass the finite-checking writer. It also *counts* the owners each file must use (`runThermalAnalysis`, `accepted()` ×5, `nodeOf`, `ordinalOf` ×2, every P18-CMD command object, `parseFaceReference` ×2 …), so deleting one call and keeping another cannot pass. 102 fixture trees each hold exactly one defect and each must produce exactly one violation naming the rule.

**Downwards** (the entry must not become a second implementation): the same gate fails `ThermalAnalysisRun.cpp` if the chain is not called once each in order, if `validateThermalAnalysis` is not called exactly once with `derivedState()`, if it does any arithmetic, holds a tolerance or a status decision, constructs or assigns a definition or a setting, meshes, or writes; and fails the header if `accepted()` is spelled any other way than *a completed chain and the validator's verdict*.

## Dependencies (the brief's §153 and §127)

```text
bettercad_cli_lib  --PRIVATE-->  assembly  drawing  features  io  meshing  sketch  structural  thermal        (thermal added here)
                   --PUBLIC--->  core
```

The CLI's runtime closure contains **no GUI toolkit**: `ThermalWorkflow.cmake` resolves the binary's runtime dependencies with `file(GET_RUNTIME_DEPENDENCIES)` and fails if any is a Qt library. Thermal commands therefore run with no display and no viewer; nothing instantiates `renderer::*` (the P18-VIZ module is not linked), and a summary reads the P18 result directly, never a GUI buffer.

## Why the CLI links `thermal` and not the viewer's helper

The viewer's test support (`derive`) is kept in the tests, as the *oracle* `ThermalRun_*` compares the entry against. It was not promoted: it stops at the first refusal and returns a message string, and the validator's report cannot be produced from "what exists" without a caller re-deriving which rule to blame.
