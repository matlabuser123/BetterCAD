#include "features/FeatureTestSupport.hpp"
#include "support/TestFiles.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/materials/Completeness.hpp>
#include <bettercad/core/materials/MaterialLibrary.hpp>
#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/core/materials/Provenance.hpp>
#include <bettercad/core/materials/ThermalProperties.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/MassProperties.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <memory>
#include <string>
#include <fstream>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinRel;
using features::MaterialDefinition;
using materials::Date;
using materials::HardnessScale;
using materials::MaterialProperty;
using materials::MaterialProvenance;
using materials::MechanicalPropertyKind;
using materials::PropertyProvenance;
using materials::SourceKind;
using materials::ThermalPropertyKind;

// P15-PERSIST-001: the persisted form of canonical material engineering intent.
//
// THE RULE EVERY TEST HERE SERVES: save canonical intent, load canonical intent,
// recompute derived state. Never save a mass, an inertia, a completeness report or an
// effective material and trust it after load.
//
// THESE ARE REAL ROUND TRIPS through io::documentToJson / documentFromJson, not
// field-level unit tests of a mapping function. A schema that looks right and does not
// survive the document APIs is worth nothing.
//
// FIXTURE VALUES ARE SYNTHETIC and none is added to the shipping library, which still
// carries metadata only because ADR-028 requires a recorded source for any number.
// Dates are fixed literals; nothing here reads a clock.

namespace {

Date fixedDate(int year, int month, int day) {
    const std::optional<Date> date = Date::of(year, month, day);
    REQUIRE(date.has_value());
    return *date;
}

PropertyProvenance measured() {
    PropertyProvenance provenance;
    provenance.kind = SourceKind::Measured;
    provenance.source = "Synthetic test report";
    provenance.reference = "Specimen 3";
    provenance.revision = "Rev A";
    provenance.condition = "as-rolled";
    provenance.notes = "Three specimens.";
    provenance.date = fixedDate(2024, 3, 17);
    return provenance;
}

PropertyProvenance handbook() {
    PropertyProvenance provenance;
    provenance.kind = SourceKind::Handbook;
    provenance.source = "Synthetic handbook";
    provenance.reference = "p. 212";
    return provenance;
}

/// Every stored property Known, mixed provenance, an origin key, and a reference
/// temperature on two properties. Synthetic values throughout.
MaterialDefinition richSteel() {
    MaterialDefinition definition;
    definition.designation = "Steel S235JR";
    definition.standard = "EN 10025-2";
    definition.family = "Carbon Steel";
    definition.notes = "Synthetic fixture, not measurements.";
    definition.origin = materials::MaterialLibraryKey{"bettercad", "steel-s235jr", 1};
    definition.mechanical.density =
        MaterialProperty<Density>::known(Density::fromSi(7850.0), 293.15_K);
    definition.mechanical.youngsModulus = MaterialProperty<ElasticModulus>::known(210_GPa);
    definition.mechanical.poissonRatio =
        MaterialProperty<PoissonRatio>::known(PoissonRatio::of(0.30));
    definition.mechanical.yieldStrength = MaterialProperty<Stress>::known(235_MPa);
    definition.mechanical.ultimateTensileStrength = MaterialProperty<Stress>::known(360_MPa);
    definition.mechanical.ultimateCompressiveStrength = MaterialProperty<Stress>::known(350_MPa);
    definition.mechanical.shearStrength = MaterialProperty<Stress>::known(140_MPa);
    definition.mechanical.elongation =
        MaterialProperty<materials::Elongation>::known(materials::Elongation::ofPercent(26.0));
    definition.mechanical.hardness = MaterialProperty<materials::Hardness>::known(
        materials::Hardness::of(60.0, HardnessScale::RockwellC));
    definition.thermal.thermalConductivity =
        MaterialProperty<ThermalConductivity>::known(50_W_per_m_K, 373.15_K);
    definition.thermal.specificHeatCapacity =
        MaterialProperty<SpecificHeatCapacity>::known(460_J_per_kg_K);
    definition.thermal.thermalExpansion =
        MaterialProperty<ThermalExpansionCoefficient>::known(12.0_um_per_m_K);
    definition.thermal.meltingTemperature = MaterialProperty<Temperature>::known(1811.0_K);
    definition.thermal.electricalResistivity =
        MaterialProperty<materials::ElectricalResistivity>::known(
            materials::ElectricalResistivity::ofOhmMetres(1.43e-7));
    definition.provenance.material = handbook();
    definition.provenance.mechanical[MechanicalPropertyKind::Density] = measured();
    definition.provenance.mechanical[MechanicalPropertyKind::YoungsModulus] = handbook();
    definition.provenance.thermal[ThermalPropertyKind::ThermalConductivity] = measured();
    return definition;
}

MaterialId createOrFail(Document& document, std::string name, const MaterialDefinition& definition) {
    const Result<MaterialId> id = features::createMaterial(document, std::move(name), definition);
    REQUIRE(id);
    return *id;
}

const MaterialDefinition& definitionOf(const Document& document, MaterialId id) {
    const features::Material* material = features::findMaterial(document, id);
    REQUIRE(material != nullptr);
    return material->definition();
}

/// Saves to text and loads back, requiring both to succeed.
Document roundTrip(const Document& document) {
    const Result<std::string> text = io::documentToJson(document);
    INFO((text ? std::string{} : text.error().message));
    REQUIRE(text.has_value());
    Result<Document> loaded = io::documentFromJson(*text);
    INFO((loaded ? std::string{} : loaded.error().message));
    REQUIRE(loaded.has_value());
    return std::move(*loaded);
}

std::string jsonOf(const Document& document) {
    const Result<std::string> text = io::documentToJson(document);
    REQUIRE(text.has_value());
    return *text;
}

/// A document with one extruded 20 x 30 x 50 mm box = 30000 mm^3.
struct BoxPart {
    Document document{"Part"};
    features::Regenerator regenerator;
    ObjectId feature{};

    BoxPart() {
        auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
        addRectangle(*sketch, 0_mm, 0_mm, 20_mm, 30_mm);
        const ObjectId profile = require(document.addObject(std::move(sketch)));
        auto extrude = features::ExtrudeFeature::create(
            "Solid", {.profile = SketchId::fromValue(profile.value()), .depth = 50_mm});
        REQUIRE(extrude.has_value());
        feature = require(document.addObject(std::move(*extrude)));
        requireReport(regenerator, document);
    }
};

double massOf(Document& document, ObjectId feature) {
    features::Regenerator regenerator;
    requireReport(regenerator, document);
    const Result<features::PartMassProperties> mass =
        features::partMassProperties(document, regenerator, feature);
    REQUIRE(mass);
    return mass->mass.in(units::kg);
}

} // namespace

// --- identity and the allocator ---------------------------------------------

TEST_CASE("MaterialFile_RoundTripsTheMaterialIdExactly", "[io][persist]") {
    Document document{"Part"};
    // Create three so the ID under test is not simply the first.
    createOrFail(document, "First", {});
    createOrFail(document, "Second", {});
    const MaterialId id = createOrFail(document, "Steel", richSteel());
    const std::uint64_t value = id.value();

    const Document loaded = roundTrip(document);
    REQUIRE(features::findMaterial(loaded, id) != nullptr);
    CHECK(features::findMaterial(loaded, id)->materialId() == id);
    CHECK(features::findMaterial(loaded, id)->materialId().value() == value);
    CHECK(features::findMaterial(loaded, id)->name() == "Steel");
    // Ascending ID order, and the same three IDs.
    CHECK(features::materialIds(loaded) == features::materialIds(document));
}

TEST_CASE("MaterialFile_TheAllocatorNeverReissuesALoadedId", "[io][persist]") {
    Document document{"Part"};
    const MaterialId a = createOrFail(document, "A", {});
    const MaterialId b = createOrFail(document, "B", {});
    const MaterialId c = createOrFail(document, "C", {});
    // Delete the middle one, so the file's IDs are not contiguous and the highest
    // allocated ID is above every ID present.
    REQUIRE(features::removeMaterial(document, b));

    Document loaded = roundTrip(document);
    CHECK(features::findMaterial(loaded, a) != nullptr);
    CHECK(features::findMaterial(loaded, b) == nullptr);
    CHECK(features::findMaterial(loaded, c) != nullptr);

    // A new material must collide with nothing -- not the deleted ID either, because
    // the allocator only counts up (P15-MAT-001).
    const MaterialId next = createOrFail(loaded, "D", {});
    CHECK(next != a);
    CHECK(next != b);
    CHECK(next != c);
    CHECK(next.value() > c.value());
    CHECK(features::materialCount(loaded) == 3);
}

TEST_CASE("MaterialFile_RejectsTwoMaterialsSharingOneId", "[io][persist]") {
    // Hand-built malformed file: the same object id twice. No last-one-wins, no
    // merge, no silent overwrite.
    Document document{"Part"};
    createOrFail(document, "Steel", richSteel());
    std::string text = jsonOf(document);
    const std::string entry = R"({
      "id": )" + std::to_string(features::materialIds(document).front().value()) + R"(,
      "type": "material",
      "name": "Duplicate",
      "data": {}
    })";
    // Insert a second object with the same id.
    const std::size_t at = text.find("\"objects\": [");
    REQUIRE(at != std::string::npos);
    const std::size_t open = text.find('[', at);
    text.insert(open + 1, entry + ",");

    const Result<Document> loaded = io::documentFromJson(text);
    REQUIRE_FALSE(loaded);
    CHECK_THAT(loaded.error().message, ContainsSubstring("already in use"));
}

// --- metadata and properties ------------------------------------------------

TEST_CASE("MaterialFile_RoundTripsEveryFieldOfARichMaterial", "[io][persist]") {
    Document document{"Part"};
    const MaterialId id = createOrFail(document, "Steel", richSteel());
    const Document loaded = roundTrip(document);

    // The whole definition, compared as one value -- so a field added later and not
    // persisted fails here rather than passing a list of spot checks.
    CHECK(definitionOf(loaded, id) == richSteel());

    // And then the pieces that are easy to lose, named explicitly.
    const MaterialDefinition& after = definitionOf(loaded, id);
    CHECK(after.designation == "Steel S235JR");
    CHECK(after.standard == "EN 10025-2");
    CHECK(after.family == "Carbon Steel");
    CHECK_THAT(after.notes, ContainsSubstring("Synthetic"));
    REQUIRE(after.origin.has_value());
    CHECK(after.origin->library == "bettercad");
    CHECK(after.origin->entry == "steel-s235jr");
    CHECK(after.origin->revision == 1);
}

TEST_CASE("MaterialFile_KeepsHardnessValueAndScaleTogether", "[io][persist]") {
    // 60 HRC must not load as 60 HBW or as a bare 60.
    Document document{"Part"};
    MaterialDefinition definition;
    definition.mechanical.hardness = MaterialProperty<materials::Hardness>::known(
        materials::Hardness::of(60.0, HardnessScale::RockwellC));
    const MaterialId id = createOrFail(document, "Tool", definition);

    const Document loaded = roundTrip(document);
    const auto hardness = definitionOf(loaded, id).mechanical.hardness.value();
    REQUIRE(hardness.has_value());
    CHECK(hardness->value() == 60.0);
    CHECK(hardness->scale() == HardnessScale::RockwellC);
    CHECK_FALSE(hardness->scale() == HardnessScale::Brinell);
    // Each scale survives on its own, so the mapping is not accidentally constant.
    for (const HardnessScale scale : {HardnessScale::Brinell, HardnessScale::Vickers,
                                      HardnessScale::RockwellB, HardnessScale::RockwellC}) {
        Document one{"Part"};
        MaterialDefinition each;
        each.mechanical.hardness =
            MaterialProperty<materials::Hardness>::known(materials::Hardness::of(42.0, scale));
        const MaterialId only = createOrFail(one, "M", each);
        CHECK(definitionOf(roundTrip(one), only).mechanical.hardness.value()->scale() == scale);
    }
}

TEST_CASE("MaterialFile_KeepsAbsoluteTemperaturesAbsolute", "[io][persist]") {
    // A melting temperature and a reference temperature are ABSOLUTE, in kelvin. If
    // either were read back as an interval the number would survive and the meaning
    // would not, so the values chosen are ones an interval reading would make absurd.
    Document document{"Part"};
    MaterialDefinition definition;
    definition.thermal.meltingTemperature = MaterialProperty<Temperature>::known(1811.0_K);
    definition.mechanical.density =
        MaterialProperty<Density>::known(Density::fromSi(7850.0), 293.15_K);
    const MaterialId id = createOrFail(document, "Steel", definition);

    const Document loaded = roundTrip(document);
    const MaterialDefinition& after = definitionOf(loaded, id);
    CHECK_THAT(after.thermal.meltingTemperature.value()->in(units::K), WithinRel(1811.0, 1e-12));
    REQUIRE(after.mechanical.density.referenceTemperature().has_value());
    CHECK_THAT(after.mechanical.density.referenceTemperature()->in(units::K),
               WithinRel(293.15, 1e-12));
    // 293.15 K is 20 C; an interval reading would make it 293.15 above absolute zero
    // relative to nothing, and the round trip would not preserve the kelvin value.
    CHECK(after.mechanical.density.referenceTemperature()->si() == 293.15);
}

TEST_CASE("MaterialFile_KeepsElectricalResistivityInItsOwnType", "[io][persist]") {
    Document document{"Part"};
    MaterialDefinition definition;
    definition.thermal.electricalResistivity =
        MaterialProperty<materials::ElectricalResistivity>::known(
            materials::ElectricalResistivity::ofOhmMetres(1.43e-7));
    const MaterialId id = createOrFail(document, "Steel", definition);

    const auto value = definitionOf(roundTrip(document), id).thermal.electricalResistivity.value();
    REQUIRE(value.has_value());
    CHECK_THAT(value->ohmMetres(), WithinRel(1.43e-7, 1e-15));
    static_assert(
        std::is_same_v<decltype(*value), const materials::ElectricalResistivity&> ||
        std::is_same_v<decltype(value), std::optional<materials::ElectricalResistivity>>);
}

TEST_CASE("MaterialFile_PreservesTheExactKnownAndUnknownPattern", "[io][persist]") {
    // density known, E unknown, yield unknown, k known, cp unknown.
    Document document{"Part"};
    MaterialDefinition definition;
    definition.designation = "Partly Characterised";
    definition.mechanical.density = MaterialProperty<Density>::known(Density::fromSi(2700.0));
    definition.thermal.thermalConductivity =
        MaterialProperty<ThermalConductivity>::known(167_W_per_m_K);
    const MaterialId id = createOrFail(document, "Partial", definition);

    const Document loaded = roundTrip(document);
    const MaterialDefinition& after = definitionOf(loaded, id);
    CHECK(after == definition);
    // Unknown is UNKNOWN, not zero and not a default.
    CHECK(after.mechanical.youngsModulus.isUnknown());
    CHECK_FALSE(after.mechanical.youngsModulus.value().has_value());
    CHECK(after.mechanical.yieldStrength.isUnknown());
    CHECK(after.thermal.specificHeatCapacity.isUnknown());
    CHECK_FALSE(after.thermal.specificHeatCapacity.value().has_value());
    // The whole pattern, over both enumerations.
    for (const MechanicalPropertyKind kind : materials::mechanicalPropertyKinds()) {
        CHECK(features::hasMaterialProperty(loaded, id, kind) ==
              features::hasMaterialProperty(document, id, kind));
    }
    for (const ThermalPropertyKind kind : materials::thermalPropertyKinds()) {
        CHECK(features::hasMaterialProperty(loaded, id, kind) ==
              features::hasMaterialProperty(document, id, kind));
    }
    // An unknown property is ABSENT from the file, which is the one representation
    // that cannot be misread as a measurement.
    const std::string text = jsonOf(document);
    CHECK_THAT(text, ContainsSubstring("density"));
    CHECK_THAT(text, !ContainsSubstring("youngs_modulus"));
    CHECK_THAT(text, !ContainsSubstring("yield_strength"));
}

TEST_CASE("MaterialFile_HasOneCanonicalDensityThatBothConsumersRead", "[io][persist]") {
    // Density lives in the mechanical properties and thermal consumers read that
    // same value (P15-THERM-001). Two persisted copies could be made to disagree by
    // a hand edit, so there is only one.
    Document document{"Part"};
    MaterialDefinition definition;
    definition.mechanical.density = MaterialProperty<Density>::known(Density::fromSi(7850.0));
    definition.thermal.thermalConductivity =
        MaterialProperty<ThermalConductivity>::known(50_W_per_m_K);
    definition.thermal.specificHeatCapacity =
        MaterialProperty<SpecificHeatCapacity>::known(460_J_per_kg_K);
    const MaterialId id = createOrFail(document, "Steel", definition);

    const std::string text = jsonOf(document);
    // Exactly one "density" key in the whole file.
    std::size_t count = 0;
    for (std::size_t at = text.find("\"density\""); at != std::string::npos;
         at = text.find("\"density\"", at + 1)) {
        ++count;
    }
    CHECK(count == 1);

    const Document loaded = roundTrip(document);
    const Result<Density> mechanical = features::requireDensity(loaded, id);
    REQUIRE(mechanical);
    const Result<features::TransientConductionProperties> thermal =
        features::requireTransientConductionProperties(loaded, id);
    REQUIRE(thermal);
    // Both resolve the same canonical value.
    CHECK(thermal->density.si() == mechanical->si());
    CHECK_THAT(mechanical->in(units::kg_per_m3), WithinRel(7850.0, 1e-12));
}

TEST_CASE("MaterialFile_PersistsSiValuesRegardlessOfAnyDisplayUnit", "[io][persist]") {
    // E = 210 GPa is written as 2.1e11, because a Quantity stores SI and converts
    // only at a boundary. So no display preference can become engineering authority.
    Document document{"Part"};
    MaterialDefinition definition;
    definition.mechanical.youngsModulus = MaterialProperty<ElasticModulus>::known(210_GPa);
    const MaterialId id = createOrFail(document, "Steel", definition);

    const std::string text = jsonOf(document);
    CHECK_THAT(text, ContainsSubstring("210000000000.0"));
    CHECK_THAT(text, !ContainsSubstring("\"GPa\""));
    CHECK_THAT(text, !ContainsSubstring("\"MPa\""));

    const Document loaded = roundTrip(document);
    const MaterialDefinition& after = definitionOf(loaded, id);
    // Recovered in every unit, from the one SI number.
    CHECK_THAT(after.mechanical.youngsModulus.value()->in(units::GPa), WithinRel(210.0, 1e-12));
    CHECK_THAT(after.mechanical.youngsModulus.value()->in(units::MPa), WithinRel(210000.0, 1e-12));
    CHECK(after.mechanical.youngsModulus.value()->si() == (210_GPa).si());
}

TEST_CASE("MaterialFile_RoundTripsDemandingFloatingPointValuesExactly", "[io][persist]") {
    Document document{"Part"};
    MaterialDefinition definition;
    definition.mechanical.youngsModulus = MaterialProperty<ElasticModulus>::known(
        ElasticModulus::fromSi(210e9));
    definition.mechanical.poissonRatio =
        MaterialProperty<PoissonRatio>::known(PoissonRatio::of(0.33));
    definition.mechanical.density = MaterialProperty<Density>::known(Density::fromSi(167.3));
    definition.thermal.thermalExpansion = MaterialProperty<ThermalExpansionCoefficient>::known(
        ThermalExpansionCoefficient::fromSi(12e-6));
    definition.thermal.electricalResistivity =
        MaterialProperty<materials::ElectricalResistivity>::known(
            materials::ElectricalResistivity::ofOhmMetres(2.65e-8));
    const MaterialId id = createOrFail(document, "Values", definition);

    // BIT-EXACT through the round trip, and through a SECOND one, so a formatting
    // loss that only shows on re-save would be caught.
    const Document once = roundTrip(document);
    CHECK(definitionOf(once, id) == definition);
    const Document twice = roundTrip(once);
    CHECK(definitionOf(twice, id) == definition);
    CHECK(definitionOf(twice, id).mechanical.youngsModulus.value()->si() == 210e9);
    CHECK(definitionOf(twice, id).mechanical.poissonRatio.value()->value() == 0.33);
    CHECK(definitionOf(twice, id).mechanical.density.value()->si() == 167.3);
    CHECK(definitionOf(twice, id).thermal.thermalExpansion.value()->si() == 12e-6);
    CHECK(definitionOf(twice, id).thermal.electricalResistivity.value()->ohmMetres() == 2.65e-8);
}

// --- provenance -------------------------------------------------------------

TEST_CASE("MaterialFile_RoundTripsProvenanceIncludingTheDate", "[io][persist]") {
    Document document{"Part"};
    const MaterialId id = createOrFail(document, "Steel", richSteel());
    const Document loaded = roundTrip(document);
    const MaterialProvenance& after = definitionOf(loaded, id).provenance;

    CHECK(after == richSteel().provenance);
    CHECK(after.material == handbook());
    const PropertyProvenance density =
        materials::effectiveProvenance(after, MechanicalPropertyKind::Density);
    CHECK(density.kind == SourceKind::Measured);
    CHECK(density.source == "Synthetic test report");
    CHECK(density.reference == "Specimen 3");
    CHECK(density.revision == "Rev A");
    CHECK(density.condition == "as-rolled");
    CHECK(density.notes == "Three specimens.");
    REQUIRE(density.date.has_value());
    CHECK(*density.date == fixedDate(2024, 3, 17));
    // ISO 8601 in the file, locale-independent.
    CHECK_THAT(jsonOf(document), ContainsSubstring("\"2024-03-17\""));

    // Every source kind survives on its own, so the mapping is not constant.
    for (const SourceKind kind : materials::sourceKinds()) {
        Document one{"Part"};
        MaterialDefinition each;
        PropertyProvenance record;
        record.kind = kind;
        record.source = "x";
        each.provenance.material = record;
        const MaterialId only = createOrFail(one, "M", each);
        CHECK(definitionOf(roundTrip(one), only).provenance.material.kind == kind);
    }
}

TEST_CASE("MaterialFile_KeepsEachCitationOnItsOwnProperty", "[io][persist]") {
    // Provenance is keyed by property NAME in the file, not by position, so no
    // reordering can reattach a citation to the wrong value.
    Document document{"Part"};
    MaterialDefinition definition;
    definition.mechanical.density = MaterialProperty<Density>::known(Density::fromSi(7850.0));
    definition.mechanical.youngsModulus = MaterialProperty<ElasticModulus>::known(210_GPa);
    definition.provenance.mechanical[MechanicalPropertyKind::Density] = measured();
    definition.provenance.mechanical[MechanicalPropertyKind::YoungsModulus] = handbook();
    // k has no value, so a citation for it would be an orphan -- not written here.
    const MaterialId id = createOrFail(document, "Steel", definition);

    const Document reloaded = roundTrip(document);
    const MaterialProvenance& after = definitionOf(reloaded, id).provenance;
    CHECK(materials::effectiveProvenance(after, MechanicalPropertyKind::Density) == measured());
    CHECK(materials::effectiveProvenance(after, MechanicalPropertyKind::YoungsModulus) ==
          handbook());
    CHECK(materials::hasOwnProvenance(after, MechanicalPropertyKind::Density));
    CHECK(materials::hasOwnProvenance(after, MechanicalPropertyKind::YoungsModulus));
    CHECK_FALSE(materials::hasOwnProvenance(after, MechanicalPropertyKind::YieldStrength));
    // The file names the property, rather than relying on order.
    CHECK_THAT(jsonOf(document), ContainsSubstring("\"property\": \"density\""));
    CHECK_THAT(jsonOf(document), ContainsSubstring("\"property\": \"youngs_modulus\""));
}

TEST_CASE("MaterialFile_KeepsAKnownValueWithUnknownProvenanceKnown", "[io][persist]") {
    Document document{"Part"};
    MaterialDefinition definition;
    definition.mechanical.density = MaterialProperty<Density>::known(Density::fromSi(2700.0));
    const MaterialId id = createOrFail(document, "Uncited", definition);

    const Document loaded = roundTrip(document);
    CHECK(features::hasMaterialProperty(loaded, id, MechanicalPropertyKind::Density));
    CHECK(features::requireDensity(loaded, id).has_value());
    const Result<PropertyProvenance> provenance =
        features::materialPropertyProvenance(loaded, id, MechanicalPropertyKind::Density);
    REQUIRE(provenance);
    CHECK(provenance->empty());
    CHECK(provenance->kind == SourceKind::Unspecified);
    // No provenance section at all in the file, because nothing was stated.
    CHECK_THAT(jsonOf(document), !ContainsSubstring("provenance"));
}

// --- assignments ------------------------------------------------------------

TEST_CASE("MaterialFile_RoundTripsAnAssignmentByIdentity", "[io][persist]") {
    Document document{"Part"};
    const MaterialId id = createOrFail(document, "Steel", richSteel());
    REQUIRE(features::assignMaterial(document, id));

    const Document loaded = roundTrip(document);
    CHECK(loaded.materialAssignment() == id);
    CHECK(features::materialAssignment(loaded).state ==
          features::MaterialAssignmentState::Resolved);
    CHECK(features::effectiveMaterial(loaded)->materialId() == id);
    // BY ID, never by name. The file must not contain a name-keyed assignment.
    CHECK_THAT(jsonOf(document), ContainsSubstring("\"material_assignment\""));
    CHECK_THAT(jsonOf(document), ContainsSubstring("\"material\": " + std::to_string(id.value())));
}

TEST_CASE("MaterialFile_ResolvesDuplicateDesignationsByIdentityAcrossSaveAndLoad",
          "[io][persist]") {
    // Two materials with one designation -- names are unique, designations are not.
    // Only identity can decide which the assignment names.
    Document document{"Part"};
    MaterialDefinition first = richSteel();
    MaterialDefinition second = richSteel();
    second.notes = "The other one.";
    second.mechanical.youngsModulus = MaterialProperty<ElasticModulus>::known(200_GPa);
    const MaterialId a = createOrFail(document, "SteelA", first);
    const MaterialId b = createOrFail(document, "SteelB", second);
    REQUIRE(definitionOf(document, a).designation == definitionOf(document, b).designation);
    REQUIRE(features::assignMaterial(document, b));

    const Document loaded = roundTrip(document);
    CHECK(loaded.materialAssignment() == b);
    CHECK(loaded.materialAssignment() != a);
    CHECK(features::effectiveMaterial(loaded)->materialId() == b);
    // And each keeps its own values.
    CHECK_THAT(definitionOf(loaded, a).mechanical.youngsModulus.value()->in(units::GPa),
               WithinRel(210.0, 1e-12));
    CHECK_THAT(definitionOf(loaded, b).mechanical.youngsModulus.value()->in(units::GPa),
               WithinRel(200.0, 1e-12));
    CHECK(definitionOf(loaded, b).notes == "The other one.");
}

TEST_CASE("MaterialFile_PreservesAnUnresolvedAssignmentAndNeverRebindsIt", "[io][persist]") {
    // THE PERSISTENCE FORM OF THE NO-REBINDING INVARIANT. A = Steel, B = Steel,
    // P -> A, delete A. Save and load: still unresolved A. Save and load again:
    // still unresolved A. Never B.
    Document document{"Part"};
    MaterialDefinition shared = richSteel();
    const MaterialId a = createOrFail(document, "SteelA", shared);
    const MaterialId b = createOrFail(document, "SteelB", shared);
    REQUIRE(features::assignMaterial(document, a));
    REQUIRE(features::removeMaterial(document, a));
    REQUIRE(features::materialAssignment(document).state ==
            features::MaterialAssignmentState::Unresolved);

    const Document once = roundTrip(document);
    CHECK(once.materialAssignment() == a);
    CHECK(once.materialAssignment() != b);
    CHECK(features::materialAssignment(once).state ==
          features::MaterialAssignmentState::Unresolved);
    CHECK(features::effectiveMaterial(once) == nullptr);
    CHECK(features::findMaterial(once, a) == nullptr);
    CHECK(features::findMaterial(once, b) != nullptr);

    // A second round trip, because a loader that dropped an unresolved assignment
    // would show it on the second save rather than the first.
    const Document twice = roundTrip(once);
    CHECK(twice.materialAssignment() == a);
    CHECK(features::materialAssignment(twice).state ==
          features::MaterialAssignmentState::Unresolved);
    CHECK(features::effectiveMaterial(twice) == nullptr);
}

TEST_CASE("MaterialFile_KeepsAnAssignmentThroughARename", "[io][persist]") {
    Document document{"Part"};
    const MaterialId id = createOrFail(document, "Steel", richSteel());
    REQUIRE(features::assignMaterial(document, id));
    REQUIRE(document.rename(id, "StructuralSteel"));

    const Document loaded = roundTrip(document);
    CHECK(loaded.materialAssignment() == id);
    CHECK(features::findMaterial(loaded, id)->name() == "StructuralSteel");
    CHECK(features::effectiveMaterial(loaded)->materialId() == id);
}

TEST_CASE("MaterialFile_WritesNoAssignmentSectionWhenThereIsNone", "[io][persist]") {
    Document document{"Part"};
    createOrFail(document, "Steel", richSteel());
    CHECK_THAT(jsonOf(document), !ContainsSubstring("material_assignment"));
    const Document loaded = roundTrip(document);
    CHECK_FALSE(loaded.materialAssignment().has_value());
    CHECK(features::materialAssignment(loaded).state ==
          features::MaterialAssignmentState::Unassigned);
}

// --- derived state is not persisted -----------------------------------------

TEST_CASE("MaterialFile_ContainsNoDerivedEngineeringState", "[io][persist]") {
    // A document with geometry, a material and an assignment -- so every derived
    // quantity is available and could have been written.
    BoxPart part;
    const MaterialId id = createOrFail(part.document, "Steel", richSteel());
    REQUIRE(features::assignMaterial(part.document, id));
    // Compute them all, so nothing is unwritten merely because it was never asked for.
    const double mass = massOf(part.document, part.feature);
    CHECK(mass > 0.0);
    CHECK(features::materialCompleteness(part.document, id,
                                         materials::ConsumerKind::ThermalTransient)
              .has_value());

    const std::string text = jsonOf(part.document);
    for (const char* forbidden :
         {"\"mass\"", "\"volume\"", "\"centre_of_mass\"", "\"center_of_mass\"", "centreOfMass",
          "centerOfMass", "\"inertia\"", "inertia_tensor", "\"ixx\"", "\"iyy\"", "\"izz\"",
          "\"ixy\"", "\"ixz\"", "\"iyz\"", "shear_modulus", "bulk_modulus", "derived_shear",
          "derived_bulk", "completeness", "fea_ready", "thermal_ready", "missing_properties",
          "effective_material", "resolved_material"}) {
        CAPTURE(forbidden);
        CHECK_THAT(text, !ContainsSubstring(forbidden));
    }
    // And nothing machine-specific: no absolute path from the build or the source tree.
    CHECK_THAT(text, !ContainsSubstring("C:\\\\Users"));
    CHECK_THAT(text, !ContainsSubstring("C:/Users"));
    CHECK_THAT(text, !ContainsSubstring("bc-build"));
    CHECK_THAT(text, !ContainsSubstring("OneDrive"));
}

TEST_CASE("MaterialFile_RecomputesMassAfterLoadAndFollowsALaterDensityEdit",
          "[io][persist]") {
    // 20 x 30 x 50 mm = 3e-5 m^3. density 1000 -> 0.03 kg; 2000 -> 0.06 kg.
    BoxPart part;
    MaterialDefinition light;
    light.designation = "Synthetic";
    light.mechanical.density = MaterialProperty<Density>::known(Density::fromSi(1000.0));
    const MaterialId id = createOrFail(part.document, "Synthetic", light);
    REQUIRE(features::assignMaterial(part.document, id));
    const double before = massOf(part.document, part.feature);
    CHECK_THAT(before, WithinRel(0.03, 1e-12));

    Document loaded = roundTrip(part.document);
    // Recomputed from geometry, assignment and density -- there is no persisted mass
    // to have been trusted.
    CHECK_THAT(massOf(loaded, part.feature), WithinRel(0.03, 1e-12));

    // Change the density AFTER load: the mass must follow.
    materials::MechanicalProperties heavier = definitionOf(loaded, id).mechanical;
    heavier.density = MaterialProperty<Density>::known(Density::fromSi(2000.0));
    REQUIRE(features::setMaterialMechanical(loaded, id, heavier));
    CHECK_THAT(massOf(loaded, part.feature), WithinRel(0.06, 1e-12));

    // And change the geometry: the mass must follow that too.
    const auto* extrude = loaded.findObjectAs<features::ExtrudeFeature>(part.feature);
    REQUIRE(extrude != nullptr);
    features::ExtrudeDefinition deeper = extrude->definition();
    deeper.depth = 100_mm;
    REQUIRE(loaded.modifyObject<features::ExtrudeFeature>(
        part.feature, [&](features::ExtrudeFeature& f) { return f.setDefinition(deeper); }));
    CHECK_THAT(massOf(loaded, part.feature), WithinRel(0.12, 1e-12));
}

TEST_CASE("MaterialFile_RecomputesDerivedModuliAfterLoadWithoutStoringThem",
          "[io][persist]") {
    Document document{"Part"};
    MaterialDefinition definition;
    definition.mechanical.youngsModulus = MaterialProperty<ElasticModulus>::known(210_GPa);
    definition.mechanical.poissonRatio =
        MaterialProperty<PoissonRatio>::known(PoissonRatio::of(0.30));
    const MaterialId id = createOrFail(document, "Steel", definition);
    // Available before the save, so it could have been written.
    REQUIRE(materials::derivedShearModulus(definitionOf(document, id).mechanical).isDerived());

    const Document loaded = roundTrip(document);
    const materials::MechanicalProperties& after = definitionOf(loaded, id).mechanical;
    const auto shear = materials::derivedShearModulus(after);
    // STILL DERIVED, never promoted to a supplied value.
    CHECK(shear.isDerived());
    CHECK_FALSE(shear.isKnown());
    CHECK_THAT(shear.value()->in(units::GPa), WithinRel(210.0 / 2.6, 1e-12));
    CHECK(materials::derivedBulkModulus(after).isDerived());
    // And the file never named them.
    CHECK_THAT(jsonOf(document), !ContainsSubstring("shear_modulus"));
    CHECK_THAT(jsonOf(document), !ContainsSubstring("bulk_modulus"));
}

TEST_CASE("MaterialFile_RecomputesCompletenessAfterLoad", "[io][persist]") {
    // density known, cp known, k unknown -> ThermalTransient missing the
    // conductivity, before AND after, because the report is rebuilt from what loaded.
    Document document{"Part"};
    MaterialDefinition definition;
    definition.mechanical.density = MaterialProperty<Density>::known(Density::fromSi(7850.0));
    definition.thermal.specificHeatCapacity =
        MaterialProperty<SpecificHeatCapacity>::known(460_J_per_kg_K);
    const MaterialId id = createOrFail(document, "Steel", definition);

    const auto report = [](const Document& doc, MaterialId material) {
        const Result<materials::CompletenessReport> result = features::materialCompleteness(
            doc, material, materials::ConsumerKind::ThermalTransient);
        REQUIRE(result);
        return *result;
    };
    const materials::CompletenessReport before = report(document, id);
    CHECK(before.state == materials::CompletenessState::Incomplete);
    CHECK(before.missingThermal == std::vector{ThermalPropertyKind::ThermalConductivity});

    const Document loaded = roundTrip(document);
    const materials::CompletenessReport after = report(loaded, id);
    CHECK(after == before);
    CHECK(after.state == materials::CompletenessState::Incomplete);
    CHECK(after.missingThermal == std::vector{ThermalPropertyKind::ThermalConductivity});
    // Supplying it after load makes the report Ready, so it is genuinely recomputed.
    materials::ThermalProperties complete = definitionOf(loaded, id).thermal;
    complete.thermalConductivity = MaterialProperty<ThermalConductivity>::known(50_W_per_m_K);
    Document mutable_ = loaded.clone();
    REQUIRE(features::setMaterialThermal(mutable_, id, complete));
    CHECK(report(mutable_, id).state == materials::CompletenessState::Ready);
}

// --- custom materials and the library ---------------------------------------

TEST_CASE("MaterialFile_KeepsACustomMaterialLocalAndNeverALiveLibraryReference",
          "[io][persist]") {
    Document document{"Part"};
    const Result<materials::LibraryMaterial> entry =
        materials::findLibraryMaterial("bettercad", "al-6061-t6");
    REQUIRE(entry);
    const Result<MaterialId> imported =
        features::importLibraryMaterial(document, "Aluminium", *entry);
    REQUIRE(imported);
    // Give it values the library does not have, then clone it -- so the saved
    // document holds engineering data that exists nowhere else.
    materials::MechanicalProperties mechanical;
    mechanical.density = MaterialProperty<Density>::known(Density::fromSi(2700.0));
    REQUIRE(features::setMaterialMechanical(document, *imported, mechanical));
    const Result<MaterialId> clone =
        features::cloneMaterial(document, *imported, "AluminiumEdited");
    REQUIRE(clone);
    materials::MechanicalProperties edited;
    edited.density = MaterialProperty<Density>::known(Density::fromSi(2750.0));
    REQUIRE(features::setMaterialMechanical(document, *clone, edited));

    const Document loaded = roundTrip(document);
    // The snapshot survives: the clone's own value, not the library's and not the
    // import's.
    CHECK_THAT(definitionOf(loaded, *clone).mechanical.density.value()->in(units::kg_per_m3),
               WithinRel(2750.0, 1e-12));
    CHECK_THAT(definitionOf(loaded, *imported).mechanical.density.value()->in(units::kg_per_m3),
               WithinRel(2700.0, 1e-12));
    // The origin key is provenance and survives, and the library itself is untouched.
    REQUIRE(definitionOf(loaded, *imported).origin.has_value());
    CHECK(definitionOf(loaded, *imported).origin->entry == "al-6061-t6");
    CHECK(definitionOf(loaded, *imported).origin->revision == 1);
    const Result<materials::LibraryMaterial> stillThere =
        materials::findLibraryMaterial("bettercad", "al-6061-t6");
    REQUIRE(stillThere);
    CHECK(*stillThere == *entry);
    // Nothing consults the library on load: the loaded values are the file's, and the
    // library has no property values at all to have supplied them.
    for (const MechanicalPropertyKind kind : materials::mechanicalPropertyKinds()) {
        if (kind != MechanicalPropertyKind::Density && !materials::isDerivedKind(kind)) {
            CHECK_FALSE(features::hasMaterialProperty(loaded, *imported, kind));
        }
    }
}

TEST_CASE("MaterialFile_LoadsAnOriginKeyForALibraryEntryThisBuildDoesNotHave",
          "[io][persist]") {
    // A document whose origin names an entry that is not in this build's library must
    // still load completely: the values are the document's own (ADR-025), so a
    // missing, renamed or newer library cannot make the file unreadable.
    Document document{"Part"};
    MaterialDefinition definition;
    definition.designation = "From Another Catalogue";
    definition.origin = materials::MaterialLibraryKey{"acme", "unobtainium-7", 42};
    definition.mechanical.density = MaterialProperty<Density>::known(Density::fromSi(1234.0));
    const MaterialId id = createOrFail(document, "Exotic", definition);
    // The entry genuinely is not there.
    CHECK_FALSE(materials::findLibraryMaterial("acme", "unobtainium-7").has_value());

    const Document loaded = roundTrip(document);
    CHECK(definitionOf(loaded, id) == definition);
    CHECK(definitionOf(loaded, id).origin->library == "acme");
    CHECK(definitionOf(loaded, id).origin->revision == 42);
    CHECK_THAT(definitionOf(loaded, id).mechanical.density.value()->in(units::kg_per_m3),
               WithinRel(1234.0, 1e-12));
}

// --- the rich document, twice ------------------------------------------------

TEST_CASE("MaterialFile_RoundTripsARichDocumentTwiceWithoutDrift", "[io][persist]") {
    // A -> save -> load B -> save -> load C, requiring A == B == C by the document's
    // own equivalence. This is what catches one-way normalisation and second-save
    // drift.
    BoxPart part;
    Document& document = part.document;

    MaterialDefinition partial;
    partial.designation = "Prototype Foam";
    MaterialDefinition scratch = richSteel();
    scratch.origin.reset();
    scratch.designation = "From Scratch";

    const Result<materials::LibraryMaterial> entry =
        materials::findLibraryMaterial("bettercad", "steel-s235jr");
    REQUIRE(entry);
    const Result<MaterialId> imported = features::importLibraryMaterial(document, "Imported", *entry);
    REQUIRE(imported);
    const MaterialId rich = createOrFail(document, "RichSteel", richSteel());
    const MaterialId duplicate = createOrFail(document, "AlsoSteel", richSteel());
    const MaterialId fromScratch = createOrFail(document, "Scratch", scratch);
    const MaterialId incomplete = createOrFail(document, "Foam", partial);
    const Result<MaterialId> cloned = features::cloneMaterial(document, rich, "ClonedSteel");
    REQUIRE(cloned);
    // An assignment, then a deletion, so the document also carries an UNRESOLVED one.
    const MaterialId doomed = createOrFail(document, "Doomed", richSteel());
    REQUIRE(features::assignMaterial(document, doomed));
    REQUIRE(features::removeMaterial(document, doomed));
    REQUIRE(features::materialAssignment(document).state ==
            features::MaterialAssignmentState::Unresolved);

    const Document b = roundTrip(document);
    const Document c = roundTrip(b);

    // Canonical equivalence of the whole documents, twice.
    CHECK(equivalent(document, b));
    CHECK(equivalent(b, c));
    CHECK(equivalent(document, c));
    // And the material-level state, named.
    CHECK(features::materialIds(c) == features::materialIds(document));
    CHECK(features::materialCount(c) == 6);
    for (const MaterialId id : {*imported, rich, duplicate, fromScratch, incomplete, *cloned}) {
        CAPTURE(id.value());
        REQUIRE(features::findMaterial(c, id) != nullptr);
        CHECK(definitionOf(c, id) == definitionOf(document, id));
        CHECK(features::findMaterial(c, id)->name() == features::findMaterial(document, id)->name());
    }
    CHECK(c.materialAssignment() == doomed);
    CHECK(features::materialAssignment(c).state == features::MaterialAssignmentState::Unresolved);
    // The incomplete material is still incomplete, and specifically so.
    const Result<materials::CompletenessReport> report =
        features::materialCompleteness(c, incomplete, materials::ConsumerKind::MassProperties);
    REQUIRE(report);
    CHECK(report->state == materials::CompletenessState::Incomplete);
    CHECK(report->missingMechanical == std::vector{MechanicalPropertyKind::Density});
}

TEST_CASE("MaterialFile_SerializesTheSameStateToTheSameBytes", "[io][persist]") {
    BoxPart part;
    const MaterialId a = createOrFail(part.document, "SteelA", richSteel());
    createOrFail(part.document, "SteelB", richSteel());
    REQUIRE(features::assignMaterial(part.document, a));

    // Two saves of one unchanged document.
    const std::string first = jsonOf(part.document);
    const std::string second = jsonOf(part.document);
    CHECK(first == second);

    // And save -> load -> save is byte-identical too, which is the stronger claim:
    // the loader introduces no normalisation of its own.
    const Document loaded = roundTrip(part.document);
    CHECK(jsonOf(loaded) == first);
    const Document again = roundTrip(loaded);
    CHECK(jsonOf(again) == first);
}

TEST_CASE("MaterialFile_OrdersMaterialsAndProvenanceDeterministically", "[io][persist]") {
    // Materials in ascending ID order, and each provenance array in property
    // enumeration order -- built here in reverse so the order cannot come from
    // insertion.
    Document document{"Part"};
    MaterialDefinition definition;
    definition.mechanical.density = MaterialProperty<Density>::known(Density::fromSi(7850.0));
    definition.mechanical.youngsModulus = MaterialProperty<ElasticModulus>::known(210_GPa);
    definition.mechanical.yieldStrength = MaterialProperty<Stress>::known(235_MPa);
    definition.provenance.mechanical[MechanicalPropertyKind::YieldStrength] = handbook();
    definition.provenance.mechanical[MechanicalPropertyKind::YoungsModulus] = measured();
    definition.provenance.mechanical[MechanicalPropertyKind::Density] = handbook();
    createOrFail(document, "Zeta", definition);
    createOrFail(document, "Alpha", definition);

    const std::string text = jsonOf(document);
    // Zeta was created first, so its lower ID puts it first -- not alphabetical.
    CHECK(text.find("\"Zeta\"") < text.find("\"Alpha\""));
    // Provenance in enumeration order: density, youngs_modulus, yield_strength.
    const std::size_t density = text.find("\"property\": \"density\"");
    const std::size_t modulus = text.find("\"property\": \"youngs_modulus\"");
    const std::size_t yield = text.find("\"property\": \"yield_strength\"");
    REQUIRE(density != std::string::npos);
    REQUIRE(modulus != std::string::npos);
    REQUIRE(yield != std::string::npos);
    CHECK(density < modulus);
    CHECK(modulus < yield);
    // Repeatable.
    for (int pass = 0; pass < 5; ++pass) {
        CHECK(jsonOf(document) == text);
    }
}

// --- malformed input --------------------------------------------------------

TEST_CASE("MaterialFile_RejectsMalformedMaterialsAndLeavesNothingBehind", "[io][persist]") {
    // Every case replaces the material's "data" object in a valid file, so exactly
    // one thing is wrong each time.
    Document document{"Part"};
    const MaterialId id = createOrFail(document, "Steel", richSteel());
    const std::string valid = jsonOf(document);
    const std::string marker = "\"data\": {";
    const std::size_t at = valid.find(marker, valid.find("\"type\": \"material\""));
    REQUIRE(at != std::string::npos);
    // Find the matching close brace of the data object.
    std::size_t depth = 0;
    std::size_t end = at + marker.size() - 1;
    for (std::size_t i = end; i < valid.size(); ++i) {
        if (valid[i] == '{') {
            ++depth;
        } else if (valid[i] == '}') {
            --depth;
            if (depth == 0) {
                end = i;
                break;
            }
        }
    }
    const auto withData = [&](const std::string& data) {
        return valid.substr(0, at + marker.size() - 1) + data + valid.substr(end + 1);
    };

    struct Case {
        const char* what;
        std::string data;
        const char* expect;
    };
    const std::vector<Case> cases{
        {"a zero density", R"({"mechanical": {"density": {"value": 0.0}}})", "density"},
        {"a negative modulus",
         R"({"mechanical": {"youngs_modulus": {"value": -1.0}}})", "modulus"},
        {"an incompressible Poisson ratio",
         R"({"mechanical": {"poisson_ratio": {"value": 0.5}}})", "Poisson"},
        {"a negative conductivity",
         R"({"thermal": {"thermal_conductivity": {"value": -10.0}}})", "conductivity"},
        {"a negative melting temperature",
         R"({"thermal": {"melting_temperature": {"value": -1.0}}})", "melting"},
        {"an unknown mechanical property",
         R"({"mechanical": {"tensile_modulus": {"value": 1.0}}})", "unknown mechanical property"},
        {"an unknown thermal property",
         R"({"thermal": {"heat_of_fusion": {"value": 1.0}}})", "unknown thermal property"},
        {"an unknown top-level key", R"({"colour": "red"})", "colour"},
        {"an unknown hardness scale",
         R"({"mechanical": {"hardness": {"value": 60.0, "scale": "HRZ"}}})", "hardness scale"},
        {"a hardness with no scale",
         R"({"mechanical": {"hardness": {"value": 60.0}}})", "scale"},
        {"an unknown source kind",
         R"({"provenance": {"material": {"kind": "rumour"}}})", "source kind"},
        {"a provenance date that is not a date",
         R"({"provenance": {"material": {"kind": "measured", "date": "17/03/2024"}}})",
         "ISO 8601"},
        {"an impossible provenance date",
         R"({"provenance": {"material": {"kind": "measured", "date": "2026-02-30"}}})",
         "ISO 8601"},
        {"a provenance record for an unknown property",
         R"({"provenance": {"mechanical": [{"kind": "measured", "property": "tensile_modulus"}]}})",
         "unknown property"},
        {"two provenance records for one property",
         R"({"mechanical": {"density": {"value": 7850.0}},
             "provenance": {"mechanical": [{"kind": "measured", "property": "density"},
                                           {"kind": "handbook", "property": "density"}]}})",
         "more than once"},
        {"a property value that is a string",
         R"({"mechanical": {"density": {"value": "7850"}}})", "number"},
        {"a property that is not an object",
         R"({"mechanical": {"density": 7850.0}})", "object"},
        {"a half-filled origin key",
         R"({"origin": {"library": "", "entry": "x", "revision": 1}})", "library"},
        {"an origin revision below one",
         R"({"origin": {"library": "a", "entry": "b", "revision": 0}})", "revision"},
    };

    for (const Case& test : cases) {
        CAPTURE(test.what);
        const Result<Document> loaded = io::documentFromJson(withData(test.data));
        REQUIRE_FALSE(loaded.has_value());
        CHECK_THAT(loaded.error().message, ContainsSubstring(test.expect));
    }
    // The valid file still loads, so the harness itself is sound.
    const Document good = roundTrip(document);
    CHECK(definitionOf(good, id) == richSteel());
}

TEST_CASE("MaterialFile_RejectsANonFiniteNumber", "[io][persist]") {
    Document document{"Part"};
    createOrFail(document, "Steel", richSteel());
    const std::string valid = jsonOf(document);

    // JSON has no NaN or Infinity token, so a file containing one is not JSON and the
    // parser rejects it before any material code runs.
    for (const char* token : {"NaN", "Infinity", "-Infinity"}) {
        CAPTURE(token);
        std::string text = valid;
        const std::size_t at = text.find("7850.0");
        REQUIRE(at != std::string::npos);
        text.replace(at, std::string_view{"7850.0"}.size(), token);
        const Result<Document> loaded = io::documentFromJson(text);
        CHECK_FALSE(loaded.has_value());
    }
    // A number large enough to become infinite as a double is caught by the finite
    // check rather than becoming an infinite density.
    std::string overflow = valid;
    const std::size_t at = overflow.find("7850.0");
    overflow.replace(at, std::string_view{"7850.0"}.size(), "1e400");
    const Result<Document> loaded = io::documentFromJson(overflow);
    CHECK_FALSE(loaded.has_value());
}

TEST_CASE("MaterialFile_RejectsAMaterialWithNoId", "[io][persist]") {
    Document document{"Part"};
    createOrFail(document, "Steel", richSteel());
    std::string text = jsonOf(document);
    const std::size_t at = text.find("\"id\":", text.find("\"objects\""));
    REQUIRE(at != std::string::npos);
    text.replace(at, std::string_view{"\"id\":"}.size(), "\"no_id\":");
    const Result<Document> loaded = io::documentFromJson(text);
    REQUIRE_FALSE(loaded);
    CHECK_THAT(loaded.error().message, ContainsSubstring("no_id"));
}

TEST_CASE("MaterialFile_RejectsAnAssignmentThatIsStructurallyMalformed", "[io][persist]") {
    // A well-formed assignment to a material that is absent is legitimate
    // (Unresolved). A structurally malformed one is not, and the two must not be
    // confused.
    Document document{"Part"};
    const MaterialId id = createOrFail(document, "Steel", richSteel());
    REQUIRE(features::assignMaterial(document, id));
    const std::string valid = jsonOf(document);

    // Well formed, names a material that is not there: LOADS, as Unresolved.
    {
        std::string text = valid;
        const std::size_t at = text.find("\"material_assignment\"");
        REQUIRE(at != std::string::npos);
        const std::size_t number = text.find(std::to_string(id.value()), at);
        REQUIRE(number != std::string::npos);
        text.replace(number, std::to_string(id.value()).size(), "9999");
        const Result<Document> loaded = io::documentFromJson(text);
        REQUIRE(loaded.has_value());
        CHECK(loaded->materialAssignment() == MaterialId::fromValue(9999));
        CHECK(features::materialAssignment(*loaded).state ==
              features::MaterialAssignmentState::Unresolved);
    }
    // Structurally malformed: rejected.
    for (const char* body : {R"({"material": "Steel"})", R"({"material": -1})",
                             R"({"materials": 1})", R"({"material": 1, "extra": 2})",
                             R"([1])"}) {
        CAPTURE(body);
        std::string text = valid;
        const std::size_t at = text.find("\"material_assignment\": {");
        REQUIRE(at != std::string::npos);
        std::size_t depth = 0;
        std::size_t end = text.find('{', at);
        for (std::size_t i = end; i < text.size(); ++i) {
            if (text[i] == '{') {
                ++depth;
            } else if (text[i] == '}') {
                --depth;
                if (depth == 0) {
                    end = i;
                    break;
                }
            }
        }
        text = text.substr(0, text.find('{', at)) + body + text.substr(end + 1);
        CHECK_FALSE(io::documentFromJson(text).has_value());
    }
}

TEST_CASE("MaterialFile_AFailedLoadLeavesTheCallersDocumentUntouched", "[io][persist]") {
    // documentFromJson returns a NEW Document, so a failed load cannot reach an
    // existing one. Shown here through the file API, which is what a user drives.
    TempDir dir;
    Document original{"Part"};
    const MaterialId id = createOrFail(original, "Steel", richSteel());
    REQUIRE(features::assignMaterial(original, id));
    const auto path = dir.path() / "good.bcad";
    REQUIRE(io::saveDocument(original, path).has_value());

    const auto bad = dir.path() / "bad.bcad";
    std::string broken = jsonOf(original);
    const std::size_t at = broken.find("7850.0");
    broken.replace(at, std::string_view{"7850.0"}.size(), "0.0");
    {
        std::ofstream out{bad, std::ios::binary};
        out << broken;
    }
    const Result<Document> loaded = io::loadDocument(bad);
    REQUIRE_FALSE(loaded);

    // The original is exactly as it was, and the good file still loads.
    CHECK(definitionOf(original, id) == richSteel());
    CHECK(original.materialAssignment() == id);
    const Result<Document> good = io::loadDocument(path);
    REQUIRE(good);
    CHECK(definitionOf(*good, id) == richSteel());
}

// --- backward compatibility -------------------------------------------------

TEST_CASE("MaterialFile_LoadsADocumentFromBeforeMaterialsExistedWithoutInventingAny",
          "[io][persist]") {
    // An absent section means "none of those", which is what a pre-P15 document
    // meant by not having one. No fabricated default material, no generic Steel.
    Document before{"Part"};
    auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
    addRectangle(*sketch, 0_mm, 0_mm, 20_mm, 30_mm);
    const ObjectId profile = require(before.addObject(std::move(sketch)));
    auto extrude = features::ExtrudeFeature::create(
        "Solid", {.profile = SketchId::fromValue(profile.value()), .depth = 50_mm});
    REQUIRE(extrude.has_value());
    const ObjectId feature = require(before.addObject(std::move(*extrude)));
    const std::string text = jsonOf(before);
    // The file genuinely has no material state.
    CHECK_THAT(text, !ContainsSubstring("material"));

    const Document loaded = roundTrip(before);
    CHECK(features::materialCount(loaded) == 0);
    CHECK_FALSE(loaded.materialAssignment().has_value());
    CHECK(features::materialAssignment(loaded).state ==
          features::MaterialAssignmentState::Unassigned);
    CHECK(features::effectiveMaterial(loaded) == nullptr);
    // The geometry is unchanged, and asking for a mass fails for want of a material
    // rather than succeeding with an invented one.
    CHECK(loaded.findObject(feature) != nullptr);
    features::Regenerator regenerator;
    Document copy = loaded.clone();
    requireReport(regenerator, copy);
    const Result<features::PartMassProperties> mass =
        features::partMassProperties(copy, regenerator, feature);
    REQUIRE_FALSE(mass);
    CHECK_THAT(mass.error().message, ContainsSubstring("no material"));
}

TEST_CASE("MaterialFile_NeedsNoVersionBumpAndStillReadsVersionOne", "[io][persist]") {
    // The format's own policy: "adding a kind, an object type or an optional field
    // needs no bump. Changing what an existing field means needs one." P15 adds an
    // object type and an optional section, so the version stays 2 -- and a version-1
    // document still loads, because an absent material section means none.
    Document document{"Part"};
    createOrFail(document, "Steel", richSteel());
    const std::string text = jsonOf(document);
    CHECK_THAT(text, ContainsSubstring("\"version\": 2"));
    CHECK(io::kDocumentFormatVersion == 2);
    CHECK(io::kOldestReadableDocumentVersion == 1);

    // A version-1 file with no materials loads, and gains none.
    Document plain{"Part"};
    auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
    addRectangle(*sketch, 0_mm, 0_mm, 10_mm, 10_mm);
    require(plain.addObject(std::move(sketch)));
    std::string v1 = jsonOf(plain);
    const std::size_t at = v1.find("\"version\": 2");
    REQUIRE(at != std::string::npos);
    v1.replace(at, std::string_view{"\"version\": 2"}.size(), "\"version\": 1");
    const Result<Document> loaded = io::documentFromJson(v1);
    REQUIRE(loaded);
    CHECK(features::materialCount(*loaded) == 0);
    CHECK_FALSE(loaded->materialAssignment().has_value());

    // And a version this build does not know is refused rather than guessed at.
    std::string v3 = text;
    const std::size_t future = v3.find("\"version\": 2");
    v3.replace(future, std::string_view{"\"version\": 2"}.size(), "\"version\": 3");
    const Result<Document> ahead = io::documentFromJson(v3);
    REQUIRE_FALSE(ahead);
    CHECK_THAT(ahead.error().message, ContainsSubstring("unsupported version"));
}

TEST_CASE("MaterialFile_RejectsAnUnknownFutureFieldRatherThanIgnoringIt", "[io][persist]") {
    // The document format's existing policy is to REJECT an unrecognised key, which
    // is also the right answer for a future temperature-dependent property law: a
    // reader that silently ignored it would treat a temperature-varying conductivity
    // as a constant and compute a wrong answer confidently.
    Document document{"Part"};
    createOrFail(document, "Steel", richSteel());
    std::string text = jsonOf(document);
    const std::size_t at = text.find("\"thermal_conductivity\": {");
    REQUIRE(at != std::string::npos);
    const std::size_t open = text.find('{', at);
    text.insert(open + 1, R"("law": {"kind": "table", "knots": [[300.0, 50.0]]},)");
    const Result<Document> loaded = io::documentFromJson(text);
    REQUIRE_FALSE(loaded);
    CHECK_THAT(loaded.error().message, ContainsSubstring("law"));
}
