# P16-QUALITY-001 — analytical references

```text
SUBJECT:  the closed forms every metric is checked against
DATE:     2026-10-02
```

**No expected value in this milestone came from running BetterCAD.** Every
figure below was derived by hand, then confirmed by an independent
double-precision evaluation written from the formulas in
[METRIC_DEFINITIONS.md](METRIC_DEFINITIONS.md) — not by calling
`evaluateTetQuality`. The tests then carry the closed forms as algebra
(`std::sqrt(2.0)/12.0 * a*a*a`), not as recorded decimals, so a reader can
check the derivation rather than trust a literal.

The pattern TODO.md forbids —

```text
auto expected = productionMetric(element);
EXPECT_EQ(productionMetric(element), expected);
```

— appears nowhere. The one place a measured value is used as an input is
`QualityPolicy_AValueExactlyAtAThresholdIsOnTheGoodSideOfIt`, which takes the
element's own radius ratio as the *threshold* in order to pin the strictness of
the comparison with `std::nextafter`. That test asserts nothing about the
metric's value; it is about the inequality.

## Why two reference tetrahedra, not one

A regular tetrahedron alone is a weak reference: too many of its metrics are 1,
and several wrong formulas reproduce it. The cube-corner tetrahedron disagrees
with it on **every** shape metric, and crucially its dihedral range *straddles*
the regular one's single value — 54.74° below and 90° above — so an
implementation cannot satisfy both by accident.

```text
                         regular (edge a)          corner (legs a)
V                        sqrt(2)/12 a^3            a^3/6
edges                    a x6                      a x3, a sqrt(2) x3
aspect l_max/l_min       1                         sqrt(2)
total area               sqrt(3) a^2               (3 + sqrt(3))/2 a^2
inradius 3V/A            sqrt(6)/12 a              (3 - sqrt(3))/6 a
circumradius             sqrt(6)/4 a               sqrt(3)/2 a
radius ratio 3r/R        1                         sqrt(3) - 1
min dihedral             acos(1/3)                 acos(1/sqrt(3))
max dihedral             acos(1/3)                 pi/2
```

## Regular tetrahedron, edge 1

Coordinates, wound for a positive signed volume:

```text
(0, 0, 0)   (1, 0, 0)   (1/2, sqrt(3)/2, 0)   (1/2, sqrt(3)/6, sqrt(6)/3)
```

```text
quantity              closed form      value
V                     sqrt(2)/12       0.11785113019775793
det(J) = 6V                            0.70710678118654746
each edge             1                1
aspect ratio          1                1
each face area        sqrt(3)/4        0.4330127018922193
total area A          sqrt(3)          1.7320508075688772
inradius r = 3V/A     sqrt(6)/12       0.20412414523193148
circumradius R        sqrt(6)/4        0.6123724356957945
radius ratio 3r/R     1                1
all six dihedrals     acos(1/3)        1.2309594173407747 rad = 70.5287793655 deg
```

**The trap, recorded next to the answer:**

```text
acos( 1/3)  =  70.5287793655 deg    the INTERNAL dihedral -- the answer
acos(-1/3)  = 109.4712206345 deg    the angle between the OUTWARD NORMALS
```

The test asserts the first and also asserts the result is nowhere near the
second, so an implementation reporting the supplement fails here rather than
somewhere downstream.

## Cube-corner tetrahedron, legs a

```text
(0, 0, 0)   (a, 0, 0)   (0, a, 0)   (0, 0, a)
```

```text
quantity              closed form              value at a = 1
V                     a^3/6                    0.16666666666666666
edges                 a x3, a sqrt(2) x3       1, 1.4142135623730951
mean edge             a (1 + sqrt(2))/2        1.2071067811865475
aspect ratio          sqrt(2)                  1.4142135623730951
total area            a^2 (3 + sqrt(3))/2      2.3660254037844384
   = 3 right isoceles legs (3a^2/2) + one equilateral slant of side
     a sqrt(2) (sqrt(3)/2 a^2)
inradius              a (3 - sqrt(3))/6        0.21132486540518713
circumradius          a sqrt(3)/2              0.8660254037844386
   the circumcentre is the cube's centre (a/2, a/2, a/2)
radius ratio          sqrt(3) - 1              0.7320508075688772
min dihedral x3       acos(1/sqrt(3))          0.9553166181245092 rad = 54.7356103172 deg
   where a leg face meets the slant face
max dihedral x3       pi/2                     1.5707963267948966 rad = 90 deg
   along the three mutually perpendicular legs
```

## Triangles

### Equilateral, side a

```text
A                 sqrt(3)/4 a^2      0.4330127018922193  at a = 1
shape quality q   1                  1
all three angles  pi/3               1.0471975511965976 rad = 60 deg
```

### Right isoceles, legs a

```text
A                 a^2/2              0.5
edges             a, a, a sqrt(2)
mean edge         a (2 + sqrt(2))/3  1.1380711874576983
shape quality q   sqrt(3)/2          0.8660254037844386
   = 4 sqrt(3) (a^2/2) / (4 a^2)
min angle         pi/4               0.7853981633974483
max angle         pi/2               1.5707963267948966
```

## Degradation sequences

One family, one parameter, so the sequence shows the metrics **order**
elements rather than merely labelling two of them. Wedge: an equilateral base
of side `a` with its apex `h` above the base centroid.

```text
h/a       V/a^3       aspect   3r/R       min dih    max dih
1.000     0.144338    1.1547   0.977082   67.3801    73.8979
0.500     0.0721688   1.3093   0.857143   60.0000    82.8192
0.250     0.0360844   1.5894   0.407843   40.8934   110.9248
0.100     0.0144338   1.7066   0.084904   19.1066   147.0648
0.050     0.00721688  1.7256   0.022167    9.8264   163.0012
0.020     0.00288675  1.7310   0.003591    3.9632   173.1368
0.010     0.00144338  1.7318   0.000899    1.9840   176.5638
```

Monotone throughout: `3r/R` and the minimum dihedral fall, the maximum
dihedral rises, the volume falls. **The aspect ratio saturates at
sqrt(3) = 1.7320** — the finding recorded in
[METRIC_DEFINITIONS.md](METRIC_DEFINITIONS.md) and pinned by a test. At
`h = 0` the element becomes coplanar and is refused: it has stopped being a
bad tetrahedron and become not one at all.

Isoceles triangle on a base `a` with apex height `h`:

```text
h/a       A/a^2    q          min angle    max angle
0.800     0.400    0.996864    57.9946      64.0108
0.400     0.200    0.761341    38.6598     102.6804
0.200     0.100    0.438494    21.8014     136.3972
0.100     0.050    0.227901    11.3099     157.3801
0.050     0.025    0.115086     5.7106     168.5788
```

## The production path: a 40 mm cube

**Independent validation on real pipeline output**, which the synthetic
references cannot give. The CAD -> surface -> Netgen path meshes a cube of side
`a` as **12 congruent tetrahedra on 9 nodes**: OCCT triangulates each planar
face into two right isoceles triangles, and Netgen joins each to one interior
node. One of them is

```text
(0,0,0)   (a,a,0)   (a,0,0)   (a/2, a/2, a/2)
```

```text
quantity              closed form                      value at a = 40 mm
V                     a^3/12                           5.333333e-06 m^3
   and 12V = a^3, the cube
edges                 a x2, a sqrt(2), a sqrt(3)/2 x3
min edge              a sqrt(3)/2                      34.641016 mm
max edge              a sqrt(2)                        56.568542 mm
mean edge             a (2 + sqrt(2) + 3 sqrt(3)/2)/6  40.081932 mm
aspect ratio          sqrt(8/3)                        1.632993161855452
total area            a^2 (2 + 3 sqrt(2))/4            2.497056e-03 m^2
inradius              a / (2 + 3 sqrt(2))              6.407545 mm
circumradius          3a/4                             30.000000 mm
   the circumcentre is at (a/2, a/2, -a/4): OUTSIDE the element, which is
   ordinary for an obtuse tetrahedron and is why R is not derived from a face
radius ratio          4 / (2 + 3 sqrt(2))              0.6407544820340816
min dihedral          pi/4                             45 deg
max dihedral          2 pi/3                           120 deg
```

BetterCAD reported, over all twelve elements, `3r/R` min = mean = 0.640754,
aspect max = mean = 1.632993, dihedrals 45.000000 to 120.000000 deg, element
mean edge 40.081932 mm. **Every tetrahedron is held to the closed form, not
just the worst**: twelve congruent elements means min and max coincide, so an
implementation right on average would fail.

### The tolerance this comparison needs, and why it differs

```text
synthetic references   1e-12 relative, 1e-12 rad absolute
                       Exact coordinates. The reference tetrahedra carry
                       sqrt(3) and sqrt(6), so the metrics inherit a few units
                       in the last place -- a regular tetrahedron measures its
                       aspect ratio as 1 + 2e-16 -- which is why these are
                       relative comparisons rather than equality.

the 40 mm cube         1e-7 relative
                       MEASURED, not guessed. The eight cube corners come
                       through exactly, but Netgen places the NINTH node -- the
                       interior one -- about 6e-11 m from the geometric centre,
                       which is 1.7e-9 relative on the short edges and, after
                       the circumcentre solve, 4.5e-9 on the radius ratio and
                       3e-9 rad on the dihedrals.

translation invariance 1e-11 relative
                       DERIVED. Storing a coordinate at 100 m quantises it to
                       eps * 100 = 2.2e-14 m, and the metrics are differences
                       of such coordinates over a 10 mm edge, so the admitted
                       relative error is eps * offset / edge = 2.2e-12. A
                       property of representing the INPUT; no implementation
                       can do better.
```

The 1e-7 figure is **not** a loosened version of 1e-12 — the synthetic
references keep 1e-12. It is the accuracy of the *mesh*, and the production
test exists to catch a **wrong formula**, which is out by order one: reporting
the supplement instead of the dihedral is 0.68 rad, seven orders of magnitude
outside the bound.

## Invariances asserted

```text
translation      displacement 10 000 x the element. The circumcentre solve is
                 done relative to the first corner, so a formulation using
                 absolute positions would lose most of its digits.
rotation         0.7 rad about (1,2,3)/|(1,2,3)| -- an axis sharing no plane
                 with the reference coordinates, so the arithmetic is really
                 exercised. The volume stays POSITIVE, asserted separately
                 from its magnitude: an implementation taking an absolute
                 value would pass the magnitude check and fail the point of it.
uniform scaling  x1000. The shape metrics do not move; the sizes scale by
                 exactly k, k^2 or k^3, which is what makes "is this volume
                 good?" a question with no scale-free answer.
reflection       mirrored in z. Identical edges, identical face areas,
                 identical unsigned dihedrals -- perfect by every shape metric
                 -- and REFUSED, because the signed volume is negative.
out-of-plane     a triangle lifted out of z = 0: a triangle in three
                 dimensions has no privileged plane, and a formulation that
                 worked in two would fail.
```
