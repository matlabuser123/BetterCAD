# P18-DOF-001 — adversarial review

```text
SUBJECT:   an attempt to disprove the temperature index space before any
           matrix is assembled against it
QUESTIONS: 31 from the brief, plus 6 of the reviewer's
MUTATIONS: 17 probes, 17 KILLED, 0 survived, 0 void -- 23 runs counting the
           six reruns that turned four abort-kills into counted kills
FINDINGS:  2, both about the test METHOD; neither changed production
PRODUCTION DEFECTS: 0
GATE-BLOCKING: 0
```

Each answer below was obtained by reading the code or running something.

## Findings

**F1 — four first-batch kills were process aborts, not counted assertion
failures (test method; fixed before the freeze).** D01, D02, D03 and D08
exited −1073740791 (0xC0000409) with no Catch2 tally. Under the debug
preset's `_GLIBCXX_ASSERTIONS`, dereferencing an empty `std::optional` or an
out-of-range index aborts the process, and Catch2's stdout is lost in the
buffer. For D01 and D03 the deref was the TEST's: `*map.nodeOf(...)`,
`*map.elementDofs(...)` and `*result.temperatureOf(...)` written without a
preceding `REQUIRE`. The suite was hardened with three helpers (`nodeAt`,
`localDofs`, `temperatureAt`) that `REQUIRE` the lookup before using it,
plus `REQUIRE(index < size)` before each raw index, and the whole suite was
rerun on the unmutated tree: 33 of 33, 21 480 assertions (21 332 before).
Rerun under the hardened suite, D01 fails 11 cases by assertion and D03 13.
Production was not touched.

**F2 — D02 and D08 abort INSIDE the mutant, so their per-case verdicts had to
be taken one process at a time (test method; recorded, not fixed).** With
`dofCount()` mutated to `max(NodeId)+1`, `nodeOf` on an unknown that the
mutated count admits indexes past the four stored handles; with
`agreesWith` mutated to `true`, `reconstructTemperatures` walks the reduced
vector past its end for a constraint map that is not the complement. Both
aborts are libstdc++'s bounds assertion firing in production code whose
guard the mutant removed -- that is the mutant's behaviour, and the real
code's guard is exactly what keeps the index inside storage. Because a
single-process run loses its tally at the abort, batch 3 ran the same two
mutants through ctest, one process per case: D02 kills 18 of 33 cases (17 by
assertion, 1 by the abort, `RefusesALookupItDoesNotHave`); D08 kills 2 of 33
(`AChangedConstraintSetStalesTheFreeMapButNotTheDofMap` by assertion,
`FullReconstructionOracle` by the abort at the line that asserts the
mismatched map is refused). Nothing in the module changed; the method of
counting did.

## The brief's attacks

| Attack | Expected protection | Observed | Regression test | PASS |
| --- | --- | --- | --- | --- |
| NodeId used directly as the temperature-array index | the index is the ordinal, a distinct type; `temperatures()[dof]` | sparse fixture: index != handle for every node; `temperature[7]` is out of range of a 4-entry array | `SparseNodeIds`, `NodeIdIsNotRawEquationNumber`, `ResultArrayIsIndexedByThermalDof`; D01 | ✓ |
| `max(NodeId)+1` determines allocation | one vector of N handles | 2 nodes with handle 2^32−2: 8 bytes, `dofCount` 2 | `LargeSparseNodeIdDoesNotControlStorageSize`; D02 | ✓ |
| sparse NodeIds explode memory | same | same | same | ✓ |
| numeric sorting silently defines a different order than P16's | the enumeration is copied, never sorted; P16's order IS ascending by construction | map equals `mesh.nodes()` element for element; a reversed copy is killed | `OrdinalsFollowTheMeshOwnNodeEnumeration`; D03 | ✓ |
| a remesh with identical counts reuses the old map | the stamp | refused; identical counts asserted first | `SameCountsAndSameIdsNewMeshIsStale`, `RemeshInvalidatesOldMap`; D04 | ✓ |
| a remesh with identical NodeIds reuses the old map | the stamp | refused; identical handles asserted first | same | ✓ |
| a material edit unnecessarily stales the map | the map depends on the mesh only | mesh current, map current and equal to a rebuild, result stale | `MaterialEditKeepsDofMapCurrent` | ✓ |
| a load edit rebuilds the map | same | map current | `LoadSolverAndBoundaryConditionEditsKeepDofMapCurrent` | ✓ |
| a BC edit stales the base map | same | base map current | same | ✓ |
| a BC edit fails to stale the constraint/free map | `agreesWith` re-derives the complement | a changed constraint set: `agreesWith` false, rebuild differs | `AChangedConstraintSetStalesTheFreeMapButNotTheDofMap`; D08 | ✓ |
| `ThermalDofIndex` becomes canonical persisted intent | no field; definition not constructible from it; size mirror | compile-fail `the-definition-has-no-dof-map`; `static_assert` | `SolverLocalTypesAreDistinct` | ✓ |
| `ThermalFreeEquationIndex` becomes canonical | same | same | same | ✓ |
| a future BC stores NodeId as permanent CAD identity | `PrescribedTemperature` is `{FaceName, Temperature}` | not constructible from a NodeId or a ThermalDofIndex | `SolverLocalTypesAreDistinct` | ✓ |
| duplicate identical constraints double-counted | exact duplicates collapsed | size 2 for three listings, equal to the de-duplicated map | `DuplicateIdenticalConstraintDeduplicates`; D11 | ✓ |
| conflicting temperatures silently use the first | refused, both named | `ConflictingTemperature` in every listing order | `ConflictingConstraintFails`; D12 | ✓ |
| conflicting temperatures silently use the last | same | same | same | ✓ |
| constrained temperatures forced to zero (P17's habit) | the value is stored and reconstructed | 300 K and 350 K exact; reconstruction [310 300 320 330 350] | `ConstraintMapStoresNonzeroTemperature`, `FullReconstructionOracle`; D09, D10 | ✓ |
| Celsius inside the constraint | `Temperature` in K; no Celsius unit exists (P18-DATA-001) | a double is refused at compile time | compile-fail `a-constraint-takes-a-temperature-not-a-double` | ✓ |
| NaN/Inf becomes a prescribed temperature | `isAbsoluteTemperature` | NaN, ±inf, −1 K refused; 0 K accepted | `ConstraintMapRefusesUnusableInput`; D13 | ✓ |
| `Nfree = 0` rejected | not a DOF-layer error | everything prescribed: accepted, no rows | `FullyConstrainedAndNoConstraintsAreSupported` | ✓ |
| `Nconstrained = 0` rejected | same | nothing prescribed: accepted, row d = unknown d | same | ✓ |
| a constrained unknown receives a row | only the free list is stored | `equationOf` is `nullopt` for 1 and 4 | `FreeEquationMap`; D15 | ✓ |
| free equations contain gaps | one ascending walk | rows exactly {0..6} for 10 unknowns and 3 constraints | `EveryDofIsFreeXorConstrainedAndRowsAreContiguous` | ✓ |
| free ordering depends on hash iteration | no unordered container; ascending walk | 6 orders and 16 repeats equal | `FreeEquationOrderingDeterministic` | ✓ |
| element-local unknowns sorted | the element's own order is returned | [2, 0, 3, 1] for [9001, 7, 500000, 104] | `TetLocalDofsPreserveNodeOrder`; D16 | ✓ |
| shared nodes receive separate unknowns | one map, one lookup | the same three unknowns in both tetrahedra | `SharedNodeUsesSameDof`; D17 | ✓ |
| a missing element node silently creates an unknown | `NotFound`, nothing created | refused naming `node:8`; `dofCount` unchanged | `AnElementWithAForeignNodeIsRefusedAndCreatesNothing` | ✓ |
| a stale temperature array of the same size interpreted on a new mesh | the result and the map both carry the stamp | both refused after a remesh of equal counts | `ResultArrayIsIndexedByThermalDof` | ✓ |
| source matching relies only on Nnode/Ntet | stamp AND count | same counts refused by stamp; same stamp refused by count | `SameCountsAndSameIdsNewMeshIsStale`, `BindsExactMeshSource`; D04, D05, D06, D07 | ✓ |
| memory addresses enter provenance | `MeshStamp` is two integers; the map copies handles | — | by construction; `MultipleDocumentsHaveIndependentMaps` | ✓ |
| zero tests execute and qualification reports PASS | `ZeroMatchGuard` | 33 ≥ 20, 15 ≥ 8 | the guard | ✓ |

## Mutation probes

Each probe applies one textual change to `ThermalDof.cpp` or
`ThermalDof.hpp`, rebuilds the debug-ext test target, runs `[thermal][dof]`
and restores the file; the restore was verified afterwards by `git diff
--check`, a grep for every mutant fragment (none found) and a grep for every
original guard line (all present). The scripts and the three summary files
are under [qualification/](qualification/). "Cases" is the number of the 33
that failed; the first batch ran against the suite before F1's hardening,
the reruns after it.

| # | Mutation | Verdict | Cases | Killed by |
| --- | --- | --- | --- | --- |
| D01 | `dofOf` returns the raw `NodeId` value | KILLED | abort → 11 | `SparseNodeIds`, `NodeIdIsNotRawEquationNumber`, `OneScalarDofPerNode`, exact inverses, ordinals, scale, shared node, Tet order, result array, nothing-to-number, large handle |
| D02 | `dofCount()` = `max(NodeId)+1` | KILLED | abort → 18 (17 + 1 abort) | `LargeSparseNodeIdDoesNotControlStorageSize`, `SparseNodeIds`, free/constraint maps sized by the count, reconstruction, 20 000-node scale, foreign node, nothing-to-number; `RefusesALookupItDoesNotHave` aborts in the mutant |
| D03 | the copied node list is reversed | KILLED | abort → 13 | `OrdinalsFollowTheMeshOwnNodeEnumeration`, `MapConstructionIsDeterministic`, exact inverses, Tet order, shared node, result array, remesh, sparse, scale |
| D04 | `describes()` ignores the stamp | KILLED | 4 | `SameCountsAndSameIdsNewMeshIsStale`, `RemeshInvalidatesOldMap`, `ResultArrayIsIndexedByThermalDof`, `MultipleDocumentsHaveIndependentMaps` |
| D05 | `describes()` ignores the count | KILLED | 1 | `BindsExactMeshSource` (two snapshots of one builder) |
| D06 | free map accepts same counts as same mesh | KILLED | 3 | `FreeMapRejectsDifferentDofSource`, `SameCountsAndSameIdsNewMeshIsStale`, `RemeshInvalidatesOldMap` |
| D07 | free map ignores the count | KILLED | 1 | `FreeMapRejectsDifferentDofSource` |
| D08 | `agreesWith(constraints)` always true | KILLED | abort → 2 (1 + 1 abort) | `AChangedConstraintSetStalesTheFreeMapButNotTheDofMap`; `FullReconstructionOracle` aborts in the mutant at the refused-map line |
| D09 | `prescribedTemperature` returns 0 K | KILLED | 2 | `ConstraintMapStoresNonzeroTemperature`, `DuplicateIdenticalConstraintDeduplicates` |
| D10 | reconstruction writes 0 K for constrained unknowns | KILLED | 2 | `FullReconstructionOracle`, `FullyConstrainedAndNoConstraintsAreSupported` |
| D11 | exact duplicates kept | KILLED | 1 | `DuplicateIdenticalConstraintDeduplicates` |
| D12 | the last conflicting value wins | KILLED | 1 | `ConflictingConstraintFails` |
| D13 | the absolute-temperature check disabled | KILLED | 1 | `ConstraintMapRefusesUnusableInput` |
| D14 | the row equals the unknown's value | KILLED | 2 | `FreeEquationMap`, `EveryDofIsFreeXorConstrainedAndRowsAreContiguous` |
| D15 | a constrained unknown gets row 0 | KILLED | 3 | `FreeEquationMap`, `EveryDofIs...Contiguous`, `FullyConstrainedAndNoConstraintsAreSupported` |
| D16 | element-local unknowns sorted | KILLED | 3 | `TetLocalDofsPreserveNodeOrder`, `SharedNodeUsesSameDof`, `ResultArrayIsIndexedByThermalDof` |
| D17 | element-local unknowns are 0..3 per element | KILLED | 3 | same three |

```text
batch 1  D01-D17   17 killed (D01 D02 D03 D08 by abort, no tally)   14:29-15:19
batch 2  D01b D02b D03b D08b, hardened suite
                   D01b 11 cases, D03b 13 cases; D02b, D08b still abort,
                   now inside the mutant                              15:27-15:39
batch 3  D02c D08c one process per case through ctest
                   D02c 18 of 33 (17 Failed, 1 exit 0xC0000409)
                   D08c  2 of 33 ( 1 Failed, 1 exit 0xC0000409)       15:43-15:49
survived 0   void 0   equivalent 0
```

The breadth is not uniform and is not meant to be: D05, D07, D11, D12 and
D13 each have exactly one case that measures their fact, and that is the
case named in the attack table. D01 and D03 pull down a third of the suite
because the numbering is what everything else is built on.

## The reviewer's own six

**1. Is `ThermalDofIndex` really the same ordinal `ThermalResult` uses, or
two conventions that happen to agree?** One convention: both are "the
position in `mesh.nodes()`", and `ResultArrayIsIndexedByThermalDof` asserts
`temperatures()[dof]` against the result's own `temperatureOf` for every
node, so a future divergence fails the suite rather than going unnoticed.

**2. Why zero-based here when P17's `DofIndex` is one-based?** Because a
thermal unknown is a POSITION into the nodal array and the full system, and
the value must be the index; P17's `FreeEquationIndex` made the same choice
for the same reason and said so. A one-based handle would put a "- 1" at
every site, and the forgotten one would be an off-by-one in the conductivity
matrix.

**3. Could the conflict check depend on floating-point order?** The sort key
is (unknown, kelvin value) and the comparison of two values on one unknown
is exact equality; the same list in any order sorts to the same sequence,
so the reported pair is the same pair. 300 K and 300.000001 K are a conflict
by design, since a prescribed temperature is user intent and not a computed
quantity with a tolerance.

**4. Does `agreesWith(constraints)` cost more than it should?** O(C log F):
one count check and one binary search per constraint. It is a consumer's
pre-check, not something assembly does per element, and it replaces a
stored copy of the constrained list that could drift.

**5. Could the free topology be "current" while the system is stale?** Yes,
when only the prescribed VALUES change, and that is recorded rather than
hidden: `AChangedConstraintSetStales...` measures `agreesWith` true for the
same set at different values, and the header and SOURCE_BINDING.md state
that `Qf - Kfc Tc` moved. P18-ASSEMBLY-001 and P18-SOLVE-001 bind to the
constraint map's value, not to the free map alone.

**6. Is anything in the module keyed by `ThermalAnalysisId`?** No. The
numbering depends on the mesh, the constraint map on the numbering and the
resolved constraints, the free map on the two; two analyses on one mesh may
share equal numberings and differ in constraints. Binding the base map to an
analysis would invalidate it for nothing.

## Verdict

PASS. 31 attacks from the brief and 6 of the reviewer's, each answered by
a measurement or a compile-time refusal; 17 mutation probes, 17 killed, 0
survived, 0 void; 2 findings, both in how the tests count a kill, both
closed before the freeze (F1 by hardening the suite, F2 by counting
per process), 0 production defects, 0 gate-blocking. The production
sources that were qualified are the ones the probes were applied to and
restored from, byte for byte.
