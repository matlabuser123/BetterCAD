#include <bettercad/core/materials/MaterialProperty.hpp>
#include <bettercad/core/materials/ThermalProperties.hpp>
#include <bettercad/core/units/Format.hpp>
#include <bettercad/core/units/UnitCatalog.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/core/units/Units.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinRel;
using materials::ElectricalResistivity;
using materials::MaterialProperty;
using materials::OutOfRangeBehaviour;
using materials::PropertyLawKind;
using materials::ThermalProperties;
using materials::ThermalPropertyKind;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

} // namespace

// --- unknown is not zero ----------------------------------------------------

TEST_CASE("ThermalProperty_AnUnknownPropertyHasNoValueAndIsNotZero") {
    const ThermalProperties empty;

    REQUIRE(empty.thermalConductivity.isUnknown());
    REQUIRE(empty.specificHeatCapacity.isUnknown());
    REQUIRE(empty.thermalExpansion.isUnknown());
    REQUIRE(empty.meltingTemperature.isUnknown());
    REQUIRE(empty.electricalResistivity.isUnknown());

    // Empty, never zero. An unknown conductivity is not a perfect insulator and
    // an unknown specific heat is not a material that cannot store heat.
    REQUIRE_FALSE(empty.thermalConductivity.value().has_value());
    REQUIRE_FALSE(empty.specificHeatCapacity.value().has_value());
    REQUIRE(empty.thermalConductivity.value() != std::optional<ThermalConductivity>{0_W_per_m_K});
    REQUIRE(empty.specificHeatCapacity.value()
            != std::optional<SpecificHeatCapacity>{0_J_per_kg_K});

    // And a material with no thermal data at all is valid data.
    REQUIRE(materials::validate(empty));
}

TEST_CASE("ThermalProperty_APartiallyCharacterisedMaterialIsValidData") {
    // Conductivity known, everything else unknown: what many datasheets give.
    ThermalProperties properties;
    properties.thermalConductivity = MaterialProperty<ThermalConductivity>::known(167_W_per_m_K);
    REQUIRE(materials::validate(properties));
    REQUIRE(materials::hasSteadyConductionProperties(properties));
    REQUIRE_FALSE(materials::hasSpecificHeatCapacity(properties));
    REQUIRE_FALSE(materials::hasThermalExpansion(properties));
    // No placeholder was needed to make it constructible.
    REQUIRE(properties.specificHeatCapacity.isUnknown());
}

// --- ranges and finiteness --------------------------------------------------

TEST_CASE("ThermalProperty_RejectsAConductivityThatIsNotPositiveAndFinite") {
    for (const double si : {0.0, -1.0, -167.0, kNaN, kInf, -kInf}) {
        ThermalProperties properties;
        properties.thermalConductivity =
            MaterialProperty<ThermalConductivity>::known(ThermalConductivity::fromSi(si));
        INFO("k si = " << si);
        const Result<void> valid = materials::validate(properties);
        REQUIRE_FALSE(valid);
        REQUIRE(valid.error().code == ErrorCode::InvalidArgument);
        REQUIRE_THAT(valid.error().message, ContainsSubstring("thermal conductivity"));
        REQUIRE_FALSE(materials::hasSteadyConductionProperties(properties));
    }
    ThermalProperties good;
    good.thermalConductivity = MaterialProperty<ThermalConductivity>::known(167_W_per_m_K);
    REQUIRE(materials::validate(good));
}

TEST_CASE("ThermalProperty_RejectsASpecificHeatThatIsNotPositiveAndFinite") {
    for (const double si : {0.0, -1.0, -500.0, kNaN, kInf, -kInf}) {
        ThermalProperties properties;
        properties.specificHeatCapacity =
            MaterialProperty<SpecificHeatCapacity>::known(SpecificHeatCapacity::fromSi(si));
        INFO("cp si = " << si);
        const Result<void> valid = materials::validate(properties);
        REQUIRE_FALSE(valid);
        REQUIRE_THAT(valid.error().message, ContainsSubstring("specific heat capacity"));
        REQUIRE_FALSE(materials::hasSpecificHeatCapacity(properties));
    }
    ThermalProperties good;
    good.specificHeatCapacity = MaterialProperty<SpecificHeatCapacity>::known(500_J_per_kg_K);
    REQUIRE(materials::validate(good));
}

TEST_CASE("ThermalProperty_AcceptsANegativeThermalExpansionCoefficient") {
    // Negative thermal expansion is real: some materials contract when heated.
    // Rejecting it would be an invented restriction that throws away good data.
    for (const double perK : {-12.0e-6, -1.0e-9, 0.0, 12.0e-6, 23.0e-6, 1.0}) {
        ThermalProperties properties;
        properties.thermalExpansion = MaterialProperty<ThermalExpansionCoefficient>::known(
            ThermalExpansionCoefficient::fromSi(perK));
        INFO("alpha = " << perK << " /K");
        REQUIRE(materials::validate(properties));
        REQUIRE(materials::hasThermalExpansion(properties));
    }
}

TEST_CASE("ThermalProperty_RejectsAThermalExpansionCoefficientThatIsNotFinite") {
    for (const double perK : {kNaN, kInf, -kInf}) {
        ThermalProperties properties;
        properties.thermalExpansion = MaterialProperty<ThermalExpansionCoefficient>::known(
            ThermalExpansionCoefficient::fromSi(perK));
        INFO("alpha = " << perK);
        const Result<void> valid = materials::validate(properties);
        REQUIRE_FALSE(valid);
        REQUIRE_THAT(valid.error().message, ContainsSubstring("thermal expansion"));
        REQUIRE_FALSE(materials::hasThermalExpansion(properties));
    }
}

// --- melting temperature: absolute, not an interval -------------------------

TEST_CASE("ThermalProperty_AMeltingTemperatureIsAbsoluteAndMustBeAboveAbsoluteZero") {
    ThermalProperties good;
    good.meltingTemperature = MaterialProperty<Temperature>::known(933_K);
    REQUIRE(materials::validate(good));
    REQUIRE(good.meltingTemperature.value()->si() == 933.0);

    // A melting point of zero or below is not a temperature. This is the one
    // place where an absolute temperature and a temperature INTERVAL differ in
    // validity: an interval of -20 K is an ordinary cooling.
    for (const double kelvin : {0.0, -1.0, -273.15, -933.0, kNaN, kInf, -kInf}) {
        ThermalProperties properties;
        properties.meltingTemperature =
            MaterialProperty<Temperature>::known(Temperature::fromSi(kelvin));
        INFO("melting T = " << kelvin << " K");
        const Result<void> valid = materials::validate(properties);
        REQUIRE_FALSE(valid);
        REQUIRE_THAT(valid.error().message, ContainsSubstring("melting temperature"));
    }
}

TEST_CASE("ThermalProperty_TemperatureIsKelvinOnlySoThereIsNoCelsiusOffsetToGetWrong") {
    // The unit system has one temperature unit and its scale is 1:1, so a
    // temperature is a pure ratio to the kelvin with no offset. That is what
    // makes an absolute temperature and an interval numerically interchangeable
    // -- and why the melting temperature's range check, not the type, is what
    // keeps them apart.
    int temperatureUnits = 0;
    for (const UnitDescriptor& descriptor : unitCatalog()) {
        if (descriptor.dimension == dimensions::temperature) {
            ++temperatureUnits;
            INFO("temperature unit " << descriptor.symbol);
            REQUIRE(descriptor.scale.numerator == 1.0);
            REQUIRE(descriptor.scale.denominator == 1.0);
        }
    }
    // Exactly one. This fails the day a Celsius unit is added, which cannot be a
    // scale and needs an offset the system does not have.
    REQUIRE(temperatureUnits == 1);

    // A 20 K interval and an absolute 20 K are the same number, deliberately.
    const Temperature interval = 20_K;
    const Temperature absolute = 20_K;
    REQUIRE(interval == absolute);
}

// --- electrical resistivity (ADR-029) --------------------------------------

TEST_CASE("ThermalProperty_ResistivityIsANamedTypeAndNotABareNumber") {
    const ElectricalResistivity copper = ElectricalResistivity::ofOhmMetres(1.7e-8);
    REQUIRE_THAT(copper.ohmMetres(), WithinRel(1.7e-8, 1e-12));
    REQUIRE(copper == ElectricalResistivity::ofOhmMetres(1.7e-8));
    REQUIRE(copper != ElectricalResistivity::ofOhmMetres(2.8e-8));

    // Not a Quantity, because the dimension system has no electric-current
    // exponent (ADR-029). So it is nominally safe and dimensionally unchecked,
    // and this states which of those it is.
    static_assert(!std::is_same_v<ElectricalResistivity, Density>);
    static_assert(!std::is_same_v<ElectricalResistivity, double>);
    static_assert(!std::is_convertible_v<ElectricalResistivity, double>);
    static_assert(!std::is_convertible_v<double, ElectricalResistivity>);
    static_assert(!std::is_convertible_v<Density, ElectricalResistivity>);

    ThermalProperties properties;
    properties.electricalResistivity = MaterialProperty<ElectricalResistivity>::known(copper);
    REQUIRE(materials::validate(properties));
    REQUIRE(*properties.electricalResistivity.value() == copper);
}

TEST_CASE("ThermalProperty_RejectsAResistivityThatIsNotPositiveAndFinite") {
    for (const double ohmMetres : {0.0, -1.7e-8, -1.0, kNaN, kInf, -kInf}) {
        ThermalProperties properties;
        properties.electricalResistivity = MaterialProperty<ElectricalResistivity>::known(
            ElectricalResistivity::ofOhmMetres(ohmMetres));
        INFO("resistivity = " << ohmMetres << " Ohm m");
        const Result<void> valid = materials::validate(properties);
        REQUIRE_FALSE(valid);
        REQUIRE_THAT(valid.error().message, ContainsSubstring("electrical resistivity"));
    }
}

TEST_CASE("ThermalProperty_ResistivityPrintsItsUnitAndNoConductivityIsDerivedFromIt") {
    REQUIRE_THAT(materials::toString(ElectricalResistivity::ofOhmMetres(1.7e-8)),
                 ContainsSubstring("Ohm m"));
    // There is no stored or derived electrical conductivity. sigma = 1 / rho is a
    // relationship a consumer may compute; it is not a second authoritative
    // spelling of the same property (ADR-029).
    std::vector<ThermalPropertyKind> kinds;
    for (const ThermalPropertyKind kind : materials::thermalPropertyKinds()) {
        kinds.push_back(kind);
    }
    for (const ThermalPropertyKind kind : kinds) {
        REQUIRE_THAT(std::string{materials::toString(kind)},
                     !ContainsSubstring("conductivity, electrical"));
    }
    REQUIRE(kinds.size() == 5);
}

// --- reference temperature is provenance, not a law ------------------------

TEST_CASE("ThermalProperty_RecordsTheTemperatureAValueWasMeasuredAtWithoutMakingItALaw") {
    const ThermalConductivity k = 167_W_per_m_K;
    const auto withReference = MaterialProperty<ThermalConductivity>::known(k, 293.15_K);

    REQUIRE(withReference.isKnown());
    REQUIRE(*withReference.value() == k);
    REQUIRE(withReference.referenceTemperature().has_value());
    REQUIRE_THAT(withReference.referenceTemperature()->si(), WithinRel(293.15, 1e-12));

    // The value is unchanged by the record, and nothing evaluates it against the
    // temperature: the property is still a CONSTANT. A reference temperature says
    // where a number came from; it does not turn k into k(T).
    const auto withoutReference = MaterialProperty<ThermalConductivity>::known(k);
    REQUIRE_FALSE(withoutReference.referenceTemperature().has_value());
    REQUIRE(*withoutReference.value() == *withReference.value());
    // They are different properties, though, because one records its state and
    // the other does not.
    REQUIRE(withoutReference != withReference);
}

TEST_CASE("ThermalProperty_RejectsAReferenceTemperatureThatIsNotAnAbsoluteTemperature") {
    for (const double kelvin : {0.0, -1.0, -293.15, kNaN, kInf, -kInf}) {
        ThermalProperties properties;
        properties.thermalConductivity = MaterialProperty<ThermalConductivity>::known(
            167_W_per_m_K, Temperature::fromSi(kelvin));
        INFO("reference T = " << kelvin << " K");
        const Result<void> valid = materials::validate(properties);
        REQUIRE_FALSE(valid);
        REQUIRE_THAT(valid.error().message, ContainsSubstring("reference temperature"));
    }
    ThermalProperties good;
    good.thermalConductivity =
        MaterialProperty<ThermalConductivity>::known(167_W_per_m_K, 293.15_K);
    REQUIRE(materials::validate(good));
}

TEST_CASE("ThermalProperty_ReportsEveryProblemAtOnceRatherThanTheFirst") {
    ThermalProperties properties;
    properties.thermalConductivity =
        MaterialProperty<ThermalConductivity>::known(ThermalConductivity::fromSi(-1.0));
    properties.specificHeatCapacity =
        MaterialProperty<SpecificHeatCapacity>::known(SpecificHeatCapacity::fromSi(0.0));
    properties.meltingTemperature = MaterialProperty<Temperature>::known(Temperature::fromSi(-5.0));
    properties.electricalResistivity =
        MaterialProperty<ElectricalResistivity>::known(ElectricalResistivity::ofOhmMetres(-1.0));

    const Result<void> valid = materials::validate(properties);
    REQUIRE_FALSE(valid);
    for (const std::string_view expected : {"thermal conductivity", "specific heat capacity",
                                            "melting temperature", "electrical resistivity"}) {
        INFO("expected to mention " << expected);
        REQUIRE_THAT(valid.error().message, ContainsSubstring(std::string{expected}));
    }
}

TEST_CASE("ThermalProperty_RefusesAStoredPropertyThatClaimsToBeDerived") {
    // The same rule the mechanical properties hold: a stored value is supplied or
    // unknown. One claiming to have been computed, with nothing to compute it
    // from, would make a supplied value and a derived one indistinguishable.
    ThermalProperties conductivity;
    conductivity.thermalConductivity = MaterialProperty<ThermalConductivity>::derived(167_W_per_m_K);
    REQUIRE_FALSE(materials::validate(conductivity));
    REQUIRE_THAT(materials::validate(conductivity).error().message,
                 ContainsSubstring("marked derived"));

    ThermalProperties resistivity;
    resistivity.electricalResistivity = MaterialProperty<ElectricalResistivity>::derived(
        ElectricalResistivity::ofOhmMetres(1.7e-8));
    REQUIRE_FALSE(materials::validate(resistivity));

    ThermalProperties melting;
    melting.meltingTemperature = MaterialProperty<Temperature>::derived(933_K);
    REQUIRE_FALSE(materials::validate(melting));
}

// --- relationships: data contract only, no solver -------------------------

TEST_CASE("ThermalProperty_ThermalStrainIsAlphaTimesTheTemperatureInterval") {
    // Hand-computed, NOT by calling any production relationship:
    //   eps = alpha dT = 12e-6 /K x 50 K = 6.0e-4, dimensionless
    const ThermalExpansionCoefficient alpha = 12.0e-6_per_K;
    const Temperature interval = 50_K;

    const double strain = alpha * interval;
    REQUIRE_THAT(strain, WithinRel(6.0e-4, 1e-12));

    // The TYPE is the other half of the claim: alpha times a temperature cancels
    // to a pure number, which this system spells as a plain double.
    static_assert(std::is_same_v<decltype(alpha * interval), double>);
    static_assert(!std::is_same_v<decltype(alpha * interval), ThermalExpansionCoefficient>);
}

TEST_CASE("ThermalProperty_ThermalExpansionOfALengthIsALength") {
    // Hand-computed: dL = alpha L0 dT = 12e-6 /K x 2 m x 50 K = 0.0012 m = 1.2 mm
    const ThermalExpansionCoefficient alpha = 12.0e-6_per_K;
    const Length original = 2_m;
    const Temperature interval = 50_K;

    const Length growth = alpha * original * interval;
    REQUIRE_THAT(growth.si(), WithinRel(0.0012, 1e-12));
    REQUIRE_THAT(growth.in(units::mm), WithinRel(1.2, 1e-12));

    // A Length, proved by the type system rather than by the number.
    static_assert(std::is_same_v<decltype(alpha * original * interval), Length>);
    static_assert(!std::is_same_v<decltype(alpha * original * interval), double>);
}

TEST_CASE("ThermalProperty_HeatToRaiseAMassIsMassTimesSpecificHeatTimesTheInterval") {
    // Hand-computed: Q = m cp dT = 2 kg x 500 J/(kg K) x 10 K = 10000 J
    const Mass mass = 2_kg;
    const SpecificHeatCapacity cp = 500_J_per_kg_K;
    const Temperature interval = 10_K;

    const Energy heat = mass * cp * interval;
    REQUIRE_THAT(heat.si(), WithinRel(10000.0, 1e-12));
    REQUIRE(heat == 10000_J);

    // An Energy, and specifically not a power or a specific heat.
    static_assert(std::is_same_v<decltype(mass * cp * interval), Energy>);
    static_assert(!std::is_same_v<decltype(mass * cp * interval), Power>);
    // cp is SPECIFIC -- per unit mass -- so it is not a total heat capacity,
    // which would be J/K and would depend on how much material there is.
    static_assert(!std::is_same_v<SpecificHeatCapacity, decltype(Energy{} / Temperature{})>);
}

TEST_CASE("ThermalProperty_RelationshipsAreRepeatableToTheBit") {
    const ThermalExpansionCoefficient alpha = 12.0e-6_per_K;
    const Length original = 2_m;
    const Temperature interval = 50_K;
    const Mass mass = 2_kg;
    const SpecificHeatCapacity cp = 500_J_per_kg_K;

    const double strain = alpha * interval;
    const double growth = (alpha * original * interval).si();
    const double heat = (mass * cp * interval).si();
    for (int pass = 0; pass < 20; ++pass) {
        REQUIRE(alpha * interval == strain);
        REQUIRE((alpha * original * interval).si() == growth);
        REQUIRE((mass * cp * interval).si() == heat);
    }
}

TEST_CASE("ThermalProperty_ConductivityHasConductivityDimensionsForAFutureFourierConsumer") {
    // q = -k grad(T) is P18's. What is settled here is only that k carries the
    // right dimensions to be used in it, and is not some other thermal number.
    static_assert(std::is_same_v<decltype(ThermalProperties{}.thermalConductivity)::ValueType,
                                 ThermalConductivity>);
    static_assert(std::is_same_v<ThermalConductivity, Quantity<dimensions::thermalConductivity>>);
    static_assert(!std::is_same_v<ThermalConductivity, Power>);
    static_assert(!std::is_same_v<ThermalConductivity, SpecificHeatCapacity>);
    static_assert(!std::is_same_v<ThermalConductivity, Pressure>);
    // k times a temperature gradient (K/m) times an area is a power -- the shape
    // Fourier's law needs -- and the type system says so without a solver.
    static_assert(std::is_same_v<decltype(ThermalConductivity{} * (Temperature{} / Length{})
                                          * Area{}),
                                 Power>);

    static_assert(std::is_same_v<decltype(ThermalProperties{}.specificHeatCapacity)::ValueType,
                                 SpecificHeatCapacity>);
    static_assert(std::is_same_v<decltype(ThermalProperties{}.thermalExpansion)::ValueType,
                                 ThermalExpansionCoefficient>);
    static_assert(std::is_same_v<decltype(ThermalProperties{}.meltingTemperature)::ValueType,
                                 Temperature>);
    static_assert(std::is_same_v<decltype(ThermalProperties{}.electricalResistivity)::ValueType,
                                 ElectricalResistivity>);
    SUCCEED("compile-time dimensional checks held");
}

// --- enumeration and the future contract ----------------------------------

TEST_CASE("ThermalProperty_EnumeratesItsKindsInAFixedSemanticOrder") {
    const std::vector<ThermalPropertyKind> expected{
        ThermalPropertyKind::ThermalConductivity,
        ThermalPropertyKind::SpecificHeatCapacity,
        ThermalPropertyKind::ThermalExpansion,
        ThermalPropertyKind::MeltingTemperature,
        ThermalPropertyKind::ElectricalResistivity,
    };
    for (int pass = 0; pass < 10; ++pass) {
        const std::span<const ThermalPropertyKind> kinds = materials::thermalPropertyKinds();
        REQUIRE(std::vector<ThermalPropertyKind>(kinds.begin(), kinds.end()) == expected);
    }
    for (const ThermalPropertyKind kind : expected) {
        REQUIRE_FALSE(materials::toString(kind).empty());
    }
    // Density is NOT here. It is enumerated with the mechanical properties,
    // which is its one home.
    for (const ThermalPropertyKind kind : expected) {
        REQUIRE_THAT(std::string{materials::toString(kind)}, !ContainsSubstring("density"));
    }
}

TEST_CASE("ThermalProperty_NamesTheLawKindsAFutureMilestoneMayImplement") {
    // Only Constant is implemented. The others exist so that the extension has
    // one vocabulary rather than inventing its own later.
    REQUIRE(materials::toString(PropertyLawKind::Constant) == "constant");
    REQUIRE(materials::toString(PropertyLawKind::Table) == "table");
    REQUIRE(materials::toString(PropertyLawKind::AnalyticLaw) == "analytic law");
}

TEST_CASE("ThermalProperty_FixesTheInvariantsAFutureTemperatureTableMustHold") {
    const std::span<const std::string_view> invariants = materials::temperatureTableInvariants();
    REQUIRE(invariants.size() == 6);

    // Each of these is a defect that is cheap to prevent now and expensive to
    // find once a table exists.
    const std::vector<std::string_view> expected{
        "the independent variable is absolute temperature",
        "values carry their property's own strong type",
        "temperature points are ordered, ascending",
        "temperature points are deterministic, never from unordered iteration",
        "duplicate temperature points are invalid",
        "behaviour outside the range is chosen explicitly, never defaulted",
    };
    REQUIRE(std::vector<std::string_view>(invariants.begin(), invariants.end()) == expected);
}

TEST_CASE("ThermalProperty_NamesTheOutOfRangeChoicesAndOffersNoDefault") {
    // A future law must CHOOSE. Naming all three, with none of them a default, is
    // what stops silent extrapolation beyond a material's measured range.
    REQUIRE(materials::toString(OutOfRangeBehaviour::Fail) == "fail");
    REQUIRE(materials::toString(OutOfRangeBehaviour::Clamp) == "clamp");
    REQUIRE(materials::toString(OutOfRangeBehaviour::Extrapolate) == "extrapolate");
}
