# P4 — Sketch data model (P4-001 … P4-003): verification

Date: 2026-09-14. Incremental build of the existing preset trees; every
changed translation unit was recompiled. Raw logs are in this directory.
Toolchain: GCC 16.1.0 (MinGW-w64 UCRT), CMake 4.4.2.

| Preset | Configure | Build (`-Werror`) | `warning:`/`error:` lines | Tests |
|--------|-----------|-------------------|---------------------------|-------|
| debug | exit 0 | exit 0 | 0 | 184/184 passed |
| release | exit 0 | exit 0 | 0 | 184/184 passed |
| debug-shared | exit 0 | exit 0 | 0 | 184/184 passed |

There are 22 new test cases: `tests/core/math/FrameTests.cpp` (7),
`tests/sketch/SketchTests.cpp` (13) and
`tests/sketch/SketchDocumentTests.cpp` (2).

## Design

- New library `bettercad_sketch` (layer 1). It depends on `bettercad_core`
  only, not on the OCCT-backed geometry library.
- To make that possible, the pure value types moved from
  `bettercad_geometry` into core (`include/bettercad/core/math/`) and were
  renamed consistently: `Point2D`, `Point3D`, `Direction3D`, `Axis3D`,
  `BoundingBox2D`, `BoundingBox3D`, plus the new `Frame3D`. The P3 geometry
  API and tests were updated, and all 162 existing tests passed after the
  move.
- `Sketch` is a `DocumentObject` (type name `"sketch"`). Points are entities
  of their own; lines, circles and arcs reference point entities (arc = centre,
  start and end points, counter-clockwise). Connected geometry shares points,
  and constraints (P5) can reference whole entities.

## P4-001 — Sketch coordinate system

`Frame3D`: origin, local X axis, local Y axis and normal, with
Y = normal × X, and the placement transform (`toGlobal`, `toLocal`,
`signedDistance`). A sketch's placement defaults to the global XY plane.

| Test | Evidence |
|------|----------|
| `The global XY plane maps sketch coordinates unchanged` | axes, `toGlobal`/`toLocal` exact, signed distance |
| `The XZ and YZ planes are right-handed` | orthonormal, X × Y = N, mappings (u,v) → (u,0,v) and (0,u,v) |
| `A general frame is orthonormal and round-trips points` | normal (1,1,1) at an offset origin; 3 points round-trip to 1e-10 mm |
| `Frame creation projects the X direction into the plane` | Gram–Schmidt projection; point projection |
| `Invalid frames are rejected` | X parallel or antiparallel to the normal; non-finite origin |
| `A new sketch lies on the global XY plane`, `A sketch on another plane places its geometry in model space` | sketch placement, `toGlobal`/`toLocal` of entity coordinates |

## P4-002 — Point, Line, Circle, Arc

| Test | Evidence |
|------|----------|
| `Spec usage: sketch.addLine(Point2D{0_mm, 0_mm}, Point2D{100_mm, 0_mm})` | line type, length 100 mm, deterministic IDs (points 1, 2; line 3) |
| `Points, lines, circles and arcs have stable IDs and types` | each type; a removal leaves other IDs unchanged; IDs are not reused |
| `Lines can share end points` | rectangle from 4 shared points; dependents; moving a corner moves both lines |
| `Invalid entities are rejected and leave the sketch unchanged` | 11 invalid creations (zero length, repeated point, non-point reference, missing ID, NaN, bad radius, zero or full sweep, unequal arc radii, equal arc ends) |
| `Referenced points cannot be removed` | `FailedPrecondition` while a line uses the point; removal allowed afterwards |
| `Editing reports whether anything changed` | point move, radius, construction flag, wrong-type errors |
| `The same construction steps produce identical sketches` | content and ID equality; clone; difference detected |

## P4-003 — Geometric query API

Entity lookup (`findEntity`), entity type, endpoints, length (line, arc,
circle), radius, centre, sweep and bounding boxes. Arc bounds include the
exact axis-extreme points the arc passes through.

| Test | Evidence |
|------|----------|
| `Endpoints of lines and arcs` | exact endpoints; wrong type → `InvalidArgument`; missing → `NotFound` |
| `Lengths of lines, arcs and circles` | 3-4-5 line = 5 mm; circle 2πr; quarter arc πr/2; 270° arc 3πr/2 |
| `Radius, centre and sweep of circles and arcs` | sweep 90°; wrap-around arc 350°→10° = 20° |
| `Bounding boxes of entities are exact` | point, line, circle, arc through +Y (45°→135°), arc across +X (350°→10°), 270° arc, whole sketch |

## CreateSketchCommand (carried over from P2-002)

`CreateSketchCommand` is built on `AddObjectCommand`.
`CreateSketchCommand creates a sketch that undo removes and redo restores`
checks typed `SketchId`, `findObjectAs<Sketch>`, lookup by name, and
execute/undo/redo with document equivalence.
`Sketch edits inside a document are tracked` checks `modifyObject<Sketch>`
revision tracking and clone independence.
