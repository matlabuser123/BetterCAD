#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/materials/MaterialLibrary.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using Catch::Matchers::ContainsSubstring;
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
