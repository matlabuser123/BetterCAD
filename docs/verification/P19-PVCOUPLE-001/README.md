# P19-PVCOUPLE-001 — Pressure–Velocity Coupling

RESULT: PASS — 17 of 17 qualification stages exit 0, 0 stage(s) failed
COMMIT: pending

## TASK

`P19-PVCOUPLE-001` — Pressure–Velocity Coupling. Authorized 2026-11-08 by the
owner's "implement and fully veify" over the seventeen-item checklist, which
arrived pasted into the instruction as the seven before it did.

**THE PASTED TEXT WAS DIFFED AGAINST `TODO.md`'S OWN SECTION BEFORE ANY WORK.**
Byte-for-byte identical across all 36 lines, 17 unticked items and 0 ticked —
counted rather than assumed — sha256 `43a305f28e5dd8f0` over both after CR
normalization, and re-verified identical after the authorization block was
written. So the scope is this repository's checklist unaltered.

```text
Define predictor                  Define residual evaluation
Define pressure correction        Define continuity closure
Define velocity correction        Define stopping criteria
Define face-flux correction       Define maximum iterations
Define pressure reference         Reject divergence
Define under-relaxation policy    Reject NaN / Inf
Structured solver diagnostics     Deterministic iteration behaviour
Independent simple benchmark      Regression PASS
Evidence recorded
```

The checklist also carries an instruction: **"Do not silently copy OpenFOAM
implementation."** It is treated as a constraint and answered in `ADR-075`
Decision 10.

## SCOPE

### Three boundaries this milestone does not cross, found before any code

The decisive finding of the UNDERSTAND phase is that **SIMPLE's two linear
solves are not this milestone's work**, and three committed records say so
independently:

```text
src/cfd/CMakeLists.txt   "the SOLVE -- still P19-LINSOLVE-001's decision,
                          unchanged.  The generic kernel it would reuse is
                          BetterCAD::linalg ... So BetterCAD::linalg is NOT
                          linked here and no linalg header is included"
CfdSparse.hpp            the CONTAINER is admitted and nothing more: "no
                          factorisation, no solve, no iteration"
TODO.md                   P19-LINSOLVE-001 owns "Pressure-system solver" and
                          "Momentum-system solver" BY NAME -- and it is NOT
                          AUTHORIZED
```

And a second boundary, in the same file: `P19-STEADY-001` owns "Validate
inputs", "Prepare mesh topology", "Prepare BCs", "Initialize fields",
"Convergence loop" and "Atomic result publication", with `CfdFailureStage`
living in `CfdResult.hpp` because it belongs to the result.

**TEN OF THE SEVENTEEN ITEMS BEGIN WITH THE WORD "DEFINE", AND THE NEXT
MILESTONE'S BEGIN WITH THE SOLVERS.** That is the boundary, and it is the
file's own rather than one chosen here. So this milestone defines the coupling
and consumes a caller-supplied solve, exactly as `P19-MOM-001` consumes a
caller-supplied pressure gradient; it links no solver, writes no factorisation
or iterative kernel, reads no `Document` and publishes no `CfdResult`.

### ADR-058 had already defined five of the seventeen items

The P19-CONT-001 lesson, exactly one milestone later. `CfdData.hpp` already
declares this checklist's answers:

```text
item  6  under-relaxation policy   CfdIterationSettings{0.7, 0.3}, "the classic
                                   SIMPLE pair", both in (0, 1], with validate()
item  9  stopping criteria         CfdConvergenceSettings{momentum, continuity,
                                   divergenceFactor}
item 10  maximum iterations        CfdSteadySettings{1000}, and reaching it "is
                                   a FAILURE ... never a result"
item 11  reject divergence         the divergenceFactor criterion itself
item  5  pressure reference        CfdReferencePressure, and
                                   PreparedCfdBoundary::referenceCell() ALREADY
                                   RESOLVES THE CELL -- P19-BC-001 computed it
item  7  residual evaluation       CfdResiduals fixes the record's shape
item  8  continuity closure        CfdMassBalance's two measures, produced by
                                   P19-CONT-001's qualified evaluator
```

So "Define X" means **bind to and compute the declared definition**, not invent
a second one. Without this phase the milestone would have built the second
parameter system `ARCHITECTURE.md` rejects — the same near miss `P19-CONT-001`
recorded, one milestone earlier.

### A fourth note: the central claim is checked by a QUALIFIED external measure

The face-flux correction exists so the corrected fluxes satisfy discrete
continuity. The instrument that measures exactly that — `evaluateCfdContinuity`
— was qualified on 2026-11-08 as an independent operator, written before this
one existed and without knowledge of it. So the milestone's headline claim is
validated against a qualified external measure rather than a restatement of its
own formula.

That trap is not hypothetical here: `P19-GRAD-001`'s r5 and `P19-CONV-001`'s
second review each found an "independent" oracle that was a
character-for-character transcription of production.

## BASELINE

```text
HEAD at authorization   6e45a80  (P19-CONT-001 recorded)
cfd subsystem           723,852 assertions in 544 test cases
full suite              6687 tests per preset
TODO.md                 688 ticked boxes, 342 unticked
```

## ARCHITECTURE

`ADR-075` — "The coupling consumes a supplied solve, and the correction residual
IS the solve residual". Twelve decisions; three candidates compared on eleven
axes for the one architecturally significant choice.

### The candidates for the solve

```text
A  a caller-supplied CfdLinearSolve port          CHOSEN
B  link BetterCAD::linalg privately and solve here
C  assemble only, return the systems, no driver
```

**B is rejected because it spends a decision that is not this milestone's.** It
is the shortest path and it would work; `ADR-069` Decision 3 records why buying
convenience with another milestone's decision is refused. **C is rejected
because it cannot satisfy items 9 through 14** — stopping criteria, maximum
iterations, divergence rejection, non-finite rejection, diagnostics and
deterministic iteration are all properties of a loop, and a milestone that
returns matrices has none of them.

A also turned out to be the stronger TESTING position rather than a concession:
the suite supplies a dense direct solve that is independent of BetterCAD
entirely, so the benchmark measures the coupling rather than a solver's
convergence.

### The measurements, made before any C++ existed

Seed `20261108`. Raw output in
[experiments/measure_pvc.out](experiments/measure_pvc.out) and
[experiments/q6.out](experiments/q6.out).

```text
Q1  one face visit vs two, for the interpolated face coefficient
      `1 - w` round-trips exactly          68.2728%
      w + w' == 1 exactly                  87.0376%
      TWO VISITS BIT-IDENTICAL             57.3703%      worst rel 1.241051e-14
Q2  the compatibility condition, sum of the right-hand side
      balanced                             6.217249e-15
      unbalanced                           5.327555e+00
Q3  the corrected flux against discrete continuity
      pinned matrix asymmetry              0.000000e+00
      worst |sum_f phi| before             7.485727e+00
      worst |sum_f phi| after              2.220446e-14
Q4  d_P from the unrelaxed diagonal        exactly 1/alpha too large
                                           (1.428571 at alpha_u = 0.7)
Q5  face-visit order and the divergence
      bit-identical under a shuffle        0 / 200       worst rel 1.918002e-12
Q6  div(phi_corrected) == A p' - rhs
      gap / throughput, exact solve        1.686762e-14
      gap / scale, DELIBERATELY WRONG p'   3.810534e-16
      worst |div| over cells != ref        1.199041e-14
      worst |div| AT the reference cell    2.020606e-14
      worst |compatibility sum|            5.329071e-15
```

Each decision these support:

```text
Q1 -> Decision 2   the face loop visits each face ONCE. A per-cell loop
                   recomputes the weight from the other side and agrees
                   bit-for-bit on only 57.3703% of faces, and
                   linalg::solveSymmetricSparse REQUIRES symmetry -- so a
                   two-visit assembly would produce a matrix the
                   already-qualified solver cannot accept
Q2 -> Decision 5   the compatibility check discriminates across fifteen orders
                   of magnitude, so the bound can sit anywhere between
Q3 -> Decision 4   clearing the reference row AND column leaves the matrix
                   exactly symmetric
Q4 -> Decision 6   d_P must use the RELAXED diagonal; the error is exactly
                   1/alpha_u, a 42.86% inflation at the default
Q5 -> Decision 7   every loop runs in FaceIndex / CellIndex order
Q6 -> Decision 5   the identity holds, and holds for a WRONG p'
```

### The identity, which is the milestone's centre

The correction adds `sum_f a_f (p'_P - p'_N)` to each cell's flux sum, which is
`(A p')_P`, and the system solved is `A p' = -div(phi*)`. Therefore

```text
div(phi_corrected)_P  ==  (A p')_P - b_P  ==  THE LINEAR SOLVE'S OWN RESIDUAL
```

exactly, as algebra and not as an approximation. Three consequences, and the
first would have been a defect:

```text
1  CONTINUITY CLOSURE IS EXACTLY AS GOOD AS THE PRESSURE SOLVE, NO BETTER.
   A hard-coded 1e-14 bound would refuse a conforming solver running at
   CfdLinearSolverSettings::relativeResidualTolerance = 1e-9 while it did
   nothing wrong.  The bound is DERIVED from that setting.
2  THE TEST ASSERTS THE IDENTITY, not merely that continuity came out small --
   strictly stronger, since it holds for a wrong p' too.
3  THE PINNED CELL IS NOT A WEAK SPOT.  Every other row states its own
   continuity, and the compatibility condition makes the global sum vanish, so
   the reference cell's residual is the compatibility error: 2.020606e-14
   against 1.199041e-14 elsewhere, measured rather than hoped.
```

### Two design corrections the derivation forced

```text
the velocity correction   `-d_P (grad p')_P` with d_P = V_P/a_P and the
                          Green-Gauss gradient has the CELL VOLUME CANCEL
                          analytically, leaving `-(1/a_P) sum_f p'_f S_f`: one
                          face loop, no volume, no gradient operator
why not the qualified     reconstructCfdPressureGradient imposes the PRESCRIBED
gradient operator         p_b at a FixedValue facet (src/cfd/CfdGradient.cpp's
                          `prescribedAt` returns facet.pressure), and the
                          pressure CORRECTION is ZERO there.  Using it would
                          impose the wrong boundary value at exactly the
                          boundaries that make the system non-singular.  That is
                          a correctness defect, not an inconvenience, and a test
                          DEMONSTRATES the disagreement rather than asserting it
```

### Why the qualified diffusion operator is not reused for the matrix

Considered seriously — the pressure-correction matrix has the shape of a
diffusion Laplacian — and rejected on two grounds, the second decisive:

```text
the coefficient   assembleCfdDiffusion computes Gamma_f = mu * A_f / d_f from
                  ONE uniform viscosity.  The correction needs a PER-FACE
                  rho * D_f where D_f interpolates V/a_P.  Admitting a per-face
                  coefficient means changing a qualified operator's API to serve
                  this checklist, which ADR-074 Decision 1 refused for exactly
                  this reason
the boundary      DECISIVE.  Diffusion's table is a VELOCITY table.  The
                  correction's condition is the PRESSURE column and is different
                  in kind: a prescribed flux gives p' zero gradient and
                  contributes NOTHING, a prescribed pressure gives p' = 0 and a
                  diagonal.  Reuse would apply the wrong condition, not merely
                  an awkward one
```

## BLAST RADIUS

```text
direct callers        NONE YET.  P19-STEADY-001 is the intended consumer and is
                      not authorized, so nothing in the tree calls this.
what it consumes      assembleCfdMomentum (P19-MOM-001), cfdMassFlux
                      (P19-CONV-001), evaluateCfdContinuity (P19-CONT-001),
                      cfdFaceInterpolationWeight (P19-DIFF-001),
                      CfdSparseMatrix (P19-DIFF-001), PreparedCfdBoundary
                      (P19-BC-001), CfdTopology/CfdGeometry
what it modifies      NOTHING.  No existing source file is changed; the three
                      CMake edits are additive registrations
serialization         UNTOUCHED.  Nothing here is persisted: a coupling
                      iteration is derived state, recomputed from the document
                      and the fields
stable references     UNTOUCHED
the CLI and the GUI   UNTOUCHED.  No command, no option, no panel
regression set        the FULL suite in three presets, because the operator
                      consumes five qualified operators and a defect in how it
                      calls one of them could surface anywhere they are used
```

## IMPLEMENTATION

```text
include/bettercad/cfd/CfdCoupling.hpp      new, the public API
src/cfd/CfdCoupling.cpp                    new, the implementation
tests/cfd/CfdCouplingTests.cpp             new, 54 cases / 1,870 assertions
tests/compile_fail/CfdCouplingMisuse.cpp   new, 37 cases
src/cfd/CMakeLists.txt                     one line
tests/CMakeLists.txt                       one line
tests/compile_fail/CMakeLists.txt          the 37 registrations
docs/architecture/decisions/ADR-075-*.md   new
```

**NO EXISTING SOURCE FILE IS MODIFIED.**

Four public entry points, which map onto the checklist:

```text
cfdCouplingPredictor            items 1, 12, 13     relax, solve, expose a_P
assembleCfdPressureCorrection   items 2, 5          the symmetric system
applyCfdCouplingCorrection      items 3, 4, 6       the three corrections
advanceCfdCoupling              items 7, 8, 14      one outer iteration
cfdCouplingVerdict              items 9, 11         the criteria
cfdCouplingBudgetExhausted      item 10             the budget
```

### What the compiler corrected, recorded because the result is better

`CfdPressureCorrectionSystem` and `CfdCouplingIteration` have **no default
constructor**, because `CfdSparseMatrix`'s is private (`ADR-036`, possession is
the evidence). The first draft built an iteration incrementally and filled it
in; the compiler refused, so `advanceCfdCoupling` now constructs its result
WHOLE at the end. That is also the better shape — a half-built iteration cannot
exist, which is the atomicity rule applied by the type system rather than by
discipline. The same correction `P19-CONT-001` took on its report.

## TESTS

54 Catch2 cases / 1,870 assertions, and 37 compile-fail cases.

```text
cfd subsystem before   723,852 assertions in 544 cases
cfd subsystem after    725,722 assertions in 598 cases
delta                    1,870              and  54
this suite               1,870 assertions in  54 cases     EXACTLY
```

A deleted test and an added test net to nothing in a pass/fail count. They do
not net to nothing here, so **no pre-existing cfd assertion moved**, and the
continuity suite still reads 7,435 in 49.

### The independent reference

A dense Gaussian elimination with partial pivoting, written in the test file,
sharing no line with any operator under test. It also computes the residual in
`ADR-052`'s normalised form, so the port reports a number the operator can act
on. Two analytic benchmarks sit on top of it:

```text
a closed domain at rest   an EXACT fixed point: u = 0 with p constant is the
                          steady solution, so p' must be exactly zero, the
                          velocity must not move and every face flux must stay
                          bit-zero.  Nothing is read from the implementation
a duct, inlet to outlet   whatever the fields converge to, mass must be
                          conserved globally, and the corrected flux must
                          satisfy discrete continuity at EVERY iteration rather
                          than only at the end
```

### Four test defects of the author's own, each found by a failing assertion

```text
1  the row-sum check used an ABSOLUTE 1e-9 bound and failed at 2.12e-09.
   Made relative, it STILL failed -- at HALF the row's largest entry, which is
   the real answer: a neighbour of the pinned cell lost its off-diagonal when
   that column was cleared, so its row no longer cancels.  The row sum is the
   magnitude of the removed coefficient, and the test now asserts THAT, which
   is a stronger claim than cancellation.  Both earlier forms are recorded in
   the test's own comment
2  a MovingWall bound to a whole tetrahedral boundary is REFUSED by
   bindCfdBoundary: "its target is not planar ... tangency has no meaning on a
   curved target".  And its velocity must be TANGENTIAL to the facet.  The
   tangent is now CONSTRUCTED from the facet's own outward normal
3  the port-residual test tried to force a refusal by SCALING the dense solve's
   reported residual by 1e12.  It still passed, because that solve's residual
   is at or near zero and scaling zero is zero.  The report is now STATED
4  the compile-fail guards were written as `#if defined(CASE)` and the
   registration defines `BETTERCAD_CF_CASE`, so every translation unit was
   EMPTY, every build succeeded and all 37 gates reported "exit code 0,
   expected 1".  THE GATES CAUGHT IT.  Had they been written to expect success,
   37 vacuous tests would have passed
```

### A limit of the compile-fail group, stated rather than implied

The unit types do hold: a density, a pressure and a velocity are distinct types.
**The spans do not.** Every field is `std::span<const double>` of SI values
(`ADR-066`), so a velocity span and a pressure span are the SAME TYPE, and
swapping them is a runtime length or finiteness refusal at best and a silent
wrong answer at worst. No compile-fail case can hold it, and the group's header
says so.

## ADVERSARIAL REVIEW

Two independent reviews, both BEFORE any freeze, as `CLAUDE.md`'s lifecycle
requires and as `P19-CONV-001` established. Each was given the code, the
checklist and the ADR, and explicitly NOT the author's conclusions. One was
pointed at correctness and architecture, the other at test adequacy.

**BOTH WERE READ-ONLY, AND THAT WAS A DELIBERATE CHANGE.** On `P19-CONT-001` a
reviewer with build access left a live mutation in `src/cfd/` and a backup file
inside the frozen surface. `CLAUDE.md` says "reviewers challenge the
implementation, they do not race it", and the fix is structural rather than
procedural: a reviewer that cannot write cannot leave anything behind. Both were
instructed to describe experiments rather than run them, and the mutation probes
below were run by the author afterwards.

```text
review A, correctness      18 findings, 16 CONFIRMED and 2 SUSPECTED
review B, test adequacy    15 weak or vacuous assertions, coverage mapped to
                           all 17 checklist items, and 22 PREDICTED MUTATION
                           SURVIVORS with a per-mutation prediction
```

### The sharpest finding: a guard this project had already lost twice

`CfdTopology::owner` is invalid for a face the topology does not hold,
`CellIndex::kInvalid` is `SIZE_MAX`, and this operator WRITES through that index
— into the symbolic pattern, the right-hand side and the per-cell face sums.
`geometry.describes` compares the mesh stamp and the counts and never the owner
and neighbour ranges, so nothing upstream establishes it.

`CfdConvection.hpp` records that `P19-DIV-001`'s review and then
`P19-CONV-001`'s review each found an unguarded `SIZE_MAX` index reaching
writes, and says in as many words: **"It is not regressing a third time."** It
regressed a third time, here. `CfdCouplingProblem::TopologyNamesAnUnknownCell`
is added and runs as phase 2, as every sibling operator has it.

### Six more production defects, all fixed

```text
1  TWO ENUMERATORS COULD NEVER BE PRODUCED. `FieldIsNotFinite` and `Diverged`
   appeared only in the array and in `toString`, and both were DOCUMENTED as
   live refusals -- one cross-referenced as the in-iteration counterpart of
   `ValueNotFinite`. `CfdBoundary.hpp` states the rule: "An enumerator no input
   can produce is worse than no enumerator." Deleted; seventeen became sixteen.
2  THE REFUSAL ORDER AND THE BLAME WERE BOTH WRONG in `advanceCfdCoupling`. It
   validated settings and ran the predictor and `cfdMassFlux` before its own
   gate, so a geometry built for another mesh was reported as `SettingsInvalid`
   or `LinearSolveRefused` and phase 1 was unreachable until after three linear
   solves. And a caller's bad DENSITY came back as `EvaluationIsInconsistent`
   with `ErrorCode::Internal` -- this module blaming itself for the caller's
   input, the exact category error `P19-MOM-001`'s review found in its own first
   draft.
3  THE PIN ASSUMED A CONNECTED MESH. The flags were global and the pin fixes one
   cell, so a pressure boundary on one disconnected region made the operator
   conclude "nothing to pin" while another region's constant mode stayed free --
   and a free mode is exactly the precondition `solveSymmetricSparse` needs.
   Union-find over internal-face adjacency now decides per region.
4  DIVERGENCE FIRED ON ANY GROWTH FROM A ZERO FIRST RESIDUAL. `now > factor * 0`
   is true for any positive residual, so a run that started exactly satisfied --
   which is precisely the closed-domain-at-rest benchmark on its first iteration
   -- was reported DIVERGED on its second.
5  AN ABSOLUTE kg/s NUMBER WENT INTO A DIMENSIONLESS FIELD. Where the boundary
   throughput is zero, `ADR-058`'s measures are undefined, and the first repair
   put `largestCellResidual` straight into `CfdResiduals::continuity`, which
   feeds a `1e-6` tolerance. The scenario is concrete: at millimetre scale with
   water a wholly non-conservative field reports ~1e-9 and is called CONVERGED,
   so in a closed domain the continuity criterion was effectively disabled --
   and every REACHABLE pure-Neumann run lands in that branch.
6  THE CLOSURE BOUND COMPARED TWO DIFFERENT NORMALISATIONS. It multiplied the
   solver's L2-over-matrix-scale residual by a slack and compared it with an
   infinity-norm-over-throughput measure; the ratio between those drifts with
   mesh size. The bound is now the system's own residual IN THE SAME MEASURE,
   and `kCfdCouplingClosureSlack` is deleted -- comparing a quantity with itself
   in its own norm needs no slack factor.
```

Four smaller ones: the array test was a tautology over the array and there was
no `static_assert` on its size (every sibling has one); `SettingsInvalid` named
four settings types and only two were ever validated; the allocation-free
problem predicate every sibling exposes was missing, which left the shared
gate's `noexcept` a claim with no caller behind it; and the pinned diagonal's
"keeps its own value" rule had an unstated exception.

### A finding that was credible and WRONG, settled by measurement

Review A worked the algebra and concluded the central identity must FAIL at a
pinned cell: that row of `A` and its right-hand side are cleared, so the
residual is zero there by construction while the measured flux sum "ought to be"
the unpinned system's compatibility residual. The reasoning was sound as far as
it went, and the gap it pointed at was real — **neither identity test reached
the pinned case at all**, because both used a fixed-pressure outlet.

So a test was built for it, and it measured both sides: `-1.08e-19` against
`0`. **They agree.** The mechanism is the compatibility condition the ADR
already required: pinning happens only where no boundary fixes the pressure, so
every boundary prescribes its flux and the gate requires those to balance, and
global conservation then forces the pinned cell's sum to the negative of all the
others — each zero to solve accuracy. `ADR-075` Q6's figures had said this
rather than the opposite: `2.020606e-14` at the reference cell against
`1.199041e-14` elsewhere is the same order, not a structural break.

The finding is recorded in full because it was the right question, and because
what settled it was a measurement rather than an argument.

### A SUSPECTED finding this milestone's own tests CONFIRMED

Review A suspected that `MomentumDiagonalNotPositive` refuses a legitimate
physical state — upwind convection puts a negative mass flux on the diagonal at
a zero-gradient-velocity boundary, where diffusion adds nothing — and said it
had not built the mesh. The new tests built it: a ten-cell chain at 5 mm/s with
water assembles an x-momentum diagonal of **`-8.085e-04` kg/s**, from a correct
assembly with nothing wrong anywhere.

The refusal stands, because `d_P = V_P / a_P` has no usable value, but it is a
**hard problem and not a defective assembly**, and the header had said the
opposite. It is recorded as `L9`, and it constrains what this operator can run:
the benchmark's inlet speed is now DERIVED from the cell Peclet number rather
than picked, because at `Pe = 200` the coupling refuses rather than converges.

### The structural finding about the tests, which no identity test could reach

Review B's central point, and it is exactly right:

> the identity tests compare two PRODUCTION outputs, so they are satisfied by
> **any** face coefficient — and the corrected flux is INVARIANT under a uniform
> rescaling of it, because the correction is a projection: the matrix scales, the
> correction scales inversely, and their product does not move.

So a coefficient wrong by a factor of two, wrong by seven orders of magnitude,
read from the wrong component, or interpolated from the wrong side would have
passed the whole suite. The milestone's headline test could not see its own
central quantity.

The coefficient's VALUE is now pinned directly, against a diagonal that varies
**per cell and per component** — with a constant one, interpolating between two
equal values returns that value for every weight and the normal projection
collapses to a single multiplication, so neither was exercised at all. The
oracle forms `d = V/a` from the geometry itself and groups the product
differently from production.

Nine further gaps the same review named are closed:

```text
the predictor's velocity        read by NO assertion, so a predictor that
                               discarded the solve would have survived
the velocity correction's value pinned by nothing: one test saw zero and one
                               needed only "> 0.0"
continuityClosed == false       never observed, so `= true` would have survived
the momentum residual           the suite already contained an independent
                               transcription of ADR-052's normalisation and
                               never used it as an oracle
the balanced compatibility case the only one used an all-zero flux, so the
                               throughput was zero and the assertion tested a
                               hard-coded 0.0
a reference cell that is not 0  every fixture put the datum at cell 0
strict vs non-strict            no residual ever sat exactly AT a tolerance
the measured tolerances         the static_asserts were one-sided, so any amount
                               of LOOSENING was admitted
the benchmark                   asserted that it RAN, not that it converged --
                               and could not converge, because no fixture fed
                               the pressure gradient back into the momentum
                               assembly, so the pressure was a pure output
```

Slip walls and pressure inlets were never exercised at all; the imports sat
unused. A slip wall matters more than coverage arithmetic suggests: its
`Symmetry` velocity is the only treatment that makes the three momentum matrices
differ per component, so it is the only input that exercises Decision 11's
normal projection non-trivially.

### Findings recorded rather than fixed

```text
L10  no Rhie-Chow interpolation, so the classic odd-even pressure mode is not
     suppressed. `CfdMomentum.hpp` defers Rhie-Chow to `P19-PRES-001` BY NAME,
     and the compact two-point Laplacian is non-singular so nothing refuses.
     Raised as SUSPECTED; recorded because it is a textbook mechanism this
     operator does not address and its omission was not among the three the
     header declared.
L11  the velocity correction applies no `Symmetry` projection at a slip wall.
     Mass is still conserved because the wall flux stays bit-zero, so it is an
     inconsistency between two operators rather than a conservation break.
L12  the symbolic pass is a THIRD copy in this module.
L13  the fixtures are a SEVENTH copy, and no shared cfd fixture header exists.
```

The compile-fail group was also challenged, and the challenge is accepted: the
unit types hold but `density` is the only unit-typed scalar in the whole
coupling API, and roughly a third of the group restates C++ language rules
rather than this design's decisions. The group's header already says the part
that matters — every field is a span of SI doubles, so swapping two of them
cannot be a compile error and is not caught at runtime either.

## REGRESSION

Run as the qualification gate itself, from clean, on the frozen tree. Every
figure below was counted out of the gate's own logs after the run, not predicted
before it.

### The full suite, three presets, each configured and built from clean

```text
preset             configure  clean  build  no-op  ctest   tests  failed  error:  warning:
debug-ext             exit 0  exit 0 exit 0 exit 0 exit 0   6802       0       0         0
release-ext           exit 0  exit 0 exit 0 exit 0 exit 0   6802       0       0         0
debug-shared-ext      exit 0  exit 0 exit 0 exit 0 exit 0   6802       0       0         0

6802 = 6687 (P19-CONT-001) + 79 unit + 36 compile-fail     EXACTLY, no fudge term
```

`ctest`'s own summary reads `100% tests passed out of 6802` in all three. The
three counts a log can disagree on agree:

```text
preset             Start markers   result lines   progress-prefixed
debug-ext                   6802           6802                6802
release-ext                 6802           6802                6802
debug-shared-ext            6802           6802                6802
```

Equality of the first two is the check that **no test was started and left
unreported**, which a pass percentage cannot see.

Both builds that are not `debug-ext` were also compiled before the freeze, on
purpose: `P19-MOM-001` lost sixty-six minutes of its gate to a release-only
`-Werror=array-bounds` at `-O3`. Nothing release-only appeared, here or there.

The no-op rebuild re-ran only the git-revision step in each preset
(`[1/8] Checking git revision`, and `[1/20]` for the shared build), so the build
is idempotent and nothing in the tree regenerates itself on a second pass.

### The repeat set: determinism, not a second pass

```text
preset        selected   Start markers   result lines   progress-prefixed   markers/progress
release-ext       3434           17170          17170                3434             5.0000
debug-ext         3434           17170          17170                3434             5.0000

3434 = 3355 (P19-CONT-001) + 79 matching unit\.Cfd         EXACTLY
17170 = 3434 x 5
```

**Five executions of every one of 3434 tests, in two presets, 0 failures.**

A log of this run holds THREE different counts that all deserve the name
"result line", and the freeze record's sentence names only two of them. Measured:
`ctest --repeat until-fail:5` prints one `Start` marker and one
`Test #N: name ... Passed` line **per execution** — 17,170 of each — and prefixes
the LAST execution of each test with a progress counter, `k/3434`, giving 3,434
such lines with every index from 1 to 3434 present exactly once. So the ratio
that must be 5.000 is markers to PROGRESS-PREFIXED lines, not markers to
`Passed` lines, which are equal. `FREEZE.md` says "prints ONE result line per
test and FIVE `Start` markers"; that is true of the progress-prefixed line and
false of the `Passed` line, and the sentence does not distinguish them. The
figures it predicts are the right ones; the wording would mislead the next
reader, and did mislead this one — see `r24`.

### What a pass/fail count cannot check, measured separately

A deleted test and an added test net to nothing in a test count. They do not net
to nothing in an assertion count, and the count was read **out of the three
qualified binaries**, after the gate had built each from clean:

```text
filter   debug-ext              release-ext            debug-shared-ext
[pvc]    4950 in 79 cases       4950 in 79 cases       4950 in 79 cases
[cfd]  728802 in 623 cases    728802 in 623 cases    728802 in 623 cases
[cont]   7435 in 49 cases       (unchanged from P19-CONT-001)

728802 - 4950 = 723852   the pre-PVCOUPLE cfd figure      EXACTLY
   623 -   79 =    544   the pre-PVCOUPLE cfd case count  EXACTLY
```

**Identical in all three presets**, which is the claim that `-O3`, `-O0` and a
shared-library layout do not change which assertions run. No pre-existing cfd
assertion moved.

The compile-fail group, in the qualified runs rather than in a developer build:

```text
preset             compile_fail.cfdpvc results   distinct names   failures
debug-ext                                   36               36          0
release-ext                                 36               36          0
debug-shared-ext                            36               36          0

36 registered = 32 enumerated + 4 first-of-family = 36 `#if defined` guards
```

### The build trees were really rebuilt

The naive question — "what is the oldest compiled output in the build tree?" —
answers `libgcc_s_seh-1.dll`, dated 2026-07-30, in all three presets, and that
is not a stale object: CMake's copy carries the ORIGIN's mtime, so a copied
runtime DLL always looks older than the source. `P19-CONT-001` recorded that as
its `r17`. Scoped to what this project actually compiles and links, with a
global sort:

```text
preset             objects  older than clean   own linked  older   copied deps  content mismatch
debug-ext              686                 0           23      0            37                 0
release-ext            686                 0           23      0            37                 0
debug-shared-ext       686                 0           36      0            37                 0
```

The 37 excluded binaries per preset are excluded **by provenance, not by
convenience**: every one was hashed against its origin in the dependency prefix
or the WinLibs toolchain and is byte-identical. Not one object and not one
linked output of this project predates its own preset's clean.

### The caches, re-verified after the run

```text
preset             OpenCASCADE_DIR                  CMAKE_COMMAND   msys64 refs
debug-ext          <deps>/lib/cmake/opencascade     WinLibs                   0
release-ext        <deps>/lib/cmake/opencascade     WinLibs                   0
debug-shared-ext   <deps>/lib/cmake/opencascade     WinLibs                   0
```

and the three differ in exactly the variables they should: `CMAKE_BUILD_TYPE`
`Debug`/`Release`/`Debug`, `BUILD_SHARED_LIBS` `OFF`/`OFF`/`ON`.

## QUALIFICATION

```text
harness              docs/verification/P19-PVCOUPLE-001/qualification/
stages               17, every one exit 0
harness's verdict    "Qualification passed: every stage exited 0."
failed-stage count   qualification-times.txt: "0 stage(s) failed"
candidate            6e45a80d9057cbc0e1042b404d02abdc0bd5504f
wall clock           3h 32m 36.7s, 00:45:22.97 to 04:17:59.64 on 2026-11-09
```

The verdict is taken from the harness's own sentence and the failed-stage count,
**not from the launcher's exit code**: `P19-MOM-001`'s first attempt was reported
to this session as "completed (exit code 0)" while the harness had written
"QUALIFICATION FAILED: 2 stage(s) failed".

```text
stage                           duration
debug-ext        configure        0m 02.9s
                 build           17m 45.4s
                 no-op rebuild    0m 00.4s
                 ctest           41m 40.3s
release-ext      configure        0m 03.0s
                 build           19m 25.4s
                 no-op rebuild    0m 00.4s
                 ctest           38m 28.8s
debug-shared-ext configure        0m 02.7s
                 build           17m 21.3s
                 no-op rebuild    0m 00.4s
                 ctest           41m 42.6s
repeat release-ext               16m 55.0s
repeat debug-ext                 19m 02.5s
```

### The qualified tree is the committed tree

The eight frozen paths were read **four** times by two independent methods — the
freeze record before the launch, the harness before its first build, the harness
after its last test, and a recomputation after the run — and all four agree:

```text
apps               fc398a3d2e524178cf38e8b667236379d8381086
include            bf2e12be62617a78fafd6b95b6b18801c28d3e0e
src                e640a27ca777a976a0d168cd007d73171b8c6496
tests              17558fc3b6d1a1f524ae0a5a431448aa65b3a840
examples           4da234ebf4779f7c16d5df0075f106f7e3d4c958
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e
```

Three subtrees moved from `P19-CONT-001`'s frozen tree and they are the right
three. `TODO.md`, `ROADMAP.md` and this directory are outside the frozen surface,
which is why they could be written after the run.

That the content was unchanged is worth stating as a measurement rather than an
assumption, because the mtimes of three frozen files DID move after the freeze
record was written. They moved at 00:32:31 and 00:32:57 — before the record was
written at 00:41:06 and before the gate started at 00:45:22 — and the mutation
probes, which rewrite `src/cfd/CfdCoupling.cpp` in place, finished at 00:38:36.
No probe overlapped the gate, and `git write-tree` hashes content, so the four
agreeing readings are the proof the mtimes are not.

## RESULT

```text
TASK:            P19-PVCOUPLE-001
IMPLEMENTATION:  pressure-velocity coupling for steady incompressible flow --
                 SIMPLE built around a CALLER-SUPPLIED linear solve, because
                 the solvers themselves belong to P19-LINSOLVE-001 by name
TESTS:           79 unit cases / 4,950 assertions, plus 36 compile-fail gates;
                 6802 tests per preset in three presets, 0 failed
VALIDATION:      the correction residual IS the supplied solve's own residual,
                 an identity checked against the production operator and
                 measured at the noise floor; an independent channel benchmark
                 at cell Peclet 0.199 derived from Pe = rho*u*dx/mu
REGRESSION:      3 presets from clean + 2 repeat presets at 5 executions,
                 17170 executions each, 0 failures
RESULT:          PASS
EVIDENCE:        docs/verification/P19-PVCOUPLE-001/
                   README.md, FREEZE.md,
                   qualification/ (23 logs + the harness),
                   experiments/ (measurements and two probe rounds)
                 docs/architecture/decisions/ADR-075-...md
TODO:            17 boxes ticked, 688 -> 705
```

What this does NOT claim is set out under KNOWN LIMITATIONS in `ADR-075`
(`L1`-`L13`). The two that bind a reader hardest: the operator is confined to
low cell Peclet number, because a non-positive assembled momentum diagonal is
REACHABLE from a correct assembly and was measured at `-8.085e-04` kg/s on a
ten-cell chain (`L9`); and `M23`, the disconnected-region flag, is reported as a
documented-unreachable state rather than as covered, because no fixture builds a
mesh that produces it.

## REVISION

**r1** — the pasted checklist was diffed against `TODO.md`'s own section before
any work, byte-for-byte across 36 lines, 17 unticked and 0 ticked counted rather
than assumed, sha256 `43a305f28e5dd8f0`; and the authorization plus five scope
notes were written into `TODO.md` BEFORE any implementation, then the section
re-diffed to confirm the edit had not touched it.

**r2** — the UNDERSTAND phase found that SIMPLE's two linear solves belong to
`P19-LINSOLVE-001` by name, in three independent committed records, and that
`cfd` neither links nor includes `linalg`. The architecture changed from "write
SIMPLE" to "write SIMPLE around a supplied solve". Without that phase this
milestone would have spent a decision `ADR-069` Decision 3 deliberately
deferred.

**r3** — the same phase found that `ADR-058` already declares five of the
seventeen items' answers, and that `PreparedCfdBoundary::referenceCell()`
already resolves the pressure datum. "Define X" became "bind to and compute the
declared definition". This is the `P19-CONT-001` lesson recurring exactly one
milestone later.

**r4** — `ADR-075` Q1's FIRST FORM WAS WRONG AND REVERSED ITS OWN CONCLUSION.
It drew the interpolation weight from `rng.random()`, which returns `k * 2**-53`,
so `1 - w` is exact BY CONSTRUCTION OF THE SAMPLE and the answer came back
100.0000% — a property of the sampler, not of the assembly. A real weight is a
QUOTIENT of two distances. Re-drawn, the true answer is 57.3703%. The decision
it supports was right either way, which is why it is recorded as the near miss
it was: a correct decision resting on a broken measurement.

**r5** — Q6's first form scored the identity with a per-component relative gap,
which reports `2.0` for two values at the noise floor with opposite signs, and
so reported `2.000000e+00` for an identity that holds. Rescaled to the boundary
throughput, `1.686762e-14`.

**r6** — the velocity correction's first design called the QUALIFIED pressure
gradient operator, and that is wrong: `reconstructCfdPressureGradient` imposes
the prescribed `p_b` at a fixed-pressure facet and the pressure CORRECTION is
zero there. Found by reading `src/cfd/CfdGradient.cpp` rather than its header.
Working the algebra then removed the gradient entirely — the cell volume cancels
against `d_P = V_P / a_P` — so the correction is one face loop with no volume
and no gradient operator.

**r7** — reading `treatmentOf`'s SOURCE rather than its header's table corrected
a name: the enumerator is `Slip`, while the header's table prints `SlipWall`.
The same read established the exclusivity the pressure-correction boundary
condition rests on — a boundary prescribes the flux or the pressure, never both
and never neither — which is now asserted over `kCfdBoundaryConditionKinds`
rather than trusted.

**r8** — a self-inflicted tooling defect, the third of this milestone and the
one with the widest reach. In Git Bash a `PATH` entry must be a POSIX path, so
`export PATH="C:/Users/.../mingw64/bin:$PATH"` put NOTHING on the path: every
local build this session ran MSYS2's cmake 4.4.3 instead of the pinned WinLibs
4.4.1. The compiler still came from the cache, so the object code was
representative, but `CMAKE_COMMAND` was MSYS2's and the tree carried five
`msys64` references. The tree was DELETED and reconfigured with the pinned
cmake, after which it carries zero — the first clean cache of the session. A
reconfigure also had to be repaired: deleting the tree before confirming it
could be rebuilt lost the Catch2 download until `CURL_CA_BUNDLE` and
`SSL_CERT_FILE` were restored.

**r9** — THE OWNER/NEIGHBOUR RANGE GUARD WAS MISSING, and `CfdConvection.hpp`
records in as many words that two prior reviews forced it into its siblings:
"It is not regressing a third time." It regressed a third time, here. The guard
protects WRITES — `CellIndex::kInvalid` is `SIZE_MAX` and the symbolic pattern,
the right-hand side and the per-cell face sums are all written through that
index — and `geometry.describes` compares the mesh stamp and the counts, never
the ranges. `TopologyNamesAnUnknownCell` is added and runs as phase 2.

**r10** — two enumerators were DELETED because no code path could produce them.
`FieldIsNotFinite` and `Diverged` appeared only in the array and in `toString`,
and both were documented as live refusals — one cross-referenced as the
in-iteration counterpart of `ValueNotFinite`. `CfdBoundary.hpp` states the rule:
"An enumerator no input can produce is worse than no enumerator." Seventeen
became sixteen, and the `static_assert` on the array's size that every sibling
carries was added; without it the suite's array test was a tautology over the
array.

**r11** — `advanceCfdCoupling` ran its own gate LAST, so a stale geometry was
reported as `SettingsInvalid` or `LinearSolveRefused` and phase 1 was
unreachable until after three linear solves; and a caller's bad density reached
`cfdMassFlux` and came back as `EvaluationIsInconsistent` with
`ErrorCode::Internal` — this module blaming itself for the caller's input, which
is the category error `P19-MOM-001`'s review found in its own first draft. The
phases that need neither the predicted flux nor the diagonal now run first, in
the declared order.

**r12** — the pin assumed a CONNECTED mesh. The flags were global and the pin
fixes one cell, so a pressure boundary on one disconnected region made the
operator conclude "nothing to pin" while another region's constant mode stayed
free — and a free constant mode is exactly the precondition
`linalg::solveSymmetricSparse` needs. Union-find over internal-face adjacency
now decides per region, and the compatibility condition is per region too. That
second change incidentally made `relativeDeparture`'s own justification TRUE at
its call site: a region's net and its own throughput are sums over the same
faces and vanish together, whereas the global form divided a sum over all cells
by a scale from the boundary alone and silently returned an absolute kg/s
number into a field documented as relative.

**r13** — the closure bound compared two DIFFERENT normalisations. It multiplied
the solver's `L2`-over-matrix-scale residual by a slack of `1e2` and compared it
with `maxCellContinuity`, an infinity norm over the boundary throughput; the
ratio between them drifts with mesh size. The bound is now the system's own
residual in the SAME measure, computed here, plus the floor, and
`kCfdCouplingClosureSlack` is deleted — comparing a quantity with itself in its
own norm needs no slack factor. Its compile-fail case went with it (37 → 36).

**r14** — AN ABSOLUTE kg/s NUMBER WAS GOING INTO A DIMENSIONLESS FIELD, and the
first repair of the closed-domain case is what put it there. The review's
scenario was concrete: at millimetre scale with water a wholly non-conservative
field reports about `1e-9` against a `1e-6` tolerance and is called CONVERGED,
so in a closed domain the continuity criterion was effectively disabled — and
every reachable pure-Neumann run lands in that branch. The interior flux is the
honest denominator, `CfdMassBalance` is deliberately not filled from it, and an
absent balance is no longer treated as the same thing as a closed domain, which
had refused an open duct started from rest as an internal inconsistency.

**r15** — and the interior scale then had to be taken from the PREDICTED flux
rather than the corrected one, which a failing test showed within minutes: the
correction's whole purpose is to drive the corrected interior flux toward zero,
so scaling by it divides a noise-floor residual by a noise-floor scale. It
reported `0.375`, which is two numbers at the noise floor and nothing else.

**r16** — divergence fired on ANY growth from a zero first residual, because
`now > factor * 0` is true for any positive residual. The configuration that
reaches it is this milestone's own analytic benchmark: a closed domain at rest
has exactly zero residuals on iteration 1, so it would have been reported
DIVERGED on iteration 2. The ratio test now needs a positive baseline.

**r17** — `SettingsInvalid` named four settings types and only two were ever
validated. `CfdConvergenceSettings` and `CfdSteadySettings` reach this operator
through two `noexcept` functions that return a verdict and a `bool` and have no
channel to refuse through, so a `divergenceFactor` of `0.5` — which `validate`
rejects — made the FIRST iteration diverge, and a NaN tolerance made the verdict
`Continue` forever by accident. The enumerator's documentation is narrowed to
the two it covers, the criteria report `Continue` for unusable settings by
decision rather than by accident, and `L7` records the precondition.

**r18** — A REVIEW FINDING WAS REFUTED BY MEASUREMENT, and both the finding and
the refutation are recorded. The correctness review worked the algebra and
concluded the central identity must fail at a pinned cell. The gap it pointed at
was real — neither identity test reached the pinned case, because both used a
fixed-pressure outlet — so a test was built for it. Both sides measured
`-1.08e-19` against `0`: they agree, because pinning happens only where every
boundary prescribes its flux, the gate requires those to balance, and global
conservation then forces the pinned cell's sum to the negative of all the
others. `ADR-075` Q6's own figures had said this rather than the opposite. The
header's carve-out, added in response to the finding, was removed again and
replaced with the mechanism and the measurement.

**r19** — a SUSPECTED finding was CONFIRMED by this milestone's own new tests.
`MomentumDiagonalNotPositive` refuses a legitimate physical state: a ten-cell
chain at 5 mm/s with water assembles an x-momentum diagonal of `-8.085e-04`
kg/s, from a correct assembly. The refusal stands, because `d_P = V_P / a_P` has
no usable value, but it is a hard problem and not a defective assembly, and the
header said the opposite. It is `L9`, and it constrains what this operator can
run — the benchmark's inlet speed is now derived from the cell Peclet number
rather than picked, because at `Pe = 200` the coupling refuses rather than
converges.

**r20** — the test-adequacy review's structural finding is the one no identity
test could have reached: the corrected flux is INVARIANT under a uniform
rescaling of the face coefficient, because the correction is a projection. A
coefficient wrong by a factor of two, by seven orders of magnitude, read from
the wrong component or interpolated from the wrong side would have passed the
entire suite. The value is now pinned directly, against a diagonal varying per
cell AND per component — with a constant one the interpolation returns its
argument for every weight and the normal projection collapses to one
multiplication, so neither was exercised at all.

**r21** — nine further test gaps were closed, each named by the same review: the
predictor's velocity was read by no assertion; the velocity correction's value
by none; `continuityClosed == false` was never observed; the suite contained an
unused independent transcription of `ADR-052`'s normalisation and never compared
it with production's; the only balanced compatibility case used an all-zero flux
so the assertion tested a hard-coded `0.0`; every fixture put the reference cell
at cell 0; no residual ever sat exactly at a tolerance; the measured tolerances'
`static_assert`s were one-sided and admitted any amount of loosening; and the
benchmark asserted that it RAN rather than that it converged — and could not
converge, because no fixture fed the pressure gradient back into the momentum
assembly, so the pressure was a pure output. Slip walls and pressure inlets were
never exercised at all, and a slip wall is the only treatment that makes the
three momentum matrices differ per component, so Decision 11's normal
projection had no non-trivial test.

**r22** — the compile-fail group caught a defect of its own, twice. First its
guards were written `#if defined(CASE)` while the registration defines
`BETTERCAD_CF_CASE`, so every translation unit was EMPTY, every build succeeded
and all 37 gates correctly reported "exit code 0, expected 1" — had they been
written to expect success, 37 vacuous tests would have passed. Then, after two
enumerators were deleted, one case still referenced `CfdCouplingProblem::
Diverged` and failed for the wrong reason; the gate caught the stale reference.

**r23** — a fourth tooling defect of the author's own, from the same family as
`r8`: `cmd.exe /c` from Git Bash has its `/c` path-converted into `C:\`, so the
mutation driver started an interactive shell and exited 0 having done nothing,
twice, silently. The `//c` form did not help either. The driver is now launched
through this session's PowerShell tool. Recorded because the failure mode is the
one this milestone keeps meeting — a tool that reports success while doing
nothing — and because a probe table produced by a driver that never ran would
have read as strong evidence.

**r24** — THE RECONCILIATION NEARLY RECORDED THREE EARLIER MILESTONES' COMMITTED
EVIDENCE AS FALSE, on the strength of a measurement tool that was itself
miscounting. `grep -c -E '\.\.\.[[:space:]]+Passed'` on the repeat log answered
17,170 where `P19-CONT-001`'s README records 3,355 "result lines" for its own
run, and `grep -c -E '[0-9]+/[0-9]+[[:space:]]+Test #'` answered a flat 2,810 for
`P19-CONT-001`, `P19-MOM-001` and this milestone alike — the same number for
three different test counts, which cannot be a property of three different runs.
That constant was the tell. Recounted with `awk` and explicit `[ ]+` classes,
the log holds 17,170 `Start` markers, 17,170 `Passed` lines and 3,434
progress-prefixed lines carrying every index from 1 to 3434 exactly once, and
the earlier milestones' figures are RIGHT under the reading "result line = the
progress-counted line". This is the third measurement-tool defect of the
milestone, after `ADR-075`'s `D1` and `D2`, and the first that would have
produced a false accusation rather than a false reassurance. The fix applied to
the evidence is not a correction of the numbers but of the WORDING, which did not
distinguish the two kinds of line; the REGRESSION section now names all three
counts.

**r25** — a background shell from the implementation phase was still alive when
the gate finished, nine hours after it was launched, and it was a job whose
script REWRITES `src/cfd/CfdCoupling.cpp`. It had written nothing — its heredoc
left `python` reading an empty stdin, so it hung before reaching the edit, and
its output file was zero bytes — and the four agreeing tree readings confirm the
frozen surface never moved. It was stopped before any evidence was written. The
hazard is the `P19-CONT-001` reviewer-backup hazard in a different disguise: a
process with write access to the frozen surface outliving the phase that needed
it. The structural remedy this milestone already applied to reviewers — make
them read-only — does not reach a background job of the author's own, and the
practice this adds is to account for live background work as part of the freeze,
not after the gate.

**r26** — two evidence gaps in this directory, recorded rather than tidied
away. `experiments/probes.json` holds ROUND TWO only: it was overwritten
rather than versioned, so round one's twenty-four exact substitutions are not
reproducible from what is committed, and what survives of that round is each
probe's description and verdict in `mutations-round1.out` together with the
sha256 pair that bounds it — the table can be read but not re-run. And the
two probe logs carry DIFFERENT baselines, 5,092 assertions in 79 cases in
round one against 4,950 in 79 in round two, which is the three repairs
between the rounds: the two deleted fields took their assertions with them,
the interpolation test moved to a graded mesh and the compatibility test
gained two marginal cases. 4,950 in 79 is what all three qualified binaries
report, so round two's baseline is the qualified one; the exact per-test
split is not recoverable and is not claimed. The `.pristine` backups are
not committed either, being byte-identical copies of two files this commit
already carries — their evidential content is the hash pair, which is in
both logs.
