# P17-VIZ-001 — the element contours

```text
STATUS:  built, measured, probed and QUALIFIED. ADR-046.
         3835 / 3835 x 3 presets, 980 x 5, 3h15m17s -- FREEZE_ELEMENTS.md.
         P17-VIZ-001 is COMPLETE at 23 of 23.
         The three boxes recorded as BLOCKED for two milestones were blocked
         on a PREMISE, not on a missing capability.
         ZERO diff in the qualified P16 meshing module, checked as a gate.
         ZERO diff in the contour layer either.
```

## The blocker was a correct diagnosis with a wrong conclusion

What `TODO.md`, `REMAINING_SCOPE.md` and ADR-044 all recorded:

> `MeshView::volumeBoundaryOf` builds from the mesh's Triangle3 SURFACE
> elements, so `elementOfTriangle` returns a TRIANGLE's id. An element
> channel's values are per TETRAHEDRON. The two id spaces are disjoint, and
> `meshing::Triangle` carries `id`, `nodes` and `region` — no owning
> tetrahedron, and no accessor in meshing answers for one.
>
> Closing it needs a NEW CAPABILITY IN THE QUALIFIED P16 MESHING MODULE.

**Every sentence of the diagnosis is true. The conclusion does not follow.**

The question "which tetrahedron owns this boundary triangle" only arises if the
boundary is taken from the Triangle3 elements in the first place. Build it from
the **tetrahedra's own exterior faces** and ownership is known by construction,
because the tetrahedron is in hand when the face is emitted.

That is the whole change. It was found by probing before building — the rule
this repository already carries, for the fourth time running.

## What already existed

```text
kTetFaces   the four faces of a Tet4, with winding already a TESTED property
            (MeshView_TetFaceWindingsPointAwayFromTheOppositeVertex)
Facet       already carries its owning ElementId
build       the generic assembler both existing factories go through
```

`tetrahedraOf` already emits `Facet{tetId, face}`, which is why an element
contour **already worked** on an interior view — a tested statement, not a
hope. What was missing was only a *boundary* built the same way.

## What was added

One factory and one enumerator, both in the renderer:

```cpp
static Result<MeshView> volumeBoundaryByTetrahedronOf(const meshing::Mesh&);
enum class MeshSource { ..., VolumeBoundaryByTetrahedron };
```

```text
CHANGED   include/bettercad/renderer/MeshView.hpp   enumerator + factory
          src/renderer/MeshView.cpp                 toString case + factory
                                                    + facetsOfTetrahedronExterior
          tests/renderer/MeshViewTests.cpp           13 -> 19 cases
          tests/renderer/StructuralResultViewTests.cpp  66 -> 69 cases

NOT CHANGED
          meshing -- nothing at all. No new capability, no requalification
                     owed by P16.
          StructuralContour -- nothing at all. triangleCornerBands reads
                     elementOfTriangle and looks the id up in the field, so a
                     view whose element ids are tetrahedra simply works.
```

That `StructuralContour` needed no change is the strongest evidence the design
is right: the layer that consumes the view did not have to learn anything.

## Measured

| | |
| --- | --- |
| tet-derived boundary vs Triangle3 boundary | **identical node triples**, compared BOTH ways, counts required equal first |
| triangle count | equal to the independently counted exterior-face count |
| interior faces drawn | **none** — every drawn face is carried by exactly one tetrahedron |
| winding | **outward for every face**, checked against the body centre |
| element channels contoured | **all seven**: von Mises, StressXX/YY/ZZ, StressXY/YZ/ZX |
| on screen | uniform fill **1** colour, element contour **7** |
| MeshView suite | 19 / 19 |
| viz suite | 69 / 69 |
| combined | **88 / 88**, 0 compiler warnings |

### Why the coincidence check is the one that matters

The two boundaries are built from **different data by different code** — one
from the mesh's Triangle3 elements, one by counting face incidences over the
Tet4 elements. Comparing them checks each against something other than the code
that produced it, which is what makes it independent rather than circular.

It is asserted as a set comparison in **both directions**, with the counts
required equal first and each set's size required to equal the triangle count,
so it cannot pass on a subset or hide a duplicated face.

If the two had disagreed, either ADR-046's premise was wrong or the mesh was,
and both would have been worth knowing. They agree.

### Why the winding check does not use `kTetFaces`

A test that derived the expected normal from the same face table the code uses
would prove only that the table was applied consistently. The block fixture is
**convex**, so for every boundary face the outward normal has a positive
component along (face centroid − body centre) — a property of the solid,
independent of the face table and of P16's own Triangle3 orientation. Every
face is also required to have a non-zero normal first, so a degenerate facet
cannot pass the dot product vacuously.

## The conventions, fixed so a test can hold them

```text
A FACE IS EXTERIOR IFF EXACTLY ONE TETRAHEDRON CARRIES IT. Two is interior.
  Three or more is non-manifold and is REFUSED BY NAME rather than silently
  treated as exterior or resolved by picking one claimant.

THE KEY IS THE SORTED NODE TRIPLE; THE EMITTED FACET KEEPS THE WINDING.
  Sorting is how two tetrahedra are recognised as sharing a face. The winding
  is what makes the normal point out of the solid. Confusing the two gives a
  boundary with arbitrary normals -- which probe E3 exists to catch, because
  the picture still has the right faces in the right places.

DETERMINISM IS BY SORTING, NEVER BY HASHING. An unordered_map would make the
  facet order depend on the standard library's bucketing, and MeshView
  promises byte-identical buffers in Debug, Release and Debug-shared.

ASCENDING ElementId IS THIS FACTORY'S JOB. `build` sorts the vertices and the
  edges but NOT the facets, so the documented triangle order has to be
  delivered by the caller -- which the existing factories get for free from
  their input order and this one does not.

A MESH WITH NO TETRAHEDRON IS REFUSED, not answered with an empty view.

THE TRIANGLE3 BOUNDARY STILL REFUSES AN ELEMENT CHANNEL, and that refusal is
  kept deliberately. The two views are not interchangeable, and a test asserts
  the refusal on one and the success on the other FROM THE SAME field, legend
  and bands -- so the only thing that differs is the view.
```

## Two corrections to my own tests

**An assertion that passed vacuously.** `meshHasContour` returns
`Result<bool>`, so

```cpp
CHECK(viewer.meshHasContour(*shown));        // tests only that the CALL worked
```

is true whatever the answer. Its `CHECK_FALSE` counterpart then *failed* for
the same reason and looked exactly like a defect in `clearMeshContour`. Both
now bind the `Result`, require a value and check the bool — which is what every
pre-existing use in the file already did.

That is the third shape of weakened assertion this milestone has produced, and
the first where the vacuous one and the false failure were the same mistake
seen from both sides.

**A source edited mid-build.** The comment on
`StructuralViz_AnElementChannelNeedsTheOwningTetrahedron` was corrected while
its translation unit was compiling, so the 69/69 that followed was measured on
a binary that predated the edit. Comment-only, so nothing semantic moved — but
it was rebuilt and re-run anyway, 88/88, because "the binary is the source" is
not something to assume when the timestamps say otherwise.

## What is still not drawn

```text
LoadGlyph, deliberately. A pressure or a traction has no single point and
gravity has no location at all, so a per-facet pressure arrow needs facet
centroids through GeometryMeshMap. Named, not half-built.

THE Qt INSPECTOR SURFACE. No panel, no channel selector, no legend widget,
no stale badge, no navigation buttons. Every headless layer one would bind to
is built, tested and qualified; none of it is wired.
```

## Mutation probes

Seven probes against `facetsOfTetrahedronExterior` and the factory, filtered
across **both** suites, because a mutation that broke the view but not the
contour would otherwise look like a kill.

| Probe | Mutation | Verdict |
| --- | --- | --- |
| E1 | an interior face is treated as exterior | KILLED (4) |
| E2 | the shared key is not sorted | KILLED (4) |
| E3 | the sorted key is emitted instead of the winding | KILLED (1) |
| E4 | one of the four tetrahedron faces dropped | KILLED (4) |
| E5 | facets not sorted by element | KILLED (1) |
| E6 | a mesh with no tetrahedron is accepted | KILLED (1) |
| E7 | the non-manifold refusal removed | **SURVIVED**, then closed |

**E3 is the one worth singling out.** Emitting the sorted key instead of the
wound face produces a boundary with *the right faces in the right places* and
arbitrary normals. Nothing about the face set, the counts or the ownership
changes — only the orientation — so only the analytic outward-normal test can
see it, and that one test is what caught it. Had the winding been checked
against `kTetFaces` instead of against the solid, this mutation would have
survived.

E5 and E6 each died to exactly the case written for them, which is what the
one-probe-per-new-test rule is for.

## E7, and a guard I almost recorded as unreachable

E7 survived all 88 cases, and the obvious reading was that a face carried by
three tetrahedra cannot occur in a mesh the mesher produces — which is true,
and which `ATetrahedronDerivedBoundaryDrawsNoInteriorFace` asserts as a
precondition for its own fixture.

**That reading was wrong.** `MeshBuilder` is public, and `addTetrahedron`
checks CONNECTIVITY ONLY — whether four points enclose a positive volume is a
whole-mesh question that validation answers later, by its own documented
decision, with a note in the header explaining that the silence there is
deliberate. So a non-manifold mesh can be built by hand, and the guard is
reachable through the public API.

That makes E7 a **real gap**, not a kept-but-unreachable refusal.
`MeshView_ATetrahedronDerivedBoundaryRefusesANonManifoldFace` builds three
tetrahedra sharing one face, and requires the diagnostic to name the count
rather than merely report that something is wrong.

It is paired with a **two-tetrahedron discriminator**, because without one the
refusal would also be satisfied by a factory that rejected *any* shared face —
which would reject every real volume mesh, since an interior face is exactly a
face shared by two. Two tetrahedra have eight faces, one of them shared, so six
are drawn, and that count is asserted.

The general shape, for the second time tonight: **before recording a guard as
unreachable, ask what the module's own public builders can construct**, not
only what today's callers happen to pass.

Full log in `logs/probe-element.log`.
