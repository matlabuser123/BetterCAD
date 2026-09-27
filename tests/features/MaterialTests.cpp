#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/materials/MaterialLibrary.hpp>
#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/core/materials/ThermalProperties.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <limits>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinRel;
using features::Material;
using features::MaterialDefinition;
using materials::LibraryMaterial;
using materials::MaterialLibraryKey;

namespace {

MaterialDefinition steelDefinition() {
    MaterialDefinition definition;
    definition.designation = "Steel";
    definition.standard = "EN 10025-2";
    definition.family = "Carbon Steel";
    definition.notes = "As delivered.";
    return definition;
}

MaterialId createOrFail(Document& document, std::string name,
                        const MaterialDefinition& definition = {}) {
    const Result<MaterialId> id = features::createMaterial(document, std::move(name), definition);
    REQUIRE(id);
    return *id;
}

} // namespace

// --- identity ---------------------------------------------------------------

TEST_CASE("Material_IdentityIsTheIdAndTwoMaterialsNeverShareOne") {
    Document document{"Part"};
    const MaterialId a = createOrFail(document, "Steel");
    const MaterialId b = createOrFail(document, "Steel2");
    REQUIRE(a != b);
    REQUIRE(a.isValid());
    REQUIRE(b.isValid());
}

TEST_CASE("Material_IdWidensToObjectIdBecauseAMaterialIsADocumentObject") {
    Document document{"Part"};
    const MaterialId id = createOrFail(document, "Steel");
    // ADR-025: a material IS a document object, so this widening is the
    // intended relationship and not an accident.
    const ObjectId object = id;
    REQUIRE(object.value() == id.value());
    REQUIRE(document.contains(object));
    REQUIRE(document.nameOf(object) == "Steel");
}

TEST_CASE("Material_RenamingDoesNotChangeItsId") {
    Document document{"Part"};
    const MaterialId id = createOrFail(document, "Steel", steelDefinition());

    const Result<bool> renamed = document.rename(id, "StructuralSteel");
    REQUIRE(renamed);
    REQUIRE(*renamed);

    REQUIRE(features::findMaterial(document, id) != nullptr);
    REQUIRE(features::findMaterial(document, id)->materialId() == id);
    REQUIRE(features::findMaterial(document, id)->name() == "StructuralSteel");
    // And the definition is untouched by a rename.
    REQUIRE(features::findMaterial(document, id)->definition() == steelDefinition());
}

TEST_CASE("Material_EditingAnyMetadataFieldDoesNotChangeItsId") {
    Document document{"Part"};
    const MaterialId id = createOrFail(document, "Steel", steelDefinition());

    // Each field on its own, because "rename keeps the ID" does not prove that
    // any other edit does.
    MaterialDefinition edited = steelDefinition();
    edited.designation = "S235JR";
    REQUIRE(features::setMaterialDefinition(document, id, edited));
    REQUIRE(features::findMaterial(document, id)->materialId() == id);

    edited.standard = "EN 10025-2:2019";
    REQUIRE(features::setMaterialDefinition(document, id, edited));
    REQUIRE(features::findMaterial(document, id)->materialId() == id);

    edited.family = "Structural Steel";
    REQUIRE(features::setMaterialDefinition(document, id, edited));
    REQUIRE(features::findMaterial(document, id)->materialId() == id);

    edited.notes = "Normalised.";
    REQUIRE(features::setMaterialDefinition(document, id, edited));
    REQUIRE(features::findMaterial(document, id)->materialId() == id);

    edited.origin = MaterialLibraryKey{"bettercad", "steel-s235jr", 1};
    REQUIRE(features::setMaterialDefinition(document, id, edited));
    REQUIRE(features::findMaterial(document, id)->materialId() == id);

    REQUIRE(features::findMaterial(document, id)->definition() == edited);
}

// --- names and designations -------------------------------------------------

TEST_CASE("Material_TwoMaterialsMayShareADesignationAndStayDistinct") {
    Document document{"Part"};
    MaterialDefinition first = steelDefinition();
    first.notes = "supplier A";
    MaterialDefinition second = steelDefinition();
    second.notes = "supplier B";

    const MaterialId a = createOrFail(document, "SteelA", first);
    const MaterialId b = createOrFail(document, "SteelB", second);

    REQUIRE(a != b);
    REQUIRE(features::findMaterial(document, a)->definition().designation == "Steel");
    REQUIRE(features::findMaterial(document, b)->definition().designation == "Steel");
    REQUIRE(features::findMaterial(document, a)->definition().notes == "supplier A");
    REQUIRE(features::findMaterial(document, b)->definition().notes == "supplier B");
}

TEST_CASE("Material_ADesignationLookupReportsEveryMatchAndNeverPicksOne") {
    Document document{"Part"};
    const MaterialId a = createOrFail(document, "SteelA", steelDefinition());
    const MaterialId b = createOrFail(document, "SteelB", steelDefinition());
    MaterialDefinition aluminium;
    aluminium.designation = "Aluminium 6061-T6";
    const MaterialId c = createOrFail(document, "Al6061T6", aluminium);

    // N matches: all of them, in ID order.
    const std::vector<MaterialId> steels = features::findMaterialsByDesignation(document, "Steel");
    REQUIRE(steels == std::vector<MaterialId>{a, b});
    // 1 match.
    REQUIRE(features::findMaterialsByDesignation(document, "Aluminium 6061-T6") ==
            std::vector<MaterialId>{c});
    // 0 matches, distinguishable from the above by being empty.
    REQUIRE(features::findMaterialsByDesignation(document, "Titanium").empty());
}

TEST_CASE("Material_ADesignationLookupIsExactSoCaseAndSpacingDoNotMergeMaterials") {
    Document document{"Part"};
    MaterialDefinition upper;
    upper.designation = "Steel";
    MaterialDefinition lower;
    lower.designation = "steel";
    MaterialDefinition padded;
    padded.designation = " Steel";

    const MaterialId a = createOrFail(document, "Upper", upper);
    const MaterialId b = createOrFail(document, "Lower", lower);
    const MaterialId c = createOrFail(document, "Padded", padded);
    REQUIRE(a != b);
    REQUIRE(b != c);

    REQUIRE(features::findMaterialsByDesignation(document, "Steel") == std::vector<MaterialId>{a});
    REQUIRE(features::findMaterialsByDesignation(document, "steel") == std::vector<MaterialId>{b});
    REQUIRE(features::findMaterialsByDesignation(document, " Steel") == std::vector<MaterialId>{c});
}

TEST_CASE("Material_ObjectNamesStayUniqueAndAreNotIdentity") {
    Document document{"Part"};
    const MaterialId a = createOrFail(document, "Steel");

    // The document's unique-name rule applies to materials like anything else.
    const Result<MaterialId> clash = features::createMaterial(document, "Steel");
    REQUIRE_FALSE(clash);
    REQUIRE(clash.error().code == ErrorCode::AlreadyExists);
    // And the failure consumed no ID: the next material gets the ID the
    // rejected one would have, so a rejected create perturbs nothing.
    const MaterialId b = createOrFail(document, document.uniqueName("Steel"));
    REQUIRE(b.value() == a.value() + 1);
}

TEST_CASE("Material_AnEngineeringLabelIsNotALegalObjectNameSoItLivesInTheDesignation") {
    Document document{"Part"};
    // "Aluminium 6061-T6" has a space and a hyphen, which the object identifier
    // rule forbids. That is why a material carries two names (ADR-025).
    const Result<MaterialId> rejected = features::createMaterial(document, "Aluminium 6061-T6");
    REQUIRE_FALSE(rejected);
    REQUIRE(rejected.error().code == ErrorCode::InvalidArgument);

    MaterialDefinition definition;
    definition.designation = "Aluminium 6061-T6";
    const MaterialId id = createOrFail(document, "Al6061T6", definition);
    REQUIRE(features::findMaterial(document, id)->definition().designation == "Aluminium 6061-T6");
}

TEST_CASE("Material_MetadataIsOptionalAndStoredExactlyAsGiven") {
    Document document{"Part"};
    // A material that is named and not yet described is a normal state.
    const MaterialId bare = createOrFail(document, "Unknown1");
    REQUIRE(features::findMaterial(document, bare)->definition() == MaterialDefinition{});

    MaterialDefinition odd;
    odd.designation = "  spaced  ";
    odd.notes = "line1\nline2";
    odd.family = "Fibre-Reinforced Polymer";
    const MaterialId id = createOrFail(document, "Odd", odd);
    // Not trimmed, not collapsed, not normalised.
    REQUIRE(features::findMaterial(document, id)->definition().designation == "  spaced  ");
    REQUIRE(features::findMaterial(document, id)->definition().notes == "line1\nline2");
    REQUIRE(features::findMaterial(document, id)->definition().family ==
            "Fibre-Reinforced Polymer");
}

TEST_CASE("Material_AcceptsAFamilyNoBuiltInEntryUses") {
    Document document{"Part"};
    MaterialDefinition definition;
    definition.family = "Bio-Based Composite";
    const MaterialId id = createOrFail(document, "Custom", definition);
    REQUIRE(features::findMaterial(document, id)->definition().family == "Bio-Based Composite");
}

// --- the library ------------------------------------------------------------

TEST_CASE("Material_ImportingALibraryEntryCopiesItAndRecordsWhereItCameFrom") {
    Document document{"Part"};
    const Result<LibraryMaterial> entry =
        materials::findLibraryMaterial(materials::builtInLibraryName(), "al-6061-t6");
    REQUIRE(entry);

    const Result<MaterialId> id = features::importLibraryMaterial(document, "Al6061T6", *entry);
    REQUIRE(id);

    const Material* imported = features::findMaterial(document, *id);
    REQUIRE(imported != nullptr);
    REQUIRE(imported->definition().designation == entry->designation());
    REQUIRE(imported->definition().standard == entry->standard());
    REQUIRE(imported->definition().family == entry->family());
    REQUIRE(imported->definition().origin.has_value());
    REQUIRE(*imported->definition().origin == entry->key());
}

TEST_CASE("Material_EditingAnImportedMaterialLeavesTheLibraryEntryUntouched") {
    Document document{"Part"};
    const Result<LibraryMaterial> entry =
        materials::findLibraryMaterial(materials::builtInLibraryName(), "al-6061-t6");
    REQUIRE(entry);
    const std::string designationBefore{entry->designation()};

    const Result<MaterialId> id = features::importLibraryMaterial(document, "Al6061T6", *entry);
    REQUIRE(id);

    MaterialDefinition edited = features::findMaterial(document, *id)->definition();
    edited.designation = "6061 Aluminium, supplier measured";
    edited.notes = "density measured on batch 4471";
    REQUIRE(features::setMaterialDefinition(document, *id, edited));

    // The document's copy changed.
    REQUIRE(features::findMaterial(document, *id)->definition().designation ==
            "6061 Aluminium, supplier measured");
    // The library did not, and neither did its identity.
    const Result<LibraryMaterial> again =
        materials::findLibraryMaterial(materials::builtInLibraryName(), "al-6061-t6");
    REQUIRE(again);
    REQUIRE(again->designation() == designationBefore);
    REQUIRE(*again == *entry);
    // And the document's material keeps its own ID through the edit.
    REQUIRE(features::findMaterial(document, *id)->materialId() == *id);
}

TEST_CASE("Material_TwoImportsOfOneLibraryEntryAreTwoMaterials") {
    Document document{"Part"};
    const Result<LibraryMaterial> entry =
        materials::findLibraryMaterial(materials::builtInLibraryName(), "steel-s235jr");
    REQUIRE(entry);

    const Result<MaterialId> first = features::importLibraryMaterial(document, "SteelA", *entry);
    const Result<MaterialId> second = features::importLibraryMaterial(document, "SteelB", *entry);
    REQUIRE(first);
    REQUIRE(second);
    REQUIRE(*first != *second);
    // Same origin, same designation, different materials.
    REQUIRE(features::findMaterial(document, *first)->definition().origin ==
            features::findMaterial(document, *second)->definition().origin);
    REQUIRE(features::findMaterialsByDesignation(document, entry->designation()).size() == 2);
}

TEST_CASE("Material_ImportedMaterialsOfTwoDocumentsAreIndependent") {
    const Result<LibraryMaterial> entry =
        materials::findLibraryMaterial(materials::builtInLibraryName(), "al-6061-t6");
    REQUIRE(entry);

    Document first{"PartA"};
    Document second{"PartB"};
    const Result<MaterialId> a = features::importLibraryMaterial(first, "Al6061T6", *entry);
    const Result<MaterialId> b = features::importLibraryMaterial(second, "Al6061T6", *entry);
    REQUIRE(a);
    REQUIRE(b);

    MaterialDefinition edited = features::findMaterial(first, *a)->definition();
    edited.designation = "changed in A only";
    REQUIRE(features::setMaterialDefinition(first, *a, edited));

    REQUIRE(features::findMaterial(first, *a)->definition().designation == "changed in A only");
    REQUIRE(features::findMaterial(second, *b)->definition().designation == entry->designation());
}

TEST_CASE("Material_ALibraryIdentityAndAMaterialIdCannotBeConfused") {
    Document document{"Part"};
    const Result<LibraryMaterial> entry =
        materials::findLibraryMaterial(materials::builtInLibraryName(), "al-6061-t6");
    REQUIRE(entry);
    const Result<MaterialId> id = features::importLibraryMaterial(document, "Al6061T6", *entry);
    REQUIRE(id);

    // A library entry is named by a structured key and has no numeric ID at all,
    // so there is no value a MaterialId could collide with (ADR-025). The
    // document's material is reachable only by its MaterialId, and the library's
    // entry only by its key.
    REQUIRE(features::findMaterial(document, *id) != nullptr);
    REQUIRE(materials::findLibraryMaterial(entry->key()));
    // The origin recorded on the document material is provenance, not a handle:
    // it does not resolve to a document object.
    REQUIRE(features::findMaterial(document, *id)->definition().origin->entry == "al-6061-t6");
}

TEST_CASE("Material_AnOriginThatCouldNotBeResolvedLaterIsRejected") {
    Document document{"Part"};
    for (const MaterialLibraryKey& bad : {MaterialLibraryKey{"", "al-6061-t6", 1},
                                          MaterialLibraryKey{"bettercad", "", 1},
                                          MaterialLibraryKey{"bettercad", "al-6061-t6", 0},
                                          MaterialLibraryKey{"bettercad", "al-6061-t6", -1}}) {
        MaterialDefinition definition;
        definition.origin = bad;
        INFO("origin " << materials::toString(bad));
        const Result<MaterialId> id = features::createMaterial(document, "Candidate", definition);
        REQUIRE_FALSE(id);
        REQUIRE(id.error().code == ErrorCode::InvalidArgument);
    }
    REQUIRE(features::materialCount(document) == 0);
}

// --- lookup, deletion, and what must never rebind ---------------------------

TEST_CASE("Material_LookupOfAnIdThatIsNotThereFindsNothingRatherThanSomethingElse") {
    Document document{"Part"};
    const MaterialId real = createOrFail(document, "Steel", steelDefinition());

    REQUIRE(features::findMaterial(document, MaterialId::fromValue(real.value() + 1)) == nullptr);
    REQUIRE(features::findMaterial(document, MaterialId{}) == nullptr);
    // An ID that names a different kind of object is not a material either.
    const Result<ParameterId> parameter = document.createParameter("width", 10_mm, units::mm);
    REQUIRE(parameter);
    REQUIRE(features::findMaterial(document, MaterialId::fromValue(parameter->value())) == nullptr);
}

TEST_CASE("Material_OperationsOnAMissingMaterialFailAndChangeNothing") {
    Document document{"Part"};
    const MaterialId missing = MaterialId::fromValue(4471);

    const Result<bool> set = features::setMaterialDefinition(document, missing, steelDefinition());
    REQUIRE_FALSE(set);
    REQUIRE(set.error().code == ErrorCode::NotFound);

    const Result<void> removed = features::removeMaterial(document, missing);
    REQUIRE_FALSE(removed);
    REQUIRE(removed.error().code == ErrorCode::NotFound);
    REQUIRE(features::materialCount(document) == 0);
}

TEST_CASE("Material_DeletingOneLeavesTheOthersAndTheirIdsAlone") {
    Document document{"Part"};
    const MaterialId a = createOrFail(document, "SteelA");
    const MaterialId b = createOrFail(document, "SteelB");
    const MaterialId c = createOrFail(document, "SteelC");

    REQUIRE(features::removeMaterial(document, b));

    REQUIRE(features::materialIds(document) == std::vector<MaterialId>{a, c});
    REQUIRE(features::findMaterial(document, a)->name() == "SteelA");
    REQUIRE(features::findMaterial(document, c)->name() == "SteelC");
    REQUIRE(features::findMaterial(document, b) == nullptr);
}

TEST_CASE("Material_ADeletedIdIsNeverHandedOutAgain") {
    Document document{"Part"};
    const MaterialId a = createOrFail(document, "SteelA");
    REQUIRE(features::removeMaterial(document, a));

    const MaterialId b = createOrFail(document, "SteelB");
    REQUIRE(b != a);
    REQUIRE(b.value() > a.value());
}

TEST_CASE("Material_ADeletedIdStillDoesNotResolveAfterAnIdenticalMaterialIsCreated") {
    Document document{"Part"};
    const MaterialId a = createOrFail(document, "Steel", steelDefinition());
    const MaterialLibraryKey originOfA{"bettercad", "steel-s235jr", 1};
    MaterialDefinition withOrigin = steelDefinition();
    withOrigin.origin = originOfA;
    REQUIRE(features::setMaterialDefinition(document, a, withOrigin));

    REQUIRE(features::removeMaterial(document, a));
    REQUIRE(features::findMaterial(document, a) == nullptr);

    // Same object name, same designation, same standard, same family, same
    // notes, same origin: indistinguishable except for identity.
    const Result<MaterialId> c = features::createMaterial(document, "Steel", withOrigin);
    REQUIRE(c);
    REQUIRE(*c != a);
    // The old reference must STAY unresolved. This is the invariant a later
    // material assignment depends on: a stale MaterialId never quietly names a
    // different material.
    REQUIRE(features::findMaterial(document, a) == nullptr);
    REQUIRE(features::findMaterial(document, *c) != nullptr);
    REQUIRE(features::findMaterial(document, *c)->definition() == withOrigin);
}

TEST_CASE("Material_AnIdAlreadyInUseIsRejectedRatherThanOverwriting") {
    Document document{"Part"};
    const MaterialId a = createOrFail(document, "SteelA", steelDefinition());

    // The shape a loader takes: an object arriving with an ID that is taken.
    Result<std::unique_ptr<Material>> duplicate = Material::create("SteelB", MaterialDefinition{});
    REQUIRE(duplicate);
    const Result<void> restored = document.restoreObject(a, std::move(*duplicate));
    REQUIRE_FALSE(restored);
    REQUIRE(restored.error().code == ErrorCode::AlreadyExists);

    // The original survives untouched.
    REQUIRE(features::findMaterial(document, a) != nullptr);
    REQUIRE(features::findMaterial(document, a)->name() == "SteelA");
    REQUIRE(features::findMaterial(document, a)->definition() == steelDefinition());
}

TEST_CASE("Material_APointerFromLookupSurvivesEditsToOtherMaterials") {
    Document document{"Part"};
    const MaterialId a = createOrFail(document, "SteelA", steelDefinition());
    const Material* held = features::findMaterial(document, a);
    REQUIRE(held != nullptr);

    const MaterialId b = createOrFail(document, "SteelB");
    REQUIRE(features::setMaterialDefinition(document, b, steelDefinition()));
    REQUIRE(features::removeMaterial(document, b));

    // Still the same object, still valid: the document holds its objects by
    // unique_ptr in a node-based map.
    REQUIRE(held == features::findMaterial(document, a));
    REQUIRE(held->definition() == steelDefinition());
}

// --- enumeration and equality ----------------------------------------------

TEST_CASE("Material_EnumeratesInAscendingIdOrderEveryTime") {
    Document document{"Part"};
    const MaterialId a = createOrFail(document, "Zinc");
    const MaterialId b = createOrFail(document, "Aluminium");
    const MaterialId c = createOrFail(document, "Brass");

    // Insertion order, not alphabetical order, because the order is the ID's.
    const std::vector<MaterialId> expected{a, b, c};
    for (int pass = 0; pass < 10; ++pass) {
        REQUIRE(features::materialIds(document) == expected);
    }
    REQUIRE(features::materialCount(document) == 3);

    // Deleting the first does not renumber the rest.
    REQUIRE(features::removeMaterial(document, a));
    REQUIRE(features::materialIds(document) == std::vector<MaterialId>{b, c});
}

TEST_CASE("Material_EnumerationSkipsObjectsThatAreNotMaterials") {
    Document document{"Part"};
    const Result<ParameterId> parameter = document.createParameter("width", 10_mm, units::mm);
    REQUIRE(parameter);
    const MaterialId a = createOrFail(document, "Steel");
    REQUIRE(features::materialIds(document) == std::vector<MaterialId>{a});
}

TEST_CASE("Material_SameMetadataWithDifferentIdsAreDifferentObjects") {
    Document document{"Part"};
    const MaterialId a = createOrFail(document, "SteelA", steelDefinition());
    const MaterialId b = createOrFail(document, "SteelB", steelDefinition());

    const Material* first = features::findMaterial(document, a);
    const Material* second = features::findMaterial(document, b);
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);

    // contentEquals compares what the material IS, and these two are alike.
    REQUIRE(first->contentEquals(*second));
    // equivalent() also compares identity and name, and these two differ.
    REQUIRE_FALSE(equivalent(*first, *second));
    REQUIRE(first->materialId() != second->materialId());
}

TEST_CASE("Material_ACloneIsTheSameMaterialAndACopyOfItsContent") {
    Document document{"Part"};
    const MaterialId a = createOrFail(document, "Steel", steelDefinition());
    const std::unique_ptr<DocumentObject> copy = features::findMaterial(document, a)->clone();

    // The literal, not Material::kTypeName. Binding a reference to a
    // dll-imported constexpr static does not link in a shared build, which is
    // the convention DocumentJson.cpp already records; the static_assert below
    // is what keeps the two from drifting.
    REQUIRE(copy->typeName() == "material");
    REQUIRE(equivalent(*copy, *features::findMaterial(document, a)));
    const auto* typed = dynamic_cast<const Material*>(copy.get());
    REQUIRE(typed != nullptr);
    REQUIRE(typed->materialId() == a);
    REQUIRE(typed->definition() == steelDefinition());
}

TEST_CASE("Material_SettingTheSameDefinitionChangesNothingAndBumpsNoRevision") {
    Document document{"Part"};
    const MaterialId a = createOrFail(document, "Steel", steelDefinition());
    const std::uint64_t documentRevision = document.revision();
    const std::uint64_t objectRevision = *document.revisionOf(a);

    const Result<bool> unchanged = features::setMaterialDefinition(document, a, steelDefinition());
    REQUIRE(unchanged);
    REQUIRE_FALSE(*unchanged);
    REQUIRE(document.revision() == documentRevision);
    REQUIRE(*document.revisionOf(a) == objectRevision);

    MaterialDefinition edited = steelDefinition();
    edited.notes = "different";
    const Result<bool> changed = features::setMaterialDefinition(document, a, edited);
    REQUIRE(changed);
    REQUIRE(*changed);
    REQUIRE(document.revision() > documentRevision);
    REQUIRE(*document.revisionOf(a) > objectRevision);
}

TEST_CASE("Material_ReportsItsTypeNameForFilesAndDiagnostics") {
    Document document{"Part"};
    const MaterialId a = createOrFail(document, "Steel");
    REQUIRE(features::findMaterial(document, a)->typeName() == "material");
    // In a constant expression, so it needs no link-time symbol for the
    // dll-imported member and still pins the constant to the literal every
    // other comparison in this file uses.
    static_assert(Material::kTypeName == "material");
}

// --- the public API a caller actually writes --------------------------------

TEST_CASE("Material_TheDocumentedWorkflowBehavesAsDocumented") {
    Document document{"Part"};

    MaterialDefinition steel;
    steel.designation = "Steel";
    const Result<MaterialId> a = features::createMaterial(document, "SteelA", steel);
    const Result<MaterialId> b = features::createMaterial(document, "SteelB", steel);
    REQUIRE(a);
    REQUIRE(b);
    REQUIRE(*a != *b);

    REQUIRE(document.rename(*a, "StructuralSteel"));

    REQUIRE(features::findMaterial(document, *a)->materialId() == *a);
    REQUIRE(features::findMaterial(document, *a)->name() == "StructuralSteel");
    REQUIRE(features::findMaterial(document, *b)->name() == "SteelB");
    REQUIRE(features::findMaterial(document, *a)->definition().designation == "Steel");
    REQUIRE(features::findMaterial(document, *b)->definition().designation == "Steel");
}

TEST_CASE("Material_TwoDocumentsBuiltTheSameWayAgreeOnEverythingObservable") {
    // Determinism of the contract that is actually promised: the same sequence
    // of operations on a fresh document gives the same IDs, the same order and
    // the same metadata. Two UNRELATED documents are not promised to agree, and
    // this does not claim they are.
    const auto build = [](Document& document) {
        const Result<LibraryMaterial> entry =
            materials::findLibraryMaterial(materials::builtInLibraryName(), "al-6061-t6");
        REQUIRE(entry);
        REQUIRE(features::importLibraryMaterial(document, "Al6061T6", *entry));
        REQUIRE(features::createMaterial(document, "SteelA", steelDefinition()));
        const Result<MaterialId> throwaway = features::createMaterial(document, "Temp");
        REQUIRE(throwaway);
        REQUIRE(features::removeMaterial(document, *throwaway));
        REQUIRE(features::createMaterial(document, "SteelB", steelDefinition()));
    };

    Document first{"Part"};
    Document second{"Part"};
    build(first);
    build(second);

    const std::vector<MaterialId> firstIds = features::materialIds(first);
    REQUIRE(firstIds == features::materialIds(second));
    REQUIRE(firstIds.size() == 3);
    for (const MaterialId id : firstIds) {
        INFO("material " << id.value());
        const Material* a = features::findMaterial(first, id);
        const Material* b = features::findMaterial(second, id);
        REQUIRE(a != nullptr);
        REQUIRE(b != nullptr);
        REQUIRE(a->name() == b->name());
        REQUIRE(a->definition() == b->definition());
    }
}

TEST_CASE("Material_AMaterialIdMeansSomethingOnlyInsideItsOwnDocument") {
    // Deliberate, not accidental. An ObjectId is document-local throughout
    // BetterCAD, and naming something in another document is what
    // ObjectReference is for -- durable identity plus an untrusted locator. So
    // the same number is a different material in a different document, and this
    // records that rather than leaving a reader to discover it.
    Document first{"PartA"};
    Document second{"PartB"};

    MaterialDefinition aluminium;
    aluminium.designation = "Aluminium 6061-T6";
    const MaterialId a = createOrFail(first, "Al6061T6", aluminium);
    const MaterialId b = createOrFail(second, "Steel", steelDefinition());

    // Fresh documents allocate from the same start, so the numbers coincide.
    REQUIRE(a.value() == b.value());
    // And each resolves, inside its own document, to its own material.
    REQUIRE(features::findMaterial(first, a)->definition().designation == "Aluminium 6061-T6");
    REQUIRE(features::findMaterial(second, b)->definition().designation == "Steel");
    // Handing A's ID to B therefore finds B's material, not A's and not nothing.
    // That is why a cross-document reference must carry a DocumentId.
    REQUIRE(features::findMaterial(second, a) != nullptr);
    REQUIRE(features::findMaterial(second, a)->definition().designation == "Steel");
    REQUIRE(first.id() != second.id());
}

TEST_CASE("Material_AnEmptyDesignationIsAValueAndIsSearchableAsOne") {
    Document document{"Part"};
    const MaterialId undescribed = createOrFail(document, "Unknown1");
    const MaterialId also = createOrFail(document, "Unknown2");
    const MaterialId named = createOrFail(document, "Steel", steelDefinition());

    // Searching for "" finds the materials that have no designation. It does not
    // match everything, and it does not match nothing.
    REQUIRE(features::findMaterialsByDesignation(document, "") ==
            std::vector<MaterialId>{undescribed, also});
    REQUIRE(features::findMaterialsByDesignation(document, "Steel") ==
            std::vector<MaterialId>{named});
}

TEST_CASE("Material_ANonAsciiDesignationIsStoredByteForByteAndIsNotIdentity") {
    Document document{"Part"};
    // An object name may not contain these at all -- the identifier rule is
    // ASCII letters, digits and '_' -- which is the other reason a material
    // needs a separate designation.
    REQUIRE_FALSE(features::createMaterial(document, "Kupfer\xC3\xA4"));

    MaterialDefinition definition;
    definition.designation = "Kupfer \xC3\xA4 \xE2\x8C\x80";  // UTF-8: ä, ⌀
    definition.notes = "\xC2\xB5m tolerance";                  // UTF-8: µ
    const MaterialId id = createOrFail(document, "Kupfer", definition);

    const MaterialDefinition& stored = features::findMaterial(document, id)->definition();
    REQUIRE(stored.designation == definition.designation);
    REQUIRE(stored.notes == definition.notes);
    REQUIRE(stored.designation.size() == definition.designation.size());

    // No normalisation, so a different byte sequence is a different designation
    // even where Unicode would call the two equivalent. Identity is the ID, so
    // this changes nothing about which material it is.
    const std::string decomposed = "Kupfer a\xCC\x88 \xE2\x8C\x80";
    REQUIRE(features::findMaterialsByDesignation(document, decomposed).empty());
    REQUIRE(features::findMaterialsByDesignation(document, definition.designation) ==
            std::vector<MaterialId>{id});
}

TEST_CASE("Material_SurvivesADocumentCopyWithItsIdentityAndContent") {
    Document document{"Part"};
    const Result<LibraryMaterial> entry =
        materials::findLibraryMaterial(materials::builtInLibraryName(), "al-6061-t6");
    REQUIRE(entry);
    const Result<MaterialId> imported =
        features::importLibraryMaterial(document, "Al6061T6", *entry);
    REQUIRE(imported);
    const MaterialId own = createOrFail(document, "SteelA", steelDefinition());

    const Document copy = document.clone();

    REQUIRE(features::materialIds(copy) == features::materialIds(document));
    for (const MaterialId id : features::materialIds(document)) {
        INFO("material " << id.value());
        const Material* original = features::findMaterial(document, id);
        const Material* copied = features::findMaterial(copy, id);
        REQUIRE(copied != nullptr);
        REQUIRE(copied != original);  // a copy, not the same object
        REQUIRE(equivalent(*copied, *original));
        REQUIRE(copied->definition() == original->definition());
    }
    REQUIRE(features::findMaterial(copy, *imported)->definition().origin == entry->key());
    REQUIRE(features::findMaterial(copy, own)->definition() == steelDefinition());
}

TEST_CASE("Material_CanBeRemovedAndPutBackUnderItsOwnIdWhichIsWhatUndoNeeds") {
    // Commands and undo are a later milestone (P15-CMD-001). What has to be true
    // now is that the data model does not stand in the way: a material can be
    // taken out and restored under the SAME identity, which is exactly the shape
    // an undo of a delete takes.
    Document document{"Part"};
    const MaterialId a = createOrFail(document, "SteelA", steelDefinition());
    const MaterialId b = createOrFail(document, "SteelB");

    Result<std::unique_ptr<DocumentObject>> removed = document.removeObject(a);
    REQUIRE(removed);
    REQUIRE(features::findMaterial(document, a) == nullptr);
    REQUIRE(features::materialIds(document) == std::vector<MaterialId>{b});

    REQUIRE(document.insertObject(std::move(*removed)));

    const Material* restored = features::findMaterial(document, a);
    REQUIRE(restored != nullptr);
    REQUIRE(restored->materialId() == a);
    REQUIRE(restored->name() == "SteelA");
    REQUIRE(restored->definition() == steelDefinition());
    // And it is back in its place in the order, because the order is the ID's.
    REQUIRE(features::materialIds(document) == std::vector<MaterialId>{a, b});
}

// --- mechanical properties (P15-MECH-001) -----------------------------------

namespace {

materials::MechanicalProperties steelMechanical() {
    // Arithmetic fixtures, not a claim about any real steel: this milestone
    // stores no sourced property values (ADR-028).
    materials::MechanicalProperties properties;
    properties.density = materials::MaterialProperty<Density>::known(Density::fromSi(7850.0));
    properties.youngsModulus = materials::MaterialProperty<ElasticModulus>::known(210_GPa);
    properties.poissonRatio =
        materials::MaterialProperty<PoissonRatio>::known(PoissonRatio::of(0.30));
    properties.yieldStrength = materials::MaterialProperty<Stress>::known(250_MPa);
    return properties;
}

} // namespace

TEST_CASE("Material_EditingAMechanicalPropertyDoesNotChangeItsId") {
    Document document{"Part"};
    const MaterialId id = createOrFail(document, "SteelA", steelDefinition());
    const MaterialId before = features::findMaterial(document, id)->materialId();

    REQUIRE(features::setMaterialMechanical(document, id, steelMechanical()));

    // The identity survives a property edit, which is what a later assignment
    // holding a MaterialId depends on.
    REQUIRE(features::findMaterial(document, id)->materialId() == before);
    REQUIRE(features::findMaterial(document, id)->materialId() == id);
    // And the metadata is untouched by a mechanical edit.
    const MaterialDefinition& definition = features::findMaterial(document, id)->definition();
    REQUIRE(definition.designation == steelDefinition().designation);
    REQUIRE(definition.standard == steelDefinition().standard);
    REQUIRE(*definition.mechanical.youngsModulus.value() == 210_GPa);

    // Changing one property again still does not touch the ID.
    materials::MechanicalProperties changed = steelMechanical();
    changed.youngsModulus = materials::MaterialProperty<ElasticModulus>::known(200_GPa);
    REQUIRE(features::setMaterialMechanical(document, id, changed));
    REQUIRE(features::findMaterial(document, id)->materialId() == before);
}

TEST_CASE("Material_RefusesAnInvalidMechanicalPropertyAndKeepsWhatItHad") {
    Document document{"Part"};
    const MaterialId id = createOrFail(document, "SteelA");
    REQUIRE(features::setMaterialMechanical(document, id, steelMechanical()));

    materials::MechanicalProperties bad = steelMechanical();
    bad.youngsModulus =
        materials::MaterialProperty<ElasticModulus>::known(ElasticModulus::fromSi(0.0));
    const Result<bool> refused = features::setMaterialMechanical(document, id, bad);
    REQUIRE_FALSE(refused);
    REQUIRE(refused.error().code == ErrorCode::InvalidArgument);

    // Unchanged: a rejected edit leaves the material exactly as it was.
    REQUIRE(*features::findMaterial(document, id)->definition().mechanical.youngsModulus.value()
            == 210_GPa);

    // Nor can a non-finite value reach a material. A MaterialProperty can HOLD
    // one in a local variable -- quantity construction is unchecked throughout
    // BetterCAD -- but the material layer is where it is stopped.
    materials::MechanicalProperties notFinite = steelMechanical();
    notFinite.density = materials::MaterialProperty<Density>::known(
        Density::fromSi(std::numeric_limits<double>::quiet_NaN()));
    REQUIRE_FALSE(features::setMaterialMechanical(document, id, notFinite));
    notFinite.density = materials::MaterialProperty<Density>::known(
        Density::fromSi(std::numeric_limits<double>::infinity()));
    REQUIRE_FALSE(features::setMaterialMechanical(document, id, notFinite));
    REQUIRE(features::findMaterial(document, id)->definition().mechanical == steelMechanical());

    // A material cannot be CREATED with an invalid property either.
    MaterialDefinition definition;
    definition.mechanical.poissonRatio =
        materials::MaterialProperty<PoissonRatio>::known(PoissonRatio::of(0.5));
    const Result<MaterialId> rejected = features::createMaterial(document, "Bad", definition);
    REQUIRE_FALSE(rejected);
    REQUIRE(rejected.error().code == ErrorCode::InvalidArgument);
}

TEST_CASE("Material_AnImportedMaterialHasNoPropertiesAndEditingItCannotReachTheLibrary") {
    Document document{"Part"};
    const Result<LibraryMaterial> entry =
        materials::findLibraryMaterial(materials::builtInLibraryName(), "al-6061-t6");
    REQUIRE(entry);
    const Result<MaterialId> id = features::importLibraryMaterial(document, "Al6061T6", *entry);
    REQUIRE(id);

    // The library carries METADATA ONLY at this milestone, so an import starts
    // with every mechanical property Unknown rather than with a placeholder.
    const materials::MechanicalProperties& imported =
        features::findMaterial(document, *id)->definition().mechanical;
    REQUIRE(imported == materials::MechanicalProperties{});
    REQUIRE(imported.youngsModulus.isUnknown());

    REQUIRE(features::setMaterialMechanical(document, *id, steelMechanical()));
    REQUIRE(*features::findMaterial(document, *id)->definition().mechanical.youngsModulus.value()
            == 210_GPa);

    // The library entry is identical afterwards, and a second import still
    // arrives with nothing set: there is no shared mutable state to reach.
    const Result<LibraryMaterial> again =
        materials::findLibraryMaterial(materials::builtInLibraryName(), "al-6061-t6");
    REQUIRE(again);
    REQUIRE(*again == *entry);
    const Result<MaterialId> second =
        features::importLibraryMaterial(document, "Al6061T6b", *again);
    REQUIRE(second);
    REQUIRE(features::findMaterial(document, *second)->definition().mechanical
            == materials::MechanicalProperties{});
}

TEST_CASE("Material_MechanicalPropertiesOfTwoDocumentsAreIndependent") {
    const Result<LibraryMaterial> entry =
        materials::findLibraryMaterial(materials::builtInLibraryName(), "al-6061-t6");
    REQUIRE(entry);
    Document first{"PartA"};
    Document second{"PartB"};
    const Result<MaterialId> a = features::importLibraryMaterial(first, "Al6061T6", *entry);
    const Result<MaterialId> b = features::importLibraryMaterial(second, "Al6061T6", *entry);
    REQUIRE(a);
    REQUIRE(b);

    REQUIRE(features::setMaterialMechanical(first, *a, steelMechanical()));

    REQUIRE(features::findMaterial(first, *a)->definition().mechanical == steelMechanical());
    REQUIRE(features::findMaterial(second, *b)->definition().mechanical
            == materials::MechanicalProperties{});
}

TEST_CASE("Material_RequiresACompleteElasticSetOrFailsNamingEveryGap") {
    Document document{"Part"};
    const MaterialId id = createOrFail(document, "SteelA");

    SECTION("nothing known: both inputs are named, and so is the material") {
        const Result<materials::LinearElasticConstants> constants =
            features::requireLinearElasticConstants(document, id);
        REQUIRE_FALSE(constants);
        REQUIRE(constants.error().code == ErrorCode::FailedPrecondition);
        REQUIRE_THAT(constants.error().message, ContainsSubstring("SteelA"));
        REQUIRE_THAT(constants.error().message, ContainsSubstring("object:"));
        REQUIRE_THAT(constants.error().message, ContainsSubstring("no Young"));
        REQUIRE_THAT(constants.error().message, ContainsSubstring("no Poisson"));
    }

    SECTION("only E known: the report names the ratio and not the modulus") {
        materials::MechanicalProperties partial;
        partial.youngsModulus = materials::MaterialProperty<ElasticModulus>::known(210_GPa);
        REQUIRE(features::setMaterialMechanical(document, id, partial));

        const Result<materials::LinearElasticConstants> constants =
            features::requireLinearElasticConstants(document, id);
        REQUIRE_FALSE(constants);
        REQUIRE_THAT(constants.error().message, ContainsSubstring("no Poisson"));
        REQUIRE_THAT(constants.error().message, !ContainsSubstring("no Young"));
        // The MISSING INPUT is named, never the derived constant: told that the
        // shear modulus is unavailable, a user has nothing to act on.
        REQUIRE_THAT(constants.error().message, !ContainsSubstring("shear modulus"));
        REQUIRE_THAT(constants.error().message, !ContainsSubstring("bulk modulus"));
    }

    SECTION("both known: the set is complete and the derived values are right") {
        REQUIRE(features::setMaterialMechanical(document, id, steelMechanical()));
        const Result<materials::LinearElasticConstants> constants =
            features::requireLinearElasticConstants(document, id);
        REQUIRE(constants);
        REQUIRE(constants->youngsModulus == 210_GPa);
        REQUIRE(constants->poissonRatio == PoissonRatio::of(0.30));
        // Hand-computed, not from the production relationship:
        //   G = 210 / (2 x 1.30) = 80.76923076923077 GPa
        //   K = 210 / (3 x 0.4)  = 175 GPa
        REQUIRE_THAT(constants->shearModulus.si(), WithinRel(80.76923076923077e9, 1e-12));
        REQUIRE_THAT(constants->bulkModulus.si(), WithinRel(175.0e9, 1e-12));
    }
}

TEST_CASE("Material_RequiresDensitySeparatelyFromTheElasticConstants") {
    Document document{"Part"};
    const MaterialId id = createOrFail(document, "SteelA");

    // Elastic constants present, density absent: a stiffness calculation must not
    // fail over a density it never uses, and a mass must not silently get one.
    materials::MechanicalProperties elasticOnly;
    elasticOnly.youngsModulus = materials::MaterialProperty<ElasticModulus>::known(210_GPa);
    elasticOnly.poissonRatio =
        materials::MaterialProperty<PoissonRatio>::known(PoissonRatio::of(0.30));
    REQUIRE(features::setMaterialMechanical(document, id, elasticOnly));

    REQUIRE(features::requireLinearElasticConstants(document, id));
    const Result<Density> density = features::requireDensity(document, id);
    REQUIRE_FALSE(density);
    REQUIRE(density.error().code == ErrorCode::FailedPrecondition);
    REQUIRE_THAT(density.error().message, ContainsSubstring("SteelA"));
    REQUIRE_THAT(density.error().message, ContainsSubstring("no density"));

    REQUIRE(features::setMaterialMechanical(document, id, steelMechanical()));
    const Result<Density> found = features::requireDensity(document, id);
    REQUIRE(found);
    REQUIRE_THAT(found->si(), WithinRel(7850.0, 1e-12));
}

TEST_CASE("Material_RequirementsOfAMissingMaterialFailWithoutInventingAnything") {
    Document document{"Part"};
    const MaterialId missing = MaterialId::fromValue(4471);
    const Result<materials::LinearElasticConstants> constants =
        features::requireLinearElasticConstants(document, missing);
    REQUIRE_FALSE(constants);
    REQUIRE(constants.error().code == ErrorCode::NotFound);
    const Result<Density> density = features::requireDensity(document, missing);
    REQUIRE_FALSE(density);
    REQUIRE(density.error().code == ErrorCode::NotFound);
    const Result<bool> set = features::setMaterialMechanical(document, missing, steelMechanical());
    REQUIRE_FALSE(set);
    REQUIRE(set.error().code == ErrorCode::NotFound);
}

TEST_CASE("Material_TwoMaterialsWithIdenticalPropertiesAreStillTwoMaterials") {
    Document document{"Part"};
    const MaterialId a = createOrFail(document, "SteelA", steelDefinition());
    const MaterialId b = createOrFail(document, "SteelB", steelDefinition());
    REQUIRE(features::setMaterialMechanical(document, a, steelMechanical()));
    REQUIRE(features::setMaterialMechanical(document, b, steelMechanical()));

    const Material* first = features::findMaterial(document, a);
    const Material* second = features::findMaterial(document, b);
    REQUIRE(first->definition().mechanical == second->definition().mechanical);
    REQUIRE(first->contentEquals(*second));
    // Alike in every value and still distinct, because identity is the ID.
    REQUIRE_FALSE(equivalent(*first, *second));
    REQUIRE(a != b);
}

TEST_CASE("Material_PropertiesSurviveACloneAndARemoveAndReinsert") {
    Document document{"Part"};
    const MaterialId id = createOrFail(document, "SteelA", steelDefinition());
    REQUIRE(features::setMaterialMechanical(document, id, steelMechanical()));

    const Document copy = document.clone();
    REQUIRE(features::findMaterial(copy, id)->definition().mechanical == steelMechanical());

    Result<std::unique_ptr<DocumentObject>> removed = document.removeObject(id);
    REQUIRE(removed);
    REQUIRE(document.insertObject(std::move(*removed)));
    REQUIRE(features::findMaterial(document, id)->definition().mechanical == steelMechanical());
    REQUIRE(features::findMaterial(document, id)->materialId() == id);
}

// --- thermal properties (P15-THERM-001) -------------------------------------

namespace {

materials::ThermalProperties steelThermal() {
    // Arithmetic fixtures, not a claim about any real steel: no library entry
    // carries a sourced property value (ADR-028).
    materials::ThermalProperties properties;
    properties.thermalConductivity =
        materials::MaterialProperty<ThermalConductivity>::known(50_W_per_m_K, 293.15_K);
    properties.specificHeatCapacity =
        materials::MaterialProperty<SpecificHeatCapacity>::known(500_J_per_kg_K);
    properties.thermalExpansion =
        materials::MaterialProperty<ThermalExpansionCoefficient>::known(12.0e-6_per_K);
    properties.meltingTemperature = materials::MaterialProperty<Temperature>::known(1800_K);
    properties.electricalResistivity = materials::MaterialProperty<materials::ElectricalResistivity>::
        known(materials::ElectricalResistivity::ofOhmMetres(1.4e-7));
    return properties;
}

} // namespace

TEST_CASE("Material_EditingAThermalPropertyDoesNotChangeItsId") {
    Document document{"Part"};
    const MaterialId id = createOrFail(document, "SteelA", steelDefinition());
    const MaterialId before = features::findMaterial(document, id)->materialId();

    REQUIRE(features::setMaterialThermal(document, id, steelThermal()));

    REQUIRE(features::findMaterial(document, id)->materialId() == before);
    REQUIRE(features::findMaterial(document, id)->materialId() == id);
    // Metadata untouched by a thermal edit.
    REQUIRE(features::findMaterial(document, id)->definition().designation
            == steelDefinition().designation);
    REQUIRE(features::findMaterial(document, id)->definition().thermal == steelThermal());
}

TEST_CASE("Material_ThermalAndMechanicalPropertiesCoexistWithoutDisturbingEachOther") {
    Document document{"Part"};
    const MaterialId id = createOrFail(document, "SteelA", steelDefinition());

    REQUIRE(features::setMaterialMechanical(document, id, steelMechanical()));
    REQUIRE(features::setMaterialThermal(document, id, steelThermal()));

    // Both halves present at once.
    const MaterialDefinition& both = features::findMaterial(document, id)->definition();
    REQUIRE(both.mechanical == steelMechanical());
    REQUIRE(both.thermal == steelThermal());

    // Editing the thermal half does not reset the mechanical half...
    materials::ThermalProperties changedThermal = steelThermal();
    changedThermal.thermalConductivity =
        materials::MaterialProperty<ThermalConductivity>::known(45_W_per_m_K);
    REQUIRE(features::setMaterialThermal(document, id, changedThermal));
    REQUIRE(features::findMaterial(document, id)->definition().mechanical == steelMechanical());

    // ...and editing the mechanical half does not reset the thermal half.
    materials::MechanicalProperties changedMechanical = steelMechanical();
    changedMechanical.yieldStrength = materials::MaterialProperty<Stress>::known(300_MPa);
    REQUIRE(features::setMaterialMechanical(document, id, changedMechanical));
    REQUIRE(features::findMaterial(document, id)->definition().thermal == changedThermal);
    REQUIRE(features::findMaterial(document, id)->materialId() == id);
}

TEST_CASE("Material_HasExactlyOneDensityAndBothConsumerPathsReadIt") {
    // The single-source proof. A thermalDensity beside a mechanicalDensity would
    // be two authoritative values for one physical quantity, so there is one
    // store -- in the mechanical properties, which is where P15-MECH-001 put it --
    // and the thermal path reads that.
    Document document{"Part"};
    const MaterialId id = createOrFail(document, "SteelA");
    materials::ThermalProperties thermal;
    thermal.thermalConductivity = materials::MaterialProperty<ThermalConductivity>::known(50_W_per_m_K);
    thermal.specificHeatCapacity =
        materials::MaterialProperty<SpecificHeatCapacity>::known(500_J_per_kg_K);
    REQUIRE(features::setMaterialThermal(document, id, thermal));

    SECTION("with no density, the thermal path reports it missing") {
        const Result<features::TransientConductionProperties> transient =
            features::requireTransientConductionProperties(document, id);
        REQUIRE_FALSE(transient);
        REQUIRE_THAT(transient.error().message, ContainsSubstring("no density"));
        REQUIRE_FALSE(features::requireDensity(document, id));
    }

    SECTION("one density set once is what both paths return") {
        materials::MechanicalProperties mechanical;
        mechanical.density = materials::MaterialProperty<Density>::known(Density::fromSi(7850.0));
        REQUIRE(features::setMaterialMechanical(document, id, mechanical));

        const Result<Density> massPath = features::requireDensity(document, id);
        const Result<features::TransientConductionProperties> thermalPath =
            features::requireTransientConductionProperties(document, id);
        REQUIRE(massPath);
        REQUIRE(thermalPath);
        // Same property, same value, bit for bit.
        REQUIRE(thermalPath->density == *massPath);
        REQUIRE(thermalPath->density.si() == massPath->si());
    }

    SECTION("changing the one density changes what the thermal path sees") {
        materials::MechanicalProperties mechanical;
        mechanical.density = materials::MaterialProperty<Density>::known(Density::fromSi(7850.0));
        REQUIRE(features::setMaterialMechanical(document, id, mechanical));
        mechanical.density = materials::MaterialProperty<Density>::known(Density::fromSi(2700.0));
        REQUIRE(features::setMaterialMechanical(document, id, mechanical));

        REQUIRE_THAT(features::requireDensity(document, id)->si(), WithinRel(2700.0, 1e-12));
        REQUIRE_THAT(features::requireTransientConductionProperties(document, id)->density.si(),
                     WithinRel(2700.0, 1e-12));
    }

    SECTION("setting the density back to unknown makes both paths report it missing") {
        materials::MechanicalProperties mechanical;
        mechanical.density = materials::MaterialProperty<Density>::known(Density::fromSi(7850.0));
        REQUIRE(features::setMaterialMechanical(document, id, mechanical));
        mechanical.density = materials::MaterialProperty<Density>::unknown();
        REQUIRE(features::setMaterialMechanical(document, id, mechanical));

        REQUIRE_FALSE(features::requireDensity(document, id));
        REQUIRE_FALSE(features::requireTransientConductionProperties(document, id));
    }
}

TEST_CASE("Material_RefusesAnInvalidThermalPropertyAndKeepsWhatItHad") {
    Document document{"Part"};
    const MaterialId id = createOrFail(document, "SteelA");
    REQUIRE(features::setMaterialThermal(document, id, steelThermal()));

    materials::ThermalProperties bad = steelThermal();
    bad.thermalConductivity =
        materials::MaterialProperty<ThermalConductivity>::known(ThermalConductivity::fromSi(0.0));
    const Result<bool> refused = features::setMaterialThermal(document, id, bad);
    REQUIRE_FALSE(refused);
    REQUIRE(refused.error().code == ErrorCode::InvalidArgument);
    REQUIRE(features::findMaterial(document, id)->definition().thermal == steelThermal());

    // Non-finite cannot enter a material either, through any property.
    materials::ThermalProperties notFinite = steelThermal();
    notFinite.specificHeatCapacity = materials::MaterialProperty<SpecificHeatCapacity>::known(
        SpecificHeatCapacity::fromSi(std::numeric_limits<double>::quiet_NaN()));
    REQUIRE_FALSE(features::setMaterialThermal(document, id, notFinite));
    notFinite = steelThermal();
    notFinite.meltingTemperature = materials::MaterialProperty<Temperature>::known(
        Temperature::fromSi(std::numeric_limits<double>::infinity()));
    REQUIRE_FALSE(features::setMaterialThermal(document, id, notFinite));
    REQUIRE(features::findMaterial(document, id)->definition().thermal == steelThermal());

    // Nor can a material be CREATED with an invalid thermal property.
    MaterialDefinition definition;
    definition.thermal.meltingTemperature =
        materials::MaterialProperty<Temperature>::known(Temperature::fromSi(-5.0));
    const Result<MaterialId> rejected = features::createMaterial(document, "Bad", definition);
    REQUIRE_FALSE(rejected);
    REQUIRE(rejected.error().code == ErrorCode::InvalidArgument);
}

TEST_CASE("Material_AnImportedMaterialHasNoThermalPropertiesAndEditingItCannotReachTheLibrary") {
    Document document{"Part"};
    const Result<LibraryMaterial> entry =
        materials::findLibraryMaterial(materials::builtInLibraryName(), "al-6061-t6");
    REQUIRE(entry);
    const Result<MaterialId> id = features::importLibraryMaterial(document, "Al6061T6", *entry);
    REQUIRE(id);

    // Metadata only in the library, so an import starts with nothing thermal set.
    REQUIRE(features::findMaterial(document, *id)->definition().thermal
            == materials::ThermalProperties{});

    REQUIRE(features::setMaterialThermal(document, *id, steelThermal()));

    // The library entry is identical afterwards, and a second import still
    // arrives empty: no shared mutable thermal state exists to reach.
    const Result<LibraryMaterial> again =
        materials::findLibraryMaterial(materials::builtInLibraryName(), "al-6061-t6");
    REQUIRE(again);
    REQUIRE(*again == *entry);
    const Result<MaterialId> second =
        features::importLibraryMaterial(document, "Al6061T6b", *again);
    REQUIRE(second);
    REQUIRE(features::findMaterial(document, *second)->definition().thermal
            == materials::ThermalProperties{});
    // And the first material kept its own identity through the edit.
    REQUIRE(features::findMaterial(document, *id)->materialId() == *id);
}

TEST_CASE("Material_ThermalPropertiesOfTwoDocumentsAreIndependent") {
    const Result<LibraryMaterial> entry =
        materials::findLibraryMaterial(materials::builtInLibraryName(), "al-6061-t6");
    REQUIRE(entry);
    Document first{"PartA"};
    Document second{"PartB"};
    const Result<MaterialId> a = features::importLibraryMaterial(first, "Al6061T6", *entry);
    const Result<MaterialId> b = features::importLibraryMaterial(second, "Al6061T6", *entry);
    REQUIRE(a);
    REQUIRE(b);

    REQUIRE(features::setMaterialThermal(first, *a, steelThermal()));

    REQUIRE(features::findMaterial(first, *a)->definition().thermal == steelThermal());
    REQUIRE(features::findMaterial(second, *b)->definition().thermal
            == materials::ThermalProperties{});
}

TEST_CASE("Material_RequiresConductionPropertiesPerConsumerAndNamesEveryGap") {
    Document document{"Part"};
    const MaterialId id = createOrFail(document, "SteelA");

    SECTION("nothing known: steady conduction names the conductivity") {
        const Result<ThermalConductivity> k = features::requireThermalConductivity(document, id);
        REQUIRE_FALSE(k);
        REQUIRE(k.error().code == ErrorCode::FailedPrecondition);
        REQUIRE_THAT(k.error().message, ContainsSubstring("SteelA"));
        REQUIRE_THAT(k.error().message, ContainsSubstring("object:"));
        REQUIRE_THAT(k.error().message, ContainsSubstring("no thermal conductivity"));
    }

    SECTION("nothing known: transient conduction names all three at once") {
        const Result<features::TransientConductionProperties> transient =
            features::requireTransientConductionProperties(document, id);
        REQUIRE_FALSE(transient);
        REQUIRE_THAT(transient.error().message, ContainsSubstring("no density"));
        REQUIRE_THAT(transient.error().message, ContainsSubstring("no specific heat capacity"));
        REQUIRE_THAT(transient.error().message, ContainsSubstring("no thermal conductivity"));
    }

    SECTION("only the conductivity known: steady conduction is satisfied, transient is not") {
        materials::ThermalProperties partial;
        partial.thermalConductivity =
            materials::MaterialProperty<ThermalConductivity>::known(50_W_per_m_K);
        REQUIRE(features::setMaterialThermal(document, id, partial));

        // Steady conduction needs only k, so a missing density must not fail it.
        const Result<ThermalConductivity> k = features::requireThermalConductivity(document, id);
        REQUIRE(k);
        REQUIRE(*k == 50_W_per_m_K);

        const Result<features::TransientConductionProperties> transient =
            features::requireTransientConductionProperties(document, id);
        REQUIRE_FALSE(transient);
        REQUIRE_THAT(transient.error().message, ContainsSubstring("no density"));
        REQUIRE_THAT(transient.error().message, ContainsSubstring("no specific heat capacity"));
        REQUIRE_THAT(transient.error().message, !ContainsSubstring("no thermal conductivity"));
    }

    SECTION("everything known: the set is complete and typed") {
        REQUIRE(features::setMaterialMechanical(document, id, steelMechanical()));
        REQUIRE(features::setMaterialThermal(document, id, steelThermal()));

        const Result<features::TransientConductionProperties> transient =
            features::requireTransientConductionProperties(document, id);
        REQUIRE(transient);
        REQUIRE_THAT(transient->density.si(), WithinRel(7850.0, 1e-12));
        REQUIRE(transient->specificHeatCapacity == 500_J_per_kg_K);
        REQUIRE(transient->thermalConductivity == 50_W_per_m_K);
    }
}

TEST_CASE("Material_RequiresThermalExpansionSeparatelyAndAcceptsANegativeOne") {
    Document document{"Part"};
    const MaterialId id = createOrFail(document, "SteelA");

    const Result<ThermalExpansionCoefficient> missing =
        features::requireThermalExpansion(document, id);
    REQUIRE_FALSE(missing);
    REQUIRE_THAT(missing.error().message, ContainsSubstring("no thermal expansion"));

    // Conduction data does not satisfy an expansion consumer, and vice versa.
    materials::ThermalProperties conduction;
    conduction.thermalConductivity =
        materials::MaterialProperty<ThermalConductivity>::known(50_W_per_m_K);
    REQUIRE(features::setMaterialThermal(document, id, conduction));
    REQUIRE(features::requireThermalConductivity(document, id));
    REQUIRE_FALSE(features::requireThermalExpansion(document, id));

    // A negative coefficient is real data and must come through unchanged.
    materials::ThermalProperties contracting;
    contracting.thermalExpansion = materials::MaterialProperty<ThermalExpansionCoefficient>::known(
        ThermalExpansionCoefficient::fromSi(-2.5e-6));
    REQUIRE(features::setMaterialThermal(document, id, contracting));
    const Result<ThermalExpansionCoefficient> negative =
        features::requireThermalExpansion(document, id);
    REQUIRE(negative);
    REQUIRE_THAT(negative->si(), WithinRel(-2.5e-6, 1e-12));
}

TEST_CASE("Material_ThermalRequirementsOfAMissingMaterialFailWithoutInventingAnything") {
    Document document{"Part"};
    const MaterialId missing = MaterialId::fromValue(4471);
    REQUIRE_FALSE(features::requireThermalConductivity(document, missing));
    REQUIRE(features::requireThermalConductivity(document, missing).error().code
            == ErrorCode::NotFound);
    REQUIRE_FALSE(features::requireTransientConductionProperties(document, missing));
    REQUIRE_FALSE(features::requireThermalExpansion(document, missing));
    REQUIRE_FALSE(features::setMaterialThermal(document, missing, steelThermal()));
}

TEST_CASE("Material_ThermalPropertiesSurviveACloneAndARemoveAndReinsert") {
    Document document{"Part"};
    const MaterialId id = createOrFail(document, "SteelA", steelDefinition());
    REQUIRE(features::setMaterialMechanical(document, id, steelMechanical()));
    REQUIRE(features::setMaterialThermal(document, id, steelThermal()));

    const Document copy = document.clone();
    REQUIRE(features::findMaterial(copy, id)->definition().thermal == steelThermal());
    REQUIRE(features::findMaterial(copy, id)->definition().mechanical == steelMechanical());

    Result<std::unique_ptr<DocumentObject>> removed = document.removeObject(id);
    REQUIRE(removed);
    REQUIRE(document.insertObject(std::move(*removed)));
    REQUIRE(features::findMaterial(document, id)->definition().thermal == steelThermal());
    REQUIRE(features::findMaterial(document, id)->definition().mechanical == steelMechanical());
    // Including the reference temperature recorded on the conductivity.
    REQUIRE(features::findMaterial(document, id)
                ->definition()
                .thermal.thermalConductivity.referenceTemperature()
                .has_value());
}
