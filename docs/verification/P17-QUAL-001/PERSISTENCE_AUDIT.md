# P17-QUAL-001 — persistence, commands and visualisation audit

```text
SUBJECT:  whether anything derived can reach the file, the history or the
          display as authority; whether undo restores intent and only intent;
          whether a loaded document can trust an old array
METHOD:   the writer's signature and allow-lists read; grep for a handle in
          src/io; the covering tests named; the final gate run over every
          reference document
```

## Persistence (P17-PERSIST-001, ADR-048)

```text
what is written            "type": "structural-analysis": mesh control id,
                           mode, loads, restraints, solver settings -- the
                           five members of the definition, none derived
                           (StructuralPersistence_TheWholeCanonicalIntentRoundTrips,
                           ..._SolverSettingsRoundTripExactly)
what cannot be             the writer takes `const StructuralAnalysis&` and
                           nothing else; a StructuralResult is not a document
                           object, so there is no field for it
                           (..._NoSolverStateReachesTheFile,
                           ..._TheFileNeedsNoSolverOrMesherToBeInterpreted)
NodeId / ElementId in io   0 uses; 2 comment lines saying so
nodal force load           REFUSES the save, atomically, naming the load and
                           what to do; the reader refuses "nodal_force" by name
                           (..._ANodalForceLoadIsRefusedRatherThanWritten,
                           ..._TheReaderRefusesANodalForceLoadByName)
unresolved reference       preserved as written and NOT rebound; still refuses
                           the solve after a load
                           (..._AnUnresolvedReferenceIsPreservedAndNotRebound,
                           ..._AnUnresolvedReferenceStillRefusesTheSolveAfterLoading)
malformed input            rejected rather than defaulted; duplicate ids
                           rejected rather than merged; unknown fields rejected
                           (..._RejectsMalformedStructuralIntent,
                           ..._RejectsInvalidSolverSettingsRatherThanDefaulting,
                           ..._RejectsDuplicateIdentitiesRatherThanMerging)
a loaded analysis          has intent and NO current result; the first solve
                           after a load is computed, not restored
                           (..._ALoadedAnalysisHasIntentAndNoCurrentResult)
byte stability             save/load/save identical, a fixed point over three
                           passes; independent of edit order; identical whatever
                           the solve has done, in four states; independent of
                           mesh and result size
                           (..._SaveLoadSaveIsByteIdentical,
                           ..._SerializationIsByteIdenticalAndIndependentOfEditOrder,
                           ..._TheFileIsIdenticalWhateverTheSolveHasDone,
                           ..._TheFileDoesNotFollowTheMeshOrTheResultSize)
pre-P17 documents          load and simply have no analyses; the format version
                           stays 2 (..._APreP17DocumentLoadsAndSimplyHasNoAnalyses)
history                    does not reach the file (..._TheCommandHistoryDoesNotReachTheFile);
                           commands undo and redo exactly after a load
                           (..._CommandsUndoAndRedoExactlyAfterALoad)
full round trip            destroy, load, regenerate, mesh, solve -> the same
                           solution (..._AFullDestroyAndLoadRoundTripReSolvesEquivalently);
                           every reference document, bit for bit
                           (StructuralReference_SaveLoadAndResolveReproducesEverySolutionBitForBit)
```

**The final gate**, `P17Qualification_SolvingLeavesEveryReferenceDocumentByteIdentical`:
all twelve committed reference documents, saved before and after a mesh and a
solve through the production entry (ten solved, two refused), byte-identical,
object count and revision unchanged, no derived array name in the bytes.

## Commands and undo/redo (P17-CMD-001, ADR-047)

```text
what a command edits       ONE analysis definition, through Document::modifyObject
                           and setDefinition, which validates before assigning
what undo restores         the stored before-definition, ASSIGNED, never inverted
                           (StructuralCommand_RepeatedUndoRedoDoesNotDriftTheStoredValues,
                           ..._UndoAllThenRedoAllIsExactAtEveryStep,
                           ..._EditLoadRestoresEveryFieldExactly,
                           ..._RestraintComponentsSurviveUndoAndRedoExactly)
what undo cannot restore   a stiffness matrix, a result, a numbering: no
                           command's API mentions one, and the compile-fail
                           cases pin it (A_COMMAND_IS_NOT_KEYED_ON_A_DOF_INDEX,
                           THE_DEFINITION_HAS_NO_STIFFNESS_MATRIX,
                           A_COMMAND_IS_NOT_CONSTRUCTIBLE_FROM_A_RESULT)
the old result             stays STALE after an undo, because the revision
                           moved forward (..._UndoDoesNotResurrectTheOldResult,
                           ..._UndoMovesTheRevisionForwardRatherThanBack); re-proved
                           in the one-sequence gate with a fresh solve that
                           agrees with the pre-edit solve bit for bit
failure                    atomic: a refused edit never reaches the history and
                           leaves a current result current
                           (..._ADuplicateLoadIdFailsAtomically,
                           ..._ANonFiniteLoadValueFailsAtomically,
                           ..._AFailedCommandLeavesACurrentResultCurrent,
                           ..._ACommandWhoseApplyFailedCannotBeUndone)
no-op                      moves no revision, stales nothing
settings                   canonical, undoable, validated on create
order                      a load or restraint edited in place keeps its position;
                           a removed one returns to its original position
identity                   create/undo/redo keep the same AnalysisId; two
                           analyses do not confuse their load ids
```

## Visualisation (P17-VIZ-001, ADR-043..046)

```text
reads, never computes      every channel value is the source's value
                           (StructuralViz_EveryChannelsValuesAreItsSourcesValues,
                           ..._InspectionReportsExactSourceValues,
                           ..._RangeAndExtremesMatchAnIndependentSweep)
deformed display           x + s u in SI metres from the UNDEFORMED buffer every
                           time, non-cumulative, presentation-only scale
                           (..._DeformedPositionsAreTheUndeformedPlusScaledDisplacement,
                           ..._ADeformedDisplayAtScaleZeroIsTheUndeformedImage,
                           ..._ArrowScaleIsPresentationOnly)
mutation                   rendering changes nothing it reads
                           (..._RenderingChangesNothingItReads); the result is
                           immutable by construction
identity                   refuses a result of another mesh, of the same mesh from
                           another source, a stale DOF numbering
                           (..._RefusesAResultFromADifferentMesh,
                           ..._AViewRefusesAResultOfTheSameMeshFromAnotherSource,
                           ..._AStaleDofNumberingIsRefusedNotTrusted)
stale state                never drawable as current; a stale contour is not
                           drawn at all (..._StaleResultIsNeverDrawableAsCurrent,
                           ..._AStaleContourIsNotDrawnAtAll)
absent is not zero         (..._AChannelWithNoValueIsAbsentNotZero,
                           ..._AnEntityWithNoValueIsRefusedNotColouredBandZero)
signs                      every signed channel keeps every sign; a reaction
                           keeps the qualified sign; magnitude extremes report
                           the signed value
thresholds                 none of its own; StructuralDisplayState reads
                           MeshCurrency and AnalysisState side by side
scale against the kernel   the deformed mesh overlays its own CAD solid at the
                           same scale -- 42036 covered pixels all four ways,
                           measured against the kernel's own solid
                           (..._ADeformedMeshOverlaysItsOwnCadSolidAtTheSameScale)
GUI                        apps/bettercad has no structural reference; the Qt
                           inspector surface is not wired (recorded scope limit)
```

## Verdict

```text
undo/redo PASS                               generated solver arrays excluded
                                             from history: by construction and
                                             by compile-fail case
persistence PASS                             derived solver arrays excluded as
                                             authority: no field exists; twelve
                                             documents byte-identical through
                                             a solve
visual inspection PASS                       a read-only adapter that refuses
                                             what it cannot honestly draw
```
