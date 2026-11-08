# ADR-073 — Momentum is the sum of qualified operators, the terms stay separable by construction, and no stabilization is added

- Status: Accepted
- Date: 2026-11-08
- Milestone: P19-MOM-001 — Momentum Equation
- Supersedes: nothing
- Amends: nothing. `ADR-069` Decision 1's positive diffusion diagonal and
  `ADR-072` Decision 3's positive convection diagonal are **relied on** here
  and restated, not changed. `CfdConvection.hpp` already states the
  consequence — "In the momentum equation both sit on the left, so
  `A_conv + A_diff` is the operator and no sign is flipped at the meeting
  point" — and names this milestone as the one that needs it.

## Context

`P19-MOM-001` is authorized with a thirteen-item checklist, pasted into the
instruction and byte-for-byte identical to the one already in `TODO.md`
(sha256 `386e3242048e9e85` over both after CR normalization, 13 unticked and
0 ticked, counted). It asks for the incompressible momentum equations

```text
rho (dU/dt + div(U U)) = -grad p + mu lap U + rho g
```

assembled "within v1 scope", with a steady form, a transient term, four named
contributions, boundary contributions, a sparse global matrix, deterministic
assembly, no hidden stabilization, an independent small-system oracle, a
regression pass and evidence.

**This is the first milestone of the phase that CONSUMES its predecessors
rather than adding an operator beside them.** Three of them recorded that
nothing consumed them, in those words. `P19-GRAD-001` wrote "nothing here is
wired into `assembleCfdDiffusion`"; `P19-DIV-001` recorded as its L1 that
"nothing consumes this divergence yet … it is one term of an equation
`P19-MOM-001` and `P19-PRES-001` have not assembled"; `P19-CONV-001` recorded
the same of its own operator. `TODO.md` carries the same sentence in its
`P19-DIV-001` section. (An earlier draft cited a LINE NUMBER there, which was
already wrong in both the committed and the working tree and would go stale
again at every edit; an independent review checked it.)
So the honest statement of novelty here is not a new discretisation — every
term but two is already written and qualified — but the **composition**, and
the question this ADR has to answer is what composition means when the parts
are qualified and must stay so.

What exists, and is not re-derived:

```text
assembleCfdConvection   +div(phi .), per component, positive diagonal under
                        Upwind, asymmetric by construction          ADR-072
assembleCfdDiffusion    -div(mu grad), per component, positive diagonal,
                        symmetric to the last bit, implicit arm only unless
                        a face gradient is supplied                 ADR-069
cfdMassFlux             phi = rho (U_f . S_f), kg/s, out of the owner  ADR-072
reconstructCfdPressureGradient  a cell-centred grad p in Pa/m       ADR-070
CfdSparseMatrix         a validated CSR container whose only door is fromCsr,
                        with rowSumSi, coeffSi, multiply             ADR-069
treatmentOf             the frozen per-field boundary table          ADR-067
CfdGravity              the canonical body force, ABSENT when there is none,
                        with a referenceHeight that belongs to prescribed
                        boundary PRESSURES and not here           ADR-058/061
```

### The measurements, made before any C++ existed

`P19-DIV-001`'s first ADR draft predicted a bit-exact global sum and was
wrong — a test asserting it would have failed on a correct operator.
`P19-CONV-001` therefore measured first and found two of its own predictions
wrong. Five arithmetic claims of this design were measured the same way, with
a fixed seed (`202611081`), in plain IEEE-754 double in the order the C++
writes, modelling the slot-wise sum exactly as the assembly will do it.

```text
Q1  matrix term accumulation order
    ((conv+diff)+trans) vs (conv+(diff+trans))
      200,000 cases   78.06% bit-exact   worst relative 4.034e-13
    ((conv+diff)+trans) vs ((trans+diff)+conv)
      200,000 cases   78.06% bit-exact   worst relative 4.034e-13

Q2  hydrostatic cancellation, -(V*fl(rho*g)) + ((rho*V)*g)
    exactly 0.0 on 130,101 / 200,000 (65.05%)
    worst |residual| / |gravity term| = 3.649e-16

Q3a row-sum additivity, relative to the SUM
      50,000 cases    47.39% bit-exact   worst relative 5.264e-12
Q3b the same departure relative to the ROW SCALE (max |a_ij|)
      50,000 cases    47.39% bit-exact   worst 1.085e-14

Q4  the transient diagonal lost in a steady diagonal, dt out to 1e20
    invisible on 45,162 / 200,000 (22.58%)
    largest |trans/steady| at which it was still lost: 1.087e-16

Q5  source accumulation order, five addends
    declared vs reversed      57.45% bit-exact  worst relative 3.044e-13
    declared vs ascending     68.64% bit-exact  worst relative 3.044e-13
```

**Two of those figures are corrections to this script's own first run, and
both are recorded rather than quietly fixed**, because each looked like a
result and was an artefact of the measure.

Q2 first reported "worst relative 1.000e+00", which reads as total failure.
It is an artefact of comparing a residual against exact zero with a relative
measure: `|a - 0| / max(|a|, 0)` is 1.0 for every non-zero `a`, so the figure
was a number about nothing. The quantity that means something is
`|residual| / |term|`, and it is `3.649e-16`.

Q4 first sampled `dt` in `1e-4..1e4` and reported `0/200,000` invisible,
which reads as "the transient term is always visible" and is a statement
about the SAMPLING: within those ranges `rho V / dt` never fell sixteen orders
below the steady diagonal. Re-sampled out to `dt = 1e20` it is lost 22.58% of
the time, at a ratio of `1.087e-16` — which is simply the diagonal's last bit,
and is the honest form of the claim.

Q3 is kept in both forms deliberately. Dividing by the SUM measures the
cancellation of a nearly-null row and not the assembly, and quoting it alone
would justify a tolerance two decades looser than the operator needs; dividing
by the row's own scale gives `1.085e-14`, a factor of 500 tighter, and that is
the bound a test may assert. Quoting only the second would hide that the sum
itself is ill-conditioned near a null row, so both are published.

**What the measurements decide.** No order-independence may be claimed of
either accumulation. The assembly fixes ONE order, states it, and the suite
reproduces that order rather than a mathematically equal one — which is why
the term decomposition can still be asserted BIT-EXACTLY while the row-sum
additivity cannot.

## Decision

### Decision 1 — Momentum is the SUM of the qualified operators, not a re-derivation

`assembleCfdMomentum` calls `assembleCfdConvection` and
`assembleCfdDiffusion`, verifies that the two CSR patterns are identical, and
sums them slot-wise; the transient diagonal is added afterwards. It does not
walk a face and decide an upwind direction, and it does not form an
over-relaxed decomposition.

Three candidates were compared.

```text
                       A: re-derive     B: sum systems    C: accumulator
                          in one pass      (CHOSEN)          threaded through
correctness            two more copies  one copy of each  one copy of each
                       of qualified     term, already     term
                       arithmetic       qualified
architecture fit       a second         composition of    EDITS TWO
                       discretisation   public API        QUALIFIED
                       of the same                        operators' API for
                       equation                           one new consumer
complexity             one pass, most   three CSR builds  one pass, an output
                       code             + a slot add      parameter in two
                                                          qualified functions
persistence            none of the three stores anything: all are derived
determinism            all three deterministic; B's order is fixed and stated
performance            best             3x the matrix     best
                                        builds, unmeasured
extensibility          a new scheme     a new scheme is   a new scheme is free
                       must be added    free             but every term gains
                       in two places                     an accumulator arg
failure modes          a divergence     a sub-refusal     a half-written
                       between the two  must be surfaced  matrix on a mid-pass
                       copies is        (Decision 7)      refusal
                       SILENT
migration cost         none             none              re-qualifies ADR-069
                                                          and ADR-072
testability            the decomposition  THE DECOMPOSITION  the sub-terms are
                       is unobservable:   IS THE            not observable at
                       nothing to         IMPLEMENTATION    all, so "no hidden
                       compare against                      stabilization" has
                                                            no falsifiable form
```

**A is rejected because a second copy of a qualified discretisation is the
"second parameter system" ARCHITECTURE.md rejects, in miniature, and because
its divergence from the first would be silent.** `P19-CONV-001`'s own review
found the interpolation weight left unpinned in the same place
`P19-GRAD-001` and `P19-DIV-001` had each left it unpinned — three milestones,
one duplicated three-line interpolation, three identical defects. A fourth
copy of the upwind decision and a second copy of the over-relaxed
decomposition would be the same mistake at a larger scale.

**C is rejected because it edits two qualified operators to serve one new
consumer**, which is the shape of change `ADR-063` Decision 7 refused and
`ADR-069` refused again, and because it destroys the only falsifiable form of
the checklist's "no hidden stabilization" item: if the terms are never
separately observable, no test can show the assembly is exactly their sum.

**B is chosen, and its cost is accepted and stated.** Three CSR builds where
one pass would do, with no performance baseline to say what that costs,
because nothing consumes this operator either (L1 below) and a baseline for an
operator with no caller would be a number with no meaning — `P19-DIV-001`'s
and `P19-CONV-001`'s recorded reason, unchanged.

### Decision 2 — The patterns are VERIFIED identical, never assumed

`assembleCfdConvection` and `assembleCfdDiffusion` build the same pattern — a
diagonal for every cell, plus both off-diagonals of every internal face with a
neighbour — through the same `layoutOf`, which sorts and deduplicates. So a
slot-wise sum is exact and needs no search.

**Relying on that by reading the code would be a coupling with no gate.** The
assembly compares `rowStart()` and `inner()` of the two matrices element by
element and refuses with `AssemblyIsInconsistent` if they differ, naming the
first disagreeing position. The alternative considered was summing through
`coeffSi`, which is robust to differing patterns — and which would **silently
drop** any coefficient of either operator that fell outside momentum's own
pattern. A refusal is better than a drop: the drop is a conservation error
with no symptom, and it is exactly the class of fault `P19-CONV-001` recorded
when an unguarded `SIZE_MAX` index reached writes.

Unreachable while both operators keep their present symbolic pass, and
recorded as UNREACHED rather than claimed tested — the treatment
`P19-BC-001` gave `DegenerateFacet`.

### Decision 3 — The three terms are dimensionally commensurate, and that is asserted at COMPILE TIME

Every coefficient of the momentum matrix is a mass flow rate, in kg/s:

```text
convection   phi                     = rho (U . S)    kg/s
diffusion    mu * faceArea / distance = Pa s m^2 / m  = kg/s
transient    rho * volume / timeStep  = kg/s
```

and every source entry is a force, in N:

```text
convection   phi * velocity           = kg/s * m/s        = N
transient    (rho V / dt) * velocity  = kg/s * m/s        = N
pressure     volume * pressureGradient = m^3 * Pa/m       = N
gravity      rho * volume * acceleration                  = N
```

`CfdMomentum.hpp` carries `static_assert`s over the units framework's own
`decltype` arithmetic for all seven, and `assembleCfdMomentum` computes the
transient coefficient as a `MassFlowRate` and each locally formed source term as
a `Force`, reaching a plain `double` only at the point of storage. **Together
those two things mean a term whose formula lost a density or a volume does not
compile**, and the second half is what makes the first load-bearing.

**AN EARLIER DRAFT HAD ONLY THE FIRST HALF, AND THE CLAIM WAS THEREFORE FALSE.**
The assembly computed every term in raw doubles through `.si()`, so
`rho * volume * dt` would have compiled, satisfied all seven assertions, passed
every finiteness gate and been caught only by a runtime oracle — the assertions
proved the units framework's own dimension algebra and nothing this milestone
wrote. An independent review found it, and the inversion was exact: the runtime
test was the strongest check in the milestone and these were the weakest, which
is the opposite of what this decision claimed for them. The arithmetic did not
change when it was fixed — `operator*` and `operator/` are
`fromSi(a.si() OP b.si())`, so the grouping and every bit are what they were,
and Q1's and Q5's measurements still describe the code.

**THE HONEST COUNT IS FIVE INDEPENDENT IDENTITIES AND TWO COROLLARIES.** Both
`CfdTransientCoefficient{} * Velocity{}` and the convection-against-pressure
comparison follow from the others; the file labelled one of them as a corollary
and left the other unmarked, so both documents said "seven" as though all seven
bit. The same review counted them. A term with the wrong dimensions is the failure
mode a numerical test is worst at catching: it produces finite, plausible,
order-of-magnitude-wrong numbers that pass every finiteness gate and every
symmetry gate, and on a fixture with volumes near 1 it can pass a magnitude
assertion too.

ARCHITECTURE.md's "units are part of correctness" is usually kept by a typed
accessor. Here it is kept by a proof.

### Decision 4 — The transient term is backward Euler, and the previous time level is a SEPARATE named input

`ADR-058` fixes backward Euler and there is no second scheme, so there is no
selector: a field with one legal value would be a false capability.

```text
rho V / dt   added to the diagonal
rho V / dt * U_previous   added to the source
```

The assembly takes `std::optional<Time> timeStep`. Present means transient and
requires `previousVelocity`; absent means the steady form and requires
`previousVelocity` to be EMPTY. **An inert input is refused rather than
ignored**, in both directions, which is P18's recorded treatment of an initial
temperature on a steady analysis and `CfdData.hpp`'s reason for making the
absence of gravity the absence of a record.

**`previousVelocity` and `currentVelocity` are two different arrays and the
naming is load-bearing.** `currentVelocity` is the latest outer-iteration
field, needed only to pass to diffusion for a symmetry face's cross terms;
`previousVelocity` is the field at time level `n`. Passing the current field
where the previous one is wanted makes the transient term's two halves cancel:
the diagonal still carries `rho V / dt` and the source carries
`rho V / dt * U_current`, so the system is the steady one perturbed toward its
own current state, which **converges to the correct steady answer** and would
pass every steady test in the suite. A test asserts the source carries the
PREVIOUS values on a fixture where the two fields differ, because nothing else
in the suite can see this.

### Decision 5 — The pressure gradient is caller-supplied, per cell, typed, and its absence is REPORTED

`CfdMomentumInput::pressureGradient` is a
`std::span<const PressureGradient3>`, one per cell, empty meaning the term is
omitted and `CfdMomentumSummary::pressureGradientApplied` saying so. This is
exactly `CfdDiffusionInput::faceGradient`'s shape and precedent, for the same
reason: the term is a field somebody else reconstructed, the operator must not
choose the reconstruction method, and a silently absent term is worse than an
absent one that is reported.

Reconstructing internally from cell pressures was rejected: it would make
momentum choose between Green-Gauss and least squares, which is the analysis's
decision and `ADR-070`'s parameter, and it would recompute a field a SIMPLE
loop already holds.

Taking a `CfdPressureGradientField` directly was considered and rejected on
balance. It would carry "possession is the evidence" — the field exists only
if it passed `ADR-070`'s gate — but it needs a pointer or a reference wrapper
in an aggregate input, it couples momentum to a class where a span of a typed
quantity suffices, and **the field carries no mesh stamp**, so it would not
actually establish more than the cell count does. That last point is recorded
as L6 rather than hidden: neither form can tell that a gradient was
reconstructed on the mesh being assembled.

### Decision 6 — Gravity is `rho V g`, its absence is the absence of a record, and `referenceHeight` is NOT read here

`std::optional<CfdGravity>`, absent meaning no gravity.
`CfdData.hpp` is explicit that "NO GRAVITY IS THE ABSENCE OF THIS RECORD, not
a zero vector: a zero vector would be a second spelling of none", and
`validate(CfdGravity)` refuses a zero vector, so an `Acceleration3` member
defaulting to zero would have admitted a state the canonical type forbids.

**`CfdGravity::referenceHeight` is deliberately not read.** It exists for
prescribed boundary PRESSURES: `ADR-058` puts the hydrostatic part inside `p`,
so a static-pressure inlet prescribes its value at that height and
`P19-BC-001` already varies the prescribed pressure along the face as
`p_b - rho |g| (h - h_ref)`. Reading it again here would apply the hydrostatic
field twice — once in the boundary values the gradient was reconstructed from,
once in the body force. The term is `rho V g` and nothing else.

**A constant `rho g` at a constant density changes no velocity field in a
domain no boundary pressure fixes**, because it is absorbable into a modified
pressure. That is a property of the equation, not a defect, and the operator
neither hides it nor pretends otherwise: the hydrostatic cancellation is
asserted as a test (Q2's bound) rather than described in prose. This is also
why gravity here is NOT buoyancy, which `ADR-058` refuses from P19 v1:
buoyancy needs density to vary with temperature, and there is one constant
density.

### Decision 7 — The sub-operators' refusals are PREDICTED inside momentum's own gate

`cfdConvectionProblem` and `cfdDiffusionProblem` exist so a caller can ask
whether an input is usable without paying for the answer. Momentum's gate
calls both, in phase order, after its own checks, and reports a sub-refusal as
`ConvectionRefusedTheInput` or `DiffusionRefusedTheInput` with the
sub-problem's own name in the message.

So the assembly calls that follow cannot fail for an input reason, and if one
still does it is `AssemblyIsInconsistent` — unreachable, and recorded as such.
The alternative, re-deriving every check the two operators already make, would
be a third copy of two gates that must agree with them forever.

**This inherits their recorded limitation and does not pretend to fix it.**
Both predictors share one decision path with their assembly, so their
agreement cannot fail — `P19-DIFF-001`'s L8, `P19-GRAD-001`'s L9,
`P19-DIV-001`'s own. Momentum's gate is therefore as complete as theirs and no
more.

### Decision 8 — One component, `cells` by `cells`, and that IS the "sparse global matrix"

`ADR-066`: "The segregated momentum predictor assembles no row that couples
`u` to `v`." A velocity assembly is three calls, one per `VectorComponent`,
exactly as diffusion's and convection's are.

A `3N` by `3N` block system was considered, because the checklist says
"global". It is rejected: with no `u`-`v` coupling it is block-diagonal with
three blocks of identical PATTERN, so it would store the same pattern three
times and — worse — would make a coupled row expressible, which `ADR-066`
forbids. "Global" is read as "over all cells", which is what `cells` by
`cells` is, as against a cell-local or element matrix.

**The three components' matrices are not copies, and the reason is worth
stating.** `A_conv` is identical across components (the component enters only
the source), and `A_diff` differs only through a `Symmetry` face, whose
diagonal contribution carries `n_i^2`. So on a mesh with no slip wall the
three matrices are equal and on a mesh with one they are not. No caching
exploits this: it would be an optimisation with no measurement behind it, for
a consumer that does not exist.

### Decision 9 — "No hidden stabilization" is given a falsifiable form

The checklist item is a negative claim, and a negative claim with no test is
prose. It is given three forms, in descending strength:

```text
1  TERM DECOMPOSITION, BIT-EXACT. The assembled matrix equals
   (A_conv + A_diff) slot-wise with the transient diagonal added after, and
   the source equals the five terms summed in the declared order -- both
   reproduced by the suite in the production order, so the assertion is
   bit-exact and any added term, however small, fails it
2  NO DIAGONAL FLOOR. The suite asserts a_ii is exactly its named parts on a
   fixture whose diagonal is NEGATIVE by construction: a one-cell tet with
   pressure outlets, so every boundary face is ZeroGradient and diffusion
   contributes nothing there, and a HAND-MADE all-inward phi, so the whole
   boundary flux lands on the diagonal with a negative sign. A max(a_ii, eps),
   a clamp or an added artificial viscosity would all raise it
3  THE MEASURED DOMINANCE IS REPORTED, NOT ACHIEVED.
   CfdMomentumSummary::rowsNotDiagonallyDominant counts the rows that are not
   weakly diagonally dominant and claims nothing about them. Pinned to a known
   non-zero count on a Central, high-flux chain, where central differencing is
   unbounded by construction
```

**THE SECOND FORM'S FIXTURE IS THE SECOND ATTEMPT, AND THE FIRST ONE COULD NOT
DO WHAT THIS DECISION SAID IT DID.** It used the same one-cell tet with a
UNIFORM OWNER VELOCITY, whose diagonal is `rho (U . sum_f S_f)` — the cell's
closure residual, approximately zero and of indeterminate sign, not negative —
and the test asserted no sign at all. An independent review traced the closure
identity. The flux is hand-made now, which the operator accepts because it asks
nothing about a flux's divergence (`ADR-072` Decision 5 refused that threshold
for the reason `ADR-063` finding F1 gives), so the sign is a property of the
fixture rather than of rounding.

What is NOT added, named so that a reader need not infer it from an absence:
no artificial or numerical viscosity; no upwinding beyond the
`CfdConvectionScheme` the caller chose; no deferred-correction blending; no
Rhie–Chow interpolation (that is `P19-PRES-001`'s); no under-relaxation
(`CfdIterationSettings`' factors are the outer iteration's, and applying them
in the assembly would put the solver's state in the operator); no diagonal
boosting, clipping, limiting or flooring; no Peclet-based scheme switching.

`CfdConvectionSummary::backflowFaces` and `CfdDiffusionSummary::deferredFraction`
are carried through verbatim in the summary, so the two conditions under which
the assembled operator is NOT bounded remain visible to a consumer holding
only momentum's answer.

### Decision 10 — The error channel is `std::expected` with a tag

As `ADR-072` Decision 10, for the reason it records: `ResultIsNotFinite`
judges an OUTPUT, so no predictor can reach it, and with a plain `Error` the
enumerator would be observable only through a message's wording — against
`CfdBoundary.hpp`'s "a message is not a tag" rule. `P19-DIV-001` recorded that
as its L10 and `P19-CONV-001` resolved it; this milestone inherits the
resolution rather than the limitation.

## Consequences

**Good.** Three qualified operators gain their first consumer, which is the
limitation three milestones recorded against themselves. The momentum equation
exists as one function whose terms are separable by construction, so "no hidden
stabilization" is a bit-exact assertion rather than a claim. Seven dimensional
identities are proved at compile time. Nothing is re-derived, so no copy can
diverge from a qualified original.

**Bad, and accepted.** Three CSR builds where one pass would do, with no
performance baseline. Momentum is now coupled to two operators' CSR patterns,
gated by a refusal rather than by a type. The gate is only as complete as the
two predictors it calls, which share a decision path with their own
assemblies.

**Ugly.** `previousVelocity` versus `currentVelocity` is a trap that the type
system cannot close — both are `std::span<const double>` of length `3 * cells`
in the same layout — and it is closed by a name, a comment and one test.

## Known limitations

```text
L1  NOTHING CONSUMES THIS OPERATOR. There is no solve, so no velocity field is
    produced and no momentum equation is advanced. P19-CONT-001 and
    P19-PRES-001 are not authorized
L2  BOUNDEDNESS IS INHERITED AND CONDITIONAL. Upwind + no backflow + a
    discretely conservative phi; the operator sees only the second, reports it,
    and refuses none of the three
L3  THE DIFFUSION TERM IS INCOMPLETE ON A NON-ORTHOGONAL MESH unless a face
    gradient is supplied -- ADR-069's limitation, carried, reported through
    the sub-summary's correctionApplied and deferredFraction
L4  NO PECLET REPORT, so Central's unboundedness is stated and not measured
L5  THE PRESSURE GRADIENT'S ACCURACY IS THE CALLER'S. Momentum validates its
    length and finiteness and nothing else
L6  NEITHER THE PRESSURE GRADIENT NOR THE PREVIOUS VELOCITY CARRIES A MESH
    STAMP, so a field of the right LENGTH from another mesh is accepted. Taking
    ADR-070's field type instead would not have fixed this
L7  NO TIME-STEP QUALITY RULE. A Courant number is not computed and no step is
    refused as too large: ADR-063 finding F1 reserves engineering thresholds
    to the owner, and ADR-071 and ADR-072 refused the same thing for the same
    reason
L8  NO PERFORMANCE BASELINE, for L1's reason
L9  TWO OF THE SIXTEEN REFUSALS ARE UNREACHABLE through the public API and
    are recorded as UNREACHED rather than covered: TopologyNamesAnUnknownCell
    (buildCfdTopology holds) and ConvectionRefusedTheInput (every input phase
    of cfdConvectionProblem is pre-empted by an earlier momentum phase, and its
    remaining enumerators are themselves unreachable). So is the CONDITIONAL
    arm of BoundaryDescribesAnotherTopology, which needs a foreign boundary
    whose face count matches. AN EARLIER DRAFT OF THIS LINE SAID FIVE AND THE
    TEST SUITE'S HEADER SAID THREE, and both were wrong in different
    directions: an independent review counted every enumerator against the
    suite and found two that were reachable in three lines and untested
    (GeometryDescribesAnotherTopology, BoundaryDescribesAnotherTopology), with
    the consequence that DELETING phase 1 or phase 2 left the suite green
L10 THE GATE IS AS COMPLETE AS THE TWO PREDICTORS IT CALLS (Decision 7)
L11 ASSEMBLY IS INCONSISTENT IS REACHED, through diffusion's untagged channel.
    assembleCfdDiffusion returns Result<CfdDiffusionSystem>, which carries no
    tag, so a viscosity that overflows mu A / d inside that assembly cannot be
    distinguished here from a genuine inconsistency. P19-DIFF-001's limitation
    inherited; tested rather than denied
L12 THE REPORTED TRANSIENT EXTREMA'S OWN FINITENESS GATE IS UNREACHABLE for
    this operator, because every transient coefficient is added to a diagonal
    and the coefficient gate runs first. Kept as a defence against that
    ceasing to be true, and recorded as UNREACHED rather than justified by
    CfdConvection.cpp's argument, which does not transfer
```
