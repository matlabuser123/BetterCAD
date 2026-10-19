# P18-BC-001 — adversarial review

```text
SUBJECT:  an attempt to disprove the thermal boundary conditions before any
          matrix is assembled from them
QUESTIONS: 40 from the brief (section 180), plus 9 of the reviewer's
MUTATIONS: 58 distinct, 58 KILLED, 0 survived, 0 void at the end -- 65 runs
           (see MUTATIONS.md)
FINDINGS:  7 (one production defect, found by the first qualification run and
           fixed; one test gap closed; one policy divergence from the brief's
           recommendation recorded for the owner)
PRODUCTION DEFECTS: 1 (a missing DLL export annotation, found by the shared-library
                    preset and by nothing else)
GATE-BLOCKING: 0
```

Each answer below was obtained by running something or by reading the code. No
production LOGIC changed after the first mutation batch began; the tests gained two
overflow-order assertions between batches, and the one production edit, a DLL export
annotation (F7), came after the freeze and is described below.

## Findings

**F1 — the order of the division was not tested (test gap; fixed).** The
convection entries are formed as `h * (A / 12)` and `(h * (A / 3)) * T_inf` so a
product overflows only when the entry does. Probe B25, which forms the source as
`(h T_inf A) / 3`, survived the first batch because no input separated the two
orders. Two inputs now do (`h A = 3e308` and `h T_inf A = 3e308`, each with a
perfectly representable entry), and B25b and its matrix sibling B53 are killed.

**F2 — six probes did not compile (method; reformulated).** B14, B15, B16, B26,
B27 and B28 left a variable or a parameter unused, which `-Werror` refuses. Each
was reformulated with the name still used and rerun; all six were killed.

**F3 — a test of mine could not fire (test bug; fixed).** The first overflow case
asked for a matrix overflow on a 1.2e-3 m² cap, which would need `h` above the
largest double. It now uses the source gate on the real cap and a 100 m²
kernel triangle for the matrix gate.

**F4 — the brief's synthetic numbering is not constructible (limitation;
redesigned).** `MeshBuilder` demands strictly increasing node handles, so
`NodeId` 7 cannot follow 900 and an unknown is always the rank of its handle.
The fixtures use 7, 11, 42, 900 in the only order allowed and still show that no
unknown equals its handle. Order independence of the constraint seam is tested
over every permutation of its requests instead.

**F5 — `h = 0` stays refused canonically (policy divergence; owner decision).**
The brief recommended allowing it. P18-DATA-001's committed rule is finite and
strictly positive, with a mutation of its own (`h = 0` accepted) that its tests
kill, and changing it would reopen a qualified milestone. The kernel and the
integration seam are total over `h >= 0` and return zero, so the mathematics the
brief asks for at `h = 0` is implemented and tested. The one-line canonical change
is described in BC_MODEL.md.

**F6 — three limits, recorded.** Inherited from P16: a drilled hole's cylindrical
wall has no `FaceRole` and cannot be a target. Reachable only through the seams:
`TargetWithoutFacets` (P16 never hands an empty set for a face that resolved) and
a stale input to a function the assembler has yet to call: `isCurrent` and
`requireCurrent` are a function and not a wall until P18-ASSEMBLY-001 makes them
one.

**F7 — a struct with out-of-line members was not exported (PRODUCTION DEFECT;
fixed).** The first qualification run passed debug-ext and release-ext and then
failed the debug-shared-ext BUILD: `undefined reference to
bettercad::thermal::PreparedConvectionBoundary::area() const`. The type is a plain
`struct` whose three accumulating members (`area`, `conductance`, `source`) are
defined in `ThermalBoundary.cpp`; on Windows a shared build exports a symbol only
if it is annotated, and the struct carried no `BETTERCAD_THERMAL_EXPORT`. A static
build links it without one, which is why the 92 test cases, 58 mutation probes and
two of three presets all passed. **Root cause:** the annotation was added to the
classes and free functions and forgotten on the one struct with member functions
defined in the library. **Fix:** the annotation, with a comment saying why. **Why
nothing earlier could see it:** every non-shared preset ignores the macro; only the
shared preset links across a DLL boundary. **Verification:** the shared test binary
now links and the 92 cases pass in it; the full three-preset qualification was rerun
from the start on the corrected tree (run 1 is preserved, not counted). **Regression
protection:** the shared preset IS the test; the audit that no other struct in the
header has an out-of-line member is in the header's comment. The mutation evidence
was re-established on the final text for a representative sample (MUTATIONS.md): the
change is an export annotation and cannot alter a probe's verdict, but it was not
taken on trust.
**Equivalent mutants considered and not run:** reordering a facet's three local
sums; row-major against column-major storage of a symmetric matrix.

## The brief's attacks

| # | Attack | Expected protection | Observed | Test / mutation | PASS |
| --- | --- | --- | --- | --- | --- |
| 1 | prescribed temperature stored in Celsius | kelvin only; one rule | 350 K stays 350, never 76.85 | `PrescribedTemperatureDefinition`, `StoresNonzeroKelvin`; B02 | ✓ |
| 2 | 350 K becomes zero (P17 restraint logic reused) | a constraint carries a value | 350 K at the end of the chain; a free node has no temperature, not 0 K | `StoresNonzeroKelvin`; B01 | ✓ |
| 3 | duplicate boundary nodes give duplicate equations | one constraint per unknown | six corners, four constraints | `PrescribedTemperatureDeduplicatesNodes`, `MappedFaceDirichletMapsUniqueNodesOnce`; B03 | ✓ |
| 4 | conflicting temperatures: first wins | refused | refused, nothing accepted | `ConflictingTemperatureRejected`; B04 | ✓ |
| 5 | conflicting temperatures: last wins | refused | refused | same; B05 | ✓ |
| 6 | conflicting temperatures averaged | refused | refused | same; B06 | ✓ |
| 7 | an overlap conflict along one edge goes undetected | every unknown examined | end cap 350 K and a side wall 300 K disagree at exactly the shared nodes, counted | `PartialSurfaceOverlapConflictRejected` | ✓ |
| 8 | identical overlap treated as a conflict | exact equality | 300 K and 300 K deduplicate to the union, shared nodes attributed to both | `IdenticalOverlapAlongAnEdgeDeduplicates`, `DuplicateIdentical...` | ✓ |
| 9 | additive convection deduplicated | additive, never merged | two identical conditions give exactly twice | `ConvectionSuperposition...`; B35 | ✓ |
| 10 | Robin + Dirichlet overlap without a policy | stated policy, tested | both kept; convection bit-identical with and without the temperature | `DirichletAndRobinOverlapPolicy` | ✓ |
| 11 | `h` negative | refused | refused by kernel, seam and preparation | `NegativeConvectionCoefficientRejected`; B28b | ✓ |
| 12 | NaN `h` passes because `h < 0` is false | negated comparisons and `isfinite` | refused | `NaNConvectionCoefficientRejected`; B29 | ✓ |
| 13 | `T_inf` nonfinite | the one temperature rule | refused, even beside `h = 0` | `NaNAmbient...`, `InfiniteAmbient...`, `ZeroConvectionCoefficientPolicy`; B30, B40 | ✓ |
| 14 | the Robin matrix omits its source | both terms always | both, in one kernel | residual tests; B16b | ✓ |
| 15 | convection implemented as source only | both terms | the constant-ambient residual is zero only with both | `ConvectionAmbientEquilibrium`; B54 | ✓ |
| 16 | `Qconv` sign reversed | derived (WEAK_FORM.md) | above/below ambient residual ±600 W | `ConvectionAboveAmbientHeatLoss`, `...BelowAmbientHeatGain`; B17 | ✓ |
| 17 | `Kconv` sign reversed | derived | the whole-matrix oracle | `SingleTriangleConvectionMatrixAnalytical`; B18 | ✓ |
| 18 | `Kconv` multiplied by `k` | no conductivity argument | the kernel has none; a scaled matrix fails the oracle | `ConvectionDoesNotInvolveConductivity`, `ConvectionIsIndependentOfTheMaterial`; B19 | ✓ |
| 19 | one-point centroid quadrature | closed form | `hA/6` and `hA/12`, not `hA/9` | `ConvectionRejectsCentroidMatrixMutation`; B14b | ✓ |
| 20 | silent diagonal lumping | closed form | off-diagonals positive | `ConvectionIsNotLumped`; B15b | ✓ |
| 21 | row-sum tests pass while the matrix is wrong | whole-matrix comparison | the wrong matrices have the right row sums and are rejected | the two trap tests | ✓ |
| 22 | triangle area uses mm² | `Point3D` is metres | `s^2` scaling at five scales | `ConvectionAreaScaling`; B22 | ✓ |
| 23 | reversed triangle order flips a sign | unsigned area, no normal | bit-identical global terms | `TriangleWindingDoesNotChangeTheTerms` | ✓ |
| 24 | shared-node Robin contributions overwrite | every facet's term kept for the assembler to add | every facet kept; in the test-side accumulation a shared diagonal is 4, not 2 | `SharedFacetNodeContributionsSum`; B55, B56 | ✓ (see MUTATIONS.md) |
| 25 | `NodeId.value()` indexes the constraint storage | `ThermalDofMap::dofOf` | no unknown equals its handle (7 to 0, 900 to 3) | `SparseNodeIdsUseThermalDofMap`; B57 | ✓ |
| 26 | the largest `NodeId` sizes the constraint map | storage by unknown | a node id of 4 294 967 294 on 3 nodes gives 3 entries | `LargeSparseNodeIdDoesNotSizeConstraintStorage` | ✓ |
| 27 | a canonical BC persists facet IDs | a face and numbers only | `sizeof` mirrors; compile-fail THE_..._HAS_NO_FACET | `CanonicalGeometryReference...`; B51 | ✓ |
| 28 | a canonical BC persists `NodeId`s | likewise | THE_..._HAS_NO_NODE | same | ✓ |
| 29 | a canonical BC persists `ThermalDofIndex`es | likewise | THE_..._HAS_NO_UNKNOWN | same | ✓ |
| 30 | remesh with the same counts reuses the old prepared BC | source = stamp + count | 9/12/12 in both generations, still stale | `SameCountsNewMeshIsStale`; B44 | ✓ |
| 31 | remesh with the same ids reuses it | likewise | every id identical, stale by stamp alone | `SameIdsNewMeshIsStale`; B43 | ✓ |
| 32 | an unresolved reference binds the nearest face | P16 answers, nothing substituted | hole bottom, counterbore, spotface, unknown object refused with both caps present | `NoNearestFaceRebind`, `UnresolvedGeometryReferenceFails`; B41, B42 | ✓ |
| 33 | an empty mapped target silently succeeds | refused | `TargetWithoutFacets` from both seams | `EmptyMappedTargetFails`; B09, B10 | ✓ |
| 34 | one invalid BC is dropped while the rest proceed | all or nothing | a bad middle condition refuses the set | `OneInvalidConditionRefusesTheWholeSet`, `AFailedPreparationPublishesNothing`; B58 | ✓ |
| 35 | a material edit remeshes or rebinds | no material in the source | mesh `Current`, prepared boundary current and bit-identical, result stale for `Material` only | `MaterialEditKeepsPreparedBoundaryMappingCurrent`; B50 | ✓ |
| 36 | load edits alter BC mapping | loads are not an input | a load, the mode, the initial condition and the time step leave it current | `LoadSolverAndTransientEditsDoNotInvalidate` | ✓ |
| 37 | solver settings alter BC mapping | not an input | a tolerance edit leaves it current | same | ✓ |
| 38 | a penalty sneaks into preparation | exact constraints only | no penalty, matrix or right-hand side in the type or the source | `PreparedBoundaryHasNoPenaltyAssemblyOrSolve`, compile-fail group; B52 (representative) | ✓ |
| 39 | a set with no Dirichlet BC is rejected | valid at this layer | convection-only and empty sets prepare | `NoDirichletBoundarySetIsValid`; B13 | ✓ |
| 40 | zero targeted tests still report PASS | per-group minimums | fifteen filters, each with a minimum equal to the observed count; `verify-harness.cmd` PASS | `ZeroMatchGuard.cmake` | ✓ |

## The reviewer's own attacks

| # | Attack | Observed | Test | PASS |
| --- | --- | --- | --- | --- |
| R1 | a request names an unknown the numbering does not have | `EntityNotInMesh`, never clamped or skipped | `SeamRefusalsOfTheConstraintMap` | ✓ |
| R2 | the conflict report depends on the order the conditions were listed in | identical under all 120 permutations; ordered by condition id | `ConflictReportIsSummarisedAndOrderIndependent`, `DiagnosticsAreDeterministic` | ✓ |
| R3 | one id used for a temperature and a convection | `DuplicateConditionId`, checked before values | `IdentitiesAreCheckedFirst`; B36 | ✓ |
| R4 | a numbering built for another body | `DofMapMismatch`, no constraint written | `ADifferentBodyIsStale`, `ANumberingForAnotherMeshIsRefused`; B12 | ✓ |
| R5 | an overflow that hides behind the other gate | matrix gate and source gate each fire alone, by message | `InvalidAreaAndOverflowAreRefused`; B26b, B27b | ✓ |
| R6 | a model kept across a remesh sees the new mesh | it does, by design; the tests capture old values by value | `RemeshInvalidatesPreparedBC` | ✓ |
| R7 | a pair counted once per request, not once per unknown | counted once per unknown | `ConflictCountsEachUnknownOncePerPair`; B08 | ✓ |
| R8 | a zero-area facet contributes a silent zero | `DegenerateFacet`, never a zero term | `ZeroAreaFacetFails`; B31, B34 | ✓ |
| R9 | the corner order of a facet is sorted | row `i` stays corner `i` | `ConvectionFacetCarriesTheAnalyticalTermsAndOwnOrder`; B33 | ✓ |

## Cross-preset

The brief's cross-preset fixtures (resolved node and facet counts, constraint
unknowns and values, the Robin analytical entries, ambient equilibrium and sign,
conflict diagnostics, stale-source behaviour) were compared on each preset's own
binary after the qualifying run: the suite passed 92 of 92 and 4785 assertions in
all three, the two 64-bit bit fingerprints are identical (hand Robin triangle
06de4e52c3ef700f, real block 42982f4671b0e15b), and the 44 measurement lines the
suite prints are identical after sorting, with 0 differences. Discrete semantics
match: the same problem values, the same stale reasons, the same conflict records,
the same counts. The shared-library preset is the one that exposed F7. See
DETERMINISM.md.

## Verdict

No gate-blocking finding. One production defect (F7), a missing export
annotation, was found by the shared-library qualification preset and fixed. One test
gap (F1) was found by mutation and closed; one policy (F5) differs from the brief's
recommendation for a stated reason and is left to the owner.
