# P16-QUAL-001 — final analytical, quality and determinism tables

```text
SOURCE:   every figure here is printed by the suite running on the FROZEN
          CANDIDATE -- fingerprint 8ab30a31, the tree the three presets
          qualified -- and not copied forward from an earlier milestone's logs
RUN:      bettercad_tests "[refmod][mesh],[p16qual]" on the qualified
          debug-ext binary: 34 cases, 23693 assertions, exit 0
          qualification/final-tables-run.txt
          qualification/p16qual-final-candidate.txt       the six gates
          qualification/reference-run-final-candidate.txt --meshes evidence,
          diffed against P16-REFMOD-001 and IDENTICAL but for timings
NOTE:     the figures are identical to P16-REFMOD-001's, which is the expected
          result and is itself a check: the reference models and their tests are
          byte-identical between the two candidates, so a difference would mean
          something non-deterministic had crept in
```

## Analytical volume table

The oracle is a closed form applied to the model's **own dimensions**, read from
its parameters. `tests/reference/Analytic.hpp` includes no BetterCAD and no OCCT
header, so it cannot call a production volume function — the rule is structural
rather than a matter of discipline.

```text
| Model      | FORMULA              | Vanalytic mm^3     | Vocct mm^3         | Vmesh mm^3         | REL ERR   | TOLERANCE          | PASS |
| RM-MESH-01 | abc                  | 294000             | 293999.99999999994 | 294000.00000000006 | 1.980e-16 | 1e-9 planar        | PASS |
| RM-MESH-02 | pi r^2 h             | 117809.72450961724 | 117809.72450961718 | 117212.51992517807 | 5.069e-03 | derived bound      | PASS |
| RM-MESH-03 | LWt - pi r^2 t       | 63517.699835307561 | 63517.699835307569 | 63560.69856538716  | 6.770e-04 | derived bound      | PASS |
| RM-MESH-04 | pi(Ro^2 - Ri^2) h    | 81430.081581047431 | 81430.081581047416 | 81017.293772282923 | 5.069e-03 | derived bound      | PASS |
| RM-MESH-05 | abc                  | 14400              | 14400              | 14399.999999999998 | 1.263e-16 | 1e-9 planar        | PASS |
| RM-MESH-06 | abc, base            | 118800             | 118799.99999999999 | 118800.00000000003 | 2.450e-16 | 1e-9 planar        | PASS |
| RM-MESH-06 | abc, placed          | 118800             | 118800             | 118799.99999999999 | 1.225e-16 | 1e-9 planar        | PASS |
| RM-MESH-07 | abc                  | 240000             | 240000             | 239999.99999999997 | 1.213e-16 | 1e-9 planar        | PASS |
| RM-MESH-08 | none -- no body      | n/a                | n/a                | n/a                | n/a       | n/a                | PASS |
```

`Vocct` is the kernel's own integration, used as a **second implementation**
rather than as the oracle. Worst OCCT-against-closed-form error across the
suite: **4.941e-16** relative.

### Tolerance rationale, and why three of them are not tolerances at all

```text
PLANAR BODIES            1e-9 relative, the repository's figure for geometric
                         accumulation. A planar body's facets tile the exact
                         solid, so only summation separates the sum from the
                         closed form. MEASURED WORST: 2.450e-16 -- seven orders
                         of margin, so this gate is not what is being measured

CAD vs CLOSED FORM       1e-12 relative, the repository's figure for
                         well-conditioned double-precision algebra.
                         MEASURED WORST: 4.941e-16

CURVED BODIES            a DERIVED TWO-SIDED BOUND, not a tolerance. An
                         inscribed chord polygon contains the disc of radius
                         r - d and lies in the disc of radius r, where d is the
                         declared surface deflection, so the volume is bounded
                         on both sides by closed forms. Nothing is fitted and no
                         segment count has to be known
```

```text
| Model      | BOUND, mm^3                              | MEASURED           | WIDTH    | WHERE  |
| RM-MESH-02 | [115465.31099187586, 117809.72450961724] | 117212.51992517807 | 2.010e-2 | inside, below |
| RM-MESH-03 | [63517.699835307561, 63798.086979640444] | 63560.69856538716  | 4.405e-3 | inside, ABOVE |
| RM-MESH-04 | [79318.342269212546, 82693.590876413087] | 81017.293772282923 | 4.167e-2 | inside |
```

**The direction is a prediction, not an allowance.** RM-MESH-03's lower bound
*is* its analytic volume, because an inscribed polygon removes **less** material
than the true circle — so its mesh must come out above, while RM-MESH-02's and
RM-MESH-04's come out below. A mesh that filled RM-MESH-03's hole would read
72000, exceeding the permitted upper bound by **29.32 times the bound's own
width**.

No tolerance was loosened at any point in P16 to obtain a pass. The one time a
comparison had to change — RM-MESH-02's convergence levels — it was because two
deflections produced the *same* boundary and the test was passing on one unit in
the last place; the fix made the gate stricter, not looser.

## Final quality table

Under P16-QUALITY-001's own `reportOnlyThresholds()`, which carries no
thresholds at all because quality thresholds are solver requirements and P17
owns them. So nothing can be a Warning or a Failure, and `satisfiesPolicy()`
reduces to structural validity — the honest answer when no solver has stated a
requirement.

```text
| Model                 | Tet4 | Inv | Warn | Fail | WorstAspect | WorstRadiusRatio | elem | MinDihedral | elem | MaxDihedral | MinTetVol mm^3 | PASS |
| MeshBlock             |    6 |   0 |    0 |    0 |     4.0933  |       0.43725    |    1 |   14.6211   |    1 |   115.641   | 49000          | PASS |
| MeshCylinder          |  508 |   0 |    0 |    0 |    16.0391  |       0.00124845 |  504 |    0.362683 |   54 |   177.113   | 0.475278       | PASS |
| MeshPlateWithHole     |  126 |   0 |    0 |    0 |    20.4856  |       0.0129063  |  120 |    1.11273  |    6 |   165.482   | 39.6433        | PASS |
| MeshTube              |  808 |   0 |    0 |    0 |    14.8994  |       0.00634594 |  462 |    2.3722   |  462 |   170.046   | 0.202694       | PASS |
| MeshThinPlate         |   12 |   0 |    0 |    0 |    80.0062  |       0.000322641|   12 |    0.71612  |   12 |   178.709   | 1199.93        | PASS |
| MeshTransformedBase   |   12 |   0 |    0 |    0 |     3.88104 |       0.127381   |    6 |   14.9313   |    6 |   152.325   | 9899.93        | PASS |
| MeshTransformedPlaced |   12 |   0 |    0 |    0 |     3.88104 |       0.127381   |    5 |   14.9314   |    5 |   152.325   | 9900           | PASS |
| MeshLocalRefinement   |  267 |   0 |    0 |    0 |     5.61104 |       0.0411619  |  139 |    7.89468  |  101 |   165.695   | 31.5733        | PASS |
| MeshOpenProfile       |  n/a | n/a |  n/a |  n/a |     n/a     |       n/a        |  n/a |    n/a      |  n/a |   n/a       | n/a            | PASS |
```

**Nothing is hidden.** RM-MESH-05's worst radius ratio is 1355 times worse than
RM-MESH-01's and its maximum dihedral reaches 178.7 degrees — a nearly flat
tetrahedron — and every one of those elements is structurally valid. And the
thin plate is **not** the worst mesh in the suite: RM-MESH-02's cylinder has a
*lower* minimum dihedral (0.363 against 0.716), because a chord-polygon boundary
produces slivers where the facets are long and thin along a curved wall. That is
a property of the approved pipeline — the boundary is fixed before the backend
sees it — and it is recorded rather than smoothed over.

```text
ELEMENT ORIENTATION, computed in the test from the node coordinates:
positive 1751    zero 0    negative 0    non-finite 0
minimum element volume > 0 and finite in every model; smallest 0.202694 mm^3
```

## Final determinism table

Five runs each, to P16-VOL-001's own standard — counts, the volume **bitwise**,
the sizing restrictions, and element connectivity index for index — plus node
positions and the quality report's values *and order*.

```text
| Model      | Runs | Node count | Tet count | Volume mm^3        | Quality | Mapping | Fingerprint | PASS |
| RM-MESH-01 |    5 | stable, 8  | stable, 6 | 294000.00000000006 | stable  | stable  | identical   | PASS |
| RM-MESH-03 |    5 | stable, 81 | stable,126| 63560.69856538716  | stable  | stable  | identical   | PASS |
| RM-MESH-06 |    5 | stable, 9  | stable, 12| 118799.99999999999 | stable  | stable  | identical   | PASS |
| RM-MESH-07 |    5 | stable, 61 | stable,267| 239999.99999999997 | stable  | stable  | identical   | PASS |
```

Also compared equal across the five runs: `boundaryVolume()` bitwise, the whole
`VolumeConformity`, `ResolvedSizing::restrictions` and `::regions`, the
canonical node set, `MeshQualityReport::summaries` and `::findings` — their
order as well as their values — and the mapping's completeness.

**Determinism includes failure.** All nine documents' outcomes are stable over
three attempts each, and RM-MESH-08 produces the same refusal with the same
diagnostic text every time, publishing nothing on any attempt.

**And across processes**: the five-repeat ctest stage ran 698 tests back to back
five times in release-ext (694.73 s) and debug-ext (635.73 s), both 100%.

## The six final gates, measured

```text
authority            RM-MESH-03: mesh generated (126 tets, map complete,
                     quality valid), document bytes before == after, object
                     count unchanged, document revision unmoved

density independence RM-MESH-02 at 24 mm: 114 nodes, 361 tets
                     RM-MESH-02 at  6 mm: 376 nodes, 1977 tets
                     saved document: 3347 BYTES EITHER WAY
                     (a node array for the fine mesh alone would be 9024 bytes,
                      nearly three times the whole file)

surface orientation  RM-MESH-04: 288 boundary triangles, enclosed volume
                     81017.293772282937 mm^3, identical over 4 generations --
                     bitwise, and the stored winding identical triangle by
                     triangle

stale sequence       RM-MESH-01: 6 tets at 120 mm, refused while stale, refused
                     after a FAILED rebuild, 12 tets at 150 mm (367500 mm^3,
                     exactly 150 x 70 x 35)

breadth round-trip   all 9 documents round-tripped their canonical intent;
                     8 regenerated an identical mesh; RM-MESH-08 still refused
                     after a load; no "nodes"/"tetrahedra"/"elements" key in
                     any of the nine files

deleted body         RM-MESH-01: 6 tets and Current, body removed, then
                     ineligibility object_not_found, generation REFUSED, the
                     direct path refused identically, and the 6 tetrahedra it
                     still holds report stale
```
