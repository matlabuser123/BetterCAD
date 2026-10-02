# P16-SIZE-001 — sizing results

```text
SUBJECT:  what the sizing controls actually did, measured
DATE:     2026-10-02
```

Every number here was produced by the test suite and appears in the
qualification logs. None was estimated.

## Global coarse / medium / fine

Fixture: a cylinder r 6 x h 20 mm, boundary deflection 0.05 mm — fine enough
that the boundary is not the binding constraint (see "The boundary bound",
below). CAD volume `pi r^2 h = 2.26195e-06 m^3`.

```text
target    nodes   Tet4    mean edge   median edge   max edge   volume (m^3)
6.0 mm      149    554      6.54 mm       5.38 mm    23.23 mm   2.25048e-06
3.0 mm      235   1086      4.76 mm       3.09 mm    22.77 mm   2.25048e-06
1.5 mm      434   2185      3.11 mm       2.10 mm    20.11 mm   2.25048e-06
```

```text
CHARACTERISTIC SCALE FALLS      median 5.38 -> 3.09 -> 2.10 mm
                                mean   6.54 -> 4.76 -> 3.11 mm
VOLUME IS UNCHANGED             2.25048e-06 m^3 at every target
                                (0.5% below the CAD volume, from below, which
                                is the facet understatement P16-VOL-001
                                established as one-sided for a curved body)
GEOMETRY VALID AT EVERY TARGET  validate() clean, boundary conforms
```

The gate is the characteristic scale, not the element count. Counts are
reported because they are informative, and they happen to rise here, but a
backend makes discrete decisions and a count can move either way between two
nearby targets — see the next section for a fixture where it does not move at
all.

## The boundary bound

Fixture: a 40 x 40 x 40 mm block. Its boundary is 12 triangles of 40–57 mm,
because OCCT triangulates a **planar** face with two triangles whatever the
deflection.

```text
target     Tet4    median edge   volume (m^3)        valid?
20 mm        12      40.00 mm    6.4e-05             yes
10 mm        12      40.00 mm    6.4e-05             yes
```

**Identical meshes.** A tetrahedron cannot be smaller than the boundary
triangles it must conform to, so on this body a global target of 20 or 10 mm
has nowhere to act. Both meshes are still valid and both still recover the
hand-computed volume exactly, which is what makes this a bound rather than a
failure.

This is the approved pipeline working as designed — a validated surface goes
in and the backend only fills it — and refining the boundary is
`SurfaceMeshControls`' job. It is pinned by
`SizeGlobal_CannotBeFinerThanTheBoundaryItMustConformTo` so that a future
reader measuring a block and finding no effect learns why from the suite.

Measured and deliberately **not** asserted: a 5 mm target on this block gives
25 tetrahedra while a 4 mm target gives 12 again. The backend's decisions are
not monotonic once the boundary dominates, which is exactly why element count
is not the gate.

## Scale robustness

Geometrically similar blocks with proportionally scaled targets. A
metres/millimetres slip anywhere in the chain would show up here as one of the
two meshing absurdly.

```text
body            target    median edge    volume (m^3)      hand-computed
4 x 4 x 4 mm     1 mm      ~1/10 of the   6.4e-08           4e-3^3
40 x 40 x 40 mm 10 mm      larger one's   6.4e-05           40e-3^3
```

The assertion is that the median edge ratio lies between 5 and 20 — a wide
band, because the mesher is not obliged to produce similar meshes, but a
factor-of-1000 unit error misses it by orders of magnitude.

## Local refinement

Fixture: the same cylinder, global target 6 mm, a 1.5 mm control on the top
disc (`FaceRole::EndCap`).

```text
                             target region     mid-body
                             (z 16-22 mm)      (z 7-13 mm)
with the local control        3.68 mm mean      9.51 mm mean
global only                   (coarser)         (no finer)
```

```text
1. TARGET REGION FINER THAN WITHOUT THE CONTROL      yes
2. AND DENSER                                        yes
3. AND FINER THAN THE NON-TARGET REGION               3.68 vs 9.51 mm
4. AND THE REST IS NOT DRAGGED DOWN WITH IT           mid-body no finer than
                                                      the global-only mesh
RESOLVED SLAB                                         z [15.5, 20] mm, h 1.5 mm
GEOMETRY                                              valid, boundary conforms
```

Item 4 is what separates local refinement from a global size change in
disguise, and item 3 compares against the **mid-body** rather than the far
disc: both end discs are finely triangulated by the surface mesher, so
comparing one disc with the other measures the surface and not the control.

## Wrong-region guard

The same control applied to each end disc in turn, compared by node density in
each region across the two meshes. Comparing regions *within* one mesh would
measure the surface; comparing the *same* region *between* two meshes isolates
the control.

```text
region          refined by its own control   refined by the other
top (z 16-22)              72 nodes                  59 nodes
bottom (z -2-4)            71 nodes                  52 nodes
```

Both directions, so neither can pass by accident. This test caught a real
defect: with a slab one target deep the top refined and the bottom did not —
see AUDIT.md Finding 3.

## Precedence and order independence

```text
control set                              insertion order   resolved restrictions
top 1.5 mm, bottom 3 mm                  top first         identical
top 1.5 mm, bottom 3 mm                  bottom first      identical
```

Equal restriction lists, equal node and element counts, equal volume. Order
independence holds **by construction**: every backend restriction is a
maximum, `RestrictLocalH` keeps the smaller, and the minimum of a set does not
depend on arrival order. The canonical resolution additionally stores
restrictions in a position-keyed `std::map` and sorts its slabs.

Overlapping controls on different faces resolve to the smaller size, and every
emitted restriction is one of the two requested sizes — never something in
between.

Two controls on the **same** face are refused by `validate` as
`DuplicateFaceControl` before any geometry work.

## Regeneration

```text
edit                                control state   result
none                                Resolved        refinement at the top
depth 20 -> 60 mm (EndCap survives) Resolved        refinement followed the
                                                    face to z = 60; volume
                                                    40x40x60 recovered exactly
HoleBottom on a block with no hole  Unresolved      MESH REFUSED
face of another object              Unresolved      MESH REFUSED
```

A control that does not resolve is kept and reported, never dropped and never
moved to a nearby or similarly named face.

## Invalidation and separation

```text
change                              mesh stale?   why
nothing                             no
global target 20 -> 10 mm           YES           sizing intent differs
add a local control                 YES           sizing intent differs
20 mm written as 0.02 m             no            SAME intent; Length is
                                                  dimensioned
surface deflection changed          YES           a different request -- but
                                                  the SIZING intent is
                                                  untouched, asserted separately
material assigned                   no            meshing does not depend on
density 7850 -> 2700 kg/m^3         no            material
geometry edited without regenerating  refused     P16-GEOM-001's currency check
```

## Determinism

Five runs of the same geometry with the same controls, compared on node count,
element count, volume, the resolved restriction list and the connectivity of
every tetrahedron in order. Identical every time, within a preset.

Cross-preset mesh determinism remains unasserted, as `P16-VOL-001` recorded:
nothing exports a mesh to compare yet.

## Validation

```text
input                     outcome
global target 0           refused, non_positive_size
global target negative    refused, non_positive_size
global target NaN         refused, NON_FINITE_size (not non-positive -- the
                          comparison that decides "positive" answers false for
                          NaN, and reporting it that way would hide that the
                          value is not a number)
global target +/-inf      refused, non_finite_size
local target 0/neg/NaN/inf refused, naming the control's index and face
two controls, one face    refused, duplicate_face_control, naming the SECOND
EndCap with an entity     refused, invalid_face_selector (the core selector
                          rule, not re-implemented here)
no global target at all   ACCEPTED: BetterCAD's own default applies
target coarser than body  ACCEPTED: valid mesh, volume recovered exactly
```

Every refusal happens before the backend is called.
