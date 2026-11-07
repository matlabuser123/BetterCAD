# P19-CONV-001 — pre-implementation and post-review measurements

Five scripts, run with fixed seeds, measuring the arithmetic this milestone's
design rests on. Four of the five exist because a claim turned out to be wrong,
and each is kept with its output rather than replaced, so the corrections are
readable.

`P19-DIV-001`'s first ADR draft predicted a bit-exact global sum and was wrong.
Predicting again would have been the same mistake twice, so every numerical
claim in `ADR-072` was measured before the C++ existed — and then, after two
independent reviews, two of those claims turned out to have been measured
against a model narrower than the claim.

## The scripts

| script | seed | what it measures | output |
| --- | --- | --- | --- |
| `conv_experiment.py` | 202611072 | Q1 the per-face upwind decomposition; Q2 the row-sum oracle; Q3 reverse flow; Q4 boundedness | `conv_experiment.out` |
| `conv_experiment2.py` | 202611072 | the same four, with three of revision 1's own defects corrected | `conv_experiment2.out` |
| `conv_experiment3.py` | 202611073 | the row-sum identity with the boundary POLICY distinguished | `conv_experiment3.out` |
| `conv_q5.out` | — | per-scheme exactness of a face's two coefficients | `conv_q5.out` |
| `conv_experiment5.py` | 202611075 | the two identities an independent review found false as published | `conv_experiment5.out` |

## What each revision got wrong

**Revision 1** reported a Q2 "relative gap" of `4.34e+13` for arithmetic that
is correct to half an eps. That figure is **withdrawn**, and the cause was a
wrong oracle rather than a rounding problem: it compared the matrix row sum
against the divergence of `phi`, which are different quantities, because at an
inflow boundary face the convective flux is a SOURCE and never a coefficient. A
large real difference divided by a small row magnitude produced the absurd
number. `ADR-072` Decision 5 exists because of this.

Revision 1 had two further defects of its own:

- Q4's `worst` was assigned only inside the `> 1e-14` branch, so "worst
  relative departure: 0" meant *nothing exceeded the threshold* and read as
  *always exactly equal*. A variable that records only failures, printed as if
  it were the worst case, is the same family of defect as a hand-typed count
  standing beside a computed one — which `P19-DIV-001` recorded in its own
  tooling.
- Q3b drew fluxes across twelve orders of magnitude, so its relative figure
  measured CONDITIONING rather than the identity.

**Revision 2** fixed those three and reported sane figures throughout — every
relative gap at half an eps. Its row-sum term was still `sum_boundary
min(phi,0)`, all inflow.

**Revision 3** found that term wrong one level deeper, while the header was
being written. An inflow face leaves the matrix only when the TRANSPORTED
VARIABLE is `FixedValue`; at a `PressureInlet` the `U` column is
`ZeroGradient`, so the face value is the owner's and that face's negative `phi`
stays in the matrix. Measured against mixed boundary policies, the all-inflow
form is exact on 10.26% to 11.96% of meshes and departs by a relative `0.9995`
on 20,815 of 139,802 cells — a finite, structural error. The corrected
`prescribedInflow` term is exact to half an eps on the same meshes. Revision
2's figures are not withdrawn: they were measured with every boundary face
implicitly `FixedValue`, where the two formulas coincide.

**Q5** was added when the implementation made the per-scheme difference
visible: upwind writes a share of exactly `0` or `1`, so a face contributes its
flux bit-exactly, while central sums two rounded products and need not recover
`phi`. Measured over 200,000 fluxes: upwind `200,000/200,000` bit-exact, worst
relative `0`; central `178,457/200,000` (89.23%), worst relative `2.22e-16`.
`ADR-072` Decision 5's "scheme-independent" claim is true of the mathematics
and false of the arithmetic, and now says so.

**Revision 4** exists because two independent adversarial reviews, run before
the qualification freeze, found that `ADR-072` Decisions 7 and 8 published
identities that are false as written — and that the measurements behind them
could not have been of this operator.

```text
Q6  A(-phi) == A(phi)^T - diag(div phi)
    Upwind   FixedValue boundaries only    19.25% bit-exact, worst rel 4.36e-16
    Upwind   ZeroGradient boundaries        0.00%             worst rel 1
    Upwind   mixed                          4.10%             worst rel 1
    Central  any policy                     0.00%             worst rel 2.1 to 2.7

Q7  upwind dominance deficit
    FixedValue only    published -div       8.82% worst 1
                       corrected           32.23% worst 3.74e-16
    ZeroGradient only  published           17.35% worst 2
                       corrected           17.35% worst 2
```

The transpose identity was derived from the upwind scatter rule — reversing a
face's flux swaps the two cells' roles — and then stated unqualified. A central
share is geometric and does NOT swap, so the identity fails by `phi(2w-1)` per
face; and a `ZeroGradient` or `Symmetry` boundary face carries its full `phi` on
the diagonal, so reversing the sign flips that term instead of removing it,
failing by `phi_b`. Revisions 1 to 3 had no central branch at all and modelled
every boundary face as `A[p][p] += max(q,0)`, which is the `FixedValue` rule —
so "general mesh ... worst relative `4.93e-16`" overstated what had been
measured.

The dominance identity was missing the `prescribedInflow` term, which is
Decision 5's own correction repeated in the neighbouring decision one paragraph
later. Revision 1's Q4 ran with NO BOUNDARY FACES, where the two forms
coincide.

**And revision 4 found a third condition neither the review nor the ADR had
stated.** The deficit is defined with `|a_ii|`, so once backflow at a
zero-gradient boundary drives the diagonal negative the identity breaks
whatever the inflow term is — which is why both forms sit at 17.35% in the
zero-gradient column. The scope is therefore `a_ii >= 0`, i.e. no backflow, and
`CfdConvectionSummary::backflowFaces` is not a diagnostic beside the identity:
it is the identity's validity condition.

## How to re-run

```text
python -I conv_experiment.py
python -I conv_experiment2.py
python -I conv_experiment3.py
python -I conv_experiment5.py
```

No argument, no environment, no dependency beyond the standard library. Each
prints its seed first. Nothing here reads the BetterCAD source: these are
models of the arithmetic, which is what lets them disagree with it.

## What these scripts are NOT

They are not a test of the operator. They model the assembly's arithmetic in
Python to decide what the C++ may assert and with what tolerance; the operator
itself is tested in `tests/cfd/CfdConvectionTests.cpp`, and the agreement
between the model and the code is established there and not here. A figure in
this directory justifies a tolerance; it never stands in for a passing test.
