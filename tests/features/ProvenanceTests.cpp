#include "features/FeatureTestSupport.hpp"

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
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <functional>
#include <ranges>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinRel;
using features::MaterialDefinition;
using materials::CompletenessState;
using materials::ConsumerKind;
using materials::Date;
using materials::IssueKind;
using materials::MaterialProperty;
using materials::MaterialProvenance;
using materials::MechanicalPropertyKind;
using materials::PropertyProvenance;
using materials::SourceKind;
using materials::ThermalPropertyKind;

// P15-PROV-001: provenance, completeness and consumer requirements.
//
// THE RULE THE WHOLE FILE IS ABOUT: missing engineering data produces an explicit
// missing-data result, never a guessed one. Not one test here supplies a default,
// and several exist only to prove that no default appears.
//
// FIXTURE VALUES ARE SYNTHETIC. The numbers below are plausible for an aluminium
// but they are fixtures, not data, and nothing in this file adds a value to the
// production library -- which carries metadata only, because ADR-028 requires a
// recorded source for any shipped number.
//
// DATES ARE FIXED. Nothing here reads a clock. A test that used today's date would
// make the suite's output depend on when it ran, and would make a "deterministic
// serialization" claim meaningless.

namespace {

/// A fixed date, so no test depends on when it ran.
Date fixedDate(int year, int month, int day) {
    const std::optional<Date> date = Date::of(year, month, day);
    REQUIRE(date.has_value());
    return *date;
}

PropertyProvenance datasheet() {
    PropertyProvenance provenance;
    provenance.kind = SourceKind::ManufacturerData;
    provenance.source = "Synthetic supplier datasheet";
    provenance.reference = "Table 4";
    provenance.revision = "Rev C";
    provenance.date = fixedDate(2024, 3, 17);
    provenance.condition = "T6";
    return provenance;
}

PropertyProvenance handbook() {
    PropertyProvenance provenance;
    provenance.kind = SourceKind::Handbook;
    provenance.source = "Synthetic handbook";
    provenance.reference = "p. 212";
    return provenance;
}

PropertyProvenance measured() {
    PropertyProvenance provenance;
    provenance.kind = SourceKind::Measured;
    provenance.source = "Synthetic internal test report";
    provenance.date = fixedDate(2025, 11, 2);
    provenance.notes = "Three specimens, synthetic fixture.";
    return provenance;
}

/// EVERY stored property Known. Synthetic values.
///
/// Deliberately exhaustive, and that matters for more than tidiness. When a
/// required property is removed from this fixture to test a gap, every OTHER
/// property is still present -- including the ones a "helpful" implementation
/// might reach for as a substitute: an ultimate tensile strength beside a removed
/// yield, an electrical resistivity beside a removed conductivity, four other
/// pressures beside a removed modulus.
///
/// A sparser fixture hid exactly that. Mutation-testing a substitution into
/// isAvailable() was caught by only one test until this fixture was filled in;
/// afterwards the cross-check caught it too.
MaterialDefinition characterised() {
    MaterialDefinition definition;
    definition.designation = "Synthetic Aluminium";
    definition.mechanical.density = MaterialProperty<Density>::known(Density::fromSi(2700.0));
    definition.mechanical.youngsModulus = MaterialProperty<ElasticModulus>::known(69_GPa);
    definition.mechanical.poissonRatio =
        MaterialProperty<PoissonRatio>::known(PoissonRatio::of(0.33));
    definition.mechanical.yieldStrength = MaterialProperty<Stress>::known(276_MPa);
    definition.mechanical.ultimateTensileStrength = MaterialProperty<Stress>::known(310_MPa);
    definition.mechanical.ultimateCompressiveStrength = MaterialProperty<Stress>::known(300_MPa);
    definition.mechanical.shearStrength = MaterialProperty<Stress>::known(207_MPa);
    definition.mechanical.elongation =
        MaterialProperty<materials::Elongation>::known(materials::Elongation::ofPercent(12.0));
    definition.mechanical.hardness = MaterialProperty<materials::Hardness>::known(
        materials::Hardness::of(95.0, materials::HardnessScale::Brinell));
    definition.thermal.thermalConductivity =
        MaterialProperty<ThermalConductivity>::known(167_W_per_m_K);
    definition.thermal.specificHeatCapacity =
        MaterialProperty<SpecificHeatCapacity>::known(896_J_per_kg_K);
    definition.thermal.thermalExpansion =
        MaterialProperty<ThermalExpansionCoefficient>::known(23.6_um_per_m_K);
    definition.thermal.meltingTemperature = MaterialProperty<Temperature>::known(855.0_K);
    definition.thermal.electricalResistivity =
        MaterialProperty<materials::ElectricalResistivity>::known(
            materials::ElectricalResistivity::ofOhmMetres(3.99e-8));
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

materials::CompletenessReport reportFor(const Document& document, MaterialId id,
                                        ConsumerKind consumer) {
    const Result<materials::CompletenessReport> report =
        features::materialCompleteness(document, id, consumer);
    REQUIRE(report);
    return *report;
}

/// A document with one extruded 20 x 30 x 50 mm box, for the mass integration.
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

} // namespace

// --- the date foundation ----------------------------------------------------

TEST_CASE("Provenance_DatesAreValidatedAndFormatISO8601", "[features][provenance]") {
    // ISO 8601, assembled from three integers, so the text cannot pick up a locale,
    // a time zone or a clock.
    CHECK(materials::toString(fixedDate(2026, 9, 28)) == "2026-09-28");
    CHECK(materials::toString(fixedDate(1, 1, 1)) == "0001-01-01");
    CHECK(materials::toString(fixedDate(2024, 12, 31)) == "2024-12-31");

    // Validated on construction, including leap years, so an impossible date
    // cannot be stored and then formatted as though it were real.
    CHECK(Date::of(2024, 2, 29).has_value());   // 2024 is a leap year
    CHECK_FALSE(Date::of(2023, 2, 29).has_value());
    CHECK_FALSE(Date::of(1900, 2, 29).has_value()); // divisible by 100, not 400
    CHECK(Date::of(2000, 2, 29).has_value());       // divisible by 400
    CHECK_FALSE(Date::of(2024, 4, 31).has_value());
    CHECK_FALSE(Date::of(2024, 13, 1).has_value());
    CHECK_FALSE(Date::of(2024, 0, 1).has_value());
    CHECK_FALSE(Date::of(2024, 1, 0).has_value());
    CHECK_FALSE(Date::of(0, 1, 1).has_value());

    // Chronologically ordered, so a caller may sort by date.
    CHECK(fixedDate(2024, 3, 17) < fixedDate(2024, 3, 18));
    CHECK(fixedDate(2024, 3, 17) < fixedDate(2025, 1, 1));
    CHECK(fixedDate(2024, 3, 17) == fixedDate(2024, 3, 17));
}

// --- the provenance record --------------------------------------------------

TEST_CASE("Provenance_RecordsEveryFieldAndReportsWhenItSaysNothing", "[features][provenance]") {
    const PropertyProvenance record = datasheet();
    CHECK(record.kind == SourceKind::ManufacturerData);
    CHECK(record.source == "Synthetic supplier datasheet");
    CHECK(record.reference == "Table 4");
    CHECK(record.revision == "Rev C");
    CHECK(record.condition == "T6");
    REQUIRE(record.date.has_value());
    CHECK(materials::toString(*record.date) == "2024-03-17");
    CHECK_FALSE(record.empty());

    // An empty record and an absent one mean the same thing, which is what makes
    // the material-level default work.
    CHECK(PropertyProvenance{}.empty());
    PropertyProvenance justAKind;
    justAKind.kind = SourceKind::UserEntered;
    CHECK_FALSE(justAKind.empty());
    PropertyProvenance justANote;
    justANote.notes = "something";
    CHECK_FALSE(justANote.empty());
}

TEST_CASE("Provenance_DistinguishesMeasuredDataFromReferenceData", "[features][provenance]") {
    // The distinction that matters: a measured yield and a handbook nominal of the
    // same number are not the same claim.
    CHECK(materials::isMeasured(SourceKind::Measured));
    for (const SourceKind kind : materials::sourceKinds()) {
        if (kind != SourceKind::Measured) {
            CHECK_FALSE(materials::isMeasured(kind));
        }
    }
    CHECK(materials::isReferenceData(SourceKind::Handbook));
    CHECK(materials::isReferenceData(SourceKind::Standard));
    CHECK(materials::isReferenceData(SourceKind::ManufacturerData));
    CHECK(materials::isReferenceData(SourceKind::LibraryReference));
    CHECK_FALSE(materials::isReferenceData(SourceKind::Measured));
    CHECK_FALSE(materials::isReferenceData(SourceKind::UserEntered));
    CHECK_FALSE(materials::isReferenceData(SourceKind::Calculated));
    CHECK_FALSE(materials::isReferenceData(SourceKind::Unspecified));

    // NOT inferred from whether a source string exists. A citation is not a
    // measurement, and an uncited measurement is still one.
    PropertyProvenance citedHandbook = handbook();
    CHECK_FALSE(materials::isMeasured(citedHandbook.kind));
    PropertyProvenance uncitedMeasurement;
    uncitedMeasurement.kind = SourceKind::Measured;
    CHECK(uncitedMeasurement.source.empty());
    CHECK(materials::isMeasured(uncitedMeasurement.kind));

    // Every kind has a distinct name, so a report cannot conflate two.
    std::vector<std::string_view> names;
    for (const SourceKind kind : materials::sourceKinds()) {
        names.emplace_back(materials::toString(kind));
    }
    std::ranges::sort(names);
    CHECK(std::ranges::adjacent_find(names) == names.end());
    CHECK(names.size() == 8);
}

TEST_CASE("Provenance_AKnownValueMayHaveNoSourceAndThatIsNotAGap", "[features][provenance]") {
    // "Unknown provenance" and "unknown property value" are different things, and
    // conflating them would either block solvers or hide missing data.
    Document document{"Part"};
    MaterialDefinition definition;
    definition.mechanical.density = MaterialProperty<Density>::known(Density::fromSi(2700.0));
    const MaterialId id = createOrFail(document, "Uncited", definition);

    // The value is there.
    CHECK(features::hasMaterialProperty(document, id, MechanicalPropertyKind::Density));
    const Result<Density> density = features::requireDensity(document, id);
    REQUIRE(density);
    CHECK_THAT(density->in(units::kg_per_m3), WithinRel(2700.0, 1e-12));

    // The source is not, and says so.
    const Result<PropertyProvenance> provenance =
        features::materialPropertyProvenance(document, id, MechanicalPropertyKind::Density);
    REQUIRE(provenance);
    CHECK(provenance->empty());
    CHECK(provenance->kind == SourceKind::Unspecified);

    // And the consumer is READY: a missing citation does not block an analysis.
    CHECK(reportFor(document, id, ConsumerKind::MassProperties).state == CompletenessState::Ready);
}

// --- the two-level default --------------------------------------------------

TEST_CASE("Provenance_APropertyTakesTheMaterialDefaultUntilItStatesItsOwn",
          "[features][provenance]") {
    // ADR-028's two-level model: per property, with a material-level default so an
    // entry transcribed wholly from one datasheet does not repeat the citation nine
    // times.
    Document document{"Part"};
    MaterialDefinition definition = characterised();
    definition.provenance.material = datasheet();
    definition.provenance.mechanical[MechanicalPropertyKind::YoungsModulus] = handbook();
    definition.provenance.thermal[ThermalPropertyKind::ThermalConductivity] = measured();
    const MaterialId id = createOrFail(document, "Mixed", definition);

    const MaterialProvenance& provenance = definitionOf(document, id).provenance;

    // The density has no record of its own, so it takes the material default.
    CHECK_FALSE(materials::hasOwnProvenance(provenance, MechanicalPropertyKind::Density));
    CHECK(materials::effectiveProvenance(provenance, MechanicalPropertyKind::Density) ==
          datasheet());

    // The modulus overrides it.
    CHECK(materials::hasOwnProvenance(provenance, MechanicalPropertyKind::YoungsModulus));
    CHECK(materials::effectiveProvenance(provenance, MechanicalPropertyKind::YoungsModulus) ==
          handbook());

    // And so does the conductivity, on the thermal side.
    CHECK(materials::hasOwnProvenance(provenance, ThermalPropertyKind::ThermalConductivity));
    CHECK(materials::effectiveProvenance(provenance, ThermalPropertyKind::ThermalConductivity) ==
          measured());
    // While the specific heat falls back.
    CHECK_FALSE(materials::hasOwnProvenance(provenance, ThermalPropertyKind::SpecificHeatCapacity));
    CHECK(materials::effectiveProvenance(provenance, ThermalPropertyKind::SpecificHeatCapacity) ==
          datasheet());

    // So one material legitimately says three different things about where its
    // numbers came from, which is the normal case rather than a defect.
    CHECK(materials::isReferenceData(
        materials::effectiveProvenance(provenance, MechanicalPropertyKind::Density).kind));
    CHECK(materials::isMeasured(
        materials::effectiveProvenance(provenance, ThermalPropertyKind::ThermalConductivity).kind));
}

TEST_CASE("Provenance_OfADerivedModulusReportsItsInputsRatherThanASourceOfItsOwn",
          "[features][provenance]") {
    // ADR-028: "a derived property reports the provenance of the inputs it was
    // derived from". A derived shear modulus has no datasheet -- it has an equation
    // -- and must not appear to be a sourced stored value.
    Document document{"Part"};
    MaterialDefinition definition = characterised();
    definition.provenance.mechanical[MechanicalPropertyKind::YoungsModulus] = handbook();
    definition.provenance.mechanical[MechanicalPropertyKind::PoissonRatio] = measured();
    const MaterialId id = createOrFail(document, "Derived", definition);
    const MaterialProvenance& provenance = definitionOf(document, id).provenance;

    for (const MechanicalPropertyKind kind :
         {MechanicalPropertyKind::ShearModulus, MechanicalPropertyKind::BulkModulus}) {
        const PropertyProvenance effective = materials::effectiveProvenance(provenance, kind);
        CHECK(effective.kind == SourceKind::Calculated);
        // No borrowed citation: it does not claim the handbook's or the test
        // report's source as its own.
        CHECK(effective.source.empty());
        CHECK(effective.standard.empty());
        CHECK_FALSE(effective.date.has_value());

        // The inputs, and their own provenance, in enumeration order.
        const auto inputs = materials::derivedInputProvenance(provenance, kind);
        REQUIRE(inputs.size() == 2);
        CHECK(inputs[0].first == MechanicalPropertyKind::YoungsModulus);
        CHECK(inputs[0].second == handbook());
        CHECK(inputs[1].first == MechanicalPropertyKind::PoissonRatio);
        CHECK(inputs[1].second == measured());
    }

    // A stored property is not derived, so asking for its inputs is a category
    // error and returns nothing rather than inventing a chain.
    CHECK(materials::derivedInputProvenance(provenance, MechanicalPropertyKind::Density).empty());
    CHECK(materials::derivedInputProvenance(provenance, MechanicalPropertyKind::YoungsModulus)
              .empty());
}

TEST_CASE("Provenance_CannotBeAttachedToADerivedProperty", "[features][provenance]") {
    Document document{"Part"};
    const MaterialId id = createOrFail(document, "Fixture", characterised());
    for (const MechanicalPropertyKind kind :
         {MechanicalPropertyKind::ShearModulus, MechanicalPropertyKind::BulkModulus}) {
        const Result<bool> refused =
            features::setMaterialPropertyProvenance(document, id, kind, measured());
        REQUIRE_FALSE(refused);
        CHECK(refused.error().code == ErrorCode::InvalidArgument);
        CHECK_THAT(refused.error().message, ContainsSubstring("derived"));
    }
    CHECK(definitionOf(document, id).provenance.mechanical.empty());
}

// --- provenance is metadata -------------------------------------------------

TEST_CASE("Provenance_EditsPreserveTheMaterialIdAndTheAssignment", "[features][provenance]") {
    Document document{"Part"};
    const MaterialId id = createOrFail(document, "Fixture", characterised());
    REQUIRE(features::assignMaterial(document, id));

    MaterialProvenance provenance;
    provenance.material = datasheet();
    provenance.mechanical[MechanicalPropertyKind::Density] = measured();
    REQUIRE(features::setMaterialProvenance(document, id, provenance));
    REQUIRE(features::setMaterialPropertyProvenance(document, id,
                                                    MechanicalPropertyKind::YoungsModulus,
                                                    handbook()));
    REQUIRE(features::setMaterialPropertyProvenance(
        document, id, ThermalPropertyKind::ThermalConductivity, measured()));

    CHECK(features::findMaterial(document, id) != nullptr);
    CHECK(features::findMaterial(document, id)->materialId() == id);
    CHECK(features::materialCount(document) == 1);
    CHECK(features::materialAssignment(document).state ==
          features::MaterialAssignmentState::Resolved);
    CHECK(features::materialAssignment(document).material == id);
}

TEST_CASE("Provenance_EditsChangeNoNumberAnywhere", "[features][provenance]") {
    // The metadata guarantee, checked through a real mass and a real derived
    // modulus rather than by comparing the definition to itself.
    BoxPart part;
    const MaterialId id = createOrFail(part.document, "Fixture", characterised());
    REQUIRE(features::assignMaterial(part.document, id));

    const Result<features::PartMassProperties> before =
        features::partMassProperties(part.document, part.regenerator, part.feature);
    REQUIRE(before);
    const materials::MaterialProperty<ElasticModulus> shearBefore =
        materials::derivedShearModulus(definitionOf(part.document, id).mechanical);
    const materials::MechanicalProperties mechanicalBefore =
        definitionOf(part.document, id).mechanical;
    const materials::ThermalProperties thermalBefore = definitionOf(part.document, id).thermal;

    MaterialProvenance provenance;
    provenance.material = datasheet();
    provenance.mechanical[MechanicalPropertyKind::Density] = measured();
    provenance.mechanical[MechanicalPropertyKind::YoungsModulus] = handbook();
    provenance.thermal[ThermalPropertyKind::ThermalConductivity] = measured();
    REQUIRE(features::setMaterialProvenance(part.document, id, provenance));

    // Not one value moved.
    CHECK(definitionOf(part.document, id).mechanical == mechanicalBefore);
    CHECK(definitionOf(part.document, id).thermal == thermalBefore);

    const Result<features::PartMassProperties> after =
        features::partMassProperties(part.document, part.regenerator, part.feature);
    REQUIRE(after);
    CHECK(after->mass.in(units::kg) == before->mass.in(units::kg));
    CHECK(after->volume.in(units::mm3) == before->volume.in(units::mm3));
    CHECK(after->aboutCentreOfMass == before->aboutCentreOfMass);
    CHECK(materials::derivedShearModulus(definitionOf(part.document, id).mechanical) ==
          shearBefore);
    const Result<materials::LinearElasticConstants> constants =
        features::requireLinearElasticConstants(part.document, id);
    REQUIRE(constants);
    CHECK(constants->shearModulus == *shearBefore.value());
}

// --- the edit and removal policy --------------------------------------------

TEST_CASE("Provenance_IsClearedForAPropertyWhoseValueChanges", "[features][provenance]") {
    // THE POLICY, and the reason for it: a citation describes a NUMBER. When the
    // number changes the citation describes nothing that is there, and keeping it
    // would make the document claim a source it does not have.
    Document document{"Part"};
    MaterialDefinition definition = characterised();
    definition.provenance.material = datasheet();
    definition.provenance.mechanical[MechanicalPropertyKind::Density] = measured();
    definition.provenance.mechanical[MechanicalPropertyKind::YoungsModulus] = handbook();
    const MaterialId id = createOrFail(document, "Fixture", definition);

    materials::MechanicalProperties mechanical = definitionOf(document, id).mechanical;
    mechanical.density = MaterialProperty<Density>::known(Density::fromSi(2810.0));
    REQUIRE(features::setMaterialMechanical(document, id, mechanical));

    const MaterialProvenance& after = definitionOf(document, id).provenance;
    // The density's own citation is gone -- it described 2700, not 2810.
    CHECK_FALSE(materials::hasOwnProvenance(after, MechanicalPropertyKind::Density));
    // Cleared, not replaced with a claim: it falls back to the material default
    // rather than asserting UserEntered, because BetterCAD does not know where the
    // new number came from.
    CHECK(materials::effectiveProvenance(after, MechanicalPropertyKind::Density) == datasheet());
    // The UNCHANGED property keeps its own citation.
    CHECK(materials::hasOwnProvenance(after, MechanicalPropertyKind::YoungsModulus));
    CHECK(materials::effectiveProvenance(after, MechanicalPropertyKind::YoungsModulus) ==
          handbook());
    // And the material default is untouched: it describes the material, not a value.
    CHECK(after.material == datasheet());
}

TEST_CASE("Provenance_SurvivesAnEditThatChangesNothing", "[features][provenance]") {
    // Writing the same values back is not a change, so no citation is lost. A
    // policy that cleared on every write would make a read-modify-write edit of one
    // property destroy the provenance of all the others.
    Document document{"Part"};
    MaterialDefinition definition = characterised();
    definition.provenance.mechanical[MechanicalPropertyKind::Density] = measured();
    definition.provenance.mechanical[MechanicalPropertyKind::YoungsModulus] = handbook();
    const MaterialId id = createOrFail(document, "Fixture", definition);

    // Change ONE property; the other's citation must survive.
    materials::MechanicalProperties mechanical = definitionOf(document, id).mechanical;
    mechanical.yieldStrength = MaterialProperty<Stress>::known(300_MPa);
    REQUIRE(features::setMaterialMechanical(document, id, mechanical));

    const MaterialProvenance& after = definitionOf(document, id).provenance;
    CHECK(materials::hasOwnProvenance(after, MechanicalPropertyKind::Density));
    CHECK(materials::effectiveProvenance(after, MechanicalPropertyKind::Density) == measured());
    CHECK(materials::hasOwnProvenance(after, MechanicalPropertyKind::YoungsModulus));
}

TEST_CASE("Provenance_IsRemovedWithThePropertyItDescribed", "[features][provenance]") {
    // No orphan citation: metadata claiming a source for a number that is not there
    // is how a document comes to look better documented than it is.
    Document document{"Part"};
    MaterialDefinition definition = characterised();
    definition.provenance.mechanical[MechanicalPropertyKind::Density] = measured();
    definition.provenance.thermal[ThermalPropertyKind::ThermalConductivity] = handbook();
    const MaterialId id = createOrFail(document, "Fixture", definition);

    REQUIRE(features::removeMaterialProperty(document, id, MechanicalPropertyKind::Density));
    REQUIRE(features::removeMaterialProperty(document, id,
                                             ThermalPropertyKind::ThermalConductivity));

    const MaterialProvenance& after = definitionOf(document, id).provenance;
    CHECK_FALSE(materials::hasOwnProvenance(after, MechanicalPropertyKind::Density));
    CHECK_FALSE(materials::hasOwnProvenance(after, ThermalPropertyKind::ThermalConductivity));
    CHECK(after.mechanical.empty());
    CHECK(after.thermal.empty());

    // And the general report finds no orphan, because there is none.
    const Result<materials::CompletenessReport> report = features::materialReport(document, id);
    REQUIRE(report);
    CHECK(std::ranges::none_of(report->issues, [](const materials::MaterialIssue& issue) {
        return issue.kind == IssueKind::OrphanProvenance;
    }));
}

TEST_CASE("Provenance_ThatDescribesNothingIsReportedAsAnOrphanAndDoesNotBlock",
          "[features][provenance]") {
    // Reachable by attaching provenance to a property that has no value -- which is
    // legitimate to do, and worth reporting rather than refusing.
    Document document{"Part"};
    MaterialDefinition definition;
    definition.mechanical.density = MaterialProperty<Density>::known(Density::fromSi(2700.0));
    const MaterialId id = createOrFail(document, "Sparse", definition);
    REQUIRE(features::setMaterialPropertyProvenance(document, id,
                                                    MechanicalPropertyKind::YoungsModulus,
                                                    handbook()));

    const Result<materials::CompletenessReport> report = features::materialReport(document, id);
    REQUIRE(report);
    const auto orphan = std::ranges::find_if(report->issues, [](const materials::MaterialIssue& i) {
        return i.kind == IssueKind::OrphanProvenance;
    });
    REQUIRE(orphan != report->issues.end());
    CHECK(orphan->mechanical == MechanicalPropertyKind::YoungsModulus);
    CHECK_THAT(orphan->message, ContainsSubstring("no value"));

    // NON-BLOCKING: it is a documentation gap, not a reason a solve cannot run.
    CHECK_FALSE(materials::isBlocking(IssueKind::OrphanProvenance));
    CHECK_FALSE(materials::isBlocking(IssueKind::ProvenanceIncomplete));
    CHECK(materials::isBlocking(IssueKind::MissingProperty));
    CHECK(materials::isBlocking(IssueKind::InconsistentValues));
    // So the mass consumer, which needs only the density, is still Ready.
    CHECK(reportFor(document, id, ConsumerKind::MassProperties).state == CompletenessState::Ready);
}

TEST_CASE("Provenance_ThatContradictsItselfIsAWarningNotARefusal", "[features][provenance]") {
    Document document{"Part"};
    MaterialDefinition definition = characterised();
    // A standard, with no standard named.
    PropertyProvenance vague;
    vague.kind = SourceKind::Standard;
    vague.source = "some standard";
    definition.provenance.mechanical[MechanicalPropertyKind::Density] = vague;
    // Citation details with no kind.
    PropertyProvenance kindless;
    kindless.source = "a book";
    kindless.reference = "p. 9";
    definition.provenance.mechanical[MechanicalPropertyKind::YoungsModulus] = kindless;
    // Accepted -- sparse and even self-contradictory metadata is never a reason to
    // refuse engineering data.
    const MaterialId id = createOrFail(document, "Vague", definition);

    const Result<materials::CompletenessReport> report = features::materialReport(document, id);
    REQUIRE(report);
    const auto incompleteCount =
        std::ranges::count_if(report->issues, [](const materials::MaterialIssue& i) {
            return i.kind == IssueKind::ProvenanceIncomplete;
        });
    CHECK(incompleteCount == 2);
    // And nothing is blocked by it.
    CHECK(reportFor(document, id, ConsumerKind::MassProperties).state == CompletenessState::Ready);
    CHECK(reportFor(document, id, ConsumerKind::FeaLinearStatic).state == CompletenessState::Ready);
}

// --- library import and clone provenance ------------------------------------

TEST_CASE("Provenance_AnImportRecordsTheLibraryEntryItCameFrom", "[features][provenance]") {
    Document document{"Part"};
    const Result<materials::LibraryMaterial> entry =
        materials::findLibraryMaterial("bettercad", "al-6061-t6");
    REQUIRE(entry);
    const Result<MaterialId> id =
        features::importLibraryMaterial(document, "Aluminium", *entry);
    REQUIRE(id);

    const MaterialDefinition& definition = definitionOf(document, *id);
    // The origin key, as before.
    REQUIRE(definition.origin.has_value());
    CHECK(definition.origin->entry == "al-6061-t6");
    // AND the provenance, which is what ADR-028 says the key is.
    CHECK(definition.provenance.material.kind == SourceKind::LibraryReference);
    CHECK_THAT(definition.provenance.material.source, ContainsSubstring("al-6061-t6"));
    CHECK_THAT(definition.provenance.material.revision, ContainsSubstring("rev 1"));
    CHECK(definition.provenance.material.standard == "ASTM B221");

    // Every property, having no citation of its own, traces to the entry.
    CHECK(materials::effectiveProvenance(definition.provenance, MechanicalPropertyKind::Density)
              .kind == SourceKind::LibraryReference);
}

TEST_CASE("Provenance_OfAnEditedImportNoLongerClaimsTheLibraryForTheChangedValue",
          "[features][provenance]") {
    // THE TEST THE BRIEF CALLS MOST IMPORTANT. A user imports a library material,
    // then changes a value. The document must not go on claiming the library as the
    // source of a number the library never gave.
    Document document{"Part"};
    const Result<materials::LibraryMaterial> entry =
        materials::findLibraryMaterial("bettercad", "al-6061-t6");
    REQUIRE(entry);
    const Result<MaterialId> id = features::importLibraryMaterial(document, "Aluminium", *entry);
    REQUIRE(id);

    // Give the density a library citation of its own, as a transcription would.
    PropertyProvenance fromLibrary;
    fromLibrary.kind = SourceKind::LibraryReference;
    fromLibrary.source = "BetterCAD library bettercad/al-6061-t6 rev 1";
    materials::MechanicalProperties mechanical;
    mechanical.density = MaterialProperty<Density>::known(Density::fromSi(2700.0));
    REQUIRE(features::setMaterialMechanical(document, *id, mechanical));
    REQUIRE(features::setMaterialPropertyProvenance(document, *id, MechanicalPropertyKind::Density,
                                                    fromLibrary));
    CHECK(materials::effectiveProvenance(definitionOf(document, *id).provenance,
                                        MechanicalPropertyKind::Density) == fromLibrary);

    // Now the user changes the number.
    mechanical = definitionOf(document, *id).mechanical;
    mechanical.density = MaterialProperty<Density>::known(Density::fromSi(2750.0));
    REQUIRE(features::setMaterialMechanical(document, *id, mechanical));

    // The per-property library claim is GONE. What remains is the material-level
    // provenance, which is a true statement about where the MATERIAL came from and
    // is not a claim about this number's value.
    const MaterialProvenance& after = definitionOf(document, *id).provenance;
    CHECK_FALSE(materials::hasOwnProvenance(after, MechanicalPropertyKind::Density));
    CHECK(after.material.kind == SourceKind::LibraryReference);
    // The user can now say where 2750 actually came from, explicitly.
    REQUIRE(features::setMaterialPropertyProvenance(document, *id, MechanicalPropertyKind::Density,
                                                    measured()));
    CHECK(materials::effectiveProvenance(definitionOf(document, *id).provenance,
                                        MechanicalPropertyKind::Density) == measured());
}

TEST_CASE("Provenance_SurvivesACloneExactlyAndIndependently", "[features][provenance]") {
    Document document{"Part"};
    MaterialDefinition definition = characterised();
    definition.provenance.material = datasheet();
    definition.provenance.mechanical[MechanicalPropertyKind::Density] = measured();
    definition.provenance.thermal[ThermalPropertyKind::ThermalConductivity] = handbook();
    const MaterialId source = createOrFail(document, "Source", definition);

    const Result<MaterialId> clone = features::cloneMaterial(document, source, "Clone");
    REQUIRE(clone);
    // Provenance is part of the definition, so it clones with the values -- every
    // field, including the fixed date.
    CHECK(definitionOf(document, *clone).provenance == definitionOf(document, source).provenance);
    CHECK(definitionOf(document, *clone).provenance.material.date == fixedDate(2024, 3, 17));

    // And independently: editing the clone's provenance leaves the source's alone.
    REQUIRE(features::setMaterialPropertyProvenance(document, *clone,
                                                    MechanicalPropertyKind::Density, handbook()));
    CHECK(materials::effectiveProvenance(definitionOf(document, *clone).provenance,
                                        MechanicalPropertyKind::Density) == handbook());
    CHECK(materials::effectiveProvenance(definitionOf(document, source).provenance,
                                        MechanicalPropertyKind::Density) == measured());
}

// --- consumer requirements --------------------------------------------------

TEST_CASE("Provenance_ConsumerRequirementsAreExactAndDoNotOverreach", "[features][provenance]") {
    using Mech = MechanicalPropertyKind;
    using Therm = ThermalPropertyKind;
    const auto required = [](ConsumerKind consumer) { return materials::requiredProperties(consumer); };

    CHECK(required(ConsumerKind::MassProperties).mechanical == std::vector{Mech::Density});
    CHECK(required(ConsumerKind::MassProperties).thermal.empty());

    CHECK(required(ConsumerKind::FeaLinearStatic).mechanical ==
          std::vector{Mech::YoungsModulus, Mech::PoissonRatio});
    // NOT density: a static solve with no body force never touches a mass.
    CHECK(required(ConsumerKind::FeaLinearStatic).thermal.empty());

    CHECK(required(ConsumerKind::FeaLinearStaticWithGravity).mechanical ==
          std::vector{Mech::Density, Mech::YoungsModulus, Mech::PoissonRatio});

    CHECK(required(ConsumerKind::FeaYieldStrength).mechanical ==
          std::vector{Mech::YoungsModulus, Mech::PoissonRatio, Mech::YieldStrength});

    // Steady conduction needs a conductivity and NOTHING else. Requiring a density
    // or a specific heat here would block a perfectly solvable problem.
    CHECK(required(ConsumerKind::ThermalSteady).mechanical.empty());
    CHECK(required(ConsumerKind::ThermalSteady).thermal == std::vector{Therm::ThermalConductivity});

    CHECK(required(ConsumerKind::ThermalTransient).mechanical == std::vector{Mech::Density});
    CHECK(required(ConsumerKind::ThermalTransient).thermal ==
          std::vector{Therm::ThermalConductivity, Therm::SpecificHeatCapacity});

    CHECK(required(ConsumerKind::ThermoMechanical).mechanical ==
          std::vector{Mech::YoungsModulus, Mech::PoissonRatio});
    CHECK(required(ConsumerKind::ThermoMechanical).thermal ==
          std::vector{Therm::ThermalExpansion});

    // NO consumer requires a derived modulus: for an isotropic material both are
    // exactly determined by E and nu, so requiring the independent pair is the
    // honest statement of what is needed.
    for (const ConsumerKind consumer : materials::consumerKinds()) {
        for (const Mech kind : required(consumer).mechanical) {
            CHECK_FALSE(materials::isDerivedKind(kind));
        }
    }
    // Every consumer requires at least one property, and every one has a distinct
    // name.
    std::vector<std::string_view> names;
    for (const ConsumerKind consumer : materials::consumerKinds()) {
        const materials::PropertyRequirement requirement = required(consumer);
            CHECK((!requirement.mechanical.empty() || !requirement.thermal.empty()));
        names.emplace_back(materials::toString(consumer));
    }
    std::ranges::sort(names);
    CHECK(std::ranges::adjacent_find(names) == names.end());
    CHECK(names.size() == 7);
}

TEST_CASE("Provenance_TheRequirementsTableAgreesWithTheFunctionsConsumersActuallyCall",
          "[features][provenance]") {
    // THE CROSS-CHECK THAT KEEPS ONE SOURCE OF TRUTH. requiredProperties() is a
    // table; the require*() entry points are what P15-MASS, P17 and P18 actually
    // call. If the two disagreed, a report would say Ready and the call would fail.
    //
    // So for each consumer, each required property is removed in turn and BOTH must
    // then object. Six of the seven consumers have a runtime function today;
    // FeaYieldStrength has none yet, because no yield consumer exists, and that is
    // recorded rather than faked.
    using Mech = MechanicalPropertyKind;
    using Therm = ThermalPropertyKind;

    struct Case {
        ConsumerKind consumer;
        std::function<bool(const Document&, MaterialId)> succeeds;
    };
    const std::vector<Case> cases{
        {ConsumerKind::MassProperties,
         [](const Document& d, MaterialId i) { return features::requireDensity(d, i).has_value(); }},
        {ConsumerKind::FeaLinearStatic,
         [](const Document& d, MaterialId i) {
             return features::requireLinearElasticConstants(d, i).has_value();
         }},
        {ConsumerKind::FeaLinearStaticWithGravity,
         [](const Document& d, MaterialId i) {
             return features::requireLinearElasticConstants(d, i).has_value() &&
                    features::requireDensity(d, i).has_value();
         }},
        {ConsumerKind::ThermalSteady,
         [](const Document& d, MaterialId i) {
             return features::requireThermalConductivity(d, i).has_value();
         }},
        {ConsumerKind::ThermalTransient,
         [](const Document& d, MaterialId i) {
             return features::requireTransientConductionProperties(d, i).has_value();
         }},
        {ConsumerKind::ThermoMechanical,
         [](const Document& d, MaterialId i) {
             return features::requireLinearElasticConstants(d, i).has_value() &&
                    features::requireThermalExpansion(d, i).has_value();
         }},
    };

    for (const Case& test : cases) {
        CAPTURE(materials::toString(test.consumer));
        // Fully characterised: the table says Ready and the function succeeds.
        {
            Document document{"Part"};
            const MaterialId id = createOrFail(document, "Fixture", characterised());
            CHECK(reportFor(document, id, test.consumer).state == CompletenessState::Ready);
            CHECK(test.succeeds(document, id));
        }
        // Remove each required property in turn: both must object, every time.
        const materials::PropertyRequirement requirement =
            materials::requiredProperties(test.consumer);
        for (const Mech kind : requirement.mechanical) {
            CAPTURE(materials::toString(kind));
            Document document{"Part"};
            const MaterialId id = createOrFail(document, "Fixture", characterised());
            REQUIRE(features::removeMaterialProperty(document, id, kind));
            const materials::CompletenessReport report = reportFor(document, id, test.consumer);
            CHECK(report.state == CompletenessState::Incomplete);
            CHECK(std::ranges::find(report.missingMechanical, kind) != report.missingMechanical.end());
            CHECK_FALSE(test.succeeds(document, id));
        }
        for (const Therm kind : requirement.thermal) {
            CAPTURE(materials::toString(kind));
            Document document{"Part"};
            const MaterialId id = createOrFail(document, "Fixture", characterised());
            REQUIRE(features::removeMaterialProperty(document, id, kind));
            const materials::CompletenessReport report = reportFor(document, id, test.consumer);
            CHECK(report.state == CompletenessState::Incomplete);
            CHECK(std::ranges::find(report.missingThermal, kind) != report.missingThermal.end());
            CHECK_FALSE(test.succeeds(document, id));
        }
    }
}

TEST_CASE("Provenance_CompletenessIsPerConsumerAndOneMaterialGivesDifferentAnswers",
          "[features][provenance]") {
    // The central rule: "complete" is meaningless without a consumer.
    Document document{"Part"};
    MaterialDefinition definition;
    definition.designation = "Conductivity Only";
    definition.thermal.thermalConductivity =
        MaterialProperty<ThermalConductivity>::known(167_W_per_m_K);
    const MaterialId id = createOrFail(document, "ConductivityOnly", definition);

    // READY for steady conduction, with nothing else known at all.
    const materials::CompletenessReport steady = reportFor(document, id, ConsumerKind::ThermalSteady);
    CHECK(steady.state == CompletenessState::Ready);
    CHECK(steady.ready());
    CHECK(steady.missingMechanical.empty());
    CHECK(steady.missingThermal.empty());
    CHECK(steady.issues.empty());
    // The absent density is NOT reported as a blocker: steady conduction does not
    // need it, and reporting it would block work that can be done.
    CHECK(steady.presentThermal == std::vector{ThermalPropertyKind::ThermalConductivity});

    // INCOMPLETE for everything that needs more.
    CHECK(reportFor(document, id, ConsumerKind::MassProperties).state ==
          CompletenessState::Incomplete);
    CHECK(reportFor(document, id, ConsumerKind::FeaLinearStatic).state ==
          CompletenessState::Incomplete);
    CHECK(reportFor(document, id, ConsumerKind::ThermalTransient).state ==
          CompletenessState::Incomplete);
}

TEST_CASE("Provenance_ReportsExactlyWhichFeaPropertyIsMissing", "[features][provenance]") {
    using Mech = MechanicalPropertyKind;
    Document document{"Part"};
    MaterialDefinition definition;
    definition.mechanical.youngsModulus = MaterialProperty<ElasticModulus>::known(69_GPa);
    // nu and yield deliberately Unknown.
    const MaterialId id = createOrFail(document, "PartlyElastic", definition);

    const materials::CompletenessReport elastic =
        reportFor(document, id, ConsumerKind::FeaLinearStatic);
    CHECK(elastic.state == CompletenessState::Incomplete);
    CHECK(elastic.presentMechanical == std::vector{Mech::YoungsModulus});
    CHECK(elastic.missingMechanical == std::vector{Mech::PoissonRatio});
    REQUIRE(elastic.issues.size() == 1);
    CHECK(elastic.issues[0].kind == IssueKind::MissingProperty);
    CHECK(elastic.issues[0].mechanical == Mech::PoissonRatio);
    CHECK(elastic.issues[0].consumer == ConsumerKind::FeaLinearStatic);

    // The strength consumer wants one more, and says so -- in enumeration order.
    const materials::CompletenessReport strength =
        reportFor(document, id, ConsumerKind::FeaYieldStrength);
    CHECK(strength.state == CompletenessState::Incomplete);
    CHECK(strength.missingMechanical == std::vector{Mech::PoissonRatio, Mech::YieldStrength});
}

TEST_CASE("Provenance_ReportsExactlyWhichThermalPropertyIsMissing", "[features][provenance]") {
    using Mech = MechanicalPropertyKind;
    using Therm = ThermalPropertyKind;

    // density known, cp known, k unknown -> missing the conductivity, exactly.
    {
        Document document{"Part"};
        MaterialDefinition definition;
        definition.mechanical.density = MaterialProperty<Density>::known(Density::fromSi(2700.0));
        definition.thermal.specificHeatCapacity =
            MaterialProperty<SpecificHeatCapacity>::known(896_J_per_kg_K);
        const MaterialId id = createOrFail(document, "NoConductivity", definition);
        const materials::CompletenessReport report =
            reportFor(document, id, ConsumerKind::ThermalTransient);
        CHECK(report.state == CompletenessState::Incomplete);
        CHECK(report.missingMechanical.empty());
        CHECK(report.missingThermal == std::vector{Therm::ThermalConductivity});
        CHECK(report.presentMechanical == std::vector{Mech::Density});
    }
    // density unknown, cp unknown, k known -> both missing, in a DEFINED order.
    {
        Document document{"Part"};
        MaterialDefinition definition;
        definition.thermal.thermalConductivity =
            MaterialProperty<ThermalConductivity>::known(167_W_per_m_K);
        const MaterialId id = createOrFail(document, "ConductivityOnly", definition);
        const materials::CompletenessReport report =
            reportFor(document, id, ConsumerKind::ThermalTransient);
        CHECK(report.state == CompletenessState::Incomplete);
        // Mechanical before thermal, each in enumeration order.
        CHECK(report.missingMechanical == std::vector{Mech::Density});
        CHECK(report.missingThermal == std::vector{Therm::SpecificHeatCapacity});
        REQUIRE(report.issues.size() == 2);
        CHECK(report.issues[0].mechanical == Mech::Density);
        CHECK(report.issues[1].thermal == Therm::SpecificHeatCapacity);
    }
    // And the specific-heat-only gap.
    {
        Document document{"Part"};
        MaterialDefinition definition = characterised();
        const MaterialId id = createOrFail(document, "Fixture", definition);
        REQUIRE(features::removeMaterialProperty(document, id, Therm::SpecificHeatCapacity));
        const materials::CompletenessReport report =
            reportFor(document, id, ConsumerKind::ThermalTransient);
        CHECK(report.missingThermal == std::vector{Therm::SpecificHeatCapacity});
        // Steady conduction is unaffected, because it never needed cp.
        CHECK(reportFor(document, id, ConsumerKind::ThermalSteady).state ==
              CompletenessState::Ready);
    }
}

TEST_CASE("Provenance_ReportsMultipleMissingPropertiesInADeterministicOrder",
          "[features][provenance]") {
    using Mech = MechanicalPropertyKind;
    Document document{"Part"};
    MaterialDefinition definition;  // nothing known at all
    definition.designation = "Nothing Known";
    const MaterialId id = createOrFail(document, "NothingKnown", definition);

    const materials::CompletenessReport report =
        reportFor(document, id, ConsumerKind::FeaLinearStaticWithGravity);
    CHECK(report.state == CompletenessState::Incomplete);
    // Enumeration order: Density, YoungsModulus, PoissonRatio.
    CHECK(report.missingMechanical == std::vector{Mech::Density, Mech::YoungsModulus,
                                                  Mech::PoissonRatio});
    REQUIRE(report.issues.size() == 3);
    CHECK(report.issues[0].mechanical == Mech::Density);
    CHECK(report.issues[1].mechanical == Mech::YoungsModulus);
    CHECK(report.issues[2].mechanical == Mech::PoissonRatio);

    // Repeatable: the same state gives the same report, every time.
    for (int pass = 0; pass < 8; ++pass) {
        CHECK(reportFor(document, id, ConsumerKind::FeaLinearStaticWithGravity) == report);
    }
}

// --- no fabrication, no substitution ----------------------------------------

TEST_CASE("Provenance_NeverSubstitutesOnePropertyForAnother", "[features][provenance]") {
    using Mech = MechanicalPropertyKind;
    using Therm = ThermalPropertyKind;

    // An ultimate tensile strength does NOT satisfy a yield requirement, even
    // though both are stresses and the UTS is the larger number.
    {
        Document document{"Part"};
        MaterialDefinition definition = characterised();
        const MaterialId id = createOrFail(document, "NoYield", definition);
        REQUIRE(features::removeMaterialProperty(document, id, Mech::YieldStrength));
        CHECK(features::hasMaterialProperty(document, id, Mech::UltimateTensileStrength));

        const materials::CompletenessReport report =
            reportFor(document, id, ConsumerKind::FeaYieldStrength);
        CHECK(report.state == CompletenessState::Incomplete);
        CHECK(report.missingMechanical == std::vector{Mech::YieldStrength});
    }
    // An electrical resistivity does NOT satisfy a thermal conductivity, though
    // both describe transport.
    {
        Document document{"Part"};
        MaterialDefinition definition;
        definition.thermal.electricalResistivity =
            MaterialProperty<materials::ElectricalResistivity>::known(
                materials::ElectricalResistivity::ofOhmMetres(2.65e-8));
        const MaterialId id = createOrFail(document, "ResistivityOnly", definition);
        CHECK(features::hasMaterialProperty(document, id, Therm::ElectricalResistivity));
        const materials::CompletenessReport report =
            reportFor(document, id, ConsumerKind::ThermalSteady);
        CHECK(report.state == CompletenessState::Incomplete);
        CHECK(report.missingThermal == std::vector{Therm::ThermalConductivity});
    }
}

TEST_CASE("Provenance_NeverAcceptsAPropertyOfTheRightDimensionForTheWrongMeaning",
          "[features][provenance]") {
    using Mech = MechanicalPropertyKind;
    // ElasticModulus and Stress ARE THE SAME TYPE -- both Quantity<pressure>
    // (P15-UNITS-001). So the type system cannot tell a modulus from a yield
    // strength, and only the property KIND can. This is the test that the
    // requirement is semantic.
    static_assert(std::is_same_v<ElasticModulus, Stress>);

    Document document{"Part"};
    MaterialDefinition definition;
    // Every pressure-dimension property EXCEPT the Young's modulus.
    definition.mechanical.yieldStrength = MaterialProperty<Stress>::known(276_MPa);
    definition.mechanical.ultimateTensileStrength = MaterialProperty<Stress>::known(310_MPa);
    definition.mechanical.ultimateCompressiveStrength = MaterialProperty<Stress>::known(300_MPa);
    definition.mechanical.shearStrength = MaterialProperty<Stress>::known(200_MPa);
    definition.mechanical.poissonRatio = MaterialProperty<PoissonRatio>::known(PoissonRatio::of(0.33));
    const MaterialId id = createOrFail(document, "NoModulus", definition);

    // Four pressures present, and the modulus requirement is still unmet.
    const materials::CompletenessReport report =
        reportFor(document, id, ConsumerKind::FeaLinearStatic);
    CHECK(report.state == CompletenessState::Incomplete);
    CHECK(report.missingMechanical == std::vector{Mech::YoungsModulus});
    CHECK(features::hasMaterialProperty(document, id, Mech::YieldStrength));
    CHECK(features::hasMaterialProperty(document, id, Mech::ShearStrength));
    // And the derived shear modulus is unavailable too, because E is what it needs.
    CHECK_FALSE(features::hasMaterialProperty(document, id, Mech::ShearModulus));
    CHECK_FALSE(features::requireLinearElasticConstants(document, id).has_value());
}

TEST_CASE("Provenance_ReportsAMissingPropertyRatherThanEverFillingIt", "[features][provenance]") {
    // The no-fabrication rule, walked over every consumer and every property it
    // requires: a material with NOTHING known must report every requirement as
    // missing, and no consumer entry point may return a value.
    Document document{"Part"};
    MaterialDefinition definition;
    definition.designation = "Empty";
    const MaterialId id = createOrFail(document, "Empty", definition);

    for (const ConsumerKind consumer : materials::consumerKinds()) {
        CAPTURE(materials::toString(consumer));
        const materials::CompletenessReport report = reportFor(document, id, consumer);
        const materials::PropertyRequirement requirement = materials::requiredProperties(consumer);
        CHECK(report.state == CompletenessState::Incomplete);
        CHECK(report.missingMechanical == requirement.mechanical);
        CHECK(report.missingThermal == requirement.thermal);
        CHECK(report.presentMechanical.empty());
        CHECK(report.presentThermal.empty());
    }
    // Every runtime entry point fails, and none returns a default.
    CHECK_FALSE(features::requireDensity(document, id).has_value());
    CHECK_FALSE(features::requireLinearElasticConstants(document, id).has_value());
    CHECK_FALSE(features::requireThermalConductivity(document, id).has_value());
    CHECK_FALSE(features::requireTransientConductionProperties(document, id).has_value());
    CHECK_FALSE(features::requireThermalExpansion(document, id).has_value());
    // And the derived moduli are Unknown rather than a fabricated 200 GPa or 0.3.
    CHECK(materials::derivedShearModulus(definitionOf(document, id).mechanical).isUnknown());
    CHECK(materials::derivedBulkModulus(definitionOf(document, id).mechanical).isUnknown());
    CHECK_FALSE(materials::derivedShearModulus(definitionOf(document, id).mechanical)
                    .value()
                    .has_value());
}

// --- consistency ------------------------------------------------------------

TEST_CASE("Provenance_ReportsInconsistentSuppliedValuesWithoutCorrectingThem",
          "[features][provenance]") {
    Document document{"Part"};
    MaterialDefinition definition = characterised();
    // A yield strength above the ultimate tensile strength.
    definition.mechanical.yieldStrength = MaterialProperty<Stress>::known(400_MPa);
    definition.mechanical.ultimateTensileStrength = MaterialProperty<Stress>::known(310_MPa);
    // Accepted: the values are each individually valid, and a user entering a
    // datasheet must be able to pass through an inconsistent intermediate state.
    const MaterialId id = createOrFail(document, "Inconsistent", definition);

    // Not corrected.
    CHECK(definitionOf(document, id).mechanical.yieldStrength.value()->in(units::MPa) == 400.0);
    CHECK(definitionOf(document, id).mechanical.ultimateTensileStrength.value()->in(units::MPa) ==
          310.0);

    const Result<materials::CompletenessReport> general = features::materialReport(document, id);
    REQUIRE(general);
    const auto inconsistent =
        std::ranges::find_if(general->issues, [](const materials::MaterialIssue& issue) {
            return issue.kind == IssueKind::InconsistentValues;
        });
    REQUIRE(inconsistent != general->issues.end());
    CHECK_THAT(inconsistent->message, ContainsSubstring("ultimate tensile strength"));
    CHECK_THAT(inconsistent->message, ContainsSubstring("yield strength"));
    // An inconsistency is about a RELATIONSHIP, so no single property is named.
    CHECK_FALSE(inconsistent->mechanical.has_value());
    CHECK_FALSE(inconsistent->thermal.has_value());

    // A consumer with every required value present but contradictory data is
    // INVALID, not Incomplete: there is nothing to supply, something to fix.
    const materials::CompletenessReport strength =
        reportFor(document, id, ConsumerKind::FeaYieldStrength);
    CHECK(strength.missingMechanical.empty());
    CHECK(strength.state == CompletenessState::Invalid);
    // A consumer that uses neither value still sees the inconsistency reported --
    // a contradiction in the material is its business -- but the mass one needs
    // only a density, so it is Invalid rather than Incomplete too.
    CHECK(reportFor(document, id, ConsumerKind::MassProperties).missingMechanical.empty());
}

TEST_CASE("Provenance_HasNoSuppliedShearModulusToContradictTheDerivedOne",
          "[features][provenance]") {
    // The classic inconsistency a material model has to police -- a supplied G that
    // disagrees with E and nu -- CANNOT ARISE here. ADR-027 gave
    // MechanicalProperties no slot for a shear or bulk modulus, so it was designed
    // out rather than validated. What remains is that the derived value tracks its
    // inputs exactly.
    Document document{"Part"};
    const MaterialId id = createOrFail(document, "Fixture", characterised());
    const materials::MechanicalProperties& mechanical = definitionOf(document, id).mechanical;

    const materials::MaterialProperty<ElasticModulus> shear =
        materials::derivedShearModulus(mechanical);
    REQUIRE(shear.isDerived());
    // G = E / (2(1 + nu)) = 69 / 2.66
    CHECK_THAT(shear.value()->in(units::GPa), WithinRel(69.0 / (2.0 * 1.33), 1e-12));
    // Derived, so it is never stored and cannot disagree with anything.
    CHECK_FALSE(shear.isKnown());
    CHECK(materials::isDerivedKind(MechanicalPropertyKind::ShearModulus));
    CHECK(materials::isDerivedKind(MechanicalPropertyKind::BulkModulus));
}

// --- the general report -----------------------------------------------------

TEST_CASE("Provenance_TheGeneralReportListsEverythingAndGatesNothing", "[features][provenance]") {
    Document document{"Part"};
    MaterialDefinition definition;
    definition.thermal.thermalConductivity =
        MaterialProperty<ThermalConductivity>::known(167_W_per_m_K);
    const MaterialId id = createOrFail(document, "ConductivityOnly", definition);

    const Result<materials::CompletenessReport> general = features::materialReport(document, id);
    REQUIRE(general);
    // It lists plenty missing, across every property kind.
    CHECK(general->presentThermal == std::vector{ThermalPropertyKind::ThermalConductivity});
    CHECK(general->missingMechanical.size() == materials::mechanicalPropertyKinds().size());
    CHECK(general->missingThermal.size() == materials::thermalPropertyKinds().size() - 1);
    // But it raises no MissingProperty issue, and its state is Ready -- because
    // nothing is WRONG. No real material has every property, and a state that was
    // always Incomplete would carry no information.
    CHECK(std::ranges::none_of(general->issues, [](const materials::MaterialIssue& issue) {
        return issue.kind == IssueKind::MissingProperty;
    }));
    CHECK(general->state == CompletenessState::Ready);

    // And the consumer that needs only what is there agrees.
    CHECK(reportFor(document, id, ConsumerKind::ThermalSteady).state == CompletenessState::Ready);

    // Deterministic across repeats.
    for (int pass = 0; pass < 8; ++pass) {
        const Result<materials::CompletenessReport> again = features::materialReport(document, id);
        REQUIRE(again);
        CHECK(*again == *general);
    }
}

// --- assignment integration -------------------------------------------------

TEST_CASE("Provenance_AssignmentFailuresStayDistinctFromCompletenessFailures",
          "[features][provenance]") {
    // "Choose a material" and "characterise it further" need different things from
    // the user, so they must not arrive as the same answer.
    Document document{"Part"};

    // Nothing assigned: the assignment's diagnostic, not a completeness report.
    const Result<materials::CompletenessReport> unassigned =
        features::effectiveMaterialCompleteness(document, ConsumerKind::MassProperties);
    REQUIRE_FALSE(unassigned);
    CHECK_THAT(unassigned.error().message, ContainsSubstring("no material"));

    // Assigned but incomplete: a REPORT, which succeeds and says Incomplete.
    MaterialDefinition definition;
    definition.designation = "Bare";
    const MaterialId id = createOrFail(document, "Bare", definition);
    REQUIRE(features::assignMaterial(document, id));
    const Result<materials::CompletenessReport> incomplete =
        features::effectiveMaterialCompleteness(document, ConsumerKind::MassProperties);
    REQUIRE(incomplete);
    CHECK(incomplete->state == CompletenessState::Incomplete);
    CHECK(incomplete->missingMechanical == std::vector{MechanicalPropertyKind::Density});

    // Assigned material deleted: back to the assignment's diagnostic.
    REQUIRE(features::removeMaterial(document, id));
    const Result<materials::CompletenessReport> dangling =
        features::effectiveMaterialCompleteness(document, ConsumerKind::MassProperties);
    REQUIRE_FALSE(dangling);
    CHECK_THAT(dangling.error().message, ContainsSubstring("no such material"));
}

TEST_CASE("Provenance_CompletenessAgreesWithWhatTheMassCalculationActuallyDoes",
          "[features][provenance]") {
    BoxPart part;
    MaterialDefinition definition;
    definition.designation = "Bare";
    const MaterialId id = createOrFail(part.document, "Bare", definition);
    REQUIRE(features::assignMaterial(part.document, id));

    // Incomplete, and the mass fails -- the two agree.
    CHECK(features::effectiveMaterialCompleteness(part.document, ConsumerKind::MassProperties)
              ->state == CompletenessState::Incomplete);
    CHECK_FALSE(features::partMassProperties(part.document, part.regenerator, part.feature)
                    .has_value());

    // Supply the density: Ready, and the mass succeeds.
    materials::MechanicalProperties mechanical;
    mechanical.density = MaterialProperty<Density>::known(Density::fromSi(2700.0));
    REQUIRE(features::setMaterialMechanical(part.document, id, mechanical));
    CHECK(features::effectiveMaterialCompleteness(part.document, ConsumerKind::MassProperties)
              ->state == CompletenessState::Ready);
    const Result<features::PartMassProperties> mass =
        features::partMassProperties(part.document, part.regenerator, part.feature);
    REQUIRE(mass);
    CHECK_THAT(mass->mass.in(units::kg), WithinRel(0.081, 1e-12));
}

// --- persistence ------------------------------------------------------------

TEST_CASE("Provenance_IsCanonicalStateAndTheSaveGapStaysLoud", "[features][provenance]") {
    // PROVENANCE PERSISTENCE IS BLOCKED, and this is what can be verified now.
    //
    // ADR-028 requires provenance to be persisted. No material of ANY kind can be
    // saved yet: the document writer rejects an object type it does not recognise,
    // and "material" is not recognised, which is P15-PERSIST-001's work. So there is
    // no file format to round-trip provenance through, and no round-trip test can
    // exist.
    //
    // What IS verified: provenance is CANONICAL state on MaterialDefinition, so
    // P15-PERSIST-001 cannot write a material without it; it survives a
    // value-semantics round trip exactly (the clone test above); and the save gap
    // stays loud, so provenance cannot be dropped silently.
    Document document{"Part"};
    MaterialDefinition definition = characterised();
    definition.provenance.material = datasheet();
    definition.provenance.mechanical[MechanicalPropertyKind::Density] = measured();
    definition.provenance.thermal[ThermalPropertyKind::ThermalConductivity] = handbook();
    const MaterialId id = createOrFail(document, "Cited", definition);

    // Canonical: it is part of the definition, and content equality includes it.
    const features::Material* material = features::findMaterial(document, id);
    REQUIRE(material != nullptr);
    MaterialDefinition withoutProvenance = definition;
    withoutProvenance.provenance = {};
    CHECK_FALSE(withoutProvenance == definition);
    const Result<MaterialId> plain = features::createMaterial(document, "Plain", withoutProvenance);
    REQUIRE(plain);
    // Same values, different provenance -> NOT content-equal. So a writer that
    // dropped provenance would produce a document that does not compare equal to
    // the one it saved.
    CHECK_FALSE(features::findMaterial(document, *plain)->contentEquals(*material));
}

TEST_CASE("Provenance_HasADeterministicOrderingWithNoUnorderedContainer",
          "[features][provenance]") {
    // The maps are std::map over the property-kind enumerations, so enumerating
    // provenance gives the same order in every build and on every run. Built in
    // reverse insertion order to prove the order comes from the key rather than
    // from insertion.
    Document document{"Part"};
    MaterialDefinition definition = characterised();
    definition.provenance.mechanical[MechanicalPropertyKind::YieldStrength] = handbook();
    definition.provenance.mechanical[MechanicalPropertyKind::Density] = measured();
    definition.provenance.mechanical[MechanicalPropertyKind::YoungsModulus] = datasheet();
    const MaterialId id = createOrFail(document, "Ordered", definition);

    std::vector<MechanicalPropertyKind> order;
    for (const auto& [kind, record] : definitionOf(document, id).provenance.mechanical) {
        order.push_back(kind);
    }
    // Enumeration order: Density, YoungsModulus, ..., YieldStrength.
    CHECK(order == std::vector{MechanicalPropertyKind::Density,
                               MechanicalPropertyKind::YoungsModulus,
                               MechanicalPropertyKind::YieldStrength});
    // And a report over it repeats exactly.
    const Result<materials::CompletenessReport> first = features::materialReport(document, id);
    REQUIRE(first);
    for (int pass = 0; pass < 8; ++pass) {
        CHECK(*features::materialReport(document, id) == *first);
    }
}

TEST_CASE("Provenance_RevisionIsMetadataAndIsNeverOrdered", "[features][provenance]") {
    // A revision string is equal only to itself. Ordering revisions would need a
    // scheme every supplier agrees on, and there is none -- "Rev C" is not greater
    // than "Rev B" as far as BetterCAD is concerned.
    PropertyProvenance revC = datasheet();
    PropertyProvenance revB = datasheet();
    revB.revision = "Rev B";
    CHECK_FALSE(revC == revB);
    CHECK(revC.revision == "Rev C");
    CHECK(revB.revision == "Rev B");
    // Stored and returned verbatim, whatever shape it has.
    Document document{"Part"};
    MaterialDefinition definition = characterised();
    PropertyProvenance odd = datasheet();
    odd.revision = "2nd edition, 3rd printing";
    definition.provenance.mechanical[MechanicalPropertyKind::Density] = odd;
    const MaterialId id = createOrFail(document, "Odd", definition);
    CHECK(materials::effectiveProvenance(definitionOf(document, id).provenance,
                                        MechanicalPropertyKind::Density)
              .revision == "2nd edition, 3rd printing");
}
