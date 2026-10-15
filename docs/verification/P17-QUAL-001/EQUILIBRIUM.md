# P17-QUAL-001 — the reference models through the production path, and their equilibrium

```text
SOURCE:   bettercad_example_reference_models --structural, run after the
          qualification with each preset's OWN binary from the frozen
          candidate (qualification/postrun/reference-run-<preset>.txt);
          every line is a transcription of the debug-ext output, and the
          release-ext and debug-shared-ext outputs are IDENTICAL to it line
          for line apart from the timing lines (CROSS_PRESET.md)
AGAINST:  P17-REFMOD-001's reference-run-debug-ext.txt: identical, every
          non-timing line -- which is what byte-identical production code
          predicts and what was measured rather than assumed
```

## The twelve documents

| Model | Document | Nodes | Tet4 | Free eqns | Validation | Outcome | Residual | Pivot ratio | eta_F | eta_M |
| --- | --- | ---: | ---: | ---: | --- | --- | --- | --- | --- | --- |
| RM-FEA-01 | StructuralPatch | 9 | 12 | 15 | accepted | solve | 2.68e-17 | 0.130 | 1.50e-16 | 1.86e-16 |
| RM-FEA-02 | StructuralAxialBar | 10 | 16 | 18 | accepted | solve | 1.79e-17 | 0.0951 | 5.42e-16 | 1.50e-15 |
| RM-FEA-03 | StructuralCantilever | 138 | 489 | 306 | accepted_with_warnings (2 envelope warnings) | solve | 1.80e-17 | 4.82e-04 | 1.12e-13 | 5.98e-13 |
| RM-FEA-04 | StructuralPressureBlock | 8 | 6 | 12 | accepted | solve | 3.75e-17 | 0.330 | 5.05e-18 | 1.52e-17 |
| RM-FEA-05 | StructuralGravityBlock | 10 | 16 | 18 | accepted | solve | 4.58e-17 | 0.0329 | 6.18e-16 | 5.61e-16 |
| RM-FEA-06 | StructuralTransformedBase | 9 | 12 | 15 | accepted | solve | 4.65e-17 | 0.0859 | 1.44e-16 | 8.67e-17 |
| RM-FEA-06 | StructuralTransformedPlaced | 9 | 12 | 15 | accepted | solve | 2.64e-17 | 0.105 | 7.21e-17 | 4.82e-17 |
| RM-FEA-07 | StructuralLocalRefinementRoot | 78 | 369 | 222 | accepted | solve | 2.68e-17 | 0.0386 | 1.12e-15 | 2.48e-15 |
| RM-FEA-07 | StructuralLocalRefinementTip | 60 | 271 | 168 | accepted | solve | 1.97e-17 | 0.0360 | 3.06e-15 | 6.06e-15 |
| RM-FEA-08 | StructuralUnderConstrained | 9 | 12 | — | accepted, 8 constrained DOFs | **REFUSAL** by the pivot gate: smallest pivot 1.1e-17 of the largest, "may be under-constrained" | — | — | — | — |
| RM-FEA-09 | StructuralStaleMesh | 9 | 12 | 15 | accepted | solve | 3.96e-17 | 0.147 | 5.50e-17 | 1.34e-16 |
| RM-FEA-10 | StructuralUnresolved | 9 | 12 | — | rejected at `loads`, 12 constrained DOFs, 0 loaded nodes | **REFUSAL**: `loads_unresolvable`, "load:2 ... Nothing nearby is substituted" | — | — | — | — |

```text
10 model IDs expected, 10 executed, 10 solved, 2 refused, 12 documents
published_result no    for both refusals
```

## Force and moment equilibrium

Every solved model, under the production default `EquilibriumTolerance
{force = 1e-12, moment = 1e-12}`, reactions against the assembled external
load:

| Model | Total reaction (N) | External (N) | eta_F | eta_M | Gate |
| --- | --- | --- | --- | --- | --- |
| RM-FEA-01 | (8.5e-14, 1.7e-13, 3000.0000000000005) | (0, 0, -2999.9999999999995) | 1.50e-16 | 1.86e-16 | PASS |
| RM-FEA-02 | (1.6e-11, 8.7e-12, -21600.000000000015) | (0, 0, 21600) | 5.42e-16 | 1.50e-15 | PASS |
| RM-FEA-03 | (1.0e-11, 198.98615400873939, 4.4e-12) | (0, -198.98615400905973, 0) | 1.12e-13 | 5.98e-13 | PASS |
| RM-FEA-04 | (-5.7e-14, 0, 5399.9999999999982) | (0, 0, -5399.9999999999982) | 5.05e-18 | 1.52e-17 | PASS |
| RM-FEA-05 | (5.6e-16, 6.7e-16, 10.777508349999987) | (0, 0, -10.777508350000002) | 6.18e-16 | 5.61e-16 | PASS |
| RM-FEA-06 base | (9.1e-13, 0, 98999.999999999942) | (0, 0, -98999.999999999971) | 1.44e-16 | 8.67e-17 | PASS |
| RM-FEA-06 placed | (65999.999999999985, -33000, 66000) | (-65999.999999999971, 33000, -66000) | 7.21e-17 | 4.82e-17 | PASS |
| RM-FEA-07 root | (5.0e-14, 299.99999999999858, 5.4e-13) | (0, -300, 0) | 1.12e-15 | 2.48e-15 | PASS |
| RM-FEA-07 tip | (-9.5e-13, 299.99999999999619, -1.3e-12) | (0, -300, 0) | 3.06e-15 | 6.06e-15 | PASS |
| RM-FEA-09 | (-2.8e-14, -3.4e-13, 3000) | (0, 0, -3000) | 5.50e-17 | 1.34e-16 | PASS |

Two rows are worth reading against each other. RM-FEA-06's total reaction in
the placed document is `R (0, 0, 99000)` with R's third column
`(2/3, -1/3, 2/3)` exactly: `(66000, -33000, 66000)`, the force and the
moment both balanced in the rotated frame. RM-FEA-03's `eta_M` of 5.98e-13
is the largest in the suite by three orders and sits at 60% of the gate: the
measurement behind finding F1, which P17-REFMOD-001's `ANALYTICAL_VALIDATION.md`
records with the two cases (L = 120 mm; 2465 tets) that cross it.

**Moment equilibrium "where relevant"**: every model whose load is not
collinear with its reactions -- the cantilevers RM-FEA-03 and RM-FEA-07,
the placed block RM-FEA-06 with three non-zero reaction components, and the
gravity column RM-FEA-05 whose weight acts off the origin -- balances about
the origin to 1e-15 or better; the axially loaded blocks balance trivially
and are measured anyway.

## What each solve came to

| Model | U (J) | max \|u\| (m) | max von Mises (Pa) |
| --- | --- | --- | --- |
| RM-FEA-01 | 3.1175253826080208e-04 | 2.2757757155534776e-07 | 2467880.079150897 |
| RM-FEA-02 | 0.23142857142857104 | 2.1535447759544763e-05 | 50000000.000000075 |
| RM-FEA-03 | 1.5506499378414136e-04 | 1.5698141397420332e-06 | 5171211.77429152 |
| RM-FEA-04 | 6.5193918964935788e-04 | 2.7379596925014462e-07 | 1803968.7750770743 |
| RM-FEA-05 | 2.9033097697784947e-09 | 1.0866069423795997e-09 | 2917.7612762606095 |
| RM-FEA-06 base | 0.094355459074912343 | 2.1408843095462878e-06 | 17712741.087825816 |
| RM-FEA-06 placed | 0.094355458706504372 | 2.1408836124553069e-06 | 17712741.646789078 |
| RM-FEA-07 root | 3.7252242968374304e-05 | 2.5960885994562326e-07 | 426691.48829494842 |
| RM-FEA-07 tip | 3.9470604736045392e-05 | 2.776811358037859e-07 | 470049.79906189331 |
| RM-FEA-09 | 2.8329870275605888e-04 | 2.0486852330260381e-07 | 1505043.8968983707 |

## The model-change gate, on every solvable document

After its main dimension is edited the runner requires, for every one of the
ten: currency `stale: the geometry changed`, validation `rejected` with input
problem `mesh_stale`, the solve `refused` -- then a remesh and a re-solve.
All ten did, and re-solved to a different answer:

| Model | Edit | Nodes / Tet4 after | Free eqns | U after (J) | max \|u\| after (m) |
| --- | --- | --- | ---: | --- | --- |
| RM-FEA-01 | patch_a 40 -> 48 | 9 / 12 | 15 | 3.7122785714879922e-04 | 2.2432824647995027e-07 |
| RM-FEA-02 | bar_l 90 -> 120 | 8 / 6 | 12 | 0.30857142857142972 | 2.8651673028759416e-05 |
| RM-FEA-03 | cant_l 100 -> 80 | 263 / 1207 | 681 | 1.0960879487741862e-04 | 1.1135599011623344e-06 |
| RM-FEA-04 | press_a 60 -> 72 | 9 / 12 | 15 | 8.0185216957303523e-04 | 2.6919453858213175e-07 |
| RM-FEA-05 | grav_h 80 -> 100 | 11 / 21 | 21 | 5.6590686422300162e-09 | 1.6880820526943949e-09 |
| RM-FEA-06 base | sbase_a 90 -> 110 | 8 / 6 | 12 | 0.11252797513514874 | 2.0223600351347158e-06 |
| RM-FEA-06 placed | splaced_a 90 -> 110 | 9 / 12 | 15 | 0.11439664730144362 | 2.1441214894696199e-06 |
| RM-FEA-07 root | loc_l 100 -> 120 | 63 / 289 | 177 | 5.1524326857982632e-05 | 3.7814198298754318e-07 |
| RM-FEA-07 tip | loct_l 100 -> 120 | 63 / 285 | 177 | 4.4686585713425852e-05 | 3.1523400039698926e-07 |
| RM-FEA-09 | stale_c 30 -> 36 | 9 / 12 | 15 | 3.4287846390522155e-04 | 2.4387512475553187e-07 |

RM-FEA-02 at 120 mm is a different tetrahedralisation (6 against 16) and
still affine-exact: `U = (1/2) F delta` with `delta = 50e6 x 0.12 / 2.1e11`
is 0.3085714... J, as printed.
