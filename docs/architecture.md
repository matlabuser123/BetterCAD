# BetterCAD architecture

BetterCAD is organised as a stack of libraries with a strict downward
dependency direction. Applications sit on top; third-party kernels sit
underneath, hidden behind adapters.

```text
 apps/bettercad (Qt desktop)        apps/bettercad_cli
            │                               │
     renderer        scripting              │
            └───────┬──────────────────────┘
                    io              native .bcad, STEP/STL export
                    │
                 features           extrude, revolve, chamfer, fillet, variable-radius fillet, hole, linear and circular patterns, mirror, sweep, loft, split, combine, shell, draft, rib (later: ...)
                    │
                  sketch            entities, constraints, solver
                    │
                   core             units, ids, parameters, document,
                    │               geometry abstraction, topology
                    │
        src/**/occt/ adapters       the only code that sees Open CASCADE
```

## Rules

These are enforced by `tests/architecture/CheckLayering.cmake`, which runs as
the `architecture.layering` test. Fixture trees under
`tests/architecture/fixtures/` prove the checker catches each violation.

1. **Open CASCADE containment.** OCCT headers (`*.hxx`) may only be included
   from an `occt/` adapter directory under `src/` (for example
   `src/core/geometry/occt/`). Public BetterCAD headers never expose OCCT types.
2. **Qt containment.** Qt headers may only be included from `apps/bettercad/`
   and `src/renderer/`. The modelling libraries are GUI-free.
3. **Layering.** A module may include public headers of its own module or of a
   lower layer only. Layers: `core` 0, `sketch` 1, `features` 2, `io` 3,
   `renderer`/`scripting` 4.
4. **Public/private split.** Public headers live in `include/bettercad/<module>/`
   and use only `<bettercad/...>`-style includes. Private headers live next to
   their sources in `src/<module>/`.

## Targets

| Target                       | Alias                         | Kind       | Contents                                      |
|------------------------------|-------------------------------|------------|-----------------------------------------------|
| `bettercad_core`             | `BetterCAD::core`             | library    | Build info, units, IDs, parameters, document  |
| `bettercad_geometry`         | `BetterCAD::geometry`         | library    | Solid geometry, meshing, STEP over OCCT       |
| `bettercad_sketch`           | `BetterCAD::sketch`           | library    | Sketches, constraints, solver (layer 1)       |
| `bettercad_features`         | `BetterCAD::features`         | library    | Features, regeneration, validation (layer 2)  |
| `bettercad_io`               | `BetterCAD::io`               | library    | Native document files, STEP/STL export        |
| `bettercad_cli_lib`          | `BetterCAD::cli_lib`          | static lib | CLI commands (new, info, validate, export)    |
| `bettercad_cli`              | —                             | executable | `bettercad-cli`                               |
| `bettercad`                  | —                             | executable | Qt desktop application (placeholder)          |
| `bettercad_tests`            | —                             | executable | Catch2 unit tests                             |
| `bettercad_test_occt`        | —                             | static lib | Test-only STEP read-back (OCCT)               |
| `bettercad_reference_models` | `BetterCAD::reference_models` | static lib | The mechanical reference models (P11-REF-001) |

Further module libraries (renderer, scripting, ...) are added as their
milestones start; see `TODO.md`.

## Conventions

- **Language:** C++23, no compiler extensions, no C++ modules.
- **Namespaces:** `bettercad` for core types, `bettercad::<module>` for modules
  (`bettercad::units`, `bettercad::sketch`, ...), `bettercad::app` and
  `bettercad::cli` for applications.
- **Symbol export:** each library gets a generated `<module>/Export.hpp`
  (`BETTERCAD_<MODULE>_EXPORT`). Libraries build static by default; the
  `debug-shared` preset verifies the shared-library configuration.
- **Warnings:** a strict warning set is applied to BetterCAD targets only
  (`cmake/BetterCADCompilerOptions.cmake`); presets build with warnings as
  errors.
- **Testing:** every milestone ships with Catch2 unit tests; executables also
  get process-level tests that launch the real binaries.

## Core engineering types

### Units (`bettercad/core/Units.hpp`)

- Engineering APIs take `Quantity<D>` types (`Length`, `Angle`, `Pressure`,
  ...), not plain `double`. Dimensional errors fail to compile.
- Values are stored in coherent SI units (m, kg, s, K, rad); `.si()` and
  `.in(units::mm)` extract numbers at API boundaries such as the geometry
  kernel adapter or the UI.
- Angle is a base dimension of its own, so `Length / Length` is a `double`
  but never an `Angle`.
- Unit factors are exact rationals, so one conversion is one correctly
  rounded operation. Arithmetic on SI values is compared with tolerances,
  never bit-exactly.
- Persist SI values with the shortest round-trip decimal representation.
  Display units are presentation metadata.

### Identity (`bettercad/core/Id.hpp`)

- Every persistent item has a strongly typed ID (`SketchId`, `FeatureId`,
  ...). IDs are never container indices, and allocators never reuse values.
- Document objects (sketches, features, parameters, bodies) share one
  object ID space; their IDs widen to `ObjectId`. Sketch entities and
  constraints are scoped to their sketch; faces, edges and vertices to their
  body.
- `DocumentId` is a UUID, because documents need global identity.

### Errors (`bettercad/core/Error.hpp`)

- Recoverable failures (bad input, missing references, malformed files)
  return `Result<T>` = `std::expected<T, Error>`. Programming errors are not
  reported through `Result`.
- `ErrorCode` is a broad, stable category; specifics go in the message.
  Failed operations leave the object unchanged.

### Parameters (`bettercad/core/parameters/`)

- A parameter's dimension is fixed by its display unit when it is created.
  Assignments of another dimension fail.
- Revision counters increment on effective changes only. This is the basis
  for dirty tracking and regeneration (P8).

### Parameter expressions (P12-PARAM-001)

- **Grammar** (`Expression.hpp`). `Expression::parse()` turns text such as
  `width - 2 * edge_distance` into a postfix program:
  `+ - * /`, unary `+`/`-`, parentheses, the usual precedence and left
  associativity, decimal numbers with an optional unit, and parameter
  names. It needs no document. Limits: 512 bytes, 32 levels of parentheses
  and unary signs, 64-character names.
  - A number without a unit is dimensionless; with one, it is converted with
    the unit catalog's exact factor.
  - A name directly after a number is always a unit, never a parameter:
    there is no implicit multiplication (`2 width` is refused). The unit is
    the longest catalog symbol that ends where a name would end, so `3 m/s`
    is a velocity and `3 mm/speed` is 3 mm divided by `speed`.
  - Malformed text fails with ParseError; oversized text, deep nesting and
    numbers out of range with InvalidArgument. Messages quote the token and
    give its byte offset; bytes that are not printable ASCII are shown as
    `the byte 0xNN`, so no UTF-8 sequence is cut.
- **Evaluation** is a loop over the postfix form on `DimensionedValue`s
  (dimension + SI value; `units/DimensionedValue.hpp`, shared with
  `ModifyParameterCommand`). `+`/`-` need equal dimensions (DimensionMismatch
  otherwise, quoting both operands); `*`/`/` combine them. Division by zero
  and non-finite values are InvalidArgument. Nothing is coerced.
- **Parameters** keep the text; a `Parameter` or `ParameterTable` does not
  interpret it. A `Document` checks its syntax whenever it stores one
  (`setParameterExpression`, `insertParameter`, `restoreParameter`, and so
  loading). A parameter with an expression is *driven*: setting its value
  directly is FailedPrecondition, except together with a new expression in
  one `ModifyParameterCommand`.
- **Dependencies** (`ParameterExpressions.hpp`). `buildDependencyGraph()`
  adds an edge from every parameter an expression names. A name that is not
  a parameter is listed in `DocumentGraph::unresolved` (NotFound, or
  InvalidArgument for an object's name), as is text that does not parse.
- **Evaluation of a document.** `evaluateParameterExpressions()` evaluates
  every driven parameter in the graph's topological order (ties by ID) and
  stores the values that changed, through a private `Document` entry point.
  A result must have the parameter's own dimension. A failing expression,
  and every driven parameter that depends on it, keeps its last value and
  is reported (`failed`, `blocked`). Parameters in a cycle are not
  evaluated (`cycles`). A parameter that names itself is a cycle of one.
- **Regeneration** runs that evaluation first in every pass. A value that
  changes advances the parameter's revision, so its dependents are dirty.
  Failed expressions are failures of the pass (their dependents are
  blocked); `RegenerationReport::updatedParameters` lists the driven
  parameters whose value changed. Parameters are never in `regenerated`.
- **Validation** evaluates on its copy first, checks sketches with the
  evaluated values, and reports unparsable expressions and object names as
  document-consistency errors, unknown names as missing references, cycles
  as dependency cycles, and evaluation failures as "failed to evaluate".
- **Files.** The `expression` field keeps its format-version-1 form: the
  text next to `si_value`, which holds the last evaluated value. A file thus
  loads without evaluating anything, and an older reader, which ignores
  expressions, still finds the values that were saved. A stored value that
  disagrees with its expression is replaced at the next regeneration and
  reported in `updatedParameters`. Unknown names load (a missing reference
  is a valid document state); malformed text is refused with its JSON path.

### Document and commands (`bettercad/core/document/`)

- `Document` owns parameters, polymorphic `DocumentObject`s and metadata.
  There is no global document state.
- Object kinds (sketch, feature, body) subclass `DocumentObject`, which
  defines `typeName()`, `clone()` and `contentEquals()`, in the module that
  defines them. Core never depends on them.
- Parameters and objects share one ID space and one namespace.
- Edits go through `Command`s (`execute`/`undo`/`redo`) recorded in a
  `CommandHistory`. Redo reproduces the exact state of the first execution,
  including IDs.
- Every mutation reports whether it changed anything. Only effective changes
  advance revisions, and only revisions drive dirty state (and, from P8,
  regeneration).

### Math value types (`bettercad/core/math/`)

- `Point2D`, `Point3D`, `Direction3D`, `Axis3D`, `BoundingBox2D`,
  `BoundingBox3D` and `Frame3D` live in `bettercad_core`, so 2D code (the
  sketch module) does not depend on the solid-modelling kernel.
- `Frame3D` is a right-handed orthonormal frame (Y = normal × X). It is the
  placement of a sketch plane.
- `Vector3D` (`Vector.hpp`) is a dimensionless vector, e.g. a direction as a
  user gives it before it is normalized. `Translation3D` is a displacement in
  lengths. `Translation3D::along(direction, distance)` is one product per
  component, so a pattern offset k·s·d is computed exactly once per instance.
- `UniformBSpline` (`BSpline.hpp`, P12-SKETCH-002) is a planar,
  non-rational B-spline with uniform integer knots, degree 2 to 5, open
  (clamped) or periodic: de Boor evaluation with the derivative, the knots in
  the kernel's form, and Bézier pieces by blossoming. It lives in core so
  sketches and the geometry kernel share one curve. `gaussLegendreRule()`
  gives 8 Newton-refined nodes on [0, 1], exact to degree 15: Green's-theorem
  integrands of splines up to degree 5, first moments included.
- `RigidTransform3D` (`RigidTransform.hpp`) is an orthogonal matrix plus a
  translation, applied as p' = A·p + t: a Euclidean isometry, which never
  scales. `RigidTransform3D::rotation(axis, angle)` builds A with Rodrigues'
  formula from the angle's own sine and cosine, and t = o − A·o, so the axis
  stays fixed. `RigidTransform3D::reflection(point, normal)` builds
  Householder's A = I − 2·n·nᵀ (det A = −1), with t = p0 − A·p0, so the plane
  stays fixed: p' = p − 2((p − p0)·n)n. `reversesOrientation()` (the sign of
  det A) tells a mirror from a rotation. A pure translation keeps an exactly
  identity matrix (`isTranslation()`), and applying it is bit-identical to
  adding the translation. Up to P11-FEAT-006 the class only held rotations;
  P11-FEAT-007 widened it to reflections (`rotationMatrix()` became
  `matrix()`) rather than adding a second transform type.

### Geometry (`bettercad/core/geometry/`, library `bettercad_geometry`)

- The public API (`Body`, `MassProperties`, primitives, booleans, plus the
  core value types `Point3D`, `Axis3D`, `BoundingBox3D`) contains no Open
  CASCADE types.
  `Body` is an immutable, cheaply copied handle to kernel data defined in
  `src/core/geometry/occt/`.
- OCCT is a PRIVATE dependency. Code using the geometry API compiles without
  OCCT headers, which a test checks with `__has_include`.
- OCCT model space is millimetres. Conversion to and from SI quantities
  happens only in the adapter.
- Kernel exceptions become `ErrorCode::Internal` results; boolean results
  are validated before they are returned.
- Mass properties use adaptive integration (1e-10 relative target) and
  report the error estimate. Bodies without faces on surfaces of extrusion
  or revolution (planes, cylinders, cones, spheres, tori, loft B-splines)
  use the kernel's adaptive Gauss integration of the whole shape, exact to
  rounding for them (P3, P11, P12-DATUM-001 probes). On faces swept from
  ellipses and splines that integration missed an extruded spline's volume
  by 4.5e-2, an elliptic prism's by 5.5e-9 while reporting 2e-16, their
  areas by up to 1e-2 (P12-SKETCH-002 kernel probe) and an elliptic prism's
  centre by 6e-4 mm (P12-DATUM-001). Bodies with such faces are summed face
  by face as cones from one apex (the mean of the vertices), which is how
  the kernel integrates internally:
  - a swept-curve face that covers its whole parameter rectangle is
    integrated by BetterCAD (volume, first moment and area; 8-point
    Gauss–Legendre on each knot span, halved until the results change by
    less than 1e-14, with compensated sums);
  - any other swept-curve face takes the kernel's Gauss–Kronrod integration
    over knot spans. It is exact on them but took about 10 s on a full
    revolution, so it is used only on trimmed faces;
  - all other faces take the kernel's adaptive integration and area.
- **Triangulation** (`Mesh.hpp`). `triangulate(body, {linearDeflection,
  angularDeflection})` returns a neutral indexed `Mesh` with vertices per face
  and outward counter-clockwise triangles. It meshes a copy: the kernel caches
  triangulations on shared faces, and meshing in place would make results
  depend on earlier requests. Meshing is single-threaded, so it is
  deterministic.
- **STEP** (`Exchange.hpp`). `writeStep(namedBodies, options)` returns AP214
  text in millimetres, with one product per body named after it. The header
  names BetterCAD as the originating system. A fixed `timeStamp` makes the
  output byte-for-byte reproducible. Functions return file contents;
  `bettercad_io` writes files.
- The kernel's default messenger printed to stdout. The adapter removes that
  printer once per process (`occt::initializeSession()`): a library must not
  write to the application's output, and problems are reported as `Result`
  errors.
- **Sweeps** (`Sweeps.hpp`). `makePrism(region, from, to)` translates a
  planar region along its normal; `makeRevolution(region, axis, from, to)`
  rotates it about an axis in its plane, right-handed, with
  0 < to − from ≤ 360°. Both share one profile-face builder
  (`occt/OcctSweeps.cpp`). Before calling the kernel, `makeRevolution`
  checks:
  - that the axis origin is finite (directions are finite unit vectors by
    construction) and the axis lies in the plane;
  - that the region lies on one side of the axis (touching is allowed).
    The check is exact for lines, arcs, circles and ellipses: it uses
    segment ends and the extreme points of arcs, circles and ellipses, so an
    arc bulging across the axis is caught even when its ends are not. For a
    spline it uses the poles, which bound the curve: a spline whose poles
    cross the axis is refused even if its curve does not.

  Afterwards it requires one or more valid solids with a finite, positive
  volume.

  `makeSweep(region, path)` moves a region along a `PlanarPath`: line, arc
  and circle segments (the profile segment types) in the local coordinates
  of a plane, head to tail, in the order of travel. A full circle is a
  closed path on its own, starting on the plane's X axis. The rules are
  BetterCAD's; the kernel only builds:
  - **Planning, before the kernel** (`SweepPlan.cpp`, kernel-independent).
    The path must be non-empty, finite, non-degenerate and connected (to
    1e-10 m, nothing is repaired). The path must start on the region's plane
    and leave it at right angles. Segments must meet tangentially (to
    1e-9 rad), except two straight segments, which may meet at a corner of
    less than 180°; the corner is mitred, both segments ending on the plane
    that bisects it. The region must not reach an arc's centre, and no
    straight segment may be shorter than its mitres use. All of these are
    InvalidArgument.
  - **Orientation (follow path).** The region moves with the frame (T, N, B),
    where T is the path's tangent, B the path plane's normal (fixed) and
    N = B × T. Every point keeps its N and B coordinates, so the region turns
    with the path and never twists: it translates along lines and turns
    about each arc's axis as in a revolution. The region may sit anywhere in
    its plane; its offset from the path is kept.
  - **Kernel.** `occt/OcctSweeps.cpp` uses `BRepOffsetAPI_MakePipeShell`
    (`TKOffset`) with a fixed binormal and right (mitred) corners. Each
    loop is swept on its own and the holes' solids are subtracted. The
    kernel probe in `docs/verification/P11-FEAT-008/kernel-probe` shows why:
    the default corner modes return solids flagged valid but wrong (half the
    volume, or none).
  - **Checks afterwards.** The result must be one valid solid without
    self-interference (`BRepAlgoAPI_Check`, which finds a path passing too
    close to itself). By the theorem of Pappus, its volume must equal the
    region's area times the length of the path its centroid travels
    (`regionCentroid()`), to 1e-9 relative. That check caught every wrongly
    built corner when the default corner mode was tried.

  `makeLoft(sections)` builds a ruled solid through two or more planar
  regions, in the order given: straight lines join matching points of
  consecutive sections. Again the rules are BetterCAD's:
  - **Planning, before the kernel** (`LoftPlan.cpp`, kernel-independent).
    - Each section is one closed loop (lines and arcs, or a circle) without
      holes.
    - The planes must be parallel (to 1e-9 rad). The loft runs from the
      first section towards the second, and every section must lie strictly
      beyond the one before (never on the same plane, never back). The list
      is kept as given, never sorted.
    - Every loop is re-expressed in a common frame (the loft direction as
      normal, the first section's X axis) and taken counter-clockwise about
      the loft direction, whatever its sketch's orientation.
  - **Matching (correspondence).** Consecutive sections must have the same
    shape: both circles, or the same lines and arcs in the same cyclic
    order, matched arcs turning by equal angles. Each loop starts where its
    corners lie nearest the previous section's corners, measured from each
    section's centroid (the least twist). Costs within 1e-9 of the
    sections' size tie and go to the loop's first start, so rounding noise
    cannot flip a twist. Circles match angle for angle from the common X
    axis (their seams are placed there).
  - **Fold check and volume.** Between two sections the cross-section's
    area is quadratic in the height, known from the two sections and the
    section halfway (matching points averaged: lines to lines, arcs to
    arcs). It must stay positive, or the loft folds, which is refused. Its
    integral, h/6 (A0 + 4 Am + A1) (the prismatoid formula), is the expected
    volume.
  - **Kernel.** `BRepOffsetAPI_ThruSections` (solid, ruled) with
    `CheckCompatibility(false)`, so the kernel keeps BetterCAD's matching.
    The probe in `docs/verification/P11-FEAT-009/kernel-probe` shows that a
    shifted matching, a reversed section or smooth interpolation give other
    solids, which the kernel still calls valid, and that coincident or
    backward sections give "valid" solids of no or wrong volume.
  - **Checks afterwards.** One valid solid, no self-interference
    (`BRepAlgoAPI_Check`), and a volume equal to the prismatoid volume to
    1e-8 relative. The kernel makes cones and cylinders between coaxial
    circles and arcs, and B-spline surfaces elsewhere (even where the side
    is planar, as between parallel lines). Volumes agree to rounding, except
    between arcs or circles that are not coaxial or are turned against each
    other, where they agree within 6.3e-10 (measured). Plane references
    therefore do not find a loft's sides (only its end faces).
- **Edges and edge references** (`Edges.hpp`). `listEdges(body)` describes a
  body's edges by their geometry: curve kind, ends, length, number of faces,
  and `faceAngle`, the angle between the two faces' outward normals (0 where
  they join smoothly, 90° along a box edge). Features refer to an edge by an `EdgeSignature`: the edge's
  supporting line, or its circle (centre, axis, radius), in a canonical form.
  `findEdges(body, signature)` returns the edges on that curve, matched
  within 1e-7 mm and 1e-9 rad. Kernel enumeration order and kernel object
  identity are never used as references. This is geometric matching, **not**
  persistent topological naming (a later milestone), and its limits are part
  of the contract:
  - it survives changes that keep the edge on its curve, such as widening a
    box, which lengthens its edges along the width;
  - it fails with NotFound when the curve moves or changes size, for example
    when a box gets taller and its top edges move;
  - it fails as ambiguous when several edges lie on the curve, for example
    when a cut splits the edge.

  No other edge is ever substituted.
- **Chamfer** (`Chamfer.hpp`). `chamferEdges(body, request)` returns a new
  body; the input is never modified. A `ChamferRequest` holds edge
  signatures, a mode and its values:
  - `EqualDistance`: d on both faces;
  - `TwoDistance`: d1 on the reference face and d2 on the other;
  - `DistanceAngle`: d on the reference face, with the chamfer face at the
    given angle to it.

  The reference face at each edge is the one whose outward normal is closer
  to the request's `referenceSide`. If the two normals cannot be told apart,
  the chamfer fails instead of guessing. `validate(request)` checks
  everything that does not depend on a body.

  **Fit check.** Before the kernel runs, every chamfer must fit, by at least
  0.001 mm. That includes edges the kernel adds along a smooth (tangent)
  chain. On each face next to a chamfered edge, the chamfer's strip must:
  - stay clear of the face's other edges;
  - not run across the face;
  - not meet another chamfer's strip.

  Otherwise the result is `FailedPrecondition` with the widths and the room.

  The check exists because OCCT 8.0.1, built with our toolchain, crashes
  the process on chamfers that do not fit, instead of failing (see
  `docs/verification/P11-FEAT-002/`). It measures straight-line
  distances, so on curved faces it errs towards refusing.

  Kernel failures that remain become `FailedPrecondition`, and every result
  must be one valid solid with a finite, positive volume.
- **Fillet** (`Fillet.hpp`). `filletEdges(body, request)` rounds edges with a
  constant radius; the input body is never modified. A `FilletRequest` is
  edge signatures plus a radius. The kernel continues a fillet along
  tangent edges, so a smooth chain is rounded as a whole. Where selected
  edges meet at a vertex, the kernel blends the corner: two edges meet as the
  union of their corner regions, three as a spherical corner.
  - Edges whose faces join smoothly (face angle below 1e-6 rad) have no
    corner to round and are refused.
  - The fit check is the chamfer's, with a strip width of r·tan(γ/2), where
    γ is the largest face angle sampled along the edge. That is r where
    faces meet square, and exact for planes and for the planes and
    cylinders of turned parts.
  - The same OCCT builder crashes on fillets that do not fit, including
    concave ones (`docs/verification/P11-FEAT-003/`).
- **Variable-radius fillet** (`VariableFillet.hpp`, P12-FEAT-006).
  `variableFilletEdges(body, request)` rounds straight edges with radii that
  vary along them. A `VariableFilletRequest` is, per edge, a line signature
  and radius stations: a position from 0 to 1 (0 at the end that comes first
  along the line's canonical direction, so no regeneration can reverse them)
  and a radius; the first at 0, the last at 1, positions increasing.
  - **The law.** OCCT 8.0.1 (`BRepFilletAPI_MakeFillet::SetRadius` with
    (u, r) pairs) builds the clamped cubic spline through the stations and
    the end radii repeated at the ends of the spine's internal extension,
    half the edge's length for an edge that meets no other blend.
    `RadiusLaw` (private, kernel-free) computes that spline itself.
    `validate()` refuses stations whose spline leaves the range of the two
    stations around any point; `radiusAt()` exposes the law. Other kernel
    inputs are never used: its law-function input ends the process, and
    `SetLaw` yields an unfilleted body
    (`docs/verification/P12-FEAT-006/investigation/`).
  - **The edges.** Straight, between two planar faces meeting at an angle,
    continuing smoothly into no other edge (the kernel's contour has that
    edge alone) and sharing no vertex with another edge of the request. On
    such edges the kernel's law depends on the edge's own stations only.
  - **The build.** The edge is added with no radius, the contour's first
    vertex tells which way the kernel runs along it, and the stations are
    handed over in that direction. The shared fit check uses the law's
    largest radius.
  - **The result is checked, not trusted.** Beyond the shared validation:
    no self-intersection; one fillet face generated from each edge; the
    kernel's law (`GetBounds`, `GetLaw`, or `IsConstant`/`Radius` for equal
    stations) equal to BetterCAD's within 1e-9 mm; and sampled points of
    the fillet face on the rolling-ball arc of the law's radius, its boundary
    on the edge's faces at r tan(g/2), within 1e-7 mm. Any failure is
    `Internal`, and nothing is kept.
  - Setback distances and selectable corner transitions are not offered:
    the kernel has no input for either (deferred, see `TODO.md`).
- **Shared blend machinery** (`occt/OcctBlend.{hpp,cpp}`). Chamfer and
  fillet (both kinds) share:
  - edge resolution (exactly one edge between two faces, not already in
    another reference's chain);
  - the strips of each tangent chain;
  - the fit check;
  - the guarded kernel build and result validation.

  Messages name the operation, so each is reported in its own terms.
- **Faces and face references** (`Faces.hpp`). `listFaces(body)` describes
  a body's faces: surface kind, area, centroid and, for planar faces, a
  `FaceSignature`. A signature is the face's plane and outward side: the
  plane's point nearest the origin and the outward normal, without negative
  zeros. `findFaces(body, signature)` returns the faces on that plane that
  face the same way (normals within 1e-9 rad, the point within 1e-7 mm of
  the plane). Only planar faces can be referenced; curved ones are refused
  by `validate()`. As for edges, this is geometric matching, not persistent
  topological naming:
  - it survives changes that keep the face on its plane (a wider block);
  - it fails with NotFound when the plane moves (the top of a taller block);
  - several faces can lie on one plane; users of references say how they
    choose.

  `facePoint()` and `faceCoordinates()` map between model space and the
  face's (u, v) coordinates: the origin is the plane's point nearest the
  origin, and u and v are two model axes projected into the plane (X and Y
  for a plane facing ±Z, X and Z for ±Y, otherwise Y and Z). The rule does not
  depend on the side, so both faces of a plate share coordinates, and on a
  box's faces (u, v) are model coordinates. `faceFrame()` turns a signature
  into a sketch frame: that origin and u axis, the outward normal, and
  Y = normal x X (so on a face facing -Z, y is -v).
- **Face names** (P12-STREF-001, P12-SKETCH-003; `FaceName` in
  `bettercad/core/document/References.hpp`). A name is the feature that
  generated a face and the face's role (`FaceSelector`):
  - `start_cap` and `end_cap`;
  - `side`, with the profile entity that swept it and, for a sweep, the path
    edge it runs along (`along`);
  - `hole_bottom`, `counterbore_floor` and `spotface_floor`;
  - `chamfer`, with the edge reference (from 1) whose face it is.

  A face a pattern or mirror copies keeps the original's name with the copy
  step appended (`copies`: the copying feature and the instance, from 1; a
  mirror's image is instance 1). Names are persistent engineering intent;
  kernel faces, their order and their addresses never are.
  - A body carries names on its faces (`occt::BodyData::names`, each (face,
    name) once, ordered by the face's position in the shape's face map).
    `listFaces()` shows them; `findNamedFaces()` returns the faces that carry
    a name.
  - `makePrism()`, `makeRevolution()`, `makeSweep()` and `makeLoft()` with a
    `SweptFaceNamer` name what they generate (`SweptFace`): the region at
    the start and at the end, and the side each segment of each loop sweeps,
    identified through the kernel's history. The segment indices are the
    region's as given, whatever orientation the adapter builds the loops
    in (`docs/verification/P12-SKETCH-003/kernel-probe`).
    - Prisms: `FirstShape()`, `LastShape()`, `Generated(edge)`.
    - Revolutions: `FirstShape()` and `LastShape()` (a full turn has
      neither), and the swept shape of each edge (`BRepSweep_Revol::Shape()`;
      `Generated()` misses the planar faces of a full turn). A segment on
      the axis sweeps no face.
    - Sweeps: `FirstShape()` and `LastShape()` for an open path, and one
      face per path segment for each profile edge, in path order
      (`BRepOffsetAPI_MakePipeShell::Generated()`); a side is named only
      when the kernel lists exactly one face per path segment.
    - Lofts: `FirstShape()` and `LastShape()`; the ruled sides are B-splines
      and are not named.
  - `cutHole()` with a `HoleFaceNamer` names a blind hole's flat bottom and a
    counterbore's floor, the faces its cutter's bottom and step generate;
    `chamferEdges()` with a `ChamferFaceNamer` names the faces each edge
    reference's chain cuts (`BRepFilletAPI_MakeChamfer::Generated(edge)`).
  - Boolean operations carry the names of both operands through the
    kernel's history, including the merging of coplanar faces
    (`OcctFaceNames.cpp`). A face the operation deleted loses its names; a
    modified face passes them to every face it became (a split face to each
    part; merged faces all to the merged face); a kept face keeps them.
    Faces the history does not account for lose their names. Nothing is
    carried by geometric similarity
    (`docs/verification/P12-STREF-001/kernel-probe`).
  - Operations built on these booleans (holes, patterns) carry their
    inputs' names the same way; so do chamfers and fillets (from the
    blend's `Modified()` and `IsDeleted()`) and transforms (the moved copy of
    each face keeps its names). A fillet names nothing of its own.
    `renameFaces()` maps a body's names (patterns and mirrors append their
    copy step with it) and shares the shape.
- **Hole** (`Hole.hpp`). `cutHole(body, request)` drills a cylindrical hole
  into a planar face, perpendicular to it and always into the material (along
  the reversed outward normal). A `HoleRequest` is:
  - the face reference and a centre in its (u, v) coordinates;
  - a type (`Simple`, `Counterbore`, `Countersink`, `Spotface`) and an
    extent (`Through`, `Blind`);
  - the diameter, a blind hole's depth, and the head's dimensions;
  - optionally a `CosmeticThread` (P12-HOLE-001): a thread's major diameter
    and length, described and checked but not cut.

  Fields a type or extent does not use must be zero; in particular a through
  hole has no depth. Before the kernel runs:
  1. **Placement.** The face is the one on the referenced plane that contains
     the centre. No face on the plane: NotFound. Faces on the plane but none
     under the centre: FailedPrecondition. The centre on two faces (on the
     edge between coplanar faces): FailedPrecondition, ambiguous. No other
     face is substituted.
  2. **No side breakout.** The entry outline (the head's, if any) must lie
     inside the face, at least 0.001 mm from every edge of it (its outer
     boundary and any holes in it). A hole that would break out of a side is
     refused, not built.
  3. **Depth.** The material along the axis is measured from the face. A
     blind hole, or a through hole's head, must end at least 0.001 mm before
     it does. A blind hole that would reach the far side is refused ("make it
     a through hole"), never silently made through.

  The cutter is the hole's section revolved about the axis. It starts
  0.01 mm outside the face; a through cutter runs 1 mm past the body's
  bounding box, so it has no stored depth and follows the body's thickness.
  It is subtracted with `booleanDifference()`. The result must be valid,
  keep the number of solids, and have a finite positive volume below the
  input's. A blind hole must remove exactly its own volume (within 1e-9 of
  the body's volume): less means it broke into a cavity, which is refused.
  A through hole may pass through cavities along its axis ("through all").
  The preflight is for these guarantees, not for crash avoidance: a
  kernel probe of degenerate holes found no crashes
  (`docs/verification/P11-FEAT-004/`).

  A spotface is cut like a counterbore and names its floor `spotface_floor`:
  the type records which the hole is, since a seat and a recess for a head
  are different intent even where the geometry is the same. A cosmetic
  thread cuts nothing at all — the bore is the thread's core — and is
  checked against the hole it is in: wider than the bore, narrower than any
  head, longer than the head, and no longer than a blind hole or than the
  material under the face.
- **Hole standards** (`bettercad/core/standards/`, P12-HOLE-001; layer 0, no
  geometry and no kernel). Tabulated data from published standards, keyed by
  the designations engineers write:
  - `MetricThread` (ISO 68-1, ISO 262, ISO 965-2): the sizes ISO 965-2 gives
    limits of size for, parsed from and printed as "M8" or "M8x1"; the basic
    diameters follow from the profile (H = sqrt(3)/2 P, D2 = D - 3/4 H,
    D1 = D - 5/4 H).
  - `internalThreadLimits()` (ISO 965-1): the limits of size of an internal
    thread, as the basic diameters plus the fundamental deviation of the
    position (zero for H) and the tolerances TD1 and TD2 of the grade.
    BetterCAD knows the grade ISO 965-2 gives each size (5 up to M1.4, 6
    above), in position H or G, and refuses the rest.
  - `clearanceHoleDiameter()` (ISO 273): the fine, medium and coarse
    clearance holes of a bolt's nominal diameter, and the tolerance class
    the standard gives each series for information (H12, H13, H14).
  - `limitDeviations()` (ISO 286): the standard tolerances IT1 to IT18 and
    the limit deviations of the classes ISO 286-2 tabulates for sizes up to
    500 mm with the fundamental deviations D, E, F, G and H.

  A feature stores the designation, never the dimensions it stands for: a
  thread's size and class, a clearance hole's bolt and series, a bore's
  tolerance class. Regeneration resolves them, so a file keeps its
  engineering intent and a table correction reaches every model. Every value
  is transcribed twice — for BetterCAD and for the tests — and both
  transcriptions are checked against published copies of the standards
  (`docs/verification/P12-HOLE-001/standards/`).
- **Split** (`Split.hpp`, P12-FEAT-002). `splitBody(body, plane, keep)` cuts
  a body with a plane and keeps what lies in front of it (the side its
  normal points to), behind it, or both.
  - The parts are the body's intersection with, and difference from, a box
    on the plane's front side that encloses the body's bounds (1 mm beyond
    them). The booleans carry the body's face names; the faces on the plane
    get none.
  - A plane that does not cross the body fails with FailedPrecondition:
    either the bounds lie wholly on one side (within 1e-7 mm), or one side
    of a body whose bounds cross the plane is empty.
  - `gatherSolids(parts)` puts the solids of several bodies side by side in
    one body (a compound, not a union, so touching parts stay apart), with
    their names. It gives a both-sides split its two solids.
- **Shell** (`Shell.hpp`, P12-FEAT-003). `shellBody(body, request)` hollows
  one solid into walls of the request's thickness, opened where the faces
  its names (`FaceName`) are on are removed; a name several faces carry
  opens all of them. There must be at least one open face: a closed hollow
  is not built.
  - The kernel (`BRepOffsetAPI_MakeThickSolid::MakeThickSolidByJoin`) offsets
    the remaining faces inward (the outside stays) or outward (the inside
    stays). Offset faces that part at an edge are extended until they meet
    (`GeomAbs_Intersection`), so wall corners are sharp. A kernel probe
    found the rounded join (`GeomAbs_Arc`) invalid on an outward shell of a
    body with a concave edge, and the intersection join valid and exact on
    every body it tried (`docs/verification/P12-FEAT-003/kernel-probe/`).
  - The kernel's success is not trusted. For walls too thick for the body
    OCCT 8.0.1 reports success and returns the unchanged body (every
    remaining face without its offset) or an invalid solid. A shell is
    accepted only as one valid solid in which every remaining face has
    generated a face of the result, no open face is left, the volume is
    finite and positive, and (inward) smaller than the body's. Otherwise,
    and when the kernel fails or throws, the result is FailedPrecondition
    with the kernel's reason; walls that meet where the body is thin, and
    inward walls at least as thick as a round they follow, end there.
  - Names are carried through the kernel's history: remaining faces keep
    theirs, and an open face's names go to the rim the kernel makes of it
    (Modified). The walls are not named.
- **Draft** (`Draft.hpp`, P12-FEAT-004). `draftFaces(body, request)` turns
  the faces its names are on by a signed angle in (-90, 90) deg about their
  lines on a neutral plane, whose normal is the pull direction: going along
  it, a positive angle takes material away. Only planes, cylinders and
  cones are drafted; the kernel (`BRepOffsetAPI_DraftAngle`) turns every
  face tangent to a named face with it, and refuses (at `Add`) a face
  parallel to the plane or a tangent chain that reaches one.
  - The result is checked: one valid solid that does not intersect itself
    (`BRepAlgoAPI_Check`), with the input's numbers of faces, edges and
    vertices and an image (`ModifiedShape()`) of every face. A kernel probe
    found that the kernel reports success for a draft that shrinks a face
    to nothing, returning an invalid, self-intersecting solid; larger angles
    fail in the kernel. Both end as FailedPrecondition.
  - Names are carried through `ModifiedShape()`: the kernel reports the
    turned faces as generated, not modified, so the boolean history
    (`carriedNames()`) would drop them.
  - `FaceNameList.hpp` holds the face-name list check shells and drafts
    share.
- **Rib** (`Rib.hpp`, P12-FEAT-005). `addRib(body, request, namer)` joins
  a wall to the body that fills the space between an open profile (lines,
  arcs and open splines in a plane, head to tail) and the body, on one side
  of the profile, with a thickness symmetric about the plane or on one side
  of it. It uses only qualified operations:
  - the profile is extended along its end tangents to a rectangle 1 mm
    beyond the body's and the profile's extents in the plane, and closed
    along the rectangle on the filled side (the left of the direction of
    travel, or the right when flipped); `makePrism()` makes that region a
    slab, its faces marked by internal names;
  - the slab less the body (whose names are dropped for the purpose) falls
    into pieces (`solidsOf()`, `Split.hpp`); the rib is every piece with a
    face of the profile, and a piece that also has a face of the rectangle
    means the side is not closed off (FailedPrecondition);
  - the markers become the rib's own names, and the pieces are joined to the
    body with `booleanUnion()`. The result must be valid, keep the body's
    number of solids and add exactly the pieces' volume (1e-9 relative).

  A profile that stops short of the body is thereby extended to it, and one
  that reaches into the body is cut back by it. A region the kernel cannot
  make a face of (the extended profile crosses itself) fails with
  FailedPrecondition.
- **Translation, rotation and reflection** (`Transform.hpp`).
  `translated(body, translation)` and `transformed(body, motion)` return a
  moved copy with its own geometry (`BRepBuilderAPI_Transform`, copying); the
  input is never modified. `transformed()` hands the kernel BetterCAD's own
  matrix (`gp_Trsf::SetValues`), so the kernel does not recompute the
  rotation, and a pure translation takes the `translated()` path exactly.
  References move with their own `translated()` and `transformed()`
  functions:
  - an `EdgeSignature` gives the moved line or circle (a circle's axis turns
    with it);
  - a `FaceSignature` gives the moved plane, unchanged by a move within the
    plane, with its outward normal moved as a vector (so a reflected face
    faces the mirrored way);
  - a `HoleRequest` gets the moved face and centre (the centre is moved in
    3D and re-expressed in the moved face's coordinates).

  Each is the exact moved geometry, never a search for similar entities.
  **Reflections** (det A = −1) become a negative `gp_Trsf`. The kernel's
  copy then reverses every face, so faces keep pointing out of the material,
  and `transformed()` checks the result: the image must enclose a positive
  volume equal to the source's within 1e-9 relative (measured agreement is
  below 1e-15), or the call fails with Internal. A reflected planar face has
  a left-handed frame, whose surface normal (XDirection × YDirection) is the
  opposite of its main direction; the face descriptor (`OcctFaces.cpp`)
  takes that into account, so face references on mirrored bodies resolve.
  Meshes follow the surface parametrization and the face orientation, so
  mirrored bodies triangulate outward-facing (checked by the STL tests).

### Sketches (`bettercad/sketch/`, library `bettercad_sketch`)

- `Sketch` is a `DocumentObject` holding entities in the local coordinates
  of its `Frame3D` placement.
- Points are entities. Lines, circles, arcs, ellipses and splines reference
  point entities; arcs are counter-clockwise from start to end. Connected
  geometry shares points, and constraints reference whole entities.
- **Ellipses and splines** (P12-SKETCH-002). An ellipse references its
  centre and two vertex points; its first semi-axis runs to `xVertex`, its
  second, |yVertex − centre| long, is perpendicular to it (either may be the
  longer). An internal perpendicularity equation keeps the axes
  perpendicular, so a free ellipse has 5 degrees of freedom; distances and
  horizontal/vertical constraints on the vertices size and orient it. A
  spline references its poles (a `UniformBSpline`, degree 2 to 5): an open
  spline starts at its first pole and ends at its last, a periodic one is
  closed and smooth. Poles are free points (2 degrees of freedom each).
  Ellipse vertices and poles take every point constraint; concentric
  accepts ellipses; tangency with an open spline applies at a joint with a
  line, an arc or another spline (the spline's end leg parallel to the line,
  perpendicular to the arc's radius, or parallel to the other leg), and the
  joint must exist when the constraint is added. Length: ellipses by the
  arithmetic-geometric mean, splines by 8-point Gauss–Legendre on 64 pieces
  per span. Bounds: exact for ellipses, the poles' for splines.
- Entity IDs are per sketch, deterministic and never reused. A point that
  other entities reference cannot be removed.
- Validation happens when entities are created. Later point edits are free,
  because the solver moves points.
- Constraints reference whole entities and are validated when added.
  Distance, Radius and Diameter values may be driven by a length parameter,
  Angle values by an angle parameter (`isDrivable()`).
- The solver (`bettercad/sketch/Solver.hpp`, Eigen private) solves
  F(x) = 0 over free point coordinates and circle radii. It uses minimum-norm
  Gauss–Newton steps with length-scaled residuals, and diagnoses
  under/fully/over-constrained, inconsistent and failed solves. It writes
  geometry back only when the result is solved.
- **Constraints added by P12-SKETCH-001**, each with an analytic Jacobian
  and residuals in lengths:
  - *Angle* (two lines, 0 < θ < 180°): |d1| sin(φ − θ), with φ the
    counter-clockwise angle from the first line to the second. Lines have no
    direction, so φ and φ + 180° are the same configuration.
  - *Tangent*: a line and a circle or arc, as the centre's signed distance
    from the line minus the radius, on the side where the centre starts; two
    circles or arcs, as the centre distance minus r1 + r2 (external) or
    |r1 − r2| (internal), whichever the start is nearer. **At a joint** (the
    entities end at one point, or at points a coincident constraint joins)
    tangency is instead the line perpendicular to the arc's radius there, or
    the two radii parallel: the distance form is second order at a joint and
    placed the joint only to √(2·r·tolerance) (4e-5 mm for r = 8 mm).
  - *Concentric* (two circles or arcs with different centre points), the
    centres' x and y.
  - *Midpoint* (a point and a line, not one of its end points),
    p − (a + b)/2.
  - *Symmetric* (two points and a line): their midpoint on the line, and
    their segment perpendicular to it.
  - *Diameter*: the radius equation with half the value.

  Entities are stored in a canonical order: (point, line) for midpoints and
  point-line distances, (line, circle/arc) for tangents, (point, point, line)
  for symmetric. Values out of range (an angle of 0 or 180°, a diameter of 0)
  and degenerate references (a concentric pair sharing its centre point, a
  line's own end point as its midpoint) are refused when the constraint is
  added, and again when a driving parameter sets a value.
- **Curved profiles** (P12-SKETCH-002, `features::extractRegions()`, see
  Features). Circles, ellipses and periodic splines are loops on their own;
  lines, arcs and open splines are edges, joined at coincident ends (an open
  spline may end where it starts). A profile segment is one of `LineSegment2D`, `ArcSegment2D`,
  `CircleSegment2D`, `EllipseSegment2D` and `SplineSegment2D`. Areas and
  centroids are exact by Green's theorem (spline spans by the Gauss rule
  above). Loop nesting is decided exactly: an ellipse by its implicit
  equation, a spline by halving its Bézier pieces until each lies beside
  the test ray. The kernel builds ellipse edges (major axis first) and
  B-spline edges with the spline's own poles and knots. Prisms, revolutions
  and sweep profiles take every segment type; loft sections and sweep paths
  refuse ellipses and splines.
- **Undoable sketch edits** (`ModifySketchCommand`). The edit runs on a copy;
  on success the sketch's content before and after is kept, and undo/redo
  restore it with `Sketch::restoreContent()`, IDs and ID counters included.

### Reference geometry (P12-DATUM-001)

- `PlaneReference` and `AxisReference` (`bettercad/core/document/References.hpp`)
  name reference geometry by object ID only: without an object, a principal
  plane (`xy`, `yz`, `xz`, with the frames of `Frame3D::xy()`, `yz()`, `xz()`)
  or axis of the model; with a datum plane or axis, that element; with a
  coordinate system, its principal plane or axis. They are core value types,
  so sketches (layer 1) can hold them. A plane reference to a feature with a
  `FaceSelector` (P12-STREF-001, P12-SKETCH-003) names one of the faces the
  feature generates, or a copy of it, and resolves to that face's plane.
  `referencedObjects()` lists the objects a reference depends on: the
  object and the copying features.
- `DatumPlane`, `DatumAxis` and `CoordinateSystem`
  (`bettercad/features/Datums.hpp`, types `datum_plane`, `datum_axis`,
  `coordinate_system`) are document objects that store only how they are
  placed:
  - a plane is fixed, offset from a plane along its normal, or turned about an
    axis lying in a plane;
  - an axis is fixed, or the line where two planes meet, through its point
    nearest the model's origin;
  - a coordinate system is fixed, or placed from another (or the model's) by
    rotations about the base's X, Y and Z axes, in that order, then a
    translation along the base's axes.

  Offsets, angles, translations and rotations are literals or parameters.
  Fields another kind does not use must keep their defaults.
- **Resolution is a pure function of the document.** `resolvePlane()`,
  `resolveAxis()` and `resolveCoordinateSystem()` compute model-space
  geometry from the definitions, recursively, and nothing is cached in the
  objects. Errors are structured and name the objects: NotFound, a wrong kind
  (InvalidArgument), a wrong dimension (DimensionMismatch), parallel planes,
  an axis off its base plane, and nesting beyond 64 (FailedPrecondition). The
  dependency graph reports cycles first.
- **Regeneration.** The regenerator checks that each datum object resolves;
  a failure blocks what depends on it. A sketch with an `attachment` gets the
  resolved plane as its placement, set only after the sketch has solved, so a
  failure changes nothing. Its dependencies include the attached object (and
  the features copying an attached face), so a parameter that moves a datum
  or a face rebuilds the sketch and the features on it.
- **References in features.** `MirrorPlane::reference` and
  `PatternAxis::reference` replace the plane's origin and normal, or the
  axis' origin and direction, which then keep their defaults. The mirror
  offset still applies along the resolved normal. A mirror plane may not
  refer to a face: mirrors are regenerated without the other features'
  bodies.

### Face references (P12-STREF-001, P12-SKETCH-003)

- `features::FaceReferences.hpp` resolves face names. Seven kinds name their
  faces (`namesFaces()`), through `detail::sweptFaceNamer()`,
  `holeFaceNamer()`, `chamferFaceNamer()` and the rib's namer:
  - extrudes and revolves: the start cap on the sketch plane (behind it when
    symmetric; for a reversed extrude or a negative revolve the caps swap,
    so the start cap stays on the sketch plane), the end cap at the depth or
    angle, and a side per profile entity (`extractLabelledRegions()` keeps
    each segment's entity);
  - sweeps: the caps, and a side per profile entity and path edge (path
    segment i is `path.edges[i]`);
  - lofts: the caps at the first and last sections;
  - holes: the bottom (blind holes) and the counterbore floor;
  - chamfers: the face of each edge reference;
  - ribs (P12-FEAT-005): their two walls, the one at the lower offset from
    the sketch plane as the start cap and the other as the end cap, and the
    side each profile edge makes (its entity must be one of the rib's
    edges).

  Linear and circular patterns and mirrors copy faces: each instance's tool
  (an extrude's or revolve's), hole or chamfer, or a body mirror's image, is
  renamed or named with the copy step (`detail::appendCopies()`). A pattern
  of a pattern appends the whole chain, in the order the copies were made:
  the inner pattern's step first, then the outer's, so a nested copy names
  the feature that made the face, the inner pattern and its instance, and
  the outer pattern and its instance.
- `checkFaceName()` checks a name against the document alone:
  - the feature exists (NotFound) and is a feature that names its faces; a
    pattern or mirror is refused with a hint to name the face it copies, a
    fillet as naming nothing (InvalidArgument);
  - the selector is valid and the feature generates the role: a revolve's or
    extrude's side has no path edge, a sweep's side has one, a loft has no
    sides, a hole bottom needs a blind hole, a counterbore floor a
    counterbored one (InvalidArgument);
  - a side's entity is a non-construction curve of the feature's profile
    sketch, a sweep's path edge an edge of its path, and a chamfer's edge
    reference one of its references (NotFound);
  - every copy names an existing (NotFound) pattern or mirror, a mirror only
    with instance 1 (InvalidArgument).
- `resolveFacePlane()` looks for the name in the body of the feature that
  holds it (`holderOf()`): the generating feature's own body, or the body of
  the last feature in `copies`. The regenerator provides the bodies through
  a `BodyLookup`. It fails when that feature has no body
  (FailedPrecondition), when no face carries the name (NotFound: the
  feature's own operation removed the face, a full turn has no caps, a
  pattern no longer makes the instance), when a named face is not a plane
  (InvalidArgument), and when the named faces do not lie on one plane
  facing one way (FailedPrecondition). Otherwise the result is the face's
  frame (`faceFrame()`) with the normal pointing out of the material. There
  is no fallback to faces that lie where the named face used to be, and no
  other instance is taken for a missing one.
- `resolvePlane()` and `resolveAxis()` take the lookup, so sketch
  attachments and datum planes and axes may refer to faces. A reference
  depends on its feature, so a change that moves the face rebuilds what is
  placed on it. A sketch attached to a face of the feature it profiles is a
  dependency cycle.
- Resolving in the producing (or copying) feature's body means that later
  features cannot move or remove the reference: a sketch on an extrude's end
  cap stays on that plane even if a later cut removes the face.
- `CreateDatumCommand<D>` and `ModifyDatumCommand<D>`
  (`DatumCommands.hpp`) make creation and editing undoable. Validation
  reports references of the wrong kind and driving parameters of the wrong
  dimension; the CLI describes each object and each sketch's attachment.

### Features (`bettercad/features/`, library `bettercad_features`)

- Features are `DocumentObject`s that store inputs only. For example,
  `ExtrudeFeature` holds an `ExtrudeDefinition` (profile sketch, depth or
  driving depth parameter, direction, operation, target, termination).
- **Solid features** (`Feature.hpp`) derive from `SolidFeature`, which
  exposes the target feature whose body the feature consumes, if any, and
  `consumedFeatures()`: the target, or more for kinds that consume several
  bodies (a combine).
  Extrude and Revolve also have a `FeatureOperation` (new body, join, cut,
  intersect). Shared infrastructure serves every kind:
  - `validateOperation()` checks the operation/target pairing;
  - `combineWithTarget()` performs the body combination, so no feature
    encodes booleans itself. Kernel failures come back prefixed with the
    feature's name;
  - the private `SolidSupport` helpers resolve the profile sketch and its
    regions and unite one tool solid per region;
  - `CreateFeatureCommand<F>` / `ModifyFeatureCommand<F>` provide undoable
    creation and editing for any kind with a `Definition`.

  Result bodies, validation and the CLI use `SolidFeature`, so a new kind
  plugs in without touching them.
- **Extrude** (`ExtrudeFeature.hpp`) and **Revolve** (`RevolveFeature.hpp`)
  are the solid features so far.
- **Split and combine** (P12-FEAT-002, `SplitFeature.hpp`,
  `CombineFeature.hpp`, types `split` and `combine`).
  - A `SplitDefinition` holds the target feature, the plane as a
    `PlaneReference` (a principal plane, a datum plane, a coordinate system's
    plane or a named face) and what to keep (`geometry::SplitKeep`). Both
    parts make one body of two solids.
  - A `CombineDefinition` holds the target feature, one or more tool
    features (in order, none repeated, none the target) and an operation
    (join, cut or intersect). The combine consumes the target and every
    tool, so none of them remains a result body.
  - `regenerateSplit()` resolves the plane with the regenerator's bodies,
    then splits. `regenerateCombine()` applies the operation with each tool
    body in turn, and fails with FailedPrecondition when a tool has no body
    or a step leaves nothing ("nothing is left after cutting Big
    (object:15)"). Both carry the input bodies' face names; neither names
    faces of its own.
  - Their handlers (`regenerateBodyFeature`) read other features' bodies
    through a `BodyLookup`. Splits and combines depend on their inputs
    (and a split on the objects its plane refers to). They cannot be a
    pattern's or feature mirror's source.
- **Shell** (P12-FEAT-003, `ShellFeature.hpp`, type `shell`). A
  `ShellDefinition` holds the target feature, the open faces as face names,
  the thickness (literal, or a length parameter) and the side
  (`geometry::ShellSide`: inward or outward). The shell consumes its
  target.
  - `regenerateShell()` checks each open face's name (`checkFaceName()`),
    requires that a face of the target's body carries it (NotFound
    otherwise: "open face 1, the end cap of Boss (object:4), is not a face of
    the body of Block (object:2)"), and calls `shellBody()`. The name is
    looked up in the target's body, where earlier features have carried it,
    not in the generating feature's own body: a face a later feature
    removed cannot be opened.
  - A shell depends on its target, its thickness parameter and the features
    its open faces name (generators and copying features). It names no
    faces of its own, and cannot be a pattern's or feature mirror's source.
- **Rib** (P12-FEAT-005, `RibFeature.hpp`, type `rib`). A `RibDefinition`
  holds the target feature, the profile sketch, its ordered edges, the
  thickness (literal, or a length parameter), the placement (symmetric,
  along or against the sketch's normal) and whether the rib fills the right
  of the profile instead of its left. The rib consumes its target.
  - `resolveRibProfile()` takes the sketch's lines, arcs and open splines in
    the given order, head to tail (the first edge turned to meet the second,
    each later edge starting exactly where the one before ends), in the
    sketch's placement. Construction geometry, points, circles, ellipses,
    periodic splines, degenerate and disconnected edges are refused.
  - `regenerateRib()` calls `addRib()` with names for the feature: segment
    i's side is the side of `edges[i]`. A rib depends on its target, its
    sketch and its thickness parameter; it cannot be a pattern's or feature
    mirror's source.
- **Draft** (P12-FEAT-004, `DraftFeature.hpp`, type `draft`). A
  `DraftDefinition` holds the target feature, the faces as face names, the
  neutral plane as a `PlaneReference` (a named face included, facing out of
  the material) and the angle (literal, or an angle parameter). The draft
  consumes its target.
  - `regenerateDraft()` resolves the plane with the regenerator's bodies
    (`regenerateBodyFeature`), checks the faces like a shell's open faces
    (`detail::requireNamedFaces()`: `checkFaceName()`, then a face of the
    target's body must carry each name), and calls `draftFaces()`.
  - A draft depends on its target, its angle parameter, the plane's objects
    and the features its faces name. It names no faces of its own, and
    cannot be a pattern's or feature mirror's source.
- **Through-all extrude** (P12-FEAT-001). An extrude's `termination` is
  `Blind` (at its depth) or `ThroughAll`. A through-all extrude stores no
  depth: it is a cut (the only operation it takes) through all of its
  target, however thick that becomes.
  - `extrudeTool()` takes the target's body and works out the tool's extent
    at regeneration. It projects the corners of the body's exact bounding box
    on the sketch normal and reaches 1 mm beyond them: from the sketch plane
    along the normal, against it (reversed), or both ways (symmetric).
  - A target that lies wholly on the side the cut does not go to fails with
    FailedPrecondition; so does a missing target body.
  - Patterns and mirrors rebuild the tool at each instance
    (`throughAllOperation()`): the box is projected on the moved sketch
    normal, so a copy turned onto a thicker wall still cuts through it.
- A `RevolveDefinition` holds:
  - the profile sketch;
  - a `RevolveAxis`: the sketch's X or Y axis, or a line entity of the
    profile sketch, so the axis follows the sketch;
  - an angle in (0, 360°], literal or driven by an angle parameter;
  - a direction (positive, negative or symmetric by the right-hand rule);
  - the operation and target.
- **Sweep** (`SweepFeature.hpp`) moves a sketch's closed profiles along a
  path. A `SweepDefinition` holds:
  - the profile sketch;
  - the path (`SweepPath`): another sketch and an ordered list of its line,
    arc or circle entities, so the path follows that sketch's constraints
    and parameters;
  - the orientation, `FollowPath`, the only mode (see `makeSweep()` under
    Geometry);
  - the operation and target, as for extrudes and revolves.

  `resolveSweepPath()` turns the entities into a `PlanarPath` in the path
  sketch's plane. Each edge is oriented to start where the one before ends,
  and the direction of travel is fixed by the edges: a single line or arc
  runs from its start to its end, a circle counter-clockwise from its X
  axis, and several edges from the first edge's end that does not meet the
  second. Missing edges fail with NotFound. Points, circles joined with other
  edges, zero-length edges and edges that do not meet fail with
  InvalidArgument, naming the entities; nothing is substituted or repaired.
  The sweep depends on both sketches and its target, so a change to either
  sketch's parameters rebuilds it.
- **Loft** (`LoftFeature.hpp`) passes through the closed profiles of two or
  more sketches. A `LoftDefinition` holds:
  - the sections (`LoftSection`), in the loft's order: each a sketch and an
    offset along the sketch plane's normal, literal or driven by a length
    parameter (so the sections' spacing can be a parameter);
  - the interpolation, `Ruled`, the only mode (see `makeLoft()` under
    Geometry);
  - the operation and target, as for extrudes and revolves.

  `resolveLoftSections()` takes each section sketch's one closed profile
  (FailedPrecondition for none, several, or one with a hole) and moves its
  plane by the offset. Errors name the section ("section 2 (sketch 'Top'):
  ..."). The loft depends on every section's sketch, their offset
  parameters and its target. A definition needs two sections or more, and
  no section may repeat another (the same sketch at the same offset).
- **Chamfer** (`ChamferFeature.hpp`) modifies another feature's body. A
  `ChamferDefinition` holds:
  - the target feature, which the chamfer consumes;
  - edge references (`geometry::EdgeSignature`, with the limits described
    under Geometry);
  - the mode, with a distance that is literal or driven by a length
    parameter, plus the second distance or the angle, and the reference
    side.

  Values a mode does not use must be zero, so none is silently ignored.
  Regeneration resolves the references on the target's current body. A
  reference that matches no edge, or several, fails the chamfer with a
  message naming the reference. The chamfer then keeps no body, and the
  target's body is untouched.
- **Fillet** (`FilletFeature.hpp`) works the same way on the same edge
  references. A `FilletDefinition` holds:
  - the target feature;
  - the edges;
  - a constant radius, literal or driven by a length parameter.

  The private `SolidSupport` helper `applyToTargetBody()` gives chamfer,
  fillet and hole the same target-body handling and message prefix.
- **Variable-radius fillet** (`VariableFilletFeature.hpp`, P12-FEAT-006, type
  `variable_fillet`). A `VariableFilletDefinition` holds the target feature
  and, per edge, a line signature and its stations, each radius literal or
  driven by a length parameter. It consumes its target, carries its names
  and names nothing of its own. It depends on its target and on each radius
  parameter. `validate()` checks the law when every radius is a literal;
  otherwise regeneration checks it once the values are known
  (`resolveVariableFilletRequest()`). Patterns and mirrors refuse it (a
  copy's edge may run the other way along its line).
- **Hole** (`HoleFeature.hpp`) drills into another feature's body, which it
  consumes. A `HoleDefinition` holds:
  - the target feature and the placement face (`geometry::FaceSignature`);
  - the centre in face coordinates, each coordinate literal or driven by a
    length parameter;
  - the type and extent, the diameter and a blind hole's depth, each
    literal or driven by a length parameter, and the head's dimensions.

  `validate()` applies the geometry request's rules to literal values;
  relations that involve a driven value (a counterbore wider than a driven
  diameter) are checked at regeneration. Regeneration resolves the face on
  the target's current body, so a hole placed on the bottom face of an
  extrude (its start plane) follows any thickness, and a through hole stays
  through. A hole on a face that moves (the top of a thicker extrude) fails
  with NotFound and keeps no body.
- **Linear pattern** (`LinearPatternFeature.hpp`) repeats another feature's
  operation along one direction, or two for a grid. A
  `LinearPatternDefinition` holds:
  - the source feature, which the pattern consumes (its `target()`);
  - for each direction, a vector as given (any finite, non-zero vector,
    normalized when used), a count and a length. The count may be driven by
    a dimensionless parameter holding a whole number, the length by a
    length parameter;
  - for each direction, what that length means (`PatternDistribution`):
    `Spacing`, the step between neighbours, or `TotalLength`, the whole
    row's span from the first instance to the last, where the step is
    L/(N − 1) and the count must be at least 2;
  - for each direction, whether it is `symmetric`: the copies sit on both
    sides of the source, which is then the middle instance, so the count
    must be odd;
  - the instances that are `suppressed`: indices from 1, each below the
    count, listed once in increasing order.

  The count includes the source, so 1 is the source alone. Instance (i, j)
  is the source moved by m(i)·s1·d1 + m(j)·s2·d2, where s is each
  direction's step and m is `patternStepMultiple()`: 0, 1, 2, … along a
  plain direction and 0, +1, −1, +2, −2, … about a symmetric one, so that
  raising the count leaves what an existing index means unchanged. Every
  offset is that multiple times the step, computed from the source, never
  by adding to the previous instance. Instances are numbered i + j·count1,
  with 0 the source. They are not document objects: an instance is
  identified by its pattern and index, and `patternInstances()` lists them,
  each marked with whether the definition suppresses it. There are at most
  `kMaxPatternInstances` (500), a guard against runaway input: building
  time grows with the square of the count.

  A suppressed instance makes no geometry and keeps its index: suppressing
  one never renumbers another, a name that refers to it resolves to nothing
  (`NotFound`, never the next surviving instance), and unsuppressing it
  brings back the same index and the same geometry. Suppressing every copy
  is refused, as is suppressing instance 0, which is the source itself.

  Regeneration (`regenerateLinearPattern()`) starts from the source's body
  (instance 0) and applies the source's own operation at every other
  instance, in order:
  - **Extrude and revolve.** The tool (`extrudeTool()`, `revolveTool()`) is
    moved and united (new body, join) or subtracted (cut). New-body
    instances that touch or overlap fuse, like the regions of one extrude.
    Intersect sources are refused. A through-all cut's tool is rebuilt for
    each instance, against the body at that instance.
  - **Hole, chamfer and fillet.** The feature's references are moved exactly
    and applied with all of the feature's own checks. A moved hole must fit
    its face (so overlapping holes are refused); a moved edge must match
    exactly one edge.

  - **A pattern.** Every instance of the source pattern is made again,
    moved by the outer instance's motion composed with its own
    (`RigidTransform3D::after()`), and the faces it makes carry the inner
    pattern's copy step and then the outer's. The source's own suppressed
    instances stay suppressed wherever the outer pattern puts them, and the
    effective count — the product down the whole chain — must stay within
    `kMaxPatternInstances`. Patterns nested more than
    `kMaxPatternNesting` (8) deep are refused as a cycle, as is a pattern
    that names itself; a genuine cycle is caught by the dependency graph
    before regeneration.

  The first failing instance fails the whole pattern, with its index and
  offset in the message ("instance 5 at (100, 0, 0) mm: hole: …"), and a
  nested failure names both ("instance 1 at 180 deg: Holes instance 2:
  hole: …"). The pattern then keeps no body; partial patterns are never
  produced.
- **Circular pattern** (`CircularPatternFeature.hpp`) repeats another
  feature's operation around an axis. A `CircularPatternDefinition` holds:
  - the source feature, which the pattern consumes (its `target()`);
  - the axis: an origin and a direction as given (any finite, non-zero
    vector, normalized when used);
  - a count, which includes the source and may be driven by a
    dimensionless parameter holding a whole number;
  - whether the pattern is `symmetric`, turning its copies both ways about
    the source, which is then its middle instance (an odd count; refused
    for a full circle, whose instances already go all the way round), and
    which of its instances are `suppressed`, exactly as for a linear
    pattern;
  - the spacing: `FullCircle` (360°/count apart, no angle), `IncludedAngle`
    (the source to the last instance, angle/(count − 1) apart) or
    `AngleStep` (angle apart), the angle literal or driven by an angle
    parameter;
  - the direction: `Positive` (right-handed about the axis) or `Negative`.

  Instance i is the source turned by m(i)·step, with m as for a linear
  direction, computed from the source for every instance
  (`circularPatternInstances()`), never by adding to the previous one. A full circle never has an instance at 360°, which would be
  the source again; for the same reason an included angle or the span
  (count − 1)·step of an angle step must stay below 360° (to within
  1e-9 rad). Angles are not normalized: 400° is refused, not taken as 40°.
  The count shares the linear pattern's limit (`kMaxPatternInstances`).

  Regeneration (`regenerateCircularPattern()`) works as for linear
  patterns, with a rotation in place of the offset, and its first failure
  names the instance and its angle ("instance 3 at 90 deg: hole: …"). A
  source that the axis passes through turns onto itself: new-body instances
  coincide and fuse into the source, and a hole is refused by the hole's own
  placement check (the second instance would be drilled into the first).
  A circular pattern may repeat a pattern, and a linear pattern a circular
  one, on the same terms as any other nesting.
- **Mirror** (`MirrorFeature.hpp`) reflects another feature across a plane.
  A `MirrorDefinition` holds:
  - the source feature, which the mirror consumes (its `target()`);
  - the plane (`MirrorPlane`): an origin, a normal as given (any finite,
    non-zero vector, normalized when used; its sense does not matter) and an
    offset along the unit normal, literal or driven by a length parameter.
    The plane passes through origin + offset·n̂;
  - the scope: `Feature` applies the source's own operation once more,
    reflected, to the body the source made (a pattern instance); `Body`
    reflects the source's whole body, with everything that built it;
  - keep-original: whether the result keeps the source next to its image. A
    feature mirror always does; a body mirror may leave it out, giving the
    image alone.

  Every point goes to p' = p − 2((p − p0)·n̂)n̂ (`resolveMirrorReflection()`
  resolves the plane, `RigidTransform3D::reflection()` builds the motion).
  The result is instance 0 (the source's body) and instance 1 (its image),
  in that order, built by the pattern support's atomic loop; the image is not
  a document object. A failure fails the whole mirror, naming the image and
  its plane ("the mirror image across the plane through (64, 0, 0) mm facing
  (1, 0, 0): hole: …"), and the mirror keeps no body. A feature mirror takes
  the same sources as a pattern (new-body, join and cut extrudes and
  revolves, holes, chamfers and fillets, whose references are reflected
  exactly); a body mirror takes any feature with a body, patterns and
  mirrors included. Feature mirrors of patterns or mirrors, and patterns of
  mirrors, are refused. Where the image coincides with the source, extrudes
  and revolves unite or cut the same tool again (the body is unchanged),
  while a hole the plane maps onto itself, or a chamfer or fillet edge it
  maps onto one of the feature's own edges, is refused with its own
  diagnostic instead of being applied twice.
- **Pattern support** (`src/features/pattern/PatternSupport.hpp`, private).
  Both patterns and the mirror share one subsystem: the per-instance
  operation of each source kind (`instanceOperation()`), the build loop that
  applies it instance by instance with atomic failure (`buildPattern()`),
  the count resolution and the circular angle rules. Each pattern only
  computes its placements (`RigidTransform3D`s) and their labels; the mirror
  has one placement, the reflection.
- Bodies are derived: regeneration computes them from the current document.
  Regenerating does not change the document's revision or dirty state.
- `extractRegions()` turns a sketch into `geometry::PlanarRegion`s. It
  connects edges by position, rejects open ends and branches, and classifies
  holes by nesting. `geometry::makePrism()` and `makeRevolution()` sweep
  regions; the kernel sees only these value types.

### Dependencies and regeneration

- Objects declare their inputs through `DocumentObject::dependencies()`, and
  `buildDependencyGraph()` turns a document into a `DependencyGraph`
  (`bettercad/core/document/DependencyGraph.hpp`). Parameter expressions add
  edges from the parameters they name (see Parameter expressions).
- `features::Regenerator` rebuilds only what changed: items whose revision
  differs from the one recorded at their last build, plus everything
  downstream of them. It works in deterministic dependency order.
- Failures are reported per item. Dependents of failures, missing
  references and cycles are blocked and keep no stale results.
- New object kinds plug in a regeneration handler by type name.
- **Result bodies** (`ResultBodies.hpp`). A feature's body is a model result
  unless another feature consumes it (`SolidFeature::consumedFeatures()`):
  the target of a Join/Cut/Intersect, the body a chamfer, shell, draft or rib
  modifies, a pattern's source, or a split's or combine's inputs.
  `regenerateResultBodies()` regenerates a copy of the document and returns
  those bodies. Exports use it.
- **Validation** (`Validation.hpp`). `validateDocument()` runs six checks on
  a copy of the document:
  1. document consistency (references point at the right kind of item and
     dimension, e.g. revolve axes are lines, revolve angles are angles, and
     chamfer distances, fillet radii and hole dimensions and centres are
     lengths; pattern counts are dimensionless, spacings lengths, circular
     pattern angles angles and mirror plane offsets lengths; sweep paths
     are in sketches, and their edges are not points; loft sections are
     sketches, and their offsets lengths);
  2. missing references (including revolve axis lines and sweep path edges
     in their sketches);
  3. dependency cycles;
  4. sketch constraints (each sketch solves with its driving parameters;
     not fully constrained is a warning);
  5. feature regeneration;
  6. geometry (non-empty, valid solids with positive volume).

  Each problem is reported once, under the first check that finds it. The
  report also lists the result bodies with their mass properties and bounds.

### Serialization (`bettercad_io`)

- Core types have no file-format code. `bettercad_io` maps them to JSON with
  strict, path-aware validation. Unknown fields are rejected, and every
  format carries a `format` tag and a `version`.
- **Native document format** (`.bcad`, `bettercad/io/DocumentFile.hpp`). This
  is transparent, pretty-printed JSON (`"format": "bettercad-document"`,
  version 1, `"units": "SI"`). It holds:
  - the document UUID, name and metadata;
  - the ID counter (`last_allocated_id`);
  - the parameters;
  - the objects, as `{id, type, name, data}` in ascending ID order.

  Sketch data holds the placement frame, an optional `attachment`
  (`{"object": id, "plane": "xy"}`, or for a feature's face
  `{"object": id, "face": {...}}` with `"role"` (`start_cap`, `end_cap`,
  `side`, `hole_bottom`, `counterbore_floor`, `chamfer`) and, where the role
  takes them, `"entity"` and `"along"` (IDs) and `"edge"` (from 1), and for a
  copy `"copies": [{"feature": id, "instance": n}, ...]` in the order the
  copies were made), the entities and constraints with
  their own IDs, and the sketch's ID counters. An ellipse stores `center`,
  `x_vertex` and `y_vertex`; a spline, whose type name in files is
  `"bspline"` (the curve it is; `"spline"` stays unknown), stores `poles`,
  `degree` and `periodic`. A constraint stores `value`
  (metres) for distances, radii and diameters and `angle` (radians) for
  angles, each only when its type has one. Extrude, revolve and chamfer
  data hold their definitions:
  - a split stores `target`, `plane` (a plane reference, as for sketch
    attachments) and `keep` (`"front"`, `"back"`, `"both"`); a combine
    stores `target`, `tools` (feature IDs in order) and `operation`
    (`"join"`, `"cut"`, `"intersect"`);
  - a rib stores `target`, `profile`, `edges` (entity IDs in order),
    `thickness` and an optional `thickness_parameter`, `placement`
    (`"symmetric"`, `"along_normal"`, `"against_normal"`) and `flipped`;
  - a draft stores `target`, `faces` (face names), `neutral_plane` (a plane
    reference), `angle` (radians) and an optional `angle_parameter`;
  - a shell stores `target`, `open_faces` (face names, each
    `{"feature": id, "face": {...}}` with the face as in a plane reference),
    `thickness` and an optional `thickness_parameter`, and `side`
    (`"inward"`, `"outward"`);
  - an extrude stores `depth` and an optional `depth_parameter`, or, for a
    through-all extrude, `"termination": "through_all"` and no depth (a
    missing `termination` means `"blind"`);
  - a revolve axis is stored as
    `{"type": "sketch_x" | "sketch_y" | "line", "line": id}` and its angle
    in radians;
  - a chamfer edge reference is stored as
    `{"curve": "line", "point": [...], "direction": [...]}` or
    `{"curve": "circle", "center": [...], "axis": [...], "radius": r}`;
  - a chamfer mode is `"equal_distance"`, `"two_distance"` or
    `"distance_angle"`, and a chamfer stores `distance2`, `angle` and
    `reference_side` only when its mode uses them;
  - a fillet stores `target`, `edges` (as for chamfers), `radius` in
    metres and an optional `radius_parameter`;
  - a variable-radius fillet stores `target` and `edges`, each
    `{"edge": ..., "stations": [...]}` with the edge reference as for
    chamfers and each station `{"position": u, "radius": metres}` with an
    optional `radius_parameter`;
  - a hole stores `target`; `face` as
    `{"surface": "plane", "point": [...], "normal": [...]}`; `center` as
    face `[u, v]` with optional `center_u_parameter` and
    `center_v_parameter`; `type` (`"simple"`, `"counterbore"`,
    `"countersink"`), `extent` (`"through"`, `"blind"`), `diameter` and an
    optional `diameter_parameter`. Only a blind hole stores `depth` and
    `depth_parameter`, only a counterbore `counterbore_diameter` and
    `counterbore_depth`, and only a countersink `countersink_diameter` and
    `countersink_angle` (radians);
  - a linear pattern stores `source` and `first` (plus `second` for a grid).
    Each direction is `{"direction": [x, y, z], "count": n, "spacing":
    metres}`, with optional `count_parameter` and `spacing_parameter`. The
    vector is stored as given, not normalized;
  - a circular pattern stores `source`; `axis` as `{"origin": [...],
    "direction": [x, y, z]}` (the direction as given); `count` with an
    optional `count_parameter`; `spacing` (`"full_circle"`,
    `"included_angle"`, `"angle_step"`); and `rotation` (`"positive"`,
    `"negative"`). Only the angle spacings store `angle` (radians) and an
    optional `angle_parameter`. An axis given by a reference is stored as
    `{"reference": {"object": id, "axis": "z"}}` instead;
  - a mirror stores `source`; `plane` as `{"origin": [...], "normal":
    [x, y, z], "offset": metres}` (the normal as given) with an optional
    `offset_parameter`; `scope` (`"feature"`, `"body"`); and
    `keep_original`. A plane given by a reference stores `reference`
    (`{"object": id, "plane": "xy"}`) in place of `origin` and `normal`;
  - a sweep stores `profile`; `path` as `{"sketch": id, "edges": [ids]}`
    (in the order of travel); `orientation` (`"follow_path"`); `operation`;
    and an optional `target`;
  - a loft stores `sections` in the loft's order, each as `{"sketch": id,
    "offset": metres}` with an optional `offset_parameter`;
    `interpolation` (`"ruled"`); `operation`; and an optional `target`;
  - a datum plane stores `kind`: `"fixed"` with `frame`; `"offset"` with
    `base` (a plane reference), `offset` (metres) and an optional
    `offset_parameter`; `"angled"` with `base`, `axis` (an axis reference),
    `angle` (radians) and an optional `angle_parameter`;
  - a datum axis stores `kind`: `"fixed"` with `axis` (`{"origin": [...],
    "direction": [...]}`), or `"intersection"` with `first` and `second`;
  - a coordinate system stores `kind`: `"fixed"` with `frame`, or
    `"offset"` with an optional `base` (an ID), `x`, `y`, `z` (metres), `rx`,
    `ry`, `rz` (radians) and optional `<key>_parameter`s.

  Files without these objects and fields are written exactly as before.

  The rules are:
  - **Only inputs are stored.** Geometry is derived and is regenerated after
    loading; the solved sketch state is saved, so regenerating a loaded file
    reproduces the saved geometry bit for bit.
  - **Values are exact.** Values are SI and written with round-trip-exact
    decimals. Frames are restored component for component
    (`Frame3D::fromAxes`).
  - **References are IDs.** A reference to a missing item (e.g. a deleted
    profile) is a valid document state. It loads and is reported by
    regeneration. Inside a sketch, references must resolve.
  - **Every ID is kept.** Counters are persisted, so IDs are never reused,
    and must not be below an ID in use. File IDs are capped at 2^53 − 1.
  - **Output is deterministic.** Saving the same document gives the same
    bytes.
  - **Saving is atomic.** `saveDocument` writes a sibling temporary file and
    renames it over the target.
- New object kinds need a serializer in `src/io/json/`; saving a document
  that contains an unsupported kind fails with `InvalidArgument` rather than
  dropping data.
- **Versioning policy.** Adding an object type is an additive change within
  format version 1: files without the new type are byte-identical to before,
  and an older reader rejects the new type with "unknown object type". The
  version changes only when the meaning of existing data changes.
- **Model export** (`ModelExport.hpp`). `exportStep` and `exportStl` write a
  document's result bodies. STL is written by BetterCAD from
  `geometry::Mesh` (`Stl.hpp`, binary or ASCII, millimetres, one solid).
  Every file bettercad_io writes goes through one atomic-replace path
  (`src/io/FileIo.cpp`), with UTF-8 paths on every platform.

### Command-line tool (`apps/bettercad_cli`)

- Commands: `new`, `info`, `validate`, `export-step`, `export-stl`,
  `version`, `help`.
- The commands live in `bettercad_cli_lib`, so tests drive them in process;
  process-level tests also run the real executable.
- Exit status is 0 for success, 1 for failure or an invalid document, and 2
  for an invalid command line.
- On Windows the entry point is `wmain`, which converts the UTF-16 command
  line to UTF-8, so non-ASCII file names work.
- Options accept units, e.g. `--tolerance 0.05mm`.

### Mechanical reference models (`examples/reference_models`)

Five production parts and a swept U-bolt, built only through the public
document, parameter, sketch and feature APIs — the same route a user, a
script or the GUI takes. Nothing there creates geometry directly: every
body comes from regenerating a document, and the architecture check (which
covers `examples/` as well) keeps Open CASCADE out of them.

- **The parts.** A stepped shaft (revolve, hole, mirror, fillet, chamfer),
  a bolted flange (extrude, holes, circular pattern, chamfer, fillet), a
  V-belt pulley (revolve, revolved cut, hole, fillets, chamfers), a pillow
  block (extrudes joined, body mirror, extruded cut, hole, linear pattern,
  fillet, chamfer), an L bracket (extrudes joined, holes, linear pattern,
  mirror, fillet, chamfer, lofted gusset) and the U-bolt (sweep, chamfer).
  Together they use every P11 feature.
- **Builders.** `buildShaftReferenceModel()` and its siblings return a
  document that has not been regenerated, plus the IDs of the parameters and
  objects tests change. They are deterministic: the same builder always
  gives the same items with the same IDs, under a fixed document ID, so a
  saved model is reproducible byte for byte. `examples/models/reference/`
  holds those files, and a test keeps them in step with the builders.
- **Fingerprints.** `fingerprint(document, regenerator)` collects what
  identifies a regenerated model: its items with their IDs, kinds and names,
  how many features it has, and each result body's validity, topology,
  volume, area, centroid and bounds. Tests compare fingerprints exactly for
  a rebuild, a save and a load, and within a tolerance across a change and
  back.
- **Parameters.** The P11 models were built before parameter expressions
  were evaluated (P12-PARAM-001), so every dimension a feature follows is
  one parameter used directly. Where a model needs a derived dimension, a
  sketch builds the relation geometrically: the shaft is dimensioned by half
  its length, so the same parameter sets the overall length and the mirror
  plane for the tail centre hole.
- **The example program** `bettercad_example_reference_models` builds every
  model, prints its fingerprint and its build, regeneration, save and load
  times, and with `--out <dir>` writes the models and their STEP and STL
  exports.

### Test tooling

- `tests/support/` holds shared fixtures:
  - the P9 bracket model, the turned part (revolves), and the parametric
    block (`BlockModel`) with its chamfered, filleted, drilled
    (`HoleModels.hpp`), patterned (`PatternModels.hpp`) and mirrored
    (`MirrorModels.hpp`) variants, the swept models (`SweepModels.hpp`) and
    the lofted models (`LoftModels.hpp`);
  - temporary directories;
  - mesh and STL analysis written from first principles (divergence-theorem
    volume, edge-manifold watertightness).
- `tests/support/occt/` reads exported STEP files back with the kernel
  (solids, validity, volume, area and bounds). It is test-only tooling,
  since STEP import is a later milestone, and follows the same rule as
  product code: only an `occt/` directory includes OCCT headers.

## Dependencies

| Dependency              | Version         | Used by                        | Obtained via                            |
|-------------------------|-----------------|--------------------------------|-----------------------------------------|
| Qt                      | 6.11.2 (qtbase) | desktop app, renderer          | `deps/` superbuild or system package    |
| Open CASCADE Technology | 8.0.1           | `bettercad_geometry` (private) | `deps/` superbuild or system package    |
| Catch2                  | 3.16.0          | tests                          | FetchContent (pinned archive + SHA-256) |
| nlohmann/json           | 3.12.0          | `bettercad_io` (private)       | FetchContent (pinned archive + SHA-256) |
| Eigen                   | 5.0.1           | `bettercad_sketch` (private)   | FetchContent (headers, SHA-256)         |

pybind11 (scripting) will be added when that milestone starts.

Binary dependencies must be built with the same toolchain and C runtime as
BetterCAD. The official Qt MinGW packages link `msvcrt.dll`, so they cannot be
mixed with a UCRT-based MinGW toolchain; the superbuild exists for that reason.
