# P15-REFMOD-001 — adversarial review

An attempt to disprove the suite and the product, not to describe them. Read against the
final diff: `examples/reference_models/` (+3 files, 2 modified), `tests/reference/`
(+1 file, `Analytic.hpp` extended), `tests/CMakeLists.txt`, and — because a reference model
found a defect — `apps/bettercad_cli/MaterialReports.cpp`.

A reference suite can fail in two directions: the product can be wrong, or the suite can
be unable to tell. Most of these attacks are aimed at the second.

## Attacks

| # | Attack | Outcome |
| --- | --- | --- |
| 1 | An expected mass came from production code | **Held.** Every expected value comes from `analytic::cuboid`, `cylinder`, `hollowCylinder`, `rotated`, `shifted`, `placed`. `Analytic.hpp` includes `<array> <cmath> <cstddef> <numbers> <vector>` and **no BetterCAD header at all** — it cannot call the kernel, `massProperties`, or the CLI even by accident. |
| 2 | A cube hides swapped inertia axes | **Held.** RM-MAT-01 is 200 x 300 x 500, so Ixx = 2.295, Iyy = 1.9575, Izz = 0.8775 kg m^2 — three distinct numbers, and the test asserts Ixx > Iyy > Izz as well as each value. A transposition is a 17% error. |
| 3 | Symmetric geometry hides rotation bugs | **Held.** RM-MAT-02's transformed case turns the shaft 90° about X, which moves the axis moment from zz to yy, and produces Ixz = -0.735 kg m^2 about the origin. A transform that dropped `R I R^T` leaves the axis moment on zz; one that dropped the parallel-axis shift loses the products entirely. Both are asserted. |
| 4 | The hollow tube uses a bounding-cylinder volume | **FOUND A REAL DEFECT — see F1.** The suite's own check held (volume must be under 0.6 of the bounding cylinder), and it caught the CLI reporting the un-bored blank. |
| 5 | Duplicate material names rebind after save/load | **Held, and the fixture is stronger than asked.** Two objects cannot share a NAME, so the real case is the DESIGNATION: RM-MAT-05 holds **three** materials designated "Aluminium 6061-T6" with two different densities. Deleting the assigned one leaves the assignment Unresolved, keeping its intent, through **two** round trips, and the other two do not inherit it. |
| 6 | The CLI returns the first same-designation material | **Held — but untested until F2.** Added: the CLI refuses, names all three with their IDs, prints nothing to stdout, and each still answers correctly when named by ID (7800 vs 2700 kg/m^3). |
| 7 | A custom-material edit mutates the library source | **Held.** The library entry is a compiled-in constant with no values to mutate; the import copies metadata and records the key as origin. Editing the clone leaves the import's density exactly where it was. |
| 8 | A density edit leaves a stale mass | **Held.** Volume and centroid unchanged to 1e-15; mass and inertia scale by exactly rho2/rho1. |
| 9 | An E edit leaves a stale derived G or K | **Held twice.** They recompute to closed form — and there is no slot they could have been stored in, so staleness is impossible rather than merely absent. |
| 10 | Model regeneration leaves a stale mass or inertia | **Held.** RM-MAT-02's length 400 -> 500 mm: volume, mass, centroid and both transverse moments are re-checked against closed form for the NEW dimension, and the volume ratio is exactly 1.25. |
| 11 | A failed regeneration returns the last successful mass | **Held, and the test was strengthened — see F3.** Boring a 200 mm hole through a 120 mm cylinder leaves nothing; the mass is refused with `'Bore' (hole, object:6) has no body`. |
| 12 | An incomplete FEA material receives a default nu | **Held.** `FeaLinearStatic` reports Incomplete naming the Poisson ratio; `requireLinearElasticConstants` fails; the CLI prints UNKNOWN and the string `0.3` appears nowhere in its output. |
| 13 | An incomplete thermal material receives a default k | **Held.** `ThermalTransient` reports Incomplete naming the conductivity, while the density and specific heat it also needs are listed present. |
| 14 | A known value with unknown provenance is treated as missing | **Held.** RM-MAT-01's yield strength is Known and UNCITED on purpose; the report gives an empty record and the value stays Known. Two different answers, not one. |
| 15 | Provenance moves between properties after persistence | **Held, but untested until F4.** RM-MAT-01 cites three properties from three DIFFERENT source kinds, so any swap changes an answer. Added: kind, reference and date are each re-checked per property after a load. |
| 16 | Assembly mass counts a repeated part once | **N/A — there is no assembly mass.** No aggregation exists anywhere in `include/` or `src/`. Recorded, not invented. |
| 17 | Assembly inertia omits the rotation or the shift | **N/A** for the same reason. The MECHANISM is covered by attack 3 on a single body. |
| 18 | Suppression leaves a stale occurrence mass | **N/A** for the same reason. |
| 19 | A derived mass is persisted and used after reload | **Held.** Every model is saved, loaded and its mass **recomputed** and compared to closed form; P15-PERSIST-001 separately checks the file against 24 forbidden substrings. |
| 20 | A derived G or K comes back SUPPLIED after a load | **Held, but untested until F4.** Added: the file contains neither `shear_modulus` nor `bulk_modulus`; after loading, both are still `isDerived()` and not `isKnown()`, while E and nu come back supplied — so "derived" is a real distinction and not a label everything wears. |
| 21 | Structured CLI values differ from core by a unit conversion | **Held.** Each number is PARSED from the CLI's output and compared for exact equality with the core's double, then against closed form. A tolerance would have hidden a unit error; exact equality cannot. |
| 22 | The runner masks a failed CLI command | **Held structurally.** Each process test asserts its own exit code; there is no pipeline to mask one, no `|| true`, and no shell. The runner itself returns non-zero on the first model that fails anything. |
| 23 | A zero-test CTest filter passes | **Held twice.** The test presets set `noTestsAction: error`, and P15-CLI-001 added a guard that counts the selected tests and records the number. |
| 24 | Stale binaries produce apparently valid evidence | **Held.** `$<TARGET_FILE:...>` names the binary this preset built; the harness's no-op rebuild stage proves nothing was left to build; every preset is cleaned first. |
| 25 | Debug, Release and Debug-shared disagree | **Held.** Three presets, full suite each, plus determinism tests that compare 17-digit fingerprints and saved bytes. |
| 26 | The suite tests six models that all only check density and mass | **Held.** RM-MAT-01 carries inertia and provenance; 02 carries a transform; 03 carries a void; 04 carries equal volumes at different densities; 05 carries library independence and a duplicate designation; 06 carries three different completeness answers. |
| 27 | A synthetic value is mistaken for sourced datasheet data | **Held.** Every material's `notes` says `TEST / SYNTHETIC ENGINEERING DATA ... Not sourced datasheet figures`, and a test asserts that text reaches the CLI's output. The library entries carry real designations and **no values**. |
| 28 | A model passes because its own builder computed the expectation | **Held.** The builders take dimensions in millimetres and densities in SI; the tests restate those numbers as metres in the closed-form call. The two agree only if the product is right. |
| 29 | A reference test is not idempotent and the determinism gate fails at three hours | **Held, and checked before the freeze.** 65 tests run under `--repeat until-fail:5` in 9.8 s. This is the check P15-CLI-001 learned to run early. |
| 30 | An "expected" product of inertia is numerical noise compared relatively | **Found in my own test — see F5.** |

## Findings

### F1 — A REAL PRODUCTION DEFECT, found by RM-MAT-03

`mass-properties` with no feature argument listed **every feature that had a body**, which
for a chain includes the ones a later feature consumed. On the tube:

```text
validate says:   1 result body -- Bore, volume 1884955.592 mm^3
mass-properties said:
  Tube (object:4)   volume 3392920.0658769775 mm^3   mass 26.464776513840427 kg
  Bore (object:6)   volume 1884955.592153876  mm^3   mass 14.702653618800234 kg
```

The first of those two is the **un-bored blank** — the bounding cylinder — reported as a
body of the part. That is the brief's own automatic-FAIL wording, "hollow tube expected
volume uses outer cylinder only", arriving from the other direction: not a wrong
expectation, a wrong answer.

**Root cause.** The command enumerated `document.objects()` filtered by
`regenerator.body(id) != nullptr`. A bored part is a chain: the extrude produces a solid and
the hole consumes it, so both have a body while only the last is a body of the part.

**General fix.** `features::resultFeatures(document)`, which is the product's own answer and
what `validate` and `export-step` already used. The CLI asks rather than deciding again. The
explicit form, `mass-properties <file> <feature>`, still answers for any feature with a body,
because asking what the blank weighed before the hole is a legitimate question.

**Regression test**, mutation-verified: a bored box, asserting one reported body, its name,
the closed-form bored volume, and that the intermediate still answers when named. Reverting
the fix fails it; restoring it passes.

This is the reference suite doing its job. Twelve qualified milestones and 2764 tests did
not find it, because no earlier fixture combined a feature chain with a mass.

### F2, F3, F4 — three gaps in the suite, all closed

* **F2** the three-way duplicate designation was built but never queried through the CLI.
* **F3** the failed-regeneration test **took a `SUCCEED` escape hatch**: it branched on
  whether the mass was refused and asserted nothing in the branch it took. That is the same
  defect P15-MASS-001 found in one of its own tests, and it is worth naming twice — a test
  that accepts either outcome passes whichever the product does, including the one it must
  not do. Replaced with a direct `REQUIRE_FALSE` plus assertions on the diagnostic.
* **F4** derived-stays-derived and provenance-stays-put were asserted only implicitly, by a
  whole-definition equality after a round trip. Both are now explicit, per property, by
  kind, reference and date.

### F5 — my own tolerance model was wrong, and the fix is not a loosened tolerance

The transformed case compared an expected `Iyz` of 1.9e-17 kg m^2 against the kernel's
1.86e-17 **relatively**, and failed at 2.5%.

Both numbers are noise. The expected one is noise from my own rotation matrix:
`rotationAboutX(90)` uses `cos(pi/2) = 6.1e-17`, so the triple product leaves 1e-17 where
closed form has an exact zero. Comparing that against the kernel's own 1e-17 compares
nothing.

The rule is now: a component **negligible beside the diagonal moments** is compared
absolutely against zero, scaled by the largest diagonal; the rest relatively. That is the
correct model for a product of inertia, which is only meaningful beside the diagonal — and
it is not a weakened gate, because the same test asserts that the transformed `Ixz` exceeds
0.5 kg m^2, so the genuinely non-zero products are still compared relatively.

## Known limitations

1. **No assembly mass aggregation** (attacks 16–18). `ComponentDefinition` has no material
   field, a `Document` holds one optional `MaterialId`, and no aggregation exists. RM-MAT-04
   is therefore a multi-material document SET plus an assembly proving the assignment is
   document-level. Aggregate mass, centre of mass and inertia are **N/A**, not PASS.
2. **No per-occurrence material**, for the same architectural reason.
3. **No invalid/open-shell geometry fixture.** The brief allows referencing existing
   evidence, and the geometry-validity suite already covers it. What this milestone adds is
   the adjacent case: sound geometry with a material short of data, which is F3's fixture.
4. **The built-in library carries no property values**, so RM-MAT-05 supplies them after
   the import. That is a P15-MAT-001 limitation, not one this milestone can fix.
5. **The material models are not in `AllModelsTests.cpp`'s suite-wide loops**, which iterate
   the parts catalog. The equivalent gates — build twice, regenerate after a change, save
   and reload — are in `MaterialModelsTests.cpp` instead, per model.
6. **No locale is exercised.**

## Conclusion

One production defect found, root-caused, fixed generally and pinned by a mutation-verified
regression test. Five findings in the suite itself, all resolved — including one test of
mine that asserted nothing in the branch it took.
