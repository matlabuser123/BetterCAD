# P17-MAT-001 — what P15 already owns

```text
SUBJECT:  every property, requirement and validation this milestone's
          checklist names, and who actually owns it
METHOD:   read the qualified code before writing any. The finding is that P15
          owns all of it, and owns it for THIS consumer by name.
```

## The headline, and it changed the milestone

The checklist asks P17 to validate `E > 0`, to validate `-1 < nu < 0.5`, to
reject NaN and infinity, and to define missing-material and custom-material
behaviour. **P15 does all of it, and does it at the point of entry** — which is
a stronger guarantee than validating at the solver, because it means an
unusable value cannot be in a document at all.

That was discovered by a failing test rather than by reading, and the story is
in `ADVERSARIAL_REVIEW.md` finding F1: the first draft of the test file created
materials with `nu = -1`, `E = 0` and NaN, and every case failed at
`createMaterial` instead of at the resolver.

## The property matrix

| Property / concept | Canonical owner | Type | Internal unit | Missing state | Validation owner | P17 use |
| --- | --- | --- | --- | --- | --- | --- |
| Young's modulus | `core/materials` | `ElasticModulus` = `Pressure` | Pa | `MaterialProperty<T>` with no value | **P15**, at entry and at `requireLinearElasticConstants` | consumed |
| Poisson ratio | `core/materials` | `PoissonRatio` | — | same | **P15**, same two places | consumed |
| Density | `core/materials` | `Density` | kg/m³ | same | **P15**, at entry and at `requireDensity` | consumed, gravity mode only |
| Shear / bulk modulus | `core/materials` | `ElasticModulus` | Pa | derived, never supplied | **P15** derives from E and nu (ADR-027) | consumed as derived |
| Material assignment | `features` | `MaterialAssignment` | — | `Unassigned` / `Unresolved` / `Invalid` | **P15** | consumed |
| Custom materials | `features` | `MaterialDefinition` | — | — | **P15**, same validator as any other | consumed, not special-cased |
| Provenance | `core/materials` | `MaterialProvenance` | — | absent | **P15**; **no consumer requires it** | referenced by id, never copied |
| Per-consumer requirements | `core/materials` | `PropertyRequirement` | — | — | **P15**: "THE one definition; nothing else may hold a second copy of this table" | **read**, never restated |
| Completeness | `core/materials` | `CompletenessReport` | — | — | **P15** | surfaced unchanged |

**Nothing in the "P17 use" column says "implemented".** That is the result of
the audit, not a shortcut.

## `ConsumerKind`, read rather than assumed

```text
requiredProperties(FeaLinearStatic)            = { YoungsModulus, PoissonRatio }
requiredProperties(FeaLinearStaticWithGravity)  = { Density, YoungsModulus,
                                                    PoissonRatio }
thermal, both                                   = { }
```

Measured by `StructuralMaterial_TheModeChoosesP15sConsumerAndNothingElseDecidesIt`,
which asserts the exact vectors. If P17 ever grew a second copy of this table,
that test is what would disagree with it.

P15's own comment states the principle, and it is the one that stops P17
over-requiring:

> Note what is NOT required, because over-requiring is as wrong as
> under-requiring and is harder to notice. … FeaLinearStatic needs E and nu
> ONLY. A density is required by the WITH GRAVITY variant, because that is when
> a mass enters the equations. … No consumer requires PROVENANCE.

So the density question is not P17's judgement call. It is a row of P15's table,
and `resolveStructuralMaterial` **reads the row** rather than hardcoding the
rule:

```cpp
const PropertyRequirement required = requiredProperties(consumerFor(mode));
const bool needsDensity = std::ranges::find(required.mechanical,
                                            MechanicalPropertyKind::Density) != ...
```

That is the one line that makes the two impossible to disagree.

## `CompletenessState`: presence AND validity, not presence alone

The brief asks this to be audited rather than inferred. The answer is in
`isAvailable`'s own documentation:

> "Usable" means Known or Derived **AND within range** — a value that validation
> would refuse does not count as present, or a report would call a material
> Ready and then a consumer would fail.

So completeness folds out-of-range into not-available, deliberately. The
consequence for P17 is recorded rather than worked around: there is **no fourth
`MaterialProblem` for "present but invalid"**, because adding one would mean
deciding here what out-of-range means — the duplication this milestone exists to
avoid. The distinction survives where it is useful, in P15's diagnostic, which
says either

```text
"<material> has no Young's modulus"
"<material> has a Young's modulus of X, which is not a usable value"
```

## Where validation actually happens

Three layers, and P17 adds none of them:

```text
1. AT ENTRY       createMaterial and setMaterialMechanical both validate
                  through core/materials/MechanicalProperties.cpp. An unusable
                  E, nu or density is REFUSED, so it never reaches a document.
                  A refused edit also leaves the old value intact.
2. AT COMPLETENESS isAvailable treats an out-of-range value as absent, so a
                  consumer query cannot call such a material Ready.
3. AT CONSUMPTION  requireLinearElasticConstants and requireDensity validate
                  again, name every gap at once, and name the missing INPUT
                  rather than the derived constant.
```

`resolveStructuralMaterial` calls layer 3. Measured: **0 occurrences** of
`isFinite`, `<= 0`, `>= 0.5`, `minPoissonRatio` or `maxPoissonRatio` in
`src/structural/StructuralMaterial.cpp`.

## The ranges, quoted from the code

```cpp
// core/materials/MechanicalProperties.hpp
inline constexpr double minPoissonRatio = -1.0;
inline constexpr double maxPoissonRatio = 0.5;
```

compared with `<=` and `>=`, so both bounds are **exclusive**:
`-1 < nu < 0.5`. Exactly the range the checklist asks for, already implemented
and already qualified.

`E`: present, finite, `> 0`. `rho`: present, finite, `> 0`. Both read out of
`src/features/material/Materials.cpp`.

## Near-incompressibility: no threshold invented

The brief warns against inventing one, and none was. The hard bound is
`nu < 0.5`, which is where `lambda = E nu / ((1 + nu)(1 - 2 nu))` divides by
zero and the isotropic constitutive matrix is singular for a displacement-only
formulation. `nu = 0.49` **resolves**, and there is a test that says so.

Whether 0.49 is numerically *advisable* for a Tet4 formulation is a different
question, and it belongs to `P17-ELEM-001` or `P17-VALID-001`. Keeping
"physically valid" and "numerically advisable" apart is what stops the drift the
brief describes, where three layers each pick a slightly different limit.

## What P17-MAT-001 therefore adds

Three things, and they are the three P15 cannot know:

```text
StructuralAnalysisMode       WHICH consumer this analysis is -- intent, so it
                             lives on the analysis definition. A density on the
                             material does not make a problem a self-weight
                             problem
resolveStructuralMaterial    one solver-ready view per mode, carrying the
                             material's id and revision as references back to
                             P15, and a density exactly when the mode requires
                             one
the integration proof        that an E or nu edit leaves the P16 mesh CURRENT
                             and makes the structural result STALE
```

Plus `structuralMaterialCompleteness`, which is P15's report for P15's consumer,
not reinterpreted, not filtered and not reordered.

## No second canonical record

ADR-028's hard rule, verified by search rather than asserted:

```text
raw double or float data member in the module       0
hardcoded 210, 7850, 0.3, "steel", "aluminium"      0 outside comments
a second requirement table                          0
GUI, io, persistence or command dependency          0
cache, static or mutable state                      0
every Document parameter                             const
mutating P15 call (setMaterial*, modifyObject)       0
```

`StructuralMaterial` holds a `MaterialId`, a revision, a mode, four elastic
constants and an optional density — `sizeof` under 128 bytes, asserted. No
name, no designation, no provenance copy, no database entry.
