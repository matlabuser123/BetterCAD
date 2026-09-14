# P9 — Persistence: verification

Date: 2026-09-14. Each preset was configured, built with warnings as errors
and tested with CTest. CTest runs only after a successful build. Raw logs are
in this directory.

| Preset | Configure | Build (`-Werror`) | `warning:`/`error:` lines | Tests |
|--------|-----------|-------------------|---------------------------|-------|
| debug | exit 0 | exit 0 | 0 | 255/255 passed |
| release | exit 0 | exit 0 | 0 | 255/255 passed |
| debug-shared | exit 0 | exit 0 | 0 | 255/255 passed |

There are 10 new test cases: 9 in `tests/io/DocumentFileTests.cpp` and 1 in
`tests/io/ParameterJsonTests.cpp`.

## Design

- **API** (`bettercad/io/DocumentFile.hpp`):
  - `documentToJson` / `documentFromJson` convert between a document and
    text.
  - `saveDocument` / `loadDocument` work with files. The native extension is
    `.bcad`.
- **Format.** The file is transparent JSON; a complete example is the golden
  text in `The document format is transparent JSON`.
  - Root: `format` `"bettercad-document"`, `version` 1, `units` `"SI"`,
    `document` (UUID, name, metadata, `last_allocated_id`), `parameters`,
    `objects`.
  - Each object is `{id, type, name, data}`:
    - A `sketch` stores its placement frame, its entities and constraints
      (each with its own ID), and the counters `last_entity_id` and
      `last_constraint_id`.
    - An `extrude` stores `profile`, `depth`, optional `depth_parameter`,
      `direction`, `operation` and optional `target`.
- **What is preserved:**
  - IDs of every parameter, object, entity and constraint;
  - the ID counters, so deleted IDs are never reused;
  - parameters, with unit, dimension and expression;
  - sketches, including construction flags and disabled constraints;
  - constraints and their driving parameters;
  - features;
  - dependency relationships (stored as ID references);
  - document identity and metadata.
- **Geometry is not stored.** It is regenerated after loading. The solved
  sketch state is saved, so regeneration reproduces the saved geometry bit
  for bit.
- **Loading is strict.** Unknown fields, wrong types, unknown kinds or enum
  values, duplicate IDs or names, and invalid values are all errors. The
  message names the JSON path, e.g. `objects[0].data.entities[1].radius:
  expected a number`.
  - Within a sketch, references must resolve.
  - A reference between items that points at nothing (e.g. a deleted
    profile) is a legitimate document state. It loads, and regeneration
    reports it.
  - Counters below an ID in use are rejected. IDs above 2^53 − 1 are
    rejected, which keeps files JSON-safe and the ID space far from
    exhaustion.
- **Saving:**
  - Output is deterministic.
  - An object kind with no serializer fails with `InvalidArgument` instead
    of being dropped.
  - Text that is not valid UTF-8 fails with `InvalidArgument`.
  - Files are written atomically: a sibling `.tmp` file is renamed over the
    target.

## Evidence

| Requirement | Test | Result |
|-------------|------|--------|
| **Acceptance:** create → save → close → load → regenerate gives equivalent geometry and IDs | `P9 acceptance: create, save, close, load and regenerate give the same geometry and IDs` | pass (details below) |
| Save (deterministic, exact, transparent) | `Saving is deterministic and loss-free`; `The document format is transparent JSON` | pass (details below) |
| Load (strict, with paths) | `Invalid document files are rejected with the JSON path`; `Sketch entities may appear in any order in the file` | pass (details below) |
| Round-trip regression | `A model saved before its first regeneration regenerates identically after loading`; `Every extrude direction and operation round-trips` (12 combinations); `References to missing items are kept and reported by regeneration` | pass (details below) |
| Files | `Documents are saved to and loaded from files` | pass (details below) |

**Acceptance.** The test model is a bracket with three features:

- a Pad from a 100 × 60 rectangle with an 8 mm hole, driven by parameters;
- a Pocket cut, sketched on a plane facing −Z;
- a Slot from arcs and lines on a tilted plane, extruded symmetrically.

The model also has an angle parameter, an expression, metadata, a
construction arc, a disabled constraint, all nine constraint types, and
deleted items at the document and sketch levels.

The test builds the model, regenerates it and saves it, then destroys the
original document and loads the file. The loaded document is `equivalent`
and has the same item IDs. The dependency graph has the same edges and the
same topological order.

A fresh regeneration of the loaded document:

- rebuilds the same six objects in the same order;
- gives bit-identical volume, area, centre of mass, bounding box and
  topology for all three features;
- matches the analytic volumes within 1e-12.

New items continue at the next ID: document ID 15, entity 16, constraint 12.

**Save.**

- Saving twice gives identical bytes, and save → load → save gives
  identical bytes.
- Solved point coordinates and placement components survive bit for bit.
- A small document serializes exactly to the golden JSON text.

**Load.** 36 malformed variants of the golden file are rejected with the
expected code and path. They cover:

- syntax errors, truncation and number overflow;
- format, version and units;
- missing, unknown and mistyped fields;
- unknown kinds;
- invalid UUID, duplicate, zero, negative and too-large IDs;
- counters below IDs in use;
- duplicate and invalid names;
- dangling in-sketch references, negative radius and invalid constraint
  targets;
- non-unit or left-handed frames;
- invalid extrude definitions.

Entities stored before the points they reference still load.

**Round-trip regression.**

- A document saved before its first regeneration regenerates to the same
  bit-identical geometry as the original, and stays editable: changing
  `width` rebuilds Base → Pad → Pocket.
- Every direction × operation pair round-trips.
- A dangling profile reference survives and is reported as `NotFound` by
  regeneration.

**Files.**

- A saved file equals the golden text, and no temporary file is left behind.
- Saving does not mark the document clean; a loaded document is clean.
- An existing longer file is fully replaced.
- Non-ASCII file names work.
- A missing file or a directory gives `NotFound`; an unwritable location
  gives `IoError`.
- Errors in a corrupt file name the file and the JSON path.
- Unsupported object kinds and non-UTF-8 text are refused, and nothing is
  written.

## Fixes found while writing the tests

- **Exceptions escaped the Result API.** nlohmann/json reports number
  overflow (`1e400`) as `out_of_range` and invalid UTF-8 on output as
  `type_error`. Neither was caught, so either would have escaped as an
  exception. Parsing now catches every JSON exception. Output goes through
  `dumpJson`, which returns `InvalidArgument`. For the same reason,
  `parametersToJson` now returns `Result<std::string>`.
- **Document names were not validated on load.** The document constructor
  does not validate names, so loading now sets the name through
  `setName()`, which does.
- **The first test fixture was locally over-constrained.** It put an Equal
  constraint on the slot's arcs. The P6 rank analysis correctly reported it
  as redundant, since the parallel lines already fix both radii to first
  order. The fixture now uses a Radius constraint there, and Equal on a
  square pocket.

## Other changes

- `requireReport` and `volumeMm3` moved into `tests/features/FeatureTestSupport.hpp`
  for reuse.
