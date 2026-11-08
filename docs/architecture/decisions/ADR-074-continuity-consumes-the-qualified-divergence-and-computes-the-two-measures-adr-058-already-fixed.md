# ADR-074 — Continuity consumes the qualified divergence, computes the two measures ADR-058 already fixed, and reports inlet/outlet without checking one against the other

```text
Status      Accepted
Date        2026-11-08
Milestone   P19-CONT-001 — Continuity Equation
Supersedes  nothing
Amends      nothing.  It BINDS to ADR-058's mass-balance measures, ADR-066's
            `phi`, ADR-067's boundary table, ADR-071's divergence and ADR-072's
            mass-flux producer, and changes none of them
```

## Context

The checklist asks for a continuity equation: `div U = 0`, or in finite-volume
form `sum_f rho (U_f . S_f) = 0`. Four things already in the tree decide most of
how that must be built, and the first two were found by reading the tree rather
than by designing against it.

**ADR-058 ALREADY FIXED THE TWO MEASURES, AND IT FIXED THEM AGAINST A TRAP THIS
CHECKLIST WALKS INTO.** Its section is titled *"Mass balance means a discrete
measure, not a sum of inlets and outlets"*, and the reason is that with
pressure-only boundaries the outflow is an **outcome** of the solve, so the check
cannot be "outlet equals the prescribed inlet". The two measures are

```text
(a) cell continuity residual     sum_f phi_f            per cell
(b) global boundary imbalance    sum over boundary phi
    each relative to the throughput  0.5 * sum |phi|  over the boundary
```

This checklist's item 7 is *"Inlet/outlet mass-balance check"* — in those words,
the thing ADR-058's heading says mass balance is not. The invariant wins:
inlet and outlet flow are **reported**, and the **check** is (b).

**`CfdData.hpp` ALREADY DECLARES THE ANSWER.** `CfdMassBalance` carries
`maxCellContinuity` and `globalImbalance`, and its comment says the measures are
fixed there "so that no later milestone can quietly report a different one" —
and assigns the throughput **floor** and the **acceptance thresholds** to
`P19-MASSBAL-001` by name. So this milestone computes two declared measures; it
defines neither, and it sets no threshold.

**`P19-MASSBAL-001` OVERLAPS THIS CHECKLIST BY SEVEN ITEMS.** Cell continuity
residual and Deterministic aggregation are identical; Relative tolerance /
Absolute tolerance and Multiple inlets / Multiple outlets are this checklist's
items split in two; Net mass imbalance, Global mass residual, Inlet mass flow and
Outlet mass flow cover three more. The overlap is the checklists', not this
implementation's, and `TODO.md`'s authorization note records the boundary. That
milestone is **not authorized** and nothing here anticipates it.

**THE PIECES THIS CONSUMES ARE ALL QUALIFIED.** `cfdMassFlux` (ADR-072) produces
`phi`, one per face in `FaceIndex` order, SI kg/s, signed out of the owner, and
exactly `0.0` at all three wall kinds. `divergeCfdMassFlux` (ADR-071) already
returns the per-cell net outflow `sum_f ±phi_f`, `netBoundaryFlux()`,
`globalNetFlux()` and a relative accumulation measure. `PreparedCfdBoundary`
(ADR-067) carries the condition kind per boundary face, and `CfdTopology` carries
`patchOf(BoundaryFaceIndex)`.

### A name collision that would have made every mass balance pass

```text
CfdDivergenceField::globalImbalance()  =  globalNetFlux() - netBoundaryFlux()
                                          a FLOATING-POINT accumulation artifact
                                          ADR-071 measured it: bit-zero on
                                          40.34%, worst 1.125e-16 relative

CfdMassBalance::globalImbalance        =  |sum over boundary phi| / throughput
                                          THE PHYSICAL mass imbalance
```

**Same name. Different quantity. Different order of magnitude. Wiring the first
into the second passes every mass balance, always**, because the artifact stays
near `1e-16` however badly the flow violates conservation. Nothing in the type
system prevents it: both are a `double` reached through a member called
`globalImbalance`. This was found in the UNDERSTAND phase, before any code, and
Decision 3 is the defence.

### The measurements, made before any C++ existed

Seed `20261108`, pure-Python model of the operator's own arithmetic. Script and
output are in `docs/verification/P19-CONT-001/experiments/`.

**THIS SENTENCE USED TO SAY "in the summation order the operator will use", AND
THAT IS TRUE OF Q1, Q2, Q4 AND Q5 BUT NOT OF Q3.** A review read
`buildCfdTopology` rather than this claim and found that the boundary range is
sorted by `(patch, ownerSlot, key)` — `CfdTopology.cpp`'s patch CSR depends on
it and says so: *"The boundary range is ordered by patch, so each patch's faces
are contiguous and the starts are a prefix count."* So each patch's faces are a
**contiguous block** of `BoundaryFaceIndex`, and Q3's model deals them
round-robin. Q3 therefore measures a different associativity from the one the
operator performs.

The consequence is bounded and is recorded rather than papered over: contiguous
re-association rounds no worse than interleaved, so `1e-14` is if anything more
conservative than the measurement suggests — but the bound is **not** currently
backed by a measurement of the arithmetic it bounds, and two test comments that
claimed the patches were "interleaved ... so the two accumulation orders
genuinely differ" were simply wrong about the topology. Both are corrected.

```text
Q1  per-cell residual of a field CONSERVATIVE BY CONSTRUCTION
    (uniform U, phi_f = rho (U . S_f), summed over a closed tet)
      metre-scale tets         worst 3.2789e-15   bit-zero 19.86%
      millimetre-scale tets    worst 7.4598e-15   bit-zero 18.73%
      worst mesh closure       4.2666e-16 / 7.0816e-16
    THE RESIDUAL IS ABOUT TEN TIMES THE CLOSURE, so the closure does NOT bound
    it: the dot products round too, not only the area sum. ADR-071 recorded that
    bit-zero closure and bit-zero constant-field divergence are not the same
    set; this quantifies the gap in the direction that matters here.

Q2  (outflow - inflow)  vs  the single-pass boundary net
      balanced fields    worst 5.8386e-16   bit-exact  1.02%
      unbalanced         worst 9.0866e-16   bit-exact 12.68%

Q3  sum of per-PATCH subtotals  vs  the single-pass boundary net
      balanced fields    worst 3.1237e-16   bit-exact  1.93%
      unbalanced         worst 9.6334e-16   bit-exact 18.64%

Q4  closed domain, every facet a wall, phi = 0.0 by ADR-067's table
      non-zero boundary nets   0 / 5000

Q5  discrimination of the denominator 0.5 * sum |phi|
      balanced field     worst 5.6167e-16
      unbalanced field   worst 1.9590e+00
```

**Q2 AND Q3 ARE THE REASON THIS ADR EXISTS IN ITS PRESENT FORM.** A reader would
expect `outflow - inflow` to be the net, and it is — in real arithmetic. In
floating point it is bit-equal on **1.02%** of balanced fields. ADR-071's first
draft predicted a bit-exact global sum and was wrong; predicting a bit-exact
directional split here would have been the same error one milestone later.

Q5 bounds the relative measure: all flow one way gives `|net| = sum|phi| =
2 * throughput`, so **the relative global imbalance lies in `[0, 2]`**, with 2
meaning totally unbalanced. That is an invariant worth asserting, and it is why
`0.5` is in the denominator rather than `1.0`.

## Decision

### Decision 1 — Continuity CONSUMES the qualified divergence; it does not re-walk the interior

Three candidates, compared on the axes `CLAUDE.md` names.

```text
                        A consume            B own traversal      C extend
                        divergeCfdMassFlux   over all faces       CfdDivergence
correctness             inherits a qualified inherits nothing     inherits, but
                        scatter              re-derives the sign  changes it
architecture fit        extends an existing  a second traversal   edits a
                        abstraction          of the same faces    QUALIFIED
                                                                  operator
complexity              boundary walk only   full face walk       new optional
                                                                  parameter
persistence             none (derived)       none                 none
determinism             divergence's order   its own order         divergence's
                        + a boundary pass    to defend             order
performance             two passes over      one pass              one pass
                        boundary faces
extensibility           per-patch attribution the same             forces every
                        is additive                                caller to care
failure modes           sub-operator refusal  a sign error here    re-qualifies
                        is PREDICTED          cannot be caught     P19-DIV-001
                                              by comparison
migration cost          none                 none                 ALL callers
testability             decomposition is      nothing independent  entangled
                        checkable against     to check against
                        the sub-operator
```

**A is chosen.** C is rejected outright: `P19-DIV-001` is qualified, its public
API is not this checklist's to change, and `P19-CONV-001` already recorded
leaving a duplicated three-line interpolation in place rather than "editing a
qualified operator for a convenience". B is rejected because a second traversal
that re-derives the owner/neighbour sign is precisely the error ADR-072 Decision
3 says a scatter cannot express — and with B there is no independent operator
left to check the answer against.

So `evaluateCfdContinuity` calls `divergeCfdMassFlux` for the per-cell residuals
and the global sums, and walks **boundary faces only** for attribution. This is
ADR-073 Decision 1's pattern — momentum is the sum of qualified operators —
applied to a measurement instead of an assembly.

### Decision 2 — The two measures are ADR-058's; this milestone computes them and defines none

`maxCellContinuity` is `max over cells |sum_f phi_f|` and `globalImbalance` is
`|sum over boundary phi|`, each divided by `0.5 * sum |phi|` over the boundary.
Item 1, "Define mass-flux convention", is satisfied by **binding** to ADR-066's
`phi` and stating the convention in the header — not by defining a second one.
Three milestones each invented `phi` privately before `P19-CONV-001` made one;
a fourth definition is the "second parameter system" `ARCHITECTURE.md` rejects.

### Decision 3 — The two `globalImbalance` spellings are separated by TYPE, not by discipline

`CfdContinuity` never touches `CfdDivergenceField::globalImbalance()`. The
physical measure is built from `netBoundaryFlux()` alone, and the accumulation
artifact is carried under a **different name**, `accumulationResidual`, so that
no assignment between the two compiles into silence.

```text
reported as                          built from
massBalance.globalImbalance          |netBoundaryFlux()| / throughput
accumulationResidual                 divergence().summary().relativeImbalance
                                     CARRIED VERBATIM -- see the amendment below
```

**AMENDED 2026-11-08, THE SAME DAY: `accumulationResidual` WAS RE-DERIVED HERE
WITH THE WRONG DENOMINATOR, AND TWO INDEPENDENT REVIEWS FOUND IT.** The first
implementation computed

```text
|globalNetFlux - netBoundaryFlux|  /  sum over BOUNDARY faces of |phi|
```

but the numerator is a rounding artifact of a sum over **all cells**, so the
scale its error belongs to is the **all-faces** flux magnitude — which is
precisely what `CfdDivergenceSummary::relativeImbalance` already divides by. The
two denominators differ by the ratio of interior to boundary flux magnitude, and
nothing bounds it. Measured in the operator's own summation order on the two-cell
fixture, with a **correct** scatter:

```text
interior / boundary      as re-derived       relativeImbalance
1e6   / 1e-6             7.614493e-06        4.568696e-17
1e12  / 1e-12            1.000000e+00        6.000000e-24
1e200 / 1e-200           1.000000e+00        0.000000e+00
```

Three things followed from it, and all three are now fixed:

```text
IT EXCEEDED ITS OWN REPORTED BOUND by eight decades.  The suite asserts
  accumulationResidual < 1e-14 and passed only because every fixture opened
  the ENTIRE boundary, maximising the denominator.  A nearly-closed domain --
  a duct with a small inlet and outlet -- breaks it with no formula error
IT REACHED EXACTLY 1.0, the value this ADR and the header both declare to be
  the signature of a scatter writing `+flux` to both sides.  The falsifiable
  consequence was falsified by correct code
IT WAS IDENTICALLY 0.0 IN A CLOSED DOMAIN, because the boundary magnitude is
  bit-zero there, so `relativeDeparture` returned its scale<=0 arm whatever
  the scatter did.  The suite asserted `== 0.0` on all eight reference bodies
  and called it "stronger than the measured bound".  It was vacuous -- the
  tautological-check defect this very ADR cites twice as the thing to avoid
```

The fix is to **consume** the qualified number instead of re-deriving it, which
is what Decision 1 argues for and which this was the one place in the file to
ignore. It removes code. The proof that the old assertion was blind is that the
same reference bodies now report `1e-17` to `3e-17` where they previously
reported exactly zero.

A test asserts the two differ by many orders of magnitude on a field whose mass
balance genuinely fails, so a swap cannot pass: the artifact is `~1e-16` where
the physical measure is `~1`. **A mutation probe applies the swap and must kill
the suite.** Naming alone would not have been enough, because the collision is
across two modules' types and neither owner can see the other's spelling.

### Decision 4 — Direction comes from the SIGN of `phi`, never from the condition kind

`phi` is signed out of the fluid at a boundary face, so a positive value is
**outflow** and a negative value is **inflow**, whatever the facet is called.
`PressureInlet` and `PressureOutlet` carry `phi` as `Computed` precisely because
the flow rate is an outcome, and `P19-CONV-001` already reports `backflowFaces`,
so a facet named an inlet may carry outflow. Attributing direction from
`CfdBoundaryConditionKind` would be wrong exactly in the regime the diagnostic
exists for.

The kind is still reported, per patch, so a reader can see that a patch named an
inlet is carrying outflow. **That juxtaposition is the diagnostic**; it is not an
error, and nothing is refused for it.

### Decision 5 — A closed domain is checked EXACTLY, and it is a separate item for a reason

ADR-067's table gives `phi = FixedValue 0` at `NoSlipWall`, `MovingWall` and
`Slip`, and ADR-072 writes `0.0` exactly there. A domain whose every facet is a
wall therefore has a boundary flux that is **bit-zero**, measured at 0 non-zero
nets in 5000 trials (Q4), and the check is `== 0.0` rather than a tolerance.

It is a separate item because the throughput is also zero, so there is nothing to
normalise against: the relative measures are **undefined**, not small. This is
why the closed domain cannot be folded into the relative check as a degenerate
case, and why Decision 9 reports the relative measures as optional.

### Decision 6 — Inlet and outlet are REPORTED; the check is the global boundary imbalance

Items 4, 7 and 9 are satisfied by reporting `inflow`, `outflow` and a per-patch
breakdown, and by checking (b). No assertion is made that `outflow == inflow`,
and none that a prescribed inlet is reproduced at an outlet: ADR-058 forbids the
second and Q2 shows the first is not even a floating-point identity.

### Decision 7 — The aggregation order is fixed and documented, and the reconstruction identities are MEASURED BOUNDS

Boundary faces are visited in `BoundaryFaceIndex` order, once each. Per-patch
subtotals accumulate in that order within each patch, and the patch order is
`PatchIndex` order. Nothing iterates an unordered container.

The two reconstructions a reader would assume are identities are **not**:

```text
outflow - inflow        ==  netBoundaryFlux    bit-exact on  1.02%  (Q2)
sum of patch subtotals  ==  netBoundaryFlux    bit-exact on  1.93%  (Q3)
```

Both are reported as agreeing to `kCfdContinuityAggregationTolerance` and the
suite asserts the bound, not the equality. Item 10 is satisfied by the fixed
order plus a test that two runs over the same input are **bit-identical**, which
is a claim about determinism and distinct from these, which are claims about
associativity.

### Decision 8 — Two tolerances, each tight to its own measurement, and neither is an acceptance threshold

```text
kCfdContinuityCellTolerance         1.0e-13   worst measured 7.4598e-15  (Q1)
kCfdContinuityAggregationTolerance  1.0e-14   worst measured 9.6334e-16  (Q2/Q3)
```

Each is about one decade above its own worst measurement over random meshes, not
tight to it, for the reason `kCfdConvectionRowSumTolerance` gives: the
measurement is of random meshes and not of every mesh.

**AMENDED: Q1 DOES NOT MEASURE THE QUANTITY THE CELL CONSTANT IS COMPARED
AGAINST, AND BOTH REVIEWS FOUND IT INDEPENDENTLY.** Q1 normalises per cell by
that cell's own `sum_f |phi_f|`; the implementation reports

```text
maxCellContinuity = max over cells |sum_f phi_f|  /  (0.5 * sum over BOUNDARY |phi|)
```

because `ADR-058` fixes the denominator as the boundary throughput. The two
differ by `(that cell's flux magnitude) / (2 * boundary throughput)`, which is 1
only by coincidence. On a one-cell tetrahedron — which was the **only** fixture
that applied the bound — the factor is exactly 2, leaving about 6.7x of headroom
rather than the documented decade.

The denominator is not this milestone's to change: `ADR-058` fixed it and
`CfdData.hpp` declares it. So what changes is the claim and the coverage:

```text
the bound is justified as a FLOATING-POINT floor on the per-cell residual,
  which Q1 does measure -- and the extra factor relating it to the throughput
  denominator is recorded here rather than left implied
the bound is now APPLIED on the eight reference bodies, which run from 6 to
  808 cells, where a uniform velocity is divergence-free for a reason external
  to the code.  Before this, no multi-cell mesh ever met the constant
```

The second half matters more than the first: a review proved by measurement that
reporting the **global** imbalance under the name `maxCellContinuity` passed all
40 cases and 6943 assertions, because every site that touched that member was
either a one-cell fixture — where the cell residual IS the boundary net — or a
`>= 0.0` that cannot fail. `CfdData.hpp` exists to stop exactly that drift, and
its sibling member was undefended.

**TWO AND NOT ONE, DELIBERATELY.** A single bound at `1e-13` would be a hundred
times looser than the aggregation measurement supports, and a formula error in
per-patch aggregation two orders of magnitude large would pass it. A tolerance
that cannot fail is not a gate — the defect `P19-DIFF-001`'s review found in a
tautological cardinality check, in a different dress.

Neither is an acceptance threshold. `CfdData.hpp`'s "CONVERGENCE IS NOT
ACCEPTANCE" and ADR-053's rule put policy in `P19-VALID-001` and
`P19-MASSBAL-001`, and `continuityResidualTolerance` in `CfdConvergenceSettings`
is a user convergence setting that this operator does not read.

### Decision 9 — The throughput floor is NOT set here, so the relative measures are OPTIONAL

`CfdData.hpp` assigns the floor to `P19-MASSBAL-001`. Rather than invent one, the
evaluator returns the relative measures in a `std::optional<CfdMassBalance>`,
present exactly when the boundary throughput is strictly positive, and always
returns the **absolute, dimensioned** quantities. A caller that needs a floor
applies its own.

Rejected alternatives: a floor of `0.0` would divide by zero in a closed domain;
any non-zero floor would be `P19-MASSBAL-001`'s number chosen here; returning
`0.0` for the relative measure in a closed domain would report perfect balance
where the measure is undefined, which is the stronger failure because it reads
as evidence.

### Decision 10 — The error channel is `std::expected` with a tag, and sub-operator refusals are PREDICTED

As ADR-073 Decision 7 and Decision 10. `evaluateCfdContinuity` gates its own
inputs in declared phase order, predicts `cfdMassFluxDivergenceProblem` before
calling the divergence, and maps a sub-operator refusal onto its own tag rather
than letting a foreign enumerator escape. A refusal the predictor cannot have
foreseen is an internal failure, because it means the sub-operator's gate moved.

`divergeCfdMassFlux` returns an **untagged** `Error` — a limitation ADR-071
records about itself — so the tag cannot be read and is inferred from the
predictor's agreement. That inference is exactly as strong as ADR-071's
documented enumeration and no stronger, and the implementation says so in a
comment rather than presenting it as a reading.

**AMENDED 2026-11-08, THE SAME DAY, BECAUSE WRITING THE TEST FALSIFIED THE
JUSTIFICATION.** This decision originally claimed the evaluator needed its own
finiteness loop because "requiring every input to be finite admits `1.7e308`,
from which a boundary sum can overflow". That is false. `divergeCfdMassFlux`
gates its own `netBoundaryFlux`, `globalNetFlux` **and** its accumulated
magnitude `sum_f |flux_f|`, and refuses such an input with

```text
the mass flow rate divergence totals are not finite: boundary inf,
global inf, accumulated magnitude inf
```

while every per-cell residual is finite. Every quantity this evaluator computes
is bounded by that magnitude:

```text
inflow, outflow        <= sum over boundary |phi|  <= sum_f |flux_f|
throughput              = 0.5 * sum over boundary |phi|
every patch subtotal   <= its patch's own |phi| sum
netBoundaryFlux         IS the divergence's value, already gated
largestCellResidual     a maximum over values the divergence gated
```

so no input can overflow one of them without the sub-operator refusing first.
The three relative residuals could in principle need twice the scale, since
`|a - b|` can, but `a` and `b` there are the same boundary net reconstructed two
ways and are nearly equal rather than opposite.

The loop is **kept as an unreachable defence** and recorded as `L8`, for the
reason ADR-071 keeps `PrescribedValueMissing` and ADR-073 keeps its extrema
check: it guards a promise another module makes about its own gate. It is NOT
kept with the false story attached. The inequalities above are asserted by a
test, so a change that made one of them false would make the loop reachable and
say so.

The attempt that produced this correction is worth recording in itself: the test
was written to reach the loop by the route P19-MOM-001's surviving probe M19 had
exposed — per-cell sums finite while the same-sign accumulation overflows — and
it failed on its own premise. The route was real in momentum and is not here,
and the difference is that ADR-071's operator gates a magnitude ADR-073's did
not.

## Consequences

```text
+  the per-cell residual and the global sums come from a QUALIFIED operator, and
   the decomposition is checkable against it rather than merely believed
+  the name collision is defended by a type and a mutation probe, not by care
+  no measure, no floor and no threshold is invented: two declared measures are
   filled, and the owner of the numbers stays P19-MASSBAL-001
+  every floating-point claim is a measured bound, and the two that a reader
   would assume are identities are measured NOT to be
-  two passes over the boundary faces where one would do.  Accepted: the
   divergence's pass is inside a qualified operator and is not this milestone's
   to fold into
-  P19-MASSBAL-001 will re-tick seven items this milestone ticks.  Recorded in
   TODO.md rather than resolved, because resolving it means editing another
   milestone's checklist
```

## Known limitations

```text
L1  NOTHING CONSUMES THIS.  There is no solve, so the evaluator is never run on
    a converged field.  It measures what it is handed
L2  NO ACCEPTANCE VERDICT.  It reports; it does not say whether a flow is
    acceptable.  That is P19-MASSBAL-001's and P19-VALID-001's
L3  THE RELATIVE MEASURES ARE ABSENT in a closed domain, by Decision 9, so a
    caller wanting one number in every case must supply a floor
L4  NO TRANSIENT ACCUMULATION.  `d(rho)/dt` is zero at constant density, so
    steady and transient continuity coincide here; P19-MASSBAL-001's "Transient
    accumulation policy" item is not this one's
L5  `phi` IS CALLER-SUPPLIED and its conservativeness is not assumed.  A `phi`
    that was never produced by `cfdMassFlux` is accepted and measured, because a
    policy says where a value comes from and not whether a sum may be taken over
    it (ADR-071 Decision 5)
L6  THE PER-PATCH BREAKDOWN IS BY `PatchIndex`, not by boundary condition.  Two
    patches carrying the same condition stay separate, which is the finer
    answer, but a caller wanting per-condition totals aggregates them itself
L7  NO PERFORMANCE BASELINE, for the fourth milestone running, because nothing
    solves yet and a baseline for a measurement with no solver behind it would
    be a number with no meaning
L8  THE FINITENESS LOOP IS UNREACHABLE, by the inequalities in Decision 10's
    amendment, and is kept as a defence against another module's gate moving
    rather than as a reachable refusal.  The TAG is reachable and tested,
    through the divergence's own refusal.
    IT WAS BRIEFLY REACHABLE, AND BY THIS MILESTONE'S OWN DEFECT. While
    `accumulationResidual` was re-derived with the boundary-only denominator
    (Decision 3's amendment), an input with large interior and tiny boundary
    flux made that ratio `inf` -- so the loop fired and refused a VALID input,
    with a message blaming a boundary sum that had not overflowed. Consuming
    the divergence's own gated figure closes it. Recorded because "unreachable"
    was false for a few hours and the reason was not the inequalities but a bug
L11 THE PHASE-2 BOUNDARY-SOURCE ARM IS UNREACHED BY ANY TEST, and a review
    proved it by short-circuiting the condition and finding the suite green.
    `bindCfdBoundary` always returns a DEFAULT `CfdBoundarySource`, which its
    own header states, and every fixture in the suite goes through it -- so
    `!(source == CfdBoundarySource{})` is false on every constructible input
    and the inner comparison never runs. The unconditional boundary-face count
    check beside it is what the refusal tests actually fire.
    THIS ONE IS WORSE THAN AN ORDINARY UNCOVERED BRANCH: `P19-CONV-001`'s first
    draft DROPPED this very comparison and only a review caught it. It is back,
    and there is still no test that would notice if it went again. Reaching it
    needs a `prepareCfdBoundary`-built boundary, which no cfd suite currently
    constructs. Recorded rather than left implying coverage
L12 `ErrorCode::Internal` IS NEVER OBSERVED FROM THIS MODULE. All five
    `internalFailure(...)` sites sit behind guards that no constructible input
    reaches, so the caller/internal code split -- which exists because a review
    found `P19-MOM-001` stamping an internal inconsistency `InvalidArgument` --
    is carried only by `toString` round-trips and a string comparison. The
    distinction is right and is untested
L13 Q3 MEASURED AN ASSOCIATIVITY THE OPERATOR DOES NOT PERFORM. Its model
    deals faces to patch subtotals round-robin; `buildCfdTopology` sorts the
    boundary range by patch, so each patch's faces are CONTIGUOUS. The bound is
    almost certainly conservative either way -- contiguous re-association rounds
    no worse -- but `kCfdContinuityAggregationTolerance`'s justification for
    `patchSumResidual` is not a measurement of the arithmetic it bounds
L14 `maxCellContinuity`'s DENOMINATOR IS NOT Q1'S. Q1 normalises per cell by
    that cell's own flux magnitude; ADR-058 fixes the reported denominator as
    the boundary throughput. The constant is justified as a floating-point
    floor on the per-cell residual, which Q1 does measure, and the factor
    relating the two is recorded in Decision 8's amendment rather than folded
    into the bound
L9  THREE MORE REFUSALS ARE UNREACHABLE from any constructible input:
    `TopologyNamesAnUnknownCell` and `TopologyNamesAnUnknownPatch`, because
    `buildCfdTopology` validates both, and `EvaluationIsInconsistent`, because
    phases 1, 5 and 6 cover everything the divergence's mass-flux path can
    report.  Four of eight enumerators are therefore unreached, which is a
    higher proportion than any P19 operator so far -- a consequence of
    consuming a qualified operator whose gate already covers most of the same
    ground, and recorded rather than reduced by deleting guards that two
    separate reviews have already had to force into sibling operators
L10 `CfdContinuityReport` HOLDS A COPY OF THE DIVERGENCE FIELD, which doubles
    the per-cell storage for a report whose own additions are per-patch.
    Accepted because exposing the sub-operator's answer verbatim is what makes
    the decomposition checkable, and a reference-counted or borrowed field
    would make the report's lifetime depend on its input's
```
