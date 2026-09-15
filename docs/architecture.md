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
                 features           extrude, revolve, chamfer, fillet, hole, linear and circular patterns, mirror, sweep (later: ...)
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

| Target                | Alias                 | Kind        | Contents                                        |
|-----------------------|-----------------------|-------------|-------------------------------------------------|
| `bettercad_core`      | `BetterCAD::core`     | library     | Build info, units, IDs, parameters, document    |
| `bettercad_geometry`  | `BetterCAD::geometry` | library     | Solid geometry, meshing, STEP over OCCT         |
| `bettercad_sketch`    | `BetterCAD::sketch`   | library     | Sketches, constraints, solver (layer 1)         |
| `bettercad_features`  | `BetterCAD::features` | library     | Features, regeneration, validation (layer 2)    |
| `bettercad_io`        | `BetterCAD::io`       | library     | Native document files, STEP/STL export          |
| `bettercad_cli_lib`   | `BetterCAD::cli_lib`  | static lib  | CLI commands (new, info, validate, export)      |
| `bettercad_cli`       | —                     | executable  | `bettercad-cli`                                 |
| `bettercad`           | —                     | executable  | Qt desktop application (placeholder)            |
| `bettercad_tests`     | —                     | executable  | Catch2 unit tests                               |
| `bettercad_test_occt` | —                     | static lib  | Test-only STEP read-back (OCCT)                 |

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
  report the kernel's error estimate.
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
    The check is exact: it uses segment ends and, for arcs and circles,
    their extreme points, so an arc bulging across the axis is caught even
    when its ends are not.

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
- **Shared blend machinery** (`occt/OcctBlend.{hpp,cpp}`). Chamfer and
  fillet share:
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
  box's faces (u, v) are model coordinates.
- **Hole** (`Hole.hpp`). `cutHole(body, request)` drills a cylindrical hole
  into a planar face, perpendicular to it and always into the material (along
  the reversed outward normal). A `HoleRequest` is:
  - the face reference and a centre in its (u, v) coordinates;
  - a type (`Simple`, `Counterbore`, `Countersink`) and an extent
    (`Through`, `Blind`);
  - the diameter, a blind hole's depth, and the head's dimensions.

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
- Points are entities. Lines, circles and arcs reference point entities;
  arcs are counter-clockwise from start to end. Connected geometry shares
  points, and constraints reference whole entities.
- Entity IDs are per sketch, deterministic and never reused. A point that
  other entities reference cannot be removed.
- Validation happens when entities are created. Later point edits are free,
  because the solver moves points.
- Constraints reference whole entities and are validated when added.
  Distance and Radius values may be driven by a document parameter.
- The solver (`bettercad/sketch/Solver.hpp`, Eigen private) solves
  F(x) = 0 over free point coordinates and circle radii. It uses minimum-norm
  Gauss–Newton steps with length-scaled residuals, and diagnoses
  under/fully/over-constrained, inconsistent and failed solves. It writes
  geometry back only when the result is solved.

### Features (`bettercad/features/`, library `bettercad_features`)

- Features are `DocumentObject`s that store inputs only. For example,
  `ExtrudeFeature` holds an `ExtrudeDefinition` (profile sketch, depth or
  driving depth parameter, direction, operation, target).
- **Solid features** (`Feature.hpp`) derive from `SolidFeature`, which
  exposes the target feature whose body the feature consumes, if any.
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
  are the solid features so far. A `RevolveDefinition` holds:
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
    normalized when used), a count and a spacing. The count may be driven by
    a dimensionless parameter holding a whole number, the spacing by a
    length parameter.

  The count includes the source, so 1 is the source alone. Instance (i, j)
  is the source moved by i·s1·d1 + j·s2·d2, computed from the source for
  every instance, never by adding to the previous one. Instances are
  numbered i + j·count1, with 0 the source. They are not document objects:
  an instance is identified by its pattern and index, and
  `patternInstances()` lists them. There are at most `kMaxPatternInstances`
  (500), a guard against runaway input: building time grows with the square
  of the count.

  Regeneration (`regenerateLinearPattern()`) starts from the source's body
  (instance 0) and applies the source's own operation at every other
  instance, in order:
  - **Extrude and revolve.** The tool (`extrudeTool()`, `revolveTool()`) is
    moved and united (new body, join) or subtracted (cut). New-body
    instances that touch or overlap fuse, like the regions of one extrude.
    Intersect sources are refused.
  - **Hole, chamfer and fillet.** The feature's references are moved exactly
    and applied with all of the feature's own checks. A moved hole must fit
    its face (so overlapping holes are refused); a moved edge must match
    exactly one edge.

  The first failing instance fails the whole pattern, with its index and
  offset in the message ("instance 5 at (100, 0, 0) mm: hole: …"). The
  pattern then keeps no body; partial patterns are never produced. Patterns
  of patterns are refused (a second direction makes grids).
- **Circular pattern** (`CircularPatternFeature.hpp`) repeats another
  feature's operation around an axis. A `CircularPatternDefinition` holds:
  - the source feature, which the pattern consumes (its `target()`);
  - the axis: an origin and a direction as given (any finite, non-zero
    vector, normalized when used);
  - a count, which includes the source and may be driven by a
    dimensionless parameter holding a whole number;
  - the spacing: `FullCircle` (360°/count apart, no angle), `IncludedAngle`
    (the source to the last instance, angle/(count − 1) apart) or
    `AngleStep` (angle apart), the angle literal or driven by an angle
    parameter;
  - the direction: `Positive` (right-handed about the axis) or `Negative`.

  Instance i is the source turned by i·step, computed from the source for
  every instance (`circularPatternInstances()`), never by adding to the
  previous one. A full circle never has an instance at 360°, which would be
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
  Circular patterns of patterns, and linear patterns of circular ones, are
  refused.
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
  (`bettercad/core/document/DependencyGraph.hpp`).
- `features::Regenerator` rebuilds only what changed: items whose revision
  differs from the one recorded at their last build, plus everything
  downstream of them. It works in deterministic dependency order.
- Failures are reported per item. Dependents of failures, missing
  references and cycles are blocked and keep no stale results.
- New object kinds plug in a regeneration handler by type name.
- **Result bodies** (`ResultBodies.hpp`). A feature's body is a model result
  unless another feature consumes it as its target (the target of a
  Join/Cut/Intersect, or the body a chamfer modifies).
  `regenerateResultBodies()` regenerates a copy of the document and returns
  those bodies. Exports use it.
- **Validation** (`Validation.hpp`). `validateDocument()` runs six checks on
  a copy of the document:
  1. document consistency (references point at the right kind of item and
     dimension, e.g. revolve axes are lines, revolve angles are angles, and
     chamfer distances, fillet radii and hole dimensions and centres are
     lengths; pattern counts are dimensionless, spacings lengths, circular
     pattern angles angles and mirror plane offsets lengths; sweep paths
     are in sketches, and their edges are not points);
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

  Sketch data holds the placement frame, the entities and constraints with
  their own IDs, and the sketch's ID counters. Extrude, revolve and chamfer
  data hold their definitions:
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
    optional `angle_parameter`;
  - a mirror stores `source`; `plane` as `{"origin": [...], "normal":
    [x, y, z], "offset": metres}` (the normal as given) with an optional
    `offset_parameter`; `scope` (`"feature"`, `"body"`); and
    `keep_original`;
  - a sweep stores `profile`; `path` as `{"sketch": id, "edges": [ids]}`
    (in the order of travel); `orientation` (`"follow_path"`); `operation`;
    and an optional `target`.

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

### Test tooling

- `tests/support/` holds shared fixtures:
  - the P9 bracket model, the turned part (revolves), and the parametric
    block (`BlockModel`) with its chamfered, filleted, drilled
    (`HoleModels.hpp`), patterned (`PatternModels.hpp`) and mirrored
    (`MirrorModels.hpp`) variants, and the swept models (`SweepModels.hpp`);
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
