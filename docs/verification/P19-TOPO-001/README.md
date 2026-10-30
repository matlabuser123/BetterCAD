# P19-TOPO-001 — Finite-Volume Topology

```text
TASK:            P19-TOPO-001 — Finite-Volume Topology
IMPLEMENTATION:  a derived, compactly indexed cell/face topology over a proved
                 P16 Tet4 mesh, with four distinct index types and eight refusals
TESTS:           36 unit cases / 108,373 assertions; 23 compile-fail cases;
                 5998 tests per preset across three presets; 2839 tests x 5
                 repetitions in two presets
VALIDATION:      the Tet4 arithmetic identity 4C == 2I + B on all eight reference
                 meshes, and every internal-face count equal to the F1 column
                 P19-MESH-001 recorded from two other code paths
RESULT:          PASS
EVIDENCE:        this directory; qualification/ holds the 23 raw logs
TODO:            22 boxes ticked on PASS (TODO.md lines 2230-2251)
```

**STATUS: PASS.** Qualified 2026-10-31 on candidate `dacbf561abce4451dee420e54118425e5d3ccfbd`
plus this milestone's uncommitted candidate files. All 17 stages exited 0; the
eight qualified subtrees are byte-identical before the first build, after the
last test run, and in the tree being committed.

---

## SCOPE

Authorized 2026-10-31 by the owner, who pasted the 22-item checklist and wrote
"implement this and fully verify". `TODO.md` had reserved this milestone with
"Authorizing it is the owner's decision alone", so nothing here was begun before
that message.

In scope: the 22 checklist items, mapped item by item at the end of this
document. Out of scope and **not authorized**: P19-GEOM-001 (areas, volumes,
face centroids, the `S . d > 0` condition). This milestone deliberately stops at
connectivity and orientation, which is why no geometric metric is computed here
beyond the two scalars the refusals need.

## BASELINE

Measured on `debug-ext` before the first candidate file existed:

```text
full suite per preset          5939
repeat selection               2803
compile_fail.cfdids            28 cases
architecture tests             205
```

## ARCHITECTURE

[ADR-064](../../architecture/decisions/ADR-064-the-finite-volume-topology-is-a-derived-compactly-indexed-value-built-on-a-proved-mesh.md)
compares three candidates on eleven axes and records seven decisions. The ones
that bind:

- **The topology is a derived value, not document state.** It is built from a
  mesh and a mapping, it is never persisted, and it carries the mesh generation
  it was built for so it cannot be used across a remesh.
- **Four index spaces, four types.** `CellIndex`, `FaceIndex`,
  `BoundaryFaceIndex` and `PatchIndex` are dense and 0-based. `ElementId` is a
  mesh handle: sparse, 1-based, reused every generation (ADR-031). Zero means
  "the first cell" in one space and "invalid" in the other, which is why the
  separation rests on the type system and not on naming.
- **No container is sized by a handle's value.** Every one is sized by a count,
  with an explicit translation vector between the two spaces.
- **Possession is the proof.** The constructor is private with two friends, so
  holding a `CfdTopology` means the eight refusals were checked on the mesh it
  came from.
- **The eight refusals are a subset of what ADR-063's mesh gate already
  refuses**, so on a mesh holding a `CfdMeshContract` none of them can fire. The
  full eight-row mapping is in the ADR; the `UnpatchedBoundaryFace <-
  UnattributedBoundary` link is the non-obvious one and is written down there
  rather than left to be re-derived.

## BLAST RADIUS

The topology is new and nothing consumes it yet, so the reachable surface is the
registration points, not the CFD module's behaviour:

```text
direct callers              none in production; P19-GEOM-001 is the intended one
mesh module                 read-only (Mesh, MeshIds, GeometryMeshMap)
cfd module                  reads CfdModel and CfdMeshContract; changes neither
persistence                 none -- nothing is serialized, so no format moved
regeneration                none -- the topology is rebuilt, never migrated
stable references           none -- a topology index is not a persistent identity
CLI                         no new surface
layering                    cfd (42) over meshing (40); architecture.layering passes
registration points touched src/cfd/CMakeLists.txt, tests/CMakeLists.txt,
                            tests/compile_fail/CMakeLists.txt,
                            tests/cli/ZeroMatchGuard.cmake
```

The frozen CFD implementation from P16/P19-MESH-001 was not modified. No existing
test was weakened, renamed or removed; the 28 `cfdids` compile-fail cases and the
205 architecture tests are unchanged and passing.

## IMPLEMENTATION

```text
include/bettercad/cfd/CfdTopology.hpp     the four index types, the eight
                                          problems, the public accessors
src/cfd/CfdTopology.cpp                   one walk of the mesh per call, then
                                          the assembler
```

The build is a single pass: `walk` keys every tetrahedron face by its sorted
`NodeId` triple into an ordered map, assigning the owner to the first claimant in
ascending `ElementId` order; the assembler then emits internal faces ordered by
`(owner, neighbour, key)` followed by boundary faces, so the two ranges are
contiguous rather than interleaved. `cellByElement_` is built in ascending
`ElementId` order, so it is already sorted and no sort is performed —
`cellIndexOf`'s `lower_bound` depends on that, and a test asserts it rather than
trusting it.

`patchFace` returns `std::optional<std::size_t>`. It previously returned a bare
`SIZE_MAX`, which is the exact sentinel this file's own comments reject.

### The two refusals added during review

`faceNodes` documents that every face is wound outward of its owner, and
P19-GEOM-001 is told to build its area vectors on that guarantee. `kTetFaces` is
outward only for a positively oriented tetrahedron, and the builder originally
checked neither cell volume nor the orientation of a shared face. The guarantee
was therefore documented but not held.

It is now **enforced**, not merely documented:

```text
NonPositiveCellVolume        an inverted cell: kTetFaces winds its faces INWARD
InconsistentFaceOrientation  two POSITIVE cells folded onto one side of a shared
                             face -- which a volume check alone cannot catch
```

Both were already in ADR-063's vocabulary, so Decision 7's "subset of what the
mesh gate refuses" property survives. Each has a fixture built to fail it.

## TESTS

`tests/cfd/CfdTopologyTests.cpp`, 36 cases, **108,373 assertions**, measured on
the qualified `debug-ext` binary with the seed pinned
(`direct-run-debug-ext.txt`).

The structural helper `checkStructure(topology, mesh)` runs on **all eight**
reference meshes from the sweep and asserts, per model: ascending `cellOf`; each
face a face of its owner's tetrahedron (and of its neighbour's); geometric
outward winding against the owner's apex; boundary winding a cyclic rotation of
the P16 triangle's; `owner < neighbour`; `cellOf(owner) < cellOf(neighbour)`; and
both adjacency directions.

It exists because a review found each structural property being checked on the
one model chosen for it, which left `RM-MESH-06 MeshTransformedPlaced` — the one
place a placement transform could invert a winding — structurally untested.

```text
definition and addressing
  TheIndexTypesAreDistinctAndZeroIsAValidIndex
  TheIndexTranslationsAreMutualInverses
  IndexesCellsWhoseElementHandlesAreSparse
  SparseTriangleHandlesRoundTripAndACellHandleIsNotOne
  EveryAccessorIsTotalForAnIndexItDoesNotHold

structure, on every reference mesh
  EveryP16ReferenceMeshYieldsATopology
  OwnerIsTheLowerCellAndEveryInternalFaceHasANeighbour
  EveryFaceIsWoundOutwardOfItsOwner
  CellAndFaceAdjacencyAgreeInBothDirections
  PatchesPartitionTheBoundaryFaces
  InternalFacesAreOrderedByOwnerThenNeighbour
  TwoCellsOnTheSameFourNodesShareFourFacesNotEight
  APatchWithNoFacetsIsCountedAndItsSpanIsEmpty
  PatchFaceNamesTheCadFaceAndNotThePatchIndex

the eight refusals, each on a mesh built to fail it
  RefusesAnInvertedCellBecauseItsFacesAreNotWoundOutward
  RefusesTwoCellsFoldedOntoOneSideOfTheirSharedFace
  RefusesAFaceSharedByThreeCells
  RefusesAFaceWithNoSecondCellAndNoBoundaryTriangle
  RefusesABoundaryTriangleThatIsNoFaceOfAnyCell
  RefusesABoundaryTriangleOnAFaceTwoCellsShare
  RefusesTwoBoundaryTrianglesOnOneFace
  RefusesABoundaryTriangleAttributedToNoPatchOrToSeveral

diagnostics
  TheRefusalNamesTheControlAndEveryFindingInOrder
  ThePlainBuildersRefusalNamesNoControlAndJoinsEveryFinding
  TwoUnpatchedTrianglesAreOneFindingWithACountOfTwo
  ABoundaryTriangleOnAThreeCellFaceReportsBothProblemsInOrder
  AFacetInSeveralPatchesIsReportedOnceAndNamesThem
  ARefusedInspectionStillReportsTheCensusItClassified
  EveryProblemHasADistinctStableNameAndTheArrayIsDeclarationOrder

binding, determinism and edges
  RefusesAContractProvedForAnotherMeshGeneration
  DescribesIsFalseForALaterGenerationOfTheSameMesh
  ThePlainBuilderCarriesNoGeometryRevision
  TwoBuildsOfOneMeshAreEqual
  EqualityDiscriminatesSoDeterminismMeansSomething
  AReorderedPatchFacetListYieldsTheSameTopology
  AnEmptyMeshYieldsAnEmptyTopologyAndIsNotTheContractsRefusal
```

### Compile-fail cases

`tests/compile_fail/CfdTopologyMisuse.cpp`, group `cfdtopo`, **23 cases**, built
once with no `BETTERCAD_CF_*` macro as a control that must compile — so every
failure is attributable to its own line and not to a missing header.

The 23 split 10 / 6 / 6 / 1, counted from the registered case names:

```text
10  the four topology subscripts do not interconvert, in the directions a call
    can confuse them: a-cell-is-not-a-face, a-face-is-not-a-cell,
    a-face-is-not-a-boundary-face, a-boundary-face-is-not-a-face,
    a-boundary-face-is-not-a-patch, a-patch-is-not-a-boundary-face, and the four
    passing-X-where-Y-is-wanted forms
 6  a topology subscript is not a mesh handle, both directions:
    an-element-handle-is-not-a-cell-index, a-cell-index-is-not-an-element-handle,
    a-node-handle-is-not-a-cell-index, passing-an-element-where-a-cell-is-wanted,
    passing-a-cell-where-an-element-is-wanted,
    passing-a-node-where-a-cell-is-wanted
 6  an index is not an integer, has no arithmetic, and two kinds do not compare:
    an-integer-is-not-a-face-index, an-index-is-not-an-integer,
    an-index-has-no-arithmetic, two-index-kinds-do-not-compare, and the two
    passing-forms across the integer boundary
 1  a-topology-cannot-be-built-without-its-builder -- the constructor is shut;
    without this case nothing proved possession means anything
```

### The zero-match guard

`ctest -R <no-match>` exits 0, so `tests/cli/ZeroMatchGuard.cmake` asserts
per-filter minimum counts. 22 named `unit\.CfdTopology_*` pins plus the group
filter were added and the group minimum raised 15 -> 30, so losing five cases to
a silent registration failure now fails the build. `cli.mesh.zero-match-guard`
passed in all three presets (58.30 s on debug-ext).

## INDEPENDENT VALIDATION

`independent-census-check.txt`. For a Tet4 mesh every cell has four faces, every
internal face is shared by two cells and every boundary face by one, so

```text
4C == 2I + B
```

This holds on all eight reference meshes, and every internal-face count equals
the F1 column P19-MESH-001 recorded three days earlier.

A second agreement falls out of the direct run: the census file records the
Cylinder as 946 internal + 140 boundary faces, and the winding test — which
counts faces by iterating `topology->faces()` and never reads the census —
reports "over 1086 faces". 946 + 140 = 1086.

**What is and is not independent here.** The identity is arithmetic on the
counts, not a re-run of the code that produced them, and the F1 column came from
the mesh contract's census and the tests' own `faceIncidence` — two code paths
that are not this one. The tests' own face-incidence helper, by contrast, is an
independent *transcription* of the same method (a sorted node triple into an
ordered map) and not an independent method, so it cannot detect an error in the
method itself. An earlier draft of ADR-064 called it independent validation; a
review was right that it is not, and the ADR now says so.

## ADVERSARIAL REVIEW

Two independent reviewers were given the final diff, per CLAUDE.md's parallel-work
rule — reviewers challenge the implementation, they do not race it. Both found
real defects. Neither finding was waved away and none was closed by weakening a
test or moving a tolerance.

**Adversarial review — one credible defect, resolved.** `faceNodes` promised
outward winding unconditionally while the builder checked neither cell volume nor
shared-face orientation, so a plain-built topology could hand P19-GEOM-001
silently inverted normals. The reviewer offered documenting the precondition as
the alternative; it was enforced instead, as described under IMPLEMENTATION.

**Test-gap review — eight claims corrected.** The failure mode CLAUDE.md names
outright: claims with no evidence behind them.

```text
1  "a test asserts owner < neighbour on every internal face of every reference
   mesh"                    -- it ran on Block alone     -> checkStructure, all 8
2  "a test asserts it is sorted" for cellByElement_
                            -- no such test existed      -> now asserted
3  patchFace asserted only where expected value == index passed in; the fixture
   built to break that coincidence was never used with it
                                                         -> now it is
4  "a mesh whose elements are 1, 2, 7, 900000 is a legal mesh"
                            -- MeshBuilder cannot build one; handles come from
                               one counter, so interleaving is the only reachable
                               sparsity                  -> ADR and header reworded
5  the "independent" census is an independent transcription, not an independent
   method                                                -> reworded; see above
6  Decision 7's subset mapping was wrong in detail
                                                         -> full eight-row table
7  patchFace returned a bare SIZE_MAX                    -> std::optional
8  determinism rested on an operator== never shown to discriminate
                                                         -> EqualityDiscriminates...
```

Defect 4 is worth stating plainly: the design decision is unchanged, because a
container sized by a handle is wrong either way, but the ADR should not have
stated as present fact a mesh the module cannot build.

### Defects found by the tests themselves, during implementation

- A boundary triangle rejected for having no patch `continue`d before
  `entry.covered = true`, so the same face *also* reported `MissingNeighbour` —
  one defect became two findings and the second was a lie. Fixed by setting
  `covered` and `triangle` before the patch lookup.
- `DescribesIsFalseForALaterGenerationOfTheSameMesh` assumed `prepare()`
  remeshes; it returns the cached generation. The case now asserts that cache
  behaviour explicitly and uses a second `MeshedFluid` for the
  different-generation half.
- An aggregate assertion `worstTowardOwner < 0.0` was initialised to `0.0` while
  every value is negative, giving `0.0 < 0.0`. It was removed rather than
  repaired: Catch2's `CHECK` does not abort, so the per-face check already covers
  every face and the aggregate asserted nothing new. It is reported, not
  asserted.

## FAILURE PATHS

Eight refusals, each with a fixture built to fail it, reported in declaration
order with the meshing control named:

```text
NonPositiveCellVolume        InconsistentFaceOrientation
NonManifoldFace              MissingNeighbour
UnownedBoundaryFace          BoundaryFaceWithTwoOwners
DuplicateBoundaryFace        UnpatchedBoundaryFace
```

A refusal is atomic: no topology is produced, and the refused inspection still
reports the census it managed to classify. Two unpatched triangles are one
finding with a count of two, not two findings. A facet in several patches is
reported once and names them. A boundary triangle on a three-cell face reports
both problems, in declaration order.

Cross-generation use is refused by construction: a contract proved for one mesh
generation is rejected against another, with a message saying it "was proved for
mesh generation ... says nothing about" the other.

## PERSISTENCE

**Not applicable, deliberately.** The topology is a derived value: nothing is
serialized, no file format moved, and no persistent identity is minted. A
topology index is not a stable reference and must never be stored as one — which
is why it is a distinct type from `ElementId` and why the topology carries the
mesh generation it was built for. `ThePlainBuilderCarriesNoGeometryRevision` and
`DescribesIsFalseForALaterGenerationOfTheSameMesh` are the tests that hold this
line.

## DETERMINISM

```text
TwoBuildsOfOneMeshAreEqual                  two builds of one mesh compare equal
EqualityDiscriminatesSoDeterminismMeansSomething
                                            and equality is not vacuous, so the
                                            above asserts something
AReorderedPatchFacetListYieldsTheSameTopology
                                            input facet order does not reach the
                                            result
```

Ordering is total and explicit everywhere it matters: faces are keyed by a sorted
`NodeId` triple in an ordered map, owners are the first claimant in ascending
`ElementId` order, and internal faces are emitted by `(owner, neighbour, key)`.
No unordered iteration, no clock, no unfixed seed, no thread scheduling and no
locale dependence.

Measured across repetitions and configurations: the repeat stage ran the 2839
selected tests **five times each** in both `release-ext` and `debug-ext`, giving
**14,195** strict per-test `Passed` markers in each log, and the 36 topology
cases appear 180 times (36 x 5) in each. The direct-binary run pins
`--rng-seed 1`, because this binary's `--order` defaults to `rand` with a
per-run seed and an unpinned run is not reproducible evidence — the defect that
failed P19-MESH-001's postrun check.

## REGRESSION

```text
preset              tests  result   total test time
debug-ext            5998  100%     1902.05 sec
release-ext          5998  100%     1388.88 sec
debug-shared-ext     5998  100%     1455.76 sec
repeat release-ext   2839  100%      831.41 sec   (x5 repetitions)
repeat debug-ext     2839  100%      885.76 sec   (x5 repetitions)
```

`5998 = 5939 baseline + 36 unit.CfdTopology_ + 23 compile_fail.cfdtopo`.
`2839 = 2803 + 36`; the harness filter's `unit\.Cfd` reaches the new cases.

Per preset: 36 topology cases, 23 `cfdtopo` compile-fail cases, 28 `cfdids`
compile-fail cases unchanged, 205 architecture tests including
`architecture.layering`. Zero `warning:` and zero `error:` in all three build
logs under `-Werror`. Zero genuine failure, timeout, not-run or skipped markers
in any ctest log.

**Two log readings that look like problems and are not**, both checked rather
than assumed:

- The summary line in this CMake is `100% tests passed out of N` — it omits the
  `, 0 tests failed` clause when none failed. A grep expecting that clause
  matches nothing, which is a reader's defect, not a test result.
- `skipped` and `disabled` appear in the logs only inside test *names*
  (`unit.ThermalValidation_ASkippedRuleIsNeverAPass`,
  `unit.Constraints can be disabled, changed and driven by parameters`), and each
  of those tests passed. Nothing was skipped or disabled. Likewise the 14,200
  loose `Passed` hits per repeat log are 14,195 markers plus five repetitions of
  one test whose name ends `...ItPassed`.

An earlier full-suite run reported nine failures (3 `cfdtopo`, 6 `cfdids`). Each
compile-fail case invokes `cmake --build`, and CMakeLists and sources were being
edited during that run. Re-run on a settled tree, all 51 pass. That was
interference, not a regression — established by re-running, not by argument.

## QUALIFICATION

Frozen before the first build; `FREEZE.txt` records the candidate file hashes,
the four registration points, the harness hashes and the eight subtrees.

```text
candidate           dacbf561abce4451dee420e54118425e5d3ccfbd + the candidate files
started             2026-10-31 04:02:35
finished            2026-10-31 06:13:51   (2 h 11 min 16 s)
stages              17 of 17 exited 0; 0 stages failed
preflight           OK, 2312.06 GB free of 3814.25 GB, threshold 20 GB
CMake / generator   4.4.1 (Ninja)
compiler            GNU 16.1.0, C++23, WinLibs POSIX UCRT
Qt / Netgen / Catch2 6.11.2 / 6.2.2604 / 3.16.0 (fetched)
warnings as errors  ON in all three presets
no-op rebuild       exit 0 in all three presets (build is settled)
```

The harness is **byte-identical** to P19-MESH-001's, so the only difference
between the two qualifications is the tree under test:

```text
qualify.cmd            d313a64070718c44fae290ac042fe259d1a03c8b  (as since P12)
run-qualification.cmd  22b19f25052486453b7b7e51b84f3b4b8afb57f0
launch.cmd             f11039e6dc366d7c469a15cf74fb3867fd5ffeff
verify-harness.cmd     11855b8ff891093b021d1e0351761283d0eedd24
preflight-space.cmd    a65d6578000b6e8a2811d5154bcec24be09c2b54
preflight-space.ps1    2f0d4228488f6a574ca57ec89f0fad0107b16b59
```

### The qualified tree is the committed tree

The eight subtrees, recomputed by the harness's own method (scratch index,
`git add -A`, `git write-tree --prefix=`) at three separate times:

```text
                     FREEZE.txt     after the last     the tree being
                     (before)       test run           committed
apps                 fc398a3d       fc398a3d           fc398a3d
include              269e4a3f       269e4a3f           269e4a3f
src                  450c99d1       450c99d1           450c99d1
tests                aaa0243d       aaa0243d           aaa0243d
examples             4da234eb       4da234eb           4da234eb
cmake                5a382115       5a382115           5a382115
CMakeLists.txt       3ec3c10f       3ec3c10f           3ec3c10f
CMakePresets.json    3229f0f8       3229f0f8           3229f0f8
```

Five of the eight are unchanged from P19-MESH-001's frozen values, confirming
nothing outside this milestone moved.

## KNOWN LIMITATIONS

1. **"No max-ID-sized arrays" is proved by review and by negative lookups, not
   by measuring allocation.** No allocation measurement was taken. What is
   established is that every container is sized by a count (read off the
   implementation, and the reason the translation vector exists), and that a
   lookup far outside every handle the mesh issued answers "no" rather than
   reading past the end of anything. A memory-instrumented proof is not part of
   this milestone.
2. **Sparsity is interleaving-only through the real builder.** `MeshBuilder`
   allocates element handles from one counter shared by triangles and tetrahedra,
   so the reachable sparsity is gaps of one or two where a triangle sits between
   tetrahedra. Arbitrary gaps are tested only through a far-outside lookup. The
   design decision is independent of this, since a container sized by a handle is
   wrong either way.
3. **The census's independence is arithmetic plus a prior milestone's record**,
   not a second implementation of face extraction. Stated in full under
   INDEPENDENT VALIDATION.
4. **The worst outward-winding margin on the Cylinder is `-2.871449e-09`.** This
   is not a thin tolerance. `normalOf` is the unnormalised `(b-a) x (c-a)`, so
   `normal . toApex` is exactly six times the owner's signed volume and carries
   the dimensions of a volume: its magnitude is set by the smallest cell in the
   mesh, and only its **sign** is asserted. The sign is guaranteed rather than
   hoped for, by the `NonPositiveCellVolume` refusal. The check is not vacuous
   either — a wrong `kTetFaces` entry or a wrong owner would flip the sign on a
   positive-volume cell.
5. **Six of the eight refusals are unreachable in production** through
   `requireCfdTopology` on a mesh holding a `CfdMeshContract`, by design
   (ADR-064 Decision 7). They are reachable through the plain builder, which is
   what the tests use. The other two — `NonPositiveCellVolume` and
   `InconsistentFaceOrientation` — are not merely defensive: `faceNodes`'s
   guarantee is false without them.
6. **The copied preflight prints "P19-MESH-001 preflight"** in
   `run-qualification.out`. Cosmetic, and left unedited deliberately: editing it
   would break the harness byte-identity that makes this qualification
   comparable to the last one. The preflight's behaviour and thresholds are
   milestone-independent.
7. **Nothing consumes the topology yet.** There is no CLI surface and no
   renderer path. P19-GEOM-001 is the intended first consumer and is **NOT
   AUTHORIZED**; finishing this milestone is itself a stop condition.

## RESULT

**PASS.** All 22 checklist items are met, with the two narrowings and one
collapse recorded explicitly below rather than glossed.

## CHECKLIST MAPPING

Where an item is narrower than its wording, or collapses onto another, it says
so. Nothing here is ticked on the strength of its wording alone.

```text
 1 Define compact CellIndex       CellIndex; TheIndexTypesAreDistinctAndZeroIsAValidIndex,
                                  TheIndexTranslationsAreMutualInverses; the
                                  interconversion and integer-boundary compile-fail cases
 2 Define compact FaceIndex       FaceIndex; same cases
 3 Define boundary-face index     BoundaryFaceIndex; PatchesPartitionTheBoundaryFaces,
                                  InternalFacesAreOrderedByOwnerThenNeighbour
 4 ElementId != CellIndex         compile-fail an-element-handle-is-not-a-cell-index,
                                  a-cell-index-is-not-an-element-handle,
                                  passing-an-element-where-a-cell-is-wanted,
                                  a-node-handle-is-not-a-cell-index;
                                  SparseTriangleHandlesRoundTripAndACellHandleIsNotOne
 5 Define owner cell              OwnerIsTheLowerCellAndEveryInternalFaceHasANeighbour;
                                  checkStructure on all 8 meshes
 6 Define neighbour cell          same
 7 Define boundary face           PatchesPartitionTheBoundaryFaces;
                                  InternalFacesAreOrderedByOwnerThenNeighbour (contiguity)
 8 Define internal face           same
 9 Define boundary patch
   membership                     PatchesPartitionTheBoundaryFaces,
                                  PatchFaceNamesTheCadFaceAndNotThePatchIndex,
                                  APatchWithNoFacetsIsCountedAndItsSpanIsEmpty,
                                  AFacetInSeveralPatchesIsReportedOnceAndNamesThem
10 Define face orientation        EveryFaceIsWoundOutwardOfItsOwner; checkStructure's
                                  geometric winding on all 8; ENFORCED by
                                  NonPositiveCellVolume + InconsistentFaceOrientation
11 Define cell-to-face adjacency  CellAndFaceAdjacencyAgreeInBothDirections,
                                  TwoCellsOnTheSameFourNodesShareFourFacesNotEight
12 Define face-to-cell adjacency  same, cross-checked in both directions
13 Reject duplicate internal
   faces                          NARROWER THAN ITS WORDING. A duplicate internal face
                                  is not representable: the key is a sorted node
                                  triple in a map, so the second claimant becomes the
                                  neighbour and the third is refused. What is rejected
                                  is the two reachable attempts --
                                  RefusesAFaceSharedByThreeCells (NonManifoldFace) and
                                  RefusesTwoBoundaryTrianglesOnOneFace
                                  (DuplicateBoundaryFace). Deduplication itself is
                                  proved by TwoCellsOnTheSameFourNodesShareFourFacesNotEight
14 Reject missing neighbours      RefusesAFaceWithNoSecondCellAndNoBoundaryTriangle
                                  (MissingNeighbour)
15 Reject boundary faces with
   two owners                     RefusesABoundaryTriangleOnAFaceTwoCellsShare
                                  (BoundaryFaceWithTwoOwners)
16 Reject internal faces with
   one owner                      COLLAPSES ONTO ITEM 14. An internal face with one
                                  owner IS a face with no second cell and no boundary
                                  triangle: same refusal, same test. Recorded rather
                                  than counted twice as if two checks existed
17 Sparse mesh IDs supported      IndexesCellsWhoseElementHandlesAreSparse,
                                  SparseTriangleHandlesRoundTripAndACellHandleIsNotOne.
                                  NARROWER: see KNOWN LIMITATIONS 2
18 No max-ID-sized arrays         Proved by review plus negative lookups, NOT by
                                  measuring allocation. See KNOWN LIMITATIONS 1
19 Exact mesh-source binding      RefusesAContractProvedForAnotherMeshGeneration,
                                  DescribesIsFalseForALaterGenerationOfTheSameMesh,
                                  ThePlainBuilderCarriesNoGeometryRevision
20 Determinism PASS               TwoBuildsOfOneMeshAreEqual,
                                  EqualityDiscriminatesSoDeterminismMeansSomething,
                                  AReorderedPatchFacetListYieldsTheSameTopology;
                                  2839 tests x 5 repetitions in two presets, 14,195
                                  markers each
21 Regression PASS                5998 tests at 100% in three presets; 28 cfdids and
                                  205 architecture tests unchanged and passing; zero
                                  warnings under -Werror
22 Evidence recorded              this directory
```

## FILES

```text
include/bettercad/cfd/CfdTopology.hpp              new
src/cfd/CfdTopology.cpp                            new
src/cfd/CMakeLists.txt                             registration
tests/cfd/CfdTopologyTests.cpp                     new, 36 cases
tests/compile_fail/CfdTopologyMisuse.cpp           new, 23 cases
tests/compile_fail/CMakeLists.txt                  registration
tests/CMakeLists.txt                               registration
tests/cli/ZeroMatchGuard.cmake                     22 pins, minimum 15 -> 30
docs/architecture/decisions/ADR-064-*.md           new
docs/verification/P19-TOPO-001/                    this evidence
```

## EVIDENCE FILES

```text
README.md                        this document
FREEZE.txt                       the pre-build freeze and required counts
independent-census-check.txt     4C == 2I + B on eight meshes, vs ADR-063's F1
direct-run-debug-ext.txt         108,373 assertions, seed pinned
qualification/                   23 raw logs: 3 configure, 3 clean, 3 build,
                                 3 rebuild, 3 ctest, 2 ctest-repeat,
                                 qualification-times.txt, run-qualification.out,
                                 run-qualification.err, and the 6 harness scripts
```

## REVISION

First revision. Qualified once, from clean, on the frozen tree; no re-run was
needed and none was voided. The implementation changed twice after the first
green test run — once for the adversarial finding (two refusals added) and once
for the test-gap findings — and the freeze was taken after both, so the tree that
was qualified is the tree that was reviewed.
