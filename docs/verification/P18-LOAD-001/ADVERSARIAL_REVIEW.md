# P18-LOAD-001 — adversarial review

```text
SUBJECT:  an attempt to disprove the thermal heat loads before any matrix or
          boundary condition is built on them
QUESTIONS: 34 from the brief (section 169), plus 10 of the reviewer's
MUTATIONS: 46 distinct, 45 KILLED, 1 retired as dead code, 0 survived, 0 void at
           the end -- 63 runs (see MUTATIONS.md)
FINDINGS:  9 (two defects in the first version of the production code, both
           found by mutation and both fixed before the freeze; none found by the
           brief's questions themselves)
GATE-BLOCKING: 0
```

Each answer below was obtained by running something or by reading the code.

## Findings

**F1 — a dead safety check in the production code (fixed).** The first version
ended with a scan that refused a non-finite entry anywhere in the load vector.
Mutation L27 deleted it and no test could tell. Each share is checked finite
before it is added and the addition is refused when the sum overflows, so no
entry can become non-finite and the scan is unreachable. A check that cannot
fire reads as protection it does not give; it was deleted, and the argument for
why the vector is finite by construction sits where the scan used to be.

**F2 — an overflow gate that no input could reach (production defect; fixed).**
The share was formed as `(q'' A) / 3`. Any input large enough to overflow the
running sum had already overflowed the product, so the "superposing ... overflowed"
gate was unreachable. L41 (gate removed) survived for that reason. The shares are
now `q'' (A/3)` and `q''' (V/4)`, which is the same arithmetic one rounding
apart and moves the overflow to where the sum can be hit. A 1e308 flux on a
rectangle exercises it, and L41b and L42b are killed. Nothing in a normal range
changes: the difference between the two orders is below the 1e-14 tolerance.

**F3 — the order "value, then target" had no test (L26b; test added).** A
non-finite value is reported as a bad number and not as a bad face.
`ValuesAreCheckedBeforeTargetsAreResolved` pins it.

**F4 — the volumetric seam's uniqueness check had no test (L18; test added).**
`RepeatedAndMisorderedTetrahedraAreRefused`.

**F5 — the milestone's own tests misused the model they test (test bug; fixed).**
`ThermalModel` borrows the mesher's mesh. After a remesh the *old* model sees the
*new* mesh, so a test that kept `m1` and compared it with `m2` compared the new
mesh with itself. Four cases failed on the first run, for three separate reasons:
this one, F6 and F7. The tests now capture the old stamp, numbering and prepared
loads **by value** before remeshing. This is not a production defect: a model
that cannot outlive a remesh is what makes possession of one a proof that the
mesh is current.

**F6 — two "different" boundary meshes were identical (test bug; fixed).** The
coarse and fine cylinder controls first produced the same boundary mesh, so a
"remesh" test would have compared a mesh with itself (brief attack 33). The
fine control now also tightens the surface deflection (`linearDeflection` 5e-6
m, `angularDeflection` 0.0872664626 rad) and the test **asserts** the counts
differ: 156 against 900 nodes, 605 against 4118 tetrahedra, 140 against 572
boundary facets, 34 against 142 facets on the cap.

**F7 — signed volumetric cancellation is not bit-exact (test method; stated, not
loosened).** `+q''' V` and `-q''' V` over twelve tetrahedra cancel to 0.83 eps of
the entries, not to zero, because `a+b+c-a-b-c` rounds differently across the
elements that meet at a node. The assertion is made in eps units with a bound
of 8. The surface cancellation is exact (two terms per node) and is asserted
`== 0`.

**F8 — two failure values are reachable only through the integration seams
(limitation; recorded).** P16 never hands an empty facet set for a face that
resolved, and a `ThermalModel` cannot exist for a mesh with no tetrahedra, so
`TargetWithoutFacets` and `BodyWithoutElements` cannot be produced from a
document. They are tested through the seams and recorded as defensive values.

**F9 — a drilled hole's cylindrical wall cannot be named (limitation inherited
from P16; recorded).** `FaceRole` has no role for it. A load cannot target it
and nothing here works around that by persisting a mesh facet.

**Equivalent mutants considered and not run:** reordering the three corner
additions of one facet; summing per-load totals in another order (see
MUTATIONS.md).

## The brief's attacks

| # | Attack | Expected protection | Observed | Test / mutation | PASS |
| --- | --- | --- | --- | --- | --- |
| 1 | `q''` stored in W/mm² but labelled W/m² | `HeatFlux` is an SI quantity; no conversion in the source | none stored; scaling the geometry by 1e-3 scales the heat by 1e-6, not 1 | `UnitsContract`, `SurfaceGeometryScaling`; compile-fail A_HEAT_FLUX_IS_NOT_A_DOUBLE; L09b | ✓ |
| 2 | `q'''` stored in W/mm³ | likewise | none stored; the same scaling gives 1e-9 | `UnitsContract`, `VolumeGeometryScaling`; L10b | ✓ |
| 3 | positive heat-flux sign disagrees between core and the balance | one stated convention: positive = into the body | `+q''` adds `+Q` at each corner; a negative flux extracts | `SignConventionIsPositiveIntoTheBody`; SIGN_CONVENTION.md; L06b | ✓ |
| 4 | facet normal orientation flips a scalar load | the area is unsigned, no normal is read | reversing a facet's winding leaves every entry bit-identical | `FacetWindingDoesNotChangeTheHeatInput` | ✓ |
| 5 | `q''A` per node instead of `q''A/3` | share is `q'' (A/3)` | a 3 m² triangle at 12 W/m² gives 12 W per corner and 36 W total, exactly | `SingleTriangleHeatFluxAnalytical`; L01b | ✓ |
| 6 | divide by 4 on a triangle | likewise | 15 cases fail | L02b | ✓ |
| 7 | `q'''V/3` instead of `/4` | share is `q''' (V/4)` | unit tet at 24 W/m³: 1 W per corner, 4 W total | `UnitTetGenerationAnalytical`; L03b | ✓ |
| 8 | total nodal heat is not `q''A` | the shares sum to `q'' A` | agrees to 1e-13 on the mapped end cap (0.6 W exact on the block) | `SurfaceHeatRateConserved`; L15, L43 | ✓ |
| 9 | total generation is not `q'''V` | the shares sum to `q''' V` | block: 2.4e-5 m³ at 2.5e6 W/m³ is 60 W, exact | `VolumeHeatRateConserved`, `MultiTetBodyConservesTheHeatRate`; L16, L44 | ✓ |
| 10 | a shared node's contributions overwrite | `+=` | two facets or tets sharing a node add | `SurfaceSharedNodeAccumulates`, `SharedVolumeNodeAccumulates`; L11 | ✓ |
| 11 | `NodeId.value()` indexes the load vector | `ThermalDofMap::dofOf` is the only index | nodes 7, 104, 9001, 500000 land on ordinals 0..3 | `SparseNodeIdsUseThermalDofMap`; L12b (38 cases) | ✓ |
| 12 | the largest `NodeId` sizes the vector | `dofCount()` sizes it | a node id of 4 294 967 294 on a three-node mesh gives a vector of three entries (24 bytes) | `LargeSparseNodeIdDoesNotSizeTheVector`; L13 (30 cases) | ✓ |
| 13 | a canonical load persists facet ids | the load holds a `FaceName` and a flux | `sizeof` mirror and `static_assert`; compile-fail A_NODE_LIST_IS_NOT_A_FACET_LIST, THE_SURFACE_LOAD_HAS_NO_FACET | `CanonicalGeometryReferenceIsAFaceNameAndNothingMeshLocal`; L46 | ✓ |
| 14 | a canonical load persists `NodeId`s | none present | likewise (THE_SURFACE_LOAD_HAS_NO_NODE) | same; L46 | ✓ |
| 15 | a volume load persists `ElementId`s | none present | THE_VOLUMETRIC_LOAD_HAS_NO_ELEMENT | same | ✓ |
| 16 | remesh with the same counts reuses the old prepared load | the source holds the mesh **stamp** and the node count | two builders with identical counts differ in stamp; the load is stale for reason mesh | `SameCountsNewMeshIsStale`; L30 | ✓ |
| 17 | remesh with the same `NodeId`s reuses it | likewise | same ids, new stamp: stale | `SameIdsNewMeshIsStale`; L29 | ✓ |
| 18 | a stale prepared load reaches assembly | `requireCurrent` is the gate assembly must call | refused with the stale reasons; assembly does not exist yet, so the gate is a function and not a wall | `RemeshInvalidatesPreparedLoad`, `PreparedLoadBindsExactMeshSource`; L31 to L35 | ✓ (see limitation) |
| 19 | an unresolved reference binds the nearest face | P16 answers, nothing is substituted | `HoleBottom`, an unknown object and a foreign object give `TargetUnresolved`, even with both caps present | `NoNearestFaceRebind`, `UnresolvedReferenceFailsAndBindsNothing`; L37 to L39 | ✓ |
| 20 | an empty mapped target silently gives zero heat | refused | `TargetWithoutFacets` and `BodyWithoutElements` | `EmptyTargetIsNotAZeroLoad`; L20, L21 | ✓ (seam only, F8) |
| 21 | an unsupported target is silently ignored | refused | a malformed selector is `TargetInvalid`; an unnameable face cannot be written | `MalformedSelectorFails`; GEOMETRY_REFERENCE.md | ✓ |
| 22 | one invalid load is dropped while the rest proceed | all or nothing | a bad second load refuses the set and publishes no vector | `OneInvalidLoadRefusesTheWholeSet`, `AFailedPreparationPublishesNothing` | ✓ |
| 23 | preparation rewrites the canonical target into mesh ids | read-only | the load list, document, mesh and mapping compare equal before and after | `PreparationDoesNotMutateCanonicalOrDerivedState` | ✓ |
| 24 | a negative heat extraction is `abs()`-corrected | no `abs` in the kernel | a negative flux gives a negative entry; zero stays zero | `ZeroAndNegativeValuesArePreservedExactly`, `SignedCancellation`; L07b, L08b | ✓ |
| 25 | NaN passes a sign or magnitude check | finiteness is tested first | NaN and both infinities are refused with the load named | `RejectsNonFiniteHeatFlux`, `RejectsNonFiniteGeneration`; L26c | ✓ |
| 26 | surface area uses viewer-mm coordinates | `Point3D` is metres | the mesh area is read in m² | `SurfaceGeometryScaling`; L09b | ✓ |
| 27 | tet volume uses mm³ | likewise | read in m³ | `VolumeGeometryScaling`; L10b | ✓ |
| 28 | `abs(V)` on an inverted tet | the signed volume of a validated element | an inverted or zero-volume tet is refused, not corrected | `ZeroVolumeAndInvertedTetFail`; L40, L23 | ✓ |
| 29 | a material change alters the conversion | the loads do not read the material | a conductivity edit leaves the prepared load current and bit-identical; the thermal **result** goes stale on material alone | `MaterialEditDoesNotInvalidatePreparedGeometryTarget`, `VolumeGenerationIsMaterialIndependent`; L45 | ✓ |
| 30 | LOAD implements convection | out of scope | no convection symbol; source audit; compile-fail THE_PREPARED_LOAD_HAS_NO_CONVECTION | `PreparationIsIndependentOfBoundaryConditionsAndMaterial`; L28b | ✓ |
| 31 | preparation depends on solver settings | the loads list is the only input | a solver tolerance and a boundary condition leave the prepared load current | `SolverAndBoundaryConditionEditsDoNotInvalidate`, `DocumentLoadsOnlyAreRead` | ✓ |
| 32 | multiple loads depend on unordered iteration | no unordered container | audit: 0 matches; load order moves only the summation order | `LoadOrderOnlyAffectsTheSummationOrder`, `RepeatedPreparationIsBitIdentical` | ✓ |
| 33 | coarse and fine meshes are identical but called a remesh | asserted different | node, tet, boundary-facet and cap-facet counts all differ (F6) | `RemeshReResolvesGeometryReference` | ✓ after the fix |
| 34 | zero targeted tests still report PASS | per-group minimums | nine filters, each with a minimum equal to the observed count; `verify-harness.cmd` PASS | ZeroMatchGuard.cmake | ✓ |

## The reviewer's own attacks

| # | Attack | Observed | Test | PASS |
| --- | --- | --- | --- | --- |
| R1 | a model kept across a remesh sees the new mesh | it does, by design; possession of a model proves the mesh is current | F5; `RemeshInvalidatesPreparedLoad` | ✓ |
| R2 | an edit that moves only the geometry revision | the prepared load is stale for reason geometry | `GeometryEditStalesPreparedLoad`, `EditStalesPreparedLoad` | ✓ |
| R3 | the same load list under a different body | stale for reason body | `ADifferentBodyIsStale`; L32 | ✓ |
| R4 | a load list with a repeated load id | refused before any integration | `DuplicateLoadIdsAreRefused`; L25 | ✓ |
| R5 | a numbering built for another mesh | refused with `DofMapMismatch`, no vector written | `ANumberingForAnotherMeshIsRefused`; L14b | ✓ |
| R6 | a repeated or out-of-order facet or element list counts twice | refused | `RepeatedAndForeignEntitiesAreRefused`, `RepeatedAndMisorderedTetrahedraAreRefused`; L17, L18 | ✓ |
| R7 | a share or a running sum overflowing to infinity | refused, naming the gate that fired | `NonFinitePreparedContributionFails`; L41b, L42b | ✓ |
| R8 | a lookup of a heat input for an unknown the map does not have | answers only for unknowns it has | `SparseLookupByUnknownAnswersOnlyForUnknownsItHas` | ✓ |
| R9 | a failed preparation leaves a partly written vector | atomic: nothing is published | `AFailedPreparationPublishesNothing` | ✓ |
| R10 | a drilled hole's wall as a target | cannot be written (F9) | GEOMETRY_REFERENCE.md | recorded |

## Cross-preset

The brief's six cross-preset fixtures (surface analytical, volume analytical,
P16 mapped face, remesh, unresolved-reference failure, multi-load superposition)
were compared on each preset's own binary after the qualifying run: the suite
passed 66 of 66 and 21 588 assertions in all three, the two 64-bit bit
fingerprints are identical (hand cases 3063bb8f8518f832, real block
9a0ad0769a0a552f), and the 42 measurement lines the suite prints are identical
after sorting, with 0 differences. Discrete semantics match: the same problem
values, the same stale reasons, the same counts. See DETERMINISM.md.

## Verdict

No gate-blocking finding. Two defects in the first version of the production
code (F1, F2) were found by mutation, not by the brief's questions, and are
fixed. The two limitations (F8, F9) are inherited from P16's mapping and are
recorded rather than worked around.
