# P18-TRANSIENT-001 — prescribed temperatures in the transient system

```text
SUBJECT:  how a nonzero Dirichlet value enters every step, exactly, and is held exactly
CODE:     reduceThermalSystem (P18-SOLVE-001, reused), solveThermalTransientAlgebra, constraintsHeld
```

## Exact, per step

For every step the effective system `A = C/dt + K`, `b = Q + (C/dt) T[n]` is partitioned by P18-DOF-001's
free-equation map and the prescribed temperatures `T_c` move to the right-hand side:

```text
   A_ff T_f[n+1] = b_f - A_fc T_c        with  A_fc = C_fc / dt + K_fc
```

There is no penalty (`A_ii += 1e20`), no row replacement, no `T_c`-dependent scaling and no approximation.
The `- A_fc T_c` term is the one the steady solver's first mutation drops (`beff = Qf`); the same mutation
here would be killed by every Dirichlet test, and the oracle with an interleaved partition, a 1-in-3 and a
2-in-3 prescribed set is checked against the test's own dense elimination state by state.

## Held exactly

After the free unknowns are solved the full state is rebuilt through P18-DOF-001's `reconstructTemperatures`
(free rows take their solved value, prescribed ones take the prescription) and the acceptance step then
checks, by **equality of doubles**, that every prescribed unknown equals its prescription. A tolerance
would admit an integrator that approximated a boundary condition. The check is on the candidate, not on the
solver's output, and it is run for every step; a candidate that moved a prescribed unknown by `1e-9` K is
refused as `constraint_not_preserved` (test) and the check applied to the wrong vector is a killed mutation
(V01).

## Interleaving

Nothing assumes the prescribed unknowns are first, last or contiguous. The tests prescribe
`{0}`, `{1}`, `{2}`, `{0,2}` and `{1,2}` of a three-unknown chain, an interior unknown of a four-unknown
tetrahedron with sparse node ids (`10 … 13`, the prescribed node is handle 11 = unknown 1), and a
36-node face of a 216-unknown grid.

## All unknowns prescribed

Valid. `T = T_c` at every state, the kernel is not invoked (`kernelSolves == 0`, `kernelInvoked == false`),
the residual records are exactly zero and the initial overrides are counted. `Q` is irrelevant to `T` and
relevant to the boundary heat, which P18-BALANCE-001 reads.

## No prescribed temperature

Valid. A model anchored by convection alone, or by nothing but its initial temperature, is a transient
model (see [FORMULATION.md](FORMULATION.md)). Nothing in this module requires a Dirichlet condition.

## The prescribed temperature in the provenance

A `T_bar` edit does not stale the assembled system (it enters no matrix and no heat vector) but it changes
`T_c`, so it stales the solution. The solution's source therefore records the canonical
prescribed-temperature conditions **by value** (and only those; the convection conditions are in the
system's own source). See [CURRENTNESS.md](CURRENTNESS.md).
