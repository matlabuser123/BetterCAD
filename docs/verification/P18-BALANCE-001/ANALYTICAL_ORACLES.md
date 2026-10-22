# P18-BALANCE-001 — analytical reference cases

```text
SUBJECT:  every expected value in the balance suite, with its derivation; none is read back from the code under test
```

## Steady

| Id | Model | Derivation | Expected | Test |
| --- | --- | --- | --- | --- |
| BAL-STEADY-01 | `g = 2 W/K`, nodes at 400 K and 300 K | `K T = [2*400 - 2*300, -2*400 + 2*300] = [+200, -200]` | reactions `+200 / -200`, `B = 0` | `ReactionSignAnalytical`, `SteadyPureConductionBalance` |
| — | six-node chain, `g = 5` per link, 400 K / 300 K | series conductance `5/5 = 1 W/K`, `1 * 100` | reactions `+100 / -100` (and `-100 / +100` reversed) | `TwoTemperatureReactionsEqualOpposite` |
| BAL-STEADY-02 | `g = 2`, 300 K prescribed, 100 W at the free node | `T1 = 300 + 100/2 = 350`; `K T = [-100, +100]`, `Q = [0, 100]` | reaction -100, `B = 0` | `AppliedHeatBalancedByDirichletReaction` |
| — | 3-chain `g = 2`, 500 K / 380 K | `1 W/K * 120 K`; `T_mid = 440 K` | reactions `+120 / -120` | `SteadyDirichletReaction` |
| BAL-STEADY-03 | one body, only exit a convecting triangle `hA = 4`, 300 K air, 12 W generated | `T = Tinf + P/(hA) = 303 K`; exit `4 * 3 = 12 W` | generation +12, convection -12, no reaction, `B = 0` | `SteadyGenerationAndConvection` |
| Robin only | 40 W applied, `hA = 4`, 300 K | `T = 300 + 40/4 = 310 K` | surface +40, convection -40 (out +40) | `SteadyRobinOnlyOracle` |
| BAL-STEADY-04 | all three nodes held at 330 K / 290 K in 300 K air | `hA (T - Tinf)` = `+120` out / `-40` out | convection -120 & reaction +120; convection +40 & reaction -40 | `SteadyFixedTemperatureAndConvection` |
| face | `Tb = 310 / 290 / 300 K`, `hA = 4`; non-uniform `[310, 320, 330]` | `hA (Tb - Tinf)`; `hA (mean - Tinf)` | out `+40 / -40 / 0`; `80` | `ConvectionSign…`, `ConvectionHeatTransferIsKconvTMinusQconv…` |
| two ambients | `hA = 4` at 300 K, `hA = 1` at 280 K, 30 W applied | `4(T-300) + 1(T-280) = 30`, `T = 302` | out `8` and `22`; twin conditions `15 + 15` | `MultipleConvectionBoundariesAreAdditive` |
| sink and source | `g = 2`, 300 K prescribed; +100 W surface, -20 W generation | `T1 = 340`; `K T = [-80, +80]` | surface +100, generation -20, reaction -80 | `SignConvention` |
| constant T, generated | both nodes at 300 K, 20 W generated | `r = -Q` | reaction -20 | `ConstantTemperatureUnderGenerationIsRemovedByTheReaction` |

## Slab, end to end (real block)

| Quantity | Closed form | Measured |
| --- | --- | --- |
| heat through the bar | `k A dT / L = 45 * 1.2e-3 * 100 / 0.02 = 270 W` | hot reaction `+270.000000000 W`, cold `-270.000000000 W` |
| the same from the recovered flux | `|q| A = 225 000 * 1.2e-3` | 270.000000 W (P18-POST-001) |
| 8 W through a 40 x 20 mm face (`1e4 W/m²`) vs a 300 K cap | `q'' A` | surface +8.000000, reaction -8.000000 |
| 2.4 W generated, end cap convecting | `q''' V = 1e5 * 2.4e-5` | generation +2.4, convection -2.4 |
| a cap held at 400 K that also convects | `h A (400 - 293.15) = 3.2055 W` | 3.205500 W |

## Transient

| Id | Model | Derivation | Expected | Test |
| --- | --- | --- | --- | --- |
| BAL-TRANS-01 | insulated, uniform, nothing applied | `dT = 0` | every term 0 | `TransientInsulatedNoLoad` |
| BAL-TRANS-02 | `C = [3 2; 2 3]` (10 J/K), 20 W, `dt = 2` | `dT = 20 * 2 / 10 = 4 K`; `dE = 10 * 4 = 40 J` | storage 20 W = generation | `TransientInsulatedGeneration` |
| BAL-TRANS-03 | lumped 10 J/K, `hA = 2`, 300 K air, `T0 = 400`, `dt = 1` | `10 (T1 - 400) + 2 (T1 - 300) = 0`, `T1 = 4600/12` | storage `-166.667 W` = convection | `TransientLumpedConvection` |
| — | the same with 30 W applied | `T1 = (4000 + 600 + 30)/12` | `30 + conv = storage` | same |
| BAL-TRANS-04 | `C = [2 1; 1 2]`, `K = [1 -1; -1 1]`, node 0 at 300 K, node 1 330 K -> 320 K | `C dT = [-10, -20]`, `K T' = [-20, 20]` | reaction `-30 W` (steady formula `-20`) | `TransientDirichletReactionIncludesCapacity` |
| energy | `C = [2 1; 1 2]`, `dT = [10, -4]` | `[3, 3] . dT = 18 J` | 18 J (diag(C): 12 J) | `NonuniformStoredEnergyUsesConsistentCapacity` |
| energy, prescribed | `dT = [10, 0]` | `[3, 3] . dT = 30 J` | 30 J (diag(C): 20 J; free only: 0) | `TransientStoredEnergyUsesFullC` |
| fully constrained | both nodes 300 K, `Q = [5, 3]` | `r = -Q` | reactions `-5, -3`; no storage | `TransientFullyConstrainedStillBalances` |
| BAL-TRANS-05 | hand tetrahedron, 10 W, `dt = 0.3`, `t_end = 1` | steps 0.3, 0.3, 0.3, 0.1; `dE = 10 dt` | rate 10 W every step (nominal: 3.33 W) | `TransientFinalPartialStepUsesActualDt` |
| integrated | the same | `10 W * 1 s` | external = stored = endpoints = 10 J | same |
| block | insulated, 2.4 W, 100 s | `2.4 * 100 = 240 J = rho cp V dT` | 240 J; last step 2 s | `TransientInsulatedGenerationWithAPartialLastStep` |

## Not closed forms

A fine-cylinder steady balance and a six-step transient on the same mesh are recorded as smoke runs: finite terms, the books close to rounding, the sparse pattern is sparse, and the balance is
repeatable. Their numbers are in README.md and TOLERANCE_MEASUREMENT.md; no closed form is claimed for them.
