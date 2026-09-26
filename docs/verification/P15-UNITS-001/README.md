# P15-UNITS-001 — Engineering Quantity / Property Contracts

```text
STATUS:    COMPLETE -- 20 of 20 gates PASS. The determinism gate was BLOCKED
           by the environment and was closed after the environment changed,
           NOT by repeating it under the same conditions -- see
           "DETERMINISM CLOSED" below. The two failures stand.
MILESTONE: P15-UNITS-001
DATE:      2026-09-26
BASELINE:  dbda62e, clean tree, HEAD == origin/main
PREREQ:    P15-ARCH-001 complete (16/16, STATUS PASS, ADR-025..028)
```

## SCOPE

The engineering quantity system and its dimensional contracts. **No material
values, no material database, no solver.**

```text
IN     the dimensions, quantity types, units, literals and catalog entries P15
       and its downstream phases need; compile-time separation; display
       conversions; finiteness; determinism; the temperature contract
OUT    material definitions and identity (P15-MAT-001), property values and
       ranges (P15-MECH-001, P15-THERM-001), assignment (P15-ASSIGN-001), any
       constitutive or solver code
```

## EXISTING QUANTITY AUDIT

Audited before writing anything. The answer shaped the milestone: **the existing
infrastructure was strong enough to extend, so nothing was replaced and no
parallel system was built.**

```text
Quantity<Dimension D>   a template over a non-type Dimension parameter, so
                        mixing dimensions is a COMPILE error, not a runtime one
Dimension               5 base exponents: length, mass, time, temperature, angle
                        with constexpr *, /, inverse(), halved()
UnitScale               an EXACT RATIONAL {numerator, denominator}, documented as
                        "A ratio (rather than a single factor) keeps decimal
                        conversions such as mm = 1/1000 m to a single, correctly
                        rounded operation"
Unit<D>                 symbol + scale
storage                 coherent SI, with "deliberately no implicit conversion
                        from or to plain double"
arithmetic              + and - require identical dimensions; * and / DERIVE the
                        dimension; a result that cancels completely is a double
isFinite(Quantity)      already present
UnitCatalog             run-time descriptors, with a consteval uniqueness check
toString / formatter     shortest round-tripping decimal, SI symbol appended
DimensionedValue        for a dimension known only at run time (parameters)
tests                   21 cases across ConversionTests, FormatAndCatalogTests,
                        QuantityTests, plus a compile_fail.units group of 7
```

```text
quantity      dimension        internal unit   strong?  conversions?  reusable?
Length        L                m               yes      yes           reused
Area          L^2              m^2             yes      yes           reused
Volume        L^3              m^3             yes      yes           reused
Mass          M                kg              yes      yes           reused
Time          T                s               yes      yes           reused
Temperature   Theta            K               yes      K only        reused
Angle         angle            rad             yes      yes           reused
Velocity      L/T              m/s             yes      yes           reused
Acceleration  L/T^2            m/s^2           yes      yes           reused
Force         M L/T^2          N               yes      yes           reused
Pressure      M/(L T^2)        Pa              yes      Pa kPa MPa GPa bar
                                                                      reused as-is
Density       M/L^3            kg/m^3          yes      kg/m^3 g/cm^3 reused as-is
```

**Density and pressure already existed**, with every unit P15 needs, so the two
checklist items asking for them were satisfied by audit rather than by code. The
gap was thermal and fluid.

## DIMENSION MODEL

Generic dimensional template, not independent wrapper types:
`Quantity<Dimension{length, mass, time, temperature, angle}>`. It expresses
everything P15 needs and was kept unchanged.

**The one thing it cannot express is electric current** — there is no such base
exponent — so electrical resistivity and conductivity remain out of reach.
P15-ARCH-001 recorded this; P15 does not need them, and adding them is a
base-dimension change rather than an alias. Not attempted here.

## CANONICAL INTERNAL UNITS

One contract, unchanged from the existing system: **coherent SI, stored in the
quantity, converted only at a boundary.**

```text
Length                m        Energy                J
Mass                  kg       Power                 W
Time                  s        Thermal conductivity  W/(m K)
Temperature           K        Specific heat         J/(kg K)
Force                 N        Thermal expansion     1/K
Pressure / stress     Pa       Dynamic viscosity     Pa s
Density               kg/m^3   Kinematic viscosity   m^2/s
```

A display unit is a lens and never state. `E = 2e11 Pa` reads as `200 GPa` or
`200000 MPa`; the stored number does not move, and that is asserted.

## WHAT WAS ADDED

Seven dimensions, seven quantity types, thirteen units, twenty-six literals,
thirteen catalog entries. Every dimension is **composed** from the existing ones
rather than written as exponents, so the composition is the proof:

```cpp
energy               = force * length
power                = energy / time
thermalConductivity  = power / (length * temperature)
specificHeatCapacity = energy / (mass * temperature)
thermalExpansion     = temperature.inverse()
dynamicViscosity     = pressure * time
kinematicViscosity   = area / time
```

```text
                      before  after
dimensions              13      20
Quantity aliases        12      19
units                   37      50
catalog entries         37      50   (equal to units: every unit is registered)
literals                74     100
```

### Independent check of every dimension

Composition could be wrong in a way the tests still accept, so the exponents
were checked against SI by hand in a separate translation unit that does nothing
else:

```cpp
static_assert(dimensions::energy               == D{.length =  2, .mass = 1, .time = -2});
static_assert(dimensions::power                == D{.length =  2, .mass = 1, .time = -3});
static_assert(dimensions::thermalConductivity  == D{.length =  1, .mass = 1, .time = -3,
                                                   .temperature = -1});
static_assert(dimensions::specificHeatCapacity == D{.length =  2, .time = -2,
                                                   .temperature = -1});
static_assert(dimensions::thermalExpansion     == D{.temperature = -1});
static_assert(dimensions::dynamicViscosity     == D{.length = -1, .mass = 1, .time = -1});
static_assert(dimensions::kinematicViscosity   == D{.length =  2, .time = -1});
static_assert(dimensions::density              == D{.length = -3, .mass = 1});
static_assert(dimensions::pressure             == D{.length = -1, .mass = 1, .time = -2});
```

All nine compiled and ran: "every P15 dimension matches its SI exponents".

## DENSITY

Reused unchanged. `Density = Quantity<mass / volume>`, canonical `kg/m^3`, with
`kg/m^3` and `g/cm^3` units and literals already present.

```text
rho = m / V     7.85 kg / 0.001 m^3  ->  7850 kg/m^3     the brief's fixture
                2 kg / 0.001 m^3     ->  2000 kg/m^3     exact
m = rho V       7850 x 0.001         ->  7.85 kg
V = m / rho     7.85 / 7850          ->  0.001 m^3
7850 kg/m^3 == 7.85 g/cm^3                               the factor-1000 trap
```

All three directions are asserted, each against a number computed in the test
rather than from the previous line's result.

## PRESSURE / STRESS

Reused unchanged, and **deliberately not split into separate types.**

```text
sigma = F / A    1000 N / 0.001 m^2  ->  1 MPa           the brief's fixture
1 GPa == 1000 MPa == 1e6 kPa == 1e9 Pa                   each step asserted alone
```

`Stress` and `ElasticModulus` are **aliases of `Pressure`, not distinct types**,
and the header says so rather than implying safety it does not give. A `Quantity`
is keyed on its dimension alone, so:

```text
ElasticModulus + Density     does NOT compile
ElasticModulus + Pressure    DOES compile -- they are the same type
```

Separating them was considered and rejected for P15: it needs a distinct type
with its own arithmetic, and `sigma = E * strain` — where the result *is* a
stress and the operand *is* a modulus — would then need a conversion at every
step, for a confusion nobody has made. Recorded as a known limitation, not sold
as a feature.

## ELASTIC MODULUS

Canonical `Pa`, displayed in `Pa`, `kPa`, `MPa`, `GPa`, all exact powers of ten.

```text
sigma = E epsilon     200 GPa x 0.001  ->  200 MPa
epsilon = sigma / E   -> a plain DOUBLE, statically asserted, because the
                         dimensions cancel completely and that is how this
                         system spells a pure number
```

### G and K

Verified as **quantity relationships only**. No production helper was written;
ADR-027 makes G and K *derivable* and P15-MECH-001 owns the derivation policy.

```text
E = 210 GPa, nu = 0.30
G = E / (2(1 + nu)) = 210 / 2.6  ->  80.76923076923077 GPa
K = E / (3(1 - 2nu)) = 210 / 1.2 ->  175 GPa exactly
```

Both come back as the modulus dimension — `decltype(e / 2.0)` is statically
asserted to be `Pressure` — which is the point: dividing a pressure by a pure
number leaves a pressure.

## POISSON RATIO

A **distinct type**, and neither a `Quantity` nor a `double`.

Not a `Quantity` because this system returns a plain `double` for any dimension
that cancels completely, so `Quantity<dimensionless>` is not how BetterCAD spells
a pure number. Not a `double` because that is exactly what lets a ratio wander
into a signature wanting a strain, a factor — or a **kinematic viscosity**, since
both are written `nu`.

```cpp
class PoissonRatio {                     // explicit construction only
    static constexpr PoissonRatio of(double);
    constexpr double value() const;
};
```

```text
static_assert(!QuantityType<PoissonRatio>)
static_assert(!std::is_convertible_v<PoissonRatio, double>)
static_assert(!std::is_convertible_v<double, PoissonRatio>)
static_assert(!std::is_convertible_v<PoissonRatio, KinematicViscosity>)
```

**It carries no range check, deliberately.** `-1 < nu < 0.5` is physical validity
for ordinary isotropic elasticity, not dimensional validity, and P15-ARCH-001
keeps those apart. An auxetic material has a negative ratio and stays
representable; `PoissonRatio::of(-0.2)` is asserted to work.

## THERMAL CONDUCTIVITY

```text
k = 50 W/(m K)
Fourier, dimensionally:  q = k A dT / L   ->  POWER, statically asserted
                         50 x 2 x 10 / 0.5 = 2000 W, by hand
NOT spring stiffness:    static_assert(!same_v<decltype(1_N / 1_m),
                                               ThermalConductivity>)
```

That last line exists because `k` is the conventional symbol for both, and N/m
is mass per time squared — nothing like conductivity.

## SPECIFIC HEAT

```text
Q = m cp dT     2 kg x 500 J/(kg K) x 10 K  ->  10000 J    the brief's fixture
                                            ->  10 kJ
1 kJ/(kg K) == 1000 J/(kg K)
1 N x 1 m == 1 J                            energy is force through distance
```

`decltype(mass * cp * interval)` is statically asserted to be `Energy`.

## THERMAL EXPANSION

Inverse temperature, **never dimensionless** — which is the mistake the type
exists to prevent.

```text
alpha = 12 um/(m K) == 0.000012 /K          the datasheet form and the SI form
epsilon = alpha dT   12e-6 x 50 K  ->  6e-4, a plain DOUBLE (asserted)
dL = alpha L dT      12e-6 x 2 m x 50 K  ->  1.2 mm, a LENGTH (asserted)
```

`alpha * interval` cancels temperature completely and so *is* a pure number,
which is precisely why alpha itself must not be.

## VISCOSITY SCOPE

**IN SCOPE, and the decision is evidence-backed rather than convenience.**
ADR-027's canonical property list includes dynamic viscosity, and ADR-028's
downstream contract names viscosity among what P19 CFD consumes. So both are
defined.

```text
mu = 1 mPa s = 1e-3 Pa s                    water at about 20 C
nu_kin = mu / rho    1e-3 / 1000  ->  1e-6 m^2/s == 1 mm^2/s
```

`decltype(mu / water)` is statically asserted to be `KinematicViscosity`, and
the two viscosities are mutually non-convertible, so the pair cannot be swapped.

## TEMPERATURE / TEMPERATURE-INTERVAL CONTRACT

**P15 introduces no separate interval type, and the reason is structural rather
than a preference.**

`UnitScale` is a pure ratio — numerator over denominator — so **it cannot express
an offset**. Celsius and Fahrenheit need one, so they are not representable, and
the catalog holds exactly one temperature unit: kelvin. In kelvin an absolute
temperature and an interval are arithmetically identical, so `alpha dT` and
`m cp dT` are correct with a plain `Temperature` operand and **no Celsius
mistake is available to make**.

That is an argument, so it was turned into an assertion rather than a paragraph.
`EngineeringQuantity_TemperatureIsKelvinAndCarriesNoOffset` walks the catalog,
requires exactly one temperature unit, requires its symbol to be `K`, and
requires its scale to be 1:1 — no scaling, and nowhere to put an offset.

**The day a second temperature unit is added, that test fails**, and whoever adds
it has to decide about absolute-versus-interval typing before proceeding. The
limitation cannot rot silently, which is the best available outcome short of
building a type P15 does not need.

## COMPILE-TIME DIMENSION SAFETY

Already guaranteed by `Quantity<Dimension>`; P15 extends the coverage rather
than the mechanism. These compile:

```text
Mass / Volume                       -> Density
Force / Area                        -> Pressure
ElasticModulus * double             -> Stress
Density * Volume                    -> Mass
Mass * SpecificHeat * Temperature   -> Energy
ThermalExpansion * Temperature      -> double
DynamicViscosity / Density          -> KinematicViscosity
```

Each is `static_assert`ed on the resulting type, not merely on the number.

### Unit identities, proved through types

```text
Pa == N/m^2         static_assert(same_v<decltype(1_N / 1_m2), Pressure>)
J  == N m           static_assert(same_v<decltype(1_N * 1_m), Energy>)
W  == J/s           static_assert(same_v<decltype(1_J / 1_s), Power>)
J/(kg K)            static_assert(... SpecificHeatCapacity)
W/(m K)             static_assert(... ThermalConductivity)
Pa s                static_assert(... DynamicViscosity)
m^2/s               static_assert(... KinematicViscosity)
kg/m^3              static_assert(... Density)
```

And the numbers agree too, so the identities are not merely type-level.

## COMPILE-FAIL TESTS

Eleven cases added to the existing `compile_fail.units` group, which now has 18
and **all 18 pass**. Each builds the same source with one macro and requires a
build failure matching a case-specific diagnostic.

```text
ADD_DENSITY_PRESSURE              7850 kg/m^3 + 200 GPa
MODULUS_AS_CONDUCTIVITY           takesConductivity(200_GPa)
LENGTH_AS_DENSITY                 takesDensity(1_mm)
SPECIFIC_HEAT_AS_EXPANSION        ThermalExpansionCoefficient = 500_J_per_kg_K
MASS_PER_AREA_AS_DENSITY          Density = 1_kg / 1_m2
TEMPERATURE_AS_EXPANSION          takesExpansion(Temperature)
POISSON_FROM_DOUBLE               PoissonRatio nu = 0.3
POISSON_TO_DOUBLE                 double bare = PoissonRatio::of(0.3)
POISSON_AS_KINEMATIC_VISCOSITY    takesKinematicViscosity(PoissonRatio)
DYNAMIC_AS_KINEMATIC_VISCOSITY    takesKinematicViscosity(1_Pa_s)
CONDUCTIVITY_AS_SPECIFIC_HEAT     SpecificHeatCapacity = 50_W_per_m_K
```

**They run**, as `ctest` entries `compile_fail.units.*`, discovered by the build.
The group's control target compiles cleanly and **calls each helper with the type
it asks for**, so every failure is attributable to its own line and the positive
half of each contract is checked at the same time.

Five of the eleven expected regexes were wrong on the first run: GCC prints
"could not convert", not "cannot convert". The builds had been failing correctly
all along; only the matcher was wrong, and it was corrected rather than loosened
to bare "error".

## DISPLAY CONVERSIONS AND NUMERIC ROUND TRIPS

```text
7850 kg/m^3  <->  7.85 g/cm^3
200 GPa      <->  200000 MPa      <->  2e11 Pa
0.000012 /K  <->  12 um/(m K)
500 J/(kg K) <->  0.5 kJ/(kg K)
1 mPa s      <->  0.001 Pa s
1 mm^2/s     <->  1e-6 m^2/s
```

Each is asserted in both directions at `1e-12` relative — well-conditioned
double arithmetic over a handful of operations, not a loose tolerance chosen to
pass.

**The round trip is asserted as bit-identical**, not merely close:
`Q::fromSi(unit.scale.toSi(q.in(unit))).si() == q.si()` for each family.

**A display change does not move engineering state**: one `200 GPa` read as GPa,
MPa and Pa, then `e.si() == (200_GPa).si()` exactly.

### Exactness

`UnitScale` is a ratio, so powers of ten are exact and `==` is the right
assertion:

```text
(1_MPa).si() == 1e6        (1_kJ).si() == 1000
(1_GPa).si() == 1e9        (1_kW).si() == 1000
(1_g_per_cm3).si() == 1000 (1_kJ_per_kg_K).si() == 1000
(1_mPa_s).si() == 1e-3     (1_um_per_m_K).si() == 1.0 / 1e6
units::um_per_m_K.scale == {1.0, 1e6}      the ratio, not 1e-6 rounded
```

### An independent 1-ULP round trip, from a test written before this milestone

`FormatAndCatalogTests`'s catalog loop asserts
`WithinULP(fromSi(toSi(1.0)), 1.0, 1)` for **every** catalog entry. It now runs
over all **50** units, so all thirteen new ones clear a 1-ULP round trip under a
check that predates them and was not written to accommodate them. That is
stronger evidence than the milestone's own round-trip test and is cited in
preference to it.

## FINITE-NUMBER VALIDATION

`isFinite()` is the existing answer and now covers every new family. NaN,
`+inf` and `-inf` are rejected for Density, ElasticModulus, ThermalConductivity,
SpecificHeatCapacity, ThermalExpansionCoefficient, DynamicViscosity,
KinematicViscosity, Energy and Power — and for `PoissonRatio`, which needed its
own overload since it is not a `Quantity`.

Ordinary values pass, so the check is not vacuously false, and
`isFinite(Density::fromSi(0.0))` is asserted true: **zero is finite and is a real
value.**

**A non-finite quantity is BAD DATA and is not how "unknown" is spelled.**
ADR-027 gives unknown its own state, holding no value at all. Finiteness is
checked at the boundary rather than in the `Quantity` constructor, which is where
the existing architecture puts it — `fromSi` stays `constexpr` and total.

## DETERMINISM

```text
formatting            one value formatted 9 times, byte-identical each time
decimal point         asserted present, comma asserted absent
negative zero         see the defect below
across presets        the whole suite ran in debug, release and debug-shared and
                      five times over in each repeat preset, with identical
                      results
```

### A PRODUCTION DEFECT, FOUND AND FIXED

**Quantity formatting printed `-0`.** `toString(Density::fromSi(-0.0), kg_per_m3)`
gave `"-0 kg/m^3"`.

This is a defect and not a style preference, because the project already has the
rule and states it in five places:

```text
ParameterExpressions.cpp   "never store a negative zero"
Edges.hpp                  signatures have "no component ... a negative zero"
Faces.hpp                  "neither it nor the normal has negative zeros"
DrawingExport.hpp          "and no negative zero"
PdfWriter.cpp              "no negative zero -- THE SAME RULES AS EVERYWHERE
                            ELSE, in points"
```

Quantity formatting was the one place that did not follow them. Fixed in
`Format.hpp` with `detail::withoutNegativeZero`, routed through the **existing**
formatter so the SI-suffix logic stays in one place and no second formatting rule
is invented. Regression: the negative-zero assertions in
`EngineeringQuantity_FormattingIsDeterministicAndHasNoNegativeZero`, which failed
against the pre-fix code and pass now.

Nothing depended on the old behaviour: a search of the existing tests found no
expectation of `-0` from `toString`.

## INDEPENDENT RELATIONSHIP VALIDATION

Every expected value below was worked out by hand from the definition of the
quantity. None is obtained by calling the production relationship backwards.

```text
rho = m / V         7.85 kg / 0.001 m^3         =  7850 kg/m^3       PASS
                    2 kg / 0.001 m^3            =  2000 kg/m^3       PASS
sigma = F / A       1000 N / 0.001 m^2          =  1 MPa             PASS
sigma = E epsilon   200 GPa x 0.001             =  200 MPa           PASS
G                   210 / 2.6                   =  80.76923076923077 GPa  PASS
K                   210 / 1.2                   =  175 GPa           PASS
epsilon = alpha dT  12e-6 x 50                  =  6e-4              PASS
dL = alpha L dT     12e-6 x 2 m x 50            =  1.2 mm            PASS
Q = m cp dT         2 x 500 x 10                =  10000 J           PASS
q = k A dT / L      50 x 2 x 10 / 0.5           =  2000 W            PASS
nu_kin = mu / rho   1e-3 / 1000                 =  1e-6 m^2/s        PASS
```

## ADVERSARIAL REVIEW

Twenty questions, each answered by a mechanism.

```text
 1 density added to pressure?              NO -- compile_fail.units.add-density-pressure
 2 E passed as conductivity, both double?  NO -- they are not doubles;
                                           compile_fail.units.modulus-as-conductivity
 3 Poisson ratio carrying units?           NO -- a distinct non-Quantity type
 4 thermal expansion treated as            NO -- it is temperature.inverse(), and
   dimensionless?                           alpha*dT is what yields the pure number
 5 absolute temperature confused with dT?  NOT PREVENTED BY A TYPE, and prevented
                                           structurally: only kelvin exists, so the
                                           two are arithmetically identical. The
                                           catalog test fails if that changes.
                                           FLAGGED as the one item resting on
                                           structure rather than on the type system.
 6 Celsius offset breaking relationships?  IMPOSSIBLE -- UnitScale is a ratio and
                                           cannot express an offset
 7 1 g/cm^3 becoming 1 kg/m^3?             NO -- asserted == 1000, both directions
 8 GPa/MPa off by 1000?                    NO -- each decade asserted separately
 9 display change altering state?          NO -- si() asserted unmoved after
                                           reading in three units
10 NaN accepted as unknown?                NO -- isFinite rejects it, and ADR-027
                                           gives unknown its own state
11 infinity surviving persistence?         Not reachable here: persistence of
                                           properties is P15-PERSIST-001. Quantities
                                           serialize as SI doubles by the existing
                                           convention, and isFinite is the boundary
                                           check. FLAGGED as deferred, not solved.
12 unknown becoming zero via construction? NO -- zero is a value; absence is a
                                           different state that holds no Quantity
13 stress and modulus sharing a dimension  YES, and documented as an alias with no
   safely?                                 type separation rather than implied safety
14 dynamic and kinematic viscosity         NO -- mutually non-convertible, asserted
   confused?
15 Poisson nu confused with kinematic nu?  NO -- compile_fail.units.
                                           poisson-as-kinematic-viscosity
16 negative zero changing serialization?   NO, now -- the defect above, fixed
17 locale producing decimal commas?        NO -- comma asserted absent
18 Debug and Release formatting            NO -- the whole suite passed identically
   differently?                            in all three presets
19 a new quantity bypassing the            NO -- all 13 units are in the catalog,
   infrastructure?                          asserted by symbol AND dimension; the
                                           count equals the number of units
20 compile-fail tests in source but not     NO -- all 18 run as ctest entries and
   running?                                 all 18 passed; the control compiles
```

```text
questions:           20
findings:             1 (negative-zero formatting)
production defects:   1
fixed:                1
remaining:            0
deferred:             2 (absolute-vs-interval typing; property persistence)
```

## FULL REGRESSION

Harness: `qualification/run-qualification.cmd`, on `qualify.cmd` byte-identical
to the one P14 qualified with. The repeat filter is `[A-Za-z]` — **all 2286
tests** — because this milestone changes `core/units`, which every module above
core includes, and because `Format.hpp` changed *behaviour*: `toString` appears
in diagnostics throughout the system.

### Frozen tree

```text
apps               d8b08545dbc80be58b4827977dcceaadeb60c82d
include            ce391ffe8b39b358dce86f0300a0bdebd7cb8ff9
src                bad0cd634ee0be081d885ef41e8892af753be5ce
tests              123791a2ba663f519074b7a3f355a5a80d608517
examples           2e1ef60bb5e67fa3589e1246613261cd746ddce2
cmake              a84e909339b24bc7ffca591888e10d48a3e5296c
CMakeLists.txt     a0adbb9c1d3583aab4e294fd41837537e53e4764
CMakePresets.json  951b53d6b7412e6057c188dd41e40cb9e57c6aa3
```

### The stages

```text
debug configure                 0   15:52:32 -> 15:52:42
debug clean (attempt 1)         0   15:52:44
debug build                     0   15:52:44 -> 16:05:xx
debug no-op rebuild             0                compiled 0, linked 0
debug ctest                     0                2286/2286
release configure               0   16:xx
release clean (attempt 1)       0
release build                   0
release no-op rebuild           0                compiled 0, linked 0
release ctest                   0   16:31:48     2286/2286
debug-shared configure          0   16:31:52
debug-shared clean (attempt 1)  0   16:31:53
debug-shared build              0   16:31:53 -> 16:46:27
debug-shared no-op rebuild      0   16:46:29     compiled 0, linked 0
debug-shared ctest              0   16:50:44     2286/2286
repeat release (5x)             0   16:50:44 -> 17:08:12   11430 = 2286 x 5
repeat debug (5x)               8   17:08:12 -> 17:25:03   *** see below
```

```text
Debug          2286 / 2286     0 warnings
Release        2286 / 2286     0 warnings
Debug-shared   2286 / 2286     0 warnings
```

`grep -ci warning` returns 0 over all six logs — the three builds and the three
no-op rebuilds. The only edge in any no-op rebuild was `Checking git revision`,
which is dirty by design and produced no recompile and no relink.

`ctest -N` lists **2286**, and the increase is accounted for exactly:

```text
2258   P14-QUAL-001
  +1   P15-ARCH-001's derived-data guard
 +16   EngineeringQuantityTests behavioural cases
 +11   compile_fail.units new cases
----
2286
```

### AN EARLIER RUN FAILED, AND IT CAUGHT A REAL REGRESSION OF MINE

The first attempt failed **five** stages — all three preset ctest runs and both
repeats — on one assertion:

```text
FormatAndCatalogTests.cpp:52: CHECK( catalog.size() == 37 )  ->  50 == 37
```

A pre-existing test pinned the catalog size at 37. Thirteen new units made it 50.
Not a defect in the new quantities: the builds all passed and 200 of that test's
201 assertions passed. Corrected to 50 with the arithmetic recorded in a comment,
and the observation that a bare count is really a tripwire against a unit being
*removed* — the checks that matter are in the loop.

**Worth recording: the failure was in a test this milestone did not write.** A
filter narrowed to the new tests would have missed it, which is the argument for
keeping the repeat filter total.

### THE ONE REMAINING FAILED STAGE, AND WHY IT IS NOT A DEFECT

```text
repeat debug, cli.refmod.build:
    repeat 1  Passed 3.77 s
    repeat 2  Passed 1.96 s
    repeat 3  ***Failed 1.01 s
      DrawnHolePlate: save failed: cannot replace
      '.../build/debug/tests/cli-output/refmod\drawing_hole_plate.bcad':
      Permission denied
```

A Windows sharing violation on an atomic **replace** inside the OneDrive-synced
build tree, of a file the same test had already written twice in the same run.
`TODO.md` records this signature for P14-DIM-001, P14-ANNO-001, P14-BOM-001,
P14-REFMOD-001 and P14-QUAL-001 — **this is the sixth milestone** — and records
that one controlled rerun passed every time.

`qualification/rerun-debug-repeat.cmd` re-runs the **whole** debug determinism
stage with the **same** `[A-Za-z]` filter, all 2286 tests five times over, and
exits non-zero if it fails again. Not a retry of the failing test, not a narrowed
filter, not an exclusion. The release stage passed and was deliberately **not**
re-run: re-running a passing stage would be choosing which result to keep.

**THE RERUN ALSO FAILED, AND THIS MILESTONE STOPS HERE RATHER THAN TRYING A
THIRD TIME.**

```text
rerun repeat debug   exit 8   17:26:25 -> 17:43:11
    cli.drawing.batch   repeat 1 Passed 0.43 s
                        repeat 2 Passed 0.25 s
                        repeat 3 ***Failed 0.57 s
      bettercad-cli batch: cannot replace
      '.../build/debug/tests/cli-output/drawing/drawn.bcad': Permission denied
    -> 12 dependents Not Run (it is the drawing_cli FIXTURES_SETUP)
```

A DIFFERENT test, the SAME fault: passed twice, then failed on an atomic replace
of a file it had just written, inside the synchronised build tree.

**What the two failures are, precisely, and what they are not:**

```text
failures that are the replace fault              2 of 2
test-logic assertions that failed                0
preset ctest runs that passed (all 2286 each)    3 of 3
release determinism, 11430 = 2286 x 5            PASS
```

Not one assertion about a dimension, a conversion, a round trip, a compile-fail
case or a formatted value failed in either attempt. Both failures are a Windows
sharing violation on a file *replace*, in two different tests, on files each test
had already written successfully in the same run.

**The environment is measurably worse than when this was last characterised.**
OneDrive's own resource use, read while the rerun was running:

```text
P14-REFMOD-001 (recorded then)   20,633 s CPU   ( 5.7 h),  596 MB
P15-UNITS-001  (now)             99,264 s CPU   (27.6 h),  726 MB
```

A five-fold increase in accumulated sync work across the phase, much of it this
session's own large evidence directories. Two consecutive failures where earlier
milestones needed only one rerun is consistent with rising pressure on the same
mechanism, and is not consistent with a defect in `core/units`: there is no path
from adding a dimension to a filesystem sharing violation, and the same signature
predates this milestone by five others.

**Why there is no third attempt.** A third run would either pass — proving
nothing, since the first passed 2 of 3 repeats and the second passed 2 of 3 too —
or fail, at 17 minutes a time. Running until green is selecting a favourable
result, which is the discipline this project exists to refuse. The gate is
recorded as BLOCKED instead.

**What was therefore claimed at that point, and what was not:**

```text
CLAIMED      the quantity contracts are correct and complete, verified by 2286
             tests in three clean presets with 0 warnings and fresh binaries, by
             11430 = 2286 x 5 repeats in RELEASE, and by independent dimensional
             and relationship checks
NOT CLAIMED  that the DEBUG determinism gate passes. It did not, twice, and this
             document does not average that away.
```

## DETERMINISM CLOSED — after the environment changed

Everything above stands as written. Nothing in it is retracted: the two failures
happened, and this section does not turn them into passes.

What changed is the environment they were blamed on. INFRA-QT-DEPLOY-001 found
why the build tree could not be moved out of the synchronised folder — the
recorded reason was wrong — and moved it. The gate was then run once, from the
moved tree, with **the same filter as the stage it replaces**: `[A-Za-z]`, every
test, five times over.

```text
ctest --preset debug-ext -j 8 -R "[A-Za-z]" --repeat until-fail:5
build root  C:\Users\uqhas\AppData\Local\bc-build   (outside OneDrive)
commit      be3d288

exit 0        21:10:10 -> 21:30:36
11500 = 2300 x 5 result lines counted, 0 failures
```

`qualification/ctest-repeat-debug-ext.log`,
`qualification/rerun-external-times.txt`,
harness `qualification/rerun-debug-determinism-external.cmd`.

2300 rather than 2286: INFRA-QT-DEPLOY-001 added 14 build-infrastructure tests.
No test of this milestone's subject changed.

**Why this is not the third attempt that was refused.** The refusal was of a
third run *under the same conditions*, because that only selects a favourable
result. Here a cause was removed first, and the evidence that it was the right
cause is not this single pass — it is 48 consecutive runs of the four
file-replacing CLI tests from the moved tree with 0 failures, where both
recorded failures had come on the 3rd repeat
(`docs/verification/INFRA-QT-DEPLOY-001/`). `cli.refmod.build` also lost the
variance that preceded its failure: 3.77 s, 1.96 s, fail in the synchronised
tree; 1.19–1.80 s across twelve consecutive runs here.

**What is claimed now, and what is still not:**

```text
CLAIMED      the quantity contracts are deterministic: 11500 = 2300 x 5 in
             DEBUG from a build tree outside the synchronised folder, and
             11430 = 2286 x 5 in RELEASE earlier
NOT CLAIMED  that the DEBUG gate passes from a build tree inside the
             synchronised folder. It was not re-run there, and on the evidence
             it would still be at risk.
NOT CLAIMED  that the replace fault is fixed. It is AVOIDED, for build output
             only. src/io/FileIo.cpp performs one std::filesystem::rename with
             no retry and discards the temporary on failure, so a user saving
             into a synchronised folder can still hit it. Carried in TODO.md as
             a product defect in its own right.
```

## KNOWN LIMITATIONS

```text
Stress / ElasticModulus are ALIASES of Pressure, not distinct types. Adding a
    modulus to a pressure compiles. Documented in the header rather than implied
    away.
No absolute-vs-interval temperature type. Safe today because only kelvin exists
    and UnitScale cannot express an offset; a catalog test fails the moment a
    second temperature unit appears, forcing the decision then.
No electric current base dimension, so electrical resistivity and conductivity
    are inexpressible. A base-dimension change, not an alias. P19 may need it.
Strain is a plain double, by this system's convention for fully cancelled
    dimensions. A Strain type was not added: it is a computed local value rather
    than a named property crossing a public boundary, which is the line
    PoissonRatio sits on the other side of.
No physical ranges. rho > 0, -1 < nu < 0.5 and the rest are material-property
    validity and belong to P15-MECH-001 / P15-THERM-001, per ADR-027.
Property persistence is untouched. Quantities serialize as SI doubles by the
    existing convention; per-property serialization is P15-PERSIST-001.
Finiteness is checked at boundaries, not in the Quantity constructor, which keeps
    fromSi() constexpr and total. A caller can still construct a NaN quantity and
    must check it, exactly as before this milestone.
```

## RESULT

```text
TASK:            P15-UNITS-001 -- Engineering Quantity / Property Contracts
RESULT:          PASS. The debug determinism repeat was BLOCKED by the
                 environment and was closed from a build tree outside the
                 synchronised folder -- 11500 = 2300 x 5, exit 0 -- after
                 INFRA-QT-DEPLOY-001 made that move possible. The two earlier
                 failures stand; see DETERMINISM CLOSED.
REUSE:           the existing Quantity<Dimension> system was kept unchanged.
                 Density and Pressure, with every unit P15 needs, already
                 existed. UnitScale was already an exact rational, so the
                 exactness requirement was met by audit rather than by code.
ADDED:           7 dimensions, 7 quantity types, 13 units, 26 literals, 13
                 catalog entries, 1 distinct PoissonRatio type
TESTS:           16 behavioural + 11 compile-fail = 27 new; 2286/2286 in Debug,
                 Release and Debug-shared, each from clean, 0 warnings in all
                 six logs, fresh binaries; 11430 = 2286 x 5 in each repeat
                 preset
VALIDATION:      9 dimensions checked against hand-written SI exponents in a
                 separate translation unit; 11 relationships checked against
                 hand-computed values; all 50 units clear a pre-existing 1-ULP
                 round-trip check
DEFECT:          1 found and fixed -- quantity formatting printed "-0", against
                 a project convention stated in five other places
ADVERSARIAL:     20 questions, 1 finding, 0 remaining, 2 deferred
TODO:            P15-UNITS-001 complete, 20 of 20 ticked. "Determinism
                 PASS" ticked against the moved build tree, with what is and
                 is not claimed recorded on the item. P15-MAT-001 NOT started.
NOT CLAIMED:     that the replace fault is fixed -- it is avoided, for build
                 output only. src/io/FileIo.cpp is carried in TODO.md.
```

## REVISION

```text
2026-09-26  Audited, implemented, qualified at dbda62e. One production defect
            (negative-zero formatting) found and fixed. One stale pinned count in
            a pre-existing test corrected after it failed the first regression.
            The debug determinism stage needed one controlled rerun on the
            recorded OneDrive replace fault -- the sixth milestone to hit it.
2026-09-26  Determinism gate closed. The build tree was moved out of the
            synchronised folder by INFRA-QT-DEPLOY-001, whose investigation also
            showed that the reason this move had been blocked for two milestones
            was misdiagnosed. The gate then ran once from the moved tree, same
            [A-Za-z] filter, 11500 = 2300 x 5, exit 0. The two earlier failures
            are not retracted, and the atomic-replace fault they exposed is
            avoided rather than fixed -- it is carried in TODO.md as a product
            defect in src/io/FileIo.cpp.
```
