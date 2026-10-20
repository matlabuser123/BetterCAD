# P18-SOLVE-001 — Steady Thermal Solver

```text
MILESTONE:  P18-SOLVE-001
SUBJECT:    K T = Q after prescribed temperatures, by exact reduction  Kff Tf = Qf - Kfc Tc,
            through P17's qualified symmetric sparse solve EXTRACTED below the thermal layer
            (ADR-052), with an independent residual and an anchoring analysis
RESULT:     PASSED 2026-10-20, qualified in three presets, 4638 of 4638 each
```

## Baseline

```text
HEAD                 241c13aba2701e661e355c5a310d3d04c06709b4   (P18-ASSEMBLY-001's record commit)
tree                 9324d82b91f93eca49ad802989d6505af07236a8
origin/main          241c13a...  (the owner's push of both P18-ASSEMBLY-001 commits, verified)
working tree         clean apart from the session's untracked .claude/
predecessor suite    4546 of 4546, three presets (P18-ASSEMBLY-001)
this milestone       4638 tests: 4546 + 51 ThermalSolve + 10 Linalg + 26 compile-fail + 5 architecture
```

## What the audit decided before any code was written

Full table in [SOLVER_ARCHITECTURE.md](SOLVER_ARCHITECTURE.md). The decisions that shaped the work:

- **P17's solver cannot be included from thermal (ADR-050).** The generic part of
  `StructuralSolve.cpp` — a symmetric sparse direct solve, its three-condition acceptance and its
  independent residual — is a self-contained kernel that names no structural type. ADR-050 left two
  honest answers open (extraction, or an independent private link) and refused a hand copy.
- **Extraction, not a second link (ADR-052).** A second private Eigen link would have duplicated a
  qualified pivot gate that two modules could then let drift apart. The kernel moved, mechanically, to
  a new module `linalg` at layer 5. `structural::solveSymmetricSparse` is now a wrapper that adds P17's
  units and rebuilds P17's wording from the kernel's structured failure, byte for byte. **The 415
  structural cases pass unchanged**; no structural test, header or message changed.
- **A thermal prescribed value is not zero.** P17's partition needs no `- Kfc Tc` because its
  prescribed displacement is zero. Here the term is what makes the solution depend on the boundary at
  all, and the first mutation list item is that it is dropped.
- **Well posedness is a thermal question.** The library reports success on a singular matrix, so the
  kernel's pivot gate is kept, and on top of it a model-level analysis reads the matrix for regions
  with no absolute-temperature anchor and refuses them with a classification a user can act on. A
  fully prescribed model is a success, a convection-anchored model with no Dirichlet is a success, a
  disconnected model is a success when every region is anchored.
- **The solve returns a `SteadyThermalSolution`, not a `ThermalResult`.** The latter needs a gradient
  and a flux per element and a heat balance (P18-POST-001 and P18-BALANCE-001).

## What this milestone adds

```text
include/bettercad/linalg/SymmetricSolve.hpp    the generic kernel's public surface (no Eigen)
src/linalg/SymmetricSolve.cpp                  P17's runKernel, moved; structured failure; evaluateResidual
src/linalg/CMakeLists.txt                      bettercad_linalg: core PUBLIC, Eigen PRIVATE
include/bettercad/thermal/ThermalSolve.hpp     the reduction, the analysis, the solve, the solution, the provenance
src/thermal/ThermalSolve.cpp                   no factorisation: it calls linalg
src/structural/StructuralSolve.cpp             kernel replaced by a wrapper (the only structural source change)
tests/thermal/ThermalSolveTests.cpp            the mathematics and the refusals, on hand systems
tests/thermal/ThermalSolveMappedTests.cpp      the authority claims, on real P16 meshes
tests/thermal/ThermalSolveTestSupport.hpp      fixtures and the test's own dense elimination
tests/linalg/SymmetricSolveTests.cpp           the generic kernel on its own terms
tests/compile_fail/ThermalSolveMisuse.cpp      26 cases
tests/architecture/CheckTargetDependencies.cmake + 4 fixtures + 5 tests   the ADR-050 link-graph gate
docs/architecture/decisions/ADR-052-...md      the decision
```

`ThermalData.hpp` changed by two corrected comments (the tolerance was documented as `||K T - Q|| / ||Q||`).
The layering checker reports **525 files, 0 violations** (521 plus four new).

## The frozen formulation

| | |
| --- | --- |
| reduction | `Kff Tf = beff`, `beff = Qf - Kfc Tc`; exact, no penalty, no row replacement; orders from P18-DOF's maps |
| kernel | `linalg::solveSymmetricSparse`: SimplicialLDLT, pivot ratio `min|D|/max|D| >= 1e-12`, finite `x`, independent residual |
| acceptance | `||r||_2 / (||Kff||_inf ||Tf||_2 + ||beff||_2) <= 1e-9`; prescribed values held exactly |
| well posedness | every connected region of the nonzero entries of `Kff` must contain a row whose sum differs from zero by more than `1e-10` of its absolute sum |
| `Nfree = 0` | valid: `T = Tc`, kernel not invoked |
| no Dirichlet | valid if convection anchors every region |
| singular / unanchored | refused: `missing_temperature_datum`, `unanchored_thermal_region`, `singular_system` |
| result | `SteadyThermalSolution`: `T` by unknown, residual, pivot ratio, retained full residual, source |
| units | K, W, W/K in; K out |

[SOLVER_ARCHITECTURE.md](SOLVER_ARCHITECTURE.md), [ADR_050_COMPLIANCE.md](ADR_050_COMPLIANCE.md),
[SOLVER_BACKEND.md](SOLVER_BACKEND.md), [DIRICHLET_PARTITION.md](DIRICHLET_PARTITION.md),
[RESIDUAL_VALIDATION.md](RESIDUAL_VALIDATION.md) (with the tolerance table),
[SINGULARITY.md](SINGULARITY.md), [DISCONNECTED_COMPONENTS.md](DISCONNECTED_COMPONENTS.md),
[ANALYTICAL_ORACLES.md](ANALYTICAL_ORACLES.md), [CURRENTNESS.md](CURRENTNESS.md),
[DETERMINISM.md](DETERMINISM.md), [SCOPE.md](SCOPE.md).

Failure is classified by fifteen named problems, every one reachable and tested, and is **atomic**: a
refused solve publishes nothing and, held in P18-DATA's slot, leaves any earlier result stale.

## Tests

51 ThermalSolve cases (39 532 assertions), 10 Linalg cases (171), from two kinds of fixture. **Hand
systems** with expected values typed in or from the test's own dense elimination: the one-free-node
oracle, the nonzero-Dirichlet trap, symmetric and asymmetric 3-node chains, the Robin-only unknown, the
fully prescribed system, pure-Neumann and disconnected models, a five-unknown interleaved partition,
random SPD systems of order 6 to 23, a unit tetrahedron, structured grids. **Real meshes** through the
whole chain (document, extrude, P16 mesh, mapping, `ThermalModel`, `ThermalDofMap`, prepared loads and
boundary, assembled system): a 40 x 30 x 20 mm steel block and a 6 mm radius cylinder.

| Group | Cases | What it settles |
| --- | --- | --- |
| partition and Dirichlet | 5 | `Kff`, `Kfc`, `Qf`, `Tc`, `beff` entry by entry on an interleaved case; `T1 = 350`, not 50 |
| analytical and FE oracles | 13 | chains, Robin, fully prescribed, dense oracle, linear profile, affine patch, block with Dirichlet and convection |
| singularity | 5 | pure Neumann (compatible and not), indefinite and exactly singular and tiny-pivot systems, the anchor tolerance |
| disconnected regions | 3 | anchored passes, unanchored fails, stored zeros join nothing |
| residual and finiteness | 8 | independent residual, bad solutions refused, NaN/Inf, constraint violation, overflow, scale invariance |
| source and lifecycle | 8 | each field alone, edits, remesh, seam refusals, recorded inputs, failed re-solve |
| determinism and purity | 5 | repeats, bit fingerprints, inputs unmutated |
| sparse solves | 3 | 18^3 grid, real cylinder, sparse node ids |
| other | 1 | every problem has a name |
| generic kernel (`Linalg_`) | 10 | exact 2 x 2, structured failures, residual without a solve, scaling, determinism |
| compile-fail | 26 | dimensioned values, three index spaces, no conjured solution, no gradient/flux/reaction/penalty/selector |
| architecture | 5 | `linalg` includes nothing above core, thermal links no structural target |

Measured values the suite prints: `T1 = 350` exactly for the one-free-node oracle, `366.6666666666667`
for the asymmetric chain, `310` for the Robin-only unknown, a 6^3 grid's linear profile to `1.25e-12 K`,
an affine patch to `2.84e-13 K`, the real block's `400 -> 300 K` profile to `5.7e-14 K`, its series with a
convection film to `1.7e-13 K`, 60 W generated and 60 W leaving through the film, the 18^3 grid
(5832 unknowns, 5184 free, `nnz(Kff)` 70 766) to `1.1e-11 K`, and the 900-unknown cylinder inside the
maximum-principle bounds.

## Zero-match protection

`tests/cli/ZeroMatchGuard.cmake` carries thirteen filters (the total, the kernel, the eight groups and
the three gates), each with a minimum equal to the observed count. `verify-harness.cmd` (11855b8f) passed
beforehand.

## Mutation probes

43 distinct mutations: **43 killed, 0 survived, 0 void.** Table, the brief's list mapped, and the two
test gaps the first pass found (the reduction's own finiteness check and the residual loop's per-row
check, each shadowed by a later check with the same classification): [MUTATIONS.md](MUTATIONS.md). They
include `beff = Qf`, `Tc = 0`, free equations assumed first, an epsilon, a pin, "success without the
residual", a raw absolute residual, a stale system accepted, and the two architecture gates.

## Adversarial review

30 questions from the brief and 7 of the reviewer's, each answered with a test, a probe or an audit:
[ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md). **No production defect.** One documentation defect
(the tolerance comment, fixed), two test gaps (closed). No gate-blocking finding.

## Regularisation and scope audit

```text
epsilon diagonal:               NO   (probe S08 killed)
automatic pinning:              NO   (probe S09 killed)
penalty Dirichlet:              NO   (exact reduction; compile-fail THE_SOLUTION_HAS_NO_PENALTY)
weak thermal spring:            NO
pseudo-inverse acceptance:      NO   (a singular model is refused; no code path returns a level for it)
hidden reference temperature:   NO   (compile-fail THE_SETTINGS_HAVE_NO_REFERENCE_TEMPERATURE)
production dense reduced matrix: NO  (no n*n allocation, no toDense; nnz(Kff) against Nfree^2 reported)
P17 solver hand-copied into P18: NO  (the kernel exists once, in linalg)
transient solve implemented:    NO
grad(T) / heat flux implemented: NO
reaction heat / heat balance qualified: NO  (the full residual is retained, not interpreted)
```

## Known limitations

- The solve accepts any finite temperature field, including one below 0 K; the policy belongs to
  P18-POST-001 (`ThermalResult::create` refuses it) and P18-VALID-001.
- `steadySolveFailure` runs a full solve to produce its diagnosis; diagnostics and tests only.
- The datum classification depends on `kAnchorTolerance = 1e-10`; a model anchored more weakly than that
  is reported as lacking a datum. Both edges are measured and tested; the pivot gate and the residual
  are not relaxed by it.
- One algorithm, no fallback, no iterative method: P18 is correctness first.
- The `SteadyThermalSolution` is not a `ThermalResult`; the conversion is P18-POST-001's.
- Nothing is persisted: a solution and the reduced system are derived, in memory.

## Regression

The structural source changed (the kernel body), so **P17 is requalified**, not merely re-run:

```text
unfiltered ctest, three presets    4638 / 4638 each   (4546 predecessor + 51 + 10 + 26 + 5)
repeat x5, release-ext, debug-ext  1623 / 1623 each   (1557 predecessor + 51 + 10 + 5)
P17 structural suite alone         415 cases, 271 414 assertions, identical in all three presets
                                   (solve, reaction, validation, post, commands; the solve suite is 21 cases)
P16 mesh and mapping, P18-ARCH/DATA/MAT/DOF/ELEM/LOAD/BC/ASSEMBLY   passed unchanged
build                              0 warnings over 693 objects, in each preset
no-op rebuild                      1 edge in each preset
architecture checker               525 files, 0 violations; 24 architecture tests
```

## Result

```text
STATUS                PASS
QUALIFIED             debug-ext, release-ext, debug-shared-ext
TESTS                 4638 of 4638 in each preset; repeat 1623 x 5 in two
THIS MILESTONE        51 ThermalSolve + 10 Linalg cases, 39 703 assertions; 26 compile-fail; 5 architecture
WARNINGS              0 over 693 objects in each preset
STAGES                17, all exit 0; 15:10:06 -> 20:22:14, 5h12m08s; run 1 of 1
MUTATIONS             43 distinct, 43 killed, 0 survived, 0 void; 2 test gaps closed
ADVERSARIAL           30 + 7 questions, 5 findings, 0 production defects, 0 gate-blocking
LAYERING              525 files, 0 violations
CROSS-PRESET          bit fingerprints identical (b56e5e03da24db75, cddff5146fccbec9, 08d6173d68fdd1e1);
                      60 message lines, 0 differences
FINGERPRINT           eight paths equal at the freeze, before the first build and after the last repeat;
                      include 691d479c, src 9ae037da, tests 2c2d5ced
CONTRACT              P18_STEADY_SOLVER_CONTRACT_FINGERPRINT e9d68f4008bbf22ee44d4bfe1d45a68411ad5501
ADR-050               ADR_050_DEPENDENCY_FINGERPRINT 7c0158819237c593852bea9563fd38caa0ab374e
QUALIFIED == COMMITTED  proven by the commit script and recorded in FREEZE.md
NEXT                  P18-TRANSIENT-001 -- Transient Thermal Solver. NOT AUTHORIZED.
```

## Revision

First issue, with the milestone.