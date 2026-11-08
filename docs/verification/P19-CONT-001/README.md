# P19-CONT-001 — Continuity Equation

RESULT: PASS — qualified on all three presets from a clean tree, 0 stages failed
COMMIT: pending

## TASK

`P19-CONT-001` — Continuity Equation. Measure `sum_f phi_f = 0` per cell and
`sum over boundary phi = 0` over the mesh, for `ADR-066`'s face mass flux, with
the inlet/outlet and per-patch attribution a reader needs to diagnose a failure.

AUTHORIZED 2026-11-08 by the owner's "implement and fully verify" over the
twelve-item checklist, pasted into the instruction as `P19-BC-001`'s,
`P19-IC-001`'s, `P19-DIFF-001`'s, `P19-GRAD-001`'s, `P19-CONV-001`'s and
`P19-MOM-001`'s were.

**THE PASTED TEXT WAS DIFFED AGAINST `TODO.md`'S OWN SECTION BEFORE ANY WORK**
and is byte-for-byte identical across all 31 lines after CR normalization,
sha256 `2c7e8a70c635f48b` over both, with **12 unticked items and 0 ticked** —
counted with `grep -c`, not assumed. So the scope is this file's checklist
unaltered.

## SCOPE

The twelve items, and what each means here:

```text
 1  Define mass-flux convention      BIND to ADR-066's phi; define nothing
 2  Cell continuity residual         sum_f phi_f per cell, from the divergence
 3  Global mass imbalance            |sum over boundary phi|, relative
 4  Boundary mass flow               inflow and outflow, by the SIGN of phi
 5  Internal-face cancellation       measured through its aggregate consequence
 6  Closed-domain check              exact, because the table prescribes phi = 0
 7  Inlet/outlet mass-balance check  REPORTED; the check is item 3 (ADR-058)
 8  Relative/absolute tolerance      two measured floating-point bounds
 9  Multiple inlets / outlets        per-patch attribution, PatchIndex order
10  Deterministic aggregation        fixed order; two runs bit-identical
11  Regression PASS                  the gate
12  Evidence recorded                this directory
```

### Three scope notes recorded at authorization time, not discovered later

**(1) "Define mass-flux convention" does not authorize a second mass flux.**
`P19-CONV-001` already defines `phi` and `ADR-072` fixes its sign and scatter.
This milestone binds to that definition and states it. Three earlier milestones
each invented `phi` privately before `P19-CONV-001` made one, which is why this
is written down before the work rather than after.

**(2) The twelve items contain no matrix and no solve.** Every one is a
residual, an aggregate, a balance, a tolerance or a determinism claim, so the
deliverable is an EVALUATOR and not an operator assembling into
`CfdSparseMatrix`. A pressure-Poisson matrix belongs to `P19-PVCOUPLE-001` and
`P19-LINSOLVE-001`, and neither is authorized.

**(3) The global imbalance is reported and never asserted zero.**
`P19-DIV-001` measured a bit-exact global sum at 40.34% and recorded the
consequence as its L3.

### A fourth note: the scope collision with P19-MASSBAL-001

Found during UNDERSTAND and recorded in `TODO.md` rather than resolved
silently. `P19-MASSBAL-001` ("Mass Conservation / Flow Balance", **not
authorized**) carries fifteen items of which seven restate this checklist's in
the same or nearly the same words:

```text
this checklist                  P19-MASSBAL-001
Cell continuity residual   <->  Cell continuity residual        IDENTICAL
Deterministic aggregation  <->  Deterministic aggregation       IDENTICAL
Relative/absolute          <->  Relative tolerance +
  tolerance                     Absolute tolerance              SPLIT IN TWO
Multiple inlets / outlets  <->  Multiple inlets +
                                Multiple outlets                SPLIT IN TWO
Global mass imbalance      <->  Net mass imbalance +
                                Global mass residual            near-identical
Boundary mass flow         <->  Inlet + Outlet mass flow        overlapping
Inlet/outlet mass-balance  <->  Inlet + Outlet mass flow        overlapping
```

**AND `CfdData.hpp` ALREADY DECLARES THE ANSWER.** `CfdMassBalance` fixes the
two measures `ADR-058` chose — `maxCellContinuity` and `globalImbalance`, each
relative to the boundary throughput `0.5 * sum |phi|` — with the comment that
they are fixed there "so that no later milestone can quietly report a different
one", and it assigns **the floor and the acceptance thresholds to
`P19-MASSBAL-001` by name**.

The boundary this milestone works to, decided before any code:

```text
DOES      compute the two declared measures and fill the declared type
DOES NOT  set the throughput floor
DOES NOT  judge a flow against an acceptance threshold
```

Its own "Relative/absolute tolerance" item is therefore the FLOATING-POINT bound
on its own identities, the kind `kCfdConvectionRowSumTolerance` already is, and
not a policy threshold — `ADR-053`'s rule and `CfdData.hpp`'s "CONVERGENCE IS
NOT ACCEPTANCE" both put policy elsewhere.

A consequence stated plainly: **`P19-MASSBAL-001` will later re-tick items this
milestone ticks.** That duplication is the checklists', not this
implementation's.

## BASELINE

```text
HEAD at the start      8e6e71f932b63efee4fe91384d3ce65dc2cff337
cfd subsystem          716,417 assertions in 495 cases
full suite per preset  6601
repeat set per preset  3306
TODO boxes             676 ticked / 354 unticked / 1030 total
```

## ARCHITECTURE

`ADR-074`. Ten decisions; the three that shaped the result:

**Decision 1 — continuity CONSUMES the qualified divergence.** Three candidates
were compared on eleven axes. `divergeCfdMassFlux` (`ADR-071`) already produces
the per-cell net outflow and the global sums, so the evaluator calls it and
walks BOUNDARY faces only, for the attribution that operator cannot do because
it takes no boundary. Rejected: re-walking the interior (a second traversal that
re-derives the owner/neighbour sign is the error `ADR-072` Decision 3 says a
scatter cannot express, and leaves nothing independent to check against), and
extending `CfdDivergence` (its public API is not this checklist's to change, and
`P19-CONV-001` already recorded leaving a duplicated interpolation in place
rather than editing a qualified operator for a convenience).

**Decision 3 — the two `globalImbalance` spellings are separated by TYPE.**

```text
CfdDivergenceField::globalImbalance()  =  globalNetFlux() - netBoundaryFlux()
                                          a FLOATING-POINT artifact, ~1e-16
CfdMassBalance::globalImbalance        =  |sum over boundary phi| / throughput
                                          the PHYSICAL imbalance
```

Same name, different quantity, different magnitude. **Wiring the first into the
second passes every mass balance, always**, because the artifact stays near
`1e-16` however badly conservation fails. The evaluator never reads the first;
the artifact is carried as `accumulationResidual` so no assignment between them
compiles into silence, and a mutation probe applies the swap.

**Decision 4 — direction comes from the SIGN of `phi`, never the condition
kind.** `PressureInlet` and `PressureOutlet` carry `phi` as `Computed` precisely
because the rate is an outcome of the solve, so a facet named an inlet may carry
outflow — `P19-CONV-001` already reports `backflowFaces`. Attributing direction
from the name would be wrong exactly in the regime the diagnostic exists for.

### The measurements, made before any C++ existed

Seed `20261108`, pure-Python model of the operator's own arithmetic in the
summation order it uses. [`experiments/measure.py`](experiments/measure.py),
[`experiments/measure.out`](experiments/measure.out).

```text
Q1  per-cell residual of a field CONSERVATIVE BY CONSTRUCTION
      metre-scale tets        worst 3.2789e-15   bit-zero 19.86%
      millimetre-scale tets   worst 7.4598e-15   bit-zero 18.73%
      worst mesh closure      4.2666e-16 / 7.0816e-16
    THE RESIDUAL IS ABOUT TEN TIMES THE CLOSURE, so the closure does NOT bound
    it: the dot products round too, not only the area sum
Q2  (outflow - inflow) vs the single-pass net
      balanced    worst 5.8386e-16   bit-exact  1.02%
      unbalanced  worst 9.0866e-16   bit-exact 12.68%
Q3  sum of per-PATCH subtotals vs the net
      balanced    worst 3.1237e-16   bit-exact  1.93%
      unbalanced  worst 9.6334e-16   bit-exact 18.64%
Q4  closed domain, every facet a wall       0 / 5000 non-zero
Q5  discrimination of 0.5 * sum |phi|       balanced 5.6167e-16
                                            unbalanced 1.9590
```

**Q2 AND Q3 ARE WHY THERE ARE TWO TOLERANCES.** A reader expects
`outflow - inflow` to BE the net; in floating point it is bit-equal on 1.02% of
balanced fields. Claiming either reconstruction as an identity would have
repeated `ADR-071`'s own first-draft error one phase later.

```text
kCfdContinuityCellTolerance         1.0e-13   one decade above Q1's 7.4598e-15
kCfdContinuityAggregationTolerance  1.0e-14   one decade above Q2/Q3's 9.6334e-16
```

A single shared `1e-13` would be a hundred times looser than the aggregation
measurement supports, so a formula error two orders of magnitude large in the
directional split or the per-patch sum would pass it. A tolerance that cannot
fail is not a gate.

Q5 also bounds the measure: all flow one way gives `|net| = sum|phi| = 2 *
throughput`, so **the relative global imbalance lies in `[0, 2]`**, which is
asserted.

## BLAST RADIUS

```text
direct callers           none.  New public API; nothing consumes it (L1)
indirect dependents      none
existing source changed  NONE.  Three CMake lists gain one line each
serialization            unaffected.  CfdMassBalance is DECLARED in CfdData.hpp
                         and is not changed here; the report is derived and
                         never persisted
regeneration / undo      unaffected.  No document state
stable references        unaffected
CLI                      unaffected
layering                 cfd is layer 42; this consumes cfd and core only
tests                    one new unit suite, one new compile-fail group; the
                         cfd subsystem totals shift by exactly the new suite
reference models         the eight meshable bodies gain a continuity check
```

The shape — one new header, one new source, one new suite, one new compile-fail
group, no existing source file modified — is the shape `P19-MOM-001` proved one
milestone earlier.

## IMPLEMENTATION

```text
include/bettercad/cfd/CfdContinuity.hpp       new, public API
src/cfd/CfdContinuity.cpp                     new, 8-phase gate + evaluation
tests/cfd/CfdContinuityTests.cpp              new, 40 cases
tests/compile_fail/CfdContinuityMisuse.cpp    new, 37 cases
src/cfd/CMakeLists.txt                        +1 line
tests/CMakeLists.txt                          +1 line
tests/compile_fail/CMakeLists.txt             +1 group, 4 regex groups
docs/architecture/decisions/ADR-074-*.md      new
```

The gate's eight phases, in declared order, which is the check order BETWEEN
phases (within a phase the first offending entry decides, and no order is
claimed there):

```text
1  GeometryDescribesAnotherTopology
2  BoundaryDescribesAnotherTopology   conditional-source arm, P19-DIFF-001's
                                      correction inherited
3  TopologyNamesAnUnknownCell         UNREACHED; the divergence indexes by
                                      owner/neighbour and its gate has no
                                      enumerator for this
4  TopologyNamesAnUnknownPatch        UNREACHED; this evaluator subscripts a
                                      per-patch array by patchOf(...).value()
5  ValueLengthWrong
6  ValueNotFinite
7  ResultIsNotFinite                  reached through the DIVERGENCE's gate
8  EvaluationIsInconsistent           UNREACHED; ErrorCode::Internal
```

### Two things the compiler and a test corrected, recorded because the result is better

**The report's constructor is the compiler's design, not mine.**
`CfdDivergenceField`'s default constructor is private — "a field that exists has
been through the gate" — so holding one as a default-constructed member does not
compile. `CfdContinuityReport` therefore has one private constructor taking the
field by value, and the sub-operator's invariant propagates into this type BY
CONSTRUCTION rather than by comment.

**`ADR-074` Decision 10's justification for the finiteness loop was FALSE, and
writing the test for it is what showed so.** The claim was that a finite
`1.7e308` input could overflow a boundary sum this evaluator computes. It cannot:
`divergeCfdMassFlux` gates its own `netBoundaryFlux`, `globalNetFlux` **and** its
accumulated magnitude `sum_f |flux_f|`, and every quantity computed here is
bounded by that magnitude. The test was built for the route `P19-MOM-001`'s
surviving probe M19 had exposed — per-cell sums finite while the same-sign
accumulation overflows — and it failed on its own premise with

```text
the mass flow rate divergence totals are not finite: boundary inf,
global inf, accumulated magnitude inf
```

The loop is kept as an unreachable defence and recorded as `L8`; the header and
the ADR carry the correction, and the inequalities that make it unreachable are
now asserted by a test rather than argued.

## TESTS

```text
unit cases                49   40 before the reviews, 9 added against MEASURED
                               mutation survivors and one against an untested
                               promise (an empty patch)
assertions             7,435
compile-fail cases        37   37 registered, 37 in the file, diffed BOTH WAYS
```

**THE ASSERTION COUNT IS DOMINATED BY LOOPS, AND THAT IS WORTH KNOWING WHEN
READING IT.** A review counted roughly 360 `CHECK`/`REQUIRE` statements
producing the total, so the per-cell and per-face loops carry most of it. The
figure is a count of assertions executed, not of distinct claims made, and the
claims that matter are the three listed below plus the items table.

### What the hygiene pass removed, after the reviews

```text
CHECK(true);                    a runtime assertion whose only purpose was to
                                make Catch2 count the case as having asserted
                                something.  The static_asserts beside it ARE
                                the test; replaced by SUCCEED with a reason
four literal-vs-literal CHECKs  `kCfdContinuityCellTolerance > 7.4598e-15` and
                                siblings compared two compile-time constants
                                the static_asserts already pin.  Moved into the
                                static_asserts' own failure messages, where the
                                relationship to the measurement belongs
Hand::movingWalls()             defined, never called; it read as coverage of
                                the MovingWall arm, which
                                `movingWallOnOneFacet` actually provides
a bare 1.0e-14                  where the named constant was in scope
```

### Three claims corrected rather than deleted

```text
the splitPatches comment    claimed round-robin made the patches INTERLEAVED in
                            BoundaryFaceIndex order, and that contiguous blocks
                            would "agree bit-for-bit by accident".  Both false:
                            `buildCfdTopology` sorts the boundary range by
                            patch, so patches are always contiguous -- and
                            contiguous blocks would not agree bit-for-bit
                            either, since ((a+b)+(c+d)) is not (((a+b)+c)+d)
"TWO INLETS AND TWO         described a fixture with one of each
 OUTLETS"
"the inlet patch carries    not asserted and not true: phi is assigned by face
 inflow, the outlet         parity while patch membership is contiguous.  The
 patch outflow"             per-patch signs are pinned by
                            `EachPatchRecordDescribesItsOwnFaces`, which
                            re-derives each patch's contents independently
```

### `AConservativeFieldClearsTheCellTolerance` was named for a bound it could not read

Both reviews found this one. The fixture was `walls()`, where ADR-067's table
makes every boundary `phi` bit-zero — so each cell's residual is the **full
internal flux**, O(1) rather than O(1e-15), the field *least* able to clear the
bound in its name. And the throughput is zero there, so `massBalance()` is
deliberately absent and the bound could not be read even if the field had been
conservative. Its one assertion duplicated another test.

It now uses `outlets()`, where `phi` is `Computed` at the boundary so the
producer uses the interior velocity, the cell sums telescope to the mesh
closure, and the throughput is positive. The bound is asserted at the maximum
**and per cell**, each scaled by the magnitude that cell accumulated — which is
the quantity Q1 actually measured.

### A limit of the three re-derivation assertions, recorded

The `residual == expected` checks on `directionalSplitResidual`,
`patchSumResidual` and `accumulationResidual` write the combining formula and
the denominator independently, so a change to either is caught. But they compare
the summary against the report's **own** other accessors, so they cannot detect
a wrong `inflow`, `outflow`, patch `net` or `netBoundaryFlux`. What carries
those is the bound plus the provenance assertions added after the reviews —
which is exactly where the two surviving mutations had got through.

The suite's three implementation-independent claims:

```text
(a) THE DECOMPOSITION   every per-cell residual and the headline
                        netBoundaryFlux are the QUALIFIED divergence's own
                        values, bit-for-bit, checked against a divergence the
                        test takes itself
(b) THE TWO MEASURES    massBalance().globalImbalance is the PHYSICAL boundary
                        sum and NOT divergence().globalImbalance(); the two are
                        separated by ten orders of magnitude on a field whose
                        balance genuinely fails
(c) THE SIGN CONVENTION a positive boundary phi is outflow and a negative one
                        inflow, whatever the facet is CALLED, and a
                        PressureInlet carrying outflow is reported not refused
```

### Independent validation

Three oracles, none of which derives its expected values from the
implementation:

```text
A ONE-CELL HAND ORACLE, EXACT. phi = {8, -2, 4, -2}, chosen to be exactly
representable so the oracle carries no rounding and the comparison is `==`:
  outflow 12, inflow 4, net 8, sum|phi| 16, throughput 8
  globalImbalance   |8| / 8 = 1.0   EXACTLY
  maxCellContinuity |8| / 8 = 1.0   EXACTLY
The values are order-independent -- every partial sum is a small integer -- so
the oracle smuggles in no assumption about ADR-064's face numbering.
A RATIO THAT IS A POWER OF TWO IS DELIBERATE: an earlier draft used
{8, -2, 4, -1}, whose imbalance is 9/7.5 = 1.2, and 1.2 is NOT representable,
so the expectation would have had to be written as `9.0 / 7.5` and the test
would have compared the code's division against the same division.

A TWO-CELL HAND ORACLE WITH AN INTERNAL FACE. The internal face carries 16
and each cell's three boundary faces sum to exactly -16 or +16, so both
residuals are exactly 0, the boundary net is exactly 0, and the boundary never
sees the 16 that crossed the mesh -- the closed-form statement of
internal-face cancellation.  Which boundary face belongs to which cell is
ASKED of the topology, not assumed.

THE DIVERGENCE THEOREM, analytically. For a uniform U, `sum_f phi_f =
rho U . (sum_f S_f) = 0` because a closed cell's outward area vectors sum to
zero. The orthogonal tetrahedron's four vectors are computed BY HAND here --
(-a^2/2,0,0), (0,-a^2/2,0), (0,0,-a^2/2), (a^2/2)(1,1,1) -- asserted to sum to
exactly zero, and the producer's phi is checked against `rho (U . S_f)` as a
SET, because the face order is ADR-064's.
```

### Three fixture premises failed before working, and all three are recorded

```text
a moving wall with zero velocity    REFUSED by the binder: "a wall at rest is
                                    a no-slip wall"
a moving wall on every facet        REFUSED: "its target is not planar ...
                                    tangency has no meaning on a curved
                                    target".  A tetrahedron's facets are not
                                    coplanar; one facet is trivially planar
the finiteness route above          the divergence refuses first
```

Each time the test refused to pass vacuously, which is what the guard was for.

## ADVERSARIAL REVIEW

Two independent reviews, both BEFORE any freeze, as `P19-CONV-001` established
and `P19-DIV-001`'s discarded attempt argues for. Each was given the code and
the checklist and explicitly NOT the author's conclusions; one was pointed at
correctness and architecture, the other at test adequacy.

```text
review A, correctness     14 findings
review B, test adequacy   17 findings, and 24 MUTATION PROBES APPLIED TO THE
                          REAL TREE of which 12 SURVIVED
agreement                 the two reached five of the same findings
                          independently, which is the strongest signal in the
                          pair: the cell tolerance's denominator, the vacuous
                          `AConservativeFieldClearsTheCellTolerance`, the
                          `PressureInlet` test whose loop body never runs, the
                          false per-patch comment, and the dead `patchMixed`
```

### One production defect, confirmed by a third independent model

`accumulationResidual` divided an ALL-CELLS numerator by a BOUNDARY-ONLY
denominator. Review A found it, and rather than take the figures I re-derived
them in a separately written script. The two agree to every digit:

```text
interior / boundary      as implemented      ADR-071's denominator
1e6   / 1e-6             7.614493e-06        4.568696e-17
1e12  / 1e-12            1.000000e+00        6.000000e-24
1e200 / 1e-200           1.000000e+00        0.000000e+00
```

Fixed by consuming `divergence().summary().relativeImbalance` — the qualified
number over the correct scale. **The proof that the old assertion was blind is
that the same eight reference bodies now report `1e-17` to `3e-17` where they
previously reported exactly `0.0`.** Review A had claimed the real artifact was
non-zero in a closed domain; my own two-cell model showed zero, so the point
looked unproven — the reference bodies settle it in the reviewer's favour, and
my model was simply too small to show it.

### Three more gaps closed in production code

```text
phase 3    an internal face with a NULLOPT neighbour passed this gate and was
           refused by `divergeCfdMassFlux`'s BODY, which its own predictor
           cannot see.  That made this file's claim that "the only refusal its
           predictor cannot foresee is DivergenceIsNotFinite" FALSE, and would
           have reported a topology fault as `ResultIsNotFinite` with
           `InvalidArgument`.  Covered, which makes the claim true rather than
           weakening the tag
phase 3b   the divergence's other body refusal, a cell with no usable volume,
           reported under phase 1's enumerator because the divergence's own
           comment calls it "phase 1's currency failure surfacing late"
patchMixed write-only.  `record.kind.reset()` already did the work and the loop
           that read it called `reset()` a second time.  Removed, not commented
```

### Eight new tests, each aimed at a MEASURED survivor

Every one of these exists because a probe was applied, built and run green:

```text
closedDomain from `wallFaces > 0`      no fixture had EVER mixed wall and open
                                       boundary faces, so the ordinary CFD
                                       arrangement was untested
the patch table rotated by one         the record's LABEL is written in one
                                       loop and its CONTENTS in another, and
                                       only the label was asserted
the worst cell hardcoded to 0, and     the location was only ever asserted
  left empty on a conservative mesh    `== 0`, on one- and two-cell fixtures
the non-finite scan at face 0 only     every poison fixture wrote index 0
`maxCellContinuity` as the GLOBAL      every site was a one-cell fixture, where
  measure                              the cell residual IS the boundary net,
                                       or a `>= 0.0` that cannot fail
`globalImbalance` from this file's      `directionalSplitResidual` being small
  own `outflow - inflow`               is exactly what hid it
`worstClosure` + 1.0                   the member appeared once, in an INFO
the `[0,2]` range                      all four samples were uniform, so the
                                       measure was exactly 2.0 every time
```

### Findings recorded rather than fixed

```text
the phase-2 source arm is unreached           L11; reaching it needs a
                                              `prepareCfdBoundary` boundary no
                                              cfd suite constructs
`ErrorCode::Internal` is never observed       L12
Q3 modelled the wrong associativity           L13; patches are contiguous
`maxCellContinuity`'s denominator is not      L14; ADR-058 fixes the
  the one Q1 measured                         denominator, so what changes is
                                              the claim and the coverage
no EMPTY patch is ever produced                the header promises an all-zero
                                              record for one; whether
                                              `buildCfdTopology` admits an
                                              empty `MappedFace` is unverified,
                                              so the promise is either tested
                                              or withdrawn before `[x]`
```

### Four process failures, three of them mine

**(1) I RAN THE TWO REVIEWERS IN PARALLEL WITH BUILD ACCESS TO THE SAME FILE.**
`CLAUDE.md` says in as many words: *"Do not set several agents editing the same
subsystem at once; reviewers challenge the implementation, they do not race
it."* Both mutated `src/cfd/CfdContinuity.cpp` concurrently and raced each
other's restores.

**(2) A MUTATION WAS LEFT IN THE LIVE SOURCE** — `if (false)` in place of phase
3's owner range guard, with a `.adv-review-backup` file parked inside the frozen
surface. The suite passes either way, because that guard is unreachable; it was
caught by a file-state notice and a diff, not by a test. Had the freeze been
taken then, a disabled guard would have been qualified and committed. Detail in
r8.

**(3) MY OWN FIRST PROBE HARNESS WAS BROKEN, AND THE NO-OP CONTROL CAUGHT IT.**
All 28 probes reported "rejected by the compiler" — including a reorder of two
independent assignments, which cannot fail to compile. `python3` launched from
Git Bash inherits a `PATH` containing `C:/msys64`, so CMake's regenerate
resolved Open CASCADE to MSYS2's 7.9.3 instead of the pinned 8.0.x and died
before any compiler ran. **Every "KILL" in that run was worthless.** The control
exists for exactly this and earned its place on first use.

**(4) THAT FAILED REGENERATE POISONED THE `debug-ext` CMake CACHE**, writing
`OpenCASCADE_DIR=C:/msys64/ucrt64/lib/cmake/opencascade` into it, after which
even a clean `cmd` configure failed. Exactly one entry was wrong and the other
two build trees were untouched — but the tree was **deleted and reconfigured
from scratch** rather than patched, because `ninja -t clean` does not remove
`CMakeCache.txt`, so a poisoned cache would have survived into the qualified
build, and because "only one entry is wrong" is an inference from a `grep` while
a wipe is a fact.

## REGRESSION

The full suite, from a clean tree, in all three presets. Not the cfd subsystem,
not the affected targets — every registered test, because the blast radius above
reaches persistence, regeneration and the reference models.

```text
preset             tests   passed   failed
debug-ext           6687     6687        0
release-ext         6687     6687        0
debug-shared-ext    6687     6687        0

6687 = 6601 (P19-MOM-001) + 49 unit + 37 compile-fail      EXACTLY, no fudge term
```

`ctest`'s `Start` markers equal its result lines in each full-suite log (6687 and
6687), so no test was started and left unreported.

### The repeat set: determinism, not a second pass

```text
preset            selected   executions   result lines   markers/lines
release-ext           3355        16775           3355           5.000
debug-ext             3355        16775           3355           5.000

3355 = 3306 (P19-MOM-001) + 49 matching unit\.Cfd            EXACTLY
16775 = 3355 x 5
```

**Five executions of every one of 3355 tests, in two presets, 0 failures.** The
ratio is checked because `ctest --repeat until-fail:5` prints one result line and
five `Start` markers per test: a log holding 3355 of each would mean the repeat
flag had silently not applied, which is how `P19-CONV-001`'s first
reconciliation failed.

### What the suite cannot check, measured separately

A pass/fail count cannot see an assertion that stopped running — a deleted test
and an added test net to nothing in it. So the cfd subsystem's own arithmetic was
read **out of the qualified binaries**, after the gate, in all three presets:

```text
                     assertions    cases
before (8e6e71f)        716,417      495
qualified binaries      723,852      544
delta                     7,435       49
the continuity suite      7,435       49        EXACTLY

and bit-identical in debug-ext, release-ext AND debug-shared-ext
```

**No pre-existing cfd assertion moved.** And the continuity suite returning the
same 7,435 in a `-O3` build, a `-O0` build and a shared build is the check that
none of its arithmetic depends on optimisation level or linkage.

## QUALIFICATION

Run from the frozen tree recorded in [FREEZE.md](FREEZE.md) — three presets, each
configured, cleaned, built from scratch, no-op rebuilt and run in full, then the
repeat set in two presets.

```text
qualified candidate   8e6e71f932b63efee4fe91384d3ce65dc2cff337
started               Sun 2026-11-08 18:56:18.24
finished              Sun 2026-11-08 22:08:59.96     elapsed 3 h 12 m 41.72 s
stages                17 = 3 presets x (configure, clean, build, no-op
                      rebuild, ctest) + 2 repeat runs
verdict               "Qualification passed: every stage exited 0."
                      "0 stage(s) failed"
```

```text
preset             configure    clean      build      no-op    ctest
debug-ext              3.01 s   815 files  16 m 35.79 s  0.36 s  38 m 17.61 s
release-ext            2.82 s   815 files  17 m 51.12 s  0.36 s  33 m 51.73 s
debug-shared-ext       3.11 s   828 files  16 m 22.90 s  0.40 s  36 m 11.01 s

repeat release-ext     15 m 42.89 s        repeat debug-ext     17 m 33.23 s
```

**THE VERDICT IS READ FROM THE HARNESS, NOT FROM THE LAUNCHER.** Both sentences
above are the harness's own. `P19-MOM-001`'s first attempt was reported by the
launcher as "completed (exit code 0)" while the harness wrote "QUALIFICATION
FAILED: 2 stage(s) failed", so an exit code is not evidence here.

### The qualified tree is the committed tree

`qualify.cmd` records the eight subtree identities before the first build and
again after the last test run. **All eight are identical in both readings, and
identical to [FREEZE.md](FREEZE.md)'s record:**

```text
apps               fc398a3d2e524178cf38e8b667236379d8381086
include            bb7f9c6c442b2fa7bed549fd738cc182d585a862
src                9d7539d75f47bc003322dfd4917ae30fb88ce659
tests              55ba3a0a5a3ce1b2b974fef411b6d25ebd597cf8
examples           4da234ebf4779f7c16d5df0075f106f7e3d4c958
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e
```

Nothing under the frozen surface moved during the 3 h 12 m, so the qualification
is not void. A fourth reading is taken out of the commit object below.

### Zero diagnostics, and what the naive grep says instead

```text
preset             build steps   "error:"   "warning:"
debug-ext                  816          0           0
release-ext                816          0           0
debug-shared-ext           816          0           0
```

A case-insensitive grep for `error` returns exactly one hit per log, and it is
`Building CXX object .../Error.cpp.obj` — a FILENAME. It is recorded because the
difference between that and a diagnostic is the difference between a pass and a
false pass, and the count was read rather than assumed.

### The no-op proof: every binary newer than every source

A second `cmake --build` on each preset does only the always-run target:

```text
debug-ext          [1/8]  Checking git revision
release-ext        [1/8]  Checking git revision
debug-shared-ext   [1/20] Checking git revision
```

Checked again by timestamp, with a GLOBAL sort across the frozen surface:

```text
newest source      2026-11-08 18:03:28   tests/cfd/CfdContinuityTests.cpp

preset             objects   older than that source   oldest object   its clean
debug-ext              683                        0   18:56:25.24     18:56:23.21
release-ext            683                        0   19:51:21.79     19:51:20.18
debug-shared-ext       683                        0   20:43:09.26     20:43:07.44

our linked outputs      26 / 26 / 39   older than that source: 0 / 0 / 0
```

**Every compiled object postdates its own preset's clean**, which is the stronger
statement: not merely newer than the source, but produced by this run.

A note on the method, because the naive form of this query FAILS on a correct
tree. "The oldest file in the build tree" names a vendored third-party header
(nlohmann/json, 2025-04-08) or a runtime DLL copied out of the toolchain
(`libgcc_s_seh-1.dll`, 2026-07-30) — 2571 such files in `debug-ext` alone. Those
are build INPUTS, and CMake's copy carries the origin's modification time. Each
was checked against its origin rather than explained away:

```text
libgcc_s_seh-1.dll, libstdc++-6.dll, libwinpthread-1.dll
    byte-identical to the pinned WinLibs toolchain's own copies, same mtime
libTKPrim.dll, libngcore.dll
    byte-identical to the deps prefix's copies
```

so none of them is a build product of the frozen surface. The freshness check is
scoped to what the gate compiled and linked, and the scoping is recorded as part
of the result.

### The caches, verified again after the run

The check this milestone adds, for the reason [FREEZE.md](FREEZE.md) gives — the
harness pins the toolchain by `PATH` order while `find_package` derives candidate
prefixes from `PATH` membership, so dependency resolution was never pinned:

```text
preset             OpenCASCADE_DIR                       msys64 refs   compiler
debug-ext          <deps>/lib/cmake/opencascade                    0   WinLibs
release-ext        <deps>/lib/cmake/opencascade                    0   WinLibs
debug-shared-ext   <deps>/lib/cmake/opencascade                    0   WinLibs

<deps>    C:/Users/Hasib/AppData/Local/bettercad-deps/gnu-16-mingw-amd64
WinLibs   .../BrechtSanders.WinLibs.POSIX.UCRT_.../mingw64/bin/c++.exe
```

**The harness itself was NOT changed to achieve this.** It stays byte-identical
to the lineage every previous milestone used — five of its six files verified by
hash — and a check was added rather than a variable, because `P19-MOM-001` passed
this only by luck and altering a gate to cover that would hide it.

## RESULT

```text
TASK:            P19-CONT-001 — Continuity Equation
IMPLEMENTATION:  CfdContinuity.{hpp,cpp} — the two measures ADR-058 already
                 fixed, computed by consuming the qualified divergence operator
                 and walking boundary faces for inlet/outlet and per-patch
                 attribution. No new summary type, no second parameter system.
TESTS:           6687 per preset x 3 presets, 100% passed, 0 failed;
                 3355 x 5 executions x 2 presets, 0 failed;
                 49 unit cases / 7435 assertions and 37 compile-fail cases,
                 identical in all three presets
VALIDATION:      analytic and independent — a closed domain is exactly
                 conservative (0 of 5000 cells non-zero), a balanced
                 inlet/outlet pair reports 5.6167e-16 against an unbalanced
                 pair's 1.9590, and both tolerances sit one decade above a
                 measurement made before any C++ existed (seed 20261108)
ADVERSARIAL:     two independent reviews BEFORE the freeze, 31 findings;
                 one production defect found and fixed (accumulationResidual's
                 all-cells numerator over a boundary-only denominator, which
                 read exactly 1.0 for CORRECT arithmetic at 1e12/1e-12 and
                 made an assertion on 8 reference bodies vacuous);
                 22 of 22 behavioural mutations caught, 3 by the COMPILER,
                 5 of 5 documented-unreachable guards survived,
                 and the NO-OP CONTROL SURVIVED
RESULT:          PASS
EVIDENCE:        this directory; qualification/ holds the 23 raw logs,
                 qualification-times.txt the stage table,
                 FREEZE.md the frozen tree, experiments/ the measurements
TODO:            12 boxes ticked, on this PASS
```

### What is NOT claimed

```text
the acceptance thresholds   ADR-058 assigns the throughput floor and the
                            PASS/FAIL thresholds to P19-MASSBAL-001 BY NAME.
                            This milestone reports both measures and checks
                            them against MEASURED tolerances; it does not
                            decide what value fails a mesh
five guards are unreachable L9, L11 and the cell-volume and internal-face arms
                            survived mutation because nothing can reach them
                            through the public API. They are kept, and recorded
                            as unreachable rather than as tested
L12                         ErrorCode::Internal has never been observed
L13                         Q3's associativity model measures a round-robin
                            patch order; the real CSR is contiguous
L14                         the cell tolerance's denominator is documented
```
## REVISION

**r1** — the pasted checklist was diffed against `TODO.md`'s own section before
any work, byte-for-byte across 31 lines, 12 unticked and 0 ticked counted rather
than assumed, and the authorization plus three scope notes were written into
`TODO.md` BEFORE any implementation.

**r2** — the UNDERSTAND phase found that `CfdData.hpp` already DECLARES this
milestone's answer as `CfdMassBalance`, and that `ADR-058` already fixed the two
measures. The architecture changed from "define and compute a continuity report"
to "compute two measures already fixed, and define none". Without that phase
this milestone would have created the second parameter system
`ARCHITECTURE.md` rejects — the very thing scope note (1) had warned about for
`phi` an hour earlier.

**r3** — the same phase found `ADR-058`'s section heading "Mass balance means a
discrete measure, **not a sum of inlets and outlets**", which constrains
checklist item 7 against its own wording. The invariant wins: inlet and outlet
flow are reported, and the check is the global boundary imbalance.

**r4** — the `globalImbalance` name collision was found before any code existed.
`CfdDivergenceField::globalImbalance()` and `CfdMassBalance::globalImbalance`
have the same name and different meanings, and the swap would make every mass
balance pass. Defended by a different name, a ten-orders-of-magnitude test and a
mutation probe; the type system catches the naive assignment but not the `.si()`
form, and the compile-fail group's header says so rather than implying a whole
guard.

**r5** — the report's constructor was changed from default-plus-assign to a
private constructor taking the divergence, because the sub-operator's private
default constructor would not compile. The invariant is now inherited by
construction.

**r6** — `ADR-074` Decision 10's justification for the finiteness loop was FALSE
and was corrected the same day, after the test written to reach that loop failed
on its own premise. The loop is recorded as unreachable (`L8`) and the
inequalities that make it so are asserted by a test.

**r7** — a verification script of mine was wrong before the thing it checked
was: a case-list diff between the compile-fail file and its CMake registration
reported four unregistered cases, because the regex excluded the last entry of
each `foreach` list, which ends in `)`. 37 = 37 both ways once corrected. The
registration was never wrong; the check was.

**r8** — **AN ADVERSARIAL REVIEWER LEFT A MUTATION IN THE LIVE SOURCE, AND IT
WAS CAUGHT BY A FILE-STATE CHECK RATHER THAN BY THE SUITE.**

The correctness reviewer was given build access so it could verify its own
claims instead of guessing. It applied a probe to `src/cfd/CfdContinuity.cpp`,
saved the original as `src/cfd/CfdContinuity.cpp.adv-review-backup`, and **did
not revert it**. The working tree was left holding

```text
line 174   if (false) {
           where the original reads
           if (!owner.isValid() || owner.value() >= cells) {
```

— phase 3's owner range guard disabled. The guard `P19-DIV-001`'s review forced
into a sibling operator, that `P19-CONV-001`'s review found had regressed, and
that this milestone's header cites both of those for.

```text
found by      the harness's "file changed on disk" notice, then a diff of the
              working file against the backup the reviewer itself had left
NOT found by  the test suite, which passes either way -- the guard is
              unreachable from any constructible topology, which is exactly
              why the mutation was invisible
restored      from the backup, verified byte-identical afterwards, and the
              stray backup file DELETED: it sat inside the frozen surface,
              where `git add -A src` would have committed it
```

**THE REAL HAZARD WAS THE FREEZE.** Nothing in the suite would have objected,
so the mutated tree would have been frozen, qualified for about four hours, and
committed with a disabled guard and a `[x]` beside it. `P19-MOM-001`'s mutation
discipline — "the file's sha256 was compared against the original after each
revert, so a probe that failed to revert could not poison the qualification" —
was written for probes I applied myself; it did not anticipate a reviewer
applying one. The lesson generalises: **the tree's identity is checked before a
freeze because anything may have touched it, not because the author might have
forgotten.**

Two process corrections follow, and both are recorded rather than merely
intended: a reviewer given build access is also given no instruction that
implies editing, and the frozen surface is diffed for unexpected files and
content immediately before the freeze hashes are taken — which is what
`FREEZE.md`'s working-tree listing is for, and which would also have caught the
stray `.adv-review-backup`.

**r9** — the review's central finding is CONFIRMED by an independently written
model, and it is a real behavioural defect of mine:
`CfdContinuitySummary::accumulationResidual` divided an ALL-CELLS numerator by a
BOUNDARY-ONLY denominator. Measured in the operator's own summation order:

```text
interior / boundary flux     as implemented      ADR-071's own denominator
1e6   / 1e-6                 7.614493e-06        4.568696e-17
1e12  / 1e-12                1.000000e+00        6.000000e-24
1e200 / 1e-200               1.000000e+00        0.000000e+00
```

The reported number reaches **exactly 1.0 for correct arithmetic** — the value
the header itself declares to be the signature of a broken scatter — and the
suite's `< 1e-14` assertions pass only because every fixture opens the entire
boundary, maximising the denominator. The 1e6 figure agrees with the reviewer's
to every digit from a separately written script.

`CfdDivergenceSummary::relativeImbalance` is already this exact quantity over
the correct scale, already qualified, and already reachable through
`report.divergence().summary()`. Re-deriving it with a different denominator was
the ONE place in this file where a qualified answer was recomputed instead of
consumed — against `ADR-074` Decision 1's own argument. The fix removes code.

**r10** — THE SECOND REVIEW APPLIED 24 PROBES TO THE REAL TREE AND 12 SURVIVED.
Eight new tests were written against them, each naming the mutation it kills in
its own comment so a reader can see what it is for. The survivors fell into one
shape: a quantity asserted only on a fixture that could not discriminate it. The
sharpest were `maxCellContinuity` replaceable by the GLOBAL measure (every site
was a one-cell fixture, where the two coincide, or a `>= 0.0` that cannot fail),
`closedDomain` derivable from `wallFaces > 0` (no fixture had ever mixed wall
and open boundary faces, so the ordinary CFD arrangement was untested), and the
whole per-patch table rotatable by one (the record's label is written in one
loop and its contents in another, and only the label was asserted).

**r11** — TWO MORE FALSE CLAIMS OF MINE, both found by a reviewer reading the
code rather than the prose. The `.cpp` still carried the finiteness
justification that the header and the ADR had already recorded as false — the
same "a fix at one site is not a fix of a class" failure that cost
`P19-MOM-001` its first qualification, one milestone later and in documentation
rather than in code. And "the only refusal its predictor cannot foresee is
`DivergenceIsNotFinite`" was false: `divergeCfdMassFlux` has two refusal sites
in its BODY, outside its gate. Both gaps are now covered by phases 3 and 3b, so
the claim is true rather than softened.

**r12** — ASSERTING A PREMISE SHOWED THE PREMISE WAS WRONG. A review noted the
reference-body loop guarded only `cells > 0` while its comment claimed
"thousands of cells, hundreds of boundary faces". The first guard written was
`cells > 100`, and `MeshBlock` failed it with **six**. Measured, the eight
bodies run from 6 to 808 cells and 12 to 288 boundary faces. The comment is
corrected, the counts are tabulated in the test, the guards are the measured
minima, and one suite-level check holds the "some body is large" claim.

**r13** — MY PROVENANCE ASSERTION WAS THE WRONG FORM AND FAILED ON FIVE BODIES
WHILE PRINTING IDENTICAL VALUES. It recovered the numerator by multiplying the
measure back by the throughput, and `(a/b)*b` is not `a`. Recomputing the same
division on the same operands is bit-exact and still names which numerator was
used, which is the whole point of the assertion.

**r14** — **MY FIRST MUTATION HARNESS WAS BROKEN AND THE NO-OP CONTROL CAUGHT
IT.** All 28 probes came back "rejected by the compiler", including a reorder of
two independent assignments — which cannot fail to compile. `python3` launched
from Git Bash inherits a `PATH` containing `C:/msys64`, so CMake's regenerate
resolved Open CASCADE to MSYS2's 7.9.3 instead of the pinned 8.0.x and the
configure died before any compiler ran. Every verdict in that run was worthless.
The harness now builds through the same `.cmd` wrapper the session already uses,
which removes the class of difference instead of patching one variable.

**THE CONTROL IS THE ONLY REASON THIS WAS CAUGHT**, and it is the fifth
measurement-tool defect of this milestone. A probe table reporting "20 of 22
killed" would have looked like strong evidence.

**r15** — THAT FAILED REGENERATE POISONED THE `debug-ext` CMAKE CACHE, writing
`OpenCASCADE_DIR=C:/msys64/ucrt64/lib/cmake/opencascade` into it, after which
even a clean `cmd` configure failed — `find_package` honours a cached
`OpenCASCADE_DIR`. Exactly one entry was wrong and the other two build trees had
zero MSYS2 references, but the tree was DELETED and reconfigured from scratch
rather than patched:

```text
`ninja -t clean` does NOT remove CMakeCache.txt, so the qualification would
  have built against the poisoned cache
"only one entry is wrong" is an inference from a grep for one string; a stale
  try-compile result or compiler check need not contain it.  A wipe is a fact
```

**r16** — the gate PASSED on its first attempt, 17 of 17 stages exit 0 across
3 h 12 m 41.72 s, and the eight subtree identities after the last test run are
identical to the eight before the first build and to `FREEZE.md`'s record. The
verdict was taken from the harness's own two sentences and the failed-stage
count, never from the launcher's exit code — `P19-MOM-001`'s first attempt was
reported as "completed (exit code 0)" while the harness wrote "QUALIFICATION
FAILED: 2 stage(s) failed".

**r17** — THE FRESHNESS CHECK'S FIRST FORM REPORTED `STALE FAIL` ON ALL THREE
PRESETS OF A CORRECT TREE, and it was the sixth measurement-tool defect of this
milestone. "The oldest compiled output in the build tree" matched
`libgcc_s_seh-1.dll` at 2026-07-30, eight million seconds older than the newest
source. The cause was the query, not the tree:

```text
the build tree holds build INPUTS as well as outputs -- 2571 files in
  debug-ext predate the newest source, most of them vendored nlohmann/json
  headers dated 2025-04-08
CMake's copy carries the ORIGIN's modification time, so a runtime DLL copied
  out of the pinned toolchain keeps the toolchain's date
```

Both were then PROVEN rather than asserted: the three GCC runtime DLLs are
byte-identical to the WinLibs toolchain's own copies with the same mtime, and
`libTKPrim.dll` and `libngcore.dll` are byte-identical to the deps prefix's, so
none is a product of the frozen surface. Re-scoped to what the gate compiled and
linked, the answer is unambiguous: 683 objects per preset and 26 / 26 / 39 of our
own linked outputs, **none** older than the newest source, and every object
postdating its own preset's clean. The first form is recorded because it would
have read as a qualification failure, and a gate must not be relaxed to clear
one — it has to be understood first.
