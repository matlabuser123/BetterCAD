# P15-REFMOD-001 — Materials / Engineering Data Reference Models

**STATUS: PASS.** Qualified on the FIRST attempt across three presets, 2805/2805 each,
0 warnings, 36435 test executions, 0 failures, and reference-model output byte-identical
across Debug, Release and Debug-shared. **A reference model found a production defect**
in `mass-properties`, which is fixed, mutation-tested and regression-pinned. Full detail
at the end.

## BASELINE

```text
branch               main
HEAD / origin/main   c676d40224e48cbb34053b9f9c2dec2edab0c75b (equal)
HEAD^{tree}          d42100389daf422beaf789041f241e3d239dd606
working tree         clean
suite                2764 tests
build path           the -ext presets, $penv{BETTERCAD_BUILD_ROOT}/<preset>, outside OneDrive
```

## PREREQUISITES

All twelve PASS, each verified from its own evidence directory:

```text
P15-ARCH-001  P15-UNITS-001  P15-MAT-001   P15-MECH-001
P15-THERM-001 P15-ASSIGN-001 P15-MASS-001  P15-CUSTOM-001
P15-PROV-001  P15-CMD-001    P15-PERSIST-001  P15-CLI-001
```

## SCOPE

Authorized by TODO.md: 21 items. P15-QUAL-001 was not started.

The architecture audit came first and is the substance of the design:
**[AUDIT.md](AUDIT.md)**. Three reference suites already existed — parts (P11/P12),
assemblies (P13), drawings (P14) — and P13 and P14 had each added their own header, catalog
and runner loop. P15 is the fourth, built the same way. **No second reference-model
framework, no second analytical toolkit, no new process harness.**

`tests/reference/Analytic.hpp` already satisfied the hardest requirement on its own:
Green's theorem, Pappus and Gauss–Legendre, in a header whose only includes are
`<array> <cmath> <cstddef> <numbers> <vector>`. It gained closed-form inertia, which it did
not have.

## REFERENCE SUITE

Eight documents under six model IDs. Every property value is
**TEST / SYNTHETIC ENGINEERING DATA**, said so in each material's own `notes`, and a test
asserts that text reaches the CLI's output. The library entries the models import carry
real designations and standards and **no values** — the separation P15-MAT-001 built.

| ID | Document | Geometry | Material | Proves |
| --- | --- | --- | --- | --- |
| RM-MAT-01 | MaterialBlock | 200 x 300 x 500 mm cuboid | synthetic aluminium, fully characterised, 3 cited properties | three DISTINCT principal moments; every property; provenance |
| RM-MAT-02 | MaterialShaft | cylinder r 50, h 400 mm | synthetic steel | axis vs transverse moment; a transformed body |
| RM-MAT-03 | MaterialTube | Ro 60, Ri 40, h 300 mm | synthetic steel | the void reduces volume, mass AND inertia |
| RM-MAT-04 | MaterialPartA | 100^3 mm | synthetic aluminium | equal volume, lower density |
| RM-MAT-04 | MaterialPartB | 200 x 100 x 50 mm | synthetic steel | the SAME volume, 2.888x the mass |
| RM-MAT-04 | MaterialAssembly | 80^3 mm, two occurrences | synthetic steel | one document-level material across an assembly |
| RM-MAT-05 | MaterialCustom | 120 x 80 x 60 mm | library import, its clone, and a twin sharing the designation | custom independence; a three-way duplicate label |
| RM-MAT-06 | MaterialIncomplete | 100^3 mm | partial, no-density, and inconsistent | Ready, Incomplete and Invalid from one document |

### RM-MAT-01 — aluminium rectangular block

* **geometry** 200 x 300 x 500 mm, one corner at the origin. Non-symmetric deliberately: a
  cube gives three equal moments and a transposed inertia axis would pass everything.
* **material** rho 2700, E 70 GPa, nu 0.33, yield 276 MPa, UTS 310 MPa, elongation 0.12,
  hardness 95 HBW, k 167, cp 896, alpha 23.6e-6, melting 855 K.
* **assignment** one material, resolved.
* **analytics** V = 30 000 000 mm^3 and m = 81 kg **exactly**; centroid exactly
  (100, 150, 250) mm; Ixx 2.295, Iyy 1.9575, Izz 0.8775 kg m^2 — see
  [ANALYTICAL_TABLES.md](ANALYTICAL_TABLES.md).
* **properties** read through `requireDensity`, `requireLinearElasticConstants`,
  `requireThermalConductivity`, `requireTransientConductionProperties`,
  `requireThermalExpansion` — the boundaries a solver consumes, not field reads. G and K
  match closed form and report `isDerived()`.
* **provenance** three properties cited from three DIFFERENT source kinds (Handbook,
  ManufacturerData, Measured) so that a record moving between properties changes an answer.
  The yield strength is Known and UNCITED on purpose.
* **persistence / CLI** round trip; the CLI prints every value the core holds.

### RM-MAT-02 — steel cylindrical shaft

* **geometry** a circle on XY extruded in +Z, so the axis IS Z — established by the
  construction and asserted, not assumed.
* **analytics** V = pi r^2 h, I_axis = m r^2 / 2 = 0.0306305 kg m^2,
  I_transverse = m(3r^2 + h^2)/12 = 0.3420409 kg m^2. The axis moment is asserted to be the
  SMALLEST of the three, and the two transverse ones equal.
* **transformed body** turned 90° about X and moved to (100, 200, 300) mm. Independently:
  `I' = R I R^T` moves the axis moment from zz to **yy**; the parallel-axis shift from the
  moved centroid gives Ixz = -0.735 kg m^2. Mass and volume are invariant to 1e-15. A
  transform that dropped the rotation leaves the axis moment on zz; one that dropped the
  shift loses the products. Both are asserted.

### RM-MAT-03 — hollow steel tube

* **analytics** V = pi(Ro^2 - Ri^2)h = 1 884 955.592 mm^3, m = 14.7026536 kg,
  I_axis = m(Ro^2 + Ri^2)/2, I_transverse = m[3(Ro^2 + Ri^2) + h^2]/12.
* **void accounted for** a bounding cylinder of Ro would be 3 392 920 mm^3 — **1.8 times**
  this. The test asserts the measured volume is below 0.6 of it, and that the tube's axis
  moment exceeds a solid rod's of the same mass.
* **This model found a production defect.** See FINDINGS.

### RM-MAT-04 — multi-material document set

* Two parts of **equal volume** (10^6 mm^3 each) at 2700 and 7800 kg/m^3, so a mass that
  followed the geometry rather than the material would give the same answer twice. The mass
  ratio is asserted to be exactly 7800/2700.
* The assembly places **two occurrences of one part definition** at different transforms,
  with one document-level material, and the test asserts the same part, different instance
  IDs, different placements, one material.
* **Aggregate mass, centre of mass and inertia are N/A**, on three verified architectural
  facts: `Document` holds exactly one optional `MaterialId`; `ComponentDefinition` has no
  material field; and no assembly mass aggregation exists in `include/` or `src/`. Summing
  the two masses in a test would validate the test's arithmetic, not the product.

### RM-MAT-05 — custom engineering material

* A **genuine library import** of `bettercad/al-6061-t6 rev 1`, given synthetic values, then
  **cloned**. The clone is what the part is assigned, so every consumer resolves a
  document-local custom material with no special branch.
* `imported.id != custom.id`; both carry the origin key, because cloning does not change
  where the values came from; editing the clone leaves the import's density untouched; the
  library entry is a compiled-in constant with no values to mutate.
* **rho1 -> rho2**: volume and centroid unchanged to 1e-15, mass and inertia scale by
  exactly rho2/rho1. **E, nu -> new**: G and K recompute to closed form and the mass does
  not move.
* A **third** material shares the import's designation. Two objects cannot share a NAME, so
  a duplicate designation is what a duplicate engineering label actually is; three materials
  answer to it and the core returns all three rather than choosing.

### RM-MAT-06 — missing-property failure model

Deliberately incomplete, and a valid document.

```text
consumer              required                    state        missing
mass_properties       density                     Ready        --
fea_linear_static     E, nu                       Incomplete   poisson_ratio
thermal_transient     density, k, cp              Incomplete   thermal_conductivity
```

* **No defaults.** `requireLinearElasticConstants` fails; the CLI prints UNKNOWN; the string
  `0.3` appears nowhere in its output for this material.
* **The mass still succeeds**, because the density is known: 100^3 mm at 2700 is 2.7 kg.
  Incomplete for FEA is not incomplete for a mass.
* **Subcase, no density:** a mass request is refused naming the property. Never zero.
* **Subcase, inconsistent:** an ultimate tensile strength BELOW the yield strength. Every
  property `FeaYieldStrength` requires is present, so this is not Incomplete — it is
  **Invalid**, and the values are not silently corrected. A supplied shear modulus
  inconsistent with E and nu cannot be built: ADR-027 gives it no slot.

## CONFIGURATION CASE

**PASS, not N/A.** ADR-026 makes an assignment configuration-independent, so the brief's
first branch applies: two configurations, switched none -> Tall -> Plain -> Tall -> none, and
the material identity, the assignment and the designation are unchanged at every step.

A sharper case comes free: a configuration that overrides a **parameter** makes the mass
**refuse**, because the carried regeneration defect means the volume in hand would be the
base configuration's. At the base the same document answers again, so the refusal is about
the override and not about the document.

## MODEL CHANGE RECOMPUTATION

RM-MAT-02's length 400 -> 500 mm, regenerated: volume, mass, centroid and both transverse
moments are re-checked against closed form **for the new dimension**, and the volume ratio
is exactly 1.25. Nothing is compared against the previous answer.

The runner changes a main dimension on every one of the eight models and requires a
successful regeneration, so a model that could not follow a parameter would fail the fixture.

## MATERIAL CHANGE RECOMPUTATION

RM-MAT-05, above: a density edit moves the mass and the inertia and nothing else; a
stiffness edit moves G and K and not the mass. Each subsystem responds only to the canonical
inputs it actually reads.

Through the **production commands** as well: `EditMaterialCommand` execute / undo / redo,
and `RemoveMaterialAssignmentCommand` execute / undo. The mass after an undo is
**recomputed**, not restored — no mass is in the history because none is canonical — and the
MaterialId never moves.

## DUPLICATE NAME / NO-REBIND

Duplicate object NAMES are impossible: names are unique across objects and parameters and
re-checked on every rename. The real case is the DESIGNATION, and RM-MAT-05 carries three.

Deleting the assigned material leaves the assignment **Unresolved with its intent**, through
**two** round trips, and the two remaining same-designation materials do not inherit it. The
CLI refuses the ambiguous designation, names all three with their IDs, and prints nothing to
stdout; named by ID each answers with its own density (2700 vs 7800).

Renaming a material changes only the displayed name: the ID, the assignment, the whole
definition, the mass, the inertia and every mechanical and thermal value are byte-identical
afterwards.

## PROVENANCE

Three source kinds on three properties of RM-MAT-01, each re-checked after a load by kind,
reference **and** date. A known value with no citation stays a known value with no citation —
that is a different answer from a missing value, and both are asserted.

## PERSISTENCE

Every model: construct -> regenerate -> save -> load -> revalidate. The identity, the
assignment state and ID, the metadata, all values, the known/unknown states and the
provenance survive.

**Derived state is RECOMPUTED after load**, not compared: the mass is regenerated from the
loaded intent and checked against **closed form**. That is the only way to show the file
carried no stale authority.

The three richest models go save -> load -> save -> load and the two files are **byte
identical**, so the loader adds no normalisation of its own.

A saved model contains neither `shear_modulus` nor `bulk_modulus`, and after loading both
are still `isDerived()` and not `isKnown()`, while E and nu come back supplied.

## CLI / CORE EQUIVALENCE

Structured, not string-matched: each number is **parsed** from the CLI's output and compared
for **exact equality** with the core's own double, then against closed form.

```text
compared exactly     volume, mass, Ixx, density, E, nu, derived G, k, cp
compared by identity MaterialId in the effective-material report
compared by state    the completeness state and exit status, per consumer
```

Exact rather than within a tolerance because the CLI prints shortest-round-trip text, so the
double recovered from stdout is the double the core computed. A tolerance would have hidden a
unit error.

**Multi-process workflow**, five processes sharing nothing but bytes on disk:

```text
A  the runner builds, weighs and saves all eight models          exit 0
B  bettercad-cli material-list        reads one back             exit 0
C  bettercad-cli material-set         EDITS the custom density   exit 0
D  bettercad-cli mass-properties      4.608 kg, closed form      exit 0
E  bettercad-cli material-completeness  incomplete, named        exit 1
E  bettercad-cli material-completeness  ready, same material     exit 0
F  bettercad-cli material-show        reads the edit back        exit 0
F  bettercad-cli material-show        the clone SOURCE untouched exit 0
```

The binary is `$<TARGET_FILE:...>`, the absolute path of the executable this preset built:
no PATH to be ambiguous, no installed copy, no stale binary from another preset. Each step
asserts its own exit code — there is no pipeline to mask one, no `|| true` and no shell.

## ANALYTICAL ERROR TABLES

[ANALYTICAL_TABLES.md](ANALYTICAL_TABLES.md) — expected, actual, absolute error, relative
error and PASS for V, m, the three centroid coordinates and all six inertia components, for
seven bodies.

**Worst relative error anywhere in the suite: 3.28e-14**, against a tolerance of **1e-10** —
four orders of margin, and orders below any real defect: a transposed inertia axis on
RM-MAT-01 is a 17% error and a bounding-cylinder volume on RM-MAT-03 is 80%.

Tolerances are not "engineering percentages". They are the measured rounding level of
analytic OCCT solids, taken from the runner's own 17-significant-digit output.

## DETERMINISM

Each of the eight models is built **twice from clean** and required to give: the same
document ID, the same material IDs, equal definitions, the same assignment, **byte-identical
saved files**, and equal 17-digit geometric fingerprints. Two saves of two independently
built documents is a stronger statement than two saves of one.

All 41 new tests, plus the existing CLI and material ones, were run under
`ctest --repeat until-fail:5` **before the freeze** — 65 tests, five times each, in 9.8 s.
That check exists because P15-CLI-001 lost a three-hour qualification to a test that was not
idempotent.

## ADVERSARIAL REVIEW

**[ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md)** — 30 attacks, 1 production defect, 5
findings in the suite, 6 known limitations.

## FINDINGS

### A production defect, found by RM-MAT-03

`mass-properties` listed **every feature that had a body**, which for a chain includes the
ones a later feature consumed:

```text
validate:          1 result body -- Bore, 1884955.592 mm^3
mass-properties:   Tube (object:4)  3392920.0658769775 mm^3   26.46 kg   <- the BLANK
                   Bore (object:6)  1884955.592153876  mm^3   14.70 kg
```

The first is the un-bored cylinder, reported as a body of the part — the brief's own
automatic-FAIL wording arriving from the other direction: not a wrong expectation, a wrong
answer.

Root cause: the command enumerated `document.objects()` filtered by
`regenerator.body(id) != nullptr`. Fixed generally with `features::resultFeatures()`, the
product's own answer, already used by `validate` and `export-step`. The explicit
`mass-properties <file> <feature>` form still answers for an intermediate, because asking
what the blank weighed is a legitimate question.

Pinned by a **mutation-verified** regression test: reverting the fix fails it.

Twelve qualified milestones and 2764 tests had not found it, because no earlier fixture
combined a feature chain with a mass.

### Four findings in the suite, all closed

A test of mine **took a `SUCCEED` escape hatch** and asserted nothing in the branch it took;
a three-way duplicate designation was built but never queried through the CLI; and
derived-stays-derived and provenance-stays-put were only implicit. All four are now explicit.

### One tolerance model of mine was wrong

An expected product of inertia of 1.9e-17 was compared **relatively** against the kernel's
1.86e-17 and failed at 2.5%. Both are noise — the expected one from `cos(pi/2) = 6.1e-17` in
my own rotation matrix. A component negligible beside the diagonal is now compared
absolutely, scaled by the largest diagonal. Not a loosened gate: the same test asserts the
transformed Ixz exceeds 0.5 kg m^2, so genuinely non-zero products are still relative.

## FINAL MATRIX

```text
Model       Geometry  Mass  CM  Inertia  Mech  Thermal  Missing  Save/Load  CLI   PASS
RM-MAT-01      yes    yes   yes   yes    yes     yes      N/A       yes     yes   yes
RM-MAT-02      yes    yes   yes   yes    yes     yes      N/A       yes     yes   yes
RM-MAT-03      yes    yes   yes   yes    yes     yes      N/A       yes     yes   yes
RM-MAT-04      yes    yes   yes   yes*   yes     yes      N/A       yes     yes   yes
RM-MAT-05      yes    yes   yes   yes    yes     yes      N/A       yes     yes   yes
RM-MAT-06      yes    yes   yes   yes    yes     yes      yes       yes     yes   yes
```

`Missing` is N/A for RM-MAT-01..05 because those materials are complete — an absent
diagnostic is the correct result there, and RM-MAT-06 is the model that carries it.

`yes*` for RM-MAT-04's inertia means **per part**, which is what the architecture has.
Aggregate assembly mass, centre of mass and inertia are **N/A** — no aggregation exists,
`ComponentDefinition` has no material field, and a document holds one material. Verified in
the headers, recorded in AUDIT.md, not invented.

## THREE-PRESET REGRESSION

Qualified on the **first attempt**.

```text
preset             configure  clean  build  no-op rebuild  suite       warnings
debug-ext              0        0      0          0        2805/2805      0
release-ext            0        0      0          0        2805/2805      0
debug-shared-ext       0        0      0          0        2805/2805      0

repeat release-ext, [A-Za-z], until-fail:5   exit 0   14025 = 2805 x 5, 0 failures
repeat debug-ext,   [A-Za-z], until-fail:5   exit 0   14025 = 2805 x 5, 0 failures
qualification finished 19:33:12, 0 stage(s) failed        (15:51:07 -> 19:33:12, 3h 42m)
```

2805 = the 2764 of P15-CLI-001 plus the 41 added here. All three presets discover the same
2805, and both repeat stages show exactly **14025 passing executions** — 2805 x 5, counted
from the logs rather than taken from a summary line.

**36435 test executions in total, 0 failures.**

### Tests selected, executed and passed

```text
stage                        selected   executed   passed
debug-ext ctest                  2805       2805     2805
release-ext ctest                2805       2805     2805
debug-shared-ext ctest           2805       2805     2805
repeat release-ext               2805      14025    14025
repeat debug-ext                 2805      14025    14025
                                          ------   ------
                                           36435    36435
```

The `selected` figures for the repeat stages are the harness's own, written to
`qualification-times.txt` by the guard P15-CLI-001 added; a zero there is a failed stage.
Each preset discovers **69** material and reference tests of this milestone's subject.

### CROSS-PRESET NUMERICAL EQUIVALENCE

The reference-model runner was run from each preset's own build directory and its
17-significant-digit output compared:

```text
debug-ext vs release-ext         IDENTICAL
debug-ext vs debug-shared-ext    IDENTICAL
```

Byte for byte, to the last digit of every volume, mass, centroid and inertia component.
Not "equal within a tolerance" and not "materially the same" — the same doubles.

### The tests carrying this milestone's claims, in all three presets

```text
refmod.material.build                      -- eight models built and saved      passed x3
refmod.material.build.tube-void            -- the void, 1884955.59 mm^3         passed x3
refmod.material.mass-after-edit            -- 4.608 kg across two processes     passed x3
refmod.material.completeness.incomplete    -- the gate reaching the exit code   passed x3
ReferenceModel_RmMat01_BlockMassPropertiesMatchClosedForm                       passed x3
ReferenceModel_RmMat02_TransformedShaftRotatesAndShiftsCorrectly                passed x3
ReferenceModel_RmMat03_TubeAccountsForTheVoid                                   passed x3
ReferenceModel_RmMat05_CliRefusesTheThreeWayDuplicateDesignation                passed x3
ReferenceModel_RmMat05_ADuplicateDesignationNeverRebinds                        passed x3
ReferenceModel_RmMat06_ConsumerCompletenessIsPerConsumerAndNeverDefaulted       passed x3
ReferenceModel_MaterialModels_AssignmentSurvivesConfigurationSwitching          passed x3
ReferenceModel_MaterialModels_MassRefusesUnderAConfigurationThatOverridesA...   passed x3
ReferenceModel_MaterialModels_EveryModelRoundTripsAndRecomputesItsDerivedState  passed x3
ReferenceModel_MaterialModels_AFailedRegenerationDoesNotLeaveTheLastGoodMass    passed x3
ReferenceModel_MaterialModels_AreDeterministic                                  passed x3
MaterialCli_MassProperties_ReportTheResultBodiesAndNotConsumedIntermediates     passed x3
```

That last one is the regression test for the defect RM-MAT-03 found.

`verify-harness.cmd` was run before the qualification and required a non-zero exit from a
preset that does not exist: 2 stages failed, exit 2, and it confirmed that a repeat filter
matching no tests is counted as a failed stage.

**No replace fault anywhere** — zero occurrences of "Permission denied" or "cannot replace"
across every log.

**Qualified tree = committed tree.** The eight tree IDs recorded before the first build and
after the last test run are identical:

```text
apps 7532b4b3748efaa1282874af618a6d41bcb87751
include 6d55546edc191bbc2ca37d1db1b17dcb68cd3fcd
src c0c52b09a0497e075aeeb22ff87b97bb53303c3f
tests a02b8b70e8ba6d951a25b7d50022a2897bac96d8
examples 9187931fa6824e98d3da6296cdc40af6ca4b8607
cmake 7ed703a3031144d26063a1ff02996e7664e29524
CMakeLists.txt 13823e788ed3975792affa9ee4354ffce9479d80
CMakePresets.json 3229f0f89b236744d7d5c082bed47c28b718288e
```

`include/`, `src/`, `cmake/`, `CMakeLists.txt` and `CMakePresets.json` are **byte-identical
to the baseline commit**: this milestone changed nothing below the application layer. The
production fix it did make is in `apps/bettercad_cli/`.

Only `docs/` changed after the freeze, and `docs/` is outside the fingerprint.

## WARNINGS

0 in all three builds, with `-Werror` and the project's full warning set.

## KNOWN LIMITATIONS

1. **No assembly mass aggregation**, and therefore no aggregate centre of mass or inertia,
   no per-occurrence material and no suppression-affects-mass case. Three verified
   architectural absences, recorded rather than worked around.
2. **No invalid/open-shell geometry fixture.** The geometry-validity suite covers it and the
   brief allows referencing that evidence. What this milestone adds instead is sound geometry
   with a material short of data, and a regeneration that fails leaving no stale mass.
3. **The built-in library carries no property values**, so RM-MAT-05 supplies them after the
   import. A P15-MAT-001 limitation.
4. **The material models are not in `AllModelsTests.cpp`'s suite-wide loops**, which iterate
   the parts catalog; the equivalent gates are per model in `MaterialModelsTests.cpp`.
5. **No locale is exercised.**
6. **Two models name their result body `Body`.** Harmless in the product — different
   documents — but it made a first version of the evidence script merge two models silently.
   The script keys by model now. Worth knowing if anything else ever parses that output.

## RESULT

```text
TASK:            P15-REFMOD-001
IMPLEMENTATION:  a fourth reference suite -- 8 documents under 6 model IDs -- built
                 the way P13 and P14 built theirs: one header, one catalog, one
                 runner loop, one test file. Plus closed-form inertia in the
                 existing analytical toolkit, and ONE PRODUCTION FIX in
                 apps/bettercad_cli/MaterialReports.cpp.
TESTS:           41 added; 2764 -> 2805
VALIDATION:      every expected value from closed form in a header with NO
                 BetterCAD include. Worst relative error anywhere: 3.28e-14
                 against a 1e-10 tolerance. Cross-preset output byte-identical.
RESULT:          PASS
EVIDENCE:        this directory; AUDIT.md for the architecture and the verified
                 limits; ANALYTICAL_TABLES.md for the per-quantity error tables;
                 ADVERSARIAL_REVIEW.md for 30 attacks and the defect;
                 qualification/ for all 20 stage logs and the tree fingerprints
TODO:            updated -- 21 boxes ticked
```

**A reference model found a defect in a command shipped one milestone earlier.** That is
the whole argument for reference models, and it is the result worth reporting: RM-MAT-03
is the first fixture in the repository to combine a feature chain with a mass, and
`mass-properties` had been listing consumed intermediate bodies as results. On the tube it
reported the un-bored blank first — 3392920 mm^3, 26.46 kg — while `validate`, which used
`resultFeatures()`, correctly said there was one body at 1884955 mm^3. 2764 tests had not
found it.

**The infrastructure was already there, again.** Three reference suites existed and the last
two had each added their own header, catalog and runner loop, so the fourth followed a path
worn twice. `Analytic.hpp` already did Green's theorem, Pappus and Gauss-Legendre with no
BetterCAD include — it needed closed-form inertia and nothing else.

**Three of the brief's fixtures could not be built as written, and are recorded rather than
approximated.** A multi-material assembly needs a material field `ComponentDefinition` does
not have; aggregate assembly mass needs an aggregation that does not exist; and an
inconsistent supplied shear modulus needs a slot ADR-027 deliberately withholds. Each was
checked in the headers, and the third was replaced by the inconsistency the product does
detect — an ultimate tensile strength below the yield strength — which gives a genuine
`Invalid`.

**Three findings were about my own tests.** One took a `SUCCEED` escape hatch and asserted
nothing in the branch it took, which is the same defect P15-MASS-001 found in one of its
own. One tolerance model compared numerical noise relatively. One evidence script silently
merged two models that name their result body alike. None of the three would have failed
the gate; all three would have weakened it.

## REVISION

First revision. Qualified on the first attempt; no source or test file changed after the
freeze.
