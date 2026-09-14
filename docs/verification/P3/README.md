# P3 — Open CASCADE geometry foundation (P3-001 … P3-004): verification

Date: 2026-09-14. Incremental build of the existing preset trees; every
changed translation unit was recompiled. Raw logs are in this directory.
Toolchain: GCC 16.1.0 (MinGW-w64 UCRT), CMake 4.4.2, Open CASCADE
Technology 8.0.1 (built by `deps/` with the same toolchain).

| Preset | Configure | Build (`-Werror`) | `warning:`/`error:` lines | Tests |
|--------|-----------|-------------------|---------------------------|-------|
| debug | exit 0 | exit 0 | 0 | 162/162 passed |
| release | exit 0 | exit 0 | 0 | 162/162 passed |
| debug-shared | exit 0 | exit 0 | 0 | 162/162 passed |

There are 23 new geometry test cases in `tests/core/geometry/`.
`geometry-accuracy-release.txt` is the output of
`examples/geometry_accuracy` (Release build).

## P3-001 — Open CASCADE integration

```text
BetterCAD API            include/bettercad/core/geometry/*.hpp   (no OCCT types)
     ↓
Geometry abstraction     Body (pimpl handle), Point3, Axis3, BoundingBox, MassProperties
     ↓
Open CASCADE adapter     src/core/geometry/occt/*                (only place OCCT is included)
     ↓
OCCT 8.0.1               TKernel … TKBool, linked PRIVATE to bettercad_geometry
```

Evidence:

- **Containment.** `architecture.layering` passes: no `*.hxx` include outside
  an `occt/` adapter directory.
- **Private dependency.** `PrimitiveTests.cpp` contains
  `#if __has_include(<TopoDS_Shape.hxx>) #error ... #endif`, and the test
  suite compiles. Negative control: the same probe compiled *with* the OCCT
  include path fails with `#error OCCT headers visible`. The test TU's
  compile command contains no `opencascade` path.
- **Kernel identity.** `The geometry kernel is Open CASCADE 8.0` checks
  `geometryKernel()`, which reports "Open CASCADE Technology" / "8.0.1".
- **Exceptions.** Kernel exceptions (`Standard_Failure`, which derives from
  `std::exception` in 8.0) are converted to `ErrorCode::Internal` results by
  `occt::guardKernelCall`. No kernel exception crosses the API.
- **Units.** OCCT model space is millimetres; conversion happens only in
  `occt/OcctBody.hpp`.

## P3-002 — Primitive solids

`makeBox`, `makeCylinder` and `makeSphere` each have an origin-based form and
a placed form. Tests:

- `Spec usage: makeBox(100_mm, 50_mm, 20_mm)`.
- `Primitives have the expected topology`: box 1/1/6/12/8; cylinder 3 faces
  and 3 edges (2 circles + seam); sphere 1 face and 3 edges (seam + 2
  degenerate pole edges).
- `Primitive sizes must be positive, finite and above kernel precision`: 10
  invalid inputs rejected with `InvalidArgument`; 1 µm is accepted.
- Also: `Directions are normalized and reject zero vectors`, `A default body
  is empty`, `Bodies are immutable values that share their shape`.

## P3-003 — Analytic geometry-property validation

Volume, surface area, centre of mass and bounding box are compared with
closed-form solutions (`PropertyTests.cpp`):

| Solid | Checked against | Sizes |
|-------|-----------------|-------|
| Box | V = LWH, A = 2(LW+LH+WH), centroid, bounds | 4 sizes, 0.25 mm to 2.5 m |
| Cylinder | V = πr²h, A = 2πr(r+h), centroid, bounds | 3 sizes, r from 0.5 to 250 mm |
| Sphere | V = 4/3πr³, A = 4πr², centroid, bounds | r = 1, 25, 500 mm |
| Placed solids | centroid and bounds move, volume unchanged | box corner, cylinder along +X, sphere at (1, 2, 3) m |

Volumes and areas use kernel adaptive integration with a 1e-10 relative
target. The kernel's own error estimate is exposed
(`MassProperties::volumeRelativeError`) and tested to be below 1e-10.

## P3-004 — Boolean operations

`booleanUnion`, `booleanDifference` and `booleanIntersection` (OCCT BOP,
single-threaded, `SimplifyResult`, solids only, result validated with
`BRepCheck_Analyzer`). Tests (`BooleanTests.cpp`):

| Test | Analytic check |
|------|----------------|
| Box − through cylinder (spec example) | V = 100000 − 2000π mm³, area, centroid, bounds, valid, topology 1/1/7/15/10 |
| Off-centre hole | centroid x = (V_box·50 − V_hole·25)/(V_box − V_hole) |
| Overlapping boxes | ∪ 1500, − 500, ∩ 500 mm³; coplanar faces merged (each result is a 6-face box); centroids |
| Sphere ∩ octant box | V = πr³/6, centroid at 3r/8 |
| Bicylinder (Steinmetz) | V = 16r³/3 |
| Sphere and off-axis cylinder | V(A∪B) = V(A)+V(B)−V(A∩B); (A−B)+(B−A)+(A∩B) = A∪B |
| Disjoint and enclosing bodies | union keeps 2 solids; empty intersection; subtracting an enclosing body gives empty |
| Determinism | same inputs give bit-identical volume and identical topology; inputs unchanged |
| Errors | empty operands rejected with `InvalidArgument` |

## Measured accuracy and tolerances

Measured relative errors (`geometry-accuracy-release.txt` and test output):

| Case | Relative error |
|------|----------------|
| Box, box − hole, box ∪/∩ box | 0 – 3.4e-16 |
| Cylinder, sphere (mm and m scale) | ≤ 5.8e-16 |
| Sphere ∩ octant box | 5.0e-15 |
| Bicylinder | 1.9e-13 |
| Sphere/off-axis cylinder, inclusion–exclusion | 3.1e-12 |
| Sphere/off-axis cylinder, partition identity | 4.1e-11 |

Test tolerances (`tests/core/geometry/GeometryTestSupport.hpp`):

| Tolerance | Value | Used for | Margin over measurement |
|-----------|-------|----------|-------------------------|
| `kRelTight` | 1e-12 | primitives; booleans whose intersection curves are lines or circles | ≥ 200× |
| `kRelCurvedIntersection` | 1e-11 | bicylinder (elliptic intersections) | 50× |
| `kRelApproximatedIntersection` | 1e-9 | curved surfaces meeting in general curves, which the kernel approximates to 1e-7 mm | 24× (partition) |
| positions | 1e-9 mm absolute | centroids, bounds | — |

## Findings during implementation

- **Misclassified tolerance.** The inclusion–exclusion test for an off-axis
  sphere and cylinder first used `kRelTight` and failed at 3.1e-12. Such
  surfaces meet in a general quartic curve that OCCT approximates, so the
  case belongs to a separate category. Its 1e-9 bound follows from the
  kernel's 1e-7 mm intersection precision over the ~100 mm² interface. The
  measured values are recorded above. The bound reflects the kernel's
  precision, not the value that happened to pass.
- **OCCT version matching.** OCCT's package version file only accepts an
  exact patch-level match (`find_package(OpenCASCADE 8.0)` rejects 8.0.1).
  BetterCAD finds the package without a version and checks
  8.0 ≤ version < 9.
- **Leaked compile definitions.** OCCT's package config adds Release-only
  directory definitions (`UNICODE`, `NOMINMAX`, `_WIN32_WINNT`,
  `OCC_CONVERT_SIGNALS`) to the calling directory. They are saved and
  restored around `find_package`, so Debug and Release compile identically.
- **`M_PI` in OCCT headers.** OCCT 8 headers use `M_PI`, which strict
  `-std=c++23` hides on MinGW. `_USE_MATH_DEFINES` is defined privately for
  `bettercad_geometry`, the only target that includes OCCT headers.
- **Deprecated typedefs.** OCCT 8 deprecates the `TopTools_*` container
  typedefs, and `-Werror` caught their use. The adapter uses the
  `NCollection` types.
- **Bounding-box padding.** `Bnd_Box::Get()` pads results with the box's
  gap. The adapter zeroes the gap after `AddOptimal(..., useTriangulation =
  false, useShapeTolerance = false)` to report exact bounds, as the
  bounding-box tests confirm to 1e-9 mm.
