# P7 — Extrude: verification

Date: 2026-09-14. Incremental build of the existing preset trees; every
changed translation unit was recompiled. From this milestone on, CTest is
skipped when a build fails, so a stale test binary cannot be counted.
Raw logs are in this directory.

| Preset | Configure | Build (`-Werror`) | `warning:`/`error:` lines | Tests |
|--------|-----------|-------------------|---------------------------|-------|
| debug | exit 0 | exit 0 | 0 | 230/230 passed |
| release | exit 0 | exit 0 | 0 | 230/230 passed |
| debug-shared | exit 0 | exit 0 | 0 | 230/230 passed |

There are 18 new test cases in `tests/features/`.

## Design

```text
Sketch ─► extractRegions (features) ─► geometry::PlanarRegion ─► geometry::makePrism ─► Body
```

- **Geometry API.** New in the geometry API (no OCCT types):
  - `PlanarRegion`: an outer loop plus holes, made of line, arc and circle
    segments in a `Frame3D`.
  - `signedArea`: Green's theorem, with arcs integrated exactly.
  - `makePrism(region, from, to)`: loops are oriented automatically; vertices
    are shared exactly between consecutive edges; the face is built on an
    explicit plane.
- **New library `bettercad_features` (layer 2).** It depends on core,
  sketch and geometry.
- **`ExtrudeFeature`.** A `DocumentObject` (type `"extrude"`) that stores
  only its inputs, `ExtrudeDefinition{.profile, .depth, .depthParameter,
  .direction, .operation, .target}`. `regenerateExtrude()` computes the body
  from the current document. Regenerated geometry is derived, so it does not
  change the document revision.
- **Commands.** `CreateExtrudeCommand` and `ModifyExtrudeCommand` (undoable).

## Closed-profile detection

`extractRegions(sketch)` connects the ends of non-construction lines and
arcs by position, within the sketch tolerance, which covers both shared
point entities and solved coincident constraints. Every end must meet
exactly one other end. It then walks the cycles, takes circles as loops,
orients all loops counter-clockwise, and classifies them by nesting depth
using an exact ray-crossing test that includes arcs: even depth is an outer
loop, odd depth a hole.

| Test | Evidence |
|------|----------|
| `A rectangle of connected lines is one closed region` | 4 segments, area 5000 mm², sketch plane |
| `Loop orientation is normalized to counter-clockwise` | clockwise input → positive area |
| `Lines closed by coincident constraints form a profile once solved` | open before solving (1–2 mm gaps), closed after |
| `A circle is a closed region on its own` | area πr² |
| `Nested loops become holes; loops inside holes become islands` | plate with hole (clockwise hole, area 5000 − 100π); island → 2 regions; disjoint loops → 2 regions |
| `Profiles may contain arcs` | slot area 800 + 100π |
| `Construction geometry is not part of profiles` | diagonal branches until marked as construction |
| `Open, branching and empty sketches have no valid profile` | `FailedPrecondition` with "open" / "branches" messages |

## Extrude feature and analytic volume regression

| Test | Analytic check |
|------|----------------|
| `Spec acceptance: 100 x 50 mm rectangle extruded 20 mm has V = 100000 mm^3` | measured **100000.0000000000146 mm³** (rel. error 1.5e-16), 6 faces, bounds (0,0,0)–(100,50,20) |
| `Extrude directions` | reversed: z ∈ [−20, 0]; symmetric: z ∈ [−10, 10]; XZ-plane sketch extrudes along −Y |
| `Extruded regions with circles, holes and arcs` | cylinder πr²h; plate with hole (5000 − 100π)·20 with 7 faces; slot (800 + 100π)·5; two regions → 2 solids |
| `Join, cut and intersect combine with a target body` | cut 100000 − 2000π; join 100000 + 3000π; intersect 2000π; missing target → `FailedPrecondition` |
| `Analytic volume regression for extruded rectangles` | 5 sizes from 0.5 mm to 1.2 m: V = w·h·d to 1e-12, bit-identical on repeated regeneration |
| `Extrude definitions are validated`, `Regeneration reports broken references` | invalid depth, missing profile, operation/target mismatch; missing sketch, non-sketch profile, missing/angle-valued depth parameter, open profile |
| `Extrude features are created and modified through commands` | create / modify / undo / redo with the volume following |

## Parameter-driven regeneration

`Changing the depth parameter from 20 mm to 40 mm regenerates the solid`
starts from a document with parameter `depth = 20 mm`, a 100 × 50 mm
rectangle sketch, and an extrude driven by the parameter:

| Step | Measured volume (mm³) |
|------|-----------------------|
| regenerate | 100000.0000000000146 |
| `ModifyParameterCommand` depth = 40 mm, regenerate | 200000.0000000000291 |
| undo, regenerate | 100000.0000000000146 |
| redo, regenerate | 200000.0000000000291 |

`Sketch changes flow into the extrude after solving`: changing the sketch's
width constraint to 120 mm and solving gives 120 × 50 × 20 mm³.

Automatic propagation (a parameter change marking dependent sketches and
features dirty and regenerating them in order) is P8.

## Findings during implementation

- A first run had one failing test, caused by the test itself: two sketches
  with the same name in one document. The document correctly rejected the
  duplicate. The test now uses distinct names.
