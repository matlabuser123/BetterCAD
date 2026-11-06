# ADR-070 — The gradient is reconstructed by weighted least squares, with Green-Gauss kept and measured against it

```text
Status      Accepted
Date        2026-11-07
Milestone   P19-GRAD-001 — Gradient Reconstruction
Authorized  2026-11-07, the owner's "implement and fully verify", with the
            fourteen item checklist quoted in full
Supersedes  nothing
Amends      the unit-alias placement P19-DIFF-001 chose (Decision 3)
Depends on  ADR-058 (one FV cell is one P16 tetrahedron), ADR-063 (the
            topology and its non-orthogonality finding F1), ADR-065 (the
            geometric metrics, and the skewness measurement), ADR-066 (the
            component-blocked layout), ADR-067 (the frozen boundary table),
            ADR-069 (the diffusion operator, which consumes a gradient)
```

## Context

`P19-DIFF-001` assembles the diffusion operator and takes its non-orthogonal
correction from a **caller-supplied** face gradient, because the gradient was
not its to build. Its evidence records that as limitation L1, and names this
milestone as the one that closes it. Nothing in the repository reconstructs a
finite-volume gradient: the only gradients that exist are FEM shape-function
gradients in `thermal` and `structural`, which are at layers 45 and 50 and so
cannot be included here even if their method applied, which it does not — a
shape-function gradient is element-local and divides by a Jacobian determinant,
where a finite-volume gradient is a reconstruction from neighbouring cell
values.

So the method is open, and the checklist's own wording makes the choice
explicit rather than implied: it asks for a **Gauss gradient**, and for a
**least-squares gradient "if needed"**. That conditional is the decision this
ADR exists to make, and a word like "if needed" is discharged by a measurement
or it is not discharged at all.

### The measurement, made before any C++ existed

This is the same discipline `ADR-069` Decision 2 used for the surface-vector
decomposition: the question is pure arithmetic, so it was answered in isolation
first, over random tetrahedra, with the seed recorded (`20261107`).

**First, an identity.** For any closed polyhedron, applying the divergence
theorem to the vector field `x` gives

```text
sum_f C_f (x) S_f = V I
```

where `C_f` is a face centroid, `S_f` the outward area vector, and the product
is the outer product. Measured over 20,000 random tetrahedra, the worst
relative deviation is **2.900e-16**, against a machine epsilon of 2.220e-16.

That identity has a consequence which decides the shape of this milestone. For
an exact linear field `phi(x) = a + g . x`, Green-Gauss gives

```text
(1/V) sum_f phi(C_f) S_f  =  (1/V) [ a sum_f S_f + sum_f (g . C_f) S_f ]
                          =  (1/V) [ 0 + V g ]
                          =  g        exactly, on ANY mesh
```

using `sum_f S_f = 0`, which is `ADR-065` Decision 2's own key identity. **So
Green-Gauss is linear-exact on any mesh when it is given the true value at the
face centroid.** Whatever inexactness it shows is therefore not Gauss's; it
belongs to the interpolation that estimates `phi_f` from the two cell values.

**Second, the magnitude of that interpolation error.** The same experiment,
with neighbour centroids displaced tangentially by a controlled multiple of the
cell-to-face distance, so that the segment `C_P -> C_N` no longer passes through
`C_f` — which is what skewness is. Worst relative error over 3,000 samples per
row, with the sample split by cell shape so the two causes are not conflated:

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
       6.50      1.01e-12        2.66e+02        5.41e-11       2129763.0
```

Four things follow, and each is a measurement rather than an argument:

1. **At skewness zero the two Gauss columns are the same number**, 2.51e-14.
   The interpolation is exact exactly when there is no skewness, which is the
   sharpest available statement of where the error comes from.
2. **At any non-zero skewness Green-Gauss loses linear exactness outright**, not
   gradually: 24 % relative error at skewness 0.25 on a well-shaped cell, and
   590 % at the 6.5 `ADR-065` measured on RM-MESH-03.
3. **Cell shape amplifies what skewness causes.** The sliver rows are one to two
   orders of magnitude worse than the well-shaped rows at the same skewness. Two
   distinct causes, and only the second is a mesh-quality question —
   `P19-VALID-001`'s, by `ADR-058`'s own allocation.
4. **Least squares is linear-exact throughout**, at or near rounding in every
   row, because it fits the neighbour values at their own centroids and
   interpolates nothing. But **its normal matrix is not always well
   conditioned**: four neighbours determining three unknowns is barely
   over-determined, and the eigenvalue ratio reaches 1.7e5 on well-shaped cells
   at high skewness and 2.1e6 with slivers admitted.

The checklist asks for linear-field exactness. No P16 reference mesh has zero
skewness. **So least squares is needed, and the measurement is why.**

## Decision 1 — The product is a CELL gradient, and nothing is wired into the diffusion operator

One `CartesianVector` per cell, in `CellIndex` order. Not a face gradient.

Green-Gauss and least squares are both cell-centred reconstructions; a face
gradient is an *interpolation of* cell gradients, and interpolating to faces is
how the correction reaches `assembleCfdDiffusion`. That wiring is a coupling
step, and this checklist does not contain it: `assembleCfdDiffusion` already
accepts `std::span<const VelocityGradient3> faceGradient` and needs no change,
so changing it would be scope this milestone was not given.

**The consequence is stated rather than hidden: nothing consumes this
milestone's output.** That is the same position `P19-DIFF-001` ended in — it
assembled a matrix no solver has seen — and it is the project's rhythm, not an
oversight. `P19-DIFF-001`'s L1 is narrowed by this milestone, not closed by it.

### Rejected: produce the face gradient too

It is three lines of interpolation and it would let the diffusion operator's
correction run for the first time, which is tempting precisely because it is
easy. Rejected because the interpolation of a gradient to a face has its own
accuracy question (the same skewness question, one level up), because choosing
it would pre-empt how the momentum assembly wants to deal with deferred
correction, and because "the next milestone's work is easy from here" is not
authorization. `CLAUDE.md`: a milestone is not permission to refactor the
repository.

## Decision 2 — Both methods are implemented, least squares is the default

```cpp
enum class CfdGradientMethod : std::uint8_t { GreenGauss, LeastSquares };
```

Three candidates were weighed.

```text
                        Green-Gauss only   least squares only   BOTH
linear-exact on P16        NO                 yes                yes
cost per cell              4 flops/face       3x3 solve          either
needs a conditioning gate  no                 YES                yes
measures its own error     no                 no                 YES
discharges "if needed"     no (asserts it)    no (asserts it)    by measurement
answers "skew mesh         no                 partly             YES
  behaviour"
```

**Green-Gauss only** is rejected by the measurement above: it cannot deliver
linear-field exactness on any mesh P16 produces, and the checklist asks for it.

**Least squares only** is rejected for a subtler reason. The checklist asks for
a Gauss gradient *and* for "skew mesh behaviour". With only one method there is
nothing to compare on a skewed mesh except an analytic field, which tests
correctness but says nothing about *behaviour*. Keeping both means the skew-mesh
item is answered by the difference between two implementations on the same mesh,
measured, rather than by a sentence.

**Both** is chosen. Least squares is the default because it is the one that is
exact; Green-Gauss is kept because it is cheap, because the checklist names it,
and because it is the instrument that measures the skewness penalty on a real
mesh rather than on the synthetic configurations above.

### What was almost chosen and should be recorded

**Green-Gauss with an iterative skewness correction** — set
`phi_f = phi_bar + g . (C_f - C_bar)` and iterate. It is the standard remedy and
it would keep one method. It was rejected because it needs the gradient in order
to compute the gradient, so it brings a convergence criterion, an iteration
count and a divergence failure mode into a milestone whose checklist contains
none of those three; and because on a mesh with skewness 6.5 there is no
evidence here that it converges at all. Least squares reaches the same place
with no iteration. If a later milestone wants it, the measurement above is the
baseline it should beat.

## Decision 3 — One template, two instantiations, and the unit aliases move to `CfdData.hpp`

```cpp
template <typename Q> class CfdGradientField;          // Q is the gradient quantity
using CfdPressureGradientField = CfdGradientField<PressureGradient>;
using CfdVelocityGradientField = CfdGradientField<VelocityGradient>;
```

This mirrors `CartesianVector`'s own documented rationale in `CfdData.hpp`
("ONE TEMPLATE, DISTINCT TYPES"): because the quantity is the template
argument, a pressure gradient cannot be passed where a velocity gradient is
wanted, and the check is at compile time rather than a runtime tag.

**Rejected: one field class carrying a `CfdVariable` tag** with
`pressureGradient()` and `velocityGradient()` accessors that return an empty
optional on a mismatched read. It needs one class instead of two instantiations,
and it was rejected because it converts a compile-time error into a runtime
`std::nullopt` that a caller can ignore — the "sentinel a caller forgets to
check" that `ADR-064` rejected for `neighbour`, wearing an optional's clothes.

The field stores `std::vector<double>` in SI, three per cell, and converts on
read through `Q::fromSi`. So the arithmetic stays in the `.cpp` behind two
explicit instantiations, and no engineering API is unitless —
`ARCHITECTURE.md`'s rule is kept by the accessor, not by the storage, which is
the same split `ADR-068` chose when it put an expanded field in a
caller-owned `std::span<double>`.

**THE AMENDMENT.** `P19-DIFF-001` declared

```cpp
using VelocityGradient  = decltype(Velocity{} / Length{});   // CfdDiffusion.hpp
using VelocityGradient3 = CartesianVector<VelocityGradient>;
```

in `CfdDiffusion.hpp`, where it was the only consumer. It now has two, so the
two aliases move to `CfdData.hpp` — the lowest cfd header, which already owns
`CartesianVector`, `VectorComponent` and `Velocity3` — and
`PressureGradient`/`PressureGradient3` join them there. `CfdDiffusion.hpp`
already includes `CfdData.hpp`, so every existing consumer still compiles
unchanged and the public spelling of both names is identical.

This is a public API change and it needs the reason stated: **one declaration
instead of two**. Declaring them again in a gradient header would be legal —
identical typedefs in one namespace — and would be a second source of truth for
a type the two operators must agree on exactly, which is the "second parameter
system" `ARCHITECTURE.md` rejects, in miniature.

**No `core/units` alias is added.** `thermal` has
`dimensions::temperatureGradient` and a `TemperatureGradient` alias in
`core/units`, so there is precedent for the other choice. `P19-DIFF-001`
declined it, citing `ADR-063` Decision 7, and that reasoning is unchanged:
`decltype(Pressure{} / Length{})` is already a real type with a real dimension,
and adding a layer-0 alias to serve one module is a change to the bottom of the
stack for a convenience at layer 42.

## Decision 4 — The boundary face value comes from the policy, per method

`ADR-067`'s table is frozen and this milestone reads it; it adds no kind and
changes no column. What each policy means for a gradient:

```text
policy          Green-Gauss: the face value       least squares: the row
FixedValue      the prescribed value, EXACT       (C_f - C_P, phi_b - phi_P)
ZeroGradient    phi_f = phi_P                     (h n, 0)   -- Decision 5
Symmetry        phi_f = U_P,i - n_i (U_P . n)      (C_f - C_P, phi_f - phi_P)
Computed        refused by name                    refused by name
```

Two of those deserve their reasons recorded.

**`FixedValue` makes Green-Gauss exact at that face**, because the value at the
face centroid is known rather than interpolated — Decision 1's identity applies
directly. So a Dirichlet-dominated mesh flatters Green-Gauss, and a test that
used only such a mesh would measure the wrong thing. The skew-mesh comparison
must therefore run on a mesh with internal faces carrying the error, which the
reference bodies do.

**`Symmetry` couples the components**, and this is the finding of the
UNDERSTAND phase that most changed the design. A slip wall gives
`U_b = U_P - (U_P . n) n`, so the boundary value of component `i` depends on
**all three** components of the owner cell's velocity. The gradient of one
velocity component is therefore *not* a function of that component's field
alone. `ADR-066` says the segregated predictor assembles no row coupling `u` to
`v`, and that remains true of the matrix; it is not true of this boundary value.
The consequence is in the API: reconstructing a velocity-component gradient on a
mesh with a slip wall requires the full component-blocked current velocity, and
its absence is a named refusal rather than a zero.

`Computed` is refused for the reason `ADR-069` refused it: it is the outcome of
a solve, not a boundary condition, and it is unreachable down the `U` and `p`
columns of the frozen table. Reached only by a hand-built boundary, and reported
rather than silently treated as a zero gradient.

## Decision 5 — A `ZeroGradient` least-squares row is the normal-scaled direction with value zero

`ZeroGradient` is `g . n = 0`, a constraint on the gradient, not a value at a
point. Writing it as a least-squares row needs care, and the obvious form is
wrong.

```text
WRONG   (C_f - C_P, 0)        says g . (C_f - C_P) = 0, which is g . n = 0 only
                              when the cell-to-face vector is parallel to n --
                              that is, only on an orthogonal mesh
CHOSEN  (h n, 0)              h = orthogonalDistance(face) = (C_f - C_P) . n,
                              so the row says g . n = 0 exactly, and carries a
                              length so it weights consistently with the value
                              rows
```

`ADR-065` already guarantees `orthogonalDistance` is strictly positive for a
held face and is exactly `h_P / 4` in projection, so the scaling is well defined
and needs no guard of its own.

The wrong form is recorded because it is the one that falls out of treating
every boundary face uniformly, and because on an orthogonal fixture the two
forms agree exactly — so a test built only on the orthogonal pair cannot tell
them apart. The suite therefore pins this on a non-orthogonal fixture, and a
mutation that swaps the forms is part of the sweep.

## Decision 6 — Weights are inverse distance squared, and they cannot affect linear exactness

`w_N = 1 / |d_N|^2`. Standard, scale-aware, and it keeps a distant neighbour from
dominating a near one.

The property worth stating is what the weights **cannot** do: for an exactly
linear field every residual `phi_N - phi_P - g . d_N` is identically zero at the
true gradient, so **in exact arithmetic any** positive weighting reproduces it.

**THOSE THREE WORDS WERE MISSING AND A MUTATION FOUND IT.** The first version of
this decision said any positive weighting reproduces it full stop, and the
mutation sweep was given that claim as a PREDICTION: replacing `1/|d|^2` with
`1.0` on the internal rows should survive a linear-exactness test. It was
**killed**, by the reference-body case.

The mathematics is unchanged — the mutation does not make the fit inexact. What
it does is leave the internal rows at weight 1 while the boundary rows keep
`1/|d|^2`, so the normal matrix becomes badly SCALED, its conditioning rises, and
the rounding error rises with it past a bound stated in units of
`eps * valueScale / lengthScale`. So the test is sensitive to the weighting
through conditioning, not through exactness.

Both halves are worth keeping straight, because they say different things:

```text
the weights cannot    change the answer in exact arithmetic for a linear field.
                      True, and it is why the linear-exactness tests check the
                      METHOD rather than the tuning
the weights can       change the rounding error, because they change the
                      conditioning of the normal matrix -- and a bound tight
                      enough to be worth asserting is tight enough to see it
```

A prediction that turns out wrong is more useful than one that turns out right,
and recording it is the only thing that makes the sweep's other results mean
anything.

## Decision 7 — Singular is refused; ill-conditioned is reported

These are different questions and the project has a precedent for each.

```text
NUMERICALLY       the extreme eigenvalue ratio exceeds
  RANK-DEFICIENT  kCfdGradientConditioningLimit = 1e12, or the determinant is
                  not strictly positive. Refused by name as
                  NeighbourhoodIsDegenerate
ILL-CONDITIONED   solvable, and the ratio is below that limit. REPORTED in the
                  summary as the worst ratio over the mesh, and not refused
```

**THIS PARAGRAPH WAS WRONG IN THE FIRST VERSION OF THIS ADR AND IS CORRECTED
HERE.** It described the refusal as the strictly singular case — `det <= 0`, or
fewer than three independent directions — and never named
`kCfdGradientConditioningLimit`, which the code has and which also refuses
systems that are *solvable* but have no significant digits left. A reader of the
decision would not have learned the constant existed. That is the same class of
defect a review found in `ADR-069` Decision 7, which described an interpolation
weight that did not exist; the remedy there was to implement the code, and the
remedy here is to describe it.

So the honest statement of the line: `1e12` is a **floating-point** limit, not an
engineering one. The normal matrix is `sum_N w_N d_N (x) d_N`, so its condition
number is the SQUARE of the underlying fit's, and at `1e12` the solve retains
about four of its sixteen digits. Past that the direction of the computed
gradient stops being determined by the data, so there is nothing worth
returning.

**And where the limit sits is measured rather than asserted.** Every P16
reference body comes in below about `1e5`, and a tetrahedron 1 mm thick in a 1 m
cell at `3.6e5` is still solved; the cells that get refused are orders of
magnitude past `1e12`, and a test brackets the crossing from both sides so the
limit is known to separate hopeless systems from merely awkward ones rather than
to sit in the middle of the real data. Refusing a numerically rank-deficient
system is not a quality threshold; it is the statement that the normal equations
determine nothing. Reporting an ill-conditioned one follows
`ADR-063` finding F1 exactly: it reserves any accept-or-refuse threshold on mesh
quality to `P19-VALID-001` and the owner, and `ADR-069` honoured that by
reporting the omitted correction fraction rather than refusing. An operator that
chose its own conditioning threshold here would be making a mesh-acceptance
decision that is not its to make.

The ratio is the extreme eigenvalue ratio of the symmetric 3×3, computed in
closed form. It is a true 2-norm condition number of the normal matrix, which is
the **square** of the conditioning of the underlying fit — stated here because
reporting a squared quantity as "the condition number" would overstate the
problem by a factor of two in the exponent.

## Decision 8 — The 3×3 solve is closed-form arithmetic in `cfd`, and `linalg` is still not linked

`src/cfd/CMakeLists.txt` carries a standing clause, amended once by `ADR-069`:
no linear algebra is linked, no `linalg` header is included, and the generic
kernel a later solve would reuse is `P19-LINSOLVE-001`'s decision to spend.

A symmetric 3×3 solve by cofactors, and the closed-form eigenvalues of a
symmetric 3×3, are written out here in full. The clause is not violated and the
distinction is the one `ADR-069` Decision 3 drew:

```text
a GENERIC KERNEL      a sparse factorisation, an iterative solver, a
                      preconditioner. BetterCAD::linalg's, and linking it is a
                      decision this milestone does not get to make
CLOSED-FORM           a 3x3 cofactor inverse and the trigonometric form of a
  ARITHMETIC          symmetric 3x3's eigenvalues. Finite expressions with no
                      iteration, no convergence criterion and no tuning
```

What is in the lower layers was **read, not assumed**, because `ADR-069`
Decision 7 was a decision that described code which did not exist and this
project does not get to make that mistake twice:

```text
core/math           NO general matrix type and NO solve. The only 3x3 in it is
                    RigidTransform3D's private rotation, exposed through
                    apply() and a matrix() getter -- no inverse, no solve, no
                    eigenvalues. So there is nothing in a lower layer to reuse
BetterCAD::linalg   ONE public header, SymmetricSolve.hpp, and one solver:
                    solveSymmetricSparse over a CSR triple. Sparse, not dense,
                    and it has no eigenvalue routine at all. Using it for a
                    dense 3x3 would mean building a CSR matrix per cell to
                    reach a library call, and it could not answer the
                    conditioning question of Decision 7 in any case
```

So the arithmetic here is not a hand copy of `linalg`: there is nothing in
`linalg` of this shape to copy. It is the standard cofactor expansion and the
standard trigonometric form for a symmetric 3×3's eigenvalues.

Worth recording as corroboration rather than as a new claim: `linalg`'s own
`SparseIndex` is `std::uint64_t`, which is exactly what `ADR-069` Decision 3
asserted when it made `CfdSparseIndex` cfd's own alias of the same width and
signedness "by construction". That assertion is now read from the header rather
than taken on trust.

## Decision 9 — Determinism is the stored face order, and nothing else

```text
the per-cell order    CfdTopology::cellFaces(cell), ascending FaceIndex. STORED,
                      so the accumulation order is fixed once rather than per
                      caller
the sign              taken from CfdTopology::owner(face). CfdGeometry's own
                      header warns that a caller summing a cell's four stored
                      area vectors WITHOUT consulting owner gets a residual of
                      order one, not of order eps -- so the sign is read, never
                      assumed
no unordered          no unordered container in either file
the cell order        ascending CellIndex, and each cell's gradient depends only
                      on its own neighbourhood, so the cells are independent and
                      the order cannot change a value
```

## Decision 10 — What this milestone refuses to do

```text
no face gradient               Decision 1. The diffusion operator is unchanged
no skewness correction         Decision 2's rejected candidate. It needs the
                               gradient to compute the gradient
no limiter                     a limiter bounds a reconstruction for a
                               convection scheme; there is no convection scheme.
                               P19-CONV-001's, and it is not authorized
no quality threshold           Decision 7. ADR-063 F1 reserves it
no core/units alias            Decision 3. ADR-063 Decision 7's reasoning holds
no second field store          the gradient is DERIVED, by ADR-058's own
                               classification: never persisted, never in
                               history, never an authority. Nothing is added to
                               the document, and no CFD serializer exists to
                               change
no solve                       still nothing in cfd solves anything
```

## Consequences

**Good.** Linear-field exactness holds on every mesh P16 produces, which plain
Green-Gauss cannot give. The skewness penalty becomes a measured number on a
real mesh rather than a theoretical worry, because both methods exist and can be
differenced. The unit aliases have one home. A degenerate neighbourhood is a
named refusal rather than a plausible-looking wrong vector.

**Bad, and accepted.** Two methods are more code than one, and the second exists
to measure the first. A 3×3 solve per cell is more expensive than four
multiply-adds per face — unmeasured, because nothing in `cfd` has a performance
baseline and this milestone does not create one. The conditioning number is
reported and not acted on, so a caller who ignores the summary can consume a
badly conditioned gradient; that is the same bargain `ADR-069` struck with the
omitted correction, and it is deliberate.

**Unresolved, and named.** Whether the reported conditioning ratio should ever
become a refusal is `P19-VALID-001`'s and the owner's. Whether the face gradient
is interpolated from these cell gradients, or assembled some other way, belongs
to whichever milestone first couples the correction into a momentum equation.
