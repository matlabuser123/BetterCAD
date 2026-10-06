# P16-REFMOD-001 — results

```text
SOURCE:   every figure here is printed by the suite or the runner. The RESULTS
          table is emitted by
          MeshReferenceSuite_EveryValidModelMeshesAndTheInvalidOneIsRefused;
          the per-model lines by
          bettercad_example_reference_models --meshes, whose output is kept at
          qualification/reference-run-debug-ext.txt
PRESET:   figures from debug-ext. release-ext and debug-shared-ext produce the
          SAME counts and the SAME volumes -- see CROSS-PRESET below
```

## Every model

```text
| Model      | Outcome | Nodes | Tet4 | Vanalytic mm^3        | Vmesh mm^3           | RelError  | MinTetVol mm^3 | Inv | Warn | Fail | Facets |
| RM-MESH-01 | mesh    |     8 |    6 | 294000                | 294000.00000000006   | 1.980e-16 | 49000          |   0 |    0 |    0 |     12 |
| RM-MESH-02 | mesh    |   140 |  508 | 117809.72450961724    | 117212.51992517807   | 5.069e-03 | 0.475278       |   0 |    0 |    0 |    140 |
| RM-MESH-03 | mesh    |    81 |  126 | 63517.699835307561    | 63560.69856538716    | 6.770e-04 | 39.6433        |   0 |    0 |    0 |    160 |
| RM-MESH-04 | mesh    |   261 |  808 | 81430.081581047431    | 81017.293772282923   | 5.069e-03 | 0.202694       |   0 |    0 |    0 |    288 |
| RM-MESH-05 | mesh    |     9 |   12 | 14400                 | 14399.999999999998   | 1.263e-16 | 1199.93        |   0 |    0 |    0 |     12 |
| RM-MESH-06 | mesh    |     9 |   12 | 118800                | 118800.00000000003   | 2.450e-16 | 9899.93        |   0 |    0 |    0 |     12 |   base
| RM-MESH-06 | mesh    |     9 |   12 | 118800                | 118799.99999999999   | 1.225e-16 | 9900           |   0 |    0 |    0 |     12 |   placed
| RM-MESH-07 | mesh    |    61 |  267 | 240000                | 239999.99999999997   | 1.213e-16 | 31.5733        |   0 |    0 |    0 |     12 |
| RM-MESH-08 | refusal |   n/a |  n/a | n/a                   | n/a                  | n/a       | n/a            | n/a |  n/a |  n/a |    n/a |
```

`n/a`, not zero. RM-MESH-08 has no body, so it has no volume and no elements —
and the brief is explicit that a failure model's mesh fields must not be forced
to a fake zero.

### The analytical volume table, with OCCT as the second oracle

```text
| Model                 | Vanalytic mm^3     | Vocct mm^3         | Vmesh mm^3         | OCCT rel err | Mesh rel err | PASS |
| MeshBlock             | 294000             | 293999.99999999994 | 294000.00000000006 | 1.980e-16    | 1.980e-16    | yes  |
| MeshCylinder          | 117809.72450961724 | 117809.72450961718 | 117212.51992517807 | 4.941e-16    | 5.069e-03    | yes  |
| MeshPlateWithHole     | 63517.699835307561 | 63517.699835307569 | 63560.69856538716  | 1.146e-16    | 6.770e-04    | yes  |
| MeshTube              | 81430.081581047431 | 81430.081581047416 | 81017.293772282923 | 1.787e-16    | 5.069e-03    | yes  |
| MeshThinPlate         | 14400              | 14400              | 14399.999999999998 | 0            | 1.263e-16    | yes  |
| MeshTransformedBase   | 118800             | 118799.99999999999 | 118800.00000000003 | 1.225e-16    | 2.450e-16    | yes  |
| MeshTransformedPlaced | 118800             | 118800             | 118799.99999999999 | 0            | 1.225e-16    | yes  |
| MeshLocalRefinement   | 240000             | 240000             | 239999.99999999997 | 1.213e-16    | 1.213e-16    | yes  |
| MeshOpenProfile       | none               | none               | none               | n/a          | n/a          | yes  |
```

`Vocct` is the kernel's own integration of the CAD body, used as a **second
implementation** of the volume rather than as the oracle — the closed form is
the oracle, and OCCT agreeing with it to 4.9e-16 at worst is an independent
cross-check of both. The mesh error for a curved model is the chord error of
its boundary, bounded below.

**The four planar models recover their volume to 2.5e-16 relative**, which is
double-precision accumulation and nothing else: their facets tile the exact
solid. The three curved models sit inside a two-sided bound derived from the
declared deflection, and **RM-MESH-03's sits above its analytic volume while
RM-MESH-02's and RM-MESH-04's sit below** — the direction prediction, confirmed.
See [ANALYTICAL_VALIDATION.md](ANALYTICAL_VALIDATION.md).

## Element orientation

Computed in the suite from the node coordinates — a determinant per element, not
a production call.

```text
| Model      | Positive | Zero | Negative | Non-finite |
| RM-MESH-01 |        6 |    0 |        0 |          0 |
| RM-MESH-02 |      508 |    0 |        0 |          0 |
| RM-MESH-03 |      126 |    0 |        0 |          0 |
| RM-MESH-04 |      808 |    0 |        0 |          0 |
| RM-MESH-05 |       12 |    0 |        0 |          0 |
| RM-MESH-06 |   12 x 2 |    0 |        0 |          0 |
| RM-MESH-07 |      267 |    0 |        0 |          0 |
                 TOTAL 1751 positive, 0 zero, 0 negative, 0 non-finite
```

Every model also passes the rest of the shared structural audit: no missing or
repeated node handle, no duplicate tetrahedron, every element in a valid region,
every coordinate finite, the tetrahedra's own face incidence consistent (no face
used by three or more), the one-sided face set exactly equal to the stored
triangle set, and a **positive** enclosed volume from the boundary triangles'
winding — which is what makes the hole-wall normal check below meaningful.

## Boundary conformity and mapping

```text
| Model      | CAD faces | Named | Unnamed | Facets | Mapped | Unmapped | Twice | No facets |
| RM-MESH-01 |         6 |     6 |       0 |     12 |     12 |        0 |     0 |         0 |
| RM-MESH-02 |         3 |     3 |       0 |    140 |    140 |        0 |     0 |         0 |
| RM-MESH-03 |         7 |     7 |       0 |    160 |    160 |        0 |     0 |         0 |
| RM-MESH-04 |         4 |     4 |       0 |    288 |    288 |        0 |     0 |         0 |
| RM-MESH-05 |         6 |     6 |       0 |     12 |     12 |        0 |     0 |         0 |
| RM-MESH-06 |     6 x 2 | 6 x 2 |       0 | 12 x 2 | 12 x 2 |        0 |     0 |         0 |
| RM-MESH-07 |         6 |     6 |       0 |     12 |     12 |        0 |     0 |         0 |
```

**`unnamed_faces 0` everywhere**, which is the payoff from building RM-MESH-03's
and RM-MESH-04's holes as inner loops of their profile sketches rather than as
drilled features: a drilled hole's wall carries no `FaceName` at all, and the
model could not have mapped it.

Every model's `GeometryMeshMappingReport::complete()` is true, with no issues.

### Region by region

```text
| Model      | Region           | Facets | Resolved |
| RM-MESH-01 | fixed_end        |      2 | yes      |
|            | loaded_end       |      2 | yes      |
| RM-MESH-02 | bottom_cap       |     34 | yes      |
|            | top_cap          |     34 | yes      |
|            | lateral_wall     |     72 | yes      |
| RM-MESH-03 | hole_wall        |     72 | yes      |
|            | clamped_faces    |      4 | yes      |   two faces in one set
| RM-MESH-04 | outer_wall       |     72 | yes      |
|            | inner_wall       |     72 | yes      |
|            | bottom_annulus   |     72 | yes      |
|            | top_annulus      |     72 | yes      |
| RM-MESH-05 | broad_face       |      2 | yes      |
| RM-MESH-06 | datum_face       |      2 | yes      |   both placements
|            | first_side       |      2 | yes      |
| RM-MESH-07 | refined_face     |      2 | yes      |
|            | coarse_face      |      2 | yes      |
```

RM-MESH-02's three sets partition its 140 facets exactly (34 + 34 + 72), and
RM-MESH-04's four partition its 288 (72 x 4), with **zero shared facets between
any pair** — checked pairwise, in both directions.

### Orientation of the curved walls

```text
| Model      | Wall       | Facets | Facing the axis | Facing away | Neutral |
| RM-MESH-03 | hole_wall  |     72 |              72 |           0 |       0 |
|            | outer side |      2 |               0 |           2 |       0 |
| RM-MESH-04 | outer_wall |     72 |               0 |          72 |       0 |
|            | inner_wall |     72 |              72 |           0 |       0 |
```

This is the brief's named trap, measured: *outward from the material* for a
bore's wall means **toward** the axis, and for a boss's wall means away from it.
A check that assumed the radial direction would pass one and fail the other, so
the suite checks both on the tube and checks an outer face on the plate as the
control.

### Walls at different radii

```text
RM-MESH-04  every outer-wall node is within 1e-9 mm of Ro = 30 mm
RM-MESH-04  every inner-wall node is within 1e-9 mm of Ri = 18 mm

            asserted on the MINIMUM and the MAXIMUM of each wall's node radii,
            so one misattributed facet would break it
```

The geometric proof that the two names did not simply swap — a mapping that
confused them would pass every disjointness check above.

## Void preservation

Independent occupancy, with a radius derived from the declared deflection and
nothing else: the void certainly contains the disc of radius `r - d`, so anything
inside that disc is in the void with no appeal.

```text
| Model      | Void   | r mm | Safe r mm | Nodes inside | Centroids inside | In the chord band | Closest node mm |
| RM-MESH-03 | hole   |   15 |     14.75 |            0 |                0 |  reported, > 0    | >= 14.75, < 15  |
| RM-MESH-04 | bore   |   18 |     17.75 |            0 |                0 |  reported, > 0    | >= 17.75, < 18  |
```

```text
VOID OCCUPANCY VIOLATIONS: 0
```

And the check is **not vacuous**: the closest node is inside `r` but not inside
`r - d`, so the mesh really does come up against the region being tested. The
suite asserts both halves.

The same check is re-run on RM-MESH-03 **after** its thickness edit, against the
new geometry.

## Quality

Under P16-QUALITY-001's own default policy, `reportOnlyThresholds()`, which
carries no thresholds at all — so nothing can be a Warning or a Failure, and
`satisfiesPolicy()` reduces to structural validity. The brief forbids adding
reference-model thresholds, and P17 owns what a solver needs.

The whole matrix, with the **worst element per metric by handle** — because a
report naming a bad value without saying which element holds it cannot be acted
on:

```text
| Model                 | Tet4 | Inv | Warn | Fail | WorstAspect | WorstRadiusRatio | elem | MinDihedral | elem | MaxDihedral | MinTetVol mm^3 | elem |
| MeshBlock             |    6 |   0 |    0 |    0 |     4.0933  |       0.43725    |    1 |   14.6211   |    1 |   115.641   | 49000          |    1 |
| MeshCylinder          |  508 |   0 |    0 |    0 |    16.0391  |       0.00124845 |  504 |    0.362683 |   54 |   177.113   | 0.475278       |  369 |
| MeshPlateWithHole     |  126 |   0 |    0 |    0 |    20.4856  |       0.0129063  |  120 |    1.11273  |    6 |   165.482   | 39.6433        |  122 |
| MeshTube              |  808 |   0 |    0 |    0 |    14.8994  |       0.00634594 |  462 |    2.3722   |  462 |   170.046   | 0.202694       |  546 |
| MeshThinPlate         |   12 |   0 |    0 |    0 |    80.0062  |       0.000322641|   12 |    0.71612  |   12 |   178.709   | 1199.93        |    1 |
| MeshTransformedBase   |   12 |   0 |    0 |    0 |     3.88104 |       0.127381   |    6 |   14.9313   |    6 |   152.325   | 9899.93        |    4 |
| MeshTransformedPlaced |   12 |   0 |    0 |    0 |     3.88104 |       0.127381   |    5 |   14.9314   |    5 |   152.325   | 9900           |    2 |
| MeshLocalRefinement   |  267 |   0 |    0 |    0 |     5.61104 |       0.0411619  |  139 |    7.89468  |  101 |   165.695   | 31.5733        |  110 |
| RM-MESH-08            |  n/a | n/a |  n/a |  n/a |     n/a     |       n/a        |  n/a |    n/a      |  n/a |   n/a       | n/a            |  n/a |
```

**RM-MESH-05 is a genuine stress case and it is not hidden.** Its worst radius
ratio is **1355 times** worse than RM-MESH-01's, its minimum dihedral falls from
14.6 degrees to 0.72, and its maximum reaches 178.7 — a nearly flat tetrahedron.
Every one of those elements is structurally valid, `invalid = 0`, and the suite
reports the numbers rather than a verdict nobody set.

> **And the thin plate is not the worst mesh in the suite, which the matrix
> makes plain.** RM-MESH-02's cylinder has a **lower** minimum dihedral — 0.363
> degrees against the plate's 0.716 — and RM-MESH-03's plate-with-hole has a
> worse aspect ratio than anything but the thin plate. Those are the slivers a
> chord-polygon boundary produces where the facets are long and thin along a
> curved wall, and they are a property of the approved pipeline: the boundary is
> fixed before the backend sees it, and the backend must conform to it.
>
> This is recorded rather than smoothed over because it is exactly the kind of
> behaviour the brief says the suite exists to expose. It is also why the
> thin-plate comparison is made against **RM-MESH-01**, a well-proportioned
> planar body, and not against "the rest of the suite" — which would have been
> a weaker claim and, for the minimum dihedral, a false one.
>
> No threshold is attached to any of it, because P16 has none to attach: these
> are numbers for P17 to state a requirement against.

### RM-MESH-05's sizing sensitivity, recorded whatever it shows

```text
| Target | Nodes | Tet4 | Worst radius ratio | Min dihedral deg |
|  20 mm |     9 |   12 |        0.000322641 |         0.71612  |
|   5 mm |    11 |   19 |        0.00034409  |         0.755676 |
|   1 mm |    12 |   24 |        0.00021803  |         0.567609 |
```

**Refining does not help, and the suite says so.** A 1 mm target on a 1.5 mm
plate produces 24 elements, not thousands, because the boundary is two triangles
per planar face and the volume target cannot subdivide it. The brief forbids
changing settings until a PASS appears and then hiding the bad result; all three
planned cases are here, and every one of them is structurally valid.

## Local refinement

```text
global 20 mm, local 6 mm on the y = 0 side face (F); band reach 12 mm

| Mesh           | Nodes | Tet4 | F tets | F nodes | F median | F min   | G tets | G nodes | G median | G min   |
| global only    |     9 |   12 |      2 |       4 | 61.6442  | 40      |      2 |       4 | 61.6442  | 40      |
| + local at F   |    61 |  267 |     25 |       8 | 28.9891  |  8.70349|     10 |       4 | 42.0199  | 11.7644 |
| + local at G   |    57 |  246 |      7 |       4 | 40       | 11.3203 |     25 |       9 | 28.7113  |  7.37507|
```

**The mirror is the decisive test.** A third mesh refines G instead of F with
everything else identical, so the same band can be compared between the two:

```text
at F   28.99 mm when F is refined   vs   40.00 mm when G is      PASS
at G   28.71 mm when G is refined   vs   42.02 mm when F is      PASS
nodes near F   8 when F is refined  vs    4 when G is            PASS
nodes near G   9 when G is refined  vs    4 when F is            PASS
```

A control that refined the whole body, or that resolved to the wrong face, cannot
pass both halves. **The qualification is not the total element count** — which
the brief forbids — although it is reported: 12 to 267.

**And the slab is at the right face**, which is the P16-SIZE + P16-MAP
integration: the resolved `BoxSizeRestriction` starts at y = 0 and reaches
inward, with the mirrored control's slab ending at y = 60 instead.

> **Why the band and not the face's own facets.** The owners of F's two boundary
> facets keep a **57.85 mm** median edge in a mesh whose element count the
> control multiplied by twenty-two. OCCT triangulates a planar face with two
> triangles whatever the deflection, so the elements touching them must span the
> face however fine the interior is. Local sizing refines the *volume* near a
> face — that is what a slab means — so the volume near the face is where it has
> to be measured. The first version of this test measured the facets' owners and
> reported no refinement at all.

## Model-change remeshing

```text
| Model      | Edit             | V1 analytic mm^3   | V2 analytic mm^3   | V2 mesh mm^3       | Old state      | New state |
| RM-MESH-01 | a 120 -> 150 mm  | 294000             | 367500             | 367500             | stale_geometry | current   |
| RM-MESH-03 | t 12 -> 16 mm    | 63517.699835307561 | 84690.266447076749 | 84747.598087182982 | stale_geometry | current   |
```

**Stale for the GEOMETRY, not the intent** — the distinction P16-CMD-001 was
built for, and one a single boolean could not express. The new mesh is checked
against the **new** analytic volume, which is what catches a reused M1: the old
volume is 20% and 25% away respectively, and the suite asserts the new mesh is
more than 10% from the old analytic value as well as within tolerance of the new
one.

RM-MESH-03's new mesh is re-checked for void occupancy, and is inside the bound
recomputed for the new thickness.

## Settings-change remeshing

```text
RM-MESH-02, global target 12 -> 6 mm, geometry untouched

               508 tets -> 1977 tets
median edge  14.6718 mm -> 9.59777 mm
mean edge                   falls
state           current -> stale_INTENT -> current
CAD volume                  unchanged, still 117809.72450961724 mm^3
```

**Stale for the INTENT, not the geometry.** `isStale(document, mesh)` is false
and `isStale(document, mesh, controls)` is true, which is exactly the
invalidation precision the milestone exists for. The old mesh stays
**inspectable** throughout, which P16-VIZ-001's stale-state contract needs.

On the cylinder rather than the block, and the choice is forced: a block's
boundary is irreducible, so a global target has nowhere to act on it — this
suite's own sizing table shows three levels giving one mesh.

RM-MESH-07's local size is edited separately, 6 mm -> 3 mm, measured in the band
at F (both the median edge and the node density fall), and undone.

## Undo and redo

```text
RM-MESH-02, through CommandHistory and SetGlobalMeshSizeCommand

S1  12 mm   508 tets,  median 14.6718 mm
S2  24 mm   361 tets,  median 19.0971 mm
undo to S1  508 tets,  median 14.6718 mm        IDENTICAL to S1
redo to S2  361 tets,  median 19.0971 mm        IDENTICAL to S2

history entries: 1
```

Every mesh is validated against the intent that is **current at that point**,
and each records the target it used. Undo and redo are exact, not approximate.

**One history entry for one edit**, whatever the meshes cost — the rule
P16-CMD-001 was given in as many words: the history stores size intent, not a
hundred thousand tetrahedra.

S2 is **coarser**, not finer, and the direction is chosen for cost rather than
for meaning: this case meshes four times, and what it proves is that each mesh
follows the intent current at that point, which two distinguishable meshes
establish whichever way round they are. Refining to 6 mm gave 1977 tetrahedra
and cost **53 seconds per run** in a debug build for nothing the case asserts;
at 24 mm it is 3 seconds. The *finer* direction is proved by the
settings-change case, which meshes twice and is about the scale.

## Save / load / regenerate

```text
| Model      | File bytes | Intent after load | Mesh in the file? | Mesher after load | Regenerated mesh |
| RM-MESH-01 |       5092 | identical         | NO                | empty, no_mesh    | identical        |
| RM-MESH-03 |       6212 | identical         | NO                | empty, no_mesh    | identical        |
| RM-MESH-07 |       5435 | identical         | NO                | empty, no_mesh    | identical        |
```

The canonical fingerprint is the whole `MeshControlDefinition` — the body, the
discretisation, the sizing, the quality policy and the boundary sets — and it
compares **equal** across the save and the load. Deliberately not the mesh: a
fingerprint containing it could not tell a restored mesh from a regenerated one.

**The generated mesh is not in the file**, checked against the bytes rather than
asserted: no `"nodes"`, no `"tetrahedra"`, no `"elements"` key appears, and the
files are five to six kilobytes where a mesh of even these small models is tens
of kilobytes of coordinates.

**Boundary-set identity survives**: the same `BoundarySetId`, the same name and
the same `FaceName` list, set by set. So does the local control's
`GeometryReference` — which is why RM-MESH-07 is in this list rather than a
plain block.

After loading, the mesher holds nothing and reports `no_mesh` until something
asks it to generate; regeneration then reproduces the mesh **bitwise** — the same
volume double and the same canonical node set — and the mapping report compares
equal. The current facet handles are new, which is allowed: facet IDs are derived
and a remesh reassigns them.

## CLI

Fresh processes, the real executable, resolved per preset through
`$<TARGET_FILE:bettercad_cli>` so nothing comes from `PATH`:

```text
<BETTERCAD_BUILD_ROOT>/debug-ext/bin/bettercad-cli.exe
<BETTERCAD_BUILD_ROOT>/release-ext/bin/bettercad-cli.exe
<BETTERCAD_BUILD_ROOT>/debug-shared-ext/bin/bettercad-cli.exe
```

Each preset therefore runs the binary it has just built. The documents those
processes read are written by
`<BETTERCAD_BUILD_ROOT>/<preset>/bin/bettercad_example_reference_models.exe`,
again per preset, as a ctest `FIXTURES_SETUP`. HEAD, the tree fingerprint and
the presets are in [FREEZE.md](FREEZE.md).

```text
| Model      | Command         | Exit | Asserted                                              |
| RM-MESH-01 | mesh-settings   |    0 | global 20 mm, local (0), two sets with their IDs       |
| RM-MESH-01 | mesh-generate   |    0 | 8 nodes, 6 elements, 12 facets, 0.000294 m^3          |
| RM-MESH-01 | mesh-info       |    0 | state current                                         |
| RM-MESH-01 | mesh-validate   |    0 | dataValid true, 0 issues, empty issue list            |
| RM-MESH-01 | mesh-quality    |    0 | structurallyValid true, invalidElements 0             |
| RM-MESH-03 | mesh-generate   |    0 | 81 nodes, 126 elements, 160 facets, 6.356069856538716e-05 m^3 |
| RM-MESH-03 | mesh-boundaries |    0 | hole_wall resolved 72 facets; clamped_faces 4         |
| RM-MESH-03 | info/validate/quality |  0 | as RM-MESH-01                                   |
| RM-MESH-04 | mesh-boundaries |    0 | outer_wall 72, inner_wall 72, both annuli 72          |
| RM-MESH-07 | mesh-settings   |    0 | local sizing (1): face:LocalBlock:side:N  6 mm        |
| RM-MESH-07 | mesh-generate   |    0 | 61 nodes, 267 elements, 0.00023999999999999998 m^3    |
| RM-MESH-07 | mesh-boundaries |    0 | refined_face 2, coarse_face 2                         |
| RM-MESH-07 | info/validate/quality |  0 | as RM-MESH-01                                   |
| RM-MESH-08 | mesh-generate   |    1 | regeneration_failed, OpenSolid named, profile open    |
| RM-MESH-08 | mesh-info       |    1 | the same                                              |
| RM-MESH-08 | mesh-validate   |    1 | the same                                              |
| RM-MESH-08 | mesh-quality    |    1 | the same                                              |
| RM-MESH-08 | mesh-boundaries |    1 | the same                                              |
```

RM-MESH-08's diagnostic, in full, from a separate process:

```text
bettercad-cli mesh-generate: regeneration_failed: OpenSolid (object:5) did not
regenerate: OpenSolid: the profile is open: an edge ends at (0, 0) mm without a
neighbour
```

**It names the failing feature and the real cause**, not a missing mesh control
— which is what a model without one would have reported, and the defect
P16-CLI-001's mutation M6 found. Nothing is printed to stdout.

## Core / CLI equivalence

```text
| Model      | Quantity    | Core (in process)  | CLI (fresh process)      |
| RM-MESH-01 | nodes       |                  8 |                        8 |
|            | Tet4        |                  6 |                        6 |
|            | facets      |                 12 |                       12 |
|            | volume      | 294000.00000000006 mm^3 | 0.000294 m^3 (same double) |
|            | validation  | dataValid, 0 issues | dataValid true, 0 issues |
|            | quality     | invalid 0           | invalidElements 0        |
| RM-MESH-03 | nodes/Tet4  |            81 / 126 |                 81 / 126 |
|            | facets      |                 160 |                      160 |
|            | volume      | 63560.69856538716 mm^3 | 6.356069856538716e-05 m^3 |
|            | mapping     | hole_wall 72 facets | hole_wall resolved 72 facets |
| RM-MESH-07 | nodes/Tet4  |            61 / 267 |                 61 / 267 |
|            | volume      | 239999.99999999997 mm^3 | 0.00023999999999999998 m^3 |
|            | mapping     | refined_face 2, coarse_face 2 | the same       |
| RM-MESH-08 | outcome     | refusal, nothing published | exit 1, same diagnostic |
```

**The same figures, by construction rather than by coincidence**: the numbers in
the CMake regexes are what `MeshModelsTests.cpp` measures in process for the same
model, so a CLI that substituted its own default, its own sizing or its own
validation would print something else.

The millimetre and SI spellings of a volume differ in their last bit because
`Length::in(mm3)` divides by a factor that is not exactly representable while the
CLI prints `si()` directly. Same double, two conversion paths.

## Determinism

Five runs each, to P16-VOL-001's own standard — counts, the volume **bitwise**,
the sizing restrictions, and the element connectivity index for index — plus node
positions and the quality report, which a reference suite can afford to compare.

```text
| Model      | Runs | Nodes | Tet4 | Volume mm^3        | Connectivity stable |
| RM-MESH-01 |    5 |     8 |    6 | 294000.00000000006 | yes                 |
| RM-MESH-03 |    5 |    81 |  126 | 63560.69856538716  | yes                 |
| RM-MESH-06 |    5 |     9 |   12 | 118799.99999999999 | yes                 |
| RM-MESH-07 |    5 |    61 |  267 | 239999.99999999997 | yes                 |
```

Also compared equal across the five runs: `boundaryVolume()` bitwise,
`VolumeConformity` whole, `ResolvedSizing::restrictions` and `::regions`, the
canonical node set, `MeshQualityReport::summaries` and `::findings` (their
**order** as well as their values, which is what makes a report diffable), and
the mapping's completeness.

**And all nine documents' outcomes are stable** over three attempts each,
RM-MESH-08 included: the same refusal, the same diagnostic text, and nothing
published on any attempt.

```text
new tests under ctest --repeat until-fail:5   53 tests x 5, see
                                              qualification/repeat-debug-ext.txt
```

## Rigid transform (RM-MESH-06)

```text
transform verified before use: three unit norms, three zero dot products,
                               X x Y = N, and no axis left alone

CAD volume      base 118799.99999999999   placed 118800            invariant to 1e-12
mesh volume     118800.00000000003        118799.99999999999       both exact
counts          9 nodes / 12 tets         9 nodes / 12 tets        equal
bounding box    from (0, 0, 0)            from (18.6667, -27, -7)  THE MESH MOVED

node gap after R x + t
    boundary nodes      below 1e-9 mm        the CAD-determined ones, exact
    interior node       2.66e-4 mm           the BACKEND's own choice
    body diagonal       108.171 mm           so 2.5e-6 relative

quality         base            placed           difference
worst aspect    3.8810436740650056  3.8810436740650065   1e-15 relative
radius ratio    0.12738065408518734 0.12738142378331926  6.0e-6 relative
min dihedral    14.931323238548343  14.931412614778592   9e-5 degrees
max dihedral    152.3247162851346   152.32451087819666   2e-4 degrees
```

**The two populations are judged separately, and that is this model's finding.**
A boundary node is a vertex of the kernel's triangulation of a CAD face, so it
must move with the body to rounding — and does. The one interior node is Netgen's
own choice, computed from world coordinates by an algorithm under no
equivariance obligation. One tolerance over both would mean either failing a
correct mesh or giving up the check that proves the body moved at all.

The quality tolerance is **derived from the node discrepancy**, not chosen: a
shape metric is Lipschitz in its nodes' positions, so two meshes differing by
2.5e-6 relative in one node can differ by that order in a metric. The gate is
1e-4, above every measurement and far below what a frame defect would do: on a
14.93 degree dihedral it allows 0.0015 degrees, so an error of even a *tenth of
a degree* fails it by a factor of 67.

**The count equality is a property of this configuration, not a general
guarantee.** Measured, the same model at a = 110 mm gives 6 tetrahedra in one
placement and 12 in the other; the runner's model-change step records it. The
suite asserts the equality where it holds and says what it is.

## Cross-preset equivalence

```text
PRESET            REFERENCE SUITE             CLI FIXTURES
debug-ext         28 cases, 22817 assertions  25 / 25
release-ext       28 cases, 22817 assertions  25 / 25
debug-shared-ext  28 cases, 22817 assertions  25 / 25
```

**Identical, and that includes the element counts and the volume doubles.** The
CLI fixtures assert exact node, element and facet counts and exact SI volumes,
and the same regexes pass under `-O0 -g`, under the optimiser, and across a DLL
boundary — so for these models Netgen's output is byte-deterministic across
presets, which the brief asks to be recorded if it holds. It is recorded as a
measurement, not promoted to a general guarantee.
