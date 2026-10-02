# P16-VOL-001 — validation and independent references

```text
SUBJECT:  what is checked, against what, and why each tolerance is the number
          it is
RESULT:   every acceptance check is against something computed independently of
          the code under test. One tolerance exists, and it is an
          accumulation bound rather than a modelling choice.
DATE:     2026-10-02
```

## The acceptance chain

A `VolumeMesh` cannot exist unless all of these passed, in this order:

```text
1  the backend returned elements          NG_OK is not enough; see NETGEN_PIPELINE.md
2  validate() is clean                     connectivity, degeneracy, orientation,
                                           duplication, regions, finiteness
3  the boundary conforms                   both directions, by exact position
4  the volume is recovered                 against the boundary's enclosed volume
```

**Order 2 before 4 is deliberate.** A mesh with an inverted or degenerate
element has no meaningful volume, so reporting that its volume disagrees would
describe a symptom and hide the cause. The same reasoning P16-GEOM-001 used to
put its currency check before its geometry checks.

## The two independent references, and why both

```text
the boundary's enclosed volume    The tetrahedra tile EXACTLY the polyhedron
                                  the surface bounds. An identity, not an
                                  approximation.

the CAD volume                    What the body really measures. A faceted
                                  boundary UNDERSTATES it for any curved face,
                                  so the right assertion is convergence from
                                  below -- never equality.
```

Holding a curved body to the second as though it were the first is the standard
way to end up loosening a tolerance until a test passes. The acceptance gate
inside `generateVolumeMesh` therefore uses **only the first**; the CAD volume is
recorded for comparison and is asserted in the tests, where the deflection can
be varied.

### The one tolerance, and its justification

```cpp
constexpr double kVolumeAccumulationTolerance = 1e-9;   // relative
```

Both sides are sums of 3x3 determinants over the same vertices in the same
units. They differ only in grouping — the enclosed volume sums over boundary
triangles from the origin, the tetrahedral volume over elements — so the
difference is floating-point accumulation and nothing else. Double precision
gives roughly 1e-16 per operation; a few thousand operations put the expected
disagreement near 1e-13. `1e-9` is the repository's established figure for
geometric accumulation, four orders above what is needed, and it is **not** a
modelling tolerance: no mesh that is geometrically wrong passes it, because a
wrong mesh misses by percent, not by 1e-9.

The comparison is **relative**, so it is scale-independent — a 1 mm body and a
1 m body are held to the same standard.

## Hand-computed expectations

Every number below is closed form, evaluated by hand, and none was obtained by
running BetterCAD and recording what it said.

```text
box 20x30x40 mm      V = 20*30*40 = 24000 mm^3 = 2.4e-5 m^3
                     planar faces only, so the facet boundary IS the body and
                     EQUALITY is the right expectation -- which is exactly why
                     a box is the fixture that can check volume recovery

cylinder r10 h25     V = pi r^2 h = 7.854e-6 m^3
                     curved, so asserted as a one-sided bound plus convergence

tube Ro20 Ri12 h30   V = pi (Ro^2 - Ri^2) h = 2.4127e-5 m^3
                     the void is the point: a mesher that filled the bore would
                     report pi Ro^2 h = 3.7699e-5, which is 1.56x larger

unit tetrahedron     V = 1/6 det(I) = 1/6
                     the boundary extractor's four outward faces must enclose
                     exactly this
```

### A bound that was written wrong, twice

The tube's "void was not filled" guard was first written
`actual < filledBore / 2`. That is **arithmetically impossible**: this annulus
is `pi(400-144)/pi(400)` = 64% of its outer cylinder, so a correct mesh can
never satisfy it. The test failed on correct code.

It is recorded here, and in a comment in the test, because this is the **second
time** that exact mistake has been made on that exact fixture — the first was in
P16-SURF-001, and a memory note about it did not prevent the repeat. The
replacement bound, `actual < (annulus + filledBore) / 2`, is both meaningful and
achievable, and the arithmetic is written out beside it.

## Duplicate tetrahedra: why `validate()` and not P16-QUALITY-001

P16-DATA-001 left this to P16-QUALITY-001, with a reason worth quoting:

> detecting the pair needs a canonical key over node sets that is a whole-mesh
> question … It belongs with the other whole-mesh, threshold-free-but-global
> checks

`validate()` is where the other whole-mesh threshold-free check already lives
(`NodeSharedBetweenRegions`), and `TODO.md` places "no duplicate tetrahedra" in
P16-VOL-001's own checklist. So the deferral's reasoning points *here*, and the
authority says here. It is a **data defect**, not a quality finding, by the
existing file's own definition: two tetrahedra on one node set describe the same
region of space, at most one can be right, and deciding that needs no tolerance.

The detector keys on the **sorted** node handles, which matters more than it
looks:

```text
(1,2,3,4) and (1,2,3,4)    the same, trivially
(1,2,3,4) and (2,1,4,3)    an EVEN permutation: same volume, same nodes.
                           An order-sensitive comparison calls these different.
(1,2,3,4) and (2,1,3,4)    an ODD permutation: same nodes, NEGATIVE volume.
                           Both a duplicate AND inverted, and the more
                           dangerous case, because the inverted twin is exactly
                           what an order-sensitive check would miss.
```

All three are tested, and so is the control that makes them mean something: two
tetrahedra **sharing a face** must be accepted, because that is what every real
volume mesh is made of. A duplicate check keyed on "shares nodes" rather than on
the node set would reject every mesh.

Three copies of one element give **two** complaints, not one and not three: the
first is the element the others duplicate.

## Boundary conformity

Computed from the tetrahedra's own connectivity — faces used by exactly one
element — and compared against the input surface by exact node position. **The
backend's own account of the surface it produced is never consulted**, because
that would be asking the thing under test to grade itself.

Counted in **both** directions:

```text
unmatchedBoundaryFaceCount        a boundary face matching no surface triangle
unmatchedSurfaceTriangleCount     a surface triangle matched by no boundary face
```

The second direction is the one that catches a backend quietly re-triangulating
the boundary or swallowing part of it; counting only the first cannot see it.
Both are zero for box, cylinder and tube, which establishes that Netgen
preserves the surface it is given rather than refining it.

Matching is by **exact** coordinate, and there is no tolerance to tune: the
backend returns the boundary points it was given, bit for bit, because it copies
them. If a backend ever moved a boundary node, the check reports it as
unmatched — the honest outcome, not a reason to add an epsilon.

The extractor is also tested directly, where the answer is known by counting:

```text
one tetrahedron            4 boundary faces, and their outward windings enclose
                           exactly the tetrahedron's own volume -- which is what
                           distinguishes a correct winding from four faces in
                           any order
two sharing a face         6, not 8: the shared face is interior and is keyed on
                           the node SET, so it is not seen as two faces
```

## No absolute values anywhere

`tetrahedralVolume` sums **signed** volumes. A test builds two tetrahedra of
equal shape, one inverted, and requires the sum to be **zero** — not twice one
of them. An `abs()` anywhere in the chain would make an inverted element
contribute as though it were correct, which is the defect the sign convention
exists to expose.

## What is validated by the type system rather than by a test

`VolumeMesh`'s constructor is private with one friend, so possessing one is the
evidence. Three compile-fail cases prove it cannot be fabricated, cannot be
promoted from a plain `Mesh`, and exposes no way to mutate the mesh it is
evidence about.

A plain `Mesh` stays freely constructible, deliberately: inspection and
visualisation (`P16-VIZ-001`) need to hold a mesh that may be invalid, together
with its report.
