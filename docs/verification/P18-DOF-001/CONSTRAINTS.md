# P18-DOF-001 — the temperature constraint map

```text
representation   ThermalTemperatureConstraint { ThermalDofIndex dof;
                                                Temperature temperature; }
unit             K, absolute, through the units framework; no double, no
                 Celsius (there is no Celsius unit in the tree)
ordering         ascending ThermalDofIndex, unique
binding          the DOF map's MeshStamp and unknown count
```

## The P18-specific fact

P17's `ConstraintSet` records WHICH degrees of freedom are prescribed and
carries no value, because a structural restraint holds a displacement at
zero. A thermal Dirichlet condition holds a node at an arbitrary absolute
temperature, so the value is part of the constraint, and the reduced steady
system a later solve forms is

```text
    Kff Tf = Qf - Kfc Tc        with Tc != 0 in general
```

`uc = 0` is NOT copied from P17. Measured
(`ThermalDof_ConstraintMapStoresNonzeroTemperature`): unknown 1 at 300 K and
unknown 4 at 350 K come back as exactly 300 and 350, a free unknown has NO
prescribed temperature (not 0 K), and the constraint type cannot be written
without its unknown nor given a bare double (compile-fail
`a-constraint-takes-a-temperature-not-a-double`). Mutations D09 (the map
returns 0 K) and D10 (reconstruction writes 0 K) are killed.

## Validation

| Input | Outcome | Problem | Test |
| --- | --- | --- | --- |
| an unknown outside 0 ..< `dofCount()` | refused, `NotFound` | `DofNotInMap` | `ConstraintMapRefusesUnusableInput` |
| NaN, ±inf, below 0 K | refused, `InvalidArgument` | `TemperatureNotAbsolute` | same |
| 0 K | accepted (the limit of the scale, as everywhere in P18) | — | same |
| the same unknown twice at the same temperature | one constraint, counted once | — | `DuplicateIdenticalConstraintDeduplicates` |
| the same unknown at two temperatures, any order | refused, both values named | `ConflictingTemperature` | `ConflictingConstraintFails` |
| 300 K and 300.000001 K on one unknown | conflict: exact equality decides sameness | `ConflictingTemperature` | same |
| nothing prescribed | accepted; `Nfree = Ndof` | — | `FullyConstrainedAndNoConstraintsAreSupported` |
| everything prescribed | accepted; `Nfree = 0` | — | same |

The absolute-temperature rule is P18-DATA-001's `isAbsoluteTemperature`,
called rather than restated, so the canonical boundary condition and the
mathematical constraint refuse the same values. Whether a system with no
prescribed temperature is solvable (it has a null mode) or one with every
temperature prescribed is useful (it has nothing to solve) is
P18-SOLVE-001's and P18-VALID-001's question; the DOF layer represents both.

## Ordering and cardinality, measured

`ThermalDof_ConstraintMapDeterministic`: unknowns {8, 2, 5} listed in all
six orders give one map, ordered 2, 5, 8, each carrying its own temperature;
identical over 16 repeats. `Nconstrained` counts UNIQUE unknowns:

```text
Ndof 10, unique constraints {1, 4, 7}     Nconstrained 3, Nfree 7
DOF 4 = 300 K, DOF 4 = 300 K, DOF 1 = 310  Nconstrained 2, not 3
DOF 4 = 300 K, DOF 4 = 350 K               refused: "unknown 4 is prescribed
                                            both 300 K and 350 K"
```

Mutation D11 (duplicate counted twice) and D12 (last value wins) are killed.

## Attribution is not here

Which canonical boundary condition produced a constraint, and which CAD face
it came from, is P18-BC-001's to record. At this layer the final mathematical
constraint appears once per unknown, with its temperature, and the
`NodeId` it passed through on the way is not stored.

## Time

ADR-049 defers the time-varying load and BC policy to P18-TRANSIENT-001 and
asks the data model to leave room. The constraint map stores a constant
temperature per unknown and no function, table or callback; a time-varying
prescription, if ever authorized, is a new constraint map per time level
built from the same two inputs, not a change to this type.
