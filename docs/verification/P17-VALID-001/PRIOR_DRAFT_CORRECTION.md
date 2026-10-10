# P17-VALID-001 — three claims this milestone made and then disproved

Recorded because each was written into this milestone's own code or evidence
before it was checked, and the check contradicted all three. CLAUDE.md's rule is
that documentation follows evidence and that when a document and the evidence
disagree, the evidence is right. These are those corrections, kept rather than
quietly overwritten.

**The first two were caught by turning a claim into a measurement; the third by
re-reading the code and asking what one accessor actually returns.** None was
caught by re-reading the reasoning, which was plausible in all three cases.

## Correction 1 — "Tet4 reproduces a linear field exactly at every shape"

### What was claimed

An early draft of [QUALITY_DISTRIBUTIONS.md](QUALITY_DISTRIBUTIONS.md) carried
this as "Finding 4":

```text
Tet4's own consistency does NOT degrade with element shape.

  "A linear displacement field has constant strain, and Tet4's shape
   functions represent it exactly for ANY non-degenerate element geometry."

  "So there is no accuracy threshold derivable from the element kernel
   either."
```

and the policy section concluded `QUALITY HARD FAILURE THRESHOLD: NOT
IMPLEMENTED`.

### Why it was wrong

The first sentence is true in EXACT ARITHMETIC and false in double precision,
and the difference is the whole milestone. Tet4's shape functions do represent
a linear field exactly for any non-degenerate geometry — but `B` carries
`1 / (6V)` factors, and recovering a strain from nodal displacements that
differ by less and less as an element flattens is a cancellation. The algebra
is exact; the arithmetic is not.

The second sentence inherited the error: having concluded there was no
degradation, it concluded there was nothing to derive a threshold FROM.

### What the measurement said

```text
3r/R          relative strain error, field 0   field 1
1.0           8.13152e-16                      --
8.99995e-06   1.33176e-13                      5.66453e-13
9.0e-12       1.08912e-10                      5.83867e-10
9.0e-14       1.38789e-09                      2.80431e-09
9.0e-22       1.10120e-05                      8.03741e-05
```

Eleven orders of magnitude of movement. At the bottom of the range the
recovered strain is wrong by eighty parts per million while P16 still reports
the mesh structurally valid and the kernel still returns finite numbers.

So the correct reading was the opposite of the draft's: **because the
degradation is real and measurable, a threshold IS derivable.** One was
derived, at `3r/R < 1e-10`.

## Correction 2 — "one bound on the radius ratio is enough"

### What was claimed

Correction 1's fix shipped a one-bound policy, and ADR-042's first version
stated it as Decision 1:

```text
3r/R < 1e-10   ->   HARD FAILURE

no hard-fail bound on the aspect ratio
no hard-fail bound on either dihedral angle
```

with candidate D — "a second derived bound" — REJECTED on the grounds that "no
law was measured against the aspect ratio".

### Why it was wrong

The objection was correct and the conclusion did not follow. No law had been
measured against the aspect ratio because **only one degeneration family had
been measured at all.** The wedge family flattens toward a plane. A
tetrahedron can also stretch toward a line, and nothing in the evidence had
touched that case.

The adversarial review asked whether the fit constant was a property of the
KERNEL or of the WEDGE. That question is what produced the measurement.

### What the measurement said

A needle — an equilateral base shrunk while the apex stays 10 mm away:

```text
base (mm)   3r/R          aspect    error        err*sqrt(3r/R)   err/aspect
10          0.977082      1.1547    2.71051e-16  2.67927e-16      2.35e-16
0.01        0.00173155    1000      4.44929e-13  1.85144e-14      4.45e-16
0.0001      1.73205e-05   100000    3.31965e-11  1.38157e-13      3.32e-16
1.0e-6      1.73205e-07   1.0e+07   2.52476e-09  1.05075e-12      2.52e-16
```

`err*sqrt(3r/R)` — Correction 1's law — climbs by a factor of **3900** across
this series. `err/aspect` is constant to within 2.5. **The needle obeys
`err ~ C * aspect`, a different law.**

And the decisive comparison:

```text
a NEEDLE at 3r/R 1.73155e-03   errs 4.44929e-13
a WEDGE  at 3r/R 8.99946e-04   errs 1.17455e-14    -- a WORSE radius ratio,
                                                      38x LESS error
```

So the radius ratio does not determine the error, and the one-bound policy
guaranteed an accuracy it did not deliver: the needle at aspect 1.0e+07 has a
radius ratio of 1.73205e-07, comfortably above the 1e-10 floor, and was
**ACCEPTED** with a recovered strain wrong by 2.52476e-09 — outside the stated
1e-9 requirement.

### What changed as a result

```text
a second hard-fail bound   aspect > 3e5, derived from the needle family's own
                           law at the same 1e-9 requirement, with a 7.5x
                           margin and two-sided verification
ADR-042 Decision 1         rewritten: two bounds, one per family, with the
                           reason neither is redundant
ADR-042 candidate D        REJECTED -> CHOSEN, and the reversal is recorded
                           with the measurement that caused it rather than
                           presented as the original intent
ADR-042 candidate E        added, holding the ORIGINAL objection for the
                           dihedral angles, where it still applies
the needle fixture         a permanent test, with the needle's law PINNED
                           between 1e-16 and 1e-15 and Correction 1's law
                           asserted to FAIL on it -- so the two-family
                           finding cannot silently regress into one law
```

## Correction 3 — "a contradictory quality policy should refuse the solve"

### What was claimed

`ValidationCode::QualityPolicyUnusable` was a **refusal**, documented like this:

```text
The quality policy the report was produced under is self-contradictory, so
no verdict was reached. Reporting a pass nobody can justify is worse than
reporting that the question could not be answered.
```

### Why it was wrong

The reasoning conflated P16's verdict with P17's. P16 does skip its
classification when handed a contradictory policy — but it still **measures
every metric** and reports it as under the report-only default. This module
reads `summaries`, not P16's classifications, and its own policy is fixed and
validated by `PolicyIsAcceptedByP16sOwnPolicyChecker`. So everything needed to
reach a structural verdict is present and valid.

Refusing therefore blocked a structurally sound, accurate mesh because of an
unrelated error in the user's **reporting preferences** — the false rejection
the brief warns against, and a contradiction of this milestone's own ADR-042
Decision 7, which says P16 stays report-only and P17 does not reach into its
verdict.

### And it was reachable from an ordinary document

This is what moved it from a wart to a defect. A `MeshControl` carries its own
`QualityThresholds`, and the mesher evaluates the held report under them:

```cpp
held.quality = evaluateMeshQuality(held.mesh.mesh(), definition.quality);
```

So a user who puts a bound on a dimensioned size, or orders two bounds the
wrong way round, would have had their structural solve refused — on a mesh
whose metrics were fine.

### What changed as a result

```text
severityOf                 QualityPolicyUnusable -> Warning
the mesh stage's           was "any finding short-circuits", which would have
short-circuit              stopped the summary read on a mere warning. Now it
                           short-circuits ON A REFUSAL ONLY.
the diagnostic             now says the structural verdict is unaffected and
                           why, instead of implying no verdict was reached
the test                   RefusesAReportProducedUnderAContradictoryPolicy
                           -> WarnsAboutAContradictoryPolicyWithoutRefusing,
                           which asserts the metrics are still read AND that a
                           genuinely bad element under the same broken policy
                           is still refused -- so the warning cannot become an
                           excuse
```

### How it was caught

By re-reading the staged chain after the freeze and asking what `model->quality()`
actually is — the **mesher's** report, produced under the **mesh control's**
policy, not under P17's. The qualification run was 30 minutes in and was
stopped and voided, because CLAUDE.md is unambiguous on both halves: a credible
defect found in review is resolved before `[x]`, and source changed after the
freeze voids the qualification.

**The adversarial review had asked the right question and accepted the wrong
answer.** Its section 10 asks "did we cross an architectural boundary", and the
answer recorded was "P16 untouched — zero meshing source files changed". That
is true and it is not the whole question: nothing was written to P16, but P17
was *reading P16's policy verdict and treating it as its own*. A boundary can
be crossed by reading.

## What was NOT affected

Findings 1, 2 and 3 of [QUALITY_DISTRIBUTIONS.md](QUALITY_DISTRIBUTIONS.md)
stand unchanged, and they are what keeps both bounds from becoming the
shape-quality rejection line the brief forbids:

```text
no shape metric separates BetterCAD's qualified meshes from
   pathological-but-usable ones
the aspect ratio is INVERTED between those two sets
the radius ratio and minimum dihedral INTERLEAVE
```

Both hard-fail bounds sit three to six orders of magnitude clear of the worst
element of any qualified reference mesh, and a 0.198-degree sliver passes both.

## The pattern, for the next milestone

```text
a claim that sounds like a theorem, IS a theorem, and does not survive
floating point
    Correction 1. "Tet4 is exact for a linear field" is true, and the
    arithmetic had the last word.

a constant measured on one case, generalised to every case
    Correction 2. The law was real; its universality was assumed.

an accessor whose name says less than its provenance
    Correction 3. `model->quality()` is a MeshQualityReport, and the
    question that mattered was whose policy produced it. A boundary can be
    crossed by READING, not only by writing, and "zero files changed in
    that module" does not answer it.
```

P17-REACTION-001 recorded the mirror image of Correction 1 — an expectation
that the geometric path would NEED a looser tolerance, refuted because a planar
face integrates exactly. In all three cases the reasoning was sound, the
measurement disagreed, and the measurement was right.

The specific lesson from Correction 2 is narrower and more useful: **when a
threshold is derived from a fitted constant, the fit must be measured on more
than one way for the input to go bad.** Asking "is this constant a property of
the thing I am measuring, or of the fixture I measured it with" is what the
adversarial gate is for, and here it changed the shipped policy.
