# P19-FLUID-001 — Fluid Property Resolution

```text
STATUS:    PASS
MILESTONE: P19-FLUID-001, the third milestone of P19 — CFD Integration
SCOPE:     the fluid a CFD analysis consumes: P15 learns a dynamic viscosity and
           an incompressible-flow consumer; CFD resolves its fluid through P15
           and refuses a missing viscosity by name. No topology, no field, no solve.
```

## Baseline

```text
HEAD at start      c8b53f6a76e3088831679084c85e34e66c311e73  (P19-DATA-001's record)
origin/main        c8b53f6a76e3088831679084c85e34e66c311e73, verified
authorized         2026-10-27, the owner's "start P19-FLUID-001", with the
                   checklist already recorded in TODO.md
```

## What the audit found

ADR-027 had already named the dynamic viscosity a canonical property "(P19)", and
P15's `ConsumerKind` documentation recorded that CFD was left out only until its
physics was decided. ADR-058 decided it. So the milestone was **a P15 schema
change, made the P15 way**: unlike P18-MAT-001, where P15 already had every
property, here the property and the consumer had to be added. The audit mapped
every seam a property flows through: validation, availability, the requirement
table, provenance, removal, the file, the CLI, and the feature functions.

## What was decided (ADR-062)

```text
1  mu is a P15 property, ThermalPropertyKind::DynamicViscosity (appended), in the
   existing "physical and thermal" group beside the electrical resistivity --
   NOT a third FluidProperties group, which would need a third branch at every
   seam; nu = mu / rho is never stored
2  ConsumerKind::IncompressibleFlow requires { density } and { dynamic viscosity },
   nothing thermal; requireIncompressibleFlowProperties is its runtime function;
   P15's old "no CFD consumer" guard becomes "no turbulent-flow consumer"
3  CFD resolves a CfdFluid (id, revision, rho, mu; nu derived) through P15 only:
   P15's function decides and words the refusal, P15's table names the category;
   nothing restated (P19-DATA-001's CFD-side validate(CfdFluidProperties) retired)
4  a fluid edit stales a CFD result through the material revision, for `fluid`
   alone; geometry and mesh untouched; a conductivity edit stales it too
   (conservative, as P18-MAT-001 recorded)
5  the file key is optional and SI: older files load unchanged, no version bump
```

The checklist, item by item: [FLUID_MODEL.md](FLUID_MODEL.md).

## Tests

```text
tests/features/FluidPropertyTests.cpp      11 cases  P15: units, validation at entry, the
                                                     require functions and their wording,
                                                     the consumer row, provenance both ways,
                                                     reference temperature, removal, file
tests/cfd/CfdFluidTests.cpp                13 cases  CFD: resolution through P15, no default,
                                                     every gap named, unusable values cannot
                                                     reach it, completeness, what an edit
                                                     stales and what it does not, determinism
MaterialCli_Viscosity_...                   1 case   the CLI sets, reads and shows it, in Pa s
cli.material.completeness.incompressible-flow        process test
CfdInput_RefusesADensityWithNoViscosity              P19-ARCH-001's tripwire, tripped on purpose
moved counts                                         thermal kinds 5 -> 6, consumers 7 -> 8,
                                                     property codes 14 -> 15
```

## Adversarial review

```text
INDEPENDENT REVIEWER   0 blockers, 0 major; 8 minor, 3 nits -- all fixed or recorded
MUTATION PROBES        17 of 18 killed; FP18 an equivalent mutant (defence in depth
                       behind P15's entry invariant), recorded
AUTHOR'S QUESTIONS     8, each answered by a test
```

[ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md).

## Known limitations

```text
The viscosity is listed under "thermal" in the file and in CLI reports (ADR-062 §1).
A conductivity edit also stales a flow result: the material revision, not a
  per-property one.
cfdFluidCompleteness reports P15's material-level inconsistencies for this
  consumer as for every other; resolveCfdFluid needs only rho and mu.
An older build refuses a file carrying the new key, by name.
One material per document: a fluid and a solid housing still cannot share one.
This MinGW toolchain has no ASan/UBSan.
```

## Regression

Three presets, each configured, **cleaned**, rebuilt and run unfiltered, then two
repeat stages, in one detached run: 22:35:47 to 05:23:21, 6h47m34s, 17 stages,
`qualify.cmd exit 0`, first attempt. Because core's material headers changed, every
module recompiled in every preset.

```text
PRESET            CLEAN  BUILD    NO-OP REBUILD  CTEST
debug-ext           0    30m52s     1 edge       5919/5919   5001.59 s
release-ext         0    36m39s     1 edge       5919/5919   4764.49 s
debug-shared-ext    0    30m51s     1 edge       5919/5919   4962.22 s

REPEAT (5x each of 2783 selected tests)
release-ext                                      2783/2783   1669.34 s
debug-ext                                        2783/2783   2070.39 s

warnings, all three clean builds   0   (-Werror, 753 objects each)
zero-match groups                  246, 0 short, 0 unexecuted, in each preset
fluid/CFD/P15 cases on each binary 2769 assertions in 77 cases, identical
compile-fail (prov + cfdids)       43/43 in each preset
```

[FREEZE.md](FREEZE.md).

## Result

```text
RESULT:   PASS
TESTS:    5919/5919 in debug-ext, release-ext and debug-shared-ext, each from
          clean; 0 warnings over 753 objects; 2783 x 5 repeats in two presets;
          17 stages, 0 failed
TREE:     apps fc398a3d, include 833e8b29, src 27c6be73, tests 2ce7a53c moved;
          examples, cmake, CMakeLists.txt, CMakePresets.json identical to HEAD;
          identical before and after the run
EVIDENCE: this directory
ADR:      ADR-062
NEXT:     P19-MESH-001 is NOT AUTHORIZED
```

## Revision

First issue, 2026-10-27.
