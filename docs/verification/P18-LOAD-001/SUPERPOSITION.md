# P18-LOAD-001 — superposition, ordering and attribution

```text
SUBJECT:  how several loads combine, in what order, and how the total is
          attributed
```

## Linear, and nothing is deduplicated by target

Thermal conduction with prescribed loads is linear, so loads **add**. Two loads
on the same face with different identities are two loads: their shares are
summed node by node, and the result equals one load of the summed flux.
Nothing is merged because the resolved facets happen to be equal.

```text
L1 = 300 W/m^2 and L2 = 125 W/m^2, same end cap, ids 1 and 2
Q(L1, L2)   vs   Q(L1) + Q(L2)        max difference 0 (<= 1e-15 of the scale)
Q(L1, L2)   vs   Q(425 W/m^2)         <= 1e-14 of the scale
totals      0.36 W + 0.15 W = 0.51 W
different faces: end cap +300, start cap -80  ->  (300 - 80) * 1.2e-3 W
```

Volume (1e6 and 3.5e5 W/m^3) and mixed (750 W/m^2 on the cap plus 4e5 W/m^3)
superpose the same way: total `q'' A + q''' V = 0.9 + 9.6 = 10.5 W` against 10.5.

## Cancellation

`+L` and `-L` on one target cancel: the surface residual is exactly 0 (two terms
per node); the volumetric one is 2.2e-15 W against entries of 12 W, which is
0.83 eps, because `a + b + c - a - b - c` is not bit-reversible across many
tetrahedra per node. The test asserts it in rounding units rather than claiming
exactness it does not have. An `abs` anywhere would double instead of cancel.

## Order

The loads are processed **in the order the definition gives**, which P18-DATA
fixed as canonical (insertion order, compared by `operator==`, persisted as
given). Preparation invents no ordering of its own. Within a load: facets
ascending (P16's deduplicated set), corners in stored topology order. One serial
pass into a dense `Ndof` vector; no unordered container, no parallel reduction.

```text
the same loads in two orders    equal to 1e-14 of the scale (addition is not associative)
the same order twice            bitwise identical (50 repeats, 0 differences)
the contributions               listed in the order given
```

## Attribution is kept, and never double-counted

Each load keeps its own `IntegratedHeat { entities, area, volume, heatRate }`;
the combined vector is the superposition. The per-kind totals P18-BALANCE-001
will need are derived, never stored beside the vector:

```text
appliedSurfaceHeatRate()     sum of the surface loads' heat rates, in load order
volumetricGenerationRate()   sum of the volumetric loads' heat rates, in load order
totalHeatRate()              the sum of the VECTOR, in unknown order

mixed: surface 0.9 W + volume 9.6 W = 10.5 W = the vector's total
```

The global total is the vector's own sum; it is not formed by adding the
combined vector to the per-load figures, so nothing is counted twice. A
mutation that made the surface total include the volumetric loads (L36) is
killed.

## One invalid load refuses the set

If any load fails, the whole preparation fails and **no** vector is returned:
a vector containing only the valid loads would silently drop the user's
intent. The diagnostic names the first bad load in canonical order and not the
valid ones (`OneInvalidLoadRefusesTheWholeSet`, `DiagnosticsAreDeterministic`:
20 repeats, same code, same message).

## Not implemented here

No heat balance (P18-BALANCE-001), no reaction heat at prescribed
temperatures, no convection source, no global assembly. The vector is returned
to P18-ASSEMBLY-001, which owns the system.
