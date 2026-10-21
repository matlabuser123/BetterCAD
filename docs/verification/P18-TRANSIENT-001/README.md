# P18-TRANSIENT-001 — Transient Thermal Solver

```text
MILESTONE:  P18-TRANSIENT-001
SUBJECT:    C dT/dt + K T = Q by backward Euler at a fixed step, (C/dt + K) T[n+1] = Q + (C/dt) T[n], with the
            prescribed temperatures eliminated exactly at every step, a deterministic time grid that lands on t_end,
            a uniform initial temperature over which the prescribed values dominate, two independent per-step
            residuals, and a source-bound, atomic history
RESULT:     PASSED 2026-10-21, qualified in three presets, 4745 of 4745 each
```

## Baseline

```text
HEAD                 147d35572045791f16e45ff65db2d64e4c4aefb4   (P18-SOLVE-001's record commit)
tree                 caade9aa774893a3c0f731799a3598de1160090a
origin/main          241c13a...  (P18-ASSEMBLY-001's record commit: the two P18-SOLVE-001 commits, 8fdcb0cf and
                     147d355, were committed and are NOT yet pushed; this milestone stacks on them)
working tree         clean apart from the session's untracked .claude/
predecessor suite    4638 of 4638, three presets (P18-SOLVE-001)
this milestone       4745 tests: 4638 + 72 ThermalTransient + 35 compile-fail
```

## What was decided before the code

- **One reduction, one kernel.** Every step is the steady machinery applied to a different matrix and right-hand side:
  `A = C/dt + K`, `b = Q + (C/dt) T[n]` go through `reduceThermalSystem` (the exact `- Afc Tc` partition) and
  `linalg::solveSymmetricSparse` (ADR-052). No qualified file was changed: `ThermalSolve.{hpp,cpp}`,
  `SymmetricSolve.{hpp,cpp}` and `StructuralSolve.cpp` are byte-identical to HEAD (FREEZE.md); the only edit to a
  qualified file is one comment in `ThermalData.hpp` that said how the end time would be reached and now says it.
- **The steady singularity rules do not apply.** `C/dt + K` is positive definite where `C` is, and the initial temperature
  fixes the level. A pure-Neumann model and a disconnected model of positive capacity SUCCEED (the steady solve refuses
  both); a region with no capacity at all is refused by the pivot gate. `analyseAnchoring` is not called.
- **The capacity is the consistent matrix, as assembled.** The non-uniform oracle `C = [2 1; 1 2], K = [1 -1; -1 1],
  dt = 1, T0 = [300, 330]` gives `[310, 320]`; a lumped capacity gives `[306, 324]`.
- **A step is accepted by two independent residuals** recomputed from the original `K`, `C` and `Q`: the solve of the system
  that was built, and the heat equation itself on the free rows. The second is what catches a system built wrongly
  (`C*dt`, a flipped history sign) and then solved perfectly.
- **The time grid** is a multiplication, lands on `t_end` exactly, and shortens its last step instead of overshooting;
  the tolerance on `t_end/dt` is one number (`1e-9`), `integratedSpan()` reports the span actually integrated.
- **The solve returns a `TransientThermalSolution`, not a sequence of `ThermalResult`.** A `ThermalResult` needs a gradient
  and a flux per element and a heat balance with a stored-energy rate (P18-POST-001, P18-BALANCE-001).
- **K and C are different types** in the three entry points; the swap that gives a plausible wrong answer does not compile.

## What this milestone adds

```text
include/bettercad/thermal/ThermalTransient.hpp   TimeGrid, effectiveMatrix, validateTransientStep, physicalStepResidual,
                                                 solveThermalTransientAlgebra, the provenance, TransientThermalSolution,
                                                 solveTransientThermalSystem / solveTransientThermal (+ definition overload)
src/thermal/ThermalTransient.cpp                 no factorisation: it calls linalg
src/thermal/CMakeLists.txt                       + ThermalTransient.cpp
tests/thermal/ThermalTransientTests.cpp          the mathematics and the refusals, on hand systems and a Kuhn grid
tests/thermal/ThermalTransientMappedTests.cpp    the authority claims, on real P16 meshes
tests/thermal/ThermalTransientTestSupport.hpp    fixtures and the test's own dense backward-Euler oracle
tests/compile_fail/ThermalTransientMisuse.cpp    35 cases
tests/CMakeLists.txt, tests/compile_fail/CMakeLists.txt, tests/cli/ZeroMatchGuard.cmake   registrations; 16 filters
include/bettercad/thermal/ThermalData.hpp        one comment brought up to date
```

The layering checker reports **527 files, 0 violations** (525 plus the new header and source).

## The frozen formulation

| | |
| --- | --- |
| step | `(C/dt + K) T[n+1] = Q + (C/dt) T[n]`, history from `T[n]`, one `dt` per step |
| reduction | `A_ff T_f = b_f - A_fc T_c`, `A_fc = C_fc/dt + K_fc`; exact; prescribed held bit for bit |
| kernel | `linalg::solveSymmetricSparse` per step (refactorised; see FORMULATION.md for the cost) |
| acceptance | reduced `||r|| / (||A_ff|| ||T_f|| + ||b_eff||)` AND physical `||C dT/dt + K T - Q||_free / (||C|| ||dT||/dt + ||K|| ||T|| + ||Q||)`, both `<= 1e-9` |
| t = 0 | the uniform initial temperature, prescribed values overriding it (counted) |
| grid | `t_k = k dt`, `t_N = t_end` exactly, last step shortened, tolerance `1e-9`, at most 1 000 000 steps |
| storage | `FinalOnly` or `EveryStep`; at most 50 000 000 stored temperatures |
| singular | refused only where actually singular (no capacity at all): `singular_system` at the step, never regularised |
| result | `TransientThermalSolution`: grid, sample times, states, per-step records, resolved initial state, source |
| units | K, W, W/K, J/K, s |

[FORMULATION.md](FORMULATION.md), [TIME_GRID.md](TIME_GRID.md), [INITIAL_CONDITION.md](INITIAL_CONDITION.md),
[CONSTRAINT_TREATMENT.md](CONSTRAINT_TREATMENT.md), [TRANSIENT_RESIDUAL.md](TRANSIENT_RESIDUAL.md),
[ENERGY_CHECKS.md](ENERGY_CHECKS.md), [LUMPED_BENCHMARK.md](LUMPED_BENCHMARK.md),
[TIME_STEP_REFINEMENT.md](TIME_STEP_REFINEMENT.md), [STEADY_LIMIT.md](STEADY_LIMIT.md),
[CURRENTNESS.md](CURRENTNESS.md), [DETERMINISM.md](DETERMINISM.md), [SCOPE.md](SCOPE.md).

Failure is classified by twenty-one named problems, twenty reachable and tested and one (`factorization_failure`) the
kernel's defensive classification, and is **atomic**: a refused run publishes nothing and, held in P18-DATA's slot,
leaves any earlier result stale.

## Tests

72 ThermalTransient cases (105 966 assertions) and 35 compile-fail cases, from three kinds of fixture. **Hand systems** with
expected values typed in, from closed forms or from the test's own dense elimination: the one-unknown and non-uniform
two-unknown oracles, three-unknown chains with five prescribed sets, disconnected bodies, a node with no capacity,
Kuhn grids of 64 and 216 unknowns. **Real meshes** through the whole chain (document, extrude, P16 mesh, mapping,
`ThermalModel`, `ThermalDofMap`, prepared loads and boundary, an assembled TRANSIENT system): a 40 x 30 x 20 mm steel block
and a 6 mm radius cylinder of 900 unknowns. **Hand-assembled systems** through the seam.

| Group (names overlap by design) | Cases | What it settles |
| --- | --- | --- |
| time grid | 10 | exact and partial grids, `0.3/0.1`, the tolerance edges, multiplication not summation, `dt > t_end`, `t_end = 0`, the resource guard, the integrated span |
| effective matrix | 3 | the union of two patterns, `C/dt + K` entry by entry, asymmetric, overflowing and mismatched refusals |
| hard oracles | 2 | `4600/12`; `[310, 320]` and the lumped `[306, 324]` it must not give |
| singularity and capacity | 5 | pure Neumann, disconnected, unanchored, no capacity, a node with no capacity but a conduction path, the insulated block |
| Dirichlet and the initial state | 3 | five prescribed sets against the dense oracle, `1000/3` not `300`, all unknowns prescribed |
| stepping and output | 3 | the shortened last step, both policies, per-step evidence |
| refusals | 11 | settings, policy, initial, sizes, constraints, history size, a failure at a later step, an overflowing history, the order of refusals, the resolution limit |
| acceptance and the physical residual | 8 | the real step accepted; seven wrong finite steps refused; a wrongly built system; the boundary rows excluded; a small step |
| scaling, limits, benchmarks | 10 | scale invariance, the steady limit twice, first order refinement, the lumped body, the lumped block, energy, constant temperature (x2) |
| source, currentness, lifecycle | 10 | the recorded inputs, every edit, a remesh, mode confusion, the definition overload, the failed-run lifecycle, accessors |
| determinism and purity | 4 | repeated runs, bit fingerprints, inputs unmutated |
| sparse | 3 | 216 unknowns against the dense oracle, the 900-unknown cylinder, sparse node ids |
| seam | 3 | the assembled-system entry on a hand mesh |
| vocabulary | 1 | every problem and every stale reason has a distinct stable name |
| compile-fail | 35 | K is not C, dimensioned values, no conjured history or grid, no gradient/flux/reaction/interpolation/restart, no selector/lumping/retry/theta/method/adaptive/profile |

Measured values the suite prints: `T[1] = 4600/12 = 383.3333` and `[310, 320]` exactly; the discrete closed form of the
non-uniform pair to `1e-10`, and its first-order refinement (errors `0.759, 0.393, 0.200, 0.101` K for `dt = 0.2 … 0.025`,
orders `0.951, 0.976, 0.988`); the lumped body's orders `0.945, 0.971, 0.985`; the real block's `400 → 300 K` steady limit to
`1.1e-13 K` of the closed form and `5.7e-14 K` of the qualified steady solver; generation + Dirichlet + convection to the
steady solve within `1.7e-13 K`; the lumped block (`tau = 666.646 s`, `Bi = 2.6e-3`) within 0.057 K of the discrete lumped
exponential over 100 steps and with a per-step energy identity good to `2.9e-13`; 60 W into the insulated block raising its
mean temperature `69.232899 K` in 100 s; a 64-unknown Kuhn body gaining `1080 J` in `20 s` at `54 W`; 216 unknowns agreeing
with the dense oracle to `9.1e-13 K`; worst per-step residuals `8.3e-17` (reduced) and `4.3e-17` (physical) on the real
block; a constant temperature in equilibrium moving by `7.4e-13 K` in 20 steps.

## Zero-match protection

`tests/cli/ZeroMatchGuard.cmake` carries sixteen new filters (the total, the thirteen groups, the vocabulary and the compile-fail
group), each with a minimum equal to the observed count. `verify-harness.cmd` (11855b8f) passed beforehand.

## Mutation probes

85 distinct mutations: **85 killed, 0 survived, 0 void.** Table, the three survivors the first pass found (all test gaps,
closed), the six probes that did not compile (reformulated), the eighteen that a name collision silently skipped (rerun) and
the harness defect that mis-logged the survivors: [MUTATIONS.md](MUTATIONS.md). They include `C*dt`, a flipped and a missing
history, a lumped capacity, an epsilon on the diagonal, "the prescribed value does not dominate at t = 0", a time grid summed
instead of multiplied, a last step that is not shortened, a final time that is computed, the acceptance step skipped, the physical
residual with each term and row wrong, every provenance field dropped and every staleness comparison disabled, and the steady
singularity rule applied to `K` (TA21, killed by 10 cases).

## Adversarial review

30 questions, each answered with a test, a probe, an audit or a stated limit: [ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md).
**No production defect.** Six findings: two test gaps (closed), two pieces of redundant or unreachable code (removed or
proven), one numerical limit (documented), one tooling defect (corrected). No gate-blocking finding.

## Regularisation and scope audit

```text
epsilon diagonal:               NO   (probe E06 killed; source audit: 0 matches in code)
automatic pinning:              NO   (the unknown is solved or the run fails)
penalty Dirichlet:              NO   (exact reduction; compile-fail THE_SOLUTION_HAS_NO_PENALTY)
lumped or replaced capacity:    NO   (probe E05 killed; compile-fail THE_SETTINGS_HAVE_NO_LUMPING)
adaptive step / retry:          NO   (a failed step fails the run; compile-fail …NO_ADAPTIVE_STEP, …NO_RETRY)
silent dt change:               NO   (the last step is shortened and reported; integratedSpan() says what was integrated)
time-dependent load or BC:      NO   (compile-fail A_LOAD_HAS_NO_TIME_FUNCTION, A_BOUNDARY_CONDITION_HAS_NO_TIME_FUNCTION)
steady datum rule in the transient solve: NO  (analyseAnchoring is not called; probe TA21 killed)
production dense effective matrix: NO  (sparse CSR throughout; nnz(K) 2446 of 46656 and 11 506 of 810 000 reported)
steady solver or kernel changed: NO  (byte-identical to HEAD)
gradient / flux / reaction / balance implemented: NO
```

## Known limitations

- **The cost per step is a full factorisation.** Reusing one factorisation across constant steps is an optimisation that
  would need a change to the qualified kernel; it is recorded, not done. The step limit is 1 000 000 and a run near it is hours.
- **A step below the resolution of the temperature is refused.** At `dt/tau ~ 1e-12` the increment (3e-10 K on 300 K) is below
  the rounding of the temperature, so the physical residual cannot be met; `1e-6` and `1e-3` run; a tolerance of `1` accepts.
  The message says why.
- **Inside the grid tolerance the integrated span is `N dt`**, which differs from `t_end` by at most `1e-9 t_end`; reported by
  `integratedSpan()`, not hidden.
- **The canonical data forbids `t_end = 0` and `dt > t_end`; the grid accepts both** so they can be tested; the solve applies the
  canonical rule. Documented in TIME_GRID.md.
- **The solve accepts any finite temperature field**, including one below 0 K, as the steady solve does; the policy belongs to
  P18-POST-001 and P18-VALID-001.
- **`factorization_failure` is the kernel's defensive classification and has no test that reaches it.**
- The history is derived and in memory; nothing is persisted. It is not a `ThermalResult`; the conversion is P18-POST-001's.
- **Unpushed commits.** Two P18-SOLVE-001 commits (8fdcb0cf, 147d355) are local; this milestone's commits stack on them, so the
  owner's push sends all of them.

## Regression

Nothing qualified was changed, so nothing is requalified, but the full suite was run in every preset:

```text
unfiltered ctest, three presets    4745 / 4745 each   (4638 predecessor + 72 + 35)
repeat x5, release-ext, debug-ext  1695 / 1695 each   (1623 predecessor + 72)
P17 structural suite alone         415 cases, 271 414 assertions, identical in all three presets
P18-SOLVE, ASSEMBLY, BC, LOAD, ELEM, MAT, DOF, DATA, ARCH   passed unchanged (51, 62, 92, 66, 60, 31, 33, 36 cases)
build                              0 warnings over 697 objects, in each preset
no-op rebuild                      1 edge in each preset
architecture checker               527 files, 0 violations; 24 architecture tests
```

## Result

```text
STATUS                PASS
QUALIFIED             debug-ext, release-ext, debug-shared-ext
TESTS                 4745 of 4745 in each preset; repeat 1695 x 5 in two
THIS MILESTONE        72 ThermalTransient cases, 105 966 assertions; 35 compile-fail
WARNINGS              0 over 697 objects in each preset
STAGES                17, all exit 0; 05:40:32 -> 10:43:43, 5h03m11s; run 1 of 1
MUTATIONS             85 distinct, 85 killed, 0 survived, 0 void; 3 test gaps closed
ADVERSARIAL           30 questions, 6 findings, 0 production defects, 0 gate-blocking
LAYERING              527 files, 0 violations
CROSS-PRESET          bit fingerprints identical (a52a8f274b935773, e9e19be85000cc6d);
                      51 message lines, 0 differences
FINGERPRINT           eight paths equal at the freeze, before the first build and after the last repeat;
                      include 4757ad6f, src 2d17a75f, tests 97eebe01
CONTRACT              P18_TRANSIENT_SOLVER_CONTRACT_FINGERPRINT ec053b623d0b6c17548c9bf874011c4a9185ece0
TIME GRID POLICY      TIME_GRID_POLICY_FINGERPRINT e92276197f2e1a2a499b96f95a106187c8137f3b
QUALIFIED == COMMITTED  proven by the commit script and recorded in FREEZE.md
NEXT                  P18-POST-001 -- Thermal Result Recovery. NOT AUTHORIZED.
```

## Revision

First issue, with the milestone.
