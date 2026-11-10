# P19-TRANSIENT-001 — Transient Incompressible Solver

```text
RESULT:   PASS -- 2026-11-10, nineteen of nineteen, ON THE THIRD
          QUALIFICATION ATTEMPT. The two earlier attempts failed and are kept
          as failures, with their diagnoses, in qualification/voided-run-1/
          and qualification/voided-run-2/. Neither implicated the tree and no
          product code changed between the three attempts. Every figure in the
          REGRESSION and QUALIFICATION sections below is read from the
          harness's own logs and from nothing else.
```

## TASK

Add transient Navier–Stokes integration: backward Euler, a fixed step from
`t = 0` to the analysis's end time, the last step shortened so the run lands on
the end time exactly, pressure–velocity coupling inside every step, and a
history whose last sample is the analysis's answer.

Nineteen checklist items, authorized 2026-11-10 by the owner's "implement and
fully verify" over a section pasted into the instruction. The pasted section was
diffed against `TODO.md`'s own before any code was written:

```text
pasted section == TODO.md lines 4021-4054, byte for byte, 0 differing bytes
                  34 lines, 700 bytes, sha256 789ccb1d82fa2813...
items             19 unticked, 0 ticked
whole file        738 ticked / 292 unticked, the committed state at 10c871a
```

Every earlier P19 milestone is complete, and `P19-TRANSIENT-001` is the first
open one, so no dependency was skipped to reach it.

## SCOPE

### What the repository had already decided

The UNDERSTAND phase found that this milestone's hardest single piece — the
transient term itself — was qualified two milestones ago, and that the data
model had been built for this milestone four milestones before that.

```text
Transient momentum term     CfdMomentumInput::timeStep + previousVelocity,
                            backward Euler, no selector      P19-MOM-001
                                                             ADR-073
Pressure-velocity coupling  advanceCfdCoupling               P19-PVCOUPLE-001
Per-step residual           advanceCfdCoupling step 7        ADR-058
Per-step mass balance       evaluateCfdContinuity            P19-CONT-001
Define dt / end time /      CfdTransientSettings, and canonicalText already
  output times              fingerprints all four fields     P19-DATA-001
Reject invalid dt           validate(CfdTransientSettings)
Reject time ordering        the same validator: an end time shorter than one
                            step is refused there
Reject NaN / Inf            applyCfdCouplingCorrection refuses a non-finite
                            corrected field; stageOf maps it P19-STEADY-001
Define initial fields       prepareCfdInitialField           P19-IC-001
```

`CfdResultMetadata` has carried `time` and `step` since `P19-DATA-001`, with
`CfdResult::create` already enforcing "transient: a finite time > 0 s and a step
>= 1" and already documenting that "t = 0 is the initial condition, not a
solve". `validate(CfdAnalysisDefinition)` already required a transient analysis
to carry transient settings and to carry no steady ones.

So what is genuinely new is **the grid, the march and the sampling**. Four
questions were open, and `ADR-078` answers them: where the time grid lives, what
gets published, how the step loop relates to the steady loop, and what
"time-dependent" means in a data model that holds no time functions.

### What is NOT authorized, named so the drift stays visible

```text
P19-VALID-001    whether the numbers are good enough. CfdResult.hpp:
                 "CONVERGENCE IS RECORDED, NOT RE-JUDGED HERE". A march that
                 converges at every step is not a validated one, and nothing
                 in this directory claims otherwise.
P19-POST-001     gradients, fluxes, forces, integrals over a history
P19-MASSBAL-001  a mass balance over the whole time span
P19-PERSIST-001  persisting a history. Every run starts from the canonical
                 initial condition; there is no restart.
turbulence       P19-TURB-ARCH-001 onwards
time-dependent   ADR-058 deferred them from P19 v1. This milestone states the
boundary data    POLICY and enforces it, and does not add them.
a scheme         backward Euler is fixed by ADR-058. No theta, no selector.
selector
adaptive dt      no step rejection, no retry, no refinement during a run
ADR-077 L12      the convection scheme and the gradient method stay hard-coded
                 and outside the fingerprint. Inherited, reported, NOT fixed.
```

## BASELINE

Measured on `debug-ext` before any file was written, and again after:

```text
                       before            after
[cfd]                  753230 / 663      755043 / 697
[cfd][steady]          1263 / 19         1263 / 19      IDENTICAL
[linalg]               2560 / 29         2560 / 29      IDENTICAL
[structural]           271414 / 415      271414 / 415   IDENTICAL
[thermal]              595622 / 1168     595622 / 1168  IDENTICAL
architecture           205               205            IDENTICAL
ctest total            6903              6965
```

`755043 = 753230 + 1813` and `697 = 663 + 34` exactly, and
`6965 = 6903 + 10 + 24 + 28`, so the whole delta is the new suite and the new
compile-fail group and nothing existing moved. The steady suite being
bit-identical is the check that `ADR-078` Decision 4 was kept: `CfdSteady.hpp`
and `CfdSteady.cpp` were not edited.

## ARCHITECTURE

`ADR-078`, eight decisions. Three were architecturally significant enough to
carry candidates and a comparison; the rest are recorded because they are
invisible in a diff.

### The time grid is local to cfd, because the layering forbids reusing P18's

`P18-TRANSIENT-001` holds a qualified `TimeGrid` with exactly the semantics
needed, and `CfdTransientSettings` points at it by name. It is **unreachable**:
`ADR-059` put `cfd` at layer 42 and `thermal` at 45 so a future coupling can run
out of CFD into thermal, and `tests/architecture/CheckLayering.cmake` enforces
the mirror image. The architecture suite even carries a dedicated negative
fixture, `tests/architecture/fixtures/cfd-links-thermal/`.

Three candidates were compared (a cfd-local type; moving P18's down to `core`;
no type at all). Moving P18's type is a refactor of a qualified tree whose
honest cost is re-running P18's gates, which this checklist does not authorize;
no type at all makes six of the nineteen items reachable only through a full
solve on a meshed document. The cost of the choice is stated rather than
discovered later: the grid *algorithm* now exists twice in the tree and no build
check can see a divergence between the copies (`ADR-078` L1).

### The slot holds the final sample, and `EveryStep` is `t_1 … t_N`

`CfdResultSlot` holds at most one `CfdResult`, and a `CfdResult` is the solution
"at one time level". The final sample goes through the slot; every requested
sample is on the report, each a full `CfdResult` with its own provenance, time
and step. No qualified type changed.

P19's `EveryStep` keeps `N` samples where P18's keeps `N + 1`, and the
difference is forced rather than chosen: `CfdResult::create` refuses a sample at
`t = 0`, because no system was solved to produce it. The `t = 0` state is on the
report as a plain array, which is what it is.

### A separate orchestrator, and `solveCfdSteady` is untouched

P18 made the same split and it is qualified. Expressing steady as a one-step
transient is wrong on the physics — the steady momentum form has no
`rho V / dt` at all, and `CfdMomentumInput` enforces that distinction — and
extracting a shared body would be a refactor of a file qualified one day
earlier. The declared cost is that the ORDER of five operator calls appears in
two files; the mitigation is a measurement, not a promise (see INDEPENDENT
VALIDATION).

## BLAST RADIUS

```text
new                include/bettercad/cfd/CfdTransient.hpp
                   src/cfd/CfdTransient.cpp
                   tests/cfd/CfdTransientTests.cpp
                   tests/compile_fail/CfdTransientMisuse.cpp
                   docs/architecture/decisions/ADR-078-*.md
registration       src/cfd/CMakeLists.txt         1 line
                   tests/CMakeLists.txt           4 lines
                   tests/compile_fail/CMakeLists.txt  the new group
declarations       NONE changed. No existing signature, field, enumerator or
changed            default moved.
```

**The radius is small because nothing existing changed, and that was verified
rather than asserted.** `P19-STEADY-001`'s qualification gate failed because a
private constructor gained a parameter and an inherited compile-fail case that
NAMED that constructor silently became an arity test — a deliberate non-caller,
invisible to a search for the factory's callers. The lesson is in `TODO.md` and
in `ADR-077` Decision 2. Applied here, the question is not "who calls what I
changed" but "what is sensitive to what I ADDED":

```text
a new public header       nothing enumerates the cfd headers; no GLOB in any
                          CMakeLists, so the new files had to be registered
                          explicitly and a missed registration is a link error
a new test tag            nothing asserts the set of tags
a new compile-fail group  the group's control target must COMPILE, and it does
the layering              cfd(42) may include core(0), features(20),
                          meshing(40) -- and NOT thermal(45). P18's TimeGrid is
                          cited in comments and included nowhere.
```

The three identical baselines above (`[cfd][steady]`, `[linalg]`,
`architecture`) are the measurement that the radius really was this small.

Serialization: none — there is no load path for `CfdAnalysis`, and a result is
derived state that is never persisted. Stable references: untouched. CLI/GUI:
there is no CFD CLI yet (`P19-CLI-001` is open). Reference models: unaffected.

## IMPLEMENTATION

```text
CfdTimeGrid            a value type: create / stepCount / timeAt / stepAt /
                       endTime / nominalStep / hasPartialFinalStep /
                       integratedSpan / times. Private default constructor, so
                       possession is the evidence (ADR-036).
CfdTransientStepRecord one per step: index, time, stepSize, outerIterations,
                       firstResiduals, residuals, balance, the three summary
                       measures, and the transient coefficient range.
CfdTransientReport     the grid, the step records, the samples as CfdResults,
                       the sample times, the t = 0 arrays, the policy and the
                       total iteration count. No default constructor, because
                       the grid has none.
solveCfdTransient      the march. Settings from the definition; no settings
                       parameter (ADR-077 Decision 7, inherited).
kMaxCfdTimeSteps       1,000,000 steps
kMaxCfdStoredValues    50,000,000 field values
kCfdTimeGridTolerance  1e-9 relative, for `endTime / dt` being a whole number
```

### Two guards were kept and no `validate` call was added

`!definition.transient.has_value()` and `grid->stepCount() == 0` are both
unreachable through any definition a `Document` can hold — `validate` requires a
transient analysis to carry transient settings and requires
`endTime >= timeStep > 0`, so the ratio is at least 1. Both are kept anyway,
because each stands in front of a dereference: `*definition.transient` and
`samples.back()`. That is the distinction `P19-STEADY-001` drew and this file
follows: an unreachable guard in front of undefined behaviour is cheap
insurance, and an unreachable guard in front of a `validate` call is dead code.

Four `validate` calls were therefore NOT added, for the reason
`currentResultSource` already names: "EVERY INPUT GATE, INHERITED RATHER THAN
RE-ASKED".

### The two velocity arrays are the whole milestone

`CfdMomentumInput` takes `currentVelocity` (the latest outer iterate, which
diffusion's symmetry cross terms are taken from) and `previousVelocity` (`U^n`,
frozen for the whole of a step's inner loop). They are spans of the same length
in the same layout, so the type system cannot tell them apart, and `ADR-073`
Decision 4 records the consequence of passing the current field as the previous
one: the transient term's two halves cancel, the system becomes the steady one
perturbed toward its own current state, **and the run converges to the correct
steady answer with every steady assertion passing.**

The march therefore copies `velocity` into `previousVelocity` at the top of each
step and never touches it again until the next step. Two tests can see the
defect, and both are checklist items — see INDEPENDENT VALIDATION.

## TESTS

```text
tests/cfd/CfdTransientTests.cpp           34 cases, 1813 assertions
tests/compile_fail/CfdTransientMisuse.cpp 28 gates + 1 control
```

Ten of the thirty-four cases are about `CfdTimeGrid` alone and need no mesh, no
solve and no document, which is the testability argument `ADR-078` Decision 1
made for having the type at all.

**Twenty-nine of those cases and about 1440 assertions were there before the
reviews; five cases and roughly 370 assertions were added because of them.** The
five are named in ADVERSARIAL REVIEW below.

### The fixture starts from plug flow, and that is a correctness requirement

`CfdInitialCondition` is uniform. A uniform velocity equal to the inlet speed is
**divergence-compatible** with the boundary: a uniform field carries zero net
flux through every closed cell, and the start cap's prescribed inflow is exactly
what that field carries. A ZERO initial velocity is NOT compatible — the inlet
pushes mass into a field that carries none — so `t = 0+` would be an impulsive
start, where incompressibility forces the velocity to jump by O(1) and the
pressure to scale as `1/dt`. The first-step change would then not be O(dt), and
the refinement assertion would be measuring the wrong thing.

Starting from plug flow makes the march the classical start-up problem, which is
smooth in time and is what backward Euler's order can be read off.

### The timescales are computed, not guessed

```text
viscous      rho h^2 / mu = 998 * 0.01^2 / 1.002e-3  ~  100 s
convective   L / u        = 0.040 / 1.0e-5           ~  4000 s
```

so `dt = 0.1 s` is strongly transient and `dt = 1e7 s` is effectively steady.
Each is asserted against the computed timescale with a `REQUIRE` rather than
assumed in a comment, and the cell Peclet confinement `P19-PVCOUPLE-001` is
qualified in is re-asserted here too.

### Measured iteration counts, which say the inner loop really couples

```text
dt = 0.1 s                  step 1: 60 iterations   step 2: 8
dt = half a viscous time    57, 33, 15, 4
dt = 1e7 s (one step)       59   -- and the steady solve took 59 too
```

Step 1 is expensive for a physical reason: `p^0` is zero everywhere while the
developed field needs a pressure gradient, and the pressure is a constraint
rather than an evolving field, so the whole pressure solution is built inside
step 1. Step 2 inherits it as a guess.

**The first measurement forced a change.** The suite's per-step budget was 60,
and step 1 measured exactly 60 — one iteration from a false failure on any
preset that rounds differently. It is now 200, and the reason is recorded beside
it. A suite that happened to pass on its first attempt by one iteration is not a
suite that passes.

### The two policy items are build failures, because a negative claim needs one

"Time-dependent BC policy" and "Time-dependent load policy" are claims that
nothing varies with time. Thirteen of the twenty-eight compile-fail gates are
"no member named" gates over every record a time profile would have to live in —
`VelocityInlet`, `PressureOutlet`, `CfdBoundaryCondition`, `CfdGravity`,
`CfdInitialCondition`, `CfdTransientSettings` — plus gates that a prescribed
speed and a gravity vector do not accept a callable, and that the solver takes
no time argument. If a later milestone adds a time profile, those gates stop
compiling and the decision is revisited, which is the outcome the policy wants.

A runtime companion was added and then **corrected**: the prescribed boundary
throughput must not move with time. The first version of that assertion demanded
equality to `1e-9` relative and failed at `4.1e-7`, which was the ASSERTION's
defect and not the march's — `continuityScale` is the boundary throughput and
only the INLET half of it is prescribed, while the outlet flux is computed and
converges to the inlet's value only to the `1e-4` the definition accepts. The
bound is now that derived `1e-4`, with the measured `4.1e-7` recorded.

## INDEPENDENT VALIDATION

Five independent references. Two of them are the checklist items that catch the
one defect this milestone can introduce; one is analytic; one is the previous
milestone's qualified solver; and one is the previous milestone's own measured
figure.

### An analytic reference, for the boundary throughput

`continuityScale` is the boundary throughput, and for a channel with one
prescribed inlet and one outlet that has a closed form:

```text
rho u A = 998 * 1.0e-5 * (0.040 * 0.030) = 1.1976e-5 kg/s     analytic
                                           1.1976e-5 kg/s     measured
```

Five significant figures, against a number computed from the fixture's
dimensions and nothing else. It is the only genuinely independent assertion
available for any of the step record's five summary pass-throughs, and it was
added because a mutation analysis showed every one of them was satisfied by a
hardcoded positive constant.

The bound asserted is `1e-2` relative, derived rather than fitted: only the
INLET half of the throughput is prescribed, and the outlet flux converges to it
only to the `1e-4` the definition accepts.

### The steady suite's own gravity measurement, reproduced through the transient path

```text
                        steady (P19-STEADY-001)   transient, one large step
dp/dy over the cells    -5.78e-05 Pa/m            -5.77935e-05 Pa/m
ratio to -rho*g          1.158                     1.15819
the defect's signature   0.160                     excluded by a wide margin
```

Four significant figures of agreement with a figure measured a day earlier by a
different orchestrator, on the same body with the same body force. This is both
a validation of the transient path's body force and a second cross-check
between the two orchestration bodies that `ADR-078` Decision 4 left duplicated.

### Time-step refinement: the first step's change is first order in dt

```text
dt            0.2          0.1          0.05
|U^1 - U^0|   2.6147e-08   1.31833e-08  6.61937e-09
ratio                1.9834        1.9916
```

Backward Euler from a divergence-compatible state gives `U^1 - U^0 = O(dt)`, so
halving `dt` halves the change, and the measured ratios approach 2 from below as
`dt` shrinks. **With the `U^n` defect this ratio would be 1**, because the step
would converge to the steady answer at every `dt`.

### The steady limit, from two directions

```text
one step at dt = 1e7 s vs solveCfdSteady on the same document
  relative |U - U_steady|   1.50592e-07
  relative |p - p_steady|   3.8991e-08
  outer iterations          59 and 59
```

At `dt = 1e7 s` — `1e5` viscous times — `rho V / dt` vanishes from the matrix
relative to convection and diffusion, so one step must land on the steady
solution. This is also the cross-check `ADR-078` Decision 4 promised in place of
a shared loop body: if the two orchestration bodies ever drift apart, an O(1)
relative difference appears here.

The bound asserted is `1e-3`, DERIVED and not fitted: each run stops when its
residuals fall below `1e-4` relative and is iterated no further, so two
solutions of the same fixed point can differ by about twice that before either
has broken a promise. The measurement sits four decades inside it, and the bound
is left at the derived value because the measurement is what the test reports
and the bound is what it promises.

The long-time form is the second direction: four steps of half a viscous time
each, with the distance to the steady solution asserted to fall at every sample
and the per-step change asserted to shrink. That is the approach to the fixed
point, and the solver's own effort says the same thing — 57, 33, 15 and 4 outer
iterations.

### The solve-port call count, which matches the steady solver's exactly

```text
P19-STEADY-001   244 solve-port calls over 61 outer iterations   = 4.000
P19-TRANSIENT-001 240 solve-port calls over 60 outer iterations  = 4.000
```

Measured here as a by-product of the atomicity test, which needs step 1's call
count to know where to fail. Four calls per outer iteration is SIMPLE's shape —
three momentum components and one pressure correction — so this is a check that
the march drives the coupling the same way the steady loop does, taken from a
counter rather than from reading the code.

### A short march is NOT the steady solution

```text
|U(0.2 s) - U^0|       2.62407e-08
|U(0.2 s) - U_steady|  1.47091e-06
|U^0 - U_steady|       1.49715e-06
```

0.2 s is 0.002 viscous times, so the field has barely moved: it is 1.75% of the
way from plug flow to the developed profile. **With the `U^n` defect it would be
at the steady answer after one step.** This is the direction that matters most,
because it fails loudly rather than subtly.

### The fixed-point test `ADR-078` described is NOT REACHABLE, and what replaced it

Decision 8 proposed starting a march AT the steady solution and asserting it
stays there. That needs a non-uniform initial condition, and
`CfdInitialCondition` is uniform by `ADR-068`'s design: there is no way to put a
developed profile into a document. The ADR has been amended rather than left
saying something the suite does not do. Nothing is lost for defect detection,
because that test could not have seen the `U^n` defect anyway — the two arrays
are equal at a fixed point — and what it would have checked, the term's exact
cancellation, `CfdMomentumTests.cpp` holds one layer down.

## ADVERSARIAL REVIEW

Three passes, on concerns that overlap deliberately: the author's own read of
the final diff, an adversarial review, and a mutation / test-gap analysis. The
two reviews were given the code and the context and NOT the author's
conclusions, which is the only way their agreement means anything.

**All three found the same live defect, and the author found it first.** That is
recorded as a fact about this milestone rather than as a credit: a defect three
independent passes converge on is one the first pass should have caught, and the
first pass did.

### The defect: the per-step mass balance was never assigned

`solveCfdTransient` set seven fields of `CfdTransientStepRecord` from each
iteration and updated a local `balance` that went into the SAMPLE's metadata —
and never assigned `record.balance`. So every published step record carried a
default-constructed `CfdMassBalance{0.0, 0.0}`: two zeros where the qualified
evaluator's two measures belong, beside a `massBalanceDefined == true` telling a
consumer they were measured. **"Per-step mass balance" is one of this
milestone's nineteen checklist items, and it was reporting zeros.**

**The suite passed on it, and that is the part worth keeping.** The only
assertions on the field were

```cpp
REQUIRE(std::isfinite(step.balance.maxCellContinuity));
REQUIRE(std::isfinite(step.balance.globalImbalance));
```

and `std::isfinite(0.0)` is true. The test case containing them is named *"the
per-step mass balance comes from the qualified evaluator"*. A later hardening
pass had added `REQUIRE(cfd::isWellFormed(step.balance))`, which is "finite and
not negative" on both members — also satisfied by the default. Three assertions
in the test named for the claim, and not one of them could fail for it.

The repair is one line, and the test that holds it is a **structural
cross-check with no tolerance**: the record and the sample are filled from the
same iteration, so they must be EQUAL, and any field the march forgets to copy
makes them differ.

```text
without the fix   REQUIRE( metadata.massBalance == report->steps[i].balance )
                  FAILED at sample 0
with the fix      the whole suite passes
```

Verified by reverting the one line, rebuilding and running — the log is
`experiments/per-step-balance-regression.txt`. The open-channel test now also
asserts `step.balance.maxCellContinuity > 0.0`, which a default cannot satisfy.

### The substantive finding: the per-step criterion goes vacuous at small dt

The adversarial review derived, and this suite then measured, that the
acceptance criterion a step is judged by weakens as the step shrinks. The
algebra is short enough to repeat. `advanceCfdCoupling` measures the momentum
residual against the field the iteration started from, with the system as
supplied, normalised by `||A||_inf ||U|| + ||b||`. On a step's first iteration
that field is `U^n`, and `ADR-073`'s two transient halves cancel exactly:

```text
A U^n - b = (A_steady + rho V/dt) U^n - (b_steady + rho V/dt * U^n)
          = A_steady U^n - b_steady
```

The numerator is independent of `dt`; the denominator grows as `1/dt`. So the
reported first-iteration residual is `Theta(dt)`.

**Measured, and not in the direction this author predicted:**

```text
dt       step 1   step 2   step 3
1e-1       60        8        8
1e-3        1       65       62
1e-5        1       65       62
```

The first assertion written for this predicted the LATER steps would collapse,
and it FAILED at `65 <= 8`. It is **step 1** that collapses — from sixty
iterations to one — and the work then moves into steps 2 and 3 rather than
disappearing, because step 1 is normally where the whole pressure field is built
from `p = 0`. The effect saturates between `1e-3` and `1e-5`, so it is a regime
and not a trend.

**The answer is not wrong.** A step accepted on its first iteration is a
lagged-pressure step: consistent, first order, and still published with a
conservative flux, because the correction runs whether or not the loop iterates
again. What stops being worth what a reader expects is the EVIDENCE —
`outerIterations = 1` beside a residual under tolerance says "this step
converged" about a step that coupled nothing.

**Not fixed, and the reason is scope.** The normalisation belongs to
`advanceCfdCoupling`, qualified under `P19-PVCOUPLE-001`; changing it would
change what every steady run is accepted on as well, and a criterion that is
scale-free in `dt` is a numerical decision this checklist does not authorize.
Recorded as `ADR-078` L11 with the measurement, and
`CfdTransient_TheAcceptanceCriterionWeakensAsTheStepShrinks` pins it. Every
validation in this suite runs at `dt >= 0.05 s`, where step 1 still takes tens
of iterations, and the refinement test now asserts
`steps.front().outerIterations > 1` so its measurement is provably about
converged steps.

### Five test cases were added, each closing a surviving mutation

```text
AMarchThatFailsAtALaterStepPublishesNothing
    ADR-078's HEADLINE atomicity promise was tested only at step 1, because
    every failure test in the file failed there. Moving `recordSuccess` inside
    the sampling block -- publishing each sample as produced -- survived the
    whole suite. The new test counts step 1's solve calls with a one-step
    march, then refuses the call immediately after, so step 1 converges and IS
    sampled and step 2's first solve fails: the slot must still hold the
    one-step result byte for byte. Measured 240 solve calls over 60 iterations,
    exactly 4 per iteration, matching P19-STEADY-001's 244 over 61.

GravityReachesEveryStepsMomentumEquation
    `CfdTransient.cpp` carries a comment saying the steady solver's missing
    body force "is not repeated here" -- and nothing held the claim, because no
    transient fixture set a gravity record at all. Dropping
    `momentum.gravity = definition.gravity` survived the entire suite. The new
    test mirrors the steady discriminator and measures dp/dy = -5.77935e-05
    Pa/m, ratio 1.15819 of -rho*g, against the steady suite's -5.78e-05 and
    1.158 -- four significant figures of agreement, and the defect's signature
    is 0.160.

AClosedDomainMarchesWithAnUndefinedMassBalance
    `closedDomain` and `massBalanceDefined` both survived being replaced by a
    literal, because every other march drives the OPEN channel where the
    expected values are false and true. A fixture whose expected values are the
    OTHER ones is the only thing that can falsify a constant.

TheAcceptanceCriterionWeakensAsTheStepShrinks
    the finding above, measured.

CfdTimeGrid_TheWholeNumberToleranceIsRELATIVEToTheStepCount
    nothing exercised the band where the relative and absolute forms of
    kCfdTimeGridTolerance differ -- between 1e-9 and 1e-9 * N -- which is
    exactly the band that opens up at large step counts, and where the absolute
    form would manufacture a final step 20,000 times shorter than the nominal
    one.
```

### Nine smaller findings, acted on

```text
the private-member regex   narrowed from "is private|within this context|has
                           no member|no member named" to the access
                           diagnostics alone. Admitting "no member" re-opens
                           the hole the gate's own comment closes -- rename
                           `steps_` and it would pass for the wrong reason,
                           which is P19-STEADY-001's arity-for-access failure
                           in a different disguise. It still passes.
the t = 0 pressure         only its SIZE was asserted, so publishing the FINAL
                           pressure field there survived. Now asserted zero at
                           t = 0 and non-zero in the published result.
the sampling policy        `report->sampling` was only asserted in the
                           FinalOnly test, so hardcoding FinalOnly survived.
the failure record         only `outerIterations.has_value()` was asserted, so
                           saturating with `max` instead of `min` -- reporting
                           4294967295 for every failure -- survived. The VALUE
                           is now asserted, and so is `source.has_value()`.
the Diverged stage         the test carried a WARN-and-return escape for "no
                           residual grew", which meant the Diverged refusal
                           could be DELETED and the suite would still pass. It
                           is now FORCED by a lying solve port -- ADR-075 L4
                           says the coupling cannot detect one -- and the "step
                           N of M" diagnostic is exercised at step 2 as well.
continuityScale            any positive constant satisfied the old assertions.
                           Now checked against the ANALYTIC throughput
                           rho u A = 1.1976e-5 kg/s; measured 1.1976e-5.
the firstResiduals         two mutations survived: assigning the baseline every
                           inner iteration, and recording the LAST residuals as
                           the baseline. The second disables divergence
                           detection outright. Now pinned by re-asking the
                           qualified verdict with the baseline as both
                           arguments, which must answer Continue for any step
                           that needed more than one iteration.
the guard boundary         `tooMany` is one PAST the limit, so `>=` for `>`
                           survived. The exact limit is now asserted accepted.
the grid's accessors       `integratedSpan()` could have been `return end_;`
                           and `times()` could have been off by one, because
                           only the length was asserted. Both closed, in the
                           one grid where integratedSpan differs from endTime.
```

### Four documentation corrections, which are the same failure as a false checkbox

```text
CfdTransient.hpp   cited `CfdTransient_AShortRunWithASmallStep...` as one of
                   only TWO defences against the milestone's most dangerous
                   defect. The test is `...AShortMarchWithASmallStep...`. A
                   reader auditing that defence was sent to a name that does
                   not exist.
ADR-078            named the new guard `kMaxCfdStoredSamples`; it is
                   `kMaxCfdStoredValues`, and the distinction is load-bearing
                   because the guard is on values.
ADR-078 L1         said a future divergence between the two grid copies would
                   be invisible, without saying they ALREADY diverge at
                   `endTime == 0`. A "sync the two grids" change reading only
                   L1 would re-introduce the path the cfd guard prevents.
the suite's header credited a helper called `transientRegime()` with asserting
                   the transient regime. No such function exists; the claim was
                   true and the name was fiction.
```

`ADR-078` Decision 8's fixed-point test was found unreachable during
implementation, before the reviews, and the ADR was amended then — see
INDEPENDENT VALIDATION.

### What was NOT acted on, and why

```text
MeshRefused is never reached from the transient suite
    the four require* gates are inherited from the steady solve UNCHANGED and
    need a mesh this fixture cannot produce. Recorded as ADR-078 L13 rather
    than papered over, which is the same treatment P19-STEADY-001 gave its
    three unreachable stages.

momentum.currentVelocity fed the wrong array would not be seen
    it is read only for diffusion's symmetry cross terms, and no transient
    fixture has a Symmetry face. A symmetry-face transient fixture is new
    fixture work and the steady suite has the same gap. ADR-078 L13.

transientTermApplied cannot be falsified
    the orchestrator always supplies a step, so the flag is always true and no
    fixture can make it false. What IS falsifiable is asserted: the coefficient
    is strictly positive, component-independent BIT-EXACTLY, and scales as 1/dt
    (ratio 2.000 under halving, and 2.000 again on the shortened final step).
    ADR-078 L13.

the step records are reserved up front
    kMaxCfdStoredValues guards the samples and nothing guards the records, so a
    million-step FinalOnly march reserves them all before the first step. An
    allocation failure rather than a wrong answer, and P18 has the same shape.
    ADR-078 L12.

Upwind -> Central and LeastSquares -> GreenGauss are not discriminated
    both orchestrators pin the same two choices, so the steady-limit
    cross-check cannot tell them apart; changing one WOULD be caught, changing
    both would not. This is ADR-077 L12 read from the test side, recorded as
    ADR-078 L14. The scheme and the method remain outside the fingerprint,
    which is the open scope decision this milestone inherits and does not take.

the 32-bit saturation path
    unreachable: a march would need more than 2^32 outer iterations. The
    boundary value is now asserted for a one-iteration budget, which closes the
    max-for-min mutation; the saturation itself stays documented, not asserted.
```

### What the reviews verified clean

Worth recording, because a review that only produces findings has not been
shown to look at anything else. Both passes read these and reported them
correct: the per-step freeze of `U^n` (a deep copy at the top of each step, with
`iteration->corrected.velocity` a vector rather than a span aliasing the input,
so no self-assignment or dangling); `CfdTimeGrid` line by line against
`thermal::TimeGrid`, with **no finite positive input on which the two disagree**
except the documented `endTime == 0`; the single `refuse` lambda, with
`recordSuccess` called exactly once and reachable only when every step
converged; the provenance read once from the one authority before the march;
every loop in `CellIndex`/`FaceIndex` order with no unordered container, no wall
clock and no thread; the includes a strict subset of `CfdSteady.cpp`'s plus
`CfdSteady.hpp`, with no `thermal`, no `structural`, no OCCT and no Qt; and the
28 compile-fail gates failing for their intended reasons rather than through a
missing default constructor.

One suspected defect was chased and found **not reachable**: `residualsThatGrew`
returning an empty string, which would leave the divergence diagnostic with a
dangling colon. `cfdCouplingVerdict` does report `Diverged` for a non-finite
residual, which no growth comparison reproduces — but `advanceCfdCoupling`'s own
`isWellFormed(residuals)` gate refuses one before returning success, so growth
is the only route to that branch. The same reasoning `CfdSteady.cpp` records for
its copy.

## REGRESSION

Every figure below was counted off the gate's own logs AFTER the run, and every
one was predicted in `FREEZE.md` BEFORE it. Three scripts are kept:

```text
experiments/reconcile.py                82 checks over the harness's own logs.
                                        Every predicted figure is PARSED OUT of
                                        FREEZE.md rather than typed in twice, so
                                        an agreement is with the freeze and not
                                        with this file. Exits non-zero on any
                                        mismatch rather than printing one
experiments/reconcile-falsification.py  five single-line sabotages of a COPY of
                                        the evidence, each one REQUIRED to make
                                        reconcile.py fail
experiments/freshness.py                the build-graph freshness query. Runs no
                                        build
```

### The reconciliation was falsified before it was believed

`P19-STEADY-001` recorded a reconciliation check that reported PASS off one
parsed path instead of eight — a comparison that cannot fail is not a
comparison. So this gate's reconciliation was broken on purpose, one thing at a
time, in a scratch copy of the evidence:

```text
control, the untouched copy                     exit 0, 0 FAIL   reproduces the PASS
one "...   Passed" line removed                 exit 1, 1 FAIL   Passed 6964 != 6965
one identity hex altered, POST-RUN block only   exit 1, 1 FAIL   pre-build != post-run
one assertion figure altered in the readout     exit 1, 4 FAIL   three presets disagree
                                                                 AND the baseline
                                                                 subtraction breaks
the verdict sentence removed from the .out      exit 1, 1 FAIL   no pass is stated
one stage's exit code changed from 0 to 1       exit 1, 1 FAIL   a non-zero stage
```

Five sabotages, five catches, and the untouched control still passes — so the 82
agreements are agreements and not a script that cannot disagree. The third row
is the most informative: one altered figure trips four independent checks,
because the three presets are compared against each other and the subtraction
against the previous milestone's baseline is checked separately.

### The three full ctest logs, counted four ways each

```text
                            debug-ext   release-ext   debug-shared-ext
Start markers                    6965          6965               6965
progress-prefixed lines          6965          6965               6965
result lines                     6965          6965               6965
Passed                           6965          6965               6965
***Failed / Exception / Timeout     0             0                  0
"Exit code 0x"                      0             0                  0
"tests failed out of"          absent        absent             absent
summary line        100% tests passed out of 6965, all three
```

**FOUR COUNTS OF THE SAME NUMBER, NOT ONE NUMBER FOUR TIMES.** Start markers,
progress-prefixed lines, result lines and `Passed` are four different regexes
over the same log, and a log that lost a test would disagree with itself.

`ctest` adds its failed clause only when something failed, so the PRESENCE of
`tests failed out of` is itself a failure marker — `0 tests failed out of 6965`
is a string a passing run cannot print. It is absent from all five logs.

### Per-group counts, identical in all three presets

```text
architecture.                 205    IDENTICAL to baseline
unit.CfdSteady_                19    IDENTICAL to baseline
unit.CfdTimeGrid_              10    new
unit.CfdTransient_             24    new
compile_fail.cfdtransient      28    new
compile_fail. (every group)  1076
```

The two that matter most are not about this milestone's own code. 205 says the
layering and containment tests neither grew nor shrank; 19 says `CfdSteady`
really was untouched, which is `ADR-078` Decision 4's whole claim. And
6903 + 10 + 24 + 28 = 6965 is `FREEZE.md`'s arithmetic reproduced off the logs.

### The two repeat logs

```text
                                  repeat release-ext   repeat debug-ext
Start markers (EXECUTIONS)                     17635              17635
result lines (EXECUTIONS)                      17635              17635
Passed (EXECUTIONS)                            17635              17635
progress-prefixed (SELECTED)                    3527               3527
ratio markers : progress                      5.0000             5.0000
unit.CfdTransient_ executions                    120                120
unit.CfdTimeGrid_ executions                      50                 50
compile_fail. executions                           0                  0
```

24 x 5 = 120 and 10 x 5 = 50, so the new suite is INSIDE the repeat set rather
than merely selected by it. The ratio is exactly 5.0000 in both logs, which is
the check that `--repeat until-fail:5` ran five times and not four.

**`release-ext` SELECTED 3527 HERE AND 3522 IN THE VOIDED SECOND ATTEMPT**, and
the difference is the point. 3527 - 3522 = 5, exactly the number of test cases
the adversarial review and the mutation analysis added after the launcher's
first draft; that attempt's release-ext tree had been built before those five
existed, and with no build stage having run, its repeat stage reported
`100% tests passed` off a superseded binary. The three build trees were emptied
before this run so that a stage with no build behind it fails loudly instead of
passing quietly. `qualification/voided-run-2/README.md` has the measurement.

### The builds, and the only substring allowed to look like a problem

```text
error:    0 occurrences in all three build logs
warning:  0 occurrences in all three build logs, under -Werror
831       build edges in each of the three, from a genuinely empty tree
```

The only `Error` substring in any build log is the filename `Error.cpp`, at edge
4, 5 and 4 of 831 — counted and listed rather than assumed.

### The no-op rebuilds

```text
debug-ext           [1/8]  Checking git revision      one line, one edge
release-ext         [1/8]  Checking git revision      one line, one edge
debug-shared-ext    [1/20] Checking git revision      one line, one edge
```

Exactly one edge ran in each preset and it was the always-dirty git-revision
stamp: no real work was redone, which is what a no-op rebuild has to show. The
bracketed total is ninja's PLANNED edge count and not work done — a distinction
`P19-STEADY-001` had to correct about its own freeze, and these three figures
are the same 8, 8 and 20 it measured.

### Assertion identity, read FROM THE THREE QUALIFIED BINARIES

`ctest` captures no stdout from a passing test, so the ctest logs cannot
establish how many assertions ran. These were read by running each qualified
`bettercad_tests.exe` under a tag filter after the gate; running a binary
rebuilds nothing, and `freshness.py` was re-asked afterwards and still read
zero.

```text
tag                 assertions    cases    debug-ext  release-ext  debug-shared
[cfd]                  755,043      697      same        same         same
[cfd][transient]         1,813       34      same        same         same
[cfd][steady]            1,263       19      same        same         same
[linalg]                 2,560       29      same        same         same
[structural]           271,414      415      same        same         same
[thermal]              595,622    1,168      same        same         same
```

Eighteen readings; all six figures identical across all three presets, and all
six equal to what `FREEZE.md` committed to before the run.

```text
755,043 - 1,813 = 753,230        and        697 - 34 = 663
```

**THAT IS THE MEASUREMENT A PASSING SUITE CANNOT MAKE.** 753,230 assertions in
663 cases is `P19-STEADY-001`'s `[cfd]` baseline exactly, so the new suite
accounts for the whole of the difference and nothing in the existing cfd suite
moved. `[linalg]`, `[structural]` and `[thermal]` identical to their baselines
is the check that a cfd-only change really was cfd-only: a pass/fail count
cannot see an assertion that stopped running, and these can.

## QUALIFICATION

### The verdict, which is the harness's own two statements

```text
run-qualification.out      Qualification passed: every stage exited 0.
qualification-times.txt    0 stage(s) failed
stages with an exit code   17
stages that exited 0       17
```

**NOT THE LAUNCHER'S EXIT CODE.** It read 0 for `P19-MOM-001`'s failed attempt,
0 for `P19-STEADY-001`'s failed attempt, and 0 for both of this milestone's
failed attempts as well as for the one that passed. On its own it distinguishes
nothing, and five runs across three milestones have now turned on that.

```text
started    Tue 2026-11-10 08:39:50
finished   Tue 2026-11-10 13:28:48      4 h 49 min
17 stages  3 presets x (configure, clean, build, no-op rebuild, ctest)
           + 2 repeat sets at --repeat until-fail:5
```

```text
                  configure    build     ctest     ctest's own test time
debug-ext                7s   32m25s    71m07s     4266.67 s
release-ext              4s   38m02s    58m11s     3490.77 s
debug-shared-ext         5s   16m04s    38m17s     2297.29 s
repeat release-ext            --        16m14s      973.64 s
repeat debug-ext              --        18m16s     1095.79 s
```

### Three attempts, and the first two were the environment

```text
1  06:15 -> 07:18   FAILED, 5 stages   the environment stopped being able to
                                       launch processes 63 minutes in: 374 of
                                       375 ctest failures are 0xC0000142
                                       (STATUS_DLL_INIT_FAILED) and both
                                       remaining configures returned it within
                                       30 ms of being asked
2  07:25 -> 08:28   FAILED, 3 stages   launched from a shell that cannot see
                                       the dependency prefix, so CMake never
                                       put it on CMAKE_PREFIX_PATH and
                                       find_package(OpenCASCADE) fell through
                                       to msys64's 7.9.3, which the project's
                                       own supported-range gate refused
3  08:39 -> 13:28   PASSED, 0 stages   this one
```

**NO PRODUCT CODE CHANGED BETWEEN THE THREE**, and the frozen surface is
bit-identical in all three: the harness read the same eight identities before
its first build on each attempt, and they are `FREEZE.md`'s eight. The second
attempt's cause was established by running one probe script in both shells and
comparing — `cmake EXISTS` reads YES in one and NO in the other for the same
absolute path, with `LOCALAPPDATA` identical — rather than by inferring it from
the error message. Each attempt's logs and full diagnosis are kept:
`qualification/voided-run-1/README.md` and
`qualification/voided-run-2/README.md`.

Attempt 1 is the one still carrying a hypothesis rather than a measurement. A
concurrent `[structural]`/`[thermal]` test run was in flight alongside its
`ctest -j4` and the compile-fail groups, each case its own compiler process, and
`0xC0000142` is a process that could not start. Attempt 3 ran with nothing else
running and did not reproduce it, which is consistent with that hypothesis and
is not a proof of it.

### The configuration that was qualified

```text
CMake               4.4.1 (Ninja)
C++ compiler        GNU 16.1.0, the WinLibs POSIX UCRT package
C++ standard        C++23
Warnings as errors  ON
Qt                  6.11.2       from the deps prefix
Open CASCADE        8.0.1        from the deps prefix
Netgen              6.2.2604
Catch2              3.16.0, fetched
```

```text
all three caches   0 msys64 references
all three caches   16 references to bettercad-deps/gnu-16-mingw-amd64
distinct           Debug/OFF, Release/OFF, Debug/ON -- three configurations,
                   not one repeated three times
```

**THE ZERO IS THE REFUTATION OF THE SECOND ATTEMPT'S FAILURE**, stated as a
count rather than as a reassurance. That attempt configured against
`C:/msys64/ucrt64`'s Open CASCADE 7.9.3; these three caches contain no msys64
reference at all.

### The harness

Re-hashed after the run, by hash and not by assertion:

```text
launch.cmd             cf6c1f112d71586098a78652091d0375c8d04051   sha1sum
preflight-space.cmd    59910328ab43e07b9e0c474f969994ad483d3f68   sha1sum
preflight-space.ps1    b76d9325e8131c265443540a9ac914e236731a66   sha1sum
qualify.cmd            244b38de8feac0475af7cdc56ba9415d27d8dd29   sha1sum
verify-harness.cmd     afd0197d4f4726db7f533c2f27f4f6e069ba83c6   sha1sum
run-qualification.cmd  278d9f997b0f26e427e72ba4048fd77f60cd82ea   git hash-object
```

The five `sha1sum`s match `FREEZE.md`'s five exactly, so all three attempts ran
the same instrument and the fix was to the environment and not to the
instrument. The two hashing conventions are labelled wherever they appear and
never agree for the same file.

### Freshness, asked of the build graph

```text
                       graph outputs   existing on disk   NEWER than build exit
debug-ext                       6079               1151                       0
release-ext                     6079               1151                       0
debug-shared-ext                6092               1164                       0
```

Asked of `ninja -t targets all`, a GRAPH QUERY that runs no edge. The reference
time is each build log's own mtime. Most graph outputs do not exist because the
compile-fail case targets are `EXCLUDE_FROM_ALL` and their objects are never
produced — those compiles fail by design.

```text
debug-ext         test binary mtime vs its build exit     -5.44 s
release-ext                                              -35.20 s
debug-shared-ext                                         -12.28 s
```

**NO BUILD WAS RUN DURING THIS RECONCILIATION.** A relink would move the
binaries' mtimes and destroy the evidence being checked. Each binary's own mtime
sits a few seconds BEFORE its build exit — the link, then the build's wrap-up —
which is what zero-newer looks like from the other direction. The query was run
twice, before and after the per-tag readout executed those binaries, and read
zero both times.

### The qualified tree is the committed tree

The eight frozen paths were read, and every reading agrees:

```text
1  by the harness, before its first build          08:39:50, this run
2  by the harness, after its last test run         13:28:47, this run
3  recomputed from the WORKING TREE during this reconciliation, by the
   harness's own method -- scratch GIT_INDEX_FILE, read-tree HEAD, add -A,
   write-tree --prefix -- so the real index is never touched
4  off the STAGED tree, from the real index, before the commit was made
5  out of the commit object itself, which can only be read AFTER the commit
   exists, and is therefore recorded by the follow-up commit, exactly as
   P19-STEADY-001's fifth reading was
```

```text
apps               fc398a3d2e524178cf38e8b667236379d8381086
include            4759b76839047d95cf096ae80bf071c52c7e3edb
src                c33a7b6fdbe7cb1b4deb66727152525e85dd4a48
tests              47658ea579a4cf9e46d2f77410b5b1c12b71ccef
examples           4da234ebf4779f7c16d5df0075f106f7e3d4c958
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e
```

**`git rev-parse HEAD:<path>` IS USED ONLY AT STEP 5.** Before the commit it
reads the PREVIOUS commit's tree and would agree with itself for the wrong
reason, which is why steps 3 and 4 use an index instead. `FREEZE.md` records
that distinction and this section keeps it.

**AND THE EQUALITY CHECK IS ITSELF CHECKED.** `reconcile.py` asserts that both
of the harness's readings hold EIGHT paths before it reports them equal, and
compares them against `FREEZE.md`'s table as a third party — two one-element
dicts compare equal just as well as two eight-element ones, which is the defect
`P19-STEADY-001`'s first reconciliation pass had. The falsification run then
altered one hex in the post-run block and required the comparison to fail; it
did.

Three readings of the eight were also taken across the three qualification
ATTEMPTS, which is a stronger statement than it looks: the harness read them
before its first build on each of 2026-11-10 06:15, 07:25 and 08:39, and all
three readings are these eight. Nothing about the frozen surface changed while
the environment was being fixed.

## KNOWN LIMITATIONS

Fourteen, and all fourteen are in `ADR-078`'s own list.

**THIS SECTION UNDERCOUNTED ITS OWN LIMITATIONS AND IS CORRECTED HERE.** It
first read "Carried from `ADR-078`, L1 to L9, plus one found during
implementation" and named only `L10`, which silently dropped four — `L11`,
`L12`, `L13` and `L14` — three of which came out of the adversarial review and
the mutation analysis described above, and one of which (`L11`) this file
devotes a whole subsection to. A documentation claim that disagrees with the
evidence is the same failure as a false checkbox, so it is corrected rather than
left standing. Found while filling the gate's own result; recorded as `r2`.

```text
L1  THE GRID ALGORITHM EXISTS TWICE IN THE TREE, in cfd and in thermal, and no
    build check can see a divergence between them. The two copies ALREADY
    diverge at one input: thermal::TimeGrid accepts endTime == 0 and
    CfdTimeGrid refuses it, because CfdResult::create needs a time above zero
    and a step of at least one.
L2  THE TWO ORCHESTRATION BODIES ARE CROSS-CHECKED AT ONE OPERATING POINT. The
    large-dt steady limit is a measurement and not a proof of equivalence: a
    difference that vanishes as rho V / dt -> 0 would not be seen by it.
L3  BACKWARD EULER IS FIRST-ORDER AND NOTHING HERE MEASURES THE CONSTANT. The
    ORDER is asserted by halving dt; the absolute temporal error against an
    exact solution is not, because the fixture has no closed-form transient.
L4  NO ADAPTIVE STEPPING, NO RETRY, NO STEP REJECTION. dt comes from the
    definition and never changes; a step that fails fails the run.
L5  NO RESTART AND NO PERSISTENCE. Every run starts from the canonical initial
    condition, and a history is not persisted at all.
L6  THE SAMPLES ARE THE INTEGRATOR'S OWN TIMES. No requested output times in
    the canonical data, so no off-grid request and no interpolation.
L7  THE CONVECTION SCHEME AND THE GRADIENT METHOD ARE STILL HARD-CODED, outside
    the definition and outside its fingerprint, inheriting ADR-077 L12
    unchanged. REPORTED, NOT TAKEN -- it is a scope decision and it is in the
    RESULT section as one.
L8  A SUPPLIED SOLVE THAT IS NON-DETERMINISTIC MAKES THE RUN
    NON-DETERMINISTIC, and nothing here can detect that. A transient run
    multiplies the hole's reach by the step count.
L9  NO PERFORMANCE BASELINE, eighth milestone running -- and a transient run is
    steps x iterations linear solves, which makes it the most expensive thing
    in the tree.
L10 A FINAL STEP CAN BE ARBITRARILY SHORT. When endTime / dt misses a whole
    number by slightly more than kCfdTimeGridTolerance, the last step can be a
    tiny fraction of dt, which makes that step's rho V / dt enormous. It stays
    finite and the step converges trivially, so this is waste rather than a
    wrong answer, and it is P18's behaviour unchanged. Fixing it would change
    the grid contract, and "the last step is the remainder, however small" is
    what makes the march land on endTime exactly. FOUND DURING IMPLEMENTATION.
L11 THE PER-STEP CONVERGENCE CRITERION BECOMES VACUOUS AT A SMALL ENOUGH dt,
    AND THE PUBLISHED EVIDENCE THEN SAYS MORE THAN IT MEANS. Derived by an
    adversarial review and then measured: on a step's first iteration the two
    transient halves cancel exactly, so the residual numerator is independent
    of dt while the normalisation grows as 1/dt. Outer iterations per step
    measured 60/8/8 at dt = 1e-1 and 1/65/62 at dt = 1e-3 and at 1e-5 -- step 1
    collapses to ONE and the work moves to the later steps. The answer is not
    wrong: such a step is a lagged-pressure step, consistent, first order and
    published with a conservative flux. What stops being worth what a reader
    expects is the EVIDENCE. NOT FIXED HERE -- the normalisation belongs to
    advanceCfdCoupling, qualified under P19-PVCOUPLE-001, and changing it would
    change what every steady run is accepted on.
L12 THE STEP RECORDS ARE ALLOCATED UP FRONT, proportional to the step count.
    kMaxCfdStoredValues guards the SAMPLES and nothing guards the records, so a
    valid definition with dt = 1e-6 and endTime = 1 reserves a million records
    before the first step is taken. Found by an adversarial review.
L13 FOUR THINGS THE SUITE CANNOT FALSIFY, named so that nobody reads them as
    tested: transientTermApplied from the orchestrator, which always supplies a
    step; CfdFailureStage::MeshRefused; momentum.currentVelocity, read only for
    diffusion's symmetry cross terms with no Symmetry face in any fixture; and
    the 32-bit saturation of CfdSolveFailure::outerIterations.
L14 THE HARD-CODED SCHEME AND GRADIENT METHOD ARE NOT DISCRIMINATED BY THE
    STEADY-LIMIT CROSS-CHECK, because both orchestrators pin the same two
    choices. Changing one would be caught; changing both would not. L7's cost
    read from the test side.
```

## RESULT

```text
TASK:            P19-TRANSIENT-001 -- Transient Incompressible Solver
IMPLEMENTATION:  four files added, three CMakeLists registrations. No existing
                 declaration changed; CfdSteady.hpp and CfdSteady.cpp are
                 untouched. ADR-078.
TESTS:           34 cases / 1813 assertions new, plus 28 compile-fail gates and
                 one control target. 6965 of 6965 in each of three presets;
                 3527 x 5 = 17635 executions in each of two presets.
VALIDATION:      five independent references. The analytic boundary throughput
                 rho u A = 1.1976e-5 kg/s, matched to five figures. The steady
                 suite's own gravity measurement reproduced through the
                 transient path: -5.77935e-05 Pa/m, ratio 1.15819, against
                 -5.78e-05 and 1.158 measured a day earlier. The qualified
                 steady solver reproduced by one large step to 1.50592e-07
                 relative on the velocity in the same 59 outer iterations.
                 First-step refinement ratios 1.9834 and 1.9916 against a
                 first-order prediction of 2 -- the test that sees ADR-073
                 Decision 4's dangerous defect, which would make them 1. And
                 240 solve-port calls over 60 outer iterations = exactly 4, the
                 same ratio as the steady solver's 244/61.
REGRESSION:      82 reconciliation checks over the harness's own logs, all
                 agreeing with FREEZE.md's pre-run figures, and the
                 reconciliation itself falsified by five sabotages that it
                 caught.
RESULT:          PASS
EVIDENCE:        docs/verification/P19-TRANSIENT-001/, ADR-078
TODO:            nineteen of nineteen ticked, on PASS
```

One live defect was found by review and fixed before the gate: the per-step mass
balance was never assigned, and the suite passed on it because the only
assertion was `isfinite` and zero is finite. The per-step acceptance criterion's
behaviour at small `dt` is recorded as a measured finding and belongs to
`P19-PVCOUPLE-001`, not here.

**TWO SCOPE DECISIONS ARE OPEN AND REPORTED RATHER THAN TAKEN**, and neither is
Claude's to make: `ADR-078` `L7` / `ADR-077` `L12`, the convection scheme and
gradient method hard-coded outside the analysis definition and outside its
fingerprint; and the inherited literal `0x08` byte in `verify-harness.cmd`,
present in 72 of the 96 committed copies across the lineage and never evaluated
by any gate.

Finishing a milestone is itself a stop condition. Nothing after this is
authorized.

## REVISION

```text
r1  first draft of this directory, written before the qualification gate ran
r2  the gate's result, on the THIRD attempt. REGRESSION and QUALIFICATION
    filled from the harness's own logs; the header's RESULT and the RESULT
    section filled; two voided attempts recorded with their diagnoses in
    qualification/voided-run-1/ and qualification/voided-run-2/. The
    reconciliation was falsified before it was believed, which is r2's own
    answer to the check P19-STEADY-001 found could not fail.
    AND ONE DEFECT IN THIS FILE WAS FOUND WHILE FILLING IT: KNOWN LIMITATIONS
    claimed "L1 to L9, plus one found during implementation" while ADR-078
    carries FOURTEEN, so four were silently dropped -- including L11, the
    vacuous-criterion finding this file gives a subsection of its own. All
    fourteen are now named. Documentation that undercounts the evidence is the
    same failure as a false checkbox, which is why it is corrected in place and
    the correction is stated rather than quietly made.
```
