# P18-CMD-001 — the command model

```text
STATUS:  built and measured. ADR-055.
         NO NEW COMMAND FRAMEWORK. NO NEW INVALIDATION MECHANISM. NO NEW IDENTITY TYPE.
         The audit found the framework already there, as it did for P17-CMD-001, and this milestone is the ten commands on it.
```

## The architecture audit

| Concern | Existing owner | Existing behaviour | Reusable by P18? | Action |
| --- | --- | --- | --- | --- |
| command base | `bettercad::Command` | `execute` failure → document unchanged; `undo` exactly reverts; `redo` reproduces the state **including the same IDs**; non-copyable | **yes** | derive |
| the two stacks | `bettercad::CommandHistory` | binds to one document; `execute` pushes **only after success** and clears redo **only then**; a failed `undo`/`redo` leaves the command where it was | **yes** | use as is |
| create / delete an object | `AddObjectCommand`, `DeleteObjectCommand` | restore the prototype's recorded `ObjectId`; the object is `clone()`d with its ID | **yes** (create) | wrap |
| a thermal analysis | `ThermalAnalysis : DocumentObject` | a document object, so it already has an id, a revision, undo and a place in the dependency graph | **yes** | edit through the document |
| atomic definition edit | `Document::modifyObject` + `ThermalAnalysis::setDefinition` | `setDefinition` calls `validate(definition)` **before** assigning; returns `false` and moves no revision for an effective no-op; `modifyObject` bumps the object and document revisions only on `true` | **yes** | the one write path |
| canonical identities | `ThermalAnalysisId`, `ThermalLoadId`, `ThermalBoundaryConditionId` (`Id<Tag>`) | strong ids, no implicit conversion between them or from an integer | **yes** | key every command on them |
| canonical record validators | `validate(ThermalLoad)`, `validate(ThermalBoundaryCondition)`, `validate(UniformInitialTemperature)`, `validate(ThermalSolverSettings)`, `validate(TransientSettings)` | record-level; consult no mesh | **yes** | call, never restate |
| canonical order | `ThermalAnalysisDefinition::loads`, `::boundaryConditions` | kept as given; compared by `operator==`; ids unique | **yes** | append on add, replace in place on edit |
| result currentness | each derived object's recorded source; the report's `ThermalResultSource` | see [CURRENTNESS.md](CURRENTNESS.md) | **yes** | add nothing |
| history payload | a command object | the command's own members; no document snapshot | **yes** | before/after of one definition |
| no-op policy | P15/P16/P17 | succeed, move no revision, still recorded | **yes** | follow |
| compile-fail enforcement | `tests/compile_fail/` | a control target must compile, so each failure is attributable | **yes** | one new group |
| document snapshots | none | `CommandHistory` stores commands, never document copies; `AddObjectCommand` holds one clone of the new analysis, whose definition has no derived member | **n/a** | nothing to fix |

So **the generic document snapshot never deep-copies derived thermal state**, because there is no generic snapshot: the history is a stack of command objects. The brief's section 91 ("if the framework snapshots
the whole document, fix the architecture") found nothing to fix.

## What was built

Ten commands in `bettercad::thermal` (`include/bettercad/thermal/ThermalCommands.hpp`, `src/thermal/ThermalCommands.cpp`):

```text
CreateThermalAnalysisCommand          wraps AddObjectCommand
ThermalAnalysisEditCommand            the base: execute / undo / redo once; before() / after() / analysis() / changedAnything()
  AddThermalLoadCommand
  EditThermalLoadCommand
  RemoveThermalLoadCommand
  AddThermalBoundaryConditionCommand
  EditThermalBoundaryConditionCommand
  RemoveThermalBoundaryConditionCommand
  SetInitialTemperatureCommand
  SetThermalSolverSettingsCommand
  SetTransientSettingsCommand
```

### The command inventory

| Command | Target | Canonical payload | Undo payload | Derived payload |
| --- | --- | --- | --- | --- |
| create | (creates) `ThermalAnalysisId` | name, `ThermalAnalysisDefinition` | the created object (its definition) | **none** |
| add load | `ThermalAnalysisId` | `ThermalLoad` (id, kind, `FaceName` or none, signed value) | the previous definition | **none** |
| edit load | `ThermalAnalysisId`, `ThermalLoadId` (inside the record) | the whole replacement `ThermalLoad` | the previous definition | **none** |
| remove load | `ThermalAnalysisId`, `ThermalLoadId` | the id | the previous definition (so position returns) | **none** |
| add condition | `ThermalAnalysisId` | `ThermalBoundaryCondition` (id, `FaceName`, T̄ or h and T∞) | the previous definition | **none** |
| edit condition | `ThermalAnalysisId`, `ThermalBoundaryConditionId` | the whole replacement record | the previous definition | **none** |
| remove condition | `ThermalAnalysisId`, `ThermalBoundaryConditionId` | the id | the previous definition | **none** |
| initial temperature | `ThermalAnalysisId` | `UniformInitialTemperature` — **one value in kelvin** | the previous definition | **none** |
| solver settings | `ThermalAnalysisId` | `ThermalSolverSettings` — two ratios | the previous definition | **none** |
| transient settings | `ThermalAnalysisId` | `TransientSettings` — step, end time, output policy | the previous definition | **none** |

`ThermalCommand_EveryEditCommandPayloadIsTwoCanonicalDefinitions` walks the nine edits: for each, the command's `before()` equals the definition the test seeded, its `after()` equals an expected definition the
**test built by hand**, the document equals it, and undo and redo return to the two.

## Decisions the audit settled by following the project

* **Load and condition order is the user's, and is preserved.** An add appends; an edit replaces in place; a removal's undo returns the record to its position because the whole definition is assigned back.
* **A thermal edit stales the result and not the mesh.** The mesh-control handle is the only dependency of the analysis on the mesh; a load, a condition or a setting is not a meshing question
  ([CURRENTNESS.md](CURRENTNESS.md)).
* **A no-op edit succeeds, moves no revision and is still recorded** (`NoOpEditSucceedsAndMovesNothing`).
* **Entities are never overwritten silently.** A duplicate id is `AlreadyExists`; a missing target is `NotFound`; the command layer invents **no conflict policy** between conditions — two prescribed
  temperatures on one face are accepted as canonical intent and refused only where P18-BC prepares them (`ACommandPermitsAConflictThatPreparationReports`).

## What is deliberately not here

* **No whole-definition edit, no deletion, no mode switch.** The checklist names none of them, and the brief asks not to invent a mode-switch command. Because the data model is mode-complete, `SetInitialTemperature`
  and `SetTransientSettings` are **refused for a steady analysis** with a diagnostic that says why (`InitialTemperatureIsRefusedForASteadyAnalysis`, `TransientSettingsAreRefusedForASteadyAnalysis`); creating a
  transient analysis is a create with a full definition.
* **No output-time vector.** The data model's transient settings are a step, an end time and an output *policy* (`FinalOnly` | `EveryStep`); there is no schedule to store, and the actual step sequence (including its
  shortened last step) is derived by P18-TRANSIENT and appears in no payload (`TransientSettingsDoNotPersistDerivedTimeGrid`).
* **No `NamedBoundarySet` target.** The only canonical target of a thermal load or condition is a `FaceName`; the brief's second variant does not exist in P18-DATA. Every selector form the data model validates
  — start cap, end cap, side with entity, side with a path edge, side with a path sketch, chamfer edge, a copied face, and a face of a feature that does not exist — is round-tripped
  ([GEOMETRY_REFERENCE.md](GEOMETRY_REFERENCE.md)).
* **No change to any qualified file.** The thermal module gained one source and one header and one line in its CMake list.
