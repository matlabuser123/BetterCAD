# BetterCAD

BetterCAD is a parametric engineering CAD platform under early development.
The long-term goal is a modern alternative to traditional mechanical CAD:
parametric part modelling, assemblies, simulation, automation and semantic
versioning.

Development proceeds in small, verified milestones. See [TODO.md](TODO.md)
for status; an item is ticked only when it is implemented and covered by
passing tests. Architecture and rules are in
[docs/architecture.md](docs/architecture.md).

## Requirements

- CMake 3.25 or newer, Ninja
- A C++23 compiler with `<format>`: GCC 13+, Clang 17+ or MSVC 19.37+
- For the desktop application: Qt 6.5+ (Widgets)
- From milestone P3 on: Open CASCADE Technology 8.0

Tested toolchain: GCC 16.1 (WinLibs MinGW-w64, UCRT) with CMake 4.4 on
Windows 11.

## Building

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
ctest --preset debug
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

## Running

```sh
build/debug/bin/bettercad-cli --version     # bettercad-cli 0.1.0
build/debug/bin/bettercad-cli version       # full build report
build/debug/bin/bettercad                   # desktop application (placeholder)
```

### Command-line tool

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
- [examples/models/reference/](examples/models/reference/) holds six
  mechanical parts — a stepped shaft, a bolted flange, a V-belt pulley, a
  bearing housing, a mounting bracket and a U-bolt — built by
  [examples/reference_models/](examples/reference_models/) through the public
  API alone. Every dimension of each is checked against geometry computed
  independently from its parameters
  ([evidence](docs/verification/P11-REF-001/README.md)).
- Exports regenerate the model first and write its result bodies in
  millimetres.
- Exit status: 0 success, 1 failure, 2 invalid command line.

## Repository layout

```text
apps/bettercad/        Qt desktop application
apps/bettercad_cli/    bettercad-cli command-line tool
cmake/                 build-system modules and templates
deps/                  superbuild for Qt and Open CASCADE
docs/                  architecture notes and verification records
include/bettercad/     public headers, one directory per module
src/<module>/          implementation and private headers
tests/                 unit, process-level and architecture tests
examples/              sample models and programs
data/                  test data (empty for now)
```

## License

Not yet chosen; see [LICENSE](LICENSE). Third-party components keep their own
licenses: Open CASCADE uses LGPL 2.1 with an exception, Qt uses LGPL 3, and
Catch2 uses BSL 1.0.
