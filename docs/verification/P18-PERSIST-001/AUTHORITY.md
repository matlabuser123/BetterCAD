# P18-PERSIST-001 — what is saved, what is never saved, and why the exclusion cannot be broken by accident

```text
SAVE FILE  =  CANONICAL THERMAL INTENT        NOT  a thermal solver or result cache
```

## Persisted, and not persisted

| Data | Saved? | How that is enforced |
| --- | --- | --- |
| `ThermalAnalysisId` (the object id), the analysis definition | **YES** | the object envelope and `data` |
| `ThermalLoadId`, kind, signed value, `FaceName` | **YES** | `loads[]`; ids are never regenerated on load (`ALoadedDocumentKeepsItsIdentities…`) |
| `ThermalBoundaryConditionId`, kind, values, `FaceName` | **YES** | `boundary_conditions[]` |
| initial temperature, solver settings, transient settings | **YES** | `initial_temperature`, `solver`, `transient` |
| every `GeometryReference` form, unresolved ones included | **YES**, exactly | `faceNameToJson`; `GeometryReferencesSurviveExactlyInEveryForm`, `AnUnresolvedReference…` |
| | | |
| P16 mesh-local resolution, `NodeId`, `ElementId`, facets | **NO** | the reader gets no document, no mesh and no mesher — there is nothing to resolve with; gate rules 3 and 9; an unknown key `node`/`element`/`facet`/`dof` is refused |
| `ThermalDofMap`, constraint map, free-equation map | **NO** | gate rule 2; not even declared where the persistence file is |
| prepared loads, prepared boundary | **NO** | gate rule 2 |
| `K`, `C`, `Q`, reduced matrices, factorisations | **NO** | gate rule 2 (`ThermalSystem`, `SparseMatrix`, `Eigen`) |
| steady temperature, transient history | **NO** | a `ThermalPostResult`/solution is not a member of any document object |
| `grad(T)`, `q`, `|q|` | **NO** | same |
| reaction heat, heat-balance arrays | **NO** | same |
| validation report, currentness, "validated" flags | **NO** | derived from sources; a `"validated"`/`"current"` key is refused |
| visualisation buffers, classifier output, actors | **NO** | the renderer is a higher layer |
| `k`, `rho`, `cp`, a material | **NO** | P15 owns them; gate rule 4; `TheMaterialStaysP15s…` |
| timestamps, durations, iteration counts, statistics | **NO** | gate rule 6; no object type writes one |
| command history | **NO** | `SerializationIsByteIdentical…` builds one state through commands with an undo and a redo and compares the sections |

## Why this is structural rather than maintained

```cpp
Result<Json> thermalAnalysisToJson(const thermal::ThermalAnalysis& analysis);

Result<std::unique_ptr<thermal::ThermalAnalysis>>
thermalAnalysisFromJson(const Json& data, std::string name, std::string_view path);
```

The writer's only argument is the analysis object — and a `ThermalAnalysis` holds a `ThermalAnalysisDefinition`, which has seven members and no derived one. A result does not live on a document object at all; it is returned by the solve, recovery and balance functions and held by the caller. **There is no expression the writer could contain that depends on whether a solve has run.** The reader has no `Document`, no `Regenerator` and no `Mesher`, so an unresolved reference has nothing to be rebound *to*.

This is a stronger statement than a test: two of the brief's mutations ("serialize K", "serialize the temperature history") cannot be written without first changing a signature and every caller ([MUTATIONS.md](MUTATIONS.md)). What tests and the gate add is that the *shape* cannot change by accident.

## The three lines of defence

1. **The type system.** The definition cannot hold a derived thing ([the compile-fail group of P18-DATA](../P18-DATA-001/README.md)).
2. **The gate** (`tests/architecture/CheckThermalPersistenceOwnership.cmake`, 24 tests: the real tree, a valid fixture and 22 fixtures that each break exactly one rule). It scans `ThermalAnalysisJson.cpp` — with comments removed — for includes (the shared header and the standard library only), derived names, mesh identity, material names, parsed numbers, non-determinism, resolution and unit conversion, and *counts* that the core's owners are used at every place that needs them; and it scans the rest of `src/io` for any thermal include other than the canonical analysis object, and the dispatch for derived names.
3. **The bytes.** [BYTE_IDENTITY.md](BYTE_IDENTITY.md): the same document, saved before and after every derived stage, is identical; and the thermal section does not follow the size of the mesh or of the result.

## Reachability, measured

A translation unit including only the shared private header `io/json/ObjectJson.hpp` (which is what the persistence file includes), preprocessed with the `debug-ext` flags and searched for each derived name:

```text
NOT DECLARED where the persistence code is written (0 occurrences, 5.8 MB of preprocessed output)
  ThermalPostResult   ThermalSystem   SteadyThermalSolution   TransientThermalSolution
  ThermalBalanceResult   ThermalValidationReport   PreparedThermalLoads   PreparedThermalBoundary
  ThermalDofMap   ThermalDofIndex   ThermalFreeEquationIndex   ThermalModel   Eigen   SparseMatrix

DECLARED, and PRE-EXISTING -- reached through the shared header's other object types (features, meshing), not introduced here
  Mesher   Regenerator   VolumeMesh   MeshStamp   NodeId   ElementId
```

So the derived **thermal** chain is unreachable from the persistence code. The mesh and geometry types ARE declared — `ObjectJson.hpp` serves every object type — which is exactly why the gate forbids the thermal file from *naming* them (rules 2, 3 and 9) rather than relying on the compiler to keep them out. The numbers are recorded in [qualification/reachability-persist.txt](qualification/reachability-persist.txt).

## P15 remains the material authority

```text
P15 remains material authority:      YES
P18 persistence duplicates k:        NO
P18 persistence duplicates rho:      NO
P18 persistence duplicates cp:       NO
a material reference in the section: NO (the body's P15 assignment is the document's own)
```

`TheMaterialStaysP15sAndIsNotCopiedIntoTheAnalysis` saves a document, loads it into a fresh study, edits the **P15** conductivity of the loaded document tenfold and requires the re-solved temperature field to change — which it could not if the analysis carried its own copy.
