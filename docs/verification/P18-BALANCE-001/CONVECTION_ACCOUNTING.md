# P18-BALANCE-001 — convection accounting

```text
SUBJECT:  the heat a convecting face exchanges is Kconv T - Qconv, taken from the qualified terms; the ambient source alone is not a transfer
```

## The weak form decides it

P18-BC-001 (`WEAK_FORM.md`) writes the Robin condition `-k grad T . n = h (T - Tinf)` as two terms of the global system: `Kconv` (a W/K matrix added to `K`) and
`Qconv = h Tinf A / 3` per node (a W source added to `Q`). The heat the face actually exchanges with the air is the residual of that boundary term:

```text
Q_conv,out(face) = sum_i ( Kconv T_face - Qconv )_i          (W; positive when the face is hotter than its ambient)
Q_convection     = - sum over conditions Q_conv,out           (W; INTO the body)
```

`Tri3Convection::residualFrom(Tface)` already returns `Kconv T - Qconv` per node (P18-BC-001 published it for this milestone: "P18-BALANCE-001 reports them"). The
balance sums its three entries per facet, the facets per condition (P16's ascending facet order) and the conditions in the prepared boundary's order. It never
re-integrates `h`, `A` or the ambient.

For a uniform face temperature `Tb` the three entries sum to `h A (Tb - Tinf)`:

| Face (`hA = 4 W/K`, `Tinf = 300 K`, area 0.5 m², `h = 8`) | `Tb` | out | INTO the body |
| --- | --- | --- | --- |
| hotter than the air | 310 K | +40 W | **-40 W** |
| at the air | 300 K | 0 W (the ambient source `h Tinf A` is 1200 W) | 0 W |
| colder than the air | 290 K | -40 W | **+40 W** |
| non-uniform `[310, 320, 330]` | mean 320 K | `4 * 20 = 80 W` | -80 W |

The non-uniform row equals the hand-written dense `1^T (Kconv T - Qconv)` of the test, and the uniform rows equal `h A (Tb - Tinf)` — the closed form —
without the production code being asked for either.

## Why `Qconv` alone is wrong

`Qconv` is half of a boundary term: it is the heat the ambient WOULD bring in if the face were at 0 K. The face at the ambient temperature exchanges nothing but its `Qconv` is
`h Tinf A` = 1200 W. Summing `Qconv` as "the convection" reports 1200 W of heating for a body in equilibrium with the air. `ConvectionHeatTransferIsKconvTMinusQconvAndNotTheSource`
asserts `|source - exchange| > 1000 W` on that case, and the mutation "convection = Qconv only" is killed (MUTATIONS.md).

## Not derived from the conductive flux

The conductive flux of P18-POST-001 times a boundary normal is a different quantity (the discrete `q . n` of an element-constant field), and not the weak-form exchange.
It is used as a cross-check only, on the slab: `|q| A = 270 W` against the hot reaction 270 W and `k A dT / L`.

## Multiple conditions

| Situation | Treatment | Evidence |
| --- | --- | --- |
| two different ambients on one body | two entries, ordered as the conditions, summing | `MultipleConvectionBoundariesAreAdditive` (8 W + 22 W = 30 W at 302 K) |
| the same face carried by two conditions | BOTH count: P18-BC-001 keeps overlapping Robin conditions separate and they ADD in `K`; no deduplication | the twin case (15 W + 15 W); `AdditiveConvectionBoundariesBothCount` on the real block (25 and 10 W/(m² K) on the end cap) |
| a prescribed temperature on the same face | both kept: the convection is still a heat exchange and the constraint supplies it | `FixedTemperatureAndConvectionOverlapOnOneFace` (loses 3.2055 W = `h A (400 - 293.15)`; hot reaction 273.2055 W) |

This is the opposite of the Dirichlet rule, deliberately: two identical prescribed temperatures are ONE mathematical constraint (and one reaction); two Robin conditions are two
physical exchanges. The generic "deduplicate identical conditions" shortcut would be right for one and wrong for the other, and the distinction is tested on both sides.

## Timing

A backward-Euler step evaluates the exchange at `T[n+1]`. Using `T[n]` (or a midpoint) gives a different number that is not the one in the step's equation; the test shows the difference
(-200 W against -166.667 W on the lumped body; and on the real block the old-state exchange is more negative than the step's at every one of 10 steps).

## Orientation

`Tri3Convection` is a scalar-area term; the facet's winding appears nowhere. The balance takes the entries as they are, so reversing a triangle cannot change a convection total
(P18-BC-001 tested the term; the balance adds no orientation-dependent code, and the audit finds no normal in `ThermalBalance.cpp`).
