# P16-QUALITY-001 — measured results

```text
SUBJECT:  what the metrics report about the meshes BetterCAD actually produces
DATE:     2026-10-02
```

Every number here was produced by the test suite and appears in the
qualification logs. None was estimated.

## The headline finding

**The first thing the quality metrics did was reveal that BetterCAD's
curved-body meshes contain near-degenerate elements.** This is reported, not
hidden, and no threshold was chosen to make it go away.

```text
body                                  worst 3r/R   worst dihedral   worst tri q
40x40x40 mm block, default sizing      0.640754     45 to 120 deg     0.866025
60x60x20 mm block bored 20 mm, 8 mm    0.032153      1.23 to 176.1    0.037152
cylinder r6 h20 mm, 3 mm target        0.000451      0.26 to 176.9    0.090328
hollow tube r10/r6 h20 mm, 3 mm        0.013208      2.90 to 167.7    0.090328
cylinder, global 6 mm + 1.5 mm local   0.005122      2.21 to 175.6    0.090328
cylinder SURFACE only, 0.05 mm defl.   n/a           n/a              0.090328
```

The planar-faced block is excellent and matches its closed form exactly. Every
curved or bored body carries elements with a minimum dihedral angle of a few
degrees and a radius ratio three orders of magnitude below ideal.

**Where it comes from.** A tetrahedron must conform to the boundary triangles
it is built on, so a thin boundary triangle forces a thin element. The surface
triangulation of the cylindrical wall at 0.05 mm deflection has a worst
triangle shape quality of 0.090328 and a minimum angle of 2.993467° — and that
same pair of figures appears, identically, on the surface mesh alone, on the
cylinder's volume mesh and on the tube's. For the bored block the chain is
visible in a single number: the volume mesh's worst dihedral is **1.230616°**
and the boundary's worst triangle angle is **1.230616°**. The same angle.

For the cylinder the volume mesher made something worse still — 0.26° against
the boundary's 2.99° — so the surface is the origin but not the whole story.

**What this is not.** It is not a P16-QUALITY defect, and it is not a licence
to pick a lenient threshold. It is a measurement with a clear owner: the
surface mesher's deflection controls (`P16-SURF-001`) and, when a solver states
a requirement, `P17`. Recording it is the milestone working.

## The block: closed form over every element

```text
40 x 40 x 40 mm, default sizing      12 Tet4, 12 Tri3
```

Twelve congruent tetrahedra, each matching the hand-derived closed form for
"OCCT's two triangles per planar face, joined to one interior node":

```text
metric            measured                closed form
V                 5.333333e-06 m^3        a^3/12
min edge          34.641016 mm            a sqrt(3)/2
max edge          56.568542 mm            a sqrt(2)
mean edge         40.081932 mm            a (2 + sqrt(2) + 3 sqrt(3)/2)/6
aspect ratio      1.632993                sqrt(8/3)
inradius          6.407545 mm             a/(2 + 3 sqrt(2))
circumradius      30.000000 mm            3a/4
radius ratio      0.640754                4/(2 + 3 sqrt(2))
min dihedral      45.000000 deg           pi/4
max dihedral      120.000000 deg          2 pi/3
sum of volumes    6.4e-05 m^3             a^3, the cube
```

min and max coincide for every metric, because the twelve are congruent — so
this is every element, not an average. Full derivation in
[ANALYTICAL_REFERENCES.md](ANALYTICAL_REFERENCES.md).

Its triangles are right isoceles, `q = sqrt(3)/2 = 0.866025`, min angle 45°,
which is what a cube's boundary should be.

## Every production body: ranges hold

For each body, every element of every mesh:

```text
V > 0                                    (an inverted one would be refused)
det(J) == 6V exactly                     (==, not approximately)
l_min <= l_mean <= l_max
r > 0 and R >= 3r                        the radius ratio's upper bound,
                                         which is a theorem, not an observation
aspect >= 1
0 < 3r/R <= 1
0 < min dihedral <= max dihedral < pi
0 < q <= 1 for triangles
0 < min angle <= max angle < pi
```

```text
body                                   Tet4   Tri3   invalid   structurally
                                                     elements  valid
40x40x40 mm block                        12     12      0       yes
60x60x20 mm bored block, 8 mm           147    196      0       yes
cylinder r6 h20 mm, 3 mm                1086   140      0       yes
hollow tube r10/r6 h20, 3 mm             303    324      0       yes
cylinder, 6 mm + 1.5 mm on the top      615    140      0       yes
cylinder surface only                     0    140      0       yes
```

Not one invalid element in 2163 tetrahedra and 952 triangles across the six
bodies — so the near-degenerate elements above are genuinely **valid and
badly shaped**, which is exactly the distinction this milestone exists to
make. A layer that collapsed the two would have reported them as mesh
failures.

## Local refinement

```text
cylinder r6 h20 mm, global 6 mm, 1.5 mm on the top disc
element mean edge   1.280336 mm to 12.904671 mm     a factor of 10
```

Refinement puts elements of two sizes next to each other, which is where a
mesher is most likely to produce a bad transition element. It produced none
that is invalid, and the worst radius ratio (0.005122) is better than the
uniform 3 mm cylinder's (0.000451) rather than worse.

## Classification

Under the shipped default — report-only, no thresholds — nothing can fail:

```text
a 3.96 deg sliver, default policy    structurallyValid  true
                                     validElements      1
                                     failureElements    0
                                     findings           none
                                     satisfiesPolicy()  true
                                     3r/R recorded      0.003591
```

Under a policy someone chooses, the same mesh classifies without any metric
moving:

```text
policy                                 metrics       verdict
no thresholds                          identical     Valid
3r/R warning 0.5, failure 0.1          identical     Failure, 1 finding
```

Three elements under one policy (`3r/R` warning 0.8, failure 0.1):

```text
element              3r/R      class
regular tetrahedron  1.0000    Valid
cube corner          0.7321    Warning
sliver (h/a = 0.02)  0.0036    Failure
findings order       Failure first, then Warning. Valid elements produce none.
```

## Validity boundary

```text
input                                   outcome
mirrored regular tetrahedron            REFUSED, "inverted". Identical edges,
                                        areas and unsigned dihedrals to the
                                        original -- perfect by every shape
                                        metric.
coplanar four points                    REFUSED, "degenerate"
collinear triangle                      REFUSED, "degenerate ... collinear"
element naming an absent node           REFUSED, naming element:9 and node:77
element repeating a handle              REFUSED, naming the handle
body 1e200 m across                     REFUSED, "non-finite signed volume"
body 1e90 m across                      REFUSED, face area "zero or not finite"
1 m base, apex 1e-200 m above it        ACCEPTED with defined == false:
                                        R = 1.7e199 m and its square
                                        overflows. Counted Invalid,
                                        contributes NO samples to any summary.
                                        Structurally VALID -- so this is the
                                        quality layer's own undefined case and
                                        not a restatement of P16-DATA-001's.
a 1.98 deg sliver                       ACCEPTED, Valid, every metric present
empty mesh                              structurally invalid (EmptyMesh), so it
                                        cannot pass for want of bad elements
two regions sharing a node              structurally invalid, and BOTH elements
                                        still measured: a whole-mesh defect does
                                        not make an element's metrics undefined
one inverted element among two          invalidElements 1, validElements 1,
                                        tets.size() 1 -- the invalid one gets
                                        NO metrics, so nothing averages the two
                                        into something acceptable
```

## Policy validation

```text
input                                   outcome
threshold on tet_volume                 refused: "dimensioned size"
NaN or infinite bound                   refused: "non-finite threshold"
higher-is-better, warning 0.2 / failure refused, naming "higher-is-better"
  0.5
lower-is-better, warning 8 / failure 4  refused, naming "lower-is-better"
one bound only                          accepted; classifies, reports no
                                        warnings when only a failure bound is
                                        set
no thresholds at all                    accepted; this is the default
value exactly ON a bound                Valid -- the comparison is STRICT
one nextafter step past it              Warning
past both bounds                        ONE finding, Failure
```

## Determinism

```text
synthetic mesh (2 tets, 1 triangle, a policy)   5 evaluations, reports
                                                compared whole and identical
40 mm block production mesh                     3 evaluations, identical
```

The comparison is the entire `MeshQualityReport` as a value: per-element
metrics, per-metric summaries, findings, their order, the structural report and
the recorded policy. Nothing is built from an unordered container; elements are
visited in ascending `ElementId`, summaries are keyed in a `std::map`, and
findings are sorted by classification, then metric, then element, with ties in
the worst-element search broken by the lower `ElementId`.

Cross-preset mesh determinism remains unasserted, as `P16-VOL-001` and
`P16-SIZE-001` recorded: nothing exports a mesh to compare. The quality report
is a pure function of a mesh, so it inherits exactly that limitation and adds
none.

## Read-only

```text
check                                   outcome
mesh compared before and after          identical, including the inverted
  evaluation                            element an optimiser would repair
report mutated afterwards               mesh unchanged
compile: evaluateMeshQuality(Mesh&)     does not compile
compile: evaluateTetQuality(Mesh&)      does not compile
compile: report.mesh                    does not compile
compile: Mesh from evaluateMeshQuality  does not compile
```

The runtime comparison shows that today's implementation does not mutate; the
four compile-failure cases show that no implementation behind this API could.

## Mutation testing

See [ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md) for the table. Every
mutation was killed; the ones killed by a test, rather than by the compiler,
are identified there with the test that caught them.
