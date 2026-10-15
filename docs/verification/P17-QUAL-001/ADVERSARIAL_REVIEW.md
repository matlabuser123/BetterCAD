# P17-QUAL-001 — final adversarial review

```text
SUBJECT:   an attempt to disprove P17 as a whole before calling it qualified
QUESTIONS: 30 from the brief, and 14 further ones of the reviewer's
FINDINGS:  13 -- 11 document drift (ADR text, resolved by amendment; one of
           them also a docs/ARCHITECTURE.md correction), 1 stale status
           block in a qualified milestone's README (D1, addendum), 1 test
           defect in this milestone's own new file found and fixed before
           the freeze (T1)
PRODUCTION DEFECTS:          0
GATE-BLOCKING DEFECTS:       0
OWNER DECISIONS CARRIED:     3 -- F1, F2, F3 from P17-REFMOD-001, unchanged
```

Each question below was answered by reading the code or running something.
The format is the brief's: the ATTACK, the TEST that answers it, the RESULT.
Where the answer was "yes, it could", the resolution came before the freeze.
Where a question could not be answered in this environment, that is said.

## The thirty the brief names

**1. Can P17 solve a stale mesh?**
ATTACK: edit a parameter after meshing, then call the production entry.
TEST: RM-FEA-09 in the runner on all ten solvable documents (`after_change ...
currency stale: the geometry changed validation rejected input_problem
mesh_stale solve refused`); `StructuralStaleMesh_IsRefusedBeforeAssemblyAndSolvesAgainAfterRemeshing`;
`StructuralValidation_RefusesAStaleMeshThroughThePublicEntry`;
`StructuralInput_RefusesAMeshOfGeometryThatHasSinceChanged`.
RESULT: refused at the Inputs stage before anything is numbered or assembled;
the held mesh is kept and reported stale. **No.**

**2. Can P17 solve geometry that has failed regeneration?**
ATTACK: a result current, then a depth of zero, then a rebuild that FAILS.
TEST: `P17Qualification_AFailedRegenerationNeverYieldsACurrentResult` (new
at this gate: no structural test had put the failed rebuild in sequence);
`StructuralInput_RefusesAfterAFailedGenerationOfEligibleGeometry`.
RESULT: validation rejected at Inputs with `geometry_ineligible`, 0 nodes
measured past it, solve refused, generate refused, state
`inputs_unavailable`; recovery at the original depth reproduces the first
fingerprint bit for bit and the first result stays stale. **No.**

**3. Can a material edit leave an old result current?**
ATTACK: change E and ask whether the old result is current; also delete the
material stale reason from the code (probe Q5).
TEST: `StructuralData_AMaterialEditStalesTheResultAndNotTheMesh`,
`StructuralMaterial_AYoungsModulusEditLeavesTheMeshCurrentAndStalesTheResult`,
step 3 of `P17Qualification_EveryDependencyEditStalesTheResultInOneSequence`.
RESULT: stale for `{material}`, mesh current; Q5 KILLED by three cases. **No.**

**4. Can a load edit leave an old result current?**
TEST: `StructuralCommand_EveryPhysicsCommandStalesTheResultAndNotTheMesh`;
step 4 of the one-sequence gate, through `EditStructuralLoadCommand` and the
real history, with the fresh solve showing twice the displacement for twice
the pressure to 1e-12. RESULT: stale for `{analysis}`. **No.**

**5. Can a restraint edit leave an old result current?**
TEST: `StructuralBC_ARestraintEditStalesTheResultAndNotTheMesh`; step 5 of
the one-sequence gate (a side-face hold added by command: more constrained
DOFs, a different fingerprint). RESULT: stale for `{analysis}`. **No.**

**6. Can NodeId become permanent CAD load identity?**
ATTACK: look for a `NodeId` in a load, a restraint, a command, the file.
TEST: `StructuralLoad_CanonicalFaceLoadsCarryNoMeshHandle`,
`StructuralBC_CanonicalRestraintsCarryNoMeshHandle`; grep `NodeId|ElementId`
in `src/io` and `include/bettercad/io`: 2 comment lines, 0 uses;
`StructuralPersistence_ANodalForceLoadIsRefusedRatherThanWritten` for the
one load that DOES carry a NodeId (mesh-local, refuses the save, ADR-048);
compile-fail `A_RESTRAINT_DOES_NOT_TARGET_A_NODE`.
RESULT: **No.** A `NodalForceLoad` is the deliberate exception and it dies
with its mesh (`StructuralLoad_NodalForceIsMeshLocalAndDiesWithItsMesh`).

**7. Can ElementId survive a remesh as solver intent?**
TEST: `StructuralLoad_SurvivesARemeshWithoutAnyFacetIdentity`,
`StructuralBC_ReResolvesTheNodeSetAfterARemesh`,
`MeshDofMap_ARemeshInvalidatesTheNumberingAndTheConstraintsBuiltOnIt`,
`StructuralReaction_StalesOnARemeshAndRefusesAStaleLookup`.
RESULT: every facet, node and DOF is re-resolved from the `FaceName` per
mesh; a numbering or a result of the old mesh is refused by stamp. **No.**

**8. Can a load silently move to another face after regeneration?**
ATTACK: search for nearest-face, closest, rebind logic.
TEST: grep in `src/structural`: 2 lines, both comments saying there is none;
`StructuralLoad_RefusesAnUnresolvedTargetRatherThanTreatingItAsZero`;
`StructuralPersistence_AReloadedFaceLoadResolvesToTheSameGeometry`;
RM-FEA-10 committed with a load on a face that does not exist.
RESULT: the reference is KEPT and reported; the solve is refused at the load
stage with the restraint stage already passed; "Nothing nearby is
substituted" is the diagnostic's own text. **No.**

**9. Can an unresolved support bind to a nearby face?**
TEST: `StructuralBC_RefusesAnUnresolvedTargetWithNoNearestFaceFallback`,
`StructuralBC_RefusesTheDrilledHoleWallWithNoGeometricFallback`,
`StructuralPersistence_AnUnresolvedReferenceIsPreservedAndNotRebound`,
`StructuralViz_DrilledHoleWallIsNotFakedByTheViewer`. RESULT: **No.**

**10. Can an inverted Tet enter stiffness assembly?**
ATTACK: remove the `det < 0` refusal (probe Q4).
TEST: `Tet4Element_RejectsAnInvertedTetrahedronRatherThanReorderingIt`,
`..._RejectsAnOddNodePermutation`, `..._ReflectedGeometryIsRejectedBecauseOrientationFlips`;
upstream, P16 refuses a mesh with a non-positive element before P17 sees it.
RESULT: Q4 KILLED, 3 cases. **No.**

**11. Can a zero-volume Tet produce finite-looking stiffness?**
TEST: `Tet4Element_RejectsDegenerateAndNonFiniteGeometry` (det == 0 and
non-finite are `DegenerateElement` before any division);
`StructuralPost_RefusesANonFiniteOrInvalidElementInCheckOrder`;
grep `std::abs(det|volume)` in `src/structural`: 0. RESULT: **No.**

**12. Can engineering and tensor shear conventions get mixed?**
TEST: the convention is in the FIELD NAMES (`gammaXy`,
`StructuralData_TheShearConventionIsEngineeringAndIsInTheFieldNames`); D
carries `mu` on its shear diagonal (`Tet4Element_EngineeringShearGivesTauEqualsMuGamma`,
`StructuralPost_ConvertsPureEngineeringShearThroughMuAndNotTwoMu`);
`tensorOf(Strain6)` halves (`StructuralPost_HalvesEngineeringShearWhenBuildingTheStrainTensor`,
`..._ReportsPrincipalStrainsThroughTheHalvedShear`); P17-REFMOD-001 M12
(`2 mu`) KILLED by the patch test and the axial bar. RESULT: **No.**

**13. Can B or D ordering disagree with post-processing?**
TEST: one frozen order `XX YY ZZ XY YZ ZX`
(`StructuralData_TheTensorComponentOrderIsFrozen`,
`Tet4Element_BRowOrderMatchesTheSharedVoigtOrdering`); recovery reaches B
and D only through `strainFrom`/`stressFrom` (ADR-040 audit row); the
viewer cannot transpose channels (`StructuralViz_ChannelsAreNotTransposed`).
RESULT: **No.**

**14. Can global K lose symmetry during assembly?**
TEST: `StructuralSystem_IsExactlyAsSymmetricAsItsElementsAre` (no
`0.5 (K + K^T)` anywhere to hide an asymmetry);
`..._MatchesAnIndependentDenseScatterOfEveryElement`; Q1 (an overwrite
breaks the scatter) KILLED by 11 cases. RESULT: **No.**

**15. Can duplicate sparse entries overwrite instead of accumulate?**
ATTACK: probe Q1, `+=` to `=` at the one accumulation line.
RESULT: KILLED, 11 cases / 67 assertions, including both closed forms. **No.**

**16. Can an under-constrained model appear solved?**
ATTACK: remove the pivot gate (probe Q2); RM-FEA-08 with eight DOFs all in z.
TEST: `StructuralUnderConstrained_IsRefusedByThePivotGateAndNothingIsPublished`,
`StructuralSolve_RefusesAFreeBodyAlthoughTheLibrarySucceeds`,
`..._RefusesAnInsufficientlyRestrainedModel`, `StructuralValidation_DoesNotClaimSixDegreesOfFreedomIsSufficient`.
RESULT: Q2 KILLED, 9 cases; RM-FEA-08 refused with the smallest pivot
1.1e-17 of the largest and nothing published. **No.**

**17. Can the solver return success with a large residual?**
ATTACK: remove the residual gate (probe Q3).
TEST: `StructuralSolve_ReportsEveryProblemThroughTheSameOrderedChecks`,
`..._ChecksTheFreeResidualAndKeepsTheReactions`; the residual is computed
over BetterCAD's own CSR and `solver.error()` is never consulted.
RESULT: Q3 KILLED -- by ONE test, which `MUTATION.md` records rather than
dresses up: no physics case can reach the gate because LDLT on an SPD system
is backward stable. **No**, with the thinness of the kill stated.

**18. Can reactions fail force equilibrium?**
ATTACK: flip the reaction sign (probe Q7).
TEST: 19 reaction cases plus the two closed forms; the runner's
`equilibrium force_eta` on every solved document (1e-13 .. 1e-18).
RESULT: Q7 KILLED, 21 cases. **No.** The MOMENT half is where F1 lives: a
correct bending solve of ~2500 needle elements measures `eta_M` 1.15e-12
against the 1e-12 threshold (P17-REFMOD-001), so the threshold refuses a
correct answer at that size. Carried to the owner; no threshold changed.

**19. Can pressure conversion fail to preserve the resultant?**
TEST: `StructuralLoad_TractionPreservesTheResultantForceAndMoment`,
`..._ConvergesToTheAnalyticLoadAsTheSurfaceIsRefined`,
`StructuralPressureBlock_PreservesTheResultantAndBalancesTheReactions`
(5400 N); REFMOD M11 (`A t / 2`) and M13 (outward) KILLED. RESULT: **No.**

**20. Can a rigid transform change stress invariants?**
TEST: `StructuralTransformed_KeepsTheInvariantsAndRotatesTheVectors`
(RM-FEA-06, connectivity correspondence asserted, invariants to 1e-5 against
1e-6 measured), `StructuralPost_KeepsTheInvariantsWhenTheWholeModelIsRotated`,
`Tet4Element_IsCovariantUnderAProperRotation`. RESULT: **No.**

**21. Can displacement scaling mutate actual result data?**
TEST: `StructuralViz_DeformedPositionsAreTheUndeformedPlusScaledDisplacement`
(from the undeformed buffer each time), `..._ADeformedDisplayAtScaleZeroIsTheUndeformedImage`,
`..._RenderingChangesNothingItReads`; `StructuralResult` has no setter.
RESULT: **No.**

**22. Can the GUI recalculate stresses differently from core?**
TEST: grep `src/renderer` for a stress, strain, von Mises, principal or
stiffness formula: 0; `StructuralViz_EveryChannelsValuesAreItsSourcesValues`,
`..._InspectionReportsExactSourceValues`; `apps/bettercad` has 0 structural
references. RESULT: **No** -- and the Qt surface is not wired at all, which
is a recorded scope limit, not a second authority.

**23. Can undo restore a stiffness matrix instead of analysis intent?**
TEST: no command's API mentions one; compile-fail
`THE_DEFINITION_HAS_NO_STIFFNESS_MATRIX`, `A_COMMAND_IS_NOT_CONSTRUCTIBLE_FROM_A_RESULT`,
`A_COMMAND_IS_NOT_KEYED_ON_A_DOF_INDEX`; undo ASSIGNS a stored definition
(`StructuralCommand_RepeatedUndoRedoDoesNotDriftTheStoredValues`); the
result after undo is recomputed, not revived (step 6 of the one-sequence
gate: same numbers as before the edit, bit for bit, and the OLD result still
stale). RESULT: **No.**

**24. Can save/load trust old displacement / stress arrays?**
TEST: the writer takes `const StructuralAnalysis&` -- there is no field;
`StructuralPersistence_NoSolverStateReachesTheFile`,
`..._ALoadedAnalysisHasIntentAndNoCurrentResult`,
`..._TheFileIsIdenticalWhateverTheSolveHasDone`;
`P17Qualification_SolvingLeavesEveryReferenceDocumentByteIdentical` over all
twelve documents. RESULT: **No.**

**25. Can the CLI implement separate Tet stiffness logic?**
TEST: grep `apps/bettercad_cli` for `Eigen`, `StiffnessMatrix`,
`computeTet4Stiffness`, `assembleStructuralSystem`, `solveStructuralSystem`,
`recoverFields`, `isotropicElasticity`: 0; one `solveStructuralAnalysis`
call; `StructuralCli_SolveMatchesTheCore` and its three siblings.
RESULT: **No.**

**26. Can the CLI return zero on a singular solve?**
ATTACK: probe Q6, `ExitCode::Success` after `solve_failed`.
TEST: `refmod.structural.refused.under-constrained.*` (four fresh-process
commands on the committed RM-FEA-08 document), `StructuralCli_EveryRefusalClassReachesTheExitStatus`.
RESULT: Q6 KILLED at the process boundary, 4 of 14. **No.**

**27. Can mesh-quality thresholds differ between GUI and CLI?**
TEST: `structuralQualityThresholds()` has exactly one caller, inside
`validateStructuralAnalysisForSolve`; the CLI and the viewer reach it only
through that entry; `StructuralCli_ThereIsNoFlagThatBypassesValidation`,
`StructuralCli_ValidationMatchesTheCore`. RESULT: **No.**

**28. Can stale binaries produce qualification evidence?**
TEST: every process test takes its executable from `$<TARGET_FILE:...>`;
each preset is CLEANED before its build and the no-op rebuild re-runs only
the git-revision stamp; the binaries' sizes and SHA-256 are recorded per
preset in `FREEZE.md`; the seven probes were followed by a rebuild from the
restored tree and the harness cleans again. RESULT: **No.**

**29. Can a zero-test filter pass qualification?**
TEST: `ZeroMatchGuard.cmake` asserts a discovered count per filter (this
milestone adds `unit\.P17Qualification` 3) and proves its own counter
against an impossible pattern; `verify-harness.cmd` proves a zero-match
repeat filter is counted as a FAILED stage; `qualify.cmd` records the
selected count. RESULT: **No.** Table in `FREEZE.md`.

**30. Can source change after the final qualification?**
TEST: the eight-path fingerprint is taken by the harness before the first
build and after the last test, recomputed before the commit, and the commit's
own subtree hashes are compared against it; `docs/`, `TODO.md`, `ROADMAP.md`
and `README.md` are outside it by construction. RESULT: **No**; see
`FREEZE.md`.

## Fourteen further questions

**31. Can a result be made current by editing its provenance?** No:
`StructuralResultSource` is reachable only through a const accessor
(`StructuralData_AStaleResultCannotBeMadeCurrentByEditingItsProvenance`).

**32. Can a result of another mesh with the SAME node and element counts be
read against this one?** No: `describes()` compares stamps, never counts
(`StructuralData_AResultMustMatchItsMeshExactly`,
`StructuralViz_AViewRefusesAResultOfTheSameMeshFromAnotherSource`).

**33. Can a no-op edit or a failed command stale a current result, charging
a re-solve for nothing?** No: `StructuralCommand_ANoOpEditDoesNotStaleACurrentResult`,
`..._AFailedCommandLeavesACurrentResultCurrent`.

**34. Can the one-sequence gate itself be fooled by a remesh?** It was,
once, in its first draft -- finding T1 below.

**35. Can a density-less material solve a gravity analysis?** No:
`StructuralMaterial_DensityIsRequiredOnlyByTheGravityMode`,
`StructuralValidation_RefusesAMaterialMissingWhatTheModeRequires`; and a
density on the material does NOT make a problem a gravity problem -- the
mode does (P17-MAT-001).

**36. Can an analysis with no load at all publish `u = 0` as a solution?**
It warns: `NoLoadsApplied` is a warning by design
(`StructuralValidation_WarnsWhenNothingIsLoaded`), and the solve proceeds to
an exactly zero field. Recorded as a design choice, not a hole: the result is
correct and carries the warning.

**37. Can two analyses in one document confuse their loads, or the CLI pick
one silently?** No: `StructuralCommand_TwoAnalysesDoNotConfuseTheirLoadIds`,
`StructuralPersistence_TwoAnalysesRoundTripWithoutLeaking`,
`StructuralCli_AmbiguityIsRefusedRatherThanGuessed` (`ambiguous_structural_analysis`).

**38. Can a non-finite load value, a duplicate id or a malformed file reach
the solver?** No: `StructuralLoad_RefusesNonFiniteValuesAndDuplicateIdentities`,
`StructuralCommand_ANonFiniteLoadValueFailsAtomically`,
`StructuralPersistence_RejectsMalformedStructuralIntent`,
`..._RejectsDuplicateIdentitiesRatherThanMerging`.

**39. Can a configuration override reach a solve on geometry it did not
rebuild?** No: `ConfigurationOverrideActive` is P16's first ineligibility
and P17 inherits it unchanged through `requireStructuralModel` (carried
constraint 2; ADR-036 audit row). BLOCKED BY DESIGN, not supported.

**40. Is the patch test a tautology?** No: the single-tet case compares the
production stress with Hooke's law computed in `Analytic.hpp`, which includes
no BetterCAD header, and the meshed case checks `K u` against boundary
tractions computed from the suite's own area vectors; REFMOD M03 and M10
mutate the ORACLE and the suite fails.

**41. Does the cantilever "converge" because the tolerance is loose?** It
does not converge to beam theory at all on these meshes: 3.8%, 4.7%, 4.8% of
Timoshenko (F2), monotone, with the mechanism measured (F3: every wall
triangle spans the full length, so the wall tetrahedra are needles). The
suite asserts the trend, the monotonicity, the exact equilibrium and the
chord bound, and claims nothing more. Carried to the owner.

**42. Are the suite's fingerprints the same across presets, or only its
verdicts?** The same: bit-identical fingerprints over every displacement,
strain, stress, principal value, von Mises, hydrostatic stress and reaction
in debug-ext, release-ext and debug-shared-ext, and byte-identical CLI
payloads (`CROSS_PRESET.md`, re-measured on this candidate's binaries).

**43. Is any tolerance in this milestone's own tests looser than the one it
inherits?** No tolerance was added: the three gates assert byte identity,
set equality, exact fingerprint equality, and one linearity check at 1e-12
relative on an eight-node solve whose residual is 4e-17. `ANALYTICAL_VALIDATION.md`
carries the inherited table unchanged.

**44. Did the evidence of the qualified milestones agree with itself?** Not
entirely -- D1 below.

## Findings

### D1 — P17-VIZ-001's README still says PARTIAL (DOCUMENT DRIFT, addendum)

`README.md` opens with `STATUS: PARTIAL, 18 of 23 boxes` and has a section
"Why this is PARTIAL and not PASS"; `REMAINING_SCOPE.md` says the milestone
"stays open". Both were written after the second of four qualification runs.
`FREEZE_ELEMENTS.md` records the fourth run "takes P17-VIZ-001 to 23 of 23",
`TODO.md` ticks all 23, and the commit chain carries `e795acb`. Resolved by a
dated addendum at the top of both files; the original text is kept because
its measurements are still the measurements. `MILESTONE_AUDIT.md`.

### A1..A11 — eleven ADR sentences overtaken by later milestones (DOCUMENT DRIFT, amended)

Listed and resolved in `ADR_AUDIT.md`: ADR-034 (a milestone-scoped
verification sentence), ADR-035 (`ARCHITECTURE.md` never respaced --
corrected), ADR-036 (a count, and a forecast ADR-042 superseded), ADR-037
(`RestraintResolution::constrained` since ADR-041), ADR-038 (three
statements overtaken by ADR-039: Eigen linked, no zero-copy `Map`,
`Triplet` present), ADR-042 (the entry's signature), ADR-043 (a duplicated
one-line wrapper over the single kernel conversion), ADR-047 (two commands
missing from the list). **No invariant was touched by any of them**, which
is why none is a gate failure; eight ADRs carry a dated amendment and the
code was not changed.

### T1 — this milestone's own one-sequence gate was wrong in its first draft (TEST DEFECT, fixed before the freeze)

The first draft solved every step through `requireSolve()`, which
REGENERATES the mesh. The step-6 check then found the step-4 result stale for
`{mesh, analysis}` rather than `{analysis}` and the test FAILED -- correctly,
because the test had remeshed between the two solves and the mesh stamp had
moved. The fix is in the test, not the code: steps 3 to 7 now solve the mesh
ALREADY HELD (`solveHeld`), and the gate additionally asserts that the last
result's mesh stamp equals step 2's, so every stale verdict is provably the
edit's. The production behaviour the first draft tripped over -- a
regenerated mesh is a different mesh, and a result of the old one is stale
for that reason too -- is exactly right. Recorded because a reviewer should
know the gate was tightened by a failure, not written clean.

## Not a finding, recorded so it is not re-discovered

```text
the "ExactlyTwoAreWarnings" test name asserts three        a name; the assertion
                                                           is the one that runs
StructuralSystem.hpp:10-16 repeats ADR-038's overtaken      comments; recorded in
sentences; StructuralValidation.hpp:649-656 explains an     ADR_AUDIT.md, not
origin it then defaults; StructuralReaction.hpp:433-434     edited, because the
says "element by element"; BetterCADDependencies.cmake:50   qualification changed
says Eigen is private to sketch                            no production file
the residual gate is pinned by one synthetic test           MUTATION.md, Q3
NoLoadsApplied is a warning, and u = 0 is published         by design (ADR-042)
```

## Verdict

```text
adversarial review     PASS -- 44 questions, 0 production defects,
                       0 gate-blocking; 3 owner decisions carried unchanged
```
