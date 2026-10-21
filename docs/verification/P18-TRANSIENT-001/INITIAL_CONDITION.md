# P18-TRANSIENT-001 — the initial condition and the output policy

```text
SUBJECT:  the state at t = 0, how it meets the prescribed temperatures, and what is stored
CODE:     solveThermalTransientAlgebra, steps 3 and 4 and the sampling policy
```

## The initial state

`UniformInitialTemperature` (ADR-049) is one absolute temperature, in K, finite and not below 0 K. The run
expands it to a vector of `Ndof` temperatures. The algebraic seam takes a vector, and checks **each**
entry (`isAbsoluteTemperature`): `NaN`, `±inf` and anything below 0 K are `invalid_initial_temperature`, and
the message names the unknown. `0 K` is an absolute temperature and is accepted (tested).

## Prescribed dominates at t = 0

A Dirichlet unknown is not unknown. Its first state is its prescription, not the uniform guess:

```text
state[0] = initial temperature, then
for every prescribed unknown d:   state[0][d] = T_bar[d]       (overwritten)
```

The overwrite is applied **before the first history term is formed**, so `(C/dt) T[0]` uses the prescribed
value at held nodes. The override is **counted** (`initialOverrides`) and the resolved state is kept
(`initialTemperatures()`), so nothing is silent. An initial temperature that already equals a prescription
overrides nothing and counts nothing.

The hand oracle (`K = [1 -1; -1 1]`, `C = [2 1; 1 2]`, unknown 0 held at 400 K, initial 300 K, `dt = 1`):

```text
b_f = Q_1 + C_10 * T[0]_0 + C_11 * T[0]_1 = 1*400 + 2*300 = 1000       A_ff = 3     A_fc = 1 - 1 = 0
T[1]_1 = 1000/3 = 333.33...          (with the unresolved 300 K the history would give 900/3 = 300)
```

The real block (initial 300 K; 400 K and 300 K caps) reports **4** overrides, the four nodes of the hot
cap; the first stored state holds 400 K there.

## Output policy

```text
FinalOnly    one state, at t_end         history memory O(Ndof)
EveryStep    N + 1 states, t_0 ... t_N   history memory O((N+1) Ndof); the first is the RESOLVED initial state
```

There is **no list of requested output times** in the canonical data — only the policy — so there is no
off-grid request, no event splitting and no interpolation. The sample times are the grid's own times, and
the last is `t_end` exactly. `FinalOnly` and `EveryStep` produce the same final state **to the last bit**
(tested), and the per-step evidence (residuals, pivot ratio) is kept for every step under either policy.

A run of zero steps returns the resolved initial state as its only sample (`FinalOnly` and `EveryStep`
alike), no records, no kernel solve.

## Memory

`samples * Ndof` may not exceed `kMaxStoredValues = 50 000 000` temperatures (400 MB). Larger is refused
before the first step as `history_too_large`, naming the final-only policy. Tested with 51 unknowns over a
million steps; removing the guard turns that refusal into a run that does not end within the harness's 240 s,
which the mutation harness counts as killed by timeout (TA08).
