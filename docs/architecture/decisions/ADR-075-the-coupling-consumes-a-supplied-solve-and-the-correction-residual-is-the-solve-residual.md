# ADR-075 — The coupling consumes a supplied solve, and the correction residual IS the solve residual

- **Status:** Accepted, 2026-11-08
- **Milestone:** P19-PVCOUPLE-001 — Pressure–Velocity Coupling
- **Supersedes:** nothing. **Amends:** nothing.
- **Depends on:** ADR-052 (the generic symmetric solve), ADR-058 (the P19 data
  model, the two mass-balance measures, the pressure datum), ADR-064 (face
  numbering), ADR-066 (component-blocked fields), ADR-067 (the boundary
  treatment table), ADR-069 (the sparse container, and the deferred solve
  decision), ADR-071/072 (`phi`'s sign and its producer), ADR-073 (the momentum
  sum), ADR-074 (continuity consumes the qualified divergence).

## Context

`P19-PVCOUPLE-001` is the first milestone of this phase that has to make the
pressure and the velocity agree. Everything before it left that open on purpose:
`P19-MOM-001` takes a pressure gradient from its caller, and `P19-CONT-001` only
*measures* whether `phi` conserves mass. SIMPLE is what closes the loop.

Three constraints were already in force before this milestone started, and none
of them is this milestone's to change.

```text
THE SOLVERS ARE NOT OURS   src/cfd/CMakeLists.txt: "the SOLVE -- still
                           P19-LINSOLVE-001's decision, unchanged".
                           CfdSparse.hpp: the container, "no factorisation, no
                           solve, no iteration". P19-LINSOLVE-001's checklist
                           owns "Pressure-system solver" and "Momentum-system
                           solver" BY NAME, and it is NOT AUTHORIZED.
                           BetterCAD::linalg is not linked by cfd.
THE DATA MODEL IS FIXED    ADR-058 already declares CfdIterationSettings,
                           CfdConvergenceSettings, CfdSteadySettings,
                           CfdLinearSolverSettings, CfdResiduals,
                           CfdMassBalance and CfdReferencePressure.
THE ORCHESTRATION IS NOT   P19-STEADY-001 owns "Validate inputs", "Convergence
OURS                       loop" and "Atomic result publication", and
                           CfdFailureStage lives in CfdResult.hpp.
```

So the question this ADR answers is not "how do we write SIMPLE" but **"what
part of SIMPLE can exist without spending a decision that belongs to the next
milestone, and does that part satisfy all seventeen items?"**

## The measurements, made before any C++ existed

Seed `20261108`. The model is a structured box rather than a tetrahedral mesh,
recorded as `L1`: every question below is about owner/neighbour pairs, areas,
distances, volumes and **accumulation order**, none of which needs cell shape.

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

**TWO MEASUREMENT-TOOL DEFECTS OF THE AUTHOR'S OWN ARE RECORDED RATHER THAN
QUIETLY FIXED**, which is this phase's running theme:

```text
D1  Q1's first form drew w from rng.random(), which returns k * 2**-53. `1 - w`
    is then exact BY CONSTRUCTION OF THE SAMPLE, the answer came back
    100.0000%, and the question had never been asked. A real weight is a
    QUOTIENT of two distances. Re-drawn, the true answer is 57.3703% -- which
    REVERSES the conclusion the first form supported.
D2  Q6's first form scored the identity with a per-component relative gap,
    which reports 2.0 for two values at the noise floor with opposite signs,
    and so reported `2.000000e+00` for an identity that holds. Rescaled to the
    boundary throughput, 1.686762e-14.
```

## Decision 1 — The coupling consumes a CALLER-SUPPLIED linear solve

A `CfdLinearSolve` is a caller-supplied callable taking a `CfdSparseMatrix`, a
right-hand side and `CfdLinearSolverSettings`, and returning a solution or a
diagnostic. The coupling calls it; it does not contain one, does not link
`BetterCAD::linalg`, and does not include a `linalg` header.

This is the same shape `P19-MOM-001` already uses for the pressure gradient: the
operator states what it needs and the caller supplies it.

### The candidates

```text
                        A  SUPPLIED PORT      B  LINK linalg HERE   C  ASSEMBLE ONLY
                           (chosen)

correctness             equal -- the port     equal                 cannot satisfy
                        is called where a                           items 9-14 at
                        solve belongs                               all
architecture fit        keeps ADR-069 D3's    SPENDS a decision     keeps the
                        deferral intact;      TODO.md assigns to    boundary, but
                        cfd links nothing     P19-LINSOLVE-001 by   leaves the
                        new                   name                  milestone
                                                                    unfinished
complexity              one type-erased       a new link edge, a    least code,
                        callable              translation layer,    least value
                                              and a second owner
                                              of solve policy
persistence             none -- nothing       none                  none
                        here is persisted
determinism             the caller's solver   same                  n/a
                        decides; recorded
                        as L4
performance             one indirect call     one fewer indirect    n/a
                        per solve, against    call per solve
                        a factorisation
extensibility           any solver, incl.     whatever is linked    n/a
                        the symmetric one
                        already qualified
failure modes           a solver's refusal    identical             n/a
                        is surfaced as a
                        coupling diagnostic
migration cost          P19-LINSOLVE-001      that milestone would  P19-STEADY-001
                        supplies a port and   find the decision     would have to
                        changes nothing here  already made for it   write the loop
testability             a test supplies an    a test gets whatever  stages testable,
                        INDEPENDENT dense     the linked solver     loop untestable
                        solve, which is the   does -- weaker
                        reference
```

**B is rejected because it spends a decision that is not this milestone's.** It
is the shortest path and it would work; `ADR-069` Decision 3 records why buying
convenience with someone else's decision is refused, and the same answer applies
here. **C is rejected because it cannot satisfy items 9 through 14** — stopping
criteria, maximum iterations, divergence rejection, non-finite rejection,
diagnostics and deterministic iteration are all properties of a loop, and a
milestone that returns matrices has none of them.

A also turns out to be the *stronger* testing position, not a concession: a test
supplies a dense direct solve that is independent of BetterCAD entirely, so the
benchmark measures the COUPLING rather than a solver's convergence.

## Decision 2 — The pressure-correction matrix is assembled here, in ONE visit per face

The coefficient is

```text
a_f = rho * D_f * |S_f| / |d_f|        D_f the face value of  d_P = V_P / a_P
```

and the system is the Laplacian `sum_f a_f (p'_P - p'_N) = -sum_f phi*_f`.

The loop is over **faces, once each**: one `a_f` is computed and written as
`+a_f` to both diagonals and `-a_f` to both off-diagonals in a single visit.

**THIS IS NOT A STYLISTIC PREFERENCE. Q1 MEASURED THE ALTERNATIVE AT 57.3703%
BIT-IDENTICAL.** A per-cell loop sees an internal face twice and recomputes the
weight from that side; `1 - w` round-trips exactly only 68.2728% of the time and
`w + w'` is exactly 1 only 87.0376% of the time, so the two visits disagree on
42.63% of faces, worst relative `1.241051e-14`. Since
`linalg::solveSymmetricSparse` **requires** symmetry, a two-visit assembly would
produce a matrix the already-qualified solver cannot accept. Symmetry here comes
from the single visit and from nothing about the interpolation — which is also
why `fromCsr` is given a symmetry tolerance rather than trusted.

`P19-DIFF-001` reached the same conclusion for its own operator ("THE LOOP IS
OVER FACES, ONCE EACH, which is what makes conservation structural rather than
checked"). This measurement is an independent confirmation of that decision for
a different matrix, and it is recorded because the author's first prediction was
the opposite.

### Why NOT reuse the qualified diffusion operator

It was considered seriously: the pressure-correction matrix has the shape of a
diffusion Laplacian. It is rejected on two grounds, and the second is decisive.

```text
the coefficient   assembleCfdDiffusion computes Gamma_f = mu * A_f / d_f from
                  ONE uniform viscosity. The correction needs a PER-FACE
                  rho * D_f, and D_f interpolates V/a_P. Admitting a per-face
                  coefficient means changing a qualified operator's API to
                  serve this checklist -- exactly what ADR-074 Decision 1
                  refused for continuity.
the boundary      DECISIVE. Diffusion's boundary table is a VELOCITY table.
                  The correction's condition is the PRESSURE column, and it is
                  a different condition in kind: where the flux is prescribed
                  p' is zero-gradient and contributes NOTHING, and where the
                  pressure is prescribed p' = 0 contributes a diagonal. Reusing
                  diffusion would apply the wrong condition, not merely an
                  awkward one.
```

## Decision 3 — The p' boundary condition is the treatment table's `pressure` column, and the table's exclusivity is ASSERTED

```text
treatmentOf(kind).pressure == FixedValue     p' = 0  -> diagonal only
treatmentOf(kind).pressure == ZeroGradient   no contribution at all
```

Read from `ADR-067`'s table this is exact, and it coincides with the `massFlux`
column: `pressure` is `FixedValue` exactly where `massFlux` is `Computed`. That
is one physical fact said twice — **a boundary prescribes either the flux or the
pressure, never both and never neither** — and the pressure-correction equation
is over-determined at a boundary that did both and under-determined at one that
did neither.

So it is **asserted over `kCfdBoundaryConditionKinds` rather than trusted**, as a
test on the canonical array. Reading the function's source rather than the
header's comment also corrected a name: the enumerator is `Slip`, while the
header's table prints `SlipWall`. `treatmentOf`'s unreachable fallback returns
the wall treatment, which preserves the exclusivity, so even that path cannot
break the assumption.

## Decision 4 — The reference pressure binds to `referenceCell()`, and p' is pinned by clearing ROW AND COLUMN

`P19-BC-001` already resolves the datum: `PreparedCfdBoundary::referenceCell()`
is present exactly when no boundary fixes the pressure level. This milestone
**binds to it and computes nothing of its own**, per `ADR-058`, which assigns the
datum to the boundary preparation.

When it is present the pressure-correction matrix is singular in the constant
mode, and `p'` is pinned at that cell by clearing its row **and its column** and
setting the diagonal to 1.

**CLEARING THE COLUMN IS HARMLESS PRECISELY BECAUSE THE PINNED VALUE IS ZERO**,
and that is why this is elimination and not regularisation. For every other cell
the cleared column would have contributed `A[P,ref] * p'_ref`, and `p'_ref` is
exactly `0`, so each remaining row still states `(A_orig p')_P = rhs_P`
unchanged. Q3 measured the pinned matrix's asymmetry at exactly `0.0`.

A penalty on the diagonal was rejected: `ADR-052`'s kernel says "NO
REGULARISATION ... no pinned unknown, no penalty", and a penalty would also
break Decision 5's identity by adding a term the flux correction does not
contain.

## Decision 5 — The claim is an IDENTITY, and the closure tolerance derives from the SOLVE tolerance

The face-flux correction adds `sum_f a_f (p'_P - p'_N)` to each cell's flux sum,
which is `(A p')_P`. The system is `A p' = -div(phi*)`. Therefore

```text
div(phi_corrected)_P  ==  (A p')_P - rhs_P  ==  THE LINEAR SOLVE'S OWN RESIDUAL
```

exactly, as algebra and not as an approximation. Q6 measured the gap at
`1.686762e-14` of the boundary throughput with an exact solve, and at
`3.810534e-16` with a **deliberately wrong** `p'` — the second number is the one
that matters, because it shows the identity is structural rather than an artifact
of `p'` being correct.

Three consequences, and the first is the one that would have been a defect:

```text
1  CONTINUITY CLOSURE IS EXACTLY AS GOOD AS THE PRESSURE SOLVE, NO BETTER. A
   hard-coded 1e-14 closure bound would be wrong: a conforming solver at
   CfdLinearSolverSettings::relativeResidualTolerance = 1e-9 would miss it
   while doing nothing wrong. The bound is DERIVED from that setting.
2  THE TEST ASSERTS THE IDENTITY, not merely that continuity is small -- a
   strictly stronger check, since it holds for a wrong p' too.
3  THE COMPATIBILITY CONDITION IS WHY THE PINNED CELL IS NOT A WEAK SPOT. Every
   non-reference row states its own continuity, and the global sum of the
   right-hand side vanishes (Q2: 6.217249e-15 balanced against 5.327555e+00
   unbalanced), so the reference cell's residual is the compatibility error and
   not a hole. Q6: 2.020606e-14 at the reference cell against 1.199041e-14
   elsewhere -- the same order, measured rather than hoped.
```

## Decision 6 — Momentum relaxation is implicit, and `d_P` uses the RELAXED diagonal

`CfdIterationSettings::velocityRelaxation` is applied to the momentum equation in
the classic implicit form, so the diagonal becomes `a_P / alpha_u`. **The `d_P`
used by the correction must then be `V_P / (a_P / alpha_u)`, not `V_P / a_P`.**

Q4 measured the error of getting this wrong as **exactly `1 / alpha_u`** — a
42.86% inflation of every correction coefficient at the default `0.7`. It is
exact because the factor is a constant, which is also what makes it dangerous:
it does not look like noise, it looks like a different but plausible answer, and
the iteration still converges, to the same place, more slowly. It is recorded
here and pinned by a test.

## Decision 7 — Determinism is `FaceIndex` order, always

Q5 measured the divergence accumulated under a shuffled face order as
bit-identical in **0 of 200** trials, worst relative `1.918002e-12`. Every loop
in this operator therefore runs in `FaceIndex` order (internal faces first, per
`ADR-064`) and every cell loop in `CellIndex` order, with no unordered container
anywhere on the path. This is the same conclusion `ADR-074` Decision 6 reached
for continuity, and the same measurement supports it.

## Decision 8 — No new settings, residual or balance type

`CfdIterationSettings`, `CfdConvergenceSettings`, `CfdSteadySettings`,
`CfdLinearSolverSettings`, `CfdResiduals`, `CfdMassBalance` and
`CfdReferencePressure` are `ADR-058`'s and are **bound, not re-declared**. The
continuity closure is `evaluateCfdContinuity`'s `CfdMassBalance`, produced by the
operator `P19-CONT-001` qualified — which was written before this milestone
existed and without knowledge of it, so the central claim is checked against a
qualified external measure rather than a restatement of its own formula.

That trap is not hypothetical: `P19-GRAD-001`'s r5 and `P19-CONV-001`'s second
review both found an "independent" oracle that was a character-for-character
transcription of production.

## Decision 9 — The driver returns a report and publishes nothing

`CfdFailureStage` and `CfdSolveFailure` live in `CfdResult.hpp` and belong to the
result. This operator reports its own outcome in its own vocabulary and
`P19-STEADY-001` maps it, because that milestone owns "Atomic result
publication". Nothing here reads a `Document` or writes a `CfdResult`.

## Decision 10 — SIMPLE is implemented from the published mathematics

The checklist says "Do not silently copy OpenFOAM implementation", and that is
treated as a constraint. SIMPLE is published (Caretto, Gosman, Patankar and
Spalding 1972; Patankar, *Numerical Heat Transfer and Fluid Flow*, 1980). Every
discrete formula is written out above and derived against **this** project's
conventions — `ADR-064`'s face numbering, `ADR-067`'s boundary table,
`ADR-071`/`072`'s sign for `phi`, `ADR-066`'s component blocking. No OpenFOAM
source was consulted or transcribed. Where this implementation departs from the
common textbook presentation, Decisions 4, 5 and 6 say so explicitly.

## Known limitations

```text
L1   the pre-design measurements model a structured box, not a tetrahedral
     mesh. No question asked depends on cell shape; Q3's and Q6's ABSOLUTE
     levels would differ on real tets, and the identity would not.
L2   this milestone contains no linear solver, so every claim about a solved
     field is conditional on the supplied solve. That is Decision 1, not an
     oversight, and P19-LINSOLVE-001 is where it stops being conditional.
L3   SIMPLE's rate of convergence is not claimed anywhere. The iteration's
     STOPPING is specified and tested; how many iterations a given problem
     needs is a property of the problem and the relaxation factors.
L4   determinism is this operator's, not the caller's. A supplied solver that
     is itself non-deterministic makes the run non-deterministic, and nothing
     here can detect that.
L5   no performance baseline, for the fourth milestone running, because the
     solve -- which dominates -- is supplied from outside.
L6   the pressure-correction matrix is assembled with the SIMPLE
     approximation, which neglects the neighbour velocity corrections. That is
     what makes it SIMPLE rather than SIMPLEC or PISO, and it is a property of
     the chosen algorithm, not an error in it.
L7   D1 reversed a conclusion. Had the first form of Q1 been trusted, Decision
     2 would have been justified by a measurement that said the opposite, and
     the decision would still have been right. A correct decision resting on a
     broken measurement is recorded here as the near miss it was.
```

## Decision 11 — The face coefficient uses the NORMAL-PROJECTED diagonal

The three component momentum systems have three diagonals, and there is only one
pressure equation. Rather than pick one component or average them arbitrarily,
the face coefficient uses the projection onto the face normal:

```text
d_f^eff = sum_i n_i^2 d_f^i           a_f = rho * d_f^eff * |S_f| / |d_f|
```

The correction is a NORMAL flux, so this is the form the mathematics gives, and
it has three properties the alternatives do not:

```text
reduces exactly      when d^x = d^y = d^z it is sum_i n_i^2 d = d, since
                     sum_i n_i^2 = 1. The isotropic case is not approximated.
stays symmetric      n_i^2 is sign-free, so the owner's +n and the neighbour's
                     -n give the same coefficient -- and it is still computed
                     once per face, per Decision 2.
no arbitrary pick    "use the x diagonal" is direction-biased and
                     "use the mean" is a choice with no derivation.
```

`sum_i n_i^2 == 1` is a checkable identity rather than an assumption, and the
summary reports the worst spread between the three component diagonals so a
caller can see when the isotropic reading is strained.

## Decision 12 — The velocity correction is a face sum, and the volume CANCELS

The velocity correction is `u'_P = -d_P (grad p')_P`. With `d_P = V_P / a_P` and
the Green–Gauss gradient `(grad p')_P = (1/V_P) sum_f p'_f S_f`, the volume
cancels **exactly and analytically**:

```text
u'^i_P = -(V_P / a_P^i) * (1 / V_P) * sum_f p'_f S_f,i
       = -(1 / a_P^i) * sum_f p'_f S_f,i
```

So the correction is ONE FACE LOOP with no division by the cell volume, no
dependence on `V_P` at all, and no gradient operator.

### Why the qualified gradient operator is NOT used for p'

This was the first design, and it is wrong. **`reconstructCfdPressureGradient`
imposes the PRESCRIBED boundary pressure at a `FixedValue` facet** —
`src/cfd/CfdGradient.cpp`'s `prescribedAt` returns `facet.pressure` — and the
pressure *correction* is **zero** there, not `p_b`. Reconstructing `grad p'`
with it would impose the wrong boundary value at exactly the boundaries that
make the pressure equation non-singular.

That is a correctness defect, not an inconvenience, so the correction carries its
own boundary condition:

```text
p'_f = w p'_P + (1 - w) p'_N        internal face
p'_f = 0                            treatmentOf(kind).pressure == FixedValue
p'_f = p'_P                         treatmentOf(kind).pressure == ZeroGradient
```

This is not a second gradient operator: it is the correction formula with the
volume cancelled, and it shares its face loop and its boundary condition with
the flux correction, which is where the condition comes from in the first place.
Two identities make it checkable rather than merely plausible:

```text
a uniform p' gives EXACTLY zero correction, because sum_f S_f = 0 over a
  closed cell and the zero-gradient facets contribute p'_P S_f
the face sum equals -a_P^i times the Green-Gauss gradient of p' in exact
  arithmetic, so the two forms are compared in floating point rather than
  assumed equal
```

A test also demonstrates, by measurement, that substituting the qualified
operator gives a DIFFERENT answer whenever a fixed-pressure boundary carries a
non-zero `p_b` — so the reason for not reusing it is evidence and not assertion.

---

## Amendments after the two adversarial reviews, 2026-11-08

Two independent read-only reviews ran before any freeze — one on correctness and
architecture, one on test adequacy. Neither was given the author's conclusions.
They found enough to change four of the decisions above, and the changes are
recorded here rather than folded silently into the text.

### Decision 2 is amended: the gate was missing the range guard

**THE SHARPEST FINDING, AND IT WAS A THIRD REGRESSION.** `CfdTopology::owner` is
invalid for a face the topology does not hold, `CellIndex::kInvalid` is
`SIZE_MAX`, and this operator WRITES through that index — into the symbolic
pattern, the right-hand side and the per-cell face sums. `geometry.describes`
compares the mesh stamp and the counts and never the owner and neighbour ranges,
so nothing upstream establishes it.

`CfdConvection.hpp` records that `P19-DIV-001`'s review and then
`P19-CONV-001`'s review each found an unguarded `SIZE_MAX` reaching writes, and
states: "It is not regressing a third time." It regressed a third time, here.
`CfdCouplingProblem::TopologyNamesAnUnknownCell` is added, as every sibling
operator has it, and the guard runs as phase 2.

### Decision 4 is amended: the pin is PER REGION

The flags were global and the pin fixes one cell. On a mesh with two
disconnected fluid regions, a pressure boundary on region A lifted only that
region's constant mode while the global flag concluded "nothing to pin" —
leaving region B singular, which is exactly the precondition
`linalg::solveSymmetricSparse` needs. A union-find over internal-face adjacency
now decides per region, and the gate refuses any region that neither fixes its
pressure nor contains the reference cell. The compatibility condition is per
region too, which incidentally makes `relativeDeparture`'s own justification
true at its call site: a region's net and its own throughput are sums over the
same faces, so they vanish together, while the earlier global form divided a
sum over ALL cells by a scale from the boundary alone and silently returned an
absolute kilogram-per-second number.

### Decision 5 is amended twice, and one review finding is REFUTED

**The closure bound's derivation did not transfer between norms.** It multiplied
the SOLVER's residual — the L2 form `ADR-052` defines — by a slack of `1e2` and
compared it with `maxCellContinuity`, which is an infinity norm over the
boundary throughput. The ratio between those drifts with mesh size, since the
matrix-norm term grows like the square root of the cell count while the
throughput does not. The bound is now the system's OWN residual **in the same
measure**, computed here, plus `kCfdCouplingClosureFloor`.
`kCfdCouplingClosureSlack` is deleted: comparing a quantity with itself in its
own norm needs no slack factor.

**An absolute kilogram-per-second number was going into a dimensionless field.**
Where the boundary throughput is zero, `ADR-058`'s two measures are undefined,
and the first repair put `largestCellResidual` — kilograms per second — into
`CfdResiduals::continuity`, which feeds
`CfdConvergenceSettings::continuityResidualTolerance = 1e-6`. The review's
scenario was concrete: at millimetre scale with water, a wholly
non-conservative field reports about `1e-9` and is called converged, so in a
closed domain the continuity criterion was effectively disabled — and every
*reachable* pure-Neumann run lands in that branch. The interior flux is the
honest denominator, and `CfdMassBalance` is deliberately NOT filled from it.
A third correction followed from a failing test: the scale must come from the
PREDICTED flux, not the corrected one, because the correction drives the
corrected interior flux toward zero and the ratio then becomes noise over noise
(it reported `0.375`).

**And the identity does NOT fail at the pinned cell.** The correctness review
worked the algebra and concluded it must: that row of `A` and its right-hand
side are cleared, so the residual is zero there by construction while the
measured flux sum "ought to be" the unpinned system's compatibility residual. A
test built the pinned case — which neither identity test had reached, since both
used a fixed-pressure outlet — and measured both sides: `-1.08e-19` against
`0`. They agree, and the mechanism is the compatibility condition this ADR
already required. Pinning happens only where no boundary fixes the pressure, so
every boundary prescribes its flux and phase 8 requires those to balance; global
conservation then forces the pinned cell's sum to the negative of all the
others, each zero to solve accuracy. Q6's own figures said this rather than the
opposite — `2.020606e-14` at the reference cell against `1.199041e-14`
elsewhere is the same order, not a structural break. **The finding is recorded
because it was credible and wrong, and because measurement is what settled it.**

### A suspected finding is CONFIRMED, and it is a real limitation

The correctness review suspected that `MomentumDiagonalNotPositive` refuses a
legitimate physical state, and said it had not built the mesh. This milestone's
own tests built it: upwind convection contributes a negative mass flux to the
diagonal at a zero-gradient-velocity boundary and diffusion contributes nothing
there, so a convection-dominated cell beside a pressure boundary assembles a
non-positive diagonal with nothing wrong anywhere. A ten-cell chain at 5 mm/s
with water gives `-8.085e-04` kg/s.

The refusal stands, because `d_P = V_P / a_P` has no usable value and SIMPLE has
no coefficient to correct with — but it is a **hard problem, not a defective
assembly**, and the header said the opposite. It is now `L9`, and it constrains
what this operator can run: the benchmark's inlet speed is derived from the cell
Peclet number rather than picked, because at `Pe = 200` the coupling refuses
rather than converges.

### Decision 9 is amended: the refusal order and the blame were both wrong

`advanceCfdCoupling` validated the settings and ran the predictor and
`cfdMassFlux` before its own gate, so a geometry built for another mesh was
reported as `SettingsInvalid` or `LinearSolveRefused` and phase 1 was
unreachable until after three linear solves. Worse, a caller's bad DENSITY
reached `cfdMassFlux`, whose refusal came back wrapped as
`EvaluationIsInconsistent` with `ErrorCode::Internal` — this module blaming
itself for the caller's input, which is the exact category error
`P19-MOM-001`'s review found in its own first draft. The phases that need
neither the predicted flux nor the diagonal now run first, in the declared
order.

### Two enumerators are deleted

`FieldIsNotFinite` and `Diverged` could never be produced: both appeared only in
the array and in `toString`. `CfdBoundary.hpp` states this project's rule — "An
enumerator no input can produce is worse than no enumerator, so the value is
absent rather than documented as unreachable" — and both were documented as
live refusals, one even cross-referenced as the in-iteration counterpart of
`ValueNotFinite`. Seventeen becomes sixteen, and a `static_assert` on the
array's size is added, which every sibling had and this operator did not.

### Decision 11 was effectively untested, and now is not

A slip wall's `Symmetry` velocity is the only treatment that makes the three
momentum matrices differ per component. Every fixture used a constant diagonal,
so the normal projection was always evaluated as a single value times a unit
sum, and the projection was never exercised — nor was the interpolation weight,
since interpolating between two equal values returns that value for every
weight. The suite now carries a slip-wall fixture and a per-cell,
per-component varying diagonal.

### What the test-adequacy review's structural finding changed

Its central point: the identity tests compare two PRODUCTION outputs, so they
are satisfied by **any** face coefficient — wrong by two, wrong by seven orders
of magnitude, taken from the wrong component, interpolated from the wrong side.
The corrected flux is INVARIANT under a uniform rescaling of the coefficient,
because the correction is a projection: the matrix scales, the correction
scales inversely, and their product does not move. That is exactly right, and
no amount of identity testing can reach it.

So the coefficient's VALUE is now pinned directly, against a varying diagonal,
with the oracle forming `d = V/a` from the geometry itself and grouping the
product differently. Nine other gaps the same review named are closed: the
predictor's velocity (read by no assertion, so a predictor returning zero would
have survived), the velocity correction's value, `continuityClosed == false`
(never observed), the momentum residual against the suite's own unused oracle, a
non-zero balanced compatibility flux (the only balanced case had a zero
throughput, so the assertion tested a hard-coded `0.0`), a reference cell that
is not cell 0, the strictness of the convergence comparisons, two-sided bounds
on the measured tolerances, and a benchmark that asserts CONVERGENCE rather than
merely running — which required feeding the pressure gradient back into the
momentum assembly, since without it the pressure was a pure output and nothing
could converge in the usual sense.

### Limitations added

```text
L7   `cfdCouplingVerdict` and `cfdCouplingBudgetExhausted` are noexcept and
     return a verdict and a bool, so they cannot refuse invalid
     CfdConvergenceSettings or CfdSteadySettings. They state the precondition
     and report `Continue` for settings that are not usable -- by decision
     rather than by accident, which is what a NaN tolerance produced before.
L8   `sum_i n_i^2 == 1` is RELIED ON and not verified per face. A non-unit
     normal would silently scale every face coefficient. The reliance is on
     CfdGeometry's invariant and a test asserts it over a real mesh; an earlier
     comment claimed this operator checked it.
L9   a non-positive assembled momentum diagonal is REACHABLE from a correct
     assembly, and is refused. SIMPLE needs a positive diagonal, so this
     operator is confined to the regime where the momentum discretisation
     provides one -- in practice a low cell Peclet number.
L10  no Rhie-Chow interpolation. The predicted flux is regenerated from the cell
     velocity each iteration and the conservative corrected flux is not fed back
     into the predictor, so the classic odd-even pressure mode is not
     suppressed. The compact two-point Laplacian is non-singular, so nothing
     refuses, and at convergence the correction goes to zero, so no residual can
     see it. `CfdMomentum.hpp` defers Rhie-Chow to `P19-PRES-001` by name.
     Raised by the correctness review as a SUSPECTED finding and recorded rather
     than resolved.
L11  the velocity correction applies no `Symmetry` projection at a slip wall,
     so such a cell gets a full three-component correction including a
     wall-normal part, while `assembleCfdDiffusion` suppresses that component in
     the matrix. Mass is still conserved, because the wall flux stays bit-zero,
     so this is an inconsistency between two operators and not a conservation
     break. Recorded because `CfdBoundary.hpp` makes it a live question.
L12  the symbolic pass is written a THIRD time in this module, after
     CfdDiffusion.cpp's and CfdConvection.cpp's. Extracting a shared one means
     editing two qualified operators to serve this milestone.
L13  the test fixtures are a SEVENTH copy. No shared cfd fixture header exists,
     and creating one means editing six test files belonging to qualified
     milestones -- a refactor combined with feature work.
```
