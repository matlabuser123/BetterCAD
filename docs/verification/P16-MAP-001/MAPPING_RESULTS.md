# P16-MAP-001 — measured results

```text
SUBJECT:  what the correspondence actually mapped, on every reference fixture
DATE:     2026-10-03
```

Every number here was produced by the test suite and appears in the
qualification logs. None was estimated.

## The required mapping matrix

Recorded by `Map_RecordsTheFaceVocabularyOfTheReferenceFixtures`, which prints
the whole map so the suite's assumptions about which faces are named are
visible rather than buried.

```text
fixture                       CAD    boundary  mapped  unmapped  ambiguous  faces
                             faces    facets                                w/o
                                                                          facets  PASS
BOX 30x20x10 mm                  6        12      12        0         0        0   yes
CYLINDER r8 h20 mm               3       140     140        0         0        0   yes
TUBE r12/r6 h20 mm               4       288     288        0         0        0   yes
BORED 40x30x10, 12 mm hole       7       160     160        0         0        0   yes
BOX on the XZ plane              6        12      12        0         0        0   yes
```

The transformed block maps to the same six faces and the same twelve facets as
the one at the origin, and its `end_cap` facets face **-y** where the other's
face **+z** -- the XZ sketch plane's normal is -Y, so the extrusion runs to
negative y. Measured, not assumed: a mapping done in a stale frame would give
the same normals for both.

**Ambiguous is zero by construction and counted anyway.** The attribution is a
`std::map<ElementId, std::size_t>`, so a facet cannot have two faces; the
report nevertheless counts the facets appearing in more than one per-face list,
which checks the inversion instead of trusting it.

### Face by face

```text
BOX 30x20x10 mm                               facets  reference
  plane  n=(0,-1,0)                                2  side / profile line 1
  plane  n=(1,0,0)                                 2  side / profile line 2
  plane  n=(0,1,0)                                 2  side / profile line 3
  plane  n=(-1,0,0)                                2  side / profile line 4
  plane  n=(0,0,-1)                                2  start_cap
  plane  n=(0,0,1)                                 2  end_cap

CYLINDER r8 h20 mm
  cylinder r=8.000 mm                             72  side / the profile circle
  plane  n=(0,0,-1)                               34  start_cap
  plane  n=(0,0,1)                                34  end_cap

TUBE r12/r6 h20 mm
  cylinder r=12.000 mm                            72  side / the outer circle
  cylinder r=6.000 mm                             72  side / the INNER circle
  plane  n=(0,0,-1)                               72  start_cap   (annular)
  plane  n=(0,0,1)                                72  end_cap     (annular)

BORED 40x30x10 mm, 12 mm through hole
  plane  x3 and x1                              2 ea.  side / profile lines
  plane  n=(0,0,-1)                               40  start_cap   (annular)
  plane  n=(0,0,1)                                40  end_cap     (annular)
  cylinder r=6.000 mm                             72  NO NAME  <- the hole wall
```

**The tube's bore is a named face and the drilled plate's wall is not**, and
that single line is the most informative result in this document. A hole formed
by a profile is swept by that profile's circle and is an ordinary `Side` face;
a hole formed by `cutHole` has a wall its namer never names. Both are mapped;
only the first is selectable. `REFERENCE_AUDIT.md` §7 has the cause.

## Required bidirectional evidence

`MapBox_ReverseMappingAgreesWithTheForwardMapping` walks every face and every
one of its facets and asks the reverse question.

```text
fixture   selected reference        CAD->facet   facet->same ref   mismatch  PASS
BOX       all six, in turn                  12                12          0   yes
TUBE      side / inner circle (bore)        72                72          0   yes
BORED     the unnamed hole wall             72                72*         0   yes
```

\* the reverse answer names the same CAD **face index** and carries an **empty
name list**, which is the correct answer for a face the naming infrastructure
did not name — not a nearby face's name.

The box case is exhaustive: `checked == boundaryFacetCount` is asserted, so no
facet is quietly skipped.

## Required coverage evidence

`MapBox_PartitionsTheBoundaryWithNoGapAndNoOverlap`, the strongest gate in the
suite:

```text
union of the six faces' facet sets    == every boundary facet of the mesh
pairwise intersections                == empty, at facet-identity level
size before deduplication             == size after
```

The same disjoint-and-complete property is asserted for the cylinder's three
faces and the tube's four.

## Planar, cylindrical and hole-wall validation

Judged **geometrically**, from the kernel's own `FaceInfo`, while the mapping
decides by provenance — so the check is independent of the method.

```text
planar face       every vertex of every facet satisfies the face's plane to
                  1e-9 m, AND the facet's winding normal agrees with the
                  face's outward normal (dot > 0.9)
cylindrical face  every vertex at the face's exact radius from its exact axis,
                  to 1e-9 m
cylinder wall     |n_z| < 1e-6, so no cap facet can be among them
cylinder cap      n_z > 0.9 (or < -0.9), and every vertex at the cap's z
hole wall (bore)  dot(outward normal, radial position) < 0 -- OUT OF THE
                  MATERIAL points INTO the void, which is the opposite of
                  "radially outward"
sharp edge        the +z and +x faces' facet sets are disjoint although their
                  vertices satisfy both planes, AND they share at least two
                  nodes, so the faces really do touch
```

The 1e-9 m epsilon has one source, stated once: `P16-SURF-001` measured a
cylinder's triangulation nodes at the true radius within it, and planar faces
are exact. It is a representation epsilon over positions the kernel itself
produced. **The mapping has no tolerance of its own.**

## Required remesh evidence

`MapRemesh_TheSameReferenceResolvesAgainstACoarseAndAFineMesh`.

```text
geometry reference            cylinder r8 h20 mm, side / the profile circle
                              -- the same FaceName value in both cases

coarse mesh                   8 mm target, 0.1 mm deflection
                              738 Tet4, 140 boundary facets, 72 on the face
fine mesh                     2 mm target, 0.02 mm deflection
                              2038 Tet4, 248 boundary facets, 126 on the face

facets on the selected face   72 -> 126; fine > coarse, asserted
mesh generations              different: fine.owns(coarseMap.stamp()) is false
                              and coarse.owns(fineMap.stamp()) is false

geometry reference preserved  YES -- it is the same value; nothing remapped it
old facet IDs treated as      NO. A query mixing a coarse map with the fine
  permanent                   mesh is REFUSED as mapping_stale, not answered
both sets map to the same     YES -- each checked geometrically against the
  current CAD face            kernel's FaceInfo for that mesh
both mappings complete        YES
```

## Required topology-change evidence

`MapTopologyChange_ADeletedFaceBecomesUnresolvedAndIsNeverRebound`.

```text
original geometry reference   the blind hole's HoleBottom, Resolved, with a
                              non-empty facet set

model edit                    the hole becomes a THROUGH hole: its flat bottom
                              ceases to exist. (The edit also clears the depth,
                              because the feature refuses a through hole with
                              one -- and modifyObject reports "no change" as a
                              plain false, so the test asserts the edit really
                              happened rather than describing an unchanged
                              model.)

existing stable-reference     the name is carried by the operation's history,
  result                      and the operation no longer produces that face,
                              so it names nothing
P16-MAP result                Unresolved. The reference is still in
                              `requested`, its face list is empty, its facet
                              list is empty, fullyResolved() is false
silent reassignment           NO. There is no geometric fallback to reassign
                              with
the rest of the mapping       still complete -- so the unresolved reference is
                              not a symptom of a broken map
```

And the preserving direction, `Map_SurvivesATopologyPreservingEdit`: the
block's depth goes 10 → 25 mm, the `end_cap` reference still resolves, and
every facet it maps to is at z = 25 mm — it followed the face rather than
staying where it was.

Also `MapBored_TheCapsKeepTheirNamesThroughTheBoring`: drilling a through hole
is itself a topology change, and P12-STREF's history carries each cap's name
onto the annulus it became. That is the existing infrastructure's guarantee,
and this milestone neither extends nor weakens it.

## Required local-sizing evidence

```text
canonical geometry reference  cylinder r8 h20 mm, end_cap
P16-SIZE target               LocalMeshSizing{.face = <that FaceName>,
                                              .targetSize = 1.5 mm}
P16-MAP target                boundaryFacetsOf(map, <that same FaceName>)
same reference contract       YES -- asserted as value equality between the
                              reference sizing RECORDED as resolved and the
                              reference the mapping resolved
refined-region facets mapped  the facets' owning tetrahedra have a smaller mean
                              edge than the opposite face's, measured
wrong-region facets           0 -- the two faces' facet sets are disjoint, and
                              the whole mapping is complete
```

Both resolve the reference through `geometry::findNamedFaces`. Their
*resolution of mesh entities* differs — sizing tests nodes against the face's
plane or cylinder, the mapping uses provenance — and that difference is recorded
as a carried limitation rather than papered over.

## Independence

```text
change                                      mapping
a material created and assigned             IDENTICAL (whole map compared)
the full mesh-quality evaluation run         IDENTICAL
a display triangulation at 5 mm deflection   IDENTICAL
```

The display case matters most: it proves engineering correspondence does not go
through the kernel's cached tessellation, so a viewer changing its deflection
cannot move a boundary condition.

## Determinism

```text
whole GeometryMeshMap, tube fixture       5 constructions, identical
derived facet set for the bore             5 resolutions, identical
derived node set for the bore              5 derivations, identical
```

The comparison is the whole value: face order, names, signatures, facet lists,
the report and its issue order. Nothing is built from a hash container; faces
are in `listFaces` order, every derived list is sorted and deduplicated, and
the report's issues are sorted by a total key.

## Refusals

```text
input                                          outcome
a mesh built from an earlier geometry revision refused, mapping_stale
a mesh of a different feature (same document)  refused, mapping_stale
a stale model (edited, not regenerated)        refused with P16-GEOM-001's own
                                               diagnostic, unchanged
a coarse map queried against the fine mesh     refused, mapping_stale
a malformed FaceSelector                       refused, geometry_reference_invalid
an empty reference list                        refused, InvalidArgument
a reference to another object                  Unresolved -- a status, not an
                                               error, and no fallback
a tetrahedron's handle as a facet              refused, no_boundary_correspondence
an unknown element handle                      refused, mesh_facet_invalid
a face ordinal of 4                            refused, mesh_facet_invalid
an interior tetrahedron face                   refused, no_boundary_correspondence,
                                               naming it interior
a boundary triangle as a volume element        refused, mesh_facet_invalid
a set with no id / no name / no face           refused
a set listing one face twice                   refused, "same reference"
a report with no facets at all                 complete() is false
```

## Node, element and region sets

```text
query                                    result
CAD face -> boundary facets -> NodeIds   ascending, deduplicated; every node
                                         exists; every node is on a selected
                                         facet; count equals the facets' own
                                         node set; and every one is at the
                                         cap's z
boundary facets -> owning Tet4           ascending, deduplicated; exactly ONE
                                         owner per facet, checked -- none means
                                         the facet is not a face of the volume,
                                         two means it is interior
region -> Tet4                           every tetrahedron of the mesh; count
                                         equals the mesh's own tetrahedron
                                         count; the region's CAD identity is
                                         the feature
all four faces of all twelve tetrahedra  every answer is either a boundary
  of the box                             facet or "interior"; the boundary
                                         count EQUALS the mesh's own, which
                                         also proves this layer's winding table
                                         agrees with tetrahedralBoundary's
```

## Named boundary sets

```text
set                     intent                       coarse    fine     survives
"fixed_end"             cylinder start_cap           resolved  resolved  id, name
                                                                         and intent
                                                                         identical;
                                                                         FACETS
                                                                         DIFFER
"bolt_hole"             tube side / inner circle     resolved  --        only bore
                                                                         facets
"ends"                  box end_cap + start_cap      union, deduplicated, ascending
"pressure" + "heat_flux" both the box's end_cap       equal facets, DIFFERENT ids,
                                                     neither merged
"fixed" + "fixed"       different faces               same display name, different
                                                     ids, different facets
"mixed"                 end_cap + a HoleBottom on a   partially resolved: entry 1
                        block with no hole            Resolved, entry 2
                                                     Unresolved, the set keeps
                                                     its id and name, and the
                                                     resolved half still works
```

Facet identity is **not** persisted across a remesh, and is not required to be:
what is stored is the geometry selection.
