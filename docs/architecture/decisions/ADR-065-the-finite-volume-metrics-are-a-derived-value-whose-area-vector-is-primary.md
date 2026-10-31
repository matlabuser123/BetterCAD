# ADR-065 — The finite-volume metrics are a derived value whose area vector is primary

```text
STATUS:    Accepted
DATE:      2026-10-31
MILESTONE: P19-GEOM-001
TOUCHES:   ADR-027 (the canonical quantity set, which gains an oriented area),
           ADR-032 (positive element Jacobians), ADR-058 (derived state is never
           persisted), ADR-063 (the face table, the owner rule and the winding
           convention, all binding here), ADR-064 (the topology this hangs on;
           its two volume/orientation refusals are what make this arithmetic
           safe)
```

## Context

ADR-064 built the addressing and said, twice, what it was deliberately leaving
for this milestone:

> No face area vector, centroid, volume or quality metric (P19-GEOM-001).

and, about the winding of `faceNodes`:

> Possession of a `CfdTopology` is therefore proof that `faceNodes` means what it
> says, which is what P19-GEOM-001's area vectors and ADR-063's `S . d > 0` rest
> on.

So the geometry is the first milestone that computes a *number* a solver will
divide by. The topology named the cells and the faces; nothing yet says how big a
cell is, where its centroid sits, or which way and how much area a face presents.

Two things already exist and constrain the answer. `meshing::signedVolume` and
`meshing::triangleArea` compute the two scalar measures of a tetrahedron and a
triangle, with the sign convention ADR-032 fixed — and `triangleArea` is
deliberately *unsigned*, because `Mesh.hpp` refuses to have an opinion about which
way a triangle faces:

> Unsigned, because a triangle in three dimensions has no sign — it has a normal,
> which is the cross product itself and is what orientation means here.

And P19-MESH-001 already measured the two quality metrics ADR-063 named, in its
own test code, over every reference mesh, leaving a note for this milestone:

> P19-GEOM-001 will compute them in production and must agree.

Those recorded numbers are this milestone's independent oracle: a different code
path, written for a different milestone, before this one was authorized.

## Decision 1 — A derived `CfdGeometry` value, built from a mesh and its topology

Three candidates were considered seriously.

```text
                     A: derived cfd value       B: free functions,         C: methods on
                     (CHOSEN)                   computed per call          CfdTopology
correctness          identical formulas in      identical formulas         identical formulas
                     all three
architecture fit     mirrors CfdMeshContract    fits, but has nowhere      WRONG: ADR-064 says
                     and CfdTopology: a         to put the measured        the topology is
                     derived value bound to     summary, and no single     connectivity "and
                     a MeshStamp                place the identities       nothing else"; it also
                                                are known to hold         stores no coordinate,
                                                                          so every call would
                                                                          need the mesh anyway
complexity           one walk, two arrays       no storage, but every     smallest diff, worst
                                                caller re-derives the     boundary
                                                centroids it needs
persistence          none (ADR-058)             none                      none
determinism          sums in ascending index    depends on each            as A
                                                caller's loop order
performance          each metric once; the      a gradient operator       as A
                     relational metrics are     re-derives two cell
                     a few flops on demand      centroids per face, per
                                                iteration
failure modes        one gate: the stamp        none, so a mesh/topology   as A
                     binding                    mismatch is silent
testability          the value can be           primitives test well;     as A
                     compared field for         the assembly cannot be
                     field between two builds   compared between builds
```

**A is chosen.** B is not wrong about the *primitives* — the point-level formulas
really are pure functions of three or four points, and that half of B is adopted
below — but a value is what gives the identities one place to be true and the
measured summary somewhere to live.

`CfdGeometry` is therefore the third derived value in this module's chain, with
the same shape as the first two: it borrows nothing, it carries the `MeshStamp`
it was computed from, it is never persisted, and `describes` is how a holder
checks it is being applied to the same mesh generation.

## Decision 2 — The area vector `Sf` is primary; the area and the unit normal are derived from it

For a face with nodes `a, b, c` wound outward of its owner:

```text
Sf = 1/2 (b - a) x (c - a)      stored
A  = |Sf|                       derived
n  = Sf / |Sf|                  derived
```

The alternative — store the area and the unit normal, and reconstruct
`Sf = A n` — was rejected on the milestone's key identity. The four outward area
vectors of a tetrahedron sum to zero *algebraically*: with `u = b - a`,
`v = c - a`, `w = d - a`, the four half cross products are
`v x u`, `u x w`, `(v - u) x (w - u)` and `w x v`, whose sum expands to
`v x w + w x v = 0`. Computing `Sf` directly keeps that cancellation; computing
it as `A n` passes every component through a square root and a division first, and
the residual grows by orders of magnitude for no gain. The identity is the thing
a reader will check, so the representation that preserves it wins.

It also makes `A = |Sf|` and `n = Sf / A` *exactly consistent* with each other and
with the stored vector, which three separately stored fields would not be.

A first draft added that the rejected representation's residual "grows by orders of
magnitude for no gain". That was never measured and is withdrawn as stated; what is
measured is the chosen one, whose per-cell closure residual reaches 2.03e-16 over
the eight reference meshes and is bit-exactly zero on two of them. The argument for
`Sf` does not need the comparison: a square root and a division per component
cannot improve a cancellation, and the identity is the thing a reader checks.

## Decision 3 — `AreaVector3D` lives in `core`; the winding convention's operations live in `cfd`

An oriented area is a quantity, and this repository has **two** conventions for a
typed three-vector, in two different layers. An adversarial review of this ADR
found that its first draft compared only two candidates and never named the
second convention at all, which is the one CLAUDE.md's "prefer extending an
abstraction that is already there" rule points straight at. All three:

```text
                    A: core, written out      B: cfd::CartesianVector<Area>   C: a cfd-local
                    as a fifth sibling        (the existing template)         struct
                    (CHOSEN)
what exists         core/math/Vector.hpp      CfdData.hpp holds the           nothing
already             holds Translation3D,      template, with Velocity3
                    Force3D, Traction3D,      and Acceleration3 as its
                    Moment3D                  instances
reuse               a fifth hand-written      REUSES the template, and        none
                    struct beside four        gains component(VectorComponent)
                    others                    free
blast radius        layer 0, so every         none outside cfd                none outside cfd
                    module recompiles;
                    additive, no behaviour
                    change
who can use it      every layer               layer 42 and above ONLY         as B
```

**A is chosen, on the layering.** B is attractive and was nearly taken — it reuses
a real abstraction and touches nothing below layer 42 — but an oriented area is
wanted *below* cfd. `meshing` is layer 40 and cannot include layer 42 at all, and
`meshing/SurfaceMesh.hpp` is doing this exact arithmetic today with bare doubles:
it documents a facet orientation test as `dot(a, cross(b, c))` and computes it
untyped. Putting the quantity in cfd would permanently deny it to the module with
the most obvious use for it, and would leave thermal and structural taking a
*geometric* primitive from the CFD module, which is the wrong direction for a
dependency even though the layer numbers permit it.

`core/math/Vector.hpp`'s own rule for admitting a sibling also fits. A `Force3D`
is there

> because a force is an engineering quantity and not a structural-analysis
> concept — P18's heat flux will want the same treatment

and an oriented area is a geometric quantity, not a CFD concept.

The cost of A is accepted and named: a full recompile, and a fifth struct where a
template instantiation would have done. What it does NOT do is unify the two
conventions — `CartesianVector<Q>` stays where it is, with its field quantities,
and nothing here is rewritten to use it. That unification would be a refactor
across two modules, and a milestone is not permission to refactor the repository.

`Point3D - Point3D -> Translation3D` is added for a separate reason: the
difference of two positions is a displacement, that is the signature the type
system already implies, and without it every caller spells the subtraction out
component by component. It belongs in core under any of the three candidates, and
so do `magnitude(Translation3D)`, `dot(Translation3D, Direction3D)` and
`Translation3D * double`, which are operations on core types alone.

Three further additions were written and then **removed** before the milestone
closed, because nothing used them: `AreaVector3D::along(direction, area)`, the
binary `operator-(AreaVector3D, AreaVector3D)`, and
`operator*(double, Translation3D)`. The family is not uniform about these in any
case — `Moment3D` has no `along` and `Translation3D` no binary minus — and an
unused function is an untested one.

`Point3D - Point3D -> Translation3D` is added for the same reason: the difference
of two positions is a displacement, that is the signature the type system already
implies, and without it every caller spells the subtraction out component by
component.

The *operations that depend on a winding* go in `cfd`, not in `meshing`:

```text
triangleAreaVector(a, b, c)    cfd   -- signed; its sign IS ADR-063's convention
triangleCentroid(a, b, c)      cfd
tetrahedronCentroid(a,b,c,d)   cfd
meshing::triangleArea          meshing, unchanged -- unsigned, no opinion
meshing::signedVolume          meshing, reused unchanged for the cell volume
```

This is the line `Mesh.hpp` already drew. An area *vector* is precisely the
opinion about which way a triangle faces that the meshing module declines to
have, and ADR-063 is where that opinion was fixed. The cell volume is **not**
re-implemented: `meshing::signedVolume` is called, so there is one formula for a
tetrahedron's volume in the repository and the sign convention cannot drift.

## Decision 4 — Store the primary quantities and the two per-cell aggregates; derive the relational metrics on demand

```text
stored    per cell    volume, centroid, signed surface-vector sum, closure
          per face    area vector, centroid
          per face    owner, neighbour          copied from the topology
          per boundary face  the face it is     copied from the topology
derived   per face    area, unit normal, orthogonality cosine,
                      non-orthogonality angle, owner-to-neighbour vector,
                      cell-to-face vector, orthogonal distance, skewness
```

A test-gap review found the heading and this table saying FOUR stored arrays and
SIX derived metrics while the code had six and eight — and, worse, that the two
arrays carrying the milestone's headline identity had no decision covering them at
all, in a decision whose stated reason for not storing things is ADR-064's "a
second copy is a second thing to keep true". Both are recorded now.

**Why the per-cell surface-vector sum and its closure ARE stored.** They cannot be
recomputed from this value alone: the sign each face enters with comes from
`owner`, which lives on the topology, so a caller holding only a `CfdGeometry`
could not reproduce them. Storing them also fixes the accumulation order and the
sign convention once instead of per caller — and the sign is the part that
matters, because a caller summing a cell's four stored area vectors without
consulting `owner` gets a residual of order one rather than of order eps. Four
doubles per cell.

The relational metrics are a handful of flops over two stored centroids and one
stored area vector. Storing them as well would roughly triple the memory and
would create exactly what ADR-064 refused for the owner/neighbour tables: "a
second copy is a second thing to keep true". The measured extremes a caller
actually wants in aggregate are in the summary, computed once.

Determinism follows the rule `core/math/Vector.hpp` states for summing forces —
floating-point addition is not associative, so the order is part of the answer.
Every sum here runs in ascending index order, and the array order is the
topology's, which ADR-064 made total.

## Decision 5 — No refusal enum, but the guards are real and four of them are reachable

> **Read this whole section before quoting it.** Its first version claimed that
> every way the arithmetic could fail was already refused upstream, that the
> guards were unreachable, and that an attempt to reach one had been made and had
> failed. The last of those was never true, and the first two are false. The
> correction is below, under "The correction"; the heading and this paragraph were
> left asserting the withdrawn claim after the rest of the section was rewritten,
> which a review caught.

ADR-063 has nine named problems and ADR-064 has eight, each demonstrated on a mesh
built to fail it. This milestone names **none** — and that is a decision about
naming, not a claim that nothing can fail. Everything the metrics divide by is
non-zero on any mesh that yields a topology **in exact arithmetic**, and the proof
of that is short enough to write down; what it does not cover is the computed
values, which is where four of the guards turn out to be reachable.

Let face `f` be wound outward of its owner `P`, `Π` the plane of its three nodes,
`n` the unit normal, and `h_P` the perpendicular distance from `Π` to `P`'s fourth
vertex.

```text
(i)   V_P = A_f h_P / 3, and ADR-064 refuses V_P <= 0 (NonPositiveCellVolume),
      so h_P > 0 and A_f > 0: no face of a topology-valid cell is degenerate.
(ii)  C_P is the mean of four vertices, three of them on Π and the fourth at
      signed distance -h_P (negative BECAUSE the winding points away from the
      owner, which is what faceNodes guarantees). So C_P is at -h_P / 4 < 0.
(iii) For an internal face, ADR-064 refuses two cells that traverse f in the
      same direction (InconsistentFaceOrientation), so the neighbour lists f
      with the opposite winding and its apex is at +h_N, with h_N > 0 by (i).
      So C_N is at +h_N / 4 > 0.
(iv)  d . n = (h_N + h_P) / 4 > 0.                      orthogonal distance
(v)   C_f lies on Π, so (C_f - C_P) . n = h_P / 4 > 0.  the boundary case
(vi)  cos(theta) = d.n / |d| > 0, so theta < 90 deg.    ADR-063 section 6
(vii) d . n != 0, so the line C_P + t d meets Π.        skewness is finite
```

So `V_P`, `A_f`, `|d|` and `d . n` are all non-zero **in exact arithmetic**, and the
proof uses exactly the two refusals an adversarial review forced ADR-064 to add.
The defect that review found — `faceNodes` promising an outward winding with
nothing checked — is what makes the steps above available at all; had it been
documented away instead of enforced, every step from (ii) onward would be an
assumption rather than a consequence.

### The correction: the proof is about exact arithmetic, and three guards are reachable

The first version of this decision went one step further and said the retained
guards were "unreachable through a topology", and that *"an attempt to construct a
mesh that yields a topology and reaches one of them is recorded there, and it did
not succeed."*

**No such attempt had been made when that sentence was written.** It was a
prediction stated as a result, which is the one thing this project's rules forbid
without exception. An adversarial review made the attempt and succeeded on its
first candidate, and a third guard turned up when the resulting test was run. The
sentence is withdrawn and the record below replaces it.

The error in the reasoning is worth keeping in front of a reader, because it is
easy to repeat: step (i) proves `V > 0 => A > 0` **of the exact values**, and a
guard only ever sees computed ones. A positive computed determinant does not imply
a positive computed area.

```text
GUARD                              STATUS        WITNESS
a face area is zero or non-finite    REACHABLE    three vertices at 0, v, 2v are
                                                  EXACTLY collinear (doubling is
                                                  exact), so every minor of the
                                                  cross product is x(2y) - y(2x) = 0.
                                                  Such a tetrahedron is degenerate,
                                                  its exact volume is zero, and its
                                                  computed volume is rounding noise
                                                  that ADR-064 accepts when positive:
                                                  +4.63e-18 m^3 on this toolchain.
                                                  Exact only without FMA contraction
a centroid is not finite             REACHABLE    four coordinates at 1.2e308 are
                                                  each FINITE, which is all
                                                  MeshBuilder requires, and their sum
                                                  overflows at the SECOND addition.
                                                  The determinant stays finite
                                                  (+1.67e299) because the magnitude
                                                  enters each of its terms only
                                                  linearly. THE ONE WITNESS THAT IS
                                                  NOT A ROUNDING ACCIDENT:
                                                  deterministic, in every rounding
                                                  mode. A control at 1e300 builds
an orthogonal distance is <= 0       REACHABLE    two cells within 1e-18 m of their
                                                  shared face's plane but far apart
                                                  IN it: d . n is then two orders
                                                  below the rounding error of
                                                  differencing the centroids, and its
                                                  sign is an accident. 28 of 157
                                                  family members
a face has no finite skewness        REACHABLE    the same configuration drives
                                                  d . Sf to zero. 1 of 157, found by
                                                  RUNNING the test above rather than
                                                  by reasoning about it
a cell volume is <= 0 or non-finite  UNREACHABLE  the assembler calls the same
                                                  meshing::signedVolume on the same
                                                  node order ADR-064's gate called,
                                                  so the comparison is bit-identical
a face has no finite angle           UNREACHABLE  the angle is arccos of a value
                                                  clamped to [-1, 1], so it is finite
                                                  whenever the cosine exists; and the
                                                  cosine is empty only for an empty
                                                  unit normal (refused in pass 2) or
                                                  |d| <= 0, and |d| == 0 forces every
                                                  component of d to zero, hence
                                                  d . n == 0 exactly, so the DISTANCE
                                                  guard fires first. A zero |d|
                                                  cannot outrun it
the element and node lookups         UNREACHABLE  same stamp, and mesh storage is
                                                  append-only -- which matters rather
                                                  than being incidental, because
                                                  MeshBuilder::build() may be called
                                                  twice on a growing builder, giving
                                                  two Mesh values with one stamp
```

The first version of this table had the centroid row as UNREACHABLE with the
reason "a mean of four finite ones cannot overflow at any coordinate a mesh can
hold", and folded the angle and the skewness into one row whose verdict the
skewness witness made wrong. Both were found by the review that re-checked the
correction, which is the second time this decision has been corrected and the
reason the warning at the top of the section is there.

**So the guards are not defence in depth; two of them are load-bearing**, and the
project's standard applies to them in full: each reachable one is demonstrated on a
mesh built to fail it, as ADR-063's nine and ADR-064's eight are.

Both witnesses are knife-edges, and the tests are written to say so rather than to
pretend otherwise. No single fixture can *pin* either, because in exact arithmetic
both quantities are strictly positive — every witness is a rounding artefact. The
collinear tetrahedron's volume is exactly zero, so whether ADR-064's gate accepts
it depends on the sign of noise; that test asserts **both** branches, so it cannot
fail because another build rounded the other way, and it still pins the
reachability whenever the gate does accept. The orthogonal-distance test asserts
over a deterministic family of 200 configurations rather than over any one of them,
and reports the counts.

An enum of named problems was still rejected, on `Mesh.hpp`'s rule about the
element types it refused to reserve:

> an enumerator with no arity, no orientation convention and no validation would
> be a promise the code does not keep

That rejection survives the correction above, but for a different reason than the
one first given. It is not that the guards cannot fire — three can. It is that a
`CfdGeometryProblem` enum would be an *engineering diagnosis* of something that is
not an engineering defect: every witness is a mesh ADR-063 and ADR-064 would call
valid and that no mesher would produce, failing a floating-point comparison at an
aspect ratio of `1e-18`. The `FailedPrecondition` names the cell or face and the
value it computed, which is what a reader of such a failure actually needs. If a
real mesh ever reaches one, that is the moment to give it a name.

The fourth refusal, the stamp binding — a topology proved for one mesh generation
applied to another — is reachable by ordinary means and is tested, as ADR-063's and
ADR-064's were.

## Decision 6 — Skewness is internal-only; non-orthogonality is total

```text
nonOrthogonality(face)   total     internal: angle(C_N - C_P, n)
                                   boundary: angle(C_f - C_P, n)
skewness(face)           optional  internal only; empty at a boundary face
ownerToNeighbour(face)   optional  empty at a boundary face
cellToFace(face)         total
```

The asymmetry is deliberate. The non-orthogonality angle measures how far the
direction a gradient is taken along departs from the face normal; at a boundary
face that direction is `C_f - C_P`, which is real, is what a boundary gradient is
evaluated on, and is what makes the metric worth reporting there. Skewness
measures how far the face centroid lies from the line joining the two cell
centroids — it is the error in interpolating *between two cell values*, and at a
boundary face there is no second value to interpolate with. Defining it there with
`d = C_f - C_P` would put the intersection point exactly at `C_f` and return
identically zero: a number that looks like a measurement and is an artefact of the
definition. An empty optional says what is true.

`ownerToNeighbour` is an optional for the reason ADR-064 gave for `neighbour`
being one: a sentinel is the kind of thing a caller forgets to check.

## Decision 7 — Measured, never judged

The summary carries the totals and the extremes — total volume, total boundary
area, smallest and largest cell, the worst non-orthogonality and skewness, the
smallest orthogonal distance, and the closure residuals — and **no threshold**.
P19-VALID-001 owns acceptance, and P19-MESH-001 already established the pattern
with a test named for it (`TheReferenceMeshQualityIsMeasuredNotJudged`). Its owner
finding F1 records that the eight P16 reference meshes are poor finite-volume
meshes; this milestone measures that more precisely and still refuses none of
them.

Two numerical points belong here rather than in a comment, and both were measured
rather than argued.

**The clamp on the non-orthogonality cosine is load-bearing, and a fixture already
in the suite proves it.** The quotient `d . n / |d|` is clamped to `[-1, 1]` before
`acos`. This is not defensive padding: on the *most* regular face possible `d` is
exactly parallel to `n`, the quotient rounds to just above 1, and `acos` returns
NaN — the best input is the one that breaks it. The two-cell mesh the tests have
used since P19-TOPO-001 is that input. Its shared face lies on `x + y + z = 10 mm`
with cell centroids at `(2.5, 2.5, 2.5)` and `(5, 5, 5)`, so the true angle is
exactly zero, and in double precision the quotient comes out as

```text
d . n / |d| = 1.000000000000000222      (1 + 1 eps)
std::acos(that) = NaN
the clamped angle = 0 rad
```

`CfdGeometry_ThePerfectlyOrthogonalFaceIsWhereTheCosineExceedsOne` asserts the
quotient *exceeds* one and that `acos` of it *is* NaN, so if a future change stops
this fixture reaching that state the test says so rather than leaving the clamp
looking like dead code. P19-MESH-001's measurement already clamped; this records
why, so the clamp is not removed as redundant.

**The angle is ill-conditioned near zero, and the cosine is the form a solver
should use.** `d(theta)/d(cos) = -1 / sin(theta)` diverges as `theta -> 0`, so a
cosine perturbed by one eps becomes an angle of `sqrt(2 eps)`. Under a rigid
transform of a whole mesh — which should change nothing — the measured difference
on that same exactly-orthogonal face is `2.107e-8` rad, against
`sqrt(2 eps) = 2.107e-8`: the predicted value to three significant figures. The
geometry has not lost anything; `acos` has. The cosine over the same transform
moves by `8.9e-16`.

So the angle is the *reported* form and the cosine is the *computed* one.

**A first draft told consumers to form that cosine themselves** — "divide
`orthogonalDistance(face)` by the magnitude of `ownerToNeighbour(face)`,
well-conditioned everywhere" — and declined to add an accessor "for a quantity
that is already one division away". An adversarial review pointed out that the
suite's own clamp fixture proves that expression returns `1.000000000000000222` on
the most regular interior face a tetrahedral mesh can have: one eps ABOVE one, and
outside the domain of `acos`, `asin` and anything else that takes a cosine. The
recommendation handed a consumer an out-of-range number, and nothing tested the
path it recommended.

So the division is now the accessor `orthogonalityCosine(FaceIndex)`, the clamp
lives inside it, `nonOrthogonality` is defined as its `arccos`, and there is one
clamp in the module instead of a documented instruction to reproduce it.
`CfdGeometry_TheRecommendedConsumerCosineIsInRangeOnEveryFace` exercises the
recommended path on every face of all eight reference meshes.

The measured invariances, with the attributions corrected — the figure first
quoted here was from the two-cell fixture and was presented as general:

```text
                               two-cell fixture   worst of 8 reference meshes
the cosine, under a rigid transform   8.9e-16                    2.69e-14
the angle, same transform             2.107e-8  = sqrt(2 eps)    (same mechanism)
```

The reference-mesh figure is larger for a reason worth recording, and the reason
first given here was wrong. It said `d . n` is a cancellation "with relative error
of order `eps / cos`", which took a RELATIVE error and used it as an ABSOLUTE
bound on the quotient; divide through and the `1/cos` cancels. The cosine's
absolute error is of order

```text
eps * L / |d|           L = the distance of the nodes from the model origin
```

because `d` is a difference of two centroids each carrying `eps * L`. A review
raised it, and it was then measured here rather than taken on the review's word:
driving `cos` from 0.9918 to 0.0920 on a fixed fixture leaves the error at or
below 5.6e-17, while translating that same fixture from the origin to 10 km
raises it from 0 to 8.2e-13 as `L / |d|` goes from 3.6 to 6.3e4. So the suite's
2.69e-14 comes from
its `L / |d|` of a few hundred, and the test's bound is calibrated to models near
the origin — which is now what the constant's comment says. Note that this is
the same `eps * L / (d . n)` recorded below for `orthogonalDistance`, divided by
`cos`: the two statements are one mechanism, and the earlier `eps / cos` made them
look like two.

**And the quantity with the worst conditioning is not the angle.** A second review
observed that this decision documented the conditioning of the metric that matters
less and omitted the one that matters more. `orthogonalDistance` is the number a
diffusion coefficient divides by, and it is a difference of two cell centroids
projected on the normal — catastrophic cancellation, with relative error of
order `eps * L / (d . n)`. Measured on two slivers of extent 1 m sharing a tilted
face, against the exact `h/2`:

```text
h/L     1e-6 -> 3.8e-10    1e-10 -> 2.7e-6    1e-14 -> 3.2e-2
        1e-8 -> 2.5e-8     1e-12 -> 3.3e-4    1e-16 -> 191 per cent
```

The eight reference meshes are nowhere near this — the smallest internal
`d . n` is 0.141 mm on a 60 mm body — and the guard of Decision 5 catches the
cases that go non-positive. But "measured, never judged" means a consumer can be
handed a number that is wholly wrong with no diagnostic, and that belongs in the
evidence's KNOWN LIMITATIONS rather than in nobody's notes. P19-VALID-001 is where
a bound on it would go.

## Consequences

Accepted:

- the metrics are recomputed wholesale by every remesh, like the topology
- a caller holding a `CfdGeometry` across a remesh gets `describes() == false`,
  never a silent reinterpretation
- `core/math/Vector.hpp` grows a fifth vector type, so every module recompiles;
  nothing changes behaviourally
- the identities are verified in tests and reported in the summary, not enforced
  as a build-time tolerance — a gate with a tolerance in it is a gate that can be
  tuned
- the quality metrics now exist in two places: production, and P19-MESH-001's
  test-local `measure()`. The duplication is deliberate and both stay — but it
  is a cross-check of the IMPLEMENTATION and not of the DEFINITION, and calling it
  "the independent check" was wrong. Both transcribe one formula, so they agree
  however wrong that formula is; what checks the definition is the closed-form
  anchor (`arccos(3/5)` and `sqrt(10)/15`), which a test-gap review found the
  milestone lacked entirely for the skewness.

Rejected and worth recording:

- storing `A` and `n` instead of `Sf` (Decision 2)
- an `AreaVector3D` local to `cfd` (Decision 3)
- caching the relational metrics (Decision 4)
- a refusal enum mirroring ADR-063 and ADR-064 (Decision 5)
- defining skewness at a boundary face, where it is identically zero (Decision 6)
- a closure tolerance as a build-time gate (Decision 7)
