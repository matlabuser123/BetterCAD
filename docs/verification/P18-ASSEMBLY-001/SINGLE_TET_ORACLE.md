# P18-ASSEMBLY-001 — the single-tetrahedron oracle

```text
SUBJECT:  one unit tetrahedron assembled to a 4 x 4 system, against matrices
          derived by hand
```

Mesh: nodes `1 (0,0,0)`, `2 (1,0,0)`, `3 (0,1,0)`, `4 (0,0,1)`; one tetrahedron
`[1 2 3 4]`; `V = 1/6`; positively oriented.

## NodeId to unknown

```text
NodeId   1  2  3  4
dof      0  1  2  3        (the rank of the handle among the strictly increasing handles)
```

## Expected, actual, error

`k = 6 W/(m K)`, `rho cp = 120 J/(m^3 K)`, generation `q''' = 24 W/m^3`.

```text
shape gradients      (-1,-1,-1) (1,0,0) (0,1,0) (0,0,1)
k V                  6 * 1/6 = 1

expected K  = G^T G   [  3 -1 -1 -1 ]
                      [ -1  1  0  0 ]    W/K
                      [ -1  0  1  0 ]
                      [ -1  0  0  1 ]
actual K              identical, 16 stored entries      max entry error 0.00e+00

expected C  = rho cp V / 20 [2 1 1 1; ...]
            = (120/6/20) = 1   [ 2 1 1 1; 1 2 1 1; 1 1 2 1; 1 1 1 2 ]   J/K
actual C              identical                         max entry error 0.00e+00

expected Q  = q''' V / 4 = 24 / 6 / 4 = 1 W at each corner      [1 1 1 1]
actual Q              identical                         error 0.00e+00 (<= 1e-15)
sum Q                 4 W = q''' V
```

The composite (K, C and Q together) also matches an independent dense oracle to
`1e-15`; `K 1` is zero to `1e-15` and `1^T C 1 = 20 J/K = rho cp V` to `1e-14`.

The expected matrices are typed into the test from the derivation above, not computed
by the element kernel. The assertion that the kernel agrees with the same numbers is
P18-ELEM-001's; here it shows the scatter into a 4 x 4 global matrix changes nothing.

Tests: `ThermalAssembly_SingleTet{Conductivity,Capacity,Generation,CompositeSystem}Analytical`.