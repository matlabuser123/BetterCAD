# P18-BC-001 — scope

```text
SUBJECT:  what this milestone implements and what it does not, with the
          evidence for each "not"
```

## Implemented

| Capability | Where |
| --- | --- |
| resolve a `FaceName` to current facets, unique nodes and unknowns | `ThermalBoundary.cpp` (`uniqueFacetDofs`) over `ThermalTarget` |
| prescribed temperature as an exact, attributed constraint map | `resolveTemperatureConstraints` over P18-DOF-001 |
| conflict detection with a structured, order-independent report | `TemperatureConflict` |
| the consistent Robin facet matrix and source, with units and sign | `ThermalConvection.cpp` |
| per-facet convection terms bound to unknowns in the facet's own order | `integrateConvection` |
| one atomic prepared value with a value-typed source and stale reasons | `PreparedThermalBoundary` |

## NOT implemented, and the evidence

| Not implemented | Evidence |
| --- | --- |
| global thermal assembly | no global matrix or scatter exists; the test-side `Dense` in the test support header is the only accumulation, and it lives in `tests/`; compile-fail THE_PREPARED_BOUNDARY_HAS_NO_STIFFNESS_MATRIX; source audit |
| thermal solve | no solver call, no reduced system; compile-fail ..._HAS_NO_SOLUTION; source audit |
| penalty Dirichlet enforcement | no diagonal or right-hand side is touched for a constraint; compile-fail ..._HAS_NO_PENALTY; scope test |
| radiation | no `T^4`, no emissivity; compile-fail ..._HAS_NO_RADIATION; source audit |
| time-varying `T_bar`, `h` or `T_inf` | the canonical record holds three constants; deferred to P18-TRANSIENT-001 |
| thermal contact | no interface, no contact conductance; source audit |
| result fields | no temperature, flux or balance is computed or stored |
| convective heat from a solved field | P18-BALANCE-001 |
| persistence | nothing prepared is written anywhere; the audit in GEOMETRY_REFERENCE.md |
| a CLI or viewer change | none |

## Unchanged

No file under `src/core`, `src/features`, `src/meshing`, `src/structural` or `apps/`
changed. `ThermalData.hpp`, `ThermalDof.hpp`, `ThermalLoadVector.hpp` and
`ThermalTarget.hpp` are consumed as they were qualified. The only existing files
touched are the three CMake lists that register new sources and tests, and the
zero-match guard.

## The orchestration policy stated for the later milestones

A prepared boundary depends on the canonical **conditions**, the mesh and the
geometry, and on nothing else. P18-ASSEMBLY-001 must call `requireCurrent` before
scattering and must treat a stale boundary exactly as it treats a stale heat
input. A material edit stales the thermal result and must not stale this value or
remesh P16; a load, solver or transient edit likewise. If a later milestone builds
one combined analysis snapshot it may be coarser than this source but not finer.
