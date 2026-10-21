# P18-POST-001 — scope audit

```text
SUBJECT:  what this milestone implemented, and the things it was told not to
```

## Implemented

| Item | Where |
| --- | --- |
| nodal temperature recovery (K, the solver's value, absolute-temperature rule) | `ThermalPostState::temperatures`, `checkTemperatures` |
| temperature magnitude/value (the scalar `T`; no vector magnitude of a scalar) | `descriptorOf(Temperature)`, 1 component |
| element temperature gradient `grad(T) = G Te` through P18-ELEM's path | `fieldsFrom` |
| conductive heat-flux vector `q = -k grad(T)` | `fieldsFrom` |
| heat-flux magnitude `|q|` | `HeatFlux3::magnitude` |
| component order `[x, y, z]`; sign convention (flow toward lower temperature) | UNITS_AND_COMPONENTS.md, FOURIER_LAW.md |
| explicit units (K, K/m, W/m², W/m²) | `descriptorOf`, `static_assert`s |
| all results finite (inputs, gradient, flux, magnitude) | `PostProblem`, 11 stable names |
| constant-gradient patch validation | PATCH_TEST.md |
| independent Fourier-law validation (slab, both signs, three meshes, steady and transient) | SLAB_VALIDATION.md |
| deterministic result ordering | DETERMINISM.md |
| no GUI recomputation | GUI_OWNERSHIP.md, `CheckRecoveryOwnership.cmake` |
| source-bound, current-or-refused recovery for steady and transient | CURRENTNESS.md |

## Not implemented, by instruction (each has a test or a compile-fail case that says so)

| Not here | Belongs to | Evidence of absence |
| --- | --- | --- |
| heat balance, residual of the energy, reactions | P18-BALANCE-001 | compile-fail `THE_RESULT_HAS_NO_HEAT_BALANCE`, `…NO_REACTIONS` |
| boundary flux, `q . n`, convective boundary heat | P18-BALANCE-001 | compile-fail `THE_RESULT_HAS_NO_BOUNDARY_FLUX`, `…NO_CONVECTIVE_FLUX` |
| storage / stored-energy rate, `dT/dt` | P18-BALANCE-001 | compile-fail `THE_RESULT_HAS_NO_STORAGE_FLUX`, `…NO_TEMPERATURE_RATE` |
| nodal heat-flux smoothing, patch recovery, nodal averaging | P18-VIZ-001 (display only, if ever) | compile-fail `…NO_NODAL_HEAT_FLUX`, `…NO_SMOOTHED_FIELD`; PM32; source audit |
| gradient magnitude as a field | not requested | compile-fail `THE_RESULT_HAS_NO_GRADIENT_MAGNITUDE` |
| Celsius / any unit conversion | never in the core | compile-fail `THE_RESULT_HAS_NO_CELSIUS`; PM09 |
| colour map, component selection, range, legend, clipping | P18-VIZ-001 | compile-fail `THE_SOURCE_HAS_NO_COLOUR_MAP`, `…NO_COMPONENT_SELECTION`, `A_FIELD_DESCRIPTOR_HAS_NO_COLOUR` |
| commands, undo, CLI | P18-CMD-001 | no command type is added; the layering checker shows no new dependency (the count is in FREEZE.md) |
| persistence of any recovered value | P18-PERSIST-001 | no serialisation symbol; RESULT_MODEL.md |
| the validation suite (cross-solver, manufactured solutions) | P18-VALID-001 | only the oracles listed in the brief |
| anisotropic conductivity, `k(T)` | outside ADR-049 | `k` is one `ThermalConductivity` |
| a change to any qualified module | — | FREEZE.md: the untouched files are byte-identical to HEAD |

## What changed outside the new files

```text
src/thermal/CMakeLists.txt                 + ThermalPost.cpp
tests/CMakeLists.txt                       + ThermalPostTests.cpp, ThermalPostMappedTests.cpp
tests/compile_fail/CMakeLists.txt          + group thermalpost (31 cases)
tests/architecture/CMakeLists.txt          + 4 tests (recovery ownership)
tests/cli/ZeroMatchGuard.cmake             + 13 filters
```

New files: `include/bettercad/thermal/ThermalPost.hpp`, `src/thermal/ThermalPost.cpp`, `tests/thermal/ThermalPost{Tests,MappedTests,TestSupport}`,
`tests/compile_fail/ThermalPostMisuse.cpp`, `tests/architecture/CheckRecoveryOwnership.cmake` and three fixtures.

## Layering

`thermal` (layer 45) includes core, features, meshing and linalg only. `ThermalPost.cpp` adds no include outside what the thermal module already used; the checker
reports no violation (FREEZE.md carries the count).
