# P18-ELEM-001 — the capacity matrix Ce, and the policy

```text
SUBJECT:  Ce = integral rho cp N^T N dV  [J/K], 4 x 4, and the decision
          CONSISTENT versus LUMPED
POLICY:   P18 initial capacity policy = CONSISTENT   (kTet4CapacityPolicy)
TESTS:    ThermalElement_UnitTetCapacityAnalytical, _GeneralTetCapacityOracle,
          _CapacityConsistentPolicy, _CapacitySymmetric, _CapacityPositiveDefinite,
          _CapacityFullRank, _CapacityRowSums, _CapacityTotalHeatCapacity,
          _OnePointQuadratureIsNotTheConsistentMatrix, _DensityScaling,
          _SpecificHeatScaling, _CapacityRejectsInvalid*, _CapacityNeedsNoConductivity,
          _CapacityNonFiniteProduct, _CapacityPhysicalMaterial
```

## Policy: CONSISTENT, exact closed form

ADR-049 left "consistent versus lumped" to this milestone, requiring one stated
policy with `C` symmetric positive definite. **CONSISTENT** is chosen:

```text
Ce = rho cp V / 20 * | 2 1 1 1 |         Ce_ii = rho cp V / 10
                     | 1 2 1 1 |         Ce_ij = rho cp V / 20   (i != j)
                     | 1 1 2 1 |
                     | 1 1 1 2 |
```

from the exact integrals for a Tet4 of volume `V`: `integral Ni^2 dV = V/10`,
`integral Ni Nj dV = V/20`. Reasons, recorded in FORMULATION.md: it is the
Galerkin form ADR-049 commits to and lumping is an approximation of it; both
are SPD but the consistent matrix has a known spectrum the tests can pin
(which is what makes the one-point and lumping mutations detectable); and the
one cost of consistency -- a possibly non-monotone first backward-Euler step at
small time steps -- belongs to the time integration, which P18-TRANSIENT-001
measures against the 1D slab and lumped-body benchmarks. If lumping is ever
wanted it is a separate, explicit, separately qualified policy with its own
ADR. There is no diagonal variant, no policy enum and no setting in this
kernel (compile-fail `the-capacity-has-no-lumped-variant`), and no
conditional `capacityMatrix()` that changes formulation behind a flag.

It is written in closed form, not integrated: `Ni Nj` is quadratic.

## The trap, and the tests that close it

A one-point centroid rule (`Ni = 1/4`) gives `rho cp V/16` in **every** entry.
That matrix has the correct row sums (`rho cp V/4`), the correct total
(`rho cp V`) and the wrong diagonal, and it has **rank one**. A test that
checked conservation alone would pass it. So the capacity tests pin the full
matrix, and `OnePointQuadratureIsNotTheConsistentMatrix` shows the two
side by side on the general tetrahedron:

```text
                       centroid rho cp V/16 * ones      lumped rho cp V/4 * I      production
row sums               rho cp V / 4 = 747406            rho cp V / 4               rho cp V / 4
sum of all entries     rho cp V                         rho cp V                   rho cp V
eigenvalues            ~0, ~0, 5.7e-12, 7.47e+05        (4 x rho cp V/4)           1.49e+05 x 3, 7.47e+05
rank                   1                                4                          4
every entry differs    -                                -                          by > 0.1 * entry
```

Mutations E22 (lumped diagonal), E23 (centroid), E24 (off-diagonal =
diagonal), E25, E26, E33 are all killed.

## Unit tetrahedron, rho cp = 120 J/(m^3 K) — the hand matrix

`V = 1/6`, so `rho cp V / 20 = 120 / 120 = 1`:

```text
          | 2 1 1 1 |
Ce   =    | 1 2 1 1 |   J/K      actual: identical
          | 1 1 2 1 |            max |Ce - hand| = 0.00e+00 (diagonal and
          | 1 1 1 2 |            off-diagonal both 0.00e+00)
```

`rho = 12 kg/m^3`, `cp = 10 J/(kg K)` are **test-only scaling values**, not a
material; a realistic case (`k 45, rho 7800, cp 500`) is separate
(`CapacityPhysicalMaterial`) and agrees with the oracle to 1e-14.

## Invariants

```text
diag / off-diag    Ce_ii = 2 Ce_ij to 1e-15; worst relative error of rho cp V/10
                   and rho cp V/20: 0.00e+00 over 24 fixtures
off-diagonals      all > 0 (catches accidental lumping)
trace              2 rho cp V / 5 to 1e-14
symmetry           max |Cij - Cji| = 0.00e+00 over 34 fixtures
row sums           rho cp V / 4: worst relative error 1.77e-16
total              1^T Ce 1 = rho cp V (J/K): worst relative error 1.65e-16;
                   Tet4Capacity::total() agrees; a uniform rate of 2.5 K/s
                   stores rho cp V * 2.5 W
no null mode       Ce 1 != 0 (every component positive) -- Ce, unlike Ke,
                   depends on the absolute rate vector
spectrum           rho cp V/20 (x3) and rho cp V/4 (x1) to 1e-12; the ratio
                   lambda_min / lambda_max = 0.2 exactly; the unit tetrahedron
                   at rho cp = 120 gives 1, 1, 1, 5
definiteness       x^T Ce x > 0 for 30 random non-zero x per fixture; every
                   eigenvalue > 0; rank 4
```

## Material scaling

`Ce(c rho) = c Ce(rho)` and `Ce(c cp) = c Ce(cp)` for `c` in `{0.25, 2, 10}`,
to 1e-15; `(rho, cp) = (1200, 800)` against `(7800, 500)` agree through the
product. Each is tested separately so a field misuse is caught.

## Field-swap protection

`k = 42.75`, `rho = 7853.25`, `cp = 486.5` are distinct, so `k` in `Ce` or
`rho`/`cp` in `Ke` would be a different number (`FieldSwapProtection`,
`CapacityNeedsNoConductivity`). A `rho`/`cp` swap gives the **same product**,
so a runtime value check cannot see it: that is why `ThermalCapacity` is two
distinct `Quantity` types and the swap is a compile error
(`rho-and-cp-are-not-interchangeable`).

## Inputs and finiteness

`rho` and `cp` each finite and `> 0`, named separately
(`InvalidDensity`, `InvalidSpecificHeat`; the density first if both are bad),
no `abs` (mutations E30, E31). `rho = 1e200`, `cp = 1e200` are each finite and
positive and their product `rho cp V` overflows: refused as
`NonFiniteResult`, never emitted (E32).

## Dependence on conductivity: none

`computeTet4Capacity` takes the geometry and the `ThermalCapacity` pair only.
`Ce.total()` is `rho cp V` and not `k V`, `k rho V` or `k cp V`
(`CapacityNeedsNoConductivity`); passing a conductivity is a compile error
(`capacity-takes-no-conductivity`).
