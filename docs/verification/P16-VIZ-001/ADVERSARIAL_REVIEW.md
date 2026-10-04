# P16-VIZ-001 — adversarial review

```text
SUBJECT:  an attempt to disprove the milestone before calling it complete
DATE:     2026-10-04
METHOD:   read the diff against the brief's own attack list and against the
          invariant this milestone exists to protect; then mutation-test all
          five production files.
```

**Three production defects and six test gaps found and fixed.** Every defect
was found by running or measuring something. Five of the six test gaps were in
tests written for this milestone that were passing — which is the part worth
dwelling on, because a passing test that cannot fail is worse than no test: it
occupies the space where a real one would go, and it reports success while
doing it.

Two of the gaps were found by the mutation set rather than by reading, which is
what the mutation set is for.

---

## Finding 1 — the mesh was a thousand times too small

**Severity: real, and it made the feature useless in the GUI. Fixed.**

`MeshView::positions()` are SI metres, because a `meshing::Node` holds a
`Length` and SI is what the project computes in. **OCCT model space is
millimetres** — `core/geometry/occt/OcctBody.hpp`: *"OCCT model space uses
millimetres: its default precision (1e-7) and tolerances are designed for
millimetre-scale models"*. The adapter uploaded metres.

**Why it was invisible for a while.** A mesh displayed ALONE looked perfect:
`fitAll` frames the only thing in the scene, so a box 1000 times too small
fills the view exactly as a correct one would. It only broke beside its own CAD
solid, where `fitAll` framed the union and the mesh became a sub-pixel speck.

The clue was a pair of numbers that should have been one:

```text
mesh alone, isometric, fitted      32674 covered pixels
the same box as a CAD solid        32674
mesh and solid together            54120     <-- one silhouette, two scales
```

The test that caught it was asking something else entirely — whether hiding the
CAD leaves the mesh visible — and it reported `0` covered pixels for a mesh
that was demonstrably displayed.

**Fix.** Every position reaching OCCT goes through `geometry::occt::toModel`,
the project's only metres-to-model conversion. It lives in the OCCT adapter and
NOT in `MeshView`: the adapter is unit-honest SI and is tested headlessly, and
millimetres are a fact about the kernel rather than about the mesh.

**The test that now pins it** is the one that would have caught it on day one:
a boundary mesh of a solid has the same silhouette as the solid, so from a
fixed camera the mesh alone, the solid alone, and both together must cover the
SAME number of pixels. Any scale or frame error gives three different numbers.
It runs on three fixtures — at the origin, offset by (50, 40) mm, and on the XZ
plane, whose normal faces −Y and is the asymmetry most likely to expose a sign
error.

---

## Finding 2 — a selection survived a remesh

**Severity: real, and plausible-looking. Fixed.**

The inspector cleared its selection details when the mesh went away, and not
when the mesh was REPLACED. So after regenerating:

```text
Element 13 (triangle3)
nodes 4 7 9
signed volume ...
aspect ratio 1.83
CAD face 3
```

stayed on screen against a mesh in which element 13 is a **different element**.
`ElementId` values are reused across generations (ADR-031), so nothing about
the number said so, and every field was internally consistent.

**Fix.** The panel remembers the `MeshStamp` whose selection it is showing and
clears when it changes. There is deliberately no attempt to carry a selection
across a remesh: nothing tracks a tetrahedron between meshes, and the kind of
selection that SHOULD survive — a CAD face — survives by being re-resolved
through `P16-MAP-001`, which is a different mechanism entirely.

A GUI claim now asserts it: `mesh: selection after regenerating: Nothing
selected`.

---

## Finding 3 — the mesh command could not mesh an edited model

**Severity: real, and it would have read as a bug in meshing. Fixed.**

Editing the geometry and asking for a mesh failed with

```text
the geometry of 'Solid' (object:2) is stale: it or something it is built from
has changed since it was last built. Regenerate before meshing
```

which is `P16-GEOM-001` doing exactly its job: *"a stale or failed model must
never produce a nominally valid current mesh"*. The window simply never
regenerated.

**Fix.** `generateMesh` regenerates the document first, which is what "mesh
this" means in a CAD application, and reports a regeneration failure as the
mesh's failure — because from the user's side that is what happened: they asked
for a mesh and did not get one. What would have been wrong is meshing the old
body and calling the result current, which the core would refuse anyway.

---

## Findings 4 to 7 — four tests that could not fail

Grouped, because they share one cause and one lesson.

**Finding 4 — a box cannot distinguish two tessellations.** The test for
"the engineering mesh is not the viewer's triangulation" used a block, and
asserted the two triangle counts differ. They do not: a box's faces are planar,
every tessellation of them is the same twelve triangles. A `MeshView` secretly
built from the CAD display triangulation would have produced **byte-identical
buffers and passed**. Fixed by using a bored body, whose cylindrical wall is
the only thing that can tell two tessellations apart.

**Finding 5 — a coarse tessellation request is a no-op.** Even on the bored
body, asking the kernel for a 5 mm deflection returned 160 triangles — exactly
the engineering boundary's 160 — because **OCCT does not COARSEN an existing
triangulation**; it keeps the finer one it already has. Only a finer request is
a real stimulus: 0.01 mm gives 324.

```text
requested deflection    triangles
5 mm                    160   <-- the engineering mesh's own, unchanged
0.01 mm                 324
engineering boundary    160
```

Recorded beyond this milestone: `P16-MAP-001`'s own
`Map_IsUnchangedByADisplayTriangulation` uses 5 mm, so its stimulus is weaker
than it appears. **Its claim still holds** — the map is unchanged either way —
but the call it makes to provoke a change does not provoke one. That is an
observation about a qualified milestone's test strength, not a defect in its
result, and changing a qualified test is not this milestone's to do.

**Finding 6 — a block's boundary cannot refine.** The remesh test asserted
that a finer mesh has more facets on a chosen face, and compared coarse and
fine *volume* sizing on a block. Neither changes a planar face's two triangles:
`SurfaceMeshControls` bounds deflection from the TRUE surface, and
`generateVolumeMesh` requires the boundary to conform to the surface it was
given, so volume sizing cannot refine a boundary at all. Fixed by refining
deflection on the bore wall, where the facet count genuinely grows.

**Finding 7 — a highlight on invisible facets.** The highlight test lit every
*other* render triangle by index and asserted the image changed. From a top
view only 2 of a box's 12 facets are visible, and whether either has an even
index is an accident of the mesher's ordering. Fixed by highlighting all of
them, so whatever is visible must change colour whatever the ordering is.

**The lesson, and it is the same one four times: a test needs a fixture that
can express the difference it is asserting.** A plane is exactly
representable, so no tessellation question can be asked of a box. A subset
chosen by index is a subset chosen at random with respect to visibility. In all
four cases the assertion was correct, the code was correct, and the test was
worth nothing.

---

## Finding 8 — the quality classification test could not fail

**Severity: a test gap, found by a surviving mutation. Closed.**

Two tests compared `classOf` against the report's findings, element by
element, and both passed. The mutation that replaces the whole function with

```cpp
return meshing::QualityClass::Valid;
```

**survived both.**

The cause is `reportOnlyThresholds()`, which the tests used to produce the
report. It sets no limits at all, so nothing is ever classified: every element
of every fixture is `Valid`, the findings list is empty, and a function that
ignores the report entirely and answers `Valid` agrees with it everywhere. The
tests were comparing a constant with a constant.

**Fix.** A policy that actually classifies something — a deliberately tight
aspect-ratio bound, warning at 1.05 and failure at 3.0 — plus two guards that
the earlier version lacked:

```text
REQUIRE(report.warningElements + report.failureElements > 0)   the report
                                                               classified
                                                               something
CHECK(nonValid > 0)                                            and the view
                                                               reported at
                                                               least one
```

Without the second, a `classOf` answering `Valid` unconditionally would still
satisfy every per-element comparison, because most elements genuinely are
valid. The brief asks for a fixture with an intentionally poor element; a
tight threshold on an ordinary mesh is the same thing from the other
direction, and it does not require constructing a bad mesh to get it.

---

## The thread running through this milestone: a plane is exactly representable

Five tests in this milestone were weakened or made meaningless by **one fact**,
and it is worth stating on its own because it will do it again.

> A planar face is represented exactly by two triangles. No deflection, no
> sizing control and no refinement request changes that. So **nothing about
> surface refinement can be demonstrated on a box.**

Where it bit:

```text
1  the remesh test asserted a finer mesh has more facets on a chosen face,
   using coarse and fine VOLUME sizing on a block -- neither changes a planar
   face's two triangles, and volume sizing cannot refine a boundary at all
   because generateVolumeMesh requires the boundary to conform to the surface
   it was given

2  the display-tessellation test asserted the engineering mesh and the kernel's
   tessellation differ, on a block -- they do not, so a MeshView secretly built
   from the CAD triangulation would have produced byte-identical buffers and
   PASSED

3  the same test, once moved to a curved body, asked for a COARSER deflection
   -- and OCCT does not coarsen an existing triangulation, so the call changed
   nothing and the test still proved nothing. Only a finer request is a
   stimulus: 5 mm gives 160 triangles, 0.01 mm gives 324

4  the large-mesh fixture asked a block for 1.5 mm elements and got twelve.
   The resolved sizing record confirms the 1.5 mm was received and used: a
   40 mm block's boundary is irreducibly 40 mm and the target had nowhere to
   act

5  and P16-SIZE-001 had already written all of this down, in a comment at the
   top of its own test file: "OCCT triangulates a PLANAR face with two
   triangles whatever the deflection ... refining the BOUNDARY is
   SurfaceMeshControls' job"
```

**The lesson is not "read the other milestone's comments", though that would
have saved four of these.** It is that a fixture has to be able to EXPRESS the
difference a test asserts, and a box cannot express refinement. Every one of
those five tests had a correct assertion about correct code and was worth
nothing. The fix in each case was a curved face — a cylinder, or the bore wall
of the drilled block — and a stimulus that moves in the direction the kernel
will actually follow.

---

## What was NOT a defect, checked and recorded

**`SetDisplayMode(0)` on a mesh presentation.** Added while chasing Finding 1,
with a comment claiming it fixed the symptom. It did not — the unit mismatch
did. Tested by removal: the `[meshdisplay]` suite passes without it, because
`AIS_InteractiveContext`'s default display mode is already 0. It is kept as a
one-line guard, with the comment corrected to say so, because
`INFRA-VIEWER-001`'s Finding 1 was exactly this shape for `AIS_Shape` — whose
own default IS wireframe.

**Eight lines over the 100-column limit, left alone.** `.clang-format` sets
`ColumnLimit: 100`, and eight of this milestone's lines run to 101-117. They
were not reflowed, and the reason is not indifference: no test enforces the
limit, and **474 committed files already contain 6185 lines over it**,
including files inside qualified milestones. Reflowing eight lines to match a
limit the codebase does not hold would be unrelated cleanup, and it would void
a mutation run that had just finished — a textual change breaks the mutation
patterns, so the set would have to run a third time for no gain in either
correctness or consistency. Recorded as a decision rather than left as an
oversight.

**`MeshQualityReport` carries no mesh identity.** Recorded in
[VIEWER_AUDIT_PART2.md](VIEWER_AUDIT_PART2.md) rather than fixed here: the
adapter pairs a report with the stamp it was evaluated against, which solves it
in scope, and putting a `MeshStamp` inside the report would change
`P16-QUALITY-001`'s qualified data model. Every caller holding a report across
a remesh has the same problem, P17 included.

---

## The brief's attack list

Answered one by one. Where the answer is a test, the test is named.

```text
Can the viewer hold a second authoritative copy of the mesh?
    It holds buffers and two lookup tables, keyed to a MeshStamp, and no mesh.
    Nothing reads an engineering value out of them: inspection reads the
    canonical mesh. compile_fail.meshview.reach-a-mesh-through-a-view proves
    there is no mesh to reach.

Can editing a GUI buffer change the engineering mesh?
    A finished meshing::Mesh HAS NO MUTATOR -- addNode and its siblings are on
    MeshBuilder. compile_fail.meshview.mutate-a-mesh proves it: the invariant
    is structural, not a const that someone has to remember.

Can mesh rendering use the CAD viewer tessellation instead of the P16 mesh?
    MeshDisplay_IsUnaffectedByTheCadDisplayTessellation, on a bored body and
    with a FINER request -- see Findings 4 and 5 for why both matter.

Can the boundary renderer include internal Tet faces?
    No, and not by filtering either: a VolumeMesh carries its boundary AS
    Triangle3 elements, so there is no face-incidence counting in the GUI and
    nothing to get wrong. MeshView_VolumeBoundaryDrawsEveryBoundaryFacetAndNo-
    InteriorFace also checks the count is not a multiple of the tet count.

Can a NodeId be confused with a render vertex index?
Can an ElementId be confused with a triangle primitive index?
    Three compile-failure cases, both directions. The reverse one matters most:
    the mesh enumerates nodes in ascending NodeId, which makes the mistake look
    plausible and usually right -- it is wrong exactly when a volume mesh has
    interior nodes, which a boundary view does not draw.

Can a remesh preserve a selected numeric ElementId incorrectly?
    It did. Finding 2.

Can worst-element navigation use a stale quality report?
    MeshQualityView refuses, and MeshScene cannot be constructed from a
    mismatched set at all: MeshScene_RefusesASetThatDoesNotDescribeOneMesh.

Can a stale CAD-to-mesh mapping highlight old facets?
    MeshHighlight_RefusesAMappingOrACacheFromADifferentGeneration.

Can a current mesh look stale? Can a stale mesh look current?
    MeshDisplay_AStaleMeshDoesNotLookLikeACurrentOne compares the pixels: same
    geometry, same camera, and the images must differ while the covered area
    must not -- staleness recolours, it does not change what is drawn.

Can a failed remesh make the old mesh appear current?
    MeshStatus_AFailedRemeshCannotLookCurrent, and the GUI claim
    gui.mesh.stale.

Can a hidden mesh be deleted?
    MeshDisplay_HidingTheMeshLeavesTheCadBodyAndViceVersa: both presentations
    stay listed through every toggle.

Can the quality heatmap recalculate metrics differently from core?
Can the GUI contain separate threshold constants?
    MeshScene_ClassifiesEveryElementAsTheReportDoes compares every element
    against the report. And there is not one comparison against a quality
    number anywhere in src/renderer or apps/bettercad -- the classification is
    read, never computed.

Can a hole-wall highlight select the outer cylinder?
    MeshHighlight_HighlightsAnUnnamedHoleWallByItsPlaceInTheMap checks every
    highlighted facet's nodes lie within 6.1 mm of the bore axis, which the
    outer faces of a 40x30 block cannot satisfy.

Can a transformed body render in the wrong coordinates?
    MeshDisplay_TheMeshOverlaysACadSolidThatIsNotAtTheOrigin, on three
    fixtures including the XZ plane.

Can rendering snap poor mesh nodes to CAD and hide conformity defects?
    MeshView_DrawsOnlyTheNodesItsFacetsReference compares every drawn position
    with the canonical node EXACTLY -- no tolerance, so no room to "improve"
    one.

Can quality inspection mutate the mesh? Can switching display mode?
    MeshInspection_NothingItDoesChangesTheMesh and
    MeshDisplay_StyleChangesTheImageAndNotTheMesh, both by whole-mesh
    fingerprint.

Can one AIS object per Tet destroy usability?
    One batched presentation per mesh, asserted:
    viewer.displayed().size() == 1 with the mesh displayed.

Can a stale render cache survive M1 to M2?
    MeshView_DoesNotDescribeADifferentMeshGeneration, and every entry point
    that pairs a cache with a mesh refuses a mismatch.

Can a viewer index become persistent boundary identity?
    A render index is a std::size_t that only MeshView translates; a
    PresentationId is viewer-local (four compile-failure cases from
    INFRA-VIEWER-001).

Can an interior facet be highlighted as a CAD boundary?
    sourceFaceOf answers NoBoundaryCorrespondence for a face shared by two
    tetrahedra -- P16-MAP-001's own definition, not a second one.

Can a material edit invalidate the mesh display?
    P16-MAP-001 established that a material assignment leaves the whole map
    identical; the state here is derived from the geometry revision, which a
    material does not move.
```

---

## Mutation testing

Nineteen plausible mistakes across all five production files, each followed by
a rebuild and the `[renderer]` suite. Harness, list and log in
`qualification/mutation/`.

`mutate.sh` and `apply.py` are carried **byte-identical** from
`INFRA-VIEWER-001` (`git hash-object` compares equal). The one thing that had
to change is in `run-mutations.sh` beside them rather than in the harness:
`mutate.sh` calls `python`, and PATH on this machine sometimes resolves that to
the Windows Store alias — a stub that prints *"Python was not found"* and
**exits 0**. The first attempt therefore completed in two seconds with an empty
`results.txt` and no error at all, which is exactly the kind of silent nothing
that could be read as a pass.

### The first run, and what it found

```text
killed by a test   15
compiler only       1
SURVIVED            3
```

The three survivors split into one real gap and two faults of my own, which is
worth separating because they carry opposite lessons.

```text
GENUINE GAP      "every element is classified Valid" survived. Finding 8.

MY MUTATION WAS A NO-OP
                 "an unresolved reference is reported as resolved" inserted
                 set.requested.clear() BEFORE the push_back that fills it, so
                 it cleared an empty vector. A mutation that does nothing
                 cannot be killed, and says nothing about the tests. Moved
                 after the push_back.

UNREACHABLE, AND REMOVED
                 "the signed volume is reported without its sign" is the
                 IDENTITY on every mesh P16 will produce: a valid Tet4 is
                 positively oriented, generateVolumeMesh refuses an inverted
                 one, and a VolumeMesh cannot be hand-built because its
                 constructor is private by ADR-030. So no test reachable from
                 the public API can distinguish abs(v) from v. It is removed
                 rather than carried as a permanent survivor, and replaced by
                 one in the same file that a test must catch. What IS asserted
                 is tighter than a sign check: the inspection's volume must
                 equal meshing::signedVolume's result EXACTLY, so the
                 inspection cannot compute its own.

COMPILER ONLY, AND REWRITTEN
                 "a stale mesh is drawn exactly like a current one" removed a
                 ternary and left staleFacetColour() unreferenced, which
                 -Wunused-function turned into a build failure. A compiler kill
                 shows the mutant would not build, NOT that a test would
                 notice. Rewritten to neutralise setStale instead, which keeps
                 every name used.
```

### The qualifying run

```text
 1  render edges are not deduplicated, so every shared edge is drawn twice  MeshView.cpp              killed, 20 failed assertions
 2  the vertex buffer holds every node, not only the ones drawn             MeshView.cpp              killed, 1 failed assertion
 3  a vertex index past the buffer is answered instead of refused           MeshView.cpp              killed, 1 failed assertion
 4  an element that is not a tetrahedron is skipped instead of refused      MeshView.cpp              killed, 2 failed assertions
 5  a tetrahedron face is wound the wrong way round                         MeshView.cpp              killed, 1 failed assertion
 6  the volume boundary reports itself as the engineering surface           MeshView.cpp              killed, 3 failed assertions
 7  a render cache claims to describe any mesh                              MeshView.hpp              killed, 2 failed assertions
 8  the generation check passes whatever it is given                        MeshInspection.cpp        killed, 7 failed assertions
 9  a failed generation attempt leaves the state looking current            MeshInspection.cpp        killed, 2 failed assertions
10  the mesh state ignores the document's geometry revision                 MeshInspection.cpp        killed, 2 failed assertions
11  every element is classified Valid                                       MeshInspection.cpp        killed, 14 failed assertions
12  worst-element navigation answers the first metric whatever was asked    MeshInspection.cpp        killed, 4 failed assertions
13  an unresolved reference is reported as resolved                         MeshInspection.cpp        killed, 1 failed assertion
14  a scene accepts a quality report from another mesh generation           MeshScene.cpp             killed, 1 failed assertion
15  a scene accepts a mapping from another mesh generation                  MeshScene.cpp             killed, 1 failed assertion
16  mesh positions reach the kernel in SI metres instead of model units     OcctMeshPresentation.cpp  killed, 3 failed assertions
17  a highlight index past the render buffer is accepted                    OcctViewer.cpp            killed, 1 failed assertion
18  a stale mesh is drawn exactly like a current one                        OcctMeshPresentation.cpp  killed, 2 failed assertions
19  every node is reported as interior, so none has a CAD face              MeshScene.cpp             killed, 2 failed assertions

19 killed by a test   0 killed by the compiler   0 SURVIVED   0 not applied
```

**Every mutation killed by a TEST.** None was left to the compiler and none
survived. The run was made once, uninterrupted, against the tree that was then
frozen; the first run's results are kept beside it as `results-first-run.txt`
because what they found is the reason three of these mutations and two of the
tests look the way they do.

---

## Carried limitations

```text
NO CLIPPING OR SECTION PLANE
    Interior inspection is implemented as "the chosen tetrahedra, as their own
    view", which needs no clipping and does not turn the whole volume into
    geometry. Graphic3d_ClipPlane is available in the dependency, so a section
    mode is an absence of code rather than of capability. The checklist marks
    interior inspection OPTIONAL and this is the cheaper honest answer.

NO ELEMENT LABELS IN THE 3D SCENE
    FreeType is deliberately off (INFRA-VIEWER-001), so OCCT draws no glyphs
    in the view. ElementId is shown in the inspector panel, which is what the
    brief's "optional on-screen label" permits.

NO QUALITY HEATMAP
    Quality is inspected per element and navigated per metric. A colour map
    over the whole mesh would need a legend bound to the report's categories,
    and nothing yet asks for one; the classification is already reported for
    every element that is selected.

THE DISPLAY TESTS NEED A GL IMPLEMENTATION
    [meshdisplay] creates a real driver and fails rather than skips without
    one. Everything with engineering content -- [meshview], [meshinspection],
    [meshscene], and all ten GUI claims -- is headless and runs anywhere.

ONE BODY PER DOCUMENT IN THE GUI
    generateMesh refuses a document with more than one result body, saying
    that choosing between them needs a model tree. That is honest about the
    missing UI rather than silently meshing the first one.

A PICK AT A NON-UNIT DEVICE PIXEL RATIO IS STILL NOT CLICKED
    Carried from INFRA-VIEWER-001 and unchanged: the logical-to-device
    conversion's arithmetic is measured, but that a pick lands under the
    cursor at 150% needs a cursor.
```

## Revision

First issue, 2026-10-04.
