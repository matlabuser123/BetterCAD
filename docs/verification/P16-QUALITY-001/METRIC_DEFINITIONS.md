# P16-QUALITY-001 — metric definitions

```text
SUBJECT:  every quality metric, defined before any threshold exists
DATE:     2026-10-02
```

This document is the metric convention. It was written before the
implementation, and it is the authority the implementation and the tests are
both held to.

Each metric is given as **a formula, a range, an ideal value and a direction**.
A threshold is not part of a metric: it is a separate, policy-level choice that
cannot change a computed number. See "Threshold policy" at the end.

Angles are **radians** and lengths **metres**, because every value inside
BetterCAD is SI. Degrees appear in this document only as a reader's aid.

## The boundary: validity is not quality

Absolute, and it is the reason this layer exists in a separate file from
`MeshValidation`:

```text
STRUCTURAL   P16-DATA-001's validate(), reused unchanged and run FIRST
             a node reference that does not resolve; a repeated handle; a
             non-finite coordinate; a ZERO or NEGATIVE signed volume; a
             duplicate tetrahedron; a node shared between regions; an empty
             mesh.
             The mesh does not describe a body. ALWAYS INVALID.

QUALITY      here
             aspect ratio, radius ratio, dihedral angles, edge distribution,
             triangle shape.
             The mesh is a valid discretisation that may solve badly. A matter
             of degree, and of a threshold someone chose.
```

An inverted tetrahedron is **not** "poor quality with a low score": it is
invalid, and no metric is computed for it. There is **no `abs()` anywhere in
`src/meshing/MeshQuality.cpp`** — taking one would make an inverted element
indistinguishable from a correct one, which is precisely what the sign exists
to expose.

The reverse holds just as strictly. A 2° sliver is structurally valid, and the
report says so: `structurallyValid == true`, the element classified `Valid`
under a policy that sets no bound, with every measured number present.

## Where the definitions come from

**Nothing here is a backend convention.** nglib exposes no quality API at all —
no element-quality call, no shape measure, no histogram — so there was no
Netgen score to reverse-engineer a meaning for, and no risk of adopting one.
Netgen's own internal `CalcTotalBad` is not reachable through nglib and plays
no part.

Every formula below is standard finite-element practice, stated explicitly
here so that a reader never has to guess which of two conventions BetterCAD
followed.

## Tet4 metrics

For a tetrahedron on `p1 p2 p3 p4`, with `a1 = p2-p1`, `a2 = p3-p1`,
`a3 = p4-p1`.

### Signed volume

```text
V = (1/6) a1 . (a2 x a3)
range     any real; a VALID element has V > 0
ideal     n/a -- a dimensioned size, not a score
direction ContextOnly
```

`signedVolume` from P16-DATA-001, **reused, not reimplemented**. One
determinant with one sign convention in the repository: a second one with its
own sign habit is how an inverted element becomes acceptable somewhere.

### Jacobian determinant

```text
det(J) = det[a1 a2 a3] = 6V        for the linear Tet4 reference map
range     any real; a VALID element has det(J) > 0
direction ContextOnly
```

**Exactly 6V**, so it is *derived* in `TetQuality::jacobianDeterminant()`
rather than stored. Storing it would invite the two numbers drifting apart,
and the equality is asserted as `==` — not approximately — in the tests.

It is typed `Volume`, not `double`: "the Jacobian" of a linear Tet4 has the
dimension of volume, and calling it dimensionless is how signed volume and
Jacobian get conflated.

### Edge-length statistics

```text
the six edges: (p1p2) (p1p3) (p1p4) (p2p3) (p2p4) (p3p4)
minEdge  = l_min        maxEdge = l_max
meanEdge = (sum l_i)/6
range     (0, inf)      direction ContextOnly
```

`meanEdge` is the mean over **this element's six edges**. The mesh-level
summary of it is then the mean over **elements** — one contribution per
element, not one per edge occurrence. Stated because "mean edge length" has
two readings and the other one (counting a shared edge once per element that
uses it) gives a different number.

### Aspect ratio

```text
aspectRatio = l_max / l_min
range     [1, inf)      ideal 1 (the regular tetrahedron)
direction LowerIsBetter
```

**Why this definition and not another.** Two alternatives were considered and
rejected:

```text
R / 3r              rejected: the reciprocal of the radius ratio below, so
                    carrying both would be carrying one metric twice
l_max / h_min       rejected: its ideal value is sqrt(3/2) = 1.2247, not 1.
   (longest edge    An "aspect ratio" whose perfect score is 1.22 is exactly
    over shortest   the convention mismatch this document exists to prevent.
    altitude)
```

`l_max / l_min` catches **stretching**. It is also the metric whose weakness is
measured and recorded, which is why a second shape metric is carried:

```text
a wedge on a 10 mm equilateral base, flattening:
   apex 10.0 mm   aspect 1.1547    3r/R 0.977
   apex  1.0 mm   aspect 1.7066    3r/R 0.0849
   apex  0.1 mm   aspect 1.7318    3r/R 0.000899
```

**The aspect ratio saturates at sqrt(3) = 1.732 as the element flattens, while
the radius ratio collapses by three orders of magnitude.** An implementation
shipping `l_max/l_min` alone would report a 1.98° sliver as a 1.73 — a number
that looks fine. Pinned by
`QualityTet_AspectRatioAloneCannotDetectASliver`.

### Radius ratio

```text
r = 3V / A_total                   inscribed sphere
R = |c - p1|, c the circumcentre   circumscribed sphere
radiusRatio = 3r / R
range     (0, 1]        ideal 1 (the regular tetrahedron, uniquely)
direction HigherIsBetter
```

The factor three normalises the regular tetrahedron to exactly 1, since
`R = 3r` there. `r <= R/3` holds for every tetrahedron, so the upper bound is
a theorem and not an observation; it is asserted over every element of every
production mesh.

The circumcentre is solved as

```text
c = ( |a1|^2 (a2 x a3) + |a2|^2 (a3 x a1) + |a3|^2 (a1 x a2) ) / (2 det[a1 a2 a3])
```

and `det[a1 a2 a3] = 6V`, so the denominator is `12V`. **That is exactly why
this is ill-conditioned for a near-degenerate element**, and why the result is
checked for finiteness rather than trusted — see "Undefined" below. The
circumcentre may lie outside the element (it does for every tetrahedron of a
meshed cube, at `z = -a/4`), which is ordinary and is why `R` is not derived
from a face.

Radius ratio catches **slivers and needles**, where the aspect ratio does not.

### Dihedral angles

```text
for the four OUTWARD unit face normals n_i,
theta_ij = arccos( -(n_i . n_j) )            the INTERNAL dihedral
minDihedralAngle = min over the six pairs
maxDihedralAngle = max over the six pairs
range     (0, pi)
ideal     arccos(1/3) = 1.23096 rad = 70.5288 deg   (regular, all six)
direction min: HigherIsBetter      max: LowerIsBetter
```

A tetrahedron has four faces and six edges, and **every pair of faces shares
exactly one edge** — four choose two is six — so the six pairs of normals give
the six edge dihedrals with no edge-to-face table to get wrong.

**THE CONVENTION, stated because confusing it is the classic error.** The
internal dihedral is measured inside the material. For outward normals it is
`arccos(-(n_i . n_j))`, giving 70.5288° for a regular tetrahedron. The angle
**between the outward normals** is the supplement, `arccos(-1/3)` = 109.4712°.
Reporting that as the dihedral would make every well-shaped element look bad —
or make a bad one look acceptable. Both values are asserted in
`QualityTet_RegularTetrahedronMatchesItsClosedFormMetrics`, the first as the
answer and the second as what the answer must not be, and the mutation that
drops the minus sign is killed.

## Triangle3 metrics

### Area

```text
A = (1/2) |(p2-p1) x (p3-p1)|
range     (0, inf); A == 0 is DEGENERATE and refused
direction ContextOnly
```

`triangleArea` from P16-DATA-001, reused. Unsigned: a triangle in three
dimensions has no sign, it has a normal, and which way that normal points is
P16-MAP-001's question.

### Edge-length statistics

As for Tet4, over the three edges. `ContextOnly`.

### Shape quality

```text
q = 4 sqrt(3) A / (l1^2 + l2^2 + l3^2)
range     (0, 1]        ideal 1 (equilateral, uniquely)
direction HigherIsBetter
```

Derived rather than copied: for an equilateral triangle of side `a`,
`A = sqrt(3) a^2 / 4` and the edge-square sum is `3 a^2`, so
`q = 4 sqrt(3) (sqrt(3) a^2/4) / (3 a^2) = 1`. Degeneracy sends `A` to zero
with the edges finite, so `q -> 0`. Scale-free and rotation-invariant, both
asserted.

### Interior angles

```text
theta_i = arccos( (u . v) / (|u| |v|) )   for the two edges meeting at vertex i
range     (0, pi)       ideal pi/3 = 60 deg (equilateral, all three)
direction min: HigherIsBetter      max: LowerIsBetter
```

## Undefined is not "bad"

`TetQuality::defined` and `TriangleQuality::defined` are false when a metric
could not be computed as a finite number. The element is then **`Invalid`, not
"bad but valid"**: an infinity is never clamped into a plausible-looking score
that a threshold could be relaxed past.

Three reachable routes, all tested:

```text
volume overflows            a body 1e200 m across: the determinant is inf.
                            REFUSED (no metrics at all).
a face's area overflows     a body 1e90 m across: the cross product is 1e180
                            and squaring it for the magnitude overflows.
                            REFUSED.
circumradius overflows      R = 0.173/h for a 1 m equilateral base with its
                            apex h above it, so h = 1e-200 m gives R = 1.7e199
                            whose square overflows. REPORTED with
                            defined == false, counted Invalid, and it
                            contributes NO samples to any summary -- rather
                            than contributing an infinity that would poison
                            every aggregate.
```

`safeAcos` clamps its argument to [-1, 1]. That is floating-point hygiene on a
value already mathematically in range — the vectors are normalised and checked
for finiteness first, so the only thing absorbed is the last bit of the
representation. It is **not** a tolerance on geometry and not mesh repair.

## Mesh-level aggregation

```text
MetricSummary { count, minimum, maximum, mean, worst }
```

`mean` is the arithmetic mean over elements, by plain accumulation in ascending
`ElementId` — the order `Mesh` stores elements in, so the result does not
depend on a hash or on iteration order.

**`worst` is per metric, and that is deliberate.** There is no overall score
and no weighted ranking, because the weights could not be justified and
because a single number would hide failure modes: a sliver and a stretched
element are bad in different ways. Demonstrated, not asserted in a comment:

```text
                      aspect ratio   max dihedral
corner tetrahedron    1.4142         90.00 deg
spike (h = 5a)        5.0332         86.70 deg
```

The worst aspect ratio is the spike; the worst maximum dihedral is the corner.
A combined ranking would have to prefer one of those answers.

A `ContextOnly` metric has **no** worst element and says so with an invalid
handle, rather than naming an arbitrary one.

## Threshold policy

A policy is `std::map<QualityMetric, {optional<double> warning, failure}>`.

```text
SEPARATE FROM COMPUTATION   a policy decides how a number is classified and
                            can never change the number. A test runs one mesh
                            through a lenient and a strict policy and requires
                            IDENTICAL metrics with different classifications.

COMPARISONS ARE STRICT      HigherIsBetter: value < failure -> Failure;
                            else value < warning -> Warning
                            LowerIsBetter:  value > failure -> Failure;
                            else value > warning -> Warning
                            A value exactly ON a bound is on the GOOD side of
                            it. Pinned with std::nextafter, so nothing exists
                            between the two cases.

FAILURE BEFORE WARNING      an element past both bounds is reported once, at
                            its worst.

ABSENT BOUND                reported and not classified. Said by
                            std::optional, never by a sentinel value.
```

`validate(QualityThresholds)` refuses three contradictions:

```text
a bound on a ContextOnly metric   "every tetrahedron must have a volume above
                                  1" is a statement about metres, not quality,
                                  and would mean different things for a bracket
                                  and a bridge
a non-finite bound                NaN or infinity
a failure bound on the GOOD side  a policy that classifies a value as Failure
of its warning bound              while calling it better than the warning is
                                  not strict, it is a mistake
```

### BetterCAD's default is report-only, with no thresholds at all

A decision, not an omission.

Quality thresholds are **solver requirements**, and no solver exists yet: P17
owns what a structural analysis needs from a mesh. Inventing numbers now would
be fabricating engineering judgement and dressing it as a default, and every
reference model would then be graded against a standard nobody set. Worse, the
obvious way to pick them — choose values that make the existing reference
bodies green — is the specific failure the milestone brief forbids.

So P16 ships the metrics, the machinery and the semantics, and ships no
opinion. `reportOnlyThresholds()` returns an empty policy, asserted empty by
`QualityPolicy_BetterCadShipsNoThresholdsAtAll`, and under it a structurally
valid element is `Valid` with every measured number present.

`satisfiesPolicy()` is therefore not "the mesh is good". Under the default it
reduces to structural validity, which is the honest answer when no requirement
has been stated.

## This layer observes

It never moves a node, reorders connectivity, drops an element or returns a
repaired mesh. Every entry point takes `const Mesh&` and returns values, and
nothing returned aliases the mesh.

Enforced by the type system rather than by this paragraph: four compile-failure
cases in `tests/compile_fail/MeshQualityMisuse.cpp` show that no mutable
overload exists to write through, that the report carries no route back to the
mesh, and that no repaired mesh comes out of evaluation.
