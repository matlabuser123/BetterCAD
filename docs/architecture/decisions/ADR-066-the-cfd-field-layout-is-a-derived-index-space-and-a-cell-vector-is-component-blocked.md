# ADR-066 — The CFD field layout is a derived index space, and a cell-centred vector is component-blocked

```text
STATUS:    Accepted
DATE:      2026-11-04
MILESTONE: P19-DOF-001
TOUCHES:   ADR-031 (mesh handles may be sparse, so no field is indexed by one),
           ADR-058 (derived state is never persisted; the authorized solver is
           collocated finite volume with SIMPLE-family coupling, which is what
           decides the vector layout), ADR-061 (the analysis definition holds
           intent only, which is the "no field arrays in canonical state" rule
           this milestone has to keep), ADR-064 (the four index types this
           extends and the face order it inherits), ADR-065 (the metrics, which
           are indexed by the same indices, and whose binding gap is closed
           here). It REVERSES P17-DOF-001's interleaved numbering, for a stated
           reason, and does not supersede it.
```

## Context

ADR-064 built the addressing and ADR-065 hung the metrics on it. Both said, in
the same words, what they were leaving here:

> No field storage (P19-DOF-001).

What is still missing is small to state and permanent once chosen: where each
unknown *lives*, how long an array of it is, and what subscript addresses one
component of a vector field. The authorized solver decides most of it already.
ADR-058 fixed:

```text
cell-centred collocated finite volume   a cell is one P16 tetrahedron
STEADY laminar                          SIMPLE-family pressure-velocity coupling
```

and the owner's checklist names the layout that follows from it: pressure
cell-centred, velocity cell-centred, mass flux face-centred.

So the open questions are not *which* locations — they are the collocated
scheme's — but how the index space is shaped, how it is bound to the mesh
generation it means something against, and what a boundary edit invalidates.

Two constraints are inherited and not re-decided. ADR-064's four index types
exist, and its face order is binding:

```text
[0, internalFaces)        internal, ascending by (owner, neighbour)
[internalFaces, faces)    boundary, ascending by (patch, owner, face key)
```

And `MeshIds.hpp` is explicit that mesh handles may be sparse, so no array here
is sized by a handle's value — every one is sized by a count.

## Decision 1 — A field map is a derived layout, and holds no field

`CfdFieldMap` carries the counts, the translation between indices, and the
binding to the mesh generation and the topology it was derived from. It carries
no pressure, no velocity and no flux.

Candidates:

```text
(a) a layout value, no arrays                             CHOSEN
(b) a field container, CfdField<Q, Location>, holding
    the values alongside the layout                       rejected
(c) the arrays on the canonical analysis definition       rejected outright
```

(b) is rejected because it would decide ownership, mutability, aliasing and copy
semantics for arrays that nothing in the tree yet produces. The first real
consumer — a boundary condition filling a patch's run (P19-BC-001), a momentum
predictor wanting three contiguous component blocks (P19-MOM-001) — should make
that decision with a use in hand. A container written now would be the
speculative abstraction the engineering rules forbid, and the checklist this
milestone is authorized against asks for *indexing*, not storage.

(c) is rejected outright, and it is the checklist's own last structural item. A
per-cell array inside the canonical definition is mesh-local derived state
promoted to authority: ADR-061 holds intent only, ADR-058 forbids persisting
derived state, and a saved document carrying a velocity field would make a
remesh a silent contradiction rather than a detectable staleness. What a
consumer holds instead is its own `std::span`, of length
`map.size(CfdVariable::Velocity)`.

AND THIS DECIDES SOMETHING AN EARLIER MILESTONE EXPECTED OF THIS ONE, so it is
recorded rather than left as a drifting forward reference. `CfdResult.hpp` says,
in P19-DATA-001's words:

> So the container is defined complete in everything but the arrays, and
> P19-DOF-001 adds them.

It does not. The owner's authorization for this milestone is a seventeen item
checklist about locations, indexing, binding and invalidation, and no item on it
is the result's arrays; scope comes from `TODO.md` alone, and an expectation
recorded in a header is not an authorization. The engineering reason points the
same way: nothing in the tree yet *produces* a field, so arrays added now would
be validated against no producer, and `CfdResult::create` would have to take a
topology to check their lengths — a change to a qualified type for a caller that
does not exist. The arrays therefore arrive with the first milestone that
computes a field, which will have this index space to key them by. The two
comments that said otherwise are corrected to say so, and the deferral is in the
evidence.

## Decision 2 — Three locations, and the variable table is frozen here with its dimensions

```text
CfdFieldLocation::Cell           one value per cell            [0, cells)
CfdFieldLocation::Face           one value per face            [0, faces)
CfdFieldLocation::BoundaryFace   one value per boundary face   [0, boundaryFaces)
```

`BoundaryFace` IS A LOCATION OF ITS OWN and not merely the tail of `Face`. A
prescribed inlet velocity, a wall shear or a patch's outflow exists only on the
boundary; an array over every face would be mostly unused, and the boundary
range is already grouped by patch (ADR-064), so a boundary array is the one a
boundary condition can fill patch by patch in contiguous runs. ADR-064 made the
same division for the same reason when it gave `BoundaryFaceIndex` a type
separate from `FaceIndex`.

The variable table, frozen here:

```text
symbol  variable           location   components   dimension      SI
p       Pressure           Cell       1            M L^-1 T^-2    Pa
U       Velocity           Cell       3            L T^-1         m/s
phi     MassFlux           Face       1            M T^-1         kg/s
```

THE DIMENSIONS ARE IN THE TYPE SYSTEM, NOT IN THIS TABLE ALONE. Each row
carries a `bettercad::Dimension`, and a `static_assert` ties it to the catalog's
own: pressure to `dimensions::pressure`, velocity to `dimensions::velocity`,
mass flux to `dimensions::mass / dimensions::time`. "Units are part of
correctness" is therefore a compile-time check at this boundary rather than a
sentence a later milestone can drift from — a momentum assembly that stored a
pressure where the velocity field goes would have to disagree with this table
to compile.

WHY `phi` IS A MASS FLOW RATE (kg/s) AND NOT A MASS FLUX (kg/(m^2 s)), despite
the name every finite-volume text uses. It is the face-integrated quantity
`rho (U . Sf)`, which is what a control-volume balance sums over the four faces
of a cell; the per-area form would have to be multiplied by `faceArea` at every
site, and the site that forgot would be a conservation error that still looked
dimensionally plausible. The name follows the literature; the dimension follows
the arithmetic, and the table states both so they cannot be confused.

NO `MassFlowRate` ALIAS IS ADDED TO THE CORE CATALOG. Nothing stores one yet.
The dimension is formed where it is used, from `dimensions::mass` and
`dimensions::time`, which the dimension type composes at compile time; adding a
layer-0 name for a quantity no value has is a change to shared infrastructure
with no present caller.

## Decision 3 — A cell-centred vector is COMPONENT-BLOCKED, which reverses P17

```text
CellVectorIndex value  =  component * cells + cell

component X (0)  ->  [0,        cells)
component Y (1)  ->  [cells,  2 cells)
component Z (2)  ->  [2 cells, 3 cells)
```

Candidates:

```text
(a) component-blocked   comp * cells + cell       CHOSEN
(b) interleaved         3 * cell + comp           rejected (P17's choice)
(c) no vector subscript: three scalar fields      rejected
```

THE REASON IS THE AUTHORIZED SOLVER, AND IT IS THE OPPOSITE OF P17'S. ADR-058
fixes SIMPLE-family coupling, so the momentum predictor is *segregated*: three
scalar systems of `cells` rows, solved one component at a time, and the pressure
correction is a fourth system of the same shape. Blocking makes each of those
four a contiguous run, so one scalar linear-solve interface serves all four, a
component's residual norm is a span reduction rather than a strided one, and
under-relaxation applies to a span.

P17-DOF-001 chose interleaving, and was right to:

> an element's stiffness contribution couples the degrees of freedom of its own
> four nodes, so interleaving draws the twelve indices it writes from four short
> runs, whereas component blocking scatters them across three regions N apart.

A Tet4 stiffness matrix is a full 12x12 block that couples components *within*
an element. The segregated momentum predictor assembles no row that couples `u`
to `v`. What coupling exists — the pressure gradient source, a slip wall's
tangential projection, the non-orthogonal correction — enters per component, on
the right-hand side or the diagonal, one system at a time. Copying P17's
convention here would put every momentum solve on a stride-3 view and force
either a gather and a scatter per outer iteration or a strided solver
interface, to buy locality for the convection term's per-cell read. The choice
is permanent, so it is recorded with its reason, as P17's was.

AND BLOCKING MAKES THE SCALAR LAYOUT A SPECIAL CASE OF THE VECTOR LAYOUT.
Component X's block is exactly the cell-centred scalar array, so
`vectorIndex(cell, X).value() == cell.value()`, and the two conventions are one
convention rather than two that have to be kept consistent. That is the reason a
pressure field and a velocity component can be handed to the same solve.

THE HAZARD THAT FOLLOWS, STATED RATHER THAN LEFT FOR A REVIEW TO FIND. Because
the X block and the scalar array coincide, a caller who passes a `3 cells` array
where a `cells` array is wanted reads the X component instead of failing loudly,
and the reverse reads past the end only above `cells`. Two things answer it, and
neither is a convention: `CellIndex` and `CellVectorIndex` do not convert in
either direction and the compile-fail group proves it, and `size()` is asked of
the map per *variable*, so an array's length comes from the frozen table rather
than from the caller's arithmetic. Interleaving would turn the same confusion
into visible garbage sooner; that is its one advantage here, and it does not
outweigh four contiguous solves.

(c) is rejected because the checklist asks for cell-centred vector indexing, and
because a subscript invented at its first use inside a momentum assembly is a
subscript with no name and no compile-fail group.

## Decision 4 — Reuse ADR-064's four index types, and add exactly one

```text
cell-centred scalar     CellIndex           [0, cells)           ADR-064's
cell-centred vector     CellVectorIndex     [0, 3 cells)         NEW
face field              FaceIndex           [0, faces)           ADR-064's
boundary field          BoundaryFaceIndex   [0, boundaryFaces)   ADR-064's
```

Candidates for the three that already exist:

```text
(a) reuse them                                             CHOSEN
(b) a parallel family: CellScalarIndex, FaceFieldIndex,
    BoundaryFieldIndex                                     rejected
(c) one generic Index<Location> template                   rejected
```

(b) is rejected because a `CellScalarIndex` would be a second name for a
`CellIndex` over the same range with the same meaning — the second addressing
space ADR-064 went to some length not to create. A cell-centred scalar field IS
indexed by the cell, and saying so is stronger than giving the subscript a
second name and then having to prove the two agree.

(c) is rejected for the reason `MeshIds.hpp` and ADR-064 both gave for writing
their handles out: each type's semantics should be stated where it is declared,
and a generic template can be instantiated for a fifth purpose by accident.

`CellVectorIndex` therefore follows ADR-064's shape exactly, down to the
inversion that file explains: zero is a valid subscript, a default-constructed
one is `kInvalid` and not component X of cell 0, and it converts to and from
nothing.

## Decision 5 — The binding, and three invalidation scopes rather than two

A field map carries the `MeshStamp` and the `GeometryRevision`, as ADR-065's
geometry does, and additionally the patch partition: how many boundary faces
each patch holds, and which CAD face each patch is. Against a topology it
reports one of three verdicts:

```text
MeshChanged              the stamps differ, or cells / faces / internalFaces
                         differ -> every field of every location is invalid
BoundaryLayoutChanged    the mesh part agrees and the patch partition differs
                         -> cell fields and internal-face values keep their
                            meaning; boundary fields do not
Current                  both agree
```

Candidates:

```text
(a) stamp + counts, a two-valued verdict         rejected here, and it is
                                                 what ADR-065 used
(b) stamp + counts + the patch partition,
    three-valued                                 CHOSEN
(c) hold a copy of the whole topology            rejected
```

WHY (a) IS NOT ENOUGH HERE THOUGH IT WAS ENOUGH THERE. The checklist requires
that a boundary edit invalidate boundary-derived state *only*, which is a claim
about a partial invalidation, and a verdict of "same or not" cannot express it.
Worse, (a) cannot even detect the case: two topologies of ONE mesh generation
that differ in their patch attribution agree on the stamp, on `cells`, on
`faces` and on `internalFaces`, and differ only in the order of the boundary
range — so (a) would call them the same indexing, and a boundary field built
against one would be silently mis-indexed by the other.

THAT CONFIGURATION IS NOT REACHABLE FROM A `CfdModel`, and the gap is recorded
as a gap rather than as a defect. `requireCfdTopology` reads one mapping from
one model, and two topologies of one generation are bit-identical — ADR-064's
assignment is deterministic and a test asserts it. Reaching it needs
`buildCfdTopology`'s plain span of `MappedFace`, which exists so that the
refusals can be shown a mesh the gate refuses. ADR-065's
`CfdGeometry::describes(const CfdTopology&)` has the same gap for the same
reason, and it is not widened here: this is the milestone whose job is the
binding, so the binding is what gets tightened.

(c) is rejected because it duplicates state the caller already holds, and makes
the map O(cells) to copy for a question that is O(patches) to answer.

THE VERDICT COVERS THE ATTRIBUTION AS WELL AS THE ORDERING, deliberately. A
re-attribution that exchanges only the two patches' CAD face indices leaves every
boundary slot holding the same face, so a boundary array's *values* are still
correctly placed; what has changed is which model face they belong to, and that is
how a boundary condition reached them. `BoundaryLayoutChanged` is therefore the
right answer there too, and the suite asserts it on exactly that witness.

KNOWN LIMITATION, STATED HERE RATHER THAN DISCOVERED LATER. The partition is
compared as the per-patch boundary-face count and the CAD face index of each
patch, so the undetected family is every re-attribution that preserves BOTH: any
exchange of facets between patches that leaves each patch's size and CAD face
unchanged. Two patches of equal size swapping their whole membership is the
example the suite measures, not the whole family. Closing it would mean carrying
a per-boundary-face value whose only use is this comparison, and
`CfdTopology.hpp`'s own rule argues against that ("a second copy is a second
thing to keep true"). `patchOf` for a boundary field is therefore a binary search
over the partition offsets and not a stored array, and the undetected case is a
hand-built pair of `MappedFace` spans that no mapping produces.

A TEST ASSERTS THE LIMITATION, which means it will FAIL if the binding is ever
strengthened. That is intended: the failure is the notice that this paragraph
needs rewriting, not that something broke.

## Decision 6 — A field map of no cells is refused, and ADR-065 deliberately differs

```text
CfdFieldMapProblem::TopologyHasNoCells        buildCfdFieldMap refuses
CfdFieldMapProblem::TopologyIsOfAnotherMesh   requireCfdFieldMap refuses
```

ADR-065 accepted an empty topology and produced zeroes, because the measurement
of nothing is zero and no consumer is misled by it. A NUMBERING of nothing is a
different object. P17-DOF-001 and P18-DOF-001 both refuse it, in the same words:

> A numbering of nothing is not a degenerate system but an absent one: N == 0
> would reach an assembler as a matrix with no rows and would solve trivially,
> reporting success for a model that was never there.

That argument holds verbatim for `cells == 0`, so the asymmetry with ADR-065 is
deliberate and is the reason rather than an oversight. The refusal is REACHABLE
and tested: a mesh with no tetrahedra and no patches yields a buildable topology
with zero cells, because ADR-064's inspection finds no defect in nothing — the
same witness ADR-065 used for its empty-geometry case.

## Decision 7 — No field arrays in canonical state, and what proves it

Three things, none of them prose:

```text
already proved, cited not re-counted   the cfdids compile-fail cases of
                                       P19-DATA-001: the definition has no
                                       pressure field, no velocity field, no
                                       mass flux and no cells
added here                             the definition has no field map, and a
                                       field map cannot be stored in one
added here                             an analysis HELD IN A DOCUMENT keeps the
                                       same canonical text and the same
                                       fingerprint across a remesh that changes
                                       the cell count, so nothing mesh-sized is
                                       inside the canonical form
```

The third is the one that can fail as the module grows, which is why it is a
test over `canonicalText` and `fingerprint` rather than an inspection of the
struct: a field added to the definition by some later milestone would have
either to leave the canonical text unchanged — and then it is not canonical — or
to change it, and fail this test.

AND IT HAS TO READ THE DEFINITION BACK FROM THE DOCUMENT, which this decision
first failed to say and both of this milestone's reviews caught independently.
The first version of the test built a *local* `CfdAnalysisDefinition`, took its
canonical text, remeshed, and then asked the same unmodified struct again — so it
checked that `canonicalText` is a pure function, which it is, and nothing else.
The defence above was false of it: a `std::vector<double> velocities` added to
the definition would have been empty both times and the test would have passed.
The analysis is now created in the document, the edit and the remesh both happen
in that document, and the definition is read back through `findCfdAnalysis` each
time. The same correction was needed for the boundary-condition test, for the
same reason.

## Consequences

```text
+ a field is a span whose length comes from the map and whose subscript has a
  type; a solver cannot size an array from a mesh handle, because no accessor
  here offers one
+ the four systems a SIMPLE iteration solves are four contiguous runs of equal
  length, so one scalar interface serves pressure and each velocity component
+ the dimensions of the three variables are a compile-time fact at this
  boundary, so a later milestone cannot store a pressure as a velocity and agree
  with the table
+ a boundary edit is distinguishable from a remesh, so a consumer can keep its
  cell fields across one
+ nothing persists: the map is O(patches) in size, holds no mesh handle and no
  value, and is rebuilt from the topology whenever it is wanted
- a cell-centred vector's component blocks are cells apart, so a per-cell read
  of U touches three cache lines rather than one; the convection term pays it
  (an expectation, not a measurement — no milestone may cite it as measured)
- the X block of a vector field and the scalar array coincide numerically, so
  the type system rather than the arithmetic is what separates them
- an empty patch's range has an INVALID first slot, so a consumer must ask
  `isEmpty()` before span arithmetic: `subspan(first.value(), 0)` on an empty
  range is undefined behaviour rather than an empty subspan. The alternative
  gives an interior empty patch the next patch's first slot, which is another
  patch's value; the hazard lands on P19-BC-001 and the warning is in the
  header where that arithmetic gets written
- a face field is NOT wholly covered by `describesCells`: under
  `BoundaryLayoutChanged` its internal range keeps its meaning and its boundary
  tail does not, so a mass flux survives in part
- one more derived value to rebuild after a remesh, and one more binding to
  check; both are cheap, and the alternative is a cached layout that goes stale
```

## What the two reviews changed

Both ran before the qualification freeze, independently, and both found the same
four evidence defects — which is the useful signal, since neither saw the other's
report:

```text
the canonical-state proof was vacuous      Decision 7, corrected above; the test
                                           now reads the document
the gate's one distinguishing line was
a no-op on every tested path               `requireCfdTopology` already stamps
                                           the model's revision, so deleting the
                                           gate's assignment passed the whole
                                           suite. A plain-topology fixture now
                                           fails without it, verified by
                                           injecting the mutation
a header cited a measurement nobody made   "a test measures the size against a
                                           mesh of four thousand cells": no such
                                           test, and the largest reference mesh
                                           is about a thousand cells. The header
                                           now names the two instruments that do
                                           carry the claim
a test comment described a fixture it
did not build                              "checked on a five-cell layout" over a
                                           two-cell fixture, where blocking and
                                           interleaving agree on the cell. The
                                           discrimination moved to the block's
                                           six cells
```

And four coverage gaps, each now a test: the per-patch-size branch of the
boundary comparison decided no verdict anywhere; every empty patch in the suite
was trailing, so `upper_bound`'s justification was unexercised; no layout had
`internalFaces == 0`; and the fluid-edit test never touched the viscosity the
header claims alongside the density.
