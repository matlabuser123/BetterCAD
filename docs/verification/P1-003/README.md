# P1-003 — Parameter system: verification

Date: 2026-09-14. Incremental build of the existing preset trees; every
changed translation unit was recompiled. Raw logs are in this directory.
Toolchain: GCC 16.1.0 (MinGW-w64 UCRT), CMake 4.4.2.

| Preset | Configure | Build (`-Werror`) | `warning:`/`error:` lines | Tests |
|--------|-----------|-------------------|---------------------------|-------|
| debug | exit 0 | exit 0 | 0 | 105/105 passed |
| release | exit 0 | exit 0 | 0 | 105/105 passed |
| debug-shared | exit 0 | exit 0 | 0 | 105/105 passed |

A first, looser scan (`warning|error`, case-insensitive) flagged one line per
log. Each was the build line for the source file `Error.cpp`, not a
diagnostic. The table uses the exact GCC/linker markers.

## What was implemented

- `Result<T>` = `std::expected<T, Error>` with a broad `ErrorCode` category
  and a message (`include/bettercad/core/Error.hpp`). It reports
  recoverable failures.
- `Parameter` (`include/bettercad/core/parameters/Parameter.hpp`). Fields:
  stable `ParameterId`, identifier name, SI value, display unit (which fixes
  the dimension), optional expression (stored, not evaluated), and a
  revision counter. Values must be finite. Display units must come from the
  unit catalog.
- `ParameterTable`: parameters keyed by ID with unique names. It exposes
  parameters only as const, so every change goes through the table and keeps
  its invariants. It has a table-level revision and lists parameters in ID
  order.
- `bettercad_io` library (new, layer 3): JSON serialization
  (`include/bettercad/io/ParameterJson.hpp`) using nlohmann/json 3.12.0 as a
  private dependency. It uses strict, path-aware validation.

## Acceptance criteria

| Criterion | Tests |
|-----------|-------|
| Parameter creation works | `Parameters are created with ID, name, value, unit and revision 1`; `Spec examples: width, height and hole diameter`; `Parameters of other dimensions keep their dimension and unit`; `Parameters can be created from a number and a runtime unit`; `Invalid parameters are rejected at creation`; `Parameter names must be identifiers` |
| Parameter modification works | `Changing the value updates it and increments the revision`; `Setting an identical value is not a change`; `Expressions are stored and cleared`; `Renaming validates the new name`; table: `Table modifications are dimension-checked`, `The table revision tracks effective changes only`, `Renaming keeps names unique...`, `Removing returns the parameter and frees its name` |
| Units are preserved | `The display unit can change without changing the value`; value set in inches keeps display unit `mm`; `Round trip preserves IDs, names, dimensions, units and expressions` |
| Invalid dimension assignment fails | `Assigning a value of another dimension fails and changes nothing` (value, runtime unit and display unit; state and revision unchanged); `Reading a parameter as the wrong quantity type fails`; `Table modifications are dimension-checked` |
| Serialization round-trip works | `Parameter tables round-trip through JSON` (equivalent, byte-identical re-serialization); `SI values round-trip bit for bit` (9 values incl. `-0.0`, `5e-324`, `DBL_MAX`, `0.1+0.2`); `An empty table round-trips`; `The JSON format is transparent` (exact expected text); `Malformed parameter documents are rejected with a path` (16 malformed inputs) |

## File format example

Exact output checked by `The JSON format is transparent`:

```json
{
  "format": "bettercad-parameters",
  "version": 1,
  "parameters": [
    {
      "id": 1,
      "name": "width",
      "si_value": 0.1,
      "unit": "mm",
      "dimension": {
        "length": 1
      }
    }
  ]
}
```

## Design notes

- The SI value is the persisted source of truth, so loading reproduces the
  exact double (see P1-001: display values do not always round-trip
  bit-exactly through a unit conversion).
- The dimension is written alongside the unit as a consistency check. A
  disagreement is reported as `DimensionMismatch`, not silently resolved.
- Revision counters are runtime change tracking and are not persisted;
  `equivalent()` compares content and ignores them.
- Serialization lives in `bettercad_io`, not in core, so core has no
  file-format dependency. P9's document format will reuse the private JSON
  helpers (`src/io/json/`).
