# ADR-006 — Assemblies are a new module at layer 3, and io moves up

Status: Accepted; the table below was extended by
        [ADR-015](ADR-015-drawing-module-and-layer.md), which inserts
        `drawing` at 4 and moves `io` to 5 and `renderer`/`scripting` to 6.
        The decision here — `assembly` at 3, above `features` and below `io`
        — is unchanged.
Date: 2026-09-19

## Context

Assembly code has to live in a module, and the layer rules decide which
modules it may use and which may use it. This is not a filing decision: the
layering check is executable and fails the build.

`tests/architecture/CheckLayering.cmake:23` holds the table, and it *is* the
source of truth — `ARCHITECTURE.md:80` mirrors it non-normatively:

```cmake
set(layer_core 0)
set(layer_sketch 1)
set(layer_features 2)
set(layer_io 3)
set(layer_renderer 4)
set(layer_scripting 4)
```

The rule is **strictly lower** (`CheckLayering.cmake:86`): an include of
another module is a violation unless `layer_target LESS layer_file`. Same-layer
cross-module dependencies are forbidden. An unknown module is itself a
violation — `"unknown module '<x>' (add it to the layer table)"`
(`:59`) — so a new directory cannot appear without editing the table.

What assemblies need, and what needs assemblies:

- Assembly code needs `features`: regeneration handlers, `resolvePlane` /
  `resolveAxis` for mate references (`Datums.hpp:216`), the `BodyLookup`
  callback and result bodies. So its layer must be **> 2**.
- `io` must serialize components and mates, so `io`'s layer must be **>**
  the assembly layer.
- The CLI must describe and validate assemblies; `apps` is outside the table.

With `io` at 3, there is no number left between them. This cannot be absorbed
silently.

Note that `bettercad_geometry` is a separate CMake target but lives under
`include/bettercad/core/geometry/`, so `module_of()` maps it to `core`,
layer 0 (`CheckLayering.cmake:46`). There is no `layer_geometry`, and a new
module gets no such exemption.

`ARCHITECTURE.md:93` already reserves the directory: "Target directories not
yet created: `src/assembly/`, …".

## Constraints

- Invariant 11: dependencies flow downward; module cycles are prohibited.
- Invariant 6: OCCT stays behind adapters — only inside an `occt/` directory
  under `src/` (`CheckLayering.cmake:31`).
- `P12-QUAL-001` qualified the current tree. Renumbering must not change
  behaviour, and must be verifiable.

## Options

1. **New module `assembly` at layer 3; `io` → 4; `renderer`/`scripting` → 5.**
2. **Assemblies inside `features`** (layer 2), as `features/assembly/`.
3. **Split it: definition types in `core`, resolution in a higher module** —
   mirroring how `PlaneReference` lives in `core`
   (`core/document/References.hpp`) while `resolvePlane` lives in `features`.

## Decision

Option 1, with a deliberate borrowing from option 3.

- A new module `assembly` at **layer 3**; `io` becomes **4**;
  `renderer` and `scripting` become **5**.
- The *reference and definition value types* that carry no geometry —
  the mate reference kinds and the placement intent — live in **`core`**,
  beside `References.hpp`, so that `io` and the CLI can name them without
  depending on `assembly` internals, and so `core` keeps owning the
  vocabulary of references.
- The `DocumentObject` kinds, resolution, the constraint system and the
  solver live in **`assembly`**.

## Rationale

Option 2 avoids the renumber and is genuinely cheaper today, which is why it
was considered rather than dismissed. It was rejected for three reasons.
`features` is already the largest module and conflating part features with
assembly structure would make the boundary between "a feature of a part" and
"a relationship between parts" disappear exactly where it matters most.
`ARCHITECTURE.md` reserves `src/assembly/`, so option 2 contradicts a
recorded intention without new evidence. And the drawings, BOM and simulation
capabilities that follow all consume assemblies; if assemblies are a private
corner of `features`, every one of them ends up depending on `features`
wholesale.

Option 3 alone cannot work: a component must resolve bodies and a mate must
resolve datums, and both are `features` operations, which `core` may not
reach. But its *insight* is right and is kept — the reference vocabulary
belongs in `core`, which is where `References.hpp` already puts it, so that
the format and the CLI describe assemblies without reaching into the module
that solves them.

The renumber is the honest cost of option 1. It is a mechanical, verifiable
change: five lines in `CheckLayering.cmake`, one `add_subdirectory` in
`src/CMakeLists.txt` (which documents itself as "lowest layer first"), a
`src/assembly/CMakeLists.txt` modelled on `src/features/CMakeLists.txt`, and
an export header via `bettercad_add_library(... EXPORT_BASE BETTERCAD_ASSEMBLY
EXPORT_HEADER bettercad/assembly/Export.hpp)`. Nothing about the renumber
changes any dependency that exists today: `io` at 4 may still use everything
it uses at 3, because every module it uses is below both numbers. The
checker has six self-tests against fixture trees
(`tests/architecture/CMakeLists.txt:9`), so the change is verified by the
same mechanism it modifies.

Doing the renumber now, while `assembly` is empty, is much cheaper than doing
it once assembly code exists.

## Consequences

- The layer table becomes: `core` 0, `sketch` 1, `features` 2, `assembly` 3,
  `io` 4, `renderer`/`scripting` 5.
- `ARCHITECTURE.md:80` must be updated with it, and `docs/architecture.md:35`
  mirrors it too.
- `io` gains a dependency on `assembly` for persistence. That is the point of
  the ordering, and it means the `.bcad` reader and writer are the only
  places outside `assembly` that need to know its object kinds — consistent
  with how every other kind is serialized today.
- `assembly` must contain no OCCT. It composes transforms and calls
  `features` and `core/geometry`; `geometry::transformed(Body,
  RigidTransform3D)` (`core/geometry/Transform.hpp:21`) already does the
  kernel-facing work, and it preserves face names, which
  [ADR-004](ADR-004-mates-reference-semantic-geometry-only.md) depends on.
- Because `Regenerator::registerHandler` is public
  (`features/Regenerator.hpp:74`), `assembly` registers its own regeneration
  handlers and `features` does not need to know assemblies exist. That keeps
  the dependency one-way and is what makes layer 3 workable.
- Any later module that consumes assemblies (drawings, BOM, simulation) sits
  above 3, not inside `features`.

## Verification

The renumber is verified by `architecture.layering` passing with the new
table, its six checker self-tests still passing, and a clean build of all
three presets — which is what the milestone that performs it must run. No
source file's includes should need to change: if any does, a dependency was
pointing the wrong way already and that is a finding, not a fix-up.
