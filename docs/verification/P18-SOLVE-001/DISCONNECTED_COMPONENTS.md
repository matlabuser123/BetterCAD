# P18-SOLVE-001 — disconnected thermal regions

```text
RULE:  every independent thermal region must be well posed.  NOT: the mesh must be one component.
```

A model may legitimately have several bodies or a body cut by prescribed temperatures into pieces. The
solver does not reject disconnection; it rejects an UNANCHORED region. The decision is made on the
connected components of the graph of the **nonzero** entries of the reduced matrix `Kff`: a stored
exact zero (the pattern holds them, being a function of connectivity alone) joins nothing.

| Case | Components | Anchors | Expected | Result |
| --- | --- | --- | --- | --- |
| A = {0,1,2} held by `T0 = 300 K`; B = {3,4} pure conduction | 2 | A | refuse | `unanchored_thermal_region` ("1 of the model's 2") |
| the same B tied to a 300 K ambient by 0.5 W/K, 10 W applied | 2 | A and B | solve | T_A = 300, T_B = 320 K (0.5 (T - 300) = 10) |
| A held at 300 K, B held at 400 K at an end node | 2 | A and B | solve | agrees with the dense oracle to 1e-10 |
| a pure-Neumann 2-node and 6-node chain | 1 | none | refuse | `missing_temperature_datum` |
| blocks joined only by stored zeros: {0,1} unanchored, {2} anchored | 2 | {2} | refuse | `unanchored_thermal_region` (and the analysis reports 2 regions, not 1) |

Probes S21 (stored zeros connect regions), S22 (one anchored row anchors every region), S23 (every
disconnected model rejected) and S24 (the two classifications swapped) are all killed.

The distinction between `missing_temperature_datum` and `unanchored_thermal_region` is deliberate:
the first says the model has no absolute temperature anywhere; the second says some part of it does
and some part does not, which is the disconnected-body or cut-off-region case. Neither claims a cause
the matrix cannot show: the message names the count of regions and one unknown inside the first
unanchored one, and nothing about the geometry.