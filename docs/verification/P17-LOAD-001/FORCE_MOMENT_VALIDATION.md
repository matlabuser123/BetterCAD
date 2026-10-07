# P17-LOAD-001 — force and moment validation

```text
SUBJECT:  the measured conservation of the resultant force AND the resultant
          moment, in the tables the brief asks for. Every expected value was
          formed in the test from a fixture's own declared dimensions; none
          came from a production helper.
```

## Why the moment is not optional

A wrong nodal distribution can preserve the total force **exactly**. Putting
the whole facet force on one corner does: the three thirds and the single whole
sum to the same vector. Mutation probe **M2** does exactly that and is invisible
to every force-resultant assertion in the suite — it is caught only through the
first moment.

That is the justification for every moment row below, and the reason the
fixtures are asymmetric and the origins are off-centre: a moment about the
centroid, or about a symmetric face's centre, would be zero and would prove
nothing.

## Analytical facet integration

The reference triangle `(0,0,0), (2,0,0), (0,3,0)`, worked out by hand:

```text
u = (2,0,0)   v = (0,3,0)   u x v = (0,0,6)   A_vec = (0,0,3)
A = 3 m^2     centroid = (2/3, 1, 0)          outward normal = +Z
```

| Quantity | Expected | Actual | Note |
| --- | --- | --- | --- |
| `A_vec` | `(0, 0, 3)` | `(0, 0, 3)` | **exact**, asserted with `==` |
| `A` | `3` | `3` | exact; and asserted **not** `6`, which is the unhalved cross product |
| `A` with reversed winding | `3` | `3` | exact — a triangle has no signed area |
| `A_vec` with reversed winding | `(0, 0, -3)` | `(0, 0, -3)` | exact — it does have a direction |
| per-corner force, `t = [2,-1,4]` | `[2, -1, 4]` | `[2, -1, 4]` | exact. `A t / 3` with `A = 3` cancels, which makes this the clearest possible check of the divisor |
| three corners summed | `[6, -3, 12]` | `[6, -3, 12]` | exact, `= A t` |
| per-corner, `A = 0.0012`, `t_z = -1000` | `-0.4` | `-0.4` | to 1e-15 relative |

Asserted against the **wrong** answers too: the per-corner force is required
not to equal `A t / 2` and not to equal `A t`.

A skew triangle `(1,1,1), (3,1,2), (1,4,5)`, also by hand:
`u x v = (-3,-8,6)`, halved `(-1.5,-4,3)`, `A = sqrt(27.25)` — matched to 1e-15.

A degenerate triangle `(0,0,0), (1,0,0), (2,0,0)` gives an area vector of
exactly zero and an area of exactly zero, and a facet with zero area is refused
as `DegenerateFacet`.

## Surface traction: force and moment

Fixture: a 40 x 30 x 20 mm block, end cap area `1.2e-3 m^2` from its own
dimensions. Traction `[2000, -1500, 4000] Pa`, origin `(-0.1, 0.05, -0.2) m`.

| Quantity | Expected | Tolerance | PASS |
| --- | --- | --- | --- |
| facet area sum | `1.2e-3 m^2` (analytic CAD area) | 1e-12 rel | yes |
| `F_x` | `t_x A = 2.4 N` | 1e-12 rel | yes |
| `F_y` | `t_y A = -1.8 N` | 1e-12 rel | yes |
| `F_z` | `t_z A = 4.8 N` | 1e-12 rel | yes |
| `M` about the off-centre origin | `A (x_c - O) x t`, with `x_c = (0.020, 0.015, 0.020)` from the dimensions | 1e-10 rel | yes |

The area row is independent of the load and comes first deliberately: a planar
face's facets tile it exactly, so an area error is caught before it becomes a
force error. Three numbers are compared — the analytic CAD area, the facet-area
sum, and the resultant — which triangulates the failure.

Production's own `resultantForce()` and `resultantMomentAbout()` are then
compared against the sums computed in the test, to 1e-14 and 1e-12.

RM-MESH-01, a 120 x 70 x 35 mm block whose end-cap area is `8.4e-3 m^2`,
traction `[3000, -2000, 5000] Pa`, origin `(-0.05, 0.02, -0.03)`:

| Quantity | Expected | Tolerance | PASS |
| --- | --- | --- | --- |
| facet area sum | `8.4e-3 m^2` | 1e-12 rel | yes |
| `F` | `t A` component-wise | 1e-11 rel | yes |
| `M` | `A (x_c - O) x t`, `x_c = (0.060, 0.035, 0.035)` | 1e-10 rel | yes |

## Pressure: force and moment

Fixture: the same block, `p = 250000 Pa`, end cap outward normal `+Z`.

| Case | Expected `F_z` | Tolerance | PASS |
| --- | --- | --- | --- |
| end cap, `p > 0` | `-p A = -300 N` — **inward** | 1e-12 rel | yes |
| start cap, same `p` | `+p A = +300 N` — also inward, because its normal is `-Z` | 1e-12 rel | yes |
| end cap, `p < 0` | `+300 N` — suction, not clamped | 1e-12 rel | yes |
| transverse components | `0` | 1e-9 of `p A` | yes |

The two caps from one scalar, with opposite signs, is what "follows the normal"
means — a global-direction pressure cannot produce it.

The area-applied-once check is asserted against its own failure mode: the
magnitude is required to be below `2 p A` and to differ from `p A^2` by more
than a tenth of `p A`.

**Closed surface.** All six faces of the block under one uniform pressure:

```text
net force      zero to 1e-10 of a single face's force
```

The continuum result for a uniform pressure on a closed boundary is zero, and
this is the sharpest orientation regression available — one flipped facet
normal leaves a residual. The scale is a real face force rather than 1, so the
bound means something.

## Refinement convergence — and two vacuous drafts

**This table is the third version of this test, and the first two were
vacuous.** Recorded because the reason is a property of the geometry:

```text
draft 1   varied the sizing with requireWith(), which builds a VolumeMesh the
          Mesher never HOLDS -- so requireStructuralModel found no mesh and
          refused. The test failed, correctly, for a reason unrelated to loads
draft 2   refined a PLANAR face of RM-MESH-03 and measured 40 facets at every
          level. A plane is exactly representable, so no deflection or sizing
          change retriangulates it: "the resultant is unchanged" was true
          because NOTHING changed
```

The fixture is now RM-MESH-02's cylindrical wall, radius 25 mm and height
60 mm, analytic lateral area `2 pi r h = 9.4248e-3 m^2`. Surface deflection
varied through the document's own `MeshControl`:

| Deflection | Facets | Loaded nodes | Facet area vs analytic | Resultant error |
| --- | --- | --- | --- | --- |
| 0.40 mm | **72** | 72 | below | largest |
| 0.10 mm | **100** | 100 | closer | smaller |
| 0.025 mm | **200** | 200 | within 0.1% | `< 1e-3` |

Asserted, in this order:

```text
1  the levels DIFFER -- strictly more facets and strictly more loaded nodes at
   each step. FIRST, because every claim below is meaningless otherwise, which
   is exactly how draft 2 passed
2  the facet area never EXCEEDS the analytic area -- an inscribed polygon is
   shorter than its circle -- and increases monotonically
3  the relative error of the resultant falls monotonically, to below 1e-3
4  at every level the resultant is exactly t times the FACET area, to 1e-11 --
   so the integration is right at every discretisation and the convergence
   above is the geometry's
```

That last row is the important distinction: the load integration is exact for
the mesh it is given, and what converges is the mesh's approximation of the
cylinder.

## Remesh invariance

RM-MESH-01, the same canonical load, the model remeshed:

```text
canonical load target        FaceName -- byte-identical across the remesh,
                             because it holds a face name and a traction and
                             nothing else
M1 mesh stamp                one MeshId
M2 mesh stamp                a DIFFERENT MeshId -- asserted
same facet handles required  NO
facet area                   equal to 1e-12 relative
resultant force              equal, and equal to t A_analytic to 1e-11
resultant moment             equal to 1e-10 of (force x characteristic length)
```

## Gravity

| Fixture | Analytic volume | Expected weight | Tolerance | PASS |
| --- | --- | --- | --- | --- |
| 40 x 30 x 20 mm block | `2.4e-5 m^3` | `-rho V g` | 1e-10 rel | yes |
| RM-MESH-01 | `2.94e-4 m^3` | `-rho V g` | 1e-10 rel | yes |
| sideways `g` | same | `+rho V g` along X | 1e-10 rel | yes |
| `rho` 7850 -> 2700 | same | scales with `rho` | 1e-10 rel | yes |

| Moment | Expected | Tolerance | PASS |
| --- | --- | --- | --- |
| about the origin, 40x30x20 block | `(r_cm - O) x M g`, `r_cm` the geometric centre | 1e-10 rel | yes |
| about the origin, RM-MESH-01 | the same, `r_cm = (0.060, 0.035, 0.0175)` | 1e-10 rel | yes |

Both weights are checked against the **analytic** volume from the fixture's
declared dimensions, not against `tetrahedralVolume()`. A planar-faced block is
tiled exactly, so the two agree and the analytic figure is a usable oracle.

## Scale laws

Geometrically similar blocks at three scales, with the origin scaled too:

| Quantity | Expected | Tolerance | PASS |
| --- | --- | --- | --- |
| traction force | `s^2` | 1e-9 rel | yes |
| pressure force | `s^2` | 1e-9 rel | yes |
| gravity force | `s^3` | 1e-8 rel | yes |
| traction moment | `s^3` | 1e-8 rel | yes |

These are the sharpest unit checks in the milestone. Dropping the `0.5` from the
area vector, or `A/3` becoming `A/2`, or gravity's `V/4` becoming `V/2`, all
break a row — and all three are mutation probes that are killed.

## Superposition and ordering

```text
F(L1 + L2) = F(L1) + F(L2)      to 1e-11 of the combined scale
M(L1 + L2) = M(L1) + M(L2)      to 1e-11, about an off-centre origin
reversing the list              the SAME nodal field, node for node, worst
                                per-node difference <= 1e-14 of the scale
removing a load                 leaves exactly the other contribution, node
                                for node, by equality
two DIFFERENT ids on one face   accepted and summed: 100 + 200 Pa gives
                                -300 Pa A
two records with one LoadId     REFUSED -- one would be silently ignored
```

## Tolerance policy

Every bound above is **relative to a physical scale**, never a bare absolute:

```text
forces        relative to the expected force
moments       relative to the expected moment, or to (force x characteristic
              length) where the expectation is zero
zero results  absolute, against a scale formed from a real face force or the
              body's own weight -- never against 1
per-node      relative to the largest nodal force in the field
```

The reason is the scale-law table: the same fixture at `s = 0.5` and `s = 2`
spans four orders of magnitude in force and six in moment, and one absolute
tolerance cannot serve both.
