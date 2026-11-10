# ADR-078 — The transient solver marches a local time grid, and the slot holds the final sample

```text
STATUS:    Accepted
DATE:      2026-11-10
MILESTONE: P19-TRANSIENT-001
TOUCHES:   ADR-049 and P18-TRANSIENT-001 (the qualified backward-Euler solver
           this one is modelled on and may not include), ADR-058 (the transient
           settings, the sampling policy and the residual record this binds
           rather than redefines), ADR-059 (the layering that puts cfd below
           thermal and so out of reach of P18's TimeGrid), ADR-061 (the result
           carries a time and a step for exactly this milestone), ADR-073
           Decision 4 (the transient momentum term, and the one array whose
           misuse converges to the wrong answer quietly), ADR-075 (the coupling
           driven once per outer iteration), ADR-077 (the steady orchestrator
           this parallels and deliberately does not refactor)
```

## Context

Nineteen checklist items, and — as with `P19-STEADY-001` — the UNDERSTAND phase
found most of the numerics already qualified. What is different this time is
that the *data model* was built for this milestone four milestones ago, and
reading it is most of the design:

```text
ALREADY DECIDED, NOT OURS TO REOPEN
CfdTransientSettings   timeStep, endTime, maxIterationsPerStep, sampling
                       P19-DATA-001, and `validate` already refuses a
                       non-finite or non-positive step, a non-finite or
                       non-positive end time, an end time shorter than one
                       step, a zero iteration budget and an unknown policy
validate(definition)   a transient analysis must carry transient settings and
                       must NOT carry steady ones; the reverse for steady
canonicalText          writes transient.time_step, transient.end_time,
                       transient.max_iterations_per_step and
                       transient.sampling into the definition fingerprint
CfdResultMetadata      `time` and `step`, documented as "NONE for steady; the
                       sample time ... for a transient sample (t = 0 is the
                       initial condition, not a solve)"
CfdResult::create      "transient: a finite time > 0 s and a step >= 1"
CfdMomentumInput       `timeStep` (PRESENT MEANS TRANSIENT) and
                       `previousVelocity` (U^n), backward Euler, no selector
CfdResultSlot          at most ONE result and at most one failure
```

So the momentum equation's transient term is **not this milestone's to write**.
`ADR-073` put `rho V / dt` on the diagonal and `rho V / dt * U^n` in the source,
with its unit assertions and a dedicated test for the one way a caller can get
it wrong. Four questions are genuinely open, and they are the decisions below:
where the time grid lives, what gets published, how the step loop relates to
the steady loop, and what "time-dependent" means in a data model that has no
time functions in it.

## Decision 1 — The time grid is a cfd-local value type, because the layering forbids reusing P18's

`P18-TRANSIENT-001` already holds a qualified `TimeGrid` with exactly the
semantics this milestone needs: `t_k = k * dt` by multiplication rather than by
a running sum, a shortened final step so the run lands on `t_end` exactly, O(1)
memory for a million steps, and possession as the evidence that the times are
finite and strictly increasing. `CfdTransientSettings`'s own documentation
points at it by name — "a fixed step from t = 0 to `endTime`, the last step
shortened so the run lands on `endTime` exactly (ADR-058, as P18's
`TimeGrid`)".

It is **unreachable**. `ADR-059` put `cfd` at layer 42 and `thermal` at 45, so
that a future one-way coupling can run out of CFD into thermal, and
`tests/architecture/CheckLayering.cmake` enforces the mirror image: `cfd` may
not include `thermal`. CLAUDE.md is explicit that the layering test "enforces
containment and layering, and fails the build. Do not work around it."

### The candidates

```text
A  a cfd-local CfdTimeGrid with the same semantics
B  move P18's TimeGrid down to core and have both modules use it
C  no type at all: compute the step sequence inline in the orchestrator
```

### The comparison

```text
                       A                   B                   C
correctness            same algorithm,     one algorithm,      same algorithm,
                       independently       one qualification   untestable in
                       tested                                  isolation
architecture fit       clean: cfd owns     clean in the end,   poor: six of
                       its own             but core gains a    nineteen items
                       discretisation      solver concept      are about the grid
scope                  inside this         a cross-module      inside this
                       milestone           refactor of a       milestone
                                           qualified P18 type
P18 re-qualification   none                REQUIRED: thermal's every step and
                                           its evidence would have to be re-run
                                           to stand behind the move
duplication            the ALGORITHM is    none                none
                       stated twice
testability            a value type with   as A                the partial final
                       its own suite                           step is reachable
                                                               only by solving
```

### The decision, and the argument that settles it

**Option A.** Two things settle it together. First, CLAUDE.md: "A milestone is
not permission to refactor the repository", and Option B is a refactor of a
type that `P18-TRANSIENT-001` qualified — moving it would put thermal's
qualification behind a tree that was never qualified, and the honest response
would be to re-run P18's gates, which this checklist does not authorize.
Second, Option C fails on testability in a way that matters: "Actual final
partial step", "Deterministic timestepping", "Define `dt`", "Define end time",
"Define output times" and "Reject time ordering errors" are six of the nineteen
items, and inline arithmetic makes every one of them reachable only through a
full solve on a meshed document. A value type makes them assertions about a
function.

**What Option A costs, stated rather than discovered later.** The grid
*algorithm* now exists twice in the tree, in two modules that cannot share it.
If the rule for the final partial step is ever changed, it must be changed in
both, and nothing in the build will say so. The mitigation is not a test — no
test in `cfd` can see `thermal`'s grid — it is this paragraph, the citation in
`CfdTransient.hpp`, and the fact that both files name the other. That is
weaker than a compiler check and is recorded as such in Known limitations
(`L1`).

**What is NOT duplicated:** no numerics. The grid computes times; the momentum
term, the coupling and the kernels are all called, never re-derived.

### Where the policy gate sits, and where it does not

`CfdTimeGrid::create` is **total over finite positive inputs**: `dt > endTime`
is one shortened step of `endTime`, exactly as P18's is. It does *not* refuse
that case, because "you must be able to take at least one full step" is an
analysis policy and `validate(CfdTransientSettings)` already holds it,
qualified under `P19-DATA-001`:

> the end time {} s is shorter than the time step {} s, so not one step could be
> taken

A grid that also refused it would be a second spelling of one rule, and the two
could drift. The suite tests both halves where they live: the grid's behaviour
directly, and the orchestrator's refusal through the definition.

### The one guard the grid adds that the settings do not

`validate(CfdTransientSettings)` accepts `timeStep = 1e-300` with
`endTime = 1e300`. That is a valid pair of finite positive times whose step
count is not representable, and nothing upstream refuses it. The grid refuses
it on the COUNT — `!(ratio <= kMaxCfdTimeSteps)`, which also catches an
overflowed quotient — and **never enlarges the step to fit**, which would be
solving a different problem from the one asked for. A second guard bounds the
stored samples, for the same reason and with the same rule: refused, never
silently reduced to fewer samples.

## Decision 2 — The slot holds the final sample; the history is the report's

`CfdResultSlot` holds at most one `CfdResult`, and a `CfdResult` is "the
accepted solution of one CFD analysis on one mesh, **at one time level**". A
transient run with `CfdTimeSampling::EveryStep` produces many.

### The candidates

```text
A  the slot gets the FINAL sample; the report carries every requested sample
   as a full CfdResult
B  a new CfdTransientSolution container is published into the slot, which
   means changing CfdResultSlot to hold either kind
C  the slot gets the final sample; the report carries samples as raw arrays
```

### The comparison

```text
                      A                   B                   C
qualified types       CfdResult and its   CfdResultSlot and   CfdResultFields
changed               slot UNCHANGED      analysisState both  unused for the
                                                              samples
every sample is       yes, by CfdResult   yes                 NO: raw arrays
validated             ::create                                carry no proof
every sample has      yes                 yes                 no
provenance
resultCurrency /      unchanged, and      must learn a        unchanged
analysisState         answer about the    second shape
                      final sample
memory                the final sample    one copy            the final sample
                      is held twice                           is held twice
P19-VIZ / P19-POST    get validated       as A                get arrays they
later                 results                                 must re-validate
```

### The decision

**Option A.** It changes no qualified type, and the reason is not convenience:
`CfdResultSlot`, `resultCurrency` and `analysisState` were qualified under
`P19-DATA-001` with semantics that are about *the analysis's current answer*,
and the current answer of a transient run is its final state. Option B would
make `analysisState` answer a question it was not qualified for. Option C
throws away the one thing `CfdResultFields` exists to prove — that three
lengths were checked against a layout and every value in it is finite — for
every sample but the last.

The cost of A is that the final sample exists twice: once in the slot and once
as `samples.back()`. That is one field set, and it buys an invariant the suite
asserts directly: **the slot's result equals the report's last sample**, by
value. A duplication that is checkable is better than a reference that can go
stale.

## Decision 3 — `EveryStep` samples `t_1 … t_N`, and P19 differs from P18 here deliberately

P18's `EveryStep` history stores `t_0 … t_N`: `N + 1` states, the first being
the resolved initial condition. P19's **cannot**, and the reason is a gate that
was written four milestones ago:

```text
CfdResult::create    transient: a finite time > 0 s and a step >= 1
CfdResultMetadata    t = 0 is the initial condition, not a solve
```

A sample at `t = 0` has no step index and no residuals, because no system was
solved to produce it — it is the initial condition, which the document already
owns as canonical state (`CfdInitialCondition`). Publishing it as a *result*
would be a solved field that was never solved for.

So `EveryStep` means **every step**, which is what `CfdData.hpp` already says
("The field at every step, including the end time"), and the count is `N`, not
`N + 1`. `FinalOnly` is `{t_N}`, one sample. The initial field is recoverable
from the definition, and the report records it as `initialPressure` /
`initialVelocity` so that a reader comparing step 1 against `t = 0` does not
have to re-expand it — as a plain array, not as a result, because that is what
it is.

## Decision 4 — A separate orchestrator, and `solveCfdSteady` is not refactored

The body of a transient step is nearly the body of a steady iteration: mass
flux from the current velocity, pressure gradient, three momentum assemblies,
`advanceCfdCoupling`, the verdict. The temptation is to extract the shared body
and have both call it.

### The candidates

```text
A  a separate solveCfdTransient, calling the same qualified operators
B  extract the per-iteration body into a shared internal helper, change
   solveCfdSteady to call it
C  make solveCfdSteady a one-step transient with the term switched off
```

### The decision

**Option A**, and three things point the same way.

First, **P18 did exactly this**, and it is qualified: `solveThermalAlgebra` and
`solveThermalTransientAlgebra` are separate entry points in separate headers,
and neither is expressed in terms of the other.

Second, Option C is wrong on the physics, not merely inelegant. A steady
analysis is not a transient one with one step: the steady momentum form has no
`rho V / dt` on the diagonal at all, and `CfdMomentumInput` enforces the
distinction (`timeStep` present requires `previousVelocity`; absent requires it
empty, and a mismatch is `TransientInputsAreInconsistent`). Expressing steady
as transient would mean choosing a `dt` that stands for infinity, which is a
number this project would then have to defend.

Third, Option B is a refactor of a file qualified under `P19-STEADY-001` and
committed one day ago, which CLAUDE.md addresses twice: "Do not combine a large
refactor with unrelated feature work" and "no speculative abstraction". The
shared helper would need parameters that only the transient caller uses.

**What Option A costs, stated rather than discovered later.** The *order* of
five operator calls now appears in two files. `CfdMomentum.hpp` warns about
exactly this shape — "`P19-CONV-001`'s review found one three-line
interpolation duplicated across three milestones with the same defect left in
it three times" — and the warning is taken seriously here, with one difference
that matters: what is duplicated is a sequence of calls into qualified
operators, not a copy of a discretisation. No arithmetic is repeated.

The mitigation is a measurement rather than a promise. **A single transient step
with `dt` far larger than the convective and diffusive timescales drives
`rho V / dt` toward zero relative to the rest of the matrix, so its solution
must be the steady solution**, and the suite asserts that against
`solveCfdSteady` on the same document. If the two orchestration bodies ever
diverge in a way that matters, that assertion is what sees it. It is recorded
in Known limitations (`L2`) that this is a cross-check at one operating point
and not a proof of textual equivalence.

## Decision 5 — The divergence baseline resets at every step

`cfdCouplingVerdict(current, first, settings)` takes `first` as "the residual
record of the FIRST iteration of the run, which is what `divergenceFactor` is
relative to". In a transient run, **"the run" is the step**.

The alternative — one baseline for the whole march — is wrong in both
directions. A flow starting from rest has genuinely larger residuals at step 50
than at step 1 if the field is changing more there, so a run-wide baseline
could declare divergence for a healthy solve; and a step that genuinely blows
up late in a run would be measured against a baseline from a different state,
where the factor means nothing. Each step is a fresh nonlinear problem posed
from a new `U^n`, and the scale its convergence is relative to is its own first
iteration.

This is stated here because it is invisible in the code — it is one `optional`
being reset inside the step loop rather than outside it — and because
`CfdTransientStepRecord` carries both `firstResiduals` and `residuals` per step
precisely so that the choice is readable from the report and assertable from a
test.

## Decision 6 — The per-step budget reuses the qualified predicate

`cfdCouplingBudgetExhausted(completed, const CfdSteadySettings&)` is
`P19-PVCOUPLE-001`'s, and its parameter is a `CfdSteadySettings`. A transient
run's budget is `CfdTransientSettings::maxIterationsPerStep`.

The orchestrator builds
`CfdSteadySettings{.maxIterations = maxIterationsPerStep}` locally and calls the
qualified predicate, rather than writing `completed >= maxIterationsPerStep`
itself. The type name reads oddly for one line; what it buys is that
"exhausted" has **one** definition in the tree. The alternative is a second
comparison that is trivially right today and is a second place to change if the
predicate's boundary is ever revisited (it is `completed >= maxIterations`, so a
budget of 1 permits exactly one iteration — a boundary worth having in one place
only).

A step that exhausts its budget **fails the run**: no retry, no smaller step, no
partial history. `CfdTransientSettings` already says so ("a step that does not
converge within them fails the analysis") and P18 says it too ("A step that
fails fails the run: there is no retry, no smaller step and no partial
result"). The failure is `CfdFailureStage::IterationLimitReached` and the
diagnostic names the step index and the time.

## Decision 7 — Nothing is time-dependent, so the boundary and the body force are prepared once

Two checklist items are policies rather than features: "Time-dependent BC
policy" and "Time-dependent load policy". The policy is that **there is no time
dependence anywhere in the canonical data**, so there is nothing to freeze:

```text
CfdBoundaryCondition   a velocity, a pressure, a wall -- numbers, not functions
CfdGravity             an acceleration vector and a reference height
CfdInitialCondition    a uniform state at t = 0
CfdTransientSettings   two times, a count and a policy enum
```

`prepareCfdBoundary` takes the definition and the geometry and no time, so
preparing it once before the march and using it for every step is not an
optimisation — it is the only thing the data can mean. The same holds for
`definition.gravity`, which reaches every step's momentum assembly unchanged.

A policy stated in prose is prose, so it is enforced the way
`P18-TRANSIENT-001` enforced its own ("the compile-fail group proves no member
carries a time function"): `CfdTransientMisuse.cpp` gates that no boundary
condition, no gravity record, no initial condition and no transient settings
record accepts a callable or a per-time container, and that the solver takes no
time-varying parameter. Those gates fail to compile if a later milestone adds a
time profile without revisiting this decision, which is the outcome this policy
wants.

**What this decision does NOT claim.** It does not claim that time-dependent
boundary conditions are undesirable or out of scope forever; `ADR-058` deferred
them from P19 v1, and when they arrive they will arrive as canonical data, a
fingerprint change and a per-step preparation. The claim is narrower and
checkable: *in this tree, today, nothing a definition can hold varies with
time.*

## Decision 8 — Only the velocity has a previous time level

`U^n` is a real field with a real time derivative. The pressure in an
incompressible flow is **not**: it carries no time derivative in the momentum
equation and acts as the constraint multiplier that makes the velocity
divergence-free. So the march keeps:

```text
previousVelocity   U^n, FROZEN for the whole of step n+1's inner loop
velocity           the current outer iterate, updated every iteration
pressure           carried across steps as the next step's starting GUESS,
                   with no "previous pressure" role and no term in any equation
massFlux           derived from the current velocity every iteration, as in
                   the steady solve -- never carried as state
```

The distinction between the first two is the one defect `ADR-073` Decision 4
warns about at length, and it is worth repeating here because this milestone is
the first caller that can make it: passing the CURRENT velocity as
`previousVelocity` makes the transient term's two halves cancel, the system
becomes the steady one perturbed toward its own current state, **and it
converges to the correct steady answer with every steady assertion passing**.

Two tests can see it, and both are checklist items:

```text
Time-step refinement     ||U^1 - U^0|| must scale with dt. With the defect it
                         is ||U_steady - U^0|| at every dt.
Steady-limit comparison  a SHORT run with a small dt must NOT match the steady
                         solution. With the defect it matches at once.
```

A fixed-point test — start from the steady solution, take a step, stay there —
**cannot** see it, because the two arrays are equal at a fixed point. That is
recorded so that nobody later reads a fixed-point test as covering this.

### And that fixed-point test turned out to be UNREACHABLE, which this decision got wrong

The paragraph above originally went on to describe the fixed-point test as one
of the suite's checks. It is not, and cannot be: starting a march AT the steady
solution needs a NON-UNIFORM initial condition, and `CfdInitialCondition` holds
one uniform velocity and one uniform pressure by `ADR-068`'s design. There is no
way to put a developed profile into a document, so there is no way to pose that
test at this layer at all.

What replaces it is the approach to the fixed point: a march of four steps of
half a viscous time each, with the distance to `solveCfdSteady`'s answer
asserted to fall at every sample and the per-step change asserted to shrink.
Measured iteration counts 57, 33, 15, 4 — which is the same statement read off
the solver's own effort.

Nothing is lost for defect detection, since the fixed-point test could not see
the `U^n` defect either way. What is lost is a check on the transient term's
exact cancellation at a fixed point, and `CfdMomentumTests.cpp` holds that one
layer down, where the input can be constructed directly.

## Consequences

```text
new public header    include/bettercad/cfd/CfdTransient.hpp
new source           src/cfd/CfdTransient.cpp
new types            CfdTimeGrid, CfdTransientStepRecord, CfdTransientReport
new entry point      solveCfdTransient(document, regenerator, mesher,
                                       analysis, solve, slot)
new guards           kMaxCfdTimeSteps, kMaxCfdStoredValues,
                     kCfdTimeGridTolerance
                     (this block first named the second one
                     kMaxCfdStoredSamples, and an adversarial review
                     caught it. The distinction is load-bearing: the
                     guard is on field VALUES, not on samples, which is
                     the whole point of the argument above.)
unchanged            CfdResult, CfdResultFields, CfdResultSlot,
                     CfdResultMetadata, CfdFailureStage (nine stages, no
                     tenth), CfdSolveFailure, CfdTransientSettings,
                     validate(), canonicalText, CfdSteady.hpp and
                     CfdSteady.cpp -- not one line
reused as-is         stageOf (P19-STEADY-001's mapping, sixteen onto nine),
                     cfdCouplingVerdict, cfdCouplingBudgetExhausted,
                     advanceCfdCoupling, cfdMassFlux, assembleCfdMomentum,
                     reconstructCfdPressureGradient, prepareCfdBoundary,
                     prepareCfdInitialField, expandCfdInitialField,
                     currentResultSource, requireCfdModel / Mesh / Topology /
                     Geometry / FieldMap
```

`stageOf` is reused rather than re-declared, which means `CfdTransient.cpp`
includes `CfdSteady.hpp` for one function. That is a same-module include and
the layering test is indifferent to it; the alternative — a second sixteen-case
table — is the duplication this project rejects everywhere else. It also means
a seventeenth `CfdCouplingProblem` still breaks exactly one translation unit.

## Known limitations

```text
L1  THE GRID ALGORITHM EXISTS TWICE IN THE TREE, in cfd and in thermal, and
    the layering makes sharing it impossible without a refactor this milestone
    is not authorized to make. No build check can see a divergence between
    them. Decision 1.

    AND THE TWO COPIES ALREADY DIVERGE AT ONE INPUT, which an adversarial
    review pointed out this limitation failed to say. `thermal::TimeGrid`
    accepts `endTime == 0` and returns a grid of one time and no step;
    `CfdTimeGrid` REFUSES it, because `CfdResult::create` requires a transient
    sample to carry a time above zero and a step of at least one, so a
    zero-step grid would leave nothing to publish and would move that refusal
    onto a `.back()` on an empty vector. The header and a test both record it.
    It is recorded HERE too, because this is the document a future "sync the
    two grids" change would read, and syncing them naively would re-introduce
    the path the cfd guard exists to prevent.

L2  THE TWO ORCHESTRATION BODIES ARE CROSS-CHECKED AT ONE OPERATING POINT.
    The large-dt steady limit is a real measurement, not a proof of textual
    equivalence: a difference that vanishes as rho V / dt -> 0 would not be
    seen by it. Decision 4.

L3  BACKWARD EULER IS FIRST-ORDER AND NOTHING HERE MEASURES THE CONSTANT.
    "Time-step refinement" asserts the ORDER by halving dt; the absolute
    temporal error against an exact solution is not measured, because the
    fixture has no closed-form transient. An analytic transient reference is
    P19-VALID-001's to bring.

L4  NO ADAPTIVE STEPPING, NO RETRY, NO STEP REJECTION. dt comes from the
    definition and never changes during a run. A step that fails fails the run.
    This is ADR-058's formulation and P18's rule, carried unchanged.

L5  NO RESTART AND NO PERSISTENCE. Every run starts from the canonical initial
    condition. A result is derived state (ADR-030) and a history is not
    persisted at all.

L6  THE SAMPLES ARE THE INTEGRATOR'S OWN TIMES. There is no list of requested
    output times in the canonical data -- only the policy -- so there is no
    off-grid request and no interpolation, exactly as P18 found.

L7  THE CONVECTION SCHEME AND THE GRADIENT METHOD ARE STILL HARD-CODED, and
    this milestone inherits ADR-077 L12 unchanged rather than fixing it: the
    orchestrator pins Upwind and LeastSquares, both outside the definition and
    outside the fingerprint, contradicting ADR-073 Decision 5. Moving them into
    the definition means changing CfdAnalysisDefinition and canonicalText, both
    qualified under P19-DATA-001, and no item on this checklist asks for a
    selector. REPORTED, NOT TAKEN.

L8  A SUPPLIED SOLVE THAT IS NON-DETERMINISTIC MAKES THE RUN NON-DETERMINISTIC,
    and nothing here can detect that. ADR-075 L4, ADR-076 L5 and ADR-077 carry
    the same hole; a transient run multiplies its reach by the step count.

L9  NO PERFORMANCE BASELINE. Eighth milestone running. A transient run is
    `steps x iterations` linear solves, which makes it the most expensive thing
    in the tree, and no measurement of it exists.

L10 A FINAL STEP CAN BE ARBITRARILY SHORT. When `endTime / dt` misses a whole
    number by slightly more than kCfdTimeGridTolerance, the remainder -- and so
    the last step -- can be a tiny fraction of dt, which makes that step's
    rho V / dt enormous. It stays finite and the step converges trivially, so
    this is waste rather than a wrong answer, and it is P18's behaviour
    unchanged. Recorded rather than fixed: fixing it would change the grid
    contract and diverge from the implementation this one is checked against,
    and "the last step is the remainder, however small" is what makes the march
    land on endTime exactly.

L11 THE PER-STEP CONVERGENCE CRITERION BECOMES VACUOUS AT A SMALL ENOUGH dt,
    AND THE PUBLISHED EVIDENCE THEN SAYS MORE THAN IT MEANS. An adversarial
    review derived this and the suite measured it. advanceCfdCoupling measures
    the momentum residual against the field the iteration started from, with
    the system as supplied, normalised by ||A||_inf ||U|| + ||b||. On a step's
    FIRST iteration that field is U^n, and ADR-073's two transient halves then
    cancel exactly:

        A U^n - b = (A_steady + rho V/dt) U^n - (b_steady + rho V/dt * U^n)
                  = A_steady U^n - b_steady

    so the numerator is independent of dt while the denominator grows as 1/dt.
    The reported first-iteration residual is therefore Theta(dt).

    MEASURED on the suite's channel, outer iterations per step:

        dt = 1e-1    60   8   8
        dt = 1e-3     1  65  62
        dt = 1e-5     1  65  62

    Step 1 collapses from sixty iterations to ONE, and the work moves into the
    later steps rather than disappearing -- step 1 is normally where the whole
    pressure field is built from p = 0. The effect is SATURATED, so it is a
    regime and not a trend. The suite's first prediction was that the LATER
    steps would collapse; that assertion failed at 65 <= 8 and the measurement
    replaced it.

    THE ANSWER IS NOT WRONG. A step accepted on its first iteration is a
    lagged-pressure step: consistent, first order, and published with a
    conservative flux, because the correction runs whether or not the loop
    iterates again. What stops being worth what a reader expects is the
    EVIDENCE -- outerIterations = 1 beside a residual under tolerance says
    "this step converged" about a step that coupled nothing.

    NOT FIXED HERE, and the reason is scope: the normalisation belongs to
    advanceCfdCoupling, qualified under P19-PVCOUPLE-001, and changing it would
    change what every steady run is accepted on as well. A criterion that is
    scale-free in dt is a numerical decision this checklist does not authorize.
    Every validation in the suite runs at dt >= 0.05 s, where step 1 still
    takes tens of iterations, and the refinement test asserts that directly.

L12 THE STEP RECORDS ARE ALLOCATED UP FRONT, PROPORTIONAL TO THE STEP COUNT.
    kMaxCfdStoredValues guards the SAMPLES; nothing guards the per-step
    records, so a valid definition with dt = 1e-6, endTime = 1 and FinalOnly
    sampling reserves a million records before the first step is taken.
    CfdTimeGrid's "O(1) in memory however many steps it has" is true of the
    grid and not of the march built on it. The failure mode is an allocation
    failure rather than a wrong answer, and P18 has the same shape. Found by an
    adversarial review.

L13 FOUR THINGS THE SUITE CANNOT FALSIFY, named so that nobody reads them as
    tested:
      - transientTermApplied is unfalsifiable FROM THE ORCHESTRATOR, because
        the orchestrator always supplies a step. What is falsifiable, and is
        asserted, is the coefficient: strictly positive, component-independent
        bit-exactly, and scaling as 1/dt (ratio 2.000 under halving).
      - CfdFailureStage::MeshRefused is never reached: the four require* gates
        are inherited from the steady solve unchanged and need a mesh this
        fixture cannot produce.
      - momentum.currentVelocity is read only for diffusion's symmetry cross
        terms, and no transient fixture has a Symmetry face, so feeding it the
        wrong array would not be seen here. The steady suite has the same gap.
      - the 32-bit saturation of CfdSolveFailure::outerIterations cannot be
        reached: a march would need more than 2^32 outer iterations. The
        boundary VALUE is now asserted for a one-iteration budget, which closes
        a max-for-min mutation.

L14 THE HARD-CODED SCHEME AND GRADIENT METHOD ARE NOT DISCRIMINATED BY THE
    STEADY-LIMIT CROSS-CHECK, because both orchestrators pin the same two
    choices. Swapping Upwind for Central, or LeastSquares for GreenGauss, in
    ONE of them would be caught; changing both would not. This is L7's cost
    read from the test side.
```
