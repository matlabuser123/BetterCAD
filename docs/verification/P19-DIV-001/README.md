# P19-DIV-001 — Divergence / Flux Operator

```text
TASK:            P19-DIV-001 — Divergence / Flux Operator
IMPLEMENTATION:  the face-integrated flux, scattered face by face into the
                 owner and the negated neighbour, with the boundary face value
                 taken from ADR-067's frozen policy table and the internal face
                 value from ADR-070's own exported interpolation weight
                 (ADR-071)
TESTS:           40 unit cases, 36,930 assertions, IDENTICAL in Debug, Release
                 and Debug-shared; 28 compile-fail gates
VALIDATION:      a one-cell tetrahedron against rational arithmetic, with its
                 volume checked independently first; a constant field against
                 zero on eight reference bodies, bounded by the measured
                 closure; an exactly linear field against trace(A) with the
                 true face value; an INDEPENDENT weight oracle, re-expressed
                 from the two cell centroids and the face centroid, over the
                 Cylinder's 946 internal faces; the trace of ADR-070's
                 Green-Gauss gradient, as a second operator computing the same
                 quantity; invariance under a compound rigid motion; and a
                 20,000-mesh numerical experiment, run BEFORE any C++ existed,
                 which measured cancellation, closure and linear exactness and
                 contradicted the design's own first draft
REVIEW:          two independent reviews, run AFTER the first qualification had
                 passed -- which was itself a process defect. They found five
                 defects in the code and eight in the suite, converging
                 independently on the two sharpest. All thirteen fixed, which
                 VOIDED that qualification. Attempt 2 was run from clean
RESULT:          PASS
EVIDENCE:        docs/verification/P19-DIV-001/
```

## SCOPE

Authorized 2026-11-07 by the owner's **"Authorize DIV-001"**, in answer to a
question that named this milestone and no other. **The authorization was
recorded in `TODO.md` before any code was written**, in the four places that
carry a milestone's status, as P19-GRAD-001's, P19-DIFF-001's and P19-IC-001's
were.

One thing about this authorization is different from the four before it, and it
is recorded rather than smoothed over. **The owner did not say "implement and
fully verify", and did not quote or alter the checklist.** So the scope is the
twelve items that already stood in `TODO.md`, read exactly as they stood, and
authorizing the milestone is taken as permission to implement it under this
repository's own definition of done — nothing wider. Nothing after it is
authorized, and `P19-CONV-001` is not started.

```text
* [ ] Scalar flux divergence          * [ ] Closed-volume conservation
* [ ] Velocity divergence             * [ ] Independent analytical validation
* [ ] Face-flux summation             * [ ] Determinism PASS
* [ ] Internal-face cancellation      * [ ] Regression PASS
* [ ] Boundary-face contribution      * [ ] Evidence recorded
* [ ] Constant-field validation
* [ ] Linear-field validation
```

What this milestone found already present, and did not build:

```text
every metric it reads                P19-GEOM-001 / ADR-065
  cellVolume, faceAreaVector, faceCentroid, cellCentroid, faceUnitNormal,
  orthogonalDistance, ownerToNeighbour, cellToFace, skewness,
  cellSurfaceVectorSum and cellClosure -- ADR-065 being the AUTHORITY on the
  sign each face is taken with, and its header warning that summing a cell's
  stored area vectors without consulting `owner` leaves a residual of order
  ONE rather than of order eps
the adjacency                        P19-TOPO-001 / ADR-064
  cells, faces, cellFaces, owner, neighbour, isInternal, boundaryFaceOf
the boundary policy table            P19-BC-001 / ADR-067
  treatmentOf(kind), frozen, and the only place that table exists
phi itself                           P19-DOF-001 / ADR-066
  CfdVariable::MassFlux, already FACE-located and already kg/s
the interpolation weight             P19-GRAD-001 / ADR-070
  cfdFaceInterpolationWeight, the exported (d.n - (C_f - C_P).n) / (d.n),
  which this operator now CALLS rather than re-deriving -- see review
  finding A2, which is the reason the word "exported" is in that line
```

**READING THAT CODE CHANGED THE DESIGN TWICE, BEFORE ANY OF IT WAS WRITTEN.**
Both corrections are in ADR-071 and both were found by reading rather than by
testing afterwards:

1. **`phi` is already the face-INTEGRATED `rho (U . Sf)` in kg/s**, not a
   per-area flux. ADR-066 settled that and `CfdFields.hpp` says so. So its
   divergence is a pure signed sum over a cell's faces with **no area factor**,
   and a first design that multiplied by `faceArea` would have been a
   conservation error that still looked dimensionally plausible.
2. **There is no prescribed `phi` to read at a boundary.** `CfdBoundaryFace`
   carries `pressure` and `velocity` and nothing else, and ADR-067's `phi`
   column is `Computed` at both pressure boundaries. So the mass-flux path
   consults no boundary at all, and — the part that is easy to get backwards —
   it must **NOT refuse `Computed`**, because `Computed` is the normal state of
   the variable it is given (ADR-071 Decision 5). A compile-fail gate,
   `THE_MASS_ENTRY_POINT_TAKES_NO_BOUNDARY`, holds the first half of that at
   compile time.

## BASELINE

`df99157`, P19-GRAD-001's record commit. **6381 tests per preset, 636 ticked
boxes.**

This operator sits between two recorded limitations and closes neither, which
is stated here rather than discovered later: P19-DIFF-001's L1 wants a FACE
gradient, and P19-GRAD-001's L1 says nothing in the repository consumes its
cell gradient. Wiring those together is a coupling step no checklist contains.
**Nothing consumes this divergence either.** It is a term of an equation that
P19-MOM-001 and P19-PRES-001 have not assembled.

## ARCHITECTURE

ADR-071, ten decisions. The part worth reading first is that **the design was
measured before it was written**, and the measurement contradicted it.

### The experiment that came before the C++

`experiments/01-cancellation-and-closure.py`, seed 20261107, deterministic.
Four questions, 20,000 random meshes each, and the point of asking them first
was that each one had a design decision riding on it.

```text
Q1  does the sign flip change a bit?   (-S).U == -(S.U)
    200,000 trials, 0 mismatches, worst relative gap 0.000e+00
    EXACT -- so an internal face's two contributions cancel BIT-FOR-BIT,
    which is what makes a face-centred scatter a conservation GUARANTEE
    rather than a numerical hope (Decision 2)

Q2a per internal face, owner + neighbour == 0.0?
    20,000 faces, 20,000 bit-exact (100.00%)
    AND THIS ONE IS A TAUTOLOGY -- see below. It is kept, labelled

Q2b globally, the sum over cells against the boundary sum?
    20,000 meshes, 8,067 bit-exact (40.34%)
    worst |imbalance| / sum_f |flux_f| = 1.125e-16   (eps = 2.220e-16)
    NOT bit-exact, AND IT CANNOT BE: each cell accumulates its own faces
    before the two meet, and (a + x) + (b - x) is not a + b in floating
    point. So the global imbalance is MEASURED and reported (Decision 3)

Q3  closure, and the constant mode
    sum_f S_f bit-zero on 972 of 20,000 tets (4.86%), worst 3.993e-16
    constant-field divergence bit-zero on 4,827 (24.14%), worst 2.144e-16
    closure at rounding does NOT give a bit-zero constant-field
    divergence, and the two are not the same set in either direction

Q4  a linear field with the TRUE face value, against trace(A)
    worst 2.966e-16 relative to sum|S||U|/V   (eps = 2.220e-16)
    LINEAR-EXACT to rounding, so every departure on a real mesh belongs to
    the INTERPOLATION and not to the surface sum
```

**ADR-071'S FIRST DRAFT PREDICTED A BIT-EXACT GLOBAL SUM. Q2b says 40.34%.**
A test asserting a bit-zero global imbalance would therefore FAIL on a correct
operator, which is exactly the kind of test this repository has been bitten by
before. The ADR carries an amendment recording the prediction and its
withdrawal, the suite bounds the relative figure instead, and the header says
so where a caller meets the number.

**Q2a AS WRITTEN IS A TAUTOLOGY, AND IT IS THE SAME TAUTOLOGY THE SUMMARY
MEMBER WAS DELETED FOR.** Its predicate is `flux + (-flux) == 0.0`, which is
true for every finite double, so the 100.00% is not a measurement — it was
found by re-reading the script after the run, which is late. What the script
genuinely establishes about pairwise cancellation is **Q1**: that `(-S).U` is
bit-for-bit `-(S.U)` for the three-term dot product, which is a real claim and
could have come out false. Q2a's remaining value is as a check on the
EXPERIMENT'S OWN mesh builder — that each internal face appears exactly once
with an owner and a neighbour. The line is kept and labelled rather than
deleted, because the committed output must match the committed script.

**The same reading was then applied to the suite's own case, and an independent
reviewer took it further than the author had.** The author noted that
`AnInternalFacesTwoContributionsSumToExactlyZero` sets every boundary face to
zero, so its `first + second == 0.0` assertion inherits exactly the structure
above, and that what makes the case non-vacuous is the owner's value being
exactly `kDiscriminatingConstant`. **Reviewer B found the sharper half of it:
the case runs the MASS path, where the assertion holds for ANY
implementation** — finding B2 below. A second case on the VELOCITY path now
carries the claim ADR-071 Decision 2 actually makes.

**AND THE FIRST DRAFT OF THE EXPERIMENT ITSELF WAS WRONG, in the way
P19-GRAD-001 finding r8 describes.** It divided by `max(|boundary_total|,
1e-300)` and reported a worst gap of `9.992e+284` for arithmetic that is correct
to half an eps. That was the first thing measured in this milestone and it
reproduced the previous milestone's own mistake; the denominator is
`sum_f |flux_f|` now, the magnitude of the terms accumulated, never the answer.

### Two summary members were withdrawn before any code was written

Both would have read as evidence and carried none:

```text
internalCancellationIsExact  would have asked whether x + (-x) == 0.0 for a
                             finite x. True for every finite double, so a
                             summary member that cannot vary. Q2a establishes
                             the property once, structurally, and the suite
                             pins it per face
constantModeIsExact          would have been FALSE as derived. Q3 measured
                             closure bit-zero on 4.86% of tets and the
                             constant-field divergence bit-zero on 24.14%,
                             so the flag would have reported one thing while
                             naming another. Replaced by the measured
                             `worstClosure` (Decision 8)
```

### One template, two entry points, and the aliases are additive

`CfdDivergenceField<Q>` stores the volume integral and divides on read
(Decision 1), instantiated at `VolumetricFlowRate` (m³/s, the velocity path)
and `MassFlowRate` (kg/s, ADR-066's `phi`). The two entry points have different
signatures because they have different refusals — one reads a face field it is
handed, the other reads cell values and a frozen policy table — and writing
them as one function with an optional boundary would hide that behind a null
check (Decision 4).

**The one public API change is purely additive.** `VolumetricFlowRate` and
`MassFlowRate` are added to `CfdData.hpp`; nothing is removed or renamed, so no
existing spelling changes and no existing caller can break. P19-GRAD-001 MOVED
two aliases between headers and had to prove source compatibility first; this
milestone has nothing to prove, which the diff shows rather than asserts:
**142 insertions and 0 deletions across the whole tracked frozen surface.**

The dimensions were `static_assert`ed against the unit catalog **in a
standalone translation unit, before any operator code existed** — eight
assertions naming `dimensions::volume / dimensions::time`,
`dimensions::mass / dimensions::time` and `dimensions::density /
dimensions::time` directly, so the aliases were checked against the catalog
rather than against each other. `VolumetricFlowRate / Volume` is
`VelocityGradient` **exactly**, which is why there is **no** separate
velocity-divergence alias: it would name a distinction the type system does not
make. The suite keeps that as a compile-time assertion, so a later edit to the
catalog cannot quietly break it.

## BLAST RADIUS

Purely additive, and the diff is the measurement:

```text
new     include/bettercad/cfd/CfdDivergence.hpp        411 lines
        src/cfd/CfdDivergence.cpp                      666
        tests/cfd/CfdDivergenceTests.cpp              2419
        tests/compile_fail/CfdDivergenceMisuse.cpp     299
changed include/bettercad/cfd/CfdData.hpp              +27  two aliases
        src/cfd/CMakeLists.txt                          +1
        tests/CMakeLists.txt                            +1
        tests/cli/ZeroMatchGuard.cmake                 +42
        tests/compile_fail/CMakeLists.txt              +71
                                               142 insertions, 0 DELETIONS
```

**"0 deletions" describes the TRACKED surface, and the qualifier is earned.**
Review finding A2's fix deleted a function — the `siOf(const Translation3D&)`
overload, which became unused once the transcribed weight was removed and would
otherwise have failed `-Werror=unused-function`. That deletion is invisible in
the diff above because it is inside `src/cfd/CfdDivergence.cpp`, a file this
milestone creates. Keeping dead code to satisfy a compiler is worse than
removing it, so it was removed.

What that reaches, and what it does not:

```text
direct callers            NONE. Nothing in the repository consumes a
                          divergence yet, which is limitation L1 below rather
                          than an omission
serialization             NO CFD SERIALIZER EXISTS. P19-PERSIST-001 is later
                          and unauthorized, so there is no round trip to test
                          and none is claimed
stable references         not involved: the operator reads CellIndex and
                          FaceIndex, which ADR-064 owns, and stores no
                          reference of its own
regeneration / undo       no CFD command exists (P19-CMD-001, unauthorized)
the CLI                   untouched
the reference models      read-only, from tests, on eight bodies
the three frozen trees    apps, examples and cmake are byte-identical to
                          P19-GRAD-001's freeze, and so are both root files
one header in another     CfdDivergence.cpp now includes CfdDiffusion.hpp for
  milestone's module      the exported weight. Same module, same layer
                          (cfd, 42), so it crosses no architectural boundary;
                          architecture.layering passed in all three presets
```

`CfdSparse` is deliberately NOT linked by this translation unit: a divergence is
a reduction, not an operator with rows (Decision 10).

## IMPLEMENTATION

The scatter is the whole operator, and it is four lines:

```text
for each face, in FaceIndex order:
    flux = F_f . S_f                         one product per face
    si[owner]      += flux
    if internal:  si[neighbour] += -flux     the EXACT negation, by Q1
    else:         netBoundary   += flux
```

The three things that make it correct rather than plausible:

```text
the sign is READ, never assumed   ADR-065 winds faceAreaVector out of the
                                  OWNER. The scatter consults `owner` and
                                  negates for the neighbour; summing a cell's
                                  stored vectors without consulting it leaves
                                  a residual of order ONE
one product per face              so the two contributions are x and -x of the
                                  same double, and Q1 makes that cancel
                                  bit-for-bit. A gather would compute the
                                  product twice and lose the guarantee
Symmetry contributes exactly 0.0  not the projected value, which is zero only
                                  to rounding. ADR-067 defines a symmetry
                                  plane as one nothing crosses, so the flux is
                                  zero BY DEFINITION (Decision 7), and the
                                  suite pins the exact zero against the
                                  projected alternative
```

The velocity face value is a vector, interpolated with ADR-070's own exported
weight (Decision 6) on internal faces, and taken from ADR-067's table on
boundary faces: the prescribed velocity at `FixedValue`, the owner's own at
`ZeroGradient`, exactly zero at `Symmetry`. The `U` column never holds
`Computed`, and a total switch over the enum answers for the fourth value by
name rather than by falling through.

**Two things in this section were corrected by the independent reviews, and the
corrections are the reason the wording above is what it is.** The weight is
called from one exported definition rather than transcribed (A2), and it now
**refuses** a degenerate face instead of returning a midpoint (A3) — the only
place in the operator where a wrong answer could have been returned as a
success. The range guards that protect the span reads were also moved ahead of
the reads (A1). All three are detailed under ADVERSARIAL REVIEW.

## TESTS

40 unit cases, **36,930 assertions in Debug, 36,930 in Release and 36,930 in
Debug-shared**. The identity across configurations is checked rather than
assumed: a count that agreed only in Debug would mean a branch compiled out.
The whole `[cfd]` subsystem is **667,506 assertions in 399 cases**, identical
in all three presets, which is `630,576 + 36,930` and `359 + 40` exactly — so
no pre-existing cfd assertion moved.

Where the twelve checklist items are pinned:

```text
Scalar flux divergence        AMassFluxDivergenceIsTheSignedSumOfItsFaceValues
                              AMassFluxDivergenceNeedsOneValuePerFace
                              TheMassFluxPathTakesNoBoundaryAndAcceptsA-
                                ComputedPolicy
                              TheStoredQuantityIsTheIntegralAnd-
                                DivergenceAtDividesByTheVolume
Velocity divergence           AVelocityDivergenceNeedsThreeValuesPerCell
                              AZeroGradientFacetUsesTheOwnersOwnVelocity
                              AFixedValueFacetUsesThePrescribedVelocity
                              ASymmetryFacetContributesExactlyZeroFlux
                              TheSymmetryExactZeroBeatsTheProjectedAlternative
Face-flux summation           TheOwnerSignIsReadAndNotAssumed
                              AnAnalyticOneCellTetMatchesClosedFormArithmetic
Internal-face cancellation    AnInternalFacesTwoContributionsSumToExactlyZero
                                (the MASS path)
                              AnInternalFacesTwoVelocityContributionsSumTo-
                                ExactlyZero   (the VELOCITY path, which is the
                                path ADR-071 Decision 2 is about -- finding B2)
Boundary-face contribution    TheBoundaryPolicyCountsSumToTheBoundaryFaceCount
                              TheGlobalNetFluxEqualsTheBoundaryFluxToRounding
Constant-field validation     AConstantVelocityDivergesToZeroOnAFixture
                              AConstantVelocityDivergesToZeroOnEvery-
                                ReferenceBody
                              TheWorstClosureIsReportedAndBoundsThe-
                                ConstantModeResidual
Linear-field validation       ALinearVelocityDivergesToItsTraceOnAFixture
                              ALinearVelocityDivergesToItsTraceWithTrue-
                                FaceValues
                              ALinearVelocityDivergesToItsTraceOnEvery-
                                ReferenceBody
Closed-volume conservation    TheGlobalNetFluxIsTheSumOfEveryCellsNetOutflow
                              TheRelativeImbalanceIsMeasuredAgainstThe-
                                AccumulatedMagnitude
                              TheRelativeImbalanceIsZeroWhenNothingWas-
                                Accumulated
Independent analytical        AnAnalyticOneCellTetMatchesClosedFormArithmetic
  validation                  TheInterpolationWeightMatchesItsClosedFormAnd-
                                VariesOnARealMesh
                              TheVelocityDivergenceAgreesWithTheTraceOfThe-
                                GreenGaussGradient
                              TheDivergenceIsInvariantUnderARigidMotion
Determinism PASS              DividingTwiceGivesBitIdenticalFields, and the
                              two repeat stages below
Regression PASS               the three full presets below
Evidence recorded             this directory
```

And the behaviours no checklist item names, which the API nevertheless
declares:

```text
TheFluxQuantitiesCarryTheDimensionsTheirNamesClaim   the unit contract
EveryProblemHasAName                                 all seven enumerators,
                                                     one assertion EACH, by
                                                     name (finding B4)
AFieldDoesNotHoldACellItWasNotBuiltWith              out-of-range read
TheDeclaredPhaseOrderIsTheCheckOrder                 the refusal ORDER, for
                                                     the phases a constructible
                                                     input can reach
ASuccessfulInputPredictsNoProblem                    the noexcept predictor
                                                     must not disagree with the
                                                     operator it predicts. The
                                                     name says what it pins --
                                                     see L9, and the rename
                                                     under REVISION
TheWorstSkewnessIsReportedOnInterpolatedFacesOnly     a boundary face has no
                                                     interpolation, so it must
                                                     not enter the reduction
TheSummaryCountsMatchTheTopology                      internalFaces +
                                                     boundaryFaces == faces, on
                                                     every reference body; and
                                                     the mass path's
                                                     worstSkewness == 0.0,
                                                     moved here from a one-cell
                                                     mesh (finding B5)
TheLargestMagnitudeIsTheMaximumAbsoluteNetOutflow     a reduction pinned on
                                                     cells that DIFFER, plus an
                                                     all-negative field
                                                     (finding B3)
AStaleGeometryIsRefusedFirst                          the four refusals
ANonFiniteValueIsRefusedAfterTheLength
ANonFiniteResultIsRefusedRatherThanReturned
AStaleBoundaryIsRefusedOnTheVelocityPathOnly
```

**28 compile-fail gates**, each registered with the diagnostic family its own
case actually produces. **Every expected diagnostic was READ OFF THE COMPILER
before the CMake registration was written** — P19-GRAD-001 had three gates fail
because its regex did not list `invalid initialization`, so the regexes here are
measured rather than guessed:

```text
no member named / has no member                                2 cases
could not convert | cannot convert | no matching function |
  no match for | invalid conversion | conversion from |
  invalid initialization                                      18 cases
no match for | no matching function                            3 cases
is private within this context | private |
  no matching function | use of deleted                        4 cases
read-only | assignment of member | discards qualifiers |
  no match for                                                 1 case
                                                     total    28 cases
```

Two of them are the ones worth naming. `THE_MASS_ENTRY_POINT_TAKES_NO_BOUNDARY`
holds ADR-071 Decision 5 at compile time, so the asymmetry between the two
paths cannot be quietly removed. `A_DIVERGENCE_IS_ITS_OWN_INTEGRAL` and
`AN_INTEGRAL_IS_ITS_OWN_DIVERGENCE` hold Decision 1 in both directions: the
stored integral and the divided divergence are different types, and neither
converts to the other.

**Both registrations were diffed against the source as SETS, in both
directions**, because 40 listed and 40 declared is not evidence that they are
the same 40:

```text
compile-fail  registered 28, #if defined in the source 28, sets EQUAL
              (compared with the BETTERCAD_CF_ prefix the CMake macro
              prepends -- a diff that ignores it reports 28 and 28 with an
              empty intersection, which is how this check first came out)
guard         anchored names 40, TEST_CASE names 40, sets EQUAL
              group counts "unit\.CfdDivergence_" 40 and
                           "compile_fail\.cfddiv\." 28
```

The guard is `cli.mesh.zero-match-guard`, which fails the build if a named test
stops matching — the P19-DOF-001 defence, where a filter that matched nothing
exited 0 and a qualification ran 352 tests of an expected 2991. It passed in
all three presets, taking 116.70 s, 118.53 s and 132.76 s.

## INDEPENDENT VALIDATION

Six independent references. The first depends on no helper in the suite at all,
and the fourth exists because an independent review found that the thing it
checks had been checked against a copy of itself.

**1. A one-cell tetrahedron against rational arithmetic.**
`AnAnalyticOneCellTetMatchesClosedFormArithmetic` gives four unit fluxes out of
a single tet and asserts the stored integral is **exactly 4.0** —
`integral->si() == 4.0`, not a tolerance. **The accessor's volume is then
checked against `1e-6 / 6` from closed-form arithmetic BEFORE the divergence is
checked against `4 / that`**, so a wrong volume is caught rather than cancelling
out of the ratio. No oracle, no winding helper, no interpolation. The boundary
flux, the global net and the global imbalance are all pinned to exact values on
this fixture too, since every face is a boundary face.

**2. A constant field against zero, bounded by the measured closure.**
`AConstantVelocityDivergesToZeroOnEveryReferenceBody` runs eight reference
bodies. The bound is **not** "close to zero": it is
`kFluxProductUnits * eps * |U| * sum_f|S_f| / V` with
`kFluxProductUnits = 64`, which is the magnitude of the terms the sum
accumulates. Q3 is why this cannot be an equality — closure is bit-zero on only
4.86% of tetrahedra — and `TheWorstClosureIsReportedAndBoundsTheConstantMode-
Residual` ties the reported `worstClosure` to the residual it bounds, now
against ADR-065's own owner-signed `cellSurfaceVectorSum` read per cell
(finding B8).

**3. An exactly linear field against `trace(A)`.** For `U = A x + b`,
`div U = trace(A)` identically. `ALinearVelocityDivergesToItsTraceWithTrue-
FaceValues` supplies `U(C_f)` per face and so tests the SURFACE SUM with no
interpolation in the path — Q4's identity, on real geometry. The two companion
cases then run the operator's own interpolated path on a fixture and on all
eight bodies, where the departure is the interpolation's and `worstSkewness` is
the measured bound on it. The per-cell bound is in units of the products
accumulated, via `divergenceErrorScale` (finding B7).

**4. THE INTERPOLATION WEIGHT, AGAINST AN ORACLE RE-EXPRESSED FROM DIFFERENT
ACCESSORS.** This is the reference that did not exist until reviewer B asked
for it, and the reason it did not is worth stating: the previous oracle was a
character-for-character transcription of production's expression, under a file
header claiming independence. `TheInterpolationWeightMatchesItsClosedFormAnd-
VariesOnARealMesh` now pins four things:

```text
the closed form        w == 2/3 EXACTLY on the analytic pair, and the
                       |d| = 5/4, d.n = 3/4 and cos(theta) = 3/5 that
                       fixture's own docstring had promised and NOT ONE of
                       which was asserted anywhere before
the oracle             w, computed as ((C_N - C_f).n) / ((C_N - C_P).n) from
                       the two cell centroids and the face centroid -- reading
                       NEITHER `cellToFace` NOR `orthogonalDistance`, which is
                       what production reads
it VARIES              over the Cylinder's 946 internal faces the weight is
                       strictly between 0 and 1 and is NOT constant, with the
                       number that differ from 2/3 counted. A hard-coded
                       `return 2.0 / 3.0;` survived the entire previous suite
the operator           the per-cell divergence on a linear field over those
                       946 faces, against the independent oracle
```

**5. The trace of ADR-070's Green-Gauss gradient.** A second operator,
written for a different milestone, computing the same scalar by a different
route. `TheVelocityDivergenceAgreesWithTheTraceOfTheGreenGaussGradient`
differences them. This is a consistency check between two operators and is
**not** load-bearing for the interpolation weight, because both use the same
weight — point O8 of the author's pass, and now reference 4 is what closes it
rather than reference 3 alone.

**6. Invariance under a compound rigid motion.** `TheDivergenceIsInvariantUnder-
ARigidMotion` rotates the mesh and the field together through a composed
rotation and requires the divergence to be unchanged: a scalar invariant of a
vector field, so anything that leaked a coordinate-frame assumption into the
scatter or the interpolation would move.

And the pre-implementation experiment, which is a reference of a different kind:
20,000 random meshes measured four properties of the arithmetic before any of it
was written, and the design was changed to match the measurement rather than the
other way round.

## ADVERSARIAL REVIEW

Three passes, in the order they happened, which is **not** the order the
lifecycle prescribes. The author's own pass is numbered **O1–O19** here, so its
points cannot be confused with reviewer A's **A1–A5**; reviewer B's suite
findings are **B1–B8**.

### The author's own pass, before any review

Written against the final diff, and recorded whether or not it found anything.
The questions are [CLAUDE.md](../../../CLAUDE.md)'s own.

**What did we assume?**

**O1. That `topology.owner(face)` is always a cell this geometry holds.** The
scatter checks `owner.value() >= cells` and refuses, but nothing can produce a
face whose owner is out of range: `CfdTopology` numbers cells and faces
together. So the check is UNREACHABLE and is recorded as such rather than as
something a test reaches. **Reviewer A then found that the check ran too late
to protect the read it guards — finding A1. The author had reasoned about
whether the guard could fire and not about where it sat.**

**O2. That every face belongs to one or two cells.** A face belonging to NO
cell would be counted in `netBoundaryFlux` and in no cell's outflow, which
would show up as a conservation imbalance of order one rather than of order
eps. ADR-064's census makes that impossible, and
`TheSummaryCountsMatchTheTopology` pins `internalFaces + boundaryFaces ==
faces` on every reference body — the observable consequence.

**O3. That `faceCentroid` is the right place to evaluate a "true" face value.**
The linear-exactness case supplies `U(C_f) . S_f` per face and expects
`trace(G)`. That is only the identity if the face value at the CENTROID is what
the surface integral wants, which is true for a linear field over a planar
triangle and would NOT be for a curved face or a non-linear field. P16 gives
planar triangular facets (ADR-058), so the assumption holds, and it is an
assumption about the MESH rather than about this operator.

**What case is missing?**

**O4. `PrescribedValueMissing` and `VelocityPolicyIsAnOutcome` have no test, and
cannot.** ADR-067's `U` column never holds `Computed`, and the one boundary
assembler sets the velocity whenever the policy is `FixedValue` or refuses by
name. Both are kept — the second because a total switch over a four-value enum
must answer for the fourth value, which is a stronger reason than defence.
Recorded as unreachable, not as covered.

**O5. `divergenceAt`'s non-positive-volume branch is unreachable after a
successful build**, because the builder refuses a non-positive volume by name
first. Defensive, and recorded as unreached.

**O6. There is no test for a mesh with ZERO cells or ZERO faces.**
`CfdTopology` refuses an empty mesh upstream, so the state is not
constructible — recorded rather than claimed.

**Could this pass its tests and still be wrong?**

**O7. THE SHARED-WINDING HAZARD, and it is real.** Two of the three oracles
read `topology.owner(face)` and apply ADR-064's winding exactly as production
does. A mutation of the sign dies against them, but a shared MISUNDERSTANDING
of the convention would not. Two things close that:
`AnAnalyticOneCellTetMatchesClosedFormArithmetic` asserts `integral == 4.0`
from rational arithmetic and no oracle at all; and
`TheOwnerSignIsReadAndNotAssumed` asserts `4.0` and `2.0` for the owner- and
neighbour-side cells. A global sign flip gives `-4.0` and `2.0, 4.0`.

**O8. The cross-operator case cannot catch a shared wrong weight.** It compares
against ADR-070's gradient, which uses the SAME weight. **The author wrote that
the linear-field case closes this. Reviewer B showed it did not — finding B1 —
because the weight was constant at 2/3 on every fixture under an assertion.
Reference 4 of INDEPENDENT VALIDATION is what closes it now.** This is the one
point in the author's pass that was *wrong*, as opposed to incomplete, and it
was wrong in the direction of reassurance.

**Are the expected values really independent?**

**O9. Yes for the closed forms, and the volume is the one to watch.** The
analytic case checks `cellVolume == 1e-6 / 6` against rational arithmetic
rather than against the accessor, then the divergence against `4 / that`, so a
wrong volume is caught before it can cancel out.

**O10. `worstClosure` is compared against `cellClosure` read independently in
the test** — but `cellClosure` is the same accessor production reads.
Deliberate: the claim is that the operator REDUCES correctly, not that
`CfdGeometry`'s closure is right, which is P19-GEOM-001's business. **Reviewer
B found that the companion accessor named in the header as "the independent
authority", `cellSurfaceVectorSum`, was never called at all — finding B8.**

**Did we weaken a test or move a tolerance?**

**O11. One assertion was changed, and the code was not at fault.**
`internalFaces() > 100` failed on the Block, which has exactly SIX internal
faces. The identical trap caught P19-GRAD-001 with `> 10` on the same body. The
claim moved to where it is true. That is a correction of a false claim, not a
loosened bound. **It then happened a third time during the review fixes — see
below.**

**O12. Every tolerance is in units of the products the sum accumulates**, never
relative to the answer — `kFluxProductUnits = 64` times
`eps * |U| * sum|S| / V`. **Reviewer B found one that was not: finding B7, an
answer-relative bound in the file whose own header forbids them in capitals.**

**O13. GLOBAL CONSERVATION IS ASSERTED AT THE STRENGTH THE MEASUREMENT
SUPPORTS AND NO MORE.** ADR-071's first draft predicted a bit-exact global sum;
Q2b measured 40.34%. A test asserting a bit-zero global imbalance would FAIL
for a correct operator, so the suite bounds the relative figure and reports it.
Stated in the header, the ADR and here.

**Hidden global state? Save/load? Undo/redo?**

**O14. None.** Two `std::vector<double>` of `cells`, nothing static. No CFD
serializer and no CFD command exist, so there is no round trip to test and none
is claimed. The field has value semantics and a defaulted `operator==`, which
the determinism case uses.

**Could Debug and Release differ?**

**O15. The arithmetic is plain double operations with no FMA.** P19-GEOM-001's
evidence establishes that the presets set no `-march`, so GCC emits no fused
multiply-add on the x86-64 baseline and the products are separately rounded in
both configurations. Checked rather than argued: the assertion totals must be
IDENTICAL across Debug, Release and Debug-shared, because a count that agreed
only in Debug would mean a branch compiled out. They are — 36,930 three times.

**Could order of operations matter?**

**O16. It does, and it is fixed.** The scatter runs in `FaceIndex` order and
the global sum in `CellIndex` order, both fixed in storage by ADR-064. No
parallel reduction, no unordered container, no sort. The two repeat stages
establish it across processes; `DividingTwiceGivesBitIdenticalFields` within
one.

**Can a failure leave partial state committed?**

**O17. No.** Every refusal is `return makeError(...)`, which returns an error
rather than the field under construction, so a caller never receives a
partially scattered field. The field has no public constructor, so there is no
other way to obtain one. Four compile-fail gates hold that.

**Did we cross an architectural boundary or widen the scope?**

**O18. No.** The production files include only `cfd` (layer 42) and `core`
(layer 0) headers and contain no OCCT and no Qt. `CfdSparse` is deliberately
not linked. **After finding A2's fix the translation unit also includes
`CfdDiffusion.hpp` — the same module and the same layer, so still no
boundary crossed, and `architecture.layering` passed in all three presets.**

**O19. The scope was NOT widened to close the two limitations this operator
sits next to.** P19-DIFF-001's L1 wants a FACE gradient and P19-GRAD-001's L1
says nothing consumes its cell gradient; wiring the two together is a coupling
step neither checklist contains, and it stays undone.

**Findings of the author's own pass.** Nothing in it was a defect in the code's
behaviour. Three points were about what the evidence may CLAIM — O7, O8 and
O13. Two summary members had already been removed for being decoration that
reads as evidence. **O8 was wrong, and O1, O10 and O12 were incomplete in ways
the reviews then found.** A pass that finds nothing in its own work is the
expected outcome and is exactly why it is not sufficient.

### Two independent reviews, run after the first qualification had already passed

**AND THAT ORDER WAS ITSELF THE FIRST DEFECT.** P19-IC-001, P19-DIFF-001 and
P19-GRAD-001 each carry an independent review; this milestone reached a freeze,
a launch and a PASS without one. The lifecycle in CLAUDE.md puts ADVERSARIAL
REVIEW before FULL REGRESSION and QUALIFICATION, and the author's own pass plus
a sixteen-probe mutation sweep had been treated as sufficient. They were not.
The reviews were run against the frozen tree, they found real defects, and the
qualification that had passed was therefore **VOID** — the freeze's own rule:
*if source or tests change after the freeze, the qualification is void and is
run again from clean.* It was.

Two reviewers, independent concerns, neither given the author's findings:

```text
reviewer A   the implementation and ADR-071: scatter arithmetic, boundary
             coverage against ADR-067's frozen table, the interpolation, the
             reported figures, the refusals and their order, units,
             Debug/Release, and the architectural invariants
reviewer B   the suite alone, one question: could this code pass this suite and
             still be wrong?
```

**They converged, independently, on the same two sharpest findings** — the
interpolation weight is unpinned and its oracle is a copy of production, and the
exact-cancellation claim is pinned only where it is a tautology. Convergence
from two readings that could not see each other is the strongest signal either
produced, and both findings were confirmed in the source before anything was
changed.

#### What they found in the CODE

```text
A1  THE RANGE GUARDS RAN AFTER THE READS THEY PROTECT. `build` checked
    `owner.value() >= cells` and `neighbour->value() >= cells` AFTER calling
    `faceFlux`, which on the velocity path had already indexed the caller's
    span with both -- `velocityAt` reads values[cell], values[cells + cell] and
    values[2 * cells + cell] unchecked. `geometry.describes` compares the mesh
    stamp and the counts and never the owner/neighbour ranges, so nothing
    upstream establishes them. Unreachable through `buildCfdTopology` today,
    and the sharp edge of the finding is that its two SIBLING guards each carry
    "Unreachable while the gate holds, and recorded as such" while these two
    did not -- so a reader took them for live defences that could not fire.
    FIXED: both guards now run before any cell is indexed, and are annotated

A2  ADR-071 DECISION 6 SAID "the weight therefore has exactly one definition,
    and both operators read it", AND THAT WAS FALSE WHEN IT WAS WRITTEN. There
    were three copies: the exported `cfdFaceInterpolationWeight`, a private
    copy in CfdGradient.cpp from P19-GRAD-001, and a third added by THIS
    milestone -- under a comment claiming it was "read from one definition and
    not re-derived". The non-degenerate arithmetic is identical in all three,
    so no number moved; the degenerate branches are not
    FIXED: this operator calls the exported definition. The ADR carries the
    withdrawn sentence as a labelled amendment. The gradient's copy stays, as
    another milestone's translation unit, and is recorded as limitation L11

A3  A SILENTLY SWALLOWED FAILURE, against this file's own doctrine. The copy
    returned 0.5 where there is no positive orthogonal distance or no unit
    normal, so a degenerate face would have produced a SUCCESSFUL field
    interpolated at a midpoint nobody asked for -- the only place in the
    operator where a wrong answer could be returned as a success, three lines
    from a comment reading "REFUSED, NOT SKIPPED"
    FIXED by A2's fix: the exported form returns nullopt, which the scatter
    turns into a named refusal

A4  PHASE 5 REPORTED A PHASE-2 ENUMERATOR. The per-face totality walk sat
    inside the prescribed-value phase while refusing as
    `BoundaryDescribesAnotherTopology`, so the header's claim that the enum is
    declared in PHASE ORDER was not what the code did
    FIXED: phase 2 walks every boundary face now, which is the phase that owns
    the condition; the phase-5 branch is annotated unreachable

A5  `DivergenceIsNotFinite` IS A TAG NO CALLER CAN READ. The condition is
    reachable and tested, but both entry points return a plain Error and the
    predictors cannot reach the output phase at all, so the value is observable
    only through the diagnostic's wording -- against the house rule
    `CfdBoundary.hpp` states as "a message is not a tag" and answers with
    `bindCfdBoundaryFailure`
    NOT FIXED, RECORDED: adding a `cfdDivergenceFailure` accessor is new public
    API this checklist does not contain. The header now states the asymmetry
    and names the rule it sits against (L10)
```

#### What they found in the SUITE

Eight, and every one is the same shape: an assertion that reads as evidence and
cannot fail.

```text
B1  THE WEIGHT WAS COMPLETELY UNPINNED. Every internal face the velocity path
    ever interpolated across under an assertion has w = 2/3 EXACTLY -- both
    hand fixtures have a 2:1 apex-height ratio, and on the analytic pair
    0.5/0.75 is bitwise 2.0/3.0. So `interpolationWeight` replaced by
    `return 2.0 / 3.0;` survived the WHOLE SUITE. On the eight reference bodies,
    where weights genuinely vary, the velocity path's per-cell output was
    compared against nothing at all
    AND THE ORACLE COULD NOT HAVE CAUGHT IT: `weightOracle` was a
    character-for-character transcription of production's expression -- the same
    two accessors in the same order -- under a file header claiming "a
    transposition in production cannot be mirrored here". That is
    P19-GRAD-001's r5 defect recurring one milestone later
    FIXED: the oracle is re-expressed as ((C_N - C_f).n) / ((C_N - C_P).n) from
    the two cell centroids and the face centroid, reading NEITHER
    `cellToFace` nor `orthogonalDistance`. A new case asserts the weight's
    closed form 2/3 on the analytic pair, the |d| = 5/4, d.n = 3/4 and
    cos(theta) = 3/5 that fixture's own docstring promised and NOT ONE of which
    was asserted anywhere, that the weight is strictly convex and NOT constant
    over the Cylinder's 946 internal faces, and the operator's per-cell answer
    against the independent oracle on a linear field there

B2  THE EXACT-CANCELLATION CLAIM WAS PINNED WHERE IT IS A TAUTOLOGY. The case
    runs the MASS path, where `faceFlux` is one array slot read by index, so a
    GATHER would read the same phi[f] from both sides and negate it: the
    assertion holds for any implementation. ADR-071 Decision 2 is a claim about
    the VELOCITY path, where a gather would re-derive the interpolated face
    value per cell. The author had found the same tautology in the experiment's
    own Q2a that morning and had not followed it into the suite
    FIXED: a new case takes the two-cell mesh on the VELOCITY path with a
    non-constant velocity and every facet a slip wall, so each cell's total is
    that one internal face's contribution and the sum must be exactly zero

B3  `largestMagnitude` HAD ONE ASSERTION, ON A FIELD WHERE EVERY CELL IS ZERO.
    A reduction over an all-zero set cannot distinguish max, min, first, last,
    sum or a constant
    FIXED: a new case on the fixture whose two cells are 4.0 and 2.0, with the
    two required to differ, plus an all-negative field so dropping `std::abs`
    returns zero and dies

B4  `toString`'s MAPPING WAS NEVER PINNED. The case checked non-empty,
    not-"unknown" and all-distinct. Rotating the seven returned strings by one
    position leaves all three true, so the suite passed while a non-finite
    result reported `geometry_describes_another_topology`. No string literal
    `toString` returns appeared anywhere else in the repository
    FIXED: one assertion per enumerator

B5  THE MASS PATH'S "NO SKEWNESS TO REPORT" WAS ASSERTED ON A ONE-CELL MESH
    with no internal face, where `skewness` is empty by ADR-065's contract and
    the figure is 0.0 for every path and every implementation. Hoisting the
    skewness reduction above the mass path's early return survived it
    FIXED: the claim moved onto `TheSummaryCountsMatchTheTopology`, which runs
    the mass path over eight reference bodies with thousands of internal faces

B6  THE SYMMETRY CASE ASSERTED AN UPPER BOUND WHERE ITS CLAIM IS A LOWER ONE.
    "The projected form is NOT exactly zero on at least one face -- which is
    the whole point of the decision" was followed by
    `CHECK(worstProjected <= 8 eps scale)`, which passes if it IS zero. And
    that unasserted premise is what gives the exact-zero assertions their
    power, because three of the tet's four facets lie in coordinate planes
    where the residual is exactly zero by construction
    FIXED: `CHECK(worstProjected != 0.0)`

B7  AN ANSWER-RELATIVE TOLERANCE, in the file that forbids them in capitals.
    `worst <= kExactOnAReferenceBody * max(|trace|, 1.0)` -- P19-GRAD-001's r8
    and P18-ELEM-001's F4, recurring inside the suite of the milestone whose
    own first experiment had already made the same mistake in its denominator
    that morning. `divergenceErrorScale` was two cases away and unused
    FIXED: a per-cell bound in units of the products accumulated

B8  THE FILE HEADER NAMED `cellSurfaceVectorSum` AS "the independent authority"
    ON THE WINDING AND SAID "the closure case is where it is consulted". The
    accessor was never called anywhere in the file -- the claim existed only in
    two comments, and the evidence directory was going to cite it
    FIXED: the closure case now reads ADR-065's own owner-signed sum per cell
    and compares it against the sum the test forms from `faceAreaVector` and
    `owner`, which is the one place the suite's reading of ADR-064 is checked
    against the geometry module's
```

#### A false count in one of the fixes

The first draft of the new weight case asserted `internalFaces > 1000` on the
Cylinder, which has **946**. The number was written by analogy with the closure
case's `totalInternal > 1000`, which is a sum over all eight bodies rather than
one of them. That is this milestone's own trap (`internalFaces() > 100` on the
Block, which has six — O11) and P19-GRAD-001's before it (`> 10` on the same
body), for the **third** time. The claim is asserted where it is true, and what
actually establishes the content — that the weights are not all 2/3 — is a
separate count of how many differ.

### Mutation verification

#### The original sweep, before the reviews

Sixteen distinct mutations, each applied ALONE to a clean tree, built, and
required to be killed by its named test; then the tree restored, rebuilt, and
verified BYTE-IDENTICAL to the original — which the harness checks and prints,
rather than assuming the restore worked.

The harness writes without metadata and stamps a fresh mtime, because
P19-IC-001's first sweep reported a test failing on a tree with no mutation in
it: `shutil.copy2` preserved mtimes, ninja skipped the rebuild, and a stale
binary produced a failure that was not there.

**THIS SWEEP TOOK FOUR PASSES AND THE FIRST KILLED 8 OF 16.** P19-GRAD-001's
took five and killed 10 of 20; P19-DIFF-001's killed 15 of 20 first time. The
first pass here was the worst of the three, and it was the most useful thing in
the milestone — **until the reviews, which found six more surviving mutations
this sweep had not thought to try.**

```text
pass 1   16 probes    8 KILLED   6 SURVIVED   2 VOID
pass 2    8 probes    4 KILLED   4 SURVIVED   0 VOID
pass 3    4 probes    3 KILLED   1 SURVIVED   0 VOID
pass 4    1 probe     1 KILLED   0 SURVIVED   0 VOID
         ------------------------------------------------
         16 of 16 KILLED, and 0 retired
```

Every probe was given a written PREDICTION before it ran. **Six predictions
were wrong**: four said KILLED and were not, and two were flagged AT RISK and
confirmed the risk. Recording them is the point — a sweep whose predictions all
hold has not tested the author's model of their own suite.

**The two VOIDs were the same hazard, and it is a recorded one.** Neither M4 nor
M7 compiled, and neither failure had anything to do with its claim: each removed
the last use of a name and died on `-Werror`. That is P18-ELEM-001's finding F3
— a probe that can exit non-zero for an unrelated reason proves nothing — and
P19-GRAD-001 hit it three times. Both were reformulated to keep every name live
while still breaking the behaviour under test, and both then died.

**One was a mis-pairing, not a finding.** M15 (the boundary total also
accumulates internal faces) was paired with
`AnAnalyticOneCellTetMatchesClosedFormArithmetic`, whose fixture has **no
internal face** — so that test structurally could not see it. Re-paired to
`TheGlobalNetFluxIsTheSumOfEveryCellsNetOutflow`, which sums the boundary faces
independently on a two-cell mesh, it died at once. Counted as the author's error
in pairing, not as a defect in the suite.

**Five were real defects in the suite**, and each was a test that read as
evidence and was not:

```text
a closed cell's total is blind to WHICH constant velocity was used -- for any
  constant U the total is U . sum_f S_f, and closure makes that zero whatever U
  is. Took three passes: moving the claim onto a single facet was not enough
  either, because three of the tetrahedron's four facets lie in coordinate
  planes and are each blind to two of the three components. The test now FINDS
  the slanted facet
a valid moving wall cannot distinguish a prescribed velocity from a zero one,
  and that follows from P19-BC-001's own validation: `bindCfdBoundary` requires
  a wall velocity to be TANGENTIAL, so it has no normal component and its flux
  through its own facet is zero BY CONSTRUCTION. A VELOCITY INLET prescribes
  `-v n_hat`, so its flux is `-v |S_f|`, and the mutation died
a bound loose enough to be robust cannot see a last-bit difference: the
  unstable lerp `w U_P + (1-w) U_N` adds about ONE unit of eps|U|sum|S|/V and
  the bound allows sixty-four. Detection had to come from an EXACT comparison
  against the oracle
an exact cancellation lets a wrong denominator escape through its own guard:
  on a two-cell mesh with values like 1e6 every addition is exactly
  representable, so the imbalance was exactly ZERO and both denominators
  returned zero through the `> 0` guard. The test now asserts the ratio
  IDENTITY and pins it on a reference body
a reduction over equal values is indistinguishable from an assignment.
  Flagged AT RISK before the run, and confirmed
```

**And one survivor was a finding about the CODE.** Deleting the per-cell
finiteness loop still produces a refusal, correctly: `globalNet` is the sum of
the per-cell values, so it is non-finite whenever any of them is. What changes
is the DIAGNOSTIC — the totals check names no cell, the per-cell check names
the cell and the value. The test now asserts the message contains `at cell`,
and the mutation dies on that. The check was kept, and the redundancy is
recorded as L8 rather than discovered later.

#### Six more probes, for the claims the reviews newly pinned

Each is a mutation a reviewer named as SURVIVING the suite as it stood, applied
alone to a clean tree. Every one keeps all names live, so none can die on
`-Werror` for an unused name instead of on its claim.

```text
the weight is the hard-coded 2/3 both hand fixtures have        KILLED
largestMagnitude returns zero                                   KILLED
largestMagnitude drops the absolute value                        KILLED
toString's seven names are rotated by one                       KILLED
the skewness reduction is hoisted above the mass early return    KILLED
the neighbour RE-DERIVES the face value (a gather)              KILLED
                                                        6 of 6 KILLED
```

The last one is the one worth explaining. **Exact cancellation on the velocity
path cannot be broken by any one-line change**: `-flux`, `(-1.0) * flux` and
`0.0 - flux` are all the same double, so a sign mutation does not test it. It
takes a structural change, and the probe was written as one — a gather that
re-derives the interpolated face value from the neighbour's side with the
complementary weight, which is exactly the implementation ADR-071 Decision 2
rejects. It dies on the new velocity-path case.

After the sweep the tree was restored and verified **byte-identical** to the
reviewed tree, and that tree is the one attempt 2 qualified.

Closing the five original suite defects took the suite from 10,692 assertions
to 11,304. Closing the eight review findings took it from 11,304 to **36,930**,
in 40 cases rather than 37. The assertion count more than tripled while the
case count rose by three, which is the shape to expect: most of the gain is
inside existing cases, and the largest single contributor is the new weight
case's walk over the Cylinder's 946 internal faces.

## FAILURE PATHS

Seven problems, declared in phase order, and
`TheDeclaredPhaseOrderIsTheCheckOrder` pins that the implementation checks them
in the order the header declares — for the phases a constructible input can
reach. Which are reached, and which are not, is recorded rather than blurred:

```text
GeometryDescribesAnotherTopology   REACHED   AStaleGeometryIsRefusedFirst
BoundaryDescribesAnotherTopology   REACHED   AStaleBoundaryIsRefusedOnThe-
                                             VelocityPathOnly
ValueLengthWrong                   REACHED   AMassFluxDivergenceNeedsOneValue-
                                             PerFace, AVelocityDivergenceNeeds-
                                             ThreeValuesPerCell
ValueNotFinite                     REACHED   ANonFiniteValueIsRefusedAfterThe-
                                             Length
PrescribedValueMissing             UNREACHABLE
VelocityPolicyIsAnOutcome          UNREACHABLE
DivergenceIsNotFinite              REACHED   ANonFiniteResultIsRefusedRather-
                                             ThanReturned
```

The two unreachable ones are kept deliberately and are **not** counted as
covered. ADR-067's `U` column never holds `Computed`, and the one boundary
assembler sets the velocity whenever the policy is `FixedValue` or refuses by
name, so neither state is constructible through the public API. The second is
kept because a total switch over a four-value enum must answer for the fourth
value, which is a better reason than defence; a `static_assert` ties the
enumerator list to the count, so adding an eighth problem breaks the build until
the list, `toString` and the order test are updated.

**`BoundaryDescribesAnotherTopology` is now refused in phase 2, where it
belongs.** The per-face totality walk had sat inside the prescribed-value phase
while refusing with a phase-2 enumerator, so the header's claim that the enum is
declared in phase order was not what the code did (finding A4). Phase 2 walks
every boundary face now and the phase-5 branch is annotated
`UNREACHABLE WHILE PHASE 2 HOLDS`.

**One degenerate-face path changed from a wrong answer to a refusal.** The
interpolation weight returned `0.5` where there is no positive orthogonal
distance or no unit normal, which would have produced a SUCCESSFUL field
interpolated at a midpoint nobody asked for (finding A3). The exported weight
returns `std::nullopt`, and the scatter turns that into a named refusal rather
than defaulting.

Three in-code guards are unreachable after a successful build, and are recorded
as unreached rather than as tested:

```text
face names a cell the topology does not hold    CfdTopology numbers cells and
                                                faces together, so no such face
                                                can be produced. The guard now
                                                runs BEFORE the span reads it
                                                protects (finding A1)
divergenceAt's non-positive volume branch       the builder refuses a
                                                non-positive volume by name
                                                first
the degenerate-weight refusal                   P16 gives planar facets with a
                                                positive orthogonal distance,
                                                so nullopt is not reachable
                                                through buildCfdTopology today
```

**Atomicity.** Every refusal is `return makeError(...)`, which returns an error
rather than the field under construction, so a caller never receives a partially
scattered field. The field has no public constructor and the builder is its only
friend, so there is no other way to obtain one. Four compile-fail gates hold
that: default-constructing either instantiation, and reaching either private
array.

## PERSISTENCE

**Nothing, and nothing is claimed.** There is no CFD serializer and no CFD
command in the repository: `P19-PERSIST-001` and `P19-CMD-001` are later
milestones and neither is authorized. So there is no save/load round trip to run
and none is asserted. The field has value semantics and a defaulted
`operator==`, which the determinism case uses; that is the whole of its
identity story.

## DETERMINISM

```text
the scatter        runs in FaceIndex order, which ADR-064 fixes in storage
the global sum     runs in CellIndex order, likewise fixed
no unordered container, no parallel reduction, no sort, no clock, no random
  seed and no locale dependence in the production path
within one process DividingTwiceGivesBitIdenticalFields compares two fields
                   built from the same input with the defaulted operator==
across processes   the two repeat stages: 3210 tests x 5 executions = 16,050
                   executions per log, in release-ext and in debug-ext, every
                   one Passed, with 5 executions per ctest ID and no exceptions
across configs     36,930 assertions in 40 cases in Debug, in Release and in
                   Debug-shared -- IDENTICAL, so no assertion is
                   configuration-dependent
across case order  Catch2 seeds its shuffle differently each run, and the
                   seeds observed on the three qualified trees differed while
                   the counts did not. So no assertion count depends on the
                   seed -- which is a measurement here, not an argument
```

No FMA: P19-GEOM-001's evidence establishes that the presets set no `-march`,
so GCC emits no fused multiply-add on the x86-64 baseline and each product is
separately rounded in both configurations.

The experiment's seed is **20261107**, recorded in its own output, and Python's
Mersenne Twister is the generator — so the 20,000-mesh measurements reproduce.

## PERFORMANCE

**No benchmark, and none is claimed.** The checklist asks for none, nothing in
the repository consumes a divergence yet, and inventing a baseline for an
operator with no caller would be a number with no meaning. What the
qualification measured, as a by-product, is recorded instead:

```text
the 40 unit cases        17.31 s in Debug, 14.98 s in Release, 16.96 s in
                         Debug-shared, summed over the per-test ctest timings
the slowest single case   2.10 s  TheGlobalNetFluxEqualsTheBoundaryFluxTo-
                                  Rounding (Debug); in Release the slowest is
                                  TheSummaryCountsMatchTheTopology at 1.88 s
the 28 compile-fail gates 106.50 s in Debug, 58.08 s in Release, 58.83 s in
                         Debug-shared -- each gate is a separate compiler
                         invocation, so this is compile time, not run time, and
                         it is the largest single cost this milestone adds
the whole suite           4215.5 s, 3721.5 s and 4044.9 s of summed per-test
                         time in the three presets
the whole qualification   3 h 00 m 04 s wall clock, 17 stages
```

The operator is one pass over faces plus one pass over cells, with two
`std::vector<double>` of length `cells` allocated and nothing else. That is a
statement about the code, not a measurement, and it is not offered as one.

## REGRESSION

**This is attempt 2. Attempt 1 passed, and is VOID.** It reached a PASS before
either independent review had been run; the reviews then found thirteen defects,
and fixing them changed `include`, `src` and `tests`. The freeze's own rule is
that a qualification whose source or tests move afterwards is void and is run
again from clean, so it was. **Attempt 1's twenty logs are preserved under
`qualification/attempt-1/` rather than erased** — it passed genuinely, and the
reason it does not count is worth keeping on the record.

Qualified from clean on three presets, then two repeat stages, and **every
figure below was written into `FREEZE.txt` before the run** so that it could be
reconciled rather than explained afterwards.

```text
VERDICT, from the harness's own sentence and not from a shell's exit status:

    Qualification passed: every stage exited 0.
    qualification finished Sat 11/07/2026 19:28:19.12, 0 stage(s) failed
```

```text
stages                17 of 17 exit 0, parsed from the stage table:
                      5 per preset (configure, clean, build, no-op rebuild,
                      ctest) x 3, plus 2 repeats
full set              6449/6449 Passed in debug-ext, in release-ext and in
                      debug-shared-ext. 6449 result lines, 6449 distinct ctest
                      IDs, ctest's own "100% tests passed out of 6449", and the
                      only verdict token present is `Passed`
repeat set            3210 selected (the harness logged "selects 3210 test(s)"
                      for each, so the P19-DOF-001 trap -- a filter matching
                      nothing and exiting 0 -- cannot have fired)
                      16,050 result lines per log = 3210 x 5, all Passed,
                      5 executions per ctest ID with no exceptions
failure verdicts      0 lines matching `tests failed out of`, `The following
                      tests FAILED`, `(Failed)`, `(Timeout)`, `(Exception)`,
                      `(Subprocess aborted)` or `(Not Run)` in any of the five
                      ctest logs -- counted per log, so the zero is a count and
                      not an absence of output
expected, from the    6449 = 6381 (P19-GRAD-001) + 40 unit + 28 compile-fail
freeze                3210 = 3170 (P19-GRAD-001) + 40 unit
                      BOTH EXACT. compile-fail is excluded from the repeat, as
                      in every milestone, which is why it grows by 40 and not 68
the eight identities  unchanged before the first build and after the last test
                      run, and equal to the eight FREEZE.txt recorded in
                      advance -- all three comparisons checked by parsing both
                      files, not by eye
                        apps              fc398a3d   unchanged since GRAD
                        include           18d1f2af   MOVED
                        src               b36f4e8f   MOVED
                        tests             964099c3   MOVED
                        examples          4da234eb   unchanged since GRAD
                        cmake             5a382115   unchanged since GRAD
                        CMakeLists.txt    3ec3c10f   unchanged since GRAD
                        CMakePresets.json 3229f0f8   unchanged since GRAD
                      EXACTLY THREE MOVED, and they are exactly the three this
                      milestone edited. The three were PREDICTED in the freeze
                      script and compared as a SET, so "exactly the three" is a
                      measurement; the other five are byte-identical to
                      attempt 1's as well as to GRAD's
candidate             df99157a9731b36f6f4f28772a54cf2a0fa0d2ce == HEAD
warnings              0 lines matching `warning:` or `error:` in all three
                      build logs
-Werror               BETTERCAD_WARNINGS_AS_ERRORS:BOOL=ON, read from each
                      BUILT tree's own CMakeCache.txt rather than from a preset
shared really shared  BUILD_SHARED_LIBS:BOOL=ON in debug-shared-ext's cache and
                      OFF in the other two, with CMAKE_BUILD_TYPE Debug,
                      Release and Debug respectively
no-op rebuilds        each recompiled nothing: one line, "Checking git
                      revision"
clean was real        806, 806 and 819 files removed before the first build.
                      The counts are per-tree and are not comparable across
                      presets, which is why the claim is that the clean
                      happened, not that the delta means something
the structural gates  architecture.layering PASSED in all three presets
                      (0.59 s, 0.60 s, 0.63 s), and cli.mesh.zero-match-guard
                      PASSED in all three (116.70 s, 118.53 s, 132.76 s)
the qualified binaries [div] 36,930 assertions in 40 cases and [cfd] 667,506 in
                      399, re-measured on all three trees the qualification
                      itself built -- identical in all three, and identical to
                      the pre-freeze figures
stderr                run-qualification.err is NOT empty, and what is in it
                      matters: 270 lines, every one git's "CRLF will be replaced
                      by LF" notice, and nothing else -- classified by shape,
                      so the claim is that there is no other kind of line
containment           the two production files include no OCCT header, no Qt
                      header, and name no OCCT or Qt identifier -- checked by
                      include form and by identifier, because a loose grep for
                      `<Q` matches the template parameter `<Q>` and reported ten
                      false hits the first time
```

### Counting traps that fired during this reconciliation

All of them in the measurement, none in the result. The first three are carried
forward from attempt 1 and fired again; the last two are new and both are in my
own tooling.

```text
grep -c "Passed"      OVERCOUNTS by exactly 1 per full log and by 5 per repeat
                      log. Accounted for BY NAME, not waved away: the single
                      test unit.StructuralValidation_RecordsTheWorstOfEvery-
                      MetricWhetherOrNotItPassed, once per execution
counting by NAME      UNDERCOUNTS by 2: two display names are each registered
                      under two ctest IDs, so 6449 IDs show as 6447 names. The
                      count is taken by ctest ID
grep -c "Failed"      returns hits in a run where NOTHING failed -- 74 lines
                      per full log and 205 per repeat log carry a failure word,
                      every one a test NAME on a Start line. The authoritative
                      reading is the three together: the verdict tally has the
                      single key `Passed`, 0 marker lines are unparsed, and
                      ctest's own summary says 100% of 6449
A PARSE THAT FINDS    The stage-table regex was anchored at end-of-line, and
NOTHING READS AS A    every stage line continues with a TIMESTAMP after the
PASS                  exit code. It matched 0 of 17 lines and so reported
                      "stages that did NOT exit 0: 0" -- literally true and
                      completely empty. This is P19-DOF-001's own failure in a
                      new place: a check whose denominator is zero cannot fail.
                      FIRST FOUND IN ATTEMPT 1 AND FIXED IN ONE SCRIPT ONLY --
                      it fired again here in a SECOND script that had never
                      been audited. The parse now prints the stage-line count
                      first and asserts 17, which is what caught it
A BACKSPACE THAT      And the repair stacked a second bug behind the first. The
LOOKS LIKE A WORD     fix was applied through a shell heredoc, so the intended
BOUNDARY              `\b` reached Python as a NON-RAW string escape and became
                      a literal BACKSPACE byte (0x08). The regex still
                      compiled, still matched nothing, and the stage count
                      stayed 0 -- looking exactly like the unfixed first bug.
                      Two bugs with one symptom is how a stack hides. Fixed by
                      writing the patch as a FILE rather than through a
                      heredoc, which is the fourth time backslash handling has
                      cost this milestone a wrong answer
```

### And one stale literal in my own reporting

`div_times.py` printed `all 6446 tests, summed cpu-ish: ...` — **6446 was typed
into the format string** while the total beside it was computed. It was attempt
1's figure, so after the three review cases the script reported a stale count
that read exactly like a measurement and disagreed with the other counter's
6449. Resolved by making it count what it sums; it now prints
`all 6449 tests (COUNTED, not typed)` in all three presets. A reporting script
must not contain a hand-typed count.

## KNOWN LIMITATIONS

```text
L1  NOTHING CONSUMES THIS DIVERGENCE. It is one term of an equation that has
    not been assembled: P19-MOM-001 and P19-PRES-001 are later and
    unauthorized. The operator is complete, validated and unused, and that is
    the honest state of it. P19-GRAD-001's L1 says the same of its gradient,
    and wiring the two together is a coupling step no checklist contains

L2  THE VELOCITY PATH IS AS MESH-SENSITIVE AS GREEN-GAUSS, EXACTLY. It is the
    same surface sum with the same interpolation. Q4 shows the sum is
    linear-exact with the TRUE face value, so every departure on a skewed mesh
    belongs to the interpolation, and `worstSkewness` is the measured bound on
    it. A mesh-independent divergence would be the trace of ADR-070's
    least-squares gradient; it is NOT built, because the checklist states the
    surface-sum identity and a second method would duplicate ADR-070 to answer
    a question nothing asked. Decision 10 records the refusal

L3  GLOBAL CONSERVATION IS A MEASUREMENT, NOT A GUARANTEE, and this is the one
    place where the honest claim is weaker than a reader might assume. Per
    internal face the cancellation is BIT-EXACT and structural. Globally it is
    not and cannot be: each cell accumulates its own faces before the two meet,
    and `(a + x) + (b - x)` is not `a + b`. Q2b measured bit-exactness on
    40.34% of 20,000 meshes with a worst relative imbalance of 1.125e-16. The
    operator reports `relativeImbalance`; it does not assert it is zero, and
    ADR-071's first draft was wrong to predict that it would be

L4  NO ACCEPT-OR-REFUSE ON MESH QUALITY. Imbalance and skewness are REPORTED;
    only a non-finite result is refused. ADR-063 finding F1 reserves a quality
    threshold to P19-VALID-001 and to the owner, so no number here is a gate

L5  NO PERSISTENCE, NO COMMAND, NO CLI SURFACE, and no claim about any of them

L6  TWO OF THE SEVEN REFUSALS ARE UNREACHABLE through the public API --
    `PrescribedValueMissing` and `VelocityPolicyIsAnOutcome` -- and are recorded
    as unreached rather than as covered. Both are kept: ADR-067's `U` column
    has no fourth value, and a total switch must still answer for it by name

L7  THE CROSS-OPERATOR CHECK SHARES THE INTERPOLATION WEIGHT.
    `TheVelocityDivergenceAgreesWithTheTraceOfTheGreenGaussGradient` compares
    against ADR-070, which uses the same weight, so a shared wrong weight would
    survive it. What closes that is the independent weight oracle
    (INDEPENDENT VALIDATION reference 4), which reads neither accessor
    production reads. Before the reviews this limitation was recorded as being
    closed by the linear-field case, and finding B1 showed it was not

L8  THE PER-CELL FINITENESS CHECK IS REDUNDANT IN BEHAVIOUR. `globalNetFlux` is
    the sum of the per-cell values, so it is non-finite whenever any of them is,
    and deleting the per-cell loop still produces a refusal. It is kept for the
    DIAGNOSTIC -- the totals check names no cell, the per-cell check names the
    cell and the value -- and the test asserts the message contains `at cell`.
    The redundancy is recorded here rather than discovered later

L9  THE PREDICTORS CANNOT DISAGREE WITH THE OPERATORS, because they share one
    decision path. `ASuccessfulInputPredictsNoProblem` pins that a successful
    input predicts no problem; it does not pin that two implementations agree,
    and it is not offered as that. The same limitation P19-GRAD-001 recorded as
    its L9 and P19-DIFF-001 as its L8. AND THE PREDICTOR CANNOT SEE THE OUTPUT
    PHASE: it judges an output it never computes, so an input that passes every
    input phase predicts nothing even where the sum would overflow. The header
    says so, and the case pins it with a `double` max flux that predicts
    `nullopt`

L10 `DivergenceIsNotFinite` IS A TAG NO CALLER CAN READ. The condition is
    reachable and tested, but both entry points return a plain Error and the
    predictors cannot reach the output phase, so the value is observable only
    through the diagnostic's WORDING. That sits against the rule
    `CfdBoundary.hpp` states as "a message is not a tag" and answers with
    `bindCfdBoundaryFailure`. NOT FIXED: adding a `cfdDivergenceFailure`
    accessor is new public API this checklist does not contain. The header
    states the asymmetry and names the rule it sits against. Found by reviewer
    A as finding A5

L11 THE INTERPOLATION WEIGHT STILL HAS TWO DEFINITIONS IN THE REPOSITORY.
    This operator now calls the exported `cfdFaceInterpolationWeight`, but
    P19-GRAD-001's `CfdGradient.cpp` keeps a private copy of the same
    expression. The non-degenerate arithmetic is identical, so no number
    differs; the DEGENERATE branches are not, and ADR-071 Decision 6's claim
    that the weight "has exactly one definition" was false when written and now
    carries a labelled amendment saying so. Unifying the gradient's copy means
    editing another milestone's translation unit, which this checklist does not
    authorize. Found by reviewer A as finding A2
```

## RESULT

```text
TASK:            P19-DIV-001 — Divergence / Flux Operator
IMPLEMENTATION:  CfdDivergence.hpp / .cpp -- the face-integrated flux
                 scattered into the owner and the negated neighbour, two entry
                 points, seven declared refusals, ADR-071
TESTS:           40 unit cases, 36,930 assertions, IDENTICAL in Debug, Release
                 and Debug-shared; 28 compile-fail gates; 6449/6449 in three
                 presets; 3210 x 5 in two repeat stages
VALIDATION:      rational arithmetic on a one-cell tet (exact 4.0, volume
                 checked first); a constant field on eight reference bodies
                 bounded by the measured closure; a linear field against
                 trace(A) with true face values; an INDEPENDENT weight oracle
                 over 946 internal faces; the trace of ADR-070's gradient;
                 rigid-motion invariance; and a 20,000-mesh experiment run
                 before any C++ existed
REVIEW:          author's pass (O1-O19, one point wrong and three incomplete),
                 then two independent reviews finding 5 code defects and 8
                 suite defects, converging on the two sharpest. 22 mutations
                 killed in total: 16 in the original four-pass sweep, 6 more
                 for the claims the reviews pinned
RESULT:          PASS
EVIDENCE:        docs/verification/P19-DIV-001/
                   FREEZE.txt                        the predictions, in advance
                   experiments/                      the pre-implementation
                                                     measurement
                   qualification/                    attempt 2, 17 stages
                   qualification/attempt-1/          the VOID attempt, kept
```

## REVISION

```text
r1  THE MILESTONE REACHED A FREEZE, A LAUNCH AND A PASS WITH NO INDEPENDENT
    REVIEW. The three milestones before it each carry one, and CLAUDE.md's
    lifecycle puts ADVERSARIAL REVIEW before QUALIFICATION. The author's own
    pass and a sixteen-probe mutation sweep had been treated as sufficient.
    The reviews, run late, found thirteen defects. This is recorded as the
    first defect of the milestone because it is the one that let the other
    twelve reach a passing qualification

r2  THAT QUALIFICATION IS VOID, by the freeze's own rule, because the fixes
    changed source and tests. Re-run from clean as attempt 2; attempt 1's logs
    are preserved rather than erased

r3  ADR-071 DECISION 6'S "exactly one definition" WAS FALSE WHEN IT WAS
    WRITTEN. Three copies existed. The ADR carries a dated amendment naming
    all three, stating that the non-degenerate arithmetic is identical and the
    degenerate branches are not, and recording that unifying the gradient's
    copy is out of scope (L11)

r4  THE WEIGHT ORACLE WAS A TRANSCRIPTION OF PRODUCTION under a header
    claiming independence -- P19-GRAD-001's r5 defect recurring one milestone
    later. Re-expressed from different accessors, and the file header
    corrected. The same correction applies to `cellSurfaceVectorSum`, which
    the header named as "the independent authority" and which was never called
    (B8)

r5  ThePredictorAgreesWithTheOperatorOnEveryFixture WAS RENAMED to
    ASuccessfulInputPredictsNoProblem, because the old name claimed agreement
    between two implementations and the case pins something weaker (L9). The
    name now says what the case does

r6  `internalFaces > 1000` ON THE CYLINDER, WHICH HAS 946 -- written by
    analogy with a sum over all eight bodies. The third occurrence of this
    family in two milestones. Asserted where it is true, with the content
    carried by a separate count

r7  TWO COUNTING TRAPS IN MY OWN TOOLING, both recorded under REGRESSION: a
    stage regex that matched nothing and so could not fail, in a second script
    after the first had been fixed; and a `\b` that became a literal backspace
    byte when routed through a shell heredoc, hiding behind the first bug's
    symptom. Plus a hand-typed `6446` in a reporting script's format string

r8  THE EXPERIMENT'S Q2a IS A TAUTOLOGY, found by re-reading the script after
    the run. Kept and labelled, because the committed output must match the
    committed script; its real content is Q1. Reviewer B then found the same
    tautology in the suite's own cancellation case (B2), which the author had
    not followed through from the experiment
```
