# P12-DATUM-001 — Datum Planes, Axes and Coordinate Systems Verification

## Status

**PASS.** Datum planes, datum axes and coordinate systems are document
objects. They have stable IDs and dependency edges, regenerate and report
structured failures, are undoable, and are saved and loaded. They are resolved
from their definitions at every regeneration and never cached. Sketches
attach to datum planes and coordinate-system planes and follow them. Mirror
planes and circular-pattern axes may refer to datums.

The milestone also fixed a **performance regression that P12-SKETCH-002
introduced** in `Body::massProperties` (see *Mass properties*). Loft tests
had gone from 0.45 s to 55.8 s. Summed CTest time in Release went from 728 s
to 1091 s, and is now **578 s**. Accuracy was kept, and the centre of mass of
curved bodies is now covered by tests (the old adaptive integration had put
an elliptic prism's centre 6.4e-4 mm off). Bodies without faces on surfaces of
extrusion or revolution, lofts included, are computed as they were before
P12-SKETCH-002, bit for bit.

Debug, Release and Debug-shared each passed **869/869** tests with **0
compiler warnings** from a verified clean rebuild. Every test of the P11
qualification and of `P12-SKETCH-002` still passes in all three.

Date: 2026-09-17. `main` was at `070b730` (`P12-SKETCH-002`) before this
milestone. Every number below was measured in this session and is recorded in
this directory.

## Scope

The deliverables are the ones `TODO.md` lists for `P12-DATUM-001`.

| Deliverable | Status | Main evidence (test cases) |
| --- | --- | --- |
| Datum planes: fixed; offset from a plane; turned about an axis in a plane; offsets and angles literal or driven | IMPLEMENTED | `Datum_PlanesMatchIndependentGeometry`; `Datum_DefinitionsAreCheckedForTheirKind` |
| Datum axes: fixed; the intersection of two planes | IMPLEMENTED | `Datum_AxisIsWhereTwoPlanesMeet` |
| Coordinate systems: fixed; placed from another (or the model's) by rotations about its axes and a translation, literal or driven | IMPLEMENTED | `Datum_CoordinateSystemsTurnAboutTheirBaseAxesThenMove` |
| References to the principal planes and axes of the model and of coordinate systems | IMPLEMENTED | `…TurnAboutTheirBaseAxesThenMove / principal planes and axes of a coordinate system`; `Datum_PlanesMatchIndependentGeometry / principal planes of the model`; `Datum_KindAndReferenceNamesAreStable` |
| Document objects: stable IDs, graph edges, regeneration, failure propagation (missing, wrong kind, degenerate, cycles) | IMPLEMENTED | `DatumModel_DependenciesAreGraphEdges`; `Datum_ResolutionRefusesMissingWrongAndDegenerateReferences`; `DatumModel_FailuresBlockDependentsAndRecover`; `DatumModel_RegeneratesDeterministically` |
| Sketches attached to datum or coordinate-system planes follow them | IMPLEMENTED | `DatumModel_RegeneratesWithAnalyticGeometry`; `…FailuresBlockDependentsAndRecover / a sketch attached to an axis` |
| Mirror planes and circular-pattern axes may refer to datums | IMPLEMENTED | `DatumMirror_ReflectsAcrossADatumPlane`; `DatumModel_RegeneratesWithAnalyticGeometry` (pattern about `Spindle`) |
| Undoable creation and editing, validation diagnostics, CLI description | IMPLEMENTED | `DatumCommands_CreateAndModifyAreUndoable`; `DatumValidation_ReportsWrongKindsAndDimensions`; `DatumCli_InfoDescribesDatumsAndAttachments`; `DatumCli_ValidateReportsDatumProblems`; `cli.info.datum-block`; `cli.validate.datum-block`; `cli.export-step.datum-block` |
| Save/load round trip | IMPLEMENTED | `DatumFile_SaveLoad_PreservesDatumsAndRebuildsTheSameGeometry`; `DatumFile_ExampleFileMatchesTheBuilder`; `DatumFile_MalformedDatumsAreRejectedWithThePath`; `DatumFile_ExportedBodiesReadBackFromStep` |
| Evidence | this directory | — |

**Not in scope, and not implemented:** point datums; datums on model faces or
edges (these need stable face references: `P12-SKETCH-003`); datum references
in extrude directions, revolve axes, sweeps, lofts, holes or linear
patterns; a display of datums (there is no GUI).

## Design

```text
PlaneReference, AxisReference         an object ID (or none: the model) and a principal
  (core, document/References)         plane (xy, yz, xz) or axis (x, y, z); core value types,
                                      so sketches (layer 1) can hold them
DatumPlane, DatumAxis,                document objects storing only how they are placed
  CoordinateSystem (features)         ("datum_plane", "datum_axis", "coordinate_system")
resolvePlane, resolveAxis,            model-space geometry from the definitions, recursively;
  resolveCoordinateSystem             structured errors; nesting limited to 64
Create/ModifyDatumCommand<D>          undoable creation and editing
Sketch::attachment (sketch)           an optional plane reference; a dependency
Regenerator (features)                datum objects must resolve; an attached sketch takes its
                                      resolved plane as placement after it has solved
MirrorPlane::reference,               a datum plane or axis in place of an origin and a normal
  PatternAxis::reference              or direction
validateDocument                      wrong kinds of reference, driving parameters of the wrong
                                      dimension
DatumJson, FeatureJson, SketchJson    "datum_plane", "datum_axis", "coordinate_system";
  (io)                                "attachment"; "reference"
bettercad-cli info                    a description of each datum and attachment
OcctBody (geometry adapter)           mass properties face by face for curved bodies
```

**Placement rules.**

- An *offset* plane is its base plane moved along the base's normal. The
  base's axes are kept.
- An *angled* plane is its base plane turned right-handed about an axis. The
  axis must lie in the base plane, otherwise resolution fails with its
  angle and distance from the plane.
- An *intersection* axis runs along n1 × n2, through the point nearest the
  model's origin: p = a n1 + b n2, with a = (h1 − h2 c)/(1 − c²) and
  b = (h2 − h1 c)/(1 − c²), where h are the planes' offsets and c = n1·n2.
  Parallel planes fail.
- An *offset* coordinate system turns about its base's X, then Y, then Z axis
  (each right-handed, through the base's origin), then moves along the base's
  axes.
- A datum plane has one plane (`xy`), a datum axis one axis (`z`), and a
  coordinate system all three of each. Naming another is an error, never a
  substitution.

**Nothing is cached.** A datum object stores its definition, and resolution
is a pure function of the document. A sketch's saved placement is the one its
last regeneration computed. The next regeneration overwrites it, and only
after the sketch has solved, so a failure changes nothing.

**Stable references.** References are object IDs and principal-element names.
Nothing refers to kernel faces, edges or indices, as `TODO.md` requires.

### Mass properties

`P12-SKETCH-002` moved every body with a face that is not a plane, cylinder,
cone, sphere or torus to the kernel's Gauss–Kronrod volume integration at
1e-10. The kernel probes in `kernel-probe/` show what that cost and what
replaced it:

| Finding | Probe |
| --- | --- |
| Gauss–Kronrod on a full revolution of an ellipse or spline takes 6.4–10.4 s at tolerances of 1e-7 and tighter. Looser tolerances are erratic: the spline takes 3.2 s at 1e-6 and 0.04 s at 1e-5, and the ellipse is off by 3.9e-13 at 1e-6. | `gk_tolerance_probe`, `face_integration_probe` |
| On an oblique ruled loft between circles, Gauss–Kronrod takes 7.4 s for one B-spline face. The adaptive Gauss integration takes 0.001 s and agrees to 2e-16. | `face_integration_probe` |
| The adaptive integration is wrong only on faces on surfaces of extrusion or revolution: an extruded periodic spline's side face is off by 6.7 %, and revolutions by ~3e-9. | `face_integration_probe` |
| Splitting closed faces makes Gauss–Kronrod slower, not faster (33–74 s), so that route was dropped. | `closed_face_split_probe` |
| The kernel's adaptive and Gauss–Kronrod integrators give the same per-face quantities: the cone volume and first moment about an apex. On the faces of a block with a hole they agree to 5.9e-12 mm³ and 8.7e-11 mm⁴. Face-by-face sums reproduce the whole-shape centres to 7.1e-15 mm where the integration is exact. | `face_moment_probe` |
| The adaptive integration puts an elliptic prism's centre 6.4e-4 mm off. Gauss–Kronrod is exact. | `face_moment_probe` |
| On cut (trimmed) curved faces Gauss–Kronrod stays fast: at most 0.008 s a face for extrusions, and 0.04–0.25 s a face for a revolution cut by a plane. The adaptive integration still errs on the cut elliptic prism (volume 8.6e-12, centre 1.3e-6 mm), and Gauss–Kronrod is exact to 1.4e-15 there. | `face_moment_probe` |

**The fix** (`OcctBody.cpp`, `Body::massProperties`):

- Bodies without faces on surfaces of extrusion or revolution use the
  adaptive integration of the whole shape, as before P12-SKETCH-002. This
  covers planes, cylinders, cones, spheres, tori and loft B-splines.
- The other bodies are summed face by face, as cones from one apex (the mean
  of the vertices). The kernel's integration does the same internally.
  - A swept-curve face that covers its whole parameter rectangle is
    integrated by BetterCAD. For volume, first moment and area it uses the
    8-point Gauss–Legendre rule on each knot span, halved until the results
    change by less than 1e-14.
  - Any other swept-curve face takes the kernel's Gauss–Kronrod integration
    over knot spans.
  - Every other face takes the kernel's adaptive integration.
  - Areas come from BetterCAD's rule for covered swept-curve faces and from
    the kernel for the rest.
- The rule's sums are compensated (Neumaier). Summed plainly, the million
  terms of a 64-piece refinement drifted by 1e-13 relative: the spline
  prism's centre was 4.4e-12 mm off, and its refinement never met 1e-14. This
  was found by the existing test `CurvedProfile_SweptCurveSidesHaveTheirAnalyticArea`,
  whose 1e-12 mm tolerance was kept.

Two new test cases pin the result:

- `CurvedProfile_CurvedBodiesHaveTheirAnalyticCentreOfMass` checks centres to
  1e-12 mm for a tilted elliptic prism, a quadratic spline prism (Boole's
  rule, exact for its moments) and a quarter turn of an ellipse (the sector
  centroid).
- `CurvedProfile_CutCurvedBodiesKeepTheirProperties` covers the trimmed-face
  path: an elliptic prism cut at x = 15 (segment area and moment in closed
  form), a spline prism cut in half (Boole on the lower pieces), and a
  revolved ellipse cut at z = 30 (the removed ring integrated by the
  trapezoidal rule, exponentially convergent for its integrand).

**Effect on existing results.** `efdc1cf` (`P12-SKETCH-001`) was built in
Release in a temporary worktree. Its `[loft]` values were compared with this
milestone's qualified Release build by `compare-values.py`
(`loft-values-comparison.txt`, 2885 assertions each). Every value
`Body::massProperties` computes is identical, bit for bit. The only three
differing lines are STEP read-back volumes (`LoftFileTests.cpp:342`, `:359`,
`:372`) measured by the test-only reader, which has used the kernel's
Gauss–Kronrod integration since P12-SKETCH-002. They differ by at most
2.3e-15 relative and are checked against analytic volumes to 1e-9.

## Independent Validation

Expected geometry is computed in the tests by other means than the code
under test:

- rotation matrices written out and multiplied;
- plane equations solved by hand, with the intersection point checked to lie
  on both planes and nearest the origin;
- the model's volumes and centres from the block and boss dimensions;
- the fin's bounds from its rectangle's corners mapped by the tilted frame;
- the curved centres as described above.

| Check | Tolerance | Largest deviation measured (Release) |
| --- | --- | --- |
| Datum plane and coordinate-system origins | 1e-12 mm | 0 |
| Plane axes and normals, coordinate-system axes | 1e-14 | 1.1e-16 |
| Intersection axis: on both planes, nearest the origin | 1e-12 mm | 8.9e-16 mm |
| Datum model: pattern, peg and fin volumes | 1e-12 rel | 5.7e-16 |
| Datum model: pattern centre | 1e-9 mm | 7.1e-15 mm |
| Datum model: peg and fin bounds | 1e-7 mm | 7.1e-15 mm |
| Attached sketch placements | 1e-12 mm, 1e-14 | 6.0e-17 |
| Mirror across a datum plane: volume, centre | 1e-12 rel, 1e-9 mm | 2.9e-16, 7.1e-15 mm |
| STEP read-back volume of the datum model | 1e-9 rel | 2.9e-15 |
| Curved centres (tilted ellipse, spline, quarter turn) | 1e-12 mm | 1.4e-14 mm |
| Cut elliptic and spline prisms: volume, centre | 1e-12 rel, 1e-9 mm | 2.4e-16, 3.6e-15 mm |
| Cut revolved ellipse: volume, centre | 1e-9 rel, 1e-9 mm | 1.7e-11, 1.7e-10 mm |

The cut ring's deviation comes from the kernel's approximated intersection
curve (`kRelApproximatedIntersection`, P3): both kernel integrators are off by
about 1e-11 there in `face_moment_probe` (1.0e-11 and 1.3e-11).

**Driven geometry.** `DatumModel_RegeneratesWithAnalyticGeometry` changes the
three driving parameters:

- `height` 20 → 25 mm moves `TopPlane`, the boss sketch on it, the bosses and
  the pattern;
- `half_length` 50 → 45 mm moves the `Middle` plane, the `Spindle` axis and
  the pattern's centre;
- `tilt` 30° → 50° turns `TiltPlane` and the fin on it.

The regeneration report lists `TopPlane`, `BossSketch` and `Spindle` among
the rebuilt objects, and not `PegSketch`, whose `Station` did not move. Volumes, centres and bounds match the analytic
values in both states. Undoing the three edits restores the first state, with
the boss sketch's placement back on `TopPlane`.

## Diagnostics

Asserted verbatim by the tests:

| Case | Code | Message |
| --- | --- | --- |
| missing object | NotFound | `object:99 does not exist` |
| wrong kind | InvalidArgument | `Sketch1 (object:2) is a sketch, not a datum plane or a coordinate system` |
| wrong element | InvalidArgument | `Plane1 (object:3) is a datum plane, which has only one plane: refer to it as xy, not yz` |
| wrong element (axis) | InvalidArgument | `Axis1 (object:4) is a datum axis, which is one axis: refer to it as z, not x` |
| parallel planes | FailedPrecondition | `Nowhere (object:5): its planes are parallel and do not meet` |
| axis off the base plane | FailedPrecondition | `Hinged (object:6): its axis does not lie in its base plane (it is 0 deg and 20 mm off it)` |
| length driven by an angle | DimensionMismatch | `Wrong (object:6): …` |
| cycle, resolved directly | FailedPrecondition | `… its references nest more than 64 deep …` |
| cycle, in regeneration | — | `dependency cycle: TopPlane, Middle` |
| sketch attached to an axis | InvalidArgument | `BossSketch (object:8): Spindle (object:12) is a datum axis, not a datum plane or a coordinate system` |
| definition checks | InvalidArgument | `a fixed datum plane has no base plane`; `an angled datum plane has no offset`; `an intersection datum axis needs two different planes`; `a fixed coordinate system has no base, translation or rotation`; … (12 in all) |
| mirror with both | InvalidArgument | `a mirror plane given by a reference has no origin or normal of its own` |
| validation | error | `TopPlane (object:7): the offset is driven by tilt (object:3), which is an angle, not a length`; `FinSketch (object:18): the attachment is Block (object:6), which is an extrude, not a datum plane or a coordinate system` |
| file | — | `.data.kind: unknown value 'sideways'`; `.plane: unknown value 'uv'`; `.data.first: unknown field`; `.offset_parameter: expected an ID`; `.attachment.extra: unknown field`; `a pattern axis given by a reference has no origin or direction` |

In every failure case the object and its dependents keep no result, and
nothing is substituted. `…FailuresBlockDependentsAndRecover` checks the
blocked lists, and that the sketch's placement is unchanged, then undoes the
edit and regenerates cleanly.

## Tests

21 new Catch2 test cases, all tagged `[p12]`, and 3 process tests:

- `tests/features/DatumTests.cpp`: 13 (definitions, resolution, refusals,
  model regeneration, failures, graph, mirror, commands, validation,
  determinism);
- `tests/io/DatumFileTests.cpp`: 4 (round trip, example file, malformed
  files, STEP);
- `tests/cli/DatumCliTests.cpp`: 2 (`info`, `validate`);
- `tests/core/geometry/CurvedProfileTests.cpp`: 2 (curved centres, cut curved
  bodies);
- `cli.info.datum-block`, `cli.validate.datum-block`,
  `cli.export-step.datum-block` on `examples/models/datum_block.bcad`.

`tests/support/DatumModels.hpp` builds the reference model (four parameters,
15 objects: a block, a boss on an offset plane, a pattern about an
intersection axis, a peg on a coordinate system, a fin on an angled plane).
`examples/models/datum_block.bcad` is its saved form, and
`DatumFile_ExampleFileMatchesTheBuilder` keeps the two equal.

The Release run of the 21 cases records **1198 passed assertions and 0
failed** (`datum-values-release.txt`).

## Qualification

`qualify.cmd` was run through `run-qualification.cmd` and did the following:

- configured each preset;
- removed every build output (the first attempt succeeded for each);
- rebuilt with warnings as errors under code page 65001;
- ran CTest after each successful build;
- repeated the related tests five times in Release and Debug.

The Git tree IDs it recorded equal the working tree's after the run.

| Preset | Configure | Clean | Build | TUs compiled | `warning` lines | Tests |
| --- | --- | --- | --- | --- | --- | --- |
| Debug | exit 0 | attempt 1 | exit 0 | 295 | 0 | **869/869 passed** (86.9 s) |
| Release | exit 0 | attempt 1 | exit 0 | 295 | 0 | **869/869 passed** (90.7 s) |
| Debug-shared | exit 0 | attempt 1 | exit 0 | 295 | 0 | **869/869 passed** (100.1 s) |

295 is `P12-SKETCH-002`'s 288 translation units plus 7 new ones:
`document/References.cpp`, `datum/Datums.cpp`, `datum/DatumResolution.cpp`,
`json/DatumJson.cpp` and the three new test files.

**Repeats.** The selection covers datums, sketches, mirrors, patterns,
extrusions, revolutions, sweeps, lofts, curved profiles, properties,
regeneration, validation, documents, commands, files, the CLI and P9:
`ctest -R "[Dd]atum|[Ss]ketch|[Mm]irror|[Cc]ircular|[Pp]attern|[Ee]xtru|[Rr]evol|[Ss]weep|[Ll]oft|Curved|[Pp]ropert|[Rr]egenerat|[Vv]alidat|[Dd]ocument|[Cc]ommand|[Ff]ile|cli|P9" --repeat until-fail:5`
selected 500 tests.

| Preset | Result | Time |
| --- | --- | --- |
| Release | 500/500, each 5 times (2500 passed runs) | 203.9 s |
| Debug | 500/500, each 5 times (2500 passed runs) | 207.1 s |

**Timing** (`timing-comparison.txt`, CTest's per-test times summed, `-j 8`,
same machine):

| Preset | P12-SKETCH-001 | P12-SKETCH-002 | P12-DATUM-001 |
| --- | --- | --- | --- |
| Release | 727.5 s (804 tests) | 1091.2 s (845) | **578.0 s** (869) |
| Debug | 701.1 s | 1140.1 s | **582.4 s** |
| Debug-shared | 677.1 s | 1127.4 s | **606.2 s** |

Examples in Release: `LoftFeature_RegenerationIsDeterministic` went 0.45 →
55.84 → 0.26 s; `Loft_ArcsMatchArcsOfEqualSweep` 0.48 → 54.01 → 0.43 s;
`CurvedProfile_RevolvedEllipseAndSplineFollowPappus` 41.77 → 0.23 s. The
reference-model tests are also faster than at P12-SKETCH-001, by amounts that
vary from run to run (for example `ReferenceModel_AllModelsStressRegression`
at 31.1, 23.7 and 19.5 s). None of those models has a curved swept face, so
their integration is unchanged, and no speed-up is claimed for them.

**Determinism.**

- `DatumModel_RegeneratesDeterministically` builds the model twice to
  bit-identical volumes and equivalent documents. A second pass regenerates
  nothing.
- `DatumFile_SaveLoad_…` rebuilds bit-identical volumes after a round trip
  and serializes to identical text.
- Across configurations (`values-determinism.txt`), the Debug, Release and
  Debug-shared executables printed identical measured values for the 21
  cases. Debug and Debug-shared are identical line for line (597 lines, MD5
  `64a226375492f3ee9b6558a6f9f9ebb8`, after dropping the 3 lines that print
  pointers). In Release, 10 lines differ only in the **expected** value that
  the test computes with `std::cos`/`std::sin` (by one or two ulps); the
  measured values are identical. The compiler probably evaluates those
  constant expressions differently at -O2, but that was not investigated.

## Legacy Regression

`regression-comparison.txt` compares every log by test name with
`../P11-QUAL-001/release-ctest.log` (737 names), and
`regression-comparison-sketch002.txt` with `../P12-SKETCH-002/ctest-release.log`
(844 names):

- every baseline name is present and passed in the Debug, Release and
  Debug-shared logs, and no entry failed;
- the new names are the 24 new tests (131 against P11).

**The P0–P12-SKETCH-002 regression suite remains green.** No existing test
was changed. Changes to existing production code:

- `Sketch`: the attachment, its dependency, equality and restore;
- `MirrorFeature`, `CircularPatternFeature` and their regeneration: the
  optional reference;
- `Regenerator`: the datum handlers and the sketch placement;
- `validateDocument`: reference and dimension checks;
- `FeatureJson`, `SketchJson`, `DocumentJson`: the new fields and types;
- the CLI's `info`: descriptions;
- `OcctBody::massProperties`: the face-by-face integration.

## Known Limitations

- **Point datums, and datums on model faces and edges,** are not
  implemented. Face references need the stable-reference work of
  `P12-SKETCH-003`.
- **Only sketches, mirrors and circular patterns** take datum references.
- **An intersection axis** passes through the point nearest the model's
  origin, not a point the user picks.
- **Cut swept-curve faces** take the kernel's Gauss–Kronrod integration,
  measured at up to 0.25 s a face. Their areas are the kernel's, unverified
  (as recorded at P12-SKETCH-002).
- **Mass-property error estimates** are the largest per face: the kernel's
  estimate, or the last refinement change of BetterCAD's rule.

## Evidence Files

- `README.md`: this file.
- `qualify.cmd`, `run-qualification.cmd`: the qualification as run.
- `qualification-times.txt`: every step's start, exit code and time, the HEAD
  and the Git tree IDs of the qualified sources.
- `configure-*.log`, `clean-*.log`, `build-*.log`, `ctest-*.log`: the three
  presets.
- `ctest-repeat-{release,debug}.log`: the related tests, 5 times each.
- `regression-comparison.txt`, `regression-comparison-sketch002.txt`,
  `compare-regression.py`: the legacy comparisons.
- `datum-values-release.txt`, `values.py`, `values-header.txt`: measured
  values; `values-determinism.txt`, `compare-values.py`: the same values from
  all three configurations.
- `loft-values-comparison.txt`: `[loft]` values against P12-SKETCH-001.
- `timing-comparison.txt`, `compare-times.py`: per-test times against
  P12-SKETCH-001 and P12-SKETCH-002.
- `kernel-probe/`: the raw-OCCT probes and their logs
  (`gk_tolerance_probe`, `face_integration_probe`, `closed_face_split_probe`,
  `face_moment_probe`).

## Final Result

```text
TASK:            P12-DATUM-001 Datum planes, axes, coordinate systems
IMPLEMENTATION:  plane and axis references; datum planes, datum axes and
                 coordinate systems as document objects with pure resolution;
                 sketch attachment; datum mirror planes and pattern axes;
                 commands, validation, CLI and file support; face-by-face
                 mass properties for curved bodies (performance fix)
TESTS:           21 new test cases and 3 process tests; 869/869 in Debug,
                 Release, Debug-shared; 500 related tests x5 in Release and
                 Debug
VALIDATION:      frames within 1.1e-16 of written-out rotations; model
                 volumes, centres and bounds within 7.1e-15 mm (5.7e-16 rel)
                 of analytic values; curved centres within 1.4e-14 mm;
                 measured values identical across configurations
RESULT:          PASS
EVIDENCE:        docs/verification/P12-DATUM-001/
TODO:            P12-DATUM-001 deliverables ticked
```
