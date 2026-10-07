# P17-DOF-001 — determinism

```text
SUBJECT:  that the same mesh gives the same numbering, in every build
          configuration, on every run
```

## Why this milestone's determinism is structural rather than tested into place

The numbering is derived from `Mesh`'s enumeration, and P16 already guarantees
what that is:

```text
"ORDERING IS DEFINED AND DETERMINISTIC. Nodes are stored and enumerated in
 ascending NodeId ... Nothing here is an unordered container, so the same
 construction gives the same enumeration in Debug, Release and Debug-shared."
```

So the question is not whether the numbering can be made deterministic but
whether this module introduces anything that would break it. Four things could,
and each is answered by construction:

```text
an unordered container        NONE. grep for unordered_map, unordered_set and
                              std::hash across src/structural/ and
                              include/bettercad/structural/: the only textual
                              occurrence is the comment in StructuralDof.cpp
                              saying why -- "an unordered_set of NodeIds would
                              make the duplicate check's REPORTED duplicate
                              depend on a hash's bucket order"
hidden mutable state          NONE. zero `static` storage, zero `mutable`,
                              zero `thread_local`, zero `cache` in the
                              implementation. Every `static` match in the file
                              is a `static_cast`
input order                   REMOVED BY SORTING, not merely tolerated.
                              ConstraintSet sorts into ascending DofIndex, so
                              two orderings of the same restraints build sets
                              that compare EQUAL
floating point                NOT INVOLVED. The whole module is integer
                              arithmetic over handles and ordinals. Node
                              POSITIONS are never read -- nothing here
                              computes a distance, a volume or a tolerance, so
                              there is no comparison an optimiser could
                              reorder
```

The last one is worth saying plainly because it is unusual in this codebase: a
DOF numbering is combinatorics, and a milestone with no floating-point
comparison has no `-ffast-math` class of risk and no Debug/Release numerical
divergence to look for. What remains is container order and state, and both are
answered above.

## Repeated construction

`MeshDofMap_NumberingIsIdenticalOverRepeatedBuildsOfTheSameMesh` — 16 rebuilds
of a numbering over a deliberately sparse mesh (handles 1, 4, 10, 11, 500):

```text
map == first                        all 16
dofCount() equal                    all 16
nodes() equal element for element   all 16
dofAt(i) equal for every i in 1..3N all 16
```

`FreeEquationMap_NumberingIsIdenticalOverRepeatedBuilds` — the same for the
equation numbering over a six-node mesh with three scattered constraints.

Both compare the whole object and then the whole mapping, because equality of a
container is not equality of the function it represents if an accessor computes
something.

## Order independence, exhaustively

`ConstraintSet_IsIndependentOfTheOrderConstraintsAreListedIn` walks **all 24
permutations** of a four-element prescribed set and asserts, for each:

```text
ConstraintSet          == the canonical one, and constrained() equal
FreeEquationMap        == the canonical one, and freeDofs() equal
```

and asserts the permutation count is 24, so a loop that terminated early could
not pass silently. The second line is the one that matters: order independence
of the set is useful only if the equations derived from it are identical too.

## Across presets

All 31 tests run in `debug-ext`, `release-ext` and `debug-shared-ext`, from
clean, unfiltered, in the qualification run. `debug-shared-ext` is included for
the reason it always is here: it is the only preset that exercises the DLL
boundary, and this milestone adds a new public header to an existing exported
module. Before the freeze the header was checked for the recurring defect — an
export macro on a header-defined `constexpr` or `inline` entity, which becomes
`dllimport` and fails only in that preset:

```text
grep BETTERCAD_STRUCTURAL_EXPORT StructuralDof.hpp | grep -E 'constexpr|inline'
  -> 0 occurrences
```

`kDofsPerNode`, `componentAt`, `offsetOf`, `isRecognised` and the whole of
`FreeEquationIndex` are header-defined and carry **no** macro; the three map
classes and the seven free functions have out-of-line definitions and carry it.

## Repeat stage

The qualification's repeat stage runs each selected test five times back to
back, in `release-ext` and then `debug-ext`.

An important correction made before launching: the repeat filter inherited from
P17-MAT-001 selected **751** tests and covered only **17 of this milestone's
31** — `StructuralDof_*` matched `unit\.Structural`, and `MeshDofMap_*` matched
`unit\.Mesh` by accident, while `ConstraintSet_*` and `FreeEquationMap_*`
matched nothing at all. A determinism stage run on it would have passed while
claiming a set it had not covered.

Three terms were added — `unit\.MeshDofMap`, `unit\.ConstraintSet`,
`unit\.FreeEquationMap` — and the selection now contains all 31. Verified by
**counting the milestone's own tests inside the selection**, not by reading the
filter:

```text
ctest -N -R "<filter>" | grep -cE "MeshDofMap_|ConstraintSet_|FreeEquationMap_|StructuralDof_"
  -> 31
Total Tests: 765        (751 + 14)
```

A note on `ctest --repeat`: it repeats **per test**, back to back, and fixtures
do not re-run between passes. None of these tests mutates shared input — each
builds its own mesh, document and maps — so repetition is safe. The one that
comes closest, `MeshDofMap_ARemeshInvalidatesTheNumberingAndTheConstraintsBuiltOnIt`,
builds its own document and mesher and remeshes only its own.

## What is NOT claimed

The mesher's own determinism is P16's, and this milestone relies on it in one
place: `StructuralDof_IsDeterministicAcrossARemeshOfTheSameReferenceModel`
asserts the second mesh of the same model has the same node count and the same
handles. If that ever stopped being true the test would fail — and the failure
would be P16's, which is the right place for it. What the test is actually for
is the opposite direction: that identical handles still produce a numbering the
stale constraint set is **refused** against, because the `MeshId` differs.
