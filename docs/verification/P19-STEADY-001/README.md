# P19-STEADY-001 — Steady Incompressible Solver

RESULT: PASS — qualified on the second freeze, after the first gate failed on a weakened compile-fail gate and the repair voided it
COMMIT: pending

## TASK

`P19-STEADY-001` — Steady Incompressible Solver. Authorized 2026-11-09 by the
owner's "implement and verify" over the nineteen-item checklist, which arrived
pasted into the instruction as the nine before it did.

**THE PASTED TEXT WAS DIFFED AGAINST `TODO.md`'S OWN SECTION BEFORE ANY WORK.**
Byte-for-byte identical across all 28 lines, 19 unticked items and 0 ticked —
counted rather than assumed — sha256 `209e080f2110e3d0` over both after CR
normalization. The diff is
[experiments/diff-checklist.py](experiments/diff-checklist.py), and it exits
non-zero on any difference rather than printing one.

## SCOPE

**Ten of the nineteen items name work that was already qualified.** That is the
single most important thing the UNDERSTAND phase established, and it was
established from committed records rather than inferred:

```text
Momentum predictor      advanceCfdCoupling step 1        P19-PVCOUPLE-001
Pressure correction     advanceCfdCoupling steps 3-4
Velocity correction     advanceCfdCoupling step 5
Flux correction         advanceCfdCoupling step 5
Residual computation    advanceCfdCoupling step 7, ADR-058's CfdResiduals
Mass-balance            advanceCfdCoupling step 6, evaluateCfdContinuity
                                                         P19-CONT-001
Convergence loop        ours, consulting cfdCouplingVerdict
Maximum-iteration       ours, consulting cfdCouplingBudgetExhausted
Divergence detection    cfdCouplingVerdict reports Diverged
Validate inputs         requireCfdModel .. prepareCfdInitialField
```

`CfdCoupling.hpp` named three of them as this milestone's **by name**, in the
tree, before any code was written here:

> NO LOOP. One iteration, and the criteria a loop would consult are
> `cfdCouplingVerdict`. "Convergence loop" is `P19-STEADY-001`'s item.

> A PURE FUNCTION OF ITS ARGUMENTS, so that `P19-STEADY-001`'s loop and this
> operator's own tests agree by construction rather than by inspection.

> Reaching the budget without converging is a FAILURE and never a result, which
> `CfdSteadySettings` states and `P19-STEADY-001` reports as
> `CfdFailureStage::IterationLimitReached`.

So the question this milestone answers is not "how do we write SIMPLE" — that
was `P19-PVCOUPLE-001`'s — but **what is left, and where does the solution live
once it exists**.

### What the repository had already decided

```text
ADR-058          the four settings records, CfdResiduals, CfdMassBalance,
                 CfdReferencePressure. This milestone defines no settings type
ADR-066          the index space: size(CfdVariable) gives every array length
ADR-068          a field is the caller's own span; ownership is the consumer's
ADR-075 Dec. 9   the coupling reports in its own vocabulary and publishes
                 nothing; the mapping onto CfdFailureStage is ours
ADR-076          the two kernels, and cfdLinearSolve() as the production port
P19-DATA-001     CfdFailureStage's nine stages, CfdSolveFailure,
                 CfdResultSlot's publication semantics, analysisState
```

### What is NOT authorized, named so the drift stays visible

No CLI: no item on the checklist is one. `P19-TRANSIENT-001` owns time stepping.
`P19-VALID-001` owns whether the numbers are good enough — `CfdResult.hpp`:
"CONVERGENCE IS RECORDED, NOT RE-JUDGED HERE." A converged run here is not a
validated one, and no turbulence, buoyancy or compressibility is in P19 v1 at
all.

## BASELINE

```text
HEAD at authorization   8f1c59544385b237fd57089a103d41f7dfb212c4
full suite              6870 tests in three presets
repeat set              3474, five executions each, two presets
cfd subsystem           751,771 assertions in 644 cases
linalg                    2,560 in  29
structural              271,414 in 415
thermal                 595,622 in 1168
CfdResultSlot consumers  ZERO in production -- it was written by P19-DATA-001
                         for a producer that did not exist
```

## ARCHITECTURE

[ADR-077](../../architecture/decisions/ADR-077-the-steady-solver-orchestrates-qualified-operators-and-the-result-gains-its-fields.md)
holds the candidates and the arguments. Seven decisions; the two that cost the
most to get right are below, and both were got wrong first.

### The result gains its fields, and the alternative was measured against it

`CfdResult` held provenance and metadata only. Four committed records deferred
the solution's `p`, `U` and `phi` to "the first milestone that computes a
field": `CfdResult.hpp`, `CfdInitialField.hpp`, `ADR-066` Decision 1 and
`ADR-068`. This is that milestone.

**`ADR-066` also warns, against itself, in the same breath:** "an expectation
recorded in a header is not an authorization. Scope comes from `TODO.md` alone."
That warning was taken seriously, and the test it implies was applied — can
"Atomic result publication" be satisfied WITHOUT the arrays?

It cannot. Without them `solveCfdSteady` returns, `analysisState` reports
`SolvedCurrent`, and no code anywhere can read `p`: the state says a solution
exists and nothing holds one. `CfdResult.hpp` already names that failure for a
narrower case — "a partially populated result is not a worse result, it is a
claim that is false" — and a result with no fields at all is its limit. So the
arrays are here because the item is otherwise unsatisfiable, and the headers
predicted them for the same reason rather than the other way round.

What it cost is stated rather than discovered later: `CfdResult::create` gains a
layout and a field set, its size now depends on the mesh, and the sentence in
`CfdResult.hpp` that said otherwise is **amended in place rather than deleted**.

### The settings come from the definition, and taking them as a parameter broke provenance

**An earlier draft of the header took a `CfdSteadySolveInput` carrying all four
settings records.** It was written before `CfdAnalysisDefinition` was read
closely, and it was wrong twice.

It was the second parameter system `ARCHITECTURE.md` rejects — the definition
already holds `iteration`, `convergence`, `linearSolver` and `steady` — which
this milestone's own scope note (1) had warned about, one milestone after
`P19-CONT-001` learned the same lesson.

And it broke provenance, which is the reason that settles it. `canonicalText`
writes seven settings lines into the definition fingerprint:

```text
iteration.velocity_relaxation             convergence.divergence_factor
iteration.pressure_relaxation             linear_solver.relative_residual_tolerance
convergence.momentum_residual_tolerance   steady.max_iterations
convergence.continuity_residual_tolerance
```

`CfdResultSource::definition` **is** that fingerprint. A run driven by
caller-supplied settings publishes a result whose provenance names one set of
settings while the numbers came from another, and `resultCurrency` reports it
`Current`. No test of the solver's numerics would ever catch that.

## BLAST RADIUS

```text
new files           include/bettercad/cfd/CfdSteady.hpp
                    src/cfd/CfdSteady.cpp
                    tests/cfd/CfdSteadyTests.cpp
                    tests/compile_fail/CfdSteadyMisuse.cpp
CHANGED, qualified  include/bettercad/cfd/CfdResult.hpp, src/cfd/CfdResult.cpp
                    -- P19-DATA-001's type. CfdResult::create's signature
                    changes, so every caller moves
callers moved       14, ALL IN TESTS, counted rather than estimated:
                    CfdDataTests.cpp x13, CfdFluidTests.cpp x1. There was no
                    production caller, which is the same fact as CfdResultSlot
                    having had no production consumer
mirrors moved       CfdData_AResultHoldsProvenanceAndMetadataOnly, renamed to
                    ...ProvenanceMetadataAndItsFields. Its sizeof assertion is
                    what STOPPED THE BUILD until the change was stated, which is
                    the job P19-DATA-001 built it for, and it is extended rather
                    than relaxed
gates moved         1, AND THIS LINE WAS ADDED AFTER THE FIRST
                    QUALIFICATION RUN FAILED ON IT.
                    compile_fail.cfdids.a-result-cannot-be-built-without-its-
                    factory names the private CONSTRUCTOR, whose parameter list
                    also changed. See below
not changed         every other module. [linalg] 2,560 in 29, [structural]
                    271,414 in 415 and [thermal] 595,622 in 1168 all measured
                    IDENTICAL to baseline after the change
```

### The blast radius was drawn around the factory, and it missed a gate

The fourteen moved callers were found by searching for `CfdResult::create`, and
that search was complete: every one of the fourteen is in the list, and they are
counted rather than estimated. What it could not find is a file that calls
`create` nowhere and names the private constructor instead:

```cpp
// tests/compile_fail/CfdIdMisuse.cpp, before the repair
[[maybe_unused]] const CfdResult conjured{CfdResultSource{}, CfdResultMetadata{}};
```

`ADR-077` Decision 2 gave that constructor a third parameter. The case still
failed to compile and the gate still exited 1 -- so nothing looked wrong -- but
GCC resolves arity before access, so the diagnostic became "candidate expects 3
arguments, 2 provided" and stopped matching the gate's `is private within this
context` regex. **The gate's claim had quietly stopped being tested while the
gate still looked like a gate.** The first qualification run is what found it,
1 failure in 6903, identically in all three presets.

The lesson is narrow enough to be reusable: **a change to a constructor's
parameter list reaches every compile-fail case that names that constructor, and
those cases are invisible to a search for the factory** -- they are deliberate
NON-callers. A private constructor has one intended caller and one intended
refusal, and the refusal is a test.

The repair passes the right arity so overload resolution reaches the private
constructor, rather than widening the regex, which would have left the gate green
and no longer about access. `FREEZE.md`'s last section records what was refused
and why, and the measured diagnostic after the repair.

## IMPLEMENTATION

`solveCfdSteady` reads a `Document` — the first thing in `cfd` to do so, which
`ADR-075` Decision 9 reserved for this milestone — and walks the gates that
exist:

```text
 1  the analysis, and its mode must be Steady
 2  ADR-058's four settings, READ OUT OF THE DEFINITION, each validated
 3  requireCfdModel -> currentResultSource (the one authority for provenance)
 4  requireCfdMesh -> requireCfdTopology -> requireCfdGeometry
                   -> requireCfdFieldMap
 5  prepareCfdBoundary, prepareCfdInitialField
 6  expand p and U into this call's own arrays (ADR-068's prescribed shape)
 7  loop while the budget allows:
      cfdMassFlux from the current U
      reconstructCfdPressureGradient from the current p, flattened per cell
      assembleCfdMomentum x3
      advanceCfdCoupling
      cfdCouplingVerdict: Diverged -> stop; Converged -> publish
 8  CfdResultFields::create -> CfdResult::create -> slot.recordSuccess
    or, on any refusal above, slot.recordFailure with the mapped stage
```

**The momentum systems are re-assembled every iteration, and that is required.**
`CfdMomentumInput` takes `faceFlux`, `currentVelocity`, `faceGradient` and
`pressureGradient`; all of them move with the fields. Caching them would freeze
the convection coefficients at the initial field and the run would converge to
the wrong answer with falling residuals all the way.

**The pressure gradient is passed, and a review of `P19-PVCOUPLE-001` is why.**
That review found every fixture in the coupling suite omitting it, with the
consequence recorded in that test file: "WITHOUT THE PRESSURE GRADIENT THE
MOMENTUM EQUATION IS MISSING ONE OF ITS TWO DRIVING FORCES ... so the pressure
was a pure OUTPUT and nothing could converge in the usual sense."

### A check that could not fire was written, and then removed

The first implementation looped over the corrected `p`, `U` and `phi` after
every iteration, refusing a non-finite value as
`CfdFailureStage::NonFiniteField`. Then `applyCfdCouplingCorrection` was read:

```cpp
if (!allFinite(out.faceFlux) || !allFinite(out.velocity) || !allFinite(out.pressure)) {
    return std::unexpected(internalFailure(CfdCouplingProblem::ResultIsNotFinite, ...));
}
```

`advanceCfdCoupling` never returns a success whose corrected fields hold a
non-finite value, so the loop **could not fire for any input**.
`P19-LINSOLVE-001` needed a mutation probe to find the same shape of defect in
two `linalg` kernels one milestone ago (its `M4e`, which removed a clause from
both). Here the layer below was read instead, and the loop is gone. "NaN / Inf
rejection" is discharged by the `stageOf` mapping, which is reachable and
tested.

## TESTS

```text
tests/cfd/CfdSteadyTests.cpp              19 cases    1,263 assertions
tests/compile_fail/CfdSteadyMisuse.cpp    14 gates

full suite   6903 = 6870 + 19 + 14            EXACTLY, counted by ctest -N
repeat set   3493 = 3474 + 19                 EXACTLY, compile_fail excluded
executions  17465 = 3493 x 5                  per repeat log
cfd        753,230 in 663 = 751,771 + 1,459 and 644 + 19   EXACTLY
```

The `cfd` delta is 1,459 assertions where this suite holds 1,263: the other 196
are in `CfdDataTests.cpp` and `CfdFluidTests.cpp`, whose fixtures gained a
`layout()` helper and its `REQUIRE`s when `CfdResult::create`'s signature
changed. 1,263 + 196 = 1,459, which is why the arithmetic closes.

**THE SUITE GREW FROM 12 CASES TO 19 DURING THE REVIEWS AND THE PROBES**, and
every one of the seven was added in answer to something measured rather than
imagined: the gravity regression test, the closed-domain publication path, the
published flux's conservation, the nine stage names, the layout's two counts, the
`analysisState` precedence, and an independent iteration counter.

Identical in all three presets: 1,263 assertions in 19 cases, debug static,
release static and debug shared.

### The independent reference

**The strongest check here is not a residual.** `CfdSteadyTests.cpp` carries its
own dense Gaussian elimination with partial pivoting, and the whole steady run
is driven twice — once with `cfdLinearSolve()`, once with that — and the two
published field sets compared.

```text
pressure   1.485e-15        relative to the largest |p|
velocity   4.626e-16        relative to the largest |U|
mass flux  1.415e-16        relative to the largest |phi|
```

Machine precision across the whole converged field, from a different algorithm:
dense LU with row pivoting against a sparse LDLT and a sparse QR, a different
storage order, and its own residual computed from its own dense copy.
`P19-PVCOUPLE-001`'s review found an "independent" oracle that was a
character-for-character transcription of production, so the distinction is
stated and kept.

### The Peclet confinement is asserted, not assumed

`P19-PVCOUPLE-001` is qualified only at LOW CELL PECLET NUMBER. The fixture's
inlet speed is 1e-5 m/s and the test `REQUIRE`s `Pe < 1` computed against the
domain's LARGEST dimension, which is conservative because every real cell is
smaller. Water at 0.1 m/s through 5 mm cells would be `Pe ~ 500`, outside every
qualified bound in the tree.

### Two predicted assertions were replaced by measurements

Both were in the first draft and both were wrong, and the sweep that corrected
them is [experiments/stage-probe.out](experiments/stage-probe.out).

**A NaN-returning solve port does not produce `NonFiniteField`.** It produces
`LinearSolveFailed`, because the coupling documents `LinearSolutionUnusable` as
"the supplied solve returned a solution of the wrong length, or one holding a
non-finite value". That is the right answer and the test was wrong, not the code.

**An amplifying port diverges only in a narrow band.** The first draft used a
gain of 8.0 and got `IllPosed`: an over-corrected velocity drives the upwind
convection coefficient negative and `assembleCfdMomentum`'s diagonal guard fires
before any residual can grow by a factor of 1000.

```text
gain 1.05, 1.2   iteration_limit_reached after 400
gain 1.5         DIVERGED after 13          <- what the test now uses
gain 2.0, 4.0    ill_posed, momentum diagonal at cell 7 goes negative
gain 1e6, 1e150  ill_posed after 1
gain 1e300       linear_solve_failed, the port's own residual gate
```

### Three of the nine failure stages are unreachable from here

Measured over fifteen driving ports, including a variant that amplifies **only**
the symmetric pressure system so that the momentum diagonal guard cannot be what
fires first. Recorded rather than papered over, and asserted by a test:

```text
MassBalanceFailed      "the mass balance failed VALIDATION" -- P19-VALID-001's
ContinuityNotClosed    would need the continuityClosed flag that ADR-075
                       Decision 5 DELETED after a probe showed it was true by
                       construction for every input
NonFiniteField         every route is intercepted by a guard in a lower
                       qualified layer, measured above
```

Manufacturing a check to light any of them would be the inverse of the rule this
milestone had already applied once, when the dead field loop was removed. The
MAPPING onto `NonFiniteField` is still total and still tested directly, because
`stageOf` must answer for every problem the layer below can raise whether or not
this milestone can drive it there.

## ADVERSARIAL REVIEW

Two independent reviews, both READ-ONLY, both before the freeze, with different
concerns: a correctness review that was asked to disprove the implementation, and
a test-adequacy review that was asked for mutations no test would catch. Neither
was shown the other's output. Between them: **one DEFECT, eight RISKs, seven
NITs and twenty-two mutation predictions.**

### The defect: gravity never reached the momentum equation

```cpp
CfdMomentumInput momentum;
momentum.density = density;
momentum.viscosity = viscosity;
momentum.component = component;
momentum.faceFlux = *flux;
momentum.scheme = CfdConvectionScheme::Upwind;
momentum.currentVelocity = velocity;
momentum.pressureGradient = *gradient;
// momentum.gravity was never set
```

`CfdAnalysisDefinition::gravity` is validated, stored and fingerprinted, and two
operators consume it. `prepareCfdBoundary` applies `hydrostaticPressure` at a
prescribed-pressure facet; `assembleCfdMomentum` applies the body force
`rho * V * g`. `ADR-073` Decision 6 states that those are **two halves of one
treatment** — "reading it here would apply the hydrostatic field twice". Only the
first half was reached.

Nothing refused it. `assembleCfdMomentum` validates gravity only WHEN PRESENT,
so an absent record is silently the gravity-free equation; `prepareCfdBoundary`'s
only gravity refusal needs an absent density, which the model always supplies.
And `canonicalText` writes `gravity.acceleration` and `gravity.reference_height`
into the fingerprint `CfdResultSource::definition` carries — so the wrong field
would publish under provenance naming gravity, and `resultCurrency` would report
it `Current`. That is the failure `ADR-077` Decision 7 was written against,
except that a dropped term changes the answer rather than the metadata.

Fixed in one line. **The regression test was verified to fail without it**,
which is the part that makes it a regression test:

```text
                        with the fix      without it
dp/dy over the cells    -5.78e-05 Pa/m    -7.98e-06 Pa/m   <- CATCHES IT
ratio to -rho*g          1.158             0.160
sign of dp/dy            negative          negative        <- does not
max|Uy| / max|Uz|        0.081             0.075           <- does not
```

**Writing that test corrected two of this author's own predictions.** The first
draft asserted that the sign of `dp/dy` would flip and that a spurious
transverse flow would appear. Neither happens: the outlet's prescribed
hydrostatic profile alone still produces a pressure falling with `y`, and it
drives almost no transverse flow. What it cannot produce is the full gradient —
it reaches 16% of `rho*g`, because only the boundary facet imposes it and nothing
carries it inward.

**And Earth gravity turned out to be unusable in this fixture.** At
`g = 9.81 m/s^2` the body force is `rho*g = 9790 Pa/m` against an inlet speed of
`1e-5 m/s`, so the induced flow leaves the LOW CELL PECLET regime
`P19-PVCOUPLE-001` is qualified in and `assembleCfdMomentum` refuses the run
outright — "the x momentum diagonal at cell 8 is -33.6 kg/s". That refusal is the
operator behaving correctly, so the test asks for a scaled `g` and asserts the
Peclet number from the measured field rather than from the inlet alone.

### The third unreachable guard, found by the test-adequacy review

Its `M11` proposed making the second of four `validate` calls check the FIRST
settings record again — a swap no test could see. It could not, and the reason is
that the guards were dead:

```text
CfdAnalysis::create         validates the whole definition
CfdAnalysis::setDefinition  validates the whole definition
the private constructor     reachable only from create
definition_                 assigned only by those two
deserialization             there is none for CfdAnalysis yet
```

and `validate(const CfdAnalysisDefinition&)` reaches `iteration`,
`convergence`, `linearSolver` and the steady branch's `*steady`. So every
definition reachable from `solveCfdSteady` has already passed every one of those
gates. **Four of the five guards were removed**; the fifth was kept, because what
follows it is `*definition.steady` and dereferencing an empty optional is
undefined behaviour rather than a wrong diagnostic — the same distinction
`P19-LINSOLVE-001` drew when it kept a guard in front of a reachable
out-of-bounds read.

That is the third time this milestone that a guard was written and then found
unable to fire. The first was the non-finite field loop (`ADR-077` Decision 4),
the second was the same loop's documentation, and this is the third. The only
honest resolution for an unreachable guard is removal: a test cannot be written
for one.

### What else was acted on

```text
the Diverged diagnostic   named only the factor and the iteration. It now names
                          WHICH of the four criteria grew and both its values,
                          as ARCHITECTURE.md requires. Its first run then
                          confirmed, independently, the review's finding that the
                          x/y momentum baselines are exactly zero from a zero
                          initial field -- it is CONTINUITY that trips (L9)
the dense tolerance       was 1e-8 against a 1.485e-15 measurement, seven decades
                          of slack. Now 1e-13, two decades, which is this
                          project's own practice
the iteration counts      the two runs' counts are now asserted EQUAL. The
                          1e-15 agreement silently depended on it: one iteration
                          apart, the fields differ by order 1e-4 and the test
                          would fail by four decades, reading as a numerical
                          regression rather than a stopping disagreement
four stale sentences      two headers and one source still described the removed
                          field check as a live step, including as step (e) of a
                          numbered list
CfdResult.hpp's reason    for its finiteness invariant named a guard that no
                          longer exists. The invariant is sound; the reason was
                          false
ADR-077 L8                said TWO unreachable stages where the evidence and the
                          test said three. CLAUDE.md makes the evidence
                          authoritative, so the ADR was the wrong one
ADR-077 Decision 4        claimed a test asserts the published stage is
                          NonFiniteField. It asserts LinearSolveFailed
"the same object"         the returned and recorded failures are equal by value
                          and built from one initializer, not one object
```

### What was NOT acted on, and why

Eight findings are recorded as limitations rather than fixed: `L9` the divergence
baseline's zero-residual inertness, `L10` the mixed-vintage residuals, `L11` the
closed domain's `(0, 0)` mass balance, `L12` the scheme and method being outside
the fingerprint, `L13` a throwing solve port, `L14` the ignored residual history,
and `L16`/`L17` the probe gaps a test cannot close.

`L12` is reported to the owner as a scope decision: `ADR-073` Decision 5 says
"which of `ADR-070`'s two methods made it is the analysis's decision and not this
operator's", which is the opposite of what the code does — but putting them in
the definition means changing `CfdAnalysisDefinition` and `canonicalText`, both
qualified under `P19-DATA-001`, and no item on this checklist asks for a
selector.

### What the correctness review verified clean

Explicitly, by category, because a review that only lists findings does not say
what it looked at: the iteration counting has no off-by-one (with
`maxIterations = N` the body runs exactly `N` times); no `return` path skips
publication; the fields published are the last iteration's; the provenance
cross-checks hold including the second `requireCfdModel` call, which is read-only
and cannot publish a stamp from another generation; `denseSolve` is genuinely
independent and was read line by line; determinism holds with no unordered
container, wall clock or pointer identity; save/load and undo/redo cannot
resurrect a false `Current`; no tolerance was loosened to pass; and the 14
compile-fail guards each fail for the stated reason.

## MUTATION PROBES

```text
round one    22 probes    8 KILLED, 14 SURVIVED, both no-op controls correct
round two    13 probes    8 KILLED,  5 SURVIVED, control correct
round three   2 probes    1 KILLED,  1 SURVIVED (the control)

behavioural probes, distinct    20, excluding the two no-op controls
killed after the fixes          17
surviving BY DESIGN              3, each a documented limitation: L15, L16, L17
author predictions wrong         2, both recorded rather than quietly fixed
reverts verified by hash        every probe, all three rounds, all four files
```

**TWENTY-TWO OF THE TWENTY-THREE PROBES ARE A REVIEW'S PREDICTIONS, NOT THE
AUTHOR'S.** The twenty-third is the DEFECT the correctness review found, added so
that its fix is pinned rather than trusted — and it is KILLED, which is what says
the gravity regression test is doing its job.

The full table and what each round changed is in
[experiments/mutations-summary.md](experiments/mutations-summary.md). Three
things from it are worth stating here.

**`M12` was the campaign's most valuable find.** It replaced the published mass
flux with the pre-correction flux and survived the whole suite — a
non-conservative field published as a solution, with nothing anywhere looking at
conservation. Closing it produced this milestone's independent validation of the
solution:

```text
published flux   worst per-cell |sum phi| / throughput   7.07e-17
                 global imbalance                        7.32e-17
the SAME measure on the pre-correction flux              7.26e-05
```

Checked by `divergeCfdMassFlux`, `P19-DIV-001`'s qualified operator, which knows
nothing about this milestone.

**`M2` survived TWICE, and the author's own fix is why.** It makes
`report.outerIterations` off by one. The first closure asserted
`outerIterations <= maxIterations`, which is worthless when the budget is 400 and
the run converges in sixty-one. What closes it is an independent counter: a
counting solve port, because every outer iteration performs exactly four linear
solves — measured at 244 calls over 61 iterations, ratio exactly 4.

**Three of the review's SURVIVE predictions were already dead when round one
ran**, killed by tests written in answer to the *other* review. Neither reviewer
saw the other's output.

## REGRESSION

Every figure below was counted off the gate's own logs after the run, and every
one was predicted in `FREEZE.md` BEFORE it. Both reconciliation scripts are
kept, bugs included, because the second exists to fix the first:

```text
experiments/reconcile.py    the first pass. Exits non-zero on any mismatch
                            rather than printing one -- and it DID, five times:
                            three from a ctest summary clause that cannot exist
                            in a passing run, and two from an identity parse
                            that read one path per block instead of eight
experiments/reconcile2.py   the corrected pass for exactly those two checks,
                            and it asserts that the identity comparison holds
                            eight paths BEFORE reporting the two readings equal
experiments/freshness.py    the build-graph freshness query. Runs no build
```

The first script's five mismatches were its own and not the gate's, which was
established by reading the logs rather than by assuming it — and the second of
the two bugs had reported a PASSING check (`the two readings are equal`) off one
parsed path, which is the more dangerous of the two.

```text
                          debug-ext   release-ext   debug-shared-ext
Start markers                  6903          6903               6903
progress-prefixed lines        6903          6903               6903
result lines                   6903          6903               6903
Passed                         6903          6903               6903
***Failed / Exception / Timeout   0             0                  0
"tests failed out of"        absent        absent             absent
summary line        100% tests passed out of 6903, all three
```

**FOUR COUNTS OF THE SAME NUMBER, NOT ONE NUMBER FOUR TIMES.** Start markers,
progress-prefixed lines, result lines and `Passed` are four different regexes
over the same log, and a log that lost a test would disagree with itself.

```text
                                  repeat release-ext   repeat debug-ext
Start markers (EXECUTIONS)                     17465              17465
result lines (EXECUTIONS)                      17465              17465
Passed (EXECUTIONS)                            17465              17465
progress-prefixed (SELECTED)                    3493               3493
ratio markers : progress                       5.000              5.000
unit.CfdSteady_ executions                        95                 95
```

19 x 5 = 95, so the new suite is inside the repeat set rather than merely
selected by it. The ratio is EXACTLY 5.000 in both logs, which is the check that
`--repeat until-fail:5` ran five times and not four.

### The wording that a passing run does NOT print

The first reconciliation pass reported three mismatches, and all three were the
script's fault:

```text
clean run    100% tests passed out of 6903
failed run    99% tests passed, 1 tests failed out of 6903
```

`ctest` adds the failed clause ONLY when something failed, and the script looked
for `0 tests failed out of 6903` — a string that cannot exist in a pass. The
useful consequence is the opposite test: **the PRESENCE of "tests failed out of"
is itself a failure marker**, and it is absent from all five logs. That check
replaced the broken one.

### The builds, and the only substring that is allowed to look like a problem

```text
error:    0 occurrences in all three build logs
warning:  0 occurrences in all three build logs, under -Werror
```

The only `[Ee]rror` substrings in any build log are the filenames `Error.cpp`,
`Error.hpp` and `ErrorTests`, counted and listed rather than assumed.

### Per-group counts, identical in all three presets

```text
architecture                          205 of 205, architecture.layering included
compile_fail.cfdsteady                 14, this milestone's group
compile_fail.cfdids                    28, the group whose gate the first run
                                           failed -- including the repaired one,
                                           which reads Passed in all three
unit.CfdSteady_                        19
```

### Assertion identity, read FROM THE THREE QUALIFIED BINARIES

`ctest` captures no stdout from a passing test, so these were read by running
each qualified `bettercad_tests.exe` under a tag filter. Running a binary
rebuilds nothing.

```text
tag                 assertions     cases    debug-ext  release-ext  debug-shared
[cfd]                  753,230       663      same        same         same
[linalg]                 2,560        29      same        same         same
[structural]           271,414       415      same        same         same
[thermal]              595,622      1168      same        same         same
[cfd][steady]            1,263        19      same        same         same
```

```text
[cfd]      751,771 + 1,459 = 753,230       and   644 + 19 = 663
[linalg], [structural], [thermal]          IDENTICAL to baseline
```

The last line is the measurement a passing suite cannot make: this milestone
changes `CfdResult`, which only `cfd` uses, and a deleted test plus an added
test net to nothing in a pass/fail count while they do not net to nothing here.

## QUALIFICATION

**THE FIRST GATE FAILED.** `QUALIFICATION FAILED: 3 stage(s) failed`, one test in
6903, in all three presets, while the launcher reported "completed (exit code
0)". What it found, why widening a regex was refused and what the blast radius
missed are in `BLAST RADIUS`, `REVISION` r6 and
[qualification/voided-run-1/](qualification/voided-run-1/). The repair changed
`tests/compile_fail/CfdIdMisuse.cpp`, which is inside the frozen surface, so the
first qualification was void and the gate was re-run from clean.

### The verdict, which is the harness's own two statements

```text
run-qualification.out      Qualification passed: every stage exited 0.
qualification-times.txt    0 stage(s) failed
stages with an exit code   17
stages that exited 0       17
```

**NOT THE LAUNCHER'S EXIT CODE.** It read 0 for the run that failed and 0 for the
run that passed, so on its own it distinguishes nothing. Two milestones in a row
have now turned on that distinction.

```text
started    Mon 2026-11-09 18:59:10
finished   Mon 2026-11-09 22:23:06      3 h 24 min
17 stages  3 presets x (configure, clean, build, no-op rebuild, ctest)
           + 2 repeat sets at --repeat until-fail:5
```

### The qualified tree is the committed tree

The eight frozen paths were read FOUR times: by the harness before its first
build, by the harness after its last test run, recomputed from the working tree
during this reconciliation, and finally out of the commit object itself.

```text
apps               fc398a3d2e524178cf38e8b667236379d8381086
include            588dd50bf9a82cb58b3a93ccdcfe752fcdf896df
src                9fce91ebc86a4095fcc24e821bb86a793d835650
tests              f3eb700ce67c512b15001b75471cba873a12cc55
examples           4da234ebf4779f7c16d5df0075f106f7e3d4c958
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e
```

`include` and `src` are bit-identical to the FIRST freeze; only `tests` moved for
the repair, which is the check that the repair touched no production code.

**THE EQUALITY CHECK WAS ITSELF CHECKED.** The first reconciliation pass reported
the two readings equal while having parsed only ONE path out of each block — two
one-element dicts compare equal just as well as two eight-element ones. The
script now asserts that both readings hold eight paths before it reports them
equal, and compares them to `FREEZE.md`'s table as a third party. A comparison
that cannot fail is not a comparison, which is the same shape of error as the
internally consistent assertion figure `P19-LINSOLVE-001` found.

### Freshness, asked of the build graph

```text
                       graph outputs   existing on disk   NEWER than build exit
debug-ext                       5961               1148                       0
release-ext                     5961               1148                       0
debug-shared-ext                5974               1161                       0
```

Asked of `ninja -t targets all`, which is a GRAPH QUERY and runs no edge — not of
file extensions, which is what flagged 24 zero-byte stubs on
`P19-LINSOLVE-001` that the deployment TESTS write into their own ctest scratch
directories. The reference time is each build log's own mtime, which matches the
recorded build exit to the hundredth of a second. Most graph outputs do not
exist because the compile-fail case targets are `EXCLUDE_FROM_ALL` and their
objects are never produced — the compiles fail by design.

**NO BUILD WAS RUN DURING THIS RECONCILIATION.** A relink would move the
binaries' mtimes and destroy the evidence being checked, which is
`P19-LINSOLVE-001`'s `r20` failure. Each binary's own mtime sits a few seconds
BEFORE its build exit — the link, then the build's wrap-up — which is what zero
newer should look like from the other direction.

### The no-op rebuilds, and a figure this file had wrong

```text
debug-ext           [1/8]  Checking git revision      one line, one edge
release-ext         [1/8]  Checking git revision      one line, one edge
debug-shared-ext    [1/20] Checking git revision      one line, one edge
```

Exactly one edge ran in each preset and it was the always-dirty git-revision
stamp: no real work was redone, which is what a no-op rebuild has to show.

**`FREEZE.md` predicted `[1/4]` and `1/16` and the gate measured 8, 8 and 20.**
The bracketed total is ninja's PLANNED edge count, not work done: the stamp's
dependents are planned, the stamp runs, and restat then prunes the rest. The
plan size depends on the graph's state when it is asked, so the pre-freeze
figure was measured on a differently-dirty tree and was never a property of this
milestone. The claim that matters — one edge, and that edge the stamp — held in
all three. The 8 also agrees with the 8 edges a `ninja -n` dry run plans, for the
same restat reason, which is why a dry run is not a staleness check.

### Caches

```text
all three    CMAKE_COMMAND and CMAKE_CXX_COMPILER under the WinLibs package path
all three    0 msys64 references
all three    one deps prefix, bettercad-deps/gnu-16-mingw-amd64
distinct     Debug/OFF, Release/OFF, Debug/ON -- three configurations, not one
             repeated
```

### The harness

Re-verified immediately before the second launch, by hash and not by assertion:

```text
qualify.cmd            244b38de8feac0475af7cdc56ba9415d27d8dd29   sha1sum
verify-harness.cmd     afd0197d4f4726db7f533c2f27f4f6e069ba83c6   sha1sum
launch.cmd             cf6c1f112d71586098a78652091d0375c8d04051   sha1sum
preflight-space.cmd    59910328ab43e07b9e0c474f969994ad483d3f68   sha1sum
preflight-space.ps1    b76d9325e8131c265443540a9ac914e236731a66   sha1sum
run-qualification.cmd  f8b90f59cc729226a3ba15a8718fc979387ba561   git hash-object
```

All six match `FREEZE.md`, so the repair touched the tree and not the instrument.
The two hashing conventions are labelled wherever they appear and never agree for
the same file.

### A launch that did not launch

The second gate's first launch did nothing: `'launch.cmd' is not recognized`,
because the background shell did not resolve a relative path. It is recorded
because a launcher that exits without running the harness is exactly the failure
mode `P19-PVCOUPLE-001`'s probe driver had — exited 0, wrote nothing, read like
evidence. `run-qualification.out` still held the FIRST run's content afterwards,
which is how the no-op was caught; the relaunch used an absolute path, and
`launch.cmd` sets its own directory.

## RESULT

```text
TASK:            P19-STEADY-001 -- Steady Incompressible Solver
IMPLEMENTATION:  solveCfdSteady orchestrates fifteen already-qualified
                 operators; CfdResult gains its field arrays in a nested
                 validating record (ADR-077 Decisions 2 and 7)
TESTS:           19 cases / 1,263 assertions, identical in all three presets
                 14 compile-fail gates, plus one inherited gate repaired
VALIDATION:      a dense LU reference agreeing to 1.485e-15 / 4.626e-16 /
                 1.415e-16, and the published flux's conservation measured
                 through P19-DIV-001's divergeCfdMassFlux at 7.07e-17 worst
                 per-cell against 7.26e-05 for the pre-correction flux
RESULT:          PASS
EVIDENCE:        this directory; qualification/ holds the second run's logs and
                 voided-run-1/ the first run's verdict
TODO:            19 boxes ticked, 719 -> 738
```

**The gate passed on the second freeze.** The first failed, and the failure was
this milestone's: `ADR-077` Decision 2 turned an inherited access gate into an
arity gate without it ceasing to compile-fail, so the gate still exited 1 while
the claim it holds had stopped being tested. The production code was never
wrong; the proof was. It was repaired by making the gate select the private
constructor again, not by widening its regex.

What this milestone is honest about rather than quiet about:

```text
L9 to L17        nine recorded limitations, eight of them a review's findings
                 that were NOT fixed, each with the reason
3 of 20          behavioural probes survive BY DESIGN, each with the reason a
                 test cannot reach it
3 of 9           CfdFailureStage values are unreachable from solveCfdSteady,
                 measured over 15 driving ports rather than argued
2 predictions    of the author's own were wrong and are recorded as wrong
1 scope decision L12, reported to the owner: ADR-073 Decision 5 says the
                 convection scheme and gradient method belong in the analysis
                 definition, and they are hard-coded outside the fingerprint
```

`L12` is the one an owner should read first. Of the two hard-coded numerical
choices, one is pinned only INCIDENTALLY — by a side effect of the gravity
test's `dp/dy` check — and the other is not pinned at all, because distinguishing
the two gradient methods needs an analytic reference this subsystem does not
have. Both would become observable if they were in the definition, which is what
`ADR-073` already says they should be, and no item on this checklist authorizes
putting them there.

## REVISION

**r1** — The authorization and its seven scope notes were written into `TODO.md`
before any implementation, as the nine milestones before it did. The checklist
diff exits non-zero on a difference rather than printing one.

**r2** — `CfdSteadySolveInput` existed, carrying `ADR-058`'s four settings
records, and was removed before any test was written. `canonicalText` puts every
setting in the definition fingerprint, so the struct would have let a run publish
provenance it did not have. Found by reading `CfdAnalysisDefinition` rather than
by testing, and no test of the numerics would have caught it.

**r3** — A non-finite field check was written and then removed as unreachable,
for the reason `P19-LINSOLVE-001`'s `M4e` probe gave one milestone earlier.

**r4** — Two test assertions were predictions and both were wrong. They are
replaced by a measured sweep, and the wrong predictions are recorded above
rather than quietly corrected.

**r5** — The compile-fail group's first run had 13 of 14 gates pass. The
fourteenth, `there-is-a-steady-solve-input-type`, FAILED TO COMPILE CORRECTLY
and was only mis-ATTRIBUTED: GCC says "`'CfdSteadySolveInput' is not a member of
'cfd'`" and the regex group it was in did not list that wording. This is the
`P19-GRAD-001` lesson that the group's own comment cites, caught by running the
gates rather than by trusting the list. It now has its own regex.

**r6** — **THE FIRST QUALIFICATION RUN FAILED AND VOIDED THE FIRST FREEZE.**
`QUALIFICATION FAILED: 3 stage(s) failed`, one test in 6903, the same one in all
three presets, while the launcher reported "completed (exit code 0)" — which is
the second milestone in a row where the launcher's exit code and the harness's
verdict disagreed, and the reason `FREEZE.md` says the verdict is the harness's
own two statements.

The failing gate was `compile_fail.cfdids.a-result-cannot-be-built-without-its-
factory`, inherited from `P19-DATA-001` and not written by this milestone. It had
been turned from an ACCESS test into an ARITY test by `ADR-077` Decision 2's
third constructor parameter, without ceasing to compile-fail or to exit 1. The
production code was never wrong; what this milestone broke was the proof.

Repaired by passing the right arity so the private constructor is actually
selected — measured afterwards, `is private within this context`, with the group
control still compiling and both affected groups at 42 of 42. Widening the regex
was refused: it would have produced a PASS by retiring the gate. `BLAST RADIUS`
above now carries the line it was missing, and says that it was missing.

Only `tests` moved for the repair; `include` and `src` are bit-identical to the
voided freeze, which is the check that no production code changed. The gate was
re-run from clean on the second freeze.
