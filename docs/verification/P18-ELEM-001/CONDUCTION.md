# P18-ELEM-001 — the conduction matrix Ke

```text
SUBJECT:  Ke = V k G^T G  [W/K], 4 x 4, for constant isotropic k on a Tet4
TESTS:    ThermalElement_UnitTetConductionAnalytical, _GeneralTetConductionOracle,
          _ConductionSymmetric, _ConductionConstantTemperatureNull,
          _ConductionRowSumsZero, _ConductionPositiveSemidefinite,
          _ConductionRankThree, _ConductionEnergyIdentity,
          _ConductivityScaling, _ConductionRejectsInvalidConductivity,
          _ConductionNeedsNoCapacity
```

## Formula and why no quadrature

```text
Ke_ij = integral_V grad Ni . k grad Nj dV = k V (grad Ni . grad Nj)
```

`G` is constant over a linear tetrahedron, so the integrand is constant and
`Ke = V k G^T G` is **exact**. There is no quadrature loop. `computeTet4Conduction`
takes the geometry and `k` only: no density, no specific heat, no material
object, so a steady analysis (whose resolved `ThermalMaterial::capacity()` is
empty) forms `Ke` without ever asking for `rho` or `cp`
(`ConductionNeedsNoCapacity`, `SteadyMaterialIntegration`).

`k` must be finite and `> 0`: refused as `InvalidConductivity` otherwise. No
`abs`, no `max(k, eps)` (mutations E19, E20).

## Unit tetrahedron, k = 6 W/(m K) — the hand matrix

`k/6 = 1`, so `Ke = G^T G` exactly, written out in the test:

```text
          | 3 -1 -1 -1 |
Ke   =    |-1  1  0  0 |   W/K       actual: identical, max |Ke - hand| = 0.00e+00
          |-1  0  1  0 |                     max relative error          0.00e+00
          |-1  0  0  1 |
```

Eigenvalues by hand `0, 1, 1, 4`; measured `-4.55e-16, 1, 1, 4` (relative
tolerance 1e-13 on the three positive ones).

## General tetrahedron against the independent oracle

```text
k = 42.75 W/(m K)   general fixture:  max relative |Ke - Ke_oracle|   1.96e-16
                    over 29 fixtures: worst                            1.21e-15
```

## Invariants

```text
symmetry        max |Kij - Kji|          0.00e+00 over 54 fixtures
                (exact: the dot product is commutative term by term; Ke is
                 not symmetrised -- there is no 0.5 (K + K^T) in the source)
constant mode   worst |Ke 1| / max|Ke|  2.76e-16;  |1^T Ke 1| <= 1e-13 |Ke|
add-constant    Ke (T + c 1) = Ke T:     flow change 1.96e-16; T^T Ke T unchanged
row sums        worst |sum_j Kij| / |Ke| 2.74e-16;  sum of all entries ~ 0
diagonal        Kii > 0 on every fixture (k V |grad Ni|^2)
PSD             min x^T Ke x / (|Ke||x|^2) over 50 random x per fixture
                = 9.03e-06 (the sliver), never negative beyond -1e-14
spectrum        one eigenvalue at zero: worst |lambda_0| / lambda_3 = 1.79e-16;
                three positive
rank / nullity  3 / 1 on every fixture; the null vector is parallel to
                [1 1 1 1]^T (components equal to 1e-7)
```

**Positive semidefinite, not positive definite.** A free element has the
uniform temperature as a genuine null mode; nothing regularises it
(mutations E16, E18 add an epsilon to the diagonal or break the mode and are
killed). Whatever removes the null mode is a boundary condition (later
milestones), not element physics.

**Off-diagonal signs are not asserted.** For an obtuse tetrahedron an
off-diagonal term can be positive, so an M-matrix pattern is not a universal
invariant (brief §105).

**The sliver.** The 1000:1 tetrahedron has `max|Ke| = 7.125e+03 W/K` and a
condition number of about 1e6, so its three positive eigenvalues sit closer
to the null one: asserted `> 1e-9 lambda_max`, still twelve orders above the
null eigenvalue. It is computed and finite: element-level quality acceptance
is P18-VALID-001's, not this kernel's.

## Weak-form energy identity, with an independent right-hand side

`Te^T Ke Te = k V |G Te|^2`. For the affine field
`T = a + 7.2 x - 3.5 y + 11.4 z` the gradient is `(7.2, -3.5, 11.4)` **by
hand**, so the right-hand side is `k V (7.2^2 + 3.5^2 + 11.4^2)`, with `V`
from the independent `[1 x y z]` determinant, and `a` must disappear:

```text
offsets a = 305.7 and -40.0 on every fixture:
worst relative error                 5.19e-08   (the sliver, 305.7 K offset)
in rounding units eps |T|^T|Ke||T|   0.284      (asserted <= 64)
net nodal flow, sum_i (Ke T)_i       0.288 eps 1^T|Ke||T|   (asserted <= 64)
```

The `5e-8` relative error is the cancellation of a 305.7 K offset against a
1e3-scale `Ke`, and it is **0.28 of a rounding unit**: the bound is the one
the arithmetic can achieve, stated in its own units. The nodal flows sum to
zero because conduction redistributes heat inside the element and creates
none.

## Conductivity scaling

`Ke(c k) = c Ke(k)` for `c` in `{0.25, 2, 10}` and for `k` in
`{0.2, 15, 400} W/(m K)`, to 1e-15 relative.

## Not in scope

Anisotropic conductivity (`Ke = V G^T K G` with a tensor) is **DEFERRED**:
ADR-049 refuses it and P15 stores one scalar `k` with no tensor to reduce.
Temperature-dependent `k` is likewise deferred (ADR-049, ADR-027). The kernel
never samples a material table.
