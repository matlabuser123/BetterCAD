# P18-LOAD-001 — surface heat flux

```text
SUBJECT:  q'' [W/m^2] on a CAD face -> an equivalent nodal heat input [W]
SOURCE:   thermal::integrateSurfaceHeatFlux / facetNodalHeat, driven by P16's
          boundaryFacetsOf through thermal::resolveFaceTarget
```

## The integral

For a three-node linear triangle of area `A` under a constant `q''`:

```text
Qe_i = integral_A N_i q'' dA = q'' (integral_A N_i dA) = q'' A / 3        i = 1, 2, 3
sum_i Qe_i = q'' A
```

`integral_A N_i dA = A/3` is the standard result for a linear shape function on
a triangle (the integral of a linear function is the area times its value at
the centroid, where each `N_i = 1/3`). It is exact, so there is no quadrature.
The formula is **not** `q'' A` per node and **not** `q'' A / 4`; mutations L01,
L02 and L05 (`A` doubled) are killed.

No normal is computed. The area is `meshing::triangleArea`, the unsigned
`(1/2)|(p2-p1) x (p3-p1)|` in m^2 from P16's coordinates; a zero, negative or
non-finite area is `DegenerateFacet` and is never a silent zero contribution.

## Hand oracle 1 — the single triangle

```text
nodes   (0,0,0)  (2,0,0)  (0,3,0)           A = (1/2) * 2 * 3 = 3 m^2
q''     12 W/m^2
expected total  q'' A = 36 W         expected per node  36/3 = 12 W each
actual          36 W                 actual             [12 12 12] W
sum error       0                    (exact in binary)
```

## Hand oracle 2 — a general, non-axis-aligned triangle

```text
nodes   (0.3,-0.2,0.5)  (1.7,0.4,0.9)  (0.1,1.3,-0.4)
A       1.38025359988663 m^2     by the test's own cross product
q''     -250.5 W/m^2             (extraction, sign preserved exactly)
total   -345.753526771601 W      each corner -115.251175590534 W
error   below 1e-14 relative
```

## Shared nodes accumulate; triangulation moves the distribution, not the total

A 2 x 3 m rectangle cut by one diagonal: each triangle takes 36 W at
`q'' = 12`; the two diagonal corners receive 12 W from **each** triangle (24 W),
the others 12 W; the total is 72 W. A load that **overwrote** would leave 12 W
on the diagonal and a total of 60 W (mutation L11 does exactly that and is
killed). Cut along the other diagonal the total is still 72 W and the 24 W pair
moves to the other two nodes. The invariant is `sum Q = q'' A`, not the
per-node vector.

## Through P16's mapping — a real CAD face

```text
face            end cap of the 40 x 30 x 20 mm block    FaceName{feature, EndCap}
P16 mapping     2 boundary facets, 4 mapped nodes of 9
CAD area        0.040 * 0.030 = 1.2e-3 m^2
mesh area       1.2e-3 m^2 by the test's cross product (rel error 0)
q''             500 W/m^2
prepared total  0.6 W        expected q'' A_CAD = 0.6 W        sum error 0
nodal vector    equals the test's own q'' A_f / 3 accumulation over P16's facets
loaded nodes    exactly the four at z = 20 mm; none of the start cap's at z = 0
```

The end cap and the start cap are equal-area faces on different nodes:
`GeometryReferenceResolvesCurrentBoundary` checks, node by node over the whole
mesh, that the heat reaches exactly the nodes P16 puts on the named face.

## Scaling

```text
flux      q'' -> c q''   Q -> c Q         c = 0.25, 2, 10, -3        1e-15
geometry  x' = s x       Q' = s^2 Q       s = 1e-3, 0.1, 2, 10, 100  1e-14
```

## What it refuses

`NonFiniteValue` (NaN, +inf, -inf), `TargetInvalid`, `TargetUnresolved`,
`TargetWithoutFacets` (an empty list is not a zero load), `DegenerateFacet`,
`EntityNotInMesh`, `EntitiesNotUnique` (a repeated facet would be counted
twice), `DofMapMismatch`, `NonFiniteResult` (a share, or a running sum, that
overflows). Each has a test; see [LOAD_MODEL.md](LOAD_MODEL.md).
