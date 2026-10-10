# P17-VALID-001 — the measured quality distributions the policy is built on

**This document was written before any threshold existed, and the policy was
written from it.** The brief forbids choosing a structural mesh threshold from
intuition and requires P16's measured evidence first. So the measurement came
first: `tests/reference/StructuralValidationSweepTests.cpp` sweeps every
qualified P16 reference model and the deliberately pathological fixtures, and
prints the worst value of every classifiable Tet4 metric with the element
holding it.

Raw log: [logs/quality-sweep.log](logs/quality-sweep.log).

```text
ctest --preset debug-ext -R "StructuralValidation_Sweeps" -V
2 tests, 2 passed, 35.10 s
```

Nothing here is recomputed. Every number is read out of P16's own
`MeshQualityReport::summaries`, under `reportOnlyThresholds()`, which carries a
minimum, maximum, mean and worst element for every metric whatever policy was
active. A second radius-ratio formula in P17 is the defect the architecture
boundary exists to prevent.

## The qualified reference meshes

Every one of these is a mesh BetterCAD produced and P16 QUALIFIED. A structural
acceptance policy that refuses one of them is refusing BetterCAD's own output.

```text
model                        tets   worst 3r/R      worst aspect   min dihedral   max dihedral
RM-MESH-01 block                6   0.43725              4.0933     14.6211 deg    115.641 deg
RM-MESH-02 cylinder           508   0.00124845          16.0391      0.362683 deg  177.113 deg
RM-MESH-03 plate with hole    126   0.0129063           20.4856      1.11273 deg   165.482 deg
RM-MESH-04 hollow tube        808   0.00634594          14.8994      2.3722 deg    170.046 deg
RM-MESH-05 thin plate          12   0.000322641         80.0062      0.71612 deg   178.709 deg
RM-MESH-06 transformed base    12   0.127381             3.88104    14.9313 deg    152.325 deg
RM-MESH-06 transformed placed  12   0.127381             3.88104    14.9314 deg    152.325 deg
RM-MESH-07 local refinement   267   0.0411619            5.61104     7.89468 deg   165.695 deg

every one structurallyValid YES
```

The angles in radians, which is what a `QualityThresholds` bound is in:

```text
                        min dihedral (rad)   max dihedral (rad)
RM-MESH-02 cylinder          0.006330012          3.091204998
RM-MESH-05 thin plate        0.012498652          3.119060453
RM-MESH-03 plate with hole   0.019420802          2.888205753
RM-MESH-04 hollow tube       0.041402701          2.967862580
RM-MESH-07 refinement        0.137788159          2.891923304
RM-MESH-01 block             0.255186335          2.018316200
RM-MESH-06 transformed       0.260600347          2.658572783

for reference, the regular tetrahedron's internal dihedral,
arccos(1/3) = 1.230959417 rad = 70.528779 deg, and pi = 3.141592654
```

## The deliberately pathological fixtures

P16's own flattening wedge: a 10 mm equilateral base with the apex brought
down. Each is ONE element, each is structurally valid, and each is a mesh no
engineer would accept.

```text
apex       3r/R            aspect      min dihedral     structurallyValid
10.0 mm    0.977082        1.1547       67.3801 deg     YES
 1.0 mm    0.0849037       1.70664      19.1066 deg     YES
 0.1 mm    0.00089946      1.73179       1.98399 deg    YES
 0.01 mm   8.99995e-06     1.73205       0.198478 deg   YES
```

**This reproduces P16-QUALITY-001's recorded table** at the three apex heights
it published (0.977 / 0.0849 / 0.000899, aspect 1.1547 / 1.7066 / 1.7318),
which is the independent confirmation that this sweep reads the same metrics
P16 measured. The 0.01 mm row is new here and extends the series by another
order.

## Finding 1 — the aspect ratio is INVERTED on these two sets

```text
worst aspect ratio, qualified meshes       80.0062   (RM-MESH-05 thin plate)
worst aspect ratio, pathological wedges     1.73205  (the 0.01 mm sliver)
```

`l_max/l_min` saturates at `sqrt(3) = 1.7320508` as a tetrahedron flattens, so
the worst sliver measurable scores **1.73**, while a qualified thin plate scores
**80**. Any aspect-ratio rejection line that refuses the sliver refuses every
qualified mesh in the set, and any line that accepts the thin plate accepts
every sliver.

**So `aspectRatio` cannot carry a structural acceptance policy, and this is
measured rather than argued.** The brief said as much as a warning; the sweep
puts a number on it.

## Finding 2 — the two sets INTERLEAVE on the radius ratio

Sorted, worst first, with the qualified meshes marked:

```text
8.99995e-06    wedge 0.01 mm
0.000322641    RM-MESH-05 thin plate          <- QUALIFIED
0.00089946     wedge 0.1 mm  (a 1.98 deg sliver)
0.00124845     RM-MESH-02 cylinder            <- QUALIFIED
0.00634594     RM-MESH-04 hollow tube         <- QUALIFIED
0.0129063      RM-MESH-03 plate with hole     <- QUALIFIED
0.0411619      RM-MESH-07 local refinement    <- QUALIFIED
0.0849037      wedge 1 mm
0.127381       RM-MESH-06 transformed         <- QUALIFIED
0.43725        RM-MESH-01 block               <- QUALIFIED
0.977082       wedge 10 mm
```

A qualified thin plate (3.23e-04) is **2.8x worse** than a deliberate 1.98 deg
sliver (8.99e-04), and a qualified cylinder (1.25e-03) is worse than it too.
There is no radius-ratio value that puts every qualified mesh on one side and
every pathology on the other, because the orders are interleaved.

The same interleaving on the minimum dihedral angle:

```text
0.198478 deg   wedge 0.01 mm
0.362683 deg   RM-MESH-02 cylinder            <- QUALIFIED
0.71612 deg    RM-MESH-05 thin plate          <- QUALIFIED
1.11273 deg    RM-MESH-03 plate with hole     <- QUALIFIED
1.98399 deg    wedge 0.1 mm
2.3722 deg     RM-MESH-04 hollow tube         <- QUALIFIED
7.89468 deg    RM-MESH-07 local refinement    <- QUALIFIED
14.6211 deg    RM-MESH-01 block               <- QUALIFIED
14.9313 deg    RM-MESH-06 transformed         <- QUALIFIED
19.1066 deg    wedge 1 mm
67.3801 deg    wedge 10 mm
```

A rejection line drawn to refuse the 1.98 deg sliver refuses the qualified
cylinder, the qualified thin plate AND the qualified plate with hole.

**This is exactly the trap the brief named** — "a cylinder's chord-polygon
boundary also gives a lower minimum dihedral than a thin plate, so a threshold
tuned on one body shape will reject a legitimate mesh of another" — and the
measurement is worse than the warning: the cylinder is lower than *everything*
except the most extreme sliver in the set.

## Finding 3 — P16 independently measured the same thing

P16-QUALITY-001's `QUALITY_RESULTS.md` recorded, under its own sizing:

```text
body                                  worst 3r/R   worst dihedral
40x40x40 mm block, default sizing      0.640754     45 to 120 deg
60x60x20 mm block bored 20 mm, 8 mm    0.032153      1.23 to 176.1
cylinder r6 h20 mm, 3 mm target        0.000451      0.26 to 176.9
hollow tube r10/r6 h20 mm, 3 mm        0.013208      2.90 to 167.7
cylinder, global 6 mm + 1.5 mm local   0.005122      2.21 to 175.6
```

A SECOND, INDEPENDENT CYLINDER MEASUREMENT: P16's 3 mm cylinder at 3r/R
0.000451 and 0.26 deg, this sweep's reference cylinder at 0.00124845 and
0.362683 deg. Different sizing, different mesh, same conclusion within a factor
of three. The finding is a property of meshing a curved boundary with
conforming tetrahedra, not an artefact of one fixture.

P16 also recorded WHERE it comes from, and traced it to a single number: the
bored block's worst volume dihedral is 1.230616 deg and its boundary's worst
triangle angle is 1.230616 deg — the same angle. A tetrahedron must conform to
the boundary triangles it is built on, so a thin boundary triangle forces a
thin element. **The sliver is a consequence of the surface triangulation of a
curved face, and refusing it would refuse curved geometry.**

## Finding 4 — the kernel's accuracy DOES degrade, and that is where the threshold comes from

Findings 1 to 3 rule out choosing a threshold by comparing fixtures. They do
not rule out choosing one by asking what the kernel actually loses as an
element degrades, which is a different question with a measurable answer.

So it was measured: a known linear displacement field imposed on P16's
flattening wedge, walked over sixteen orders of magnitude of radius ratio, with
the strain recovered through the production `recoverElementFields` path and
compared against the field's analytic gradient. Two fields, three orders of
strain apart, as a control.

```text
relative strain error  ~  C / sqrt(3r/R)      C between 2.59e-16 and 2.41e-15
```

The fit constant holds inside one order of magnitude while the error itself
moves through eleven — so the degradation is a property of the kernel and
double precision, not of the problem. Full table, both fields and the
derivation in [ACCURACY_LAW.md](ACCURACY_LAW.md).

**Two earlier drafts of this document were wrong here, and the measurement
caught both** ([PRIOR_DRAFT_CORRECTION.md](PRIOR_DRAFT_CORRECTION.md)). The
first claimed Tet4 reproduces a linear field EXACTLY at every shape and that no
accuracy threshold was derivable. The second derived ONE threshold from the law
above and would have accepted a needle whose recovered strain was wrong by
2.5e-09 — because **a second degeneration family obeys a different law**:

```text
FLATTENING, toward a plane    err ~ C / sqrt(3r/R)   C 2.59e-16 .. 2.41e-15
STRETCHING, toward a line     err ~ C * aspect       C 1.76e-16 .. 4.45e-16
```

Neither metric sees the other's family. A flattening element's aspect ratio
saturates at sqrt(3); a stretching element's radius ratio understates its error
by 38x — a needle at 3r/R 1.73e-03 errs 4.45e-13 while a wedge at a WORSE
9.0e-04 errs only 1.17e-14. So the policy carries TWO hard-fail bounds, one per
family, each the inverse of its own law at BetterCAD's 1e-9 geometric band, and
each verified on both sides of where it lands.

What a degenerate element ALSO damages is the GLOBAL conditioning of `K`, and
that is not a shape question with a shape answer: it depends on the whole
assembled system, not on any one element. P17-SOLVE-001 already measures it
directly, as `min|D| / max|D|` of the factorisation, and refuses the solve on
it. The gates are independent and neither substitutes for the other.

## What the policy therefore is

Stated here because it is a consequence of the findings above and not of a
preference. The reasoning is ADR-042's; the numbers are this document's.

```text
QUALITY HARD FAILURE THRESHOLDS:  IMPLEMENTED, one per degeneration family

                                  3r/R   < 1e-10
                                  aspect > 3e5

   DERIVED, not chosen: each is the inverse of its own MEASURED law,
   evaluated at BetterCAD's 1e-9 geometric-accumulation band, with a 17x
   margin on the flattening crossing and 7.5x on the stretching one. They
   sit 3.2e6 and 3.7e3 clear of the worst element of any qualified reference
   mesh, so neither refuses anything BetterCAD produces, and each refuses
   elements whose recovered strain is measurably outside the requirement.
   Verified on both sides. See ACCURACY_LAW.md.

   TWO BOUNDS AND NOT ONE, because each metric is blind to the other's
   family -- which was measured after a one-bound policy had already been
   written and shipped in this document.

   AND NOT THREE. There is no hard-fail bound on either dihedral angle,
   because no law was measured against them and Findings 1 to 3 show they
   cannot separate a qualified cylinder from a sliver. A bound with nothing
   behind it would be the invented number this evidence exists to avoid.
   A 0.198-degree sliver PASSES both hard fails, correctly: its measured
   strain error is 1.33e-13.

QUALITY WARNING THRESHOLDS:  IMPLEMENTED, derived from this table

   Set at the edge of the measured qualified envelope, rounded so that no
   qualified reference model warns. The warning means exactly one thing, and
   says it: "this element is outside the range of every mesh BetterCAD has
   qualified." It is an envelope, not an engineering judgement, and it
   rejects nothing.

STRUCTURAL INVALIDITY:  HARD FAILURE

   An inverted, degenerate or non-finite element is not a bad score, it is
   not a tetrahedron. P16 classifies it `Invalid`, which no threshold can
   produce, and P17 refuses the solve.
```

### The envelope numbers, and where each comes from

```text
metric                 direction        qualified worst       warning   failure
TetRadiusRatio         HigherIsBetter   0.000322641 (RM-05)   3.0e-4    1e-10
TetAspectRatio         LowerIsBetter    80.0062     (RM-05)   81.0      3e5
TetMinDihedralAngle    HigherIsBetter   0.006330012 (RM-02)   6.0e-3    none
TetMaxDihedralAngle    LowerIsBetter    3.119060453 (RM-05)   3.125     none
```

Each WARNING bound is the worst qualified value, rounded AWAY from the
qualified set so that comparison cannot turn on a last-bit difference between
this measurement and the next build's. Nothing was rounded toward the
pathologies to make a fixture warn. The angle bounds are radians.

Each FAILURE bound comes from a different place entirely: its family's measured
accuracy law, inverted at 1e-9. The two columns must not be confused, and the
gap between them is the point -- a warning fires at the edge of BetterCAD's own
output, while a refusal fires three to six orders beyond it.

`failure` is absent for both dihedral angles -- `std::optional`, stated rather
than signalled by a sentinel -- which is how "no hard-fail bound on this
metric" is expressed in the type rather than in a comment.

### What the envelope warning can and cannot see

Recorded because it is a real limitation and it follows from Finding 2:

```text
SEES      the 0.01 mm wedge
            3r/R 8.99995e-06 < 3.0e-4                              WARNS
            min dihedral 0.003464095 rad < 6.0e-3 rad              WARNS

DOES NOT  the 0.1 mm wedge, a 1.98 deg sliver
SEE         3r/R 8.99946e-04 > 3.0e-4                              silent
            min dihedral 0.034627158 rad > 6.0e-3 rad               silent
```

**The envelope cannot flag a 2-degree sliver, and no threshold could**, because
the qualified cylinder is worse than one on both metrics. Saying so is the
point of this document. A policy that appeared to catch it would be a policy
tuned until a chosen fixture failed, and the same line would then refuse the
cylinder BetterCAD itself produces.

## What was NOT concluded from this table

Guarding against the reading this document most invites:

```text
"BetterCAD's meshes are bad"
    NO. They conform to the surface triangulation of a curved face, which is
    what a conforming tetrahedral mesher does. P16-QUALITY-001 reached the
    same measurement and recorded the same conclusion: it is "not a
    P16-QUALITY defect, and it is not a licence to pick a lenient threshold".

"the policy is lenient because the meshes are bad"
    NO. The policy is strict about everything that is DECIDABLE -- structural
    invalidity, staleness, material completeness, constraint sufficiency,
    residual, equilibrium, finiteness -- and silent about the one thing that
    is not. Relaxing a decidable gate to compensate would be the failure
    CLAUDE.md names: "never relax a gate to obtain one".

"a SHAPE-QUALITY hard-fail could be added on the dihedral angle too"
    NOT FROM THIS EVIDENCE. It would need a measured law relating the
    dihedral angle to something a solve loses, and Findings 1 to 3 show the
    dihedral angle cannot even separate a qualified cylinder from a sliver.
    The mechanism is there -- a `QualityThresholds` value with `failure`
    left absent -- so adding one later is filling in an optional rather than
    adding machinery. What it needs is the measurement, and that is a
    milestone of its own.
```
