# P12-SKETCH-002 — Sketch Entities: Ellipse and Spline Verification

## Status

**PASS.** Sketches take ellipses and splines. Both solve, take constraints,
form profiles with exact areas, centroids and nesting, and become exact kernel
edges in extrusions, revolutions and sweep profiles. Loft sections and sweep
paths refuse them with a diagnostic. They are saved, loaded, exported and
undone like the other entities.

The milestone also found and fixed a **mass-property accuracy bug** in the
existing geometry code (see *Kernel findings*). The kernel's adaptive
integration missed the volume of an extruded spline by 4.5e-2, and its surface
area by 9.2e-3, while reporting 2e-16. Bodies with such faces now use the
kernel's Gauss–Kronrod volume integration, and the areas of swept-curve faces
are integrated by BetterCAD. Bodies bounded by planes, cylinders, cones,
spheres and tori keep the previous computation, bit for bit.

Debug, Release and Debug-shared each passed **845/845** tests with **0
compiler warnings** from a verified clean rebuild. Every test of the P11
qualification and of `P12-SKETCH-001` still passes in all three.

Date: 2026-09-17. `main` was at `efdc1cf` (`P12-SKETCH-001`) before this
milestone. Every number below was measured in this session and is recorded in
this directory.

## Scope

The deliverables are the ones `TODO.md` lists for `P12-SKETCH-002`.

| Deliverable | Status | Main evidence (test cases) |
| --- | --- | --- |
| Ellipse: centre and two vertex points on perpendicular axes; from semi-axes and rotation, or on existing points | IMPLEMENTED | `SketchEllipse_CreatedFromSemiAxesAndRotation`; `SketchEllipse_InvalidEllipsesAreRefused` |
| Spline: uniform non-rational B-spline, degree 2–5, open (clamped) or periodic | IMPLEMENTED | `SketchSpline_CreatedOnNewOrExistingPoles`; `SketchSpline_InvalidSplinesAreRefused`; `BSpline_*` (7 cases) |
| Solver: axes perpendicular; point constraints on vertices and poles; concentric ellipses; spline tangency at joints; degeneracy | IMPLEMENTED | `SketchEllipse_SolverKeepsTheAxesPerpendicular`; `SketchSpline_PolesAreFreePoints`; `SketchSpline_TangentToALineAtASharedEnd`; `SketchSpline_TangentToAnArcOrASplineAtAJoint`; `SketchSpline_TangentNeedsAJoint`; `SketchEllipseSpline_SolveDeterministically` |
| Queries: end points, centre, length, bounds | IMPLEMENTED | `SketchEllipseSpline_LengthsMatchIndependentFormulas`; `SketchEllipseSpline_BoundsContainTheCurves` |
| Profiles: loops and edges; exact area, centroid and containment | IMPLEMENTED | `SketchProfiles_EllipsesAndSplinesFormRegions`; `CurvedProfile_EllipseAreaIsPiAB`; `…QuadraticSplineAreaFollowsArchimedes`; `…SplineAreaAndCentroidMatchBooleIntegration`; `…OpenSplineClosesALoopWithLines` |
| Kernel: exact edges in extrude, revolve and sweep profiles; lofts and paths refuse them | IMPLEMENTED | `CurvedProfile_Extruded*`; `…RevolvedEllipseAndSplineFollowPappus`; `…SweptEllipseFollowsPappus`; `…SweptCurveSidesHaveTheirAnalyticArea`; `…LoftsAndPathsRefuseEllipsesAndSplines`; `SketchFeatures_*` |
| Reference validation, structured errors, save/load | IMPLEMENTED | `…MalformedCurvedLoopsAreRefused`; `…SelfIntersectingSplineIsRefusedByTheKernelCheck`; `SketchFile_EllipsesAndSplinesRoundTrip`; `SketchFile_MalformedEllipsesAndSplinesAreRejectedWithThePath`; `SketchExport_EllipsesAndSplinesReadBackFromStep`; `SketchCommands_EllipseAndSplineEditsUndoAndRedo`; `SketchDocument_ValidationAcceptsEllipsesAndSplines` |
| Evidence | this directory | — |

**Not in scope, and not implemented:** tangency to ellipses, tangency of
splines away from a joint, point-on-curve constraints, interpolating splines
(through points), rational splines (NURBS), ellipse arcs, splines and ellipses
as sweep paths or loft sections (`P12-SWEEP-001` and `P12-LOFT-001` are
separate milestones).

## Design

```text
UniformBSpline (core, math/BSpline)   the curve: de Boor with the derivative, knots in the
                                      kernel's form, Bezier pieces by blossoming; the
                                      8-point Gauss-Legendre rule
EllipseEntity, SplineEntity (sketch)  point-entity references; endPointIds()
Sketch (sketch)                       addEllipse/addSpline, queries, structural checks on load
SolverSystem (sketch, private)        ellipse: internal perpendicularity; spline: polygon
                                      degeneracy; tangent at a joint through the end leg
EllipseSegment2D, SplineSegment2D     profile segments; exact Green's-theorem terms
  (geometry)
extractRegions (features)             closed curves as loops, open splines as edges;
                                      exact nesting (implicit ellipse, Bezier halving)
OcctSweeps (geometry adapter)         gp_Elips and Geom_BSplineCurve edges; loop checks
OcctBody (geometry adapter)           mass properties by surface type (see Kernel findings)
SketchJson (io)                       "ellipse", "bspline"
```

**Ellipse.** Three point entities: centre, `xVertex` (the first semi-axis) and
`yVertex`, whose distance from the centre is the second semi-axis, 90°
counter-clockwise from the first; either may be the longer. The solver adds one
internal equation, dot(X − C, Y − C) / |Y − C| = 0, so a free ellipse has **5
degrees of freedom**. Distances size it; a horizontal or vertical constraint on
(C, X) orients it. Created from values, the Y vertex is placed with exact
(−sin, cos). On existing points it must be perpendicular: neither vertex may be
more than 1e-10 m off the perpendicular to the other's axis.

**Spline.** Poles are point entities (2 degrees of freedom each), at least
degree + 1, all distinct. Open splines are clamped: they start at the first
pole and end at the last, which is how they join other entities. Periodic
splines are closed and smooth. The same `UniformBSpline` serves the sketch, the
profile and the kernel adapter. In files a spline is `"bspline"`: the P9 test
`Invalid document files are rejected with the JSON path` requires `"spline"`
to stay an unknown type, and the name also says which curve the file holds.

**Tangency with splines** applies where the two entities meet (one point entity,
or two joined by an enabled coincident constraint). There the spline's end leg,
its first or last two poles, is the curve's tangent (a clamped B-spline starts
along P1 − P0). The leg is parallel to a line, perpendicular to an arc's radius,
or parallel to another spline's leg: one equation each. The joint must exist
when the constraint is added. Loading does not require it, because a file may
list the coincident constraint later, and a solve reports a missing joint.

**Profiles.** Circles, ellipses and periodic splines are loops on their own.
Lines, arcs and open splines are edges, and an open spline may end where it
starts. Areas and first moments come from Green's theorem: exact for
ellipses, and for splines by the 8-point Gauss–Legendre rule on each span,
exact to degree 15 (the moment integrand of a degree-5 spline has degree 14).
Loop nesting is decided exactly. A point is inside an ellipse by its implicit
equation. Spline crossings are counted on the curve's Bézier pieces, each
halved until it lies wholly beside the test ray, so a hole is never judged
against the control polygon.

**Kernel.** Ellipse edges are `gp_Elips`, with the major axis first; a longer
second axis turns the frame by 90°, which traces the same curve. B-spline
edges carry the spline's own poles, knots and multiplicities. Loops may not mix
a closed curve with other segments. A spline must pass `validate`, and a
self-intersecting spline is refused by the existing face check. The revolution
axis check uses the poles for splines. They bound the curve, so the check is
conservative (see Known Limitations).

## Kernel Findings

`kernel-probe/curve_kernel_probe.cpp` calls raw OCCT 8.0.1
(`kernel-probe/curve-kernel-probe.log`). The curve geometry is right: a
periodic quadratic spline on the 60 mm square passes through (30, 0),
(52.5, 7.5) and (60, 30) in every representation (periodic, unclamped, Bézier
pieces). The planar faces' areas are exact. What was wrong is the integration
of mass properties:

| Case (analytic value) | Adaptive Gauss, 1e-10 (the previous `massProperties`) | Gauss–Kronrod over spans, 1e-10 |
| --- | --- | --- |
| Prism of the spline square, V = 30000 mm³ | 31345.95, rel **4.5e-2** (reported 2.1e-16) | 29999.99999999998, rel 7.8e-16 |
| Elliptic prism a = 30, b = 12, h = 25 | rel **5.5e-9** (reported 4.0e-16) | rel 2.3e-15 |
| Circle as an ellipse, a = b = 10 | rel **5.5e-9** | rel 2.3e-15 |
| Revolved ellipse (Pappus) | rel **3.3e-9** | rel 1.9e-15 |
| Revolved spline square (Pappus) | rel **2.8e-9** | rel 2.0e-15 |

Surface areas of the curved sides were wrong too, and OCCT has no Gauss–Kronrod
area integration:

| Case | Adaptive Gauss 1e-10 | Adaptive 1e-12 | Plain Gauss |
| --- | --- | --- | --- |
| Elliptic prism a = 30, b = 10, h = 20 | rel **1.0e-2** | 5.8e-3 | 1.8e-4 |
| Spline square prism, h = 10 | rel **9.2e-3** | 1.6e-3 | 5.2e-3 |

Splitting the closed faces first made the ellipse exact at 32 pieces, but left
the spline at 5e-6.

**The fix** (`OcctBody.cpp`, `Body::massProperties`):

- Bodies whose faces all lie on planes, cylinders, cones, spheres or tori
  keep the adaptive Gauss integration. It is exact for them (P3, P11), and
  their results stay bit for bit.
- Any other body gets its volume and centre of mass from
  `BRepGProp::VolumePropertiesGK` (spans on, centre of gravity on).
- Faces on surfaces of extrusion or revolution that cover their whole
  parameter rectangle get their area from BetterCAD's integration. That is the
  8-point Gauss–Legendre rule in u and v on every knot span, each span halved
  until the area changes by less than 1e-14. The rectangle is checked with
  Green's theorem on the p-curves.
- Other faces keep the kernel's area.

With the fix, the probe's cases rebuilt through BetterCAD are within 2.0e-13
(areas) and 2.2e-15 (volumes) of their analytic values (tables below).

**Effect on existing results.** Loft bodies (B-spline sides) now use the
Gauss–Kronrod volume. Their measured deviations from the analytic volumes did
not change beyond rounding: the P11 values 6.26e-10 and 2.27e-10 are the
kernel's approximate surfaces, not integration. Measured on the `[loft]` tests
with the qualified P12-SKETCH-001 build against this build, the largest change
of a deviation was 1.6e-15. All 845 tests pass, and the test-only STEP reader
(`tests/support/occt/StepReadBack.cpp`) uses the Gauss–Kronrod volume too.

## Independent Validation

Expected values are computed in the tests by other means than the code under
test:

- **B-spline curve:** Bernstein polynomials (clamped splines with degree + 1
  poles are Bézier curves) and the uniform cubic B-spline basis matrix for
  periodic splines. Largest deviation: 1.1e-16 m in points and derivatives.
- **Areas:** π a b; Archimedes' quadrature of the parabola (a periodic
  quadratic spline is the midpoint polygon plus 2/3 of each corner triangle);
  Boole's rule (exact to degree 5) on the Bézier pieces with 64 panels; the
  closed form 3/5 w h for a cubic Bézier over a rectangle's corners; and
  3/20 cross(P1, P2) for a closed cubic Bézier from the origin.
- **Lengths:** the Gauss–Kummer series for ellipses, the closed-form arc
  length of a parabola, and equally spaced collinear poles.
- **Solids:** π a b h, Pappus for revolutions and sweeps, the elliptic prism's
  surface 2π a b + perimeter × h, and the Pappus surface of a revolved
  ellipse.

| Check | Tolerance | Largest deviation measured |
| --- | --- | --- |
| Ellipse and spline areas (Green's theorem) | 1e-12 rel | 1.7e-15 (a degree-5 spline against Boole) |
| Centroids vs Boole | 1e-11 rel | 7.6e-13 (the D's y; periodic splines 5.1e-15) |
| Ellipse circumference vs Gauss–Kummer | 1e-13 rel | 4.3e-16 (133.64893220555260 vs …254 mm) |
| Parabola arc length | 1e-12 rel | 3.8e-16 |
| Elliptic prisms (two frames) | 1e-12 rel | 2.1e-15 |
| Spline prisms, degrees 2–5 | 1e-9 rel | 8.9e-13 (degree 4: 59040.62499994747 vs …500008 mm³) |
| Revolved ellipse and spline square (Pappus) | 1e-9 rel | 2.2e-15 |
| Swept ellipse (Pappus) | 1e-9 rel | 2.2e-15 |
| Surface of the elliptic prism | 1e-12 rel | 2.0e-13 (4557.9342362658 vs …2649 mm²) |
| Surface of the spline prism | 1e-12 rel | 2.1e-14 |
| Surface of the revolved ellipse | 1e-12 rel | 4.1e-14 |
| STEP read-back volume | 1e-9 rel | 1.3e-15 |
| Turned ellipse bounds vs 720000 samples | 1e-8 mm | 3.2e-11 mm |
| Solved vertex positions | 1e-8 mm | 3.6e-15 mm |
| Tangency at joints (sine or cosine of the legs) | 1e-10 | 6.0e-17 |

**Driven geometry.** `SketchFeatures_DrivenEllipseAndSplinesExtrudeToTheirVolumes`
extrudes an ellipse whose semi-axis is a parameter, a periodic spline and a
"D" (a line and a cubic spline arch sharing its ends) with an elliptic hole.
The depth is a parameter. Semi-axis 30 → 40 mm and depth 10 → 15 mm rebuild
π·a·12·h (11309.7335529232 → 22619.4671058465 mm³, 2.3e-15), 3000·h and
(1620 − 50π)·h. Undo restores the first values, and the elliptic prism's
bounds are (30, 12, 10) mm.

**Degrees of freedom and diagnostics.** A free ellipse: 5. Fixed centre,
horizontal axis and two distances: fully constrained. A second orientation
constraint: over-constrained, the redundant constraint named. A Y vertex held
on the X axis: inconsistent, sketch unchanged. A vertex constrained onto the
centre: the perpendicularity condition degenerates into the coincident
constraint's equations, reported as over-constrained, nothing written. Splines:
2 degrees of freedom per pole; poles joined onto one point: `the solution
collapses entity:11 to zero size`.

## Diagnostics

Asserted verbatim by the tests (InvalidArgument unless noted):

| Case | Message |
| --- | --- |
| non-perpendicular vertices | `an ellipse's axes must be perpendicular, but they are 80 deg apart` |
| repeated vertex | `an ellipse needs three different points` |
| repeated pole | `a spline cannot use entity:1 twice` |
| degree 1 | `a spline's degree must be 2 to 5, got 1` |
| too few poles | `a spline of degree 3 needs at least 4 poles, got 3` |
| coincident poles | `a spline's poles all coincide` |
| periodic end points | `entity:6 is a periodic spline, which has no end points` |
| tangent without a joint | `entity:3 and entity:7 share no end point: a tangent constraint with a spline applies where the two meet (at one point, or at two a coincident constraint joins)` |
| joint removed (SolverFailure) | `constraint:2 needs entity:3 and entity:7 to share an end point` |
| mixed loop | `outer loop mixes a full ellipse with other segments` |
| profile spline collapsed | `entity:4 is not a valid spline: a spline's poles all coincide` |
| spline as a sweep path edge | `Sweep: the path edge entity:4 is a spline, not a line, arc or circle` (also a validation issue) |
| ellipse loft section | `makeLoft: section 1 has an ellipse; loft sections are made of lines, arcs and circles` |
| file | `unknown type 'spline'`; `….data.entities[4]: a spline's degree must be 2 to 5, got 9`; `.periodic: expected true or false` |

The SKETCH-001 test `SketchConstraint_NewTypesValidateTheirReferences` now
expects the widened signatures (`… two circles or arcs, or an open spline and
a line, an arc or an open spline, …`; `a concentric constraint takes two
circles, arcs or ellipses, …`). No P0–P11 test was changed.

## Tests

41 new Catch2 test cases, all tagged `[p12]`:

- `tests/core/math/BSplineTests.cpp`: 7 (curve, derivatives, Bézier pieces,
  knots, refusals, Gauss rule);
- `tests/core/geometry/CurvedProfileTests.cpp`: 13 (areas, centroids, segment
  points, prisms, revolutions, sweep, surfaces, refusals, self-intersection);
- `tests/sketch/EllipseSplineTests.cpp`: 12 (entities, refusals, lengths,
  bounds, solver, tangency, determinism);
- `tests/sketch/EllipseSplineDocumentTests.cpp`: 9 (profiles, driven
  extrusions, Pappus features, refusals, validation, undo, files, STEP).

The Release run of these 41 cases records **1666 passed assertions and 0
failed** (`entity-values-release.txt`).

## Qualification

`qualify.cmd` (in this directory, run through `run-qualification.cmd`)
configured each preset, removed every build output (the first attempt
succeeded for each), rebuilt with warnings as errors under code page 65001,
ran CTest after each successful build, and then repeated the related tests five
times in Release and Debug. The Git tree IDs it recorded equal the working
tree's after the run.

| Preset | Configure | Clean | Build | TUs compiled | `warning` lines | Tests |
| --- | --- | --- | --- | --- | --- | --- |
| Debug | exit 0 | attempt 1 | exit 0 | 288 | 0 | **845/845 passed** (155.4 s) |
| Release | exit 0 | attempt 1 | exit 0 | 288 | 0 | **845/845 passed** (168.3 s) |
| Debug-shared | exit 0 | attempt 1 | exit 0 | 288 | 0 | **845/845 passed** (174.1 s) |

288 = `P12-SKETCH-001`'s 283 translation units + `math/BSpline.cpp` and the
four new test files.

**Repeats.** The mass-property change touches every body, so the repeat
selection is broad:
`ctest -R "[Ss]ketch|[Ss]olver|[Cc]onstraint|BSpline|Curved|[Pp]rofile|[Ee]xtru|[Rr]evol|[Ss]weep|[Ll]oft|[Pp]ropert|[Ss]tep|STEP|[Ee]xport|[Rr]egenerat|[Vv]alidat|[Dd]ocument|[Cc]ommand|P9" --repeat until-fail:5`
selected 428 tests.

| Preset | Result | Time |
| --- | --- | --- |
| Release | 428/428, each 5 times (2140 passed runs) | 419.7 s |
| Debug | 428/428, each 5 times (2140 passed runs) | 437.8 s |

**Determinism.** `SketchEllipseSpline_SolveDeterministically` solves two
identical sketches to the same iterations and bit-identical content.
`SketchFile_EllipsesAndSplinesRoundTrip` checks that the loaded document is
equivalent, rebuilds bit-identical volumes and serializes to identical text.
Across configurations (`values-determinism.txt`), the Debug, Release and
Debug-shared executables printed identical values for the 41 cases (MD5
`83d3bdfb2a6ded1188f54045e81b1557`, 2445 lines).

## Legacy Regression

`regression-comparison.txt` compares every log by test name with
`../P11-QUAL-001/release-ctest.log` (737 names), and
`regression-comparison-sketch001.txt` with `../P12-SKETCH-001/ctest-release.log`
(803 names):

- every baseline name is present and passed in the Debug, Release and
  Debug-shared logs; no entry failed;
- the new names are the 41 new test cases (107 against P11).

**The P0–P12-SKETCH-001 regression suite remains green.** Changes to existing
production code:

- `Sketch`, `Entities`: the two entity kinds, their queries and checks;
- constraint checks and the solver's equation system (the existing equations
  are unchanged);
- `Profile`: two segment kinds, `firstPoint`/`lastPoint`/`pointAt`,
  `validate`;
- `extractRegions`: curved loops, open splines and exact nesting;
- `OcctSweeps`: curved edges and loop checks;
- `SweepPlan`, `LoftPlan`, `SweepRegeneration`, `validateDocument`: explicit
  refusals of curved paths and sections;
- `OcctBody::massProperties`: the integration fix;
- `SketchJson`: the two entity kinds.

## Known Limitations

- **Spline tangency only at joints**, and no tangency to ellipses.
- **Spline bounds and the revolution axis check use the poles.** They contain
  the curve and may be wider. `CurvedProfile_RevolvedEllipseAndSplineFollowPappus`
  pins a spline whose poles cross the axis while its curve does not: it is
  refused.
- **Ellipse axes perpendicular to the solver's tolerance.** The profile curve
  is defined by the centre, the X vertex and |Y − C|, so a Y vertex slightly
  off the perpendicular is off the curve by a second-order amount.
- **Collapsing an ellipse axis onto its centre** is reported as
  over-constrained, not as a collapse.
- **Kernel areas of trimmed curved faces.** Faces on surfaces of extrusion or
  revolution that do not cover their parameter rectangle, as cutting such a
  body can produce, and B-spline faces keep the kernel's area integration.
  That integration is exact for the loft faces P11 measured, but unverified
  in general.
- **Lofts and sweep paths** take lines, arcs and circles only.

## Evidence Files

- `README.md`: this file.
- `qualify.cmd`, `run-qualification.cmd`: the qualification as run.
- `qualification-times.txt`: every step's start, exit code and time, the HEAD
  and the Git tree IDs of the qualified sources.
- `configure-*.log`, `clean-*.log`, `build-*.log`, `ctest-*.log`: the three
  presets.
- `ctest-repeat-{release,debug}.log`: the related tests, 5 times each.
- `regression-comparison.txt`, `regression-comparison-sketch001.txt`,
  `compare-regression.py`: the legacy comparisons.
- `entity-values-release.txt`, `values.py`, `values-header.txt`: measured
  values; `values-determinism.txt`: the same values from all three
  configurations.
- `kernel-probe/curve_kernel_probe.cpp`, `kernel-probe/curve-kernel-probe.log`:
  the raw-OCCT probe and its output.

## Final Result

```text
TASK:            P12-SKETCH-002 Sketch entities: ellipse, spline
IMPLEMENTATION:  UniformBSpline; ellipse and spline entities, solver support,
                 queries, profiles with exact nesting, kernel edges, file
                 support; mass properties fixed for non-elementary faces
TESTS:           41 new tests; 845/845 in Debug, Release, Debug-shared;
                 428 related tests x5 in Release and Debug
VALIDATION:      areas and lengths within 1.7e-15 of Archimedes, Boole,
                 Gauss-Kummer and closed forms; solids within 8.9e-13 of
                 pi a b h, Pappus and area x depth; curved surfaces within
                 2.0e-13; values identical across configurations
RESULT:          PASS
EVIDENCE:        docs/verification/P12-SKETCH-002/
TODO:            P12-SKETCH-002 deliverables ticked
```
