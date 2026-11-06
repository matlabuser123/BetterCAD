# P19-DIFF-001 — Diffusion Operator

```text
TASK:            P19-DIFF-001 — Diffusion Operator
IMPLEMENTATION:  div(mu grad U) as a per-component scalar CSR matrix and source,
                 over-relaxed surface decomposition, correction caller-supplied
                 (ADR-069)
TESTS:           49 unit cases, 31,688 assertions in Debug and 31,688 in
                 Release; 22 compile-fail gates
VALIDATION:      a closed-form analytic face pair (cos theta = 3/5 exactly), an
                 exactly orthogonal pair, Heron's formula and node-averaged
                 centroids as a one-dimensional oracle, and the geometry's own
                 nonOrthogonality as an independent reading of the deferred
                 fraction
RESULT:          PASS
EVIDENCE:        docs/verification/P19-DIFF-001/
```

## SCOPE

Authorized 2026-11-06 by the owner's "implement and fully verify", with the
fourteen item checklist quoted in full. **The authorization was recorded in
`TODO.md` before any code was written**, as P19-IC-001's was.

Two stale entries in `TODO.md`'s own per-milestone status table were corrected in
the same edit, and both were defects this author left behind in the P19-IC-001
commit: that table still read "P19-IC-001 ... IN PROGRESS; no item is ticked and
no verdict is recorded" after IC-001 had passed and been pushed, and still read
"P19-DIFF-001 NOT AUTHORIZED". The table is live, not history -- the
"kept as history" marker sits *below* it. The IC-001 record's claim that its
authorization was recorded "in six places" was true of the places it touched;
this seventh place was not advanced to PASS. Recorded here rather than rewritten
into that commit, because published history is not rewritten.

What this milestone found already present, and did not build:

```text
the metrics it divides by            P19-GEOM-001 / ADR-065
  orthogonalDistance = d . n, strictly positive for a held face, already
  described there as "the distance a diffusion coefficient divides by"
  ownerToNeighbour, cellToFace, faceAreaVector, faceUnitNormal,
  orthogonalityCosine, nonOrthogonality
the index space                      P19-DOF-001 / ADR-066
  component-blocked cell vectors, and the decision that a segregated predictor
  assembles no row coupling u to v -- so this is a SCALAR operator
the boundary treatment table         P19-BC-001 / ADR-067
  the four-valued per-field policy, read down the U column
the generic symmetric CSR solve      linalg, ADR-052 -- and NOT linked, see below
```

What did not exist, and is this milestone's:

```text
nothing in cfd assembled a matrix of any kind
nothing decided which surface-vector decomposition this discretisation uses
nothing stated the sign convention a later solve would receive
nothing measured how much of the diffusive flux an uncorrected assembly omits
```

Out of scope, and named rather than attempted:

```text
P19-GRAD-001     the face gradient this operator's correction takes as input.
                 ADR-069 Decision 5 is shaped so it plugs in unchanged
P19-LINSOLVE-001 any solve. Decision 3 keeps the CSR arrays in the form
                 linalg::solveSymmetricSparse takes, but nothing here solves
P19-MOM-001      convection, the transient term, the pressure-gradient source
P19-VALID-001    the non-orthogonality THRESHOLD, which ADR-063 F1 reserves to
                 it and to the owner in as many words
```

## BASELINE

```text
HEAD at the freeze     d46a2d1  BetterCAD: record the P19-IC-001 commit
previous milestone     P19-IC-001 PASS 2026-11-06 (code and evidence c7ce82a,
                       record d46a2d1)
cfd subsystem before   516126 assertions in 264 test cases, the figure
                       P19-IC-001's own evidence records
```

## ARCHITECTURE

`ADR-069`, nine decisions. Three are worth reading before the code.

**The decomposition is over-relaxed, and the choice reversed twice before it
settled.** A diffusion flux is `mu grad(U) . S_f`, and only the part of `S_f`
parallel to `d` can be implicit in `U_N - U_P`, so every scheme splits
`S_f = E_f + T_f`. The three standard splits were measured in closed form before
any C++ was written:

```text
                  implicit coefficient   |T|/|S|      |T|/|E|
minimum           mu A cos(theta)/|d|    sin          tan
orthogonal        mu A / |d|             sqrt(2-2cos)  sqrt(2-2cos)
over-relaxed      mu A / (d . n)         tan          sin
```

The first lean was over-relaxed, on the grounds that `orthogonalDistance` is
`d . n` and ADR-065 already calls it the divisor of a diffusion coefficient. The
second was minimum correction, because `|T|/|S| = sin(theta)` is bounded by one
and this milestone DEFERS the correction, so the smallest absolute omission
looked right. The measurement that settled it is the third column: `|T|/|E|` is
what a deferred-correction iteration carries, and it is `tan(theta)` for minimum
correction -- **81.8 at the Cylinder's worst face** -- against `sin(theta)` for
over-relaxed, bounded by one at every angle. The second criterion governs the
operator's permanent use; the first describes a transitional state. Choosing a
permanent discretisation to flatter a temporary condition is the wrong way round,
and the ADR records both wrong turns rather than presenting the answer as
obvious.

**One clause of a standing decision was amended, and only one.**
`src/cfd/CMakeLists.txt` read "NO LINEAR ALGEBRA LINKED. There is no matrix in
this milestone. Admitting one is P19-LINSOLVE-001's decision." The second
sentence is now false and is corrected; the rest stands. **cfd still links no
linear algebra and includes no `linalg` header.** The cost is taken rather than
dodged: `CfdSparseIndex` is cfd's own `uint64_t` alias, the same width and
signedness as `linalg::SparseIndex` by construction so a later solve consumes
these spans untranslated, and the `static_assert` that would turn "same by
construction" into "same, enforced" belongs to the milestone that may
legitimately include that header. That is the weaker guarantee, on purpose.

**The correction is caller-supplied, because every other option was closed.**
ADR-063 Finding F1 measured P16's reference meshes at 31 to 66 degrees mean
non-orthogonality with maxima near 90 and stated that "a non-orthogonal
correction is mandatory in the discretisation, not an option" -- while also
reserving any threshold to `P19-VALID-001`. So this operator may not drop the
correction silently, may not refuse a mesh for being skew, and may not compute a
gradient (that is `P19-GRAD-001`, unauthorized). What is left is to take the
gradient as an input and REPORT the omission per face when none is given. The
payoff is that the split's exactness becomes testable now: a linear field's
gradient is known exactly, so supplying it turns the patch test into a
floating-point identity.

## BLAST RADIUS

Additive. No existing public API changed, and no existing behaviour moved.

```text
new        include/bettercad/cfd/CfdSparse.hpp
           src/cfd/CfdSparse.cpp
           include/bettercad/cfd/CfdDiffusion.hpp
           src/cfd/CfdDiffusion.cpp
           tests/cfd/CfdDiffusionTests.cpp
           tests/compile_fail/CfdDiffusionMisuse.cpp
           docs/architecture/decisions/ADR-069-....md

amended    src/cfd/CMakeLists.txt            two sources, and the one clause
           tests/CMakeLists.txt              one test file, reference block
           tests/compile_fail/CMakeLists.txt the cfddiff group
           tests/cli/ZeroMatchGuard.cmake    two group filters, 49 names
           TODO.md                           the authorization, two stale
                                             entries, then the record
```

Direct dependents: none. Nothing calls the new functions except the new tests,
because the consumer that will -- a momentum predictor -- does not exist.

Indirect reach, and why each is unaffected:

```text
CfdGeometry     read only, through nine accessors. Not modified, so
                P19-GEOM-001's qualification stands
CfdTopology     read only. Not modified
PreparedCfdBoundary  read only, through treatment() and face(). Not modified
CfdFieldMap     NOT USED. The operator indexes cells directly, because ADR-066
                makes component X's block exactly the cell-centred array
linalg          NOT LINKED, and no header included. Verified statically as
                well as by the layering test: no non-comment line of either new
                header or either new source names linalg, thermal, structural,
                OCCT or Qt, and src/cfd/CMakeLists.txt links no linalg target.
                Every mention of those names in the four files is in a comment
                explaining a decision
the layering    the two new headers and two new sources include ONLY cfd (42)
                and core (0) -- not even meshing, which the module uses
                elsewhere. architecture.layering passes, with 205 of 205
                architecture tests
```

**The measurement that settles the blast radius**: the cfd subsystem went from
516126 assertions in 264 cases -- the figure P19-IC-001's own evidence records --
to **547814 in 313**. The deltas are **31688 and 49 EXACTLY**, which is what the
new tests measure on their own, so no pre-existing cfd assertion moved. The
whole-suite arithmetic reconciles the same way:

```text
full set     6311 = 6240 (P19-IC-001) + 49 unit + 22 compile-fail
repeat set   3124 = 3075 (P19-IC-001) + 49 unit
             49 new unit tests inside the repeat, 0 compile-fail -- the repeat
             filter is byte-identical to P19-IC-001's and matches the new tests
             through its existing `unit\.Cfd` term, so nothing was widened
```

## TESTS

```text
unit.CfdDiffusion_*            49 cases, 31,688 assertions in Debug
                               31,688 in Release too -- IDENTICAL, so no
                               assertion is configuration-dependent
compile_fail.cfddiff.*         22 gates
cli.mesh.zero-match-guard      two group counts and 49 anchored names; the
                               guard names and the TEST_CASE names were diffed
                               and are the same set
architecture.*                 205 of 205, layering included
```

WHERE EACH CLAIM IS CHECKED, AND WHY THERE. No reference mesh is near
orthogonal -- the best maximum is the Block's 64.09 degrees -- so the arithmetic
cannot be checked on one. It is checked on two hand-built fixtures that already
existed in the geometry suite:

```text
the orthogonal pair   two tets sharing a face on x + y + z = 10 mm, centroids at
                      (2.5,2.5,2.5) and (5,5,5), so d is EXACTLY parallel to n
                      and theta is exactly zero. The deferred arm is identically
                      zero, and every decomposition agrees -- which is why this
                      fixture alone cannot discriminate between them
the analytic pair     |d| = 5/4, d . n = 3/4, cos(theta) = 3/5, all exact. The
                      deferred fraction must be 4/5 for over-relaxed, against
                      4/3 for minimum correction and sqrt(0.8) for orthogonal
                      correction, so this is where the choice is pinned
```

and on two more that this milestone added, both needed because of what the
reviews found:

```text
the one-cell tet      four boundary facets, no internal face. Three of its
                      facets are axis-aligned, so a slip wall on the z = 0 facet
                      has a normal with NO x part -- which is the only way to
                      reach the state the null-mode flag's recorded defect was
                      about, and it needs a MIXED boundary to do it
a mixed boundary      one condition per facet, so FixedValue, ZeroGradient and
                      Symmetry coexist. No assembly in the suite saw two arms at
                      once until this existed, and a counter incremented in the
                      wrong switch arm needs two kinds present to show
```

The eight reference bodies carry the structural claims instead: every cell
reached, `nonZeros == cells + 2*internalFaces`, the sign pattern, exact symmetry
at 808 cells as well as at two, the interpolation weight over every face, and
the deferred fraction against the geometry's own worst angle.

**AND THE SOURCE VECTOR IS NOW MEASURED, WHICH IS THIS MILESTONE'S LARGEST TEST
CHANGE.** Both reviews found it covered only by `maxAbs(source) == 0.0` and
`> 0.0`; four cases now assert its values against independent oracles:

```text
a non-zero Dirichlet   a moving wall with velocity (2, -1, -1) m/s, tangential
                       to the slanted facet so P19-BC-001 accepts it and with
                       three DISTINCT non-zero components so the component
                       selector is observable. The source must be
                       `gamma * U_b,i` per component, and the three must differ
non-zero cross terms   a per-cell, per-component current velocity, so a
                       cell-blocked read returns a different number than a
                       component-blocked one -- the only way ADR-066's layout is
                       observable in this operator
the correction's sign  equal and opposite on the two sides of an internal face,
                       against `mu g . T_f` computed in the test
the correction's face  a gradient that VARIES per face, so reading it at a fixed
                       index fails
```

## INDEPENDENT VALIDATION

```text
the coefficient         mu |S_f| / (d . n), written out again in the test file
                        rather than read from the operator
the decomposition       the three closed forms -- sin, sqrt(2-2cos), tan for
                        |T|/|E| -- computed in the test and asserted to DIFFER
                        on the analytic face, so the choice is discriminated
                        rather than recorded
the 1D oracle           Heron's formula for the shared face's area and a
                        four-node average for each centroid, both from the MESH
                        NODES -- neither is how CfdGeometry computes them, so a
                        transposition there could not be mirrored here
the skew comparison     on the analytic face the same oracle must DIFFER from
                        the assembled coefficient by exactly 5/3 = 1/cos(theta),
                        which rules out an operator dividing by |d|
the deferred fraction   recomputed from nonOrthogonality(face), an accessor the
                        operator never calls, over every face of all eight
                        reference bodies
the symmetry arm        the diagonal compared against sum Gamma n_i^2 built from
                        the face normals in the test
the null mode           A 1 computed through multiply(), compared relative to
                        largestMagnitude()
```

**The measured figures**, which is what makes the tolerance a reason rather than
a number:

```text
the split's exactness   worst 0.542 eps-units on the orthogonal pair and 0.084
                        on the analytic pair, against a bound of 64. In units of
                        eps * (the products summed), per P18-ELEM-001's finding
                        F4 -- a large constant offset multiplies terms that then
                        cancel, so the error is bounded by the products and not
                        by the answer. The offset used is 305.7
the matrix symmetry     EXACTLY 0.0, on every fixture and on all eight reference
                        bodies. Asserted as == 0.0, because Gamma_f read from
                        the owner and from the neighbour is the SAME double:
                        d' . n' = (-d) . (-n) with both negations exact in
                        IEEE-754, and faceArea one stored number. A tolerance
                        there would assert less than the truth
```

## ADVERSARIAL REVIEW

Three passes: the author's own, written down before either independent review
reported so that what they confirmed and what they added stay distinguishable,
and two independent agents given the code and the ADR but **not** the author's
conclusions — one adversarial, one test-gap. Both were told to try to disprove
the milestone, and both were read-only, so every measurement claim they made was
re-checked here before it was acted on.

**Both reviews converged on the same headline, independently: the matrix was
well tested and the SOURCE VECTOR was not.** The adversarial review put it as
"six distinct sign/index mutations survive all 34 cases"; the test-gap review
ranked the same gap first and second. That agreement is the strongest signal
either produced, and it was right.

### What the author's own pass had already found and fixed

Recorded as the author's rather than presented as review findings. All four were
caught by reading a dependency's contract, before any test ran.

```text
the null-mode flag was computed from FACE COUNTS, which is wrong for a symmetry
  face whose normal is perpendicular to the component's axis: it adds
  Gamma n_i^2 with n_i == 0, touches no diagonal, and leaves the constant mode
  intact while the count said it had been lifted
the FixedValue arm defaulted an absent velocity to 0.0, fabricating a no-slip
  wall where the intent was unknown
the boundary gate compared the source's STAMP unconditionally -- and
  bindCfdBoundary deliberately returns a DEFAULT source, so that would have
  refused every seam-built boundary and made both analytic fixtures unreachable
cfdSparseProblem was noexcept while the shared path formatted a message
```

Both reviews then found that the FIRST of those **had no regression test**, and
that it could not have one on a uniform boundary — a closed surface's normals
cannot all be perpendicular to one axis — so the fix was correct and unproven
until a mixed boundary existed. CLAUDE.md: "A bug fix is incomplete without a
regression test, unless impossible." Here it was possible.

### The findings that mattered, and what each was

**The source vector was covered only by `== 0.0` and `> 0.0`.** No test read a
numeric value out of it. Two structural reasons: every fixture used
`NoSlipWall` (prescribed value zero), `PressureOutlet` (zero-gradient, no
source) or `SlipWall`, so the Dirichlet arm only ever ran with `prescribed == 0`
— and `MovingWall` and `Velocity3` were imported and never constructed, in a
file containing a test *named*
`ANoSlipWallPrescribesZeroAndAMovingWallDoesNot` that built only the still wall.
And every slip-wall case passed an all-zero current velocity, so every cross
term was identically zero. The surviving mutations the adversarial review
enumerated include the component selector, the `gamma` factor, the Dirichlet
sign, the component-blocked index `j * cells + owner`, the `j == componentIndex`
skip, and — the sharpest — **the sign of the entire non-orthogonal correction**,
the term ADR-063 F1 calls mandatory. Closed by four new cases that assert values
against independent oracles: a moving wall with three distinct non-zero
tangential components, a per-cell-per-component current velocity, the
correction's value and opposite signs on the two sides of an internal face, and
a face-VARYING gradient so the face index is observable.

**ADR-069 Decision 7 described code that did not exist.** It said the
interpolation weight "is named and implemented now, with the single call site a
variable viscosity will enter through", and that a test "asserts it over the
full range of `w_f`". A grep of the whole module found no weight, no call site
and no test. Both reviews found it independently and both ranked it first or
near it. Since that Decision is the evidence for the checklist's "Define face
interpolation", it is implemented rather than reworded:
`cfdFaceInterpolationWeight` computed from `ownerToNeighbour` and `cellToFace`
alone, `cfdFaceViscosity` as the single call site, and a test over every face of
all eight reference bodies asserting the weight is in `[0, 1]`, recording the
range it spans, and asserting `mu_f == mu` at every one.

**The ADR's own symmetry argument described arithmetic the implementation does
not perform — and this is a correction to a claim the author had also made to
the owner.** Decision 4 argued bit-exact symmetry from the identity
`d' . n' = (-d) . (-n) = d . n`, measured over 20,000 random configurations at
worst difference exactly 0.0. The identity is true. But the assembly computes
`gamma` ONCE per face and scatters four entries in one visit, so
`largestSymmetryError() == 0.0` is a property of the scatter: it cannot fail and
cannot detect a wrong coefficient, and `kCfdDiffusionSymmetryTolerance` can never
fire. The honest claim is simpler and stronger — one visit writes both entries,
so the matrix is symmetric by construction — and the coefficient's evidence is
the one-dimensional oracle and the skew comparison instead.

**A test that could not fail, in the place it mattered most.**
`OnASkewFaceTheCoefficientExceedsTheOneDimensionalOneByOneOverCosine` compared
`gammaOracle` against a one-dimensional oracle — two test-side quantities — so it
reduced algebraically to the fixture premise `cos theta = 3/5` and **an operator
dividing by `|d|` would have passed it unchanged**, which is precisely what its
own comment said it ruled out. Both reviews found it. It now reads the
off-diagonal out of a real assembly.

**Neither caller-supplied array was checked for finiteness, and nothing checks
the source.** A single NaN in `faceGradient` or `currentVelocity` flowed into
`source` while the matrix stayed provably finite, which made
`CfdSparseMatrix`'s "possession is the evidence" guarantee read as though it
covered the whole system when it covers only the coefficients. Two refusals
added, in the existing phase order, each with length-before-value ordering
asserted and a finite extreme shown to be ACCEPTED.

**A tag that nothing emitted.** `AssembledMatrixInconsistent` was documented as
the tag a `fromCsr` refusal would carry; `Result<CfdDiffusionSystem>` has no
channel for a tag, so the assembly returned a plain `Internal` error and the
enumerator was dead. In the milestone that cites P19-BC-001's lesson about
exactly that, a tag no path produces and no test can read is a claim rather than
a guarantee. Removed, with the header now stating that the assembly's internal
inconsistencies are untagged by design because they are unreachable.

**Two copies of the split arithmetic, inside the file whose header warns against
exactly that.** `CfdDiffusion.hpp` justifies exposing `CfdFaceDecomposition` on
the grounds that "computing it again outside would be a second copy of the
arithmetic that could disagree with the assembly's" — and `armsOf` and
`cfdFaceDecomposition` held the same expressions twice, with two different
definitions of "internal" between them. `armsOf` is now a thin conversion of the
public accessor, so there is one copy and one definition.

**The reported deferred fraction overstated what was deferred.** It was maxed
over every face including `ZeroGradient` ones, whose diffusive flux is zero and
which defer nothing, while the header said "over the faces assembled". Now
restricted to the faces that contribute a coefficient, with a test showing a
zero-gradient boundary reports exactly zero on a mesh whose faces are skew.

**A tolerance tied to the fixture's units.** `WithinAbs(0.0, 1e-18)` on the
deferred arm's norm is an absolute bound on a quantity in square metres: the same
geometry expressed in metres would make a correct operator fail it. Now relative
to the face's own area.

**And the patch test's normalization was not load-bearing.** The adversarial
review measured that at an offset of 305.7 a plain `WithinRel(1e-13)` would pass
too, so the eps-units form the comment made so much of was not actually doing
anything; it also showed that `products` omits the `mu |g| |E_f|` term that
dominates near 90 degrees, so the formula was not a general bound. The offset is
raised to 1e6 and the term added — the bound now holds for the reason stated.

### Mutation verification

Twenty mutations, each applied alone to a clean tree, built, and required to be
killed by its named test; then the tree restored, rebuilt, and verified at zero
markers. The harness copies without metadata and stamps a fresh mtime on every
write, because P19-IC-001's first sweep reported a test failing on a tree with no
mutation in it — `shutil.copy2` preserved mtimes, ninja skipped the rebuild, and
a stale binary produced a failure that was not there.

**The point of this sweep is narrower than "the tests have teeth". The
adversarial review PREDICTED that eight specific source-vector mutations would
survive the suite as it stood, and the fix pass claimed to close them. A
prediction and a measurement is the only way to know whether it did.**

```text
first sweep    15 KILLED, 2 SURVIVED, 3 VOID, of 20
second sweep    4 KILLED, 1 SURVIVED, 0 VOID, of the five the first could not
                judge
third sweep     1 KILLED, after the one real gap was closed -- and attributed
                to exactly the one case whose name claims it, with nothing else
                in the group breaking
                ------------------------------------------------------------
                20 of 20 killed
```

**THE SWEEP FOUND ONE REAL GAP, AND IT IS THE MOST USEFUL THING IT DID.** Both
reviews had flagged the correction's face index as a concern; the test written to
close it could not. `AFaceVaryingGradientIsReadAtItsOwnFace` used the analytic
pair with every boundary face `ZeroGradient`, so the ONLY face contributing a
correction was the single internal one -- and `isInternal(face)` is
`face.value() < internalFaces`, which numbers internal faces FIRST, so that face
is **index 0**. Reading the gradient at a hard-coded index 0 was therefore
indistinguishable from reading it at its own face: the mutation
`faceGradient[faceSlot * 0]` survived a test whose name claimed to pin exactly
that, and whose comment said a uniform gradient "could have read any index".

The fixture now uses a prescribed-value boundary, so all six boundary faces
contribute their corrections at indices 1 through 6, and the test asserts that
premise (`contributingAboveZero >= 6`) rather than relying on it. Without the
sweep this would have shipped as a test that read as evidence and was not --
which is the failure P19-IC-001's review found in a block that called itself
"THE DISCRIMINATING PART".

All eight of the predicted survivors are killed -- the eighth only after the
test written to catch it was itself repaired, which is the sweep's own finding:

```text
KILLED   SWAP the correction's sign between owner and neighbour
KILLED   NEGATE the deferred arm: the whole correction inverted
KILLED   drop the gamma factor from the Dirichlet term
KILLED   always take the X component of the prescribed velocity
KILLED   read the current velocity cell-blocked instead of component-blocked
KILLED   flip the slip wall's cross-term sign
KILLED   include the implicit term in the cross sum as well
KILLED   read the gradient at a fixed face index          (third sweep, after
         the test written to catch it was itself repaired)
```

And the rest of the first sweep:

```text
KILLED   divide by |d| instead of d.n -- the orthogonal-correction coefficient
KILLED   flip the off-diagonal sign
KILLED   regularise the diagonal with 1e-12
KILLED   collapse Symmetry onto FixedValue, the error ADR-067 warns of
KILLED   accept an unusable viscosity
KILLED   accept a NaN in the current velocity
KILLED   accept a NaN in the face gradient
KILLED   stop requiring strictly ascending sparse columns
```

**THREE PROBES WERE VOID FOR A REASON THIS PROJECT HAD ALREADY WRITTEN DOWN, and
they are reported as void rather than as survivors.** P18-ELEM-001's review
recorded it as finding F3: "Five probes survived only because they did not
compile. Leaving a name unused trips `-Werror`, which is a VOID probe, not a
survivor. Reformulate so the mutated code still uses every name." All three
failed with `unused variable ... [-Werror=unused-variable]`, reproduced by hand
to be sure of the cause, and all three were reformulated to keep every name used
— `faceSlot * 0` instead of `0`, a trailing always-true term, an added term
identically zero. A sweep that counted them as survivors would have claimed
three gaps that do not exist; one that counted them as kills would have claimed
three verifications that never ran.

**ONE WAS MIS-PAIRED, WHICH IS THE AUTHOR'S ERROR AND NOT A GAP.** The mutation
`values[*otherDiagonal] += 1.5 * gamma` was paired with the bit-exact symmetry
test, and a **diagonal cannot be asymmetric** — `|A[i][j] - A[j][i]|` is blind to
it by construction. So the pairing asked a question that test cannot answer, and
"SURVIVED" said nothing about the suite. Re-paired with
`APureNeumannRowSumsToZeroBitExactly`, which is what a wrong diagonal actually
breaks: the row then sums to `0.5 gamma` instead of zero.

That is also, incidentally, the sharpest confirmation of the review's finding
that the symmetry test is a tautology of the scatter. It cannot see a wrong
coefficient *or* a wrong diagonal; what it can see is a scatter that writes one
side and not the other.

**ONE WAS A NO-OP.** The mutation meant to restore the earlier
deferred-fraction behaviour added a second `std::max` inside the `noteDeferred`
lambda — which is idempotent, so the mutant was identical to the original.
Reformulated to call the lambda from the per-face prologue, which is what the
operator actually did before the fix.

### What both reviews looked for and did not find

The adversarial review re-derived the mathematics independently and reproduced
every closed form and all 21 figures of the measured theta table, and reported
the arithmetic, the signs, the symmetry arm's derivation and index arithmetic,
determinism, partial state and the architectural boundaries as clean. Its words
on the one that matters most: it found **no input for which the assembled
coefficient is wrong**. The test-gap review reported the compile-fail group
(all 22 cases, each failing for its registered diagnostic, with a control that
compiles clean) and the guard block as clean, case by case.

```text
the arithmetic        E_f = (A/(d.n))d IS the over-relaxed arm; T_f . S_f = 0 is
                      an identity; |T|/|E| = sin(theta) and |T|/|S| = tan(theta)
the signs             worked by hand on both sides of an internal face and at a
                      boundary face, including the correction's winding
the symmetry arm      derived from U_b = U_P - (U_P.n)n; both signs right, the
                      n_i == 0 case correctly inert, the component-blocked index
                      correct
determinism           no unordered container, ascending face then column order,
                      no clock, no seed, no statics
partial state         every failure returns before the system is constructed
layering              cfd (42) includes core and cfd only; no linalg linked, and
                      the amended CMakeLists clause is accurate
```

### The one disagreement, and how it was settled

The test-gap review read the `[[maybe_unused]]` rationale in the compile-fail
file as wrong, on the grounds that in the control configuration the helpers "do
not exist" rather than being unused. That is correct about the control and
beside the point about the case builds, where the attribute is what keeps
`-Werror` quiet. Left as it is, and recorded here rather than silently kept.

### What is recorded rather than fixed

```text
the boundary-currency arm   reachable only from a boundary whose source is not
                            default, which no public path produces for a
                            seam-built value. Recorded as untested, not claimed
a near-90-degree face       `products` now includes the term that dominates
                            there, but no fixture reaches it: the bound is
                            correct and the case is not exercised
the predictor tautology     cfdDiffusionProblem, cfdDiffusionFailure and the
                            assembly share one decision path, so their agreement
                            cannot fail. Kept because it pins the MESSAGE
                            equality, and the per-case tag assertions elsewhere
                            are what guard against a swapped tag
```

## FAILURE PATHS

Nine refusals, declared in phase order, each asserted **by tag** rather than by
message — the lesson P19-BC-001's review recorded after finding eight refusals
whose tags could be swapped with every message assertion still passing.

```text
GeometryDescribesAnotherTopology   the metrics and the topology disagree
BoundaryDescribesAnotherTopology   two arms: the STRUCTURAL face count always,
                                   and the currency claim only when the
                                   boundary makes one
ViscosityNotUsable                 not finite, or not strictly positive
SymmetryNeedsTheCurrentVelocity    a slip wall is present and no field was given
CurrentVelocityLengthWrong         not 3 * cells, the component-blocked length
CurrentVelocityNotFinite           added after review
CorrectionLengthWrong              not one gradient per face
CorrectionNotFinite                added after review
VelocityPolicyIsAnOutcome          UNREACHABLE for U, and recorded as such
```

Phase order is tested and not merely commented: two faults are planted at once
and the earlier one must win, then the earlier is fixed and the later must
appear — so the second fault is shown to have been present and masked rather
than absent. Three pairs are checked this way, including length-before-value on
both caller-supplied arrays.

`VelocityPolicyIsAnOutcome` is **unreachable through the document path**, read
off ADR-067's frozen table rather than assumed: down the `U` column the only
values are `FixedValue`, `ZeroGradient` and `Symmetry`, and `Computed` appears
only in the `phi` column at the two pressure boundaries. It is given a named
refusal and recorded unreachable — the treatment P19-BC-001 gave
`DegenerateFacet`.

**`AssembledMatrixInconsistent` was declared and is now removed**, which is a
review finding rather than a tidy-up: nothing ever emitted it, because
`Result<CfdDiffusionSystem>` has no channel for a tag, and a tag no path
produces and no test can read is a claim rather than a guarantee. The assembly's
internal inconsistencies are untagged `Internal` errors by design and the header
says so; they are unreachable because the symbolic pass reserves every pair the
numeric pass can ask for.

Atomicity: the whole input gate runs before any geometry is read, the assembly
writes only locals, and every failure returns before `CfdDiffusionSystem` is
constructed. There are no out-parameters, so a refused assembly cannot leave
anything partly written.

## PERSISTENCE

Nothing here is persisted and nothing here is canonical. The matrix and the
source are derived values recomputed from the topology, the metrics, the
boundary and one viscosity; there is no CFD serializer in `src/io/json` at all,
and this milestone adds none. The compile-fail group carries the structural half
of that claim: a `CfdSparseMatrix` cannot be conjured, so an unvalidated matrix
does not exist, and a `CfdDiffusionSystem` holding one by value inherits the
guarantee.

## DETERMINISM

```text
the face order          CfdTopology's, ascending FaceIndex. Named as the owner
                        of the order, because changing it changes the last bits
                        of a sum
the column order        ascending within a row, which is also what makes
                        coeffSi a binary search
no unordered container  none in either file
two assemblies          bit-identical, through the matrix's own equality
```

## REGRESSION

Full qualification, three presets plus two repeat stages. The harness is
byte-identical to P19-IC-001's, making this the **fifty-ninth** milestone run on
an unchanged harness -- and it was **self-checked first**, because an unchanged
harness is not the same as a working one:

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
qualification finished Fri 11/06/2026 23:23:39.67, 0 stage(s) failed
```

**17 stages, every one exit 0**, total wall time 11627.88 s (3 h 13 m 48 s).
Every duration below is computed from the harness's own timestamps rather than
timed separately:

```text
preset             configure   clean    build   no-op rebuild      ctest
debug-ext               3.69    0.99  1213.93            0.41    2362.92
release-ext             7.93    0.45  1342.62            0.48    1898.45
debug-shared-ext        3.07    0.52  1018.48            0.34    1845.71
repeat release-ext                                                 920.52
repeat debug-ext                                                  1004.18
```

Counts, each reconciled against the figure written into `FREEZE.txt` **before**
the run:

```text
                        predicted   measured by ctest -N   the run
full set, per preset         6311                   6311   6311/6311, x3
repeat set, per preset       3124                   3124   3124/3124, x2
repeat markers per log      15620                     --   15620, x2
of which, new unit              49                     49   49, every log
of which, new compile-fail      22                     22   22, every full log
the rest                      6240                     --   6240, every log
```

`rest = 6240` is P19-IC-001's own full-set figure, and `3124 - 49 = 3075` is its
repeat figure, so **no pre-existing test moved**. Across all five logs:
**0 Failed, 0 Skipped, 0 Not Run, 0 Disabled**. The figure that establishes it
is the Passed-marker total -- 6311, 6311, 6311, 15620, 15620 -- because that
says every selected test RAN, where "none failed" does not.

A SIXTH COUNTING TRAP, met while reading this run and recorded because the five
in `FREEZE.txt` are. A grep for `Skipped|Not Run|Disabled` returns 8 lines in
each full log and 40 in each repeat, which reads like a skip and is not: all of
them are TEST NAMES, among them
`unit.CfdBoundary_AFacetOutsideTheTopologyIsRefusedRatherThanSkipped` and
`unit.ThermalValidation_ASkippedRuleIsNeverAPass`. Four such tests, each name
printed twice per pass (Start, then Passed), so 4 x 2 = 8 and x 5 = 40, and
every one Passed. Same family as the rest: a number is not evidence until the
thing that produced it is known to be the thing under test.

Other gates read from the run rather than asserted:

```text
warnings                 0 lines matching warning or error in all three build
                         logs
-Werror                  BETTERCAD_WARNINGS_AS_ERRORS:BOOL=ON, read from each
                         BUILT tree's own CMakeCache.txt, not from a preset walk
no-op rebuilds           each recompiled nothing ("Checking git revision" only)
architecture             205 of 205 in every full log, architecture.layering
                         among them
repeat selection         the harness logged "selects 3124 test(s)" for each
                         repeat preset, so the P19-DOF-001 trap -- a filter that
                         matches nothing and exits 0 -- cannot have fired
qualified candidate      d46a2d1e9c1c3cc687f8b360143c0748110e8977 == HEAD
stderr                   run-qualification.err is NOT empty, and what is in it
                         matters: 270 lines, every one git's "CRLF will be
                         replaced by LF" notice from the two identity readings
                         (135 paths x 2). No compiler diagnostic is in it
```

**The qualified tree is the committed tree**, across SIX readings: the freeze
record, the harness's reading before the first build, its reading after the last
test run, a re-read from the live working tree by the harness's own method
(scratch `GIT_INDEX_FILE`, `read-tree HEAD`, `add -A`, `write-tree --prefix`),
the staged tree before the commit, and the commit object of `dc5e5a1` itself.
All six are identical:

```text
apps               fc398a3d2e524178cf38e8b667236379d8381086   unchanged
include            d57e2b32d22b00509aa70ffb548f45614f93a54e   moved
src                5c11820c79c8b09ded4cd9201216b96e42f2b74d   moved
tests              10fa15776a5e1d3828ccaeb3dc6fd811f204b3fc   moved
examples           4da234ebf4779f7c16d5df0075f106f7e3d4c958   unchanged
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7   unchanged
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd   unchanged
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e   unchanged
```

Exactly three moved, and they are exactly the three this milestone edited. The
last two readings were taken at commit time, which is why `dc5e5a1` could not
appear in its own commit's copy of this file: that copy recorded four readings,
and this record pass raises it to six, as P19-IC-001's did.

The run was launched detached, as P19-IC-001's third attempt was, because that
milestone's second attempt had been killed by a client teardown at test 5589 of
6240. Here there was **one attempt**, and it is the one reported above.

## PERFORMANCE

Not measured, and not claimed. There is no baseline, because nothing in `cfd`
assembled a matrix before this milestone. The assembly is two passes over faces
with no allocation inside either loop, and the one measurement worth recording
is structural rather than temporal: `nonZeros == cells + 2 * internalFaces` on
all eight reference bodies, so the pattern is the connectivity and nothing is
stored twice.

## KNOWN LIMITATIONS

```text
L1  WITHOUT A CALLER-SUPPLIED GRADIENT THE OPERATOR IS A CORRECT LAPLACIAN ONLY
    ON AN ORTHOGONAL MESH, and no P16 reference mesh is one -- the best maximum
    is the Block's 64.09 degrees. ADR-063 F1 says the correction is "mandatory
    in the discretisation, not an option" and also reserves any threshold to
    P19-VALID-001, so this operator reports rather than refuses. The omission is
    measured per face and the worst fraction is in the summary. P19-GRAD-001
    closes it
L2  SKEWNESS IS NOT CORRECTED. ADR-065 measured it as high as 6.5 on
    RM-MESH-03, and a skewness correction needs the same missing gradient
L3  NO SOLVE AND NO MOMENTUM EQUATION. Nothing here is shown to produce a flow
    field: this is one operator of an equation that does not exist yet, and the
    matrix has never been handed to a solver
L4  THE BOUNDARY-CURRENCY ARM OF THE INPUT GATE IS UNTESTED. It fires only for
    a boundary whose source is not default, and `bindCfdBoundary` -- the only
    path a test can use for a hand-built mesh -- deliberately returns a default
    source. Recorded rather than claimed, after a review found no test reaches
    it
L5  THE DEFERRED FRACTION IS A MAXIMUM, AND A MAXIMUM HIDES A DISTRIBUTION.
    ADR-063 F1's own table reports mean as well as max, and a consumer deciding
    whether to trust an uncorrected assembly would want both. Adding the mean
    changes the summary's shape and is not clearly this milestone's to make
L6  THE PATCH TEST'S `products` BOUND IS CORRECT BUT UNEXERCISED NEAR 90
    DEGREES. The `mu |g| |E_f|` term that dominates there was added after a
    review showed the sum without it is not a general bound; no fixture reaches
    that angle, so the term is reasoned rather than measured
L7  THE INTERPOLATION WEIGHT'S EFFECT ON THE VISCOSITY IS UNOBSERVABLE BY
    CONSTRUCTION. For a constant `mu` every weight gives exactly `mu`, so only
    the weight's own value can be tested -- which it is, against an independent
    centroid reading, and against a hard-coded midpoint. The interpolation
    itself is exercised the day a variable viscosity exists, which ADR-058
    excludes from P19 v1
L8  THE PREDICTOR AND THE ASSEMBLY SHARE ONE DECISION PATH, so their agreement
    cannot fail. Kept because it pins the MESSAGE equality; what guards against
    a swapped tag is the per-case assertion of each tag against an independently
    named expectation
L9  NO PERFORMANCE RESULT. Nothing in cfd assembled a matrix before this
    milestone, so there is no baseline and none is claimed
```

## RESULT

```text
RESULT:   PASS
```

All fourteen checklist items are met. Four of them -- "Define orthogonal
diffusion", "Matrix coefficient sign convention", "Sparse assembly" and
"Symmetry where mathematically expected" -- are met by the operator and its
container, and the symmetry one is recorded for exactly what it is worth:
ADR-069 Decision 4 calls it STRUCTURAL rather than numerical, because `gamma` is
computed once and scattered to both sides, so `largestSymmetryError() == 0.0` is
a property of the scatter and not an independent confirmation of the
coefficient. What the suite relies on instead is the row sum and the analytic
coefficient, and the mutation sweep agrees: the symmetry assertion cannot see a
wrong coefficient or a wrong diagonal, only a scatter that writes one side.

"Define non-orthogonal correction policy" is met as a POLICY and a measurement,
not as a correction: the decomposition is chosen, derived, and pinned by a
fixture whose arithmetic is exact, the omission is measured per face, and the
worst fraction is in the summary -- while the gradient that would close it
belongs to P19-GRAD-001 and is not built here. ADR-063 F1 reserves any
accept-or-refuse threshold to P19-VALID-001 and the owner, so this operator
reports rather than refuses. That is stated again as L1 under KNOWN LIMITATIONS,
where it reads as a limitation rather than as a completion.

"Linear-field patch test" and "Independent 1D diffusion oracle" are met with
their bounds expressed in units of the products the sum accumulates, after a
review showed the earlier bound was not general. "Constant-field null
behaviour" is met in both directions, including the slip-wall case where the
constant mode survives because the wall normal has no component in the
direction being assembled -- which needed a one-cell fixture and a mixed
boundary, neither of which existed before this milestone.

"Boundary diffusion treatment" is met for the three policies that occur down the
`U` column, and the input gate's currency arm is recorded as UNTESTED (L4)
rather than claimed: it fires only for a boundary whose source is not default,
and `bindCfdBoundary` -- the only path a test has for a hand-built mesh --
deliberately returns a default one.

"Face interpolation", "Conservation across internal faces" and "Equal/opposite
internal contribution" are met and measured, the last against `mu g . T_f`
computed independently in the test rather than against the operator's own
arithmetic -- which is the defect a review found in the first version of that
case.

"Regression PASS" and "Evidence recorded" are the run above and this document.

## REVISION

```text
r1  Both independent reviews converged on one headline: the matrix was well
    tested from several directions and the SOURCE VECTOR was not. The
    adversarial review enumerated seven sign and index mutations it predicted
    would survive the suite -- including an inverted non-orthogonal correction,
    the term ADR-063 F1 calls mandatory -- and the test-gap review ranked the
    same gap first. They were right. Fifteen new cases close it, four of them
    asserting source values against independent oracles
r2  ADR-069 Decision 7 described code that did not exist: no interpolation
    weight, no call site, no test, in the Decision that IS the evidence for a
    checklist item. Both reviews found it independently. It is implemented
    rather than reworded
r3  AND THE FORMULA THAT DECISION GAVE WAS WRONG, which the new test then
    caught: |C_N - C_f| / |C_N - C_P| is not bounded by one on a skewed mesh and
    measured 3.61 on the Cylinder, so it is not a convex weight and does not
    define an interpolation. Replaced by the normal-projection form, which
    ADR-065 Decision 5 proves lies strictly inside (0, 1)
r4  AND THE CONSTANT-VISCOSITY IDENTITY WAS FALSE IN FLOATING POINT.
    `w mu + (1 - w) mu` is not exactly `mu`; the same test caught it, failing an
    exact comparison whose two sides printed identically. Written in the stable
    lerp form the difference is exactly zero
r5  The ADR's own bit-exact symmetry argument described arithmetic the
    implementation does not perform. `gamma` is computed ONCE per face and
    scattered four times, so the symmetry test is a tautology of the scatter: it
    cannot fail and cannot detect a wrong coefficient. Restated as structural,
    which is simpler and stronger, and the coefficient's evidence is the
    one-dimensional oracle and the skew comparison instead. This correction was
    also made to the owner, who had been told the stronger claim
r6  A test that could not fail, in the place it mattered most: the skew
    comparison had test-side oracles on both sides, so an operator dividing by
    |d| would have passed the very assertion whose comment said it ruled that
    out. It now reads the assembled matrix
r7  Neither caller-supplied array was checked for finiteness, and nothing
    validates the source, so a NaN reached a consumer while the matrix was
    provably finite. Two refusals added
r8  Four defects were found by the author before any test ran, by reading a
    dependency's contract: a null-mode flag computed from face counts, a
    fabricated zero wall velocity, a boundary gate that would have made both
    analytic fixtures unreachable, and a `noexcept` predicate that allocated.
    Both reviews then found that the FIRST had no regression test and could not
    have one on a uniform boundary; a mixed boundary now carries it
r9  An orthogonality guard written as `> 0.0` admitted rounding noise: on the
    tet's slanted facet `d` is parallel to `n` by symmetry, so the deferred arm
    is numerically zero and its direction is arbitrary -- it measured cos = 1
    against the area vector. The guard is now scale-relative, with the
    complement asserted so the branch split cannot drift
r10 Counting traps, all the author's own measurements rather than the code's: a
    verification regex that required eight leading spaces and so missed the last
    case of each compile-fail block, reporting 18 of 22; backslash levels
    halved through a shell heredoc, which would have written a CMake wildcard
    where a literal dot was meant; a `timeout` that killed a linker and reported
    `ld returned 143`; and an unquoted heredoc that let bash execute backticks
    inside a C++ comment. The guard generator was rebuilt to construct both
    backslashes from chr(92), immune to any escaping layer
```
