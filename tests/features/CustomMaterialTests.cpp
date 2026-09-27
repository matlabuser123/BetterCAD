#include "features/FeatureTestSupport.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/materials/MaterialLibrary.hpp>
#include <bettercad/core/materials/MechanicalProperties.hpp>
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

#include <memory>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinRel;
using features::MaterialDefinition;
using materials::Hardness;
using materials::HardnessScale;
using materials::MaterialProperty;
using materials::MechanicalPropertyKind;
using materials::ThermalPropertyKind;

// P15-CUSTOM-001: custom materials and controlled overrides.
//
// THE OVERRIDE MODEL IS A SNAPSHOT, AND ADR-025 CHOSE IT BEFORE THIS MILESTONE.
// A document owns its material values; a library entry is copied on import and
// never consulted again, "not at load, not while solving". So there is no base
// reference, no per-property override map and no inheritance, and the tests below
// verify the snapshot contract rather than an override one. What that rules out is
// tested too: no property of a document material can be inherited, because there
// is nothing to inherit from.
//
// ONE THING THE MILESTONE BRIEF ASSUMES IS NOT TRUE OF THIS TREE, and it changes
// where the deep-independence fixture has to live. The built-in library carries
// METADATA ONLY -- no density, no modulus, no conductivity -- because ADR-028
// requires a recorded source for any property value and P15-MAT-001 declined to
// invent numbers. So "clone the library's aluminium and check its density did not
// move" cannot be written: there is no density there to move. The source of a
// property-carrying clone in this architecture is a DOCUMENT-LOCAL material, so
// that is what the deep-copy tests clone, and the library tests check what the
// library actually has.

namespace {

/// A fully populated material: every stored mechanical and thermal property
/// Known, so that a clone has something to be independent about and a removal has
/// something to remove. The values are plausible for a 6061-type aluminium but
/// are fixtures, not data -- nothing here is a source.
MaterialDefinition fullyPopulated() {
    MaterialDefinition definition;
    definition.designation = "Fixture Aluminium";
    definition.standard = "none (test fixture)";
    definition.family = "Aluminium Alloy";
    definition.notes = "Test fixture values, not measurements.";
    definition.mechanical.density = MaterialProperty<Density>::known(Density::fromSi(2700.0));
    definition.mechanical.youngsModulus = MaterialProperty<ElasticModulus>::known(69_GPa);
    definition.mechanical.poissonRatio =
        MaterialProperty<PoissonRatio>::known(PoissonRatio::of(0.33));
    definition.mechanical.yieldStrength = MaterialProperty<Stress>::known(276_MPa);
    definition.mechanical.ultimateTensileStrength = MaterialProperty<Stress>::known(310_MPa);
    definition.mechanical.elongation =
        MaterialProperty<materials::Elongation>::known(materials::Elongation::ofPercent(12.0));
    definition.mechanical.hardness =
        MaterialProperty<Hardness>::known(Hardness::of(95.0, HardnessScale::Brinell));
    definition.thermal.thermalConductivity =
        MaterialProperty<ThermalConductivity>::known(167_W_per_m_K);
    definition.thermal.specificHeatCapacity =
        MaterialProperty<SpecificHeatCapacity>::known(896_J_per_kg_K);
    definition.thermal.thermalExpansion =
        MaterialProperty<ThermalExpansionCoefficient>::known(23.6_um_per_m_K);
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

materials::LibraryMaterial aluminiumEntry() {
    const Result<materials::LibraryMaterial> entry =
        materials::findLibraryMaterial("bettercad", "al-6061-t6");
    REQUIRE(entry);
    return *entry;
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

// --- creating a custom material ---------------------------------------------

TEST_CASE("CustomMaterial_CanBeCreatedFromScratchWithNothingKnown", "[features][custom]") {
    // A material the user has named and not yet characterised is a NORMAL state
    // (ADR-027), not a broken one. Creation must not demand values it does not
    // have, and must not invent any.
    Document document{"Part"};
    MaterialDefinition definition;
    definition.designation = "Prototype Foam";
    const MaterialId id = createOrFail(document, "PrototypeFoam", definition);

    const MaterialDefinition& stored = definitionOf(document, id);
    CHECK(stored.designation == "Prototype Foam");
    CHECK_FALSE(stored.origin.has_value());
    // Every stored kind Unknown, checked over the enumeration rather than a
    // hand-written list, so a property added later is covered here automatically.
    for (const MechanicalPropertyKind kind : materials::mechanicalPropertyKinds()) {
        CHECK_FALSE(features::hasMaterialProperty(document, id, kind));
    }
    for (const ThermalPropertyKind kind : materials::thermalPropertyKinds()) {
        CHECK_FALSE(features::hasMaterialProperty(document, id, kind));
    }
    // And nothing became a zero on the way in.
    CHECK_FALSE(stored.mechanical.density.value().has_value());
    CHECK(stored.mechanical.density.isUnknown());
}

TEST_CASE("CustomMaterial_CanBeCreatedWithEveryPropertySupplied", "[features][custom]") {
    Document document{"Part"};
    const MaterialId id = createOrFail(document, "Fixture", fullyPopulated());
    for (const MechanicalPropertyKind kind : materials::mechanicalPropertyKinds()) {
        // Derived kinds are present because their inputs are; the two stored
        // strength kinds the fixture leaves out are not.
        if (kind == MechanicalPropertyKind::UltimateCompressiveStrength ||
            kind == MechanicalPropertyKind::ShearStrength) {
            CHECK_FALSE(features::hasMaterialProperty(document, id, kind));
            continue;
        }
        CHECK(features::hasMaterialProperty(document, id, kind));
    }
    CHECK(features::hasMaterialProperty(document, id, ThermalPropertyKind::ThermalConductivity));
    CHECK_FALSE(features::hasMaterialProperty(document, id, ThermalPropertyKind::MeltingTemperature));
}

TEST_CASE("CustomMaterial_KeepsOneIdentityAcrossEveryKindOfEdit", "[features][custom]") {
    // THE IDENTITY GATE. A property edit is not a new material. Renaming, editing
    // metadata, editing a mechanical property, editing a thermal property and
    // REMOVING a property must all leave the MaterialId exactly as it was --
    // otherwise every assignment would break on every edit.
    Document document{"Part"};
    const MaterialId id = createOrFail(document, "MyAluminium", fullyPopulated());
    const MaterialId original = id;

    REQUIRE(document.rename(id, "MyAlu"));
    CHECK(definitionOf(document, id).designation == "Fixture Aluminium");

    MaterialDefinition edited = definitionOf(document, id);
    edited.designation = "Renamed Designation";
    edited.notes = "edited";
    REQUIRE(features::setMaterialDefinition(document, id, edited));

    materials::MechanicalProperties mechanical = definitionOf(document, id).mechanical;
    mechanical.youngsModulus = MaterialProperty<ElasticModulus>::known(70_GPa);
    mechanical.density = MaterialProperty<Density>::known(Density::fromSi(2710.0));
    REQUIRE(features::setMaterialMechanical(document, id, mechanical));

    materials::ThermalProperties thermal = definitionOf(document, id).thermal;
    thermal.thermalConductivity = MaterialProperty<ThermalConductivity>::known(170_W_per_m_K);
    REQUIRE(features::setMaterialThermal(document, id, thermal));

    REQUIRE(features::removeMaterialProperty(document, id, MechanicalPropertyKind::YieldStrength));

    // Same ID, still resolvable, and still exactly one material.
    CHECK(features::findMaterial(document, id) != nullptr);
    CHECK(features::findMaterial(document, id)->materialId() == original);
    CHECK(features::materialCount(document) == 1);
    CHECK(features::materialIds(document) == std::vector<MaterialId>{original});
}

TEST_CASE("CustomMaterial_RepeatedRenamesNeverMintANewIdentity", "[features][custom]") {
    Document document{"Part"};
    const MaterialId id = createOrFail(document, "First", fullyPopulated());
    for (const char* name : {"Second", "Third", "Fourth", "First"}) {
        REQUIRE(document.rename(id, name));
        CHECK(features::findMaterial(document, id) != nullptr);
    }
    CHECK(features::materialCount(document) == 1);
    CHECK(features::findMaterial(document, id)->name() == "First");
}

// --- duplicate names --------------------------------------------------------

TEST_CASE("CustomMaterial_TwoMaterialsMaySharculeADesignationAndStayDistinct", "[features][custom]") {
    // A designation is a label, not identity (ADR-025). Two materials designated
    // "Steel" with different values are legitimate, and a lookup by designation
    // must hand back BOTH rather than silently choosing.
    Document document{"Part"};
    MaterialDefinition soft;
    soft.designation = "Steel";
    soft.mechanical.youngsModulus = MaterialProperty<ElasticModulus>::known(200_GPa);
    MaterialDefinition hard;
    hard.designation = "Steel";
    hard.mechanical.youngsModulus = MaterialProperty<ElasticModulus>::known(210_GPa);

    const MaterialId a = createOrFail(document, "SteelA", soft);
    const MaterialId b = createOrFail(document, "SteelB", hard);
    CHECK(a != b);

    const std::vector<MaterialId> both = features::findMaterialsByDesignation(document, "Steel");
    REQUIRE(both.size() == 2);
    CHECK(both == std::vector<MaterialId>{a, b});
    // Each resolves by identity to its own values -- no first-match fallback.
    CHECK(definitionOf(document, a).mechanical.youngsModulus.value()->in(units::GPa) == 200.0);
    CHECK(definitionOf(document, b).mechanical.youngsModulus.value()->in(units::GPa) == 210.0);
}

TEST_CASE("CustomMaterial_TwoMaterialsCannotShareAnObjectNameAndTheRefusalChangesNothing",
          "[features][custom]") {
    // THE NAME IS AN IDENTIFIER AND IS UNIQUE; the DESIGNATION is a label and is
    // not. The milestone brief assumes two materials can both be named "Steel";
    // in this tree they cannot, because an object name is unique across a whole
    // document -- shared with parameters, even -- and P15-MAT-001 chose that
    // deliberately so a name can be typed on a command line without ambiguity.
    //
    // So the duplicate that must be SAFE is the designation, tested below, and the
    // duplicate that must be REFUSED is the name, tested here. Both halves matter:
    // silently renaming the second to "Steel_2" would be a document inventing a
    // name the user did not choose.
    Document document{"Part"};
    const MaterialId first = createOrFail(document, "Steel", fullyPopulated());

    const Result<MaterialId> second = features::createMaterial(document, "Steel", fullyPopulated());
    REQUIRE_FALSE(second);
    CHECK(second.error().code == ErrorCode::AlreadyExists);
    CHECK_THAT(second.error().message, ContainsSubstring("already used"));
    // The refusal consumed no ID and left the first material alone.
    CHECK(features::materialCount(document) == 1);
    CHECK(definitionOf(document, first) == fullyPopulated());

    // A clone hits the same rule, and for the same reason.
    const Result<MaterialId> clone = features::cloneMaterial(document, first, "Steel");
    REQUIRE_FALSE(clone);
    CHECK(clone.error().code == ErrorCode::AlreadyExists);
    CHECK(features::materialCount(document) == 1);

    // Document::uniqueName is the supported way to ask for a free one, so the
    // caller chooses rather than the document guessing.
    const Result<MaterialId> renamed =
        features::createMaterial(document, document.uniqueName("Steel"), fullyPopulated());
    REQUIRE(renamed);
    CHECK(*renamed != first);
    CHECK(features::materialCount(document) == 2);
    // Same content, two identities, two names.
    CHECK(definitionOf(document, *renamed) == definitionOf(document, first));
    CHECK(features::findMaterial(document, *renamed)->name() !=
          features::findMaterial(document, first)->name());
}

TEST_CASE("CustomMaterial_NoEditCanStoreAPropertyThatClaimsToBeDerived", "[features][custom]") {
    // A stored property must never claim `Derived`: a value somebody measured and a
    // value BetterCAD computed have to stay distinguishable (ADR-027), and a
    // Derived value is produced on request rather than kept. The custom-material
    // path must not be a way round that.
    Document document{"Part"};
    const MaterialId id = createOrFail(document, "Fixture", fullyPopulated());
    const MaterialDefinition before = definitionOf(document, id);

    materials::MechanicalProperties faked = before.mechanical;
    faked.density = MaterialProperty<Density>::derived(Density::fromSi(2700.0));
    const Result<bool> refusedMechanical = features::setMaterialMechanical(document, id, faked);
    REQUIRE_FALSE(refusedMechanical);
    CHECK(definitionOf(document, id) == before);

    materials::ThermalProperties fakedThermal = before.thermal;
    fakedThermal.thermalConductivity =
        MaterialProperty<ThermalConductivity>::derived(167_W_per_m_K);
    const Result<bool> refusedThermal = features::setMaterialThermal(document, id, fakedThermal);
    REQUIRE_FALSE(refusedThermal);
    CHECK(definitionOf(document, id) == before);

    // Nor through creation, so a clone cannot be seeded with one either.
    MaterialDefinition seeded;
    seeded.mechanical.youngsModulus = MaterialProperty<ElasticModulus>::derived(69_GPa);
    CHECK_FALSE(features::createMaterial(document, "Faked", seeded).has_value());
    CHECK(features::materialCount(document) == 1);
}

TEST_CASE("CustomMaterial_PropertyOperationsOnAMissingMaterialFailWithNotFound",
          "[features][custom]") {
    Document document{"Part"};
    const MaterialId absent = MaterialId::fromValue(77);

    const Result<bool> mechanical =
        features::removeMaterialProperty(document, absent, MechanicalPropertyKind::Density);
    REQUIRE_FALSE(mechanical);
    CHECK(mechanical.error().code == ErrorCode::NotFound);

    const Result<bool> thermal =
        features::removeMaterialProperty(document, absent, ThermalPropertyKind::ThermalConductivity);
    REQUIRE_FALSE(thermal);
    CHECK(thermal.error().code == ErrorCode::NotFound);

    // hasMaterialProperty answers false rather than failing, because "is there a
    // density" has an answer for a material that is not there: no.
    CHECK_FALSE(features::hasMaterialProperty(document, absent, MechanicalPropertyKind::Density));
    CHECK_FALSE(
        features::hasMaterialProperty(document, absent, ThermalPropertyKind::ThermalConductivity));

    // An ID that names a NON-material object is equally not a material.
    auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
    const ObjectId notAMaterial = require(document.addObject(std::move(sketch)));
    const Result<bool> wrongKind = features::removeMaterialProperty(
        document, MaterialId::fromValue(notAMaterial.value()), MechanicalPropertyKind::Density);
    REQUIRE_FALSE(wrongKind);
    CHECK(wrongKind.error().code == ErrorCode::NotFound);
}

TEST_CASE("CustomMaterial_ALibraryImportAndTwoCustomsMaySharculeOneDesignation",
          "[features][custom]") {
    // The adversarial three-way fixture: an imported material and two custom ones
    // all designated the same. Every one must resolve by its own identity, with no
    // library-first, local-first or first-name-match preference anywhere.
    Document document{"Part"};
    MaterialDefinition shared;
    shared.designation = "Aluminium 6061-T6";

    const Result<MaterialId> imported =
        features::importLibraryMaterial(document, "FromLibrary", aluminiumEntry());
    REQUIRE(imported);
    const MaterialId customA = createOrFail(document, "CustomA", shared);
    const MaterialId customB = createOrFail(document, "CustomB", shared);

    const std::vector<MaterialId> all =
        features::findMaterialsByDesignation(document, "Aluminium 6061-T6");
    REQUIRE(all.size() == 3);
    CHECK(all == std::vector<MaterialId>{*imported, customA, customB});
    // Only the imported one has provenance; the customs have none, and that is
    // what tells them apart when their designations cannot.
    CHECK(definitionOf(document, *imported).origin.has_value());
    CHECK_FALSE(definitionOf(document, customA).origin.has_value());
    CHECK_FALSE(definitionOf(document, customB).origin.has_value());
}

// --- the library: what it has, and that it cannot be touched ----------------

TEST_CASE("CustomMaterial_LibraryEntriesCarryMetadataOnlySoAnImportIsAllUnknown",
          "[features][custom]") {
    // Recorded as a test because the rest of this file depends on it. P15-MAT-001
    // shipped the library with NO property values (ADR-028: a value needs a
    // source), so an imported material starts fully Unknown -- and Unknown, not
    // zero. A future library revision that adds values will fail here, which is
    // the right place to be reminded that provenance comes with them.
    Document document{"Part"};
    const Result<MaterialId> id =
        features::importLibraryMaterial(document, "Aluminium", aluminiumEntry());
    REQUIRE(id);

    const MaterialDefinition& stored = definitionOf(document, *id);
    CHECK(stored.designation == "Aluminium 6061-T6");
    CHECK(stored.standard == "ASTM B221");
    CHECK(stored.family == "Aluminium Alloy");
    REQUIRE(stored.origin.has_value());
    CHECK(stored.origin->library == "bettercad");
    CHECK(stored.origin->entry == "al-6061-t6");
    CHECK(stored.origin->revision == 1);

    for (const MechanicalPropertyKind kind : materials::mechanicalPropertyKinds()) {
        CHECK_FALSE(features::hasMaterialProperty(document, *id, kind));
    }
    for (const ThermalPropertyKind kind : materials::thermalPropertyKinds()) {
        CHECK_FALSE(features::hasMaterialProperty(document, *id, kind));
    }
}

TEST_CASE("CustomMaterial_EditingAnImportedMaterialLeavesTheLibraryEntryIdentical",
          "[features][custom]") {
    // The library source after every local edit, compared field by field rather
    // than by one spot check. The entries are `static constexpr` with no setters,
    // so this cannot fail without a language-level violation -- which is exactly
    // why it is cheap to assert and worth asserting: it pins the guarantee at the
    // point that relies on it.
    const materials::LibraryMaterial before = aluminiumEntry();
    const materials::MaterialLibraryKey keyBefore = before.key();

    Document document{"Part"};
    const Result<MaterialId> id =
        features::importLibraryMaterial(document, "Aluminium", aluminiumEntry());
    REQUIRE(id);

    MaterialDefinition edited = fullyPopulated();
    edited.designation = "Something Else Entirely";
    edited.standard = "";
    edited.family = "";
    edited.notes = "rewritten";
    REQUIRE(features::setMaterialDefinition(document, *id, edited));
    REQUIRE(document.rename(*id, "Renamed"));
    REQUIRE(features::removeMaterialProperty(document, *id, MechanicalPropertyKind::Density));

    const materials::LibraryMaterial after = aluminiumEntry();
    CHECK(after.designation() == before.designation());
    CHECK(after.standard() == before.standard());
    CHECK(after.family() == before.family());
    CHECK(after.notes() == before.notes());
    CHECK(after.library() == before.library());
    CHECK(after.entry() == before.entry());
    CHECK(after.revision() == before.revision());
    CHECK(after.key() == keyBefore);
    CHECK(after == before);
    // And the whole table is still the same four entries in the same order.
    CHECK(materials::builtInMaterials().size() == 4);
    CHECK(materials::builtInMaterials()[0] == before);
}

TEST_CASE("CustomMaterial_TwoImportsOfOneEntryAreTwoIndependentMaterials", "[features][custom]") {
    Document document{"Part"};
    const Result<MaterialId> first =
        features::importLibraryMaterial(document, "AluminiumA", aluminiumEntry());
    const Result<MaterialId> second =
        features::importLibraryMaterial(document, "AluminiumB", aluminiumEntry());
    REQUIRE(first);
    REQUIRE(second);
    CHECK(*first != *second);

    materials::MechanicalProperties mechanical;
    mechanical.density = MaterialProperty<Density>::known(Density::fromSi(2700.0));
    REQUIRE(features::setMaterialMechanical(document, *first, mechanical));
    // The second import is untouched: two imports are two materials, not two
    // handles on one.
    CHECK_FALSE(features::hasMaterialProperty(document, *second, MechanicalPropertyKind::Density));
    CHECK(features::hasMaterialProperty(document, *first, MechanicalPropertyKind::Density));
}

// --- cloning a document-local material --------------------------------------

TEST_CASE("CustomMaterial_CloneGetsANewIdentityAndEquivalentContent", "[features][custom]") {
    // Same content, different identity. Both halves matter: copying the identity
    // would make assignments ambiguous, and not copying the content would make the
    // clone useless.
    Document document{"Part"};
    const MaterialId source = createOrFail(document, "Source", fullyPopulated());
    const Result<MaterialId> clone = features::cloneMaterial(document, source, "Clone");
    REQUIRE(clone);

    CHECK(*clone != source);
    CHECK(definitionOf(document, *clone) == definitionOf(document, source));
    // Content-equal through the document's own comparison, and NOT identity-equal.
    const features::Material* a = features::findMaterial(document, source);
    const features::Material* b = features::findMaterial(document, *clone);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    CHECK(a->contentEquals(*b));
    CHECK(b->contentEquals(*a));
    CHECK(a->materialId() != b->materialId());
    CHECK(a->name() != b->name());
    CHECK(features::materialCount(document) == 2);
}

TEST_CASE("CustomMaterial_CloneIsDeeplyIndependentInEveryProperty", "[features][custom]") {
    // THE DEEP-COPY GATE. Every stored property is changed on the clone, then the
    // source is compared field by field -- not one spot check -- and then the same
    // in the other direction, because shared storage would leak both ways.
    Document document{"Part"};
    const MaterialId source = createOrFail(document, "Source", fullyPopulated());
    const MaterialDefinition before = definitionOf(document, source);
    const Result<MaterialId> clone = features::cloneMaterial(document, source, "Clone");
    REQUIRE(clone);

    MaterialDefinition changed = definitionOf(document, *clone);
    changed.designation = "Changed";
    changed.standard = "Changed";
    changed.family = "Changed";
    changed.notes = "Changed";
    changed.origin = materials::MaterialLibraryKey{"other", "entry", 3};
    changed.mechanical.density = MaterialProperty<Density>::known(Density::fromSi(7850.0));
    changed.mechanical.youngsModulus = MaterialProperty<ElasticModulus>::known(210_GPa);
    changed.mechanical.poissonRatio =
        MaterialProperty<PoissonRatio>::known(PoissonRatio::of(0.29));
    changed.mechanical.yieldStrength = MaterialProperty<Stress>::known(355_MPa);
    changed.mechanical.hardness =
        MaterialProperty<Hardness>::known(Hardness::of(60.0, HardnessScale::RockwellC));
    changed.thermal.thermalConductivity =
        MaterialProperty<ThermalConductivity>::known(50_W_per_m_K);
    changed.thermal.specificHeatCapacity =
        MaterialProperty<SpecificHeatCapacity>::known(460_J_per_kg_K);
    changed.thermal.thermalExpansion =
        MaterialProperty<ThermalExpansionCoefficient>::known(12.0_um_per_m_K);
    REQUIRE(features::setMaterialDefinition(document, *clone, changed));
    // And remove some, which is a different mutation from replacing.
    REQUIRE(features::removeMaterialProperty(document, *clone,
                                             MechanicalPropertyKind::UltimateTensileStrength));
    REQUIRE(features::removeMaterialProperty(document, *clone,
                                             ThermalPropertyKind::ThermalExpansion));

    // The source is bit-for-bit the definition it started with.
    CHECK(definitionOf(document, source) == before);
    CHECK(definitionOf(document, source).designation == "Fixture Aluminium");
    CHECK(definitionOf(document, source).mechanical.density.value()->in(units::kg_per_m3) == 2700.0);
    CHECK(definitionOf(document, source).mechanical.youngsModulus.value()->in(units::GPa) == 69.0);
    CHECK(definitionOf(document, source).mechanical.hardness.value()->scale() ==
          HardnessScale::Brinell);
    CHECK(definitionOf(document, source).thermal.thermalConductivity.value()->si() ==
          (167_W_per_m_K).si());
    CHECK(features::hasMaterialProperty(document, source,
                                        MechanicalPropertyKind::UltimateTensileStrength));
    CHECK(features::hasMaterialProperty(document, source, ThermalPropertyKind::ThermalExpansion));
    CHECK_FALSE(definitionOf(document, source).origin.has_value());

    // Now the other direction: editing the SOURCE must not reach the clone.
    const MaterialDefinition cloneNow = definitionOf(document, *clone);
    MaterialDefinition sourceEdit = before;
    sourceEdit.designation = "Source Moved";
    sourceEdit.mechanical.density = MaterialProperty<Density>::known(Density::fromSi(1.0));
    REQUIRE(features::setMaterialDefinition(document, source, sourceEdit));
    CHECK(definitionOf(document, *clone) == cloneNow);
}

TEST_CASE("CustomMaterial_ClonePreservesTheKnownAndUnknownPattern", "[features][custom]") {
    // Unknown must stay Unknown through a clone. Turning it into a zero or a
    // default is the failure this whole property model exists to prevent.
    Document document{"Part"};
    MaterialDefinition partial;
    partial.designation = "Partly Characterised";
    partial.mechanical.density = MaterialProperty<Density>::known(Density::fromSi(2700.0));
    partial.mechanical.youngsModulus = MaterialProperty<ElasticModulus>::known(69_GPa);
    // yield, conductivity and everything else left Unknown on purpose.
    const MaterialId source = createOrFail(document, "Partial", partial);
    const Result<MaterialId> clone = features::cloneMaterial(document, source, "PartialClone");
    REQUIRE(clone);

    for (const MechanicalPropertyKind kind : materials::mechanicalPropertyKinds()) {
        CHECK(features::hasMaterialProperty(document, *clone, kind) ==
              features::hasMaterialProperty(document, source, kind));
    }
    for (const ThermalPropertyKind kind : materials::thermalPropertyKinds()) {
        CHECK(features::hasMaterialProperty(document, *clone, kind) ==
              features::hasMaterialProperty(document, source, kind));
    }
    // Specifically: the unknowns are unknown, not zero.
    const MaterialDefinition& cloned = definitionOf(document, *clone);
    CHECK(cloned.mechanical.yieldStrength.isUnknown());
    CHECK_FALSE(cloned.mechanical.yieldStrength.value().has_value());
    CHECK(cloned.thermal.thermalConductivity.isUnknown());
    CHECK_FALSE(cloned.thermal.thermalConductivity.value().has_value());
}

TEST_CASE("CustomMaterial_CloneKeepsHardnessScaleAndReferenceTemperature", "[features][custom]") {
    // Two pieces of property metadata that a shallow or field-by-field copy could
    // drop: the hardness SCALE (60 HRC is not 60 HB) and the temperature a value
    // was specified at.
    Document document{"Part"};
    MaterialDefinition definition;
    definition.designation = "Tool Steel";
    definition.mechanical.hardness =
        MaterialProperty<Hardness>::known(Hardness::of(60.0, HardnessScale::RockwellC));
    definition.mechanical.density =
        MaterialProperty<Density>::known(Density::fromSi(7850.0), 293.15_K);
    definition.thermal.thermalConductivity =
        MaterialProperty<ThermalConductivity>::known(24_W_per_m_K, 373.15_K);
    const MaterialId source = createOrFail(document, "ToolSteel", definition);
    const Result<MaterialId> clone = features::cloneMaterial(document, source, "ToolSteelClone");
    REQUIRE(clone);

    const MaterialDefinition& cloned = definitionOf(document, *clone);
    REQUIRE(cloned.mechanical.hardness.value().has_value());
    CHECK(cloned.mechanical.hardness.value()->value() == 60.0);
    CHECK(cloned.mechanical.hardness.value()->scale() == HardnessScale::RockwellC);
    REQUIRE(cloned.mechanical.density.referenceTemperature().has_value());
    CHECK(cloned.mechanical.density.referenceTemperature()->in(units::K) == 293.15);
    REQUIRE(cloned.thermal.thermalConductivity.referenceTemperature().has_value());
    CHECK(cloned.thermal.thermalConductivity.referenceTemperature()->in(units::K) == 373.15);

    // And editing an UNRELATED property does not disturb either of them, because
    // a whole-struct edit carries them through.
    materials::MechanicalProperties mechanical = cloned.mechanical;
    mechanical.yieldStrength = MaterialProperty<Stress>::known(1000_MPa);
    REQUIRE(features::setMaterialMechanical(document, *clone, mechanical));
    CHECK(definitionOf(document, *clone).mechanical.hardness.value()->scale() ==
          HardnessScale::RockwellC);
    CHECK(definitionOf(document, *clone).mechanical.density.referenceTemperature()->in(units::K) ==
          293.15);
}

TEST_CASE("CustomMaterial_CloneCarriesTheOriginKeyBecauseTheValuesStillCameFromThere",
          "[features][custom]") {
    // A clone of an imported material keeps `origin`, because origin records where
    // the VALUES came from and cloning does not change them. It is provenance, not
    // a live reference: no lookup happens through it (ADR-025).
    Document document{"Part"};
    const Result<MaterialId> imported =
        features::importLibraryMaterial(document, "Aluminium", aluminiumEntry());
    REQUIRE(imported);
    const Result<MaterialId> clone = features::cloneMaterial(document, *imported, "AluminiumCopy");
    REQUIRE(clone);

    REQUIRE(definitionOf(document, *clone).origin.has_value());
    CHECK(*definitionOf(document, *clone).origin == *definitionOf(document, *imported).origin);
    CHECK(definitionOf(document, *clone).origin->entry == "al-6061-t6");
    // A clone of a from-scratch material has no origin to carry, and none is
    // invented for it.
    const MaterialId scratch = createOrFail(document, "Scratch", fullyPopulated());
    const Result<MaterialId> scratchClone = features::cloneMaterial(document, scratch, "ScratchCopy");
    REQUIRE(scratchClone);
    CHECK_FALSE(definitionOf(document, *scratchClone).origin.has_value());
}

TEST_CASE("CustomMaterial_CloneAcrossDocumentsIsIndependentOfTheSourceDocument",
          "[features][custom]") {
    // Cross-document cloning IS supported, and the destination holds no reference
    // to the source afterwards -- which the last section of this test proves the
    // hard way, by destroying the source document and then using the clone.
    Document destination{"Destination"};
    MaterialId cloned{};
    {
        Document source{"Source"};
        const MaterialId original = createOrFail(source, "Source", fullyPopulated());
        const Result<MaterialId> result =
            features::cloneMaterial(destination, source, original, "Imported");
        REQUIRE(result);
        cloned = *result;

        // Editing the destination leaves the source alone.
        MaterialDefinition edit = definitionOf(destination, cloned);
        edit.designation = "Changed In Destination";
        edit.mechanical.density = MaterialProperty<Density>::known(Density::fromSi(7850.0));
        REQUIRE(features::setMaterialDefinition(destination, cloned, edit));
        CHECK(definitionOf(source, original).designation == "Fixture Aluminium");
        CHECK(definitionOf(source, original).mechanical.density.value()->in(units::kg_per_m3) ==
              2700.0);

        // And editing the source leaves the destination alone.
        MaterialDefinition sourceEdit = definitionOf(source, original);
        sourceEdit.designation = "Changed In Source";
        REQUIRE(features::setMaterialDefinition(source, original, sourceEdit));
        CHECK(definitionOf(destination, cloned).designation == "Changed In Destination");
    }
    // The source document is gone. The clone is still whole, because nothing in it
    // ever pointed there.
    const MaterialDefinition& survivor = definitionOf(destination, cloned);
    CHECK(survivor.designation == "Changed In Destination");
    CHECK(survivor.mechanical.density.value()->in(units::kg_per_m3) == 7850.0);
    CHECK(survivor.mechanical.hardness.value()->scale() == HardnessScale::Brinell);
    CHECK(survivor.thermal.thermalConductivity.value()->si() == (167_W_per_m_K).si());
}

TEST_CASE("CustomMaterial_CloneRefusesAMissingSourceAndAnUnavailableName", "[features][custom]") {
    Document document{"Part"};
    const MaterialId source = createOrFail(document, "Source", fullyPopulated());

    const Result<MaterialId> missing =
        features::cloneMaterial(document, MaterialId::fromValue(999), "Clone");
    REQUIRE_FALSE(missing);
    CHECK(missing.error().code == ErrorCode::NotFound);
    CHECK_THAT(missing.error().message, ContainsSubstring("to clone"));

    const Result<MaterialId> taken = features::cloneMaterial(document, source, "Source");
    REQUIRE_FALSE(taken);
    // A rejected clone consumed no ID and left the document with one material.
    CHECK(features::materialCount(document) == 1);
    const Result<MaterialId> after = features::cloneMaterial(document, source, "Clone");
    REQUIRE(after);
    CHECK(after->value() == source.value() + 1);
}

// --- property removal -------------------------------------------------------

TEST_CASE("CustomMaterial_RemovingAPropertyMakesItUnknownAndNeverZero", "[features][custom]") {
    // THE UNKNOWN-IS-NOT-ZERO GATE, over the five kinds the brief names plus the
    // rest of the enumeration.
    Document document{"Part"};
    const MaterialId id = createOrFail(document, "Fixture", fullyPopulated());

    REQUIRE(features::removeMaterialProperty(document, id, MechanicalPropertyKind::Density));
    const MaterialDefinition& afterDensity = definitionOf(document, id);
    CHECK(afterDensity.mechanical.density.isUnknown());
    CHECK_FALSE(afterDensity.mechanical.density.value().has_value());
    CHECK_FALSE(materials::hasDensity(afterDensity.mechanical));

    REQUIRE(features::removeMaterialProperty(document, id, MechanicalPropertyKind::YoungsModulus));
    CHECK(definitionOf(document, id).mechanical.youngsModulus.isUnknown());
    REQUIRE(features::removeMaterialProperty(document, id, MechanicalPropertyKind::YieldStrength));
    CHECK(definitionOf(document, id).mechanical.yieldStrength.isUnknown());
    REQUIRE(
        features::removeMaterialProperty(document, id, ThermalPropertyKind::ThermalConductivity));
    CHECK(definitionOf(document, id).thermal.thermalConductivity.isUnknown());
    REQUIRE(
        features::removeMaterialProperty(document, id, ThermalPropertyKind::SpecificHeatCapacity));
    CHECK(definitionOf(document, id).thermal.specificHeatCapacity.isUnknown());

    // And the consumer's answer is a diagnostic, not a zero.
    const Result<Density> density = features::requireDensity(document, id);
    REQUIRE_FALSE(density);
    CHECK_THAT(density.error().message, ContainsSubstring("no density"));
}

TEST_CASE("CustomMaterial_RemovingAPropertyIsNotSettingItToZero", "[features][custom]") {
    // The hard distinction: remove leaves Unknown and succeeds; zero is a claim
    // about the material and is refused.
    Document document{"Part"};
    const MaterialId id = createOrFail(document, "Fixture", fullyPopulated());

    materials::MechanicalProperties zeroDensity = definitionOf(document, id).mechanical;
    zeroDensity.density = MaterialProperty<Density>::known(Density::fromSi(0.0));
    const Result<bool> refused = features::setMaterialMechanical(document, id, zeroDensity);
    REQUIRE_FALSE(refused);
    // Refused, and the old value is still there -- not zeroed on the way out.
    CHECK(definitionOf(document, id).mechanical.density.value()->in(units::kg_per_m3) == 2700.0);

    materials::MechanicalProperties zeroModulus = definitionOf(document, id).mechanical;
    zeroModulus.youngsModulus = MaterialProperty<ElasticModulus>::known(0_GPa);
    CHECK_FALSE(features::setMaterialMechanical(document, id, zeroModulus));
    CHECK(definitionOf(document, id).mechanical.youngsModulus.value()->in(units::GPa) == 69.0);

    // Whereas removal succeeds and leaves Unknown.
    REQUIRE(features::removeMaterialProperty(document, id, MechanicalPropertyKind::Density));
    CHECK(definitionOf(document, id).mechanical.density.isUnknown());
}

TEST_CASE("CustomMaterial_RemovingADerivedModulusIsRefusedWithAUsefulDiagnostic",
          "[features][custom]") {
    // G and K are derived and never stored (ADR-027), so there is no slot to
    // remove. Refusing beats silently doing nothing, and the diagnostic says what
    // to remove instead.
    Document document{"Part"};
    const MaterialId id = createOrFail(document, "Fixture", fullyPopulated());

    for (const MechanicalPropertyKind kind :
         {MechanicalPropertyKind::ShearModulus, MechanicalPropertyKind::BulkModulus}) {
        const Result<bool> refused = features::removeMaterialProperty(document, id, kind);
        REQUIRE_FALSE(refused);
        CHECK(refused.error().code == ErrorCode::InvalidArgument);
        CHECK_THAT(refused.error().message, ContainsSubstring("derived"));
        CHECK_THAT(refused.error().message, ContainsSubstring("nothing stored to remove"));
    }
    // Nothing changed: E and nu are still there, so G is still reportable.
    CHECK(features::hasMaterialProperty(document, id, MechanicalPropertyKind::ShearModulus));
    CHECK(materials::derivedShearModulus(definitionOf(document, id).mechanical).isDerived());

    // And the documented way to stop BetterCAD reporting G does work.
    REQUIRE(features::removeMaterialProperty(document, id, MechanicalPropertyKind::PoissonRatio));
    CHECK_FALSE(features::hasMaterialProperty(document, id, MechanicalPropertyKind::ShearModulus));
    CHECK(materials::derivedShearModulus(definitionOf(document, id).mechanical).isUnknown());
}

TEST_CASE("CustomMaterial_RemovingOnePropertyLeavesEveryOtherPropertyAlone", "[features][custom]") {
    // NO ACCIDENTAL PARTIAL OVERRIDE, in the form that applies to a snapshot
    // model: an edit to one property touches exactly that property, and the others
    // neither move nor become Unknown nor fall back to anything.
    Document document{"Part"};
    const MaterialId id = createOrFail(document, "Fixture", fullyPopulated());
    const MaterialDefinition before = definitionOf(document, id);

    REQUIRE(features::removeMaterialProperty(document, id, MechanicalPropertyKind::YieldStrength));
    const MaterialDefinition after = definitionOf(document, id);

    CHECK(after.mechanical.yieldStrength.isUnknown());
    // Everything else, field by field.
    CHECK(after.designation == before.designation);
    CHECK(after.standard == before.standard);
    CHECK(after.family == before.family);
    CHECK(after.notes == before.notes);
    CHECK(after.origin == before.origin);
    CHECK(after.mechanical.density == before.mechanical.density);
    CHECK(after.mechanical.youngsModulus == before.mechanical.youngsModulus);
    CHECK(after.mechanical.poissonRatio == before.mechanical.poissonRatio);
    CHECK(after.mechanical.ultimateTensileStrength == before.mechanical.ultimateTensileStrength);
    CHECK(after.mechanical.elongation == before.mechanical.elongation);
    CHECK(after.mechanical.hardness == before.mechanical.hardness);
    // The whole thermal half is untouched by a mechanical removal.
    CHECK(after.thermal == before.thermal);
}

TEST_CASE("CustomMaterial_AThermalRemovalLeavesTheMechanicalHalfAlone", "[features][custom]") {
    Document document{"Part"};
    const MaterialId id = createOrFail(document, "Fixture", fullyPopulated());
    const MaterialDefinition before = definitionOf(document, id);
    REQUIRE(features::removeMaterialProperty(document, id, ThermalPropertyKind::ThermalExpansion));
    const MaterialDefinition after = definitionOf(document, id);
    CHECK(after.thermal.thermalExpansion.isUnknown());
    CHECK(after.mechanical == before.mechanical);
    // Density in particular, which thermal consumers read but which lives in the
    // mechanical half and must not be reachable from a thermal edit.
    CHECK(after.mechanical.density == before.mechanical.density);
}

TEST_CASE("CustomMaterial_RemovingAPropertyThatIsAlreadyUnknownReportsNoChange",
          "[features][custom]") {
    Document document{"Part"};
    MaterialDefinition definition;
    definition.designation = "Sparse";
    const MaterialId id = createOrFail(document, "Sparse", definition);
    const std::uint64_t revision = document.revision();

    const Result<bool> removed =
        features::removeMaterialProperty(document, id, MechanicalPropertyKind::Density);
    REQUIRE(removed);
    CHECK_FALSE(*removed);
    // No effective change, so no revision bump -- the same contract every other
    // setter here has.
    CHECK(document.revision() == revision);
}

// --- atomicity of an invalid edit -------------------------------------------

TEST_CASE("CustomMaterial_AnInvalidEditChangesNothingAtAll", "[features][custom]") {
    // Five invalid values, each attempted against a fully populated material, each
    // required to leave the WHOLE definition as it was -- not just the field it
    // targeted. A validator that wrote first and checked afterwards would fail
    // here.
    Document document{"Part"};
    const MaterialId id = createOrFail(document, "Fixture", fullyPopulated());
    const MaterialDefinition before = definitionOf(document, id);
    const std::uint64_t revision = document.revision();

    const auto expectRefused = [&](materials::MechanicalProperties properties, const char* what) {
        CAPTURE(what);
        const Result<bool> result = features::setMaterialMechanical(document, id, properties);
        CHECK_FALSE(result.has_value());
        CHECK(definitionOf(document, id) == before);
        CHECK(document.revision() == revision);
    };

    materials::MechanicalProperties negativeModulus = before.mechanical;
    negativeModulus.youngsModulus = MaterialProperty<ElasticModulus>::known(-1_GPa);
    expectRefused(negativeModulus, "E = -1 GPa");

    materials::MechanicalProperties zeroDensity = before.mechanical;
    zeroDensity.density = MaterialProperty<Density>::known(Density::fromSi(0.0));
    expectRefused(zeroDensity, "density = 0");

    materials::MechanicalProperties negativeDensity = before.mechanical;
    negativeDensity.density = MaterialProperty<Density>::known(Density::fromSi(-1.0));
    expectRefused(negativeDensity, "density = -1");

    materials::MechanicalProperties zeroYield = before.mechanical;
    zeroYield.yieldStrength = MaterialProperty<Stress>::known(0_MPa);
    expectRefused(zeroYield, "yield = 0");

    // A Poisson ratio of exactly 0.5 is incompressible and the bulk modulus
    // diverges there, so the range is exclusive. PoissonRatio::of itself carries NO
    // range check -- deliberately, because that is physical validity rather than
    // dimensional validity and P15-ARCH-001 keeps the two apart -- so the value
    // reaches the setter and is refused there.
    materials::MechanicalProperties incompressible = before.mechanical;
    incompressible.poissonRatio = MaterialProperty<PoissonRatio>::known(PoissonRatio::of(0.5));
    expectRefused(incompressible, "nu = 0.5");

    materials::MechanicalProperties auxeticTooFar = before.mechanical;
    auxeticTooFar.poissonRatio = MaterialProperty<PoissonRatio>::known(PoissonRatio::of(-1.5));
    expectRefused(auxeticTooFar, "nu = -1.5");

    materials::ThermalProperties zeroConductivity = before.thermal;
    zeroConductivity.thermalConductivity = MaterialProperty<ThermalConductivity>::known(0_W_per_m_K);
    const Result<bool> refusedThermal =
        features::setMaterialThermal(document, id, zeroConductivity);
    CHECK_FALSE(refusedThermal.has_value());
    CHECK(definitionOf(document, id) == before);

    materials::ThermalProperties negativeHeat = before.thermal;
    negativeHeat.specificHeatCapacity =
        MaterialProperty<SpecificHeatCapacity>::known(SpecificHeatCapacity::fromSi(-1.0));
    CHECK_FALSE(features::setMaterialThermal(document, id, negativeHeat).has_value());
    CHECK(definitionOf(document, id) == before);

    // After nine refusals the material is still exactly what it was.
    CHECK(definitionOf(document, id) == before);
    CHECK(document.revision() == revision);
}

TEST_CASE("CustomMaterial_AnInvalidEditIsRefusedAcrossAWholeStructWithSeveralGoodFields",
          "[features][custom]") {
    // The multi-property case. These setters replace a whole struct, so they are
    // transactional BY CONSTRUCTION: a struct with one bad field is rejected
    // entirely and the good fields in it are not applied. That is worth pinning,
    // because "two of the three took effect" is exactly the partial mutation the
    // gate forbids.
    Document document{"Part"};
    const MaterialId id = createOrFail(document, "Fixture", fullyPopulated());
    const MaterialDefinition before = definitionOf(document, id);

    materials::MechanicalProperties mixed = before.mechanical;
    mixed.density = MaterialProperty<Density>::known(Density::fromSi(2800.0));   // valid
    mixed.youngsModulus = MaterialProperty<ElasticModulus>::known(-5_GPa);       // invalid
    mixed.yieldStrength = MaterialProperty<Stress>::known(300_MPa);              // valid
    CHECK_FALSE(features::setMaterialMechanical(document, id, mixed).has_value());

    // NONE of the three landed.
    CHECK(definitionOf(document, id).mechanical.density.value()->in(units::kg_per_m3) == 2700.0);
    CHECK(definitionOf(document, id).mechanical.youngsModulus.value()->in(units::GPa) == 69.0);
    CHECK(definitionOf(document, id).mechanical.yieldStrength.value()->in(units::MPa) == 276.0);
    CHECK(definitionOf(document, id) == before);
}

TEST_CASE("CustomMaterial_IndependentEditsGiveTheSameResultInEitherOrder", "[features][custom]") {
    // Guards against a hidden dependency between unrelated properties: setting E
    // then density must land the same material as density then E.
    const auto build = [](bool modulusFirst) {
        Document document{"Part"};
        const Result<MaterialId> id = features::createMaterial(document, "M", fullyPopulated());
        REQUIRE(id);
        materials::MechanicalProperties properties = definitionOf(document, *id).mechanical;
        if (modulusFirst) {
            properties.youngsModulus = MaterialProperty<ElasticModulus>::known(71_GPa);
            REQUIRE(features::setMaterialMechanical(document, *id, properties));
            properties = definitionOf(document, *id).mechanical;
            properties.density = MaterialProperty<Density>::known(Density::fromSi(2680.0));
            REQUIRE(features::setMaterialMechanical(document, *id, properties));
        } else {
            properties.density = MaterialProperty<Density>::known(Density::fromSi(2680.0));
            REQUIRE(features::setMaterialMechanical(document, *id, properties));
            properties = definitionOf(document, *id).mechanical;
            properties.youngsModulus = MaterialProperty<ElasticModulus>::known(71_GPa);
            REQUIRE(features::setMaterialMechanical(document, *id, properties));
        }
        return definitionOf(document, *id);
    };
    CHECK(build(true) == build(false));

    // The same for a removal and an edit of different properties.
    const auto buildRemoval = [](bool removeFirst) {
        Document document{"Part"};
        const Result<MaterialId> id = features::createMaterial(document, "M", fullyPopulated());
        REQUIRE(id);
        const auto editConductivity = [&] {
            materials::ThermalProperties thermal = definitionOf(document, *id).thermal;
            thermal.thermalConductivity = MaterialProperty<ThermalConductivity>::known(150_W_per_m_K);
            REQUIRE(features::setMaterialThermal(document, *id, thermal));
        };
        const auto removeYield = [&] {
            REQUIRE(features::removeMaterialProperty(document, *id,
                                                     MechanicalPropertyKind::YieldStrength));
        };
        if (removeFirst) {
            removeYield();
            editConductivity();
        } else {
            editConductivity();
            removeYield();
        }
        return definitionOf(document, *id);
    };
    CHECK(buildRemoval(true) == buildRemoval(false));
}

// --- integration with the milestones above ----------------------------------

TEST_CASE("CustomMaterial_AnAssignmentSurvivesEveryEditOfTheMaterialItNames",
          "[features][custom]") {
    // An edit is not a new material, so an assignment must not need redoing. If a
    // setter minted a new ID the assignment would go unresolved here.
    Document document{"Part"};
    const MaterialId id = createOrFail(document, "MyAluminium", fullyPopulated());
    REQUIRE(features::assignMaterial(document, id));
    REQUIRE(features::materialAssignment(document).state ==
            features::MaterialAssignmentState::Resolved);

    REQUIRE(document.rename(id, "Renamed"));
    MaterialDefinition edited = definitionOf(document, id);
    edited.designation = "Different Label";
    REQUIRE(features::setMaterialDefinition(document, id, edited));
    materials::MechanicalProperties mechanical = definitionOf(document, id).mechanical;
    mechanical.youngsModulus = MaterialProperty<ElasticModulus>::known(70_GPa);
    REQUIRE(features::setMaterialMechanical(document, id, mechanical));
    REQUIRE(features::removeMaterialProperty(document, id, MechanicalPropertyKind::Hardness));

    const features::MaterialAssignment assignment = features::materialAssignment(document);
    CHECK(assignment.state == features::MaterialAssignmentState::Resolved);
    CHECK(assignment.material == id);
    // And the consumer sees the CURRENT values, not the ones at assignment time.
    const features::Material* effective = features::effectiveMaterial(document);
    REQUIRE(effective != nullptr);
    CHECK(effective->materialId() == id);
    CHECK(effective->definition().mechanical.youngsModulus.value()->in(units::GPa) == 70.0);
    CHECK(effective->definition().mechanical.hardness.isUnknown());
}

TEST_CASE("CustomMaterial_MassFollowsAnEditedCustomDensityWithNoReassignment",
          "[features][custom]") {
    // P15-MASS integration: m = rho V, recomputed from whatever the material says
    // now. Nothing is cached, so no assignment changes and no stale mass.
    BoxPart part;
    const MaterialId id = createOrFail(part.document, "Custom", fullyPopulated());
    REQUIRE(features::assignMaterial(part.document, id));

    // 2700 kg/m^3 x 30000 mm^3 = 0.081 kg
    const Result<features::PartMassProperties> first =
        features::partMassProperties(part.document, part.regenerator, part.feature);
    REQUIRE(first);
    CHECK_THAT(first->mass.in(units::kg), WithinRel(0.081, 1e-12));
    CHECK_THAT(first->volume.in(units::mm3), WithinRel(30000.0, 1e-12));
    CHECK(first->material == id);

    materials::MechanicalProperties mechanical = definitionOf(part.document, id).mechanical;
    mechanical.density = MaterialProperty<Density>::known(Density::fromSi(7850.0));
    REQUIRE(features::setMaterialMechanical(part.document, id, mechanical));

    // 7850 x 3e-5 = 0.2355 kg, same volume, same material identity.
    const Result<features::PartMassProperties> second =
        features::partMassProperties(part.document, part.regenerator, part.feature);
    REQUIRE(second);
    CHECK_THAT(second->mass.in(units::kg), WithinRel(0.2355, 1e-12));
    CHECK_THAT(second->volume.in(units::mm3), WithinRel(first->volume.in(units::mm3), 1e-12));
    CHECK(second->material == id);
    CHECK(features::materialAssignment(part.document).material == id);
    // The inertia followed too, in the same ratio as the mass.
    CHECK_THAT(second->aboutCentreOfMass.xx.in(units::kg_mm2),
               WithinRel(first->aboutCentreOfMass.xx.in(units::kg_mm2) * 7850.0 / 2700.0, 1e-12));

    // And REMOVING the density turns the mass into a diagnostic, not a zero.
    REQUIRE(features::removeMaterialProperty(part.document, id, MechanicalPropertyKind::Density));
    const Result<features::PartMassProperties> third =
        features::partMassProperties(part.document, part.regenerator, part.feature);
    REQUIRE_FALSE(third);
    CHECK_THAT(third.error().message, ContainsSubstring("no density"));
}

TEST_CASE("CustomMaterial_AssignmentToAnIncompleteMaterialSucceedsButMassFails",
          "[features][custom]") {
    // Assignment and property completeness are separate concepts. Assigning a
    // material nobody has characterised is a legitimate intermediate state; asking
    // it for a mass is what fails, and it fails specifically.
    BoxPart part;
    MaterialDefinition bare;
    bare.designation = "Prototype Foam";
    const MaterialId id = createOrFail(part.document, "PrototypeFoam", bare);

    REQUIRE(features::assignMaterial(part.document, id));
    CHECK(features::materialAssignment(part.document).state ==
          features::MaterialAssignmentState::Resolved);

    const Result<features::PartMassProperties> mass =
        features::partMassProperties(part.document, part.regenerator, part.feature);
    REQUIRE_FALSE(mass);
    CHECK_THAT(mass.error().message, ContainsSubstring("no density"));
    CHECK_THAT(mass.error().message, ContainsSubstring("PrototypeFoam"));

    // The elastic constants a solver would want fail separately and name the
    // inputs, not a derived constant.
    const Result<materials::LinearElasticConstants> elastic =
        features::requireLinearElasticConstants(part.document, id);
    REQUIRE_FALSE(elastic);
    CHECK_THAT(elastic.error().message, ContainsSubstring("modulus"));
}

TEST_CASE("CustomMaterial_DerivedModuliFollowAnEditedElasticConstant", "[features][custom]") {
    // P15-MECH integration: G and K are derived on request, so editing E or nu
    // moves them and there is no stored copy to go stale. A SUPPLIED G cannot
    // exist -- MechanicalProperties has no slot for one (ADR-027) -- so "a supplied
    // G silently overwritten by an E edit" is unrepresentable rather than merely
    // untested.
    Document document{"Part"};
    const MaterialId id = createOrFail(document, "Fixture", fullyPopulated());

    const auto shear = [&] {
        const MaterialProperty<ElasticModulus> g =
            materials::derivedShearModulus(definitionOf(document, id).mechanical);
        REQUIRE(g.isDerived());
        return g.value()->in(units::GPa);
    };
    // G = E / (2(1 + nu)) = 69 / (2 x 1.33)
    CHECK_THAT(shear(), WithinRel(69.0 / (2.0 * 1.33), 1e-12));

    materials::MechanicalProperties mechanical = definitionOf(document, id).mechanical;
    mechanical.youngsModulus = MaterialProperty<ElasticModulus>::known(210_GPa);
    REQUIRE(features::setMaterialMechanical(document, id, mechanical));
    CHECK_THAT(shear(), WithinRel(210.0 / (2.0 * 1.33), 1e-12));

    mechanical = definitionOf(document, id).mechanical;
    mechanical.poissonRatio = MaterialProperty<PoissonRatio>::known(PoissonRatio::of(0.3));
    REQUIRE(features::setMaterialMechanical(document, id, mechanical));
    CHECK_THAT(shear(), WithinRel(210.0 / 2.6, 1e-12));

    // The full four-constant set a solver consumes agrees.
    const Result<materials::LinearElasticConstants> constants =
        features::requireLinearElasticConstants(document, id);
    REQUIRE(constants);
    CHECK_THAT(constants->shearModulus.in(units::GPa), WithinRel(210.0 / 2.6, 1e-12));
}

TEST_CASE("CustomMaterial_ThermalConsumersFollowAnEditedThermalProperty", "[features][custom]") {
    // P15-THERM integration, including the transient set that spans both halves of
    // the definition: density from mechanical, cp and k from thermal.
    Document document{"Part"};
    const MaterialId id = createOrFail(document, "Fixture", fullyPopulated());

    const Result<ThermalConductivity> first = features::requireThermalConductivity(document, id);
    REQUIRE(first);
    CHECK_THAT(first->si(), WithinRel((167_W_per_m_K).si(), 1e-12));

    materials::ThermalProperties thermal = definitionOf(document, id).thermal;
    thermal.thermalConductivity = MaterialProperty<ThermalConductivity>::known(150_W_per_m_K);
    REQUIRE(features::setMaterialThermal(document, id, thermal));
    const Result<ThermalConductivity> second = features::requireThermalConductivity(document, id);
    REQUIRE(second);
    CHECK_THAT(second->si(), WithinRel((150_W_per_m_K).si(), 1e-12));

    const Result<features::TransientConductionProperties> transient =
        features::requireTransientConductionProperties(document, id);
    REQUIRE(transient);
    CHECK_THAT(transient->density.in(units::kg_per_m3), WithinRel(2700.0, 1e-12));
    CHECK_THAT(transient->thermalConductivity.si(), WithinRel((150_W_per_m_K).si(), 1e-12));

    // Removing the density breaks the transient set but not the steady one, and
    // the diagnostic names the missing input.
    REQUIRE(features::removeMaterialProperty(document, id, MechanicalPropertyKind::Density));
    CHECK(features::requireThermalConductivity(document, id).has_value());
    const Result<features::TransientConductionProperties> broken =
        features::requireTransientConductionProperties(document, id);
    REQUIRE_FALSE(broken);
    CHECK_THAT(broken.error().message, ContainsSubstring("density"));
}

// --- identity, deletion and enumeration -------------------------------------

TEST_CASE("CustomMaterial_ADeletedMaterialsIdIsNeverGivenToAnIdenticalReplacement",
          "[features][custom]") {
    // The no-rebinding rule from P15-ASSIGN-001, restated for custom materials: a
    // replacement with identical name, metadata and properties is a DIFFERENT
    // material, and a reference to the deleted one stays unresolved.
    Document document{"Part"};
    const MaterialId a = createOrFail(document, "Steel", fullyPopulated());
    REQUIRE(features::assignMaterial(document, a));
    REQUIRE(features::removeMaterial(document, a));

    const MaterialId b = createOrFail(document, "Steel", fullyPopulated());
    CHECK(b != a);
    CHECK(b.value() > a.value());
    CHECK(definitionOf(document, b) == fullyPopulated());

    // The assignment still names A and is UNRESOLVED -- it did not silently adopt
    // the identical B.
    const features::MaterialAssignment assignment = features::materialAssignment(document);
    CHECK(assignment.state == features::MaterialAssignmentState::Unresolved);
    CHECK(assignment.material == a);
    CHECK(assignment.material != b);
    CHECK(features::findMaterial(document, a) == nullptr);
}

TEST_CASE("CustomMaterial_ACloneCannotBeInsertedOverAnExistingIdentity", "[features][custom]") {
    // The C++ copy trap, closed from both directions. A Material carries its ID on
    // DocumentObject, so the document refuses to ADD an object that already has one
    // and refuses to INSERT one whose ID is in use -- with the existing material
    // untouched either way.
    Document document{"Part"};
    const MaterialId source = createOrFail(document, "Source", fullyPopulated());
    const features::Material* original = features::findMaterial(document, source);
    REQUIRE(original != nullptr);

    // DocumentObject::clone() is the undo/redo copy and keeps the identity, which
    // is why it is not the way to make a new material.
    std::unique_ptr<DocumentObject> copy = original->clone();
    REQUIRE(copy != nullptr);
    CHECK(copy->id() == ObjectId::fromValue(source.value()));

    const Result<ObjectId> added = document.addObject(std::move(copy));
    REQUIRE_FALSE(added);
    CHECK_THAT(added.error().message, ContainsSubstring("already has"));
    CHECK(features::materialCount(document) == 1);

    std::unique_ptr<DocumentObject> second = features::findMaterial(document, source)->clone();
    const Result<void> inserted = document.insertObject(std::move(second));
    REQUIRE_FALSE(inserted);
    CHECK(inserted.error().code == ErrorCode::AlreadyExists);
    CHECK_THAT(inserted.error().message, ContainsSubstring("already in use"));

    // Nothing merged, nothing overwrote: one material, its values intact.
    CHECK(features::materialCount(document) == 1);
    CHECK(definitionOf(document, source) == fullyPopulated());
    // And cloneMaterial, the supported route, works and allocates a fresh ID.
    const Result<MaterialId> proper = features::cloneMaterial(document, source, "Proper");
    REQUIRE(proper);
    CHECK(*proper != source);
    CHECK(features::materialCount(document) == 2);
}

TEST_CASE("CustomMaterial_EnumerationIsDeterministicAndIndependentOfCreationOrder",
          "[features][custom]") {
    // Ascending ID from the document's ordered map, so the order is the same in
    // every build and on every run and is not an unordered container's traversal.
    Document document{"Part"};
    const MaterialId first = createOrFail(document, "Zeta", fullyPopulated());
    const MaterialId second = createOrFail(document, "Alpha", fullyPopulated());
    const Result<MaterialId> third = features::cloneMaterial(document, first, "Mu");
    REQUIRE(third);

    const std::vector<MaterialId> expected{first, second, *third};
    for (int pass = 0; pass < 8; ++pass) {
        CHECK(features::materialIds(document) == expected);
    }
    // Ascending, and not alphabetical by name.
    CHECK(first.value() < second.value());
    CHECK(second.value() < third->value());
}

TEST_CASE("CustomMaterial_CloningIsDeterministicAcrossEquivalentDocuments", "[features][custom]") {
    // Two fresh documents built the same way produce the same content, the same
    // known/unknown pattern and the same IDs -- the last because the allocator
    // counts from the same place, not because IDs are global.
    const auto build = [] {
        Document document{"Part"};
        const Result<MaterialId> source = features::createMaterial(document, "S", fullyPopulated());
        REQUIRE(source);
        const Result<MaterialId> clone = features::cloneMaterial(document, *source, "C");
        REQUIRE(clone);
        return std::pair{*clone, definitionOf(document, *clone)};
    };
    const auto [idA, definitionA] = build();
    const auto [idB, definitionB] = build();
    CHECK(idA == idB);
    CHECK(definitionA == definitionB);
}
