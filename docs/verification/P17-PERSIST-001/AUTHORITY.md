# P17-PERSIST-001 — what is authority and what is derived

```text
RULE:     save  ->  structural-analysis intent
          load  ->  restore intent; regenerate the mesh through P16;
                    solve again when asked
```

## The table

| Field / data | Persist? | Where it lives instead |
| --- | --- | --- |
| `AnalysisId` | **YES** | the document object's own id |
| analysis definition (mesh control, mode) | **YES** | `data.mesh`, `data.mode` |
| `LoadId` | **YES** | `data.loads[].id` |
| canonical load values | **YES** | `magnitude`, `traction`, `acceleration` |
| `GeometryReference` load target | **YES** | `data.loads[].face`, via `faceNameToJson` |
| `RestraintId` | **YES** | `data.restraints[].id` |
| restraint component mask | **YES** | `data.restraints[].components` |
| `GeometryReference` restraint target | **YES** | `data.restraints[].face` |
| solver settings | **YES** | `data.solver`, all three fields |
| | | |
| `NodeId` | **NO** | mesh-local, rebuilt by P16 (ADR-031) |
| `ElementId` | **NO** | mesh-local |
| `DofIndex` | **NO** | `MeshDofMap`, rebuilt per mesh |
| `FreeEquationIndex` | **NO** | `FreeEquationMap`, rebuilt per solve |
| boundary facets / triangle ids | **NO** | `GeometryMeshMap`, resolved per mesh |
| `PreparedStructuralLoads` | **NO** | rebuilt by `prepareStructuralLoads` |
| `PreparedStructuralRestraints` | **NO** | rebuilt by `prepareStructuralRestraints` |
| `ConstraintSet` | **NO** | rebuilt per mesh and numbering |
| `K`, `F`, `Kff`, `Ff` | **NO** | `GlobalStructuralSystem`, assembled per solve |
| a factorisation | **NO** | inside the solver, never outside it |
| `u`, `uf` | **NO** | `StructuralResult`, held by the caller |
| strain / stress / principal / von Mises | **NO** | `StructuralResult`, `RecoveredFields` |
| reaction arrays and summaries | **NO** | `SupportReactions` |
| residual, pivot ratio, strain energy | **NO** | `StructuralSolveOutcome` |
| validation report | **NO** | recomputed by `validateStructuralAnalysisForSolve` |
| mesh quality report | **NO** | P16's, derived, recomputed |
| render / contour buffers, deformed coordinates | **NO** | P17-VIZ, per frame |
| result currentness (`SolvedCurrent` etc.) | **NO** | derived by `staleReasons` from sources |
| undo / redo history | **NO** | `CommandHistory`, per session |

**`K`, `F`, `u`, strain, stress, von Mises, reactions and the validation report
are not persisted as authority. None of them is persisted at all.**

## The exclusion is structural, not a discipline

This is the same argument P16-PERSIST-001 made, and it is why the serializer is
short. Two facts do the work:

```text
1  StructuralAnalysisDefinition has exactly FIVE members:
       MeshControlId mesh
       StructuralAnalysisMode mode
       vector<StructuralLoad> loads
       vector<StructuralRestraint> restraints
       SolverSettings solver
   and not one of them is derived.

2  A StructuralResult does not live on a document object AT ALL. It is
   returned by solveStructuralAnalysis and held by the caller, which is why
   `StructuralAnalysis`'s own header says it "holds INTENT and never a
   result: no displacement array, no stiffness matrix and no stress lives on
   a document object".
```

So there is nothing in the writer's reach to exclude. The writer's signature
says so:

```cpp
Result<Json> structuralAnalysisToJson(const structural::StructuralAnalysis& analysis);
```

Its only argument is the document object. **There is no expression it could
write that depends on whether a solve has run**, because no solve output is
reachable from it. Leaking one would mean changing the signature and every
caller — and that is a measurement, not a claim: see
[MUTATION.md](MUTATION.md), where two of the brief's requested mutations turned
out to be unreachable for exactly this reason, and what was done instead to
show the byte gate still has teeth.

The reader's signature carries the matching half:

```cpp
Result<std::unique_ptr<structural::StructuralAnalysis>>
structuralAnalysisFromJson(const Json& data, std::string name, std::string_view path);
```

No `Document`, no `Regenerator`, no `Mesher`, no geometry. **There is nothing
for an unresolved reference to be rebound TO.** The nearest-face rebind the
brief asks about cannot be written here without adding a parameter.

## The source audit the brief asks for

Searched across the whole structural module for a serializer of derived state:

```text
toJson(StructuralResult)                does not exist
serialize(GlobalStructuralSystem)       does not exist
toJson / fromJson / serialize anywhere
  in include/bettercad/structural/
  or src/structural/                    does not exist
```

The only match for "serialize" in the module is a comment in its
`CMakeLists.txt` saying it sits "BELOW IO, which will serialize its analysis
intent". So the brief's §123 concern — a debug serializer accidentally
reachable from canonical save — has nothing to check in either direction. There
is no result serializer anywhere in BetterCAD to keep out of the save path.

Searched in the canonical save path for the forbidden type names: none appears.
`StructuralAnalysisJson.cpp` includes only `ObjectJson.hpp`, which reaches
`StructuralAnalysisObject.hpp` and through it the canonical load, restraint and
data headers — and not `StructuralSystem.hpp`, `StructuralSolve.hpp`,
`StructuralPost.hpp`, `StructuralReaction.hpp` or `StructuralResult.hpp`.

**One honest qualification.** That is a statement about what the serializer
*includes*, not a claim of include isolation. `StructuralRestraint.hpp`
reaches `StructuralDof.hpp`, so `DofIndex`, `MeshDofMap`, `ConstraintSet` and
`FreeEquationMap` are *nameable* from this translation unit — exactly as
P17-CMD-001 measured for the command header, and for the same reason: that is
how the canonical restraint type has always been declared. What is checked here
is that none of them is written, and that the result and system types are not
reachable at all.

## Tested, not argued

| Claim | Test |
| --- | --- |
| no solver state in the written section | `StructuralPersistence_NoSolverStateReachesTheFile` — 32 forbidden key names, scanned after a real solve |
| the file is the same whatever the solve did | `..._TheFileIsIdenticalWhateverTheSolveHasDone` — four states, byte-compared |
| the file does not follow the mesh or the result size | `..._TheFileDoesNotFollowTheMeshOrTheResultSize` — 499 vs 1313 elements |
| a loaded analysis has no current result | `..._ALoadedAnalysisHasIntentAndNoCurrentResult` |
| loading runs no solve and no remesh | same test — the fresh `Mesher` still holds nothing afterwards |
| a save changes nothing about the document | `..._SavingChangesNothingAboutTheDocument` |
| no `NodeId` can reach the file | `..._ANodalForceLoadIsRefusedRatherThanWritten`, `..._TheReaderRefusesANodalForceLoadByName` |
| the command history does not reach the file | `..._TheCommandHistoryDoesNotReachTheFile` |
