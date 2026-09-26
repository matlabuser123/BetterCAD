#include <bettercad/core/materials/MaterialProperty.hpp>
#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/core/units/Format.hpp>
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
using materials::Elongation;
using materials::Hardness;
using materials::HardnessScale;
using materials::MaterialProperty;
using materials::MechanicalProperties;
using materials::MechanicalPropertyKind;
using materials::PropertyState;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

/// Steel-like numbers, used only as arithmetic fixtures. They are NOT a claim
/// about any real material: this milestone stores no sourced property values
/// (ADR-028), and these exist to exercise the relationships.
MechanicalProperties elasticFixture() {
    MechanicalProperties properties;
    properties.youngsModulus = MaterialProperty<ElasticModulus>::known(210_GPa);
    properties.poissonRatio = MaterialProperty<PoissonRatio>::known(PoissonRatio::of(0.30));
    return properties;
}

} // namespace

// --- unknown is not zero ----------------------------------------------------

TEST_CASE("MechanicalProperty_AnUnknownPropertyHasNoValueAndIsNotZero") {
    const MechanicalProperties empty;

    // Every slot starts Unknown, so a material with no mechanical data needs no
    // placeholders to exist.
    REQUIRE(empty.density.isUnknown());
    REQUIRE(empty.youngsModulus.isUnknown());
    REQUIRE(empty.poissonRatio.isUnknown());
    REQUIRE(empty.yieldStrength.isUnknown());
    REQUIRE(empty.ultimateTensileStrength.isUnknown());
    REQUIRE(empty.ultimateCompressiveStrength.isUnknown());
    REQUIRE(empty.shearStrength.isUnknown());
    REQUIRE(empty.elongation.isUnknown());
    REQUIRE(empty.hardness.isUnknown());

    // And not one of them reads as zero. This is the whole point of the type:
    // value() is empty, so there is no number to mistake for a measurement.
    REQUIRE_FALSE(empty.youngsModulus.value().has_value());
    REQUIRE_FALSE(empty.yieldStrength.value().has_value());
    REQUIRE_FALSE(empty.density.value().has_value());
    REQUIRE_FALSE(empty.elongation.value().has_value());
    REQUIRE(empty.youngsModulus.value() != std::optional<ElasticModulus>{0_Pa});
    REQUIRE(empty.density.value() != std::optional<Density>{Density::fromSi(0.0)});

    // An empty material is valid. Unknown is not a fault.
    REQUIRE(materials::validate(empty));
}

TEST_CASE("MechanicalProperty_ReportsWhichOfTheThreeStatesItIsIn") {
    REQUIRE(MaterialProperty<ElasticModulus>::unknown().state() == PropertyState::Unknown);
    REQUIRE(MaterialProperty<ElasticModulus>::known(210_GPa).state() == PropertyState::Known);
    REQUIRE(MaterialProperty<ElasticModulus>::derived(80_GPa).state() == PropertyState::Derived);

    const auto known = MaterialProperty<ElasticModulus>::known(210_GPa);
    REQUIRE(known.hasValue());
    REQUIRE(*known.value() == 210_GPa);
    const auto derived = MaterialProperty<ElasticModulus>::derived(80_GPa);
    REQUIRE(derived.hasValue());
    // A derived value is readable but never confusable with a supplied one.
    REQUIRE_FALSE(derived.isKnown());
    REQUIRE(materials::toString(PropertyState::Derived) == "derived");
    REQUIRE(materials::toString(PropertyState::Unknown) == "unknown");
    REQUIRE(materials::toString(PropertyState::Known) == "known");
}

// --- ranges and finiteness --------------------------------------------------

TEST_CASE("MechanicalProperty_RejectsAKnownDensityThatIsNotPositiveAndFinite") {
    for (const double si : {0.0, -1.0, -7850.0, kNaN, kInf, -kInf}) {
        MechanicalProperties properties;
        properties.density = MaterialProperty<Density>::known(Density::fromSi(si));
        const Result<void> valid = materials::validate(properties);
        INFO("density si = " << si);
        REQUIRE_FALSE(valid);
        REQUIRE(valid.error().code == ErrorCode::InvalidArgument);
        REQUIRE_THAT(valid.error().message, ContainsSubstring("density"));
    }
    MechanicalProperties good;
    good.density = MaterialProperty<Density>::known(Density::fromSi(7850.0));
    REQUIRE(materials::validate(good));
}

TEST_CASE("MechanicalProperty_RejectsAKnownYoungsModulusThatIsNotPositiveAndFinite") {
    for (const double si : {0.0, -1.0, -210.0e9, kNaN, kInf, -kInf}) {
        MechanicalProperties properties;
        properties.youngsModulus = MaterialProperty<ElasticModulus>::known(
            ElasticModulus::fromSi(si));
        const Result<void> valid = materials::validate(properties);
        INFO("E si = " << si);
        REQUIRE_FALSE(valid);
        REQUIRE_THAT(valid.error().message, ContainsSubstring("Young's modulus"));
    }
    MechanicalProperties good;
    good.youngsModulus = MaterialProperty<ElasticModulus>::known(210_GPa);
    REQUIRE(materials::validate(good));
}

TEST_CASE("MechanicalProperty_AcceptsPoissonRatiosStrictlyInsideTheStableRange") {
    // Both ends are excluded because each one makes a derivation divide by zero.
    for (const double value : {-0.999999, -0.5, 0.0, 0.2, 0.3, 0.49, 0.499999}) {
        MechanicalProperties properties;
        properties.poissonRatio = MaterialProperty<PoissonRatio>::known(PoissonRatio::of(value));
        INFO("nu = " << value);
        REQUIRE(materials::validate(properties));
    }
}

TEST_CASE("MechanicalProperty_RejectsPoissonRatiosAtOrOutsideTheStableRange") {
    for (const double value : {-1.0, 0.5, -1.5, 0.7, 1.0, kNaN, kInf, -kInf}) {
        MechanicalProperties properties;
        properties.poissonRatio = MaterialProperty<PoissonRatio>::known(PoissonRatio::of(value));
        const Result<void> valid = materials::validate(properties);
        INFO("nu = " << value);
        REQUIRE_FALSE(valid);
        REQUIRE_THAT(valid.error().message, ContainsSubstring("Poisson's ratio"));
    }
}

TEST_CASE("MechanicalProperty_RejectsEveryStrengthThatIsNotPositiveAndFinite") {
    struct Case {
        MaterialProperty<Stress> MechanicalProperties::*member;
        std::string name;
    };
    const std::vector<Case> cases{
        {&MechanicalProperties::yieldStrength, "yield strength"},
        {&MechanicalProperties::ultimateTensileStrength, "ultimate tensile strength"},
        {&MechanicalProperties::ultimateCompressiveStrength, "ultimate compressive strength"},
        {&MechanicalProperties::shearStrength, "shear strength"},
    };
    for (const Case& test : cases) {
        for (const double si : {0.0, -1.0, kNaN, kInf, -kInf}) {
            MechanicalProperties properties;
            (properties.*test.member) = MaterialProperty<Stress>::known(Stress::fromSi(si));
            const Result<void> valid = materials::validate(properties);
            INFO(test.name << " si = " << si);
            REQUIRE_FALSE(valid);
            REQUIRE_THAT(valid.error().message, ContainsSubstring(test.name));
        }
        MechanicalProperties good;
        (good.*test.member) = MaterialProperty<Stress>::known(250_MPa);
        INFO(test.name << " positive");
        REQUIRE(materials::validate(good));
    }
}

TEST_CASE("MechanicalProperty_ReportsEveryProblemAtOnceRatherThanTheFirst") {
    MechanicalProperties properties;
    properties.density = MaterialProperty<Density>::known(Density::fromSi(-1.0));
    properties.youngsModulus = MaterialProperty<ElasticModulus>::known(ElasticModulus::fromSi(0.0));
    properties.poissonRatio = MaterialProperty<PoissonRatio>::known(PoissonRatio::of(0.5));
    properties.yieldStrength = MaterialProperty<Stress>::known(Stress::fromSi(-5.0));

    const Result<void> valid = materials::validate(properties);
    REQUIRE_FALSE(valid);
    // All four, so someone fixing a material sees the whole list.
    for (const std::string_view expected :
         {"density", "Young's modulus", "Poisson's ratio", "yield strength"}) {
        INFO("expected to mention " << expected);
        REQUIRE_THAT(valid.error().message, ContainsSubstring(std::string{expected}));
    }
}

// --- elongation -------------------------------------------------------------

TEST_CASE("MechanicalProperty_ElongationIsAFractionAndSaysSoBothWays") {
    // 0.12 is 12 %, not 0.12 %. The representation is fixed so that the factor
    // of 100 cannot be in doubt.
    const Elongation twelvePercent = Elongation::of(0.12);
    REQUIRE_THAT(twelvePercent.value(), WithinRel(0.12, 1e-12));
    REQUIRE_THAT(twelvePercent.percent(), WithinRel(12.0, 1e-12));

    REQUIRE(Elongation::ofPercent(12.0) == twelvePercent);
    REQUIRE_THAT(Elongation::ofPercent(12.0).value(), WithinRel(0.12, 1e-12));

    // 12 as a fraction is 1200 %, which is a different thing entirely and is
    // still valid data -- elastomers stretch that far.
    REQUIRE_THAT(Elongation::of(12.0).percent(), WithinRel(1200.0, 1e-12));
    MechanicalProperties stretchy;
    stretchy.elongation = MaterialProperty<Elongation>::known(Elongation::of(12.0));
    REQUIRE(materials::validate(stretchy));
}

TEST_CASE("MechanicalProperty_ElongationAcceptsZeroAndRejectsNegativeAndNonFinite") {
    MechanicalProperties zero;
    zero.elongation = MaterialProperty<Elongation>::known(Elongation::of(0.0));
    REQUIRE(materials::validate(zero));

    for (const double fraction : {-0.01, -1.0, kNaN, kInf, -kInf}) {
        MechanicalProperties properties;
        properties.elongation = MaterialProperty<Elongation>::known(Elongation::of(fraction));
        INFO("elongation fraction = " << fraction);
        const Result<void> valid = materials::validate(properties);
        REQUIRE_FALSE(valid);
        REQUIRE_THAT(valid.error().message, ContainsSubstring("elongation"));
    }
}

// --- hardness ---------------------------------------------------------------

TEST_CASE("MechanicalProperty_HardnessCarriesItsScaleAndTheScaleIsPartOfTheValue") {
    const Hardness hrc = Hardness::of(60.0, HardnessScale::RockwellC);
    const Hardness hrb = Hardness::of(60.0, HardnessScale::RockwellB);

    REQUIRE(hrc.value() == 60.0);
    REQUIRE(hrc.scale() == HardnessScale::RockwellC);
    // The same number on two scales is two different hardnesses.
    REQUIRE(hrc != hrb);
    REQUIRE(hrc == Hardness::of(60.0, HardnessScale::RockwellC));

    // And a property carrying one is not equal to a property carrying the other.
    REQUIRE(MaterialProperty<Hardness>::known(hrc) != MaterialProperty<Hardness>::known(hrb));
}

TEST_CASE("MechanicalProperty_HardnessPrintsItsScaleEveryTime") {
    REQUIRE(materials::toString(Hardness::of(60.0, HardnessScale::RockwellC)) == "60 HRC");
    REQUIRE(materials::toString(Hardness::of(95.0, HardnessScale::RockwellB)) == "95 HRB");
    REQUIRE(materials::toString(Hardness::of(180.0, HardnessScale::Brinell)) == "180 HBW");
    REQUIRE(materials::toString(Hardness::of(650.0, HardnessScale::Vickers)) == "650 HV");
    // Every scale has a symbol, so none can print as a bare number.
    for (const HardnessScale scale : {HardnessScale::Brinell, HardnessScale::Vickers,
                                      HardnessScale::RockwellB, HardnessScale::RockwellC}) {
        REQUIRE_FALSE(materials::toString(scale).empty());
    }
}

TEST_CASE("MechanicalProperty_RejectsAHardnessThatIsNotPositiveAndFinite") {
    for (const double value : {0.0, -60.0, kNaN, kInf, -kInf}) {
        MechanicalProperties properties;
        properties.hardness =
            MaterialProperty<Hardness>::known(Hardness::of(value, HardnessScale::RockwellC));
        INFO("hardness = " << value);
        const Result<void> valid = materials::validate(properties);
        REQUIRE_FALSE(valid);
        REQUIRE_THAT(valid.error().message, ContainsSubstring("hardness"));
    }
}

// --- the derived constants --------------------------------------------------

TEST_CASE("MechanicalProperty_DerivesTheShearModulusFromYoungsModulusAndPoissonRatio") {
    // Hand-computed, NOT by calling the production relationship:
    //   G = E / (2(1 + nu)) = 210 / (2 x 1.30) = 210 / 2.6 = 80.769230769230769... GPa
    const double expectedSi = 80.76923076923077e9;

    const MaterialProperty<ElasticModulus> shear =
        materials::derivedShearModulus(elasticFixture());
    REQUIRE(shear.isDerived());
    REQUIRE(shear.value().has_value());
    REQUIRE_THAT(shear.value()->si(), WithinRel(expectedSi, 1e-12));
}

TEST_CASE("MechanicalProperty_DerivesTheBulkModulusFromYoungsModulusAndPoissonRatio") {
    // Hand-computed: K = E / (3(1 - 2nu)) = 210 / (3 x 0.4) = 210 / 1.2 = 175 GPa exactly.
    const double expectedSi = 175.0e9;

    const MaterialProperty<ElasticModulus> bulk = materials::derivedBulkModulus(elasticFixture());
    REQUIRE(bulk.isDerived());
    REQUIRE(bulk.value().has_value());
    REQUIRE_THAT(bulk.value()->si(), WithinRel(expectedSi, 1e-12));
}

TEST_CASE("MechanicalProperty_DerivedModuliHaveModulusDimensionsAtCompileTime") {
    // A runtime number being right does not prove the type is. These do.
    using Derived = decltype(materials::derivedShearModulus(MechanicalProperties{}));
    static_assert(std::is_same_v<Derived::ValueType, ElasticModulus>);
    static_assert(std::is_same_v<decltype(materials::derivedBulkModulus(
                                     MechanicalProperties{}))::ValueType,
                                 ElasticModulus>);
    static_assert(std::is_same_v<ElasticModulus, Quantity<dimensions::pressure>>);
    static_assert(std::is_same_v<Stress, Quantity<dimensions::pressure>>);
    static_assert(!std::is_same_v<ElasticModulus, Density>);
    static_assert(!std::is_same_v<ElasticModulus, Energy>);
    static_assert(!std::is_same_v<ElasticModulus, double>);
    static_assert(std::is_same_v<decltype(MechanicalProperties{}.density)::ValueType, Density>);
    static_assert(
        std::is_same_v<decltype(MechanicalProperties{}.poissonRatio)::ValueType, PoissonRatio>);
    static_assert(
        std::is_same_v<decltype(MechanicalProperties{}.elongation)::ValueType, Elongation>);
    SUCCEED("compile-time dimensional checks held");
}

TEST_CASE("MechanicalProperty_CannotDeriveAModulusWithoutBothInputs") {
    const MechanicalProperties complete = elasticFixture();

    MechanicalProperties noModulus = complete;
    noModulus.youngsModulus = MaterialProperty<ElasticModulus>::unknown();
    REQUIRE(materials::derivedShearModulus(noModulus).isUnknown());
    REQUIRE(materials::derivedBulkModulus(noModulus).isUnknown());

    MechanicalProperties noRatio = complete;
    noRatio.poissonRatio = MaterialProperty<PoissonRatio>::unknown();
    REQUIRE(materials::derivedShearModulus(noRatio).isUnknown());
    REQUIRE(materials::derivedBulkModulus(noRatio).isUnknown());

    const MechanicalProperties neither;
    REQUIRE(materials::derivedShearModulus(neither).isUnknown());
    REQUIRE(materials::derivedBulkModulus(neither).isUnknown());

    // Unknown, not zero, and no nu = 0.3 appearing from nowhere.
    REQUIRE_FALSE(materials::derivedShearModulus(noRatio).value().has_value());
    REQUIRE_FALSE(materials::derivedBulkModulus(noModulus).value().has_value());
}

TEST_CASE("MechanicalProperty_AnInvalidInputNeverReachesADivision") {
    // nu = 0.5 makes 1 - 2nu zero, and nu = -1 makes 1 + nu zero. Both are
    // refused before the arithmetic rather than producing an infinity.
    for (const double value : {0.5, -1.0, 0.7, -1.3, kNaN, kInf}) {
        MechanicalProperties properties = elasticFixture();
        properties.poissonRatio = MaterialProperty<PoissonRatio>::known(PoissonRatio::of(value));
        INFO("nu = " << value);
        REQUIRE(materials::derivedShearModulus(properties).isUnknown());
        REQUIRE(materials::derivedBulkModulus(properties).isUnknown());
        REQUIRE_FALSE(materials::hasLinearElasticConstants(properties));
    }
    // And an E that is not usable does not produce a modulus either.
    for (const double si : {0.0, -210.0e9, kNaN, kInf}) {
        MechanicalProperties properties = elasticFixture();
        properties.youngsModulus =
            MaterialProperty<ElasticModulus>::known(ElasticModulus::fromSi(si));
        INFO("E si = " << si);
        REQUIRE(materials::derivedShearModulus(properties).isUnknown());
        REQUIRE(materials::derivedBulkModulus(properties).isUnknown());
    }
}

TEST_CASE("MechanicalProperty_DerivationIsRepeatableToTheBit") {
    const MechanicalProperties properties = elasticFixture();
    const double shear = materials::derivedShearModulus(properties).value()->si();
    const double bulk = materials::derivedBulkModulus(properties).value()->si();
    for (int pass = 0; pass < 20; ++pass) {
        // Bit-identical, not merely close: the same inputs through the same
        // arithmetic must not drift between calls.
        REQUIRE(materials::derivedShearModulus(properties).value()->si() == shear);
        REQUIRE(materials::derivedBulkModulus(properties).value()->si() == bulk);
    }
}

// --- what is stored and what is not ----------------------------------------

TEST_CASE("MechanicalProperty_HasNoSlotToStoreAShearOrBulkModulus") {
    // ADR-027: both are exactly determined by E and nu, so storing one would
    // create a second source of truth that could disagree with the first. The
    // absence of a slot is what makes the disagreement unrepresentable, so there
    // is nothing to reconcile and no supplied-versus-derived rule to get wrong.
    //
    // Structural, and checked as such: every kind the model names is stored
    // EXCEPT those two, and those two are the only ones marked derived.
    std::vector<MechanicalPropertyKind> derived;
    for (const MechanicalPropertyKind kind : materials::mechanicalPropertyKinds()) {
        if (materials::isDerivedKind(kind)) {
            derived.push_back(kind);
        }
    }
    REQUIRE(derived == std::vector<MechanicalPropertyKind>{MechanicalPropertyKind::ShearModulus,
                                                           MechanicalPropertyKind::BulkModulus});
}

TEST_CASE("MechanicalProperty_EnumeratesItsKindsInAFixedSemanticOrder") {
    const std::vector<MechanicalPropertyKind> expected{
        MechanicalPropertyKind::Density,
        MechanicalPropertyKind::YoungsModulus,
        MechanicalPropertyKind::PoissonRatio,
        MechanicalPropertyKind::ShearModulus,
        MechanicalPropertyKind::BulkModulus,
        MechanicalPropertyKind::YieldStrength,
        MechanicalPropertyKind::UltimateTensileStrength,
        MechanicalPropertyKind::UltimateCompressiveStrength,
        MechanicalPropertyKind::ShearStrength,
        MechanicalPropertyKind::Elongation,
        MechanicalPropertyKind::Hardness,
    };
    for (int pass = 0; pass < 10; ++pass) {
        const std::span<const MechanicalPropertyKind> kinds = materials::mechanicalPropertyKinds();
        REQUIRE(std::vector<MechanicalPropertyKind>(kinds.begin(), kinds.end()) == expected);
    }
    // Every kind names itself, so a diagnostic can never print a number alone.
    for (const MechanicalPropertyKind kind : expected) {
        REQUIRE_FALSE(materials::toString(kind).empty());
    }
}

TEST_CASE("MechanicalProperty_StrengthsAreIndependentAndNoneIsInferredFromAnother") {
    MechanicalProperties properties;
    properties.yieldStrength = MaterialProperty<Stress>::known(250_MPa);
    REQUIRE(materials::validate(properties));

    // Supplying a yield strength invents nothing else: no UTS from a ratio, no
    // compressive strength equal to the tensile one, no tau = 0.577 sigma_y.
    REQUIRE(properties.ultimateTensileStrength.isUnknown());
    REQUIRE(properties.ultimateCompressiveStrength.isUnknown());
    REQUIRE(properties.shearStrength.isUnknown());

    properties.ultimateTensileStrength = MaterialProperty<Stress>::known(400_MPa);
    REQUIRE(properties.ultimateCompressiveStrength.isUnknown());
    REQUIRE(properties.shearStrength.isUnknown());

    // A compressive strength unequal to the tensile one is ordinary, not an error.
    properties.ultimateCompressiveStrength = MaterialProperty<Stress>::known(900_MPa);
    REQUIRE(materials::validate(properties));
    REQUIRE(*properties.ultimateCompressiveStrength.value() != *properties.ultimateTensileStrength.value());
}

// --- consistency, reported and never corrected -----------------------------

TEST_CASE("MechanicalProperty_ReportsAnUltimateStrengthBelowTheYieldStrength") {
    MechanicalProperties properties;
    properties.yieldStrength = MaterialProperty<Stress>::known(400_MPa);
    properties.ultimateTensileStrength = MaterialProperty<Stress>::known(250_MPa);

    // Each value is individually valid, so the material is storable...
    REQUIRE(materials::validate(properties));
    // ...and the disagreement is reported separately.
    const std::vector<std::string> found = materials::mechanicalInconsistencies(properties);
    REQUIRE(found.size() == 1);
    REQUIRE_THAT(found.front(), ContainsSubstring("ultimate tensile strength"));
    REQUIRE_THAT(found.front(), ContainsSubstring("yield strength"));

    // And nothing was changed to make it consistent.
    REQUIRE(*properties.yieldStrength.value() == 400_MPa);
    REQUIRE(*properties.ultimateTensileStrength.value() == 250_MPa);
}

TEST_CASE("MechanicalProperty_AcceptsAnUltimateStrengthEqualToOrAboveTheYieldStrength") {
    MechanicalProperties above;
    above.yieldStrength = MaterialProperty<Stress>::known(250_MPa);
    above.ultimateTensileStrength = MaterialProperty<Stress>::known(400_MPa);
    REQUIRE(materials::mechanicalInconsistencies(above).empty());

    // Equal is allowed: a perfectly brittle material yields and breaks together.
    MechanicalProperties equal;
    equal.yieldStrength = MaterialProperty<Stress>::known(250_MPa);
    equal.ultimateTensileStrength = MaterialProperty<Stress>::known(250_MPa);
    REQUIRE(materials::mechanicalInconsistencies(equal).empty());

    // A partially entered material has nothing to disagree with.
    MechanicalProperties partial;
    partial.yieldStrength = MaterialProperty<Stress>::known(250_MPa);
    REQUIRE(materials::mechanicalInconsistencies(partial).empty());
    REQUIRE(materials::mechanicalInconsistencies(MechanicalProperties{}).empty());
}

// --- consumer requirements --------------------------------------------------

TEST_CASE("MechanicalProperty_AnswersWhatEachConsumerNeedsSeparately") {
    // Not one isComplete(). Complete for what?
    MechanicalProperties elasticOnly = elasticFixture();
    REQUIRE(materials::hasLinearElasticConstants(elasticOnly));
    REQUIRE_FALSE(materials::hasDensity(elasticOnly));

    MechanicalProperties densityOnly;
    densityOnly.density = MaterialProperty<Density>::known(Density::fromSi(7850.0));
    REQUIRE(materials::hasDensity(densityOnly));
    REQUIRE_FALSE(materials::hasLinearElasticConstants(densityOnly));

    MechanicalProperties both = elasticFixture();
    both.density = MaterialProperty<Density>::known(Density::fromSi(7850.0));
    REQUIRE(materials::hasLinearElasticConstants(both));
    REQUIRE(materials::hasDensity(both));

    // A density that is present but unusable is not a density.
    MechanicalProperties bad;
    bad.density = MaterialProperty<Density>::known(Density::fromSi(kNaN));
    REQUIRE_FALSE(materials::hasDensity(bad));
}

TEST_CASE("MechanicalProperty_APartiallyCharacterisedMaterialIsValidData") {
    // Density and E known, nu and yield unknown -- what most datasheets give.
    MechanicalProperties properties;
    properties.density = MaterialProperty<Density>::known(Density::fromSi(2700.0));
    properties.youngsModulus = MaterialProperty<ElasticModulus>::known(69_GPa);

    REQUIRE(materials::validate(properties));
    REQUIRE(materials::hasDensity(properties));
    // Not enough for elasticity, and it says so rather than filling the gap.
    REQUIRE_FALSE(materials::hasLinearElasticConstants(properties));
    REQUIRE(materials::derivedShearModulus(properties).isUnknown());
}

TEST_CASE("MechanicalProperty_RefusesAStoredPropertyThatClaimsToBeDerived") {
    // derived() is public, because the derivation functions are ordinary code.
    // That lets a caller build a Derived property and put it in a stored slot,
    // where it would be a value claiming to have been computed with nothing to
    // have computed it from -- a supplied value and a derived one becoming
    // indistinguishable, which is the thing this model exists to prevent.
    MechanicalProperties properties;
    properties.youngsModulus = MaterialProperty<ElasticModulus>::derived(210_GPa);

    const Result<void> valid = materials::validate(properties);
    REQUIRE_FALSE(valid);
    REQUIRE(valid.error().code == ErrorCode::InvalidArgument);
    REQUIRE_THAT(valid.error().message, ContainsSubstring("marked derived"));

    // Every stored slot, not just that one.
    MechanicalProperties each;
    each.density = MaterialProperty<Density>::derived(Density::fromSi(7850.0));
    REQUIRE_FALSE(materials::validate(each));
    MechanicalProperties ratio;
    ratio.poissonRatio = MaterialProperty<PoissonRatio>::derived(PoissonRatio::of(0.3));
    REQUIRE_FALSE(materials::validate(ratio));
    MechanicalProperties strength;
    strength.shearStrength = MaterialProperty<Stress>::derived(150_MPa);
    REQUIRE_FALSE(materials::validate(strength));
    MechanicalProperties stretch;
    stretch.elongation = MaterialProperty<Elongation>::derived(Elongation::of(0.1));
    REQUIRE_FALSE(materials::validate(stretch));
    MechanicalProperties hard;
    hard.hardness = MaterialProperty<Hardness>::derived(Hardness::of(60.0, HardnessScale::RockwellC));
    REQUIRE_FALSE(materials::validate(hard));

    // Known and Unknown are of course fine.
    MechanicalProperties ok;
    ok.youngsModulus = MaterialProperty<ElasticModulus>::known(210_GPa);
    REQUIRE(materials::validate(ok));
    REQUIRE(materials::validate(MechanicalProperties{}));
}

TEST_CASE("MechanicalProperty_ADerivedModulusIsStillMarkedDerivedWhereItIsProduced") {
    // The state is not merely a formality: what comes out of the derivation says
    // it was computed, so a consumer that cares can tell.
    MechanicalProperties properties;
    properties.youngsModulus = MaterialProperty<ElasticModulus>::known(210_GPa);
    properties.poissonRatio = MaterialProperty<PoissonRatio>::known(PoissonRatio::of(0.30));

    REQUIRE(materials::derivedShearModulus(properties).isDerived());
    REQUIRE(materials::derivedBulkModulus(properties).isDerived());
    REQUIRE_FALSE(materials::derivedShearModulus(properties).isKnown());
    REQUIRE_FALSE(materials::derivedBulkModulus(properties).isKnown());
}
