# Validation Template

One row per quantity. The independent source is the point: an expected value
that BetterCAD computed is not validation, it is a tautology. Closed forms
where they exist (box `V = LWH`, cylinder `V = πr²h`, Pappus for sweeps and
revolutions, the prismatoid formula for lofts, Green's theorem for sections);
otherwise a published benchmark, a conservation law, a manufactured solution,
or an independent implementation.

Where a full closed form is impractical, decompose the model into pieces that
have one, and validate the pieces and the differences between stages. Where
even that is impossible, validate invariants and say so plainly.

| Quantity | Independent source / equation | Expected | Measured | Abs error | Rel error | Tolerance | Result |
| --- | --- | --- | --- | --- | --- | --- | --- |
|  |  |  |  |  |  |  |  |

Every tolerance needs a technical reason, recorded next to it: rounding for
well-conditioned double-precision algebra, geometric accumulation, a stated
kernel approximation. A tolerance is never widened to obtain a pass. If a
measurement does not meet its tolerance, the finding is the measurement, not
a looser bound.
