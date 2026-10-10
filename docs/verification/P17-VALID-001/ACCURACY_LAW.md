# P17-VALID-001 — the measured accuracy laws, and the two hard-fail thresholds derived from them

**The quality hard-fail thresholds in this milestone are derived from
measurement, and this document is that measurement.** Neither is a number from
intuition, from a standard nobody in this repository can check, or tuned until
a chosen fixture failed. Each is the inverse of a measured law evaluated at a
stated accuracy requirement, and each is verified against measurements on BOTH
sides of where it lands.

**There are TWO of them, and the second exists because adversarial review
disproved a one-bound policy this milestone had already written.** That is
recorded in full below rather than quietly fixed.

Raw logs: [logs/accuracy-law.log](logs/accuracy-law.log),
[logs/needle-law.log](logs/needle-law.log). Test:
`StructuralValidation_MeasuresHowKernelAccuracyDegradesWithElementShape` in
`tests/reference/StructuralValidationSweepTests.cpp`.

## Why a measurement was needed at all

[QUALITY_DISTRIBUTIONS.md](QUALITY_DISTRIBUTIONS.md) establishes that no shape
metric separates BetterCAD's qualified meshes from deliberately pathological
ones: on aspect ratio the two sets are inverted, and on radius ratio and
minimum dihedral they interleave across three orders of magnitude. A qualified
cylinder is worse than a 2-degree sliver.

That rules out choosing a threshold by comparing fixtures. It does not rule out
choosing one by asking what the SOLVER actually loses as an element degrades —
a different question, with a measurable answer that is a property of BetterCAD
rather than of a fixture set.

## The measurement

A linear displacement field `u = A x + b` is imposed on one tetrahedron and the
strain is recovered through the production kernel, `recoverElementFields` — the
same function `recoverFields` calls once per element, so this is the path a
solve takes. A linear field has constant strain, so the expected value is the
field's own analytic gradient, written down by hand:

```text
exx = a1   eyy = b2   ezz = c3   gxy = a2+b1   gyz = b3+c2   gzx = c1+a3
```

Nothing in the oracle calls `B`, `strainFrom` or any kinematics. All six
components are nonzero, asserted rather than assumed. Two fields three orders
of strain apart are used as a control on the fit constants.

**TWO DEGENERATION FAMILIES ARE MEASURED,** and that is the heart of this
document:

```text
FLATTENING, toward a plane
    P16's own wedge: a 10 mm equilateral base with the apex brought down.
    `3r/R` goes as the SQUARE of the apex height.

STRETCHING, toward a line
    a needle: an equilateral base shrunk while the apex stays 10 mm away.
    The aspect ratio goes as the INVERSE of the base.
```

### The flattening family

```text
apex (mm)    3r/R          field 0 error    field 1 error    f0 err*sqrt(3r/R)   f1 err*sqrt(3r/R)
1.0e-2       8.99995e-06   1.33176e-13      5.66453e-13      3.99527e-16         1.69935e-15
1.0e-4       9.0e-10       9.43603e-12      4.06332e-11      2.83081e-16         1.21900e-15
3.0e-5       8.1e-11       3.78577e-11      2.87557e-11      3.40720e-16         2.58801e-16
1.0e-5       9.0e-12       1.08912e-10      5.83867e-10      3.26736e-16         1.75160e-15
1.0e-6       9.0e-14       1.38789e-09      2.80431e-09      4.16367e-16         8.41294e-16
1.0e-8       9.0e-18       1.46528e-07      7.93283e-07      4.39584e-16         2.37985e-15
1.0e-10      9.0e-22       1.10120e-05      8.03741e-05      3.30359e-16         2.41122e-15

every row:  structurallyValid YES, invalidElements 0, kernel OK
```

Plus the mild end: a regular tetrahedron at 8.13152e-16, and wedges at apex
10.0 / 1.0 / 0.1 mm giving 2.71051e-16 / 1.98770e-15 / 1.17455e-14.

```text
LAW 1    err <= C / sqrt(3r/R)      C between 2.58801e-16 and 2.41122e-15
         over SIXTEEN orders of radius ratio and two fields
```

The last two columns are the fit constant, printed so a reader can watch it
hold rather than take the fit on trust: it stays inside one order of magnitude
while the error itself moves through eleven.

**The ASPECT RATIO sees nothing of this family.** It saturates at
`sqrt(3) = 1.7320508` as an element flattens, so the worst row above — whose
recovered strain is wrong by eighty parts per million — scores 1.73205.

### The stretching family

```text
base (mm)   3r/R          aspect    error        err*sqrt(3r/R)   err/aspect
10          0.977082      1.1547    2.71051e-16  2.67927e-16      2.35e-16
1           0.167718      10.0167   1.76183e-15  7.21529e-16      1.76e-16
0.1         0.01727       100.002   4.07931e-14  5.36084e-15      4.08e-16
0.01        0.00173155    1000      4.44929e-13  1.85144e-14      4.45e-16
0.001       0.0001732     10000     2.33063e-12  3.06723e-14      2.33e-16
0.0001      1.73205e-05   100000    3.31965e-11  1.38157e-13      3.32e-16
1.0e-5      1.73205e-06   1.0e+06   2.51021e-10  3.30362e-13      2.51e-16
1.0e-6      1.73205e-07   1.0e+07   2.52476e-09  1.05075e-12      2.52e-16
```

```text
LAW 2    err <= C * aspect          C between 1.76e-16 and 4.45e-16
         over SEVEN orders of aspect ratio
```

`err/aspect` is constant to within a factor of 2.5 across the whole series,
while `err*sqrt(3r/R)` — Law 1's constant — climbs by a factor of **3900**.
Law 1 does not describe this family at all.

## The finding that forced a second bound

**The radius ratio does not determine the recovered-strain error.** Two
elements with comparable radius ratios have errors 38 times apart:

```text
a NEEDLE at 3r/R 1.73155e-03   errs 4.44929e-13
a WEDGE  at 3r/R 8.99946e-04   errs 1.17455e-14      -- a WORSE radius ratio,
                                                        and 38x LESS error
```

So a single radius-ratio bound cannot guarantee an accuracy. An earlier draft
of this milestone derived exactly one bound, from Law 1, and shipped it:

```text
3r/R < 1e-10   ->   HARD FAILURE        (the one-bound policy, WRONG)
```

Under that policy the needle at aspect 1.0e+07 — radius ratio 1.73205e-07,
comfortably above the floor — was **ACCEPTED**, with a recovered strain wrong
by 2.52476e-09, outside the stated 1e-9 requirement. The policy guaranteed an
accuracy it did not deliver.

The error was not in the arithmetic. It was in generalising a constant measured
on one degeneration to every degeneration. The needle family was measured
BECAUSE the adversarial review asked whether the constant was a property of the
kernel or of the wedge, and the answer was the wedge.

## The derivations

```text
                          THE REQUIREMENT, for both
                          err <= 1e-9
                          BetterCAD's documented band for geometric
                          accumulation (CLAUDE.md: "1e-12 for well-conditioned
                          double-precision algebra, 1e-9 for geometric
                          accumulation"). A recovered strain is a gradient of a
                          field over element geometry, which is geometric
                          accumulation and not bare algebra.

FLATTENING                err <= C / sqrt(3r/R),  C <= 2.41122e-15
  invert                  3r/R >= (C / 1e-9)^2 = (2.41122e-06)^2 = 5.81398e-12
  choose                  3r/R < 1e-10  ->  HARD FAILURE
                          a 17x margin on the crossing, bounding the error at
                          2.41122e-10 -- 4.1x inside the requirement

STRETCHING                err <= C * aspect,      C <= 4.45e-16
  invert                  aspect <= 1e-9 / C = 2.247e+06
  choose                  aspect > 3e5  ->  HARD FAILURE
                          a 7.5x margin on the crossing, bounding the error at
                          1.335e-10 -- 7.5x inside the requirement
```

### Why these values and not others

```text
3r/R, NOT 5.8e-12 (the crossing itself) or 1e-11
    No margin, or 1.7x. The fit constant is measured to within a factor of
    nine and would move with a different FMA contraction, so a bound at the
    crossing would admit an element exceeding the requirement on the next
    compiler. P17-REACTION-001 rejected 1e-13 for leaving only 4x.

3r/R, NOT 1e-8 or larger
    Buys margin nothing needs and walks toward the qualified envelope for no
    measured reason.

aspect, NOT 1e5
    The needle fixture that MEASURES Law 2 has an aspect ratio of 100000.2 at
    a 1e-4 mm base. A bound at 1e5 would sit a fifth of a unit from a
    measurement -- a threshold whose verdict on its own evidence turns on
    rounding. It was tried, and the test caught it.

aspect, NOT 1e6
    Only 2.2x on the crossing.

aspect, NOT 1e3 or tighter
    Still far above anything qualified, but it starts refusing elements whose
    measured error is four orders inside the requirement, for no measured
    reason.
```

## Verification, in both directions, for both bounds

A threshold is only derived if the measurement confirms it where it lands. Both
sides of both bounds are measured, not interpolated.

```text
THE RADIUS FLOOR, 1e-10

  ACCEPTED, correctly
    3r/R 9.0e-10   (apex 1.0e-4 mm)   errors 9.44e-12 and 4.06e-11, inside

  REFUSED, correctly
    3r/R 8.1e-11   (apex 3.0e-5 mm)   errors 3.79e-11 and 2.88e-11 -- still
                                      inside 1e-9, so CONSERVATIVE, which is
                                      the direction a margin should err
    3r/R 9.0e-14   (apex 1.0e-6 mm)   errors 1.39e-09 and 2.80e-09, genuinely
                                      OUTSIDE 1e-9

THE ASPECT CEILING, 3e5

  ACCEPTED, correctly
    aspect 1.0e+05 (base 1.0e-4 mm)   error 3.32e-11, inside

  REFUSED, correctly
    aspect 1.0e+06 (base 1.0e-5 mm)   error 2.51e-10 -- still inside, so
                                      CONSERVATIVE
    aspect 1.0e+07 (base 1.0e-6 mm)   error 2.52e-09, genuinely OUTSIDE
```

`and the aspect ceiling lands where the measurement puts it, on both sides`
asserts the verdict AND the requirement for each of the three needles, so a
bound moved without re-measuring fails.

## And against everything legitimate

```text
worst element of any qualified P16 reference mesh
    3r/R 3.22641e-04      vs the floor   1e-10     margin 3.2e+06
    aspect 80.0062        vs the ceiling 3e5       margin 3.7e+03

the worst PATHOLOGICAL fixture that is still usable
    a 0.198-degree sliver, 3r/R 8.99995e-06, aspect 1.73205
    strain error 1.33e-13
    ACCEPTED by both bounds, correctly
```

**The 0.198-degree sliver passes.** That is the brief's requirement working as
intended — "do not turn every imperfect mesh into a hard failure" — and it is
only defensible because the measurement shows the kernel handles that element
to 1.3e-13. A policy that refused it would be refusing an element it can
actually solve.

## The laws are PINNED, because thresholds rest on them

```text
1e-16 < err*sqrt(3r/R) < 1e-14     every flattening row, 2.6x below and
                                   4.1x above the measured range
1e-16 < err/aspect     < 1e-15     every stretching row, 1.8x below and
                                   2.2x above
err*sqrt(3r/R) > 1e-14             asserted for every needle with aspect
                                   above 500 -- so Law 1 is held to FAIL here,
                                   which is what stops the two-family finding
                                   from silently regressing into one law again
```

A kernel whose gradients had drifted would leave a band while every absolute
error still looked small — the regression a bound on the error alone would
miss.

## Finding: nothing else in the stack refuses these elements

Every row of both tables, down to a radius ratio of **9.0e-22** and an aspect
ratio of **1.0e+07**, comes back `structurallyValid YES` with
`invalidElements 0`, and the kernel recovers a finite strain for all of them.

P16 is right not to refuse them: each has a positive finite signed volume and
every metric is finite, so it IS a tetrahedron, and P16's job is to measure it
rather than to judge it. The kernel is right not to refuse them: it returns
finite numbers, which is all it promises. **Nobody in the chain owned the
question "is this element accurate enough to solve on", because that question
belongs to the phase that consumes the mesh, and this is that phase.**

## What these thresholds are NOT

```text
They are not a mesh quality standard.
    They are accuracy floors. An element inside them may still be a terrible
    element, and the envelope WARNINGS exist to say so. Passing means "the
    kernel can recover a strain on this to better than 1e-9", and no more.

They are not bounds on the dihedral angles.
    No law was measured against either dihedral angle, and the measured
    distributions show they cannot separate a qualified cylinder from a
    sliver. A third bound with nothing behind it is the invented number this
    document exists to avoid.

They are not a claim about discretisation error.
    This measures the error in recovering a field the mesh represents
    exactly. How well a mesh of a given density approximates a field it
    CANNOT represent exactly is convergence, and it is a different question
    with a different answer, owned by nothing in P17.

They are not a conditioning check.
    A mesh of accurate elements can still assemble an ill-conditioned K.
    P17-SOLVE-001 measures that directly as `min|D| / max|D|` of the
    factorisation and refuses the solve on it. The gates are independent and
    neither substitutes for the other.

They are not proven complete.
    TWO degeneration families are measured, and two is what the geometry of a
    tetrahedron offers -- collapse toward a plane and collapse toward a line.
    A caps-shaped or slivered-needle hybrid is a combination of the two and
    is bounded by whichever bound it crosses first, but that is an argument
    and not a measurement, and it is recorded as such in
    KNOWN_LIMITATIONS.md.
```
