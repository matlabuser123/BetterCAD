# P15-THERM-001 — Physical / Thermal Properties

```text
STATUS:    see RESULT.
MILESTONE: P15-THERM-001
DATE:      2026-09-27
BASELINE:  4940932, clean tree, HEAD == origin/main,
           HEAD^{tree} = 334832a8775d3d0abbc8b25cb300016901e88e43
```

## PREREQUISITES

Counted from the checklists, not read off a summary line:

```text
P15-ARCH-001         16 of 16 [x]   ADR-025..028
P15-UNITS-001        20 of 20 [x]
P15-MAT-001          18 of 18 [x]
P15-MECH-001         18 of 18 [x]
INFRA-QT-DEPLOY-001   8 of 8  [x]
```

The qualified build path is still the external one: P15-MECH-001 was qualified
from `%LOCALAPPDATA%\bc-build` with 0 stages failed, and so is this.

## SCOPE

```text
IN     thermal conductivity, specific heat capacity, thermal expansion
       coefficient, melting temperature, electrical resistivity; the
       reference-temperature record; physical ranges and finiteness; the
       constant-property contract and the shape a temperature-dependent law
       must take; the consumer boundaries a thermal solver will use
OUT    the heat equation, elements, volumes, mesh, temperature fields,
       boundary conditions, convection, radiation, heat sources, time
       integration -- all P18; assignment (P15-ASSIGN-001); mass
       (P15-MASS-001); full provenance (P15-PROV-001); the file
       representation (P15-PERSIST-001)
```

## AUDIT OF THE CURRENT MODEL

```text
Type / API                      Existing role              P15-THERM change
Density                         quantity, kg/m^3           reused; NOT duplicated
ThermalConductivity             quantity, W/(m K)           reused unchanged
SpecificHeatCapacity            quantity, J/(kg K)          reused unchanged
ThermalExpansionCoefficient     quantity, 1/K               reused unchanged
Temperature                     quantity, K, ONE unit, 1:1  reused unchanged
MaterialProperty<Value>         Known/Unknown/Derived       gained ONE field:
                                                            referenceTemperature
MechanicalProperties            density, E, nu, strengths    unchanged; its
                                                            validate() now also
                                                            checks the new field
MaterialDefinition              metadata + mechanical        gained `thermal`
MaterialId                      identity                     untouched
material library                constexpr, metadata only     untouched
serialization                   a material cannot be saved   unchanged; still
                                                             refuses, loudly
electrical resistivity          DID NOT EXIST, and could     added as a scoped
                                not: no current dimension    strong type (ADR-029)
```

**Blast radius.** One field on `MaterialProperty<Value>`, which P15-MECH-001
qualified, plus one field on `MaterialDefinition`. That is small and it was not
harmless: the new field gave `known()` a second overload, which changed what the
compiler says about a misuse and broke four compile-fail assertions from the
previous milestone. See ADVERSARIAL REVIEW 1. The regression set is the whole
suite in three presets, which is how that was found.

## THERMAL PROPERTY MODEL

`materials::ThermalProperties`, built on the **same** `MaterialProperty<Value>`
the mechanical properties use — no second property system, as required.

```text
thermalConductivity      k       W/(m K)
specificHeatCapacity     cp      J/(kg K)
thermalExpansion         alpha   1/K
meltingTemperature               K, ABSOLUTE
electricalResistivity            Ohm m, scoped strong type (ADR-029)
```

Every property defaults to Unknown. A material characterised only mechanically
needs no thermal placeholders, and a conductivity without a specific heat — what
many datasheets give — is valid data.

## DENSITY

**One density, and it is not in this struct.** It stays in
`MechanicalProperties::density`, where P15-MECH-001 put it, and thermal consumers
read that same property. There is no `thermalDensity` beside a
`mechanicalDensity`, because two authoritative values for one physical quantity is
the defect this arrangement exists to prevent.

The join is `features::requireTransientConductionProperties()`, which can see both
halves of the material and takes the density from the mechanical half.

`Material_HasExactlyOneDensityAndBothConsumerPathsReadIt` proves it four ways: with
no density, the thermal path reports it missing; set once, both paths return the
same value bit for bit; changed, both paths change; set back to Unknown, both
report it missing again.

## THERMAL CONDUCTIVITY

The qualified `ThermalConductivity`, W/(m K). Validated `> 0` and finite when
Known; zero is refused, because a conductivity of zero is not a material this
model describes and no ADR asks for a perfect insulator as an idealisation.

**The Fourier-law contract is settled by types, not by a solver.** `q = -k grad(T)`
is P18's. What is proved here, at compile time, is that `k` carries conductivity
dimensions and composes correctly:

```cpp
static_assert(std::is_same_v<decltype(ThermalConductivity{} * (Temperature{} / Length{})
                                      * Area{}), Power>);
```

and that it is none of `Power`, `SpecificHeatCapacity` or `Pressure`. The last
covers "can k be confused with a stiffness".

## SPECIFIC HEAT

The qualified `SpecificHeatCapacity`, J/(kg K). Validated `> 0` and finite. No
default cp is invented anywhere.

**Specific, therefore not a total heat capacity.** A total heat capacity is J/K and
depends on how much material there is; the mass is the difference and it is a
dimension, so the compiler can see it. A compile-fail case passes `10000_J / 10_K`
where a specific heat is wanted and requires it to be refused.

## THERMAL EXPANSION

The qualified `ThermalExpansionCoefficient`, 1/K. Validated **finite only**.

**A negative coefficient is accepted, deliberately.** Some real materials contract
when heated, so a positive-only range would reject good engineering data, and no
standard BetterCAD has justifies an upper bound either. Tested across
-12e-6, -1e-9, 0, 12e-6, 23e-6 and 1.0 per kelvin, and rejected only for NaN and
the infinities. `features::requireThermalExpansion()` returns a negative
coefficient unchanged.

## MELTING TEMPERATURE

An **absolute** thermodynamic temperature, validated above absolute zero and
finite. Refused at 0 K, -1 K, -273.15 K, -933 K, NaN and both infinities.

Unknown is valid, and nothing infers it. There is no path from a material family,
a designation, a strength or a density to a melting point: no `"Steel" -> 1800 K`.

## ELECTRICAL RESISTIVITY

**This needed an architectural decision, and it has one: [ADR-029](../../architecture/decisions/ADR-029-electrical-resistivity-is-a-scoped-strong-type-not-a-sixth-base-dimension.md).**

ADR-027 had already found and recorded the problem: `Dimension` has five
exponents — length, mass, time, temperature, angle — so resistivity's `M L^3 T^-3
I^-2` has no home, and "adding one is a base-dimension change, not an alias". That
was audited again here and is still exactly true.

Three candidates were weighed. The decision is a **scoped strong type** on the
`PoissonRatio` pattern, rather than a sixth base exponent, because adding one
changes the qualified core of the unit system for a property no P15 milestone
consumes, and because `PoissonRatio` is the repository's existing answer for a
quantity that does not fit.

**The limitation is stated at the definition rather than discovered: there is no
dimensional checking.** This type cannot be multiplied by a length, and
electrical conductivity is not derivable from it in a typed way. What it does
guarantee is the confusion that actually costs something — resistivity and mass
density are both written `rho`, and a compile-fail case proves a `Density` cannot
become one, in either direction, and neither can a bare `double`.

No conductivity is stored or derived. `sigma = 1 / rho` is a relationship a
consumer may compute; it is not a second authoritative spelling of one property.

## TEMPERATURE METADATA

`MaterialProperty::known(value, at)` records the temperature a value was specified
or measured at. It is on the **one** property wrapper, so a modulus measured at
20 C is as ordinary as a conductivity measured there — which is also ADR-028's
model, where "the state it was measured at" is per-property provenance. The rest
of provenance (source, standard, revision, date) stays P15-PROV-001.

Validated as the absolute temperature it is: above absolute zero and finite,
refused for 0 K, negatives, NaN and the infinities — on mechanical properties as
well as thermal ones.

**A reference temperature does not make a property a function of temperature.**
`k = 167 W/(m K) at 293.15 K` does not mean `k(T)`. The value is a constant used
as-is over whatever range a consumer works in; the record says where the number
came from. Nothing evaluates the value against the temperature, and a test asserts
the value is unchanged by the record while the two properties remain distinguishable.

## CONSTANT-PROPERTY CONTRACT

A property's value is one number, used as-is. That is all P15 stores, and the
`Constant` law kind names it.

## FUTURE TEMPERATURE-DEPENDENT CONTRACT

Not implemented — fixed, so that today's data is not thrown away and a later law
cannot arrive with different rules. ADR-027 already fixed the essential
separation:

> "the separation is between a property's IDENTITY — which material, which
> property kind, its provenance — and its VALUE REPRESENTATION ... a later
> milestone replaces the value representation with a law ... WITHOUT TOUCHING
> material identity, assignment, or the Known/Unknown/Derivable states."

So a law changes none of: `MaterialId`, which kind a value belongs to, whether it
is Known or Unknown, or the shape of a consumer call. A consumer asks
`requireX(material)` today and `requireX(material, state)` later — a parameter,
not a restructured caller. Evaluating a constant at any temperature yields the
constant, so a temperature-aware consumer written later works against P15 data
unchanged.

```text
PropertyLawKind        Constant (implemented), Table, AnalyticLaw (named only)
OutOfRangeBehaviour    Fail, Clamp, Extrapolate -- and NO DEFAULT
```

Naming all three out-of-range choices with none of them a default is what stops
silent extrapolation beyond a material's measured range. `Extrapolate` is
explicitly "never the default".

`temperatureTableInvariants()` fixes six invariants now, while there is no table
to argue with, and a test pins them:

```text
the independent variable is absolute temperature
values carry their property's own strong type
temperature points are ordered, ascending
temperature points are deterministic, never from unordered iteration
duplicate temperature points are invalid
behaviour outside the range is chosen explicitly, never defaulted
```

## ABSOLUTE TEMPERATURE VS TEMPERATURE INTERVAL

**The type system cannot tell them apart, and this milestone does not pretend
otherwise.** `Temperature` is `Quantity<dimensions::temperature>` and is used for
both. That is not laziness — the relationships require it: `alpha * dT` has to
cancel a `1/K` against a `K`, which only works if `dT` is dimensionally a
temperature.

What the system does guarantee is that there is no offset to get wrong.
`ThermalProperty_TemperatureIsKelvinOnlySoThereIsNoCelsiusOffsetToGetWrong` walks
the unit catalog and requires **exactly one** temperature unit, with scale 1:1. A
Celsius unit cannot be a scale — it needs an offset the system does not have — so
that test fails the day one is added, rather than a conversion silently going
wrong.

The distinction that matters is therefore kept in **validation, not types**: a
melting temperature must be above absolute zero, while a temperature interval of
-20 K is an ordinary cooling. A test asserts a 20 K interval and an absolute 20 K
compare equal, deliberately, so the limitation is recorded rather than implied.

## PHYSICAL RANGE VALIDATION

```text
thermal conductivity        > 0, finite
specific heat capacity      > 0, finite
thermal expansion           finite ONLY -- negative is valid
melting temperature         > 0 K, finite
electrical resistivity      > 0, finite
reference temperature       > 0 K, finite (any property)
density                     > 0, finite (validated with the mechanical half)
any stored property         never in the Derived state
```

`validate()` reports **every** problem, not the first — tested with four
simultaneous faults. Unknown properties are never problems.

## UNKNOWN PROPERTY SEMANTICS

```text
unknown k   -> value() is empty, NOT 0 W/(m K)
unknown cp  -> value() is empty, NOT 0 J/(kg K)
```

Tested by comparing each against a property holding zero and requiring them to
differ. No NaN-as-unknown representation exists: Unknown is a state, not a
sentinel value.

## NON-FINITE VALIDATION

NaN, `+inf` and `-inf` tested through the public API for every new property, both
at `validate()` and through `setMaterialThermal()` / `createMaterial()`, which
leave the material exactly as it was. As in P15-MECH-001, a `MaterialProperty` can
*hold* a non-finite value in a local variable — quantity construction is unchecked
throughout BetterCAD — but it cannot enter a material.

## MECHANICAL / THERMAL COEXISTENCE

One material holding density, E, nu, yield, k, cp, alpha, melting temperature and
resistivity at once. Editing the thermal half leaves the mechanical half
identical; editing the mechanical half leaves the thermal half identical; the
`MaterialId` is unchanged by either. `setMaterialThermal` and
`setMaterialMechanical` each carry the other half over untouched rather than
rebuilding the definition.

## MATERIAL IDENTITY INTEGRATION

```text
id before a thermal edit == id after       tested
metadata after a thermal edit              unchanged, tested
a rejected thermal edit                    leaves the material exactly as it was
```

## LIBRARY IMMUTABILITY AND CLONE INDEPENDENCE

The library still carries no property values, so an import arrives with every
thermal property Unknown — not with a placeholder. Editing the imported material
leaves the library entry identical, and a second import after the first has been
edited still arrives empty: there is no shared mutable thermal state to reach.
Two documents importing one entry are independent.

Immutability remains structural: `constexpr` entries, `span<const T>`, copy on
import, with a compile-fail case proving an entry cannot be assigned to.

## CONSUMER REQUIREMENT FOUNDATION

Per consumer, never one ambiguous `thermalComplete()`:

```text
requireThermalConductivity()              steady conduction: k, and nothing else
requireTransientConductionProperties()    density + cp + k, all concrete
requireThermalExpansion()                 alpha
requireDensity()                          the same one density (P15-MECH-001)
```

A test proves the separation matters: with only `k` set, steady conduction is
satisfied and transient conduction fails naming the density and the specific heat
but **not** the conductivity. A stiffness or a conduction calculation must not
fail over a property it never uses.

Each failure names the material — `SteelA (object:1)` — and every missing input at
once, so a run fails once with the whole list.

## COMPILE-TIME UNIT SAFETY

Ten cases in `tests/compile_fail/ThermalMisuse.cpp`, each failing for the right
reason:

```text
therm.pressure-as-conductivity            a modulus is not a conductivity
therm.energy-as-specific-heat             an energy is not a specific heat
therm.heat-capacity-as-specific-heat      J/K is not J/(kg K)
therm.poisson-as-thermal-expansion        nu is not alpha
therm.dimensionless-as-thermal-expansion  alpha is NOT dimensionless
therm.density-as-resistivity              the two rhos cannot be swapped
therm.resistivity-from-double             a bare number is not a resistivity
therm.resistivity-to-double               nor does one become a number
therm.temperature-from-double             a melting point is a Temperature
therm.conductivity-as-temperature         and a conductivity is not one
```

The control translation unit does all ten correctly, including recording a
reference temperature, so the type safety cannot rot unnoticed. Plus
`static_assert`s in the behavioural tests pinning every property's value type and
the three relationship result types.

## RELATIONSHIP VALIDATION

Data contract only; no PDE, no solver. Each expected value is a hand-computed
decimal written in the test, and each result type is proved by `static_assert`
rather than inferred from the number.

## INDEPENDENT VALIDATION

Fixtures `alpha = 12e-6 /K`, `L0 = 2 m`, `dT = 50 K`, `m = 2 kg`,
`cp = 500 J/(kg K)`, `dT = 10 K`.

```text
Case              Expected (hand-computed)   Actual (measured)
alpha dT          6.0e-4                     0.0006, within 1e-12 relative
alpha L0 dT       0.0012 m                   0.0012 m, within 1e-12 relative
  in millimetres  1.2 mm                     1.20000000000000018 mm
                                             (literal 1.19999999999999996, ~2 ULP)
m cp dT           10000 J                    10000.0 J, exactly
```

The millimetre figure is the interesting one and is recorded rather than rounded
away: the SI value agrees closely, and converting it to millimetres multiplies by
1000 and lands about 2 ULP from the decimal literal `1.2`. A relative tolerance of
1e-12 is therefore the right check and an exact comparison would be the wrong one.

Types, proved at compile time:

```text
alpha * dT            -> double   (dimensionless; NOT ThermalExpansionCoefficient)
alpha * L0 * dT       -> Length   (NOT double)
m * cp * dT           -> Energy   (NOT Power)
k * (dT / L) * A      -> Power    (Fourier's shape, without a solver)
```

Explicit failures, all tested:

```text
k = 0                     rejected        cp = 0                   rejected
k < 0                     rejected        cp < 0                   rejected
melting T <= 0 K          rejected        resistivity <= 0         rejected
reference T <= 0 K        rejected        alpha not finite         rejected
NaN (every property)      rejected        +inf / -inf              rejected
alpha < 0                 ACCEPTED -- real engineering data
missing k                 explicit unknown, not zero
missing cp                explicit unknown, not zero
missing density           both consumer paths report it missing
stored Derived state      rejected
```

## DETERMINISM

`ThermalProperty_RelationshipsAreRepeatableToTheBit` computes the strain, the
length change and the heat twenty times and requires **bit-identical** results.
Property presence, values, validation results and enumeration order are all
deterministic: the kinds come from a fixed array, traversed ten times in one test,
and the new code contains no `unordered_map`, hash or pointer ordering. Both
repeat stages ran the whole suite five times over with no failure.

## FULL REGRESSION

Three presets, each from the external build root `%LOCALAPPDATA%\bc-build`,
configured, cleaned, built with warnings as errors, proved fresh by a no-op
rebuild, and only then tested.

```text
preset             configure  clean  build  no-op rebuild  suite       warnings
debug-ext              0        0      0          0        2446/2446      0
release-ext            0        0      0          0        2446/2446      0
debug-shared-ext       0        0      0          0        2446/2446      0

repeat release-ext, [A-Za-z], until-fail:5   exit 0   12230 = 2446 x 5, 0 failures
repeat debug-ext,   [A-Za-z], until-fail:5   exit 0   12230 = 2446 x 5, 0 failures
qualification finished 13:38:31, 0 stage(s) failed
```

2446 = the 2400 of P15-MECH-001 plus the 46 added here, and all three presets
discover the same 2446.

`verify-harness.cmd` was run first and required a non-zero exit from a preset that
does not exist: 3 stages failed, exit 3.

**No replace fault anywhere.** `Permission denied`, `cannot replace` and `Not Run`
appear in none of the logs, across 24460 repeat executions plus three full suites.
No controlled rerun was needed or used.

**Qualified tree = committed tree.** The eight tree IDs recorded before the first
build and after the last test run are identical, and equal to the committed tree:

```text
apps a3528787f4b24097c34ca11961d6c0c642315f0e
include c047624abd2112acc9ac6d317a53c2d2a30f2906
src c625baff94681d6eab58fabbe98d1b0f945d011a
tests 43602d44a8aba29a864d30ef133bb571bc2cf5d2
examples 2e1ef60bb5e67fa3589e1246613261cd746ddce2
cmake 7ed703a3031144d26063a1ff02996e7664e29524
CMakeLists.txt 13823e788ed3975792affa9ee4354ffce9479d80
CMakePresets.json 3229f0f89b236744d7d5c082bed47c28b718288e
```

Qualified on the first attempt. `debug-shared-ext` came through clean, which was
worth checking rather than assuming: `ElectricalResistivity` is a new header-only
class with an exported `toString`, the same shape as the dll-imported constant
that voided P15-MAT-001's first run.

## WARNINGS

0 in all three builds, with `-Werror` and the project's full warning set. Two
warnings were hit and fixed during development, both in test code: a range-based
`for` binding a `const std::string&` to a temporary, and a missing
`UnitCatalog.hpp` include.

## ADVERSARIAL REVIEW

All 23 questions from the brief were answered against the code. Two findings.

**1. This milestone's own change broke four of the previous milestone's
compile-fail assertions.** Adding `referenceTemperature` gave
`MaterialProperty::known()` a second overload, so a misuse stopped producing
"cannot convert" and started producing "no matching function for call to". The
four cases in `MechanicalMisuse.cpp` still failed to compile — correctly — but
their regexes no longer matched, so they reported failure for a reason that was no
longer true.

That is the second time in two milestones that a compile-fail case has gone wrong
in a way that looks like a pass or a spurious failure rather than a real signal.
It is worth naming the pattern: a compile-fail test asserts on a *compiler
message*, which is not part of the API and changes when the API's shape changes
around it. All 19 cases now key on the overload-resolution wording and were
re-verified individually against the actual output.

The total repeat filter is what surfaced it. Nothing about "thermal properties"
would have run `compile_fail.mech.*`.

**2. The new reference-temperature check on mechanical properties had no test.**
`MechanicalProperties::validate()` gained the check, because the field is on the
shared wrapper, and nothing exercised it there — only the thermal side was
covered. `MechanicalProperty_ChecksAReferenceTemperatureOnAMechanicalPropertyToo`
now does, including that the modulus value is untouched by the record.

Answered and already covered:

```text
thermal density diverges from mechanical  HasExactlyOneDensityAndBothConsumerPathsReadIt
unknown k / cp becomes zero               AnUnknownPropertyHasNoValueAndIsNotZero
alpha rejects valid negatives             AcceptsANegativeThermalExpansionCoefficient
NaN becomes a known property              RefusesAnInvalidThermalPropertyAndKeepsWhatItHad
melting temperature treated as dT         AMeltingTemperatureIsAbsoluteAndMustBeAboveAbsoluteZero
dT treated as absolute temperature        TemperatureIsKelvinOnly... (recorded, not prevented)
Celsius offset sneaks in                  TemperatureIsKelvinOnly... (one unit, 1:1)
k confused with stiffness                 compile-fail: pressure-as-conductivity
cp confused with total heat capacity      compile-fail: heat-capacity-as-specific-heat
resistivity confused with mass density    compile-fail: density-as-resistivity
resistivity as an untyped double          compile-fail: both directions
reference temperature becomes k(T)        RecordsTheTemperature...WithoutMakingItALaw
future table has duplicate temperatures   FixesTheInvariantsAFutureTemperatureTableMustHold
future extrapolation is silent            NamesTheOutOfRangeChoicesAndOffersNoDefault
thermal edit changes MaterialId           EditingAThermalPropertyDoesNotChangeItsId
local clone mutates the library           AnImportedMaterialHasNoThermalProperties...
thermal edits erase mechanical data       ThermalAndMechanicalPropertiesCoexist...
fake values needed for missing data       APartiallyCharacterisedMaterialIsValidData
consumer invents missing k / cp           the require* functions all fail
save/load drops thermal data              the io test, refusing loudly
enumeration nondeterministic              EnumeratesItsKindsInAFixedSemanticOrder
Debug/Release disagree on relationships   12230 = 2446 x 5 in both, and
                                          RelationshipsAreRepeatableToTheBit
```

## PERSISTENCE BOUNDARY

Not implemented, and not falsely claimed. A material cannot be saved at all yet
(P15-PERSIST-001), so thermal properties add no new silent-loss path — and the gap
stays loud: the document writer refuses an object type it does not know. A test
saves a document holding a material with `k`, `cp`, a melting temperature and a
reference temperature, requires the save to fail naming the type, requires nothing
to be left behind, and requires every property — reference temperature included —
to still be in the document afterwards.

## KNOWN LIMITATIONS

- **An absolute temperature and a temperature interval are the same type.** The
  relationships need `dT` to be dimensionally a temperature, so they cannot be
  separated without breaking `alpha * dT`. The distinction is kept in validation,
  and the limitation is tested rather than implied.
- **No Celsius, and deliberately no faking it.** One temperature unit, scale 1:1.
  Celsius needs an offset the unit system does not have; a test fails the day one
  is added as a scale.
- **Electrical resistivity has no dimensional checking** (ADR-029). It is
  nominally safe — it cannot be confused with a density or a bare number — and
  dimensionally inert. `sigma = 1 / rho` is not typed and no conductivity is
  offered.
- **The library carries no property values**, so thermal immutability is proved
  structurally rather than by mutating a populated entry. Unchanged from
  P15-MECH-001, and it becomes a live concern when sourced values arrive.
- **A material still cannot be saved.** P15-PERSIST-001. Loud, tested, and now
  covering thermal data and the reference temperature.
- **No temperature-dependent laws.** The contract is fixed; `Table` and
  `AnalyticLaw` are named and not implemented.
- **No per-property provenance beyond the reference temperature.** Source,
  standard, revision and date are P15-PROV-001.
- **Zero conductivity is refused**, so a perfect insulator cannot be modelled as
  an idealisation. No ADR asks for one; if a consumer does, this is the range to
  revisit.
- **A `MaterialProperty` can hold a non-finite value** in a local variable. It
  cannot enter a material, which is where it matters.

## RESULT

```text
TASK:            P15-THERM-001 -- Physical / Thermal Properties
BASELINE:        4940932, clean, HEAD == origin/main
PREREQUISITES:   P15-ARCH-001 16/16, P15-UNITS-001 20/20, P15-MAT-001 18/18,
                 P15-MECH-001 18/18, INFRA-QT-DEPLOY-001 8/8
IMPLEMENTATION:  k, cp, alpha, melting temperature and electrical resistivity on
                 the SAME property wrapper as the mechanical properties; the
                 reference-temperature record; the constant-property contract and
                 the fixed shape of a future temperature-dependent law; per-
                 consumer requirement functions for a thermal solver
ARCHITECTURE:    ADR-029 -- electrical resistivity is a scoped strong type, not a
                 sixth base dimension. Three candidates weighed; the qualified
                 unit system is untouched.
TESTS:           46 new, 2400 -> 2446. 24 thermal model, 10 integration,
                 10 compile-fail, 1 persistence, 1 mechanical reference
                 temperature
VALIDATION:      three relationships against hand-computed decimals, each with its
                 result TYPE proved by static_assert; every rejection path tested
ADVERSARIAL:     23 questions; 2 findings, 2 fixed, 0 remaining. The first was
                 this change breaking four of the previous milestone's
                 compile-fail assertions
DETERMINISM:     12230 = 2446 x 5 in release-ext AND debug-ext, 0 failures;
                 relationships bit-identical over 20 repetitions
REGRESSION:      3 presets from an external build root, 2446/2446 each, 0
                 warnings, fresh binaries, 0 stages failed, first attempt
RESULT:          PASS
EVIDENCE:        docs/verification/P15-THERM-001/
NOT CLAIMED:     that an absolute temperature and a temperature interval are
                 distinguishable by type -- they are not, and validation is what
                 keeps them apart.
                 That electrical resistivity is dimensionally checked -- ADR-029
                 says why it is not.
                 That any library entry carries a property value.
                 That a material can be saved.
TODO:            P15-THERM-001 14 of 14 ticked. P15-ASSIGN-001 NOT started.
```

## REVISION

```text
2026-09-27  Implemented, adversarially reviewed and qualified on the first
            attempt. One architectural decision recorded as ADR-029, because
            ADR-027 had left the electrical-dimension question open and pointed
            at a base-dimension change. Two review findings, both fixed: this
            milestone's new `known()` overload broke four compile-fail
            assertions from P15-MECH-001, which the total repeat filter is what
            surfaced; and the reference-temperature check added to the mechanical
            validator had no test on that side.
```
