# ADR-015 — A `drawing` module at layer 4; `io` moves up again; projection stays in `geometry`

```text
Status:     Accepted
Date:       2026-09-22
Milestone:  P14-ARCH-001
Builds on:  ADR-006 (assembly module and layer)
            ADR-010 (drawings live in the document)
Supersedes: ADR-006's layer table, which placed `io` at 4 and
            `renderer`/`scripting` at 5. ADR-006's decision — a module for
            assemblies at 3, above `features` and below `io` — stands
            unchanged; only the numbers above it move.
```

## Context

Drawing code has to live in a module, and the layer rules decide which,
because `tests/architecture/CheckLayering.cmake` is executable and fails the
build.

The table in force (`CheckLayering.cmake:22-32`):

```cmake
set(layer_core 0)
set(layer_sketch 1)
set(layer_features 2)
set(layer_assembly 3)
set(layer_io 4)
set(layer_renderer 5)
set(layer_scripting 5)
```

The rule is **strictly lower** (`:90-91`): an include of another module is a
violation unless `layer_target LESS layer_file`. Same-layer cross-module
dependency is a violation, and a module absent from the table is itself a
violation, twice over (`:62-65` for a file in an unlisted directory, `:88-89`
for an include of an unlisted module).

Two constraints fix the number:

- **Drawing must sit above `assembly` (3).** An assembly drawing is a view of
  solved component placements, at the active configuration, with suppressed
  components absent. That is `assembly`'s vocabulary.
- **`io` must sit above drawing.** ADR-010 makes drawing objects
  `DocumentObject`s in the `.bcad` file, and `objectToJson`/`objectFromJson`
  live in `src/io/json/DocumentJson.cpp`, so `io` will include
  `<bettercad/drawing/…>`.

There is no integer between 3 and 4. This is the same finding ADR-006 made —
recorded in `P13-ARCH-001`'s evidence as "the layer rule leaves no room" —
and the comment ADR-006 left in the table even anticipates its recurrence.

A second, separate question: where does the projection and hidden-line
machinery live? It is kernel work. OCCT headers may only be included from an
`occt/` directory under `src/` (`CheckLayering.cmake:35`), and every existing
geometry adapter is under `src/core/geometry/occt/` — including
`OcctStep.cpp`, whose public façade `core/geometry/Exchange.hpp` returns file
*contents* so that "writing files is the caller's job". `libTKHLR` is present
in the dependency prefix, with `HLRBRep_Algo`, `HLRBRep_PolyAlgo`,
`HLRToShape` and `PolyHLRToShape`, so hidden-line removal is a kernel call and
not a subsystem BetterCAD has to write.

## Options

1. **New module `drawing` at layer 4; `io` → 5; `renderer`/`scripting` → 6.**
2. **Drawings inside `assembly`** (layer 3), as `assembly/drawing/`.
3. **Drawing above `io`** (layer 5), with drawing objects serialized by the
   drawing module itself rather than by `io`.

## Decision

**Option 1.** The table becomes:

```text
core 0, sketch 1, features 2, assembly 3, drawing 4, io 5, renderer/scripting 6
```

And the work divides across three places, not one:

```text
core/geometry  (layer 0, occt adapter)   project a Body onto a Frame3D and
                                         classify its edges: visible, hidden,
                                         silhouette, smooth. Section cuts.
                                         Returns neutral 2D curves.

drawing        (layer 4)                 sheets, views, dimensions,
                                         annotations, tolerances, BOM.
                                         Decides WHAT to project and composes
                                         the result. Owns the drawing scene
                                         (ADR-016).

io             (layer 5)                 serialization of drawing objects;
                                         the PDF, SVG and DXF writers; atomic
                                         file writing.
```

## Rationale

Option 2 fails on meaning before it fails on layering. A drawing of a single
part needs no assembly at all, so putting drawings inside `assembly` would
make every part drawing depend on a subsystem it does not use, and would give
`assembly` a second, unrelated responsibility. ADR-006's own consequences
already ruled on this shape: "Any later module that consumes assemblies
(drawings, BOM, simulation) sits above 3, not inside `features`."

Option 3 is the only one that avoids the renumber, and it costs more than the
renumber saves. Keeping `drawing` above `io` requires drawing objects to be
serialized somewhere other than `src/io/json/`, which abandons the
`{id, type, name, data}` envelope, the single `objects` array, and the
property ADR-010 relies on — that adding a drawing needs no format version
bump. It would split persistence across two modules for the sake of three
lines in a CMake file.

The renumber itself is cheap and, as ADR-006 observed of its own, cheapest
now: three `set()` lines change, one `add_subdirectory` is inserted in layer
order, and **no existing include changes**, because everything `io` uses is
below both its old and its new number. Doing it while `drawing` is empty costs
nothing; doing it after drawing code exists costs a rebuild of the
relationships.

Splitting projection into `core/geometry` rather than `drawing` follows the
split ADR-006 already made between reference *types* in `core` and reference
*resolution* in `assembly`, and the one `geometry::writeStepAssembly` makes
between a neutral structure and its kernel backend. "Project this body onto
this plane and tell me which edges are visible" is a geometry question with a
kernel answer and no drawing vocabulary in it; "which bodies, at which
transforms, at what scale, on which sheet" is the drawing's question. Putting
the kernel call in `drawing` would either put OCCT headers at layer 4 —
requiring a second `occt/` adapter directory away from every other one — or
force `drawing` to reach down past a missing abstraction.

The consequence is worth stating: **a drawing subsystem is not one module.**
It is a geometry primitive at layer 0, a domain module at layer 4, and writers
at layer 5.

## Consequences

- `CheckLayering.cmake` gains `set(layer_drawing 4)` and changes `io` to 5,
  `renderer` and `scripting` to 6. `src/CMakeLists.txt` gains
  `add_subdirectory(drawing)` in layer order.
- `src/drawing/CMakeLists.txt` follows `src/assembly/CMakeLists.txt`:
  `bettercad_add_library(bettercad_drawing ALIAS drawing EXPORT_BASE
  BETTERCAD_DRAWING EXPORT_HEADER bettercad/drawing/Export.hpp …)` with
  `PUBLIC_LINK BetterCAD::core BetterCAD::features BetterCAD::assembly`.
- `ARCHITECTURE.md` and `docs/architecture.md` must be updated with the new
  table. ADR-006 named both files and both were missed;
  `P13-QUAL-001` found them stale fifteen milestones later. They are updated
  by the milestone that performs the renumber, and that is a gate on it.
- `src/drawing/` stops being a reserved directory. `ARCHITECTURE.md` lists it
  under "Target directories not yet created" and that line changes.
- The `drawing` module must contain no OCCT, exactly as `assembly` must not.
- **This is the second forced renumber.** A third is likely — simulation and
  drawings both sit above assembly, and anything that must be serialized sits
  below `io`. Spacing the layers (10, 20, 30 …) would end it. That is a change
  to qualified infrastructure with no behavioural effect, it is not authorized
  here, and it is recorded as a recommendation for whichever milestone next
  needs a number that does not exist.

## Rejected alternatives, and what would make them right

**Drawings inside `assembly` (Option 2)** would be right if a drawing were an
assembly concept. It is not: a part drawing uses none of it.

**Drawing above `io` (Option 3)** becomes right only if drawings stop being
`DocumentObject`s in the model's file — which is ADR-010's decision, and the
day that changes this ADR is revisited with it.

## Verification

For `P14`: `architecture.layering` passes with the new table and its six
checker self-tests still pass; a clean build of all three presets succeeds; no
source file's includes need to change because of the renumber alone; `drawing`
includes only `core`, `features` and `assembly` headers; no `.hxx` is included
outside an `occt/` directory; and both architecture documents show the table
that `CheckLayering.cmake` holds.
