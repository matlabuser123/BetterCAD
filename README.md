# BetterCAD

BetterCAD is a verification-driven mechanical CAD platform written in C++23. It
models parts the way engineers describe them — parameters, sketches,
constraints and features — and treats the resulting solid as an output of that
description rather than as the model itself.

Every capability listed below is backed by tests and by a recorded evidence
file. Nothing is described here as working unless there is a log that says so.

## Vision

The long-term goal is a modern alternative to traditional mechanical CAD:
parametric parts, assemblies, drawings, simulation, optimization, automation
and engineering version control, on one document model that does not need to be
replaced along the way. That ambition lives in [ROADMAP.md](ROADMAP.md); the
capabilities below are what exists today.

## Current Status

**Production part modeling is complete and qualified. No new development phase
is in progress, and none is authorized.**

The qualification (`P11-QUAL-001`) rebuilt the tree clean in three
configurations — Debug, Release and Debug-shared — and ran the complete test
suite in each: **738/738 tests passed with zero compiler warnings over 274
translation units, three times over**, against 22 warning flags and `-Werror`.
The 301 tests that existed before production part modeling began still pass
unchanged in all three. The six mechanical reference models come out
bit-identical across every build and every process that produced them.

Twenty qualification gates, twenty passed, none failed or blocked
([evidence](docs/verification/P11-QUAL-001/README.md)).

The next milestone is a scope decision, not a feature. See
[TODO.md](TODO.md) for exactly where the project stands.

## Verified Capabilities

| Capability | What works |
| --- | --- |
| Engineering types | Unit-safe quantities (`Length`, `Angle`, `Pressure`, …) stored in SI; dimensional mistakes fail to compile. Strongly typed stable IDs; mixing ID kinds fails to compile. |
| Parameters | Named, dimensioned, revision-tracked document parameters that drive sketches and features. |
| Document | Object registry, metadata, revisions, dirty state; all edits go through commands with working undo/redo. |
| Sketches | Points, lines, circles and arcs on a placed plane; coincident, horizontal, vertical, parallel, perpendicular, distance, radius, equal and fixed constraints; a Gauss–Newton solver that distinguishes under-, fully- and over-constrained, inconsistent and failed solves. |
| Features | Ten of them. Extrude, revolve, sweep and loft build solids as a new body, join, cut or intersect. Chamfer, fillet and hole (simple, counterbore, countersink; through and blind) modify a target feature's body. Linear pattern, circular pattern and mirror repeat another feature's operation. |
| Regeneration | An explicit dependency graph with dirty propagation, topological ordering, cycle detection and partial rebuild. A failed feature commits nothing and leaves the document exactly as it was. |
| Geometry validation | Every result is checked for validity, solid count and finite positive volume, and compared against geometry computed independently from its parameters. |
| Persistence | Native `.bcad` documents storing engineering intent, not meshes. Create → save → destroy → load → regenerate reproduces the model bit for bit, IDs included. |
| Export | STEP (AP214) and STL, both verified by reading the result back and measuring it. |
| CLI | `bettercad-cli` with `new`, `info`, `validate`, `export-step`, `export-stl`, `version` and `help`, on the same public API the tests use. |
| Reference models | Six mechanical parts built through the public API alone, every dimension checked against independently computed geometry, regenerating deterministically. |

The six reference models are a stepped shaft, a bolted flange, a V-belt pulley,
a pillow block, an L bracket and a U-bolt; between them they exercise every
feature listed above
([evidence](docs/verification/P11-REF-001/README.md)).

## Architecture at a Glance

```text
Applications        apps/bettercad (Qt, placeholder)   apps/bettercad_cli
       │
       ▼
Public API          include/bettercad/<module>/
       │
       ▼
Document            parameters, sketches, features, bodies, commands
       │
       ▼
Regeneration        dependency graph, dirty propagation, transactions
       │
       ▼
Geometry services   bodies, booleans, mass properties, meshing, STEP
       │
       ▼
Backend adapter     src/**/occt/ — the only code that sees Open CASCADE
```

Dependencies flow downward only, and the direction is enforced by the
`architecture.layering` test rather than by convention. The engine has no GUI
dependency; the desktop application and the CLI are both clients of it.

See [ARCHITECTURE.md](ARCHITECTURE.md) for the architectural invariants and
[docs/architecture.md](docs/architecture.md) for the architecture as built.

## Requirements

- CMake 3.25 or newer, Ninja
- A C++23 compiler with `<format>`: GCC 13+, Clang 17+ or MSVC 19.37+
- Open CASCADE Technology 8.0
- For the desktop application: Qt 6.5+ (Widgets)

Tested toolchain: GCC 16.1 (WinLibs MinGW-w64, UCRT) with CMake 4.4 on
Windows 11. That is the only toolchain any result in this repository was
measured on; no MSVC, Clang, Linux or macOS result is claimed.

## Build

### 1. Binary dependencies (Qt, Open CASCADE)

If your system provides Qt 6 and OCCT packages built with your compiler, pass
their location through `CMAKE_PREFIX_PATH` and skip this step. Otherwise build
them from pinned, hash-verified sources with the included superbuild. This
takes a while (roughly 30–60 minutes).

```sh
cmake -S deps -B <deps-build-dir> -G Ninja
cmake --build <deps-build-dir>
```

The superbuild installs into a toolchain-specific prefix, which the main
project finds without extra configuration:

- Windows: `%LOCALAPPDATA%/bettercad-deps/<toolchain>`
- Linux/macOS: `$XDG_DATA_HOME/bettercad-deps/<toolchain>`, or
  `~/.local/share/bettercad-deps/<toolchain>` if `XDG_DATA_HOME` is unset

Here `<toolchain>` is, for example, `gnu-16-mingw-amd64`. Override the prefix
with `-DBETTERCAD_DEPS_PREFIX=...`. Keep `<deps-build-dir>` outside
cloud-synced folders such as OneDrive.

### 2. BetterCAD

```sh
cmake --preset debug          # or: release, debug-shared
cmake --build --preset debug
```

Build trees go to `build/<preset>/` and executables to `build/<preset>/bin/`.
If the checkout is in a cloud-synced folder such as OneDrive, the sync client
can briefly lock freshly built files. Ninja then reports it cannot write
`.ninja_lock`, or a test fails to start with a sharing violation
(`0xC0000043`). Re-running works; keeping the checkout outside synced folders
avoids it.

| Option | Default | Meaning |
|--------|---------|---------|
| `BETTERCAD_BUILD_GUI` | `AUTO` | `ON` requires Qt 6; `AUTO` builds the desktop app if Qt 6 is found |
| `BETTERCAD_BUILD_CLI` | `ON` | Build `bettercad-cli` |
| `BETTERCAD_BUILD_TESTS` | `ON` (top level) | Build the test suite |
| `BETTERCAD_WARNINGS_AS_ERRORS` | `OFF` (`ON` in presets) | Treat warnings in BetterCAD code as errors |
| `BUILD_SHARED_LIBS` | `OFF` | Build BetterCAD libraries as shared libraries |

Configuring prints a summary of the compiler, build type and enabled components.

## Test

```sh
ctest --preset debug          # or: release, debug-shared
```

All three presets are supported configurations and all three are expected to
pass the complete suite. `debug-shared` links the modules as separate DLLs, so
it exercises export macros and cross-module linkage that a static build cannot
check.

## CLI / Usage

```sh
build/debug/bin/bettercad-cli --version     # bettercad-cli 0.1.0
build/debug/bin/bettercad-cli version       # full build report
build/debug/bin/bettercad                   # desktop application (placeholder shell)
```

```sh
bettercad-cli new part.bcad [--name <name>] [--force]   # empty document
bettercad-cli info examples/models/plate.bcad           # metadata, parameters, objects
bettercad-cli validate examples/models/plate.bcad       # checks; exit status 1 on errors
bettercad-cli export-step examples/models/plate.bcad plate.step
bettercad-cli export-stl examples/models/plate.bcad plate.stl --tolerance 0.05mm [--ascii]
```

- `.bcad` files are transparent JSON; see
  [examples/models/plate.bcad](examples/models/plate.bcad), which was written
  by hand.
- [examples/models/reference/](examples/models/reference/) holds the six
  mechanical reference models, built by
  [examples/reference_models/](examples/reference_models/) through the public
  API alone.
- Exports regenerate the model first and write its result bodies in
  millimetres.
- Exit status: 0 success, 1 failure, 2 invalid command line.

## Repository Structure

```text
apps/bettercad/        Qt desktop application (placeholder shell)
apps/bettercad_cli/    bettercad-cli command-line tool
cmake/                 build-system modules and templates
deps/                  superbuild for Qt and Open CASCADE
docs/architecture.md   the architecture as built
docs/verification/     milestone evidence, one directory per milestone
include/bettercad/     public headers, one directory per module
src/<module>/          implementation and private headers
tests/                 unit, process-level and architecture tests
examples/              sample models and programs
data/                  test data (empty for now)
```

## Verification Philosophy

> Implementation is not completion.

A milestone is complete only when it is implemented, tested, independently
validated where an independent reference exists, pinned by deterministic
regression tests, and supported by recorded evidence. A checkbox in
[TODO.md](TODO.md) is ticked only after all five, and every ticked item links
to the evidence that earned it.

In practice that means analytic validation rather than self-comparison (a
chamfered block's volume is checked against *V*₀ − ½*d*²*L*, not against a
previous run), determinism checked across processes and build configurations
rather than assumed, failure paths tested as carefully as success paths, and
limitations written down rather than left for a user to discover. Where a
qualification cannot speak — one platform, no CI, no sanitizers, no coverage,
no GUI — it says so instead of implying otherwise.

## Project Documents

Five documents govern the project, and each answers one question. If two of
them ever disagree, the table below decides which one is right.

| Question | Authority |
| --- | --- |
| What is BetterCAD? | [README.md](README.md) |
| What are we building long-term? | [ROADMAP.md](ROADMAP.md) |
| What is actually complete? | [TODO.md](TODO.md) + [docs/verification/](docs/verification/) |
| What should be implemented next? | [TODO.md](TODO.md) |
| How must the system be structured? | [ARCHITECTURE.md](ARCHITECTURE.md) |
| How must the work be executed? | [CLAUDE.md](CLAUDE.md) |
| What proves completion? | [docs/verification/](docs/verification/) |

- [ROADMAP.md](ROADMAP.md) — long-term capability direction
- [TODO.md](TODO.md) — authoritative implementation status and next work
- [ARCHITECTURE.md](ARCHITECTURE.md) — system architecture and dependency rules
- [CLAUDE.md](CLAUDE.md) — engineering workflow and verification rules
- [docs/architecture.md](docs/architecture.md) — the architecture as built today
- [docs/verification/](docs/verification/) — evidence for every completed milestone

## Current Limitations

These are properties of the system as qualified. The full list, with the
milestone and regression test pinning each one, is in
[TODO.md](TODO.md#known-limitations) and
[docs/verification/P11-QUAL-001/README.md](docs/verification/P11-QUAL-001/README.md).

### Modeling

- **Geometric references do not follow moved geometry.** Edges and faces are
  matched by their geometry, not named semantically. When a parameter moves the
  edge a chamfer was attached to, the feature fails with `NotFound` and keeps no
  body; no other edge is ever substituted. Semantic topology naming is a later
  capability.
- **Parameter expressions are not evaluated.** Expressions are stored but not
  computed, so a derived dimension needs its own parameter or a sketch that
  builds the relation geometrically.
- **No through-all extrude.** A cut is given a depth.
- **Sketch constraints** cover coincident, horizontal, vertical, parallel,
  perpendicular, distance, radius, equal and fixed. There is no angle, tangent,
  midpoint or symmetry constraint.
- **Uniting a half body with its mirror image** is refused when a half cylinder
  lies on the mirror plane; the kernel's fuse returns a shape its own checker
  rejects, so BetterCAD refuses it rather than building it wrongly.

### Not implemented

- **The desktop application is a placeholder shell.** It builds in all three
  configurations, and no GUI functionality is claimed or qualified. The
  parametric workflow is exercised through the core and the CLI.
- **STEP export only — there is no STEP import.** The kernel-based STEP reader
  in the repository is test-only tooling that reads exports back to check them.
  DXF, IGES and OBJ do not exist.
- **No assemblies, drawings, materials database, simulation, optimization,
  Python API, CAM or AI.** These are roadmap capabilities and none has been
  started.

### Limits of the evidence

- One platform: Windows 11 AMD64, GCC 16.1.0 (MinGW-w64), Open CASCADE 8.0.1,
  Qt 6.11.2.
- No CI. Nothing re-runs the suite automatically; qualification is a deliberate
  act.
- No sanitizers, no coverage, no memory checking, and `.clang-format` is defined
  but not enforced by any check.
- 738 tests passing means 738 tests passed. Where a behaviour has no test, the
  evidence says nothing about it.

## License

Not yet chosen; see [LICENSE](LICENSE). Third-party components keep their own
licenses: Open CASCADE uses LGPL 2.1 with an exception, Qt uses LGPL 3, and
Catch2 uses BSL 1.0.
