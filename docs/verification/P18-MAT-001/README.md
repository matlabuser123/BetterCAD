# P18-MAT-001 — Thermal Material Resolution

```text
STATUS:    PASS
MILESTONE: P18-MAT-001, the third milestone of P18 — Thermal Analysis
SCOPE:     resolving and validating the material a thermal analysis consumes,
           per analysis mode, through P15. No element, no matrix, no solve.
```

## Baseline

```text
HEAD at start      843469aa6ebb3b65a440877eb4043c14037664bb  (P18-DATA-001's
                   documentation record, on top of 314166e)
HEAD^{tree}        e416ec9ceebebb5e4b8579f0961634427315c65f
origin/main        a3b721b150338cc1cbd24ee592a11a70a6196c0c  -- FOUR COMMITS
                   BEHIND: neither P18-ARCH-001's nor P18-DATA-001's pair had
                   reached the remote when this milestone started; verified
                   with this milestone's push
working tree       clean but for the untracked .claude/ settings directory;
                   git diff, git diff --cached and git diff --check empty
P18-DATA-001       PASS, 19 of 19, 4084/4084 x 3, and its eight-path
                   fingerprint equals commit 314166e's subtrees
                   (include 5b4d1dfb, src 4fbeceef, tests de83d2e5)
P18-ARCH-001       PASS, 19 of 19
P15 / P16 / P17    QUALIFIED
authorized         2026-10-17, "start", with the owner's P18-MAT-001 brief
```

## The finding that shaped the milestone

**P15 already owns everything the checklist names, for this consumer by
name, and no schema change was needed.** `k` and `cp` are
`MaterialProperty` fields of `ThermalProperties`; `rho` is the material's one
density in `MechanicalProperties`; a non-finite or non-positive value is
refused at entry and cannot be in a document; `requireThermalConductivity`
and `requireTransientConductionProperties` refuse a missing input and name
every gap; and `requiredProperties(ThermalSteady) = { k }`,
`requiredProperties(ThermalTransient) = { rho, k, cp }` is "THE one
definition". P17-MAT-001 had measured the same for the structural consumer.

So this milestone adds the one thing P15 cannot know -- WHICH consumer a
given analysis is -- and nothing else:

```text
consumerFor(ThermalAnalysisMode)        Steady -> ThermalSteady, Transient -> ThermalTransient
resolveThermalMaterial(doc, body, mode) a derived, source-bound ThermalMaterial: id,
                                        revision, mode, k always, a rho/cp
                                        ThermalCapacity exactly when transient
thermalMaterialProblem / Completeness   the category and P15's property list
thermalCapability(doc, id)              { steady, transient }, derived, never stored
currentResultSource (P18-DATA-001)      now resolves the material for the analysis's
                                        mode
```

Full audit table: [P15_INTEGRATION.md](P15_INTEGRATION.md).

## Material authority

```text
P15 canonical material                YES, the only one
P18 canonical material database       NONE
thermal property authority            P15
default k / rho / cp                  NONE
cache                                 NONE
thermalDensity                        NONE -- rho is P15's one density
```

[MATERIAL_AUTHORITY.md](MATERIAL_AUTHORITY.md).

## What each mode requires

```text
STEADY      k                 the conductivity matrix depends on k alone
TRANSIENT   k, rho, cp        the capacity matrix depends on rho cp, and
                              [kg/m^3][J/(kg K)] = [J/(m^3 K)] needs both
```

Read from P15's table, never restated. Measured on six material variants,
both modes: k only resolves for steady and refuses for transient naming both
gaps; k + rho refuses transient naming cp alone; k + cp refuses naming rho
alone; k + rho + cp resolves for both; rho + cp without k refuses both. A
transient analysis on a k-only material is `InputsUnavailable`; the steady
one is `Ready`. [PROPERTY_REQUIREMENTS.md](PROPERTY_REQUIREMENTS.md).

## Validation

P15 makes `k = 0`, `k < 0`, NaN and ±inf unrepresentable at entry -- for k,
rho and cp alike -- and refuses the edit too, leaving the old value. The
resolver has no range check of its own and asks P15, whose diagnostic it
carries byte for byte; absent is not zero. [VALIDATION.md](VALIDATION.md).

## Provenance and currentness

```text
same MaterialId, k 42.75 -> 50          revision 1 -> 2, view not equal, result stale
same values, different MaterialId       not equal, result stale on reassignment
k / rho / cp / E edit                   mesh Current, stamp and NodeIds unchanged,
                                        geometry and meshing intent unchanged,
                                        result stale for reason material alone
no-op edit / refused edit               nothing moves, result current
removal / deletion                      InputsUnavailable; nothing cached survives
remesh without a material edit          stale for reason mesh; material source unchanged
```

**Policy: whole-material revision.** A rho or cp edit stales a steady result
that used neither, and an E edit stales a thermal result, because P15 has one
revision per material and P17 made the same choice. Cost stated: one
unnecessary re-solve. [PROVENANCE.md](PROVENANCE.md),
[CURRENTNESS.md](CURRENTNESS.md).

## Units

k W/(m K), rho kg/m³, cp J/(kg K), rho cp J/(m³ K) -- P15's types, no
conversion in P18, the three unit traps measured (45 stays 45, 7800 stays
7800, 500 stays 500), the field swap a compile error. [UNITS.md](UNITS.md).

## Tests

```text
tests/thermal/ThermalMaterialTests.cpp          31 cases, 1529 assertions
  the mode chooses P15's consumer; the table is read
  steady and transient resolution with distinctive values; volumetric heat
    capacity derived; units canonical; any finite positive value accepted
  missing k fails both modes; rho and cp required only by transient; steady
    needs neither; transient names every gap deterministically
  the capability matrix, six variants
  a transient analysis on a k-only material is not Ready
  P15 refuses unusable values at entry; the resolver still asks P15;
    missing and zero are different things
  no assignment / deleted material / deleted body; every MaterialProblem reached
  identity and revision preserved, nothing copied; same id new revision;
    three provenances distinct
  k, rho, cp and E edits: mesh current, result stale; reassignment (different
    and identical materials); removal; a failed edit changes nothing;
    a remesh stales on the mesh axis only
  resolution deterministic over 16 repeats and read-only
```

## Zero-match protection

```text
ctest -R 'unit\.ThermalMaterial' -N         Total Tests: 31   (minimum 15)
unit\.ThermalData 36, compile_fail\.thermalids\. 24, unit\.ThermalInput 11,
architecture\.checker\.[a-z-]*thermal 4 -- unchanged
an impossible filter                         0, so the counter is real
```

## Mutation probes

```text
run-time probes                  14   on the production tree, each rebuilt,
                                      run and restored (+ 2 reruns)
killed                           13   steady over-requiring, transient
                                      under-requiring, the consumer mapping,
                                      a default k, default rho/cp, cp / 1000,
                                      rho / 1000, k / 1000, provenance by id
                                      alone, the mode ignored by the result
                                      source, the body check, the capability
                                      query, the no-material category
refused by the compiler           1   rho and cp swapped: a Density is not a
                                      SpecificHeatCapacity
survived                          0
```

Two kills were first by process abort on an empty optional; the tests were
hardened and the probes rerun to clean assertion failures (finding F1).
[ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md) carries the table.

## Adversarial review

```text
QUESTIONS                 38  (32 from the brief, 6 of the reviewer's)
FINDINGS                   2
PRODUCTION DEFECTS         0
TEST METHOD FIXED          1  tests read through an optional without requiring
                              it engaged, so two mutants killed by abort
RECORDED                   1  the two value_or hits in the thermal module are
                              HeatBalance's absent TERM, not a property fallback
GATE-BLOCKING              0
```

## Known limitations

```text
A material is assigned per DOCUMENT in P15 (ADR-026); the resolver takes the
  body to establish that it exists, as P17's does. One solid is ADR-049's
  scope, so no multi-region resolution exists or is tested.

Whole-material revision: a rho, cp or E edit stales a steady thermal result
  that did not use it. One unnecessary re-solve, chosen over a second revision
  mechanism (CURRENTNESS.md).

The resolver's delegation to P15 is tested through the reachable half of
  P15's contract -- an ABSENT required property -- because P15 refuses
  unusable VALUES at entry. The same require* calls would refuse an
  out-of-range value if a file or an import ever produced one.

Negative k repaired by abs(), rho/cp replaced by epsilon: not testable by
  mutation at P18, because the values cannot reach the resolver. P15's own
  entry tests, in the unfiltered runs, are what guard them.

Steady and transient resolution return one type with an optional capacity,
  present exactly when the mode is transient (P17-MAT-001's shape). A
  separate transient type was considered and rejected as over-engineering
  while the project's established pattern is one view plus a mode.

No anisotropic or temperature-dependent conductivity can be expressed in P15,
  so there is nothing for P18 to refuse; when ADR-027's law arrives, the
  resolver is the place that must refuse or evaluate it explicitly.

This MinGW toolchain has no ASan/UBSan.
```

## Regression

Three presets, each configured, **cleaned**, rebuilt and run unfiltered,
then two repeat stages. One uninterrupted detached run, 03:38:26 to
07:29:33, 3h51m07s, 17 stages, `qualify.cmd exit 0`. Before it, the working
tree passed the whole suite in debug-ext (4115/4115, 1986.65 s,
`qualification/prefreeze.txt`).

```text
PRESET            CONFIGURE  CLEAN  BUILD    NO-OP REBUILD  CTEST
debug-ext              0       0   28m45s       1 edge      34m09s   4115/4115
release-ext            0       0   36m03s       1 edge      31m17s   4115/4115
debug-shared-ext       0       0   28m05s       1 edge      32m57s   4115/4115

REPEAT (5x each of 1244 selected tests, back to back)
release-ext            0                            1244/1244   1109.63 s
debug-ext              0                            1244/1244   1249.25 s

warnings, all three clean builds   0   (-Werror, 667 objects each)
no-op rebuilds                     0 compiles, 0 links in every preset
shared build                       11 DLLs, libbettercad_thermal.dll grown
```

**4115** is P18-DATA-001's 4084 plus this milestone's 31 material cases.
**667** objects is 665 plus the resolver and its test file. The material
suite gives the identical 1529 assertions in 31 cases and the same 6 message
lines under `-O0 -g`, under the optimiser and across a DLL boundary.
**Predecessor regression:** nothing in core, features, meshing or structural
changed, every P15 and P17 test ran in each preset, and P17-MAT-001's own
suite was run again with each preset's binary: 878 assertions in 19 cases,
three times. [FREEZE.md](FREEZE.md).

## Result

```text
RESULT:   PASS
TESTS:    4115/4115 in debug-ext, release-ext and debug-shared-ext, each
          from clean; 0 warnings over 667 objects; 1244 x 5 repeats in two
          presets; 17 stages, 0 failed
TREE:     include 5ba4f65a, src 5bd8d50b, tests 1d8b2808 moved; apps,
          examples, cmake, CMakeLists.txt, CMakePresets.json identical to
          the P18-DATA-001 freeze; identical at the freeze, before and after
          the run
P15:      unchanged; the one material authority
EVIDENCE: this directory
ADRs:     none added; ADR-026, ADR-027, ADR-028 and ADR-049 to ADR-051 applied
NEXT:     P18-DOF-001 — Temperature DOF Model. NOT AUTHORIZED by this
          result; each milestone is started by the owner.
```

## Revision

First issue, 2026-10-17.
