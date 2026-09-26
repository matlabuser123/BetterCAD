// Engineering quantity contracts (P15-UNITS-001).
//
// Every expected number here is worked out by hand from the definition of the
// quantity, never by calling the production relationship backwards. Where the
// brief gave a fixture, its value is used verbatim.
#include <bettercad/core/units/Format.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/core/units/UnitCatalog.hpp>
#include <bettercad/core/units/Units.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cmath>
#include <limits>
#include <string>

using namespace bettercad;
using namespace bettercad::literals;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

/// Well-conditioned double arithmetic over a handful of operations.
constexpr double kRel = 1e-12;

} // namespace

TEST_CASE("EngineeringQuantity_DensityIsMassOverVolume", "[units][p15][density]") {
    // The brief's fixture: 7.85 kg in 0.001 m^3 is 7850 kg/m^3.
    const Mass mass = 7.85_kg;
    const Volume volume = 0.001_m3;
    const Density density = mass / volume;
    CHECK_THAT(density.in(units::kg_per_m3), WithinRel(7850.0, kRel));

    // And its second fixture, chosen to be exact: 2 kg / 0.001 m^3 = 2000.
    CHECK_THAT((2_kg / 0.001_m3).in(units::kg_per_m3), WithinRel(2000.0, kRel));

    // The relationship runs all three ways, and each direction lands on a
    // number computed here rather than on the previous line's result.
    CHECK_THAT((density * volume).in(units::kg), WithinRel(7.85, kRel));
    CHECK_THAT((mass / density).in(units::m3), WithinRel(0.001, kRel));

    // Steel at 7850 kg/m^3 is 7.85 g/cm^3 -- a factor of exactly 1000, which is
    // the conversion most likely to be wrong by three orders of magnitude.
    CHECK_THAT(density.in(units::g_per_cm3), WithinRel(7.85, kRel));
    CHECK(7.85_g_per_cm3 == 7850_kg_per_m3);
}

TEST_CASE("EngineeringQuantity_PressureIsForceOverArea", "[units][p15][stress]") {
    // The brief's fixture: 1000 N over 0.001 m^2 is 1 MPa.
    const Pressure stress = 1000_N / 0.001_m2;
    CHECK_THAT(stress.in(units::MPa), WithinRel(1.0, kRel));
    CHECK_THAT(stress.in(units::Pa), WithinRel(1.0e6, kRel));

    // The decade ladder, exactly. Each is a separate assertion because a single
    // chain would hide which step was wrong.
    CHECK(1_GPa == 1000_MPa);
    CHECK(1_MPa == 1000_kPa);
    CHECK(1_kPa == 1000_Pa);
    CHECK_THAT((1_GPa).in(units::Pa), WithinRel(1.0e9, kRel));
    CHECK_THAT((1_MPa).in(units::Pa), WithinRel(1.0e6, kRel));

    // Stress, pressure and elastic modulus are ONE type, and that is the
    // documented contract rather than an accident -- so this compiles, and the
    // alias buys readability only.
    const ElasticModulus modulus = stress;
    const Stress asStress = modulus;
    CHECK(asStress == stress);
}

TEST_CASE("EngineeringQuantity_StressIsModulusTimesStrain", "[units][p15][stress]") {
    // sigma = E epsilon. The brief's fixture, read the other way: sigma = 200 MPa
    // at epsilon = 0.001 means E = 200 GPa, so E * epsilon must return the
    // 200 MPa it started from -- and 200 GPa x 0.001 = 200 MPa is hand arithmetic.
    const ElasticModulus modulus = 200_GPa;
    constexpr double strain = 0.001; // dimensionless, as this system spells it
    const Stress stress = modulus * strain;
    CHECK_THAT(stress.in(units::MPa), WithinRel(200.0, kRel));

    // Strain recovered from stress and modulus is a PURE NUMBER, because the
    // dimensions cancel completely -- that is this system's convention, not a
    // special case for strain.
    const double recovered = stress / modulus;
    CHECK_THAT(recovered, WithinRel(strain, kRel));
    static_assert(std::is_same_v<decltype(stress / modulus), double>);
}

TEST_CASE("EngineeringQuantity_PoissonRatioIsDimensionlessAndItsOwnType", "[units][p15][poisson]") {
    const PoissonRatio nu = PoissonRatio::of(0.3);
    CHECK_THAT(nu.value(), WithinRel(0.3, kRel));

    // It is NOT a Quantity, because a dimension that cancels completely is a
    // plain double in this system; and it is NOT a double either, so it cannot
    // wander into a signature that wanted a factor, a strain, or the other nu.
    static_assert(!QuantityType<PoissonRatio>);
    static_assert(!std::is_convertible_v<PoissonRatio, double>);
    static_assert(!std::is_convertible_v<double, PoissonRatio>);
    static_assert(!std::is_convertible_v<PoissonRatio, KinematicViscosity>);

    // Comparable and ordered, which is what a ratio needs.
    CHECK(PoissonRatio::of(0.3) == PoissonRatio::of(0.3));
    CHECK(PoissonRatio::of(0.28) < PoissonRatio::of(0.33));

    // It carries NO range check, deliberately: -1 < nu < 0.5 is physical
    // validity and belongs to P15-MECH-001, not to dimensional safety. An
    // auxetic material has a negative ratio and must be representable.
    CHECK_THAT(PoissonRatio::of(-0.2).value(), WithinRel(-0.2, kRel));
}

TEST_CASE("EngineeringQuantity_ShearAndBulkModuliKeepTheModulusDimension", "[units][p15][stress]") {
    // The brief's fixtures, both computed by hand:
    //   E = 210 GPa, nu = 0.30
    //   G = E / (2(1 + nu)) = 210 / 2.6  = 80.769230769230769... GPa
    //   K = E / (3(1 - 2nu)) = 210 / 1.2 = 175 GPa exactly
    //
    // This verifies the QUANTITY relationship only. Whether a material derives
    // G and K or stores them is ADR-027's policy and P15-MECH-001's code; no
    // production helper is called here, and none exists yet.
    const ElasticModulus e = 210_GPa;
    const PoissonRatio nu = PoissonRatio::of(0.30);

    const ElasticModulus g = e / (2.0 * (1.0 + nu.value()));
    const ElasticModulus k = e / (3.0 * (1.0 - 2.0 * nu.value()));

    // 210 / 2.6 worked out independently, to more digits than the check needs.
    CHECK_THAT(g.in(units::GPa), WithinRel(80.76923076923077, kRel));
    CHECK_THAT(k.in(units::GPa), WithinRel(175.0, kRel));

    // Both results are the modulus dimension, which is the point of the test:
    // dividing a pressure by a pure number leaves a pressure.
    static_assert(std::is_same_v<decltype(e / 2.0), Pressure>);
    CHECK((g + k).si() > 0.0); // they add, so they share a dimension
}

TEST_CASE("EngineeringQuantity_ThermalConductivityIsPowerPerLengthKelvin", "[units][p15][thermal]") {
    // k = W/(m K). Built from first principles: 50 W through 1 m per 1 K.
    const ThermalConductivity k = 50_W_per_m_K;
    CHECK_THAT(k.in(units::W_per_m_K), WithinRel(50.0, kRel));

    // Fourier's law, dimensionally: q = k A dT / L must be a POWER.
    const Power flux = k * 2_m2 * Temperature::fromSi(10.0) / 0.5_m;
    static_assert(std::is_same_v<decltype(k * 2_m2 * Temperature::fromSi(10.0) / 0.5_m), Power>);
    // 50 * 2 * 10 / 0.5 = 2000 W, by hand.
    CHECK_THAT(flux.in(units::W), WithinRel(2000.0, kRel));

    // NOT spring stiffness. N/m shares the letter k and nothing else: it is
    // mass per time squared, and conductivity is not.
    static_assert(!std::is_same_v<decltype(1_N / 1_m), ThermalConductivity>);
}

TEST_CASE("EngineeringQuantity_SpecificHeatGivesEnergyFromMassAndInterval", "[units][p15][thermal]") {
    // The brief's fixture: m = 2 kg, cp = 500 J/(kg K), dT = 10 K -> Q = 10000 J.
    const Mass mass = 2_kg;
    const SpecificHeatCapacity cp = 500_J_per_kg_K;
    const Temperature interval = Temperature::fromSi(10.0);

    const Energy heat = mass * cp * interval;
    static_assert(std::is_same_v<decltype(mass * cp * interval), Energy>);
    CHECK_THAT(heat.in(units::J), WithinRel(10000.0, kRel));
    CHECK_THAT(heat.in(units::kJ), WithinRel(10.0, kRel));

    // kJ/(kg K) is exactly 1000 J/(kg K).
    CHECK(1_kJ_per_kg_K == 1000_J_per_kg_K);

    // An energy is a force through a distance, which is the same joule.
    CHECK((1_N * 1_m) == 1_J);
}

TEST_CASE("EngineeringQuantity_ThermalExpansionIsInverseTemperature", "[units][p15][thermal]") {
    // The brief's fixture: alpha = 12e-6 /K, dT = 50 K -> strain = 6e-4.
    const ThermalExpansionCoefficient alpha = 12_um_per_m_K;
    CHECK_THAT(alpha.in(units::per_K), WithinRel(12.0e-6, kRel));

    const Temperature interval = Temperature::fromSi(50.0);
    const double strain = alpha * interval;
    // alpha * dT cancels temperature completely, so it IS a pure number -- which
    // is the whole reason alpha must not be dimensionless itself.
    static_assert(std::is_same_v<decltype(alpha * interval), double>);
    CHECK_THAT(strain, WithinRel(6.0e-4, kRel));

    // dL = alpha L dT must be a LENGTH. 12e-6 * 2 m * 50 K = 1.2e-3 m = 1.2 mm.
    const Length growth = alpha * 2_m * interval;
    static_assert(std::is_same_v<decltype(alpha * 2_m * interval), Length>);
    CHECK_THAT(growth.in(units::mm), WithinRel(1.2, kRel));

    // um/(m K) is exactly 1e-6 /K, so the datasheet form and the SI form agree.
    CHECK(12_um_per_m_K == 0.000012_per_K);
}

TEST_CASE("EngineeringQuantity_ViscosityDynamicAndKinematicAreDistinct", "[units][p15][viscosity]") {
    // In P15 scope because ADR-027 lists dynamic viscosity among the canonical
    // properties and ADR-028's P19 line names viscosity among what CFD consumes.
    const DynamicViscosity mu = 1_mPa_s; // water at about 20 C, 1e-3 Pa s
    CHECK_THAT(mu.in(units::Pa_s), WithinRel(1.0e-3, kRel));

    // nu_kin = mu / rho. Water: 1e-3 Pa s over 1000 kg/m^3 = 1e-6 m^2/s, which
    // is 1 mm^2/s -- the textbook value, arrived at from the two inputs.
    const Density water = 1000_kg_per_m3;
    const KinematicViscosity nuKin = mu / water;
    static_assert(std::is_same_v<decltype(mu / water), KinematicViscosity>);
    CHECK_THAT(nuKin.in(units::m2_per_s), WithinRel(1.0e-6, kRel));
    CHECK_THAT(nuKin.in(units::mm2_per_s), WithinRel(1.0, kRel));

    // The two viscosities are different types, so the pair cannot be swapped.
    static_assert(!std::is_convertible_v<DynamicViscosity, KinematicViscosity>);
    static_assert(!std::is_convertible_v<KinematicViscosity, DynamicViscosity>);
}

TEST_CASE("EngineeringQuantity_UnitIdentitiesHoldThroughTypes", "[units][p15]") {
    // Equivalent unit expressions must resolve to the same DIMENSION, proved by
    // the type system rather than by comparing symbol strings.
    static_assert(std::is_same_v<decltype(1_N / 1_m2), Pressure>);            // Pa == N/m^2
    static_assert(std::is_same_v<decltype(1_N * 1_m), Energy>);               // J  == N m
    static_assert(std::is_same_v<decltype(1_J / 1_s), Power>);                // W  == J/s
    static_assert(std::is_same_v<decltype(1_J / (1_kg * Temperature::fromSi(1.0))),
                                 SpecificHeatCapacity>);                      // J/(kg K)
    static_assert(std::is_same_v<decltype(1_W / (1_m * Temperature::fromSi(1.0))),
                                 ThermalConductivity>);                       // W/(m K)
    static_assert(std::is_same_v<decltype(1_Pa * 1_s), DynamicViscosity>);    // Pa s
    static_assert(std::is_same_v<decltype(1_m2 / 1_s), KinematicViscosity>);  // m^2/s
    static_assert(std::is_same_v<decltype(1_kg / 1_m3), Density>);            // kg/m^3

    // And the numbers agree too, not just the types.
    CHECK((1_N / 1_m2) == 1_Pa);
    CHECK((1_J / 1_s) == 1_W);
    CHECK((1_Pa * 1_s) == 1_Pa_s);
    CHECK((1_m2 / 1_s) == 1_m2_per_s);
}

TEST_CASE("EngineeringQuantity_DisplayConversionsRoundTripWithoutMovingTheValue",
          "[units][p15][conversion]") {
    // A display unit is a lens, never state: the stored SI value must come back
    // bit-identical after a trip through any unit.
    const auto roundTrips = [](auto quantity, auto unit) {
        using Q = decltype(quantity);
        const Q back = Q::fromSi(unit.scale.toSi(quantity.in(unit)));
        return back.si() == quantity.si();
    };

    CHECK(roundTrips(7850_kg_per_m3, units::g_per_cm3));
    CHECK(roundTrips(200_GPa, units::MPa));
    CHECK(roundTrips(200_GPa, units::Pa));
    CHECK(roundTrips(50_W_per_m_K, units::W_per_m_K));
    CHECK(roundTrips(500_J_per_kg_K, units::kJ_per_kg_K));
    CHECK(roundTrips(12_um_per_m_K, units::per_K));
    CHECK(roundTrips(1_mPa_s, units::Pa_s));
    CHECK(roundTrips(1_mm2_per_s, units::m2_per_s));

    // The brief's scaling-sensitive pairs, each checked in both directions.
    CHECK_THAT((7850_kg_per_m3).in(units::g_per_cm3), WithinRel(7.85, kRel));
    CHECK_THAT((7.85_g_per_cm3).in(units::kg_per_m3), WithinRel(7850.0, kRel));
    CHECK_THAT((200_GPa).in(units::MPa), WithinRel(200000.0, kRel));
    CHECK_THAT((200000_MPa).in(units::GPa), WithinRel(200.0, kRel));
    CHECK_THAT((0.000012_per_K).in(units::um_per_m_K), WithinRel(12.0, kRel));
    CHECK_THAT((12_um_per_m_K).in(units::per_K), WithinRel(0.000012, kRel));

    // CHANGING THE DISPLAY UNIT DOES NOT MOVE THE ENGINEERING STATE. One
    // quantity, read three ways, still the same stored number.
    const ElasticModulus e = 200_GPa;
    const double asGPa = e.in(units::GPa);
    const double asMPa = e.in(units::MPa);
    const double asPa = e.in(units::Pa);
    CHECK_THAT(asGPa, WithinRel(200.0, kRel));
    CHECK_THAT(asMPa, WithinRel(200000.0, kRel));
    CHECK_THAT(asPa, WithinRel(2.0e11, kRel));
    CHECK(e.si() == (200_GPa).si()); // unmoved, exactly
}

TEST_CASE("EngineeringQuantity_ScalesAreExactWhereTheMathematicsIsExact",
          "[units][p15][conversion]") {
    // UnitScale is a RATIO, so a decimal conversion is one correctly rounded
    // operation rather than a chain. Where the factor is a power of ten, the
    // conversion is exact and == is the right assertion, not a tolerance.
    CHECK((1_MPa).si() == 1.0e6);
    CHECK((1_GPa).si() == 1.0e9);
    CHECK((1_g_per_cm3).si() == 1000.0);
    CHECK((1_kJ).si() == 1000.0);
    CHECK((1_kW).si() == 1000.0);
    CHECK((1_kJ_per_kg_K).si() == 1000.0);
    CHECK((1_mPa_s).si() == 1.0e-3);

    // The ratio form is what makes um/(m K) exact rather than 1e-6 rounded.
    CHECK(units::um_per_m_K.scale.numerator == 1.0);
    CHECK(units::um_per_m_K.scale.denominator == 1.0e6);
    CHECK((1_um_per_m_K).si() == 1.0 / 1.0e6);
}

TEST_CASE("EngineeringQuantity_NonFiniteValuesAreNotEngineeringData", "[units][p15][validation]") {
    // isFinite() is the existing answer and it must cover the new families too.
    // A non-finite quantity is BAD DATA; it is not how "unknown" is spelled --
    // ADR-027 gives unknown its own state, holding no value at all.
    constexpr double nan = std::numeric_limits<double>::quiet_NaN();
    constexpr double inf = std::numeric_limits<double>::infinity();

    CHECK_FALSE(isFinite(Density::fromSi(nan)));
    CHECK_FALSE(isFinite(Density::fromSi(inf)));
    CHECK_FALSE(isFinite(Density::fromSi(-inf)));

    CHECK_FALSE(isFinite(ElasticModulus::fromSi(nan)));
    CHECK_FALSE(isFinite(ThermalConductivity::fromSi(inf)));
    CHECK_FALSE(isFinite(SpecificHeatCapacity::fromSi(-inf)));
    CHECK_FALSE(isFinite(ThermalExpansionCoefficient::fromSi(nan)));
    CHECK_FALSE(isFinite(DynamicViscosity::fromSi(inf)));
    CHECK_FALSE(isFinite(KinematicViscosity::fromSi(nan)));
    CHECK_FALSE(isFinite(Energy::fromSi(nan)));
    CHECK_FALSE(isFinite(Power::fromSi(inf)));

    // And Poisson's ratio, which is not a Quantity and so needs its own.
    CHECK_FALSE(isFinite(PoissonRatio::of(nan)));
    CHECK_FALSE(isFinite(PoissonRatio::of(inf)));
    CHECK_FALSE(isFinite(PoissonRatio::of(-inf)));

    // Ordinary values pass, so the check is not simply always false.
    CHECK(isFinite(7850_kg_per_m3));
    CHECK(isFinite(200_GPa));
    CHECK(isFinite(PoissonRatio::of(0.3)));
    CHECK(isFinite(Density::fromSi(0.0))); // zero is finite, and a real value
}

TEST_CASE("EngineeringQuantity_FormattingIsDeterministicAndHasNoNegativeZero",
          "[units][p15][determinism]") {
    // Repeated formatting of one value is byte-identical: no registry order, no
    // cached state, no locale drift.
    const Density density = 7850_kg_per_m3;
    const std::string first = toString(density, units::kg_per_m3);
    for (int pass = 0; pass < 8; ++pass) {
        INFO("pass " << pass);
        CHECK(toString(density, units::kg_per_m3) == first);
    }

    // A value that is negative zero must not print as "-0": the sign of zero is
    // an artefact of arithmetic, not engineering information.
    const Density negativeZero = Density::fromSi(-0.0);
    CHECK(std::signbit(negativeZero.si())); // it really is negative zero
    const std::string printed = toString(negativeZero, units::kg_per_m3);
    INFO("printed '" << printed << "'");
    CHECK_THAT(printed, !ContainsSubstring("-0"));

    // The same for every new family, since each goes through the same writer.
    CHECK_THAT(toString(ThermalExpansionCoefficient::fromSi(-0.0), units::per_K),
               !ContainsSubstring("-0"));
    CHECK_THAT(toString(SpecificHeatCapacity::fromSi(-0.0), units::J_per_kg_K),
               !ContainsSubstring("-0"));

    // Decimal points, never decimal commas, whatever the ambient locale.
    CHECK_THAT(toString(0.5_g_per_cm3, units::g_per_cm3), ContainsSubstring("."));
    CHECK_THAT(toString(0.5_g_per_cm3, units::g_per_cm3), !ContainsSubstring(","));
}

TEST_CASE("EngineeringQuantity_EveryNewUnitIsInTheCatalogExactlyOnce", "[units][p15]") {
    // The catalog is what run-time code parses and serializes through, so a unit
    // that exists in the header and not in the catalog is invisible to half the
    // system. Its symbols are already asserted unique at compile time.
    for (const std::string_view symbol : {"J", "kJ", "W", "kW", "W/(m K)", "J/(kg K)",
                                          "kJ/(kg K)", "1/K", "um/(m K)", "Pa s", "mPa s",
                                          "m^2/s", "mm^2/s"}) {
        INFO(symbol);
        const auto found = findUnit(symbol);
        REQUIRE(found.has_value());
        CHECK(found->symbol == symbol);
    }

    // And the dimensions travel with them, so a run-time lookup cannot mix a
    // conductivity up with a specific heat.
    CHECK(findUnit("W/(m K)")->dimension == dimensions::thermalConductivity);
    CHECK(findUnit("J/(kg K)")->dimension == dimensions::specificHeatCapacity);
    CHECK(findUnit("1/K")->dimension == dimensions::thermalExpansion);
    CHECK(findUnit("Pa s")->dimension == dimensions::dynamicViscosity);
    CHECK(findUnit("m^2/s")->dimension == dimensions::kinematicViscosity);
}

TEST_CASE("EngineeringQuantity_TemperatureIsKelvinAndCarriesNoOffset", "[units][p15][thermal]") {
    // THE TEMPERATURE CONTRACT, and the reason P15 needs no separate
    // temperature-interval type yet.
    //
    // UnitScale is a pure RATIO -- numerator over denominator -- so it cannot
    // express an offset. Celsius and Fahrenheit need one, so they are not
    // representable, and the catalog holds exactly one temperature unit: kelvin.
    // In kelvin an absolute temperature and an interval are arithmetically
    // identical, so alpha*dT and m*cp*dT are correct with a plain Temperature
    // operand and no Celsius mistake is available to make.
    //
    // This is a structural argument, not a promise, so it is checked:
    int temperatureUnits = 0;
    for (const UnitDescriptor& unit : unitCatalog()) {
        if (unit.dimension == dimensions::temperature) {
            ++temperatureUnits;
            INFO(unit.symbol);
            CHECK(unit.symbol == "K");
            // A ratio of 1:1 -- no scaling, and nowhere to put an offset.
            CHECK(unit.scale.numerator == 1.0);
            CHECK(unit.scale.denominator == 1.0);
        }
    }
    CHECK(temperatureUnits == 1);

    // KNOWN LIMITATION, recorded as an assertion so it cannot rot silently: the
    // day a second temperature unit appears, this test fails, and whoever adds
    // it must then decide about absolute-vs-interval typing before proceeding.
}
