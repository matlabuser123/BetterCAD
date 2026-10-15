# P17-QUAL-001 — solver identity and currentness audit

```text
SUBJECT:  whether a structural result can ever be read as describing a model
          it was not computed from -- after a geometry, mesh, material, load,
          restraint or settings change, after an undo, after a load from disk,
          or across a mesh of the same counts
METHOD:   the provenance model read from the code; every row of the staleness
          truth table traced to the test that drives it through the DOCUMENT
          (not through a mutated struct); then the rows run as ONE sequence
```

## The provenance model

```text
StructuralResultSource       body, control, GeometryRevision, MeshStamp,
                             MaterialId + materialRevision,
                             AnalysisId + analysisRevision
                             -- field-complete, defaulted operator==, so a
                             dependency added without a comparison is impossible
resultCurrency(result, now)  NoResult | Current | Stale
staleReasons(result, now)    a SET in enum order: Body, Control, Geometry,
                             Mesh, Material, Analysis -- never the first mismatch
analysisState(...)           NoAnalysis | InputsUnavailable | Ready |
                             SolvedCurrent | SolvedStale | SolveFailed
                             -- DERIVED, never stored; no cached flag anywhere
StructuralResult             immutable; no setter; no non-const source();
                             create() refuses a source that does not name the
                             mesh, a size that does not match it, or a non-finite
                             value
```

Revisions are **monotonic**: an undo restores content and takes a NEW
revision. That single property is what makes "undo cannot resurrect a result"
true without a mechanism to test (`StructuralCommand_UndoMovesTheRevisionForwardRatherThanBack`).

## The truth table, each row driven through the document

```text
| EDIT                        | MESH      | RESULT  | REASONS           | TEST                                                        |
| geometry parameter          | stale     | inputs  | (boundary refuses | StructuralData_AGeometryEditStalesTheResultThroughTheInputBoundary |
|                             | (geometry)| unavail.| before a source)  | StructuralInput_RefusesAMeshOfGeometryThatHasSinceChanged   |
| ... then remeshed           | current   | stale   | geometry, mesh    | P17Qualification_EveryDependencyEditStalesTheResultInOneSequence |
| FAILED regeneration         | not model | inputs  | geometry_ineligible| P17Qualification_AFailedRegenerationNeverYieldsACurrentResult |
|                             |           | unavail.| at the Inputs stage| StructuralInput_RefusesAfterAFailedGenerationOfEligibleGeometry |
| meshing intent              | stale     | inputs  | -> mesh after a   | StructuralInput_RefusesAMeshWhoseSizingIntentHasChanged     |
|                             | (intent)  | unavail.| remesh            | StructuralValidation_RefusesAMeshWhoseSizingIntentMoved     |
| remesh, nothing else edited | current   | stale   | mesh              | StructuralReaction_StalesOnARemeshAndRefusesAStaleLookup    |
|                             |           |         |                   | MeshDofMap_ARemeshInvalidatesTheNumberingAndTheConstraintsBuiltOnIt |
| material value              | CURRENT   | stale   | material          | StructuralData_AMaterialEditStalesTheResultAndNotTheMesh    |
|                             |           |         |                   | StructuralMaterial_AYoungsModulusEditLeavesTheMeshCurrentAndStalesTheResult |
| material re-assignment      | current   | stale   | material          | StructuralMaterial_APoissonEditAndAReassignmentBehaveTheSameWay |
| load add / edit / remove    | current   | stale   | analysis          | StructuralCommand_EveryPhysicsCommandStalesTheResultAndNotTheMesh |
| restraint add / edit / rem. | current   | stale   | analysis          | same; StructuralBC_ARestraintEditStalesTheResultAndNotTheMesh |
| solver settings             | current   | stale   | analysis          | same (ADR-047: settings are canonical)                      |
| a NO-OP edit                | current   | CURRENT | --                | StructuralCommand_ANoOpEditDoesNotStaleACurrentResult       |
| a FAILED command            | current   | CURRENT | --                | StructuralCommand_AFailedCommandLeavesACurrentResultCurrent |
| a viewer-only change        | current   | CURRENT | --                | StructuralData_AViewerOnlyChangeDoesNotStaleAResult         |
| undo after solve            | current   | stale   | analysis          | StructuralCommand_UndoDoesNotResurrectTheOldResult          |
| save / load                 | NoMesh    | none    | nothing restored  | StructuralPersistence_ALoadedAnalysisHasIntentAndNoCurrentResult |
| editing the provenance      | --        | stale   | immutable: cannot | StructuralData_AStaleResultCannotBeMadeCurrentByEditingItsProvenance |
| same counts, other mesh     | --        | refused | stamp, not counts | StructuralData_AResultMustMatchItsMeshExactly; StructuralViz_RefusesAResultFromADifferentMesh |
| every dependency alone      | --        | stale   | each, by itself   | StructuralData_EveryDependencyAloneCanStaleAResult          |
| stamp populated, not default| --        | --      | --                | StructuralData_TheSourceStampIsPopulatedFromTheDocumentAndNotDefaulted |
```

## The rows as one sequence

`P17Qualification_EveryDependencyEditStalesTheResultInOneSequence`, on
RM-FEA-04 (eight nodes, six tetrahedra, fixed base, pressure on top), measured
on the frozen candidate in debug-ext:

```text
STEP  EDIT                        MESH          RESULT (of the solve before)   FRESH SOLVE
1     press_a 60 -> 72 mm         StaleGeometry InputsUnavailable               after remesh:
      ... remeshed                Current       Stale {geometry, mesh}          current, fingerprint differs
2     global target 20 -> 15 mm   StaleIntent   InputsUnavailable
      ... remeshed                Current       Stale {mesh}                    current
3     E 210 -> 190 GPa            Current       Stale {material}, SolvedStale   current; larger displacement
4     pressure x 2 (command)      Current       Stale {analysis}                current; displacement x 2
                                                                                to 1e-12 relative
5     + Ux hold on a side face    Current       Stale {analysis}                current; more constrained
      (command)                                                                 DOFs; fingerprint differs
6     UNDO step 5                 Current       step-4 and step-5 results       current; fingerprint ==
                                                both Stale {analysis}           step 4's, bit for bit
7     residual tolerance halved   Current       Stale {analysis}                current; fingerprint ==
      (command)                                                                 step 6's, bit for bit
END   the FIRST result            --            Stale                           steps 3..7 share ONE mesh
                                                                                stamp
fingerprints   0x7080dbacde25e0ec (first) -> 0x8f56696c4cdf9274 (last)
```

Seven edits, seven stale verdicts, eight solves. Steps 3 to 7 solve the mesh
ALREADY HELD rather than regenerating, so each verdict is the edit's and not a
remesh's — asserted by comparing the mesh stamp of the last result with the
mesh stamp of step 2's.

## What is NOT a currentness signal, and why that is right

```text
result existence          an analysis with no result is Ready, not broken
                          (StructuralData_TheAnalysisStateMachineReachesEveryStateItDeclares)
equal numbers             a fresh solve that reproduces an old answer is a NEW
                          result; the old one stays stale (step 6 above)
the viewer                StructuralDisplayState reads MeshCurrency and
                          AnalysisState side by side, never merges them, holds
                          no flag of its own (StructuralViz_StaleResultIsNeverDrawableAsCurrent)
the CLI                   never holds a result between invocations; every
                          report command re-validates and re-solves
                          (StructuralCli_SolvingNeverChangesTheDocument)
```

## Verdict

```text
solver identity          PASS  -- a result is bound to a MeshStamp, an analysis
                                 revision, a material revision and a geometry
                                 revision, and nothing can rewrite the binding
currentness              PASS  -- every dependency, alone and in sequence,
                                 including undo, a failed rebuild and a load
```
