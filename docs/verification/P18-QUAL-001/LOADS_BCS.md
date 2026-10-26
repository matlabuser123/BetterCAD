# P18-QUAL-001 - loads and boundary conditions audit (brief sections 28-31)

**Verdict: signs, factors and resultants match the formulation; targets resolve by geometry reference only; unresolved is a refusal.**
Row-by-row: `audit/AUDIT_B_element_loads_bcs_assembly_solvers_tolerances.md` sections B6 (10 rows) and B7 (8 rows).

## Loads

| property | tests |
|---|---|
| positive surface flux q'' is heat **into** the body | `ThermalLoad_SignConventionIsPositiveIntoTheBody`, `ThermalReference_RM02_SurfaceHeatFluxIntoAHeldSlab`, `ThermalRefmodCli_SurfaceFluxAndTheHeldCapTransientAgree`; mutation QP07 (sign flipped) killed by 26 tests |
| triangle share `Qe_i = q'' A / 3`; tetrahedron share `Qe_i = q''' V / 4` | `ThermalLoad_SingleTriangleHeatFluxAnalytical`, `_GeneralTriangleHeatFluxAnalytical`, `_UnitTetGenerationAnalytical`, `_GeneralTetGenerationAnalytical`, `_FacetWindingDoesNotChangeTheHeatInput` |
| signed values are valid; no abs, no clamp | `ThermalLoad_ZeroAndNegativeValuesArePreservedExactly` (negative equals minus positive bit for bit), `_SignedCancellation` |
| resultants are preserved: sum of nodal shares is `q'' A` or `q''' V` | `ThermalLoad_SurfaceHeatRateConserved`, `_VolumeHeatRateConserved`, `_MultiTetBodyConservesTheHeatRate`, `_TriangulationChangesTheDistributionAndNotTheTotal`, `ThermalReference_RM04_GenerationResultantAndConservationOnTheSlab` |
| a target is a `FaceName` resolved through the P16 `GeometryMeshMap`; no facet, node, element or unknown in the canonical load | `ThermalLoad_CanonicalGeometryReferenceIsAFaceNameAndNothingMeshLocal`, compile-fail `the-surface-load-has-no-node`, `-no-facet`, `-no-unknown`, `the-volumetric-load-has-no-element` |
| an unresolved reference fails and binds nothing; there is no nearest-face fallback | `ThermalLoad_UnresolvedReferenceFailsAndBindsNothing`, `ThermalLoad_NoNearestFaceRebind` (three absent roles refused; P16 asserted to return empty; the real face untouched); mutation QP14 (unresolved not reported) |
| a target that resolved to zero facets is a refusal, not a zero load | `ThermalLoad_EmptyTargetIsNotAZeroLoad`; mutation QP15 (accepted) |
| a remesh re-resolves; the old preparation is stale | `ThermalLoad_RemeshReResolvesGeometryReference`, `_SameCountsNewMeshIsStale`, `_SameIdsNewMeshIsStale`; mutation QP25 (mesh identity not compared) |

## Boundary conditions

| property | tests |
|---|---|
| prescribed temperature is an absolute kelvin value on a `FaceName`; stored as K, never as a penalty | `ThermalBC_PrescribedTemperatureStoresNonzeroKelvin`, `_BelowZeroKelvinPrescribedTemperatureRejected`, `_PreparedBoundaryHasNoPenaltyAssemblyOrSolve` |
| unique nodes, sorted by unknown, deterministic | `ThermalBC_PrescribedTemperatureDeduplicatesNodes`, `_PrescribedTemperatureDeterministicOrder`, `_MappedFaceDirichletMapsUniqueNodesOnce` |
| conflicting duplicates fail by exact canonical equality; identical overlap deduplicates | `ThermalBC_ConflictingTemperatureRejected`, `_ConflictUsesExactCanonicalEquality`, `_ConflictReportIsSummarisedAndOrderIndependent`, `_IdenticalOverlapAlongAnEdgeDeduplicates` |
| convection (Robin): `Kconv = hA/12 [2 1 1; 1 2 1; 1 1 2]`, `Qconv = h Tinf A / 3` | `ThermalBC_SingleTriangleConvectionMatrixAnalytical`, `_ConvectionSourceAnalytical`, `_ConvectionIsNotLumped`, `_ConvectionRejectsCentroidMatrixMutation`; mutation QP19 (`hA/6`) and QP05 (the Robin source omitted, killed by 120 tests) |
| `Kconv (Tinf 1) - Qconv = 0` | `ThermalBC_ConvectionAmbientEquilibrium` (exactly 0) |
| the Robin term has no conductivity factor | `ThermalBC_ConvectionDoesNotInvolveConductivity`, `_WrongSignsAndFactorsAreDistinguished` (a x45 conductivity mutant is rejected), compile-fail `the-convection-takes-no-conductivity` |
| sign: a boundary hotter than ambient loses heat | `ThermalBC_ConvectionAboveAmbientHeatLoss`, `_ConvectionBelowAmbientHeatGain`, `ThermalReference_RM05_HeatGainSubcase`, `ThermalTransient_BlockCoolingUnderConvectionFollowsTheLumpedExponential` |

## Closed or recorded by this milestone

* **B6.10** the `TargetProblem::WithoutFacets` branch of the chain (a face that resolves but yields zero facets) is reached only at the
  integrate seams; P16 never hands an empty set, so the three production branches (`ThermalTarget.cpp:52-59`, `ThermalLoadVector.cpp:528-537`,
  `ThermalBoundary.cpp:603-610`) are **defensive**. Recorded in `LIMITATIONS.md`; not closed with a test because no input can reach them.
* **B6.6** `NamedBoundarySet` is not a thermal target (`FaceName` only). The brief's wording lists both; the implemented and tested scope is
  `FaceName`. Recorded as a scope decision for the owner (`FINDINGS.md`, D-B2).
