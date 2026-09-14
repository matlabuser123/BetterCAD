# P11-FEAT-001 — Revolve Verification

## Status

PASS

Date: 2026-09-14. The baseline is v0.1.0 (`2da8966`); `main` was at
`645490c` before this milestone.

## Scope

Every item was checked against the working tree; all are **IMPLEMENTED**.

| Item | Status | Main evidence (test cases) |
| --- | --- | --- |
| Revolve feature definition | IMPLEMENTED | `Revolve definitions are validated`; `A revolve declares its profile, angle parameter and target as dependencies` |
| Axis/reference representation | IMPLEMENTED | `A revolve about a sketch line follows the line`; `Revolution axes must be finite, with a real direction`; round trip below |
| Closed-profile revolve | IMPLEMENTED | `Full revolutions match analytic solids of revolution`; `P11-FEAT-001 acceptance: a full revolve of a constrained profile matches the analytic solid` |
| Partial-angle revolve | IMPLEMENTED | `Partial revolutions scale with the sweep angle (Pappus)`; `Partial revolves give proportional volumes in the chosen direction` |
| Positive/negative direction | IMPLEMENTED | `The sweep sense follows the right-hand rule about the axis`; the partial-angle feature test |
| New body | IMPLEMENTED | turned part `Turn`, single revolves, `Wing` |
| Add (join) | IMPLEMENTED | `Revolves join, cut and intersect with target bodies` (flange) |
| Remove (cut) | IMPLEMENTED | same test (groove); turned part (revolve cuts an extrude, extrude cuts a revolve) |
| Intersect | IMPLEMENTED | same test (cone ∩ cylinder) |
| Geometry validation | IMPLEMENTED | `makeRevolution` requires a valid solid with finite positive volume; every test checks `isValid()` and the solid count |
| Parameter regeneration | IMPLEMENTED | `P11-FEAT-001 acceptance: changing the angle or the radius regenerates the revolve and its dependents` |
| Save/load | IMPLEMENTED | `P11-FEAT-001 acceptance: revolves survive save, destroy, load and regenerate`; `Revolve data is stored as transparent JSON`; `Malformed revolve data is rejected with the JSON path` |
| Undo/redo | IMPLEMENTED | `Undo and redo of revolve commands restore identical geometry`; the angle case of the regeneration test |
| STEP/STL | IMPLEMENTED | `Revolved bodies export to STEP and STL`; `info and validate describe revolves` |
| Analytical validation | IMPLEMENTED | analytic table below; `geometry-accuracy-release.txt` |
| Diagnostics | IMPLEMENTED | list below |

## Implementation

**Geometry** (`bettercad_geometry`, OCCT only in the adapter):

- `include/bettercad/core/geometry/Sweeps.hpp`: `makeRevolution(region, axis, from, to)`.
- `src/core/geometry/occt/OcctSweeps.cpp` (renamed from `OcctPrism.cpp`):
  - a profile-face builder shared with `makePrism`;
  - checks before the kernel: sweep in (0, 360°], finite axis origin, axis
    parallel to the plane (cosine ≤ 1e-9) and in it (≤ 1e-10 m), and the
    profile on one side of the axis. The side check is exact, using segment
    ends plus arc and circle extremes.
  - `BRepPrimAPI_MakeRevol`, then a check for valid solids with a finite,
    positive volume;
  - kernel exceptions become `Internal` errors (`guardKernelCall`).

**Features** (`bettercad_features`):

- `include/bettercad/features/Feature.hpp`, `src/features/Feature.cpp`: the
  `SolidFeature` base (operation, target), `validateOperation()`, and
  `combineWithTarget()`, the shared body combination. Kernel failures come
  back prefixed with the feature's name.
- `include/bettercad/features/RevolveFeature.hpp`,
  `src/features/revolve/RevolveFeature.cpp`: `RevolveFeature` (type
  `"revolve"`) with its persistent `RevolveDefinition`:
  - profile `SketchId`;
  - `RevolveAxis`: sketch X, sketch Y, or a sketch line `EntityId`;
  - `angle`, or an `angleParameter` (a `ParameterId`);
  - `RevolveDirection`: positive, negative or symmetric;
  - `FeatureOperation`;
  - `target` (a `FeatureId`).

  The feature's identity is its `ObjectId`/`FeatureId`. Geometry is not
  stored: bodies are derived by regeneration.
- `include/bettercad/features/Regeneration.hpp`,
  `src/features/revolve/RevolveRegeneration.cpp`: `resolveAngle()`,
  `resolveAxis()` and `regenerateRevolve()`.
- `src/features/SolidSupport.{hpp,cpp}`: shared profile resolution and
  region union, also used by Extrude.
- `Regenerator.cpp`: registers the `"revolve"` handler.
- `ResultBodies.cpp` and `Validation.cpp`: work on any `SolidFeature`;
  validation also checks the angle parameter's dimension and the axis line.
- `FeatureCommands.hpp`: `CreateFeatureCommand<F>` and
  `ModifyFeatureCommand<F>`, with the Extrude and Revolve aliases.

**Persistence** (`bettercad_io`): `src/io/json/FeatureJson.cpp` and
`DocumentJson.cpp` store `"revolve"` objects in `.bcad` format version 1.
The data is profile, axis `{"type": "sketch_x" | "sketch_y" | "line",
"line": id}`, angle in radians, `angle_parameter`, direction, operation and
target.

**CLI:** `bettercad-cli info` describes revolves; `validate` and the exports
handle them through `SolidFeature`.

**Refactor.** The refactor was separated out and verified first (301/301 in
debug before any Revolve code). Extrude now uses the same shared
infrastructure, with unchanged behaviour.

## Analytical Validation

Reference geometry: a rectangle revolved 360° about the Z axis, extending
radially from r1 = 10 mm to r2 = 20 mm with axial height h = 30 mm (a
cylindrical annulus). It is drawn in the XZ plane.

Formula: V = π (r2² − r1²) h

| | Value |
| --- | --- |
| Expected | 28274.333882308136 mm³ (9000π) |
| Actual | 28274.333882308139 mm³ |
| Error | 3.64e-12 mm³ absolute, 1.29e-16 relative |
| Tolerance | 1e-12 relative (`kRelTight`) |
| Result | **PASS** |

Also checked for the same annulus: surface area (relative error 3.62e-16),
the bounding box (−20, −20, 0) to (20, 20, 30) mm and centroid (0, 0, 15) mm
(both within 1e-9 mm), validity, and 1 solid.

The tolerance is justified because revolving lines and arcs gives planes,
cylinders, cones, spheres and tori. The kernel integrates these to rounding
level: the kernel's own error estimates are ≤ 6.8e-16, and every measured
error below is ≤ 1.04e-15. The 1e-12 figure is the project's existing tight
tolerance for exact surfaces (see `tests/core/geometry/GeometryTestSupport.hpp`).
Centroid coordinates are positions, so they are compared with the absolute
position tolerance of 1e-9 mm.

Further cases, measured on the release build (`geometry-accuracy-release.txt`):

| Case | Expected V (mm³) | Actual V (mm³) | Relative error |
| --- | --- | --- | --- |
| tube wedge 90° (V₃₆₀ × 90/360) | 7068.5834705770339 | 7068.5834705770358 | 2.57e-16 |
| tube wedge 180° | 14137.166941154068 | 14137.166941154075 | 5.15e-16 |
| tube wedge 270° | 21205.750411731104 | 21205.750411731107 | 1.72e-16 |
| cone r = 10, h = 20 | 2094.3951023931954 | 2094.3951023931959 | 2.17e-16 |
| sphere r = 10 (half disc on the axis) | 4188.7902047863909 | 4188.7902047863881 | 6.51e-16 |
| torus R = 20, a = 5 | 9869.6044010893584 | 9869.6044010893602 | 1.84e-16 |
| torus wedge 120° | 3289.8681336964528 | 3289.8681336964519 | 2.76e-16 |

The tests assert these and more within 1e-12:

- Partial sweeps of 1, 45, 90, 180, 270 and 359° for volume and area.
- The feature model V = θ/2 · [(r² − b²)h − groove] after angle, radius,
  height and bore changes.
- Joins, cuts and intersections against closed-form volumes.

## Tests

Each preset was rebuilt from clean (`cmake --build --preset <p>
--clean-first`), so all 204 translation units were recompiled with warnings
as errors. CTest ran only after the build succeeded.

| Preset | Configure | Clean build | Compiler `warning:`/`error:` lines | Tests |
| --- | --- | --- | --- | --- |
| Debug | exit 0 | exit 0, 204 files compiled | 0 | 326/326 passed |
| Release | exit 0 | exit 0, 204 files compiled | 0 | 326/326 passed |
| Debug-shared | exit 0 | exit 0, 204 files compiled | 0 | 326/326 passed (see note) |

**Debug-shared note.** In the first test run after the clean build
(`ctest-debug-shared-attempt1.log`), one test did not start. Test #16, `A
revolve declares its profile, angle parameter and target as dependencies`,
was reported "Not Run": CTest could not launch `bettercad_tests.exe`
("resource busy or locked") just after it was relinked. The checkout lives
in OneDrive, whose sync client locks new files; this is the same hazard
recorded for P10. Re-running CTest on the unchanged build passed 326/326,
including #16 (`ctest-debug-shared.log`).

The 25 Revolve test cases (Catch2 tag `[revolve]`) passed in all three
presets:

- `tests/core/geometry/RevolutionTests.cpp`: 8
- `tests/features/RevolveTests.cpp`: 11
- `tests/io/RevolveFileTests.cpp`: 4
- `tests/features/ValidationTests.cpp`: 1
- `tests/cli/CliTests.cpp`: 1

## Regression

P0–P10: **PASS**. All 301 tests that existed at v0.1.0 pass in all three
presets. One legacy assertion was updated for a real reason:

- **Unknown-type example.** `Invalid document files are rejected with the
  JSON path` used `"revolve"` as its example of an unknown object type, and
  that type is now known. The example is now `"teapot"`, and a new assertion
  checks that a `"revolve"` object carrying extrude data is rejected field by
  field (`objects[1].data.depth: unknown field`).
- **Extrude refactor.** Extrude's behaviour is unchanged by the refactor.
  The only message change is that boolean failures now carry the feature's
  name.

## Diagnostics Tested

Each failure returns a `Result` error with an error code and a message
naming the feature and the cause. Nothing crashes, and no body is kept:
failed features have no body, and their dependents are blocked.

- **Open profile:** `FailedPrecondition`, "Groove: …".
- **Empty profile:** a sketch without entities gives `FailedPrecondition`,
  "Nothing: …".
- **Invalid axis entity:**
  - a missing line: `NotFound`, "Groove: the axis line entity:99 does not
    exist in sketch 'GrooveSketch'";
  - a point: `InvalidArgument`, "Groove: the axis entity:1 is a point, not a
    line".
- **Invalid axis geometry:**
  - axis off the plane: `InvalidArgument`, "… does not lie in the profile
    plane (5 mm away)";
  - not parallel to the plane: `InvalidArgument`;
  - non-finite origin: `InvalidArgument`, "the axis origin is not finite";
  - zero or non-finite directions cannot be constructed (`Direction3D`).
- **Zero-length axis line:** the sketch solver rejects it first
  (`SOLVER_FAILURE`) and the revolve is blocked. Evaluated directly,
  `resolveAxis` gives `InvalidArgument`, "the axis line entity:11 has zero
  length".
- **Profile crossing the axis:** `InvalidArgument`, "Groove: makeRevolution:
  the profile crosses the revolution axis (it extends 16 mm and 2 mm to
  either side)". This includes an arc that bulges across the axis while its
  ends do not, and a circle around the axis.
- **Self-intersecting (bow-tie) profile:** `Internal`, "makeRevolution: the
  profile face is invalid …".
- **Invalid angle:**
  - 0°, negative, over 360° or NaN in a definition: `InvalidArgument`;
  - 400° from a parameter: `InvalidArgument`, "Turn: revolve angle must be
    in (0, 360] deg …", with dependents blocked;
  - a length parameter as the angle: `DimensionMismatch`;
  - a stored angle of 7 rad: `InvalidArgument` with its JSON path.
- **Missing source sketch:** `NotFound`, "object:10 references object:9,
  which does not exist".
- **Missing target:** `NotFound`, "object:10 references object:8, which
  does not exist".
- **Target without a body** (a sketch): `FailedPrecondition`, "Groove: a cut
  feature needs the body of its target feature".
- **Boolean with an empty target body** (a cut removed everything):
  `FailedPrecondition`, "Rejoin: a join feature needs the body of its target
  feature". This is the deterministic boolean-failure case.

  A failure inside the kernel's boolean algorithm is still caught: it maps
  to `Internal` in `geometry::booleanOperation`, from P3, and now carries the
  feature's name. There is no deterministic model that makes OCCT's boolean
  fail, so that path is not exercised by a test.
- **Validation:** `bettercad-cli validate` and `validateDocument` report
  each of these once, under the matching check: consistency (an angle driven
  by a length; an axis that is not a line), missing references (a missing
  axis line) and regeneration (a crossing profile).

## Evidence Files

- `README.md`: this file.
- `configure-{debug,release,debug-shared}.log`
- `build-{debug,release,debug-shared}.log`: clean rebuilds, 204 files each,
  0 diagnostics.
- `ctest-debug.log`, `ctest-release.log`, `ctest-debug-shared.log`:
  326/326 each.
- `ctest-debug-shared-attempt1.log`: the run with the locked executable,
  kept for the record.
- `geometry-accuracy-release.txt`: kernel results against analytic
  solutions, including all solids of revolution.

## Final Result

PASS
