# P18-TRANSIENT-001 — the formulation

```text
SUBJECT:  what is solved, in which units, with which matrices, and what is deliberately not done
FROZEN:   ADR-049 (backward Euler, fixed step, uniform initial temperature, constant coefficients)
```

## The equation and the step

The semi-discrete heat equation assembled by P18-ASSEMBLY-001 is

```text
C dT/dt + K T = Q          C in J/K (consistent), K in W/K, Q in W, T in K, t in s
```

and one backward-Euler step of size `dt` from the state `T[n]` is

```text
C (T[n+1] - T[n]) / dt + K T[n+1] = Q

   A  = C / dt + K                     W/K      the effective matrix, one per distinct dt
   b  = Q + (C / dt) T[n]              W        the history term uses T[n], never T[n+1]
   A T[n+1] = b                        (unconstrained)
```

With prescribed temperatures `T_c` the unknowns are partitioned exactly as P18-SOLVE-001 does, with
`A` in place of `K` and `b` in place of `Q`:

```text
   A_ff T_f[n+1] = b_f - A_fc T_c         exact elimination; no penalty, no row replacement
```

The reduction is `reduceThermalSystem(A, b, equations, constraints)` — **the same function, the same
free-equation numbering and the same `- Kfc Tc` term** as the steady solve — and the factorisation is
`linalg::solveSymmetricSparse` (ADR-052). Nothing of the steady solver was copied or changed.

## Why `A_fc` is not `K_fc`

`A_fc = C_fc / dt + K_fc`. A prescribed unknown couples to a free one through the **capacity** as well as the
conductance, and the coupling through `C` is real: it is the heat stored in the part of an element that
belongs to the held node. The test `PrescribedDominatesTheInitialTemperatureAtTimeZero` is built so that
`A_fc` is exactly zero (`C_fc/dt = 1`, `K_fc = -1`) and the history term alone decides the answer; the
oracle value `1000/3` differs from the value obtained with an unresolved initial state (`300`).

## The capacity is the consistent matrix, as assembled

`C` is P18-ELEM-001's consistent Tet4 capacity matrix summed by P18-ASSEMBLY-001. It is never lumped,
scaled or replaced here, and the integrator contains no row-sum. The oracle that proves it:

```text
C = [2 1; 1 2]   K = [1 -1; -1 1]   dt = 1   Q = 0   T[0] = [300, 330]

   A = [3 0; 0 3]        b = C T[0] = [2*300 + 330, 300 + 2*330] = [930, 960]        T[1] = [310, 320]

   a LUMPED C (row sums 3, 3) gives  [4 -1; -1 4] T[1] = [900, 990]  ->  [306, 324]
```

The two answers differ by 4 K at every unknown, so the oracle cannot be passed by a lumped matrix. The
antisymmetric mode `(1, -1)` has `C v = v` and `K v = 2 v`, so it decays by `1/(1 + 2 dt)` per step while
the mean `(1, 1)` is constant (`K 1 = 0`); the test checks four states against that closed form.

The one-unknown oracle: `C = 10`, `G = 2`, `T_inf = 300`, `T[0] = 400`, `dt = 1`:
`(10/1 + 2) T[1] = 2*300 + (10/1)*400`, so `T[1] = 4600/12 = 383.3333…`.

## Steady singularity is not transient singularity

The steady solve refuses a model with no absolute-temperature anchor because the constant temperature is in
the null space of `K`. In the transient problem the matrix is `C/dt + K`, positive definite whenever the
capacity is, and the absolute level is fixed by the **initial temperature**. So:

```text
pure-Neumann transient model                      SUCCEEDS   (steady: missing_temperature_datum)
disconnected bodies, no prescribed temperature    SUCCEEDS   (steady: missing_temperature_datum)
one region anchored, one floating                 SUCCEEDS   (steady: unanchored_thermal_region)
a region with no capacity and no conduction       REFUSED    singular_system, at step 1
a node with no capacity but a conduction path     SOLVES     (it follows its neighbour: quasi-steady)
```

`analyseAnchoring` is **not** called by the transient solve. A mutation (TA03) that calls it and refuses an
unanchored region is killed by the pure-Neumann and disconnected tests. What is refused is what is actually
wrong, found by the factorisation's pivot gate, not by a geometric rule.

## Units, checked by the compiler

```text
C / dt        (J/K) / s = W/K        decltype(HeatCapacity / Time) == ThermalConductance
```

`effectiveMatrix`, `validateTransientStep` and `solveThermalTransientAlgebra` take `ConductanceMatrix` and
`CapacityMatrix`, two different types: handing `C` where `K` belongs does not compile
(`A_CAPACITY_IS_NOT_A_CONDUCTANCE`, `A_CONDUCTANCE_IS_NOT_A_CAPACITY`).

## What is not done

```text
no adaptive step, no step doubling       a step that fails fails the run; dt is never changed
no retry                                 no second attempt with a smaller step
no regularisation                        no A += eps*I, no pinned unknown, no penalty
no lumping, no capacity replacement      C is used as assembled
no interpolation                         the stored states are the integrator's own
no time-dependent loads or conditions    the canonical data carries numbers, not functions of time
no gradient, flux, reaction, balance     P18-POST-001 and P18-BALANCE-001
no restart, no persistence               every run starts from the canonical initial condition
```

Each absence is a compile-fail case or a test: see [SCOPE.md](SCOPE.md).

## Cost, stated

Each step **refactorises** `A_ff`. For a constant `dt` the factorisation could be reused, but the kernel
(`linalg::solveSymmetricSparse`) is the qualified one-shot of ADR-052 and changing it would requalify P17.
The cost per step is one sparse LDL^T, one extraction (`O(nnz)`), the history product and the acceptance
step's rebuild (`O(nnz)`). The fine cylinder (900 unknowns) takes ten steps in well under a second in a
debug build. A run anywhere near `kMaxTimeSteps = 1 000 000` is hours long on a real mesh, which is why
the limit is a resource guard and not a performance claim. Reuse of one factorisation across constant steps
is a recorded optimisation, not a correctness matter, and is not done here.
