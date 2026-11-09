# ADR-077 — The steady solver orchestrates qualified operators, and the result gains its fields

```text
STATUS:    Accepted
DATE:      2026-11-09
MILESTONE: P19-STEADY-001
TOUCHES:   ADR-058 (the settings, residuals and mass balance this binds rather
           than redefines), ADR-061 and ADR-066 Decision 1 and ADR-068 (the
           three records that deferred the result's field arrays to "the first
           milestone that computes a field"), ADR-075 (the coupling this drives,
           and its Decision 9 which assigned the mapping and the publication
           here), ADR-076 (the two kernels the supplied port routes to),
           ADR-030 (derived state), ADR-036 (possession is the evidence)
```

## Context

Nineteen checklist items, and the UNDERSTAND phase found that **ten of them name
work that is already qualified**. "Momentum predictor", "Pressure correction",
"Velocity correction", "Flux correction", "Residual computation" and
"Mass-balance computation" are steps 1–7 inside `advanceCfdCoupling`.
"Convergence loop", "Maximum-iteration failure" and "Divergence detection"
consult two pure functions that `CfdCoupling.hpp` names as this milestone's by
name. "Prepare mesh topology", "Prepare BCs" and "Initialize fields" each have a
`require*`/`prepare*` entry point from an earlier milestone.

So the question this ADR answers is not "how do we orchestrate SIMPLE" but
**what is genuinely left, and where does the solution live once it exists**.

```text
ALREADY DECIDED, NOT OURS TO REOPEN
ADR-058          CfdIterationSettings, CfdConvergenceSettings,
                 CfdSteadySettings, CfdLinearSolverSettings, CfdResiduals,
                 CfdMassBalance, CfdReferencePressure
ADR-075 Dec. 9   the coupling reports in its own vocabulary and publishes
                 nothing; the mapping onto CfdFailureStage is ours
ADR-076          the two kernels, and cfdLinearSolve() as the production port
P19-DATA-001     CfdFailureStage's nine stages, CfdSolveFailure,
                 CfdResultSlot's publication semantics, analysisState
ADR-066          the index space: size(CfdVariable) gives every array length
ADR-068          a field is the caller's own span; the uniform record stays the
                 only authority for an initial condition
```

## The one thing nothing holds yet

`CfdResultSlot` has **no production consumer in the tree**: `grep` finds it in
`CfdDataTests.cpp` and one compile-fail gate and nowhere else. It was built by
`P19-DATA-001` for a producer that did not exist. This milestone is that
producer, and the moment it publishes, a question that three ADRs deferred comes
due.

---

## Decision 1 — The orchestrator is a free function that consumes a supplied solve

`solveCfdSteady` lives in a new `include/bettercad/cfd/CfdSteady.hpp` and takes
the `CfdLinearSolve` port, exactly as `advanceCfdCoupling` does.

It is the first thing in `cfd` to read a `Document`, which `ADR-075` Decision 9
explicitly reserved for this milestone: "Nothing here reads a `Document` or
writes a `CfdResult`." Validating inputs *is* reading the document, through the
gates that already exist — `requireCfdModel`, then `requireCfdMesh`,
`requireCfdTopology`, `requireCfdGeometry`, `requireCfdFieldMap`,
`prepareCfdBoundary`, `prepareCfdInitialField`, `resolveCfdFluid` and
`pressureDatumProblem`.

**The port stays injectable rather than defaulted to `cfdLinearSolve()`**, for
one reason that is not style: the coupling suite's own dense Gaussian
elimination is the independent reference this milestone validates against, and a
hidden default would make that reference unreachable from outside. The cost is
that a caller must name its solver. `ADR-075` L4 and `ADR-076` L5 already record
the limitation this inherits — a supplied solve that is itself non-deterministic
makes the run non-deterministic, and nothing here can detect that.

## Decision 2 — The result gains its fields, in a nested record, validated against the layout

### The candidates

**Option A — `CfdResult` holds `CfdResultFields` (pressure, velocity, massFlux).**
`create` takes the `CfdFieldMap` and validates each length against
`size(CfdVariable)`.

**Option B — `CfdResult` is left byte-identical and a separate
`CfdSteadySolution` carries the arrays beside it.** Nothing qualified changes.

**Option C — the orchestrator returns the arrays to its caller and publishes
only provenance and metadata.** Consistent with `ADR-068`'s "a field is the
caller's own span".

### The comparison

```text
                      A: on the result   B: beside it        C: caller keeps
correctness           one answer to      TWO answers to      no answer after
                      "what is the       "what is the        the call returns
                      solution"          solution"
architecture fit      CfdResult.hpp's    the header's "two   publishes a claim
                      own words: a       answers that        with no content
                      container with     drift" warning is
                      p and U but no     exactly this shape
                      phi "would claim
                      to be a solution
                      and not be one"
the stated invariant  BREAKS "its size   keeps it            keeps it
                      does not depend
                      on the mesh"
migration cost        create() gains a   none                none
                      parameter; the
                      mirror test and
                      two compile-fail
                      gates move
persistence           none of the three changes anything: a result is DERIVED
                      state under ADR-030 and is never written to the file
determinism           identical in all three; the arrays are copied in
                      CellIndex / FaceIndex order whatever holds them
testability           A and B equal; C cannot be tested through the slot at all
```

### The decision, and the argument that settles it

**Option A.** Not because three headers expect it — `ADR-066` Decision 1 warns
against exactly that inference, in its own words: *"an expectation recorded in a
header is not an authorization. Scope comes from `TODO.md` alone."* That warning
is taken seriously here, and the test it implies was applied: **can "Atomic
result publication" be satisfied without the arrays?**

It cannot, and the reason is not aesthetic. Under Option C, `solveCfdSteady`
returns, `analysisState` reports `SolvedCurrent`, and no code anywhere can read
`p`. The state says a solution exists and nothing holds one. `CfdResult.hpp`
already names that failure for a different case — "a partially populated result
is not a worse result, it is a claim that is false" — and a result with no
fields at all is the limit of that case. Option B splits the solution across two
objects whose lifetimes are not tied, which is the "two answers that drift"
failure the same header warns about for currentness.

So the arrays are not added because headers predicted them; they are added
because item 16 is otherwise unsatisfiable, and the headers predicted them for
the same reason.

### What Option A costs, stated rather than discovered later

`CfdResult.hpp` says "THE MESH IS NOT COPIED IN: a result records the
`MeshStamp` of the mesh it belongs to, **and its size does not depend on the
mesh**." The first clause stays true — no mesh is copied. The second becomes
false, because `pressure.size() == cells` by construction. The sentence is
**amended in place rather than deleted**, as `P19-DIFF-001` amended its
predecessor and as this milestone's own `CMakeLists.txt` comment was amended:
the superseded claim and the reason are both part of the record.

`CfdData_AResultHoldsProvenanceAndMetadataOnly` asserts
`sizeof(CfdResult) == sizeof(source + metadata)`, so **this change cannot be
made silently — the build stops.** That mirror is the mechanism `P19-DATA-001`
built to force the addition to be stated, it worked, and it is extended rather
than relaxed. Its name no longer describes it, so it is renamed to
`CfdData_AResultHoldsProvenanceMetadataAndItsFields` and the old name's
disappearance is the statement.

### And one cost WAS discovered later, by the qualification gate

This heading promises costs stated in advance. This one was not: it was found by
the first qualification run, which failed on it.

The third field member also gives the **private constructor** a third parameter,
and one compile-fail case names that constructor rather than the factory:

```text
compile_fail.cfdids.a-result-cannot-be-built-without-its-factory
    CfdResult conjured{CfdResultSource{}, CfdResultMetadata{}};
    requires exit 1 AND a diagnostic matching `is private within this context`
```

After this decision the two-argument form matched no candidate at all, so GCC
reported **arity** — "candidate expects 3 arguments, 2 provided" — and never
reached access. The case still failed to compile, the gate still exited 1, and
the claim it exists to make had stopped being tested. **A gate that cannot fail
for its own reason is not a gate**, and this one had become an arity test wearing
an access test's name.

Nothing about the decision is wrong, and nothing about the production code was:
the constructor is private and `create` is still the only route to a
`CfdResult`. What the decision broke was the PROOF, which is the asset the gate
holds. The size mirror above was built to make this addition impossible to make
silently and it did its job; the access gate was collateral it did not cover,
because a `sizeof` assertion and an access diagnostic fail for different reasons.

The repair passes the right arity, so overload resolution reaches the private
constructor and the diagnostic is an access error again. Widening the regex to
accept "no matching function" was **refused**: it would have produced a passing
gate that no longer tested access, which is relaxing a gate to obtain a PASS.
Because the field set cannot be conjured either, the third argument has to come
from a real result — so the gate now proves something stronger than before: a
consumer holding a legitimate field set cannot assemble a result around it.

**The reusable form of this cost:** changing a constructor's parameter list
reaches every compile-fail case that names that constructor, and a search for
the factory's callers cannot find them, because they are deliberate
non-callers. `docs/verification/P19-STEADY-001/FREEZE.md` records the run, and
that milestone's `BLAST RADIUS` now carries the line it was missing.

## Decision 3 — Sixteen coupling problems map onto nine failure stages, and the table is total

`ADR-075` Decision 9 assigned this mapping here. It is written as one `switch`
with no `default`, so adding a seventeenth `CfdCouplingProblem` upstream fails
to compile rather than silently landing in a catch-all.

```text
the geometry / topology / layout refusals   -> IllPosed
the solve's own refusals                    -> LinearSolveFailed
a non-finite value reaching the operator    -> NonFiniteField
an inconsistent evaluation                  -> IllPosed
```

The exact assignment is in `src/cfd/CfdSteady.cpp` beside the `switch`, because
a table in prose and a table in code are two answers that drift.

## Decision 4 — "NaN / Inf rejection" is a mapping, because the check we wrote could not fire

The first implementation contained a loop over the corrected `p`, `U` and `phi`
after every iteration, refusing as `CfdFailureStage::NonFiniteField`. The
reasoning for it was sound in the abstract — a non-finite *residual* is what
`cfdCouplingVerdict` reports as `Diverged`, which is a different fact about a
different quantity, and a field can in principle go non-finite while a ratio of
norms stays finite.

**It was then found unreachable, by reading the layer below rather than by
testing it.** `applyCfdCouplingCorrection` ends:

```cpp
if (!allFinite(out.faceFlux) || !allFinite(out.velocity) || !allFinite(out.pressure)) {
    return std::unexpected(internalFailure(CfdCouplingProblem::ResultIsNotFinite, ...));
}
```

So `advanceCfdCoupling` never returns a success whose corrected fields hold a
non-finite value, and the loop could not fire for any input. `CfdBoundary.hpp`
states this project's rule for the same shape of problem — "An enumerator no
input can produce is worse than no enumerator" — and `P19-LINSOLVE-001` removed
a clause from **both** `linalg` kernels for exactly this reason, after a mutation
probe survived and showed the clause was dead. That lesson is one milestone old,
so the loop is removed here before a probe had to find it.

```text
a non-finite RESIDUAL           cfdCouplingVerdict -> Diverged
a non-finite CORRECTED FIELD    the coupling refuses it as ResultIsNotFinite,
                                and stageOf maps that to NonFiniteField
a non-finite value ANYWHERE     CfdResultFields::create refuses it as a type
ELSE                            invariant, for every caller that is not
                                solveCfdSteady
```

The item is therefore discharged by the fact that **nothing non-finite can be
published**: `applyCfdCouplingCorrection` refuses it upstream and
`CfdResultFields::create` refuses it downstream as a type invariant. The test
drives a NaN-returning solve port and asserts the published stage is
`LinearSolveFailed`, which is what the coupling reports for a solution holding a
non-finite value.

**AN EARLIER VERSION OF THIS PARAGRAPH CLAIMED THE TEST ASSERTS
`NonFiniteField`. IT DOES NOT.** An adversarial review found the contradiction,
and it matters more than a wording slip: the route this decision's own table
presents as live — a non-finite corrected field becoming `ResultIsNotFinite`
becoming `NonFiniteField` — is one the same milestone's probe sweep measured as
unreachable (L8). The honest discharge is the two-sided refusal above, not a
mapping row no input to `solveCfdSteady` can reach. `stageOf` must still answer
for that row, because the layer below can raise the problem even where this
milestone cannot drive it there, and the mapping is tested directly.

What this milestone does **not** do is keep a loop that reads like diligence and
executes never.

## Decision 5 — The loop re-assembles momentum every iteration, and passes the pressure gradient

`CfdMomentumInput` takes `faceFlux`, `currentVelocity`, `faceGradient` and
`pressureGradient`. All four move with the fields, so the three systems are
re-assembled every outer iteration; caching them would freeze the convection
coefficients at the initial field and the run would converge to the wrong
answer, silently.

**The pressure gradient is passed, and a review of `P19-PVCOUPLE-001` is why.**
That review found every fixture in the coupling suite omitting it, with the
consequence recorded in the test file: "WITHOUT THE PRESSURE GRADIENT THE
MOMENTUM EQUATION IS MISSING ONE OF ITS TWO DRIVING FORCES ... so the pressure
was a pure OUTPUT and nothing could converge in the usual sense." A steady
solver that omitted it would iterate, report falling residuals and solve a
different problem.

## Decision 6 — One failure path, and it publishes through the slot

Every refusal becomes a `CfdSolveFailure` and goes to
`CfdResultSlot::recordFailure`, which keeps any existing result untouched. There
is no partial publication and no second path: a run either reaches
`recordSuccess` with a complete field set or reaches `recordFailure` with a
stage and a diagnostic.

`outerIterations` on a failure is `nullopt` when the iteration never started —
an input or mesh refusal — and the completed count otherwise, which is what
`CfdSolveFailure`'s own documentation asks for.

## Decision 7 — The settings come from the definition, not from a parameter

`solveCfdSteady` takes a `CfdAnalysisId` and reads `ADR-058`'s four settings
records out of `CfdAnalysisDefinition`. It does **not** accept them as
arguments, and this is a correctness requirement rather than a tidying.

**An earlier draft of this ADR and of the header took a `CfdSteadySolveInput`
carrying all four.** It was written before `CfdAnalysisDefinition` was read
closely, and it was wrong twice over.

```text
it was a second parameter system   the definition already holds iteration,
                                   convergence, linearSolver and steady, and
                                   ARCHITECTURE.md rejects a second one. This
                                   milestone's own scope note (1) had warned
                                   against exactly that, one milestone after
                                   P19-CONT-001 learned it
it broke provenance                canonicalText writes every setting into the
                                   definition fingerprint, and
                                   CfdResultSource::definition IS that
                                   fingerprint
```

The second reason is the one that settles it. These seven lines are inside the
fingerprint:

```text
iteration.velocity_relaxation            convergence.divergence_factor
iteration.pressure_relaxation            linear_solver.relative_residual_tolerance
convergence.momentum_residual_tolerance  steady.max_iterations
convergence.continuity_residual_tolerance
```

So a run driven by caller-supplied settings publishes a `CfdResult` whose
provenance names one set of settings while the numbers came from another, and
`resultCurrency` reports it `Current`. That is a false claim of exactly the kind
the result's whole design exists to prevent, and no test of the solver's
numerics would ever catch it.

A caller that wants different settings edits the analysis. That moves the
revision and the fingerprint, which is what should happen.

## Consequences

```text
gained    a steady incompressible solver that runs end to end from a Document
          and publishes a readable solution
gained    CfdResultSlot's first production consumer, three milestones after it
          was written
cost      CfdResult is no longer fixed-size, and CfdResult::create gains a
          parameter. Both are stated in the header and asserted by the mirror
cost      P19-DATA-001's result tests are requalified by the full run
refused   a settings type of our own, a second residual record, a CLI, time
          stepping, and any judgement of whether the numbers are good enough
```

## Known limitations

**L1** A supplied non-deterministic solve makes the run non-deterministic and
nothing here detects it. Inherited from `ADR-075` L4 and `ADR-076` L5.

**L2** Convergence is recorded, not judged. `CfdResult.hpp`: "CONVERGENCE IS
RECORDED, NOT RE-JUDGED HERE. Whether these numbers are good enough is
validation's question (`P19-VALID-001`)." A converged run here is not a
validated one.

**L3** The operator inherits `P19-PVCOUPLE-001`'s confinement to LOW CELL PECLET
NUMBER. Nothing here widens it, and a high-Peclet run is not a qualified result.

**L4** No performance baseline. An outer iteration's cost is now measurable for
the first time, since the solve is no longer the caller's, but measuring it is
not on this checklist and `CLAUDE.md`'s performance workflow requires a baseline
before a change rather than a number in an ADR.

**L5** One mesh, one fluid, one analysis per call. Nothing batches, and nothing
here is threaded.

**L6** `CfdMomentumInput::faceGradient` is left empty, which omits diffusion's
non-orthogonal correction, so on a non-orthogonal mesh the diffusion term is
the implicit arm only and not a complete Laplacian. This is `ADR-069`'s
limitation carried forward unchanged, with `ADR-063` finding F1's measurement of
P16's reference meshes at 31 to 66 degrees mean non-orthogonality behind it. It
is carried rather than fixed because **nothing in the tree produces a per-FACE
gradient** — `reconstructCfdVelocityGradient` returns a per-CELL field, the only
places that fill `faceGradient` anywhere are the diffusion and momentum tests
with hand-built arrays, and writing a cell-to-face interpolation would be new
numerics that no item on this checklist authorizes.

**L7** In a closed domain the published `CfdResultMetadata::massBalance` is the
default `(0, 0)`. `ADR-058`'s two measures are relative to the boundary
throughput, which is zero there, so they are **undefined rather than small** —
`CfdContinuity.hpp` says so and the coupling leaves them defaulted for that
reason. `CfdResultMetadata` has no field that can express "undefined", so the
default is what reaches the result, and the distinction lives in
`CfdSteadyReport::massBalanceDefined`. What this milestone does not do is invent
a number: the evaluator's own value is passed through unchanged, whatever it is.

**L8** **THREE** of `CfdFailureStage`'s nine stages are unreachable from
`solveCfdSteady`, and that is reported rather than papered over.
`MassBalanceFailed` is documented as "the mass balance failed validation", and
validation is `P19-VALID-001`'s. `ContinuityNotClosed` would need the
`continuityClosed` flag that `ADR-075` Decision 5 **deleted** after a mutation
probe showed it was true by construction for every input. And `NonFiniteField`
is unreachable too, measured over fifteen driving ports in
`experiments/stage-probe.out`: every route is intercepted by a guard in a lower
qualified layer. Manufacturing a check to light any of them would be the inverse
of this project's own rule about enumerators no input can produce, so none is
produced.

**THIS LIMITATION SAID "TWO" UNTIL AN ADVERSARIAL REVIEW READ IT AGAINST THE
EVIDENCE.** The README and the test both said three; the ADR had not been updated
when the measurement landed. `CLAUDE.md` makes the evidence authoritative when
documents disagree, so the ADR was the wrong one and is corrected here.

**L9** The divergence baseline is **structurally inert for any residual whose
first-iteration value is exactly zero**, and in this milestone's own fixture that
is two of the four criteria. `cfdCouplingVerdict` requires `start[i] > 0.0`
before it compares a ratio, and `CfdInitialCondition` defaults to zero velocity
and zero pressure — so on iteration 1 the x- and y-momentum systems have an
identically zero source (no pressure gradient at `p = 0`, no-slip walls, an inlet
along `z` only) and `normalisedResidual` returns exactly `0.0`. For the rest of
the run no growth in x- or y-momentum can be reported as `Diverged`: a transverse
blow-up spends the whole budget and is reported `IterationLimitReached`, which is
the wrong reason. In a closed domain started at rest all four baselines are zero
and the ratio test is fully inert.

Found by an adversarial review, then confirmed independently by this milestone's
own divergence test, whose improved diagnostic names **continuity** as the
criterion that grew rather than momentum. Choosing the first iteration as the
baseline is this milestone's decision; the gating behaviour is
`P19-PVCOUPLE-001`'s qualified function, so a different baseline is a change to
that operator's contract and belongs to whoever revisits it.

**L10** The published `CfdResiduals` are **mixed-vintage**. `CfdCoupling.cpp`
states it — "THE MOMENTUM RESIDUAL IS MEASURED AGAINST THE FIELD THE ITERATION
STARTED FROM" — while continuity is measured on the corrected flux. So the one
record a result carries describes momentum about the previous velocity and
continuity about the corrected flux, while the published fields are all from the
last iteration. The published velocity has therefore never itself been measured
against `momentumResidualTolerance`. Conventional SIMPLE practice, which is why
it is a limitation and not a defect, but it was unstated on a published,
provenance-stamped artefact until a review named it.

**L11** In a closed domain the published `CfdResultMetadata::massBalance` is
`(0, 0)`, which **reads as a perfect mass balance** where the truth is that both
measures are undefined. Nothing invents the number — the evaluator's own value is
passed through, and `ADR-058`'s measures are relative to a boundary throughput
that is zero there. But the only carrier of the distinction,
`CfdSteadyReport::massBalanceDefined`, is on the return value and not in the
slot, and the slot is what `analysisState` and every future consumer read. By
`CfdResult.hpp`'s own standard — "a partially populated result is not a worse
result, it is a claim that is false" — `(0, 0)` presented as a measurement is
such a claim. Expressing it needs a field on `CfdResultMetadata`, which is
`P19-DATA-001`'s qualified type and not on this checklist.

**L12** The convection scheme and the gradient method are chosen by the
orchestrator, are **not selectable from `CfdAnalysisDefinition`, and are not in
the definition fingerprint**. `solveCfdSteady` hard-codes
`CfdConvectionScheme::Upwind` and `CfdGradientMethod::LeastSquares`; both enums
have two live values and both change the numbers materially. `ADR-073`
Decision 5 says the opposite of what the code does: "which of `ADR-070`'s two
methods made it is the analysis's decision and not this operator's."

Two consequences, stated rather than discovered later. `Central` and `GreenGauss`
are capabilities no analysis can reach through the only production producer of a
result, which is the "false capability" `CfdData.hpp` warns against. And a future
build that flips either default publishes numerically different fields under a
**byte-identical** `CfdDefinitionFingerprint`, so `resultCurrency` would call an
old result `Current` across a change that moved every number — precisely what the
fingerprint exists to prevent. Putting them in the definition means changing
`CfdAnalysisDefinition` and `canonicalText`, both qualified under
`P19-DATA-001`, and no item on this checklist asks for a selector. Reported to
the owner as a scope decision.

**L13** A **throwing** solve port, or a `std::bad_alloc` from any per-iteration
allocation, leaves `CfdResultSlot` holding neither a new result nor a failure, so
`analysisState` reports `SolvedCurrent`, `SolvedStale` or `Ready` for an analysis
whose latest attempt did not complete. The header's "ONE EXIT FOR EVERY REFUSAL"
is true of every `return` and not of a `throw`; nothing on the path is `noexcept`.
The empty-function case is handled properly, as `LinearSolveRefused`. L1 records
a non-deterministic port as the inherited hole; this is a second one, unrecorded
until a review found it.

**L14** `CfdOutputSettings::residualHistory` is **in the fingerprint and
ignored**. `canonicalText` writes `output.residual_history` and
`CfdResidualHistory` offers `FinalOnly` and `EveryIteration`; `solveCfdSteady`
publishes one `CfdResiduals` either way and `CfdSteadyReport` carries no history.
So an analysis asking for `EveryIteration` gets `FinalOnly` under a fingerprint
that says otherwise. `CfdResultMetadata` structurally cannot express a history,
which makes this a not-yet-implemented capability rather than a defect — but
`EveryIteration` is a false capability from the only producer until it is.

**L15** `CfdResult::create`'s fields-fit-this-layout guard is `A || B`, and
**only the cell-count half is exercised by a test**. A probe that dropped the
face-count half survived. Firing `B` alone needs a field set whose cell count
matches the layout and whose face count does not, and `CfdResultFields::create`
will not build one against that same layout — so it would take two layouts with
equal cell counts and different face counts, which one fixture on one mesh cannot
produce. What the suite pins instead is that a field set carries both counts
independently and that they differ on this mesh. The consequence if the half ever
broke: a field set from another mesh would pass whenever that mesh happened to
have the same cell count.

**L16** **No test drives any of the four mesh-stage refusals**, so the choice of
`CfdFailureStage::MeshRefused` for `requireCfdMesh`, `requireCfdTopology`,
`requireCfdGeometry` and `requireCfdFieldMap` is unverified end to end. A probe
that reported a field-map refusal as `IllPosed` survived. Driving one needs a
document that passes `requireCfdModel` and then fails one of the four, which this
fixture cannot construct: the reference channel either meshes and satisfies all
four or fails earlier at the model gate. An adversarial review separately noted
that the field map is `ADR-066`'s index space rather than a mesh measure, so
`MeshRefused` — documented as "P19-MESH-001's measures" — is the least-bad of the
nine for it rather than an obviously right answer. Both the gap and the judgement
are recorded rather than resolved.

**L17** The gradient method and the convection scheme **cannot be pinned by a
test from outside**, which is the other face of L12. A probe that switched
`LeastSquares` for `GreenGauss` survived: both converge, and distinguishing their
answers needs an analytic reference this subsystem does not have on a tet mesh
with 31 to 66 degrees of non-orthogonality and no non-orthogonal correction
(L6). The scheme probe, `Upwind` for `Central`, was KILLED — by the gravity
test's quantitative `dp/dy` check, which is a side effect of that test rather
than its purpose. So of the two hard-coded choices, one is pinned by accident and
one is not pinned at all. Putting both in the definition, as `ADR-073`
Decision 5 says they belong, would make them observable; that is the scope
decision L12 reports.

## Amendments after the adversarial reviews

Two independent read-only reviews ran before the freeze, with different
concerns: a correctness review, and a test-adequacy review that produced
twenty-two mutation predictions for the probe driver. Between them: one DEFECT,
eight RISKs, seven NITs and the mutation table.

**THE DEFECT: gravity never reached the momentum equation.** `solveCfdSteady`
left `CfdMomentumInput::gravity` absent, so a gravity analysis got the boundary
half of the hydrostatic treatment — `prepareCfdBoundary` applies
`hydrostaticPressure` at a prescribed-pressure facet — and no body force in the
interior. `ADR-073` Decision 6 states that those are two halves of one
treatment. Nothing refused it: `assembleCfdMomentum` validates gravity only
WHEN PRESENT, so an absent record is silently the gravity-free equation, and
`canonicalText` puts `gravity.acceleration` and `gravity.reference_height` into
the fingerprint — so a wrong field would publish under provenance naming gravity
and `resultCurrency` would report it `Current`. The same failure Decision 7 was
written against, except that a dropped term changes the answer and not just the
metadata.

Fixed in one line, with a regression test **verified to fail without it**: with
the body force the measured `dp/dy` is `-5.78e-05 Pa/m` against `-rho*g =
-4.99e-05`, a ratio of 1.158; without it, `-7.98e-06`, a ratio of 0.160.

Writing that test corrected two of this author's own predictions. The sign of
`dp/dy` does **not** flip without the fix, and no spurious transverse flow
appears — the outlet's prescribed hydrostatic profile alone still produces a
pressure falling with `y`, it just reaches 16% of `rho*g` instead of all of it.
And Earth gravity turned out to be unusable in this fixture at all: `rho*g = 9790
Pa/m` drives the flow clean out of the low-Peclet regime `P19-PVCOUPLE-001` is
qualified in, and `assembleCfdMomentum` refuses the run with a negative diagonal.
The test uses a scaled `g` and asserts the Peclet number from the measured field
rather than from the inlet alone.
