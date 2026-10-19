# P18-BC-001 — the weak form, and why the signs are what they are

```text
SUBJECT:  the derivation of Kconv and Qconv from the strong form, under the
          project's equation convention K T = Q
METHOD:   written out, then checked against the single-triangle numbers in
          ANALYTICAL_VALIDATION.md. No sign was chosen by intuition.
```

## The strong form

Steady conduction with generation, on a body Omega with boundary Gamma,
`n` the OUTWARD unit normal, Fourier's law `q = -k grad(T)`:

```text
div( k grad(T) ) + q''' = 0        in Omega
```

Two kinds of boundary, with Gamma = Gamma_T union Gamma_h:

```text
T = T_bar                          on Gamma_T       prescribed temperature
-k grad(T) . n = h (T - T_inf)     on Gamma_h       convection (Robin)
```

The left side of the Robin law is the conductive heat flux LEAVING the body
through the boundary. The right side is the convective loss to the ambient. So
`T > T_inf` loses heat, `T < T_inf` gains it, `T = T_inf` exchanges none.

## The weak form

Multiply by a test function `v` that vanishes on Gamma_T and integrate:

```text
integral_Omega  v div( k grad T ) dV  +  integral_Omega v q''' dV  =  0
```

Integrate the first term by parts (the divergence theorem):

```text
integral_Omega v div( k grad T ) dV
    =  - integral_Omega k grad(v) . grad(T) dV  +  integral_Gamma v k grad(T) . n dA
```

On Gamma_T the boundary integral vanishes because `v = 0` there. On Gamma_h the
Robin law gives `k grad(T) . n = -h (T - T_inf)`, so

```text
integral_Gamma_h v k grad(T) . n dA  =  - integral_Gamma_h v h T dA  +  integral_Gamma_h v h T_inf dA
```

Substituting:

```text
- integral k grad(v).grad(T) dV  -  integral_Gamma_h h v T dA  +  integral_Gamma_h h T_inf v dA  +  integral v q''' dV  =  0
```

Move everything that contains the unknown `T` to the left and everything that
does not to the right (multiplying through by -1):

```text
 integral k grad(v).grad(T) dV  +  integral_Gamma_h h v T dA   =   integral v q''' dV  +  integral_Gamma_h h T_inf v dA
 \_____________________________/    \________________________/        \_______________/     \____________________________/
          K T  (conduction)               Kconv T  (Robin)                  Q  (generation)         Qconv  (Robin source)
```

With the interpolation `T ~ N^T T_nodal` and `v = N_i`:

```text
(K + Kconv) T  =  Q + Qconv

K      = sum_e  integral k G^T G dV                       [W/K]    (P18-ELEM-001)
Kconv  = integral_Gamma_h  h N^T N dA                      [W/K]
Qconv  = integral_Gamma_h  h T_inf N^T dA                  [W]
```

So convection contributes **a positive matrix on the left and a positive source
on the right**, under the project's `K T = Q` convention. Both, always: with
only `Qconv` a face is a heat source that cannot relax; with only `Kconv` it is
a sink that cools toward 0 K.

**Neither contains the conductivity.** `k` appears in the strong law and is
consumed by the substitution `k grad(T).n = -h (T - T_inf)`. Writing `Kconv`
proportional to `k h` would be dimensionally wrong: W/(m K) times W/(m^2 K) times
m^2 is W^2/(m K^2), not W/K. The unit tests (a conductivity-scaled matrix is shown
to fail the whole-matrix oracle) and the compile-fail group (a conductivity is not
an argument) both reject it.

## The consistent integrals on a linear triangle

For a three-node triangle of area `A` the shape functions are the barycentric
coordinates, and `h`, `T_inf` are constant:

```text
integral_A N_i N_j dA   =  A/6   (i = j)        A/12  (i != j)
integral_A N_i dA       =  A/3
```

Therefore

```text
Kconv_f = (h A / 12) | 2 1 1 |        Qconv_f = (h T_inf A / 3) | 1 |
                     | 1 2 1 |                                  | 1 |
                     | 1 1 2 |                                  | 1 |
```

Units: `h A` is W/(m² K) · m² = W/K; `h T_inf A` is W/K · K = W; and `Kconv T` is
W, the unit of a row of `K T = Q`. The compiler checks all three
(`static_assert` in `ThermalConvection.hpp`).

## Exactness, and the two traps

The integrals are exact for a constant `h`. No quadrature is involved, and the
two obvious quadratures are wrong in different ways:

| Rule | Matrix it gives | Row sums | Verdict |
| --- | --- | --- | --- |
| consistent (this) | `hA/12 [2 1 1; 1 2 1; 1 1 2]` | `hA/3` | exact |
| one-point at the centroid, `N_i = 1/3` | `hA/9` in EVERY entry | `hA/3` | wrong interpolation; has a null vector `[1 -1 0]` the true matrix does not |
| lumped | `hA/3 I` | `hA/3` | no coupling; wrong interpolation |

All three have the same row sums, so a conservation or row-sum test cannot tell
them apart. `ThermalBC_ConvectionRejectsCentroidMatrixMutation` and
`ThermalBC_ConvectionIsNotLumped` therefore compare the **whole** matrix, and show
that the oracle rejects the two wrong matrices. (The one-point rule happens to
be exact for `Qconv`, whose integrand is linear; the source is nevertheless
written in closed form so it is not coupled to the matrix's rule.)

Spectrum of the consistent matrix: `hA/3` once (eigenvector `[1 1 1]`) and
`hA/12` twice (`[1 -1 0]`, `[1 0 -1]`); positive definite on the three facet
shape functions for `h A > 0`. Whether the *assembled* system is singular is
P18-SOLVE-001's question.

## The constant-ambient identity

With `T = T_inf` on the facet,

```text
Kconv T - Qconv = (hA/3) T_inf [1 1 1]^T - (h T_inf A/3) [1 1 1]^T = 0
```

and for a uniform `T_b` the three residual entries sum to `h A (T_b - T_inf)`: the
heat leaving the body, positive when the face is hotter than its ambient. This
ties the matrix to the source and is the strongest single test of the pair.

## Where the signs meet the rest of the system

| Place | Statement |
| --- | --- |
| P18-DATA-001 `ConvectionBoundary` | `q = h (T_ambient - T)`, positive INTO the body: the same law, read from the other side |
| `HeatBalance::convectionHeat` | heat INTO the body; negative when the body is hotter than its ambient. Equal to the NEGATIVE of the residual sum above |
| this milestone | the residual `Kconv T - Qconv` is heat LEAVING, positive outward |
| P18-ASSEMBLY-001 (later) | adds `Kconv` to `K` and `Qconv` to `Q`; no sign change |
| P18-BALANCE-001 (later) | `convectionHeat` is the negative of the residual sum over the convection facets |

## Dirichlet

Nothing is added to `K` or `Q` for a prescribed temperature. The solve reduces
the system exactly,

```text
K_ff T_f = Q_f - K_fc T_c        T_c = the prescribed values
```

(P18-SOLVE-001). A penalty (`K_ii += big`, `Q_i += big T_bar`) is a different,
inexact method and does not exist in this milestone.
