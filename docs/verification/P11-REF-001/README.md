# P11-REF-001 — Mechanical Reference Models

## Status

**PASS.** Five production reference parts and a sixth swept part build,
regenerate, save, load and export correctly, and every dimension of every one
of them agrees with geometry computed independently from its parameters.
Debug, Release and Debug-shared each rebuilt clean and ran the whole suite:
738/738 tests, 0 compiler warnings over 274 translation units in each. The
six models come out bit-identical in all three builds.

Date: 2026-09-16. `main` was at `24a8134` (P11-FEAT-009 Loft) before this
milestone. Every number below was measured in this session and is recorded
here:

- the measured values of each model's tests (`<model>/values-release.txt`),
  filtered from Catch2's XML report by `values.py`;
- the models' fingerprints and timings in each build
  (`fingerprints-{debug,release,debug-shared}.txt`), compared by
  `cross-build.py`;
- the qualification logs (`logs/`).

## Purpose

This milestone adds no CAD feature. It asks whether the P11 feature set can
build real mechanical parts through complete parametric workflows:

```text
parameters → sketches → constraints → features → dependency graph →
regeneration → a real part → save/load → STEP/STL → deterministic checks
```

Five production parts answer it, with a sixth that gives the sweep feature a
natural use. Each is built only through the public document, parameter,
sketch and feature APIs, the way a user, a script or the GUI would build it.

## Reference Model Infrastructure

`examples/reference_models/` holds the builders, as a library so the tests
use exactly what the example program ships:

| File | What it holds |
| --- | --- |
| `ReferenceModels.hpp` | The models, their builders, the catalogue and fingerprints |
| `BuildSupport.{hpp,cpp}` | A builder that records the first error, and a sketch builder |
| `Shaft.cpp`, `Flange.cpp`, `Pulley.cpp`, `BearingHousing.cpp`, `MountingBracket.cpp`, `UBolt.cpp` | One part each |
| `Fingerprint.cpp` | What identifies a regenerated model, and how two are compared |
| `Catalog.cpp`, `main.cpp` | The catalogue, and the example program |

- **Builders.** `buildShaftReferenceModel()` and its siblings return a
  document that has not been regenerated, plus the IDs of the parameters and
  objects a caller may change. They are deterministic: the same builder
  always gives the same items with the same IDs and values, under a fixed
  document ID.
- **No shortcuts.** Nothing in the builders creates geometry: every body
  comes from regenerating a document. `architecture.layering` now also scans
  `examples/`, so an OCCT header there is a build failure; a fixture tree
  (`architecture.checker.examples-occt-leak`) proves the check catches it.
- **Saved models.** `examples/models/reference/*.bcad` are these documents as
  their builders make them. `ReferenceModel_SavedModelsMatchTheBuilders`
  saves each builder's document and compares it with the committed file byte
  for byte, then loads the file and compares the regenerated fingerprints, so
  the files cannot drift from the code. The CLI works on them directly
  (`cli.validate.reference.*`, `cli.info.reference.shaft`,
  `cli.export-step.reference.bracket`, `cli.export-stl.reference.pulley`).
- **Fingerprints.** A fingerprint is the model's items (ID, kind, name), its
  feature count, and every result body's validity, topology, volume, area,
  centroid and bounds. `operator==` compares every value exactly; `compare()`
  reports the largest relative difference of a volume or area and the largest
  difference of a position, for comparisons that only have to hold to
  rounding.
- **Independent geometry.** `tests/reference/Analytic.hpp` computes the
  expected properties from the dimensions alone, with no BetterCAD or kernel
  code: cross-sections of straight lines and circular arcs, integrated with
  Green's theorem, and Pappus' theorems for the turned parts. Lines integrate
  exactly; arcs use 16-point Gauss–Legendre on pieces of at most 45°.
  `ReferenceModel_AnalyticToolkit` checks it against closed forms (a disc, a
  torus, a cone, a fillet's section, a plate with a hole).
- **Parameters.** Expressions are stored but not evaluated yet (P1-003), so
  every dimension a feature follows is one parameter, used directly. Where a
  model needs a derived dimension, its sketch builds the relation
  geometrically — the shaft is dimensioned by half its length, so one
  parameter sets both the overall length and the mirror plane of the tail
  centre hole.

## Feature Coverage Matrix

From the built documents, not from a list kept by hand
(`ReferenceModel_FeatureCoverage` reads each model's feature types):

| Feature | Shaft | Flange | Pulley | Housing | Bracket | U-bolt |
| --- | :-: | :-: | :-: | :-: | :-: | :-: |
| Extrude | | ✓ | | ✓ | ✓ | |
| Revolve | ✓ | | ✓ | | | |
| Chamfer | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| Fillet | ✓ | ✓ | ✓ | ✓ | ✓ | |
| Hole | ✓ | ✓ | ✓ | ✓ | ✓ | |
| Linear pattern | | | | ✓ | ✓ | |
| Circular pattern | | ✓ | | | | |
| Mirror | ✓ | | | ✓ | ✓ | |
| Sweep | | | | | | ✓ |
| Loft | | | | | ✓ | |

Every P11 feature is used by at least one part, and every part is a chain of
at least two. The mirror appears in both scopes: the shaft and bracket mirror
a feature (a hole), the housing mirrors a body (its half).

## Shaft

**Feature chain:** `Profile` (XZ sketch, 17 entities, 17 constraints) →
`Turn` (revolve about Z) → `CentreDrill` (countersunk hole in the z = 0 end)
→ `TailCentreDrill` (mirror of it across the shaft's middle) →
`ShoulderFillets` → `EndChamfers`.

**Parameters:** `r1` 15, `l1` 40, `r2` 20, `l2` 40, `r3` 12.5,
`half_length` 60, `centre_drill_d` 4, `centre_drill_depth` 10,
`shoulder_fillet_r` 1.5, `end_chamfer` 1 mm. The part is Ø30 × 40, Ø40 × 40
and Ø25 × 40 mm: the overall length is twice `half_length`, and the Ø25
segment takes what `l1` and `l2` leave.

**Expected and actual.** The turned blank matches the milestone's own
formula, V = π/4 (d1² L1 + d2² L2 + d3² L3) = 31250π = 98174.770 mm³, and
every step of the chain matches its independently integrated section:

| Body | Expected (mm³) | Actual (mm³) | Relative error |
| --- | --- | --- | --- |
| `Turn` (the blank) | 98174.770424681 | 98174.770424681 | 0 |
| `CentreDrill` | 97973.352 | 97973.352 | ≤ 1e-15 |
| `TailCentreDrill` | 97771.934 | 97771.934 | ≤ 1e-15 |
| `ShoulderFillets` | 97857.398 | 97857.398 | ≤ 1e-15 |
| `EndChamfers` (the part) | 97773.099 | 97773.098888601089 | 1.5e-16 |

Area 14480.007 mm² (6.3e-16), centre of mass on the axis at z = 56.4688 mm,
bounds (−20, −20, 0) to (20, 20, 120) mm. The corrections add up:
31250π − 2 × (the countersunk hole) − (the two end chamfers) + (the two
shoulder fillets) equals the integrated section to 1e-14.

**Regeneration.** Middle diameter Ø40 → Ø50 (`r2` 20 → 25 mm): all six items
rebuild, V = 126047.433 mm³ against 126047.433 (3.5e-16), bounds ±25 mm; the
collar's rims are the new circles and the old ones are gone; both centre
holes are still there. Restoring gives the original back to rounding
(volume and area identical, positions within 3.3e-16 mm).

Overall length 120 → 130 mm (`half_length` 60 → 65): the blank, both centre
holes and both shoulder fillets follow — the tail hole moves because the
mirror plane is driven by the same parameter — but the chamfer refers to the
tail rim by its circle at z = 120, which the longer shaft no longer has. It
fails with `NotFound` ("EndChamfers: chamfer: edge reference 2 (circle around
(0, 0, 120) mm …) matches no edge of the body"), keeps no body, and the rim
at z = 130 is **not** substituted. Re-selecting the edge (an undoable
`ModifyChamferCommand`, what a user does today) completes the model at the
new length. This is the geometric-reference limitation, not a defect of the
model; see Known Limitations.

**Save/load:** saved, destroyed, loaded and regenerated — identical items,
dependencies and fingerprint; still parametric afterwards. **Undo/redo:** a
sketch-driving parameter restores the parameters exactly and the geometry to
rounding; a later feature's parameter (the fillet radius) is exact both ways.
**STEP:** 1 valid solid, 97773.099 mm³ (3.9e-14), bounds within 1e-6 mm.
**STL:** 10606 triangles, closed and outward, meshed 97736.3 mm³ against
97773.1 (difference 36.8, bound 144.8). **Determinism:** 10 full rebuilds and
3 fresh builds, all fingerprints identical. **Failure recovery:** a 6 mm
shoulder fillet on a 5 mm shoulder is refused before the kernel
("ShoulderFillets: fillet: … does not fit: its fillet needs 6 mm on a face
next to the edge, which leaves only 5 mm"), the chamfer after it is blocked,
neither keeps a body, the blank is untouched, and restoring 1.5 mm gives the
original back bit for bit.

**Result: PASS.**

## Flange

**Feature chain:** `DiscSketch` → `Disc` (extrude from the mating face
z = 0) → `Bore` (hole) → `BoltHole` (hole) → `BoltCircle` (circular pattern)
→ `EdgeChamfers` → `RimFillet`.

**Parameters:** `outer_r` 50, `thickness` 12, `bore_d` 30,
`bolt_circle_r` 35, `bolt_d` 8, `bolt_count` 6, `edge_chamfer` 1,
`rim_fillet_r` 2 mm — Ø100 × 12 with a Ø30 bore and 6 × Ø8 on Ø70.

**Expected and actual.** Before the edge treatments the milestone's formula
applies: V = π/4 (Do² − Dbore² − N Dhole²) t = 82146.365 mm³, which the
integrated section reproduces to 1e-14 and the kernel to 3.6e-16. The
finished flange is 81674.894 mm³ expected against 81674.894046699133 actual
(5.3e-16), and the chamfers and fillet account for the difference by Pappus
to 1e-14.

**Bolt pattern.** Every hole is where x_i = R cos(2πi/N),
y_i = R sin(2πi/N) puts it: each of the N holes has exactly one rim circle
on each face at its position, there is none half way between two holes, and
the body has exactly 2N such circles. Checked at N = 6, at N = 8, and on a
Ø80 bolt circle.

**Regeneration.** Six holes → eight: only `BoltCircle` and the edge features
rebuild; volume and positions match. Bolt circle Ø70 → Ø80: `BoltHole` moves
and the pattern follows; the bore and the edge treatments are untouched.
Both restored: the original fingerprint, bit for bit. A larger outer
diameter (Ø100 → Ø110) moves the rim: the disc, bore and holes follow, and
the chamfer reports its missing rim circle rather than using another edge.

**Save/load** ✓ (and still parametric). **Undo/redo:** two parameter edits
undone and redone, document equivalent and fingerprints identical.
**STEP:** 1 valid solid, 81674.894 mm³ (5.9e-15). **STL:** 11482 triangles,
closed, meshed 81670.0 against 81674.9 mm³ (difference 4.9, bound 198.9).
**Determinism:** 10 rebuilds, 3 fresh builds, identical. **Failure
recovery:** a Ø96 bolt circle puts the holes through the rim; `BoltHole` is
refused, the pattern and edge features are blocked, nothing is built, and
restoring recovers bit for bit.

**Result: PASS.**

## Pulley

**Feature chain:** `Section` (XZ sketch, 12 corners) → `Blank` (revolve) →
`GrooveSection` (XZ sketch measured from the rim) → `Groove` (revolved cut)
→ `Bore` (hole) → `WebFillets` (4 edges) → `HubChamfers` (2 edges).

**Parameters:** `outer_r` 60, `rim_inner_r` 50, `rim_width` 30, `hub_r` 25,
`hub_length` 40, `web_z0` 10, `web_thickness` 10, `bore_d` 20,
`groove_z` 15, `groove_depth` 6, `groove_width` 12,
`groove_bottom_width` 4, `web_fillet_r` 3, `hub_chamfer` 1 mm.

**Expected and actual.** The blank is three annular cylinders (hub, web and
rim) and matches π/4 d² L arithmetic to 1e-14. The groove is checked by
Pappus on its own: a trapezoid of area (a + b)/2 · h turned about the axis at
its centroid radius, V = 2πRA, which accounts for the difference between the
blank and the grooved body to 1e-14. The finished pulley is 212874.606 mm³
expected against 212874.6060572218 (1.4e-16), area 47408.019 mm², centre of
mass on the axis at z = 16.5459 mm, bounds (−60, −60, 0) to (60, 60, 40) mm.

**Regeneration.** Outer diameter Ø120 → Ø140 (`outer_r` 60 → 70): every item
rebuilds, and the groove follows the rim — its bottom is a circle of radius
64 mm (70 − 6) and there is none at 54 mm — while the hub, web and bore are
untouched. Bore Ø20 → Ø25: only the bore and the two edge features rebuild;
the new rim circle is there and the old one is gone. Both restored: the
original to rounding.

**Save/load** ✓ (a deeper groove still regenerates afterwards).
**Undo/redo:** exact, in the document and the geometry. **STEP:** 1 valid
solid, 212874.606 mm³ (1.2e-13). **STL:** 39704 triangles, closed, meshed
212861 against 212875 mm³ (difference 13.4, bound 474.1). **Determinism:**
10 rebuilds, 3 fresh builds, identical. **Failure recovery:** a bore wider
than the hub is refused at the hole, the fillets and chamfers after it are
blocked, and restoring recovers bit for bit.

**Result: PASS.**

## Bearing Housing

**Feature chain:** `BaseSection`, `BossSection` (XZ sketches of one half) →
`Base`, `Boss` (extrudes, symmetric about y = 0, joined) → `Housing` (body
mirror across x = 0) → `Bore` (extruded cut through the joined housing) →
`MountHole` (hole) → `MountHoles` (2 × 2 linear pattern) → `BossFillets` →
`BaseChamfers`.

**Parameters:** `base_half_length` 60, `base_width` 60, `base_t` 12,
`boss_r` 35, `axis_height` 45, `bore_r` 20, `mount_d` 10, `mount_x` 48,
`mount_y` 15, `mount_pitch_x` 96, `mount_pitch_y` 30, `boss_fillet_r` 3,
`base_chamfer` 1.5 mm — a 120 × 60 × 12 base under a Ø70 boss with a Ø40
bore 45 mm up, and four Ø10 mounting holes.

**Expected and actual.** The half and the whole are checked against the
front section extruded along Y: the blank is
(L T + (2 R H + πR²/2) − 2 R T) W, and the mirror image is exactly the size
of the half it came from (1e-12). The finished housing is 261382.165 mm³
expected against 261382.1651324929 (3.3e-16), area 39195.261 mm², centre of
mass (0, 0, 30.4717) mm, bounds (−60, −30, 0) to (60, 30, 80) mm. The
fillets and chamfers, which run the full width, account for their difference
to 1e-14.

**Regeneration.** Bearing bore Ø40 → Ø45: the bore and everything after it
rebuild; the new bore circles are on both end faces and the old ones are
gone; the mounting holes stay. Base width 60 → 70: every feature rebuilds
(no sketch is solved again, since the width drives the extrude depths and
the bore's cutter), the part stays symmetric about y = 0 — its centre of
mass stays on that plane — the holes keep their places and the bore runs
through the new faces. Both restored: the original to rounding.

**Save/load** ✓ (still parametric). **Undo/redo:** exact. **STEP:** 1 valid
solid, 261382.165 mm³ (2.3e-14). **STL:** 2236 triangles, closed, meshed
261392 against 261382 mm³ (difference 9.7, bound 392.0). **Determinism:** 10
rebuilds, 3 fresh builds, identical. **Failure recovery:** two cases. A
mounting hole 58 mm from the middle would break out of the base end: the
hole is refused and everything after it is blocked. A Ø100 bore in a Ø70
boss cuts the arch away, and the fillet then reports that the edges it
rounds are gone (`NotFound`, "matches no edge of the body") instead of
rounding something else. Both recover when the parameter is restored.

**Result: PASS.**

## Mounting Bracket

**Feature chain:** `BaseSection`, `PlateSection` → `BasePlate`, `BackPlate`
(joined extrudes) → `BaseHole` → `BaseHoles` (2 × 2 linear pattern) →
`PlateHole` → `PlateHoles` (mirror across the middle) → `InnerFillet` →
`OuterChamfer` → `GussetFoot`, `GussetTip` (XY sketches) → `Gusset` (loft,
joined).

**Parameters:** `width` 100, `base_width` 60, `base_t` 10,
`plate_height` 70, `plate_t` 10, `hole_d` 8, `hole_inset` 15, `hole_row` 25,
`hole_pitch_x` 70, `hole_pitch_y` 20, `plate_hole_x` 20, `plate_hole_z` 45,
`centre_x` 50, `gusset_back` 5, `gusset_embed` 5, `gusset_top` 50,
`gusset_foot_t` 10, `gusset_foot_depth` 45, `gusset_tip_t` 6,
`gusset_tip_depth` 7, `inner_fillet_r` 5, `outer_chamfer` 2 mm.

**Expected and actual.** The plates match the milestone's formula,
V = Vbase + Vvertical − Voverlap = 60000 + 70000 − 10000 = 120000 mm³. The
gusset is a lofted rib: its own volume is the prismatoid h/6 (A0 + 4Am + A1),
and what it adds to the bracket is the part outside the plates less what the
inside fillet already fills, integrated in closed form. The finished bracket
is 123546.715 mm³ expected against 123546.71469101607 (2.2e-12), centre of
mass (50, 17.5682, 22.6173) mm, bounds (0, 0, 0) to (100, 60, 70) mm.

The loft's sides are B-spline surfaces even where they are flat (the known
kernel behaviour from P11-FEAT-009), so the gusseted bodies agree with the
exact geometry to 6.0e-12 relative in volume and 3.4e-6 mm in the centroid,
where the rest of the bracket agrees to rounding. The tests use those
tolerances for the gusseted stages only.

**Regeneration.** Plate height 70 → 90 mm and base width 60 → 80 mm: the
bracket rebuilds each time, the holes, fillet, chamfer and gusset stay where
they belong, and the volumes match the independently integrated section. A
taller gusset (`gusset_top` 50 → 60) rebuilds the loft alone. All restored:
the original to rounding.

**Save/load** ✓ (still parametric). **Undo/redo:** exact, in the document
and the geometry (the gusset's height drives a loft section offset, not a
sketch). **STEP:** 1 valid solid, 123546.715 mm³ (2.2e-12). **STL:** 4444
triangles, closed, meshed 123554 against 123547 mm³ (difference 7.0, bound
302.7). **Determinism:** 10 rebuilds, 3 fresh builds, identical. **Failure
recovery:** a hole 3 mm from the edge would break out of the base; it is
refused, the five features after it are blocked, nothing is built, and
restoring 15 mm recovers bit for bit.

**Result: PASS.**

## U-Bolt (the sweep model)

The five parts above have no natural sweep: their grooves and ribs are
turned, extruded or lofted, and forcing one in would have distorted a design
(the milestone's own instruction). A U-bolt is what a sweep is for.

**Feature chain:** `RodSection` (the section, on the plane where the path
starts) → `Route` (line, arc, line in the XZ plane) → `Rod` (sweep) →
`EndChamfers`. **Parameters:** `rod_r` 5, `leg` 60, `bend_r` 25,
`end_chamfer` 1 mm.

**Expected and actual.** A constant section swept along a tangent-continuous
path has V = A × L: π r² (2 leg + π R) = 15593.281 mm³, and the two chamfers
take π c² (r − c/3) each. Expected 15563.959, actual 15563.959180016738
(1e-16). The centre of mass is at x = R by symmetry and at
z = −48.3168 mm, where the half torus's own centroid,
2 (R² + r²/4) / (πR) from the bend axis, is the only part that is not
obvious. Bounds (−5, −5, −90) to (55, 5, 0) mm.

**Regeneration:** legs 60 → 80 mm — the path, the sweep and the chamfers
follow, and the chamfered ends stay at z = 0 because the path grows
downwards from the origin. **Save/load, STEP, STL, undo/redo, determinism:**
as for the other models. **Failure recovery:** a 6 mm chamfer on a Ø10 rod
is refused before the kernel, and restoring recovers bit for bit.

**Result: PASS.**

## Cross-Model Behaviour

- `ReferenceModel_AllModelsBuildInOneProcess` builds all six in one process,
  keeps them alive together, and checks that each is exactly what it is when
  built alone: no global or kernel state is shared between models.
- `ReferenceModel_AllModelsStressRegression` builds, regenerates and
  discards all six, five times over: every fingerprint is identical to the
  first round's. No crash, no drift, no leaked state.
- `ReferenceModel_AllModelsRegenerateAfterAChange` changes a main dimension
  of each model, checks the result is a different but sound body, and
  restores it to the original within rounding.
- `ReferenceModel_FeatureCoverage` derives the matrix above from the
  documents themselves.

## Cross-Build Results

`bettercad_example_reference_models` was run in each of the three builds and
its output compared by `cross-build.py` (`cross-build.txt`):

```text
== fingerprints-release.txt       (against fingerprints-debug.txt)
  BearingHousing   structure same, volume and area within 0 relative, positions within 0 mm
  Flange           structure same, volume and area within 0 relative, positions within 0 mm
  MountingBracket  structure same, volume and area within 0 relative, positions within 0 mm
  Pulley           structure same, volume and area within 0 relative, positions within 0 mm
  Shaft            structure same, volume and area within 0 relative, positions within 0 mm
  UBolt            structure same, volume and area within 0 relative, positions within 0 mm
== fingerprints-debug-shared.txt  (the same six lines)

Worst over all models and builds: 0 relative, 0 mm.
No structural differences.
```

Zero, not "within tolerance": the three files differ only in their
`timing_ms` lines. Every item, ID, name, feature count, body count, validity
flag, topology count, volume, area, centroid and bound is character-for-
character identical across an unoptimised static build, an optimised static
build and a shared-library build. The kernel's arithmetic does not depend on
the optimisation level or on how BetterCAD is linked.

The same six models are also built by the test suite in each preset: 62
reference-model tests (53 Catch2 cases and 9 CLI process tests) passed in
Debug, Release and Debug-shared alike.

## Performance Baseline

This is a baseline to compare against later, not an optimisation target: no
performance work was done in this milestone. Measured by the example program
in the qualified Release build (GCC 16.1, `-O2`, static), milliseconds,
one run:

| Model | Build | First regeneration | Unchanged | Changed parameter | Save | Load | File |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Shaft | 0.27 | 566.7 | 0.070 | 525.9 (`r2`) | 1.9 | 16.9 | 11571 B |
| Flange | 0.09 | 746.9 | 0.061 | 577.5 (`bolt_circle_r`) | 1.8 | 1.8 | 5870 B |
| Pulley | 0.11 | 617.6 | 0.092 | 583.5 (`outer_r`) | 2.1 | 20.5 | 23618 B |
| Bearing housing | 0.12 | 415.7 | 0.102 | 452.2 (`base_width`) | 2.2 | 17.9 | 14877 B |
| Mounting bracket | 0.13 | 740.4 | 0.116 | 689.9 (`plate_height`) | 2.4 | 15.0 | 24730 B |
| U-bolt | 0.09 | 173.7 | 0.030 | 182.8 (`leg`) | 1.6 | 1.6 | 6852 B |

"Build" is constructing the document — parameters, sketches, constraints and
feature definitions — before any geometry exists; it costs a fraction of a
millisecond. Everything expensive is in the kernel: a full regeneration of
these parts takes 0.2–0.7 s, and changing a parameter costs about the same,
because the regenerator re-evaluates the dirty features and every one of
these parameters is upstream of nearly the whole chain. Regenerating a model
that has not changed costs 30–120 µs: the dependency graph finds nothing
dirty and no kernel work happens at all. Saving is 2 ms and the documents
are 6–25 kB, because what is stored is the recipe rather than the geometry.

Debug figures (in `fingerprints-debug.txt`) are close for regeneration —
Shaft 619.9, Bracket 827.1 ms — since OCCT itself is the same optimised
library in both builds; the parts that are BetterCAD's own code show the
expected gap, with unchanged regeneration 0.15–0.97 ms in Debug against
0.03–0.12 ms in Release.

## Qualification

The tree was frozen and then rebuilt from scratch in each of the three
presets — `cmake --preset`, `cmake --build --clean-first`, `ctest` — followed
by the example program twice and two repeat runs. `logs/qualification-times.txt`
records every step's exit code and wall-clock time; the run took 1 h 26 min,
from 14:04:15 to 15:30:50 on 2026-09-16, and no step returned anything but 0.

| Preset | Translation units | Compiler warnings | Tests | Time |
| --- | --- | --- | --- | --- |
| Debug (`-O0 -g`, static) | 274 | 0 | 738/738 | 360.3 s |
| Release (`-O2`, static) | 274 | 0 | 738/738 | 392.0 s |
| Debug-shared (`-O0 -g`, shared) | 274 | 0 | 738/738 | 333.9 s |

Each build was a clean rebuild, so all 274 translation units were compiled
in each: no stale object file could hide a warning. "0 warnings" is what the
compiler emitted, counted by `../P11-QUAL-001/warning-audit.py` in GCC's own
`<file>:<line>:<column>: warning:` form rather than by searching for the
word; there were also no CMake warnings and no other line mentioning one.
The presets compile with `-Werror` and `BETTERCAD_WARNINGS_AS_ERRORS=ON`, so
a warning would have failed the build in any case; nothing was silenced to
reach zero.

The 738 tests are the whole suite, not a subset: the P0–P10 regression tests,
the P11 feature tests, and the 62 reference-model tests. Then, to separate
a passing run from a repeatable one, every reference-model and P11 test was
run five more times over in the two static presets:

```text
ctest --preset release -R "ReferenceModel_|reference" --repeat until-fail:5
  100% tests passed out of 435   (2175 executions, 1559.65 s)
ctest --preset debug   -R "ReferenceModel_|reference" --repeat until-fail:5
  100% tests passed out of 435   (1598.06 s)
```

`until-fail:5` stops at the first failure, so 435 × 5 = 2175 recorded passes
means every one of those tests was built, run and checked five times in each
of the two builds without a single failure. Together with the ten
regenerations inside `…RegeneratesDeterministically` and the five build-and-
discard rounds of `…AllModelsStressRegression`, each model's geometry was
reproduced far more than the ten times required, in three build
configurations and many separate processes.

The per-model measured values in `<model>/values-release.txt` come from this
same Release build: 4550 assertions over the six models (shaft 669, flange
1171, pulley 602, housing 842, bracket 937, U-bolt 329), none failed.

## Known Limitations

- **Geometric references do not follow moved geometry.** An edge or face
  reference resolves against the body as it is; when a parameter moves the
  edge (the shaft's tail rim, the flange's outer rim) the feature fails with
  `NotFound` and keeps no body, and the user re-selects the edge. Nothing is
  ever substituted, and the models show both sides of it: references that
  survive a change, and references that do not. Semantic topology naming is
  a later milestone.
- **No ambiguous reference arose.** The reference models exercise the
  "unique reference resolves" and "missing reference fails" paths; none of
  their parameter changes split an edge in two. The ambiguity path has its
  own tests in P11-FEAT-002 and P11-FEAT-003.
- **A half bore on a mirror plane.** Uniting a half body with its mirror
  image works when they meet on a plane, but not when a half cylinder lies on
  that plane: the kernel's fuse returns a shape its own checker rejects, so
  the union is refused (never built wrongly). The housing therefore cuts its
  bore after joining the halves. The behaviour is pinned by the geometry
  test "A half united with its mirror image: a planar seam merges, a
  cylindrical seam is refused"; raw OCCT shows the same union succeeding for
  an equivalent shape built with kernel primitives, so this is fragility on
  BetterCAD's prism geometry rather than a rule of the kernel.
- **Parameter expressions are not evaluated** (P1-003), so a derived
  dimension needs either its own parameter or a sketch that builds the
  relation. The models say which they use.
- **Through-all cuts.** An extrude has no "through all" mode, so the
  housing's bore is cut by an extrude whose depth is driven by the same
  width parameter as the housing itself.
- **Loft faces.** The kernel keeps a loft's sides as B-spline surfaces even
  where they are flat, so the bracket's volume and centroid agree with the
  exact geometry to 6.0e-12 and 3.4e-6 mm rather than to rounding.
- **Exports are not committed.** The STEP and STL files are reproducible
  with `bettercad_example_reference_models --out <dir>`; their measured
  properties are in the values files.

## Evidence Files

- `README.md`: this file.
- `<model>/values-release.txt`: every measured value of that model's tests
  (expected, actual, error, tolerance, diagnostics), from the Release build
  of the qualified tree. `values.py` is the filter that produced them.
- `fingerprints-{debug,release,debug-shared}.txt`: each model's items,
  bodies and timings, as the example program prints them.
- `cross-build.txt`, `cross-build.py`: the comparison of those three.
- `logs/`: configure, clean build and CTest logs for each preset, the repeat
  runs, and `qualification-times.txt`.

## Final Result

**PASS.**

The question this milestone asked was whether the P11 feature set can build
real mechanical parts through complete parametric workflows, or only pass
its own unit tests. Five production parts answer it: a stepped shaft turned
from a revolved profile with centre drills, shoulder fillets and end
chamfers; a bolted flange with a patterned bolt circle; a V-belt pulley with
a revolved groove; a pillow-block housing built as a half and mirrored; and
an L bracket with a lofted gusset. A swept U-bolt joins them so the sweep
feature has a natural use rather than a contrived one. Between them they use
every P11 feature, and each part is built only from parameters, sketches,
constraints and feature definitions through the public API — no example
creates geometry of its own, and the architecture check now covers
`examples/` so Open CASCADE cannot leak into one.

Every part is validated against geometry computed independently from its
dimensions, at every stage of its feature chain, and the agreement is at the
level of double-precision rounding: 1.2e-13 relative for the parts bounded
by planes, cylinders, cones and tori, and 6.0e-12 for the bracket, whose
lofted gusset the kernel keeps as B-spline faces. The full workflow holds
for all of them — change a dimension and the model regenerates; restore it
and the model returns; save, destroy, load and regenerate reproduces it
exactly; STEP read-back and closed STL meshes agree; invalid parameters fail
with structured diagnostics and leave the document untouched.

Two limitations were found and are reported rather than worked around:
geometric references do not follow geometry that a parameter moves, and
uniting a half body with its mirror image is refused when a half cylinder
lies on the mirror plane. Both are documented above, both are pinned by
tests, and neither was hidden by special-casing a model. No test was
weakened and no tolerance was relaxed to obtain this result.
