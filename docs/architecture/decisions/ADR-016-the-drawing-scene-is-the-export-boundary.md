# ADR-016 — The drawing scene is the export boundary, and writers compute nothing

```text
Status:    Accepted
Date:      2026-09-22
Milestone: P14-ARCH-001
Builds on: ADR-011 (drawing intent is canonical)
           ADR-015 (drawing module and layer)
```

## Context

`P14` ends in three exporters — PDF, SVG and DXF — and the failure they invite
is that each computes its own geometry and the three disagree. A drawing that
prints one way, opens in a viewer another way, and lands in a CAM system a
third way is worse than one that only exports to a single format.

The repository has one precedent and it is instructive in both directions.
`geometry::StepAssembly` (`include/bettercad/core/geometry/Exchange.hpp:45-66`)
is a structure handed to a writer: distinct parts, plus instances that place
them. It is genuinely reused — `exportStl` consumes the *same* structure
(`src/io/ModelExport.cpp:196-212`), transforming and triangulating each
instance itself — which proves the shape is not STEP-only.

But it is called `StepAssembly`, declared under a `// --- Assemblies
(P13-STEP-001) ---` heading, and its validation lives in the **backend**:
`writeStepAssembly` checks for empty instances, an out-of-range part index and
a non-finite placement (`src/core/geometry/occt/OcctStep.cpp:138-176`), so the
STL path gets none of it and simply trusts the structure. The second consumer
reads as a borrow rather than a contract.

There is also a gap. No 2D output path of any kind exists — no projection, no
hidden-line code, no 2D curve container, no vector writer. `geometry::Mesh`
and the opaque `geometry::Body` are the only geometry carriers that reach
`io`, and a `Body` is useless to a 2D writer except to hand back to the
kernel. `core/math` has `Point2D` and `BoundingBox2D` and nothing else 2D: no
`Vector2D`, `Direction2D`, `Frame2D` or `RigidTransform2D`.

## Options

1. **One neutral, validated drawing scene** owned by `drawing`; the three
   writers consume it and compute nothing.
2. **Each writer takes the drawing objects** and derives what it needs.
3. **A scene per format**, sharing a common subset.

## Decision

**Option 1.** The drawing module produces a **drawing scene**: a
backend-neutral, self-validating description of a finished sheet in sheet
coordinates, from which any writer can emit its format by transcription
alone.

```text
drawing intent  →  regenerated + solved model  →  drawing scene  →  writer
   (canonical)          (ADR-014, on demand)       (derived)       (io, layer 5)
```

The scene carries, at minimum:

```text
sheet size and orientation
groups / layers
polylines and arcs, in sheet millimetres
text runs with size, anchor and rotation
hatch regions
line type and line weight per element
```

Four rules bind it:

1. **It is named for what it is, not for a format.** It lives in a neutral
   header in `drawing` and no writer's name appears in it.
2. **It validates itself**, in `drawing`, once — finite coordinates, in-range
   indices, non-empty geometry, a sheet that contains its content. Every
   writer inherits the same guarantees rather than re-deriving or trusting.
3. **A writer transcribes.** It takes the scene and returns
   `Result<std::string>` — the file contents — exactly as
   `geometry::writeStep*` and `io::meshesToStl` do, so that "writing files is
   the caller's job, so that file handling (Unicode paths, atomic
   replacement) lives in one place" (`Exchange.hpp:14-16`) continues to hold.
   A writer computes no geometry, resolves no reference and reads no
   `Document`.
4. **The scene is derived and never persisted**, per ADR-011. It is built on
   demand and thrown away.

## Rationale

Option 2 is the failure mode named at the top, reached by the shortest path.
Three writers deriving hidden lines independently is three chances to disagree
about the same drawing, and the disagreement would show up as a customer
noticing that the PDF and the DXF differ.

Option 3 is Option 2 with extra steps: a shared subset plus per-format
extensions means the interesting cases — hatching, line weights, text metrics
— live in the extensions, which is where the divergence would be.

Option 1 is also what the `StepAssembly` precedent recommends *once its two
weaknesses are corrected*. That structure earned its keep by being reused, and
the lesson from the reuse is precise: name the thing neutrally so the second
consumer is a contract rather than a borrow, and put the validation on the
structure so the second consumer does not silently get less checking than the
first.

The layering follows ADR-015 without further argument. The scene is produced
by `drawing` (4) and consumed by `io` (5), so it is declared in `drawing` —
the same relationship `StepAssembly` has, declared in `core/geometry` (0) and
consumed by `io`. PDF, SVG and DXF need no kernel at all, so the writers are
pure `io` code beside `StlWriter.cpp`, which is the existing model for a
kernel-free format writer.

## Consequences

- The three writers are independently testable against a hand-built scene,
  with no model, no regeneration and no kernel.
- A fourth format is a new writer and nothing else.
- Export correctness splits cleanly: *is the scene right* is a drawing
  question; *is the file right* is a writer question. A read-back test can
  check the second without the first.
- **A small 2D vocabulary is missing and must be added**: the scene needs at
  least a 2D vector and a 2D transform to express placement and rotation.
  These belong in `core/math` beside `Point2D` and `BoundingBox2D`, not
  privately in `drawing`, so that `sketch` and a future renderer can use the
  same types.
- The scene is the natural place to enforce that **a suppressed component
  contributes nothing** and that **an unresolved reference emits no
  geometry** — one check, inherited by all three formats, rather than three.
- Text is the hard case and is flagged now rather than discovered later: a
  scene carries text runs with a size and an anchor, but PDF embeds fonts,
  SVG references them and DXF may stroke them. The scene must not assume a
  font model; deciding how far it goes toward glyph outlines is
  `P14-EXPORT-001`'s, and it is the most likely place for the three formats
  to diverge legitimately.
- Nothing here is built in `P14-ARCH-001`. The scene's field list above is a
  contract for `P14-EXPORT-001`, not a header.

## Rejected alternatives, and what would make them right

**Per-writer derivation (Options 2, 3)** would be right if the formats needed
genuinely different geometry rather than different encodings of the same
geometry. If a future format needed, say, a triangulated fill where the others
need a hatch pattern, that is an argument for enriching the scene, not for
splitting it.

## Verification

For `P14`: a scene built by hand and passed to all three writers produces
files that a reader agrees describe the same drawing — the same element
counts, the same extents, the same sheet size; a scene with a non-finite
coordinate or an out-of-range index is refused by the scene's own validation
before any writer sees it; a suppressed component contributes no element in
any format; and no writer includes a `Document`, a kernel header or a
reference type.
