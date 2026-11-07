# ADR-072 — Convection is an upwind scatter whose row sum is the divergence, and boundedness is conditional rather than claimed

- Status: Accepted
- Date: 2026-11-07
- Milestone: P19-CONV-001 — Convective Flux
- Supersedes: nothing
- Amends: nothing. ADR-071 Decision 5's asymmetry is **relied on** here and
  restated, not changed.

## Context

`P19-CONV-001` is authorized with a fifteen-item checklist, pasted into the
instruction and byte-for-byte identical to the one already in `TODO.md`. It
asks for a convection discretisation: a mass flux `phi`, an interpolation
scheme, a first-order upwind baseline, a central scheme "if in scope", a sign
convention, internal-face conservation, boundary treatment, a boundedness
policy, three named tests, sparse assembly, determinism, regression and
evidence. The checklist also states a recommended first qualified scheme —
`first-order upwind` — "before higher-order schemes", and that recommendation
is read here as binding rather than advisory.

What already exists matters more than what is missing.

`ADR-066` put `phi` in the field table at `CfdFieldLocation::Face`, one
component, dimension `mass/time`, label `phi`. `ADR-071` fixed its sign — out
of the owner, matching the stored area vector — and its meaning: the
face-INTEGRATED `rho (U . Sf)` in kg/s, with no area factor, because the
integral is the conserved quantity. `ADR-067` froze the per-field boundary
table, whose `phi` column is `Computed` at both pressure boundaries and
`FixedValue 0` at all three walls. `ADR-069` built `CfdSparseMatrix`, a
validated CSR container whose only door is `fromCsr`, and assembled diffusion
into it as a per-component scalar system. `ADR-070` defined the face
interpolation weight and `ADR-069` Decision 7 exports it as
`cfdFaceInterpolationWeight`.

So this milestone adds one operator and one producer to a module that already
has a sparse container, a boundary table, a weight and a divergence. The
honest statement of novelty is small, and `CfdDivergence.hpp` says so in as
many words: "There is no convection scheme — `P19-CONV-001` defines `phi` and
its interpolation and is not authorized, so this operator consumes a `phi`
somebody else produced." That sentence is the brief.

### The measurements, made before any C++ existed

Four arithmetic claims this design rests on were measured first, with a fixed
seed (`202611072`), modelling the assembly exactly as the C++ would do it —
one pass over faces in `FaceIndex` order, scattering into owner and
neighbour, no vectorisation. `P19-DIV-001`'s first ADR draft predicted a
bit-exact global sum and was wrong; predicting again would have been the same
mistake twice.

The first revision of that script had three defects of its own, and the first
is a **design** finding rather than a measurement bug. It is recorded here
because the design changed because of it.

```text
Q1  per-face  max(phi,0) + min(phi,0) == phi
    200,000 random finite phi            0 mismatches
    8 edge values incl. signed zero      0 mismatches

Q2a rowSum(A,c) vs the same quantity accumulated in FACE order
    narrow 1e-2..1e2   33.34% bit-exact, worst relative 5.56e-16
    wide   1e-6..1e6   38.83% bit-exact, worst relative 6.33e-16

Q2b rowSum(A,c) + boundaryInflow(c)  vs  div(phi)(c)
    narrow             31.91% bit-exact, worst relative 5.56e-16
    wide               37.67% bit-exact, worst relative 6.33e-16

Q3a 1D chain, uniform flux, interior rows:  A(-phi) == A(phi)^T
    2000 chains        2000 BIT-EXACT (100.00%)

Q3b general mesh:  A(-phi) == A(phi)^T - diag(div phi)
    narrow             17.80% bit-exact, worst relative 4.93e-16
    wide               14.58% bit-exact, worst relative 5.62e-16

Q4  dominance deficit  ==  -divergence
    narrow             42.51% bit-exact, worst relative 5.92e-16
    wide               46.85% bit-exact, worst relative 5.79e-16
```

**The withdrawn figure.** Revision 1 reported a Q2 "relative gap" of
`4.34e+13` and that number is withdrawn, because the oracle was wrong rather
than the arithmetic. It compared the matrix row sum against the divergence of
`phi` — two different quantities. At an inflow BOUNDARY face the convective
flux is a SOURCE and never a coefficient, so the row sum cannot contain it:

```text
rowSum(A,c) = sum_internal ±phi  +  sum_boundary max(phi, 0)
div(c)      = sum_internal ±phi  +  sum_boundary      phi
differ by                           sum_boundary min(phi, 0)
```

A large real difference divided by a small row magnitude produced the absurd
figure. Decision 5 exists because of this, and the operator reports the inflow
term so the cross-check is expressible by a consumer rather than re-derived.

Revision 1's other two defects were measurement bugs and are recorded for the
same reason `P19-DIV-001` recorded its own: Q4's "worst" was assigned only
inside the failure branch, so `0` meant "nothing exceeded the threshold" and
read as "always exactly equal" — the same family as a hand-typed count
standing beside a computed one; and Q3b drew fluxes across twelve orders of
magnitude, so its relative figure measured conditioning rather than the
identity, which is why a narrow and a wide range are now reported separately.

---

## Decision 1 — `phi` gets a PRODUCER in this module, and the sign convention is `ADR-071`'s, restated and tested rather than re-decided

`phi`'s layout, units and sign are already fixed. What does not exist is a
named function that makes one, so every consumer so far has invented its own.
This milestone exports the producer:

```text
cfdMassFlux(topology, geometry, boundary, {currentVelocity, density})
    -> Result<std::vector<double>>      one per face, FaceIndex order, kg/s
```

A positive value is flow from the owner to the neighbour across an internal
face, and out of the fluid at a boundary face. That is `ADR-071`'s sentence
unchanged, and this milestone adds a test that pins it rather than a second
definition that could drift.

### Rejected: leave `phi` caller-supplied and only document the convention

This is what `ADR-071` Decision 5 assumed, and it is why the checklist's first
item exists. A convention with no producer is prose: every caller writes
`rho * (U_f . S_f)` again, and the sign is re-derived each time. The producer
makes items 1 and 5 testable.

### Rejected: store `phi` on the document or in a field object

`ADR-068` expanded the initial field into the caller's array and kept the
uniform record as the only authority, for the reason that a derived array is
not canonical state. `phi` is derived from the velocity and the density, so it
is returned, not stored.

## Decision 2 — A wall's `phi` is set to EXACTLY `0.0` from the table, never computed from the wall velocity

`ADR-067`'s table gives all three walls `FixedValue 0` down the `phi` column.
The producer honours that literally. It does **not** evaluate
`rho (w . S_b)` and rely on the result being small.

The reason is arithmetic, not tidiness. A `MovingWall` prescribes a
TANGENTIAL velocity `w` — `P19-BC-001`'s validation requires tangency — so
`w . S_b` is zero in exact arithmetic and is **not** bit-zero once the
centroid, the area vector and the velocity have each been rounded. A computed
wall flux therefore leaks a tiny non-zero mass flow through a solid boundary,
which breaks conservation in a way no test of the wall itself would notice: it
shows up as an imbalance somewhere else. The table's zero is both correct and
bit-exact.

A mutation that replaces the zero with the computed dot product is one of this
milestone's probes.

### Rejected: compute it and assert it is below a tolerance

That converts a structural zero into a tolerance, and the tolerance would have
no derivation — `ADR-063` finding F1 measured the reference meshes at 31 to 66
degrees mean non-orthogonality, so the rounding is mesh-dependent and the
bound would be a number chosen to pass.

## Decision 3 — The traversal is a face-centred SCATTER, and the matrix is ASYMMETRIC by nature, so no symmetry tolerance is ever required of it

One pass over faces in `FaceIndex` order. Each internal face computes its
upwind coefficient once and writes it to the owner's row and its negation to
the neighbour's row in the same visit, which is `ADR-071` Decision 2's reason
restated: a second visit cannot disagree with the first if there is no second
visit.

The matrix is **not symmetric**, for either scheme, and that is a positive
property of convection rather than a defect to be bounded. Upwind writes
`A(P,P) = +q` and `A(N,P) = -q` for an outflow face and `A(P,N) = q`,
`A(N,N) = -q` for an inflow one; central writes `A(P,N) = q(1-w)` against
`A(N,P) = -q w`. Neither pairs into a symmetric entry unless `q` is zero.

So `assembleCfdConvection` passes **no** `requiredSymmetryTolerance` to
`fromCsr`, and there is no `kCfdConvectionSymmetryTolerance` to mirror
`kCfdDiffusionSymmetryTolerance`. A constant demanding symmetry of this
operator would be wrong, not merely slack.

### Rejected: a cell-centred gather, for symmetry with `ADR-070`

Rejected for `ADR-071` Decision 2's reason, and one more. A gather visits each
internal face twice and must decide the upwind direction twice; if the two
visits disagree — because one reads `phi` and the other `-phi` and the
comparison is `>= 0` — the face contributes from both cells at once and
conservation fails silently. A scatter cannot express that error.

## Decision 4 — First-order upwind is the qualified baseline; central differencing is selectable, and its unboundedness is reported rather than corrected

The checklist recommends upwind "before higher-order schemes" and lists
"Central scheme if in scope" as a separate item. Both are implemented and the
scheme is a parameter:

```text
enum class CfdConvectionScheme { Upwind, Central };
```

Upwind takes the face value from the upwind cell. Central interpolates with
`cfdFaceInterpolationWeight`, the ONE weight definition, and adds no copy of
it.

Central is second-order and **unbounded**: it can overshoot, and on a mesh
where the cell Peclet number exceeds two it produces oscillations. Nothing
here corrects that, limits it, or blends the two schemes. The summary names
the scheme that was used and states that boundedness is guaranteed for
`Upwind` only; a caller that chooses `Central` is choosing accuracy over
boundedness knowingly.

### Rejected: a blended or deferred-correction scheme

A limiter or a deferred correction is a third scheme with its own
boundedness proof, its own tests and its own failure modes. The checklist
names two and recommends one; building a third would be the overbuilding
`CLAUDE.md` forbids, and it would arrive untested beside two schemes that are.

### Rejected: implement upwind only, and read "if in scope" as "out of scope"

Central costs one branch and one call to a weight that already exists and is
already tested over a real mesh's range. Leaving it out would also leave the
boundedness policy with nothing to contrast against: "upwind is bounded" is a
weaker statement when no unbounded alternative is present to fail.

## Decision 5 — The row sum is the interior-upwind outflow, and the cross-check against `ADR-071` needs the PRESCRIBED INFLOW restored, which the summary reports

In real arithmetic the row sum of either scheme is the net mass outflow over
the faces that contribute a coefficient:

```text
upwind   row P:  sum_f [ max(phi_f,0) + min(phi_f,0) ]  =  sum_f phi_f
central  row P:  sum_f [ phi_f w + phi_f (1-w) ]        =  sum_f phi_f
```

Scheme-independent, which is what makes it worth checking: it is the
constant-field transport property (item 9) and the boundedness condition
(item 8) at once. Transporting a constant `c` gives `c sum_f phi_f`, so a
conservative flux transports a constant exactly.

**Scheme-independent in real arithmetic, and NOT equally exact in floating
point.** The implementation writes one face's two coefficients as
`phi * s` and `phi * (1 - s)`, where `s` is the owner's share. Measured over
200,000 random fluxes:

```text
Upwind   s is exactly 0 or 1    bit-exact on 200,000/200,000, worst relative 0
Central  s in (0, 1)            bit-exact on 178,457/200,000 (89.23%), worst 2.22e-16
```

Upwind is exact per face because `phi * 1.0` is `phi` and `phi * 0.0` is a
signed zero, so a face contributes its flux and nothing else. Central sums two
rounded products and need not recover `phi`, losing up to two eps per face.
Both stay far inside `kCfdConvectionRowSumTolerance`, but the claim is stated
per scheme rather than left as "scheme-independent", which would be true of the
mathematics and false of the arithmetic.

But an inflow boundary face may contribute to the SOURCE instead of the
matrix, and **which** faces do is narrower than "all inflow faces". That
correction was found while writing the header, one level below the one the
experiment found, and it is recorded rather than quietly fixed:

```text
boundary face, outflow (phi >= 0)          -> diagonal += phi      in the matrix
boundary face, inflow, FixedValue          -> source  -= phi v_b    NOT in the matrix
boundary face, inflow, ZeroGradient        -> diagonal += phi      in the matrix
boundary face, inflow, Symmetry            -> diagonal += phi      in the matrix
```

An inflow face leaves the matrix **only when the transported variable is
`FixedValue` there**. At a `PressureInlet` the `U` column is `ZeroGradient`,
so the face value IS the owner's and that face's negative `phi` lands on the
diagonal like any other. A first draft of this decision wrote the correction
term as `sum_boundary min(phi, 0)` — all inflow — which is right only if every
inflow face prescribes a value, and a pressure inlet is the counterexample.

So the reported term is the **prescribed** inflow, and the identity is

```text
rowSumSi(c) + prescribedInflow(c)  ==  divergeCfdMassFlux(phi).at(c)

prescribedInflow(c) = sum over c's boundary faces with phi < 0
                      AND the transported variable FixedValue, of phi
```

**to a tolerance of about `1e-13` relative, never bit-exactly.** Measured over
20,000 mixed-policy meshes at `7.63e-15` worst in a narrow flux range and
`1.94e-14` in a wide one, bit-exact on 31.25% to 36.44% — because `rowSumSi`
accumulates in ascending column order, `divergeCfdMassFlux` accumulates in
face order, and the inflow term is a third accumulation. The same addends in a
different order are a different double. A test asserting bit-equality here
would fail on a correct operator, which is `P19-DIV-001`'s recorded mistake
and the reason this was measured first. The bound is `1e-13` rather than
`1e-15` because three sums meet, not one.

**And the measurement is what established that the first formula was wrong.**
Run against mixed boundary policies, `sum_boundary min(phi,0)` is exact on
only 10.26% to 11.96% of meshes and departs by a relative `0.9995` on 20,815
of 139,802 cells — a finite, structural error, not a rounding one. The
corrected term is exact to half an eps on the same meshes. Had the first
formula gone into a test with a loose tolerance it would have looked like a
conditioning problem and been "fixed" by widening the bound.

### Rejected: a `rowSumIsTheDivergence` boolean on the summary

`ADR-071` Decision 8 withdrew exactly this shape of flag, and `CfdDivergence.hpp`
records why: a flag that cannot read false is decoration that reads as
evidence. This one could read false, but only by being a tolerance comparison
the operator performs on itself with a tolerance it chose — so the number is
reported and the comparison is left to the suite, which is the same resolution
`ADR-071` reached for its closure.

## Decision 6 — The boundary treatment is upwind too, and `phi`'s own `Computed` is ACCEPTED while the transported variable's is REFUSED

At a boundary face with flux `phi_b`, the convective flux is
`phi_b * value_at_face`, and the face value is the upwind one:

```text
phi_b >= 0   outflow   the upwind value is the OWNER's
                       ->  diagonal += phi_b
phi_b <  0   inflow    the upwind value is OUTSIDE the domain, so it comes
                       from the boundary condition:
     FixedValue    ->  source -= phi_b * value_b          (a source, not a coefficient)
     ZeroGradient  ->  diagonal += phi_b                  (face value IS the owner's)
     Symmetry      ->  diagonal += phi_b                  (tangential is zero-gradient)
     Computed      ->  REFUSED
```

**A `Symmetry` face is treated as zero-gradient here, and that is not the same
choice `ADR-071` Decision 7 made.** Its reason was that a symmetry plane's
NORMAL flux is zero by the policy's own definition, so a divergence may skip
it. That reason does not transfer: convection transports a TANGENTIAL
component across the face too, and the symmetry condition prescribes the
tangential components as zero-gradient, so the face value is the owner's. The
distinction does not change any number a correct caller sees — `ADR-067` gives
all three walls `phi = FixedValue 0`, so a slip wall's `phi_b` is exactly zero
and both treatments contribute exactly nothing — but skipping the face would
break Decision 5's identity for any hand-made `phi` that is non-zero there,
and silently. Treating it by the rule keeps the identity exact for every
input.

**THE SCHEME APPLIES TO INTERNAL FACES ONLY.** `Central` changes how an
internal face's value is interpolated between two cells; a boundary face has
one cell and a boundary condition, so it is treated by the rule above whatever
the scheme. This is worth stating because `cfdFaceInterpolationWeight` returns
**`1.0`** for a boundary face — a weight that is perfectly correct for
viscosity interpolation and would, if fed to the central branch, make the face
value the owner's and **silently discard a velocity inlet's prescribed
value**. A mutation that routes boundary faces through the scheme branch is
one of this milestone's probes.

The asymmetry is `ADR-071` Decision 5's, relied on in both directions and
stated because a defensive reading gets it backwards:

- **`phi`'s own policy is never consulted.** This operator takes `phi` as a
  face array exactly as `divergeCfdMassFlux` does, and `phi` is `Computed` at
  both pressure boundaries because the flow rate there is an outcome of the
  solve. By the time anyone assembles convection the solve has produced it.
  Refusing `Computed` on this path would make the operator unusable at every
  pressure boundary.
- **The TRANSPORTED variable's `Computed` is refused**, for the reason
  `CfdDiffusion.hpp` gives of `VelocityPolicyIsAnOutcome`: the switch must be
  total over four policies, the `U` column uses three, and the only correct
  answer for the fourth is a refusal. Falling through to the owner's value
  would silently admit transport of an unknown.

**There is NO compile-fail case for this asymmetry, and an earlier draft of
this decision claimed one.** An independent review checked the compile-fail
group's five enumerated claims and found none of them is the asymmetry. Nor
could one be: the asymmetry is a runtime branch — phase 9 refuses the
velocity's `Computed` while nothing anywhere consults the `phi` column — and
there is no type-level expression of "this policy value is accepted here and
refused there". The half that is observable is tested at runtime by
`CfdConvection_AcceptsAComputedMassFluxPolicyAtBothPressureBoundaries`; the
other half is the ABSENCE of a check, which no test can observe and which is
recorded in `CfdConvection.hpp` instead.

### Rejected: take `phi` per boundary face from the prepared boundary

`ADR-071` Decision 5 rejected this already, and the reason is unchanged: the
prepared boundary holds a POLICY for `phi`, not a value, because at a pressure
boundary there is no value to hold until a solve produces one.

## Decision 7 — Boundedness is CONDITIONAL on a conservative `phi`, and the dominance deficit IS the divergence, so no second number is invented

For upwind every INTERNAL face contributes `max(phi_f,0)` to the diagonal and
`min(phi_f,0)` to an off-diagonal, so the interior signs are right by
construction. The dominance deficit of a row is

```text
sum_{j != i} |a_ij|  -  |a_ii|   ==   -rowSum(i)
                                 ==   -div(phi)(i) + prescribedInflow(i)
```

**The second line is a correction, and the first published form of this
decision was WRONG.** It read `deficit == -div(phi)(i)`, dropping the
prescribed-inflow term — which is `sum_boundary min(phi,0)` all over again, the
"withdrawn figure" error of Decision 5 repeated in the neighbouring decision,
one paragraph after that decision corrects it. An independent review derived it
and the measurement confirms: over 6,000 meshes with prescribed inflow the
published form is exact on 8.82% with a worst relative departure of `1` — a
finite error — while the corrected form is exact on 32.23% at `3.74e-16`. The
reason the first measurement missed it is that it ran with NO BOUNDARY FACES AT
ALL, where the two forms coincide.

**AND THERE IS A THIRD CONDITION, which neither the published form nor the
review stated.** The deficit is defined with `|a_ii|`, and the diagonal is only
non-negative while no backflow face drives it negative. Measured over 6,000
zero-gradient-only meshes, BOTH forms are exact on just 17.35% with a worst
relative departure of `2`: once `a_ii < 0`, `|a_ii| = -a_ii` and the identity
breaks whatever the inflow term is. So the identity's scope is

```text
deficit(i) == -div(phi)(i) + prescribedInflow(i)     while a_ii >= 0
```

and `a_ii >= 0` is exactly "no backflow at a zero-gradient boundary", which
`CfdConvectionSummary::backflowFaces` counts. That count is therefore not a
diagnostic beside the identity; it is the identity's validity condition.

With that scope, the matrix is **weakly diagonally dominant with equality
exactly when `phi` is discretely conservative and no inflow is prescribed**,
and the amount by which boundedness fails is the amount by which continuity
fails — a quantity `ADR-071` already measures and reports as
`relativeImbalance`.

**There is a second way boundedness fails, and it is not the scheme's fault.**
An inflow boundary face whose transported variable is `ZeroGradient` puts its
`phi_b` — which is NEGATIVE on inflow — straight onto the diagonal, because
the face value is the owner's. That subtracts from the diagonal while adding
nothing to the off-diagonals, so it reduces dominance directly. This is
backflow at a pressure boundary, it is a real and well-known weakness of a
zero-gradient inlet, and it is what `ADR-067`'s table prescribes, so the
operator implements it rather than correcting it. `CfdConvectionSummary`
counts those faces as `backflowFaces` so a reader can see whether any
occurred; the count can read zero or non-zero and is not a flag that cannot
fail.

The policy is therefore stated and not enforced: upwind is bounded given a
conservative `phi` and no backflow at a zero-gradient boundary; both defects
are measured, reported, and never assumed. `CfdConvectionSummary` carries the scheme and the inflow term, and
points a reader at the divergence for the rest. Nothing here refuses a
non-conservative `phi`, because `ADR-063` finding F1 reserves a quality
threshold to the owner and `ADR-071` refused the same thing for the same
reason.

### Rejected: refuse a `phi` whose divergence exceeds a threshold

Not this milestone's to decide, and the threshold would be invented. A caller
that wants the check has `divergeCfdMassFlux` and its measured imbalance.

### Rejected: a `worstDominanceDeficit` summary member

It would be a second name for `-relativeImbalance`, computed by a different
accumulation, and the two could disagree at half an eps while describing the
same fact. The identity is documented and tested instead.

### Correction: this decision used to contradict itself one paragraph apart

Its opening sentence read "the diagonal is `sum_f max(phi_f,0) >= 0` ... so the
signs are right by construction", and three paragraphs later it says a
zero-gradient inflow face "puts its `phi_b` — which is NEGATIVE on inflow —
straight onto the diagonal". Both cannot be true, and an independent review
pointed at the pair. The opening claim is now scoped to INTERNAL faces, which
is where it holds, and the boundary's contribution is where the third condition
above comes from. Both tests that could have caught the contradiction used a
walls fixture, where every boundary `phi` is exactly zero — fixtures that
exclude the counterexample this decision names itself.

## Decision 8 — Reverse flow is the exact transpose relation, bit-exact on a 1D chain and a tolerance elsewhere

Reversing every flux swaps the two cells' roles at every face, which gives

```text
A(-phi)  ==  A(phi)^T  -  diag(div phi)
```

**for `Upwind`, and only where no boundary face is `ZeroGradient` or
`Symmetry`. The first published form of this decision stated it unqualified and
was WRONG in two independent ways**, both found by an independent review and
then measured.

The derivation — "reversing a face's flux swaps the two cells' roles" — is
UPWIND-SPECIFIC. The upwind share is `1` or `0` and does swap; a central share
is geometric and does not. With owner share `w`:

```text
A(-phi)(P,P)  =  -phi w          required: A(phi)^T(P,P) - div(P) = phi(w - 1)
equal only when w = 1/2; the off-diagonal gap is phi(2w - 1)
```

And at a boundary face whose transported variable is `ZeroGradient` or
`Symmetry`, the full `phi_b` sits on the diagonal, so reversing the sign FLIPS
that term rather than removing it: `A(-phi)(P,P) = -phi_b` where the identity
requires `0`. That is every pressure inlet and outlet, since `ADR-067` gives
both `U = ZeroGradient`.

The measurement, over 4,000 meshes per cell:

```text
Upwind   FixedValue boundaries only    19.25% bit-exact, worst relative 4.36e-16
Upwind   ZeroGradient boundaries        0.00%             worst relative 1
Upwind   mixed                          4.10%             worst relative 1
Central  any policy                     0.00%             worst relative 2.1 to 2.7
```

A relative departure of `1` or `2` is a finite error, not rounding — which is
how the published claim's own figures should have been read. The earlier
"general mesh ... worst relative `4.93e-16`" overstated what had been measured:
that script had no central branch at all, and modelled every boundary face as
`A[p][p] += max(q,0)`, which is the FixedValue rule. The measurement was of a
model narrower than the claim.

So the identity's scope is `Upwind`, with no zero-gradient or symmetry boundary
face carrying a non-zero flux. This is the reverse-flow test (item 11), and the
suite asserts it exactly there: on a 1D chain with a uniform flux and walls,
where every boundary `phi` is the table's exact zero and an interior cell's
divergence is bit-zero, the transpose relation was **bit-exact on 2000 of 2000
chains**. Nothing asserts it for `Central`, because it is false there.

## Decision 9 — The interpolation WEIGHT keeps exactly one definition, and the vector face-velocity APPLICATION is duplicated with `CfdDivergence.cpp`'s, which is stated rather than claimed unified

`ADR-071` Decision 6 claimed the weight "has exactly one definition, and both
operators read it" and that was false when it was written — there were three
copies — so this ADR states the true count before anyone has to find it.

- **The weight**: one definition, `cfdFaceInterpolationWeight`, declared in
  `CfdDiffusion.hpp` and exported. This milestone adds **no** copy. Central
  differencing calls it; so does the producer.
- **The application**: forming a face velocity from two cell velocities and a
  weight is three lines of arithmetic, and it now exists twice — privately in
  `CfdDivergence.cpp`, for the velocity divergence, and in
  `CfdConvection.cpp`, for the `phi` producer. These must agree.

They are **not** unified, and the reason is that unifying them means changing
a qualified operator's source to serve a new consumer, which `CLAUDE.md`
forbids combining with feature work and `ADR-063` Decision 7 refused on the
same grounds. Instead the agreement is **pinned by a test** rather than
asserted by a comment: `divergeCfdMassFlux(cfdMassFlux(U, rho))` must equal
`rho` times `divergeCfdVelocity(U)` per cell, so a divergence between the two
interpolations fails a test instead of producing two slightly different
answers. The duplication is recorded as a known limitation.

### Rejected: export `CfdDivergence.cpp`'s private helper

Additive and small, but it is new public API on a milestone whose checklist
is closed, and it would make this milestone's diff touch a qualified
operator's header for a convenience. The test is the stronger guarantee
anyway: an exported helper both callers use could still be wrong, while the
cross-check would catch it.

### Rejected: a new `CfdInterpolation.hpp` holding the weight and both applications

The right shape, and the wrong milestone. Moving `cfdFaceInterpolationWeight`
out of `CfdDiffusion.hpp` touches `P19-DIFF-001`, `P19-GRAD-001` and
`P19-DIV-001` at once — a refactor of three qualified operators, combined with
a feature, which is exactly what the working order forbids. Recorded as the
extraction a later milestone should make.

## Decision 10 — The assembly returns a TAGGED failure, which is what makes the output-judging refusal observable

`assembleCfdConvection` returns

```text
std::expected<CfdConvectionSystem, CfdConvectionFailure>
```

and not `Result<CfdConvectionSystem>`. `Result<T>` is
`std::expected<T, Error>`, so a refusal carries a message and no tag.

This diverges from `assembleCfdDiffusion`, deliberately, and the reason is
`P19-DIV-001`'s recorded limitation L10. Its `DivergenceIsNotFinite`
enumerator judges the OUTPUT, so no predictor can reach it — a predictor that
computed the answer would be doing the work it exists to avoid — and because
the entry points return a plain `Error`, that enumerator is observable only
through the wording of a message. `CfdDivergence.hpp` states the house rule it
sits against, `CfdBoundary.hpp`'s "a message is not a tag", and records that
adding a tagged channel was new public API its checklist did not contain.

This operator's checklist is a different one and its return type is being
chosen now rather than changed later, so the tagged channel is chosen. The
precedent is in the same module and the same layer:
`CfdSparseMatrix::fromCsr` returns `std::expected<CfdSparseMatrix,
CfdSparseFailure>` for exactly this reason, and `CfdSparse.hpp` says so — "THE
TAG IS THE POINT. P19-BC-001's review found eight refusals whose tags could be
swapped with every message assertion still passing."

So `AssembledValueNotFinite` is a tag a test reads, not a sentence a test
greps, and this milestone does not repeat L10. `cfdConvectionFailure` and
`cfdConvectionProblem` still exist as predictors for the input phases, with
the limitation every predictor in this module has: they share one decision
path with the assembly, so their agreement cannot fail.

### Rejected: mirror diffusion's `Result` for consistency

Consistency with a shape that a later milestone recorded as a limitation is
not a reason to repeat it. The inconsistency is one line in two headers and is
stated in both directions; the alternative is an unobservable tag.

## Decision 11 — Determinism is the stored face order and the ascending column order, and nothing else

The assembly walks faces in `FaceIndex` order and writes each row's columns in
ascending order, which `CfdSparseMatrix` requires anyway. No set, no map
iteration, no parallelism, no accumulation that depends on anything but the
topology. `multiply` is already specified as a serial sum in row and ascending
column order, so a determinism test over the assembled system means something.

## Decision 12 — What this milestone refuses to do

- **No solve.** `CfdSparseMatrix` is a container; admitting a solve kernel is
  `P19-LINSOLVE-001`'s decision. Nothing here produces a transported field, so
  no transport result is claimed.
- **No momentum assembly.** Convection is one term. `P19-MOM-001` sums it with
  diffusion and a pressure gradient; that coupling is not authorized and is
  not done. Both operators give a POSITIVE diagonal and are summed directly,
  which is stated here so the convention is on record before the milestone
  that needs it.
- **No transient term, no time scheme.** `div(phi x)` only.
- **No limiter, no blending, no Peclet-based switching.**
- **No Peclet number reported.** It needs a viscosity, which convection does
  not take; reporting one would couple this operator to diffusion's input for
  a diagnostic. It belongs where both meet.
- **No quality threshold and no refusal of a non-conservative `phi`**, per
  Decision 7.
- **No variable density.** `phi` carries one `rho`, as `ADR-058` limits P19 v1.

## Consequences

A caller can now produce a `phi` from a velocity and a density through one
named function with a tested sign convention, and assemble either an upwind or
a central convection matrix for one component into the same CSR container
diffusion uses, with a positive diagonal and no symmetry requirement. The row
sum is the divergence up to the reported boundary inflow, so continuity and
boundedness are the same measurement.

What a reader must not assume: nothing consumes this operator. It is one term
of an equation `P19-MOM-001` and `P19-PRES-001` have not assembled, exactly as
`P19-GRAD-001`'s gradient and `P19-DIV-001`'s divergence are, and no flow field
exists at the end of this milestone.
