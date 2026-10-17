# P18-DOF-001 — the free-equation map

```text
ThermalFreeEquationIndex   a row of the reduced system; zero-based; no
                           invalid value; absence is std::optional
ordering                   ascending ThermalDofIndex among the free unknowns
storage                    O(Nfree): the free unknowns in row order; the
                           forward direction is a binary search, the inverse
                           an index
binding                    the DOF map's MeshStamp and unknown count;
                           agreesWith(constraints) re-derives the complement
```

## The oracle, by hand

```text
Ndof = 5, prescribed 1 and 4

DOF 0 -> row 0
DOF 1 -> none        (prescribed; NOT row 0)
DOF 2 -> row 1
DOF 3 -> row 2
DOF 4 -> none

Nfree = 3, Nconstrained = 2, Nfree + Nconstrained = Ndof

reverse:  row 0 -> DOF 0,  row 1 -> DOF 2,  row 2 -> DOF 3,  row 3 -> none
```

`ThermalDof_FreeEquationMap` asserts every line; `freeDofs()` is the
row-to-unknown table, ascending.

## The invariant suite

`ThermalDof_EveryDofIsFreeXorConstrainedAndRowsAreContiguous`, Ndof 10,
unique constraints {1, 4, 7}:

```text
every unknown free xor constrained          asserted for all ten, and
                                            cross-checked against the
                                            ThermalConstraintMap's own answer
                                            (two independently built objects)
rows exactly {0 .. 6}                       as a set, and assigned in ascending
                                            unknown order
row -> unknown -> row                       the identity for every free unknown
an unknown of another size                  neither free nor constrained: absent
```

Mutations D14 (row = unknown) and D15 (a constrained unknown gets row 0) are
killed.

## No raw subtraction

There is no `freeEq = dof - constraintsBefore` anywhere; the map is the one
place rows are assigned, by one ascending walk, and a caller that wants a
row asks `equationOf`. The reconstruction helper walks the two ascending
lists once and never subtracts either.

## Both extremes

```text
Nconstrained = 0    every unknown is a row; row d = unknown d; accepted
Nfree = 0           no rows; every unknown prescribed; accepted; the full
                    vector is the prescribed vector
```

(`ThermalDof_FullyConstrainedAndNoConstraintsAreSupported`). Neither is a
DOF-layer error.

## Determinism

`ThermalDof_FreeEquationOrderingDeterministic`: the same constraint set in
every insertion order gives the same free map; 16 repeats are equal; and a
fresh mesh with the same handles gives a free map whose row table is equal
and whose VALUE is not, because the stamp differs -- the two things a
consumer must not confuse.
