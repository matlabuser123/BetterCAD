# P18-BALANCE-001 — Heat / Energy Balance

```text
MILESTONE:  P18-BALANCE-001
SUBJECT:    the global heat accounting of an accepted thermal solution: applied surface heat, volumetric generation, convective exchange, the
            reaction heat at every prescribed temperature and, for a transient, the stored energy of every step; the residual of the books,
            measured against a scale-aware tolerance; positive heat INTO the domain; atomic, bound to the solution it came from, refused when
            that solution is stale, deterministic, and derived only
RESULT:     PASSED 2026-10-22, qualified in three presets, 4927 of 4927 each
```

## Baseline

```text
HEAD                 fa15d36f257f698683a2d52fc4f9dcd5f8b7b684   (P18-POST-001's record commit)
origin/main          fa15d36f...  (pushed and verified by the owner before this milestone began)
working tree         clean apart from the session's untracked .claude/
predecessor suite    4820 of 4820, three presets (P18-POST-001)
this milestone       4927 tests: 4820 + 69 ThermalBalance + 33 compile-fail + 5 architecture
```

## What was decided before the code

- **Nothing qualified moves.** `ThermalSolve`, `ThermalTransient`, `ThermalPost`, `ThermalElement`, `ThermalData`, `ThermalDof`, `ThermalLoadVector`, `ThermalBoundary`, `ThermalConvection`, `ThermalSystem`, the generic
  kernel and the structural wrapper are byte-identical to HEAD (FREEZE.md compares them by blob). The audit asked whether LOAD, BC or TRANSIENT had to expose more; they did not (ATTRIBUTION.md).
- **The sign is derived, then frozen.** Heat is positive INTO the body. Row `i` of the discrete equation gives `(K T)_i = Q_i + R_i`, so the reaction at a prescribed unknown is `R_i = +r_i`
  with `r = K T - Q` (steady) or `r = C (T_{n+1} - T_n)/dt + K T_{n+1} - Q` (transient). On a conductance network with `g = 2`, 400 K / 300 K, the hot end is `+200 W`, the cold end `-200 W`, and each is asserted on its own
  so a flip cannot hide in a sum (SIGN_CONVENTION.md, REACTION_HEAT.md).
- **The reaction is read from the residual, never from what closes the books.** The imbalance is `B = -sum(r_free) + attribution defects + rounding`. A remainder reaction does not exist as a symbol (compile-fail).
- **Convection is `-sum(K_conv T - Q_conv)`**, never the ambient source alone: a 1200 W source for a face that exchanges 0 W at equilibrium (CONVECTION_ACCOUNTING.md). Two additive conditions on one face both count.
- **Attribution is checked, not trusted.** `sum Q = sum loads + sum Q_conv` is verified for every state before any residual is formed, so an omitted or doubled category is refused (`attribution_missing`), not balanced (ATTRIBUTION.md).
- **Transient storage is `1^T C dT` over every row, with the actual step.** The consistent `C` is used (row-sum lumping gives the same total energy, so the claim is carried by the reaction: `-22.5 W` lumped against `-30 W` consistent). The last, partial
  step uses its own size (1 J over 0.1 s is 10 W, not 3.33 W) (TRANSIENT_ENERGY.md).
- **A history that cannot be balanced is refused,** not guessed at: a `FinalOnly` history of several steps has no per-step data (`step_data_missing`); the balance never re-integrates (ownership gate, compile-fail).
- **The tolerance is measured.** `|B| <= 1e-12 W + 1e-13 (gross + arithmetic)`; the worst measured `|B|/(gross + arithmetic)` is `1.7e-16`, 583 times inside it (TOLERANCE_MEASUREMENT.md). Gross is the sum of the magnitudes of every
  individual heat rate, so a +1e6 W / -1e6 W cancellation cannot make the scale tiny.
- **A balance PASS is not a solve PASS.** The books can close while a free field is wrong (`K = I`, `Q = 0`, `T = [+5, -5]` has `B = 0` and a 5 W local error). The accepted-solution type is the gate.
- **Currentness is the solution's.** The balance carries the solution's own source and answers "stale" through the transient solve's comparison; a remesh with equal counts and ids is caught by the mesh stamp (CURRENTNESS.md).
- **Derived only.** Not canonical, not persisted, not in the command history; no balance setting enters the source (GUI_OWNERSHIP.md).

## What this milestone adds

```text
include/bettercad/thermal/ThermalBalance.hpp   tolerance constants and balanceWithinTolerance, BalanceProblem (11) / BalanceFailure, BalanceStatus,
                                               LoadHeat / ConvectionHeat / ReactionHeat, ThermalHeatBalance, BalanceAlgebra, IntegratedEnergy,
                                               ThermalBalanceSource, ThermalBalanceResult, the algebraic seam (steadyHeatBalance, transientStepHeatBalance),
                                               balanceSteadySolution / balanceTransientSolution, recoverSteadyBalance / recoverTransientBalance,
                                               isCurrent / requireCurrent
src/thermal/ThermalBalance.cpp                 no Jacobian, no solve, no time integration: it consumes P18-LOAD's contributions, P18-BC's residualFrom and the system
src/thermal/CMakeLists.txt                     + ThermalBalance.cpp
tests/thermal/ThermalBalanceTests.cpp          the mathematics and the refusals, on hand networks
tests/thermal/ThermalBalanceMappedTests.cpp    the authority claims, on real P16 meshes
tests/thermal/ThermalBalanceTestSupport.hpp    fixtures and the test's own oracles
tests/compile_fail/ThermalBalanceMisuse.cpp    33 cases
tests/architecture/CheckBalanceOwnership.cmake + 4 fixtures + 5 tests
tests/CMakeLists.txt, tests/compile_fail/CMakeLists.txt, tests/architecture/CMakeLists.txt, tests/cli/ZeroMatchGuard.cmake   registrations; 17 filters
```

The layering checker reports **531 files, 0 violations** (529 plus the new header and source).

## The frozen contract

| | |
| --- | --- |
| sign | positive INTO the body for loads, convection, reactions and the net; the stored-energy rate is subtracted: `net = surface + generation + convection + reaction - storage` |
| reaction | `R_i = +r_i` at each prescribed unknown, ascending `ThermalDofIndex`, one per unique unknown (never per boundary condition) |
| convection | `heatInto = -(K_conv T - Q_conv)` per condition, from the P16 facets via `residualFrom`; `heatOut` is its negative |
| loads | `integrated.heatRate` of each prepared contribution; no geometry is read |
| storage | `dE = 1^T C dT` over the actual step; rate `dE / dt`; steady has none |
| residual | `B = netImbalance()`; status `Balanced` iff `\|B\| <= 1e-12 W + 1e-13 (gross + arithmetic)` |
| scale | gross = sum of individual magnitudes; arithmetic = `sum \|K\|\|T\| + sum \|C\|\|dT\|/dt + sum \|Q\|`; floor `1e-12 W` for the normalised residual |
| history | one balance per step (`EveryStep` or a single step); the integrated energy is the sum over steps; `FinalOnly` over several steps is refused |
| result | `ThermalBalanceResult`: 1 state (steady) or N states in the solution's order; the solution's source by value; no balance setting |
| refusals | 11 stable problems, checked before the arithmetic, in a fixed order |
| units | W for heat rates, J for energy (a `static_assert` on each dimension) |

[SIGN_CONVENTION.md](SIGN_CONVENTION.md), [REACTION_HEAT.md](REACTION_HEAT.md), [CONVECTION_ACCOUNTING.md](CONVECTION_ACCOUNTING.md), [ATTRIBUTION.md](ATTRIBUTION.md),
[STEADY_BALANCE.md](STEADY_BALANCE.md), [TRANSIENT_ENERGY.md](TRANSIENT_ENERGY.md), [TOLERANCE_MEASUREMENT.md](TOLERANCE_MEASUREMENT.md), [ANALYTICAL_ORACLES.md](ANALYTICAL_ORACLES.md),
[CURRENTNESS.md](CURRENTNESS.md), [GUI_OWNERSHIP.md](GUI_OWNERSHIP.md), [DETERMINISM.md](DETERMINISM.md), [ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md),
[MUTATIONS.md](MUTATIONS.md), [SCOPE.md](SCOPE.md), [FREEZE.md](FREEZE.md).

## Tests

69 ThermalBalance cases (2240 assertions), 33 compile-fail cases and 5 architecture tests, from three kinds of fixture. **Hand networks** with expected values typed in from conductances and capacities: a
`g = 2` chain, a lumped body, a nonuniform consistent `C`, and a network with two convection conditions, a chain, a prescribed node and two loads. **Real meshes** through the whole chain (document, extrude, P16 mesh, mapping, `ThermalModel`,
`ThermalDofMap`, prepared loads and boundary, assembled system, the **qualified solvers**, balance): a 40 x 30 x 20 mm steel block and a 6 mm radius cylinder on two meshes. **Architecture fixtures** for the ownership gate.

| Group | Cases | What it settles |
| --- | --- | --- |
| sign | 5 | the frozen sign convention; the conductance network's `+200 / -200 W`; convection above, below and at ambient (`-40`, `+40`, `0`); `K T - Q_conv`, not the source |
| reactions | 8 | steady Dirichlet reaction, two temperatures equal and opposite, free residuals are not reactions, duplicate identical constraints are one reaction (270 W, not 540), constant temperature under generation, the slab against the analytical solution and P18-POST's flux |
| categories | 6 | surface total, generation total, several loads each counted, several convections additive, temperature plus loads together |
| detail, determinism | 5 | per-load, per-condition and per-reaction lists equal their category totals; 25 and 20 repeats, 0 differences |
| steady | 9 | pure conduction, applied heat against reaction, generation against convection (hand and block), Robin only, fixed temperature plus convection, combined categories, applied surface heat on the block |
| transient | 8 | insulated no-load and generation, lumped convection, Dirichlet block every step, fully constrained, convection cooling over ten steps, the capacity in the Dirichlet reaction (`-30 W`, not the steady `-20 W`), the partial last step |
| stored energy | 2 | `1^T C dT` with the full `C` (18 J against `diag(C)` 12 J) and with the nonuniform consistent `C` |
| partial step | 2 | the actual step size on the last partial step, in the algebra and on a real history |
| history | 2 | a `FinalOnly` history of several steps is refused, in the model recovery and at the seam |
| adversarial | 5 | a global `Q` is not added on top (100 W double count shown); the balance cannot rescue a wrong free field; the attribution must account for the heat; out of tolerance is a result, not a failure; an incomplete constraint map is visible |
| tolerance | 4 | zero heat, a +1e6 / -1e6 W cancellation, the measured policy with its margin in every real-mesh case, the three constants pinned by value |
| refusals | 5 | every diagnostic reachable (five sections), stable distinct names, what cannot be computed, a mixed-up mode or constraint map at the seam, sparse node ids keyed by unknown |
| currentness, purity | 6 | every edit stales a steady and a transient result, a mismatched system or prepared set, a remesh with equal counts and ids, a failed recovery publishes nothing, the inputs unmutated |
| smoke | 2 | the fine cylinder, steady and transient |
| compile-fail | 33 | no remainder reaction, no lumping, no time integration, no heat-flux field, no tolerance setting, no colour, no component selection; the result, states, terms and reactions are read-only |
| architecture | 5 | the ownership gate on the real tree (497 files outside the module, 0 violations) and on four fixtures |

The groups follow the guard's filters, which are not a partition: `TransientInsulatedGenerationWithAPartialLastStep` matches two of them (transient and partial step), and `FixedTemperatureAndConvectionOverlapOnOneFace` matches none (it is counted by the total of 69). The table's counts are the filters' counts, which sum to 69.

Measured values the suite prints (every row of TOLERANCE_MEASUREMENT.md): the slab reaction `+-200 W`; the 270 W block reaction; the first-step hot reaction `334.3 W` against the steady `270 W` because it feeds the warm-up; the worst
`|B|/(gross + arithmetic)` `1.72e-16`; the large cancellation closing to `|B| = 0` against an allowed `2.0e-7 W` on `2.0e6 W` gross; the fine cylinder `|B| = 9.9e-12 W` (steady) and `1.2e-11 W` (worst of six transient steps) on a `4.7e6 W`
arithmetic scale.

## Zero-match protection

`tests/cli/ZeroMatchGuard.cmake` carries seventeen new filters (the total, fourteen groups, the compile-fail group and the ownership gate), each with a minimum equal to the observed count (69; 5, 8, 6, 5, 9, 8, 2, 2, 2, 5, 4, 5, 6, 2; 33; 5).
`verify-harness.cmd` (11855b8f) passed beforehand and `qualify.cmd` (d313a640) is the forty-third consecutive milestone on that byte-identical harness. The post-run script re-counted the filters in each preset (identical across
presets); its recount of the refusals group uses the three-name form that predates two cases added to it, so it shows 3 where the guard's five-name form shows 5, and the total of 69 is counted separately.

## Mutation probes

48 probe ids: **45 killed, 1 void (BAL12, an equivalent mutant), 2 removed as redundant code, 0 survived at the end.** Six probes survived the first pass (BAL23, BAL24, BAL27, BAL28, BAL29, BAL47: six test gaps), each closed by
strengthening a test and rerun to a kill. They include the reaction sign flipped, the reaction as a remainder, free rows counted as reactions, a double-counted unknown, the ambient source as the convection, its sign reversed, `abs` on a load,
the global `Q` added on top, a skipped load or condition, `T[n]` for convection, no storage term in the reaction, `diag(C)`, free rows only, the nominal or integrated step, an output interval as a step, a loosened tolerance, a category-level gross
scale, a status that is always `Balanced`, a `balanced()` that ignores the states, and every currentness check removed in turn: [MUTATIONS.md](MUTATIONS.md).

## Adversarial review

36 questions, each answered with a test, a probe, an audit or a stated limit: [ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md). **No defect in a qualified module.** Three defects of the NEW code were found during development
(a gross scale that cancelled, a tolerance `10^6` too loose, a missing export for the shared build), all fixed before the freeze; six test gaps closed, two redundant checks and three unreachable branches removed, one equivalent probe replaced.
No gate-blocking finding.

## Scope audit

```text
solve or time integration reimplemented:   NO   (ThermalBalance.cpp calls no solve, no step, no Jacobian; ownership gate + fixture balance-reintegrates)
GUI / non-thermal recomputation:           NO   (497 files outside the module scanned, 0 violations; fixture balance-gui-recomputes fails the gate)
reaction as a remainder:                   NO   (BAL02 killed; compile-fail THE_RESULT_HAS_NO_REMAINDER_REACTION)
lumped capacity:                           NO   (0 `lump`, 0 `diag` in code; compile-fail THE_ALGEBRA_HAS_NO_LUMPING)
a normal, an outward direction:            NO   (source audit: 0 in code)
Celsius or any unit conversion:            NO   (source audit: 0 in code)
heat-flux field, gradient:                 NO   (that is P18-POST-001's; compile-fail)
validation verdict, analytical acceptance: NO   (P18-VALID-001 is not started)
colour, range, component selection:        NO   (compile-fail on the source; P18-VIZ-001 is not started)
persistence, command, canonical data:      NO   (no serialisation symbol in either file; no setter; P18-CMD-001 and P18-PERSIST-001 are not started)
any qualified module changed:              NO   (twenty-five files byte-identical to HEAD)
unordered containers, threads, rand:       NO   (source audit: 0 in code)
```

## Known limitations

- **A balance PASS is not a solve PASS.** The balance is a global scalar and can cancel large local errors; it is computed from an accepted solution and never substitutes for the free residual.
- **Prescribed temperatures are constant in time in this version,** so the change at a prescribed unknown is zero and its storage enters only through the capacity coupling to its free neighbours. A time-varying prescription would add the unknown's own change; the full-`C` form is already the right one for it.
- **The reaction per boundary condition is not reported:** conditions that share an unknown share its heat, and no ownership rule exists that does not invent one.
- **A `FinalOnly` history of several steps cannot be balanced,** and the refusal says how to obtain one that can.
- **The books closing does not prove the physics is right,** only that the discrete model accounts for its heat. Accuracy against analytical solutions is P18-VALID-001's.
- **Kelvin versus Celsius is not proven here:** every term depends on temperature differences only.
- **The tolerance is measured on twelve cases** (gross from 4.8 W to 2.0e6 W, arithmetic scale up to 4.7e6 W), in three presets; a larger rounding scale is still judged by the scale-aware term, which grows with it.
- **The sum is plain fixed-order double summation.** Nothing measured needs a compensated sum; the decision is recorded so that a case that does show sensitivity can reopen it.
- **The per-preset recount of the refusals group in the post-run summary uses the older three-name filter** (see Zero-match protection). It is a documentation difference, not a coverage one: the guard's filter has the five.
- **No application consumes a heat balance yet**, so the ownership gate has nothing to flag in `apps/`; it is a standing constraint on P18-VIZ-001 and P18-CMD-001.

## Regression

Nothing qualified was changed, so nothing is requalified, but the full suite was run in every preset:

```text
unfiltered ctest, three presets    4927 / 4927 each   (4820 predecessor + 69 + 33 + 5)
repeat x5, release-ext, debug-ext  1813 / 1813 each   (1739 predecessor + 69 + 5)
P17 structural suite alone         415 cases, 271 414 assertions, identical in all three presets
P18-POST, TRANSIENT, SOLVE, ASSEMBLY, BC, ELEM, DOF, MAT, DATA   passed unchanged (40, 72, 51, 62, 92, 66 ThermalLoad, 60, 33, 33, 36 cases)
build                              0 warnings, 0 errors over 705 objects, in each preset
no-op rebuild                      1 edge in each preset
architecture checker               531 files, 0 violations; 33 architecture tests
```

## Result

```text
STATUS                PASS
QUALIFIED             debug-ext, release-ext, debug-shared-ext
TESTS                 4927 of 4927 in each preset; repeat 1813 x 5 in two
THIS MILESTONE        69 ThermalBalance cases, 2240 assertions; 33 compile-fail; 5 architecture
WARNINGS              0 over 705 objects in each preset
STAGES                17, all exit 0; 05:16:10 -> 10:40:25, 5h24m15s; run 1 of 1 (see FREEZE.md)
MUTATIONS             48 ids, 45 killed, 1 void, 2 removed, 0 survived; 6 test gaps closed
ADVERSARIAL           36 questions, 12 findings, 0 qualified-module defects, 0 gate-blocking
TOLERANCE             worst |B|/(gross+arithmetic) 1.7e-16 against 1e-13 (583x); 12 cases, identical in three presets
LAYERING              531 files, 0 violations
CROSS-PRESET          bit fingerprints identical (6b81a56797403e0d, ee1e1c859a3999d5, eb44675aa9d555a1, 5a8ba61c30276a46);
                      79 message lines, 0 differences
FINGERPRINT           eight paths equal at the freeze, before the first build and after the last repeat;
                      include ee2dd26d, src 4dd2f59f, tests 314e71c9
CONTRACT              P18_BALANCE_CONTRACT_FINGERPRINT d1925ef3d79f038081924d1c255f48206191921e
QUALIFIED == COMMITTED  proven by the commit script and recorded in FREEZE.md
NEXT                  P18-VALID-001 -- Thermal Validation / Acceptance. NOT AUTHORIZED.
```

## Revision

First issue, with the milestone.
