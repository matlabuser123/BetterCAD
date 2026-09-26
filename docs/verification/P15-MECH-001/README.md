# P15-MECH-001 — Mechanical Properties

```text
STATUS:    see RESULT.
MILESTONE: P15-MECH-001
DATE:      2026-09-27
BASELINE:  261cb55, clean tree, HEAD == origin/main,
           HEAD^{tree} = 74a2c315f93378ec4b4b286eb6a0c992c3bc6fb5
```

## PREREQUISITES

Counted from the checklists, not read off a summary line:

```text
P15-ARCH-001         16 of 16 [x]   ADR-025..028
P15-UNITS-001        20 of 20 [x]
P15-MAT-001          18 of 18 [x]
INFRA-QT-DEPLOY-001   8 of 8  [x]
```

The qualified build path is the external one: P15-MAT-001 was qualified from
`%LOCALAPPDATA%\bc-build` with 0 stages failed, and so is this.

## SCOPE

```text
IN     the Known/Unknown/Derived property type ADR-027 specified and nobody had
       yet spelled; density, E, nu; yield, ultimate tensile, ultimate
       compressive and shear strengths; elongation; hardness with its scale;
       range and finiteness validation; the isotropic derivations of G and K;
       the consumer boundary a structural solver will use
OUT    element matrices, B and D matrices, mesh, boundary conditions, loads,
       stress recovery, von Mises, factor of safety -- all P17; thermal
       properties (P15-THERM-001); assignment to geometry (P15-ASSIGN-001);
       mass (P15-MASS-001); the file representation (P15-PERSIST-001)
```

## AUDIT OF THE EXISTING MODEL

Done before any production code, because ADR-027 said the property type belonged
somewhere else and it had to be established whether it was already there.

```text
Type / API                    Current role                     P15-MECH change
Density, Pressure             quantities, SI internally         reused unchanged
Stress, ElasticModulus        ALIASES of Pressure, deliberately reused unchanged
PoissonRatio                  distinct dimensionless type,      reused; its range
                              carrying NO range check           check added here
MaterialId                    identity, widens to ObjectId      untouched
MaterialDefinition            designation/standard/family/      gained one field:
                              notes/origin                      `mechanical`
material library              constexpr, metadata only          untouched
Known/Unknown/Derivable type  DID NOT EXIST                     added
serialization                 a material cannot be saved at     unchanged; still
                              all (P15-PERSIST-001)             refuses, loudly
```

Two audited facts decided the work, and both came from the qualified code rather
than from a plan:

**`PoissonRatio` had already delegated its range to this milestone**, by name:
"It carries NO range check. `-1 < nu < 0.5` is physical validity for ordinary
isotropic elasticity, not dimensional validity, and P15-ARCH-001 keeps the two
apart: the range belongs to the material property layer (P15-MECH-001)."

**ADR-027's property type did not exist.** The ADR says "the exact spelling
belongs to P15-UNITS-001", and P15-UNITS-001 added the quantities and left the
property type alone. It is added here, which is why this milestone touches
`core/materials/` rather than only `features/`.

**Blast radius.** The only pre-existing type changed is `MaterialDefinition`,
which gained a field; everything reached through that is its own `contentEquals`,
`clone`, validation and tests. No ID tag, no layer-table change, no change to any
quantity. The regression set is still the whole suite in three presets, because
what a narrow filter misses is never the new code.

## MECHANICAL PROPERTY MODEL

`materials::MaterialProperty<Value>` — ADR-027's three states, finally spelled.

```text
Unknown   nothing is held, and that is a legitimate state of a material
Known     a value somebody supplied
Derived   a value computed from others; never stored
```

`value()` returns `std::optional<Value>`. There is deliberately **no** conversion
to `Value` and **no** `valueOr()`: either would let an Unknown property become a
number somewhere far away, which is the failure the type exists to prevent. A
compile-fail case proves the conversion does not exist.

`Value` is a `Quantity` for anything with a dimension, and `PoissonRatio`,
`Elongation` or `Hardness` for the three that carry meaning without one.

## DENSITY

The qualified `Density` quantity, reused unchanged — no second representation.
Canonical unit kg/m^3, as P15-UNITS-001 qualified. Validated Known-positive-finite;
Unknown allowed; reachable by a future mass through `requireDensity()`, which is
separate from the elastic constants on purpose. No mass is calculated here.

## YOUNG'S MODULUS

`ElasticModulus` (= `Pressure`), reused. Validated `> 0` and finite when Known.
The restriction is in the property layer, not in `Quantity`, because P15-UNITS-001
separated dimensional validity from physical validity and a modulus of zero is a
perfectly well-formed pressure.

## POISSON RATIO

The semantic `PoissonRatio` type, with the range this milestone owns:

```text
-1 < nu < 0.5      both ends EXCLUDED
```

Neither exclusion is arbitrary. At `nu = 0.5` the bulk modulus divides by zero; at
`nu = -1` the shear modulus does. A ratio at either end is not a material this
model describes, so it is refused rather than allowed to reach a division.
Accepted in tests: -0.999999, -0.5, 0, 0.2, 0.3, 0.49, 0.499999. Refused: -1,
0.5, -1.5, 0.7, 1.0, NaN, +/-inf.

## SHEAR MODULUS AND BULK MODULUS

**There is no slot to store either, and that is the design, not an omission.**
ADR-027:

> "**A supplied G that contradicts E and nu is rejected, not reconciled.** The
> schema does not offer a slot for a canonical G, so the conflict the brief asks
> about cannot be represented — which is the cheapest way to handle it."

So this milestone implements `derivedShearModulus()` and `derivedBulkModulus()`
and **no** `suppliedShearModulus()` or `effectiveShearModulus()` to pair them
with. The brief asks for that triple and for consistency validation between a
supplied G and a derived one; neither is built, because a supplied G does not
exist and therefore cannot disagree with anything. The architecture is the
authority here and this is recorded rather than quietly resolved.

What that buys, in the brief's own terms:

```text
"Can G be silently invented and stored?"                 no slot exists
"Can K be silently invented and stored?"                 no slot exists
"Can supplied inconsistent G be silently replaced?"      no supplied G exists
"Can supplied G hide an inconsistency?"                  no supplied G exists
```

Proved structurally, not asserted: a test walks every kind the model names and
requires the shear and bulk moduli to be the *only* two marked derived.

```text
G = E / (2(1 + nu))
K = E / (3(1 - 2nu))
```

Both are computed with `Quantity` arithmetic — `modulus / (2.0 * (1.0 + ratio))` —
so the result **is** a modulus by construction rather than by a `fromSi()` a
reader has to trust. Compile-time `static_assert`s pin the value type to
`ElasticModulus` and away from `Density`, `Energy` and `double`.

## SUPPLIED VS DERIVED PROPERTIES

A stored property is supplied or unknown. A derived one is produced on request
and never stored, so there is one source of truth for every value.

`MaterialProperty::derived()` is public, because the derivation functions are
ordinary code. That left a hole, which adversarial review found and which is now
closed: `validate()` refuses a Derived value in any **stored** slot, because such
a value claims to have been computed with nothing to have computed it from — a
supplied value and a derived one becoming indistinguishable, the exact failure
this model exists to prevent. Tested for every slot.

## STRENGTHS

Four independent properties, each optional, each validated `> 0` and finite when
Known:

```text
yield strength                 sigma_y
ultimate tensile strength      sigma_u
ultimate compressive strength  independent of the tensile one
shear strength                 independent of both
```

**Nothing is inferred from anything.** A yield strength does not produce an
ultimate; an ultimate does not produce a compressive; no `tau = 0.577 sigma_y`
appears. A test supplies one strength and requires the other three to stay
Unknown, then supplies a compressive strength unequal to the tensile one and
requires that to be accepted, because for real materials they differ.

## ELONGATION

**A fraction, fixed here rather than left to the caller: `0.12` is 12 %.**
`Elongation::of(0.12)` and `Elongation::ofPercent(12.0)` are the same value, and
`percent()` is the display boundary. "12" meaning both 12 % and 1200 % is exactly
the ambiguity that puts a factor of 100 into an engineering result, so the type
settles it and a bare `double` cannot become one (compile-fail case).

Validated `>= 0` and finite. **No upper limit**: elongations above 100 % are
ordinary for elastomers, and an invented ceiling would reject real data.
`Elongation::of(12.0)` — 1200 % — is therefore valid, and tested as such.

## HARDNESS

A number **and** its scale, together in one value. `Hardness::of(60.0,
HardnessScale::RockwellC)`; there is no constructor taking a number alone
(compile-fail case), and `toString` always prints the symbol, so a hardness can
never be shown or stored as a bare number.

```text
Brinell HBW, Vickers HV, Rockwell B HRB, Rockwell C HRC
```

`60 HRC != 60 HRB`, and a property holding one is unequal to a property holding
the other — tested. There is no `Other` scale and no free-text scale: a hardness
whose scale is unknown is a number without meaning, and `unknown()` is how a
material says it has no hardness.

**No conversion between scales.** Published correlations are approximate and
material-dependent; none is applied without a verified source and a milestone
that asks for it. **Hardness is not a `Pressure`**, so it cannot be compared with
a yield strength (compile-fail case), even though some indentation tests are
defined as a force over an area.

Validated `> 0` and finite. No per-scale range is imposed: the usable range of
each scale comes from its standard, which BetterCAD does not have here, and
inventing one would reject real measurements.

## ISOTROPIC MODEL

The coherent pair is `E` and `nu`; `G` and `K` follow from them on request.
Nothing else is claimed, and no constitutive matrix, element or solver exists.

**Completeness is per consumer, not one flag.** `hasLinearElasticConstants()`
answers what linear elasticity needs; `hasDensity()` answers what a mass needs.
A single `isComplete()` would mean nothing to either: a stiffness calculation must
not fail over a missing density it never uses, and a test requires exactly that.

## UNKNOWN PROPERTY SEMANTICS

```text
unknown E      -> value() is empty, NOT 0 Pa
unknown yield  -> value() is empty, NOT 0 Pa
unknown rho    -> value() is empty, NOT 0 kg/m^3
unknown e      -> value() is empty, NOT 0
```

Tested by comparing each Unknown property against a property holding zero and
requiring them to differ. A wholly empty `MechanicalProperties` is valid data and
needs no placeholder to be constructible, which is what lets a partially
characterised material — density and E known, nu and yield unknown, the shape most
datasheets take — exist at all.

## PROPERTY RANGE AND NON-FINITE VALIDATION

```text
density                        > 0, finite
Young's modulus                > 0, finite
Poisson's ratio                -1 < nu < 0.5, finite
yield / tensile / compressive
  / shear strength             > 0, finite
elongation                     >= 0, finite, no upper limit
hardness                       > 0, finite, no per-scale range
any stored property            never in the Derived state
```

`validate()` reports **every** problem it finds, not the first, so someone fixing
a material sees the whole list — tested with four simultaneous faults.

**Where the finiteness boundary sits.** A `MaterialProperty` can *hold* a NaN in
a local variable, because quantity construction is unchecked throughout BetterCAD
and making `known()` return a `Result` would complicate every call site. What is
guaranteed is that it cannot enter a material: `createMaterial` and
`setMaterialMechanical` both validate first, and both are tested with NaN and
with infinity, leaving the material exactly as it was.

## CONSISTENCY VALIDATION

Individually valid properties that disagree with each other are **reported, never
corrected and never a reason to refuse the data** — a user entering a datasheet
may have mistyped, and telling them is useful while silently changing their
numbers is not.

The one rule available is `sigma_u >= sigma_y`: the ultimate tensile strength is
the highest stress a tensile test reaches, so it cannot lie below the stress at
which yielding began. Equal is allowed, because a perfectly brittle material
yields and breaks together. The tolerance is relative and tight (1e-12), since
these are two numbers a user typed rather than an accumulation.

There is no supplied-vs-derived modulus check, for the reason given above: no
supplied modulus exists to disagree.

## MATERIAL IDENTITY INTEGRATION

`MaterialDefinition` gained one field, so a property edit goes through the same
`setDefinition` path as a designation and cannot reach the `MaterialId`.

```text
id before a mechanical edit == id after      tested, twice over
metadata after a mechanical edit             unchanged, tested
a rejected edit                              leaves the material exactly as it was
```

Everything P15-MAT-001 qualified still holds and is still tested: duplicate
designations, unique object names, deleted IDs never rebinding, deterministic
enumeration.

## LIBRARY IMMUTABILITY

The built-in library still carries **no property values**, and that is deliberate:
ADR-028 requires a recorded source per value, and this milestone has no verified
sources it can cite, so inventing plausible numbers to make the library look
finished is exactly what it does not do.

Immutability is therefore proved **structurally** rather than by mutating a
populated entry:

```text
entries are constexpr with no setters          nothing to mutate
builtInMaterials() returns span<const T>       compile-fail case proves assignment
                                               is refused as discarding const
import COPIES into the document                tested: editing the copy leaves the
                                               entry identical
two documents importing one entry              tested independent
```

An import arrives with every mechanical property Unknown — not with a placeholder
— and a second import after the first has been edited still arrives empty, so
there is no shared mutable state to reach.

## DOWNSTREAM FEA CONTRACT

```text
features::requireLinearElasticConstants(document, id) -> Result<LinearElasticConstants>
features::requireDensity(document, id)                -> Result<Density>
```

`LinearElasticConstants` holds `E`, `nu`, `G`, `K` as concrete typed quantities —
no optionals, no states, no raw doubles. A solver never sees a JSON field name, a
library implementation, GUI state or a `double`.

It hands over a complete set or it fails. The diagnostic names the material and
then **every** missing input at once, in ADR-027's style:

```text
SteelA (object:1) has no Young's modulus and no Poisson's ratio, which linear
elasticity needs
```

It names the missing **input**, never the derived constant: told that the shear
modulus is unavailable a user has nothing to act on, told that the Poisson ratio
is unknown they do. A test asserts the message mentions the ratio and does **not**
mention "shear modulus" or "bulk modulus". There is no `nu = 0.3` fallback
anywhere behind it.

## DETERMINISTIC ENUMERATION

`mechanicalPropertyKinds()` returns a fixed array in semantic order — density, E,
nu, G, K, yield, ultimate tensile, ultimate compressive, shear, elongation,
hardness. Traversed ten times in one test and required to be identical; no
`unordered_map`, hash or pointer ordering exists in the new code (checked by
grep). Every kind has a name, so a diagnostic cannot print a number alone.

## COMPILE-TIME UNIT SAFETY

Nine cases in `tests/compile_fail/MechanicalMisuse.cpp`, each failing for the
right reason:

```text
mech.density-as-youngs-modulus        a density is not a modulus
mech.conductivity-as-yield-strength   nor a conductivity a strength
mech.specific-heat-as-shear-modulus   nor a specific heat a modulus
mech.poisson-from-double              a bare number is not a Poisson ratio
mech.elongation-from-double           nor an elongation
mech.hardness-without-scale           a hardness needs its scale
mech.hardness-as-stress               a hardness is not a pressure
mech.property-to-value                a property is not its value
mech.mutate-library-entry             a library entry cannot be assigned to
```

The control translation unit, built with no case macro, does all nine correctly
**and** reads a library entry, so neither the type safety nor the include can rot
unnoticed. Plus `static_assert`s in the behavioural tests pinning the derived
moduli to `ElasticModulus` and the property value types to `Density`,
`PoissonRatio` and `Elongation`.

## INDEPENDENT VALIDATION

Fixture `E = 210 GPa`, `nu = 0.30`. Expected values are hand-computed decimals
written in the test, never produced by calling the production relationship.

```text
Case          Expected (hand-computed)        Actual (measured)
E             210 GPa                         210000000000 Pa
nu            0.30                            0.3
G derived     80.76923076923077 GPa           80769230769.23077392578125 Pa
K derived     175 GPa                         174999999999.999969482421875 Pa
```

G is **bit-identical** to the hand-computed decimal. K is 1 ULP below 175 GPa —
a relative error of 1.7e-16 — because `3.0 * (1.0 - 2.0 * 0.3)` is not exactly
1.2 in binary floating point. That is why the tolerance is relative rather than
exact equality, and why it is 1e-12: the value CLAUDE.md gives for
well-conditioned double-precision algebra, four orders of magnitude above the
error actually observed.

Explicit failures, all tested:

```text
E = 0                 rejected
E < 0                 rejected
nu = -1               rejected
nu = 0.5              rejected
NaN (any property)    rejected
+inf / -inf           rejected
missing E             G and K Unknown, not zero
missing nu            G and K Unknown, not zero
missing both          G and K Unknown
nu invalid            G and K Unknown; no division is reached
stored Derived state  rejected
```

## TARGETED TESTS

47 new, all discovered by CTest, taking the suite from 2353 to 2400.

```text
tests/core/materials/MechanicalPropertiesTests.cpp   28   the model and the derivations
tests/features/MaterialTests.cpp                      9   identity, library, consumer API
tests/compile_fail/MechanicalMisuse.cpp               9   type safety
tests/io/DocumentFileTests.cpp                        1   saving refuses, loudly
```

## PERSISTENCE BOUNDARY

Not implemented, and not falsely claimed. A material cannot be saved at all yet
(P15-PERSIST-001), so mechanical properties add no new silent-loss path — but the
gap is kept **loud**: the document writer refuses an object type it does not know,
so a material carrying a Young's modulus cannot be half-written. A test saves a
document holding a material with density, E and nu, requires the save to fail
naming the type, requires no file or temporary to be left behind, and requires the
properties to still be in the document afterwards.

The dangerous version of this gap would be a tolerant writer that saves the
material and omits the properties. That test is what stops it appearing.

## THREAD-SAFETY

None claimed, none invented; the document and `IdAllocator` are not thread-safe
and materials add no guarantee. The library is `constexpr` reference data, so it
cannot be raced on, and no test depends on execution order.

## ADVERSARIAL REVIEW

All 23 questions from the brief were answered against the code. Three findings.

**1. A stored property could claim to be Derived.** `MaterialProperty::derived()`
is public, so a caller could put a Derived value into a canonical slot — a stored
value asserting it had been computed, in a slot with nothing to compute it from.
That is "a supplied property and a derived property becoming indistinguishable",
which is the failure the whole state machine exists to prevent. `validate()` now
refuses it in every stored slot, with a test per slot.

**2. A compile-fail case was failing for the wrong reason.**
`mech.mutate-library-entry` was meant to prove a library entry cannot be assigned
to. It failed with `'builtInMaterials' is not a member of 'bettercad::materials'`
— the file never included `MaterialLibrary.hpp`, so the case proved nothing about
immutability while looking like it did. Fixed, and the control now reads a library
entry so the include cannot rot unnoticed. It now fails on
`const ... LibraryMaterial ... discards qualifiers`, which is the immutability.

**3. Four compile-fail regexes expected the wrong compiler wording.** GCC says
"cannot convert" here, not "could not convert" as in the existing ID cases; the
wording differs by context. The regexes are now specific — `cannot convert
'double' to ...PoissonRatio` rather than a loose alternation — so they cannot pass
on an unrelated error.

Answered and already covered:

```text
unknown E / yield becomes zero      AnUnknownPropertyHasNoValueAndIsNotZero
NaN becomes a known property        Refuses...AndKeepsWhatItHad (material layer)
E negative                          RejectsAKnownYoungsModulus...
nu = 0.5 reaches K                  AnInvalidInputNeverReachesADivision
nu = -1 reaches G                   AnInvalidInputNeverReachesADivision
G / K invented and stored           HasNoSlotToStoreAShearOrBulkModulus
supplied G replaced / hiding        not representable (ADR-027)
tensile copied into compressive     StrengthsAreIndependentAndNoneIsInferred...
shear strength estimated            StrengthsAreIndependentAndNoneIsInferred...
12 % confused with 0.12             ElongationIsAFractionAndSaysSoBothWays
hardness loses its scale            HardnessCarriesItsScale...; compile-fail
60 HRC == 60 HRB                    HardnessCarriesItsScale...
changing E changes MaterialId       EditingAMechanicalPropertyDoesNotChangeItsId
built-in mutated through a property AnImportedMaterialHasNoProperties...
clone shares mutable state          MechanicalPropertiesOfTwoDocumentsAreIndependent
missing data blocks valid material  APartiallyCharacterisedMaterialIsValidData
P17 sees only raw doubles           LinearElasticConstants holds typed quantities
iteration nondeterministic          EnumeratesItsKindsInAFixedSemanticOrder
save/load loses data silently       the io test above
Debug and Release differ on G / K   12000 = 2400 x 5 in both, and DerivationIsRepeatableToTheBit
```

## DETERMINISM

`MechanicalProperty_DerivationIsRepeatableToTheBit` computes G and K twenty times
and requires **bit-identical** results, not merely close. Across configurations,
the same fixtures pass in Debug, Release and Debug-shared, and both repeat stages
ran the whole suite five times over with no failure.

## FULL REGRESSION

Three presets, each from the external build root `%LOCALAPPDATA%\bc-build`,
configured, cleaned, built with warnings as errors, proved fresh by a no-op
rebuild, and only then tested.

```text
preset             configure  clean  build  no-op rebuild  suite       warnings
debug-ext              0        0      0          0        2400/2400      0
release-ext            0        0      0          0        2400/2400      0
debug-shared-ext       0        0      0          0        2400/2400      0

repeat release-ext, [A-Za-z], until-fail:5   exit 0   12000 = 2400 x 5, 0 failures
repeat debug-ext,   [A-Za-z], until-fail:5   exit 0   12000 = 2400 x 5, 0 failures
qualification finished 09:39:25, 0 stage(s) failed
```

2400 = the 2353 of P15-MAT-001 plus the 47 added here, and all three presets
discover the same 2400, so no test was lost or duplicated by the new registration.

`verify-harness.cmd` was run first and required a non-zero exit from a preset that
does not exist: 3 stages failed, exit 3.

**No replace fault anywhere.** `Permission denied`, `cannot replace` and `Not Run`
appear in none of the logs, across 24000 repeat executions plus three full suites.
No controlled rerun was needed or used.

**Qualified tree = committed tree.** The eight tree IDs recorded before the first
build and after the last test run are identical, and equal to the committed tree:

```text
apps a3528787f4b24097c34ca11961d6c0c642315f0e
include aed634aeecae62bd66bfa5dbf28e9ef5487a80d6
src b5eb24b9b1cb3c296bf1079a154bbf62302fee46
tests 6225a6f5da79be4c46b60740df9c0848333f7bdb
examples 2e1ef60bb5e67fa3589e1246613261cd746ddce2
cmake 7ed703a3031144d26063a1ff02996e7664e29524
CMakeLists.txt 13823e788ed3975792affa9ee4354ffce9479d80
CMakePresets.json 3229f0f89b236744d7d5c082bed47c28b718288e
```

Qualified on the first run, unlike the previous two milestones. The shared build
in particular came through clean; the dll-imported-constant failure that voided
P15-MAT-001's first run did not recur, because the types added here are a template
and two header-only value classes with no exported constant for a test to bind to.

## WARNINGS

0 in all three builds, with `-Werror` and the project's full warning set. Two
warnings were hit and fixed during development, both in test code: a range-based
`for` binding a `const std::string&` to a temporary from a `const char*`, and a
`string_view` that would not convert to `std::string`.

## KNOWN LIMITATIONS

- **The library carries no property values.** ADR-028 requires a recorded source
  per value and there are none to cite, so library immutability for properties is
  proved structurally rather than by mutating a populated entry. It becomes a
  live concern in the milestone that adds sourced values.
- **A material still cannot be saved.** P15-PERSIST-001. Loud, tested, and now
  covering a material that carries properties.
- **An incompressible material cannot be represented.** `nu = 0.5` is excluded
  because `K` is singular there. A true elastomer at the incompressible limit
  needs a different formulation, with its own ADR.
- **Anisotropy is not representable**, which follows from having no supplied G:
  independent elastic constants are a different property set, and ADR-027 says so.
- **`Stress` and `ElasticModulus` are aliases of `Pressure`**, so the type system
  cannot tell a yield strength from a modulus. Deliberate and documented in
  P15-UNITS-001; the field name carries the distinction.
- **No per-scale hardness ranges**, so 900 HRC validates. Imposing one needs the
  standards data BetterCAD does not have here.
- **A `MaterialProperty` can hold a non-finite value** in a local variable. It
  cannot enter a material, which is where it matters and where it is tested.
- **No temperature dependence.** ADR-027 fixed the extension path — a property's
  value becomes a law evaluated at a state — and nothing here blocks it, because
  consumers ask through `requireProperty`-shaped calls rather than reading a
  number.

## RESULT

```text
TASK:            P15-MECH-001 -- Mechanical Properties
BASELINE:        261cb55, clean, HEAD == origin/main
PREREQUISITES:   P15-ARCH-001 16/16, P15-UNITS-001 20/20, P15-MAT-001 18/18,
                 INFRA-QT-DEPLOY-001 8/8
IMPLEMENTATION:  ADR-027's Known/Unknown/Derived property type, which did not
                 exist; density, E, nu, four strengths, elongation as a
                 fraction, hardness with its scale; ranges and finiteness;
                 G and K derived and never stored; the typed consumer boundary
TESTS:           47 new, 2353 -> 2400. 28 model, 9 integration, 9 compile-fail,
                 1 that saving refuses loudly
VALIDATION:      G bit-identical to the hand-computed 80.76923076923077 GPa;
                 K 1 ULP below 175 GPa, explained; every rejection path tested
ADVERSARIAL:     23 questions; 3 findings, 3 fixed, 0 remaining. One was a
                 compile-fail case that had been failing for the wrong reason
DETERMINISM:     12000 = 2400 x 5 in release-ext AND debug-ext, 0 failures;
                 G and K bit-identical over 20 repetitions
REGRESSION:      3 presets from an external build root, 2400/2400 each, 0
                 warnings, fresh binaries, 0 stages failed, first attempt
RESULT:          PASS
EVIDENCE:        docs/verification/P15-MECH-001/
NOT CLAIMED:     that a supplied shear or bulk modulus is supported -- ADR-027
                 gives it no slot, so the brief's supplied-vs-derived
                 reconciliation is not built and cannot be needed.
                 That any library entry carries a property value.
                 That a material can be saved.
TODO:            P15-MECH-001 18 of 18 ticked. P15-THERM-001 NOT started.
```

## REVISION

```text
2026-09-27  Implemented, adversarially reviewed and qualified on the first run.
            The central design question -- what to do about a supplied shear
            modulus that contradicts E and nu -- was already answered by
            ADR-027, which gives it no slot, so it is not representable and no
            reconciliation exists. Three review findings, all fixed: a stored
            property able to claim it was derived; a compile-fail case failing
            on a missing include rather than on the immutability it claimed to
            test; and four compile-fail regexes expecting the wrong GCC wording.
```
