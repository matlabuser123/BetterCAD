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
                 features           extrude, revolve, chamfer (later: fillet, ...)
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
- **Edges and edge references** (`Edges.hpp`). `listEdges(body)` describes a
  body's edges by their geometry (curve kind, ends, length, number of
  faces). Features refer to an edge by an `EdgeSignature`: the edge's
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
     dimension, e.g. revolve axes are lines, revolve angles are angles and
     chamfer distances are lengths);
  2. missing references (including revolve axis lines in their sketches);
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
    `reference_side` only when its mode uses them.

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
  - the P9 bracket model, the turned part (revolves) and the chamfered block;
  - temporary directories;
  - mesh and STL analysis written from first principles (divergence-theorem
    volume, edge-manifold watertightness).
- `tests/support/occt/` reads exported STEP files back with the kernel. It
  is test-only tooling, since STEP import is a later milestone, and follows
  the same rule as product code: only an `occt/` directory includes OCCT
  headers.

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
