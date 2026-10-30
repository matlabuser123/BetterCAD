# ADR-064 — The finite-volume topology is a derived, compactly indexed value built on a proved mesh

```text
STATUS:    Accepted
DATE:      2026-10-31
MILESTONE: P19-TOPO-001
TOUCHES:   ADR-031 (mesh handles are not document identities; the same reasoning
           decides the index types below), ADR-032 (a boundary names a CAD face),
           ADR-058 (the topology is derived, never persisted), ADR-060 (the input
           boundary this is staged after), ADR-063 (the mesh contract this is
           built on; its owner rule and face table are binding here)
```

## Context

ADR-063 proved that a P16 Tet4 mesh can serve directly as the finite-volume mesh,
and said what this milestone must build:

> Possession of a `CfdMeshContract` is the proof … and P19-TOPO-001 builds the
> compact cell/face topology on a mesh that holds one.

It also fixed the definitions this milestone is not free to re-choose: a cell is a
tetrahedron, a face is a sorted node triple, the owner is the first claimant in
ascending `ElementId` order, every face is wound by its owner, a patch is one CAD
face of the `GeometryMeshMap`, and there is no nearest-face fallback.

What does not exist yet is an addressing scheme. `requireCfdMesh` counts faces and
throws the census away; nothing names a face, and nothing says which cells sit on
either side of one. A finite-volume method needs both, and needs them as *dense
array subscripts*, because every field it assembles — pressure per cell, mass flux
per face — is an array, and every matrix coefficient is indexed by the cell pair a
face joins.

The mesh cannot supply those subscripts. `ElementId` is a mesh handle, and
`MeshIds.hpp` is emphatic about what that means: "effectively yes: dense, ordered"
today, but "allocated by the mesh, reused every generation", with no promise of
density. Triangles and tetrahedra share ONE handle space, so a mesh built with a
boundary triangle between two cells already has non-consecutive cell handles, and
a field indexed by `ElementId::value()` is wrong the moment the allocator changes.

An earlier draft of this ADR asserted instead that "a mesh whose elements are 1,
2, 7, 900000 is a legal mesh". An adversarial review showed that `MeshBuilder`
cannot currently produce one: it allocates from a single counter, so handles are
consecutive and interleaving is the only reachable sparsity. The decision below
is unchanged, because a container sized by a handle is wrong either way, but this
ADR should not have stated as present fact a mesh the module cannot build.

## Decision 1 — A derived `CfdTopology` value, built from a proved mesh

Three candidates were considered seriously.

```text
                     A: derived cfd value      B: extend P16 VolumeMesh   C: no compact index,
                     (CHOSEN)                                             ElementId + maps
correctness          owner rule and face       same                       same
                     table inherited from
                     ADR-063 verbatim
architecture fit     cfd (42) consumes         WRONG LAYER: an FV         fits, but cannot
                     meshing (40), as          topology is a CFD          express ElementId !=
                     thermal and structural    concept; meshing 40        CellIndex at all
                     already do                cannot know it
complexity           one header, one source    touches P16's qualified    least code now, most
                                               mesh and every consumer    code in every operator
persistence          none: derived, keyed      would invite persisting    none
                     to the mesh stamp         it with the mesh
determinism          ordered map, mesh's own   same                       same
                     ascending order
performance          O(1) subscript per        O(1)                       O(log n) map lookup
                     face in the solver                                   per face, forever
extensibility        GEOM-001 hangs metrics    every mesh pays for        every operator repeats
                     on FaceIndex              CFD's needs                the lookup
failure modes        four refusals, each       a P16 regression would     a sparse field array
                     demonstrable              reach every mesh user      sized by max ElementId
migration cost       none: new module file     re-qualifies P16           none now
testability         a plain Mesh builds one    a VolumeMesh cannot be     fine
                                               built by hand at all
```

**A is chosen.** B is refused on the layering rule and on ADR-063's Decision 7,
which already declined to extract a shared mesh seam (owner decision D2); a
finite-volume owner/neighbour table is not a meshing concept, and putting it in
`meshing` would make every mesh consumer pay for it and would make a CFD change a
P16 re-qualification. C is refused because the milestone's own checklist requires
`ElementId != CellIndex`, which a design with no second type cannot state, and
because the per-face map lookup it forces is paid in the innermost loop of every
operator P19 will ever add.

The topology is **derived and never persisted** (ADR-058), and it **borrows
nothing**: like `CfdMeshContract` it is a value, so it cannot dangle.

## Decision 2 — Four concrete index types, written out

`CellIndex`, `FaceIndex`, `BoundaryFaceIndex` and `PatchIndex` are concrete
classes in `bettercad::cfd`, not instances of a template and not
`bettercad::Id<Tag>`.

`MeshIds.hpp` already settled this argument for mesh handles and its reasoning
applies unchanged. An `Id<Tag>` promises persistence, stability across rebuild and
`IdAllocator`'s never-reuse guarantee. A topology subscript has none of those: it
is dense, it is an index, it is recomputed wholesale by every rebuild, and it is
meaningful only against the `MeshStamp` it was derived from. Using `Id<Tag>` would
promise what it cannot give.

No generic `Index<Tag>` template is introduced either, for the reason that header
gives: so that each type's semantics are stated where it is declared, and none can
be instantiated for a fifth purpose by accident.

```text
CellIndex          0 .. cells-1          a tetrahedron's position in the mesh's
                                         own ascending ElementId order
FaceIndex          0 .. faces-1          a face's position in the order of
                                         Decision 3
BoundaryFaceIndex  0 .. boundaryFaces-1  a boundary face's position within the
                                         boundary range
PatchIndex         0 .. patches-1        a CAD face's position in map.faces()
```

Zero is a VALID index here, unlike every mesh handle and document ID in
BetterCAD, where zero is the invalid value. That inversion is deliberate and is
the reason these are strong types: a subscript whose first legal value is 0 and a
handle whose first legal value is 1 must never be assignable to one another. Each
type is default-constructible to an explicitly invalid state carrying
`kInvalid = SIZE_MAX`, so a default-constructed index is not cell 0.

`ElementId != CellIndex` is enforced by construction — no converting constructor,
no `operator std::size_t`, explicit `fromValue` only — and the compile-fail group
`cfdtopo` proves it, alongside the other directions a subscript could be confused:
cell for face, face for boundary face, boundary face for patch, and any of them
for a bare integer.

## Decision 3 — Face order: internal faces first, then boundary faces

```text
[0, internalFaces)            internal faces, ascending by (owner, neighbour)
[internalFaces, faces)        boundary faces, ascending by (patch, owner, face key)
```

`isInternal(f)` is `f < internalFaces` — a comparison, not a stored flag — and a
boundary face's `BoundaryFaceIndex` is `f - internalFaces`. This is the
upper-triangular convention every cell-centred finite-volume code uses, and it is
chosen for the same reason they choose it: the internal range can be swept
contiguously to assemble the matrix, and the boundary range is already grouped by
patch, so applying a boundary condition to a patch is a contiguous span rather
than a filter over every face.

Within the internal range the order is by owner then neighbour, which makes the
matrix's off-diagonal entries appear in row-major order. Within the boundary range
the order is by patch first, so `patchFaces(p)` is a span.

## Decision 4 — Owner and neighbour, inherited from ADR-063

The owner of a face is **the cell with the lower `CellIndex`**, and the neighbour
is the other. Because `CellIndex` is assigned in the mesh's ascending `ElementId`
order, the lower `CellIndex` *is* ADR-063's "first claimant in ascending
`ElementId` order". The two rules are the same rule, and that is not a
coincidence to be left implicit: if this milestone had chosen any other owner, the
face area vectors P19-GEOM-001 computes would point the opposite way for those
faces, and every flux across them would change sign. The test helper
`checkStructure` asserts `owner < neighbour`, and
`cellOf(owner) < cellOf(neighbour)`, on every internal face of every reference
mesh. It was written after a review found each structural property being checked
on the ONE model chosen for it, which left the transformed body -- where a
placement could invert a winding -- structurally untested.

Face nodes are stored **wound outward of the owner** — ADR-063's rule, so the
right-hand normal points owner → neighbour for an internal face and out of the
fluid for a boundary face. That winding *is* the face orientation; there is no
separate orientation field and no sign array.

A boundary face has an owner and **no neighbour**: `neighbour(f)` returns
`std::optional<CellIndex>`, empty there. An optional, not a sentinel, because a
sentinel is the kind of thing a caller forgets to check.

## Decision 5 — Sparse mesh IDs, and no array sized by a handle

`cellOf(CellIndex) -> ElementId` is a dense vector of size `cells`.

The reverse, `cellIndexOf(ElementId) -> std::optional<CellIndex>`, is a **sorted
vector of (ElementId, CellIndex) pairs searched by `std::ranges::lower_bound`**.
Not a `std::map` (a node per cell, and a pointer chase per lookup), and above all
not a vector indexed by `ElementId::value()`, which is the one thing this decision
exists to forbid: nothing promises that handles are dense, and an array sized by
the largest one would scale with the allocator rather than with the mesh. Because
`CellIndex` is assigned in ascending `ElementId` order, that vector is already
sorted when it is built, so no sort is performed -- and `checkStructure` asserts
the ascending order through `cellOf`, on every reference mesh and on the
sparse-handle mesh, because `lower_bound`'s correctness depends on it.

The same holds for the triangle → `BoundaryFaceIndex` direction. No container in
the topology is sized by any handle's value; every one is sized by a count.

## Decision 6 — Source binding: the stamp, never the counts

A `CfdTopology` holds the `MeshStamp` it was derived from and the
`GeometryRevision` behind it, and `describes(mesh)` asks `mesh.owns(stamp)`.

`requireCfdTopology(model, contract)` refuses with `FailedPrecondition` when the
contract was proved for a different mesh generation than the model now holds. This
is the check ADR-063 said the contract exists to permit — "so that whatever is
built on it (P19-TOPO-001) can check it is being applied to the same mesh — a
contract for one generation says nothing about the next, however equal their
counts". Counts are never compared for identity; two generations of a mesh can
agree on every count and share no node.

## Decision 7 — The refusals, and what the checklist's four items mean

The topology joins two sources that must agree: the faces of the cells, and the
mesh's boundary triangles, which are what carry patch attribution. Six refusals
name the ways that join can fail, each reachable on a plain `Mesh` and each shown
by a test on a mesh built to fail it:

```text
NonManifoldFace           a face is shared by three or more cells, so its owner
                          has no single neighbour
MissingNeighbour          a face has one cell and no boundary triangle: nothing on
                          its other side, and no patch to give it a condition
UnownedBoundaryFace       a boundary triangle is a face of no cell
BoundaryFaceWithTwoOwners a boundary triangle lies on a face two or more cells share
DuplicateBoundaryFace     two boundary triangles claim one face
UnpatchedBoundaryFace     a boundary triangle is in no patch, or in more than one
```

Mapped to the checklist, honestly, including where it collapses:

```text
"Reject duplicate internal faces"      -> DuplicateBoundaryFace. The INTERNAL
                                          table cannot hold a duplicate: faces are
                                          keyed by their sorted node triple in an
                                          ordered map, so a second arrival is the
                                          same entry. The reachable duplicate is a
                                          boundary one, and that is what is
                                          refused. Narrower than the wording.
"Reject boundary faces with two owners" -> BoundaryFaceWithTwoOwners, exactly.
"Reject missing neighbours"            -> MissingNeighbour.
"Reject internal faces with one owner" -> MissingNeighbour, the SAME condition
                                          from the other side: a face with one
                                          owner that no triangle covers is both "a
                                          missing neighbour" and "an internal face
                                          with one owner". One condition, one
                                          refusal, not two code paths to make the
                                          list look longer.
```

Three of the six are not on the checklist and are refused anyway, because the
structure needs them: `NonManifoldFace`, because owner/neighbour is not
well-defined when three cells meet at a face; `UnownedBoundaryFace`, because a
patch face with no cell to own it has no flux and no owner to balance, and leaving
it unrefused would mean a silently shorter face table; and
`UnpatchedBoundaryFace`, because `patchOf` must be total over the boundary range,
and the alternative -- giving an unattributed facet to the nearest CAD face -- is
the silent rebind ADR-032 forbids.

ORDER MATTERS INSIDE THE INSPECTION, and getting it wrong was caught by a test
rather than by review. A triangle refused for having no patch still COVERS its
face. The first implementation recorded the coverage only after the patch lookup
succeeded, so the face then looked uncovered and also reported `MissingNeighbour`
-- "no second cell and no boundary triangle", which is false of a face that has a
triangle. One defect became two findings and the second was a lie. The coverage is
now recorded before the patch is looked up. The test that caught it asserts the
EXACT list of problems, not merely that the build refused; a test that checked
only `!buildable()` would have passed throughout.

**All eight are a subset of what ADR-063's gate already refuses**:

```text
THIS MILESTONE                ADR-063's refusal that already excludes it
NonPositiveCellVolume       <- NonPositiveCellVolume
NonManifoldFace             <- NonManifoldFace
InconsistentFaceOrientation <- InconsistentFaceOrientation
MissingNeighbour            <- UnclosedDomain
UnownedBoundaryFace         <- InvalidFaceOwnership
BoundaryFaceWithTwoOwners   <- InvalidFaceOwnership
DuplicateBoundaryFace       <- InvalidFaceOwnership
UnpatchedBoundaryFace       <- UnattributedBoundary
```

The last link is the only non-obvious one, and it is load-bearing for the next
milestone, so it is recorded rather than left to be re-derived:
`GeometryMeshMappingReport::complete()` is computed from the SAME per-face facet
lists that `requireCfdTopology` passes as `patches`, and `CfdModel` is what pairs
the map with the mesh (ADR-060 -- `requireCfdMesh` explicitly declines to re-check
that pairing). An earlier draft of this ADR said "these four ...
`InvalidFaceOwnership` covers the first three and `UnclosedDomain` the fourth",
which was wrong in detail; an adversarial review supplied the mapping above and
confirmed the conclusion independently.

So on a mesh that holds a `CfdMeshContract` none of them can fire. They are kept, and
tested, for the reason ADR-063 kept `inspectCfdMesh` public: a builder that trusts
its precondition silently is a builder whose precondition cannot be shown to
matter. `buildCfdTopology` therefore takes a plain `Mesh` and a plain report, and
`requireCfdTopology` is the staged gate on top.

## Consequences

```text
+ a field is a dense array, and a matrix coefficient is (owner, neighbour)
+ a boundary condition applies to a contiguous span of faces
+ P19-GEOM-001 hangs every metric on FaceIndex with nothing further to derive
+ no handle-sized allocation anywhere, so a sparse mesh costs what it weighs
+ the owner rule, the face table and the winding are ADR-063's, unchanged
- a second addressing space exists, and a reader must know which of ElementId and
  CellIndex they hold. Mitigated by the type system, not by naming: the compiler
  refuses the confusion, and the compile-fail group proves it refuses.
- the eight refusals are unreachable in production. Deliberate; see Decision 7.
- two of them, NonPositiveCellVolume and InconsistentFaceOrientation, are NOT
  merely defensive: `faceNodes`'s outward-winding guarantee is false without
  them, and P19-GEOM-001 builds its area vectors on that guarantee. An
  adversarial review found this file promising the winding unconditionally while
  checking neither.
- the topology must be rebuilt after every remesh. Correct: it is derived state,
  and the stamp binding makes a stale one refuse rather than mislead.
```

## Verification

Evidence: `docs/verification/P19-TOPO-001/`.

```text
every P16 reference mesh builds a topology, and its census agrees with the
  contract's census and with the tests' own face incidence. That incidence is an
  independent TRANSCRIPTION of the same method -- a sorted node triple into an
  ordered map -- not an independent method, so it cannot detect an error in the
  method itself. The genuinely independent checks are the arithmetic identity
  below and the interior-face counts P19-MESH-001 recorded from two other code
  paths (docs/verification/P19-TOPO-001/independent-census-check.txt)
4 * cells == 2 * internalFaces + boundaryFaces, and boundaryFaces ==
  boundaryTriangles, on every reference mesh
owner < neighbour on every internal face; every face wound outward of its owner,
  checked geometrically against the owner's opposite vertex
each of the eight refusals on a mesh built to fail it, and the refusal naming
  every finding in declaration order with the meshing control named
a topology cannot be fabricated: the constructor is private with two friends,
  proved by the compile-fail case a_topology_cannot_be_built_without_its_builder
equality discriminates, so "two builds are equal" is not vacuous
a mesh whose tetrahedron handles are non-consecutive indexes correctly, and a
  lookup far outside every handle the mesh issued answers "no" rather than
  reading past the end of anything
determinism: two builds of one mesh are equal, field for field
the stamp binding refuses a contract from another generation
compile-fail group cfdtopo: every index confusion refused
```
