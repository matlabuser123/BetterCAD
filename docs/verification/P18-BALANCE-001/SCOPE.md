# P18-BALANCE-001 — scope audit

```text
SUBJECT:  what this milestone implemented, and the things it was told not to
```

## Implemented (the fourteen checklist items)

| Checklist item | Where |
| --- | --- |
| Define heat-flow sign convention | SIGN_CONVENTION.md; P18-DATA-001's `HeatBalance`, positive INTO the domain; `ReactionSignAnalytical` |
| Recover prescribed-temperature reaction heat flow | REACTION_HEAT.md; `steadyFullResidual` / `transientFullResidual` / `reactionHeats`; one reaction per unique prescribed unknown |
| Sum applied heat flux | `PreparedThermalLoads` contributions, surface kind, per load and total |
| Sum volumetric heat generation | the same, volumetric kind |
| Sum convection heat transfer | CONVECTION_ACCOUNTING.md; `Kconv T - Qconv` from the prepared facets at the solved temperature |
| Validate steady heat balance | STEADY_BALANCE.md; hand networks and real meshes |
| Validate transient stored-energy balance | TRANSIENT_ENERGY.md; per backward-Euler step and over the run |
| Define tolerances from measurement | TOLERANCE_MEASUREMENT.md; one scale-aware policy, three constants, pinned |
| Multiple thermal boundaries | `Multiple…` tests, hand and real; two caps, two convections, four loads |
| Deterministic aggregation | DETERMINISM.md |
| Analytical reference cases | ANALYTICAL_ORACLES.md |
| Adversarial review PASS | ADVERSARIAL_REVIEW.md |
| Regression PASS | README.md |
| Evidence recorded | this directory |

## Not implemented, by instruction (each has a test or a compile-fail case that says so)

| Not here | Belongs to | Evidence of absence |
| --- | --- | --- |
| overall acceptance of an analysis, qualification status | P18-VALID-001 | `status` is a numerical self-consistency; compile-fail `THE_RESULT_HAS_NO_VALIDATION_VERDICT` |
| any GUI, colour, range, component choice | P18-VIZ-001 | compile-fail `THE_SOURCE_HAS_NO_COLOUR_MAP`, `…NO_COMPONENT_SELECTION`; ownership gate |
| persistence of a balance | P18-PERSIST-001 | compile-fail `THE_RESULT_HAS_NO_PERSISTENCE`; no serialisation symbol |
| a command or a CLI verb | P18-CMD-001 | no command type; no dependency added (layering checker) |
| a remainder reaction, or a reaction per boundary condition | not specified; ambiguous | compile-fail `THE_RESULT_HAS_NO_REMAINDER_REACTION`, `…PER_CONDITION_REACTION`; mutation BAL02 killed |
| re-integration of the transient history | never | compile-fail `THE_RESULT_HAS_NO_TIME_INTEGRATION`; ownership rule 2; `step_data_missing` instead |
| lumped capacity as an option | not specified | compile-fail `THE_ALGEBRA_HAS_NO_LUMPING`; BAL11, BAL12 killed |
| time-dependent loads or conditions | P18-TRANSIENT refuses them | compile-fail `THE_ALGEBRA_HAS_NO_TIME_FUNCTION` |
| gradient, flux, Celsius | P18-POST-001 / never in the core | compile-fail `THE_RESULT_HAS_NO_HEAT_FLUX`, `…NO_GRADIENT`, `…NO_CELSIUS` |
| a `ThermalResult` | a later milestone assembles it from the post result and this balance | compile-fail `A_BALANCE_RESULT_IS_NOT_A_THERMAL_RESULT` |
| a change to any qualified module | — | FREEZE.md: the untouched files are byte-identical to HEAD |
| the balance as canonical authority | never | derived; not persisted; not in the command history |

## What changed outside the new files

```text
src/thermal/CMakeLists.txt                 + ThermalBalance.cpp
tests/CMakeLists.txt                       + ThermalBalanceTests.cpp, ThermalBalanceMappedTests.cpp, ThermalBalanceTestSupport.hpp
tests/compile_fail/CMakeLists.txt          + group thermalbalance (33 cases)
tests/architecture/CMakeLists.txt          + 5 tests (balance ownership)
tests/cli/ZeroMatchGuard.cmake             + 17 filters
```

New files: `include/bettercad/thermal/ThermalBalance.hpp`, `src/thermal/ThermalBalance.cpp`, `tests/thermal/ThermalBalance{Tests,MappedTests,TestSupport}`,
`tests/compile_fail/ThermalBalanceMisuse.cpp`, `tests/architecture/CheckBalanceOwnership.cmake` and four fixtures.

No predecessor's derived-result interface was extended: P18-LOAD-001 already kept surface and generation apart, P18-BC-001 already published `residualFrom` and the prepared convection conditions, and the
transient history already carries the grid (ATTRIBUTION.md, TRANSIENT_ENERGY.md).

## Layering

`thermal` (layer 45) includes core, features, meshing and linalg only; `ThermalBalance` adds no include outside the thermal module's own headers. The checker's count is in FREEZE.md.
