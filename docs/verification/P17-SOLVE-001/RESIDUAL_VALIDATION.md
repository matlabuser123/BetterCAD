# P17-SOLVE-001 — the residual gate

## The formula, chosen before anything was measured

```text
            r = Kff uf - Ff                       [N]

                              ||r||_2
    normalized  =  ------------------------------------------
                    ||Kff||_inf ||uf||_2  +  ||Ff||_2

    PASS  iff  normalized <= relativeResidualTolerance
```

Dimensionless: the denominator is a force, as `||r||_2` is. Scale-aware, which
is the point — `||r|| = 1e-6` N means something very different for `F ~ 1` N
and for `F ~ 1e9` N — and the raw norms are recorded alongside so both
readings are available.

### There is no scale floor, and its absence is deliberate

The brief suggests adding one to avoid dividing by near-zero. It is not needed,
and adding it would have been worse:

```text
the denominator vanishes  <=>  uf == 0 AND Ff == 0
and then                        r = Kff 0 - 0 == 0 exactly
so the honest value is          0,  not  0 / floor
```

That case is handled exactly, and it is **asserted rather than assumed**: if
the denominator is zero while `||r||_2` is not — which is arithmetically
impossible — the solve fails rather than reporting a flattering ratio. An
invented floor would have made a zero-load system's residual depend on a
number nobody could justify, and a *tiny* load needs no floor either, because
the denominator scales with the problem.

Measured on both ends:

```text
F = 0                         normalized = 0            PASS
F = (1e-30, 2e-30) N          normalized <= 1e-9        PASS, and x matches
                              the closed form to 1e-13
F = (1e20, 2e20) N            normalized <= 1e-9        PASS, no overflow
```

### The threshold, and why it is not derived from the results

```text
relativeResidualTolerance = 1e-9
```

An LDLT of an SPD matrix is backward stable, so the normalized residual of a
well-conditioned solve sits at a small multiple of the double epsilon — around
`1e-16`. `1e-9` leaves seven orders of head-room for conditioning, and a
residual one part in a billion of the applied load is far below any engineering
significance.

The measured values are **eight orders below it**, which is the gap that makes
the number a margin rather than a tuned constant:

```text
fixture                       ||r||_2 [N]    ||r||_inf [N]   normalized   threshold  PASS
-----------------------------------------------------------------------------------------
2x2 closed form               ~1e-16         ~1e-16          <= 1e-9      1e-9       PASS
3x3 closed form               2.48e-16       --              2.98e-17     1e-9       PASS
block fixture, fixed support  4.31e-13       2.56e-13        2.07e-17     1e-9       PASS
RM-MESH-01, fixed support     3.53e-13       --              2.96e-17     1e-9       PASS
RM-MESH-03, fixed support     --             --              1.02e-17     1e-9       PASS
RM-MESH-04, fixed support     --             --              1.13e-17     1e-9       PASS
RM-MESH-02 large              4.74e-10       --              3.85e-18     1e-9       PASS
zero load, fixed support      0              0               0            1e-9       PASS
```

## It is computed independently, and that is checkable

```text
WHERE        a loop in src/structural/StructuralSolve.cpp, after the solve
             returned
OVER WHAT    BetterCAD's own CSR copy of Kff and its own Ff -- the ORIGINAL
             system, so a factorisation that modified its internal copy cannot
             affect it
WITH WHAT    nothing. No Eigen product, no solver object, and `solver.error()`
             is never consulted anywhere in the file
```

`grep` over the implementation confirms the last point: `solver.error()` does
not appear. The only `solver.info()` reads are the factorisation status and the
back-substitution status, both of which are *additional* gates, not the
residual.

## The gate is one of three, and all three are required

```text
1  info() == Success, every D(k,k) > 0, and min|D|/max|D| >= pivotFloor
2  every component of uf is finite
3  the independent normalized residual <= relativeResidualTolerance
```

**And the residual alone would not have been enough**, which is measured rather
than argued — see [SINGULARITY_VALIDATION.md](SINGULARITY_VALIDATION.md): a
near-singular system solves with a normalized residual of `1.0e-17` and an
answer five orders larger than its data.

**Nor is the library's success enough**, which is also measured. With the
threshold set below what double precision can deliver, the solver still reports
success and BetterCAD still refuses:

```text
the independent residual is too large: ||r||_2 = 4.3110787617057336e-13 N,
||r||_inf = 2.5579538487363607e-13 N, normalized = 2.0735700918513837e-17
against a threshold of 1e-300. The solver reported success; BetterCAD does not
```

That is the adversarial path of brief section 38, reached through the public
API with no test double: the settings are the injection point.

## The free system, not the full one

**This is the trap the brief calls the most important one.**

```text
r_free  =  Kff uf - Ff            ~ 0 at every free equation      THE GATE
r_full  =  K u - F                ~ 0 at the free DOFs
                                  NON-ZERO at the constrained DOFs, where the
                                  entries are the support REACTIONS
```

Requiring `K u - F = 0` over the full system would fail every correct solution
of a restrained model. Measured on the block fixture with a 1000 N load and a
fixed support:

```text
largest |r_full| over the FREE degrees of freedom          2.56e-13 N
largest |r_full| over the CONSTRAINED degrees of freedom   273.83 N
sum of the constrained entries                 (7.1e-15, 1.3e-13, -1000) N
the applied load                               (0, 0, +1000) N
```

So the constrained entries are not error — they balance the applied load to six
significant figures, which is what makes them reactions. Both halves are
asserted:

```text
the free entries ARE ~zero                    < 1e-9 * applied         PASS
the constrained entries are NOT zero          > 1e-3 * applied         PASS
and they sum to minus the applied load        within 1e-6 relative     PASS
```

The second of those is the premise of the whole test: if the constrained
entries had been zero, requiring the full residual to vanish would have been
harmless and the distinction would not have needed making.

`r_full` is **retained and not aggregated** on `SolvedSystem::fullResidual()`.
`P17-REACTION-001` owns reaction semantics, per-support aggregation and the
equilibrium statement; discarding it here would have made that milestone
recompute what is already known.

## Units

```text
K      N/m        u      m        K u    N        F      N
r      N          ||r||  N        normalized  dimensionless
U      J          = (1/2) u^T K u
```

So `K u = F` is an equation between forces and the normalized residual is a
pure number. `coeff()` returns `Stiffness`, `displacementOf()` returns
`Length`, the residual norms are `Force`, and none of them converts to another
or decays to a `double` — three compile-failure cases enforce it:

```text
compile_fail.structsolve.residual-norm-as-length
compile_fail.structsolve.residual-norm-as-double
compile_fail.structasm.stiffness-as-force            (P17-ASSEMBLY-001)
```

`values()` on each type is the one deliberate SI escape hatch, documented as
the buffer a consumer maps.

## Supplemental: energy equilibrium

For a solution with zero prescribed displacement, equilibrium at the free
degrees of freedom gives

```text
    u^T K u  =  u^T F        so        U = (1/2) u^T K u = (1/2) u^T F
```

Measured on the block fixture: `U = 3.97547e-05 J`, agreeing with `(1/2) u.F`
to `1e-9` relative. Finite and non-negative on every successful fixture, and
exactly zero for a zero load.

This is **supplemental** and does not substitute for the residual gate: it is a
single scalar identity and a scatter error that preserved it is imaginable,
whereas the residual is a statement about every equation.
