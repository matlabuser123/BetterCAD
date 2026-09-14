# P1-002 — Strong IDs: verification

Date: 2026-09-14. Incremental build of the existing preset trees; every
changed translation unit was recompiled. Raw logs are in this directory.
Toolchain: GCC 16.1.0 (MinGW-w64 UCRT), CMake 4.4.2.

| Preset | Configure | Build (`-Werror`) | Warnings/errors in build log | Tests |
|--------|-----------|-------------------|------------------------------|-------|
| debug | exit 0 | exit 0 | 0 | 76/76 passed |
| release | exit 0 | exit 0 | 0 | 76/76 passed |
| debug-shared | exit 0 | exit 0 | 0 | 76/76 passed |

## What was implemented

- `Id<Tag, Value = std::uint64_t>` (`include/bettercad/core/Id.hpp`): a
  strongly typed identifier. Default-constructed means invalid. It has no
  conversion to or from integers or between kinds. It supports ordering,
  hashing and diagnostic formatting (`sketch:12`).
- ID types: `DocumentId` (UUID), `ObjectId`, `SketchId`, `FeatureId`,
  `ParameterId`, `BodyId`, `EntityId`, `ConstraintId`, `FaceId`, `EdgeId`,
  `VertexId`.
- Document object IDs (`SketchId`, `FeatureId`, `ParameterId`, `BodyId`)
  share the object ID space and widen implicitly to `ObjectId`. Narrowing is
  impossible at the type level; the document will offer checked narrowing
  (P2). Sketch-scoped IDs (`EntityId`, `ConstraintId`) and topology IDs do not
  widen.
- `IdAllocator`: monotonic, starts at 1, never reuses values, and
  `reserveThrough()` supports loading existing IDs. It throws on exhaustion
  instead of wrapping.
- `Uuid` (`include/bettercad/core/Uuid.hpp`): RFC 9562 version 4 from OS
  entropy, or from a caller-supplied engine for reproducible tests. Parses
  and formats the canonical form; hashable and formattable.

## Evidence

- `IdTests.cpp`: 30 `static_assert`s on type safety (distinct kinds, no
  integer conversion, widening only for document objects), plus 10 runtime
  test cases covering validity, comparison, container keys, formatting, and
  the allocator's monotonicity, non-reuse, reservation and exhaustion.
- `UuidTests.cpp`: 6 test cases covering nil, version and variant bits,
  uniqueness of 1000 generated values, seeded reproducibility (64- and 32-bit
  engines), text round trip, case handling, and rejection of 7 malformed
  inputs.
- Build-failure tests `compile_fail.ids.*`: `IdsMisuse.cpp` builds without
  any misuse as the control. GCC 16.1 diagnostics from the test output
  (`{aka ...}` omitted):

```text
IdsMisuse.cpp:22:18: error: could not convert 'sketch' from 'Id<bettercad::SketchIdTag>' to 'Id<bettercad::FeatureIdTag>'
IdsMisuse.cpp:24:45: error: conversion from 'int' to non-scalar type 'bettercad::SketchId' requested
IdsMisuse.cpp:26:44: error: cannot convert 'const bettercad::SketchId' to 'uint64_t' in initialization
IdsMisuse.cpp:28:41: error: no match for 'operator==' ... (operand types are 'const bettercad::SketchId' and 'bettercad::Id<bettercad::FeatureIdTag>')
IdsMisuse.cpp:30:61: error: conversion from 'Id<bettercad::ObjectIdTag>' to non-scalar type 'Id<bettercad::SketchIdTag>' requested
IdsMisuse.cpp:32:36: error: could not convert '...EntityIdTag>::fromValue(1)' from 'Id<bettercad::EntityIdTag>' to 'Id<bettercad::ObjectIdTag>'
```

## Findings during implementation

- `-Wshadow` flagged a local named `hash` inside `std::hash<Uuid>`, which
  shadows the injected class name. It was renamed.
