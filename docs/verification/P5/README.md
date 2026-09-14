# P5 — Sketch constraints: verification

Date: 2026-09-14. Incremental build of the existing preset trees; every
changed translation unit was recompiled. Raw logs are in this directory.
Toolchain: GCC 16.1.0 (MinGW-w64 UCRT), CMake 4.4.2.

| Preset | Configure | Build (`-Werror`) | `warning:`/`error:` lines | Tests |
|--------|-----------|-------------------|---------------------------|-------|
| debug | exit 0 | exit 0 | 0 | 196/196 passed |
| release | exit 0 | exit 0 | 0 | 196/196 passed |
| debug-shared | exit 0 | exit 0 | 0 | 196/196 passed |

There are 12 new test cases in `tests/sketch/ConstraintTests.cpp`.

## Constraint representation

`Constraint` (`include/bettercad/sketch/Constraints.hpp`) has these fields:

| Field | Meaning |
|-------|---------|
| `id` | `ConstraintId`, per sketch, deterministic, never reused |
| `type` | Coincident, Horizontal, Vertical, Parallel, Perpendicular, Distance, Radius, Equal |
| `entities` | referenced `EntityId`s in the order of the type's signature |
| `value` | `Length`, for Distance and Radius only |
| `parameter` | optional `ParameterId` that drives the value (resolved by regeneration, P8) |
| `enabled` | disabled constraints are kept but ignored by the solver |

Accepted signatures:

| Type | Entities | Value |
|------|----------|-------|
| Coincident | point, point | — |
| Horizontal / Vertical | line, or point + point | — |
| Parallel / Perpendicular | line, line | — |
| Distance | point + point (> 0), line length (> 0), point + line (≥ 0) | required |
| Radius | circle or arc (> 0) | required |
| Equal | line + line, or circle/arc + circle/arc | — |

Tests: `Each constraint type records its references, value and enabled
state` (all 8 types, both Horizontal forms); `Spec setup: rectangle with
horizontal/vertical constraints, width and height`; `Constraint IDs are
stable and never reused` (rejected additions consume no ID); `Point-line
distances are stored as (point, line)`; `Constraints can be disabled,
changed and driven by parameters`; `Constraints are part of the sketch
content`.

## Reference validation

References are checked when a constraint is added or inserted. Entities that
constraints reference cannot be removed, so no constraint can reference a
missing entity.

| Test | Rejected cases |
|------|----------------|
| `Constraints cannot reference entities that do not exist` | 9 additions with missing or invalid IDs → `NotFound`; nothing added |
| `Constraints check the kinds and number of referenced entities` | 14 wrong kinds or arities → `InvalidArgument` |
| `Constraints must reference distinct entities` | 4 repeated references |
| `Only distance and radius constraints carry values, and values are checked` | missing value, value on Horizontal, NaN, zero, negative |
| `Entities referenced by constraints cannot be removed` | `FailedPrecondition` until the constraint is removed |
| `insertConstraint restores a constraint with its ID and validates it` | duplicate ID, dangling reference, missing ID; loaded IDs are reserved |
