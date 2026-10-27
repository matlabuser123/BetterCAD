// P19-FLUID-001: P15 learns a dynamic viscosity and an incompressible-flow
// consumer (ADR-062).
//
// THE PROPERTY IS P15's, NOT CFD's. Every claim here is about the material model
// alone -- validation at entry, the requirement function and its diagnostics, the
// completeness table, provenance, removal and the file -- because the point of
// putting the viscosity in P15 is that it obeys every rule every other property
// already obeys. The CFD side (tests/cfd/CfdFluidTests.cpp) only reads it.

#include "TestHelpers.hpp"

#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/materials/Completeness.hpp>
#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/core/materials/Provenance.hpp>
#include <bettercad/core/materials/ThermalProperties.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/io/DocumentFile.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinRel;
using features::MaterialDefinition;
using materials::CompletenessState;
using materials::ConsumerKind;
using materials::MaterialProperty;
using materials::MechanicalPropertyKind;
using materials::ThermalPropertyKind;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

/// A fluid: a density and a viscosity, and nothing thermal.
MaterialDefinition water() {
    MaterialDefinition definition;
    definition.designation = "Water";
    definition.mechanical.density = MaterialProperty<Density>::known(998_kg_per_m3);
    definition.thermal.dynamicViscosity = MaterialProperty<DynamicViscosity>::known(1.002_mPa_s);
    return definition;
}

MaterialDefinition withViscosity(double pascalSeconds) {
    MaterialDefinition definition = water();
    definition.thermal.dynamicViscosity =
        MaterialProperty<DynamicViscosity>::known(DynamicViscosity::fromSi(pascalSeconds));
    return definition;
}

MaterialId createOrFail(Document& document, std::string name, const MaterialDefinition& definition) {
    const Result<MaterialId> id = features::createMaterial(document, std::move(name), definition);
    INFO((id.has_value() ? std::string{} : id.error().message));
    REQUIRE(id.has_value());
    return *id;
}

materials::CompletenessReport reportFor(const Document& document, MaterialId id, ConsumerKind consumer) {
    const Result<materials::CompletenessReport> report = features::materialCompleteness(document, id, consumer);
    REQUIRE(report.has_value());
    return *report;
}

} // namespace

TEST_CASE("FluidProperty_TheViscosityIsDynamicInPascalSecondsAndTheKinematicIsDerived", "[features][fluid]") {
    // mu is a DYNAMIC viscosity, Pa s = kg/(m s); nu = mu / rho is m^2/s and has
    // no field anywhere (ADR-027, ADR-058).
    static_assert(std::is_same_v<decltype(materials::ThermalProperties{}.dynamicViscosity)::ValueType,
                                 DynamicViscosity>);
    static_assert(!std::is_same_v<DynamicViscosity, KinematicViscosity>);
    static_assert(std::is_same_v<decltype(DynamicViscosity{} / Density{}), KinematicViscosity>);
    static_assert(std::is_same_v<DynamicViscosity, Quantity<dimensions::pressure * dimensions::time>>);

    CHECK_THAT((1.002_mPa_s).si(), WithinRel(1.002e-3, 1e-15));
    CHECK(DynamicViscosity::fromSi(1.0e-3) == 1_mPa_s);
    const KinematicViscosity nu = (1.002_mPa_s) / (998_kg_per_m3);
    CHECK_THAT(nu.si(), WithinRel(1.002e-3 / 998.0, 1e-15));

    CHECK(materials::toString(ThermalPropertyKind::DynamicViscosity) == "dynamic viscosity");
    CHECK(materials::toString(ConsumerKind::IncompressibleFlow) == "incompressible flow");
}

TEST_CASE("FluidProperty_AViscosityIsRefusedAtEntryUnlessFiniteAndPositive", "[features][fluid]") {
    // The same rule every positive property obeys, at the same gate, so an unusable
    // viscosity cannot be IN a document for any consumer to meet.
    Document document{"Part"};
    for (const double bad : {0.0, -1.0e-3, kNaN, kInf, -kInf}) {
        INFO("mu = " << bad);
        const Result<MaterialId> refused = features::createMaterial(document, "Bad", withViscosity(bad));
        REQUIRE_FALSE(refused.has_value());
        CHECK_THAT(refused.error().message, ContainsSubstring("dynamic viscosity"));
    }
    CHECK(features::materialCount(document) == 0);

    // A Derived viscosity is refused too: a stored property is supplied or unknown.
    MaterialDefinition derived = water();
    derived.thermal.dynamicViscosity = MaterialProperty<DynamicViscosity>::derived(1_mPa_s);
    CHECK_FALSE(features::createMaterial(document, "Derived", derived).has_value());

    // Any positive value is accepted: no plausibility bound, by P15's policy
    // (a gas and a polymer melt differ by ten orders of magnitude).
    int index = 0;
    for (const double good : {1.8e-5, 1.0e-3, 1.0e3}) {
        INFO("mu = " << good);
        CHECK(features::createMaterial(document, "Good" + std::to_string(index++), withViscosity(good)).has_value());
    }

    // And an edit through the thermal setter is held to the same rule, changing
    // nothing when it refuses.
    const MaterialId id = createOrFail(document, "Edited", water());
    const std::uint64_t revision = features::findMaterial(document, id)->revision();
    materials::ThermalProperties thermal = features::findMaterial(document, id)->definition().thermal;
    thermal.dynamicViscosity = MaterialProperty<DynamicViscosity>::known(DynamicViscosity::fromSi(-2.0));
    CHECK_FALSE(features::setMaterialThermal(document, id, thermal).has_value());
    CHECK(features::findMaterial(document, id)->revision() == revision);
    CHECK(features::findMaterial(document, id)->definition() == water());
}

TEST_CASE("FluidProperty_RequireDynamicViscosityNamesTheMaterialAndNeverDefaults", "[features][fluid]") {
    Document document{"Part"};
    const MaterialId fluid = createOrFail(document, "Water", water());
    const Result<DynamicViscosity> mu = features::requireDynamicViscosity(document, fluid);
    REQUIRE(mu.has_value());
    CHECK(*mu == 1.002_mPa_s);

    MaterialDefinition solid;
    solid.designation = "Steel";
    solid.mechanical.density = MaterialProperty<Density>::known(7850_kg_per_m3);
    const MaterialId steel = createOrFail(document, "Steel", solid);
    const Result<DynamicViscosity> missing = features::requireDynamicViscosity(document, steel);
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().code == ErrorCode::FailedPrecondition);
    CHECK_THAT(missing.error().message, ContainsSubstring("Steel ("));
    CHECK_THAT(missing.error().message, ContainsSubstring("has no dynamic viscosity"));

    const Result<DynamicViscosity> absent = features::requireDynamicViscosity(document, MaterialId::fromValue(999999));
    REQUIRE_FALSE(absent.has_value());
    CHECK(absent.error().code == ErrorCode::NotFound);
}

TEST_CASE("FluidProperty_IncompressibleFlowNamesEveryGapAtOnce", "[features][fluid]") {
    // One diagnostic with the whole list, in P15's order (density, then viscosity),
    // so a user fixes the material once rather than one run at a time.
    Document document{"Part"};

    const MaterialId empty = createOrFail(document, "Empty", MaterialDefinition{});
    const auto both = features::requireIncompressibleFlowProperties(document, empty);
    REQUIRE_FALSE(both.has_value());
    CHECK_THAT(both.error().message,
               ContainsSubstring("has no density and no dynamic viscosity, which incompressible flow needs"));

    MaterialDefinition densityOnly;
    densityOnly.mechanical.density = MaterialProperty<Density>::known(998_kg_per_m3);
    const auto noViscosity =
        features::requireIncompressibleFlowProperties(document, createOrFail(document, "DensityOnly", densityOnly));
    REQUIRE_FALSE(noViscosity.has_value());
    CHECK_THAT(noViscosity.error().message, ContainsSubstring("has no dynamic viscosity, which incompressible"));
    CHECK_THAT(noViscosity.error().message, !ContainsSubstring("no density"));

    MaterialDefinition viscosityOnly;
    viscosityOnly.thermal.dynamicViscosity = MaterialProperty<DynamicViscosity>::known(1_mPa_s);
    const auto noDensity =
        features::requireIncompressibleFlowProperties(document, createOrFail(document, "ViscosityOnly", viscosityOnly));
    REQUIRE_FALSE(noDensity.has_value());
    CHECK_THAT(noDensity.error().message, ContainsSubstring("has no density, which incompressible"));

    const auto ready = features::requireIncompressibleFlowProperties(document, createOrFail(document, "Water", water()));
    REQUIRE(ready.has_value());
    CHECK(ready->density == 998_kg_per_m3);
    CHECK(ready->dynamicViscosity == 1.002_mPa_s);
    // Two fields and nothing else: no stored kinematic viscosity, no default.
    struct Permitted {
        Density density;
        DynamicViscosity dynamicViscosity;
    };
    static_assert(sizeof(features::IncompressibleFlowProperties) == sizeof(Permitted));
}

TEST_CASE("FluidProperty_TheFlowConsumerRequiresDensityAndViscosityAndNothingThermal", "[features][fluid]") {
    Document document{"Part"};

    // A fluid with only rho and mu is Ready for a flow -- no conductivity, no
    // specific heat: the v1 fluid is isothermal.
    const MaterialId fluid = createOrFail(document, "Water", water());
    CHECK(reportFor(document, fluid, ConsumerKind::IncompressibleFlow).state == CompletenessState::Ready);
    // And a conduction consumer, asked of the same material, is not: a viscosity
    // does not stand in for a conductivity.
    CHECK(reportFor(document, fluid, ConsumerKind::ThermalSteady).state == CompletenessState::Incomplete);

    // A thoroughly characterised SOLID with no viscosity is incomplete for a flow,
    // and the report names exactly the viscosity -- not its density, which it has.
    MaterialDefinition aluminium;
    aluminium.mechanical.density = MaterialProperty<Density>::known(2700_kg_per_m3);
    aluminium.mechanical.youngsModulus = MaterialProperty<ElasticModulus>::known(69_GPa);
    aluminium.mechanical.poissonRatio = MaterialProperty<PoissonRatio>::known(PoissonRatio::of(0.33));
    aluminium.thermal.thermalConductivity = MaterialProperty<ThermalConductivity>::known(167_W_per_m_K);
    aluminium.thermal.specificHeatCapacity = MaterialProperty<SpecificHeatCapacity>::known(896_J_per_kg_K);
    const MaterialId solid = createOrFail(document, "Aluminium", aluminium);
    const materials::CompletenessReport report = reportFor(document, solid, ConsumerKind::IncompressibleFlow);
    CHECK(report.state == CompletenessState::Incomplete);
    CHECK(report.missingMechanical.empty());
    CHECK(report.missingThermal == std::vector{ThermalPropertyKind::DynamicViscosity});
}

TEST_CASE("FluidProperty_AViscosityEditDropsItsOwnProvenanceAndNoOther", "[features][fluid]") {
    // A citation describes the number it was recorded against (P15-PROV-001), so a
    // changed viscosity loses its citation -- and only its own.
    Document document{"Part"};
    MaterialDefinition definition = water();
    definition.thermal.thermalConductivity = MaterialProperty<ThermalConductivity>::known(0.6_W_per_m_K);
    const MaterialId id = createOrFail(document, "Water", definition);
    materials::PropertyProvenance cited;
    cited.kind = materials::SourceKind::Handbook;
    cited.source = "fixture handbook";
    REQUIRE(features::setMaterialPropertyProvenance(document, id, ThermalPropertyKind::DynamicViscosity, cited)
                .has_value());
    REQUIRE(features::setMaterialPropertyProvenance(document, id, ThermalPropertyKind::ThermalConductivity, cited)
                .has_value());

    materials::ThermalProperties thermal = features::findMaterial(document, id)->definition().thermal;
    thermal.dynamicViscosity = MaterialProperty<DynamicViscosity>::known(1.1_mPa_s);
    REQUIRE(features::setMaterialThermal(document, id, thermal).has_value());

    const materials::MaterialProvenance& provenance = features::findMaterial(document, id)->definition().provenance;
    CHECK_FALSE(provenance.thermal.contains(ThermalPropertyKind::DynamicViscosity));
    CHECK(provenance.thermal.contains(ThermalPropertyKind::ThermalConductivity));
}

TEST_CASE("FluidProperty_AnEditOfAnotherPropertyKeepsTheViscositysCitation", "[features][fluid]") {
    // The converse of the case above: only a CHANGED value loses its citation, so
    // a conductivity edit must leave the viscosity's citation where it was.
    Document document{"Part"};
    MaterialDefinition definition = water();
    definition.thermal.thermalConductivity = MaterialProperty<ThermalConductivity>::known(0.6_W_per_m_K);
    const MaterialId id = createOrFail(document, "Water", definition);
    materials::PropertyProvenance cited;
    cited.kind = materials::SourceKind::Measured;
    cited.source = "fixture viscometer";
    REQUIRE(features::setMaterialPropertyProvenance(document, id, ThermalPropertyKind::DynamicViscosity, cited)
                .has_value());

    materials::ThermalProperties thermal = features::findMaterial(document, id)->definition().thermal;
    thermal.thermalConductivity = MaterialProperty<ThermalConductivity>::known(0.61_W_per_m_K);
    REQUIRE(features::setMaterialThermal(document, id, thermal).has_value());

    const materials::MaterialProvenance& provenance = features::findMaterial(document, id)->definition().provenance;
    REQUIRE(provenance.thermal.contains(ThermalPropertyKind::DynamicViscosity));
    CHECK(provenance.thermal.at(ThermalPropertyKind::DynamicViscosity).source == "fixture viscometer");
}

TEST_CASE("FluidProperty_AReferenceTemperatureIsRecordedAndChecked", "[features][fluid]") {
    // A viscosity is strongly temperature-dependent, so the temperature it was
    // measured at is worth recording -- as provenance, never as a law (ADR-027).
    // Recorded, it is checked like every other property's: an absolute temperature.
    Document document{"Part"};
    MaterialDefinition measured = water();
    measured.thermal.dynamicViscosity = MaterialProperty<DynamicViscosity>::known(1.002_mPa_s, 293.15_K);
    const MaterialId id = createOrFail(document, "Measured", measured);
    CHECK(features::findMaterial(document, id)->definition().thermal.dynamicViscosity.referenceTemperature() ==
          293.15_K);

    MaterialDefinition belowZero = water();
    belowZero.thermal.dynamicViscosity =
        MaterialProperty<DynamicViscosity>::known(1.002_mPa_s, Temperature::fromSi(-1.0));
    const Result<MaterialId> refused = features::createMaterial(document, "BelowZero", belowZero);
    REQUIRE_FALSE(refused.has_value());
    CHECK_THAT(refused.error().message, ContainsSubstring("dynamic viscosity"));
}

TEST_CASE("FluidProperty_HasDynamicViscosityAnswersOnlyForAUsableValue", "[features][fluid]") {
    materials::ThermalProperties properties;
    CHECK_FALSE(materials::hasDynamicViscosity(properties));
    properties.dynamicViscosity = MaterialProperty<DynamicViscosity>::known(1_mPa_s);
    CHECK(materials::hasDynamicViscosity(properties));
    // The predicate does not trust its input to have passed validation.
    for (const double bad : {0.0, -1.0, kNaN, kInf}) {
        properties.dynamicViscosity = MaterialProperty<DynamicViscosity>::known(DynamicViscosity::fromSi(bad));
        CHECK_FALSE(materials::hasDynamicViscosity(properties));
    }
}

TEST_CASE("FluidProperty_RemovingTheViscosityMakesItUnknownNotZero", "[features][fluid]") {
    Document document{"Part"};
    const MaterialId id = createOrFail(document, "Water", water());
    CHECK(features::hasMaterialProperty(document, id, ThermalPropertyKind::DynamicViscosity));
    REQUIRE(features::removeMaterialProperty(document, id, ThermalPropertyKind::DynamicViscosity));
    CHECK_FALSE(features::hasMaterialProperty(document, id, ThermalPropertyKind::DynamicViscosity));
    const MaterialProperty<DynamicViscosity>& mu =
        features::findMaterial(document, id)->definition().thermal.dynamicViscosity;
    CHECK(mu.isUnknown());
    CHECK_FALSE(mu.value().has_value());
    // The density is untouched: one half of the material does not disturb the other.
    CHECK(features::findMaterial(document, id)->definition().mechanical.density.value() == 998_kg_per_m3);
}

TEST_CASE("FluidProperty_TheFileStoresTheViscosityInSiAndOlderFilesStillLoad", "[features][fluid][persistence]") {
    // In Pa s, under its own key, read back to the same value -- and a file written
    // before P19-FLUID-001 loads unchanged, with the viscosity Unknown, because an
    // absent key means Unknown and no format version moved.
    Document document{"Part"};
    const MaterialId id = createOrFail(document, "Water", withViscosity(0.00125));
    const Result<std::string> json = io::documentToJson(document);
    REQUIRE(json.has_value());
    CHECK_THAT(*json, ContainsSubstring("\"dynamic_viscosity\""));
    CHECK_THAT(*json, ContainsSubstring("0.00125"));
    CHECK_THAT(*json, !ContainsSubstring("kinematic"));

    Result<Document> loaded = io::documentFromJson(*json);
    REQUIRE(loaded.has_value());
    REQUIRE(features::findMaterial(*loaded, id) != nullptr);
    CHECK(features::findMaterial(*loaded, id)->definition() == features::findMaterial(document, id)->definition());

    // A hand-edited file with an unusable viscosity is refused at load, by the same
    // validation a direct entry meets.
    std::string tampered = *json;
    tampered.replace(tampered.find("0.00125"), 7, "-0.00125");
    const Result<Document> refused = io::documentFromJson(tampered);
    REQUIRE_FALSE(refused.has_value());
    CHECK_THAT(refused.error().message, ContainsSubstring("dynamic viscosity"));

    // A reference document saved before this milestone.
    const Result<Document> older =
        io::loadDocument(std::filesystem::path{BETTERCAD_EXAMPLE_MODELS_DIR} / "thermal_plate.bcad");
    INFO((older.has_value() ? std::string{} : older.error().message));
    REQUIRE(older.has_value());
    const std::vector<MaterialId> materials = features::materialIds(*older);
    REQUIRE_FALSE(materials.empty());
    for (const MaterialId material : materials) {
        CHECK(features::findMaterial(*older, material)->definition().thermal.dynamicViscosity.isUnknown());
    }
}
