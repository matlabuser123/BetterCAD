# P18-BC-001 — the sign convention

```text
SUBJECT:  one convention for the convective boundary, written once, shared by
          the boundary conditions, assembly, balance, the CLI and the viewer
```

## Frozen here

```text
outward normal n          the unit normal of the boundary facet pointing OUT of the body
Robin law                 -k grad(T) . n  =  h (T - T_inf)
positive h (T - T_inf)    heat LEAVING the body through the face
```

So:

| Boundary temperature | `h (T - T_inf)` | Meaning |
| --- | --- | --- |
| `T > T_inf` | positive | the body loses heat to the ambient |
| `T = T_inf` | zero | no exchange |
| `T < T_inf` | negative | the ambient heats the body |

## How it appears in the matrices

```text
(K + Kconv) T = Q + Qconv         Kconv = integral h N^T N dA       positive, on the LEFT
                                  Qconv = integral h T_inf N^T dA   positive for T_inf > 0, on the RIGHT
residual of a convection facet    Kconv T - Qconv                   heat LEAVING, outward positive
```

Derived in WEAK_FORM.md, not chosen.

## Agreement with the rest of the tree

| Source | Statement | Same law? |
| --- | --- | --- |
| P18-DATA-001 `ConvectionBoundary` | `q = h (T_ambient - T)`, positive into the body | yes: `q = -(h (T - T_inf))` |
| P18-DATA-001 `HeatBalance` | every term positive INTO the body; convection negative when the body is hotter | yes: `convectionHeat = -sum(residual)` |
| P18-LOAD-001 surface flux | positive into the body | consistent: a flux load and a convection loss are the same sign family |
| ADR-049 | `-k grad(T) . n = h (T - T_inf)` | identical |

The test `ThermalBC_SignConvention` checks the identity numerically: for the
reference triangle at 350 K against a 300 K ambient the outward residual sum is
`+600 W`, P18-DATA's into-the-body figure is `-600 W`, and they are negatives of
each other; at 250 K the signs reverse.

## Orientation does not enter

Both terms depend on the UNSIGNED facet area, so reversing a triangle's vertex
order cannot change either, and no normal is computed (`TriangleWindingDoesNot
ChangeTheTerms`). The word "outward" belongs to the physics of the law, not to a
quantity the code evaluates.

## Prescribed temperature has no sign question

`T = T_bar` is a value. The reaction heat a prescribed face must supply is
P18-SOLVE-001's and P18-BALANCE-001's, in the same into-the-body convention.
