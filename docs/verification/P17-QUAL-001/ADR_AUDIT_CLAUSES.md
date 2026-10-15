# P17-QUAL-001 — ADR audit, clause by clause

Scope: ADR-034 through ADR-048 in `docs/architecture/decisions/`, checked against the
source tree at `C:\Users\hasib\OneDrive\Desktop\CAD` (read-only). Date: 2026-10-15.

Method and caveats, stated once:

- Every `file:line` below was read in this audit. Line numbers are from the files as
  they stand today.
- Test names were taken from `grep TEST_CASE(` over `tests/`. Test *bodies* were read
  only where the table says so (ADR-048's save-refusal test, ADR-042's warning-count
  test). Elsewhere a test is cited by name as the suite's pin for the clause, not as
  something whose assertions I re-verified.
- No VCS is available in the OneDrive tree, so claims of the form "no meshing source
  file changed" or "X was requalified" are recorded as not checkable here rather than
  as IMPLEMENTED.
- Statuses: IMPLEMENTED (code matches, with a location), DRIFT (ADR text disagrees
  with the code, even in a detail), NOT FOUND (searched, not present). A clause the
  ADR itself marks superseded is reported against the current ADR text.

---

## ADR-034 — The first structural solver is linear-static Tet4 on one solid

Decision: exactly one supported analysis (linear static, small strain, homogeneous
isotropic linear elasticity, Tet4, three translations per node, one solid, one current
P16 mesh); everything else is refused, never approximated.

| Clause | Code location (file:line) | Test(s) that pin it | Status | Note |
|---|---|---|---|---|
| Analysis is linear static; no dynamics/modal | `include/bettercad/structural/StructuralData.hpp:66-75` (`StructuralAnalysisMode` has only `LinearStatic`, `LinearStaticWithGravity`); `StructuralData.hpp:298-302` one `SolverAlgorithm` | `StructuralSolve_NamesTheSelectedSolverAndValidatesItsSettings` (StructuralSolveTests.cpp:359) | IMPLEMENTED | Gravity mode is still linear static (self-weight body force). |
| DOF = ux, uy, uz per node, no rotations | `StructuralData.hpp:90-98` (`DofComponent` three values, `kDofComponents`); `StructuralDof.hpp:66` `kDofsPerNode = kDofComponents.size()` | `MeshDofMap_GivesEveryNodeExactlyThreeTranslationalDofs` (StructuralDofTests.cpp:169) | IMPLEMENTED | |
| Element is Tet4 only | `src/structural/StructuralSystem.cpp:223,259,293` iterate `mesh.tetrahedra()` only; `Tet4Element.hpp:110,116` `kTet4Nodes = 4`, `kTet4Dofs = 12`; grep `Tet10|Hex8|Tetrahedron10|Hexahedron` in `src/structural` → no matches | `Tet4Element_LocalDofOrderingMatchesTheGlobalNumbering` (Tet4ElementTests.cpp:739); `StructuralSystem_MapsLocalTet4DofsOntoTheGlobalNumbering` (StructuralSystemTests.cpp:596) | IMPLEMENTED | |
| Material: homogeneous isotropic linear elasticity, `sigma = D eps`, D constant | `Tet4Element.hpp:334-335` `isotropicElasticity(const LinearElasticConstants&)`; `src/structural/Tet4Element.cpp:362-363` lambda/mu; `StructuralSystem.cpp:246` one D per assembly | `Tet4Element_ElasticityMatrixIsSymmetricAndMatchesTheLameFormulas` (Tet4ElementTests.cpp:496) | IMPLEMENTED | grep `orthotropic|plastic|contact` in `src/structural` → no matches. |
| Kinematics: infinitesimal strain, K independent of u, undeformed geometry | `Tet4Element.hpp:246-247` `strainFrom` = `B u_e` with constant B; `StructuralSystem.cpp:302` kinematics from `rows->corners` (mesh positions, never updated) | `Tet4Element_ReproducesEveryAffineStrainStateExactly` (:627); `Tet4Element_BGivesZeroStrainForEveryRigidBodyMode` (:675) | IMPLEMENTED | |
| One current regenerated solid, one current validated P16 mesh (limit inherited from P16) | `StructuralAnalysis.hpp:246-248` one `MeshControlId`; `src/structural/StructuralAnalysis.cpp:59-66` delegates to `meshing::geometryIneligibility`; `:72-85` `Mesher::currency` | `StructuralInput_RefusesAMeshOfGeometryThatHasSinceChanged` (StructuralAnalysisBoundaryTests.cpp:182) | IMPLEMENTED | The single-solid refusal itself is P16's, as the ADR says. |
| Unsupported cases refused rather than approximated | No type or branch for Tet10/Hex8/shells/contact/plasticity exists (greps above); the API cannot express such a request | none applicable | IMPLEMENTED | By absence. The ADR's "explicit diagnostic" has no surface to fire on because nothing can be asked for; this is stronger than a refusal, not weaker. |
| Verification: "no element, no DOF, no stiffness and no solve exists in src/structural/" | `src/structural/CMakeLists.txt:47-62` lists `Tet4Element.cpp`, `StructuralDof.cpp`, `StructuralSystem.cpp`, `StructuralSolve.cpp` | — | DRIFT | Milestone-scoped statement that is now false; the ADR was never amended to say "as of P17-ARCH-001". See findings. |

---

## ADR-035 — A `structural` module at layer 50; the layer table respaced in tens

Decision: the layer table becomes `core 0, sketch 10, features 20, assembly 30, drawing 40, meshing 40, structural 50, io 60, renderer 70, scripting 70`; rule 6 (library never includes `apps/`) added.

| Clause | Code location (file:line) | Test(s) that pin it | Status | Note |
|---|---|---|---|---|
| Table respaced in tens, exactly as listed | `tests/architecture/CheckLayering.cmake:46-77` (`set(layer_core 0)` … `set(layer_scripting 70)`) | `architecture.layering`, `architecture.checker.valid` (tests/architecture/CMakeLists.txt:4,9) | IMPLEMENTED | |
| `structural` = 50, `io` = 60, `renderer`/`scripting` = 70 | `CheckLayering.cmake:74-77` | as above | IMPLEMENTED | |
| Rule 3 is strictly lower | `CheckLayering.cmake:175-176` `NOT layer_${target_module} LESS layer_${file_module}` | `architecture.checker.meshing-to-structural` (CMakeLists.txt:48-50) | IMPLEMENTED | |
| Checker comment says the value is an ordinal with gaps; take a free integer | `CheckLayering.cmake:33-45` | — | IMPLEMENTED | |
| Rule 6: `include/`, `src/` must not include anything under `apps/` | `CheckLayering.cmake:199-216` | `architecture.checker.library-reaches-apps` (CMakeLists.txt:54-56); fixture `tests/architecture/fixtures/library-reaches-apps/src/structural/Leak.cpp` | IMPLEMENTED | |
| Fixtures `structural-layer-violation`, `meshing-to-structural`, `structural-backend-leak` each hold one violation and assert its message | `tests/architecture/CMakeLists.txt:45-53`; fixture dirs exist under `tests/architecture/fixtures/` | the three named `architecture.checker.*` tests | IMPLEMENTED | |
| Deliberately no structural-specific Qt fixture | `tests/architecture/CMakeLists.txt:42-44`; no such fixture directory | `architecture.checker.qt-leak` (:18-20) | IMPLEMENTED | |
| Module `CMakeLists.txt` comment quotes its layer and reason | `src/structural/CMakeLists.txt:1-15` ("layer 50", "cannot SHARE meshing's layer … strictly-lower … ADR-035") | — | IMPLEMENTED | |
| "The numbers appear in … the checker — plus prose in `ARCHITECTURE.md`" (respace reflected there) | `docs/ARCHITECTURE.md:35-37` still reads "Layers: `core` 0, `sketch` 1, `features` 2, `assembly` 3, `drawing` 4, `io` 5, `renderer`/`scripting` 6"; targets table `:48-51` says "(layer 1)…(layer 4)"; no `meshing`/`structural` row | — | DRIFT | Documentation never respaced. See findings. |

Not checkable here: "447 files, 0 violations" (requires running the checker).

---

## ADR-036 — A structural analysis input is validated once, and possession is the proof

Decision: `requireStructuralModel(document, regenerator, mesher, control)` is the single boundary; `StructuralModel` has no public constructor and one friend; checks run geometry-first and delegate to P16/P15.

| Clause | Code location (file:line) | Test(s) that pin it | Status | Note |
|---|---|---|---|---|
| Signature as stated | `include/bettercad/structural/StructuralAnalysis.hpp:246-248` | `StructuralInput_AcceptsACurrentMeshWithAUsableMaterial` (StructuralAnalysisBoundaryTests.cpp:126) | IMPLEMENTED | |
| No public constructor, exactly one friend | `StructuralAnalysis.hpp:204-220` (private ctor; `friend … requireStructuralModel`) | as above | IMPLEMENTED | |
| Possession proves: control exists; body eligible (incl. not behind a configuration override); mesh held and Current; map + quality from one lookup; material resolves with complete constants | `src/structural/StructuralAnalysis.cpp:39-45, 59-66, 72-85, 92-105, 113-121` | `StructuralInput_RefusesAControlTheDocumentDoesNotHave` (:160), `…RefusesWhenNothingHasBeenMeshed` (:174), `…RefusesAMeshOfGeometryThatHasSinceChanged` (:182), `…RefusesAMeshWhoseSizingIntentHasChanged` (:233), `…RefusesAfterAFailedGenerationOfEligibleGeometry` (:257), `…RefusesWhenTheDocumentNamesNoMaterial` (:336), `…RefusesAMaterialThatCannotDescribeLinearElasticity` (:348) | IMPLEMENTED | |
| Geometry eligibility asked BEFORE anything is read off the mesh | `StructuralAnalysis.cpp:48-66` precedes `:72` | `StructuralInput_RefusesAnIneligibleBodyBeforeLookingAtTheMesh` (:317) | IMPLEMENTED | |
| Delegation: `geometryIneligibility`, `Mesher::currency` + `describesTheModel`, `requireEffectiveMaterial` + `requireLinearElasticConstants`; mesh validity NOT re-derived | `StructuralAnalysis.cpp:59-60, 72-73, 113-118`; no signed-volume code in the file | as above | IMPLEMENTED | |
| Four elastic constants held BY VALUE; no material data of its own | `StructuralAnalysis.hpp:193-195, 227` | — | IMPLEMENTED | |
| Borrowed references; move-only | `StructuralAnalysis.hpp:155-158, 224-226` | — | IMPLEMENTED | |
| Every `InputProblem` reachable; two drafted values deleted (enum has 7) | `StructuralAnalysis.hpp:75-104` | `StructuralInput_ReachesEveryInputProblemItDeclares` (:365) | IMPLEMENTED | |
| Quality: `satisfiesPolicy()` is not the verdict; P17 reads `summaries` | `src/structural/StructuralValidation.cpp:232-246` reads `quality.summaries`; `satisfiesPolicy` not consulted in that file | `StructuralValidation_WarnsAboutAContradictoryPolicyWithoutRefusing` (StructuralValidationTests.cpp:550) | IMPLEMENTED | |
| Verification: "`tests/structural/StructuralAnalysisBoundaryTests.cpp`, nine cases" | file holds **ten** `TEST_CASE`s (`:126, 160, 174, 182, 233, 257, 317, 336, 348, 365`) | — | DRIFT | Count only. |
| "Those gates [quality, loads, restraints, sufficiency] are added to this function … The type is where they land" | They were NOT added to `requireStructuralModel` (still the 7 checks at `StructuralAnalysis.cpp:39-121`); they live in `validateStructuralAnalysisForSolve` (`StructuralValidation.cpp:366-513`) per ADR-042 Decision 4 | `StructuralValidation_StopsAtTheFirstRefusingStage` (:1150) | DRIFT | Forecast superseded by ADR-042; ADR-036 not amended. |

---

## ADR-037 — The solver's index space is derived from the mesh enumeration, interleaved per node; a reduced row is a different type

Decision: ordinal = position in `mesh.nodes()`; `DofIndex = 3k + c + 1` (1-based identity); `FreeEquationIndex` is a 0-based position with no invalid value and a deleted default ctor; three types (`MeshDofMap`, `ConstraintSet`, `FreeEquationMap`) each with one friend.

| Clause | Code location (file:line) | Test(s) that pin it | Status | Note |
|---|---|---|---|---|
| Node ordinal = the mesh's own enumeration, copied not sorted | `src/structural/StructuralDof.cpp:132-139` | `MeshDofMap_OrdinalsFollowTheMeshOwnNodeEnumeration` (StructuralDofTests.cpp:297); `MeshDofMap_NumbersSparseNodeHandlesDenselyRatherThanByHandleValue` (:266) | IMPLEMENTED | |
| Agrees with `StructuralResult`'s ordinal, checked through different APIs | `StructuralDof.hpp:228-233`; `StructuralResult.hpp` "parallel to mesh.nodes()" (header comment, read via grep `:13` region) | `MeshDofMap_UsesTheSameNodeOrdinalAsAStructuralResult` (:318) | IMPLEMENTED | |
| Interleaved: `3k+1, 3k+2, 3k+3` | `StructuralDof.cpp:23-27` `indexAt`; `StructuralDof.hpp:195-199` | `MeshDofMap_NumbersANodesThreeDofsConsecutivelyRatherThanInComponentBlocks` (:203) | IMPLEMENTED | |
| `componentAt` is the cyclic lookup over a flat element position | `StructuralDof.hpp:82-84` `kDofComponents[offset % kDofsPerNode]` | `StructuralDof_ComponentLookupIsCyclicOverAFlatElementPosition` (:1041) | IMPLEMENTED | |
| `FreeEquationIndex`: distinct type, 0-based, no invalid value, deleted default ctor | `StructuralDof.hpp:146-168` (`FreeEquationIndex() = delete;` at :150; `value()` "Zero-based" :156) | compile-fail `DOF_INDEX_AS_FREE_EQUATION`, `FREE_EQUATION_AS_DOF_INDEX`, `FREE_EQUATION_DEFAULT_CONSTRUCTED` (tests/compile_fail/CMakeLists.txt:541-552); `StructuralDof_SolverLocalIndicesAreDistinctTypes` (:1069) | IMPLEMENTED | ADR spells the case `dof-index-as-free-equation`; actual name `DOF_INDEX_AS_FREE_EQUATION`. Same case. |
| Absence is `std::optional`, not a sentinel | `StructuralDof.hpp:528` `std::optional<FreeEquationIndex> equationOf`; `StructuralDof.cpp:244-254` | `FreeEquationMap_GivesAConstrainedDofNoEquationRatherThanRowZero` (:815) | IMPLEMENTED | |
| Three types, each private ctor + exactly one friend | `StructuralDof.hpp:340-348, 450-453, 558-561` | compile-fail `MESH_DOF_MAP_CONSTRUCTED_DIRECTLY`, `CONSTRAINT_SET_CONSTRUCTED_DIRECTLY`, `FREE_EQUATION_MAP_CONSTRUCTED_DIRECTLY` (CMakeLists.txt:553-558) | IMPLEMENTED | |
| `ConstraintSet` validated: no duplicate, no out-of-range, no unknown node; sorted ascending | `StructuralDof.cpp:172-208` | `ConstraintSet_RefusesAnUnusableConstraint` (:562); `ConstraintSet_IsIndependentOfTheOrderConstraintsAreListedIn` (:530) | IMPLEMENTED | |
| `FreeEquationMap` built only from a matching pair | `StructuralDof.cpp:262-266` stamp check; `:278-285` range check | `FreeEquationMap_RefusesAConstraintSetBuiltAgainstADifferentMesh` (:835); `…ThatOutgrowsTheNumbering` (:962) | IMPLEMENTED | |
| Remesh refusal by `MeshStamp` PLUS node count | `StructuralDof.hpp:273-275` `mesh.owns(stamp_) && mesh.nodes().size() == nodes_.size()` | `MeshDofMap_ARemeshInvalidatesTheNumberingAndTheConstraintsBuiltOnIt` (:913) | IMPLEMENTED | |
| No overflow, proved by `static_assert` (`3*(2^32-1)+1 < 2^64`) | `StructuralDof.hpp:357-361` | `StructuralDof_ScalesWithoutOverflowingTheIndexSpace` (:1112) | IMPLEMENTED | |
| `DofIndex` outside `core/Id.hpp` | grep `DofIndex|FreeEquationIndex` in `include/bettercad/core` → no matches; defined at `StructuralData.hpp:119-145` | `StructuralData_ADofIndexIsSolverLocalAndNotADocumentIdentity` (StructuralDataTests.cpp:182) | IMPLEMENTED | |
| Assembly sizes from `freeCount()` and indexes with `value()` directly, no offset | `src/structural/StructuralSystem.cpp:252, 205-212` | `StructuralSystem_MapsLocalTet4DofsOntoTheGlobalNumbering` (:596) | IMPLEMENTED | |
| Element vector order `[ux1 uy1 uz1 ux2 … uz4]` via `localDofIndex` | `Tet4Element.hpp:140-143`; `StructuralSystem.cpp:149-153` static_asserts | `Tet4Element_LocalDofOrderingMatchesTheGlobalNumbering` (Tet4ElementTests.cpp:739) | IMPLEMENTED | |
| No prescribed VALUE on `ConstraintSet` | `StructuralDof.hpp:432-457` (no value field) | — | IMPLEMENTED | |
| "P17-BC-001 produces `NodalDof`s from CAD references and never stores an index" | `StructuralConstraints.hpp:153` `std::vector<DofIndex> constrained` on `RestraintResolution`; filled at `src/structural/StructuralConstraints.cpp:237, 259` | `StructuralReaction_AttributesReactionsPerRestraintAdditivelyWhenDisjoint` (StructuralReactionTests.cpp:768) | DRIFT | Derived, never persisted (hpp:146-148), added by ADR-041 Decision 3. ADR-037's sentence is now literally false and was not amended. |

---

## ADR-038 — The global stiffness matrix is a BetterCAD-owned CSR that Eigen can map

Decision: `StiffnessMatrix` is three vectors in CSR (row-major, inner ascending, SI N/m, private ctor + one friend), built by a symbolic then a numeric pass; row space is the free numbering of the empty constraint set; no regularisation; module links no linear algebra.

| Clause | Code location (file:line) | Test(s) that pin it | Status | Note |
|---|---|---|---|---|
| CSR: `rowStart[rows+1]` monotone, `rowStart.back()==nnz`, inner strictly ascending per row, values SI | `StructuralSystem.hpp:79-90, 109-161`; built at `src/structural/StructuralSystem.cpp:271-287` (`sort` + `unique` per row, `:274-275`) | `StructuralSystem_IsSparseAndNeverADenseSquareOfTheDofCount` (StructuralSystemTests.cpp:491); `StructuralSystem_MatchesAnIndependentDenseScatterOfEveryElement` (:639) | IMPLEMENTED | |
| `StiffnessMatrix` and `ForceVector`: private ctor, one friend (`assembleStructuralSystem`) | `StructuralSystem.hpp:150-154, 202-206` | `tests/compile_fail/StructuralSystemMisuse.cpp` (group exists; cases not read) | IMPLEMENTED | |
| `ForceVector` dense, length Ndof, zero-initialised | `StructuralSystem.cpp:351` `out.force.assign(out.rows, 0.0)` | `StructuralSystem_AssemblesAZeroForceVectorWithNoLoads` (:1277) | IMPLEMENTED | |
| `GlobalStructuralSystem` = K + F + source | `StructuralSystem.hpp:301-361`; `AssemblySource` six fields `:238-249` | `StructuralSystem_CarriesTheInputStateItWasAssembledFrom` (:1488) | IMPLEMENTED | Also carries the numbering and the element order (additive to the ADR's list). |
| Row space = free numbering of the EMPTY constraint set; no `- 1`, no `3 * node` | `StructuralSystem.cpp:421-427` `buildConstraintSet(*map, {})` → `buildFreeEquationMap`; rows from `equations.equationOf` `:205-212`; `:20-22` | `StructuralSystem_MapsLocalTet4DofsOntoTheGlobalNumbering` (:596) | IMPLEMENTED | |
| Two passes: symbolic pattern, then numeric accumulate | `StructuralSystem.cpp:255-287` and `:289-346` | `StructuralSystem_SumsEveryContributionAndLosesNone` (:674) | IMPLEMENTED | |
| Exactly one slot per `(row, col)`, every contribution `+=` | `StructuralSystem.cpp:341-342` | `StructuralSystem_AccumulatesANamedPairFromTwoNamedElements` (:735) | IMPLEMENTED | ADR's "mutation `+=` → `=` is killed" is pinned by these two. |
| Frozen orders: elements in P16 ascending `ElementId`; local row 0..11 then col 0..11; loads ascending by handle | `StructuralSystem.cpp:259, 321-325, 352`; `include/bettercad/meshing/Mesh.hpp:158-160` | `StructuralSystem_IsDeterministicAndFollowsP16ElementOrder` (:1437) | IMPLEMENTED | |
| All 144 local entries emitted, no pruning of exact zeros; nnz = connectivity | `StructuralSystem.cpp:318-343` | `StructuralSystem_IsSparseAndNeverADenseSquareOfTheDofCount` (:491) | IMPLEMENTED | Cross-preset nnz equality is evidence-doc only. |
| No regularisation: free body's K singular | no diagonal epsilon/pin/spring anywhere in `StructuralSystem.cpp`; grep `1e20|1e12|penalty` in `src/structural` → comments only (`StructuralSolve.cpp:23`) | `StructuralSystem_PreservesTheSixRigidBodyModesOfAFreeBody` (:947); `StructuralSystem_IsPositiveSemiDefiniteWithNullitySix` (:984) | IMPLEMENTED | |
| No post-symmetrising `0.5 (K + K^T)` | absent from `StructuralSystem.cpp`; `largestSymmetryError` is a scan `:82-96` | `StructuralSystem_IsExactlyAsSymmetricAsItsElementsAre` (:818) | IMPLEMENTED | |
| Only `coeff` (binary search), symmetry scan, finiteness scan — no algebra | `StructuralSystem.cpp:65-80, 82-96, 382-396` | `StructuralSystem_HoldsOnlyFiniteEntries` (:921) | IMPLEMENTED | |
| 64-bit index, bound proved by `static_assert` | `StructuralSystem.hpp:168-171` | — | IMPLEMENTED | |
| Atomic; takes `StructuralModel` + `PreparedLoads`, pair checked (`describes`) | `StructuralSystem.cpp:237-244, 453-473` | `StructuralSystem_RefusesLoadsPreparedForADifferentMesh` (:1376) | IMPLEMENTED | |
| "`bettercad_structural` continues to link no linear algebra at all" | `src/structural/CMakeLists.txt:67-68` `PRIVATE_LINK BetterCAD::eigen`; `StructuralSystem.hpp:10-14` still says "`bettercad_structural` still links none" | — | DRIFT | True on 2026-10-08, superseded by ADR-039 the same day; neither ADR-038 nor the header comment was amended. |
| "`P17-SOLVE-001` wraps the same arrays in `Eigen::Map<const Eigen::SparseMatrix<double>>` at zero copy" | `src/structural/StructuralSolve.cpp:256-277`: "A Map would need Eigen's own signed StorageIndex … this is a conversion rather than a view" — builds `Eigen::Triplet` + `setFromTriplets`; `StructuralSystem.hpp:15-16` still promises the Map | — | DRIFT | Behaviour is correct (no duplicates, so summation order is moot, `:273-276`), but the ADR and the header state a mechanism the solver does not use. |
| Context: "There is no `Eigen/Sparse`, no `Eigen::SparseMatrix` and no `Eigen::Triplet` anywhere in the tree" | `StructuralSolve.cpp:28-29, 261, 265` | — | DRIFT | Stale context statement after ADR-039; recorded because the ADR's "committed to" section does not scope it. |

---

## ADR-039 — The structural solver is Eigen's SimplicialLDLT with a pivot gate; Eigen admitted PRIVATE

Decision: `Eigen::SimplicialLDLT`, direct; success requires (1) `info()==Success` AND every `D(k,k) > 0` AND `min|D|/max|D| >= pivotFloor`, (2) finite `uf`, (3) an independent normalized residual gate; Eigen linked PRIVATE, no public header includes it.

| Clause | Code location (file:line) | Test(s) that pin it | Status | Note |
|---|---|---|---|---|
| Solver is `Eigen::SimplicialLDLT` | `src/structural/StructuralSolve.cpp:285-286`; `StructuralData.hpp:298-302` `SolverAlgorithm::SimplicialLdlt` (sole value) | `StructuralSolve_NamesTheSelectedSolverAndValidatesItsSettings` (StructuralSolveTests.cpp:359) | IMPLEMENTED | |
| Eigen PRIVATE to `bettercad_structural`, as sketch and assembly | `src/structural/CMakeLists.txt:67-68`; `src/sketch/CMakeLists.txt:18-19`; `src/assembly/CMakeLists.txt:23-24` | `architecture.checker.eigen-public-header` (tests/architecture/CMakeLists.txt:65-67) | IMPLEMENTED | `cmake/BetterCADDependencies.cmake:50` comment still says "private to bettercad_sketch" (stale wording; not an ADR claim). |
| No public header includes Eigen | grep `#include <Eigen` in `include/` → none (only prose mentions: `StructuralData.hpp:299`, `Tet4Element.hpp:33-39`, `StructuralPost.hpp:389,397`, `StructuralSystem.hpp:13-16`); enforced by rule 7 `CheckLayering.cmake:119-120, 189-197` | `architecture.checker.eigen-public-header` | IMPLEMENTED | Rule 7 post-dates ADR-039's text but implements its commitment. |
| Gate 1: `info()==Success` AND every pivot > 0 AND ratio >= floor | `StructuralSolve.cpp:287-317` (status), `:325-358` (`vectorD()`, `entry <= 0.0 → positive=false`, `pivotRatio < settings.pivotFloor` refused) | `StructuralSolve_RefusesAFreeBodyAlthoughTheLibrarySucceeds` (:806); `StructuralSolve_RefusesAnIndefiniteSystem` (:513); `StructuralSolve_RefusesAnExactlySingularSmallSystem` (:473) | IMPLEMENTED | Exact-zero pivot (`NumericalIssue`) is classified `SingularSystem`, `:301-303`. |
| Gate 2: every component of `uf` finite | `StructuralSolve.cpp:368-380` | `StructuralSolve_RejectsASolutionThatOverflowedToInfinity` (:643) | IMPLEMENTED | |
| Gate 3: independent residual `r = Kff uf - Ff` over BetterCAD's CSR; `solver.error()` never consulted | `StructuralSolve.cpp:382-461`; no `solver.error()` call in the file | `StructuralSolve_RefusesANearSingularSystemTheResidualWouldAccept` (:853); `StructuralSolve_ChecksTheFreeResidualAndKeepsTheReactions` (:1029) | IMPLEMENTED | |
| Pivot measure is dimensionless `min|D|/max|D|` | `StructuralSolve.cpp:341-344` | `StructuralSolve_SolvesAnIllConditionedSystemAndReportsHonestly` (:529) | IMPLEMENTED | |
| Defaults: `relativeResidualTolerance = 1e-9`, `pivotFloor = 1e-12` | `StructuralData.hpp:332, 350` | `StructuralPersistence_SolverSettingsRoundTripExactly` (StructuralAnalysisFileTests.cpp:557) | IMPLEMENTED | |
| No max-iterations / tolerance / preconditioner fields ("deliberately absent") | `StructuralData.hpp:314-353` (three fields only) | — | IMPLEMENTED | |
| AMD ordering is solver-local; solution returned in `FreeEquationIndex` order, mapped back; constrained entries exactly zero | `StructuralSolve.cpp:549-564`; `StructuralSolve.hpp:311-316` | `StructuralSolve_LeavesConstrainedDisplacementsExactlyZero` (:984) | IMPLEMENTED | |
| Residual gate on the FREE system; full residual retained | `StructuralSolve.cpp:566-594`; `StructuralSolve.hpp:347-353` | `StructuralSolve_ChecksTheFreeResidualAndKeepsTheReactions` (:1029) | IMPLEMENTED | |
| No penalty (`Kii += 1e20`), no pinning, no `Kff += eps I` | grep `1e20|1e12|penalty` in `src/structural` → only the comment at `StructuralSolve.cpp:23`; reduction is exact `:105-175` | `StructuralSolve_RefusesAnInsufficientlyRestrainedModel` (:897) | IMPLEMENTED | |
| Empty free system solves with `pivotRatio 1.0`, nothing handed to the library | `StructuralSolve.cpp:251-254` | `StructuralSolve_HandlesAZeroRightHandSideAndAnEmptySystem` (:563) | IMPLEMENTED | |
| Normalized residual has no invented floor; zero/zero handled exactly | `StructuralSolve.cpp:429-443` | same | IMPLEMENTED | |
| Single-threaded; OpenMP not enabled anywhere | grep `OpenMP|openmp|-fopenmp` in `cmake/` → none | `StructuralSolve_IsDeterministicAcrossRepeatedSolves` (:1177) | IMPLEMENTED | |
| `src/structural/CMakeLists.txt` note replaced by the record of this decision | `src/structural/CMakeLists.txt:23-40` | — | IMPLEMENTED | |
| Library's status is insufficient: `if (d == RealScalar(0)) ok = false` | `build/release/_deps/eigen3-src/Eigen/src/SparseCholesky/SimplicialCholesky_impl.h:365-369` | `StructuralSolve_RefusesAFreeBodyAlthoughTheLibrarySucceeds` (:806) | IMPLEMENTED | Quoted from the vendored 5.0.1 tree. |
| Licence: MPL-2.0; `COPYING.LGPL`/`COPYING.GPL` absent; `Amd.h` carries the Davis/Google MPL statement | `build/release/_deps/eigen3-src/COPYING.{APACHE,BSD,MINPACK,MPL2,README}` (no LGPL/GPL file); `Eigen/src/OrderingMethods/Amd.h:16-18`; pinned `cmake/BetterCADDependencies.cmake:54-55` | — | IMPLEMENTED | |

---

## ADR-040 — Recovered fields are a stage product; tensor conventions live beside the Voigt vectors

Decision: `recoverFields` returns `RecoveredFields` (carrying the solved system's `AssemblySource`), not a `StructuralResult`; `StressTensor3`/`StrainTensor3` aggregates with the Voigt mapping in `at()`; `tensorOf(Strain6)` halves each gamma; principal values via `SelfAdjointEigenSolver` (default path), sorted descending by BetterCAD; von Mises by the component formula; hydrostatic stress and principal strains implemented.

| Clause | Code location (file:line) | Test(s) that pin it | Status | Note |
|---|---|---|---|---|
| `RecoveredFields` carries `AssemblySource` copied forward; not a `StructuralResult` | `StructuralPost.hpp:467-553`; `src/structural/StructuralPost.cpp:575-577` `RecoveredFields(system.source(), …)` | `StructuralPost_CarriesTheProvenanceOfWhatItRecovered` (StructuralPostTests.cpp:1643) | IMPLEMENTED | `StructuralResult` is built only in `solveStructuralAnalysis` (`StructuralValidation.cpp:639-642`). |
| `describes(mesh)` is a mesh-identity check (stamp + counts) | `StructuralPost.hpp:483-486` | `StructuralPost_RefusesAResultBoundToADifferentMesh` (:1743) | IMPLEMENTED | |
| Each stage re-checks source/mesh/material | `StructuralPost.cpp:408-449` | `StructuralPost_RefusesASolutionFromADifferentSystem` (:1672); `…RefusesAMaterialThatIsNotTheOneSolvedWith` (:1697) | IMPLEMENTED | |
| Voigt order frozen `XX YY ZZ XY YZ ZX` | `StructuralData.hpp:180-192` | `StructuralData_TheTensorComponentOrderIsFrozen` (StructuralDataTests.cpp:232); `Tet4Element_BRowOrderMatchesTheSharedVoigtOrdering` (Tet4ElementTests.cpp:697) | IMPLEMENTED | |
| Engineering shear frozen in field names (`gammaXy`) | `StructuralData.hpp:217-224` | `StructuralData_TheShearConventionIsEngineeringAndIsInTheFieldNames` (:270) | IMPLEMENTED | |
| `mu` on D's shear diagonal, not `2 mu` | `src/structural/Tet4Element.cpp:116-126` (`d[row][row] = shear` for rows 3..5) | `Tet4Element_EngineeringShearGivesTauEqualsMuGamma` (:571); `StructuralPost_ConvertsPureEngineeringShearThroughMuAndNotTwoMu` (StructuralPostTests.cpp:678) | IMPLEMENTED | |
| B and D reached only through `strainFrom`/`stressFrom`; no second B/D | `StructuralPost.cpp:283, 296, 306`; `Tet4Element.hpp:246-247, 312` | `StructuralPost_RecoversAffineStrainFromASingleTetrahedron` (:516) | IMPLEMENTED | |
| Tensor types in the structural module as aggregates; mapping `XY→(0,1)`, `YZ→(1,2)`, `ZX→(2,0)` written once in `at()` | `include/bettercad/structural/StructuralPost.hpp:107-133, 155-186` | `StructuralPost_MapsVoigtStressOntoTheSymmetricTensorPositions` (:426) | IMPLEMENTED | |
| Two types, not one template | `StructuralPost.hpp:107, 155` (two structs) | — | IMPLEMENTED | |
| `tensorOf(Strain6)` halves each gamma; `tensorOf(Stress6)` relabels | `StructuralPost.hpp:189-206` (`0.5 * strain.gammaXy`) | `StructuralPost_HalvesEngineeringShearWhenBuildingTheStrainTensor` (:473); `StructuralPost_ReportsPrincipalStrainsThroughTheHalvedShear` (:1089) | IMPLEMENTED | |
| Symmetry structural (one `xy` field) | `StructuralPost.hpp:107-113` | same as mapping test | IMPLEMENTED | |
| `SelfAdjointEigenSolver`, default path, not `computeDirect` | `StructuralPost.cpp:122-123`; grep `computeDirect` in `src/structural` → comments only | `StructuralPost_KeepsTheInvariantsUnderARotationOfTheStressTensor` (:881) | IMPLEMENTED | |
| Values sorted descending by BetterCAD | `StructuralPost.cpp:145-149` | `StructuralPost_OrdersPrincipalStressesDescending` (:754) | IMPLEMENTED | |
| No eigenvectors computed | `StructuralPost.cpp:131` uses `eigenvalues()` only | — | IMPLEMENTED | |
| von Mises component formula, no `abs`, no clamp | `StructuralPost.cpp:85-107` | `StructuralPost_ComputesVonMisesFromTheFullThreeDimensionalState` (:918); `…LeavesVonMisesUnchangedByAHydrostaticShift` (:1022); `…ReportsRootThreeTimesTauForEachPureShear` (:1005) | IMPLEMENTED | |
| `hydrostaticStress = tr(S)/3`, normal-stress sign, not named `pressure` | `StructuralPost.hpp:212-222` | `StructuralPost_ComputesHydrostaticStressInTheNormalStressConvention` (:1065) | IMPLEMENTED | |
| Principal strains implemented through `StrainTensor3` | `StructuralPost.hpp:316-317`; `StructuralPost.cpp:177-196` | `StructuralPost_ReportsPrincipalStrainsThroughTheHalvedShear` (:1089) | IMPLEMENTED | |
| Eigen used PRIVATE in the .cpp only | `StructuralPost.cpp:28` `#include <Eigen/Eigenvalues>`; none in `StructuralPost.hpp` | `architecture.checker.eigen-public-header` | IMPLEMENTED | |

---

## ADR-041 — A reaction is the constrained residual; a shared DOF is counted once

Decision: `R[dof] = fullResidual()[dof]` on constrained DOFs; sign `R = K u - F` so `sum F + sum R = 0`; `RestraintResolution` gains `constrained`; unique ownership plus one shared aggregate; equilibrium normalised by sums of magnitudes with dimensioned floors; no rotational reaction DOFs, moments derived on request.

| Clause | Code location (file:line) | Test(s) that pin it | Status | Note |
|---|---|---|---|---|
| Reaction = constrained entries of `fullResidual()`, no second `K u` | `src/structural/StructuralReaction.cpp:322-323, 352`; no `stiffness()` call in the file | `StructuralReaction_MatchesThePartitionFormulaOnASmallSystem` (StructuralReactionTests.cpp:398) | IMPLEMENTED | |
| Free residual is not a reaction; mask built from what was constrained; other components exactly zero | `StructuralReaction.cpp:360-381` | `StructuralReaction_ReportsOnlyTheRestrainedComponentOfAPartialSupport` (:551); `…RecoversEachComponentAndOnlyTheConstrainedOnes` (:490) | IMPLEMENTED | |
| Sign: `R = K u - F`; `imbalance = external + reaction` | `StructuralReaction.hpp:14-22`; `StructuralReaction.cpp:252` | `StructuralReaction_FullyConstrainedReactionIsMinusTheAppliedLoad` (:303) | IMPLEMENTED | |
| `RestraintResolution` gains `std::vector<DofIndex> constrained`, ascending, unique, `size() == degreesOfFreedom` | `StructuralConstraints.hpp:132-153`; `src/structural/StructuralConstraints.cpp:219-259` (`sort` + `unique`, `degreesOfFreedom = mineCount`) | `StructuralReaction_AttributesReactionsPerRestraintAdditivelyWhenDisjoint` (:768) | IMPLEMENTED | Derived, not persisted (`StructuralAnalysisJson.cpp:19-23`). |
| Shared DOF counted once globally; owned/shared split; `sum owned + shared == total` | `StructuralReaction.cpp:228-248` (multiplicities, sorted vector not hash), `:383-391`, `:517-528`, `:533-568`; `StructuralReaction.hpp:110-125` | `StructuralReaction_CountsASharedDegreeOfFreedomOnceGlobally` (:853) | IMPLEMENTED | |
| Scale = sum of individual magnitudes (loaded nodes + constrained nodes), not the net | `StructuralReaction.cpp:404, 409, 445, 448, 453, 458` | `StructuralReaction_MeasuresTheEquilibriumErrorItIsGatedOn` (:949) | IMPLEMENTED | |
| Floors carry their own dimension (`Force`, `Torque`) | `StructuralReaction.hpp:228-230`; used `:258-262, 269-273` of the .cpp | — | IMPLEMENTED | |
| Thresholds measured (defaults `1e-12`), not inherited from `SolverSettings` | `StructuralReaction.hpp:221-234` | `StructuralReaction_RefusesAnImbalanceLargerThanTheThreshold` (:1319) | IMPLEMENTED | The measurement itself is evidence-doc. |
| Force AND moment checked; moment even when force passes | `StructuralReaction.cpp:575-600` | `StructuralReaction_BalancesAPureCoupleInMomentNotOnlyInForce` (:1381); `…BalancesTheMomentAboutANonZeroOriginToo` (:1439) | IMPLEMENTED | |
| No `Mx, My, Mz` nodal reaction; moment derived on request with origin parameter | `StructuralReaction.hpp:90-100` (`SupportReaction` has `Force3D` only); `momentAbout(mesh, origin)` `:383-384`, impl `StructuralReaction.cpp:662-684` | `StructuralReaction_TransfersTheMomentCorrectlyToAnotherOrigin` (:698) | IMPLEMENTED | |
| One cross product, core's `momentOf(lever, force)` | `StructuralReaction.cpp:189, 407, 515, 567, 678` | same | IMPLEMENTED | |
| Attribution uses the solve's own constrained set, not a re-resolution | `StructuralReaction.cpp:306-320` (size), `:477-486` (membership) | `StructuralReaction_RefusesEverySourceMismatch` (:1147) | IMPLEMENTED | Header `StructuralReaction.hpp:433-434` says "compared element by element"; the code compares the count and per-resolution membership (the `SolvedSystem` keeps no constrained list). Header wording overstates; ADR text does not. |
| Reactions never persisted | `src/io/json/StructuralAnalysisJson.cpp:19-23`; writer signature `:594` | `StructuralPersistence_NoSolverStateReachesTheFile` (StructuralAnalysisFileTests.cpp:932) | IMPLEMENTED | |

---

## ADR-042 — Mesh quality rejects on a measured accuracy law; one unbypassable solve entry

Decision: hard failures `3r/R < 1e-10` and `aspect > 3e5`, none on dihedral angles; warnings at the qualified envelope (`3e-4`, `6e-3 rad`, `3.125 rad`, `81.0`) that reject nothing; `< 6` constrained DOFs is a hard failure, `>= 6` says nothing; one entry `solveStructuralAnalysis` plus `validateStructuralAnalysisForSolve`; staged short-circuit; status derived; P16 untouched.

| Clause | Code location (file:line) | Test(s) that pin it | Status | Note |
|---|---|---|---|---|
| `kStructuralRadiusRatioFloor = 1e-10`, `kStructuralAspectRatioCeiling = 3e5` | `StructuralValidation.hpp:108, 130`; applied `src/structural/StructuralValidation.cpp:152-153, 180-182` | `StructuralValidation_PolicyBindsOneFailureBoundPerDegenerationFamily` (StructuralValidationTests.cpp:170); `…RejectsOnlyBelowTheMeasuredAccuracyFloor` (:396) | IMPLEMENTED | |
| No hard-fail bound on either dihedral angle | `StructuralValidation.cpp:160-168` `.failure = std::nullopt` | `StructuralValidation_AcceptsASliverBecauseTheKernelHandlesIt` (:450) | IMPLEMENTED | |
| Warning thresholds `3.0e-4`, `6.0e-3`, `3.125`, `81.0` (radians) | `StructuralValidation.cpp:152-182` | `StructuralValidation_PolicyIsAcceptedByP16sOwnPolicyChecker` (:158) | IMPLEMENTED | |
| Warnings reject nothing; severity a function of the code | `StructuralValidation.cpp:109-138`; `StructuralValidation.hpp:414-423` `status()` | `StructuralValidation_SeverityIsAFunctionOfTheCodeAndExactlyTwoAreWarnings` (:299, body read: `REQUIRE(warnings == 3)` at `:308`) | IMPLEMENTED | Test NAME says "ExactlyTwo", body asserts three (`QualityPolicyUnusable` was demoted to a warning after the name was written, `:313-319`). Stale test name; ADR-042 does not state a warning count. |
| `constrained < 6` hard failure; `>= 6` not sufficiency | `StructuralValidation.hpp:140` `kMinimumConstrainedDofs = 6`; `StructuralValidation.cpp:474-486`; `StructuralValidation.hpp:535-537` | `StructuralValidation_DoesNotClaimSixDegreesOfFreedomIsSufficient` (:1038) | IMPLEMENTED | |
| `solveStructuralAnalysis` is the only producer of a `StructuralResult` from a document; runs every gate in order | `StructuralValidation.cpp:515-659` (validate `:526-535`, prepare `:548-570`, assemble/solve/recover `:579-602`, publish `:639-642`) | `StructuralValidation_SolvesAWellPosedAnalysisEndToEnd` (:1178); `…PublishesNothingWhenAGateRefuses` (:1239) | IMPLEMENTED | |
| Signature `solveStructuralAnalysis(document, regenerator, mesher, analysis, settings)` | `StructuralValidation.hpp:653-657`: `(document, regenerator, mesher, analysis, const Point3D& origin = Point3D{}, const EquilibriumTolerance& tolerance = {})`; settings read from the definition `StructuralValidation.cpp:584-590` | — | DRIFT | `settings` removed by ADR-047 (recorded there); `origin`/`tolerance` added and not reflected in ADR-042. Also: the header comment `:649-652` says `origin` is explicit because a default of zero hid a mutation, yet `:656` defaults it to `Point3D{}` — header-internal inconsistency, flagged for P17-QUAL. |
| `validateStructuralAnalysisForSolve` available without solving; entry calls it (one implementation of the order) | `StructuralValidation.cpp:366-513`; `:526-527` | `StructuralValidation_AcceptsAWellPosedAnalysis` (:908) | IMPLEMENTED | |
| Short-circuit between stages, accumulate within a stage | `StructuralValidation.cpp:374-379, 381-387, 400-407, 423-426, 437-444` (returns); quality loop reports every metric `:313-360` | `StructuralValidation_StopsAtTheFirstRefusingStage` (:1150); `…CarriesAMeshQualityFindingIntoTheAnalysisReport` (:1461) | IMPLEMENTED | |
| Status derived from findings, never stored | `StructuralValidation.hpp:414-423` | `StructuralValidation_StatusIsDerivedSoItCannotDisagreeWithTheFindings` (:357) | IMPLEMENTED | |
| Findings ordered by severity, then code, then element | `StructuralValidation.cpp:204-219` (`stable_sort`) | `StructuralValidation_OrdersFindingsDeterministically` (:681) | IMPLEMENTED | |
| `QualityThresholds` is a `std::map` keyed by metric | `include/bettercad/meshing/MeshQuality.hpp:246` | — | IMPLEMENTED | |
| Thresholds are a value from `structuralQualityThresholds()`; P16's default untouched | `StructuralValidation.cpp:140-185`; `MeshQuality.hpp:274, 406` (`reportOnlyThresholds` still the default) | `StructuralValidation_PolicyIsAcceptedByP16sOwnPolicyChecker` (:158) | IMPLEMENTED | "No meshing source file changed" not checkable without VCS. |
| No metric recomputed; reads `summaries`, uses P16's `direction(metric)` | `StructuralValidation.cpp:230-247` (`quality.summaries.find`, `meshing::direction`); header `:4-8` | `StructuralValidation_RecordsTheWorstOfEveryMetricWhetherOrNotItPassed` (:649) | IMPLEMENTED | |
| Summary verdict agrees with `evaluateMeshQuality` per-element classification | — (test-only cross-check) | `StructuralValidation_AgreesWithP16sOwnPerElementClassification` (:613; calls `evaluateMeshQuality(*mesh, structuralQualityThresholds())` at `:628`) | IMPLEMENTED | |
| Validation never mutates (const references, returns a report); strict comparisons | `StructuralValidation.hpp:464-465, 509-512`; `StructuralValidation.cpp:257-259` | `StructuralValidation_ComparesStrictlySoAValueOnTheBoundIsOnTheGoodSide` (:1277) | IMPLEMENTED | |

---

## ADR-043 — A deformed display is an override position buffer through the one existing conversion

Decision: `MeshPresentation(view, style, positions = {})` takes an optional SI-metre override; both buffers pass through the single `toModelLength`; normals and selection rebuilt from the deformed corners; `Viewer::displayDeformedMesh` is a named sibling that refuses a length mismatch; a new scale is a new presentation.

| Clause | Code location (file:line) | Test(s) that pin it | Status | Note |
|---|---|---|---|---|
| Constructor takes an optional override span, empty = `view.positions()` | `src/renderer/occt/OcctMeshPresentation.hpp:86-87`; `.cpp:81-89` | `StructuralViz_ADeformedDisplayAtScaleZeroIsTheUndeformedImage` (StructuralResultViewTests.cpp:3340) | IMPLEMENTED | |
| Both buffers converted at the same call site by `toModelLength` | `OcctMeshPresentation.cpp:54-56` (definition), `:100-102` (corners), `:128-130` (edges) | `StructuralViz_ADeformedMeshOverlaysItsOwnCadSolidAtTheSameScale` (:3265); `MeshDisplay_TheMeshOverlaysItsOwnCadSolidAtTheSameScale` (MeshDisplayTests.cpp:183) | IMPLEMENTED | |
| "`toModelLength` in `OcctMeshPresentation.cpp` is the project's only metres-to-millimetres conversion" | A second function of the same name exists: `src/renderer/occt/OcctOverlayPresentation.cpp:23-25` (also `geometry::occt::toModel(Length::fromSi(metres))`) | `StructuralViz_AnOverlayIsDrawnAtTheMeshsOwnScale` (:4993) | DRIFT | The underlying kernel conversion (`geometry::occt::toModel`) is still single; the ADR's literal claim about the wrapper is no longer true after ADR-045. See findings. |
| Normals recomputed from the deformed corners | `OcctMeshPresentation.cpp:104-122` (`corner` arrays come from `positions`) | `StructuralViz_ADeformedDisplayAtAVisibleScaleChangesTheImage` (:3379) | IMPLEMENTED | |
| Selection array built from the same corners | `OcctMeshPresentation.cpp:330-335` (`corners_`) | — | IMPLEMENTED | |
| Render triangle i is still facet i (index-to-NodeId bridge untouched) | `OcctMeshPresentation.cpp:94-103` (non-indexed, in facet order); `lastDetectedTriangle` returns `int` `:155-160` | `StructuralViz_ADeformedDisplayStillKnowsItsMeshGeneration` (:3476) | IMPLEMENTED | |
| Edges move too | `OcctMeshPresentation.cpp:125-131` | `StructuralViz_ADeformedDisplayMovesItsEDGESToo` (:3511) | IMPLEMENTED | |
| `Viewer::displayDeformedMesh(ObjectId, const MeshView&, span, MeshStyle)` as a named sibling, one shared body | `include/bettercad/renderer/Viewer.hpp:271-273, 417-420`; `src/renderer/occt/OcctViewer.cpp:432-463` | same suite | IMPLEMENTED | |
| Refuses a length mismatch (`positions.size() == view.positions().size()`) | `OcctViewer.cpp:455-461` | `StructuralViz_ADeformedDisplayRefusesABufferThatIsNotThisMeshs` (:3424) | IMPLEMENTED | Empty buffer is refused first (`:435-436` comment; body of that branch not shown in the excerpt read). |
| No in-place `setPositions`; new scale = new presentation | `OcctMeshPresentation.hpp:93-134` exposes no position setter | — | IMPLEMENTED | By absence. |
| `deformedPositions = x + s·u` in SI metres, from the undeformed buffer each time, identity-safe lookup | `src/renderer/StructuralResultView.cpp:533-573` | `StructuralViz_DeformedPositionsAreTheUndeformedPlusScaledDisplacement` (:378); `StructuralViz_EverythingLeavingThisLayerIsSiMetres` (:320) | IMPLEMENTED | |
| Presentation holds no mesh and no engineering identity | `OcctMeshPresentation.hpp:15-18, 128-134` | — | IMPLEMENTED | |

The ADR's contour forecast (N groups) is self-amended at `ADR-043:146-177` and matches the code (ADR-044); not counted.

---

## ADR-044 — A contour is a colour per facet corner; (superseded part) an element channel needs a mapping meshing does not have

Decision: one colour per facet corner (9 doubles per facet) so an element channel draws flat and a nodal channel interpolates by the shape function; flat-vs-smooth decided headlessly in `triangleCornerBands`; `setMeshContour` refuses bad buffers; selected facets keep the highlight. The "needs a meshing mapping" claim is marked superseded by the ADR itself.

| Clause | Code location (file:line) | Test(s) that pin it | Status | Note |
|---|---|---|---|---|
| Colour per facet CORNER, nine doubles per facet, non-indexed buffer | `OcctMeshPresentation.hpp:105-118, 142-150, 169-171`; `.cpp:197-212` | `StructuralViz_AContourReachesTheScreenAsManyColours` (:3584) | IMPLEMENTED | |
| Element channel: three corners share the owner's band; nodal: each corner its own node | `src/renderer/StructuralContour.cpp:316-336` (element), `:338-356` (nodal) | `StructuralViz_ANodalChannelColoursEachCornerFromItsOwnNode` (:3659); `StructuralViz_AnElementChannelNeedsTheOwningTetrahedron` (:3713) | IMPLEMENTED | |
| Flat-vs-smooth decided in `renderer::triangleCornerBands`, headless | `include/bettercad/renderer/StructuralContour.hpp:187-211`; impl above | same | IMPLEMENTED | |
| Per-vertex colour is an array attribute (`Graphic3d_ArrayFlags_VertexColor`) | `OcctMeshPresentation.cpp:167-173, 185-188` | `StructuralViz_AContourReachesTheScreenAsManyColours` (:3584) | IMPLEMENTED | The 1-vs-4807 measurement itself is evidence-doc. |
| Element channel on a Triangle3 boundary refused by name (element and channel named) | `StructuralContour.cpp:326-331` | `StructuralViz_TheTriangle3BoundaryStillRefusesAnElementChannel` (:5740); `StructuralViz_CornerColoursRefuseWhatTheyCannotMap` (:3805) | IMPLEMENTED | |
| "Closing it needs a new capability in the qualified P16 meshing module" | Superseded by the ADR's own header (`ADR-044:5-21`); code implements ADR-046 in the renderer: `src/renderer/MeshView.cpp:232-245` | `StructuralViz_EveryElementChannelContoursTheBoundaryByTetrahedron` (:5620) | IMPLEMENTED | Current ADR text and code agree (claim retracted in place). `TODO.md:1610-1641` records the retraction. |
| `setMeshContour` refuses wrong length and any component not finite in `[0,1]`, never clamps | `src/renderer/occt/OcctViewer.cpp:551-571` | `StructuralViz_AContourBufferIsCheckedBeforeItIsDrawn` (:4126) | IMPLEMENTED | |
| Selected facet keeps the highlight colour (`facets(…, withColours=false)` for the highlight group) | `OcctMeshPresentation.cpp:162-163, 287` | `StructuralViz_ASelectedFacetKeepsItsHighlightUnderAContour` (:4030) | IMPLEMENTED | |

---

## ADR-045 — A glyph overlay is a list of segments in SI metres; glyph data carries no position

Decision: headless `loadArrowOverlay`/`reactionArrowOverlay`/`restraintSymbolOverlay` emit `OverlaySegment`s in SI metres; `PresentationKind::Overlay` + `Viewer::displayOverlay`; head at the node, five segments per arrow, one bar per constrained component, zero vector skipped, `tooSmall` counted separately, barb basis from the least-aligned axis, converted once in the adapter.

| Clause | Code location (file:line) | Test(s) that pin it | Status | Note |
|---|---|---|---|---|
| `OverlaySegment{from,to}`, `StructuralOverlay{role, segments, glyphs, skipped, tooSmall}`, `OverlayRole` | `include/bettercad/renderer/StructuralOverlay.hpp:37-106` | — | IMPLEMENTED | |
| The three overlay functions with the stated signatures | `StructuralOverlay.hpp:122-150` | `StructuralViz_AnArrowsHeadIsExactlyAtItsNode` (:4321) etc. | IMPLEMENTED | |
| `PresentationKind::Overlay` (named `Overlay`), `Viewer::displayOverlay` | `include/bettercad/renderer/Viewer.hpp:105-117, 288-289`; `OcctViewer.cpp:400-430` | `StructuralViz_AnOverlayReachesTheScreenOverItsMesh` (:4636) | IMPLEMENTED | |
| `restraintSymbolOverlay` takes the `MeshDofMap` | `StructuralOverlay.hpp:147-150`; `src/renderer/StructuralOverlay.cpp:170-173, 203-214` | `StructuralViz_ARestraintOverlayRefusesAnotherMeshsRestraints` (:5335) | IMPLEMENTED | ADR says the mask "CANNOT be derived from the restraint resolutions" because they carry "a node COUNT and never a node list" — true (`StructuralConstraints.hpp:124-127`), though `RestraintResolution::constrained` (`:153`, ADR-041) would let it be derived via the numbering. The implementation asks the `ConstraintSet` directly; no behavioural disagreement. |
| Head at the point of application, tail upstream | `StructuralOverlay.cpp:50-52` | `StructuralViz_AnArrowsHeadIsExactlyAtItsNode` (:4321) | IMPLEMENTED | |
| Five segments per arrow: shaft + four barbs in two perpendicular pairs | `StructuralOverlay.hpp:65` `kSegmentsPerArrow = 5`; `.cpp:54-71` | same | IMPLEMENTED | |
| One bar per constrained component, centred on the node, along that axis | `StructuralOverlay.cpp:192-227` | `StructuralViz_ARestraintSymbolHasOneBarPerConstrainedComponent` (:4415); `…ARestraintBarLiesAlongTheComponentItDepicts` (:4748) | IMPLEMENTED | |
| Zero vector produces no arrow; counted `skipped` | `StructuralOverlay.cpp:112-117` | `StructuralViz_AZeroForceDrawsNoArrowAndIsCounted` (:4490) | IMPLEMENTED | |
| Non-positive display length counted separately as `tooSmall` | `StructuralOverlay.hpp:91-103`; `.cpp:124-134` | `StructuralViz_AnArrowTooSmallToDrawIsCountedNotDegenerate` (:4822) | IMPLEMENTED | |
| Barb basis seeds from the axis least aligned with the shaft | `StructuralOverlay.cpp:23-38` | `StructuralViz_AnAxisAlignedArrowStillGetsItsBarbs` (:4933) | IMPLEMENTED | |
| Everything SI metres; converted once in the adapter | `StructuralOverlay.cpp:7-10` (no conversion; grep `1000` finds none in that file per its header); `OcctOverlayPresentation.cpp:23-25, 59-64` | `StructuralViz_AnOverlayLeavesThisLayerInSiMetres` (:4377); `…AnOverlayIsDrawnAtTheMeshsOwnScale` (:4993) | IMPLEMENTED | See ADR-043 drift on the wrapper name. |
| Position resolution refuses an unknown `NodeId` rather than skipping | `StructuralOverlay.cpp:100-108, 196-202` | `StructuralViz_AnOverlayRefusesWhatItCannotPlace` (:4540) | IMPLEMENTED | |
| Reaction sign drawn as qualified, not negated | `StructuralOverlay.cpp:161-168` | `StructuralViz_AReactionOverlayKeepsTheQualifiedSign` (:4590) | IMPLEMENTED | |
| Overlay is not selectable | `OcctOverlayPresentation.cpp:93-98` (`ComputeSelection` does nothing) | `StructuralViz_AnOverlayDoesNotBlockPickingTheMeshBeneathIt` (:4879) | IMPLEMENTED | |
| Presentation holds no mesh/result; receives points | `OcctOverlayPresentation.cpp:55-66` (copies endpoints only) | — | IMPLEMENTED | |
| `LoadGlyph` stays undrawn (no overlay function takes it) | `StructuralOverlay.hpp:122-150` take `NodalLoadArrow`, `ReactionArrow`, `PreparedRestraints` only | — | IMPLEMENTED | By absence. |

---

## ADR-046 — A tetrahedron-derived boundary view carries its owning element

Decision: `MeshView::volumeBoundaryByTetrahedronOf` with `MeshSource::VolumeBoundaryByTetrahedron`; exterior iff exactly one tetrahedron carries the face, three or more refused by name; sorted node triple as key, unsorted winding emitted; determinism by sorting; no-tetrahedron mesh refused; must cover the same node triples as the Triangle3 boundary; `volumeBoundaryOf` untouched; no meshing change.

| Clause | Code location (file:line) | Test(s) that pin it | Status | Note |
|---|---|---|---|---|
| Factory and fourth `MeshSource` enumerator | `include/bettercad/renderer/MeshView.hpp:67-80, 133-134`; `src/renderer/MeshView.cpp:232-245` | `MeshView_ATetrahedronDerivedBoundaryNamesTetrahedraNotTriangles` (MeshViewTests.cpp:590) | IMPLEMENTED | |
| Exterior iff one tetrahedron; two interior; ≥3 refused by name | `MeshView.cpp:196-216` | `…DrawsNoInteriorFace` (:646); `…RefusesANonManifoldFace` (:836) | IMPLEMENTED | |
| Key is the sorted triple; emitted facet keeps `kTetFaces` winding | `MeshView.cpp:158-180` | `…FacesOutOfTheSolid` (:701); `MeshView_TetFaceWindingsPointAwayFromTheOppositeVertex` (:352) | IMPLEMENTED | |
| Determinism by sorting, never hashing | `MeshView.cpp:183-191, 219-228`; grep `unordered_map|unordered_set` in `src/renderer` → comment only (`MeshView.cpp:183`) | `…IsDeterministicAndAscending` (:763) | IMPLEMENTED | |
| Mesh with no tetrahedra refused | `MeshView.cpp:233-239` | `…RefusesAMeshWithNoTetrahedron` (:809) | IMPLEMENTED | |
| Tet-derived boundary covers the same node triples as the Triangle3 boundary (independent check) | — (test-side set comparison) | `MeshView_ATetrahedronDerivedBoundaryIsTheSameSurfaceAsTheTriangle3Boundary` (:537) | IMPLEMENTED | |
| `elementOfTriangle` returns a Tet4; `source()` tells which | `MeshView.cpp:281-288, 244`; `MeshView.hpp:136` | `…NamesTetrahedraNotTriangles` (:590) | IMPLEMENTED | |
| `volumeBoundaryOf` untouched, still names Triangle3 | `MeshView.cpp:148-154` | `MeshView_VolumeBoundaryDrawsEveryBoundaryFacetAndNoInteriorFace` (:142) | IMPLEMENTED | |
| Reuses `kTetFaces`, `Facet`, `build` | `MeshView.cpp:26-31, 35, 174, 244`; `MeshView.hpp:200-213` | — | IMPLEMENTED | |
| No `RegionId` on the view | `MeshView.hpp:200-203` (`Facet{element, nodes}`) | — | IMPLEMENTED | |
| Three element contour boxes reachable | `StructuralContour.cpp:316-336` consumes the view | `StructuralViz_EveryElementChannelContoursTheBoundaryByTetrahedron` (:5620); `…AnElementContourReachesTheScreenAsManyColours` (:5783) | IMPLEMENTED | |
| `tetrahedraOf` stays selection-only (refuses empty) | `MeshView.cpp:247-252` | `MeshView_InteriorInspectionDrawsFourFacesPerTetrahedron` (:331) | IMPLEMENTED | "No change to the qualified P16 meshing module" not checkable without VCS; the implementation is entirely in `src/renderer/`. |

---

## ADR-047 — Structural commands edit one analysis definition; solver settings become canonical

Decision: one `StructuralAnalysisEditCommand` base with `edit(before)` per subclass; undo/redo assign stored definitions; `CreateStructuralAnalysisCommand` wraps `AddObjectCommand`; `SolverSettings solver{}` joins the definition and `solveStructuralAnalysis` loses its settings parameter; `validate(StructuralLoad)` factored on the existing finiteness helper; forbidden payloads unconstructible; `NodalForceLoad`'s `NodeId` is qualified intent.

| Clause | Code location (file:line) | Test(s) that pin it | Status | Note |
|---|---|---|---|---|
| Classes: `CreateStructuralAnalysisCommand`, base, `Add/Edit/RemoveStructuralLoadCommand`, `Add/Edit/RemoveStructuralRestraintCommand`, `SetSolverSettingsCommand` | `include/bettercad/structural/StructuralCommands.hpp:76, 136, 206, 230, 250, 274, 295, 311, 341` | `StructuralCommand_*` (StructuralCommandTests.cpp:183-1265) | IMPLEMENTED | |
| The class list is complete | Two further commands exist that the ADR does not list: `DeleteStructuralAnalysisCommand` (`StructuralCommands.hpp:103`) and `SetStructuralAnalysisDefinitionCommand` (`:176`) | `StructuralCommand_DeleteAnalysisUndoRestoresTheWholeAnalysis` (:237) | DRIFT | Detail: the ADR's "Decision" code block omits two shipped commands. |
| Base implements execute/undo/redo once; subclass supplies `edit(before)` | `src/structural/StructuralCommands.cpp:151-196`; `StructuralCommands.hpp:160-161` | `StructuralCommand_UndoAllThenRedoAllIsExactAtEveryStep` (:680) | IMPLEMENTED | |
| Failure atomic: before-state recorded only after a successful apply; refused edit never reaches history | `StructuralCommands.cpp:156-170` | `StructuralCommand_ADuplicateLoadIdFailsAtomically` (:283); `…ACommandWhoseApplyFailedCannotBeUndone` (:929); `…AFailedNewCommandLeavesTheRedoBranchIntact` (:655) | IMPLEMENTED | |
| Undo/redo ASSIGN a stored definition, no inversion | `StructuralCommands.cpp:173-196` | `StructuralCommand_RepeatedUndoRedoDoesNotDriftTheStoredValues` (:741) | IMPLEMENTED | |
| Create wraps `AddObjectCommand`; redo keeps the same `AnalysisId` | `StructuralCommands.hpp:95`; `.cpp:70-105`; clone keeps the ID `StructuralAnalysisObject.cpp:40-49` | `StructuralCommand_CreateAnalysisUndoRedoKeepsTheSameAnalysisId` (:183) | IMPLEMENTED | |
| Keyed on `AnalysisId`, which widens implicitly to `ObjectId` | `include/bettercad/core/Id.hpp:126, 161-163` (`operator ObjectId()` for document-object tags); commands convert explicitly `StructuralCommands.cpp:42, 122` | — | IMPLEMENTED | Implicit widening exists; the code chooses the explicit spelling. |
| Edits go through `Document::modifyObject` → `setDefinition`, which validates before assigning and returns `false` for a no-op without moving the revision | `StructuralCommands.cpp:32-44`; `src/structural/StructuralAnalysisObject.cpp:68-81` | `StructuralCommand_ANoOpEditSucceedsAndMovesNoRevision` (:587); `StructuralData_ANoOpEditDoesNotMoveTheAnalysisRevision` (StructuralDataTests.cpp:412) | IMPLEMENTED | |
| No new invalidation mechanism; revision is the staleness signal | `StructuralCommands.hpp:51-57`; no flag/notification in `StructuralCommands.cpp` | `StructuralCommand_EveryPhysicsCommandStalesTheResultAndNotTheMesh` (:1112); `…UndoDoesNotResurrectTheOldResult` (:1228) | IMPLEMENTED | |
| `SolverSettings solver{}` on the definition; `validate(definition)` calls `validate(SolverSettings)` | `include/bettercad/structural/StructuralAnalysisObject.hpp:102-120`; `StructuralAnalysisObject.cpp:13-26` | `StructuralCommand_SolverSettingsUndoRedoIsExact` (:538); `…InvalidSolverSettingsFailAtomically` (:559) | IMPLEMENTED | `SolverSettings` moved to `StructuralData.hpp:319-362` (`StructuralSolve.hpp:84-99` records why). |
| `solveStructuralAnalysis` reads settings from the definition; parameter removed | `StructuralValidation.hpp:653-657`; `src/structural/StructuralValidation.cpp:584-590` | `StructuralValidation_SolvesAWellPosedAnalysisEndToEnd` (StructuralValidationTests.cpp:1178) | IMPLEMENTED | |
| `validate(const StructuralLoad&)` exposed, implemented by the same `isFiniteValue` as `prepareStructuralLoads` | `include/bettercad/structural/StructuralLoad.hpp:224-237`; `src/structural/StructuralLoad.cpp:59, 155-167, 351` | `StructuralCommand_ANonFiniteLoadValueFailsAtomically` (:301) | IMPLEMENTED | |
| `validate(StructuralRestraint)` is record-level, does not consult a mesh | `include/bettercad/structural/StructuralRestraint.hpp:179-186` | `StructuralCommand_AnEmptyRestraintMaskFailsAtomically` (:489) | IMPLEMENTED | |
| Forbidden payloads unconstructible, probed through constructor parameters | `tests/compile_fail/CMakeLists.txt:701-756` (`A_COMMAND_IS_NOT_KEYED_ON_A_DOF_INDEX`, `THE_DEFINITION_HAS_NO_STIFFNESS_MATRIX`, `A_COMMAND_IS_NOT_CONSTRUCTIBLE_FROM_A_RESULT`, …) | the `structcmd` compile-fail group | IMPLEMENTED | |
| `NodalForceLoad` carries a `NodeId` + `MeshStamp` and is allowed; a restraint has no node target | `StructuralLoad.hpp:93-100`; compile-fail `:712-716` (deliberately not probed), `:727-728` `A_RESTRAINT_DOES_NOT_TARGET_A_NODE` | `StructuralLoad_NodalForceIsMeshLocalAndDiesWithItsMesh` (StructuralLoadTests.cpp:755) | IMPLEMENTED | |
| Loads appended; edit in place; order preserved | `StructuralCommands.cpp:242-243, 270-272` | `StructuralCommand_AnEditKeepsTheLoadAtItsOwnPosition` (:831); `…RemoveLoadUndoRestoresItAtItsOriginalPosition` (:368) | IMPLEMENTED | "Three predecessors are requalified" not checkable here. |

---

## ADR-048 — Structural-analysis persistence is one more object type; a nodal force load refuses the save

Decision: `"type": "structural-analysis"` in the existing format, `kDocumentFormatVersion` stays 2, strict unknown-field rejection, `faceNameToJson`, string enum keys, arrays; the reader calls `validate(StructuralLoad)`/`validate(StructuralRestraint)` itself and refuses duplicate ids; order preserved; a `NodalForceLoad` refuses the save atomically and the reader refuses `"nodal_force"` by name.

| Clause | Code location (file:line) | Test(s) that pin it | Status | Note |
|---|---|---|---|---|
| `"type": "structural-analysis"`; serializer pair in `StructuralAnalysisJson.cpp`, declared in `ObjectJson.hpp`, dispatched in `DocumentJson.cpp` in both places | `StructuralAnalysisObject.hpp:142`; `src/io/json/StructuralAnalysisJson.cpp:81-82, 594, 619`; `src/io/json/ObjectJson.hpp:70-72`; `src/io/json/DocumentJson.cpp:74-85, 320-327` | `StructuralPersistence_TheWholeCanonicalIntentRoundTrips` (StructuralAnalysisFileTests.cpp:406) | IMPLEMENTED | |
| `kDocumentFormatVersion` stays at 2 | `include/bettercad/io/DocumentFile.hpp:71` | `MaterialFileTests.cpp:1181` `CHECK(kDocumentFormatVersion == 2)`; `StructuralPersistence_APreP17DocumentLoadsAndSimplyHasNoAnalyses` (:1782) | IMPLEMENTED | |
| Unknown fields rejected by `requireObject` allow-list | `StructuralAnalysisJson.cpp:248-249, 477, 563-564, 621` | `StructuralPersistence_RejectsMalformedStructuralIntent` (:1440) | IMPLEMENTED | |
| Face references through `faceNameToJson`/`faceNameFromJson` | `StructuralAnalysisJson.cpp:215, 220, 268, 470, 488` | `…GeometryReferencesSurviveExactly` (:581); `…AnUnresolvedReferenceIsPreservedAndNotRebound` (:793) | IMPLEMENTED | |
| Enum file keys are strings from tables, never integers or `toString()` | `StructuralAnalysisJson.cpp:95-126, 133-159` | `…EveryAnalysisModeRoundTrips` (:437); `…EveryPersistableLoadKindRoundTrips` (:457) | IMPLEMENTED | |
| Collections are arrays | `StructuralAnalysisJson.cpp:362-372, 506-513` | — | IMPLEMENTED | |
| Reader calls `validate(StructuralLoad)` and `validate(StructuralRestraint)` itself; duplicate `LoadId`/`RestraintId` refused | `StructuralAnalysisJson.cpp:356-358, 500-502, 391-397, 528-534` | `…RejectsDuplicateIdentitiesRatherThanMerging` (:1690) | IMPLEMENTED | |
| `validate(StructuralAnalysisDefinition)` checks mesh handle and solver settings only, "fourteen lines", not extended | `src/structural/StructuralAnalysisObject.cpp:13-26` (exactly 14 lines) | — | IMPLEMENTED | |
| Collection order preserved, not sorted | `StructuralAnalysisJson.cpp:362-372` (no sort) | `…LoadAndRestraintOrderIsTheUsersAndIsPreserved` (:685) | IMPLEMENTED | |
| Writer refuses a `NodalForceLoad`, naming the load, why, and what to do | `StructuralAnalysisJson.cpp:209-244` | `StructuralPersistence_ANodalForceLoadIsRefusedRatherThanWritten` (:1133; body read `:1153-1159`) | IMPLEMENTED | |
| `objectToJson` returns `Result<Json>`; `saveDocument` propagates before opening its temp file; atomic, no file created | `DocumentJson.cpp:81-84`; `src/io/DocumentFile.cpp:9-15`; `src/io/FileIo.cpp:15-35` (temp + rename) | same test, `:1161-1164` `CHECK_FALSE(std::filesystem::exists(file))` | IMPLEMENTED | |
| Reader refuses `"type": "nodal_force"` by name | `StructuralAnalysisJson.cpp:330-337` | `StructuralPersistence_TheReaderRefusesANodalForceLoadByName` (:1176) | IMPLEMENTED | |
| `NodalForceLoad` has zero callers in `apps/` and in `src/` outside its definition | grep `NodalForceLoad` in `apps/` → one comment (`apps/bettercad_cli/StructuralEdits.cpp:46`); in `src/` → `src/structural/StructuralLoad.cpp` only | — | IMPLEMENTED | |
| No solve output reachable from the writer's signature; definition has five members, none derived | `StructuralAnalysisJson.cpp:594` `structuralAnalysisToJson(const StructuralAnalysis&)`; `StructuralAnalysisObject.hpp:45-124` | `…NoSolverStateReachesTheFile` (:932); `…TheFileIsIdenticalWhateverTheSolveHasDone` (:990); `…TheFileDoesNotFollowTheMeshOrTheResultSize` (:1046) | IMPLEMENTED | |
| Reader takes `(Json, name, path)` — no `Document`, `Regenerator`, `Mesher` | `StructuralAnalysisJson.cpp:619-620` | `…TheFileNeedsNoSolverOrMesherToBeInterpreted` (:1810) | IMPLEMENTED | |
| Solver settings always written (all three) and all required on read; unknown algorithm refused by name | `StructuralAnalysisJson.cpp:553-559, 561-590` | `…SolverSettingsRoundTripExactly` (:557); `…RejectsInvalidSolverSettingsRatherThanDefaulting` (:1620) | IMPLEMENTED | |
| Hand-written `"loads": []` accepted, normalises away (omitted when empty) | `StructuralAnalysisJson.cpp:601-614, 647-660` | `…AnExplicitEmptyCollectionNormalisesThenIsByteStable` (:1289) | IMPLEMENTED | |
| Byte-identity across no-result / current / stale / failed-solve states | — (test-side) | `…TheFileIsIdenticalWhateverTheSolveHasDone` (:990); `…SaveLoadSaveIsByteIdentical` (:1263) | IMPLEMENTED | Test bodies not read. |

---

## Summary

| ADR | clauses checked | implemented | drift | not found |
|---|---|---|---|---|
| ADR-034 | 8 | 7 | 1 | 0 |
| ADR-035 | 9 | 8 | 1 | 0 |
| ADR-036 | 11 | 9 | 2 | 0 |
| ADR-037 | 16 | 15 | 1 | 0 |
| ADR-038 | 17 | 14 | 3 | 0 |
| ADR-039 | 17 | 17 | 0 | 0 |
| ADR-040 | 18 | 18 | 0 | 0 |
| ADR-041 | 13 | 13 | 0 | 0 |
| ADR-042 | 16 | 15 | 1 | 0 |
| ADR-043 | 12 | 11 | 1 | 0 |
| ADR-044 | 8 | 8 | 0 | 0 |
| ADR-045 | 16 | 16 | 0 | 0 |
| ADR-046 | 12 | 12 | 0 | 0 |
| ADR-047 | 15 | 14 | 1 | 0 |
| ADR-048 | 18 | 18 | 0 | 0 |
| **Total** | **206** | **195** | **11** | **0** |

Not counted above because they cannot be checked from the tree alone: ADR-035 "447 files, 0 violations"; ADR-042 "no meshing source file changed"; ADR-046 "no change to the qualified P16 meshing module"; ADR-047 "three predecessors are requalified"; ADR-041/042 threshold *measurements* (the numbers are in the code; the sweeps are evidence documents).

---

## DRIFT and NOT FOUND findings (all DRIFT; nothing NOT FOUND)

Every finding is a text-versus-code disagreement. None changes a numerical result; the load-bearing invariants (identity domains, derived-vs-canonical, Voigt order, engineering shear, mu on the shear diagonal, pivot gate, CSR representation, single m→mm conversion of the kernel, save refusal) are all IMPLEMENTED above.

**F1 — ADR-034 (Verification): a milestone-scoped statement is now false.**
ADR says: "no element, no DOF, no stiffness and no solve exists in src/structural/ -- this milestone's module is the input boundary and nothing else".
Code: `src/structural/CMakeLists.txt:47-62` compiles `Tet4Element.cpp`, `StructuralDof.cpp`, `StructuralSystem.cpp`, `StructuralSolve.cpp`.
Same category as P16-QUAL-001's ADR-031 finding: true at the date, never scoped as "as of P17-ARCH-001".

**F2 — ADR-035 (Rationale): the respace was not propagated to `ARCHITECTURE.md`.**
ADR says: "The numbers appear in exactly one authoritative place — the checker — plus prose in `ARCHITECTURE.md` and the module `CMakeLists.txt` comments."
Docs: `docs/ARCHITECTURE.md:35-37` still reads "Layers: `core` 0, `sketch` 1, `features` 2, `assembly` 3, `drawing` 4, `io` 5, `renderer`/`scripting` 6"; the targets table `:48-51` still says "(layer 1)…(layer 4)" and has no `meshing` or `structural` row. The checker (`CheckLayering.cmake:46-77`) and `src/structural/CMakeLists.txt:1-15` are correct.

**F3 — ADR-036 (Verification): case count.**
ADR says: "`tests/structural/StructuralAnalysisBoundaryTests.cpp`, nine cases".
Code: ten `TEST_CASE`s at `:126, 160, 174, 182, 233, 257, 317, 336, 348, 365`.

**F4 — ADR-036 (Decision, "What possession does NOT prove"): forecast not realised.**
ADR says: "Those gates [quality, loads, restraints, sufficiency] are added to this function by the milestones that define their data. The type is where they land".
Code: `requireStructuralModel` (`src/structural/StructuralAnalysis.cpp:39-121`) still performs only the seven `InputProblem` checks; the four gates live in `validateStructuralAnalysisForSolve` (`src/structural/StructuralValidation.cpp:366-513`), which ADR-042 Decision 4 chose explicitly. ADR-042 records the choice; ADR-036 was not amended.

**F5 — ADR-037 (Consequences): "never stores an index".**
ADR says: "`P17-BC-001` produces `NodalDof`s from CAD references and never stores an index."
Code: `include/bettercad/structural/StructuralConstraints.hpp:153` `std::vector<DofIndex> constrained;` on `RestraintResolution`, filled at `src/structural/StructuralConstraints.cpp:237, 259`. It is derived state and never persisted (hpp `:146-148`), added by ADR-041 Decision 3, which says so; ADR-037's sentence is now false as written.

**F6 — ADR-038 (Decision) and `StructuralSystem.hpp`: "links no linear algebra".**
ADR says: "`bettercad_structural` continues to link no linear algebra at all."
Code: `src/structural/CMakeLists.txt:67-68` `PRIVATE_LINK BetterCAD::eigen` (ADR-039). The public header comment `include/bettercad/structural/StructuralSystem.hpp:10-14` still says "`bettercad_structural` still links none, which keeps the structural Eigen admission `P17-SOLVE-001`'s decision".

**F7 — ADR-038 (Option C / Decision) and `StructuralSystem.hpp`: the solver does not `Eigen::Map` the CSR.**
ADR says: "`P17-SOLVE-001` wraps the same arrays in `Eigen::Map<const Eigen::SparseMatrix<double>>` at zero copy".
Code: `src/structural/StructuralSolve.cpp:256-277`: "A Map would need Eigen's own signed StorageIndex and BetterCAD's index is unsigned 64-bit, so this is a conversion rather than a view — stated plainly rather than claimed as zero copy" — it builds `Eigen::Triplet`s and calls `setFromTriplets`. `StructuralSystem.hpp:15-16` still promises the Map. (Correctness is unaffected: the CSR holds each pair once, so `setFromTriplets`' unspecified reduction order has nothing to reduce, `:273-276`.)

**F8 — ADR-038 (Context): "no `Eigen::Triplet` anywhere in the tree".**
ADR says: "There is no `Eigen/Sparse`, no `Eigen::SparseMatrix` and no `Eigen::Triplet` anywhere in the tree."
Code: `src/structural/StructuralSolve.cpp:28-29, 261, 265`. Stale context after ADR-039.

**F9 — ADR-042 (Decision 4): entry-point signature.**
ADR says: "`solveStructuralAnalysis(document, regenerator, mesher, analysis, settings)`".
Code: `include/bettercad/structural/StructuralValidation.hpp:653-657` `solveStructuralAnalysis(document, regenerator, mesher, analysis, const Point3D& origin = Point3D{}, const EquilibriumTolerance& tolerance = {})`; settings come from `definition.solver` (`StructuralValidation.cpp:584-590`). ADR-047 records the removal of `settings`; nothing records the `origin`/`tolerance` parameters, and ADR-042 was not amended. Related header inconsistency for the qualifier: `StructuralValidation.hpp:649-652` says `origin` "is explicit because … a mutation proved a default of zero made an origin-ignoring path a no-op", yet `:656` gives it the default `Point3D{}`.

**F10 — ADR-043 (Context / Consequences): "the project's only metres-to-millimetres conversion".**
ADR says: "`toModelLength` in `OcctMeshPresentation.cpp` is the project's only metres-to-millimetres conversion."
Code: a second, identical wrapper `toModelLength` exists at `src/renderer/occt/OcctOverlayPresentation.cpp:23-25` (added for ADR-045). Both delegate to `geometry::occt::toModel`, so the kernel conversion is still single, but the sentence as written is no longer true and the duplicate wrapper is exactly the "two call sites agreeing" shape the ADR argues against.

**F11 — ADR-047 (Decision): class list incomplete.**
ADR says the command layer is `CreateStructuralAnalysisCommand`, `StructuralAnalysisEditCommand`, and the seven listed subclasses.
Code: `include/bettercad/structural/StructuralCommands.hpp:103` `DeleteStructuralAnalysisCommand` and `:176` `SetStructuralAnalysisDefinitionCommand` also ship (tested by `StructuralCommand_DeleteAnalysisUndoRestoresTheWholeAnalysis`, `StructuralCommandTests.cpp:237`).

### Observations outside the ADR texts (for P17-QUAL-001, not counted)

- `tests/structural/StructuralValidationTests.cpp:299` is named `…ExactlyTwoAreWarnings` but asserts `warnings == 3` (`:308`); the header `StructuralValidation.hpp:284-285` says three. Stale test name.
- `cmake/BetterCADDependencies.cmake:50` still describes Eigen as "sketch solver; private to bettercad_sketch"; three modules now link it.
- `include/bettercad/structural/StructuralReaction.hpp:433-434` says the constrained set is "compared element by element"; `src/structural/StructuralReaction.cpp:306-320, 477-486` compares the count plus per-resolution membership (the `SolvedSystem` retains no constrained list to compare against).

Totals: 206 clauses checked across 15 ADRs; 195 IMPLEMENTED, 11 DRIFT, 0 NOT FOUND.
