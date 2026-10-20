# P18-SOLVE-001 — the Dirichlet partition

```text
SUBJECT:  the exact reduction  Kff Tf = Qf - Kfc Tc,  its orders, and the term that makes
          the thermal case different from P17's
```

## The reduction

```text
[ Kff  Kfc ] [ Tf ]   [ Qf ]                      free      F  = unknowns ThermalFreeEquationMap leaves free
[ Kcf  Kcc ] [ Tc ] = [ Qc ]                      prescribed C = unknowns ThermalConstraintMap prescribes

Kff Tf = beff,    beff = Qf - Kfc Tc              exact elimination: no penalty, no row replacement, no large number
```

**P17 needed no `- Kfc Tc`: its prescribed displacement is zero. A prescribed temperature is arbitrary**,
so the term is what makes the solution depend on the boundary values at all. Omitting it solves the
problem with every prescribed node at 0 K (probes S01, S02, S03: all killed).

## Orders — one truth each

| Index space | Truth | Used for |
| --- | --- | --- |
| unknown `d` (`ThermalDofIndex`) | `ThermalDofMap`, the rank of the node handle | rows and columns of the full `K` |
| free equation `e` | `ThermalFreeEquationMap::freeDofs()`, ascending | rows and columns of `Kff`, entries of `Tf`, `Qf`, `beff` |
| prescribed ordinal `j` | `ThermalConstraintMap::constrained()`, ascending by unknown | columns of `Kfc`, entries of `Tc` |

The solver builds no second numbering: `reduceThermalSystem` takes the free map and the constraint map
and derives two lookup arrays from them. It never assumes the prescribed unknowns are first, last or
contiguous. Reconstruction goes through P18-DOF's own `reconstructTemperatures`.

The extraction visits the full matrix row by row in ascending unknown order, so every inner index of
`Kff` and `Kfc` comes out ascending without a sort, and `Kfc Tc` is summed in the row's own column
order — a fixed order, hence a fixed last bit.

## The interleaved example (the evidence the brief requires)

```text
full unknowns            0 1 2 3 4
prescribed               1 = 300 K,  4 = 350 K             (interleaved with the free ones)
free                     0 2 3        -> free equations 0 1 2

K = [  1 -1  0  0  0 ]     Q = [5 0 0 0 0] W
    [ -1  3 -2  0  0 ]
    [  0 -2  5 -3  0 ]     Kff = [ 1  0  0 ]      Kfc = [ -1  0 ]      (columns: unknowns 1, 4)
    [  0  0 -3  7 -4 ]           [ 0  5 -3 ]            [ -2  0 ]
    [  0  0  0 -4  4 ]           [ 0 -3  7 ]            [  0 -4 ]

Qf   = [5 0 0]          Tc = [300 350]
beff = Qf - Kfc Tc = [5 + 300, 0 + 600, 0 + 1400] = [305 600 1400]

T0 = 305 K;  5 T2 - 3 T3 = 600,  -3 T2 + 7 T3 = 1400   =>   T2 = 8400/26 = 323.0769...,  T3 = 8800/26 = 338.4615...
```

Every number above is asserted entry by entry by `ThermalSolve_DirichletPartitionInterleavedConstraints`
and the solution by `ThermalSolve_InterleavedConstraintsSolve`; the test's own Gaussian elimination
(`denseExpected`, which gathers the free rows, subtracts the prescribed columns and eliminates densely)
agrees to `1e-10`. Four deterministic SPD systems of order 6, 9, 14 and 23 with every third unknown
prescribed agree with the same oracle to a relative `6.2e-16`.

## The one-free-node proof

```text
K = [ 2 -2 ]    Q = [ 0 ]    T0 = 300 K held        Kff = [2]  Kfc = [-2]  Qf = [100]  Tc = [300]
    [-2  2 ]        [100]                            beff = 100 - (-2)(300) = 700           Tf = 350 K

expected T1 = 350 K        actual T1 = 350 (error 0.00e+00)
free residual 2(350) - 700 = 0            reaction at the held node  K T - Q = -100 W
```

With the term dropped the answer is `Tf = 50 K`; with the prescribed value at zero the two agree. The
test shifts the prescribed value by 10, 100 and 273.15 K and the free node moves by exactly that.

## Nothing is mutated

`K`, `Q` and the constraint map are read, never written. The full vector `K T - Q` is retained, the
reduced matrices are derived solver workspace, and no factorisation is kept (a test copies the inputs
before and after and compares).