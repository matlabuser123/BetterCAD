# P17-QUAL-001 — mutation protection

```text
SUBJECT:  for each critical P17 invariant the brief's adversarial list names,
          the evidence that a test would CATCH its violation -- not that the
          invariant is documented
METHOD:   seven controlled mutations applied to the PRODUCTION tree at this
          gate (one textual edit each, built in debug-ext, the named filter
          run, the file restored, git status checked), plus the production
          probes the owning milestones already ran and this audit re-reads
RESULT:   7 probes, 7 KILLED, 0 survived, 0 equivalent; 22m51s
          18:17:31 -> 18:40:22, one after another, restored after each
```

## The seven run at this gate

| # | File:line | Mutation | Filter | Verdict | What killed it |
| --- | --- | --- | --- | --- | --- |
| Q1 | `src/structural/StructuralSystem.cpp:341` | duplicate sparse entries OVERWRITE instead of accumulate: `values[slot] +=` becomes `=` | `[rm01],[rm02],[assembly]` | **KILLED**, 11 cases / 67 assertions | `StructuralSystem_AccumulatesANamedPairFromTwoNamedElements`, `..._SumsEveryContributionAndLosesNone`, `..._MatchesAnIndependentDenseScatterOfEveryElement`, `..._InternalForceEqualsTheElementScatter`, `..._EnergyEqualsTheSumOfElementEnergies`, `..._IsPositiveSemiDefiniteWithNullitySix`, `..._PreservesTheSixRigidBodyModesOfAFreeBody`, `..._PreservesTheRigidBodyModesOfAReferenceBody`, `..._AssemblesALargeMeshInSparseStorage`; and the closed forms: `StructuralPatch_ReproducesConstantStrainOverEveryElementOfTheMeshedBlock`, `StructuralAxialBar_MatchesTheClosedFormToRoundoff` |
| Q2 | `src/structural/StructuralSolve.cpp:345` | the pivot gate removed: `if (!positive \|\| pivotRatio < pivotFloor)` becomes `if (false)` | `[rm08],[solve]` | **KILLED**, 9 / 10 | `StructuralUnderConstrained_IsRefusedByThePivotGateAndNothingIsPublished` (RM-FEA-08), `StructuralSolve_RefusesAFreeBodyAlthoughTheLibrarySucceeds`, `..._RefusesAnExactlySingularSmallSystem`, `..._RefusesANearSingularSystemTheResidualWouldAccept`, `..._RefusesAnIndefiniteSystem`, `..._RefusesAnInsufficientlyRestrainedModel`, `..._RejectsASolutionThatOverflowedToInfinity`, `..._SolvesAnIllConditionedSystemAndReportsHonestly`, `..._ReportsEveryProblemThroughTheSameOrderedChecks` |
| Q3 | `src/structural/StructuralSolve.cpp:450` | the independent residual gate removed: `if (!(normalized <= tolerance))` becomes `if (false)` | `[solve]` | **KILLED**, 1 / 1 | `StructuralSolve_ReportsEveryProblemThroughTheSameOrderedChecks` -- see below |
| Q4 | `src/structural/Tet4Element.cpp:252` | an inverted tetrahedron ADMITTED: `if (det < 0.0)` becomes `if (false)` | `[elem],[rm01]` | **KILLED**, 3 / 6 | `Tet4Element_RejectsAnInvertedTetrahedronRatherThanReorderingIt`, `..._RejectsAnOddNodePermutation`, `..._ReflectedGeometryIsRejectedBecauseOrientationFlips` |
| Q5 | `src/structural/StructuralAnalysisObject.cpp:145` | a material edit no longer stales a result: `reasons.push_back(StaleReason::Material)` removed | `[p17qual],[currentness]` | **KILLED**, 3 / 5 | `StructuralData_AMaterialEditStalesTheResultAndNotTheMesh`, `StructuralData_EveryDependencyAloneCanStaleAResult`, and this gate's own `P17Qualification_EveryDependencyEditStalesTheResultInOneSequence` |
| Q6 | `apps/bettercad_cli/StructuralReports.cpp:243` | the CLI returns exit 0 on a failed solve: `ExitCode::Failure` becomes `ExitCode::Success` after `solve_failed` | ctest `refmod\.structural\.refused\.under-constrained\|cli\.structural\.` | **KILLED**, 4 of 14 process tests | `refmod.structural.refused.under-constrained.structural-solve`, `.structural-displacement`, `.structural-stress`, `.structural-reactions` -- the committed RM-FEA-08 document through all four result commands in fresh processes |
| Q7 | `src/structural/StructuralReaction.cpp:352` | the reaction sign flipped: `value = residual[row]` becomes `-residual[row]` | `[rm02],[rm04],[reaction]` | **KILLED**, 21 of 28 cases / 21 assertions | nineteen `StructuralReaction_*` cases -- `..._FullyConstrainedReactionIsMinusTheAppliedLoad`, `..._MeasuresTheEquilibriumErrorItIsGatedOn`, `..._RefusesAnImbalanceLargerThanTheThreshold`, `..._BalancesAPlanarPressureAgainstItsAnalyticalResultant`, `..._BalancesSelfWeightAgainstRhoVG`, `..._BalancesAPureCoupleInMomentNotOnlyInForce`, `..._BalancesTheCantileverMomentFromDistributedReactions`, `..._BalancesTheMomentAboutANonZeroOriginToo`, `..._KeepsEquilibriumWithGeometryFarFromTheOrigin`, `..._TransfersTheMomentCorrectlyToAnotherOrigin`, `..._CountsASharedDegreeOfFreedomOnceGlobally`, `..._AttributesReactionsPerRestraintAdditivelyWhenDisjoint`, `..._RecoversEachComponentAndOnlyTheConstrainedOnes`, `..._ReportsOnlyTheRestrainedComponentOfAPartialSupport`, `..._IsLinearInTheAppliedLoad`, `..._IsDeterministicAcrossRepeatedRecoveries`, `..._RecoversALargeMeshAndStillBalances`, `..._RefusesEverySourceMismatch`, `..._StalesOnARemeshAndRefusesAStaleLookup` -- and the two closed forms `StructuralAxialBar_MatchesTheClosedFormToRoundoff`, `StructuralPressureBlock_PreservesTheResultantAndBalancesTheReactions` |

Logs: the session's scratch directory holds `mutation-Qn.log`, `-build.log`
and `-tests.log` for each; the verdict lines above were transcribed from
them, and the kill lists were extracted from the Catch2 output by test-case
name.

## Two things the table says that are worth saying out loud

**Q3 is a one-test kill, and that is recorded rather than dressed up.** The
independent residual gate (`r = K_ff u_f - F_f` over BetterCAD's own CSR,
against `relativeResidualTolerance`) is removed, and exactly one case fails:
the ordered-checks case, which poses a system whose residual the gate must
refuse. No physics case can reach it, and that is the reason it exists: an
LDLT of a well-conditioned SPD system is backward stable and its normalized
residual sits at ~1e-17, seven orders inside the gate. The gate is a defence
against the LIBRARY reporting success with a wrong answer, which
`StructuralSolve_RefusesANearSingularSystemTheResidualWouldAccept` already
shows is the pivot gate's job on a near-singular system -- so the residual
gate's own kill is necessarily synthetic. One test is enough to prove the
gate is there; it is not evidence the gate has ever fired in anger, and
nothing here claims it has.

**Q6 was killed by the committed reference document, not by a unit test.**
The in-process `StructuralCli_*` cases run the command handler and check
its `ExitCode`; a mutation that returns `Success` from the solve helper is
visible to them too, but the four kills recorded are the FRESH-PROCESS ones:
`bettercad-cli` on `structural_under_constrained.bcad`, whose exit status
ctest's `WILL_FAIL`/regex contract requires to be 1 with `solve_failed:
.*singular` on stderr. That is the "can the CLI return zero on a singular
solve" question answered at the process boundary, where it matters.

## The probes the owning milestones ran, re-read at this gate

The brief's list asks about defects a single mutation at this gate cannot
reach cheaply (a header change rebuilds the tree) or that were already run
on the production formula. Each is traced to the milestone that ran it:

```text
QUESTION                                          PROBE                        VERDICT
engineering and tensor shear mixed                P17-REFMOD-001 M12: D carries 2 mu  KILLED, 5
                                                  on its shear diagonal
                                                  P17-POST-001: tensorOf(Strain6)
                                                  stops halving gamma          KILLED
B or D ordering disagrees with post-processing    P17-ELEM-001: a B row swapped;
                                                  P17-POST-001: Voigt order    KILLED
a zero-volume Tet gives finite-looking stiffness  Q4 above, and P17-ELEM-001's
                                                  degenerate fixture: det == 0
                                                  is DegenerateElement before
                                                  any 1/det                    KILLED
pressure conversion loses the resultant           P17-REFMOD-001 M11 (A t / 2) KILLED, 7
                                                  and M13 (outward pressure)   KILLED, 3
a load silently moves after regeneration          P17-LOAD-001 / P17-BC-001:
                                                  no nearest-face code exists; the
                                                  unresolved refusals are tested
                                                  and RM-FEA-10 commits one   n/a (absence)
displacement scaling mutates result data          P17-VIZ-001: 83 + 30 probes,
                                                  RenderingChangesNothingItReads
                                                  and the non-cumulative scale KILLED
the GUI recalculates stresses                     no formula exists to mutate; the
                                                  viewer's 69 cases read members
global K loses symmetry                           Q1 above (an overwrite breaks the
                                                  scatter) and P17-ASSEMBLY-001's
                                                  own symmetrisation probe     KILLED
```

## The restore

After the seven probes `git status --short` listed exactly the files this
milestone intends to change -- `tests/CMakeLists.txt`,
`tests/cli/ZeroMatchGuard.cmake`, the new test file, the eight ADR
amendments, the two P17-VIZ-001 addenda, this evidence directory -- and
**no file under `src/`, `include/`, `apps/` or `examples/`**. The debug-ext
tree was then rebuilt from the restored sources and the whole suite run
before the freeze (`qualification/prefreeze.txt`); the three-preset
qualification cleans every preset and rebuilds from scratch, so no probe
binary can be the one that runs there.
