# P19-CONV-001 — Convective Flux

TASK            P19-CONV-001 — Convective Flux
ADR             ADR-072
AUTHORIZED      2026-11-07, the owner's "Implement nd fully verify" over the
                fifteen-item checklist, pasted into the instruction
RESULT          PASS -- qualified 2026-11-08 on debug-ext, release-ext and
                debug-shared-ext; 17 stages, 0 failed
EVIDENCE        this directory

---

## SCOPE

The fifteen items of `TODO.md`'s own `P19-CONV-001` section, unaltered.

The owner pasted the checklist into the instruction, as they did for
`P19-BC-001`, `P19-IC-001`, `P19-DIFF-001` and `P19-GRAD-001`. **The pasted
text was diffed against the file's own section before any work began** and is
byte-for-byte identical across all 32 lines — 15 unticked items, 0 ticked, both
counted rather than eyeballed. So the scope is this repository's checklist and
not a restatement of it, and the question of whether the owner altered anything
is answered by a diff rather than by reading.

The checklist's closing note — "Recommended first qualified scheme:
`first-order upwind`, before higher-order schemes" — is read as part of the
scope and as binding. `ADR-072` Decision 4 records why `Central` is built too
and why boundedness is therefore reported per scheme.

Nothing after this milestone is authorized. `P19-MOM-001` is next in the phase
plan and was not started.

## BASELINE

| | at `8bc2c88` | after |
| --- | --- | --- |
| tests per preset | 6449 | 6525 |
| repeat set per preset | 3210 | 3258 |
| ticked boxes in `TODO.md` | 648 | 663 |
| ADRs | 071 | 072 |

`6525 = 6449 + 48 + 28` and `3258 = 3210 + 48`, both reconciled against the
figures `FREEZE.txt` recorded in advance. `compile_fail` is excluded from the
repeat set, as in every milestone, so the 28 new compile-fail cases raise the
total and not the repeat.

## ARCHITECTURE

`ADR-072`, twelve decisions. What matters most, and what it cost to get there:

**The operator assembles into infrastructure that already existed.**
`CfdSparseMatrix` is `P19-DIFF-001`'s validated CSR container; the boundary
table is `ADR-067`'s frozen one; the interpolation weight is
`cfdFaceInterpolationWeight`, exported by `ADR-069` Decision 7. So the honest
statement of novelty is small, and `CfdDivergence.hpp` had already written the
brief: "There is no convection scheme — `P19-CONV-001` defines `phi` and its
interpolation and is not authorized, so this operator consumes a `phi` somebody
else produced."

**`phi` gets a producer, which is what makes items 1 and 5 testable.**
`ADR-066` located `phi` and `ADR-071` fixed its sign and units, and then every
consumer invented its own. `cfdMassFlux` is the one place it is made.

**A wall's `phi` is the table's literal `0.0`, never a computed dot product**
(Decision 2). A `MovingWall` prescribes a tangential velocity, so `w . S_b` is
zero in exact arithmetic and *not* bit-zero once the centroid, the area vector
and the velocity have each been rounded — a computed wall flux leaks a tiny
mass flow through a solid boundary and surfaces as an imbalance somewhere else.

**The matrix is asymmetric, and that is correct, not slack** (Decision 3). No
`requiredSymmetryTolerance` is passed to `fromCsr` and there is no constant
mirroring `kCfdDiffusionSymmetryTolerance`. The suite asserts the *asymmetry*.

**The diagonal is positive, like diffusion's, which is why they add.** Diffusion
approximates `-div(mu grad)`; this approximates `+div(phi .)`. In the momentum
equation both sit on the left, so `A_conv + A_diff` is the operator and no sign
is flipped where they meet. Stated now because `P19-MOM-001` needs it and is
not authorized.

**The error channel is tagged** (Decision 10). `assembleCfdConvection` returns
`std::expected<CfdConvectionSystem, CfdConvectionFailure>` and not
`Result<CfdConvectionSystem>`, which is what makes the output-judging refusal
observable as a tag rather than grepped out of a sentence. `P19-DIV-001`
recorded the absence of exactly this as its limitation L10;
`CfdSparseMatrix::fromCsr` already had the precedent in the same module.

### The three decisions that were WRONG when written, and how they were found

This is the part of the architecture worth reading. Three published claims in
`ADR-072` were false, and all three were caught before the qualification freeze
— two by independent review, one by the measurement that review provoked.

**Decision 5's correction term was wrong, and the first draft of the experiment
hid it.** The row-sum identity needs the inflow that goes to the *source*
restored. The first form was `sum_boundary min(phi, 0)` — all inflow — which is
right only if every inflow face prescribes a value. A `PressureInlet` is the
counterexample: its `U` column is `ZeroGradient`, so the face value is the
owner's and that face's negative `phi` stays in the matrix. Measured against
mixed boundary policies the all-inflow form departs by a **relative 0.9995 on
20,815 of 139,802 cells** — a finite, structural error. The corrected
`prescribedInflow` term is exact to half an eps on the same meshes.

**Decision 8's transpose identity was false in two independent ways.**
`A(-phi) == A(phi)^T - diag(div phi)` was derived from the *upwind* scatter rule
— reversing a face's flux swaps the two cells' roles — and then stated
unqualified. A central share is geometric and does not swap, so the identity
fails by `phi(2w-1)` per face; and a `ZeroGradient` or `Symmetry` boundary face
carries its full `phi` on the diagonal, so reversing the sign flips that term
instead of removing it, failing by `phi_b` — which is every pressure inlet and
outlet. Re-measured:

```text
Upwind   FixedValue boundaries only    19.25% bit-exact, worst relative 4.36e-16
Upwind   ZeroGradient boundaries        0.00%             worst relative 1
Upwind   mixed                          4.10%             worst relative 1
Central  any policy                     0.00%             worst relative 2.1 to 2.7
```

A relative departure of 1 or 2 is a finite error, not rounding. The original
"general mesh ... worst relative 4.93e-16" overstated what had been measured:
that script had **no central branch at all** and modelled every boundary face
as `A[p][p] += max(q,0)`, which is the `FixedValue` rule. The measurement was
of a model narrower than the claim, which is a failure mode worth naming — it
is not a wrong number, it is a right number about the wrong thing.

**Decision 7's dominance identity was missing the same term Decision 5 had just
corrected**, one paragraph later in the neighbouring decision. The deficit is
`-div + prescribedInflow`, not `-div`. The first measurement ran with **no
boundary faces at all**, where the two forms coincide.

**And re-measuring found a third condition neither the review nor the ADR had
stated.** The deficit is defined with `|a_ii|`, so once backflow at a
zero-gradient boundary drives the diagonal negative the identity breaks
*whatever* the inflow term is — which is why both forms sit at 17.35% in the
zero-gradient column. The scope is `a_ii >= 0`, i.e. no backflow, and that makes
`CfdConvectionSummary::backflowFaces` the identity's **validity condition**
rather than a diagnostic standing beside it. That is a better understanding of
the operator than either the author or either reviewer had going in, and it came
from measuring a claim instead of defending it.

### What `ADR-072` rejected

A blended or deferred-correction scheme (a third scheme with its own
boundedness proof, arriving untested beside two that are). Refusing a `phi`
whose divergence exceeds a threshold (`ADR-063` finding F1 reserves a quality
threshold to the owner, and the threshold would be invented). A
`worstDominanceDeficit` summary member (a second name for
`-relativeImbalance`, computed differently, free to disagree at half an eps). A
`boundednessIsGuaranteed` flag — the operator cannot see whether `phi` is
conservative, so a flag named for that guarantee would read true for a wildly
non-conservative flux. Exporting `CfdDivergence.cpp`'s private helper, or
extracting a `CfdInterpolation.hpp` (a refactor of three qualified operators
combined with a feature).

## BLAST RADIUS

**Everything is additive except four build files.** No existing public API
changed, no existing behaviour changed, and no qualified operator's source was
edited — `ADR-072` Decision 9 rejected the two changes that would have done so.

| | file | what |
| --- | --- | --- |
| new | `include/bettercad/cfd/CfdConvection.hpp` | the operator, the producer |
| new | `src/cfd/CfdConvection.cpp` | the implementation |
| new | `tests/cfd/CfdConvectionTests.cpp` | 48 cases |
| new | `tests/compile_fail/CfdConvectionMisuse.cpp` | 28 cases |
| edit | `src/cfd/CMakeLists.txt` | one source line |
| edit | `tests/CMakeLists.txt` | one test-file line |
| edit | `tests/compile_fail/CMakeLists.txt` | the `cfdconv` group |
| new | `docs/architecture/decisions/ADR-072-*.md` | the ADR |

Read, not changed: `CfdTopology`, `CfdGeometry`, `CfdBoundary`, `CfdSparse`,
`CfdData`, `CfdDiffusion` (for the weight), `CfdDivergence` (for the
cross-check, in tests only).

The regression set is therefore the whole suite — to establish that *nothing
moved* — plus `architecture.layering`, which the new module files must not
violate. The three subtrees that moved at the freeze are exactly `include`,
`src` and `tests`.

**One consequence is worth stating because it is not local.** The new tests
consume `divergeCfdMassFlux` and `divergeCfdVelocity`, so a defect in
`P19-DIV-001` would now surface as a *convection* test failure. That is
deliberate: the cross-check is the only thing pinning the duplicated
face-velocity interpolation that Decision 9 declares.

## IMPLEMENTATION

`include/bettercad/cfd/CfdConvection.hpp` and `src/cfd/CfdConvection.cpp`.

Two public entry points and three predictors:

```text
cfdMassFlux(topology, geometry, boundary, {currentVelocity, density})
    -> expected<vector<double>, CfdConvectionFailure>      one per face, kg/s

assembleCfdConvection(topology, geometry, boundary, {faceFlux, scheme, component})
    -> expected<CfdConvectionSystem, CfdConvectionFailure> matrix, source, summary

cfdConvectionFailure / cfdConvectionProblem / cfdMassFluxProblem
```

The traversal is one pass over faces in `FaceIndex` order. Each internal face
computes an owner share once and writes `phi*s` and `phi*(1-s)` to the owner's
row and their exact negations to the neighbour's, in the same visit — so
internal-face conservation is a property of the traversal and not a tolerance.
The upwind share is `1` or `0`; the central share is
`cfdFaceInterpolationWeight`. A boundary face is treated by `ADR-072` Decision
6's rule whatever the scheme, because a boundary face has one cell and a
boundary condition, and because the weight returns `1.0` there — which fed to
the central branch would silently discard a velocity inlet's prescribed value.

**The gate has ten phases, and three of them exist because of the reviews.**
Phase 3 is the owner/neighbour range guard, phase 4 the component check, and
the conditional stamp comparison was restored in phase 2. Each phase is its own
walk over the boundary, which is what makes the declared enum order the check
order *between* phases; within one walk the first offending facet decides, and
no claim is made about the enum order there.

### The six code defects the reviews found

Every one was verified against the code before being accepted.

**F1 — no owner/neighbour range guard, and the unvalidated index is used for
WRITES.** `CellIndex{}` is `kInvalid`, which is `static_cast<size_t>(-1)`, and
`CfdTopology::owner` returns exactly that for a face it does not hold. In this
operator that index reached `pattern[owner].push_back(...)` and
`source[owner] -= ...`, which are writes into vectors of length `cells`, and
the producer's `velocityAt(cell)`, which reads three unchecked slots of the
caller's span. `geometry.describes` compares the mesh stamp and the counts,
never the owner and neighbour ranges, so nothing upstream established it.

This is the finding that matters most, because **`P19-DIV-001` carries this
guard and its comment records that an independent review forced it there**,
after finding the guard sitting *after* the read it exists to prevent. This
operator had no guard at all. That is a correction regressed one milestone
later, found again by a review, and it is the strongest single argument in this
phase for running the review before the gate rather than after.

**F4 — the conditional stamp check was dropped rather than made conditional.**
`P19-DIFF-001` found that comparing a boundary's mesh stamp unconditionally
refuses every seam-built boundary, because `bindCfdBoundary` returns a
default-constructed source. Its correction was to make the comparison
*conditional*. This operator's first draft dropped it entirely and was
therefore strictly weaker than the operator it cited as precedent: a boundary
prepared against a different mesh with the same boundary-face count was
silently accepted, and `cfdMassFlux` would then read that boundary's kinds and
prescribed velocities and return a successful `phi` built from the wrong
conditions. Restored to diffusion's form.

**F5 — `ResultIsNotFinite` was a catch-all for three faults that are not
non-finiteness**: a symbolic-pass slot miss, any `fromCsr` refusal (including
`ColumnsNotStrictlyAscending`, where a reader would be told a coefficient is
NaN), and a missing neighbour tagged as a *boundary* problem. That is
`CfdBoundary.hpp`'s own "a message is not a tag" rule inverted — the message
becomes the only correct part. Split out as `AssemblyIsInconsistent`, whose
message names which of the three and, for `fromCsr`, the `CfdSparseProblem` as
well.

**F6 — the three reported flow totals escaped the finiteness gate.** The scan
covered `values` and `source` and then published `inflow`, `outflow` and
`prescribedInflow` unchecked, so an assembly could succeed with
`outflow == +inf` while every coefficient was finite — exactly the hole
`ResultIsNotFinite` is documented to close, left open for the reported
quantities. `CfdDivergence.cpp` checks its own totals. Now inside the gate, and
there is a test that reaches it.

**F9 — `input.component` was never validated** and indexes a three-element
array. `VectorComponent` has a fixed underlying type, so
`static_cast<VectorComponent>(7)` is well-formed and `prescribed[7]` is
undefined behaviour. Now `ComponentOutOfRange`, which is **reachable** and
tested.

**F11 — the producer's `kind` switch had no guard.** `value` starts at `0.0`
and the switch has no `default`, so a seventh `CfdBoundaryConditionKind` would
silently receive a *wall's exact zero* — the inverse of the leak Decision 2
spends a page preventing, surfacing the way that decision says a wall leak
surfaces: as an imbalance somewhere else. Now a
`static_assert(kCfdBoundaryConditionKinds.size() == 6)` inside the switch, so
adding a kind is a build failure here.

### Two findings recorded rather than fixed

**F10 — phase 2's coverage walk cannot fire.** `treatment` returns an empty
optional only for an index the boundary does not hold, and the count equality
above it already puts every index in range. An earlier comment presented it as
a second independent check ("by face count AND by coverage"). It is kept as a
defence against that accessor's contract moving, and the comment now says it is
unreachable. CLAUDE.md names this class of check directly, and the resolution
is the one the module already uses: keep it, and never count it as evidence.

**F12 — a non-zero `phi` at a wall is accepted on the assembly path.** The
reviewer's argument is good: `ADR-067` gives all three walls `FixedValue 0`, so
refusing a non-zero `phi` there would need no invented threshold, unlike the
divergence-based refusal the ADR rejects. It is **not** changed, because
`ADR-071` Decision 5's principle is that "a policy says where a value comes
from, not whether a sum may be taken over it", and `divergeCfdMassFlux` accepts
the same input for the same reason. Changing it late in the milestone would also
break the row-sum identity's own test, which deliberately feeds a non-zero
`phi` at a slip wall. Recorded as L6.

## TESTS

`tests/cfd/CfdConvectionTests.cpp` — 48 cases.
`tests/compile_fail/CfdConvectionMisuse.cpp` — 28 cases.

### The fixtures, and why each exists

```text
the two-cell pair     the only fixture with an internal face whose two
                      contributions can be read separately
the one-cell tet      four boundary facets and no internal face: the only
                      fixture where the boundary treatment is the whole answer
the analytic pair     the owner weight is exactly 2/3 -- which is both why it
                      can pin a coefficient in closed form AND why it cannot
                      detect a constant weight
the tet chain         N cells in a line, built for this milestone: the 1D
                      advection fixture, and the only one with an interior
                      cell whose divergence is BIT-zero
the reference bodies  the row-sum identity, the producer-versus-divergence
                      cross-check, and the interpolation weight where it
                      genuinely varies
```

The fixtures are a fourth copy of `buildMesh`, `struct Hand` and their
neighbours, which also exist in the diffusion, gradient and divergence suites.
Extracting them would edit three qualified suites to serve a fourth, which the
working order forbids. **What must not be duplicated is an oracle, and none
is.**

### The weight oracle, which is the one that was wrong last time

Production computes `(orthogonalDistance - cellToFace . n) / orthogonalDistance`
from `cellToFace`, `orthogonalDistance` and `faceUnitNormal`. The oracle here
computes `((C_N - C_f) . n) / ((C_N - C_P) . n)` from `cellCentroid`,
`faceCentroid` and a normal taken by **normalising `faceAreaVector`** — three
different accessors, none of which production reads on this path. It is
therefore independent of production *and* different from
`CfdDivergenceTests.cpp`'s own oracle, which shares `faceUnitNormal` with
production.

**But independence of the oracle was the wrong half of the fix, and an
independent review is what established that.** See INDEPENDENT VALIDATION.

### The eight test defects the review found

**B1 — the interpolation weight was completely unpinned.** `uniformVelocity`
was the suite's only velocity builder, and with `U_P == U_N` the face value
`U_N + w (U_P - U_N)` is **independent of `w` for every weight**. So
`cfdFaceInterpolationWeight` was unpinned on the producer path, `w -> 0.5`
survived, an owner/neighbour transposition survived, and — worst —
`ADR-072` Decision 9's *designated* cross-check, the one the ADR says protects
the duplicated interpolation, was a tautology. This is `P19-GRAD-001`'s r5 and
`P19-DIV-001`'s own review finding, in the same place, one milestone later.
Closed by two tests: two cells with *different* velocities on the analytic
pair, and a *linear* velocity field on every reference body with the fixture
asserted to present weights that are mostly not 2/3.

**B2 — `Central` was never assembled against a `FixedValue` inflow boundary**,
which is the probe `ADR-072` Decision 6 names in as many words. Every `Central`
assembly used walls (`phi == 0`) or outlets (`ZeroGradient`). The probe is now
run.

**B3 — no `FixedValue` boundary face ever carried a positive `phi`**, so
"outflow beats prescription" was untested and reordering the sign test and the
policy test survived.

**B4 — the transported component was never varied from X.** `componentIndex`
appears once; `prescribed[componentIndex] -> prescribed[0]` survived. Now
looped over all three, with the three sources asserted pairwise different so a
constant index cannot pass by symmetry.

**B5 — `ResultIsNotFinite` was never reached**, though it is reachable and its
observability is Decision 10's whole argument for the tagged return type. Now
reached on three paths: an overflowing coefficient, an overflowing produced
flux, and an overflowing reported total.

**B6 — the three flow totals were read only where they are identically zero.**
Swapping `inflow` and `outflow` survived.

**B7 — the row-sum identity's prescribed term was identically zero in the only
test of that identity, and the test's comment denied it.** It read "a mixture:
prescribed inflow at some facets, zero-gradient at others, so BOTH terms are
exercised" while binding a boundary that makes every facet zero-gradient. So
the structural error Decision 5 exists to prevent was not reproducible by the
suite. Closed by a `bindPerFacet` fixture with half velocity inlets and half
pressure outlets, which asserts both terms are live before asserting the
identity.

**B8 — `TransportsAConstant` was an identity of `CfdSparseMatrix`, true of
every matrix.** It asserted `(A (k 1))[c] == k * rowSumSi(c)`, which is
linearity of `multiply` against `rowSumSi` and no mutation of the assembly
could fail it. Its fixture comment was also wrong. Now asserted against the
re-derived net outflow, with the fixture asserted to be non-zero.

Four further findings — `worstSkewness` tested only where one internal face
exists (so `max`, `min`, "first" and "last" all agree), the `PressureInlet` row
never value-checked, the `phi >= 0` boundary and signed zero unpinned, and
tolerances taken against a matrix-wide scale — are closed in the same pass.

## INDEPENDENT VALIDATION

Six references, none of which reads the code it checks.

**1. Closed-form flux at a velocity inlet.** `ADR-067` prescribes `U = -v n_hat`,
so `phi = rho (U . S) = -rho v |S|` exactly, with no interpolation anywhere —
the prescribed velocity is the value *at* the face. Asserted per facet to
`1e-12` relative, together with its sign, which is the convention's own test.

**2. The owner weight's closed form on the analytic pair.** `|d| = 5/4`,
`d . n = 3/4`, `cos(theta) = 3/5`, and the owner weight is exactly `2/3`.
Asserted against `2.0/3.0`.

**3. The re-derived weight on real meshes.** Over every meshable reference
body's internal faces, the produced `phi` is compared against
`rho (S_f . interpolate(U_P, U_N, w))` with `w` from the independent oracle and
a *linear* velocity field, so the weight is load-bearing at every face. The
fixture is asserted to present weights that are mostly **not** `2/3`, because
the hand fixture's weight is exactly that and a constant-weight mutation would
otherwise survive — which it did, before this test existed.

**4. The row-sum identity against `P19-DIV-001`, over an independent code
path.** `divergeCfdMassFlux` scatters per face; the row sum reads the assembled
matrix. In real arithmetic
`rowSumSi(c) + prescribedInflow(c) == divergeCfdMassFlux(phi).at(c)`, and the
per-cell prescribed term is **re-derived from `phi` and the boundary** rather
than read from the operator's summary — which is why the summary reports only
the mesh total.

**5. The producer against the velocity divergence, to density scaling.**
`div(cfdMassFlux(U, rho))` must equal `rho` times `div(U)` per cell. This is
the test `ADR-072` Decision 9 designates to pin the duplicated face-velocity
interpolation, and it now uses a linear field, because under a uniform field it
was a tautology.

**6. Hand arithmetic on the 1D chain.** With one uniform flux through every
internal face, every interior row reads `+q` on the diagonal and `-q` on its
upwind neighbour and nothing else — asserted with no tolerance at all. And
reversing every flux gives the exact transpose on interior rows, bit-for-bit,
which is where `ADR-072` Decision 8's identity actually holds.

### What validation deliberately does NOT claim

No analytic transported field. The exact steady solution of `div(phi x) = 0`
for a conservative flux and a uniform upstream value is a **constant**, and
that is asserted — as a residual, `A x = b` with `x` constant, so no linear
solve is needed and none of `P19-LINSOLVE-001`'s decisions are pre-empted.
Anything richer needs a solve, which this milestone does not have.

**A first draft of that test substituted forward along the chain and was
wrong.** With every facet a wall the chain is *closed* — no inlet, no outlet —
so a uniform internal flux is not conservative at the two end cells and the
last cell's diagonal is legitimately zero. The milestone's own Q3a measurement
had already recorded that the ends of an open chain are not conservative, and
the test was written as though it had not. The flux is now made conservative by
construction, with the construction asserted bit-exactly before the claim.

## ADVERSARIAL REVIEW

**Two independent reviews, run BEFORE the qualification freeze.** One against
the implementation, one against the suite, neither seeing the other or this
author's conclusions. 25 findings between them, every one verified against the
code before being accepted.

This is the single thing this milestone did differently from `P19-DIV-001`,
which reached a freeze, a launch and a PASS with no independent review and had
to throw away a three-hour qualification. The order in CLAUDE.md's lifecycle —
adversarial review *before* full regression and qualification — is there for
exactly this, and the cost of ignoring it was measured last milestone.

### The two findings both reviewers reached independently

Convergence from readings that could not see each other is the strongest signal
either produced:

* **the row-sum identity's prescribed-inflow term was never non-zero** in any
  test of that identity, while the test's own comment claimed it was. So the
  structural error Decision 5 exists to prevent — a relative `0.9995` on 15% of
  cells — was not reproducible by the suite.
* **the public header promised a refusal the code does not perform.** It
  described `assembleCfdDiffusion`'s stamp and cell-count comparison while
  `problemOf` compared only the boundary-face count, which made this operator
  strictly weaker than the one it cited as precedent.

### What the review changed about the published claims

Three claims in `ADR-072` were false when written. Two were caught by review
and the third by the measurement that review provoked:

* Decision 8's transpose identity, false for `Central` and for every
  zero-gradient boundary face, with a measurement that had no central branch
  and modelled every boundary as `FixedValue`.
* Decision 7's dominance identity, missing the term Decision 5 had corrected
  one paragraph earlier, with a measurement that had no boundary faces at all.
* Decisions 6 and the header both claimed "a compile-fail case holds the
  asymmetry" and nothing held it — nor could it, since the asymmetry is a
  runtime branch with no type-level expression.

Decision 7's re-measurement then found a third condition nobody had stated: the
deficit uses `|a_ii|`, so backflow breaks the identity whatever the inflow
term is, which makes `backflowFaces` the identity's validity condition.

### Where the reviews were wrong, which is worth recording too

One reviewer reported the compile-fail group as having "24 cases". It has 28,
established by a set difference between the `.cpp` and the CMake registration
rather than by a count — and that check itself first reported 24, because the
verifying regex was end-anchored and every `foreach` list's last entry carries
a trailing `)`. The cases were registered correctly; the checker was wrong. A
bare count of 24 against 28 would have looked like a real omission.

### The mutation sweep

Ten probes, each one a mutation a review said survived the first draft:

```text
weight-constant-two-thirds   KILLED   3176 failing assertions
weight-midpoint              KILLED      6
weight-transposed            KILLED      2
fixedvalue-before-sign       KILLED     13
boundary-through-scheme      KILLED      6
component-always-x           KILLED      5
drop-total-finiteness        KILLED      1
wall-computes-its-flux       KILLED     16
prescribed-inflow-sign       KILLED      6
neighbour-not-negated        KILLED     27

10 probes | killed BY THE SUITE 10 | killed by the compiler 0 | survived 0
```

**The first run of that sweep reported "8 of 8 killed" and four of those were
killed by `-Werror`, not by the suite.** A mutation that leaves a variable
unused does not compile, and a compile failure says nothing about the tests.
The probes were corrected to carry a `(void)` cast, and the sweep's verdict now
distinguishes `KILLED` from `KILLED(compile)` so the two can never be conflated
again. That is the same family of defect as a hand-typed count standing beside
a computed one, which `P19-DIV-001` recorded in its own tooling.

Note what `weight-constant-two-thirds` cost: 3176 failing assertions, all of
them in the reference-body test that did not exist before the review. On the
hand fixtures alone it is still invisible, because their weight *is* 2/3.

### The author's own pass

Recorded for what it is worth, which is less than the reviews. The author's
pass before the reviews found: the per-face decomposition's exactness, the
withdrawn `4.34e+13` figure, the all-inflow correction, the per-scheme row-sum
difference, and the `Symmetry`-as-zero-gradient divergence from `ADR-071`
Decision 7. It did **not** find any of the six code defects, and it wrote three
of the false claims. The reviews are not a formality here; they found what the
author could not.

## FAILURE PATHS

Twelve enumerators, in phase order. What is reachable and what is not:

| problem | reachable | tested |
| --- | --- | --- |
| `GeometryDescribesAnotherTopology` | yes | yes |
| `BoundaryDescribesAnotherTopology` | yes | yes (count mismatch) |
| `TopologyNamesAnUnknownCell` | no | UNREACHED, recorded |
| `ComponentOutOfRange` | **yes** | yes |
| `DensityNotUsable` | yes | yes (0, negative, NaN, inf) |
| `ValueLengthWrong` | yes | yes, both shapes |
| `ValueNotFinite` | yes | yes |
| `PrescribedValueMissing` | no | UNREACHED, recorded |
| `PolicyIsAnOutcome` | no | UNREACHED, recorded |
| `FaceWeightUnavailable` | no | UNREACHED, recorded |
| `ResultIsNotFinite` | **yes** | yes, three paths |
| `AssemblyIsInconsistent` | no | UNREACHED, recorded |

Five are unreachable from any constructible input, and each is kept because the
alternative is a wrong answer or an incomplete switch — the treatment
`P19-BC-001` gave `DegenerateFacet`. They are recorded as **unreached** rather
than claimed covered. An earlier draft of the suite's header named only two of
them and omitted `ResultIsNotFinite` entirely, which is reachable; a review
found both errors.

The phase order is asserted between the phases a constructible input can reach,
including that the length is checked before the values and the density before
the velocity's length.

## PERSISTENCE

Nothing here is persistent. The convection system is a derived value computed
from a topology, a geometry, a prepared boundary and a caller's arrays;
`ADR-068`'s rule that a derived array is not canonical state applies, so `phi`
is returned and not stored. No file format changed and no serialization test
was needed.

## DETERMINISM

The assembly walks faces in `FaceIndex` order and writes each row's columns in
ascending order, which `CfdSparseMatrix` requires anyway. No set, no map
iteration, no parallelism, no accumulation that depends on anything but the
topology. `multiply` is already specified as a serial sum in row and ascending
column order.

Pinned two ways: a test asserts that two assemblies of the same input are
bitwise identical in matrix, source and summary, for both schemes; and the
qualification's repeat stage runs the determinism set five times per preset in
two presets under `--repeat until-fail:5`.

## REGRESSION

Three production presets, seventeen stages, every stage exit 0.

```text
started   Sat 2026-11-07 23:30:24.56
finished  Sun 2026-11-08 03:37:31.69      4 h 07 m 07 s wall clock
candidate 8bc2c88a0d5d805a0df501260539fd843952c4c7  (== HEAD)
verdict   "Qualification passed: every stage exited 0."
          0 stage(s) failed
```

| preset | configure | clean | build | no-op rebuild | ctest |
| --- | --- | --- | --- | --- | --- |
| `debug-ext` | 0 | 0 | 0 | 0 | 0 — 2949.1 s |
| `release-ext` | 0 | 0 | 0 | 0 | 0 — 2360.1 s |
| `debug-shared-ext` | 0 | 0 | 0 | 0 | 0 — 1991.6 s |
| repeat `release-ext` | | | | | 0 — 919.1 s |
| repeat `debug-ext` | | | | | 0 — 1038.4 s |

```text
tests per preset        6525   100% passed, no FAILED block, in all three
test executions        52155   = 6525 x 3  +  3258 x 5 x 2
architecture.layering  passed in all three presets
warnings                   0   BETTERCAD_WARNINGS_AS_ERRORS read as ON from
                               each BUILT tree's own CMakeCache.txt, not from
                               the source
```

**76 tests added, 0 removed**, established by a set difference against
`P19-DIV-001`'s own qualified log rather than by arithmetic on the totals:
36 `unit.CfdConvection*`, 12 `unit.CfdMassFlux*` and 28
`compile_fail.cfdconv*`. The producer's cases are named for the producer, which
is why the operator prefix alone counts 36 and not 48. That nothing was removed
is the half that matters for "nothing moved".

### The assertion counts, and what they establish

```text
the 48 new cases          34,681 assertions   IDENTICAL in debug-ext,
                                              release-ext and debug-shared-ext,
                                              so no assertion here is
                                              configuration-dependent
the whole Cfd subsystem  702,187 assertions in 447 cases
                       = 667,506 + 34,681    and  399 + 48   EXACTLY
```

The subsystem arithmetic is the check that matters: `P19-DIV-001` recorded
667,506 assertions in 399 cases for the same filter, and the totals now differ
from those by precisely this milestone's own contribution. So no pre-existing
cfd assertion was moved, weakened or removed — which a passing suite alone
would not have shown, because a test deleted and a test added net to nothing in
a pass/fail count.

Measured from the three qualified build trees after the run. Reading a built
binary changes no source file, so it does not void the freeze.

### The determinism gate

`ctest --preset <P> -j 8 -R "<filter>" --repeat until-fail:5`, in `release-ext`
and `debug-ext`. The filter selected **3258** tests in each, recorded by the
harness's zero-match guard at run time, and each selected test **executed five
times**: 16290 `Start` markers against 3258 result lines, a ratio of exactly
5.000 in both presets. `compile_fail` is excluded, and the logs confirm zero
mentions of it. Of the new cases, all 36 `unit.CfdConvection*` are in the
repeat set and each ran 180 times across the two presets.

## QUALIFICATION

**The qualified tree is the committed tree.** The eight frozen subtree
identities were recorded in `FREEZE.txt` before the run, read again by the
harness before the first build and after the last test run, and recomputed a
third time by this author immediately before the commit — all by `qualify.cmd`'s
own method (a scratch `GIT_INDEX_FILE`, `read-tree HEAD`, `add -A`,
`write-tree --prefix` per directory, `ls-files -s` for the two files), never by
`git rev-parse HEAD:<path>`, which would read the committed tree and miss every
uncommitted change. A **fourth** reading was then taken back out of the commit
object of `10ad63a`, where `git rev-parse HEAD:<path>` is the correct tool because
the tree in question is precisely the committed one. All four readings agree on
all eight paths. Exactly three
moved — `include`, `src`, `tests` — and they are exactly the three this
milestone edited.

`docs/verification/` and `TODO.md` are outside the frozen surface, so this
evidence directory and the `TODO.md` tick do not void the qualification.

### Two figures FREEZE.txt recorded wrongly, corrected against the run

`FREEZE.txt` exists so the evidence cannot quietly adopt whatever the run
produces. Here the run contradicted it twice, and in both cases the frozen
*figure* was right and the *label* I attached to it was wrong. Recorded rather
than silently reconciled.

**"repeat result lines per log 16290 = 3258 x 5."** The log carries 3258 result
lines — one per test — and 16290 `Start` markers, because `--repeat
until-fail:5` reports once per test and executes five times.
`P19-DIV-001`'s qualified log settles it independently: 3210 result lines and
16050 markers for 3210 selected tests, the same structure one milestone
earlier. So 16290 is the count of **executions**, which
`run-qualification.cmd`'s own comment calls "markers" — the harness had the
right word and I did not use it. The corrected reading is strictly stronger
evidence than the label it replaces, because the ratio of exactly 5.000 is what
proves the determinism gate actually repeated.

**"total tests 6525", read as a count of names.** A set of test *names* from the
`debug-ext` log holds 6365 entries, not 6525. The suite did run 6525 distinct
tests — 6525 result lines carrying 6525 distinct test *numbers*, none repeated
— and the 160-entry gap is entirely a defect in the reading: the pattern
captured a name as a run of non-space characters, and 58 Catch2 names contain
spaces, so the truncated forms collide. `P19-DIV-001`'s log shows the same gap,
which is what exposed it as the tool's rather than the run's.

That is the **third measurement-tool defect of this author's own making in this
phase**, after the end-anchored compile-fail registration checker and the four
`-Werror` kills read as test kills. All three are the same family: a tool that
reports a number about something adjacent to what it claims to measure. Each
was caught by comparing against an independent source rather than by reading
the number again.

## KNOWN LIMITATIONS

**L1 — nothing consumes this operator.** It is one term of an equation
`P19-MOM-001` and `P19-PRES-001` have not assembled, exactly as
`P19-GRAD-001`'s gradient and `P19-DIV-001`'s divergence are. No flow field is
produced and none is claimed. Wiring them together is a coupling step no
authorized checklist contains.

**L2 — there is no solve.** `CfdSparseMatrix` is a container; admitting a solve
kernel is `P19-LINSOLVE-001`'s decision. The one analytic transport claim is
asserted as a residual rather than by solving.

**L3 — boundedness is conditional and the condition is not enforced.** Upwind
is bounded given a discretely conservative `phi` and no backflow at a
zero-gradient boundary. The operator cannot see the first — that is
`divergeCfdMassFlux`'s measurement — and implements the second because
`ADR-067`'s table prescribes it. Both are reported, neither is refused.

**L4 — `Central` is unbounded and nothing corrects it.** It overshoots where
the cell Peclet number exceeds two. No limiter, no blending, no Peclet-based
switching.

**L5 — no Peclet number is reported.** It needs a viscosity this operator does
not take, and reporting one would couple convection to diffusion's input for a
diagnostic. It belongs where both meet.

**L6 — a non-zero `phi` at a wall is accepted on the assembly path.** `ADR-067`
gives all three walls `FixedValue 0`, so refusing a non-zero `phi` there would
need no invented threshold — an independent review made that argument and it is
a good one. It is not done, because `ADR-071` Decision 5's principle is that a
policy says where a value comes from, not whether a sum may be taken over it,
and `divergeCfdMassFlux` accepts the same input for the same reason. The
producer cannot emit such a `phi`; only a hand-made one can.

**L7 — the face-velocity interpolation exists twice.** The *weight* has exactly
one definition and this milestone added no copy. The three-line *application*
of it exists privately in `CfdDivergence.cpp` and in `CfdConvection.cpp`, and
they must agree. They are not unified, because unifying them means editing a
qualified operator for a convenience; the agreement is pinned by a test rather
than asserted by a comment. `ADR-072` Decision 9 states the true count, which
is the thing `ADR-071` Decision 6 got wrong.

**L8 — five of twelve refusals are unreachable** through the public API and are
recorded as unreached rather than covered. See FAILURE PATHS.

**L9 — the predictors share one decision path with the operators**, so their
agreement cannot fail. The limitation `P19-DIFF-001` recorded as its L8,
`P19-GRAD-001` as its L9 and `P19-DIV-001` as its own. They exist so a caller
can ask whether an input is usable without paying for the answer.

**L10 — phase 2's coverage walk cannot fire**, and is kept as a defence against
`PreparedCfdBoundary::treatment`'s contract moving. Recorded rather than
counted as evidence.

**L11 — the stale comment in the harness.** `run-qualification.cmd` says
"repeat set, per preset 3039", which was already wrong for `P19-DIV-001` (3210)
and is wrong here (3258). It is a comment and not a gate, and correcting it
would change the harness's hash and break the byte-identical property every
milestone since `P16-SIZE-001` relies on. The gate that matters is the
zero-match guard, which records the selected count at run time.

**L12 — there is no rigid-motion invariance test.** `CfdDivergenceTests.cpp`
has one; this suite does not. An earlier draft of this suite's header claimed
there was, in a sentence borrowed from that file, and an independent review
checked the sentence against the tests that exist. The claim is removed rather
than the test added, because the operator's mesh-orientation behaviour is
inherited from the geometry layer that is already qualified for it — but the
absence is a real gap and is recorded as one rather than argued away.

**L13 — no performance baseline.** Nothing consumes the operator, so a
benchmark for an operator with no caller would be a number with no meaning.
`CLAUDE.md`'s performance workflow begins with a baseline and a profile; there
is nothing to profile against.

## PERFORMANCE

None measured, and none claimed. See L13.


## RESULT

```text
TASK:            P19-CONV-001 -- Convective Flux
IMPLEMENTATION:  include/bettercad/cfd/CfdConvection.hpp
                 src/cfd/CfdConvection.cpp
                 a face mass-flux producer and an upwind / central convection
                 operator assembling into CfdSparseMatrix, with a twelve-phase
                 refusal gate and a tagged error channel
TESTS:           48 Catch2 cases + 28 compile-fail cases, all passing in
                 debug-ext, release-ext and debug-shared-ext
VALIDATION:      six independent references -- the closed-form velocity-inlet
                 flux, the owner weight's closed form, the re-derived weight
                 over every meshable reference body, the row-sum identity
                 against P19-DIV-001's independent code path, the producer
                 against the velocity divergence to density scaling, and hand
                 arithmetic on the 1D chain asserted with no tolerance
RESULT:          PASS
COMMIT:          10ad63a -- production code and evidence together
EVIDENCE:        this directory
                 qualification/run-qualification.out    the verdict sentence
                 qualification/qualification-times.txt  17 stages, 0 failed
                 qualification/ctest-*.log              6525 tests x 3 presets
                 qualification/ctest-repeat-*.log       3258 x 5 x 2 presets
                 reconciliation.txt                     every frozen figure
                 FREEZE.txt                             recorded in advance
                 experiments/                            all five revisions
TODO:            updated on PASS -- the fifteen P19-CONV-001 boxes
```

**What this PASS does and does not say.** It says the operator compiles clean
under warnings-as-errors in three configurations, that 6525 tests pass in each,
that 52155 test executions produced no failure, that the determinism set ran
five times per test in two presets, and that six independent references agree
with the operator's output. It does **not** say a flow field has been computed:
nothing consumes this operator, there is no solve, and `Central` is unbounded by
construction. See KNOWN LIMITATIONS, which is the honest reading of the scope.

The thing most worth carrying forward is not the PASS. It is that **two
independent reviews run before the freeze found six code defects, three false
published claims and eight test defects** — including a memory-safety guard
`P19-DIV-001` had already been forced to add and that had regressed, and an
interpolation weight left unpinned in the same place it was left unpinned in
`P19-GRAD-001` and `P19-DIV-001`. The author's own pass before those reviews
found none of the six code defects and wrote three of the false claims.
`P19-DIV-001` paid for discovering this ordering by throwing away a three-hour
qualification. This milestone paid nothing, because the reviews came first.
## REVISION

**r1** — the checklist was diffed against `TODO.md` before any work began,
rather than read. Byte-identical, 32 lines, 15 unticked, 0 ticked, all counted.

**r2** — the pre-implementation experiment's first revision reported a
"relative gap" of `4.34e+13`. **Withdrawn**: the oracle compared two different
quantities, because at an inflow boundary face the convective flux is a source
and never a coefficient. `ADR-072` Decision 5 exists because of it.

**r3** — the same script's Q4 printed "worst relative departure: 0" from a
variable assigned only inside the failure branch, so it meant "nothing exceeded
the threshold" and read as "always exactly equal". Corrected to record
unconditionally, which showed `5.92e-16`.

**r4** — the correction term was wrong one level deeper, found while writing the
header: it is the **prescribed** inflow, not all inflow, because a pressure
inlet's `U` column is `ZeroGradient` and its flux stays in the matrix. Measured
departure of the wrong form: relative `0.9995` on 20,815 of 139,802 cells.

**r5** — the row-sum claim is scheme-independent in real arithmetic and not
equally exact in floating point. Upwind's share is exactly 0 or 1 and is
bit-exact per face; central sums two rounded products and loses up to two eps.
Measured and stated per scheme.

**r6** — `Symmetry` is treated as zero-gradient here, which is **not**
`ADR-071` Decision 7's choice. Its reason — a symmetry plane's normal flux is
zero by definition — does not transfer, because convection transports a
tangential component across the face too. Stated as a divergence from the
sibling operator rather than left to be discovered.

**r7** — the implementation compiled clean on the first attempt with
warnings-as-errors on, and the suite's first run passed 31 of 33 cases. Both
failures were my own fixtures, and one was a genuine design lesson: the
forward-substitution test assumed a closed chain was conservative at its ends,
which the milestone's own Q3a measurement had already said it is not.

**r8** — two of the suite's new fixtures were degenerate on their first run,
and both are traps `P19-DIV-001` recorded. A `PressureInlet` test used a
velocity along one axis, and three of a tetrahedron's four facets lie in
coordinate planes, so two facets legitimately had zero flux. And a component
test's three sources were identically equal because
`source[c] = -rho v^2 (sum_f S_f)[c]` and closure makes `sum_f S_f` zero — "a
closed cell's flux total is zero for any constant velocity, so a test of which
velocity was used could not fail".

**r9** — **two independent reviews were run BEFORE the freeze**, which is the
one thing this milestone did differently from its predecessor. 25 findings, six
code defects, three false published claims, eight test defects. Every finding
was verified against the code before being accepted.

**r10** — the mutation sweep's first run reported "8 of 8 killed" with four
killed by `-Werror` rather than by the suite. Corrected, and the verdict now
distinguishes a compile kill from a test kill so they cannot be conflated.

**r11** — the compile-fail registration check first reported 4 of 28 cases
unregistered. They were registered; the **checker** was end-anchored and could
not match a `foreach` list's last entry, which carries a trailing `)`. Fifth
end-anchor miss of this phase, and the set difference is what exposed it — a
bare count would have read as a real omission.

**r12** — the first launch of the qualification **did not run at all**. The
launcher could not find its own script, printed "is not recognized", and the
surrounding task still reported exit code 0. `FREEZE.txt` had already recorded
that the launcher's exit code is not the verdict, and the no-op was caught by
looking for the output files rather than trusting the code. Relaunched by
absolute path, and the harness's own pre-build reading of the eight subtree
identities then matched `FREEZE.txt` exactly.

**r13** — the qualification's background task reported **exit code 0 while the
launcher had not run the harness at all** on the first attempt, and the second
attempt's task also reported exit code 0 — this time correctly. The code is the
same in both cases, which is precisely why `FREEZE.txt` records that the
verdict is `run-qualification.out`'s own sentence plus the failed-stage count.
Both were read, and both say PASS.

**r14** — **two figures in `FREEZE.txt` were contradicted by the run**, and in
both the frozen number was right while the label was wrong: 16290 counts
repeat *executions*, not result lines, and 6525 counts tests, not distinct
*names*. `FREEZE.txt` has deliberately **not** been edited to match — a freeze
record that is retro-fitted to the run is worth nothing, and documentation
follows evidence. The corrections are stated in QUALIFICATION, with
`P19-DIV-001`'s own qualified logs as the independent arbiter in both cases.

**r15** — the reconciliation was run as a script against figures recorded in
advance, not read off the logs by eye, and it **failed on its first run** with
five reported mismatches. All five were defects in the script. Recorded because
a reconciler that passes first time has usually not been tested, and because
this is the third tool-defect of this phase in the same family — see
QUALIFICATION's closing paragraph.
