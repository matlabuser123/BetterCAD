# BetterCAD

> A verification-driven parametric mechanical CAD platform, written in C++23.

## Overview

BetterCAD models parts the way engineers describe them — parameters, sketches,
constraints and features — and treats the resulting solid as an output of that
description rather than as the model itself. The engine is headless: the CLI and
the desktop application are clients of it, not owners of its state.

Development proceeds in small, evidence-gated milestones. Every capability
listed below is backed by tests and a recorded evidence file under
[docs/verification/](docs/verification/). Nothing is described here as working
unless a log says so.

## Current Status

| | |
| --- | --- |
| Parametric part modeling | **Qualified** — `P11-QUAL-001` |
| Current implementation | `P12` — Parametric CAD Completion, in progress ([TODO.md](TODO.md)) |
| Next | `P12-LOFT-001` — differing section shapes, smooth interpolation, end conditions |
| Released | `v0.1.0` (`P0`–`P10`); `P11` is qualified but not released |

The qualification rebuilt the tree clean in Debug, Release and Debug-shared and
ran the complete suite in each: **738/738 tests and 0 compiler warnings over 274
translation units, three times over**, with 22 warning flags and `-Werror`. The
301 tests that predate `P11` still pass unchanged. The six reference models are
bit-identical across every build and process. 20 gates, 20 passed
([evidence](docs/verification/P11-QUAL-001/README.md)).

## Capabilities

| Area | Verified today |
| --- | --- |
| Engineering types | Unit-safe quantities stored in SI; strongly typed stable IDs. Dimensional and ID misuse fail to compile. |
| Parameters | Named, dimensioned, revision-tracked; drive sketches and features. Expressions with units (`width - 2 * edge_distance`) are evaluated in dependency order with dimensional analysis; cycles and bad expressions are reported, never coerced ([evidence](docs/verification/P12-PARAM-001/README.md)). |
| Document | Object registry, metadata, revisions, dirty state; every edit is a command, with undo/redo. |
| Datum geometry | Datum planes (fixed, offset, angled), datum axes (fixed, two-plane intersection) and coordinate systems (fixed, offset), literal or parameter-driven. Sketches attach to them and follow at regeneration; mirror planes and circular-pattern axes may refer to them ([evidence](docs/verification/P12-DATUM-001/README.md)). |
| Sketches | Points, lines, circles, arcs, ellipses and B-splines on a placed plane, a datum plane or a planar face of an extrude, revolve, sweep, loft, hole or chamfer, or a pattern's or mirror's copy of one, which they follow when the model changes ([evidence](docs/verification/P12-SKETCH-003/README.md)); entities: [evidence](docs/verification/P12-SKETCH-002/README.md). Coincident, horizontal, vertical, parallel, perpendicular, distance, radius, equal, fixed, angle, tangent, concentric, midpoint, symmetric and diameter constraints ([evidence](docs/verification/P12-SKETCH-001/README.md)); edits are undoable. A Gauss–Newton solver reporting under-, fully- and over-constrained, inconsistent and failed states with residual and DOF. |
| Features | Sixteen. Extrude (to a depth, or through all of its target: [evidence](docs/verification/P12-FEAT-001/README.md)), revolve, sweep (along a path of one sketch or of several, with an optional twist or guide curve: [evidence](docs/verification/P12-SWEEP-001/README.md)), loft (new body / join / cut / intersect); chamfer, fillet, hole (simple, counterbore, countersink; through, blind); linear pattern, circular pattern, mirror; split by a plane and combine bodies ([evidence](docs/verification/P12-FEAT-002/README.md)); shell, inward or outward, opened at named faces ([evidence](docs/verification/P12-FEAT-003/README.md)); draft of named faces about a neutral plane ([evidence](docs/verification/P12-FEAT-004/README.md)); rib from an open sketched profile to the body ([evidence](docs/verification/P12-FEAT-005/README.md)); variable-radius fillet of straight edges, with radius stations and a checked law ([evidence](docs/verification/P12-FEAT-006/README.md)). Holes take a spotface, a cosmetic ISO metric thread, a standard clearance size and an ISO 286 tolerance class, by their designations ([evidence](docs/verification/P12-HOLE-001/README.md)). Patterns spread their instances by spacing or over a total length, symmetrically about the source, with chosen instances suppressed, and may repeat another pattern ([evidence](docs/verification/P12-PATTERN-001/README.md)). |
| Regeneration | Dependency graph with dirty propagation, topological ordering, cycle detection and partial rebuild. A failed feature commits nothing. |
| Validation | Every result checked for validity, solid count and positive volume, then compared against geometry computed independently from its parameters. |
| Persistence | Native `.bcad` JSON storing engineering intent, not meshes. Create → save → destroy → load → regenerate reproduces the model bit for bit, IDs included. |
| Export | STEP (AP214) and STL, each verified by reading the result back and measuring it. |
| CLI | `bettercad-cli`: `new`, `info`, `validate`, `export-step`, `export-stl`, `version`, `help`. |
| Reference models | Six mechanical parts — stepped shaft, bolted flange, V-belt pulley, pillow block, L bracket, U-bolt — built through the public API alone ([evidence](docs/verification/P11-REF-001/README.md)). |

STEP **export** only; there is no STEP import. See *Current Limitations*.

## Architecture

```text
Desktop (placeholder)   CLI
            │            │
            └─────┬──────┘
                  ▼
        BetterCAD Public API
                  ▼
   Document / Commands / Transactions
                  ▼
   Dependency / Regeneration Engine
                  ▼
         Geometry Services
                  ▼
      OCCT Adapter  (src/**/occt/)
                  ▼
           Open CASCADE
```

Dependencies flow downward only, enforced by the `architecture.layering` test
rather than by convention. Open CASCADE is visible only inside `occt/` adapter
directories; Qt only in `apps/bettercad/` and `src/renderer/`.

Invariants and dependency rules: [ARCHITECTURE.md](ARCHITECTURE.md).
Modules and targets as built: [docs/architecture.md](docs/architecture.md).

## Build and Test

Requirements: CMake 3.25+, Ninja, a C++23 compiler with `<format>`
(GCC 13+, Clang 17+, MSVC 19.37+), Open CASCADE 8.0, and Qt 6.5+ for the desktop
application.

If your system provides Qt 6 and OCCT built with your compiler, pass their
location through `CMAKE_PREFIX_PATH`. Otherwise build them from pinned,
hash-verified sources (30–60 minutes):

```sh
cmake -S deps -B <deps-build-dir> -G Ninja
cmake --build <deps-build-dir>
```

The superbuild installs to `%LOCALAPPDATA%/bettercad-deps/<toolchain>` on
Windows, or `$XDG_DATA_HOME/bettercad-deps/<toolchain>` (default
`~/.local/share/...`) elsewhere. Override with `-DBETTERCAD_DEPS_PREFIX=...`.

```sh
cmake --preset debug          # or: release, debug-shared
cmake --build --preset debug
ctest --preset debug
```

All three presets are supported and expected to pass the complete suite.
`debug-shared` links modules as separate DLLs, exercising export macros and
cross-module linkage a static build cannot check.

| Option | Default | Meaning |
| --- | --- | --- |
| `BETTERCAD_BUILD_GUI` | `AUTO` | `ON` requires Qt 6; `AUTO` builds the desktop app if Qt 6 is found |
| `BETTERCAD_BUILD_CLI` | `ON` | Build `bettercad-cli` |
| `BETTERCAD_BUILD_TESTS` | `ON` (top level) | Build the test suite |
| `BETTERCAD_WARNINGS_AS_ERRORS` | `OFF` (`ON` in presets) | Treat warnings in BetterCAD code as errors |
| `BUILD_SHARED_LIBS` | `OFF` | Build BetterCAD libraries as shared libraries |

Tested toolchain: GCC 16.1 (WinLibs MinGW-w64, UCRT), CMake 4.4, Windows 11.
No MSVC, Clang, Linux or macOS result is claimed.

> If the checkout sits in a cloud-synced folder such as OneDrive, the sync
> client can briefly lock freshly built files: Ninja reports it cannot write
> `.ninja_lock`, or a test fails to start with `0xC0000043`. Re-running works.

## Usage

```sh
bettercad-cli new part.bcad [--name <name>] [--force]
bettercad-cli info      examples/models/plate.bcad
bettercad-cli validate  examples/models/plate.bcad
bettercad-cli export-step examples/models/plate.bcad plate.step
bettercad-cli export-stl  examples/models/plate.bcad plate.stl --tolerance 0.05mm [--ascii]
```

Exports regenerate the model first and write its result bodies in millimetres.
Exit status: 0 success, 1 failure or invalid document, 2 invalid command line.

`.bcad` files are transparent JSON — see
[examples/models/plate.bcad](examples/models/plate.bcad), written by hand.
[examples/models/reference/](examples/models/reference/) holds the six reference
models, built by [examples/reference_models/](examples/reference_models/).

## Repository Structure

```text
apps/bettercad/        Qt desktop application (placeholder shell)
apps/bettercad_cli/    bettercad-cli
cmake/                 build-system modules
deps/                  superbuild for Qt and Open CASCADE
docs/architecture.md   architecture as built
docs/verification/     milestone evidence, one directory per milestone
include/bettercad/     public headers, one directory per module
src/<module>/          implementation and private headers
tests/                 unit, process-level and architecture tests
examples/              sample models and programs
```

## Project Documentation

| Document | Purpose |
| --- | --- |
| [README.md](README.md) | Project overview and getting started |
| [ROADMAP.md](ROADMAP.md) | Long-term capability direction and completed capabilities |
| [TODO.md](TODO.md) | Authoritative implementation status and next work |
| [ARCHITECTURE.md](ARCHITECTURE.md) | Technical architecture and invariants |
| [CLAUDE.md](CLAUDE.md) | Engineering workflow for coding agents |
| [docs/verification/](docs/verification/) | Proof of completion |

When documents disagree, the later entry wins:
`docs/verification/` → `TODO.md` → `ARCHITECTURE.md` → `ROADMAP.md` → `README.md`.

## Verification

> Implementation is not completion.

A milestone is complete only when it is implemented, tested, independently
validated where a reference exists, pinned by deterministic regression tests,
and supported by recorded evidence.

In practice: analytic validation rather than self-comparison (a chamfered
block's volume is checked against *V*₀ − ½*d*²*L*, not a previous run);
determinism measured across processes and build configurations; failure paths
tested as carefully as success paths; limitations written down rather than left
for a user to find.

## Current Limitations

### Modeling

- **Geometric references do not follow moved geometry.** Edges and faces are
  matched by geometry, not named semantically. When a parameter moves the edge a
  chamfer was attached to, the feature fails with `NotFound` and keeps no body —
  no entity is ever substituted. Semantic topology is planned, not built.
- **Parameter expressions** have arithmetic, units and parameter names only —
  no functions, powers or constants — and only parameters take them.
- Tangent sketch constraints keep the side or kind of contact the sketch
  starts with; splines are tangent only at joints, ellipses not at all.
- Lofts and sweep paths take lines, arcs and circles only.
- Uniting a half body with its mirror image is refused when a half cylinder lies
  on the mirror plane.

### Not implemented

- **The desktop application is a placeholder shell.** No GUI functionality is
  claimed or qualified.
- **STEP export only** — no STEP import. The kernel-based reader in the tree is
  test-only tooling that reads exports back to check them. No DXF, IGES or OBJ.
- No assemblies, drawings, materials database, meshing, simulation,
  optimization, Python API, CAM or AI. All are planned; see
  [ROADMAP.md](ROADMAP.md).

### Limits of the evidence

- One platform (Windows 11 AMD64, GCC 16.1.0, OCCT 8.0.1, Qt 6.11.2).
- No CI, no sanitizers, no coverage, no memory checking; `.clang-format` is
  defined but not enforced.
- 738 tests passing means 738 tests passed. Where a behaviour has no test, the
  evidence says nothing about it.

## License

Not yet chosen; see [LICENSE](LICENSE). Third-party components keep their own
licenses: Open CASCADE LGPL 2.1 with an exception, Qt LGPL 3, Catch2 BSL 1.0.
