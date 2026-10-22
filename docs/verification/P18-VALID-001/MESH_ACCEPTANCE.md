# P18-VALID-001 — the thermal mesh-acceptance policy, and the measurement it is derived from

```text
SUBJECT:  which P16 metrics refuse a mesh for heat conduction, which only warn, which carry no bound, and why each number is what it is
OWNER:    P18-VALID-001 decides; P16 measures and states no opinion (reportOnlyThresholds() classifies nothing)
RULE:     TV_MESH_STRUCTURAL (invalid elements) and TV_MESH_QUALITY (the two accuracy bounds and the four envelope warnings)
FAILURE:  3r/R < 7.0e-6   OR   aspect ratio > 700          (derived, a factor of three inside a 1e-9 relative gradient error)
WARNING:  3r/R < 3.0e-4, aspect > 81, min dihedral < 6.0e-3 rad, max dihedral > 3.125 rad   (the qualified envelope, rounded away)
BOUNDARY: strict, in P16's direction — a value ON a bound is on the good side; a value that is not a number is never on it
```

## The policy table

| Metric | P16 definition | Warning | Failure | Boundary policy | Evidence basis | Reference cases |
| --- | --- | --- | --- | --- | --- | --- |
| `TetRadiusRatio` (3r/R) | `meshing::QualityMetric::TetRadiusRatio`, `src/meshing/MeshQuality.cpp`: `3 r / R`, (0, 1], ideal 1, worse toward 0 | below **3.0e-4** | below **7.0e-6** | `value < bound` is bad (strict); ON the bound is good | failure: the flattening law below; warning: the qualified envelope (worst 3.22641e-04) | 7 qualified P16 models; wedges; 0.3 / 0.07 / 0.05 mm plates |
| `TetAspectRatio` | `l_max / l_min`, [1, inf), ideal 1, worse larger | above **81.0** | above **700** | `value > bound` is bad (strict) | failure: the stretching law below; warning: the qualified envelope (worst 80.0062) | the same, plus 0.02 mm plate |
| `TetMinDihedralAngle` | smallest internal dihedral, radians | below **6.0e-3 rad** | **none** | strict | warning: the qualified envelope (worst 6.330018e-03); NO failure bound: no law was measured against it | RM-MESH-02 cylinder |
| `TetMaxDihedralAngle` | largest internal dihedral, radians | above **3.125 rad** | **none** | strict | warning: the qualified envelope (worst 3.119061); NO failure bound | RM-MESH-05 thin plate |
| `TetVolume`, `TetJacobianDeterminant`, edge lengths, radii | dimensioned sizes | — | — | — | `ContextOnly`: "is this volume good?" has no scale-free answer, and `validate(QualityThresholds)` refuses a bound on one | — |

Everything else is not a quality score. **Structural invalidity is not quality**: an inverted, degenerate, repeated-handle or non-finite element is reported under
`TV_MESH_STRUCTURAL` as `mesh_structurally_invalid` / `mesh_has_invalid_elements` and is fatal; no threshold produces it, no `abs(detJ)` hides it, no node is reordered.

## Why a threshold needed a measurement at all

P16-QUALITY-001 ships the metrics and states no opinion. P17-VALID-001 measured that no shape metric separates BetterCAD's qualified meshes from deliberately
pathological ones (a qualified cylinder is worse than a 2-degree sliver), so a bound cannot be chosen by comparing fixtures; it can be chosen by asking what the
solver's output actually loses as an element degrades. That is a property of the kernel, and this milestone measured it for the THERMAL kernel instead of inheriting the
structural number: the quantity is the gradient `grad(T) = G Te` that a heat flux is built from (`recoverThermalElementFields`, which calls P18-ELEM-001's
`gradientFrom`), and the field is a temperature.

## The measurement (`tests/thermal/ThermalValidationLawTests.cpp`)

A linear temperature `T = a + b x + c y + d z` is imposed on ONE tetrahedron and the gradient is recovered through the production path. A linear field has a constant
gradient, so the expected value is the field's own coefficients, typed in. The error is RELATIVE, `max|g_rec - g| / |g|`. Two degeneration families, because a tetrahedron
degenerates in two independent ways and neither shape metric sees the other's family:

```text
FLATTENING, toward a plane   P16's wedge: a 10 mm equilateral base, the apex brought down       3r/R as the square of the apex height; aspect SATURATES at sqrt(3)
STRETCHING, toward a line    a needle: the base shrunk, the apex 10 mm away                      aspect as the inverse of the base; 3r/R understates the damage
```

### The finding that is thermal's and not structural's: temperatures are ABSOLUTE

Two sets of fields were measured. The ISOLATING fields have no offset (`T = g . x`), so the only rounding is relative to the field itself; the ABSOLUTE fields carry the
same gradients on a 307 K / 412 K level, which is what every thermal solve actually has (kelvin; there is no Celsius in the tree).

```text
                                      flattening C  (err <= C / sqrt(3r/R))      stretching C  (err <= C * aspect)
isolating field A (no offset)         1.84e-16                                    1.10e-16
absolute field A  (307 K, 14 K/m)     8.61e-13                                    4.28e-13
absolute field B  (412 K, 14 kK/m)    2.05e-15  (x1000 stronger gradient)         6.7e-16
```

The isolating measurement says the kernel itself is accurate to rounding and the needle does not degrade it at all — and it is the wrong measurement for a thermal
model. With a 0.14 K change across a 10 mm element on a 307 K level, the nodal temperatures carry the rounding of a 307 K number, the gradient is a difference of
nearly equal numbers, and the SHAPE of the element multiplies that rounding: the regular element already recovers its gradient to 4.2e-13 relative, and a flattened or
stretched one amplifies it by the inverse of its radius ratio or by its aspect ratio. A mesh policy must bound that amplification; it cannot remove the baseline (a
weaker gradient has a worse baseline on EVERY mesh, a perfect one included; a stronger one a better), and the evidence says so rather than pretending it is a mesh property.
The reference condition for the bound is the weak one (field A absolute), the conservative reading.

### The flattening family (`LAW flattening` rows, absolute field A)

```text
apex (mm)   3r/R          aspect    err (A, 307 K)   err (B, 412 K)   err*sqrt(3r/R) (A)
1.0e+01     9.77082e-01   1.1547    4.18e-13         5.2e-16          4.13e-13
1.0e+00     8.49037e-02   1.7066    2.51e-12         0                7.31e-13
1.0e-01     8.99460e-04   1.7318    2.67e-11         0                8.02e-13
1.0e-02     8.99995e-06   1.7321    1.07e-10         5.2e-16          3.21e-13
1.0e-04     9.00000e-10   1.7321    2.74e-08         6.8e-11          8.22e-13
1.0e-05     9.00000e-12   1.7321    1.10e-07         0                3.29e-13
1.0e-06     9.00000e-14   1.7321    1.75e-06         4.4e-09          5.26e-13
1.0e-08     9.00000e-18   1.7321    1.68e-04         5.6e-07          5.05e-13
1.0e-10     9.00000e-22   1.7321    2.87e-02         3.6e-05          8.61e-13
```

`err * sqrt(3r/R)` stays between 2.5e-13 and 8.6e-13 over TWENTY-ONE orders of radius ratio while the error itself moves through eleven. The aspect ratio reads 1.7321
the whole way down and sees nothing. Every row is structurally valid.

### The stretching family (`LAW stretching` rows, absolute field A)

```text
base (mm)   3r/R          aspect       err (A)       err / aspect
1.0e+01     9.77082e-01   1.1547e+00   4.18e-13      3.62e-13
1.0e+00     1.67718e-01   1.0017e+01   4.18e-12      4.17e-13
1.0e-01     1.72700e-02   1.0000e+02   1.34e-11      1.34e-13
1.0e-02     1.73155e-03   1.0000e+03   5.35e-11      5.35e-14
1.0e-03     1.73200e-04   1.0000e+04   4.28e-09      4.28e-13
1.0e-04     1.73205e-05   1.0000e+05   1.37e-08      1.37e-13
1.0e-05     1.73205e-06   1.0000e+06   5.48e-08      5.48e-14
1.0e-06     1.73205e-07   1.0000e+07   8.76e-07      8.76e-14
```

`err / aspect` is bounded by 4.28e-13 over SEVEN orders of aspect ratio, while the constant Law 1 would need climbs by orders of magnitude over the same rows
(`err * sqrt(3r/R)` reaches 3.6e-10): Law 1 does not describe this family. The needle at aspect 1.0e+04 (3r/R 1.7e-4) errs 4.28e-09 — forty times the 1.07e-10 of a wedge at a radius ratio twenty times WORSE
(9.0e-06). A policy with one bound is therefore wrong, as P17 found for strain and as this milestone re-measured for temperature.

### The derivations

```text
REQUIREMENT       relative gradient error <= 1e-9, BetterCAD's documented band for geometric accumulation (CLAUDE.md), with a factor of three kept

FLATTENING        err <= C / sqrt(3r/R)       C <= 8.62e-13
                  crossing      3r/R >= (C / 1e-9)^2 = 7.4e-07
                  with 3x       3r/R >= (3 C / 1e-9)^2 = 6.7e-06   ->  7.0e-06 (rounded UP, so the margin is kept): the error at the bound is C/sqrt(7e-6) = 3.26e-10

STRETCHING        err <= C * aspect           C <= 4.28e-13
                  crossing      aspect <= 1e-9 / C = 2.3e+03
                  with 3x       aspect <= 1e-9 / (3 C) = 7.8e+02   ->  700   (rounded DOWN): the error at the bound is C * 700 = 3.0e-10
```

The constants are pinned: `ThermalValidation_TheBoundsGuaranteeTheStatedGradientAccuracy` fails if the floor is below `(3C/1e-9)^2`, if the ceiling is above `1e-9/(3C)`, or if
either is more than a factor of two more conservative than that; and `ThermalValidation_MeasuresHowGradientAccuracyDegradesWithElementShape` pins the fit bands
(8.6e-13 → 1.5e-12, 4.3e-13 → 1.0e-12, 1.8e-16 → 5e-16) so a kernel whose gradients drifted leaves a band while every absolute error still looked small.

### Both sides of both bounds, measured (`TheBoundsGuaranteeTheStatedGradientAccuracy`)

```text
RADIUS FLOOR 7.0e-06
  ACCEPTED, correctly   3r/R 8.99995e-06 (apex 1e-2 mm)  err 1.07e-10        a 0.198-degree sliver: valid, past the envelope, ACCEPTED with a warning
  REFUSED, correctly    3r/R 9.0e-10  (apex 1e-4 mm)      err 2.74e-08        genuinely outside 1e-9
  REFUSED, correctly    3r/R 9.0e-12 .. 9.0e-22          err 1.1e-07 .. 2.9e-02
ASPECT CEILING 700
  ACCEPTED, correctly   aspect 100 (base 0.1 mm)          err 1.34e-11
  REFUSED               aspect 1.0e+03 (base 1e-2 mm)     err 5.35e-11        inside 1e-9: CONSERVATIVE, the direction a margin should err
  REFUSED, correctly    aspect 1.0e+04                    err 4.28e-09        genuinely outside 1e-9
```

## And against everything BetterCAD produces (`tests/reference/ThermalValidationSweepTests.cpp`)

```text
model                        tets   worst 3r/R    margin over 7e-6   worst aspect   margin under 700   min dihedral   max dihedral
RM-MESH-01 block                6    4.3725e-01    6.2e+04x           4.0933         171x               2.5519e-01     2.0183
RM-MESH-02 cylinder           508    1.2484e-03    178x               16.0391        43.6x              6.3300e-03     3.0912
RM-MESH-03 plate with hole    126    1.2906e-02    1.8e+03x           20.4856        34.2x              1.9421e-02     2.8882
RM-MESH-04 hollow tube        808    6.3459e-03    906x               14.8994        47.0x              4.1403e-02     2.9679
RM-MESH-05 thin plate          12    3.2264e-04    46.1x              80.0062        8.75x              1.2499e-02     3.1191
RM-MESH-06 transformed placed  12    1.2738e-01    1.8e+04x           3.8810         180x               2.6060e-01     2.6586
RM-MESH-07 local refinement   267    4.1162e-02    5.9e+03x           5.6110         125x               1.3779e-01     2.8919
```

No qualified mesh is refused and none warns: `AcceptsEveryQualifiedReferenceMeshWithoutAFinding`. The envelope bounds are the measured worst rounded AWAY from the
qualified set (worst 3r/R 3.226414e-04 vs bound 3.0e-04; aspect 80.00625 vs 81; min dihedral 6.330018e-03 vs 6.0e-03; max dihedral 3.119061 vs 3.125) and each is TIGHT
against it, within a factor of two (`TheEnvelopeBoundsAreTheMeasuredWorstCase`), which is what keeps an envelope an envelope. The tightest margin to a failure bound is
RM-MESH-05's aspect ratio, 8.75 times inside the ceiling.

## The same policy on REAL meshes from the real mesher

The block thinned to a plate makes P16 produce slender tetrahedra, which is where the policy has something to say (`tests/thermal/ThermalValidationMappedTests.cpp`):

| Plate | tets | worst 3r/R | worst aspect | max dihedral | Verdict | Test |
| --- | --- | --- | --- | --- | --- | --- |
| 20 mm (the block) | 12 | qualified | qualified | — | PASS, no mesh finding | `QualifiedReferenceMeshesAreAcceptedWithoutAMeshFinding` |
| 0.3 mm | 12 | 1.0766e-04 | 133.34 | 3.1291 | **PASS_WITH_WARNINGS** (radius ratio, aspect, max dihedral) | `AWarningOnlyAnalysisIsAcceptedWithWarnings` |
| 0.07 mm | 12 | **5.8757e-06** | 571.43 | 3.1387 | **FAIL**: 3r/R below the floor; aspect inside the ceiling | `ThermalMeshQualityRejectOnARealMesh` |
| 0.05 mm | 11 | 3.9886e-06 | 1000.0 | 3.1377 | FAIL (both) | (measured) |
| 0.02 mm | 6 | 1.1991e-03 | **2500.0** | 1.5715 | **FAIL**: aspect above the ceiling; 3r/R is above even the warning bound | `TheAspectRatioBoundCatchesWhatTheRadiusRatioCannotSee` |

The 0.07 mm plate is the demonstration of the central rule: the WHOLE qualified chain runs to the end on it — the solve succeeds, the free residual passes, the books
balance — and the analysis is not accepted, because "the solver said success" is not what accepts a result. The 0.02 mm plate is the demonstration of why there are two
bounds: a radius-ratio-only policy accepts it.

## The solve itself is not what degrades (`MeasuresHowTheSolveDegradesWithAFlatteningElementInAPatch`)

An affine patch is reproduced exactly by a linear element, so any difference is what a degraded element cost. Four tetrahedra around one free interior node, the four
outer nodes prescribed to an affine field on a 307 K level, the interior node moved toward a face so that one sub-tetrahedron flattens to a worst 3r/R of 4.2e-21:

```text
s = 0 .. 1 - 1e-10      worst 3r/R 8.8e-02 .. 4.2e-21      solved yes      T(P) error 0 .. 2.6e-13 of the temperature change      pivot ratio 1      residual 0 .. 1.0e-16
```

The assembled conduction matrix of a flattened element is exact and the solve stays accurate; what degrades is the RECOVERY of a gradient (and so of a flux) on it, which is
what the bounds guard. The solver's own pivot gate and residual gate remain the guard of its conditioning, and are not relaxed or replaced by a mesh threshold.

## What these thresholds are NOT

* **Not a mesh-quality standard.** They are accuracy floors for a recovered gradient. An element inside them may still be a poor element, and the envelope warnings say so.
* **Not bounds on the dihedral angles.** No law was measured against either, and the qualified distributions show they cannot separate a qualified mesh from a sliver (the
  qualified cylinder's minimum dihedral, 6.33e-03 rad, is worse than a deliberate 1.98-degree sliver's). A third bound with nothing behind it is the invented number this
  document exists to avoid.
* **Not a claim about discretisation error.** This measures the error in recovering a field the mesh represents exactly, not how well a mesh approximates a field it cannot.
* **Not a conditioning check.** A mesh of accurate elements can still assemble an ill-conditioned `K`; the pivot ratio and the independent residual are the solver's, and are
  separate gates that this one neither replaces nor relaxes.
* **Not proven complete.** Two families are measured, and two is what the geometry of a tetrahedron offers (toward a plane and toward a line); a hybrid is bounded by
  whichever bound it crosses first, which is an argument and not a measurement. The reference gradient is 14 K/m on a 307 K level; a weaker one has a larger baseline on every mesh.

## P16 metric computation changed: NO

No file under `src/meshing` or `include/bettercad/meshing` is touched by this milestone (FREEZE.md compares them by blob), and no metric is recomputed here: the validator reads
`MeshQualityReport::summaries` and uses P16's own `direction(metric)` and `unitOf(metric)` (`ReusesP16MetricsAndAgreesWithP16sOwnClassificationUnderThePolicy` shows the
validator's verdict agrees with P16's own per-element classification under the same thresholds).
