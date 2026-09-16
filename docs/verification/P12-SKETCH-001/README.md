# P12-SKETCH-001 — Sketch Constraints Verification

## Status

**PASS.** Sketches take six new constraints: angle, tangent, concentric,
midpoint, symmetric and diameter. Each is solved with an analytic Jacobian and
diagnosed like the existing ones (degrees of freedom, redundancy, conflicts),
checked when added, stored in a canonical order, driven by parameters where it
has a value, validated, saved and loaded. Sketch edits can be undone
(`ModifySketchCommand`).

Debug, Release and Debug-shared each passed **804/804** tests with **0
compiler warnings** from a verified clean rebuild. Every test of the P11
qualification and of `P12-PARAM-001` still passes in all three.

Date: 2026-09-17. `main` was at `2316127` (`P12-PARAM-001`) before this
milestone. Every number below was measured in this session and is recorded in
this directory.

## Scope

The deliverables are the ones `TODO.md` lists for `P12-SKETCH-001`.

| Deliverable | Status | Main evidence (test cases) |
| --- | --- | --- |
| Angle: two lines, counter-clockwise from the first, in (0°, 180°), literal or driven | IMPLEMENTED | `SketchSolver_AngleTurnsTheSecondLineCounterClockwiseFromTheFirst`; `…AngleIsMeasuredBetweenLinesNotTheirDirections`; `SketchDocument_DrivenDiameterAndAngleRegenerateGeometry` |
| Tangent: line and circle/arc, or two circles/arcs (external or internal) | IMPLEMENTED | `SketchSolver_TangentLineTouchesTheCircleOnItsOwnSide`; `…TangentArcAndLineJoinSmoothly`; `…TangentAtACoincidentJointIsExact`; `…TangentArcTouchesACircleWithoutAJoint`; `…TangentCirclesKeepTheirKindOfContact`; `…TangentArcsShareASmoothJoint` |
| Concentric: two circles or arcs | IMPLEMENTED | `SketchSolver_ConcentricBringsTheCentresTogether` |
| Midpoint: a point halfway along a line | IMPLEMENTED | `SketchSolver_MidpointPlacesThePointHalfwayAlongTheLine` |
| Symmetric: two points mirrored across a line | IMPLEMENTED | `SketchSolver_SymmetricMirrorsPointsAcrossTheLine` |
| Diameter: a circle or arc, literal or driven | IMPLEMENTED | `SketchSolver_DiameterSetsTwiceTheRadius`; `SketchDocument_DrivenDiameterAndAngleRegenerateGeometry` |
| Analytic Jacobians; DOF, redundancy and conflicts for the new types | IMPLEMENTED | `SketchSolver_EachNewConstraintRemovesItsDegreesOfFreedom`; `…RedundantNewConstraintsAreOverConstrained`; `…ConflictingNewConstraintsAreInconsistent` |
| Reference validation and canonical order at creation; structured errors | IMPLEMENTED | `SketchConstraint_NewTypesValidateTheirReferences`; `SketchConstraint_AngleAndDiameterValuesAreChecked` |
| Driving parameters, regeneration, validation | IMPLEMENTED | `SketchConstraint_DrivingParametersMustHaveTheRightDimension`; `SketchDocument_DrivenDiameterAndAngleRegenerateGeometry`; `SketchDocument_InvalidDrivenValuesFailTheSketch`; `SketchDocument_ValidationSolvesTheNewConstraints` |
| Undoable sketch edits (`ModifySketchCommand`) | IMPLEMENTED | `SketchCommands_ModifySketchCommandUndoesAndRedoesEdits` |
| Save/load round-trip | IMPLEMENTED | `SketchFile_NewConstraintsRoundTrip`; `SketchFile_MalformedNewConstraintsAreRejectedWithThePath` |
| Evidence | this directory | — |

**Not in scope, and not implemented:** tangency to ellipses or splines (those
entities are `P12-SKETCH-002`), point-on-curve constraints, and angle
constraints between anything but two lines.

## Design

```text
Constraints.hpp (sketch)     six new ConstraintType values; Constraint::angle;
                             hasValue() / hasAngle() / isDrivable()
Sketch (sketch)              addAngle/addTangent/addConcentric/addMidpoint/
                             addSymmetric/addDiameter; canonical order;
                             setConstraintAngle; restoreContent
SolverSystem (sketch, private)  new equation kinds with analytic Jacobians
SketchRegeneration (sketch)  angle parameters drive angle constraints
ModifySketchCommand (sketch) undoable edit of one sketch
SketchJson (io)              new type names; "angle" in radians
validateDocument (features)  an angle constraint's parameter must be an angle
```

**Equations.** Residuals stay lengths, as for the existing constraints, so one
tolerance applies to all:

| Constraint | Equations | Residual |
| --- | --- | --- |
| Angle θ | 1 | (cross(d1, d2) cos θ − dot(d1, d2) sin θ) / \|d2\| = \|d1\| sin(φ − θ) |
| Tangent, line and circle/arc | 1 | signed distance of the centre from the line − s·R (s: the side the centre starts on) |
| Tangent, two circles/arcs | 1 | \|c1 − c2\| − (k1 R1 + k2 R2), k = (1, 1) external or (1, −1)/(−1, 1) internal, from the start geometry |
| Tangent at a joint, line and arc | 1 | dot(line, radius at the joint) / \|radius\| (perpendicular) |
| Tangent at a joint, two arcs | 1 | cross(radius 1, radius 2) / \|radius 2\| (parallel) |
| Concentric | 2 | Δx, Δy of the centres |
| Midpoint | 2 | p − (a + b)/2, per axis |
| Symmetric | 2 | signed distance of (p + q)/2 from the line; dot(q − p, line) / \|line\| |
| Diameter D | 1 | R − D/2 |

An arc's radius R is \|start − centre\|; a circle's is its radius variable.

**The joint finding.** The first tangent implementation used the distance form
everywhere. An obround slot (arcs joined to lines at shared points) then failed
to solve to tolerance. At a shared point the line already passes through the
circle, so the distance residual is second order in the angle error: at a
residual of 1e-13 m the joint could still be off by √(2·r·tolerance), about
4e-5 mm for r = 8 mm. Where the two entities end at one point entity, or at
two joined by an enabled coincident constraint, tangency is now the first-order
condition at that point (perpendicular to the radius, or two radii parallel).
`SketchSolver_TangentAtACoincidentJointIsExact` is the regression test: the
joint's cosine is −2.9e-13 and the line's distance from the centre
10.000000000004 mm (tolerance 1e-10 and 1e-8 mm).

**Canonical order and checks.** A (line, point) midpoint or point-line
distance is stored as (point, line); a (circle/arc, line) tangent as (line,
circle/arc); a symmetric constraint as (point, point, line). Refused when added,
with InvalidArgument and a message naming the constraint and what it got: a
wrong signature (`a tangent constraint takes a line and a circle or arc, or two
circles or arcs, got line, line`), an angle outside (0, 180°) (`… got 190
deg`), a length given to an angle constraint, a diameter ≤ 2e-10 m, a
concentric pair sharing its centre point, and a line's own end point as its
midpoint. `insertConstraint` (loading, undo) runs the same checks.

**Driving.** A length parameter drives Distance, Radius and Diameter; an angle
parameter drives Angle. Regeneration converts with `as<Angle>` and fails the
sketch (the dependents are blocked) when the value is out of range; validation
reports a parameter of the wrong dimension.

**Undo.** `ModifySketchCommand` runs its edit on a copy; a failing edit leaves
the document unchanged. It keeps the content before and after, and undo/redo
restore it with `Sketch::restoreContent`, IDs and ID counters included, without
running the edit again.

**Files.** Constraints keep format version 1. The new types use their names
(`"angle"`, `"tangent"`, …); an angle constraint stores `"angle"` in radians,
the others `"value"` in metres, each only when the type has one. Files
without the new types are written exactly as before.

## Independent Validation

Every solver result is checked with geometry computed in the test from the
solved points, not with the solver's residuals: `atan2` directions, point-line
distances, reflections written out as q = 2 (p·a) a − p, centre distances.
Tolerances: 1e-8 mm for positions (the solver stops below 1e-10 mm), 1e-7° for
angles, 1e-10 for cosines. The largest deviations measured in the Release run:

| Check | Tolerance | Largest deviation |
| --- | --- | --- |
| Positions and distances | 1e-8 mm | 5.8e-12 mm |
| Angles from `atan2` | 1e-7° | 2.9e-11° |
| Cosines at joints | 1e-10 | 2.9e-13 |
| Areas and volumes | 1e-12 relative | 2.4e-13 relative |

### Profiles with analytic areas

| Profile | Constraints | Analytic | Measured (Green's theorem on the solved sketch) | Relative error |
| --- | --- | --- | --- | --- |
| Regular hexagon, side 20 mm | 1 fixed, 1 horizontal, 1 distance, 4 equal, 4 angles of 60° | (3√3/2)·20² = 1039.23048454132640 mm² | 1039.23048454157151 mm² | 2.4e-13 |
| Obround, D = 16 mm, L = 40 mm | 4 tangents at joints, diameter, equal, distance | L·D + πD²/4 = 841.06192982974676 mm² | 841.06192982974676 mm² | 0 |

The hexagon has no constraint on its sixth side; the test also checks that side
is 20 mm long and turns 60°. Both sketches are **fully constrained** (0 degrees
of freedom).

### Driven geometry rebuilds with the analytic volume

`SketchDocument_DrivenDiameterAndAngleRegenerateGeometry`: a slot whose
diameter and centre distance are parameters, and a wedge (sides 60 and 40 mm)
whose angle is `opening = 2 * half_opening`, each extruded 10 mm. The expected
volumes are written out in the test: (L·D + πD²/4)·h and ½·60·40·sin(θ)·h.

| State | Slot: expected / measured (mm³) | Wedge: expected / measured (mm³) |
| --- | --- | --- |
| D = 16, L = 40, θ = 40° | 8410.61929829746805 / 8410.61929829746805 | 7713.45131623847101 / 7713.45131623855195 (1.0e-14) |
| D = 22, L = 55, θ = 70° | 15901.32711084364928 / 15901.32711084364928 | 11276.31144943090112 / 11276.31144943090112 |
| after three undos | 8410.61929829746805 / 8410.61929829747169 (4.3e-16) | 7713.45131623847101 / 7713.45131623847283 (2.4e-16) |

Tolerance 1e-12 relative. The second regeneration reports `opening` as its
only updated parameter and regenerates both sketches and both extrusions.

### Degrees of freedom, redundancy, conflicts

`SketchSolver_EachNewConstraintRemovesItsDegreesOfFreedom` starts from 18
degrees of freedom (two lines, two circles, two points) and adds one constraint
at a time: angle 18 → 17, diameter → 16, concentric → 14, midpoint → 12,
symmetric → 10, tangent → 9, exactly each constraint's equation count.

| Case | Status | Reported | Sketch |
| --- | --- | --- | --- |
| angle 90° + perpendicular | OVER_CONSTRAINED | redundant: the perpendicular | unchanged |
| concentric + coincident centres | OVER_CONSTRAINED | redundant: the coincident | — |
| diameter 20 + radius 10 | OVER_CONSTRAINED | redundant: the radius | — |
| tangent + centre-line distance 10 | OVER_CONSTRAINED | redundant: the distance | — |
| angle 30° + parallel (fixed first line) | INCONSISTENT | conflicting: both (largest residual 12.94 mm) | unchanged, `geometryChanged` false |
| diameter 20 + radius 5 | INCONSISTENT | conflicting: both | — |
| midpoint of a fixed line at a fixed point off it | INCONSISTENT | conflicting: the midpoint | — |

Under-constrained results are reported with their remaining freedom (a
horizontal tangent line slides: 2; a free arc touching a circle: 2; a
concentric arc: 3).

## Diagnostics

Asserted verbatim by the tests (all InvalidArgument):

| Case | Message |
| --- | --- |
| angle of a line and a circle | `an angle constraint takes two lines, got line, circle` |
| tangent of two lines | `a tangent constraint takes a line and a circle or arc, or two circles or arcs, got line, line` |
| concentric of a circle and a line | `a concentric constraint takes two circles or arcs, got circle, line` |
| diameter of a line | `a diameter constraint takes a circle or an arc, got line` |
| diameter 0 | `diameter 0 mm must be positive` |
| angle 0° | `an angle constraint needs an angle between 0 and 180 deg (exclusive), got 0 deg` |
| driven angle 2 × 95° (regeneration) | `… got 190 deg`; the wedge sketch fails and its extrusion is blocked, the slot is kept |
| diameter driven by an angle (validation) | `SlotSketch (object:6): constraint:4 is driven by half_opening (object:4), which is an angle, not a length` |

Malformed files are refused (`SketchFile_MalformedNewConstraintsAreRejectedWithThePath`):
a missing `angle` (`… got none`), an angle written as text
(`….angle: expected a number`) and an unknown type (`unknown type 'kissing'`).

## Tests

27 new Catch2 test cases, all tagged `[sketch][p12]`:

- `tests/sketch/AdvancedConstraintSolverTests.cpp`: 18 (each constraint,
  joints, DOF, redundancy, conflicts, hexagon, obround, determinism);
- `tests/sketch/AdvancedConstraintDocumentTests.cpp`: 9 (references and
  values, parameter dimensions, driven regeneration, invalid driven values,
  validation, `ModifySketchCommand`, file round trip, malformed files).

The Release run records **715 passed assertions and 0 failed**
(`constraint-values-release.txt`). No legacy test file was changed.

## Qualification

`qualify.cmd` (in this directory, run through `run-qualification.cmd`)
configured each preset, removed every build output (the first attempt
succeeded for each: `clean-*.log`), rebuilt with warnings as errors under code
page 65001, and ran CTest only after a successful build. It then repeated the
related tests five times in Release and Debug. It recorded the Git tree IDs of
the sources it built (`qualification-times.txt`); the working tree had the same
IDs after the run.

| Preset | Configure | Clean | Build | TUs compiled | `warning` lines | Tests |
| --- | --- | --- | --- | --- | --- | --- |
| Debug | exit 0 | attempt 1 | exit 0 | 283 | 0 | **804/804 passed** (108.8 s) |
| Release | exit 0 | attempt 1 | exit 0 | 283 | 0 | **804/804 passed** (125.0 s) |
| Debug-shared | exit 0 | attempt 1 | exit 0 | 283 | 0 | **804/804 passed** (112.9 s) |

283 = `P12-PARAM-001`'s 281 translation units + the two new test files.

**Repeats** (`ctest -R "[Ss]ketch|[Ss]olver|[Cc]onstraint|[Rr]egenerat|[Vv]alidat|[Dd]ocument|[Cc]ommand|[Ee]xpression|P9|profile|[Pp]rofile" --repeat until-fail:5`):
266 tests, the 27 new ones among them.

| Preset | Result | Time |
| --- | --- | --- |
| Release | 266/266, each 5 times (1330 passed runs) | 137.9 s |
| Debug | 266/266, each 5 times (1330 passed runs) | 139.0 s |

**Determinism.** `SketchSolver_NewConstraintsSolveDeterministically` solves
two identically built sketches: same iteration count, bit-identical content,
and a second solve changes nothing. `SketchFile_NewConstraintsRoundTrip`
checks that a loaded file is equivalent to the saved document, serializes to
identical text, and regenerates bit-identical volumes. Across configurations
(`values-determinism.txt`): the Debug, Release and Debug-shared test
executables printed identical measured values (MD5
`a9c792e49bdb0d3eb132c7133d689d43` over 564 lines, the two lines holding a
body's address removed).

## Legacy Regression

`regression-comparison.txt` compares every log by test name with
`../P11-QUAL-001/release-ctest.log` (737 distinct names), and
`regression-comparison-param001.txt` with `../P12-PARAM-001/ctest-release.log`
(776 distinct names):

- every baseline name is present and passed in the Debug, Release and
  Debug-shared logs; no entry in any log failed;
- the new names are the 27 new test cases (66 against P11: the 39 of
  `P12-PARAM-001` and these 27).

**The P0–P12-PARAM-001 regression suite remains green.** Changes to existing
production code:

- `Sketch`: the new `add…` functions, canonical order, `setConstraintAngle`,
  `restoreContent`, `adoptSolution` copies angles;
- the solver's equation system (new kinds; the existing kinds are unchanged);
- `SketchRegeneration`: angle parameters;
- `SketchJson`: the new type names and the `angle` field;
- `validateDocument`: the expected dimension of a driving parameter;
- `CreateSketchCommand`'s header gained `ModifySketchCommand`.

## Known Limitations

- **Tangency is kept on the side it starts.** A line stays on the side of the
  circle where its centre starts; two circles keep the kind of contact
  (external or internal) their start geometry is nearer to. A sketch drawn
  far from its intended shape can solve to the other tangent.
- **Joints are found by identity.** Tangency at a joint needs one shared point
  entity or an enabled coincident constraint. Two ends that merely lie on one
  another are solved with the distance form, which is exact only to
  √(2·r·tolerance) at the contact point.
- **An angle is between lines, not directions**, in (0°, 180°): 0° and 180°
  are expressed with a parallel constraint.
- **Concentric takes circles and arcs only**; a point at a centre uses a
  coincident constraint.
- **Angle and parallel residuals scale with the first line's length**, so a
  conflicting pair on a free line is also satisfied by a line of zero length.
  A collapsed solution is refused by the solver's degeneracy check
  (SOLVER_FAILURE), never written back; the conflict test fixes the first line
  so that the conflict itself is reported. Which of the two a free line gives
  was not measured.

## Evidence Files

- `README.md`: this file.
- `qualify.cmd`, `run-qualification.cmd`: the qualification as run.
- `qualification-times.txt`: every step's start, exit code and time, the HEAD
  and the Git tree IDs of the qualified sources.
- `configure-*.log`, `clean-*.log`, `build-*.log`, `ctest-*.log`: the three
  presets.
- `ctest-repeat-{release,debug}.log`: the related tests, 5 times each.
- `regression-comparison.txt`, `regression-comparison-param001.txt`,
  `compare-regression.py`: the legacy comparisons.
- `constraint-values-release.txt`, `values.py`, `values-header.txt`: the
  measured values; `values-determinism.txt`: the same values from all three
  configurations.

## Final Result

```text
TASK:            P12-SKETCH-001 Sketch constraints: angle, tangent, concentric,
                 midpoint, symmetric, diameter
IMPLEMENTATION:  six constraint types with analytic Jacobians, joint-aware
                 tangency, canonical order and checks, angle-driving
                 parameters, ModifySketchCommand, file support
TESTS:           27 new tests; 804/804 in Debug, Release, Debug-shared;
                 266 related tests x5 in Release and Debug
VALIDATION:      hexagon area 2.4e-13 and obround area exact against the
                 formulas; driven slot and wedge volumes within 1.0e-14;
                 positions within 5.8e-12 mm of independent geometry;
                 values identical across configurations
RESULT:          PASS
EVIDENCE:        docs/verification/P12-SKETCH-001/
TODO:            P12-SKETCH-001 deliverables ticked
```
