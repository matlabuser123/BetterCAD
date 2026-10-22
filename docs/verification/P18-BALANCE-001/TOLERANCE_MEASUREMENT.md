# P18-BALANCE-001 — tolerance measurement

```text
SUBJECT:  the balance tolerance is derived from measured rounding, not chosen; every number below is printed by the balance suite and read back from the qualification logs
POLICY:   |B| <= 1.0e-12 W + 1.0e-13 * (gross + arithmetic)         (kBalanceAbsoluteToleranceW, kBalanceRelativeTolerance)
MEASURED: worst |B| / (gross + arithmetic) = 1.7e-16  (transient insulated generation, worst of 15 steps)
MARGIN:   583x on the ratio; at least 584x on the allowed absolute residual in every case
PRESETS:  debug-ext, release-ext, debug-shared-ext — every measured |B| is bit-identical in all three
```

## What is measured

For every real-mesh and hand-network case the suite prints one `measure` line per balance (or the worst of a step history):

* `gross` — the sum of the magnitudes of every individual heat rate (each load, each convection condition, each reaction, the storage rate);
* `arithmetic` — `sum |K||T| + sum |C||dT|/dt + sum |Q|`, the size of the numbers the residual is formed from, which is what rounding scales with;
* `|B|` — the balance residual `-sum(r_free) + attribution defects`;
* the ratio `|B| / (gross + arithmetic)`.

The policy is `|B| <= 1e-12 W + 1e-13 * (gross + arithmetic)`. A balance PASS is never a solve PASS: the tolerance only judges the accounting of an already accepted solution.

## The table (worst first; identical in the three presets)

| Case | gross [W] | arithmetic [W] | `\|B\|` [W] | `\|B\|/(gross+arith)` | allowed [W] | allowed / `\|B\|` |
| --- | --- | --- | --- | --- | --- | --- |
| transient insulated generation, worst of 15 steps | 4.800E+000 | 1.109E+004 | 1.902E-012 | 1.72E-016 | 1.110E-009 | 584 |
| steady generation 2.4 W vs convection | 4.800E+000 | 1.368E+004 | 2.980E-013 | 2.18E-017 | 1.370E-009 | 4,596 |
| transient fine cylinder, worst of 6 steps | 9.364E+000 | 4.701E+006 | 1.187E-011 | 2.52E-018 | 4.701E-007 | 39,604 |
| large cancellation +1e6 W / -1e6 W + 5 W | 2.000E+006 | 2.415E+003 | 0 (exact) | 0 | 2.002E-007 | not finite |
| steady fine cylinder | 9.448E+000 | 4.712E+006 | 9.870E-012 | 2.10E-018 | 4.712E-007 | 47,744 |
| steady slab 400/300 K | 5.400E+002 | 1.281E+004 | 4.832E-013 | 3.62E-017 | 1.336E-009 | 2,765 |
| fixed temperature + convection overlap | 5.464E+002 | 1.282E+004 | 3.695E-013 | 2.76E-017 | 1.338E-009 | 3,621 |
| two additive convection conditions on one face | 4.800E+000 | 1.271E+004 | 2.496E-013 | 1.96E-017 | 1.273E-009 | 5,099 |
| transient Dirichlet block, worst step | 5.400E+002 | 1.281E+004 | 6.221E-013 | 4.66E-017 | 1.336E-009 | 2,148 |
| transient convection cooling, worst of 10 steps | 5.574E+000 | 1.416E+004 | 6.404E-013 | 4.52E-017 | 1.417E-009 | 2,213 |
| steady surface heat 8 W vs 300 K cap | 1.600E+001 | 1.102E+004 | 4.103E-013 | 3.72E-017 | 1.104E-009 | 2,691 |
| steady combined, two caps + two convections + four loads | 3.865E+002 | 1.338E+004 | 2.425E-013 | 1.76E-017 | 1.377E-009 | 5,679 |

Twelve cases, three presets, 36 `measure` lines (`qualification/tolerance-measurements.txt`). For each case the three presets print the same gross, arithmetic, `|B|` and ratio to every digit shown, and the bit
fingerprints of the balances are identical (`qualification/postrun-summary.txt`).

## Reading the table

* **The worst ratio is `1.72e-16`**, about 0.8 of one unit of double-precision rounding (`2.2e-16`) applied to the arithmetic scale. That is what plain fixed-order summation of a handful of terms should produce, and it is the lower bound
  nothing can beat. The transient insulated case is the worst of the twelve; its small gross (4.8 W) against a large arithmetic scale (1.1e4 W) is the likely reason, and no further cause was investigated.
* **The policy `1e-13` is 583 times the worst ratio.** It is not a round number picked to pass: the first value (`1e-10`) was found, by this very measurement, to be about `10^6` above the error and loose enough to hide a `5e-5` relative
  attribution defect on the cylinder (ADVERSARIAL_REVIEW.md, F2). It was tightened, and the margin is asserted in every real-mesh case so it cannot drift back.
* **The absolute floor (`1e-12 W`) is not what makes any case pass.** Even the smallest case allows `1.1e-9 W`, a thousand times the floor; the floor exists only for an all-zero body, where the scale is zero (`ZeroHeatCase`: the normalised residual is exactly 0).
* **Margins are 584x and above in every case on the allowed absolute residual,** not only on the ratio, so no case passes by a narrow margin.
* **The large-cancellation case** (+1e6 W and -1e6 W cancelling to a 5 W net) is the trap the brief names: normalising by the net would make an honest `1e-10 W` rounding look enormous; normalising by the sum of magnitudes (`2.0e6 W` gross) allows `2.0e-7 W`.
  The balance closes to exactly 0 here; the case proves the scale, not the rounding (BAL18 kills a category-level gross, which would have been tiny).
* **The cylinders** have the largest arithmetic scale (`4.7e6 W`) and the largest `|B|` (`1.2e-11 W`), yet the smallest ratio (`2.5e-18`): rounding grows with the arithmetic scale, which is why the policy is relative to it.

## What the tolerance does and does not claim

| Claim | Status |
| --- | --- |
| rounding in the accounting is far below the policy for the qualified cases | measured (above) |
| a 5e-5 relative attribution defect (a skipped or doubled load) is visible | tested — BAL16, BAL17, BAL20, `AttributionMustAccountForTheHeatInput` |
| the policy is the same in the three presets | measured: the same numbers, the same bits |
| a wrong free-field solution is rejected by the balance | **not claimed** — the balance can cancel large local errors; the accepted-solution type is the gate (ADVERSARIAL_REVIEW.md, question 21) |
| a model with a much larger rounding scale is judged correctly | by construction: the arithmetic term grows with it; not measured beyond `4.7e6 W` |
| accuracy of the physics | not claimed — P18-VALID-001 |

The three constants are pinned by value in `TolerancePolicyConstantsArePinned` and by `P18_BALANCE_TOLERANCE_FINGERPRINT` at the freeze (FREEZE.md).
