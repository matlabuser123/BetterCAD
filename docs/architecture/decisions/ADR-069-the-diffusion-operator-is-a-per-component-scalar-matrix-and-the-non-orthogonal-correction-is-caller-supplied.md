# ADR-069 — The diffusion operator is a per-component scalar matrix, and the non-orthogonal correction is caller-supplied

```text
status      accepted
date        2026-11-06
milestone   P19-DIFF-001 — Diffusion Operator
supersedes  nothing
amends      src/cfd/CMakeLists.txt's standing comment, in ONE clause only:
            "There is no matrix in this milestone". Decision 3 says exactly
            what changes and what stays true. ADR-065's orthogonalDistance
            anticipated this discretisation; Decision 2 says so and follows it
```

## Context

`P19-DIFF-001` is authorized to implement the finite-volume diffusion operator
for constant viscosity, `div(mu grad U)`, and to verify it independently. It is
the first milestone in `P19` that assembles a **matrix**. Everything before it
produced state, a layout, metrics, boundary intent or a starting field; nothing
produced a coefficient.

Four prior decisions constrain it before any choice is made here.

**ADR-066 settles that the operator is per-component and scalar.** It says of
the segregated predictor, in as many words:

> The segregated momentum predictor assembles no row that couples `u` to `v`.
> What coupling exists — the pressure gradient source, a slip wall's tangential
> projection, the non-orthogonal correction — enters per component, on the
> right-hand side or the diagonal, one system at a time.

and, decisively for the index space:

> Component X's block is exactly the cell-centred scalar array, so
> `vectorIndex(cell, X).value() == cell.value()`, and the two conventions are
> one convention rather than two that have to be kept consistent.

So there is no `3N x 3N` block matrix to design. There is one `N x N` scalar
matrix, used once per component, and a pressure field can be handed to the same
assembly.

**ADR-065 already built the accessor this discretisation divides by.**
`orthogonalDistance(face)` is `d . n` — not `|d|` — and its own comment calls it
"the distance a diffusion coefficient divides by". Decision 2 below shows that
exactly one of the three standard decompositions divides by `d . n`, so the
geometry layer anticipated a choice this milestone now makes explicitly rather
than inheriting silently.

**ADR-063 Finding F1 is a measured constraint, not a caution.** On P16's own
reference meshes:

```text
model                   interior faces   theta max   theta mean
RM-MESH-01 Block                   6        64.09        47.56
RM-MESH-02 Cylinder              946        89.28        57.89
RM-MESH-03 PlateWithHole          172        84.07        56.70
RM-MESH-04 Tube                  1472        85.20        55.27
RM-MESH-05 ThinPlate               18        88.81        66.48
RM-MESH-07 LocalRefinement        528        75.58        31.14
```

> A non-orthogonal correction is mandatory in the discretisation, not an
> option.

**And ADR-063 F1 also forbids this milestone from setting a threshold:** "No
threshold is set in this milestone, and none is implied by these numbers." That
is `P19-VALID-001`'s and the owner's to set. So this operator may not refuse a
mesh for being non-orthogonal, and may not quietly pretend the correction is
negligible. The only honest position left is the one Decision 5 takes.

**And ADR-067 froze the per-field boundary treatment**, which decides what a
boundary face does to a diffusion row. Reading its table down the `U` column is
what Decision 6 rests on.

## Constraints

```text
cfd is layer 42       may include core (0), linalg (5), meshing (40) and cfd.
                      May NOT include thermal (45), whose ThermalSparseMatrix
                      is therefore unavailable however similar the problem is
no OCCT, no Qt        neither appears in this milestone
units are typed       mu is a DynamicViscosity; a coefficient is SI internally
no gradient operator  P19-GRAD-001 is NOT authorized. A face gradient cannot be
                      computed here, and computing one would be unauthorized
                      work, not a convenience
no solve              P19-LINSOLVE-001 is NOT authorized. This milestone
                      assembles; it does not solve
determinism           no unordered iteration, no accumulation order that
                      depends on a hash, no seed
```

## Options

### Who owns the sparse matrix

```text
A  reuse thermal's ThermalSparseMatrix          REFUSED by layering: thermal is
                                                45, cfd is 42. Not a judgement
                                                call; the architecture test
                                                fails the build
B  put a shared sparse type in linalg (5)       REJECTED for this milestone. It
                                                would modify a module P17 and
                                                P18 are qualified against, to
                                                serve one new consumer. ADR-063
                                                Decision 7 refused the same
                                                shape of extraction for the
                                                same reason
C  a cfd-owned CSR matrix exposing linalg's
   own span form                                CHOSEN. See Decision 3
```

Option B is the one a reader will press on, because a third copy of a sparse
matrix is exactly the duplication ADR-060 worried about. The answer is ADR-063
Decision 7's, applied unchanged: the extraction would touch qualified modules
to serve a single new consumer, and the risk it removes is guarded another way
— here, by Decision 3 making the cfd matrix expose *precisely*
`linalg::SparseIndex` spans, so the two cannot drift in the only way that would
matter.

### Which surface-vector decomposition

A diffusion flux is `mu grad U . S_f`. Only the part of `S_f` parallel to `d`
can be written implicitly with `U_N - U_P`, so every finite-volume scheme splits
`S_f = E_f + T_f` with `E_f` parallel to `d` and `T_f` deferred to an explicit
correction. Three splits are standard, and they differ in where they put the
flux. With `A = |S_f|`, `n = S_f / A`, `theta` the non-orthogonality angle and
`d_hat = d / |d|`:

```text
                  E_f                      implicit coefficient   |T|/|S|   |T|/|E|
minimum           (S_f . d_hat) d_hat      mu A cos(theta) / |d|  sin       tan
  correction                               = mu A (d.n) / |d|^2
orthogonal        A d_hat                  mu A / |d|             sqrt      sqrt
  correction                                                      (2-2cos)  (2-2cos)
over-relaxed      (A^2 / (S_f . d_hat))    mu A / (d . n)         tan       sin
                    d_hat
```

Every closed form in that table was confirmed numerically to 1e-14 before any
code was written, over 20,000 random face-and-`d` pairs and again on a swept
angle, and the measured figures are these:

```text
theta      |T|/|E| minimum   |T|/|E| orthogonal   |T|/|E| over-relaxed
 10.0              0.1763               0.1743                 0.1736
 30.0              0.5774               0.5176                 0.5000
 47.6              1.0951               0.8071                 0.7385
 57.9              1.5941               0.9681                 0.8471
 66.5              2.2998               1.0966                 0.9171
 85.0             11.4301               1.3512                 0.9962
 89.3             81.8470               1.4055                 0.9999
```

Two criteria pull in opposite directions, and saying which one decides is the
whole content of the choice.

**The absolute size of the deferred term favours minimum correction**:
`|T|/|S| = sin(theta)`, bounded by one, against `tan(theta)` for over-relaxed,
which reaches 81.8 at the Cylinder's worst face.

**The ratio of deferred to implicit favours over-relaxed**: `|T|/|E| =
sin(theta)`, bounded by one at *every* angle, against `tan(theta)` for minimum
correction, which is unbounded. At `theta = 89.3` degrees minimum correction
asks a deferred-correction iteration to carry an explicit term 82 times its own
implicit term.

The second criterion decides, because it is the one that governs the operator's
*permanent* use. The first describes a transitional state — this milestone has
no gradient, so nothing applies the correction yet — and choosing a permanent
discretisation to flatter a temporary condition is the wrong way round. An
earlier draft of this ADR chose minimum correction on the first criterion; the
measurement above is what changed it.

**One claim a reviewer might credit to this choice, and should not.** Diagonal
dominance is *structural* and identical for all three: the diagonal of a row is
the sum of the same `Gamma_f` that the off-diagonals negate, so every
decomposition gives a weakly diagonally dominant row. The decomposition changes
the scale of a row, not the dominance within it.

### What to do about the correction this milestone cannot compute

```text
i    drop it, assemble the orthogonal part only    REFUSED. ADR-063 F1 says the
                                                   correction is mandatory. A
                                                   silent orthogonal-only
                                                   Laplacian on a mesh whose
                                                   mean theta is 58 degrees is
                                                   a wrong answer that
                                                   converges
ii   refuse any mesh above a threshold             REFUSED. ADR-063 F1 reserves
                                                   the threshold to VALID-001
                                                   and the owner, in as many
                                                   words. Setting one here
                                                   would also refuse every
                                                   reference mesh
iii  compute a face gradient here                  REFUSED. That is
                                                   P19-GRAD-001, which is not
                                                   authorized. Scope comes from
                                                   TODO.md, and an operator
                                                   needing a gradient is not
                                                   permission to build one
iv   take the correction as a CALLER-SUPPLIED
     face gradient, and report the omission
     per face when none is given                   CHOSEN. See Decision 5
```

## Decision 1 — The operator is a per-component scalar assembly, and it owns no field

`assembleCfdDiffusion` takes a topology, a geometry, a prepared boundary, a
constant `DynamicViscosity`, and the component being assembled. It returns a
matrix and a source vector. It stores no field, no solution and no gradient, and
it is not a class with a lifetime: the same reasoning ADR-068 applied to the
initial field applies here, one milestone later, and for the same reason — the
consumer that will own a field is a solve, and there is no solve.

A velocity assembly is three calls, one per `VectorComponent`. A pressure
assembly would be one call on the same code, because ADR-066 made the scalar
layout a special case of the vector layout.

## Decision 2 — The decomposition is OVER-RELAXED, and ADR-065 anticipated it

```text
Gamma_f = mu * faceArea(f) / orthogonalDistance(f)
```

`orthogonalDistance` is `d . n` and is strictly positive for a held face
(ADR-065 Decision 5), so `Gamma_f` is strictly positive and finite without a
guard. That it is *exactly* the accessor ADR-065 built, described there as "the
distance a diffusion coefficient divides by", is the corroboration: the geometry
layer was designed for this discretisation, and this ADR follows the
anticipation rather than discovering it.

The correction vector is `T_f = S_f - E_f`, with `E_f = (A / cos(theta)) d_hat`,
and `T_f . S_f = 0` identically for this decomposition — confirmed to 1.8e-14
over 20,000 random pairs. That orthogonality is the property the deferred
correction's conditioning rests on.

## Decision 3 — A cfd-owned CSR matrix, and cfd STILL does not link linalg

`src/cfd/CMakeLists.txt` carries a standing decision this milestone must answer
rather than walk past:

> NO LINEAR ALGEBRA LINKED. There is no matrix in this milestone. Admitting one
> is P19-LINSOLVE-001's decision, and the generic kernel it would reuse is
> BetterCAD::linalg (layer 5, ADR-052), linked PRIVATE, never an include of
> structural or thermal and never a hand copy.

**One clause of that becomes false and is amended; the rest stays true and is
kept.** "There is no matrix in this milestone" was written when there was none,
and `P19-DIFF-001` is authorized to assemble one, so the comment is corrected to
distinguish the *container*, which is this milestone's, from the *solve kernel*,
which remains `P19-LINSOLVE-001`'s decision. **cfd still links no linear
algebra**: no `linalg` header is included, no `linalg` target is linked, and
nothing here solves.

That has a consequence for the index type, and taking the easy route would have
quietly broken the comment. `CfdSparseMatrix` exposes

```text
rowStart()  std::span<const CfdSparseIndex>
inner()     std::span<const CfdSparseIndex>
values()    std::span<const double>
```

with `using CfdSparseIndex = std::uint64_t;` declared in cfd. It is **not**
`linalg::SparseIndex`, because naming that type would mean including that header
and linking that target, which is the decision being deferred. It is the same
width and signedness by construction, chosen so that
`linalg::solveSymmetricSparse(rows, rowStart, inner, values, rhs, settings)`
consumes these spans with no translation and no copy on the day it is
authorized — and `P19-LINSOLVE-001` is the milestone that adds the
`static_assert(std::is_same_v<CfdSparseIndex, linalg::SparseIndex>)` which
turns "same by construction" into "same, enforced", because that is the
milestone that may legitimately include the header to assert it.

A reader is entitled to call that a weaker guarantee than the assert, and it is.
It is weaker on purpose: a 64-bit unsigned index is also what `thermal`'s
`Index` and `structural`'s are, 32-bit inner indices being "the classic sparse
overflow", so the width is not a coin toss that could land differently — and
buying the assert today would cost a standing architectural decision that is
not this milestone's to spend.

The structure is built from the topology once, before any coefficient: a row per
cell, a column for the cell itself and one per face-adjacent neighbour, column
indices ascending within a row. Coefficients are then **accumulated** into an
existing structure rather than inserted, so assembly cannot change the pattern
and a missing entry is a refusal rather than a reallocation.

## Decision 4 — The matrix is `-div(mu grad)`, with a positive diagonal, and its symmetry is STRUCTURAL

Sign convention, stated once and only here:

```text
row P, internal face f to N:   A[P][P] += Gamma_f
                               A[P][N] -= Gamma_f
```

**P18 REFUSED TO ASSERT THIS, AND THE REASON DOES NOT CARRY OVER.**
`P18-ELEM-001`'s own formulation record says, of the finite-element conduction
matrix: "Off-diagonal signs are NOT asserted: for an obtuse tetrahedron an
off-diagonal term can be positive, so an M-matrix pattern is not a universal
invariant." That is true of `k V (grad Ni . grad Nj)`, whose sign depends on the
angle between two shape-function gradients. It is **not** true here, and the
difference is structural rather than a matter of confidence: a cell-centred
finite-volume off-diagonal is `-Gamma_f` with
`Gamma_f = mu * faceArea / orthogonalDistance`, and ADR-065 Decision 5
guarantees `orthogonalDistance > 0` strictly for a held face, so `Gamma_f` is
strictly positive on every face of every mesh and the off-diagonal is strictly
negative. So this operator CAN assert what P18 would not, and the assertion is
cheap. What it may not assert is anything about the matrix once an explicit
correction is added, and nothing is: the correction goes to the source, never to
a coefficient.

The conditioning caveat, stated rather than left to be found: over-relaxed makes
`Gamma_f` grow as `1 / cos(theta)`, so at the Cylinder's worst face it is about
82 times its orthogonal value. The *sign* is unaffected; the spread of the
diagonal is not, and that is `P19-LINSOLVE-001`'s problem to measure, not a
defect here.

So the assembled matrix is the **negative** of the discrete Laplacian: positive
diagonal, strictly negative off-diagonals, and `A x` approximates
`-div(mu grad x) * volume`-free flux sums. The convention is chosen so that the
matrix a solve receives is the positive-semi-definite one, which is the form
`linalg::solveSymmetricSparse` is written for, and so that adding a transient or
a convection term later adds to a positive diagonal rather than fighting a sign.

**The symmetry is exact, and it is STRUCTURAL -- which is a correction to what
this ADR first claimed.** The first version argued it from an identity: read from
the owner the face uses `d` and `n`, read from the neighbour it uses `-d` and
`-n`, so `d' . n' = d . n` with both negations exact in IEEE-754, hence the same
`Gamma_f` from either side. That identity is TRUE, and it was measured over
20,000 random configurations at worst difference exactly 0.0 before any code
existed. **It is not what the implementation does, and not what the test
verifies.** The assembly computes `gamma` ONCE per face and scatters four entries
in a single visit, so `A[P][N] == A[N][P]` is a property of the scatter: it
cannot fail, and it cannot detect a wrong coefficient. An adversarial review
found this and was right.

So the honest claim is the simpler and stronger one: **one visit writes both
entries, therefore the matrix is symmetric by construction.** The test asserts
`largestSymmetryError() == 0.0` and is worth keeping as a guard on the scatter --
a defective scatter that wrote one side only, or wrote a different value, would
fail it -- but it is not evidence about the coefficient, and
`kCfdDiffusionSymmetryTolerance` can never fire for this assembly. What the
coefficient rests on instead is the one-dimensional oracle and the skew
comparison, both of which read the assembled matrix.

## Decision 5 — The non-orthogonal correction is caller-supplied, and its omission is REPORTED per face

The assembly takes an optional face gradient. With it, the explicit correction
`mu * (grad U)_f . T_f` is added to the source. Without it, the correction is
omitted and the operator says so, in two ways that a caller cannot miss:

```text
CfdDiffusionSummary::correctionApplied   false
CfdDiffusionSummary::deferredFraction    the largest |T_f|/|E_f| over the
                                         assembled faces -- which is
                                         sin(theta_max) for this decomposition
```

This is the only option left standing by the constraints, and it is better than
it sounds, because it makes the decomposition's exactness **testable now**: for a
linear field `U = g . x + c`, `U_N - U_P = g . d` exactly, so the implicit term
is exactly `mu g . E_f` and the correction is exactly `mu g . T_f`. Supplying
the known constant `g` as the face gradient makes the linear-field patch test
exact to floating point, without this milestone owning a gradient operator. The
patch test therefore verifies the split itself, which is the part that is
permanent, rather than a gradient reconstruction that is not built yet.

What is NOT claimed, stated here so no reader has to infer it: **without a
supplied gradient this operator is a correct Laplacian only on an orthogonal
mesh**, and no P16 reference mesh is orthogonal. The assembled matrix is still
the correct implicit part, its symmetry and null-space properties still hold, and
the omission is reported — but a solve driven by it would be solving a modified
problem, and `P19-GRAD-001` is what closes that.

## Decision 6 — The boundary treatment reads ADR-067's table down the `U` column, and `Computed` is unreachable there

```text
policy at a boundary face      diffusion row contribution
FixedValue      U_b            A[P][P] += Gamma_b ;  b[P] += Gamma_b * U_b
ZeroGradient                   nothing: the normal derivative is zero, so the
                               diffusive flux through the face is zero. NOT a
                               zero coefficient written into the matrix -- no
                               entry is touched at all
Symmetry                       A[P][P] += Gamma_b * n_i^2
                               b[P] -= Gamma_b * n_i * sum_{j != i} n_j U_j,P
                               -- the implicit part on the diagonal, the cross
                               terms explicit, exactly as ADR-066 predicted
Computed                       UNREACHABLE for U. Refused by name
```

**THE BOUNDARY'S CURRENCY IS CHECKED ONLY WHEN THE BOUNDARY CLAIMS ONE.**
`bindCfdBoundary` -- the seam every hand-built fixture goes through --
deliberately returns a value whose `source()` is default-constructed, because
the seam is handed no model and so has no body, stamp or geometry revision to
record; BC-001's own header says `isCurrent` against any real source is then
false, which is the conservative direction. A first draft of this operator's
input gate compared that stamp and those counts unconditionally. That would have
refused **every seam-built boundary**, and so made the orthogonal two-cell
fixture and the analytic face pair -- the only two places this operator's
arithmetic can be checked in closed form, because no reference mesh is
orthogonal -- unreachable. The gate therefore has two arms: the STRUCTURAL
agreement (`boundary.boundaryFaces() == topology.boundaryFaces()`) is always
required, because the assembly indexes the boundary by the topology's own
`BoundaryFaceIndex` and a disagreement there is an out-of-range read waiting to
happen; the CURRENCY claim is checked only when the source is not the default.
The residual hazard is stated rather than closed: a *document-built* boundary
from another mesh is refused, a *seam-built* one asserts nothing and is trusted,
and establishing currency belongs to a caller that holds a model. Found by
reading BC-001's contract, not by a failing test.

**THERMAL KEEPS A DIRICHLET VALUE OUT OF ITS MATRIX, AND THIS OPERATOR CANNOT.**
`ThermalSystem`'s own words are "Prescribed-temperature unknowns stay in the
matrices ... A Dirichlet value is applied by P18-SOLVE-001, exactly, and by
nothing here", and its test asserts that assembling with and without a
prescribed temperature gives bit-identical matrices. That is available to a
finite-element method because a boundary node **is** a degree of freedom, so
there is a row to eliminate later. In cell-centred finite volume a boundary face
is **not** a degree of freedom — the unknowns are cell values and nothing else —
so there is no row to keep and nothing for a later solve to eliminate. A
`FixedValue` face has to enter the owner's row at assembly or be lost.

The precedent is therefore noted and not followed, and the reason is the
discretisation rather than a preference. What IS carried over is the part that
transfers: no penalty, no epsilon on a diagonal, no pinned unknown, no
symmetrisation, and no threshold below which an entry is dropped. A
pure-`ZeroGradient` diffusion matrix is singular with the constant vector in its
null space, and it stays singular.

`Gamma_b = mu * faceArea(f) / orthogonalDistance(f)`, where for a boundary face
ADR-065 defines `d = C_f - C_P`, and `orthogonalDistance` is then `h_P / 4`
*identically* — ADR-065 states that as an identity, not a bound. So a boundary
coefficient needs no special geometry and no second code path.

**`Computed` cannot occur for velocity, and that is read off ADR-067's frozen
table rather than assumed**: down the `U` column the only values are
`FixedValue`, `ZeroGradient` and `Symmetry`; `Computed` appears only in the `phi`
column, at the two pressure boundaries. It is still given a named refusal, and
the refusal is recorded as **unreachable through the document path** rather than
claimed tested — the treatment P19-BC-001 gave `DegenerateFacet`.

**The `Symmetry` arm needs the current velocity**, for the cross terms alone. That
is not a field the operator owns: it is a `std::span<const double>` the caller
passes, the same shape of dependency as the correction gradient, and it is
required only when a `Symmetry` face is present. An assembly asked for a
`Symmetry` face without it is refused by name rather than silently dropping the
cross terms, because dropping them would admit a tangential error at every
slip wall — ADR-067's own reason for making `Symmetry` a fourth policy.

## Decision 7 — Face interpolation is DEFINED, and for constant viscosity it is the identity

The checklist asks for a face interpolation. For `mu` constant it is the
identity, and saying only that would leave the item hollow, so the decision is
the *location*: `mu_f` is evaluated by **linear interpolation on the NORMAL
PROJECTIONS between the owner and the neighbour**, with the weight

```text
a   = (C_f - C_P) . n        the owner's normal distance to the face
b   = (C_N - C_f) . n        the neighbour's normal distance from it
w_f = b / (a + b) = b / orthogonalDistance(f)
mu_f = mu_N + w_f (mu_P - mu_N)
```

**THE PROJECTIONS, NOT THE RAW DISTANCES, AND THE IMPLEMENTATION'S OWN TEST
FORCED THAT CORRECTION.** This decision first wrote the weight as
`|C_N - C_f| / |C_N - C_P|`, which is **not bounded by one**: on a skewed face the
face centroid does not lie on the segment joining the two cell centroids -- which
is exactly what skewness measures, and ADR-065 recorded it as high as 6.5 on
`RM-MESH-03`. The sweep measured **3.61 on the Cylinder**. A number above one is
not a convex weight, so the distance ratio does not define an interpolation at
all. The projected form does, and provably: ADR-065 Decision 5 shows
`orthogonalDistance` is the quarter-sum of the two apex heights, so `a` and `b`
are both strictly positive and `w_f` lies strictly inside `(0, 1)` for every held
face.

**And the combination is written `mu_N + w (mu_P - mu_N)` rather than
`w mu_P + (1 - w) mu_N`** -- the same quantity, not the same arithmetic. The
second form is not exactly `mu` when the two viscosities are equal, because the
two products round and the sum reassociates; the same test caught that, failing
an exact comparison whose two sides printed identically. In the stable form the
difference is exactly zero, so `mu_f` is exactly `mu` for every weight and the
assembled coefficient carries no rounding that varies with a face's skewness for
no physical reason.

**THIS DECISION DESCRIBED CODE THAT DID NOT EXIST, AND BOTH REVIEWS FOUND IT.**
The paragraph above previously ended by saying the weight "is named and
implemented now, with the single call site a variable viscosity will enter
through", and that a test "asserts it over the full range of `w_f`". Neither was
true: a grep of the whole module found no weight, no call site and no test. In a
project whose rules treat a claim with nothing behind it as a failed gate, and
where this Decision is the evidence for the checklist's "Define face
interpolation", that is the most serious kind of documentation defect.

It is now implemented rather than reworded, because the checklist asks for the
interpolation and not for a note about it:

```text
cfdFaceInterpolationWeight(geometry, face)   |C_N - C_f| / |C_N - C_P|, computed
                                             from ownerToNeighbour and
                                             cellToFace alone, so it needs no
                                             topology and cannot disagree with
                                             the assembly about which cell is
                                             which. One at a boundary face
cfdFaceViscosity(geometry, face, mu)         w mu_P + (1 - w) mu_N, THE single
                                             call site, called once from the
                                             assembly loop
```

A test sweeps every face of all eight reference bodies, asserts the weight lies
in `[0, 1]`, records the range it actually spans, and asserts `mu_f == mu` at
every one of those weights. **No variable viscosity is implemented**, and
ADR-058 excludes both of its sources from P19 v1; what exists is the one function
a variable viscosity changes.

Skewness is NOT corrected. ADR-065 measured it as high as 6.5 on
`RM-MESH-03`, and a skewness correction needs the same face gradient the
non-orthogonal correction does. It is recorded as a known limitation, not
silently absorbed into the interpolation weight.

## Decision 8 — Conservation is a property of the assembly, not a test of it

Two statements the checklist asks for separately are one statement about the
loop:

```text
equal and opposite    an internal face is visited ONCE and writes four
                      entries: +Gamma to both diagonals, -Gamma to both
                      off-diagonals. There is no second visit that could
                      disagree, which is why the property is structural
conservation          the flux leaving P through f is the flux entering N
                      through f, with no residue, because both are the same
                      Gamma_f times the same (U_P - U_N)
```

A loop over cells and their faces would visit each internal face twice and have
to halve, or skip, or compare — three chances to be inconsistent. The assembly
loops over **faces**, once each, so the conservation claim is a consequence of
the iteration order rather than a check bolted onto it. Tests still measure it,
on the assembled matrix, because a structural argument that is never measured is
a comment.

## Decision 9 — What this milestone does not build

```text
P19-GRAD-001     the face gradient this operator's correction takes as input.
                 Decision 5 is written so that it plugs in without changing
                 this operator's interface
P19-LINSOLVE-001 any solve. Decision 3 exposes linalg's span form so that no
                 translation layer is needed later, but nothing here solves
P19-MOM-001      the momentum predictor, convection, the transient term, the
                 pressure gradient source. This is one operator
P19-VALID-001    the non-orthogonality THRESHOLD. ADR-063 F1 reserves it, and
                 Decision 5 reports rather than refuses so that the reservation
                 is honoured
variable mu      excluded from P19 v1 by ADR-058. Decision 7 names the single
                 call site it will enter through
skewness         not corrected; a known limitation, for the same missing
                 gradient
```

## Decision 10 — Two refusals added and one removed, after review

**`CurrentVelocityNotFinite` and `CorrectionNotFinite` are added.** The input
gate validated the LENGTH of both caller-supplied arrays and never their values,
so a single NaN in either flowed through to the source while the matrix stayed
provably finite -- and nothing, anywhere, validates the source. That made
`CfdSparseMatrix`'s "possession is the evidence" guarantee read as though it
covered the whole system when it covers only the coefficients. The viscosity gate
exists for exactly this reason and was already argued from P18's refusal to
repair a negative conductivity; the same argument applies to the arrays, so they
are refused at the input and the source is never silently cleaned.

**`AssembledMatrixInconsistent` is removed.** It was declared and documented as
the tag a refusal from `CfdSparseMatrix::fromCsr` would carry, and nothing ever
emitted it: `Result<CfdDiffusionSystem>` has no channel for a tag, so the
assembly returns a plain `ErrorCode::Internal` with the sparse problem's name in
its message. A review pointed out that a tag no path produces and no test can
read is a claim rather than a guarantee -- in the milestone that cites
P19-BC-001's lesson about precisely that. The assembly's internal
inconsistencies are now untagged `Internal` errors BY DESIGN, stated in the
header, because they are unreachable: the symbolic pass reserves every pair the
numeric pass can ask for.

So the set is nine, in phase order, and a `static_assert` in the header ties the
array's size to the count -- `-Wswitch` guards `toString` but not an array an
enumerator was left out of.

## Consequences

```text
accepted   a third sparse matrix exists in the tree, after thermal's and
           Eigen's. Mitigated by exposing linalg::SparseIndex rather than a
           private alias, so the one drift that would hurt a future solve
           cannot happen silently
accepted   over-relaxed defers the LARGEST absolute correction of the three.
           That is the cost of choosing on the ratio instead, and it is
           reported per face rather than hidden
accepted   without a caller-supplied gradient the operator is exact only on an
           orthogonal mesh, and no reference mesh is one. Reported, not
           claimed away
accepted   the Symmetry arm makes the assembly depend on the current velocity
           for its cross terms, so a diffusion assembly is not a pure function
           of geometry when a slip wall is present. Refused by name rather than
           silently dropped
gained     the linear-field patch test is exact NOW, because the correction is
           an input and a linear field's gradient is known exactly
gained     bit-exact matrix symmetry, assertable without a tolerance
gained     a pure-Neumann diffusion matrix is SINGULAR with the constant vector
           in its null space, which is physically correct and is a sharp test:
           A * 1 is zero when the boundary adds nothing to a diagonal, and not
           zero as soon as it does. "Exactly" only where a row holds one +Gamma
           and one -Gamma; see Verification
```

## Verification

```text
constant field       A * 1 == 0 with no boundary contribution, and != 0 with
                     one. BIT EXACTNESS ONLY WHERE A ROW IS ONE +Gamma AND ONE
                     -Gamma, which is a correction: in general the diagonal
                     accumulates in face order while a row sum subtracts in
                     column order, and those need not cancel to the last bit.
                     The two-cell fixture has one internal face per row and is
                     asserted exactly; a reference body is asserted relative to
                     the matrix's own largest entry, and the measured figure is
                     recorded
linear field         the exactness of the SPLIT, PER FACE -- not summed per
                     cell. A cell residual would need the boundary to prescribe
                     the field's value at each face centroid, and no condition
                     kind can express an arbitrary vector there. Stated this way
                     because the first version of this line claimed the per-cell
                     form that the test deliberately does not do
1D oracle            an analytic one-dimensional diffusion problem, its
                     coefficients written out independently of the assembly
symmetry             A[P][N] == A[N][P] bit for bit
conservation         the off-diagonal pair of every internal face, equal and
                     opposite, read off the assembled CSR
sign convention      the diagonal strictly positive, every off-diagonal
                     non-positive, on every reference mesh that meshes
boundary arms        each of FixedValue, ZeroGradient and Symmetry reached and
                     asserted BY TAG where it refuses; Computed recorded
                     unreachable
sparse structure     the pattern matches the topology's adjacency exactly, with
                     ascending columns, and assembly does not change it
determinism          two assemblies of one input are bit-identical
```

**Two tolerance rules are adopted from P18's recorded findings, before any test
is written, because both are traps this operator walks into by its nature.**

*The error of a gradient-like quantity is bounded by the size of the products it
sums, not by the size of its answer.* `P18-ELEM-001`'s adversarial review
recorded this as finding F4: three of its tests first FAILED at `1e-11` with
relative errors up to `5e-8`, because a large constant offset in the field
multiplies matrix entries that then cancel. Its remedy was to assert in units of
`eps * (the products summed)` with a bound of 64, and its record is careful to
call that "restated, not loosened". The linear-field patch test here is exactly
that shape — a field `U = g . x + c` whose interesting quantity is `g` while `c`
can be large — so the patch test is written in those units from the start, with
the absolute error recorded beside it. A bound discovered by raising it until a
test passes would be the thing this project's rules forbid.

*A value test cannot separate factors that only ever appear as a product.*
`mu`, `faceArea` and `orthogonalDistance` enter only as `mu * A / (d . n)`, so
swapping or confusing two of them is invisible to any assertion on a
coefficient. `P18`'s equivalent was `rho * cp`, and its protection was that the
two are distinct `Quantity` types so the swap is a compile error. The same
protection applies here — `DynamicViscosity`, `Area` and `Length` are distinct
types — and it is made explicit by compile-fail cases plus a **scale law per
factor**: doubling `mu` alone, the area alone and the distance alone must each
move the coefficient by its own predicted factor. Three separate laws, because
one product-level check passes on all six permutations.

**And the fixtures the numerical claims rest on already exist**, which is worth
recording so a reader does not think a mesh was built to flatter the operator.
No P16 reference mesh is near orthogonal — the best maximum is the Block's 64.09
degrees — so the orthogonal case cannot come from one. It comes from the
hand-built two-cell fixture whose shared face lies on `x + y + z = 10 mm`, where
the two centroids are `(2.5, 2.5, 2.5)` and `(5, 5, 5)` so `d` is *exactly*
parallel to `n` and the true angle is exactly zero. The non-orthogonal companion
is the analytic face pair whose every quantity is exact: `|d| = 5/4`,
`d . n = 3/4`, `cos(theta) = 3/5`. The reference meshes carry the structural
claims — every cell visited, counts agreeing, determinism, staling — and the
hand-built pair carries the arithmetic.

One hazard comes with that orthogonal fixture and is recorded by the geometry
suite: on it the computed quotient `d . n / |d|` is `1.000000000000000222`, one
eps **above** one, so `acos` of it is NaN. `orthogonalityCosine` clamps and a
hand-rolled quotient does not. This operator divides by `orthogonalDistance` and
never forms that quotient, so it is not exposed — but a test that computes an
angle to check a claim would be, and that is why the tests assert on the cosine
and the distance rather than on `theta`.

Figures, tolerances and the measured outcome are in
`docs/verification/P19-DIFF-001/README.md`. The numeric confirmations quoted in
this ADR — the closed forms, the `T_f . S_f = 0` identity and the bit-exact
symmetry — were produced before any C++ was written, and the script that
produced them is recorded with the evidence.
