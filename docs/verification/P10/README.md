# P10 — Minimal CLI: verification

Date: 2026-09-14. Each preset was configured, built with warnings as errors
and tested with CTest. CTest runs only after a successful build. Raw logs are
in this directory. `cli-transcript.log` records every command run on the
release build.

| Preset | Configure | Build (`-Werror`) | `warning:`/`error:` lines | Tests |
|--------|-----------|-------------------|---------------------------|-------|
| debug | exit 0 | exit 0 | 0 | 301/301 passed |
| release | exit 0 | exit 0 | 0 | 301/301 passed |
| debug-shared | exit 0 | exit 0 | 0 | 301/301 passed |

**An earlier debug-shared run failed for environmental reasons.** That run
came before the last test was added, and its logs are kept as
`*-debug-shared-attempt1.log`. The checkout, and so the build tree, is
inside OneDrive. During that run:

- Ninja printed "WriteFile(.ninja_lock): Unable to create file. Permission
  denied" twice. These were the only `error:` lines. The build still
  compiled all 46 changed objects, and exited 0 with no compiler warnings.
- One test, `unit.C++ standard names map __cplusplus values` (unrelated to
  P10), could not start. It exited with `0xC0000043`
  (`STATUS_SHARING_VIOLATION`): the executable was locked while the sync
  client scanned the new files.

Re-running the unchanged tree gave 300/300. The final run above passed in
full. README.md now notes this hazard.

There are 46 new tests:

- 37 new Catch2 test cases:
  - `MeshTests.cpp` (4)
  - `StlTests.cpp` (5)
  - `StepExportTests.cpp` (7)
  - `ValidationTests.cpp` (10)
  - `ExampleModelTests.cpp` (1)
  - `CliTests.cpp` (10 more)
- 9 new process tests (`cli.*`) that run the real `bettercad-cli`
  executable.

## Design

- **Geometry (OCCT adapter).**
  - `geometry::triangulate(body, options)` returns a neutral `Mesh`. It
    meshes a copy with the kernel's incremental mesher, single-threaded.
  - `geometry::writeStep(bodies, options)` produces AP214 text in mm, with
    one named product per body, through an XCAF document.
  - The kernel's stdout printer is removed once per process, because the
    translator printed statistics into the CLI's output.
- **Features.**
  - `resultFeatures()` returns the feature bodies no other feature consumes.
  - `regenerateResultBodies()` regenerates a copy and returns those bodies.
  - `validateDocument()` runs the six checks the spec lists, each problem
    reported once.
- **io.**
  - `meshesToStl()` writes binary or ASCII STL from `Mesh`.
  - `exportStep()` / `exportStl()` write a document's result bodies.
  - All writers share one atomic-replace, UTF-8-path file routine.
- **CLI.**
  - Commands live in `bettercad_cli_lib`: argument parsing with
    unit-aware quantities, `new`/`info`/`validate`, and the two exports.
  - `wmain` passes the Windows UTF-16 command line on as UTF-8.
  - Exit status is 0 for success, 1 for failure or an invalid document, and
    2 for an invalid command line.
- **Example.** `examples/models/plate.bcad` is a hand-written 100 × 50 × 20
  mm plate with a 20 mm hole, fully constrained and parameter-driven.

## Evidence

### `bettercad-cli new example.bcad`

- **Tests:** `new creates an empty, valid document and refuses to
  overwrite`; process tests `cli.new` and `cli.new.unicode-path`.
- **Result:**
  - The document is named after the file and loads with no objects.
  - `validate` passes on it.
  - A second `new` fails with exit status 1 and leaves the file unchanged.
  - `--force` replaces the file; `--name` sets the name; an empty name is a
    usage error.
  - An unwritable location fails with exit status 1.
  - A file named `Plåt ✓.bcad` works through the real executable.

### `bettercad-cli info example.bcad`

- **Tests:** `info shows metadata, parameters and objects`; process test
  `cli.info.example`.
- **Result:**
  - Shows metadata, parameters (display units, expressions) and one line per
    object: entity and constraint counts, disabled constraints, driving
    parameters, and each extrude's profile, depth, direction and operation.
  - A missing file gives exit status 1.

### `bettercad-cli validate example.bcad`

- **Tests:** `validate reports each check and the result bodies`; the 10
  `[validation]` test cases; process tests `cli.validate.example` and
  `cli.validate.missing-file`.
- **Result:** each check is exercised with a real defect:
  - **document consistency:** a profile that is an extrude, a depth
    parameter that is an angle or a sketch, a target without a body, a
    constraint driven by an angle, and files that do not load;
  - **missing references:** a deleted profile sketch;
  - **dependency cycles:** Pad ↔ Pocket;
  - **invalid sketch constraints:** INCONSISTENT and OVER_CONSTRAINED
    sketches and an invalid driving value (errors), plus under-constrained
    sketches (warnings, with DOF 5/7/6);
  - **failed feature regeneration:** an open profile, and dependents of a
    failure;
  - **invalid geometry:** a cut that removes the entire body.

  Each problem is reported once. Exit status is 0 when valid and 1 with
  errors. The example validates with volume 93716.815 mm³
  (= 100000 − 2000π). Validation does not modify the document.

### `bettercad-cli export-step example.bcad example.step`

- **Tests:** the 7 `[step]` test cases; `export-step writes the result
  bodies`; `Exports report models that do not regenerate`; `Exports of a
  document without bodies fail`; process test `cli.export-step.example`.
- **Result:**
  - The file is ISO 10303-21 / AP214 with `SI_UNIT(.MILLI.,.METRE.)`, and
    has products `Pocket` and `Slot`; the consumed `Pad` is not exported.
  - The header carries the author and BetterCAD as originating system.
  - Read back with the kernel: 2 solids, valid, with volume equal to
    V(Pocket) + V(Slot) within 1e-9.
  - A cylinder reads back with volume and area within 1e-9.
  - A fixed time stamp gives byte-identical output.
  - Names with quotes and non-ASCII characters survive.
  - Failures: no bodies or a model that does not regenerate gives exit
    status 1 with the failed items named; an unwritable path gives
    `IoError`; nothing is written.

### `bettercad-cli export-stl example.bcad example.stl`

- **Tests:** the 5 `[stl]` and 4 `[mesh]` test cases; `export-stl writes a
  closed mesh with the requested accuracy`; `export-stl validates its
  options`; process tests `cli.export-stl.example`, `cli.export-stl.ascii`
  and `cli.export-stl.bad-tolerance`.
- **Result:** files are checked by an independent STL parser, with
  enclosed volume from the divergence theorem and an edge-manifold check:
  - A box meshes into 12 triangles, watertight, with exact volume and area
    (1e-12).
  - For cylinders at deflections 0.1 / 0.01 / 0.001 mm: watertight, every
    vertex on the true surface, and a volume deficit ≤ deflection × area.
  - Meshing is repeatable after a finer request (no hidden caching).
  - The bracket exports two watertight solids in both formats. Their
    volumes are within deflection × area of the exact solids, and within
    1e-3 of the analytic volumes.
  - ASCII and binary files hold identical 32-bit coordinates.
  - A finer `--tolerance 0.0004in --angle=0.1rad` gives more triangles and
    a smaller volume error.
  - Bad options (`1kg`, `abc`, unknown unit, `5mm` for an angle, repeated
    or valued flags) give exit status 2.

## Fixes found while testing

- **Kernel statistics on stdout.** OCCT's STEP translator printed "Statistics
  on Transfer" to stdout, which would corrupt the CLI's output. The kernel
  adapter now removes the kernel's stdout printer (`occt::initializeSession`).
- **Noisy bounds.** The kernel's optimal bounding box reports
  `-1.48564e-15` for an exact 0. `validate` now prints bounds to the
  micrometre, without negative zero.
- **Test-parser bug.** The first version of the test-side ASCII STL parser
  expected a trailing empty line that its own line splitter never produced.
  Only the test tool was at fault; the STL output was correct.
