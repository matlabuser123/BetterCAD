# P18-BALANCE-001 — the prescribed-temperature reaction

```text
SUBJECT:  the heat a prescribed temperature supplies: the derivation of its sign, its formula (steady and transient) and what it is not
```

## The derivation

The assembled equation of unknown `i` is

```text
(K T)_i  =  Q_i                (steady)          (C dT/dt)_i + (K T)_i  =  Q_i          (a backward-Euler step: C (T[n+1]-T[n])/dt + K T[n+1])
```

`(K T)_i` is the heat that LEAVES node `i` through conduction to its neighbours and through convection to the ambient. `Q_i` is the heat the
loads and the ambient source bring IN. At a free unknown the two agree (that is the equation). At a prescribed unknown the temperature is
imposed, the equation is not solved, and the constraint is an extra heat `R_i` that the equation did not contain:

```text
(K T)_i  =  Q_i + R_i      =>      R_i = (K T)_i - Q_i = r_i                  steady
                                   R_i = [C (T[n+1]-T[n])/dt + K T[n+1] - Q]_i = r_i      backward-Euler step
```

So **the reaction INTO the body is `+r_i`**, the component of the full residual at the prescribed unknown. A node that loses more heat than
the sources bring (hot end of a bar) must be supplied by its constraint: `r > 0`, a reaction INTO the body. A node that receives more than it
loses (cold end) has `r < 0`: the constraint REMOVES heat. A minus sign would put heat into a cold wall.

This is not trusted from the algebra alone. It is frozen by an oracle in which the direction of the heat is obvious.

## The oracle that freezes it (`ReactionSignAnalytical`)

Two nodes joined by `g = 2 W/K`, both prescribed: 400 K and 300 K. No loads.

```text
K = [  2  -2 ]     Q = [0, 0]     T = [400, 300]
    [ -2   2 ]

K T = [ 2*400 - 2*300,  -2*400 + 2*300 ] = [ +200, -200 ] W          r = K T - Q = [ +200, -200 ]
```

Heat flows from the 400 K node to the 300 K node at `g (400 - 300) = 200 W`. The hot constraint must **supply** it (+200 into the body); the cold
one must **absorb** it (-200). `R = +r` reproduces that; `R = -r` would put 200 W into the cold wall and take it from the hot one. The total
reaction is zero, the balance is zero, and each reaction is checked separately (`reactionOf(hot) > 0`, `reactionOf(cold) < 0`) so a sign flip
cannot hide by cancelling against another category.

The second oracle (`AppliedHeatBalancedByDirichletReaction`): `g = 2`, node 0 prescribed at 300 K, 100 W applied at the free node 1.

```text
T1 = 300 + 100/2 = 350 K      K T = [2*300 - 2*350, -2*300 + 2*350] = [-100, +100]      Q = [0, 100]      r = [-100, 0]
```

The free row is satisfied (`r_1 = 0`); the prescribed row is `-100 W`: the 300 K boundary removes the 100 W the load puts in.
`Q_surface = +100, Q_reaction = -100, B = 0`.

A third, with a sink: `SignConvention` (300 K boundary, +100 W surface, -20 W generation): `r_0 = -80`, so the reaction is -80 W and the books close at zero with
the negative generation kept negative.

## The transient reaction is the FULL discrete residual

The steady `K T - Q` is **not** the transient reaction. The storage term is part of `r_i`, and with the consistent capacity `C` it couples a
prescribed node to its free neighbours: a temperature change at a neighbouring free node changes the constrained node's reaction.

`TransientDirichletReactionIncludesCapacity` (BAL-TRANS-04). `C = [2 1; 1 2]`, `K = [1 -1; -1 1]`, `Q = 0`, `dt = 1 s`. Node 0 is held at 300 K;
node 1 goes from 330 K to `T1'`. Row 1 of backward Euler, `(C/dt + K) T' = Q + (C/dt) T`, with `C/dt + K = diag(3, 3)`:
`3 T1' = 1*300 + 2*330 = 960`, so `T1' = 320 K`.

```text
dT        = [  0, -10 ]
C dT      = [ -10, -20 ]                  (the first entry is the STORAGE COUPLING of the free neighbour)
K T'      = [ 300 - 320, -300 + 320 ] = [ -20, +20 ]

r_0 = (C dT)_0 / dt + (K T')_0 - Q_0 = -10 - 20 = -30 W          reaction INTO the body at node 0
r_1 = -20 + 20 = 0                                                free row, satisfied

steady formula (K T' - Q)_0 = -20 W          WRONG: it omits the -10 W of storage coupling
lumped capacity diag(3, 3):  T1' = 322.5, r_0 = 0 - 22.5 = -22.5 W     WRONG for this model: it removes the coupling
```

and the books close only with the full residual: `dE = 1^T C dT = -30 J`, the storage rate is -30 W, the reaction is -30 W, `B = 0 + (-30) - (-30) = 0`.
Reusing the steady formula gives `B = -20 + 30 = +10 W` and is rejected by the test.

## What the reaction is not

| Not | Why | Evidence |
| --- | --- | --- |
| "whatever makes the balance close" (`R = -(Q_surface + Q_generation + Q_convection)`) | that would make the balance a tautology: a wrong load total, a dropped boundary or a sign error would be absorbed into the reaction and reported as a balanced model | `reactionHeats` reads only the residual; a mutation that defines it as the remainder is killed (MUTATIONS.md); the ownership gate has no remainder symbol |
| a free-row residual | a free residual is a solver defect, not a heat; it appears only as the balance's imbalance `B = -sum(r_free)` | `FreeResidualsAreNotReactions` |
| clamped or zeroed | a small or negative reaction is reported as computed; the tolerance applies to the balance, not to each term | no clamp in the code; zero is a valid reaction |
| counted once per boundary condition | the unit is the prescribed UNKNOWN: two identical conditions that P18-BC-001 collapsed to one constraint are one reaction | `DuplicateIdenticalDirichletConstraintsAreOneReaction`, `DuplicateTemperatureBoundariesDoNotDoubleCountTheReaction` |
| split among overlapping conditions | when conditions share an unknown, no deterministic non-double-counting ownership rule exists; the policy below | no `reactionsOf(condition)` symbol (compile-fail `THE_RESULT_HAS_NO_PER_CONDITION_REACTION`) |

## Attribution policy (frozen for this version)

```text
authoritative reaction total      the sum over UNIQUE prescribed unknowns, ascending by ThermalDofIndex
per-unknown reactions             reported: one ReactionHeat{dof, heatInto} per prescribed unknown
per-condition split               NOT reported. Two conditions that prescribe an unknown at the same temperature share it; an equal
                                  split would be an invention, and an "owner" would be arbitrary. The condition that prescribed an
                                  unknown is available upstream (PreparedThermalBoundary::constraintSources) for a later milestone
                                  that decides such a rule; this one does not.
```

`reactionOnCondition` in the real-mesh tests sums the per-unknown reactions over a condition's unknowns *by the test*, to compare a face's
reaction with a closed form. The duplicate-condition test shows why that is a test device and not a product feature: the duplicate "has" the same 270 W as the
original, because it names the same unknowns, not because the heat was supplied twice.

## Free residual and balance are different gates

For an accepted solution `r_free ~ 0` (P18-SOLVE's and P18-TRANSIENT's acceptance). The balance residual is a consequence of that, not a replacement for
it:

```text
B = Q_surface + Q_generation + Q_convection + sum_c r_c - [storage]
  = - sum_free r  +  (attribution defects)  +  rounding
```

so a global scalar can cancel large local errors: `K = I`, `Q = 0`, `T = [+5, -5]` has two free residuals of 5 W of opposite sign and `B = 0`
(`BalanceCannotRescueAWrongFreeField`). A balance PASS is therefore never a solve PASS. `ThermalBalanceResult` can only be taken from an accepted
`SteadyThermalSolution` / `TransientThermalSolution`, which cannot be built from a failing field.
