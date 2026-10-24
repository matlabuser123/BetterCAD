# P18-CMD-001 — type safety (compile-fail and static assertions)

```text
52 compile-fail cases, 52 expected failures, 0 unexpected passes — plus a control target that must compile.
```

A claim about what **cannot** happen is worth nothing in a comment. `tests/compile_fail/ThermalCommandMisuse.cpp` is built once without any `BETTERCAD_CF_*` macro as a control (`bettercad_cf_thermalcmd_control`), which
must compile with all ten commands constructed in their intended form — so every failure is attributable to its own line and not to a missing header — and then once per case with one macro defined, which must fail
with a diagnostic of the expected *class* (a refused conversion, an undeclared name, a missing member, a const violation, a deleted function, an abstract base).

## The cases

| Claim | Cases | Class of diagnostic |
| --- | --- | --- |
| a command is keyed on a `ThermalAnalysisId`, and a node, an element, a load id, a condition id or a mesh-control id is not one | 10 (`A_LOAD_COMMAND_IS_NOT_KEYED_ON_A_NODE` … `A_TRANSIENT_COMMAND_IS_NOT_KEYED_ON_A_MESH_CONTROL`) | refused conversion / no matching constructor |
| a removal is keyed on the right kind of id | 4 (`REMOVE_LOAD_NEEDS_A_LOAD_ID_NOT_A_NODE` …) | refused conversion |
| a quantity is a typed quantity, not a bare double | 5 (`AN_INITIAL_TEMPERATURE_IS_NOT_A_DOUBLE`, `A_TIME_STEP_…`, `A_HEAT_FLUX_…`, `A_CONVECTION_COEFFICIENT_…`, `A_FLUX_IS_NOT_A_TEMPERATURE`) | refused conversion |
| a record names a face, never a node | 3 (`A_LOAD_DOES_NOT_TARGET_A_NODE`, `A_CONDITION_…`, `A_LOAD_ID_IS_NOT_A_NODE_ID`) | no matching constructor |
| **the command header cannot name a derived type** | 13 (`THE_DOF_INDEX_IS_NOT_REACHABLE`, `THE_FREE_EQUATION_INDEX_…`, `THE_DOF_MAP_…`, `THE_ASSEMBLED_SYSTEM_…`, `THE_STEADY_SOLUTION_…`, `THE_TRANSIENT_SOLUTION_…`, `THE_POST_RESULT_…`, `THE_BALANCE_…`, `THE_VALIDATION_REPORT_…`, `THE_PREPARED_LOADS_…`, `THE_PREPARED_BOUNDARY_…`, `THE_RESULT_CONTAINER_…`, `THE_MODEL_…`) | "is not a member of" |
| the definition and the commands hold no derived member | 12 (no temperature array, matrix, prepared loads, DOF map, solution, heat flux, report, time grid; no `result()`, `mesh()`, `solve()`, payload setter) | "no member named" |
| the payload is read-only | 2 | const violation |
| commands are not copyable | 2 | deleted function |
| the edit base is abstract with a protected constructor | 1 | protected / abstract |

The thirteen "not reachable" cases are the strongest: they are **absences of declaration**, so a future edit that included a solver header into `ThermalCommands.hpp` would turn a case into an unexpected pass
and fail the build (and the architecture gate would fail it first). This is stronger than P17-CMD-001, whose DOF family was reachable through a restraint header.

## Why the probes go through constructor parameters

P16-CMD-001 recorded the trap and it applies unchanged: `SomeAggregate{.member = wrongId}` can **compile**, because brace elision initializes the aggregate's *first* member. A struct cannot refuse that without
gaining a constructor. What can be guaranteed is the path the commands expose, where a parameter of a named type is copy-initialized and no elision applies; that is where the probes are.

## Static assertions in the runtime suite

`ThermalCommand_StrongIdsNotInterchangeable`, `_CommandsAreNotCopyable`, `_AFaceIsTheOnlyTargetAThermalRecordTakes` and `_EveryEditCommandPayloadIsTwoCanonicalDefinitions` carry `static_assert`s that need no
compile-fail machinery and complement it:

```text
not constructible   AddThermalLoadCommand from (NodeId | ElementId | ThermalDofIndex | ThermalLoadId, ThermalLoad)
                    RemoveThermalLoadCommand from (ThermalAnalysisId, ThermalBoundaryConditionId | NodeId)
                    RemoveThermalBoundaryConditionCommand from (ThermalAnalysisId, ThermalLoadId | ThermalDofIndex)
                    the settings and initial-temperature commands from a NodeId / ElementId / ThermalDofIndex analysis
not convertible     uint64_t → ThermalAnalysisId | ThermalLoadId;  ThermalLoadId → ThermalBoundaryConditionId;
                    NodeId → ThermalLoadId;  NodeId | ElementId | ThermalDofIndex → FaceName
same type           PrescribedTemperature::face, ConvectionBoundary::face, SurfaceHeatFluxLoad::face are all FaceName
                    before() and after() are const std::optional<ThermalAnalysisDefinition>&
```

## No weakening for convenience

No overload takes a raw integer for an id, no implicit conversion between ids was added, and no `NodeId`/`ElementId`/`ThermalDofIndex` constructor was added to a record or a command to make a test or a caller
easier. The ownership gate (rule 3) additionally forbids a command file from naming a mesh identity at all.
