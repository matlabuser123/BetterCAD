# P1-001 — Unit-safe quantities: verification

Date: 2026-09-14. Clean run of all presets; raw logs are in this directory
(`configure-*.log`, `build-*.log`, `ctest-*.log`). Toolchain as in
[P0-001](../P0-001/README.md): GCC 16.1.0 (MinGW-w64 UCRT), CMake 4.4.2.

| Preset | Configure | Build (`-Werror`) | Warnings/errors in build log | Tests |
|--------|-----------|-------------------|------------------------------|-------|
| debug | exit 0 | exit 0 | 0 | 54/54 passed |
| release | exit 0 | exit 0 | 0 | 54/54 passed |
| debug-shared | exit 0 | exit 0 | 0 | 54/54 passed |

## What was implemented

- `Dimension`: integer exponents of length, mass, time, temperature and angle.
  It is a structural type used as a template argument. Angle is its own base
  dimension.
- `Quantity<D>`: stores the value in coherent SI units (m, kg, s, K, rad). It
  has no implicit conversion from or to `double` and no conversion between
  dimensions. Same-dimension `+ - == <=>` are hidden friends, and `* /` derive
  new dimensions. A fully cancelled result is `double`.
- Aliases: `Length Area Volume Angle Mass Time Temperature Velocity
  Acceleration Force Pressure Density`.
- 37 units in `bettercad::units`. Each has an exact rational SI factor
  (`mm` = 1/1000, `in` = 254/10000), so a single conversion is one correctly
  rounded operation. Every unit has integer and floating-point literals in
  `bettercad::literals` (`100_mm`, `45_deg`, `2.5_MPa`).
- A runtime unit catalog (`unitCatalog()`, `findUnit("mm")`), for use by
  parameters and serialization.
- Formatting: `siUnitSymbol`, `toString`, `std::formatter`, `operator<<`.
- Helpers: `abs`, `sqrt` (only for even exponents), `isFinite`, `approxEqual`,
  angle trigonometry (`sin cos tan arcsin arccos arctan atan2`).

## Required coverage

| Requirement | Tests |
|-------------|-------|
| mm ↔ m | `Millimetres and metres convert to and from SI` (exact single conversions, 7-value round trip) |
| deg ↔ rad | `Degrees and radians convert to and from SI` (π, π/2, 2π, 7-value round trip within 2 ULP) |
| MPa ↔ Pa | `Megapascals and pascals convert to and from SI` (exact) |
| Basic arithmetic | `Like quantities add and subtract`, `Quantities scale by plain numbers`, `Compound assignment updates the quantity`, `Like quantities compare by value...`, `Products and quotients derive the correct quantity` |
| Invalid dimension mixing rejected at compile time | 30+ `static_assert`s in `tests/core/units/QuantityTests.cpp`, plus 7 build-failure tests `compile_fail.units.*` |
| Spec usage (`Length width = 100_mm;` ...) | `Spec usage: literals construct typed quantities` |

## Compile-time rejection evidence

Each `compile_fail.units.*` test compiles `tests/compile_fail/UnitsMisuse.cpp`
with one misuse enabled and requires the build to fail with a specific
diagnostic. The same file without any misuse is part of the normal build
(`bettercad_cf_units_control`), which proves every failure comes from the
selected line. GCC 16.1 diagnostics, taken from the test output:

```text
UnitsMisuse.cpp:15:41: error: no match for 'operator+' ... (operand types are 'bettercad::Length' ... and 'bettercad::Angle' ...)
UnitsMisuse.cpp:17:14: error: no match for 'operator=' ... (operand types are 'bettercad::Length' ... and 'bettercad::Mass' ...)
UnitsMisuse.cpp:19:14: error: no match for 'operator=' in 'length = 5.0e+0' (operand types are 'bettercad::Length' ... and 'double')
UnitsMisuse.cpp:21:37: error: cannot convert 'bettercad::Length' ... to 'double' in initialization
UnitsMisuse.cpp:23:46: error: conversion from 'Quantity<bettercad::Dimension{0, 1, -2, 0, 0}>' to non-scalar type 'Quantity<bettercad::Dimension{-1, 1, -2, 0, 0}>' requested
UnitsMisuse.cpp:25:39: error: no match for 'operator<' ... (operand types are 'bettercad::Length' ... and 'bettercad::Time' ...)
UnitsMisuse.cpp:27:49: error: no matching function for call to 'sqrt(bettercad::Volume)'
```

## Findings during implementation

- **`asin`/`acos`/`atan` collided with the C library.** The first version
  declared `Angle asin(double)`, which is ambiguous with `::asin(double)`
  wherever both are visible. The unit tests caught it. The inverse functions
  are now `arcsin`/`arccos`/`arctan`.
- **Floating-point literals may round twice.** A floating-point literal
  reaches the literal operator as `long double` (80-bit on MinGW). Narrowing
  to `double` can therefore differ by 1 ULP from the plain `double` literal.
  Integer literals and exactly representable decimals are unaffected. Tests of
  non-representable decimals compare within 1 ULP.
- **SI storage makes millimetre arithmetic inexact.** Storing SI values means
  `100_mm + 50_mm` is 150 mm within 1 ULP, not bit-exactly. Tests use ULP or
  relative tolerances for arithmetic results. Persistence (P1-003/P9) must
  store the SI value exactly (shortest round-trip decimal) rather than
  re-deriving it from a display value.
