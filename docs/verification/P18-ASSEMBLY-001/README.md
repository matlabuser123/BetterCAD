# P18-ASSEMBLY-001 — Global Thermal Assembly

```text
MILESTONE:  P18-ASSEMBLY-001
SUBJECT:    the unconstrained global thermal system  K T = Q  /  C dT/dt + K T = Q
            scattered through ThermalDofMap into BetterCAD-owned sparse matrices,
            with duplicates summed, validated, and bound to the exact inputs it
            was built from
RESULT:     PASSED 2026-10-20, qualified in three presets, 4546 of 4546 each
```

## Baseline

```text
HEAD                 a5b8cc2c13b4c786885f743d1f621349df58377c   (P18-BC-001's record commit)
tree                 c3a732eb98543d3f1d3662eb24548d90bfcc978b
origin/main          a5b8cc2c...  (the owner's push of P18-BC-001, verified before this work)
working tree         clean apart from the session's untracked .claude/
predecessor suite    4448 of 4448, three presets (P18-BC-001)
this milestone       4546 tests: 4448 + 62 ThermalAssembly cases + 36 compile-fail cases
```

## What the audit decided before any code was written

- **Assembly is scatter, not physics.** `Ke` and `Ce` are P18-ELEM-001's, nodal `q''` and
  `q'''` are P18-LOAD-001's, `Kconv` and `Qconv` are P18-BC-001's, the numbering is
  P18-DOF-001's, the material is P18-MAT-001's. This milestone repeats none of those
  formulas (ownership table below; source audit in ADVERSARIAL_REVIEW.md).
- **BetterCAD's own CSR, the same representation as P17's** (ADR-038): three vectors,
  64-bit indices, strictly ascending columns, a validating constructor. No
  linear-algebra library is linked into `bettercad_thermal`; P18-SOLVE-001 decides that.
- **The full system, before any elimination.** A prescribed-temperature unknown keeps its
  row and column: elimination needs `K_fc`, and a reaction needs the constrained rows.
  A test assembles the same case with and without a prescribed temperature and the
  results are bit-identical.
- **A steady analysis has no `C`.** `capacity()` is empty, never a zero matrix; a
  transient analysis with no `rho` or `cp` is refused, never given one.
- **Currentness is by value of the source.** Counts and ids that happen to agree prove
  nothing: a remesh gives identical counts and identical numeric ids and a different
  stamp.
- **Nothing in the persisted model changed.** A system is derived and disposable.

## What this milestone adds

```text
include/bettercad/thermal/ThermalSparse.hpp   ThermalSparseMatrix (CSR), typed K and C, ThermalHeatVector
include/bettercad/thermal/ThermalSystem.hpp   ThermalSystem, the provenance, the seam and the model-level entry
src/thermal/ThermalSparse.cpp                 layout validation, lookup, symmetry measure, multiply
src/thermal/ThermalSystem.cpp                 symbolic and numeric passes, source checks, stale reasons
tests/thermal/ThermalAssemblyTests.cpp        the mathematics and the seams, on hand meshes
tests/thermal/ThermalAssemblyMappedTests.cpp  the authority claims, on real P16 meshes
tests/thermal/ThermalAssemblyTestSupport.hpp  fixtures and the independent dense oracle
tests/compile_fail/ThermalSystemMisuse.cpp    36 cases
```

No file under `src/core`, `src/features`, `src/meshing`, `src/structural` or `apps/`
changed, and no existing thermal header changed. The thermal layer includes only core,
features and meshing (ADR-050); the layering checker reports **521 files, 0
violations** (517 plus the four new files).

## The frozen composition

```text
K  =  sum over Tet4 Ke  +  sum over convection facets Kconv          W/K   sparse, symmetric
C  =  sum over Tet4 Ce                                               J/K   sparse, symmetric, transient only
Q  =  prepared heat input  +  sum over convection facets Qconv       W     dense, length Ndof
```

| Contribution | Owner |
| --- | --- |
| Tet conduction `Ke` | P18-ELEM |
| Tet capacity `Ce` | P18-ELEM |
| surface `q''` nodal contribution | P18-LOAD |
| volume `q'''` nodal contribution | P18-LOAD |
| Dirichlet constraints | P18-BC (not touched here) |
| Robin `Kconv` | P18-BC |
| Robin `Qconv` | P18-BC |
| **global sparse scatter** | **P18-ASSEMBLY** |
| Dirichlet elimination, the solve | P18-SOLVE |

| Quantity | Unit | Equation |
| --- | --- | --- |
| `K` local and global | W/K | `K T` = W |
| `C` local and global | J/K | `C dT/dt` = W |
| `Q` local and global | W | |
| temperature, rate | K, K/s | |

Documents: [ASSEMBLY_MODEL.md](ASSEMBLY_MODEL.md), [SPARSE_FORMAT.md](SPARSE_FORMAT.md),
[SOURCE_COMPATIBILITY.md](SOURCE_COMPATIBILITY.md), [UNITS.md](UNITS.md) (with the
tolerance table, one class per row), [SINGLE_TET_ORACLE.md](SINGLE_TET_ORACLE.md),
[TWO_TET_ORACLE.md](TWO_TET_ORACLE.md), [DENSE_ORACLE.md](DENSE_ORACLE.md),
[LOAD_RESULTANTS.md](LOAD_RESULTANTS.md), [SYMMETRY.md](SYMMETRY.md),
[SPARSE_SMOKE.md](SPARSE_SMOKE.md), [ANALYTICAL_VALIDATION.md](ANALYTICAL_VALIDATION.md),
[DETERMINISM.md](DETERMINISM.md), [SCOPE.md](SCOPE.md).

Failure is classified by eleven named problems, ten reachable from valid public inputs
and all tested (the eleventh is a self-check, see MUTATIONS.md), and is **atomic**: a
refusal publishes nothing.

## Tests

62 cases, 220 586 assertions, from two kinds of fixture. **Hand meshes** (one
tetrahedron, two tetrahedra, sparse and huge handles, disconnected blocks, a 9^3 grid
and a 22^3 smoke grid) with expected values typed in the test, and an independent dense
oracle. **Real meshes** through the whole chain (document, extrude, P16 mesh, mapping,
`ThermalModel`, `ThermalDofMap`, prepared loads and boundary): a 40 x 30 x 20 mm block
and a 6 mm radius cylinder.

| Group | Cases | What it settles |
| --- | --- | --- |
| dimensions, types, composition | 5 | `Ndof`, `K` and `C` are different types, steady has no `C`, the block's composition and element order |
| single tetrahedron | 4 | `K`, `C`, `Q` and the composite against hand matrices, error 0 |
| two tetrahedra | 4 | every shared entry is a sum: `K(1,1)` 2.5, `C(1,1)` 6, `Q(1)` 3 W |
| dense oracle and energy | 4 | the independent scatter agrees; `T^T K T` and `v^T C v` identities |
| convection | 6 | `Kconv`/`Qconv` land in the right unknowns, shared facets accumulate, a facet outside every element extends the pattern |
| resultants, null mode, symmetry, finiteness | 8 | surface, generation, convection and combined `Q`; `K 1 = 0`; `1^T C 1`; symmetric and finite |
| source compatibility and currentness | 14 | numbering, loads, boundary, material; remesh, same ids, same builder; each edit stales exactly what it changes; every field alone; the prescribed temperature stales nothing |
| material | 2 | steady `k` only assembles; transient needs `rho` and `cp` |
| sparse | 7 | layout validation, a missing transpose, sparse handles, 4 294 967 294, disconnected blocks, the 22^3 grid and the real cylinder |
| refusals | 2 | overflow, bad numbering, bad length, inverted tetrahedron, non-finite |
| permutation, determinism, purity | 6 | even permutation, 30 and 50 repeats, two bit fingerprints, inputs unmutated |
| compile-fail (separate) | 36 | `K` is not `C`, a row is an unknown, nothing solves or constrains |

The 62 cases are classified exclusively above (5 + 4 + 4 + 4 + 6 + 8 + 14 + 2 + 7 + 2 +
6); the zero-match guard's groups overlap on purpose and are listed in the guard.
Measured values the suite prints: unit-tetrahedron `K = [3 -1 -1 -1; -1 1 0 0; -1 0 1 0;
-1 0 0 1]` W/K and `C = [2 1 1 1; ...]` J/K with error 0; block `Q` = 0.6 + 60 + 8.7945
= 69.3945 W; block `||K 1|| = 2.22e-16`; `1^T C 1 = 86.664 J/K`; the fine cylinder has
900 unknowns, 4118 tetrahedra and `nnz(K) = 11 506`; the 22^3 grid has 10 648 unknowns
and `nnz / Ndof^2 = 1.31e-3`.

## Zero-match protection

`tests/cli/ZeroMatchGuard.cmake` carries fifteen filters (the total, the brief's nine
tracked groups and their extras, and the compile-fail group), each with a minimum equal
to the observed count. `verify-harness.cmd` (11855b8f) passed beforehand.

## Mutation probes

75 distinct mutations: **74 killed, 1 survived (an explained equivalent), 0 void.**
Table and what the probes found: [MUTATIONS.md](MUTATIONS.md). They found **five test
gaps** (a facet outside every element, `body` and `nodeCount` in the stale comparison,
and `geometry` and `body` in the recorded source) and no defect in the production
logic. All five are closed by new tests.

## Adversarial review

35 questions from the brief and 6 of the reviewer's, each answered with a test, a probe
or an audit: [ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md). **No production defect.**
The shared-library preset was built early, before the mutation work, because the
previous milestone's only defect was a missing export annotation; it linked on the
first build. No gate-blocking finding.

## Regularisation audit

```text
penalty Dirichlet:          NO   (bit-identical with and without a prescribed temperature; probe B06 killed)
epsilon diagonal:           NO   (probe A45 killed)
weak thermal spring:        NO
arbitrary pinning:          NO   (K 1 = 0 holds to 2.2e-16)
post-hoc symmetrisation:    NO   (the gate refuses; it never repairs)
small-entry pruning:        NO   (exact zeros stay in the pattern; probe B07 killed)
production dense global matrix:  NO   (no n*n allocation, no toDense; the 22^3 dense form is 907 MB and is never formed)
```

## Known limitations

- A convection facet is taken as data. Whether it is an exterior boundary facet is
  P18-BC-001's guarantee (resolved from a `FaceName` through P16); the seam does not
  re-decide it.
- `thermalAssemblyFailure` runs a full assembly to produce its diagnosis. It exists for
  diagnostics and tests, not a hot path.
- `AssemblyProblem::InconsistentSystem` is reachable only through a defective scatter.
  It is a self-check, covered by a mutation and by the direct tests of `fromCsr`.
- The symmetry gate is relative (`1e-13 x max|K|`). A correct scatter is exactly
  symmetric, so it sits far above the observed value and far below any defect.
- Assembly is serial by design, for a reproducible summation order; parallel assembly
  would be a later decision with its own determinism proof.
- Nothing is persisted: a system is derived, in memory, and does not survive a remesh by
  construction.
- Dirichlet elimination, the linear solve, singularity detection, `C / dt`, residuals,
  reactions and every result field are not here and are not hinted at by any symbol.

## Regression

Nothing outside `src/thermal` and the test registrations changed, so the predecessor
suites are the regression, and they ran in every stage:

```text
unfiltered ctest, three presets    4546 / 4546 each   (4448 predecessor + 62 + 36)
repeat x5, release-ext, debug-ext  1557 / 1557 each   (1495 predecessor + 62)
P16 mesh and mapping, P17 structural elements, constraints and loads,
P18-ARCH/DATA/MAT/DOF/ELEM/LOAD/BC passed unchanged; the P17 structural-element suite
alone is 29 cases, 4384 assertions
build                              0 warnings over 687 objects, in each preset
no-op rebuild                      1 edge in each preset
architecture checker               521 files, 0 violations
```

No generic sparse-assembly or finite-element infrastructure that P17 uses was changed
(the thermal sparse type is its own), so P17-ASSEMBLY, P17-SOLVE, P17-REACTION and
P17-REFMOD needed no separate requalification (brief section 178); all of them are part
of the unfiltered runs and passed.

## Result

```text
STATUS                PASS
QUALIFIED             debug-ext, release-ext, debug-shared-ext
TESTS                 4546 of 4546 in each preset; repeat 1557 x 5 in two
THIS MILESTONE        62 ThermalAssembly cases, 220 586 assertions; 36 compile-fail cases
WARNINGS              0 over 687 objects in each preset
STAGES                17, all exit 0; 21:27:30 -> 02:36:15, 5h08m45s; run 1 of 1
MUTATIONS             75 distinct, 74 killed, 1 equivalent survivor, 0 void; 5 test gaps closed
ADVERSARIAL           35 + 6 questions, 5 findings, 0 production defects, 0 gate-blocking
LAYERING              521 files, 0 violations
CROSS-PRESET          bit fingerprints identical (two-Tet 6398b41313705d28, 9^3 grid
                      c6a042bf47d865e0, real block 07c4bb5e30c65a38); 51 message lines, 0 differences
FINGERPRINT           eight paths equal at the freeze, before the first build and
                      after the last repeat; tests 94c593a4, src 0f4d1f8f, include 97c83ec2
CONTRACT              P18_THERMAL_ASSEMBLY_CONTRACT_FINGERPRINT 6dab28b5974c865198a20e10468b2a08a39ba60a
QUALIFIED == COMMITTED  proven by the commit script and recorded in FREEZE.md
NEXT                  P18-SOLVE-001 -- Steady Thermal Solver. NOT AUTHORIZED.
```

## Revision

First issue, with the milestone.