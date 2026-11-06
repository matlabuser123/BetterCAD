# P19-GRAD-001 — the pre-implementation experiments

**These exist because an adversarial review found that they did not.** ADR-070's
founding measurement — the identity `sum_f C_f (x) S_f = V I` to 2.900e-16 over
20,000 random tetrahedra, and the eight-row skewness table that discharges the
checklist's "least-squares gradient **if needed**" — was quoted in the ADR with
`grep -rn 20261107` returning exactly one hit: the sentence claiming the seed was
recorded. No script, no log. `CLAUDE.md`: a claim with no log behind it is a
failed gate.

Both scripts are committed here with their output, and both are deterministic:
the seed is `20261107`, set in the first lines.

```text
01-identity-and-skewness   the identity over 20,000 random tetrahedra, then
                           Green-Gauss with the true face value, Green-Gauss with
                           the usual interpolation, and least squares, against an
                           exactly linear field at five skewness levels
02-shape-versus-skewness   the same, with the sample split by cell shape
                           (3 r_in / r_circ), because the first run's worst-case
                           column conflated two causes: SKEWNESS causes the
                           interpolation error and CELL SHAPE amplifies it, and
                           only the second is a mesh-quality question
                           P19-VALID-001 owns
```

Run them with `python -I <script>`. They need nothing but the standard library.

## What they establish, and what they do not

They establish that **Green-Gauss itself is linear-exact on any mesh** when given
the true value at a face centroid — the identity is why — so every departure from
exactness it shows belongs to the interpolation, which is exact exactly where
skewness is zero. At the 6.5 skewness ADR-065 measured on RM-MESH-03 that costs
590 % of the gradient's own magnitude on an otherwise well-shaped cell. No P16
reference mesh has zero skewness, so least squares is needed.

They do **not** establish anything about the operator: they are arithmetic on
synthetic tetrahedra, run before any C++ existed. The same claims are asserted
against the real geometry and the eight reference bodies by
`tests/cfd/CfdGradientTests.cpp`, and those are the measurements the evidence
rests on.
