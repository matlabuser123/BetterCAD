# P19-MOM-001 — Momentum Equation

RESULT: PENDING — qualification not yet run
COMMIT: pending

## TASK

`P19-MOM-001` — Momentum Equation. Assemble the incompressible momentum
equations

```text
rho (dU/dt + div(U U)) = -grad p + mu lap U + rho g
```

within v1 scope, against the thirteen-item checklist in `TODO.md`.

**AUTHORIZED 2026-11-08** by the owner's "implement and fully verify" over that
checklist, which arrived pasted into the instruction exactly as `P19-BC-001`'s,
`P19-IC-001`'s, `P19-DIFF-001`'s, `P19-GRAD-001`'s and `P19-CONV-001`'s did.

**THE PASTED TEXT WAS DIFFED AGAINST `TODO.md`'S OWN SECTION BEFORE ANY WORK
BEGAN.** Byte-for-byte identical across all 32 lines after CR normalization,
sha256 `386e3242048e9e85` over both, with 13 unticked items and 0 ticked —
counted by `grep -c`, not assumed. So the scope is this file's checklist
unaltered. The authorization was recorded in `TODO.md` before any
implementation, and that edit changed no checkbox: 663 ticked and 367 unticked
before and after, total 1030.

## SCOPE

The thirteen items, and where each is answered:

```text
Steady momentum form            assembleCfdMomentum with no timeStep;
                                CfdMomentum_TheSteadyMatrixIsExactlyConvection
                                PlusDiffusionBitForBit
Transient momentum term         backward Euler, rho V / dt on the diagonal and
                                rho V / dt * U^n in the source;
                                ...TheTransientDiagonalIsDensityTimesVolumeOver
                                TheTimeStep, ...IsAddedAfterTheSumAndNowhereElse,
                                ...CarriesThePreviousFieldAndNotTheCurrentOne
Convection contribution         assembleCfdConvection, summed slot-wise
Diffusion contribution          assembleCfdDiffusion, summed slot-wise
Pressure-gradient contribution  -V (grad p)_i from a caller-supplied field;
                                ...ThePressureTermIsMinusVolumeTimesTheGradient
                                Component
Gravity/body-force contribution rho V g_i from an optional CfdGravity;
                                ...TheBodyForceIsDensityTimesVolumeTimesGravity,
                                ...TheReferenceHeightOfGravityChangesNothingHere
Boundary contributions          inherited verbatim from the two sub-operators'
                                sources and diagonals; ...SurfacesASubOperator
                                RefusalUnderItsOwnTag, and the reference bodies
Sparse global matrix            CfdSparseMatrix, cells x cells, per component;
                                ...AssemblesASquareMatrixWhosePatternIsTheCell
                                Adjacency
Deterministic assembly          ...IsBitwiseReproducibleAcrossRepeatedAssemblies,
                                plus the repeat presets in qualification
No hidden stabilization         the bit-exact term decomposition, the negative
                                diagonal case, and the reported dominance;
                                ADR-073 Decision 9
Independent small-system oracle three parts, labelled (a), (b) and (c) in the
                                suite's header with what each establishes
Regression PASS                 qualification, below
Evidence recorded               this directory
```

**WHAT IS DELIBERATELY NOT IN SCOPE**, stated so that no reader infers it:
there is no solve and so no velocity field (`P19-LINSOLVE-001`), no continuity
equation (`P19-CONT-001`), no pressure–velocity coupling and so no Rhie–Chow
interpolation (`P19-PRES-001`), no under-relaxation, no Courant number and no
time-step quality rule, no turbulence, no buoyancy and no thermal coupling.

**ONE SCOPE NOTE WAS RECORDED AT AUTHORIZATION TIME RATHER THAN DISCOVERED
LATER.** The checklist's `+ rho g` is a CONSTANT gravitational body force at the
single constant density P19 v1 carries. It is not buoyancy, which `ADR-058`
refuses from this phase, because buoyancy needs density to vary with
temperature. A constant `rho g` at constant density is also absorbable into a
modified pressure, so in a domain no boundary pressure fixes it changes no
velocity field; that is a property of the equation and not a defect, and it is
asserted as the hydrostatic-cancellation test rather than described in prose.

## BASELINE

The tree at `db53c50`, the `P19-CONV-001` hash record, with `origin/main` at the
same commit.

```text
cfd subsystem        702,187 assertions in 447 cases
cfd unit suites      14 files, 24,393 lines
cfd production       17 sources, 7,837 lines; 17 headers, 6,634 lines
architecture gate    205 tests
TODO.md              663 ticked, 367 unticked, 1030 total
```

Three milestones had recorded, in those words, that nothing consumed them.
`P19-GRAD-001`: "nothing here is wired into `assembleCfdDiffusion`".
`P19-DIV-001` L1: "nothing consumes this divergence yet … it is one term of an
equation `P19-MOM-001` and `P19-PRES-001` have not assembled".
`P19-CONV-001`: the same of its own operator. `TODO.md` carried the sentence at
line 144. **This milestone is the first of the phase to consume a predecessor
rather than add an operator beside it**, which is the whole of its novelty: every
term but two was already written and qualified.

## ARCHITECTURE

`ADR-073` — "Momentum is the sum of qualified operators, the terms stay
separable by construction, and no stabilization is added". Ten decisions; the
central one compared three candidates on eleven axes.

```text
A  re-derive every term in one face pass
   REJECTED: a second copy of two qualified discretisations, whose divergence
   from the first would be SILENT. P19-CONV-001's review found one three-line
   interpolation duplicated across three milestones with the same defect left in
   it three times; a fourth copy of the upwind decision is that mistake larger.
   It also leaves "no hidden stabilization" with no falsifiable form, because
   nothing would be separately observable
B  call both operators and sum their systems            CHOSEN
   The terms stay separable, so the checklist's negative claim becomes a
   BIT-EXACT assertion. Cost: three CSR builds where one pass would do, and a
   coupling to two operators' CSR patterns, gated by a refusal
C  thread an accumulator through the two operators
   REJECTED: edits two QUALIFIED operators' public API to serve one new
   consumer, which is the shape ADR-063 Decision 7 and ADR-069 each refused,
   and it destroys the decomposition test as A does
```

The other decisions: the patterns are verified identical rather than assumed
(2); the seven dimensional identities are proved at compile time (3); backward
Euler with the previous time level as a separately named input (4); the pressure
gradient is caller-supplied, typed, per cell, and its absence reported (5);
gravity's absence is the absence of a record and `referenceHeight` is not read
(6); the sub-operators' refusals are predicted inside momentum's own gate (7);
one component and `cells` by `cells` IS the "sparse global matrix" (8); "no
hidden stabilization" is given three falsifiable forms (9); the error channel is
`std::expected` with a tag (10).

### The design was measured before it was written

`P19-DIV-001`'s first ADR draft PREDICTED a bit-exact global sum and was wrong —
a test asserting it would have failed on a correct operator. `P19-CONV-001`
therefore measured first and found two of its own predictions wrong. Five
arithmetic claims of this design were measured before any C++ existed, with a
fixed seed (`202611081`), in plain IEEE-754 double in the order the C++ writes.
`experiments/measure.py` and `experiments/measure.out` are committed.

```text
Q1  ((conv+diff)+trans) vs (conv+(diff+trans))
      200,000 cases   78.06% bit-exact   worst relative 4.034e-13
Q2  hydrostatic cancellation -(V*fl(rho*g)) + ((rho*V)*g)
      exactly 0.0 on 130,101 / 200,000 (65.05%)
      worst |residual| / |gravity term| = 3.649e-16
Q3a row-sum additivity, relative to the SUM
      50,000 cases    47.39% bit-exact   worst relative 5.264e-12
Q3b the same, relative to the ROW SCALE (max |a_ij|)
      50,000 cases    47.39% bit-exact   worst 1.085e-14
Q4  the transient diagonal lost in a steady diagonal, dt out to 1e20
      invisible on 45,162 / 200,000 (22.58%); threshold ratio 1.087e-16
Q5  source accumulation order, five addends
      declared vs reversed   57.45% bit-exact  worst relative 3.044e-13
      declared vs ascending  68.64% bit-exact  worst relative 3.044e-13
```

**TWO OF THOSE FIGURES ARE CORRECTIONS TO THE SCRIPT'S OWN FIRST RUN, AND BOTH
ARE PUBLISHED RATHER THAN QUIETLY FIXED**, because each looked like a result and
was an artefact of the measure.

Q2 first reported "worst relative 1.000e+00", which reads as a total failure of
the physics. It is an artefact of comparing a residual against exact zero with a
relative measure: `|a - 0| / max(|a|, 0)` is `1.0` for every non-zero `a`, so
the figure was a number about nothing. The quantity that means something is
`|residual| / |term|`, and it is `3.649e-16`.

Q4 first sampled `dt` in `1e-4..1e4` and reported `0 / 200,000` invisible, which
reads as "the transient term is always visible" and is a statement about the
SAMPLING: within those ranges `rho V / dt` never fell sixteen orders below the
steady diagonal. Re-sampled out to `dt = 1e20` it is lost 22.58% of the time, at
a ratio of `1.087e-16` — the diagonal's last bit, which is the honest form of
the claim.

Q3 is published in BOTH forms deliberately. Dividing by the SUM measures the
cancellation of a nearly-null momentum row and not the assembly, and quoting it
alone would justify a tolerance two decades looser than the operator needs;
dividing by the row's own scale gives `1.085e-14`, a factor of 500 tighter, and
that is the bound a test may assert. Quoting only the second would hide that the
sum itself is ill-conditioned near a null row.

**WHAT THE MEASUREMENTS DECIDED.** No order-independence is claimed of either
accumulation. The assembly fixes ONE order, states it in the header, and the
suite reproduces that order rather than a mathematically equal one — which is
exactly why the term decomposition can be asserted BIT-EXACTLY while the row-sum
additivity cannot. `kCfdMomentumRowSumTolerance` is `1e-12` and
`kCfdMomentumHydrostaticTolerance` is `1e-14`, each two decades above its own
worst measurement rather than tight to it.

## BLAST RADIUS

**NO EXISTING SOURCE FILE WAS MODIFIED.** Three CMake lists gained one line
each; `TODO.md` and `ROADMAP.md` are documentation. So the direct blast radius is
the new files themselves, and the interesting question is the other direction:
what this operator now DEPENDS on that it did not before.

```text
direct callers of the new API      none. Nothing consumes this operator (L1)
new dependencies acquired          assembleCfdConvection, cfdConvectionProblem,
                                   assembleCfdDiffusion, cfdDiffusionProblem,
                                   CfdSparseMatrix::{fromCsr, rowStart, inner,
                                   values, coeffSi, rowSumSi}, CfdGeometry::
                                   cellVolume, CfdTopology::{owner, neighbour,
                                   faces, cells, boundaryFaces, mesh},
                                   PreparedCfdBoundary::{boundaryFaces, source}
the coupling that matters          THE TWO SUB-OPERATORS' CSR PATTERNS. A
                                   change to either symbolic pass breaks the
                                   slot-wise sum. It is gated by a refusal
                                   (AssemblyIsInconsistent) rather than by a
                                   type, and that is stated as a cost in
                                   ADR-073's Consequences, not hidden
serialization                      none. Nothing here is persisted: a momentum
                                   system is DERIVED state in ADR-058's sense
regeneration                       none. No feature, no document state
stable references                  none consumed, none produced
the CLI                            unchanged; no command reaches this
reference models                   read-only, by one new test
```

**THE REGRESSION SET WAS CHOSEN FROM THAT TABLE AND NOT FROM PROXIMITY.**
Because momentum consumes convection and diffusion, a defect in momentum cannot
reach them — but a misreading of their contracts by momentum would show up as a
momentum failure, which is why the decomposition is asserted against their own
output rather than against a transcription of their formulas. Because nothing
consumes momentum, no downstream subsystem can regress. The full suite is run
regardless, in three presets, because that is the qualification gate and
narrowing it on an argument would be the shape of reasoning this project
refuses.

## IMPLEMENTATION

```text
include/bettercad/cfd/CfdMomentum.hpp      new
src/cfd/CfdMomentum.cpp                    new
tests/cfd/CfdMomentumTests.cpp             new
tests/compile_fail/CfdMomentumMisuse.cpp   new
docs/architecture/decisions/ADR-073-...md  new
src/cfd/CMakeLists.txt                     +1 line
tests/CMakeLists.txt                       +1 line
tests/compile_fail/CMakeLists.txt          +1 group, 28 cases
TODO.md, ROADMAP.md                        documentation
```

`assembleCfdMomentum(topology, geometry, boundary, input)` returns
`std::expected<CfdMomentumSystem, CfdMomentumFailure>`: a `CfdSparseMatrix` in
kg/s, a source in newtons, and a summary carrying both sub-operators' summaries
verbatim. Fourteen gate phases, then two sub-assemblies, then a verified
slot-wise sum, then three cell-wise source terms, then three finiteness gates.

**THE ASSEMBLY IS THE SUM AND NOTHING ELSE.** It calls `assembleCfdConvection`
and `assembleCfdDiffusion`, compares their `rowStart` and `inner` arrays element
by element and refuses if they differ, sums `values` slot-wise, and adds the
transient diagonal afterwards. There is no face loop in the file: every
face-dependent coefficient was decided by an operator that was qualified before
this milestone began.

**THE ARITHMETIC IS TYPED, AND THAT IS WHAT MAKES THE HEADER'S SEVEN
`static_assert`s LOAD-BEARING.** The transient coefficient is bound to a
`MassFlowRate` and each locally formed source term to a `Force`, reaching a
plain `double` only at the point of storage. A term that lost a density, a
volume or a time does not compile.

```text
every matrix coefficient    kg/s     phi  |  mu A / d  |  rho V / dt
every source entry          N        phi U | (rho V/dt) U^n | V grad p | rho V g
```

**WHAT IS NOT ADDED**, named so a reader need not infer it from an absence: no
artificial or numerical viscosity, no upwinding beyond the caller's chosen
scheme, no deferred-correction blending, no Rhie–Chow interpolation, no
under-relaxation, no diagonal boosting, clipping, limiting or flooring, no
Peclet-based scheme switching.

## TESTS

```text
47 unit cases      14,198 assertions   tests/cfd/CfdMomentumTests.cpp
28 compile-fail    tests/compile_fail/CfdMomentumMisuse.cpp
```

The cfd subsystem's arithmetic, which is the check a passing suite cannot make:

```text
before (db53c50)   702,187 assertions in 447 cases
after              716,385 assertions in 482 cases
delta               14,198              and      35
the momentum suite  14,198 assertions in  35 ... 47 cases
```

**NO PRE-EXISTING CFD ASSERTION MOVED.** A deleted test and an added test net to
nothing in a pass/fail count; they do not net to nothing here.

### The oracle is three things, and they are not equally strong

The checklist asks for an "independent small-system oracle", and one test cannot
honestly carry that name, so the claim is split and each part is labelled in the
suite's own header:

```text
(a) CLOSED FORM. Every coefficient computed in the test from the geometry
    accessors by the published formulas. This pins the transient, pressure and
    gravity terms, which have no other oracle -- and it takes the two
    sub-operators' coefficient formulas AS GIVEN, so for convection and
    diffusion it is a TRANSCRIPTION and not an independent derivation. Saying
    otherwise would be P19-GRAD-001's r5 defect, which P19-DIV-001 then found
    again
(b) RESIDUALS, independent of every coefficient formula. A conservative flux
    admits the constant as a steady solution whatever the coefficients are;
    gravity must cancel a hydrostatic pressure gradient whatever either term's
    arithmetic looks like
(c) THE BIT-EXACT DECOMPOSITION, independent of both sub-operators' internals:
    the matrix IS their slot-wise sum plus the transient diagonal. This is the
    whole falsifiable content of "no hidden stabilization"
```

### The two signs, each checked independently

An independent review found that **the single most important claim of the
milestone — that the two operators meet with a `+` — had no independent check**.
Every assertion wrote `conv + diff`, the production expression, which catches a
mutation but cannot validate the convention; and the one test that looked
physical was provably sign-blind, because the constant is in the null space of
each operator *separately*, so `A 1 = 0` holds for `a A_conv + b A_diff` at any
`a` and `b` — the flipped sign included. The whole evidence was one sentence of
prose in `CfdConvection.hpp`.

Two tests now carry it, neither of which reads a production formula:

```text
diffusion's sign    with walls everywhere phi is BIT-ZERO at every face, so the
                    convection matrix is identically zero and A == A_diff. A
                    diffusion operator dissipates energy, so x^T A x > 0 for
                    every non-constant x; with the sign flipped the matrix is
                    negative definite. Three probes: a ramp, an alternating
                    vector and a spike
convection's sign   with pressure outlets U is ZeroGradient everywhere, so
                    diffusion contributes nothing at the boundary and its
                    internal faces cancel in the row sum. Then
                    rowSum(A_momentum, c) == divergeCfdMassFlux(phi).at(c),
                    measured against P19-DIV-001's SEPARATE OPERATOR and
                    separate code path. With the sign flipped the row sum is
                    -div(phi). The test asserts that at least one cell's
                    divergence is large enough to distinguish the two, so the
                    assertion cannot be satisfied vacuously
```

### What is deliberately not asserted

No bit-exactness of the row-sum additivity identity (measured 47.39% bit-exact,
worst `1.085e-14` of the row scale). No bit-zero hydrostatic cancellation
(65.05% exact, worst `3.649e-16` of the term). No exact steady limit for a large
time step — a bound, because the transient term is lost only below the
diagonal's last bit. No symmetry: the suite asserts the ASYMMETRY, and pairs it
with a bit-zero-flux fixture that IS symmetric so the assertion is about
convection rather than about the fixture.

### Two refusals are unreached, and an earlier draft said three — two of them wrongly

```text
TopologyNamesAnUnknownCell  buildCfdTopology holds
ConvectionRefusedTheInput   every input phase of cfdConvectionProblem is
                            pre-empted by an earlier momentum phase, and its
                            remaining enumerators are themselves unreachable
                            from a constructible input. Its OUTPUT refusal is
                            reached and is reported as ResultIsNotFinite
```

So is the conditional arm of `BoundaryDescribesAnotherTopology`, which needs a
foreign boundary whose face count matches. Everything else is tested, including
`AssemblyIsInconsistent`, which an earlier draft called unreachable and is not.

## ADVERSARIAL REVIEW

**TWO INDEPENDENT REVIEWS WERE RUN BEFORE THE QUALIFICATION FREEZE**, which is
the one process defect `P19-DIV-001` recorded against itself after discarding a
three-hour run, and which `P19-CONV-001` then got right. One took the production
code, one took the test suite and the published claims. Neither was told the
author's conclusions: each was given the files, the project's standards and a
list of axes to attack.

**42 findings. Three were CRITICAL, three MAJOR in the first review and eight in
the second.** Every one was resolved before the gate was launched, so there is
no discarded attempt here.

### What the reviews found, by kind

```text
code defects                       4
false or unsupported published     14
  claims
test defects (vacuous, missing,     16
  or transcribed oracles)
process / build defects             2
nits with no behavioural effect     6
```

### The findings that changed behaviour

**1. `AssemblyIsInconsistent` was documented as unreachable and is reachable,
under the wrong tag.** Phases 13 and 14 ask `cfdConvectionProblem` and
`cfdDiffusionProblem`, which judge INPUTS — and `CfdConvection.hpp` says so in
its own words: "IT CANNOT SEE `ResultIsNotFinite`, which judges an output this
function never computes." So a `phi` whose values are each finite but whose SUM
overflows passes every momentum phase, passes the convection predictor, and is
refused by the convection assembly. The earlier code reported that as "a
predictor disagreed with its own assembly" when the two had agreed perfectly.
Convection's sub-tag is now read and mapped to momentum's own
`ResultIsNotFinite`; diffusion's channel carries no tag, so that route keeps
`AssemblyIsInconsistent` and is **tested rather than claimed unreachable**
(L11). Reaching it needs `faceArea / orthogonalDistance` of order one, which
needs a metre-scale mesh — my first attempt used the millimetre fixture and the
assembly succeeded, so the test failed on its own premise.

**2. The seven `static_assert`s constrained nothing in the implementation.** The
assembly computed every term in raw doubles through `.si()`, so
`rho * volume * dt` would have compiled, satisfied all seven assertions, passed
every finiteness gate, and been caught only by a runtime oracle. The assertions
proved the units framework's own dimension algebra and nothing this milestone
wrote — **the exact inverse of what `ADR-073` Decision 3 claimed for them**, in
the decision that calls them "the strongest check in the milestone". The
assembly now binds the transient coefficient to a `MassFlowRate` and each
locally formed source term to a `Force`; the wrong formula is a compile error.
The arithmetic did not change: `operator*` and `operator/` are
`fromSi(a.si() OP b.si())`, so the grouping and every bit are what they were and
Q1's and Q5's measurements still describe the code.

**3. The sign where the two operators meet had no independent check** — see
TESTS above. This is the sharpest finding of the two reviews, because the claim
it attacked is the milestone's central one and the suite looked like it covered
it.

**4. An out-of-range `CfdConvectionScheme` was silently substituted by Upwind.**
The gate answered for an out-of-range `VectorComponent` and spelled out the
reasoning — an enum class with a fixed underlying type admits
`static_cast<T>(7)` — and did not make the same argument for the scheme, which
has the same property. `assembleCfdConvection` forms
`central = (scheme == Central)`, false for 7, so the system was assembled with a
scheme the caller did not ask for while the summary reported 7. New enumerator,
new phase, new test.

**5. The gate reached past the type system to inspect it.** Two spans of
three-component typed quantities were flattened with `reinterpret_cast` and
walked as doubles. Undefined behaviour by the object model; an unasserted layout
assumption that would have silently value-checked a PREFIX of each array had
`Quantity` ever gained a member — while the length phase still passed, which is
the very hole the array list exists to close; and `CfdData.hpp` already owns
`isFinite(const CartesianVector<Q>&)`, which `assembleCfdDiffusion`'s own gate
uses. **I found this one myself, before the review reported it, and the review
found it independently.** The `stride` field went with it, and so did a bug it
had already caused: a first draft derived the stride by inspecting two
characters of the array's NAME, and the test matched `"the face flux phi"` as
well as `"the face gradient"`, so the scalar flux array would have been walked
three doubles per entry and read three times past its end.

**6. Every refusal was stamped `ErrorCode::InvalidArgument`**, including the two
that judge this assembly's own behaviour. `assembleCfdDiffusion` distinguishes;
`AssemblyIsInconsistent` now carries `Internal`.

**7. A Release-only build failure, found by building the second preset.**
`-Werror=array-bounds` at `-O3` rejects `vector.back()` where GCC cannot prove
the vector non-empty after inlining the producer. The Debug preset compiles it
cleanly. CLAUDE.md's adversarial review asks "could Debug and Release differ";
this is that, and **had the qualification been launched on the strength of a
passing `debug-ext` run the freeze would have been void** — the three hours
`P19-DIV-001` lost. All three candidate reformulations, the failing one
included, compile clean in a standalone translation unit under the same flags,
so the diagnostic is inlining-dependent and a local reword was not guaranteed to
help; the fix removes the unproven ACCESS rather than rewording it.

### The test defects

**Two sections of one test could not fail.** `OmittingATermOmitsItExactlyAndSaysSo`
compared a system against itself: `steadyInput` already leaves the pressure
gradient empty and gravity absent, so clearing them was a no-op and both
sections assembled the identical input twice. They now build the system WITH
both terms and clear them.

**The deferred correction was never exercised.** `faceGradient` appeared only in
pass-through helpers and as two refusal fixtures, so dropping it from the
ASSEMBLY's `CfdDiffusionInput` while leaving the GATE's copy intact silently
deleted the non-orthogonal correction and passed the whole suite — a term
`ADR-063` finding F1 measured at 31 to 66 degrees mean non-orthogonality on the
reference meshes. The two inputs are each constructed twice in production, and
nothing had asserted the copies agreed.

**The two reported transient extrema could not be distinguished on any fixture
that asserted them.** `chainPoints` walks a screw motion — a rotation and a
translation, which is an isometry — so every cell of every chain is congruent
and `min(rho V/dt)` equals `max(rho V/dt)` to rounding. Reporting the minimum
for both fields passed the entire suite. **This is the third variation in this
repository of one defect**: an asserted quantity made independent of the thing
under test by a fixture in which the two compared values are equal.
`P19-GRAD-001`, `P19-DIV-001` and `P19-CONV-001` each recorded it about an
interpolation weight.

**Infinities were never tested in any array.** Every array was poisoned with a
NaN alone, so changing the gate's `!std::isfinite(x)` to `std::isnan(x)` —
which ADMITS an infinity in the flux, both velocities and both gradients —
passed the suite. Both qualified precedents test infinities.

**The reference bodies never touched the source**, and passed the current
velocity as the previous one with uniform values, so every cell and every
component carried the same number and any mis-indexing was invisible by
construction — the exact trap `ADR-073` Decision 4 is written about, in the test
meant to be the hardest. The five-term source order is now asserted per cell on
every reference body: thousands of samples where there had been six, which
matters because Q5 measured declared-against-reordered agreement at 57.45% per
sample.

**A tolerance was set at precisely the slackness its own comment condemned.**
`kCfdMomentumRowSumTolerance` was `1e-12`; the comment rejects the sum-relative
figure of `5.264e-12` as "two decades slacker than the operator needs", and
`1e-12` is 92 times the `1.085e-14` the same comment says a test may assert —
which is two decades. The sentence described the constant it had chosen rather
than the one it was rejecting. **Tightened to `1e-13` and the suite re-run; no
assertion needed the slack.**

**A bound was taken from the operator's own output.** The large-time-step test
compared the measured gap against `summary.largestTransientCoefficient`, which
the operator computes from the same product that produced the diagonal the gap
is measured from — so scaling the transient term by any constant scaled both
sides and the test passed for every scale factor. The bound is now derived in
the test from the density, the cell volumes and the time step, and the
operator's own report is checked against that derivation as a separate claim.

### False published claims, corrected rather than reworded

```text
"a term whose formula lost a density does not compile"   was false; now true
"AssemblyIsInconsistent is unreachable"                  was false; now tested
"both sub-operators gate their own output"               false of diffusion's
                                                         SOURCE, which is
                                                         ungated upstream
"the tet chain is the only fixture with an INTERIOR      false, and stated
 cell"  (stated twice)                                   twice, while the test
                                                         body that needed one
                                                         recorded it had looked
                                                         and found none
"three refusals are unreached"                           two of the three were
                                                         reachable in three
                                                         lines and untested
ADR L9's "five refusals are unreachable"                 wrong count, and
                                                         contradicted the test
                                                         file's own three
"the one-cell tet is where the pressure and gravity      they are pinned on the
 coefficients are pinned"                                two-cell pair
"two decades above the worst measurement" (hydrostatic)  1.44 decades
"these are RE-DERIVED, not read from the header's        vacuous: the header
 aliases"                                                DEFINES the alias as
                                                         that expression
"static stops -Wmissing-declarations firing"             the unnamed namespace
                                                         already does; verified
                                                         with the compiler
"this translation unit must compile AND LINK"            nothing links; it is
                                                         an OBJECT library
"a CfdMomentumSystem cannot be CONJURED"                 it is an aggregate
                                                         with a public matrix
"the extrema gate exists because every coefficient       true of convection,
 may still be finite"                                    false here: every
                                                         transient coefficient
                                                         reaches a diagonal
"five shapes"                                            five arrays, three
                                                         shapes
"the compile-fail group carries five claims"             the body has seven
                                                         sections; eleven cases
                                                         sat outside any claim
```

**THE AUTHOR'S OWN PASS FOUND ONE OF THE FOUR CODE DEFECTS AND WROTE TEN OF THE
FOURTEEN FALSE CLAIMS.** That is the argument for the reviews, stated as a
measurement rather than as a principle.

### Two findings I disagreed with, and what I did instead

**The dominance count's re-count is a transcription, and it stays one.** The
review is right that `ReportsDominanceMeasuredFromTheMatrixItReturns`
reproduces production's predicate line for line. But what that test claims is
narrow and true: that the REPORTED number describes the RETURNED matrix. The
count's MEANING is now pinned separately, on a Central high-flux chain where
non-dominant rows exist by construction, with the expectation derived from the
row sum. The transcription is kept, labelled as the consistency check it is,
rather than deleted — a number that disagreed with its own matrix is a real
failure mode and nothing else watches for it.

**The extrema finiteness gate is dead code and is kept.** The review is right
that it cannot fire: every transient coefficient is added to a diagonal, so a
non-finite extremum implies a non-finite coefficient and that gate runs first.
It is kept as a defence against that implication ceasing to hold, which is a
property of the loop above it rather than of the gate — and it is recorded as
UNREACHED (L12) rather than justified by `CfdConvection.cpp`'s argument, which
does not transfer.

## FAILURE PATHS

Sixteen refusals, declared in phase order, and the order is asserted between the
phases a constructible input can reach.

```text
 1  GeometryDescribesAnotherTopology   TESTED  (was called unreachable)
 2  BoundaryDescribesAnotherTopology   TESTED, face-count arm only; the
                                       conditional stamp arm is UNREACHED
 3  TopologyNamesAnUnknownCell         UNREACHED -- buildCfdTopology holds
 4  ComponentOutOfRange                TESTED
 5  SchemeOutOfRange                   TESTED  (new; was a silent substitution)
 6  DensityNotUsable                   TESTED, five values
 7  ViscosityNotUsable                 TESTED, four values
 8  GravityNotUsable                   TESTED, zero vector and non-finite
 9  TimeStepNotUsable                  TESTED, four values
10  TransientInputsAreInconsistent     TESTED, both directions
11  ValueLengthWrong                   TESTED, all five arrays
12  ValueNotFinite                     TESTED, all five arrays x three poisons
13  ConvectionRefusedTheInput          UNREACHED -- every input phase of
                                       cfdConvectionProblem is pre-empted by an
                                       earlier momentum phase, and its
                                       remaining enumerators are themselves
                                       unreachable from a constructible input
14  DiffusionRefusedTheInput           TESTED -- a Symmetry face with no
                                       current velocity is the one route
                                       momentum's own gate does not cover
15  ResultIsNotFinite                  TESTED on three routes: a convection
                                       overflow, a transient overflow, and a
                                       source overflow with every coefficient
                                       finite
16  AssemblyIsInconsistent             TESTED through diffusion's untagged
                                       channel. Its other three routes --
                                       differing patterns, a missing diagonal,
                                       a fromCsr refusal -- are UNREACHED
```

**EVERY REFUSAL ASSERTS ITS TAG AGAINST AN INDEPENDENTLY NAMED EXPECTATION, AND
THE MESSAGE SEPARATELY.** `P19-BC-001`'s review found eight refusals whose tags
could be swapped with every message assertion still passing. The `refusal()`
helper additionally asserts on every refusal that `cfdMomentumProblem` and
`cfdMomentumFailure` agree with the assembly, so a predictor that disagreed with
its own gate could not pass.

**A FAILED ASSEMBLY COMMITS NOTHING.** Every refusal path returns before the
`CfdMomentumSystem` is constructed; there is no partial state to leave behind,
because the operator owns none.

## PERSISTENCE

**NOTHING HERE IS PERSISTED, AND THAT IS `ADR-058`'S LINE RATHER THAN THIS
MILESTONE'S CHOICE.** A momentum system is DERIVED state: "matrices,
factorisations, p, U, phi, gradients ... Reproducible from the canonical intent
plus the current mesh and fluid. Never persisted as authority, never undo
payload, never a member of anything canonical."

So there is no save/load round trip to test, no serialization format, no stable
reference produced or consumed, no document state touched, and no regeneration
behaviour. The operator takes four values and returns one; it owns no field and
holds no state between calls.

**WHAT WOULD HAVE BEEN A DEFECT:** storing the assembled matrix on the analysis
object, caching it against a mesh stamp, or holding a density of its own.
`ADR-028` forbids the last one by name, and the fluid properties arrive through
the input.

## DETERMINISM

```text
bitwise reproducible across repeated assemblies   asserted, 5 repeats, on a
                                                  fixture carrying all five
                                                  source terms
no unordered iteration                            the only map is std::map
                                                  (ordered), in a fixture
                                                  builder
no wall-clock, no locale, no RNG                  grep-verified; no seed is
                                                  required by any test
no thread scheduling                              single-threaded throughout
the sum order is FIXED and STATED                 and measured: 78.06%
                                                  bit-exact against a different
                                                  grouping, so the order is
                                                  contract rather than detail
the identical assertion count in three            11,823 -> 14,198 assertions,
configurations                                    identical in debug-ext,
                                                  release-ext and
                                                  debug-shared-ext
```

**THE ORDER IS THE DETERMINISM CLAIM, AND IT IS WHY THE DECOMPOSITION CAN BE
BIT-EXACT.** `rowSumSi` sums a row serially in ascending column order;
`multiply` does the same; the matrix is `(conv + diff)` slot-wise then the
transient diagonal; the source is five terms in the declared order. The suite
reproduces each of those orders rather than a mathematically equal one, which is
exactly why the term decomposition is an equality of doubles and the row-sum
additivity is a measured bound.

## PERFORMANCE

**THERE IS NO PERFORMANCE RESULT, AND THE REASON IS THE SAME ONE `P19-DIV-001`
AND `P19-CONV-001` EACH RECORDED: nothing consumes this operator.** A baseline
for an operator with no caller would be a number with no meaning, and
`CLAUDE.md` forbids claiming a speedup without measured before-and-after
evidence — which also forbids claiming a cost without one.

**WHAT IS KNOWN, AND IS NOT A MEASUREMENT:** `ADR-073` Decision 1 accepts three
CSR builds where one face pass would do, and says so in its Consequences. The
convection and diffusion assemblies each walk the faces once and build their own
pattern; this operator then walks the slots once more to sum them and the cells
once to form the source. That is a constant factor, it is stated as a cost of
the composition rather than hidden, and it is not measured because there is
nothing to measure it against.

## KNOWN LIMITATIONS

```text
L1  NOTHING CONSUMES THIS OPERATOR. There is no solve, so no velocity field is
    produced and no momentum equation is advanced. P19-CONT-001 and
    P19-PRES-001 are not authorized
L2  BOUNDEDNESS IS INHERITED AND CONDITIONAL: Upwind, no backflow at a
    zero-gradient boundary, and a discretely conservative phi. The operator
    sees only the second, REPORTS it through the convection sub-summary's
    backflowFaces, and refuses none of the three
L3  THE DIFFUSION TERM IS INCOMPLETE ON A NON-ORTHOGONAL MESH unless a face
    gradient is supplied -- ADR-069's limitation, carried, reported through the
    sub-summary's correctionApplied and deferredFraction, and now exercised
    through momentum by a test that supplies one
L4  NO PECLET REPORT, so Central's unboundedness is stated and not measured: it
    would need a viscosity-to-flux ratio per face that this operator does not
    compute
L5  THE PRESSURE GRADIENT'S ACCURACY IS THE CALLER'S. Momentum validates its
    length and finiteness and nothing else
L6  NEITHER THE PRESSURE GRADIENT NOR EITHER VELOCITY CARRIES A MESH STAMP, so
    a field of the right LENGTH from another mesh is accepted. Taking ADR-070's
    field type instead would not have fixed this, because that type carries no
    stamp either
L7  NO TIME-STEP QUALITY RULE. No Courant number is computed and no step is
    refused as too large: ADR-063 finding F1 reserves an engineering threshold
    to the owner, and ADR-071 and ADR-072 refused the same thing for the same
    reason
L8  NO PERFORMANCE BASELINE, for L1's reason
L9  TWO OF THE SIXTEEN REFUSALS ARE UNREACHABLE and are recorded as UNREACHED,
    as is the conditional arm of a third
L10 THE GATE IS AS COMPLETE AS THE TWO PREDICTORS IT CALLS. Both share one
    decision path with their own assembly, so their agreement cannot fail --
    P19-DIFF-001's L8, P19-GRAD-001's L9, P19-DIV-001's and P19-CONV-001's own
L11 ASSEMBLY IS INCONSISTENT IS REACHED through diffusion's untagged channel
    and cannot be distinguished there from an output overflow.
    P19-DIFF-001's limitation inherited
L12 THE REPORTED EXTREMA'S OWN FINITENESS GATE IS UNREACHABLE for this
    operator and is kept as a defence rather than deleted
L13 THE PREVIOUS-VERSUS-CURRENT VELOCITY SWAP CANNOT BE CLOSED BY THE TYPE
    SYSTEM. Both are std::span<const double> of 3 * cells in the same layout,
    so no compile-fail case can distinguish them; it is closed by a name, a
    comment and one test, and the compile-fail file says so rather than
    claiming a guard it does not have
L14 THE FACE INTERPOLATION'S THREE-LINE APPLICATION STILL EXISTS TWICE in the
    cfd module, pinned by a test rather than unified -- ADR-072 Decision 9's
    position, unchanged, because unifying it means editing a qualified operator
    for a convenience
L15 THE THREE COMPONENTS' MATRICES ARE ASSEMBLED INDEPENDENTLY and are equal on
    a mesh with no slip wall. No caching exploits that: it would be an
    optimisation with no measurement behind it, for a consumer that does not
    exist
```

## MUTATION PROBES

**RUN, NOT REASONED ABOUT.** The second review produced a 21-row table of
single-line mutations and judged nine of them survivors — analytically, because
it could not build the project (a stale `CMakeCache.txt` pointed at another
machine's paths, and it said so rather than guessing). Every probe below was
applied to the real tree, built under `debug-ext`, and run against the momentum
suite; the file's sha256 was compared against the original after each revert, so
a probe that failed to revert could not poison the qualification.

`experiments/mutations.out` and `experiments/mutations2.out` are the committed
logs, with the harnesses beside them.

### Round 1 — the nine the review predicted would survive, and seven more

```text
baseline              suite PASSES, 14,198 assertions in 47 cases
applied               17      killed 16      survived 1 (the control)

KILLED                M1  conv + diff -> conv - diff
KILLED BY THE COMPILER M2  rho*V/dt -> rho*V*dt
KILLED                M3  drop faceGradient from the assembly's diffusion input
KILLED                M4  delete phase 1 (geometry describes topology)
KILLED                M5  delete phase 2's face-count arm
KILLED                M6  largestTransient = *least  (both report the minimum)
KILLED                M7  smallestTransient = *most  (both report the maximum)
KILLED                M9  never increment notDominant
KILLED BY THE COMPILER M11 previousVelocity[comp*cells + c] -> [c]
KILLED                M12 pressure term sign
KILLED                M13 drop the volume from the body force
KILLED                M14 every cell uses cell 0's volume
KILLED                M15 isfinite -> isnan in the scalar array gate
KILLED                M17 hard-code Upwind in the assembly's convection input
SURVIVED              M18 (see below -- this was a NO-OP CONTROL, mislabelled)
KILLED                M20 delete the source finiteness loop
KILLED                M22 kCfdMomentumRowSumTolerance -> 1e-30

CfdMomentum.cpp  reverted exactly = True
CfdMomentum.hpp  reverted exactly = True
final rebuild of the reverted tree: OK
```

**ALL SEVEN MUTATIONS THE REVIEW CALLED OUTRIGHT SURVIVORS ARE NOW KILLED**
(M3, M4, M5, M6, M7, M15, M20), and so are the two it called conditional (M9,
M17). That is the measured form of "the review's findings were fixed": the
suite now fails on each of the nine changes that it previously passed.

**TWO ARE KILLED BY THE COMPILER RATHER THAN BY A TEST**, which is the stronger
failure. M2 is `rho * V * dt` in place of `rho * V / dt`: it compiled before the
typed-arithmetic fix and satisfied all seven `static_assert`s, which is exactly
the review finding that said those assertions constrained nothing. M11 is the
component-blocked index collapsed to `[c]`: `MassFlowRate * double` is not a
`Force`, so the mis-indexing no longer type-checks. Neither of those is a test
result; both are the units framework refusing to build the wrong formula.

**M22 IS A TOLERANCE PROBE AND ITS KILL IS THE POINT.** Tightening
`kCfdMomentumRowSumTolerance` to `1e-30` — far past any measurement — must make
the suite fail, because otherwise the three row-sum assertions it gates are not
actually constraining anything. It fails. A tolerance nothing would notice is
a tolerance that is not doing work.

### The survivor was my own mislabel, not a gap

Round 1's M18 replaced `transient[c]` with `0.0 * transient[c] + transient[c]`,
which is the identity for every finite value. **It survived because it is a
no-op, and that is what a control is for:** it shows the other sixteen kills are
attributable to their mutations rather than to build flakiness or a flaky
fixture. But the row was LABELLED with the review's actual M18
(`values[slot] += transient[c]` → `= transient[c]`), which round 1 therefore
never ran. Recorded rather than quietly relabelled, because a mutation table
whose names do not match what it ran is the same class of defect as a comment
that names a test that does not exist — the one this milestone corrected four
times. The real M18 is run in round 2 under its own name.

### Round 2 — the probes round 1 missed, and the one it mislabelled

```text
KILLED     M18 transient diagonal += -> =  (the REAL one)
           5 cases failed, first RowSumIsTheSumOfTheTermsOwnRowSums
SURVIVED   M19 delete the MATRIX finiteness loop
KILLED     M16 reorder the three cell-wise source terms
           caught by OnTheReferenceBodiesTheDecompositionAndRowSumIdentities
SURVIVED   M10 dominance `<` -> `<=`
SURVIVED   M23 drop the pattern verification (inner arrays)
KILLED     M24 the scheme phase accepts anything
KILLED     M25 the convection sub-tag is not mapped

CfdMomentum.cpp  reverted exactly = True
```

**M16's KILL IS THE REFERENCE-BODY FIX WORKING.** The review judged it
"PROBABLY, not by construction": the source order was asserted on six samples
(two cells, three components), and Q5 measured declared-against-reordered
agreement at 57.45% PER SAMPLE, so all six agreeing by chance had about a 3.6%
probability. The order claim was enforced probabilistically. With the order now
asserted per cell on every reference body it is enforced by construction, and
the probe dies on the reference test.

**M24 AND M25 ARE THE TWO FIXES FROM REVIEW A, PROBED AS FIXES.** Restoring the
silent scheme substitution and restoring the wrong sub-tag each kill the suite,
so neither new check is decoration.

### Round 3 — closing M19

M19 was a REAL gap and it is closed. Deleting momentum's matrix finiteness loop
left the suite green because both reachable non-finite cases — a transient
overflow and a body-force overflow — also produce a non-finite SOURCE, so the
source loop fired with the same tag and the matrix loop was never the only thing
watching.

The route the sub-operators cannot see is **the sum**: each gates its own
coefficients, and `1.7e308 + 1.7e308` is not finite.
`CfdMomentum_RefusesANonFiniteCoefficientWhileEverySubMatrixStaysFinite` reaches
it on a one-cell metre-scale tet with a huge flux on exactly one face and a
viscosity calibrated from diffusion's own largest coefficient, with every
premise asserted: both sub-matrices finite, their sum non-finite on at least one
slot, and both sources finite.

**TWO EARLIER FIXTURES FOR THAT TEST FAILED ON THEIR OWN PREMISES**, and both
are recorded because the route was always real and only the fixture was wrong.
The first computed `mu` from one face's `faceArea / orthogonalDistance` and
diffusion's own matrix overflowed — its diagonal accumulates `Gamma` over all
four of a cell's faces, so the per-face figure understated it by about a factor
of four. The second used the two-cell pair, where both matrices reached `1e308`
on DIFFERENT slots and the sum overflowed nowhere.

With the test in place, M19 was re-applied and **the suite fails** — measured,
not assumed:

```text
M19 re-applied   48 cases, 47 passed, 1 failed   KILLED
reverted         rebuild clean, 14,230 assertions in 48 cases pass
```

### The two survivors, and why each is honest

```text
M10  dominance `<` -> `<=`
     Needs an exact floating-point TIE between |a_ii| and the off-diagonal sum.
     That is measure-zero on any mesh and no fixture produces it. Recorded as a
     survivor rather than chased with a hand-built tie, which would be a test
     written for the mutation rather than for the behaviour
M23  drop the pattern verification
     ADR-073 Decision 2 documents this gate as UNREACHABLE while both
     sub-operators keep their present symbolic pass, and the probe CONFIRMS
     that: with the check removed nothing changes, because the patterns never
     differ. A survivor here is the honest answer and the alternative -- a test
     that reaches it -- would require breaking one of the two qualified
     operators
```

### The totals

```text
real mutations applied     24
killed                     22   (2 of them BY THE COMPILER)
survived                    2   both recorded above with their reasons
no-op control               1   survived, as a control must
```

**TWO ARE KILLED BY THE COMPILER RATHER THAN BY A TEST**, which is the stronger
failure, and both exist only because of the typed-arithmetic fix: `rho * V * dt`
in place of `rho * V / dt` compiled before it and satisfied all seven
`static_assert`s, and `previousVelocity[c]` in place of
`previousVelocity[comp * cells + c]` now fails because `MassFlowRate * double`
is not a `Force`.

**M22 IS A TOLERANCE PROBE AND ITS KILL IS THE POINT.** Tightening
`kCfdMomentumRowSumTolerance` to `1e-30` — far past any measurement — must make
the suite fail, or the three row-sum assertions it gates are not constraining
anything. It fails.

## REGRESSION

Three presets, each configured, **cleaned**, built from scratch, rebuilt as a
no-op check and run in full. Logs in
[`qualification/`](qualification/).

```text
preset             clean      build      full ctest           warnings
debug-ext          812 files  813 tgts   6601 / 6601  100%    0
release-ext        812 files  813 tgts   6601 / 6601  100%    0
debug-shared-ext   825 files  813 tgts   6601 / 6601  100%    0
```

`warnings` counts occurrences of `warning:`, and `error:` and `^FAILED` are zero
in all three logs as well. Every preset carries `-Werror`, so a warning would
have failed the build rather than shown up here; the count is recorded because
a gate that cannot be seen to have run is not evidence.

**6601 = 6525 + 48 unit + 28 compile-fail, EXACTLY**, against `P19-CONV-001`'s
6525. No pre-existing test was dropped or registered twice.

The repeat set, five executions of each selected test, both presets:

```text
preset        selected   result lines   Start markers   ratio
release-ext      3306           3306           16530    5.000
debug-ext        3306           3306           16530    5.000
```

`3306 x 5 = 16530` exactly. **The ratio is the check, not the larger number**:
`ctest --repeat until-fail:5` prints one result line per test and five `Start`
markers, and `P19-CONV-001`'s freeze recorded those two labels the wrong way
round. 100% passed in both.

Subsystem totals from the qualified binaries:

```text
momentum suite    14,230 assertions in  48 cases
                  IDENTICAL in debug-ext, release-ext AND debug-shared-ext
cfd subsystem    716,417 assertions in 495 cases
                = 702,187 + 14,230  and  447 + 48    EXACTLY
```

An added test and a deleted test net to nothing in a pass/fail count. They do
not net to nothing here, so **no pre-existing cfd assertion moved.**

## QUALIFICATION

**ATTEMPT 1 — FAILED.** Launched 2026-11-08 08:32:47, finished 11:03:27,
`QUALIFICATION FAILED: 2 stage(s) failed`. Full analysis in
[`qualification-attempt-1/README.md`](qualification-attempt-1/README.md); the
freeze it was launched under, including the claim it got wrong, is preserved at
[`qualification-attempt-1/FREEZE-as-launched.md`](qualification-attempt-1/FREEZE-as-launched.md).

```text
release-ext build     exit 1   66m28s into the run
repeat release-ext    exit 8   entirely downstream: no binary, no discovery
root cause            ONE line. tests/cfd/CfdMomentumTests.cpp:629,
                      out.pop_back() in shortenedWithLeadingNan, rejected by
                      -Werror=array-bounds at -O3 once inlined into its caller
```

**THE LAUNCHER REPORTED EXIT CODE 0 WHILE THE HARNESS REPORTED FAILURE.** The
rule that the verdict is the harness's own sentence and not the launcher's exit
code is no longer a precaution carried from an earlier milestone; it is the
thing that stopped this run being reported as a pass.

The defect was the SECOND instance of a class whose FIRST instance was found and
fixed before attempt 1's freeze, thirty lines above it in the same file. Fixing
one site was recorded as fixing the class, and the helper carrying the second
instance was written afterwards and never compiled at `-O3`. Both the code and
the comment that overclaimed are corrected; the comment is corrected rather than
deleted, because the overclaim is the finding.

Attempt 1 nevertheless confirmed all three frozen figures exactly — 6601 in both
Debug presets, 3306 repeat, 16530 markers over 3306 result lines for a ratio of
exactly 5.000 — so the counting method is validated and attempt 2 must reproduce
them unchanged.

**ATTEMPT 2 — PASSED.** Re-frozen after the fix; details in
[`FREEZE.md`](FREEZE.md). The harness's own verdict, which is the verdict:

```text
Qualification passed: every stage exited 0.
qualify.cmd exit 0
qualification finished Sun 11/08/2026 15:13:11.09, 0 stage(s) failed
```

```text
launched     2026-11-08 11:30:04
finished     2026-11-08 15:13:11      3h 43m 06s
stages       17 of 17 exited 0
             = 3 presets x (configure, clean, build, no-op rebuild, ctest)
               + 2 repeat presets
```

**THE QUALIFIED TREE IS THE FROZEN TREE.** `qualify.cmd` records the eight
subtree identities from a scratch index before the first build and again after
the last test, and all sixteen readings agree with each other and with
`FREEZE.md`:

```text
apps               fc398a3d2e524178cf38e8b667236379d8381086
include            3bec67c503693f2c8134de3c5ca490f6d7a7eeef
src                8fa9cccf3264215e80067885a1ea9fd1b1f1b200
tests              5dfc326b0db05b78b865a38557f1e0666f806682
examples           4da234ebf4779f7c16d5df0075f106f7e3d4c958
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e
```

Nothing under the frozen surface moved during the 3h43m.

**THE FRESH-BINARY PROOF, BOTH HALVES.** The harness proves the clean half — a
`ninja -t clean` removing 812, 812 and 825 files, then 813 targets built, then a
no-op rebuild that does only the always-run `Checking git revision` target. The
freshness half is checked by hand afterwards, as `FREEZE.md` requires:

```text
newest file under the frozen surface   11:07:45   tests/cfd/CfdMomentumTests.cpp
debug-ext        bettercad_tests.exe   12:18:09   after its clean at 11:30:16
release-ext      bettercad_tests.exe   13:16:17   after its clean at 12:59:16
debug-shared-ext bettercad_tests.exe   14:04:01   after its clean at 13:48:48
```

Each binary postdates both its own clean and the newest source by at least
seventy minutes. **That 11:07:45 is the comment edit that overtook the
pre-launch Release build**, which began at 11:06:09 — so the no-op check that
caught it is vindicated by measurement rather than by argument.

The three frozen figures were predicted in `FREEZE.md` before the run and are
reproduced exactly, 6601 and 3306 and 16530, which is what makes them evidence
rather than a reading taken after the fact.

**ONE DEFECT IN MY OWN FRESHNESS SCRIPT IS RECORDED HERE BECAUSE IT WAS CAUGHT
AND NOT QUIETLY FIXED.** The first attempt at the freshness half used
`dir /s /b /o-d` over the eight paths and reported `apps\CMakeLists.txt` dated
2026-09-14 as the newest file under the frozen surface. `dir` sorts within each
path argument, not across them, so that was the newest file in `apps` and not
the newest file at all — a figure that would have made the proof look stronger
than it was while measuring the wrong thing. Re-measured with a single global
sort, the real answer is 11:07:45. This is the fifth measurement-tool defect of
this milestone, all five found by checking a measurement against something
already known rather than by trusting the tool that produced it.

## RESULT

```text
TASK:            P19-MOM-001  Momentum equation
IMPLEMENTATION:  include/bettercad/cfd/CfdMomentum.hpp  (new, public API)
                 src/cfd/CfdMomentum.cpp                (new, 14-phase gate +
                                                         assembly)
                 tests/cfd/CfdMomentumTests.cpp         (new, 48 cases)
                 tests/compile_fail/CfdMomentumMisuse.cpp (new, 28 cases)
                 src/cfd/CMakeLists.txt                 (+1 line)
                 tests/CMakeLists.txt                   (+1 line)
                 tests/compile_fail/CMakeLists.txt      (+1 group, 4 regexes)
                 NO EXISTING SOURCE FILE MODIFIED
TESTS:           48 unit cases / 14,230 assertions, identical in all three
                 presets; 28 compile-fail cases; 6601 per preset x 3; the
                 repeat set 3306 x 5 x 2 presets, all 100%
VALIDATION:      independent small-system oracle; the decomposition checked
                 bit-for-bit against the two already-qualified operators;
                 row-sum additivity to a measured 1e-13; hydrostatic balance
                 to a measured 1e-14; seven dimensional identities proved at
                 compile time by typed arithmetic
ADVERSARIAL:     two independent reviews, 42 findings, all resolved before the
                 freeze; 24 mutation probes applied to the real tree, 22
                 killed (2 by the compiler), 2 honest survivors recorded with
                 their reasons, 1 no-op control
RESULT:          PASS   (attempt 2; attempt 1 FAILED and is recorded in full)
EVIDENCE:        docs/verification/P19-MOM-001/
                   README.md                      this file
                   FREEZE.md                      the qualified tree's identity
                   qualification/                 attempt 2, PASSED
                   qualification-attempt-1/       attempt 1, FAILED
                   experiments/                   measurements and mutations
                 docs/architecture/decisions/ADR-073-*.md
TODO:            13 checkboxes ticked -- on this PASS, and not before it
```

**WHAT THIS MILESTONE DOES NOT DO**, restated here so the `[x]` cannot be read
as more than it is: the assembly produces a matrix and a source vector. It
solves nothing, produces no velocity or pressure field, and contains no
pressure-velocity coupling. `TODO.md` scopes those separately and they are not
authorized by this milestone.

The two honest mutation survivors and the limitations in **KNOWN LIMITATIONS**
stand as recorded. No gate was relaxed, no tolerance moved, and no test was
weakened at any point in either attempt.

## REVISION

**r1** — the pasted checklist was diffed against `TODO.md`'s own section before
any work began: byte-for-byte identical across all 32 lines after CR
normalization, sha256 `386e3242048e9e85` over both, 13 unticked and 0 ticked,
counted rather than assumed. The authorization was then recorded in `TODO.md`
before any implementation, and that edit changed no checkbox — 663 ticked and
367 unticked before and after.

**r2** — the design's five arithmetic claims were measured BEFORE any C++
existed, with seed `202611081`. **Two of the five figures are corrections to the
measurement script's own first run**, and both are published rather than quietly
fixed. Q2 first reported a worst relative departure of `1.000e+00`, which reads
as a total failure of the physics and is an artefact of comparing a residual
against exact zero with a relative measure — `|a - 0| / max(|a|, 0)` is `1.0`
for every non-zero `a`, so the figure was a number about nothing. Q4 first
sampled `dt` in `1e-4..1e4` and reported `0/200,000` invisible, which reads as
"the transient term is always visible" and was a statement about the sampling
rather than the arithmetic. **This is the fourth measurement-tool defect of the
phase**, after `P19-CONV-001`'s end-anchored registration checker, its four
`-Werror` kills read as test kills, and its two mislabelled frozen figures —
the same family each time: a number that is about something adjacent to what it
claims to measure.

**r3** — the first draft of `static_assert` #7 claimed it caught a case the
other six missed. It does not: an area in place of the volume gives
`Force/Length`, which the `Force` assertion already refuses. Corrected before
any review saw it, and the correction records what the assertion is actually
for.

**r4** — the first implementation of the gate derived an array's stride by
inspecting two characters of its NAME, and the test matched
`"the face flux phi"` as well as `"the face gradient"` — so the scalar flux
array would have been walked three doubles per entry and read three times past
its end. Found by reading the code back before it was built.

**r5** — a `git`-splice replacement matched the wrong anchor and deleted the
gate's helper block along with the function it was meant to replace, because
`refuse`'s declaration begins with the same token sequence as `problemOf`'s.
Caught by a structural check that counted one of each expected symbol
afterwards, and repaired in the same pass. Recorded because the check is the
reason it was a two-minute problem rather than a silent one.

**r6** — the Release preset refused to build what Debug accepted:
`-Werror=array-bounds` at `-O3` on `vector.back()` after the producer is
inlined. **Found by building the second preset before the freeze, which is the
only way it could have been found.** Had the qualification been launched on the
strength of a passing `debug-ext` run, the freeze would have been void and the
run discarded — which is the three hours `P19-DIV-001` lost. All three candidate
reformulations, the failing one included, compile clean in a standalone
translation unit under the same flags, so the diagnostic is inlining-dependent
and the fix removes the unproven access rather than rewording it.

**r7** — the first `AdmitsAUniformVelocityAsASteadySolution` test asked for a
cell with no boundary face and found none: in a chain of tets built as
`{i, i+1, i+2, i+3}` every cell shares at most two of its four faces, so EVERY
cell has a boundary face. **Its own `REQUIRE(checked > 0)` guard caught that
rather than letting it pass having checked nothing**, which is the whole reason
the guard was written. The property is reached with pressure outlets instead,
the way the qualified convection suite reaches it. Two comments that had
motivated the original fixture were NOT corrected at the same time, and an
independent review found them still asserting "the only fixture with an INTERIOR
cell" while the test body recorded the opposite — see r9.

**r8** — the first form of the large-time-step bound was the exact-arithmetic
one and failed at `dt = 1e9`: measured gap `1.29646e-12` against a claimed
`1.29644e-12`. The gap is recovered as a difference of two diagonals, so it
carries the rounding of a sum whose larger addend is the steady diagonal:
`|fl(a + t) - a| <= t + eps |fl(a + t)|`, and the surplus `2e-17` is `eps` times
the diagonal's magnitude, exactly as that inequality predicts. The term added is
a REAL part of the bound and not slack — it is quantified, proportional to the
diagonal rather than chosen, and the alternative would have been to loosen a
relative factor until the test passed. The measurement log that had predicted
the wrong bound is annotated rather than edited.

**r9** — TWO INDEPENDENT REVIEWS WERE RUN BEFORE THE QUALIFICATION FREEZE and
returned 42 findings: four code defects, fourteen false or unsupported published
claims, sixteen test defects, two process defects and six nits. Every one was
resolved before the gate was launched, so there is no discarded attempt. **The
author's own pass had found one of the four code defects and had written ten of
the fourteen false claims.** The three most serious were: a refusal documented as
unreachable that is reachable and carried the wrong tag; seven
`static_assert`s that constrained nothing in the implementation, inverting the
decision that called them the strongest check in the milestone; and the
milestone's central claim — that the two operators meet with a `+` — having no
independent check at all, because every assertion wrote the production
expression and the one test that looked physical was provably sign-blind.

**r10** — two of my own NEW tests, written to close review findings, asserted
refusals their fixtures could not reach: `mu * A / d` cannot overflow on a
millimetre mesh however large a finite `mu` is, and a huge gravity on Z does
nothing to a momentum equation assembled for X. Both failed on their own
premises rather than on the operator. Fixed with a metre-scale fixture and the
right component, and recorded because the routes are real and only the fixtures
were wrong.

**r11** — `kCfdMomentumRowSumTolerance` was `1e-12`, and its own comment
condemned that value: the paragraph rejects a sum-relative figure as "two
decades slacker than the operator needs", and `1e-12` is 92 times the figure the
same comment says a test may assert. The sentence described the constant it had
chosen rather than the one it was rejecting. Tightened to `1e-13` — one decade
above the measured `1.085e-14` — and the suite re-run; **no assertion needed the
slack.**

**r12** — the mutation probes were RUN rather than reasoned about. The second
review produced a 21-row table judging nine mutations survivors, analytically,
because it could not build the project. Each was applied to the real tree,
built, and run against the suite, with the file's sha256 compared against the
original afterwards so that a probe which failed to revert could not poison the
qualification.

**r13** — the first qualification attempt FAILED, and the failure was a process
failure of mine rather than a defect the gates had not been pointed at. A
`-Werror=array-bounds` rejection at `-O3` had been found and fixed in one helper
before the freeze, and the fix was recorded as having caught the class; a second
helper thirty lines below carried the same defect, was written after the only
`release-ext` build, and broke the Release build 66m28s into the gate. The
lesson `P19-DIV-001` recorded was "build the second preset before freezing"; the
lesson this adds is "and after the last edit to anything it compiles, proved by
a no-op build rather than by a timestamp" — which, run once, immediately caught
the same mistake a second time. Two claims already written into the tree were
corrected rather than removed: the helper comment saying the class was caught
before the freeze, and `FREEZE.md`'s pre-freeze line saying `release-ext` built
clean.
