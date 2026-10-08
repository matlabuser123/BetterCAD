# P17-SOLVE-001 — Linear Static Solver

```text
STATUS:    PASS
MILESTONE: P17-SOLVE-001, the ninth milestone of P17 — Structural FEA
SCOPE:     exact free-system reduction of the assembled global system, a
           direct sparse factorisation of Kff, independent residual
           validation, and the full displacement field with the constrained
           degrees of freedom exactly zero. No reactions, no post-processing,
           no persistence, no CLI, no GUI.
```

## Baseline

```text
HEAD at start      bd098c07959088f61bb215ca41cb753e38b91bc0
origin/main        bd098c07959088f61bb215ca41cb753e38b91bc0
HEAD^{tree}        4662a83d0b771e5d1b87c924552487f50ba16c45
working tree       clean, 0 porcelain lines
git log -1         bd098c0 BetterCAD: add deterministic structural system assembly

P17-ARCH-001  20/20   P17-DATA-001  19/19   P17-MAT-001  14/14
P17-DOF-001   12/12   P17-ELEM-001  18/18   P17-LOAD-001  18/18
P17-BC-001    16/16   P17-ASSEMBLY-001  18/18
P16  QUALIFIED (P16-QUAL-001, 2026-10-06)

every predecessor checked for OPEN boxes, not just for a PASS line: zero
across all eight, and every one has a README in docs/verification/
```

## The two decisions this milestone was reserved for

Both `src/structural/CMakeLists.txt` and `TODO.md` deferred the structural
solver's library choice and its licence statement to this milestone. It made
them, and [ADR-039](../../architecture/decisions/ADR-039-the-structural-solver-is-eigens-simplicial-ldlt-with-a-pivot-gate-eigen-admitted-private.md)
records both with the rejected alternatives.

```text
Selected library     Eigen 5.0.1, SHA256-pinned, already in the tree
Licence              MPL-2.0, verified FROM THE VENDORED TREE
Selected solver      Eigen::SimplicialLDLT
Direct / iterative   DIRECT
Threading            single-threaded, audited
New dependency       NONE
```

**No licence decision was made by accident**, which is what `TODO.md` warned
about. Eigen 5.0.1 ships `COPYING.MPL2`, `COPYING.BSD`, `COPYING.APACHE` and
`COPYING.MINPACK` and — unlike Eigen 3.x — **no `COPYING.LGPL` and no
`COPYING.GPL`**. Every file `SimplicialLDLT` needs carries the Mozilla header,
including `Amd.h`, which states that Tim Davis executed a licence permitting
MPL-2.0 distribution of the CSparse-derived ordering. MPL-2.0 is file-level
weak copyleft, so it attaches to Eigen's files and does not decide BetterCAD's
licence. The three candidate alternatives that would have — SuiteSparse,
CHOLMOD, UMFPACK — are absent from the tree and stayed absent.

Eigen is linked **PRIVATE**, and the invariant that no public header includes
it now has a test: [SOLVER_SELECTION.md](SOLVER_SELECTION.md).

## The central claim, and it is measured

**A library saying `Success` is not evidence, and neither is a passing
residual.**

```text
Eigen's SimplicialLDLT fails only on an EXACTLY zero pivot
    SimplicialCholesky_impl.h:  if (d == RealScalar(0)) { ok = false; }

so an exactly singular matrix IS caught            [1 1; 1 1] -> NumericalIssue
and a numerically singular one is NOT              free body -> Success, with a
                                                   pivot ratio of 3.4e-17
```

Worse, a residual check cannot cover the gap, because the garbage lies in the
**null space**. Measured on a system small enough to control completely, with
the pivot gate relaxed:

```text
    [ 1  1       ] [x1]   [ 1       ]
    [ 1  1+1e-13 ] [x2] = [ 1+1e-8  ]

x                      (-100079, 100080)      five orders larger than the data
pivot ratio            9.99e-14
normalized residual    1.00e-17               <-- PASSES the 1e-9 gate
```

Hence **three independent gates**, of which the library supplies one:

```text
1  info() == Success, every D(k,k) > 0, and min|D|/max|D| >= pivotFloor
2  every component of uf is finite
3  the independent residual r = Kff uf - Ff passes the normalized gate
```

[SINGULARITY_VALIDATION.md](SINGULARITY_VALIDATION.md) carries the
measurements; [RESIDUAL_VALIDATION.md](RESIDUAL_VALIDATION.md) the formula and
the threshold.

## The trap the brief calls the most important one

`K u - F` over the FULL system is correctly **non-zero** at the constrained
degrees of freedom: those entries are the support reactions. Requiring them to
vanish would fail every correct solution of a restrained model, so the gate is
`Kff uf - Ff` and the full residual is retained for `P17-REACTION-001`.

Measured on the block fixture with a 1000 N load and a fixed support:

```text
largest |K u - F| over the FREE degrees of freedom          2.56e-13 N
largest |K u - F| over the CONSTRAINED degrees of freedom    273.83 N
sum of the constrained entries                 (7.1e-15, 1.3e-13, -1000) N
the applied load                               (0, 0, +1000) N
```

Both halves are asserted, including that the constrained entries are **not**
zero — the premise that makes the distinction necessary. The mutation that
checks the residual over the full system is killed by **twelve** tests.

## Settings, with nothing inherited from the library

```text
algorithm                    SimplicialLdlt       named, not defaulted
relativeResidualTolerance    1e-9   dimensionless BetterCAD's own
pivotFloor                   1e-12  dimensionless BetterCAD's own
maximum iterations           N/A -- ABSENT from the settings, not present and
                             ignored. Compiler-enforced
iterative tolerance          N/A, likewise
preconditioner               N/A, likewise
ordering                     AMD, solver-local; the solution returns in
                             FreeEquationIndex order and the test asserts it
initial guess                N/A -- a direct solve has none
```

Both thresholds were chosen for stated reasons before anything was measured,
and the measured values sit **eight orders** (residual) and **ten orders**
(pivot ratio) clear of them, so the numbers are margins rather than tuned
constants.

## Constraint application

```text
method                     exact free-system reduction
penalty method used        NO
automatic pinning used     NO
regularisation used        NO
row zeroing used           NO, and the two methods are not combined
constrained u              EXACTLY 0.0, asserted with an exact comparison
```

`1e12`, `1e20`, `penalty`, `epsilon`, `pin`, `spring`, `stabilize` and
`regularise` each have **zero occurrences** in the implementation.
[CONSTRAINT_APPLICATION.md](CONSTRAINT_APPLICATION.md).

## Independent validation

The oracles are closed-form or a different algorithm — never the production
solver.

```text
fixture                 expected                  error         residual     PASS
-----------------------------------------------------------------------------------
2x2 SPD                 (1/11, 7/11) by hand      < 1e-14 rel   <= 1e-9      PASS
3x3 1D Laplacian        (1, 1, 1) by hand         < 1e-14 rel   2.98e-17     PASS
RM-MESH-01 reduced      dense Gaussian            2.87e-16 rel  2.96e-17     PASS
  system                elimination WITH PARTIAL
                        PIVOTING, written in the
                        test
```

The elimination is deliberately a different algorithm: it pivots, which LDLT
does not; it is dense, which production never is; and it is twenty lines in the
test file so a reader can see there is no shared assumption.

## Constraint adequacy

```text
stage  restraints                 Nconstrained  expected  actual
-------------------------------------------------------------------
C0     none                                  0  FAIL      REFUSED, SingularSystem
C1     ux on one face                        4  FAIL      REFUSED, SingularSystem
C2     fixed support on one face            12  PASS      SOLVED
```

C2: `Ndof 57`, `Nfree 45`, `nnz(Kff) 1071`, pivot ratio `0.0726`, normalized
residual `2.07e-17`, largest displacement `1.07e-07` m under 1000 N — microns,
which is what a 40 x 30 mm steel section does.

## Physical checks

```text
load scaling      u(2F) = 2 u(F)                     within 1e-10 relative  PASS
load reversal     u(-F) = -u(F)                      within 1e-10 relative  PASS
stiffness scaling u(2E) = u(E)/2                     within 1e-10 relative  PASS
zero load         u = 0 exactly, r = 0, U = 0                               PASS
energy            U = (1/2) u^T K u = (1/2) u^T F    within 1e-9 relative   PASS
axial             F L / (A E) = 1.98413e-06 m
                  computed mean  = 1.95241e-06 m     ratio 0.984            PASS
```

The axial check is **one-sided and that is the point**: a constant-strain Tet4
is stiffer than the continuum, so a coarse mesh must give a *smaller*
deflection. That inequality is known before the measurement and cannot be
tuned; demanding equality on six tetrahedra would have been a tolerance
invented to pass, and continuum accuracy is `P17-REFMOD-001`'s subject.

## Scale

```text
RM-MESH-02 large    850 nodes   4210 tets   Ndof 2550   Nfree 2250
                    nnz(Kff) 82584          pivot ratio 2.26e-04
                    ||r||_2 4.74e-10 N      normalized 3.85e-18
                    largest |u| 1.60e-06 m
                    Kff sparse 1.26 MiB against a dense 38.62 MiB
```

Nothing densifies: the reduced system is a CSR, the factorisation is sparse,
and the residual is a sparse row walk. Dense matrices appear only in the
`12 x 12` test oracle.

## Determinism

```text
fixture              runs  solver          threads  status  u          residual
---------------------------------------------------------------------------------
block, C2               5  SimplicialLDLT        1  stable  BITWISE    BITWISE
                                                            IDENTICAL  IDENTICAL
```

Measured with no tolerance, because there is nothing to tolerance. OpenMP is
not enabled in the build and `SparseCholesky` contains no `pragma`, `omp` or
`thread`, so there is no thread count to fix — and the bitwise equality is the
canary that would notice if that changed. [DETERMINISM.md](DETERMINISM.md).

## Four defects this milestone's own review found

```text
D1  PRODUCTION: Eigen's NumericalIssue from compute() was reported as
    FactorizationFailure. For the LDLT path it has exactly one cause -- a zero
    pivot -- so it is a statement about the physics, and calling it a
    factorisation fault would send a caller looking for memory. Now
    SingularSystem, with "found an exactly zero pivot"

D2  TEST: the near-singular demonstration used a perturbation of 1e-16, which
    is BELOW the ulp of 1, so the matrix was exactly singular and the
    demonstration demonstrated the opposite. Fixed at 1e-13

D3  TEST: the free body could not serve as that demonstration at all -- its
    factorisation produces a NEGATIVE pivot, so the positivity half of the
    gate refuses it however the floor is set. The demonstration moved to a
    synthetic system; the free-body test now proves Eigen reported success
    from the diagnostic itself

D4  INFRASTRUCTURE: the new architecture rule's message counted as THREE
    violations for one -- `list(APPEND)` with two strings, and then a
    SEMICOLON inside the message, which CMake treats as a list separator.
    Found by writing the rule's own fixture self-test rather than by reading
```

And a fifth, found by a probe and recorded with the mutation evidence: the
non-finite-solution test allowed either `NonFiniteSolution` or
`NonFiniteResidual`, and that disjunction let the mutation survive. The check
order is documented, so the assertion is now exact. Three P17 milestones
running have now lost a probe to an assertion weakened for safety.

[ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md) carries all twenty-nine attacks.

## Mutation protection

```text
14 probes, 14 KILLED, 0 SURVIVED
    M9   the Kfc columns kept          14 of 24, the largest kill
    M6   residual on the FULL system   12 of 24
    M3   the pivot gate removed         7 of 24
    M5   an epsilon diagonal added      6 of 24
    M1   the residual gate removed      1 of 24  -- brief section 134's
    M2   the residual forced to zero    1 of 24     required evidence
    M11  a non-finite solution accepted 1 of 25  -- after a fixture was added
                                                    to reach the branch and the
                                                    assertion was tightened
```

[MUTATION_PROTECTION.md](MUTATION_PROTECTION.md).

## Qualification

See [FREEZE.md](FREEZE.md).

## Known limitations

```text
no reactions                 the full residual is retained, not aggregated.
                             P17-REACTION-001 owns support semantics and
                             equilibrium
no post-processing           no strain, no stress, no von Mises.
                             P17-POST-001 owns that
no prescribed displacement   deferred by P17-BC-001, so there is no value
                             field to misread
no factorisation caching     a load-only change could reuse the factorisation
                             and deliberately does not: correctness first. The
                             API does not prevent it
no iterative solver          and therefore no preconditioner, tolerance or
                             iteration count. P26 owns scale
near-incompressible          nu -> 0.5 conditions badly, and the pivot gate
  materials                  will refuse what it cannot solve accurately. No
                             fixture asserts a limit, because the limit is the
                             conditioning and not a BetterCAD threshold
disconnected regions         refused through the same pivot gate as a free
                             body, with a diagnostic that does not claim to
                             know which cause it is. P16 produces one solid, so
                             there is no production fixture
no bitwise cross-preset      the properties are asserted in each preset; no
  claim                      vector was carried between runs and compared
```

## Qualification

```text
3647/3647 in debug-ext, release-ext and debug-shared-ext, each from clean
0 warnings over 628 objects in each preset
686 x until-fail:5 in release-ext and in debug-ext
17 stages, 0 failed, qualify.cmd exit 0, 3 h 16 min
no-op rebuild: 0 Building or Linking lines in all three presets
the eight component hashes IDENTICAL before the first build and after the
last test run
```

`qualify.cmd` is byte-identical at `d313a640`, unchanged since P16-SIZE-001 and
now nineteen milestones in a row. [FREEZE.md](FREEZE.md).

## Result

```text
RESULT:   PASS
TESTS:    3647/3647 in debug-ext, release-ext and debug-shared-ext, each from
          clean; 0 warnings over 628 objects; 686 x 5 repeats in two presets;
          17 stages, 0 failed
          +32 over P17-ASSEMBLY-001: 19 unit, 6 reference, 6 compile-fail,
          1 architecture self-test
LIBRARY:  Eigen 5.0.1, MPL-2.0, already pinned -- no new dependency
SOLVER:   Eigen::SimplicialLDLT, DIRECT, single-threaded
GATES:    factorisation status AND pivot ratio, finiteness, and the
          independent residual -- all three required
MUTATION: 14 probes, 14 killed
REVIEW:   29 attacks, 4 defects found and fixed, 1 of them in production
TREE:     4e41fdd2611437f952d1134fa3310918a007cb3c, and the eight component
          hashes identical at both readings
ADR:      ADR-039, with four rejected solvers and three rejected methods
EVIDENCE: this directory
TODO:     updated on PASS
```

## Revision

First issue, 2026-10-09.
