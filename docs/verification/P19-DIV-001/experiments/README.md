# P19-DIV-001 — the pre-implementation experiment

One script, committed with its output, run **before any C++ existed** so the
architecture was chosen on measurement rather than on expectation. It is
deterministic: the seed is `20261107`, set in the first lines and printed in the
output, and the generator is Python's Mersenne Twister.

```text
01-cancellation-and-closure.py   356 lines
01-cancellation-and-closure.out   50 lines
```

Four questions, each with an ADR-071 decision riding on it. 20,000 random
meshes per question.

```text
Q1  does the sign flip change a bit?   (-S).U == -(S.U)
    200,000 trials, 0 mismatches, worst relative gap 0.000e+00
    -> Decision 2: an internal face's two contributions cancel BIT-FOR-BIT, so
       a face-centred scatter is a conservation GUARANTEE and not a hope

Q2a per internal face, owner + neighbour == 0.0?
    20,000 of 20,000 bit-exact (100.00%)
    -> AND IT IS A TAUTOLOGY. See below

Q2b globally, the sum over cells against the boundary sum?
    8,067 of 20,000 bit-exact (40.34%), worst relative imbalance 1.125e-16
    -> Decision 3: the global imbalance is MEASURED and reported, never
       asserted to be zero

Q3  closure, and the constant mode
    sum_f S_f bit-zero on 972 (4.86%); constant-field divergence bit-zero on
    4,827 (24.14%)
    -> Decision 8: the summary carries the measured `worstClosure`, because a
       `constantModeIsExact` flag would have been FALSE as derived

Q4  a linear field with the TRUE face value, against trace(A)
    worst 2.966e-16 relative to sum|S||U|/V   (eps = 2.220e-16)
    -> linear-exact to rounding, so every departure on a real mesh belongs to
       the INTERPOLATION and not to the surface sum
```

## Two things this experiment got wrong, kept on the record

**ADR-071's first draft predicted a bit-exact global sum. Q2b says 40.34%.** A
test asserting a bit-zero global imbalance would therefore have FAILED on a
correct operator. The ADR carries an amendment recording the prediction and its
withdrawal. This is the measurement changing the design, which is why the script
was run first.

**Q2a as written is a tautology.** Its predicate is `flux + (-flux) == 0.0`,
true for every finite double, so the 100.00% is not a measurement — it was found
by re-reading the script after the run, which is late. What genuinely
establishes pairwise cancellation is **Q1**, a claim that could have come out
false. Q2a's remaining value is as a check on the experiment's own mesh builder:
that each internal face appears exactly once with an owner and a neighbour. The
line is kept and labelled rather than deleted, **because the committed output
must match the committed script.**

An independent review later found the same tautology in the suite's own
cancellation case, which the author had not followed through from here — finding
B2 in [../README.md](../README.md).

**And the first draft of the script itself was wrong**, in the way P19-GRAD-001
finding r8 describes: it divided by `max(|boundary_total|, 1e-300)` and reported
a worst gap of `9.992e+284` for arithmetic correct to half an eps. The
denominator is `sum_f |flux_f|` now — the magnitude of the terms accumulated,
never the answer.

## Reproducing

```text
python experiments/01-cancellation-and-closure.py
```

No dependency beyond the standard library. The output is byte-reproducible from
the recorded seed.
