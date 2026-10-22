# P18-BALANCE-001 — the steady balance

```text
SUBJECT:  B = Q_surface + Q_generation + Q_convection + Q_reaction ~ 0, computed from the assembled system, the accepted solution and the qualified attribution
```

## What is computed, in order (`steadyHeatBalance`)

```text
1  sizes agree (K, Q, constraints, T); T finite
2  the attribution accounts for the heat input:   sum Q  =  sum loads  +  sum Qconv          (else attribution_missing)
3  r = K T - Q                                      the full residual, W, over every unknown
4  reactions: R_c = + r_c at each prescribed unknown, ascending by ThermalDofIndex
5  surface and generation: the prepared contributions (P18-LOAD-001), in their order, kept apart
6  convection: per condition, sum over its facets of Kconv T - Qconv (Tri3Convection::residualFrom), then Q_conv = - out
7  every total finite; B = netImbalance of the five terms; status from the policy
```

Nothing is recomputed that another milestone owns:

| Quantity | Owner | Here |
| --- | --- | --- |
| `q'' A`, `q''' V` per load | P18-LOAD-001 | read from `PreparedThermalLoads::contributions()[i].integrated.heatRate` (the prepared nodal vector is cross-checked in the tests, never re-derived) |
| `Kconv`, `Qconv`, the facet unknowns | P18-BC-001 | read from `PreparedConvectionBoundary::facets`; the exchange is `residualFrom(Tface)` |
| `K`, `Q` | P18-ASSEMBLY-001 | read from `ThermalSystem` |
| `T`, the constraints | P18-SOLVE-001 / P18-BC-001 | read from the accepted solution and the prepared boundary's constraint map |

## The attribution check is the guard against omission and double counting

`Q` already contains the loads and the ambient source `Qconv`. The categories are a parallel VIEW of the same equation and are never added to it.
`sum Q = sum loads + sum Qconv` is checked before any temperature is read: a load or a convection condition missing from the presented sets, or an extra
one, fails with `attribution_missing` (`AttributionMustAccountForTheHeatInput`). The reverse mistake — adding `sum Q` on top of the explicit categories —
would double count; the test `GlobalHeatInputIsNotAddedToTheCategories` shows the correct residual is ~0 where the double-counted one is the full 100 W.

The ambient source alone is not the heat transferred: `Qconv = h Tinf A / 3` per node is 1200 W for the test face while the face exchanges 0 W when
`T = Tinf`; `ConvectionHeatTransferIsKconvTMinusQconvAndNotTheSource` asserts the difference.

## Reference suite (hand networks; `ThermalBalanceTests.cpp`)

| Case | Network | Surface in | Generation in | Convection in | Reaction in | B | Test |
| --- | --- | --- | --- | --- | --- | --- | --- |
| BAL-STEADY-01 | `g = 2`, 400 K / 300 K prescribed | 0 | 0 | 0 | `[+200, -200]` (total 0) | 0 | `ReactionSignAnalytical`, `SteadyPureConductionBalance` |
| BAL-STEADY-02 | `g = 2`, 300 K prescribed, 100 W at the free node | +100 | 0 | 0 | -100 | 0 | `AppliedHeatBalancedByDirichletReaction` |
| BAL-STEADY-02b | 4-node chain `g = 3`, 350 K at one end, -9 W extracted | -9 | 0 | 0 | +9 | 0 | `SteadyAppliedHeatAndReaction` |
| BAL-STEADY-03 | one body, only exit a convecting triangle `hA = 4`, 300 K air, 12 W generated | 0 | +12 | -12 | none | 0 | `SteadyGenerationAndConvection` (T = 303 K) |
| Robin only | the same, 40 W applied | +40 | 0 | -40 | none | 0 | `SteadyRobinOnlyOracle` (T = 310 K) |
| BAL-STEADY-04 | every node of the face held at 330 K / 290 K | 0 | 0 | -120 / +40 | +120 / -40 | 0 | `SteadyFixedTemperatureAndConvection` |
| BAL-STEADY-05 | convecting triangle + chain + one prescribed node + 30 W + 6 W | +30 | +6 | `-hA(mean T - Tinf)` | the rest | 0 | `SteadyCombinedCategories` |
| two ambients | `hA = 4` at 300 K and `hA = 1` at 280 K, 30 W | +30 | 0 | `-8` and `-22` | none | 0 | `MultipleConvectionBoundariesAreAdditive` (T = 302 K) |
| three links | 5-node bar `g = 4`, 360 K / 300 K, +8 W and -4 W | +4 | 0 | 0 | sum -4 | 0 | `MultipleTemperatureBoundariesAndLoadsTogether` |

Every number is typed in from a hand derivation or the test's own dense solve; the balance is never asked what it should say.

## Real meshes (`ThermalBalanceMappedTests.cpp`)

A 40 x 30 x 20 mm steel block (`k = 45`), end to end through document, extrude, P16 mesh, mapping, model, numbering, prepared loads and boundary, assembly and
the qualified steady solve.

| Case | Closed form | Balance result |
| --- | --- | --- |
| slab 400 K / 300 K | `k A dT / L = 45 * 1.2e-3 * 100 / 0.02 = 270 W` | hot reaction +270.000000000 W, cold -270.000000000 W, `B = 4.8e-13 W` |
| the same, by the recovered flux | `|q| A = 225 000 * 1.2e-3 = 270 W` (P18-POST-001) | 270.000000 W |
| 8 W applied (`1e4 W/m²` on `8e-4 m²`) vs a 300 K cap | `q'' A = 8 W` | surface +8, reaction -8 (relative 1e-8), `B = 4.1e-13` |
| 2.4 W generated, end cap convecting (`h = 25`, 293.15 K) | `q''' V = 1e5 * 2.4e-5 = 2.4 W` | generation +2.4, convection -2.4 (relative 1e-8), no reaction, `B = 3.0e-13` |
| two caps (400 K, 330 K), two convecting sides, two flux loads, two generation loads | `3 - 1.2 = 1.8 W` surface, `1.2 - 0.24 = 0.96 W` generation | +1.8, +0.96, -2.457, -0.303 W; per-item totals sum to the categories bit for bit; `B = 2.4e-13` |
| a cap held at 400 K that also convects | convection loses `h A (400 - 293.15) = 3.2055 W` | 3.205500 W; hot reaction `270 + 3.2055 = 273.2055 W` |
| two conditions on the same face, 2.4 W generated | `h1 A (Tb - T1) + h2 A (Tb - T2) = 2.4 W` | both counted, each as `h_i A (Tb - Tinf_i)` |
| two identical prescribed-temperature conditions | one constraint per unknown | reaction 270 W once, not 540 |
| fine cylinder (P16, curved wall), 900 unknowns, 144 prescribed, 142 convection facets | generation `2e6 * pi r² L = 4.5239 W` (polygon: 4.5225 W) | generation +4.522458, convection -0.183771, reaction -4.338687, `B = -9.9e-12` |

## Cross-checks that are not the method

* The retained full residual of the solution (`SteadyThermalSolution::fullResidual`) is compared, component by component, with the balance's own `steadyFullResidual`
  on the combined case (agreement 1e-9 W): the balance recomputes the residual rather than trusting a cached one, and the two agree.
* The prescribed components of that residual sum to the reaction total (relative 1e-12), and `B = -sum(r_free)` to rounding.
* The nodal vector of P18-LOAD (`PreparedThermalLoads::totalHeatRate()`) equals the sum of the contributions.

None of them is used to produce the balance; each is evidence that its categories came from different places and agree.
