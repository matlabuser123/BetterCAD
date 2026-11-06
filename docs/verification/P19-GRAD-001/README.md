# P19-GRAD-001 — Gradient Reconstruction

```text
TASK:            P19-GRAD-001 — Gradient Reconstruction
IMPLEMENTATION:  cell-centred grad(p) and grad(u_i) by weighted least squares
                 and by Green-Gauss, with the boundary face value taken from
                 ADR-067's frozen policy table (ADR-070)
TESTS:           46 unit cases, 82,762 assertions in Debug and 82,762 in
                 Release; 24 compile-fail gates
VALIDATION:      the divergence identity sum_f C_f (x) S_f = V I measured on
                 real geometry; an exactly linear field against its own
                 gradient on eight reference bodies; the tensor law
                 G' = R G R^T under a compound rotation; two independent
                 implementations of the same quantity differenced against each
                 other; and a closed-form eigenvalue oracle for the
                 conditioning gate
RESULT:          PASS
EVIDENCE:        docs/verification/P19-GRAD-001/
```

## SCOPE

Authorized 2026-11-07 by the owner's "implement and fully verify", with the
fourteen item checklist quoted in full. **The authorization was recorded in
`TODO.md` before any code was written**, as P19-DIFF-001's and P19-IC-001's
were, in the four places that carry a milestone's status.

What this milestone found already present, and did not build:

```text
every metric it divides by           P19-GEOM-001 / ADR-065
  cellVolume, faceAreaVector, faceCentroid, cellCentroid, ownerToNeighbour,
  cellToFace, orthogonalDistance, faceUnitNormal, skewness, and
  cellSurfaceVectorSum -- which ADR-065 made the AUTHORITY on the sign each
  face is taken with, and whose header warns that summing a cell's four stored
  area vectors without consulting `owner` leaves a residual of order one
the adjacency                        P19-TOPO-001 / ADR-064
  cellFaces, owner, neighbour, isInternal, boundaryFaceOf, faceOf
the boundary policy table            P19-BC-001 / ADR-067
  treatmentOf(kind), frozen, and the only place the table exists
the component-blocked layout         P19-DOF-001 / ADR-066
the consumer                         P19-DIFF-001 / ADR-069
  assembleCfdDiffusion already accepts a face gradient and needs no change
```

**NOTHING IN THE REPOSITORY RECONSTRUCTED A FINITE-VOLUME GRADIENT.** An
investigation of the whole tree found gradients only in `thermal` and
`structural`, both FEM shape-function gradients on Tet4 (`grad T = G Te`,
`eps = B u_e`), which divide by a Jacobian determinant and are element-local.
Those modules are at layers 45 and 50, so `cfd` at 42 could not include them
even if the method applied. Their CONVENTIONS were followed: a gradient is a
typed quantity and never a bare double, and a non-finite gradient is refused
rather than propagated.

## BASELINE

`d46a2d1`, P19-DIFF-001's record commit. 6311 tests per preset, 622 ticked
boxes.

`P19-DIFF-001`'s limitation L1 is what this milestone narrows: the diffusion
operator takes its non-orthogonal correction from a caller-supplied face
gradient, measures the omission per face, and reports the worst fraction,
because the gradient was not its to build.

**L1 IS NARROWED, NOT CLOSED, AND THE DIFFERENCE IS THE POINT.** This milestone
produces a CELL gradient. A face gradient is an interpolation of cell gradients,
and interpolating to faces is a coupling step this checklist does not contain —
so `assembleCfdDiffusion` is unchanged, its `faceGradient` input is still empty
on every call in the suite, and the correction it would apply has still never
run. Said plainly: **nothing consumes this milestone's output.**

## ARCHITECTURE

ADR-070, ten decisions. The three that shaped the code:

### The method, chosen by measurement rather than by preference

The checklist asks for a Gauss gradient and for a least-squares gradient
**"if needed"**. That conditional is discharged by a measurement or it is not
discharged at all, so the measurement was made before any C++ existed — the
discipline ADR-069 Decision 2 used for the surface-vector decomposition. Seed
`20261107`, recorded.

First, an identity. For any closed polyhedron, the divergence theorem applied to
the field `x` gives `sum_f C_f (x) S_f = V I`. Measured over 20,000 random
tetrahedra, the worst relative deviation is **2.900e-16** against an eps of
2.220e-16. Its consequence decides the milestone's shape:

```text
(1/V) sum_f phi(C_f) S_f  =  (1/V) [ a sum_f S_f + sum_f (g . C_f) S_f ]
                          =  (1/V) [ 0 + V g ]  =  g     exactly, on ANY mesh
```

using ADR-065 Decision 2's closure identity for the first term. **So Green-Gauss
is linear-exact on any mesh when given the true value at the face centroid, and
every departure from exactness it shows belongs to the INTERPOLATION.** That
changes what the remedy is, which is why it was worth pinning rather than
assuming.

Then the size of the interpolation error, with neighbour centroids displaced
tangentially by a controlled multiple of the cell-to-face distance — which is
what skewness is. Worst relative error over 3,000 samples per row, split by cell
shape so two different causes are not conflated:

```text
                        WELL SHAPED (3 r_in / r_circ >= 0.3)
   skewness   Gauss(true)   Gauss(interp)   least-squares   LSQ eig ratio
       0.00      2.51e-14        2.51e-14        9.00e-15            62.5
       0.25      2.42e-14        2.43e-01        8.18e-15            73.1
       1.00      1.91e-14        8.93e-01        6.01e-15            74.9
       6.50      2.08e-14        5.90e+00        1.98e-11        166420.8

                        ANY SHAPE (slivers admitted)
       0.00      6.16e-13        3.76e-12        1.31e-11        208349.3
       0.25      1.05e-12        2.08e+01        1.73e-11        222954.9
       1.00      1.60e-12        4.22e+01        3.45e-12        142984.0
       6.50      1.01e-12        2.66e+02        5.41e-11        2129763.0
```

Four readings, each a measurement:

1. **At skewness zero the two Gauss columns are the same number**, 2.51e-14. The
   interpolation is exact exactly when there is no skewness.
2. **At any non-zero skewness Green-Gauss loses linear exactness outright**: 24 %
   at skewness 0.25 on a well-shaped cell, 590 % at the 6.5 ADR-065 measured on
   RM-MESH-03.
3. **Cell shape amplifies what skewness causes** — the sliver rows are one to two
   orders worse at the same skewness. Two causes, and only the second is a
   mesh-quality question, which ADR-058 allocates to P19-VALID-001.
4. **Least squares is linear-exact throughout**, but its normal matrix is not
   always well conditioned: four neighbours determining three unknowns is barely
   over-determined.

No P16 reference mesh has zero skewness. **So least squares is needed, and the
measurement is why.** Both methods are implemented: least squares because it is
exact, Green-Gauss because the checklist names it and because the difference
between two implementations on the same mesh is the only honest answer to "skew
mesh behaviour".

**The candidate that was almost chosen is recorded**: Green-Gauss with an
iterative skewness correction, the standard remedy, rejected because it needs
the gradient to compute the gradient and so brings a convergence criterion, an
iteration count and a divergence failure mode into a checklist containing none
of the three.

### One template, two instantiations, and the unit aliases move

`CfdGradientField<Q>` with `Q` the gradient quantity, mirroring
`CartesianVector`'s own documented rationale: because the quantity is the
template argument, a pressure gradient cannot be passed where a velocity
gradient is wanted, and the compiler says so rather than a runtime tag.

**The rejected alternative is recorded**: one field class carrying a
`CfdVariable` tag, with accessors returning an empty optional on a mismatched
read. It needs one class instead of two instantiations, and it converts a
compile-time error into a `std::nullopt` a caller can ignore — the "sentinel a
caller forgets to check" ADR-064 rejected for `neighbour`, wearing an optional's
clothes.

**The one public API change**: `VelocityGradient` and `VelocityGradient3` move
from `CfdDiffusion.hpp` to `CfdData.hpp`, and `PressureGradient`/
`PressureGradient3` join them there. The reason is one declaration instead of
two: a second identical typedef would be a second source of truth for a type the
two operators must agree on exactly. Every spelling is unchanged — same names,
same namespace, and `CfdDiffusion.hpp` already includes `CfdData.hpp` — so every
existing consumer compiles unchanged, which was verified by building the module
before anything new was written. **No `core/units` alias is added**, for
ADR-063 Decision 7's reason, although `thermal` sets the opposite precedent with
`dimensions::temperatureGradient`.

### A slip wall couples the components, which the API had to admit

This is the finding of the UNDERSTAND phase that most changed the design. A slip
wall gives `U_b = U_P - (U_P . n) n`, so the boundary value of component `i`
depends on **all three** components of the owner cell's velocity: the gradient of
one velocity component is not a function of that component's field alone.
ADR-066 says the segregated predictor assembles no row coupling `u` to `v`, and
that stays true of the MATRIX; it is not true of this boundary value.

So `CfdVelocityGradientInput` takes the whole component-blocked array rather
than one component's slice. **That removes a refusal by construction rather than
by skipping a check**: there is no state in which a symmetry face is present and
the velocity it needs is absent, so no enumerator for it exists. P19-DIFF-001
needed `SymmetryNeedsTheCurrentVelocity` precisely because its input took only
what it thought it needed.

## BLAST RADIUS

```text
touched            include/bettercad/cfd/CfdData.hpp      4 aliases added,
                                                          2 of them moved in
                   include/bettercad/cfd/CfdDiffusion.hpp 2 aliases moved out
                   include/bettercad/cfd/CfdGradient.hpp  new
                   src/cfd/CfdGradient.cpp                new
                   src/cfd/CMakeLists.txt                 one source registered
                   tests/cfd/CfdGradientTests.cpp         new
                   tests/compile_fail/CfdGradientMisuse.cpp new
                   tests/CMakeLists.txt                   one source registered
                   tests/compile_fail/CMakeLists.txt      one group, 24 cases
                   tests/cli/ZeroMatchGuard.cmake         2 group counts,
                                                          46 anchored names
direct callers     NONE. Nothing in the repository calls either entry point
                   outside the tests
indirect           every consumer of CfdDiffusion.hpp, through the alias move.
                   `src/cfd/CfdDiffusion.cpp` and `tests/cfd/CfdDiffusionTests.cpp`
                   name VelocityGradient and were rebuilt first, before any new
                   code existed, to prove the move is source-compatible
serialization      NONE. A gradient is DERIVED by ADR-058's own classification:
                   never persisted, never in history, never an authority. There
                   is no CFD serializer in src/io/json at all
regeneration       NONE. Nothing is stored, so nothing can go stale
the CLI            NONE. No command reaches a gradient
reference models   read, not changed
```

The alias move is the only thing in this milestone that could break code outside
it, and it is the first thing that was built.

## IMPLEMENTATION

```text
include/bettercad/cfd/CfdGradient.hpp   the operator's surface
  CfdGradientMethod                     GreenGauss | LeastSquares
  CfdGradientProblem                    7 refusals, in PHASE ORDER, with a
                                        static_assert on the count
  kCfdGradientConditioningLimit         1e12, a floating-point limit
  CfdGradientSummary                    method, cells, the three policy counts,
                                        worstSkewness, worstConditioning,
                                        constantModeIsExact
  CfdGradientField<Q>                   one template, two instantiations, no
                                        public constructor
  CfdGradientInput                      method + SI cell pressures
  CfdVelocityGradientInput              method + component + the WHOLE
                                        component-blocked velocity
  4 free functions                      two reconstructions, two predictors
src/cfd/CfdGradient.cpp
  CfdGradientBuilder                    the only friend, so a reconstruction is
                                        the only door
    View                               nested, so no externally-linked member
                                        names an internal-linkage type
    normalSystem                       THE ONLY COPY of the least-squares row
                                        arithmetic -- the gate judges
                                        conditioning with it and the assembly
                                        solves with it
    problemOf                          THE ONLY DECISION PATH, with the message
                                        built only when a caller passes
                                        somewhere to put it, which is what makes
                                        the predictors' noexcept honest
  eigenvalueRatio                       closed-form symmetric 3x3 eigenvalues
  solveSymmetric                        closed-form cofactor solve
include/bettercad/cfd/CfdData.hpp       4 unit aliases, 2 of them moved in
include/bettercad/cfd/CfdDiffusion.hpp  2 aliases moved out, with the reason
```

Four things in the implementation were decided rather than fallen into.

**NO SILENT ZERO EVER STANDS IN FOR A FAILURE.** The first draft left a zero
gradient behind in two places — a non-positive cell volume in the Green-Gauss
path, and a `solveSymmetric` that returned empty in the assembly after the gate
had passed. A zero gradient is a legitimate answer elsewhere in this operator
(it is the right answer for a uniform field), so it must never double as a
failure signal. Both are refusals now. The second is unreachable while the gate
and the assembly agree, and is recorded as such rather than claimed tested.

**THE ROW ARITHMETIC EXISTS ONCE.** The first draft had the gate build its own
copy of the normal system to judge conditioning. That is the defect a review
found in P19-DIFF-001, where the surface-vector split was written out twice in a
file whose own header forbade exactly that. `normalSystem` is now called by
both.

**THE SIGN IS READ, NEVER ASSUMED.** `CfdGeometry`'s own header warns that
summing a cell's four stored area vectors without consulting `owner` leaves a
residual of order one rather than of order eps, because the vectors are wound out
of the OWNER. The Green-Gauss loop reads `owner` for every face, and a mutation
that ignores it is in the sweep.

**THE INTERPOLATION IS THE STABLE LERP.** `phi_N + w (phi_P - phi_N)`, not
`w phi_P + (1 - w) phi_N`. The second is not exactly `phi` when the two cells
agree, which is the defect P19-DIFF-001's own test caught in its face viscosity.

## TESTS

```text
unit.CfdGradient_*             46 cases, 82,762 assertions in Debug
                               82,762 in Release -- IDENTICAL, so no assertion
                               is configuration-dependent
compile_fail.cfdgrad.*         24 gates, every one passing, and the CONTROL
                               compiles clean under -Werror -- P19-GEOM-001's
                               recorded lesson, where a failing control broke
                               `cmake --build` for the whole project
cli.mesh.zero-match-guard      two group counts and 46 anchored names; the guard
                               names and the TEST_CASE names were diffed and are
                               the same SET, not merely the same count, and the
                               BUILT list was diffed against the source's own
                               TEST_CASE names
architecture.*                 205 of 205, layering included
```

WHERE EACH CLAIM IS CHECKED, AND WHY THERE:

```text
the volume identity   on three hand fixtures, to 64 eps in units of the
                      PRODUCTS the sum accumulates. It is the reason
                      Green-Gauss works, so it is asserted on real geometry
                      rather than taken from the ADR's 20,000-sample run
the analytic pair     |d| = 5/4, d . n = 3/4, cos(theta) = 3/5, all exact. NOT
                      orthogonal, which is what makes it the only fixture that
                      can tell ADR-070 Decision 5's two forms apart
the orthogonal pair   d exactly parallel to n, so the two forms of a
                      ZeroGradient row AGREE -- which is why Decision 5 cannot
                      be tested here, and why the suite says so
the one-cell tet      four boundary facets, no internal face: the only fixture
                      where the boundary contribution is the whole answer, and
                      where Green-Gauss has nothing to interpolate
INTERIOR CELLS        the only place the VELOCITY column's exactness can be
                      asserted. Its fit takes no boundary row at all, so the
                      four neighbour differences are exact for a linear field
                      whatever the boundary prescribes
the reference bodies  linear exactness, covariance, determinism, conditioning
                      and the skewness comparison, on meshes nobody designed to
                      flatter the operator
```

**THE BOUND IS IN UNITS OF THE PRODUCTS, AND THE FIRST ATTEMPT WAS WRONG.** A
least-squares fit works on DIFFERENCES of cell values, so a large constant offset
is represented with an absolute error of order `eps * |phi|`, their difference
carries twice that, and the gradient is that difference over a distance. The
error therefore has nothing to do with the size of the gradient. The first
version of the linear-exactness case asserted a RELATIVE bound of 1e-12 and
measured **4.16e-11** at an offset of 1e6 — which is
`eps * offset / (|g| |d|)` behaving exactly as it should, and not a defect. The
bound is now `64` units of `eps * valueScale / lengthScale`, the same factor
P19-DIFF-001 settled on, and the worst reading over the file is about one unit.
This is P18-ELEM-001's finding F4, reproduced by the author rather than inherited
from it.

**THE SKEW COMPARISON REQUIRES GREEN-GAUSS TO FAIL.** Not tolerates — requires.
`GreenGaussIsExactWhereTheFaceIsUnskewedAndIsNotWhereItIsSkewed` asserts
exactness on the orthogonal pair with the premise checked (`|skewness| < 1e-12`)
and asserts `> 1e-6` relative error on the analytic pair with its premise checked
too (`|skewness| > 1e-3`). An operator exact in both places would contradict the
identity test's reasoning, and this case would catch that.

## INDEPENDENT VALIDATION

Five independent references, none of them the code itself.

```text
the divergence identity   sum_f C_f (x) S_f = V I, derived from the divergence
                          theorem applied to the field `x`, measured on real
                          geometry to 64 eps and over 20,000 random tetrahedra
                          to 2.900e-16 before any C++ existed
an exact linear field     phi = offset + g . x, with `g` written into the test.
                          The expected gradient is the coefficient, never a
                          recovery call
the tensor law            G' = R G R^T under a compound rotation about an
                          off-origin axis along no body direction, with the
                          multiplication and transposition written out in the
                          test. NOT `g'_i = R g_i` row by row, which would be
                          WRONG -- the same error the structural suite records
                          for an element stiffness matrix
two implementations       Green-Gauss and least squares differenced against each
                          other where both are exact. A surface sum over four
                          faces against a 3x3 cofactor solve: different
                          arithmetic, so agreement is not a shared defect
a conditioning oracle     the extreme eigenvalue ratio of the same normal matrix,
                          built from the four boundary rows and solved in closed
                          form in the test, so the operator's own arithmetic is
                          not the authority on whether its own gate was right
```

## ADVERSARIAL REVIEW

Three passes: the author's own, written down before either independent review
reported; then two independent agents, each given the code and the tests but not
the author's conclusions, one asked to disprove the correctness and one asked to
find what the suite does not test.

**THIS IS THE FIRST P19 MILESTONE WHERE THE INDEPENDENT REVIEWS FOUND
SUBSTANTIVE CODE DEFECTS RATHER THAN DOCUMENTATION DEFECTS.** P19-IC-001's
nineteen findings were one code defect and eighteen claims or tests;
P19-DIFF-001's were two documentation claims and one test. Here there were four
behavioural defects in the operator, one of which the author had no reason to
suspect, and one in the suite that could not fail at all.

### The author's own pass, before either review reported

Nine findings. Two needed code or tests, two were documentation claims stronger
than their evidence, four were recorded rather than fixed, one was an addition
worth making.

```text
A1  THE VELOCITY COLUMN'S LINEAR EXACTNESS WAS NOT ASSERTED ANYWHERE. Three
    rows differing proves the component selector works; the covariance case
    compares two reconstructions of the same answer and would pass if both
    were wrong in the same covariant way. CLOSED by the interior-cell case:
    an interior cell's fit takes no boundary row, so the four neighbour
    differences are exact whatever the boundary prescribes -- which is the only
    place the claim can be made, because no condition in ADR-067's table can
    prescribe an arbitrary linear velocity facet by facet
A4  THE CONDITIONING LIMIT WAS LOAD-BEARING AND UNPINNED. Closed from both
    sides, and it uncovered the ADR defect below
A5  THE IDENTITY TEST'S COMMENT CLAIMED AN INDEPENDENCE IT DID NOT HAVE: it
    reads `owner` exactly as production does, so a shared misunderstanding of
    the winding convention would survive. COMMENT CORRECTED, test kept
A6  `largestMagnitude` was never checked against a hand value. CLOSED
A9  nothing cross-checked the two methods where both are exact. CLOSED
A2  `worstSkewness` is read twice per internal face. Harmless for a maximum,
    recorded for the milestone that adds a mean (L6)
A3  a zero volume reports a phase-1 tag from the assembly. Recorded (L8)
A7  two refusals are unreachable. Recorded, and sharpened by both reviews
A8  the problem count is a literal, not an independent figure. Recorded
```

And it found the ADR defect that matters most: **ADR-070 Decision 7 described
the degenerate refusal as the strictly singular case and never named
`kCfdGradientConditioningLimit`**, which the code has and which also refuses
solvable systems. A reader of the decision would not have learned the constant
existed. That is the same class of defect a review found in ADR-069 Decision 7,
which described an interpolation weight that did not exist. Corrected, with the
correction labelled.

### The four code defects the independent reviews found

**A NON-FINITE GRADIENT WAS RETURNED AS A SUCCESS.** Nothing between the
accumulation and the store checked the answer. Phase 4 requires every cell
VALUE to be finite, which admits 1.8e308, and `CfdGeometry` accepts coordinates
of the same magnitude -- so a surface sum over faces of opposite orientation
overflows to infinities of each sign, cancels to a NaN, passes `volume > 0`, and
reaches a caller through `at()` and `largestMagnitude()` with no error at all. A
tiny cell volume reaches the same place by division, with no overflow in the sum.
`thermal` and `structural` both refuse a non-finite gradient rather than
propagate it; this did not. Now refused as `GradientIsNotFinite`, and the test
that pins it also pins the one thing the predictor cannot promise: Green-Gauss's
refusal happens in the ASSEMBLY, so the predictor -- which assembles nothing --
returns no problem while the reconstruction still refuses.

**A SELF-REFUTING DIAGNOSTIC.** `solveSymmetric` returns empty for two different
reasons -- a non-positive determinant and a non-finite solution -- and the gate
collapsed both into `NeighbourhoodIsDegenerate`. Two adjacent cells at
+/-1.7e308 Pa, both finite, give a perfectly healthy normal matrix and an
infinite right-hand side, and the refusal read *"an extreme eigenvalue ratio of
3, against the 1e+12 a solve can carry"* -- a message that contradicts itself and
blames the mesh for a fault in the field. The two causes are reported separately
now.

**AN OUT-OF-BOUNDS READ, GUARDED ONLY BY A TABLE IN ANOTHER MODULE.**
`boundaryValue` dispatched on the policy alone and never consulted which field it
was serving, so a pressure face carrying `Symmetry` would call
`symmetryValueAt`, which reads `values[1 * cells + cell]` and
`values[2 * cells + cell]` -- past the end of a span holding one value per cell,
which does no bounds checking. Unreachable today because `treatmentOf` never
puts `Symmetry` in the pressure column; one row of a frozen table in a different
module away from undefined behaviour. Refused by name as
`SymmetryNeedsAVectorField`, and the switch consults the field as well.

**`constantModeIsExact` WAS HARD-CODED TRUE FOR LEAST SQUARES, AND ITS STATED
REASON WAS FALSE.** The reason given was that every right-hand side is a
difference, exactly zero for a uniform field. At a Symmetry face the row value is
`U_b,i - U_P,i = -n_i (U_P . n)`, which is not zero for a uniform velocity that
is not tangential to the wall -- so the flag read `true` on a mesh where a
uniform field reconstructs to something non-zero. It is computed from the
symmetry-face count now, and both halves are asserted: the gradient really is
non-zero there, and the flag really does say so.

### And one the independent oracle found, which no review predicted

The test-gap review's charge was that this file's conditioning oracle was a
line-for-line transcription of `eigenvalueRatio` -- same variable names, same
`detB` expansion -- while the suite's header claimed its oracles were written out
again so that a transposition could not be mirrored. It would have mirrored
exactly such a transposition. **Replacing it with a cyclic Jacobi
diagonalisation, which shares no arithmetic with the closed form, immediately
exposed a defect in the implementation:**

```text
eigenvalues        the textbook form     the truth      error
(1, 1, 2)          2.000000008           2              4.1e-09
(1, 2, 3)          3                     3              1.5e-16
(1, 1, 1e12)       9.9988e11             1e12           1.2e-04
(1, 1e6, 1e12)     3.1283e11             1e12           A FACTOR OF 3.2
(1, 1.5, 1e12)     inf                   1e12           A FALSE REFUSAL
```

Taking the smallest eigenvalue as `mean + 2p cos(phi + 2pi/3)` is the textbook
form and it is accurate only where the spectrum is well spread: `phi` comes
through `acos`, which is ill-conditioned at its endpoints, and the endpoints are
precisely the repeated-eigenvalue case an ill-conditioned normal matrix
approaches. The last row is the serious one -- a positive-definite matrix
reported as having a non-positive eigenvalue, so a cell that could be solved was
refused because the formula broke rather than because its neighbourhood failed to
determine a gradient.

`lambda_max` still comes from the trigonometric form, where `cos(phi)` is the
well-conditioned end; the other two now come from the invariants
(`lambda_mid + lambda_min = trace - lambda_max`,
`lambda_mid * lambda_min = det / lambda_max`) by the cancellation-free quadratic.
Still closed-form, so ADR-070 Decision 8 is unchanged, and the new form agrees
with Jacobi to every printed digit in all nine measured cases.

**THE CLEAREST EVIDENCE THAT IT MATTERED IS A MEASUREMENT THAT CHANGED.** The
flat-tetrahedron sweep's split went from **4 refused / 1 solved** to **3 refused
/ 2 solved**: the 1e-6-thick cell, whose true conditioning is 3.6e11, was being
falsely refused. Both figures are in the evidence, the earlier one as what a
defect looked like.

### The test defects the reviews found

**ONE TEST NEVER CALLED THE OPERATOR.**
`TheInterpolationWeightMatchesAnIndependentNormalProjection` contained no call to
any production entry point: every assertion was arithmetic on this file's own
`weightOracle`, and one of them --
`CHECK(value + weight * (value - value) == value)` -- reduces to
`value + weight * 0.0 == value`, true for every finite weight and every value.
Its own comment claimed the oracle's weight was used to predict Green-Gauss's
answer, and no such prediction was in the body. **Deleted**, with the reason
recorded in its place; the weight is properly pinned elsewhere, against both
candidate denominators with the discrimination asserted.

**HALF OF ANOTHER TESTED THE TEST.** A loop asserted that
`slipOracle(v, n) . n == 0` at every facet, which is identically true for any
unit `n` and involves no production value. **Removed.**

**AN ENTIRE BOUNDARY ARM WAS DEAD.** `boundaryValue`'s `ZeroGradient` case is
reached only by Green-Gauss, because least squares intercepts that policy earlier
and writes a constraint row instead -- and every one of the twelve Green-Gauss
reconstructions in the file ran on a boundary where the differentiated field is
`FixedValue`. So the documented behaviour, that a zero normal derivative means
the face takes the owner's own value, had no coverage, and replacing
`view.at(owner)` with `0.0` survived the whole suite. Closed with an oracle that
builds the surface sum both ways and requires them to differ.

**`worstConditioning` WAS ONLY EVER BOUNDED, NEVER MEASURED.** Both reviews found
this independently. Every assertion was an inequality -- `>= 1`, `< 1e12`,
`== 0.0` for Green-Gauss -- so scaling the reported value by two survived, and
so did replacing the max-over-cells reduction with the last cell's value. It is
the one number ADR-070 Decision 7 hands to P19-VALID-001 to judge a mesh by.
Both are closed: the value against Jacobi, and the reduction against an
independently computed maximum with a body where the last cell is not the worst.

**`worstSkewness`'s REDUCTION HAD THE SAME HOLE**, for a sharper reason: its
value was pinned exactly once, on a fixture with exactly ONE internal face, where
"the worst over the mesh" and "the last face visited" are the same number.
Closed on the reference bodies, over more than a thousand internal faces in all.

**PHASE 8's "LEAST SQUARES ONLY" GUARD HAD NO COVERAGE.** Deleting it changed no
outcome, because every Green-Gauss mesh in the suite was well conditioned. Closed
by requiring Green-Gauss to reconstruct a flat cell that least squares refuses --
which is the asymmetry the guard exists for.

**AND THE static_assert NAMED A TEST THAT DID NOT EXIST.** The header pointed at
"the phase-order test, which asserts the declaration order IS the check order",
and the only ordering assertion in the file was length-before-finiteness. Written
now, across every phase a constructible input can reach.

### Both reviews converged on the same five things

The two agents were given the same files and different questions, and they agreed
on: the two unreachable refusals, `worstConditioning` never being measured, the
transcribed oracle, the constant-mode flag's subtleties, and the phase-order
claim. Convergence of two independent readings on one finding is the strongest
signal either produces, and all five are closed or recorded.

### Where they disagreed, and how it was settled

The adversarial review called the phase-order claim LIKELY-but-unreachable; the
test-gap review called it serious and unpinned. **Both were partly right, and
measurement settled it**: the order IS assertable across the four phases a
constructible input reaches, and the within-a-phase ordering the adversarial
review described is real -- `BoundaryDescribesAnotherTopology` is also emitted
from inside the prescribed-value loop, so index order decides there, not enum
order. The header now says exactly that, in the wording `CfdBoundary.hpp` uses
for the same situation.

### What the reviews found and this milestone did NOT fix, with reasons

```text
the two unreachable refusals     KEPT, documentation corrected. Both reviews
                                 showed there is no door at all -- not merely no
                                 document path -- and the test-gap review quoted
                                 the project's own policy that "an enumerator no
                                 input can produce is worse than no enumerator".
                                 That policy was written about a state the DESIGN
                                 excludes; these are excluded by a frozen table
                                 in another module that this operator only reads,
                                 so the refusals are a defence against that table
                                 moving. Recorded as L4, with the reachability
                                 claim corrected rather than left standing
FP contraction between the
  gate and the assembly          NOT A RISK ON THESE PRESETS, and it is measured
                                 rather than argued: P19-GEOM-001's evidence
                                 records that the presets set no `-march`, so on
                                 x86-64 baseline GCC emits no FMA at all
                                 (checked there: `-O3` gives mulsd/addsd). So the
                                 two inline sites cannot contract differently
the three Green-Gauss silent
  fallbacks                      ONE FIXED, TWO RECORDED. The `continue` that
                                 omitted a face from the surface sum while still
                                 dividing by the whole volume is now a refusal --
                                 that one returned a WRONG gradient as a success.
                                 The `0.5` weight fallback and the `squared > 0`
                                 row guards are unreachable while `describes`
                                 holds and are listed among the unreached
                                 defensive paths
the unreached defensive code     ELEVEN SITES, enumerated by the test-gap review
                                 and listed in this evidence rather than claimed
                                 as tested: the equal-eigenvalue branch, the
                                 clamp, `solveSymmetric`'s guards behind the
                                 gate's short-circuit, the weight fallback, the
                                 three `continue` guards, the three `squared > 0`
                                 guards, the census's missing-treatment arm, and
                                 both `toString` fallbacks
the summary's field order        NOT GUARDED BY A TYPE, and deliberately.
                                 `CfdGradientSummary` has four `std::size_t`
                                 members in a row, which no compile-fail gate can
                                 separate; the mixed-boundary case gives the
                                 three policy counts three DISTINCT values down
                                 each column plus a completeness sum, so the
                                 value tests carry it instead. A division of
                                 labour, recorded as one
```

### Count

Nine findings from the author's own pass, ten from the adversarial review, and
the test-gap review's four holes plus eleven proposed mutations. Five code
defects fixed, two tests deleted or cut back, six new cases written, eight
documentation claims corrected, and eleven defensive paths recorded as unreached.
Nothing was deferred that the reviews called a defect.
### Mutation verification

Twenty distinct mutations, each applied alone to a clean tree, built, and
required to be killed by its named test; then the tree restored, rebuilt, and
verified at zero markers. The harness writes without metadata and stamps a fresh
mtime, because P19-IC-001's first sweep reported a test failing on a tree with no
mutation in it — `shutil.copy2` preserved mtimes, ninja skipped the rebuild, and
a stale binary produced a failure that was not there.

**THIS SWEEP WAS THE MOST USEFUL THING IN THE MILESTONE, AND IT TOOK FIVE PASSES.**
P19-DIFF-001's sweep killed 15 of 20 on its first pass. This one killed 10, and
the seven survivors were not a formality to be explained away: four were real
defects in the suite, and closing them grew it from 33 cases to 39 and from
26,748 assertions to 56,027.

```text
pass 1   20 probes   10 KILLED   7 SURVIVED   3 VOID
pass 2   11 probes    6 KILLED   4 SURVIVED   1 VOID
pass 3    3 probes    1 KILLED   2 SURVIVED   0 VOID
pass 4    2 probes    1 KILLED   1 SURVIVED   0 VOID
pass 5    1 probe     1 KILLED   0 SURVIVED   0 VOID
         ----------------------------------------------
         18 of the 20 killed, and 2 RETIRED WITH REASONS
```

#### The four real defects the sweep found in the suite

Each was a test that read as evidence and was not.

**A CONSTANT-FIELD TEST AT ZERO CANNOT SEE AN UNSTABLE INTERPOLATION, AND
NEITHER CAN ONE AT A DYADIC CONSTANT.** This took three passes to close and is
the sharpest thing the sweep found. All three constant-field cases used `0.0`,
and `w * 0 + (1 - w) * 0` is exactly `0` whatever the weight, so the unstable
form `w phi_P + (1 - w) phi_N` was indistinguishable from the stable
`phi_N + w (phi_P - phi_N)`. Raising the constant to `17.25` did not help
either, and the reason is worth recording: 17.25 is 69/4, so both products are
EXACT and their sum is exactly the constant whatever the weight. A search over
200,000 random constants at this fixture's measured weight of 2/3 found that
**16 % of them do distinguish the two forms**, and the constant is now
`12345.6789` — which the unstable form returns as `12345.678900000003`. The
mutation then died.

**A SUITE THAT ONLY ASKS FOR COMPONENT X CANNOT SEE A HARD-CODED COMPONENT
INDEX**, because for component X the mutation IS the original. Fixed once by
exercising all three components — and it survived again, because the case
asserted only that changing the other two components MOVES the answer, and a
wrong index also moves it, just to the wrong place. **Proving that something
moves is not proving where it moves to.** Closed by building the whole
least-squares fit from the projection oracle `U_b = U_P - (U_P . n) n` in the
test's own arithmetic.

**A TEST THAT CHECKS THREE ANSWERS DIFFER DOES NOT CHECK THEY ARE RIGHT.** The
component selector was pinned only by `TheThreeVelocityComponentsGiveThreeDifferentGradients`,
and a cell-blocked read gives three different wrong answers. Closed by the
interior-cell case, which asserts the whole velocity gradient tensor against the
tensor the field was built from — and which the author's own adversarial pass had
already asked for, for the same reason, before the sweep ran.

**A FLAG TEST WHOSE FIXTURES ALL TAKE ONE ARM IS A TAUTOLOGY.** The Green-Gauss
constant-mode flag was compared against an independently computed bit-zero
closure, which is the right comparison — but all four hand fixtures have a
bit-zero closure, so `true == true` held everywhere and a flag hard-coded `true`
survived. The reference bodies are now included, where seven of the eight do NOT
have a bit-zero closure, and both arms are asserted to be reached.

#### The two probes that are retired rather than killed, with reasons

**"RAISE THE SKEWNESS ON BOUNDARY FACES TOO" IS IMPOSSIBLE, NOT MERELY A NO-OP.**
`CfdGeometry::skewness` returns `std::nullopt` for a boundary face, by ADR-065
Decision 6's design: skewness measures the error in interpolating between TWO
cell values, and a boundary face has one. There is no boundary skewness for a
mutation to leak into `worstSkewness`. That is a guarantee the geometry provides,
not a gap in this suite, and it is why the probe is retired rather than
reformulated a third time.

**"HOIST THE NORMAL SYSTEM OUT OF THE CELL LOOP" PROBED DEAD CODE, AND THE CODE
WAS DELETED RATHER THAN TESTED.** `normalSystem` took `Sym&` and `Vec&` and reset
them on entry; both call sites declared fresh zero-initialised locals inside
their own per-cell loop, so no test could distinguish the reset being present
from its being absent. A probe that tried to make it matter had to mutate the
function and both call sites together, which tests a code shape that does not
exist. `normalSystem` now RETURNS its system: the dead reset is gone, the
out-parameters are gone, and a system that accumulates across cells is
unrepresentable rather than untested. **That is a better outcome than a kill.**

#### The prediction that was contradicted

ADR-070 Decision 6 claimed the least-squares weights cannot affect linear
exactness, because for an exactly linear field every residual is identically
zero at the true gradient. The sweep was given that as a PREDICTION: replacing
`1/|d|^2` with `1.0` should SURVIVE a linear-exactness test.

**It was killed, twice.** The mathematics is unchanged — the mutation does not
make the fit inexact. What it does is leave the internal rows at weight 1 while
the boundary rows keep `1/|d|^2`, so the normal matrix becomes badly SCALED, its
conditioning rises, and the rounding error rises with it past a bound stated in
units of `eps * valueScale / lengthScale`. The decision now says "in exact
arithmetic" and separates the two claims, and the evidence says which one the
test is actually sensitive to.

A sweep that only looks for kills cannot test a claim of this shape. **A
prediction that turns out wrong is worth more than one that turns out right**,
and recording it is what makes the eighteen kills mean anything.

#### Three probes were VOID on the first pass, all for the same documented reason

`-Werror` casualties: one used a `normOf` this translation unit does not declare
(it never needed a vector norm), one left a `unitNormal` parameter unused, one
left an `other` variable unused. That is P18-ELEM-001's finding F3 —
"a probe that does not compile is a VOID probe, not a survivor" — and its
recorded remedy was applied: reformulate so the mutated code still uses every
name. Counting them as survivors would have claimed three gaps that do not
exist; counting them as kills would have claimed three verifications that never
ran.

#### And three were MIS-PAIRED by the author, which says nothing about the suite

"Ignore `owner` and never negate the stored area vector" was paired with the
constant-mode flag test, which does not read the mutated line; re-paired with
the Green-Gauss exactness case, on a two-cell fixture where one cell must
negate, it died. "Use the unstable lerp" was paired with a LEAST-SQUARES test,
and the interpolation exists only in the Green-Gauss path. "Divide by `|d|`"
was paired with a case whose orthogonal section makes the mutation a no-op —
there `d` is parallel to `n`, so `|d|` and `d . n` are the same number — and
whose skewed section only requires Green-Gauss to be INEXACT, which a wrong
weight also is. That last one needed a new case: the whole surface sum, built in
the test with both candidate denominators, with the two required to differ.

#### The full accounting

```text
KILLED, first pass
  hard-code the interpolation weight to a midpoint
  swap the owner and the neighbour in the interpolation
  write a ZeroGradient row as the cell-to-face vector  (ADR-070 Decision 5)
  negate the least-squares right-hand side
  drop the boundary rows from the least-squares system
  swap the two summary measurements
  accept a non-finite cell value
  check the value before the length, reversing the phase order
  drop the conditioning limit from the gate
  replace the inverse-distance weight with 1.0        (PREDICTED TO SURVIVE)

KILLED after re-pairing or reformulation
  ignore owner and never negate the stored area vector
  collapse Symmetry onto ZeroGradient                 (the error ADR-067 warns of)
  read the difference at this cell, not the neighbour

KILLED after the suite was repaired
  read the velocity cell-blocked instead of component-blocked
  hard-code the Green-Gauss constant-mode flag to true
  drop the normal projection's component index
  divide by |d| instead of d . n
  use the unstable lerp

RETIRED with a reason
  raise the skewness on boundary faces too            (impossible: no boundary
                                                       skewness exists)
  hoist the normal system out of the cell loop        (dead code, now deleted)
```

## FAILURE PATHS

Seven refusals, declared in phase order, and the order is asserted: an input
that is both too short and holds a NaN reports the LENGTH.

```text
1 GeometryDescribesAnotherTopology   tested by tag
2 BoundaryDescribesAnotherTopology   tested by tag
3 CellValueLengthWrong               tested by tag, down BOTH columns, which
                                     expect different lengths
4 CellValueNotFinite                 tested by tag, for NaN and both infinities,
                                     at EVERY index of both arrays -- the
                                     measured gap P19-IC-001 closed was a NaN
                                     test that exercised one component only
5 PrescribedValueMissing             UNREACHABLE and untested; see L4
6 VariablePolicyIsAnOutcome          UNREACHABLE and untested; see L4
7 NeighbourhoodIsDegenerate          tested, and REACHABLE -- which contradicted
                                     this author's expectation
```

**THE DEGENERATE REFUSAL IS THE ONE THAT TAUGHT SOMETHING.** The expectation was
that `buildCfdGeometry` would reject a flat tetrahedron and that this refusal
would be unreachable. It accepts all five thicknesses tested, down to 1e-15,
because its own refusals are a non-positive cell volume and an inconsistent face
orientation and a flat cell has neither. Four of the five are then refused here.
Measured split, found before it was asserted: **0 refused by the metrics, 4
refused by the gradient gate, 1 solved** — the 1e-3 case, at a conditioning of
3.6e5. So the gate catches a mesh the metrics do not.

Atomicity: the whole input gate runs before any geometry is read for the answer,
the assembly writes only locals and a vector it owns, and every failure returns
before the field is handed back. There are no out-parameters, so a refused
reconstruction cannot leave anything partly written.

## PERSISTENCE

Nothing here is persisted and nothing here is canonical. A gradient is DERIVED by
ADR-058's own classification: never persisted, never in history, never an
authority. There is no CFD serializer in `src/io/json` at all and this milestone
adds none. The compile-fail group carries the structural half: a
`CfdGradientField` cannot be conjured, so a field that exists has been through
the gate.

## DETERMINISM

```text
the per-cell order   CfdTopology::cellFaces', ascending FaceIndex. STORED, so
                     the accumulation order is fixed once rather than per caller
the sign             read from CfdTopology::owner, never assumed
the cell order       ascending CellIndex, and each cell's gradient depends only
                     on its own neighbourhood, so the cells are independent and
                     their order cannot change a value
no unordered         no unordered container in either file
two reconstructions  BIT-IDENTICAL, through the field's own value equality, on
                     the fixtures and on all eight reference bodies at over 500
                     cells. And the equality is shown NOT to be always-true: a
                     nudged input and the other method both compare unequal
```

## PERFORMANCE

Not measured, and not claimed. There is no baseline: nothing in `cfd` had a
performance figure before this milestone and this one does not create one. A 3x3
solve per cell is more expensive than four multiply-adds per face, and by how
much is unknown.

The measurements worth recording are structural rather than temporal:

```text
allocation          one std::vector<double> of 3 * cells, and nothing inside
                    either loop. The normal system is a returned value of six
                    plus three doubles
the gate's cost     phase 8 walks the geometry once more, which is why it is last
                    among the input phases: every earlier phase is a span scan or
                    a boundary walk
Green-Gauss         one pass over each cell's four faces, no solve
least squares       the same pass, plus a closed-form 3x3 eigenvalue ratio and a
                    cofactor solve
```

## REGRESSION

Full qualification, three presets plus two repeat stages. The harness is
byte-identical to P19-DIFF-001's, making this the **sixtieth** milestone run on
an unchanged harness -- and this time **all five** hashes
`run-qualification.cmd` records for itself were verified by `git hash-object`,
where P19-DIFF-001's freeze checked only the two preflight ones. It was also
**self-checked first**, because an unchanged harness is not the same as a
working one:

```text
QUALIFICATION FAILED: 2 stage(s) failed.
PASS: a failed stage gave qualify.cmd exit 2, and a zero-match
      repeat filter was counted as a failed stage.
```

The verdict, which is `run-qualification.out`'s own sentence and
`qualification-times.txt`'s failed-stage count rather than any shell's exit
status:

```text
Qualification passed: every stage exited 0.
qualification finished Sat 11/07/2026  9:06:05.72, 0 stage(s) failed
```

**17 stages, every one exit 0**, total wall time 20040.44 s (5 h 34 m 00 s).
Every duration below is computed from the harness's own timestamps rather than
timed separately:

```text
preset             configure   clean    build   no-op rebuild      ctest
debug-ext                7.88    1.14  1163.22            0.41    2432.07
release-ext              7.55    0.46  1382.60            0.43    3820.20
debug-shared-ext         8.14    2.14  2538.74            1.27    5083.26
repeat release-ext                                                1872.88
repeat debug-ext                                                  1713.23
```

Each ctest figure agrees with the log's own `Total Test time (real)` to within
the resolution of the timestamps -- 2431.99, 3819.93, 5081.17, 1872.78 and
1713.14 s -- so the two readings are independent of each other.

Counts, each reconciled against the figure written into `FREEZE.txt` **before**
the run:

```text
                        predicted   measured by ctest -N   the run
full set, per preset         6381                   6381   6381/6381, x3
repeat set, per preset       3170                   3170   3170/3170, x2
repeat executions per log   15850                     --   15850, x2
of which, new unit              46                     46   46, every log
of which, new compile-fail      24                     24   24, every full log
the rest                      6311                     --   6311, every full log
the rest, repeat              3124                     --   3124, both repeats
```

`rest = 6311` is P19-DIFF-001's own full-set figure and `3124` is its repeat
figure, both reproduced exactly here, so **no pre-existing test moved**. The
architecture gate is 205 of 205 in every full log, `architecture.layering`
among them -- also unchanged from P19-DIFF-001. Across all five logs:
**0 Failed, 0 Skipped, 0 Not Run, 0 Disabled**. The figure that establishes it
is the execution total -- 6381, 6381, 6381, 15850, 15850 -- because that says
every selected test RAN, where "none failed" does not.

The repeat total is 15850 and not 3170 because `--repeat until-fail:5` prints a
BARE `Test #K: name ... Passed` line for each intermediate run and an
`N/M`-prefixed line only for the last. Counting just the prefixed form gives
3170 and understates the work by a factor of five; the two forms together give
3170 + 12680 = 15850, and resolving every line by ctest's own test id gives
**exactly 5 runs for each of 3170 ids** -- a uniform `{5: 3170}`, with nothing
unaccounted.

THE SIXTH COUNTING TRAP FROM P19-DIFF-001 RECURRED, in a new place, and a TENTH
is new. Both were met while reading this run, and both are recorded because the
nine in `FREEZE.txt` are:

```text
trap (vi), again   `grep -c Passed` returns 6382 in each full log and 15855 in
                   each repeat -- one and five MORE than the executions. The
                   excess is a test NAME, whose Start line carries the word once
                   per pass:
                   unit.StructuralValidation_RecordsTheWorstOfEveryMetric...
                   ...WhetherOrNotItPassed
                   So 6381 + 1 and 15850 + 5, every line accounted for. Last
                   milestone the same family of mis-read concerned Skipped; the
                   fix is the same, to count the result MARKERS and not the word
trap (x), new      counting by test NAME undercounts the suite by 2, because two
                   display names are each registered under two ctest ids:
                   `unit.MeshIssueKind_EveryKindHasAName` (#1998, #1999) and
                   `unit.Dependency cycles are reported and block their
                   dependents` (#973, #974). By name the full set reads 6379, by
                   id it reads 6381. Both duplicates are pre-existing, both pass
                   in every log, and both are outside this milestone -- but any
                   count taken by name is wrong by two, and every count above is
                   taken by id
```

Other gates read from the run rather than asserted:

```text
warnings                 0 lines matching warning or error in all three build
                         logs
-Werror                  BETTERCAD_WARNINGS_AS_ERRORS:BOOL=ON, read from each
                         BUILT tree's own CMakeCache.txt, not from a preset walk
shared really shared     BUILD_SHARED_LIBS:BOOL=ON in debug-shared-ext's cache
                         and OFF in the other two, so the third preset tested
                         what its name claims
no-op rebuilds           each recompiled nothing ("Checking git revision" only)
clean was real           "Cleaning... 803 files." before the first build
repeat selection         the harness logged "selects 3170 test(s)" for each
                         repeat preset, so the P19-DOF-001 trap -- a filter that
                         matches nothing and exits 0 -- cannot have fired
qualified candidate      6de4b073db74fb7998f0d42aa736be5d4413d475 == HEAD
stderr                   run-qualification.err is NOT empty, and what is in it
                         matters: 270 lines, every one git's "CRLF will be
                         replaced by LF" notice from the two identity readings.
                         No compiler diagnostic is in it, and the count is
                         identical to P19-DIFF-001's
```

ONE FIGURE ROSE AND IS NOT EXPLAINED HERE. Wall time went from
P19-DIFF-001's 11627.88 s to 20040.44 s, the increase falling almost entirely
on two ctest stages (release 1898.45 -> 3820.20, shared 1845.71 -> 5083.26).
**This milestone's own tests are not the cause**: summed over every test,
`unit.CfdGradient_*` is 28.82 s in Debug, 25.09 s in Release and 52.58 s in
shared -- between 0.43% and 0.60% of each log's duration sum -- and the 24 new
compile-fail gates add 71.91 s to 139.98 s. What dominates the sum is the
compile-fail suite as a whole, 849 tests invoking the compiler for 49% to 63% of
all measured duration, and the pre-existing long tests rose roughly in
proportion across the board (`cli.mesh.zero-match-guard` 136.17 / 120.18 /
259.43 s). The logs do not establish a cause, so none is asserted: it is
reported as measured. **No gate depends on wall time**, and the correctness
figures above are unaffected.

**The qualified tree is the committed tree**, across FOUR readings: the freeze
record, the harness's reading before the first build, its reading after the last
test run, and a re-read from the live working tree by the harness's own method
(scratch `GIT_INDEX_FILE`, `read-tree HEAD`, `add -A`, `write-tree --prefix`,
and `ls-files -s` for the two files). All four are identical on all eight paths:

```text
apps               fc398a3d2e524178cf38e8b667236379d8381086   unchanged
include            1130c5db61470fa98563c030af84b9d9473bd939   moved
src                6f139cf81ffe2b690d735b51489ebf488e125126   moved
tests              6b3f5cf17e98cf1fcae53d5201273bf06101448d   moved
examples           4da234ebf4779f7c16d5df0075f106f7e3d4c958   unchanged
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7   unchanged
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd   unchanged
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e   unchanged
```

Exactly three moved, and they are exactly the three this milestone edited: no
app, no example, no cmake module and neither root file. The two readings taken
at commit time -- the staged tree, and the commit object itself -- cannot appear
in this file's own commit, so this copy records four and the record pass raises
it to six, as P19-DIFF-001's and P19-IC-001's did.

There was **one attempt**, and it is the one reported above. It was launched
detached, as the last two milestones were, because P19-IC-001's second attempt
had been killed by a client teardown mid-run.

## KNOWN LIMITATIONS

```text
L1  NOTHING CONSUMES THIS MILESTONE'S OUTPUT. It produces a CELL gradient, and
    `assembleCfdDiffusion` wants a FACE gradient -- an interpolation of cell
    gradients, which is a coupling step this checklist does not contain. So
    P19-DIFF-001's own L1 is NARROWED and not closed: its `faceGradient` input
    is still empty on every call in the suite, and the non-orthogonal correction
    it would apply has still never run. ADR-070 Decision 1 records why that
    wiring was refused rather than taken, which was tempting precisely because
    it is three lines

L2  NO SKEWNESS CORRECTION, and Green-Gauss is therefore the inexact method on
    every mesh P16 produces. ADR-070 Decision 2's rejected candidate -- an
    iterative correction -- needs the gradient to compute the gradient, so it
    brings a convergence criterion, an iteration count and a divergence failure
    mode into a checklist containing none of the three. The penalty is measured
    instead: `worstSkewness` per reconstruction, and the two methods differenced
    on every reference body

L3  NO SOLVE, NO MOMENTUM EQUATION, AND NO FLOW FIELD. Two operators now exist
    in `cfd` -- a diffusion matrix and a gradient -- and neither has been handed
    to anything. Nothing in this module has produced a velocity or a pressure
    that solves anything

L4  TWO OF THE SEVEN REFUSALS ARE UNREACHABLE AND UNTESTED, and that is a
    materially weaker claim than "every refusal is tested by tag".
    `PrescribedValueMissing` fires only for a boundary whose policy says
    FixedValue and which holds no value, and `bindCfdBoundary` fills the value
    exactly when the policy says so. `VariablePolicyIsAnOutcome` fires only for
    the `Computed` policy, which does not appear down the `p` or `U` columns of
    ADR-067's frozen table. Both are reachable only from a hand-built
    `PreparedCfdBoundary`, which has no public constructor. Recorded rather than
    claimed, the treatment P19-DIFF-001 gave the same situation

L5  THE CONDITIONING LIMIT IS NOT PINNED BY ANY TEST TO BETTER THAN FOUR ORDERS
    OF MAGNITUDE. Every reference body sits below about 1e5, the 1e-3-thick
    tetrahedron at 3.6e5, and the refused cells far past 1e12. A test brackets
    the crossing from both sides, so the limit is known to separate hopeless
    systems from awkward ones -- but it could be 1e8 or 1e14 and every other
    case in this file would pass identically. It is a floating-point guard and
    the evidence supports it only as that; an engineering threshold is
    P19-VALID-001's, by ADR-063 finding F1

L6  `worstSkewness` IS A MAXIMUM, AND AN INTERNAL FACE IS VISITED TWICE. The
    accumulation sits in the per-cell loop, and both of a face's cells read it.
    A maximum is idempotent so the value is right, but the next milestone to add
    a MEAN to this summary will be wrong unless the accumulation moves out of
    that loop. P19-DIFF-001's L5 already recorded that a maximum hides a
    distribution and a consumer would want both

L7  THE VELOCITY COLUMN'S EXACTNESS IS ASSERTED ONLY ON INTERIOR CELLS. No
    boundary condition in ADR-067's table can prescribe an arbitrary linear
    velocity facet by facet -- a moving wall's velocity must be TANGENTIAL
    (P19-BC-001), a velocity inlet's is normal, and a pressure boundary gives
    ZeroGradient -- so a boundary-touching cell is being fitted to a constraint
    the test field violates. The pressure column has no such limit, because a
    pressure outlet can prescribe any value. This was the largest finding of the
    author's own adversarial pass: before it, the velocity column's exactness
    was not asserted anywhere at all

L8  A ZERO VOLUME IN THE GREEN-GAUSS PATH IS REPORTED AS
    `GeometryDescribesAnotherTopology`, which is literally true by
    `CfdGeometry`'s own contract -- a zero volume means the cell is not held --
    but it is reached from the assembly rather than from phase 1, and no test
    exercises it. Unreachable while `describes` holds

L9  THE PREDICTORS CANNOT DISAGREE WITH THE RECONSTRUCTIONS, because they share
    one decision path. What the test pins is that a successful input predicts no
    problem and that the predictor allocates nothing, both of which a caller
    relies on; it is not evidence that two implementations agree. P19-DIFF-001
    recorded the identical limitation as its L8

L10 NO PERFORMANCE RESULT. A 3x3 solve per cell is more expensive than four
    multiply-adds per face, and by how much is unmeasured: nothing in `cfd` has
    a performance baseline and this milestone does not create one. The one
    measurement worth recording is structural -- the gradient allocates one
    vector of `3 * cells` doubles and nothing inside either loop
```

## RESULT

```text
RESULT:   PASS
```

All fourteen checklist items are met. "Pressure gradient" and "Velocity
gradient" are met by one template instantiated twice, which is why the two
carry the same guarantees rather than merely similar ones; the velocity case is
the one that needed the tensor law `G' = R G R^T` rather than a row-by-row
rotation, and getting that wrong was a real possibility the covariance test
closes.

"Gauss gradient" and "Least-squares gradient if needed" are both met, and the
"if needed" was answered by measurement before any C++ existed: the divergence
identity `sum_f C_f (x) S_f = V I` holds to 2.900e-16 over 20,000 random tets,
so Green-Gauss with the TRUE face value is linear-exact on any mesh and all of
its inexactness is the interpolation's. That is why least squares is the default
and Green-Gauss is kept and measured against it rather than dropped (ADR-070
Decision 2). The scripts behind that measurement are committed under
`experiments/` with their output, because an adversarial review found the
founding number had no artifact -- a quantitative justification with no log
behind it is a failed gate, and it was right.

"Boundary contribution" is met for the policies that occur down the `p` and `U`
columns of ADR-067's frozen table, each taken from that table rather than
re-decided here. The slip-wall case is the one worth naming: the row value at a
Symmetry face is `-n_i (U_P . n)`, which couples the components, and a review
found the code had claimed otherwise.

"Constant-field test" is met in both directions and is the item that moved most
under adversarial pressure. `constantModeIsExact` was hard-coded true for least
squares with a stated reason that was FALSE; it is `(symmetryFaces == 0)` now,
and a conjunct for Green-Gauss. The test that was supposed to catch it could
not, because all four hand fixtures have a bit-zero closure and the probe
constant was first 0.0 and then 17.25 -- a dyadic value that cannot distinguish
`w c + (1-w) c` from `c`, since both products are exact. A search over 200,000
constants found 16% that discriminate at w = 2/3, and the suite uses
12345.6789.

"Linear-field exactness / convergence" is met on eight reference bodies with
the bound expressed in units of `eps * valueScale / lengthScale` -- not relative
to the answer, which is the first form and measured 4.16e-11 against a 1e-12
bound at an offset of 1e6. The code was right and the bound was not, which is
P18-ELEM-001's finding F4 restated.

"Skew mesh behaviour" is met as a MEASUREMENT, not a correction: `worstSkewness`
is reduced over the internal faces and reported, and the reduction is now pinned
on a mesh with more than one internal face rather than on a fixture where a max
and an assignment are indistinguishable. Boundary faces contribute nothing
because `skewness` returns `nullopt` there by ADR-065 Decision 6's design --
which is also why one mutation probe is retired as IMPOSSIBLE rather than
counted as killed.

"Rigid-transform covariance" is met under a compound rotation, using a transform
that moves every node while keeping connectivity index-for-index, because P16
does not promise the same tetrahedralisation in a rotated frame.

"SI units" is met by the four aliases and the 24 compile-fail gates, and by the
one public API change being built BEFORE any new code existed, to prove the move
of `VelocityGradient` out of `CfdDiffusion.hpp` is source-compatible.

"Deterministic ordering" is met by the two repeat stages -- 15850 executions
each, five per test, all passing -- and by the Debug and Release assertion
totals being IDENTICAL at 82,762, which says no assertion is
configuration-dependent.

"Independent analytical validation" is met four ways, and one of them found a
defect: replacing the suite's conditioning oracle, which had been a
line-for-line copy of the function it checked, with a cyclic Jacobi
diagonalisation showed the textbook trigonometric form for the SMALLEST
eigenvalue wrong by a factor of 3.2 at a conditioning of 1e12, and reporting a
positive-definite matrix as having a non-positive eigenvalue -- a FALSE REFUSAL
of a solvable cell. An oracle that shares its implementation with the thing
under test is not validation, and this is the clearest evidence in the milestone
for why.

"Regression PASS" and "Evidence recorded" are the run above and this document.

What this milestone does NOT establish is recorded under KNOWN LIMITATIONS
rather than here, and two items belong in any honest reading of the result:
nothing consumes this output yet, because `assembleCfdDiffusion` wants a FACE
gradient and the interpolation that would close P19-DIFF-001's L1 is not in this
checklist (L1); and the predictors cannot disagree with the reconstructions,
because they share one decision path, so what their tests pin is that a
successful input predicts no problem, not that two implementations agree (L9).
There is no performance result (L10).

## REVISION

```text
r1  THE MUTATION SWEEP TOOK FIVE PASSES AND THE FIRST KILLED ONLY 10 OF 20.
    Four of the seven first-pass survivors were real defects in the suite --
    a constant-field test at zero, then at a dyadic constant; a component index
    testable only away from X; a test that checked three answers DIFFER rather
    than that they are right; and a flag test whose fixtures all took one arm.
    Closing them took the suite from 33 cases to 39. The five-pass count is
    recorded because a one-pass 20-of-20 would have been a weaker result
    honestly reported
r2  ONE PREDICTION WAS CONTRADICTED. ADR-070 Decision 6 claimed the weights
    cannot affect linear exactness; the sweep was given that as a prediction and
    killed the mutation twice. The mathematics holds in exact arithmetic; the
    mutation changes the normal matrix's SCALING, so conditioning and rounding
    error rise past a bound tight enough to see it. The decision now separates
    the two claims
r3  TWO PROBES ARE RETIRED RATHER THAN KILLED, with reasons. One is IMPOSSIBLE
    -- `skewness` returns nullopt at a boundary face by ADR-065 Decision 6's
    design, so there is no boundary skewness to leak in. The other probed DEAD
    CODE: `normalSystem`'s reset was unreachable-as-load-bearing because both
    call sites declared fresh locals, and the code was DELETED rather than
    tested. `normalSystem` returns its value now
r4  TWO INDEPENDENT REVIEWS FOUND FOUR BEHAVIOURAL DEFECTS, which is more than
    either P19-IC-001 or P19-DIFF-001's reviews found: a NaN gradient returned
    as a success, a self-refuting diagnostic, an out-of-bounds read guarded only
    by a table in another module, and a constant-mode flag whose stated reason
    was false. All four are fixed and each has a test
r5  AND ONE DEFECT CAME FROM REPLACING A TRANSCRIBED ORACLE WITH AN INDEPENDENT
    ONE. The suite's conditioning oracle was a line-for-line copy of the function
    it checked; a cyclic Jacobi diagonalisation disagreed, and the textbook
    trigonometric form for the SMALLEST eigenvalue turned out to be wrong by a
    factor of 3.2 at a conditioning of 1e12 and to report a positive-definite
    matrix as having a non-positive eigenvalue -- a FALSE REFUSAL of a solvable
    cell. The smallest eigenvalue comes from the invariants now. The flat-cell
    sweep's split changed from 4-refused/1-solved to 3-refused/2-solved as a
    result, and both figures are recorded
r6  ONE TEST IS DELETED AND HALF OF ANOTHER CUT. A test-gap review found that
    `TheInterpolationWeightMatchesAnIndependentNormalProjection` never called
    the operator at all, and that one of its assertions reduced to
    `value + weight * 0.0 == value`. Deleting a test that reads as evidence and
    is not is worth more than keeping it
r7  THE ADR WAS CORRECTED TWICE, BOTH TIMES FOR THE SAME CLASS OF DEFECT THAT
    CAUGHT ADR-069. Decision 7 described the degenerate refusal as the strictly
    singular case and never named the 1e12 limit the code uses; Decision 6
    stated a claim that is true only in exact arithmetic. Both corrections are
    labelled in the ADR rather than written over
r8  THE FIRST TOLERANCE WAS STATED RELATIVE TO THE ANSWER and measured 4.16e-11
    against a 1e-12 bound at an offset of 1e6. The code was right -- that is
    `eps * offset / (|g| |d|)`, P18-ELEM-001's finding F4 -- and the bound is in
    units of `eps * valueScale / lengthScale` now
r9  COUNTING TRAP (i) FROM P19-DIFF-001'S FREEZE RECURRED VERBATIM: a script
    reported 20 compile-fail cases against the 24 written, because its filter
    dropped the last case of each foreach block. The registration was correct
    and the check was not, exactly as last time. The two are compared as SETS
    now, not counts
```
