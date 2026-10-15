# P17-QUAL-001 — formulation audit: Tet4, DOF, loads, restraints, assembly, solver, recovery, reactions, validation

```text
SUBJECT:  whether the numerical core is the formulation ADR-034 and ADR-037..042
          decided, with the failure modes the brief names searched for by
          name and each one traced to the test that would catch it
METHOD:   read the implementation at the line; grep for the forbidden
          construct; name the covering test; where a defect would be silent,
          a mutation probe (MUTATION.md)
```

## Tet4 element (P17-ELEM-001, ADR-034, ADR-040)

```text
shape gradients          constant B from the Jacobian of the four corners;
                         gradients sum to zero on every fixture
                         (Tet4Element_ShapeGradientsSumToZeroOnEveryFixture)
Voigt order              XX YY ZZ XY YZ ZX, frozen in StructuralData.hpp and
                         asserted (Tet4Element_BRowOrderMatchesTheSharedVoigtOrdering,
                         StructuralData_TheTensorComponentOrderIsFrozen)
engineering shear        gamma in the field names; D carries mu (NOT 2 mu) on
                         its shear diagonal (Tet4Element.cpp:116-126,
                         Tet4Element_EngineeringShearGivesTauEqualsMuGamma);
                         tensorOf(Strain6) halves each gamma before an
                         eigen-decomposition (StructuralPost_HalvesEngineeringShearWhenBuildingTheStrainTensor)
                         -- the "2 mu" mutation was KILLED by P17-REFMOD-001 M12
                         on the patch test and the axial bar
D                        lambda / mu from P15's LinearElasticConstants, symmetric
                         (Tet4Element_ElasticityMatrixIsSymmetricAndMatchesTheLameFormulas)
K_e = V B^T D B          symmetric, finite, PSD with exactly six zero modes
                         (Tet4Element_StiffnessIsSymmetricAndFinite,
                         ..._HasSixRigidBodyModesAndNoMore,
                         ..._IsPositiveSemidefiniteAndStoresRealEnergy)
inverted element         det J < 0 is InvertedElement, REFUSED -- not reordered,
                         not abs()'d (Tet4Element.cpp:252;
                         ..._RejectsAnInvertedTetrahedronRatherThanReorderingIt,
                         ..._RejectsAnOddNodePermutation,
                         ..._ReflectedGeometryIsRejectedBecauseOrientationFlips)
zero-volume element      det J == 0 or non-finite is DegenerateElement, refused
                         (..._RejectsDegenerateAndNonFiniteGeometry); no
                         std::abs(det) anywhere in src/structural (grep: 0)
scale laws               K ~ E, K ~ L (..._FollowsTheMaterialScaleLaw,
                         ..._FollowsTheGeometryScaleLaw); unit-equivalent inputs
                         agree (..._GivesTheSameStiffnessForEquivalentUnitInputs)
independent reference    ..._MatchesTheIndependentReferenceImplementation,
                         ..._StiffnessMatchesTheAnalyticalUnitTetrahedron,
                         ..._ReproducesEveryAffineStrainStateExactly
whole reference meshes   ..._EvaluatesEveryElementOfTheMeshingReferenceModels,
                         ..._SumsToTheMeshVolumeOverAWholeReferenceMesh,
                         ..._VolumeAgreesWithTheMeshingSignedVolume
```

## DOF model (P17-DOF-001, ADR-037)

```text
ordinal                  the mesh's own enumeration index, copied not sorted;
                         dense even for sparse handles
                         (MeshDofMap_OrdinalsFollowTheMeshOwnNodeEnumeration,
                         ..._NumbersSparseNodeHandlesDenselyRatherThanByHandleValue)
numbering                3k + c + 1, interleaved per node; index and dof exact
                         inverses (..._IndexAndDofAreExactInversesForEveryDegreeOfFreedom)
types                    DofIndex (1-based identity, solver-local) and
                         FreeEquationIndex (0-based row, no invalid value,
                         deleted default ctor) are distinct; compile-fail
                         DOF_INDEX_AS_FREE_EQUATION, FREE_EQUATION_AS_DOF_INDEX,
                         FREE_EQUATION_DEFAULT_CONSTRUCTED
constraint set           sorted, unique, validated; absence is std::optional,
                         never row zero (FreeEquationMap_GivesAConstrainedDofNoEquationRatherThanRowZero)
remesh                   stamp PLUS node count refuse a numbering built for
                         another mesh (MeshDofMap_ARemeshInvalidatesTheNumberingAndTheConstraintsBuiltOnIt)
determinism              identical over repeated builds and across a remesh of
                         the same model (StructuralDof_IsDeterministicAcrossARemeshOfTheSameReferenceModel)
CAD identity             DofIndex outside core/Id.hpp (grep: 0); not a document
                         identity (StructuralData_ADofIndexIsSolverLocalAndNotADocumentIdentity)
```

## Loads (P17-LOAD-001)

```text
targets                  canonical FaceName only; a face load carries no facet,
                         node or force (StructuralLoad_CanonicalFaceLoadsCarryNoMeshHandle)
pressure                 acts INWARD along each facet's outward normal
                         (StructuralLoad_PositivePressureActsInwardAlongTheOutwardNormal);
                         facet area vector is half the edge cross product; the
                         winding is outward of the owning tetrahedron
                         (StructuralLoad_BoundaryFacetWindingIsOutwardOfTheOwningTetrahedron)
                         -- the "outward" mutation KILLED by REFMOD M13
consistent nodal load    A t / 3 per corner (StructuralLoad_ConstantTractionGivesEachCornerOneThirdOfTheTotal)
                         -- the "A t / 2" mutation KILLED by REFMOD M11
resultant                force AND moment preserved (StructuralLoad_TractionPreservesTheResultantForceAndMoment);
                         converges to the analytic load under refinement
                         (StructuralLoad_ConvergesToTheAnalyticLoadAsTheSurfaceIsRefined);
                         RM-FEA-04: reactions 5400.000 N against p A = 5400 N
gravity                  rho V g through the mode, density REQUIRED by the mode
                         (StructuralLoad_GravityWeighsTheMeshedBodyAndNeedsADensity,
                         StructuralMaterial_DensityIsRequiredOnlyByTheGravityMode);
                         RM-FEA-05: 10.7775 N at the centre of volume
unresolved target        REFUSED, not treated as zero, nothing nearby substituted
                         (StructuralLoad_RefusesAnUnresolvedTargetRatherThanTreatingItAsZero;
                         RM-FEA-10; grep nearest/closest/rebind: comments only)
remesh                   loads survive a remesh with no facet identity
                         (StructuralLoad_SurvivesARemeshWithoutAnyFacetIdentity)
rigid transform          pressure rotates with the body, a global traction does
                         not (StructuralLoad_PressureRotatesWithTheBodyAndAGlobalTractionDoesNot)
superposition, order     StructuralLoad_SuperposesAndIsIndependentOfListOrder;
                         two loads on one DOF add (StructuralSystem_AddsTwoLoadsOnOneDegreeOfFreedom)
```

## Restraints (P17-BC-001, ADR-041)

```text
targets                  FaceName + RestraintComponents; no node target
                         (compile-fail A_RESTRAINT_DOES_NOT_TARGET_A_NODE)
resolution               every mapped node and nothing else, through P16's
                         mapping and P17-DOF's numbering, never by arithmetic
                         (StructuralBC_ConstrainsEveryMappedNodeAndNothingElse,
                         ..._NumbersThroughP17DofAndNeverByArithmetic)
unresolved               refused with NO nearest-face fallback
                         (StructuralBC_RefusesAnUnresolvedTargetWithNoNearestFaceFallback,
                         StructuralBC_RefusesTheDrilledHoleWallWithNoGeometricFallback)
overlap                  unioned, counted once globally
                         (StructuralBC_UnionsOverlappingRestraintsWithoutRefusingThem,
                         StructuralReaction_CountsASharedDegreeOfFreedomOnceGlobally)
sufficiency              NOT decided here (StructuralBC_DoesNotDecideWhetherTheModelIsSufficientlyConstrained);
                         < 6 DOFs refused by validation, >= 6 judged by the
                         pivot gate (StructuralValidation_DoesNotClaimSixDegreesOfFreedomIsSufficient)
remesh                   re-resolved after a remesh (StructuralBC_ReResolvesTheNodeSetAfterARemesh,
                         ..._FollowsTheNodeCountOfARefinedCurvedFace)
```

## Assembly (P17-ASSEMBLY-001, ADR-038)

```text
storage                  BetterCAD-owned CSR: rowStart, innerIndices, values;
                         sparse, never a dense square (StructuralSystem_IsSparseAndNeverADenseSquareOfTheDofCount)
two passes               symbolic pattern, then numeric accumulate with +=
                         at StructuralSystem.cpp:341 -- "+= -> =" probe Q1,
                         MUTATION.md
accumulation             StructuralSystem_AccumulatesANamedPairFromTwoNamedElements,
                         ..._SumsEveryContributionAndLosesNone,
                         ..._MatchesAnIndependentDenseScatterOfEveryElement
symmetry                 exactly as symmetric as its elements; no 0.5 (K + K^T)
                         (StructuralSystem_IsExactlyAsSymmetricAsItsElementsAre)
rigid-body modes         six preserved on a free body; PSD with nullity six;
                         no regularisation (grep 1e20 / penalty: a comment)
                         (StructuralSystem_PreservesTheSixRigidBodyModesOfAFreeBody,
                         ..._IsPositiveSemiDefiniteWithNullitySix,
                         ..._PreservesTheRigidBodyModesOfAReferenceBody)
energy                   sum of element energies (StructuralSystem_EnergyEqualsTheSumOfElementEnergies)
order                    P16's ascending ElementId; deterministic
                         (StructuralSystem_IsDeterministicAndFollowsP16ElementOrder)
finiteness               StructuralSystem_HoldsOnlyFiniteEntries
provenance               refuses loads prepared for another mesh
                         (StructuralSystem_RefusesLoadsPreparedForADifferentMesh)
```

## Solver (P17-SOLVE-001, ADR-039)

```text
library                  Eigen SimplicialLDLT, PRIVATE to the module; no public
                         header includes Eigen (CheckLayering rule 7)
gate 1  pivots           info() == Success AND every D(k,k) > 0 AND
                         min|D|/max|D| >= pivotFloor (1e-12) at StructuralSolve.cpp:345
                         -- probe Q2 removes it; StructuralSolve_RefusesAFreeBodyAlthoughTheLibrarySucceeds,
                         ..._RefusesAnExactlySingularSmallSystem,
                         ..._RefusesANearSingularSystemTheResidualWouldAccept,
                         ..._RefusesAnIndefiniteSystem, RM-FEA-08
gate 2  finiteness       every component of u_f finite (..._RejectsASolutionThatOverflowedToInfinity)
gate 3  residual         INDEPENDENT r = K_ff u_f - F_f over BetterCAD's CSR,
                         normalized, against 1e-9 at StructuralSolve.cpp:450;
                         solver.error() never consulted -- probe Q3 removes it;
                         StructuralSolve_ChecksTheFreeResidualAndKeepsTheReactions
no penalty, no pinning   exact reduction (StructuralSolve_ExtractsTheFreeSystemByExactReduction);
                         constrained displacements exactly zero
                         (..._LeavesConstrainedDisplacementsExactlyZero)
closed forms             2x2, 3x3, independent dense elimination of a reference
                         model's free system (StructuralSolve_MatchesAnIndependentDenseEliminationOfTheFreeSystem)
linearity, determinism   ..._IsLinearInTheLoadAndInTheStiffness,
                         ..._IsDeterministicAcrossRepeatedSolves
under-constrained        RM-FEA-08: eight DOFs all in z -> refused, smallest
                         pivot 1.1e-17 of the largest, nothing published
```

## Displacement, strain and stress recovery (P17-POST-001, ADR-040)

```text
displacements            gathered in the local DOF order from the solution,
                         magnitude a Euclidean norm, one per node exactly once
strain                   B u_e through the SAME B (StructuralPost_RecoversAffineStrainFromASingleTetrahedron,
                         ..._RecoversZeroStrainFromARigidMotion,
                         ..._MatchesAnIndependentlyFittedStrainOnEveryElement)
stress                   D epsilon through the same D; uniaxial through lambda
                         and mu, pure shear through mu not 2 mu
principal values         SelfAdjointEigenSolver on the TENSOR (halved shear),
                         sorted descending; invariants under rotation
                         (StructuralPost_KeepsTheInvariantsUnderARotationOfTheStressTensor,
                         ..._KeepsTheInvariantsWhenTheWholeModelIsRotated)
von Mises                component formula; equals |sigma| uniaxial, sqrt(3) tau
                         in pure shear, zero hydrostatic, invariant to a
                         hydrostatic shift; the suite's oracle is the
                         DEVIATORIC form, so the two are independent
hydrostatic              tr(S)/3 in the normal-stress convention
provenance               refuses another mesh, another system, another material
                         (StructuralPost_RefusesAResultBoundToADifferentMesh,
                         ..._RefusesASolutionFromADifferentSystem,
                         ..._RefusesAMaterialThatIsNotTheOneSolvedWith)
storage                  O(nodes + elements), nothing quadratic
```

## Reactions and equilibrium (P17-REACTION-001, ADR-041)

```text
reaction                 the constrained entries of the FULL residual K u - F
                         (StructuralReaction.cpp:352) -- no second K u; probe
                         Q7 flips the sign; StructuralReaction_MatchesThePartitionFormulaOnASmallSystem,
                         ..._FullyConstrainedReactionIsMinusTheAppliedLoad
components               only the constrained component of a partial support
                         (..._ReportsOnlyTheRestrainedComponentOfAPartialSupport)
sharing                  a shared DOF counted once; per-restraint attribution
                         additive when disjoint
equilibrium              force AND moment, about an explicit origin, normalised
                         by the magnitudes that participate; 1e-12 defaults
                         measured; refused above the threshold
                         (..._RefusesAnImbalanceLargerThanTheThreshold,
                         ..._BalancesAPureCoupleInMomentNotOnlyInForce,
                         ..._BalancesTheMomentAboutANonZeroOriginToo,
                         ..._KeepsEquilibriumWithGeometryFarFromTheOrigin)
analytic                 pressure resultant and rho V g balanced
                         (StructuralReaction_BalancesAPlanarPressureAgainstItsAnalyticalResultant,
                         ..._BalancesSelfWeightAgainstRhoVG); RM-FEA-04 and 05
F1                       the 1e-12 moment threshold IS crossed by a correct
                         bending solve of ~2500 needle elements (eta_M 1.15e-12,
                         P17-REFMOD-001); the committed cantilever passes at
                         6.0e-13. Recorded for the owner; no threshold changed
```

## Validation policy (P17-VALID-001, ADR-042)

```text
hard failures            radius ratio < 1e-10, aspect > 3e5 -- one bound per
                         degeneration family, derived from measured accuracy
                         laws (StructuralValidation_RejectsOnlyBelowTheMeasuredAccuracyFloor,
                         ..._TheBoundsGuaranteeTheStatedAccuracy,
                         ..._MeasuresHowKernelAccuracyDegradesWithElementShape)
no dihedral failure      a sliver is ACCEPTED because the kernel handles it
                         (..._AcceptsASliverBecauseTheKernelHandlesIt)
warnings                 the measured qualified envelope, rejects nothing
                         (..._TheEnvelopeBoundsReallyAreTheMeasuredWorstCase);
                         RM-FEA-03 solves accepted_with_warnings
sufficiency              < 6 constrained DOFs is a hard failure; six is not a
                         claim of sufficiency
one entry                solveStructuralAnalysis runs every gate in order and
                         publishes nothing when one refuses
                         (..._PublishesNothingWhenAGateRefuses,
                         StructuralCli_ThereIsNoFlagThatBypassesValidation)
staged                   stops at the first refusing stage; status DERIVED
                         from findings (..._StatusIsDerivedSoItCannotDisagreeWithTheFindings)
one definition           metrics read from P16's summaries, never recomputed
                         (..._AgreesWithP16sOwnPerElementClassification); the
                         same thresholds reach GUI and CLI because both call
                         the one entry (structuralQualityThresholds has one
                         caller: the entry)
```

## Verdict

```text
DOF model PASS           Tet4 formulation PASS      stiffness symmetry PASS
rigid-body modes PASS    loads PASS                 restraints PASS
geometry mapping PASS    assembly PASS              linear solver PASS
residual validation PASS displacement PASS          strain PASS
stress PASS              von Mises PASS             reaction recovery PASS
static equilibrium PASS  acceptance policy explicit (ADR-042)
```
