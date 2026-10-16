# P18-DATA-001 — canonical versus derived

```text
SUBJECT:  the line the whole milestone exists to draw, and how it is enforced
          by type rather than by review
```

## The table

| Item | Canonical / Derived | Type | Where it lives | How the line is enforced |
| --- | --- | --- | --- | --- |
| Which mesh control is analysed | **Canonical** | `MeshControlId` | `ThermalAnalysisDefinition::mesh` | document object, revisioned |
| Analysis mode | **Canonical** | `ThermalAnalysisMode` enum | `::mode` | enum, not bool; `THE_MODE_IS_NOT_A_BOOL` refused |
| Heat loads | **Canonical** | `std::vector<ThermalLoad>` of `ThermalLoadId` + `SurfaceHeatFluxLoad` or `VolumetricHeatSourceLoad` | `::loads` | `FaceName` targets only; `sizeof` mirror; `A_HEAT_FLUX_LOAD_DOES_NOT_TARGET_A_NODE` refused |
| Boundary conditions | **Canonical** | `std::vector<ThermalBoundaryCondition>` of `ThermalBoundaryConditionId` + `PrescribedTemperature` or `ConvectionBoundary` | `::boundaryConditions` | likewise |
| Initial condition | **Canonical** | `std::optional<UniformInitialTemperature>` -- ONE value in K | `::initialCondition` | required for transient, refused for steady; not constructible from a `std::vector<Temperature>` |
| Solver settings | **Canonical** (ADR-047's argument) | `ThermalSolverSettings`: two ratios | `::solver` | validated finite and positive |
| Transient settings | **Canonical** | `std::optional<TransientSettings>`: `dt`, `endTime`, output policy | `::transient` | required for transient, refused for steady |
| The analysis object's revision | Canonical bookkeeping | `std::uint64_t` on `DocumentObject` | the document | moves only on an effective edit |
| Thermal DOF map | **Derived** | not defined here | P18-DOF-001 | — |
| K, C, Q | **Derived** | not defined here | P18-ELEM-001, P18-ASSEMBLY-001 | `THE_DEFINITION_HAS_NO_CONDUCTIVITY_MATRIX` refused |
| T per node | **Derived** | `std::span<const Temperature>` | `ThermalResult::temperatures()` | `THE_DEFINITION_HAS_NO_TEMPERATURES` refused; result not constructible into the definition |
| grad(T) per element | **Derived** | `std::span<const TemperatureGradient3>` | `ThermalResult::gradients()` | likewise |
| q per element | **Derived** | `std::span<const HeatFlux3>` | `ThermalResult::heatFluxes()` | likewise; `|q|` derived by `magnitude()`, never stored |
| Heat balance | **Derived** | `HeatBalance` | `ThermalResult::heatBalance()` | imbalance and normalised metric computed, never stored |
| Sample time | **Derived** | `std::optional<Time>` | `ThermalResult::time()` | required for transient, refused for steady |
| Result provenance | **Derived** | `ThermalResultSource` | `ThermalResult::source()` | const reference only; `A_RESULT_SOURCE_CANNOT_BE_REWRITTEN` refused |
| Result currency, analysis state | **Derived, never stored** | `ResultCurrency`, `AnalysisState` | computed by `resultCurrency` / `analysisState` | no field anywhere holds either |
| Last result, last failure | Held, not canonical | `ThermalResultSlot` | a solver service, P18-SOLVE-001 | never a document member; never persisted |
| Visualisation buffers | **Derived** | not defined here | P18-VIZ-001 | — |

## The three enforcement instruments

**1. The header graph.** `ThermalAnalysisObject.hpp` does not include
`ThermalResult.hpp`; the result type is not visible where the definition is
declared. A definition cannot hold a result because it cannot name one. The
reverse direction -- the result includes the definition's header for the
source -- is the one the physics has: a result is about an analysis, an
analysis is not about a result.

**2. The size mirror.** `ThermalData_TheDefinitionHoldsOnlyIntent` declares a
struct with the definition's seven members in order and asserts
`sizeof(ThermalAnalysisDefinition) == sizeof(PermittedDefinition)`. Any field
added to the definition changes the size and breaks the build; the milestone
that adds an authorized field extends the mirror, and that extension is where
the field's authority as canonical intent is stated. The instrument P17 arrived
at after its first two attempts (`sizeof == sizeof(MeshControlId)`, then
trivial copyability) each broke on a legitimate intent field.

**3. Non-constructibility.** `static_assert(!is_constructible_v<
ThermalAnalysisDefinition, ThermalResult>)`, and likewise from a `MeshStamp`,
a `std::vector<Temperature>` and a `HeatBalance`; plus the compile-fail cases
that spell a member access to a result field on the definition.

## What a stale result is allowed to be

Historical. `AnalysisState::SolvedStale` is readable as what the model WAS,
never as what it is; `describesTheModel(currency)` is true for `Current` only,
so a consumer that gates on it cannot present a stale field. A result's
arrays are reachable while stale -- P16-VIZ-001 established that a stale mesh
is "old, not wrong" and may be inspected -- but every path that READS a result
against a mesh (`describes`, `temperatureOf`) compares the stamp and refuses.

## What the command history and the file hold

Audited rather than assumed:

```text
CommandHistory       stores COMMANDS (unique_ptr<Command>), executes, undoes
                     and redoes them. There is no document snapshot. The
                     generic AddObjectCommand keeps the removed object on
                     undo and re-inserts clone() on redo, and
                     ThermalAnalysis::clone() copies the definition and the
                     ID -- nothing else exists on the object to copy.
                     Test: ThermalData_AddUndoRedoRestoresTheAnalysisExactly.

io::documentToJson   dispatches on the concrete object type and REFUSES an
                     unknown one: "objects of type 'thermal-analysis' cannot
                     be saved". So a document holding a thermal analysis
                     cannot be written until P18-PERSIST-001 adds the
                     representation; it is neither silently dropped nor
                     written with anything derived.
                     Test: ThermalData_SavingADocumentWithAThermalAnalysis
                     IsRefusedUntilPersistenceExists.
```

Both are the behaviours P17-DATA-001 found for the structural analysis before
P17-CMD-001 and P17-PERSIST-001, and both are pinned by a test here so that the
P18 milestones that change them replace a known refusal.
