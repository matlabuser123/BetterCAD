#include "features/FeatureTestSupport.hpp"
#include "support/BracketModel.hpp"

#include <bettercad/assembly/Component.hpp>
#include <bettercad/assembly/Components.hpp>
#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/core/materials/ThermalProperties.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/features/Regenerator.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::ContainsSubstring;
using features::Material;
using features::MaterialAssignment;
using features::MaterialAssignmentState;
using features::MaterialDefinition;

namespace {

/// A material designated "Steel", with a body of properties, so that two of them
/// can be made indistinguishable except by identity.
MaterialDefinition steelLike(std::string notes = {}) {
    MaterialDefinition definition;
    definition.designation = "Steel";
    definition.standard = "EN 10025-2";
    definition.family = "Carbon Steel";
    definition.notes = std::move(notes);
    definition.mechanical.density =
        materials::MaterialProperty<Density>::known(Density::fromSi(7850.0));
    definition.mechanical.youngsModulus =
        materials::MaterialProperty<ElasticModulus>::known(210_GPa);
    definition.thermal.thermalConductivity =
        materials::MaterialProperty<ThermalConductivity>::known(50_W_per_m_K);
    return definition;
}

MaterialId createOrFail(Document& document, std::string name, const MaterialDefinition& definition) {
    const Result<MaterialId> id = features::createMaterial(document, std::move(name), definition);
    REQUIRE(id);
    return *id;
}

} // namespace

// --- the four states --------------------------------------------------------

TEST_CASE("MaterialAssignment_ADocumentStartsUnassignedAndThatIsNotAFault") {
    Document document{"Part"};

    const MaterialAssignment assignment = features::materialAssignment(document);
    REQUIRE(assignment.state == MaterialAssignmentState::Unassigned);
    REQUIRE_FALSE(assignment.resolved());
    REQUIRE_FALSE(assignment.material.has_value());
    REQUIRE(assignment.diagnostic.empty());
    REQUIRE(features::effectiveMaterial(document) == nullptr);
    REQUIRE(features::toString(MaterialAssignmentState::Unassigned) == "unassigned");

    // No default material appears from anywhere: no generic steel, no air, no
    // vacuum. Asking for one is a diagnostic naming the part.
    const Result<const Material*> required = features::requireEffectiveMaterial(document);
    REQUIRE_FALSE(required);
    REQUIRE(required.error().code == ErrorCode::FailedPrecondition);
    REQUIRE_THAT(required.error().message, ContainsSubstring("Part"));
    REQUIRE_THAT(required.error().message, ContainsSubstring("no material assigned"));
}

TEST_CASE("MaterialAssignment_UnassignedAndUnresolvedAreDifferentStates") {
    Document document{"Part"};
    const MaterialId steel = createOrFail(document, "Steel", steelLike());

    const MaterialAssignment unassigned = features::materialAssignment(document);
    REQUIRE(features::assignMaterial(document, steel));
    REQUIRE(features::removeMaterial(document, steel));
    const MaterialAssignment unresolved = features::materialAssignment(document);

    // Both give nullptr from effectiveMaterial(), and they are NOT the same
    // thing: one was never chosen, the other was chosen and is gone.
    REQUIRE(features::effectiveMaterial(document) == nullptr);
    REQUIRE(unassigned.state == MaterialAssignmentState::Unassigned);
    REQUIRE(unresolved.state == MaterialAssignmentState::Unresolved);
    REQUIRE(unassigned != unresolved);
    REQUIRE_FALSE(unassigned.material.has_value());
    REQUIRE(unresolved.material == steel);

    // And the diagnostics differ, because the user has to do different things.
    const std::string unresolvedMessage =
        features::requireEffectiveMaterial(document).error().message;
    REQUIRE_THAT(unresolvedMessage, ContainsSubstring("no such material"));
    REQUIRE_THAT(unresolvedMessage, !ContainsSubstring("no material assigned"));
}

TEST_CASE("MaterialAssignment_AnAssignmentNamingSomethingThatIsNotAMaterialIsInvalid") {
    Document document{"Part"};
    const Result<ParameterId> width = document.createParameter("width", 10_mm, units::mm);
    REQUIRE(width);

    // features::assignMaterial refuses to create this state...
    const Result<bool> refused =
        features::assignMaterial(document, MaterialId::fromValue(width->value()));
    REQUIRE_FALSE(refused);
    REQUIRE(refused.error().code == ErrorCode::NotFound);
    REQUIRE(features::materialAssignment(document).state == MaterialAssignmentState::Unassigned);

    // ...so it is reachable only through the document's own unvalidated setter,
    // which is what a loader and an undo use. It is still reported honestly
    // rather than mistaken for a missing material.
    REQUIRE(document.setMaterialAssignment(MaterialId::fromValue(width->value())));
    const MaterialAssignment assignment = features::materialAssignment(document);
    REQUIRE(assignment.state == MaterialAssignmentState::Invalid);
    REQUIRE(assignment.material == MaterialId::fromValue(width->value()));
    REQUIRE_THAT(assignment.diagnostic, ContainsSubstring("parameter"));
    REQUIRE(features::effectiveMaterial(document) == nullptr);
    REQUIRE_FALSE(features::requireEffectiveMaterial(document));
    REQUIRE(features::toString(MaterialAssignmentState::Invalid) == "invalid");
}

// --- assign, replace, remove ------------------------------------------------

TEST_CASE("MaterialAssignment_AssignsReplacesAndRemovesExplicitly") {
    Document document{"Part"};
    const MaterialId a = createOrFail(document, "SteelA", steelLike("supplier A"));
    const MaterialId b = createOrFail(document, "SteelB", steelLike("supplier B"));

    // Assign.
    const Result<bool> assigned = features::assignMaterial(document, a);
    REQUIRE(assigned);
    REQUIRE(*assigned);
    REQUIRE(features::materialAssignment(document).state == MaterialAssignmentState::Resolved);
    REQUIRE(features::materialAssignment(document).material == a);
    REQUIRE(features::effectiveMaterial(document)->materialId() == a);

    // Assigning the same material again changes nothing.
    const Result<bool> again = features::assignMaterial(document, a);
    REQUIRE(again);
    REQUIRE_FALSE(*again);

    // Replace, explicitly. A and B share a designation; identity decides.
    REQUIRE(features::findMaterial(document, a)->definition().designation
            == features::findMaterial(document, b)->definition().designation);
    const Result<bool> replaced = features::assignMaterial(document, b);
    REQUIRE(replaced);
    REQUIRE(*replaced);
    REQUIRE(features::effectiveMaterial(document)->materialId() == b);
    REQUIRE(features::effectiveMaterial(document)->definition().notes == "supplier B");

    // Remove.
    const Result<bool> removed = features::removeMaterialAssignment(document);
    REQUIRE(removed);
    REQUIRE(*removed);
    REQUIRE(features::materialAssignment(document).state == MaterialAssignmentState::Unassigned);
    // Removing again changes nothing and is not an error.
    const Result<bool> noop = features::removeMaterialAssignment(document);
    REQUIRE(noop);
    REQUIRE_FALSE(*noop);
    // And both materials are still there: removing an assignment is not deleting
    // a material.
    REQUIRE(features::materialCount(document) == 2);
}

TEST_CASE("MaterialAssignment_AssigningNeverAllocatesAnIdAndIsExactlyReversible") {
    Document document{"Part"};
    const MaterialId a = createOrFail(document, "SteelA", steelLike());
    const MaterialId b = createOrFail(document, "SteelB", steelLike());
    const std::uint64_t allocatedBefore = document.lastAllocatedId();

    // The shapes a command will take: assign / remove / redo, and A -> B -> A.
    REQUIRE(features::assignMaterial(document, a));
    REQUIRE(features::removeMaterialAssignment(document));
    REQUIRE(features::materialAssignment(document).state == MaterialAssignmentState::Unassigned);
    REQUIRE(features::assignMaterial(document, a));
    REQUIRE(features::effectiveMaterial(document)->materialId() == a);

    REQUIRE(features::assignMaterial(document, b));
    REQUIRE(features::assignMaterial(document, a));
    // Exactly the same identity, not an equivalent one.
    REQUIRE(features::materialAssignment(document).material == a);
    REQUIRE(features::effectiveMaterial(document)->materialId() == a);

    // An assignment is a reference, so none of that consumed an ID. If it had,
    // repeated assign/undo cycles would drift the ID space.
    REQUIRE(document.lastAllocatedId() == allocatedBefore);
}

TEST_CASE("MaterialAssignment_CanonicalStateIsEnoughForACommandToUndo") {
    // P15-CMD-001 implements commands. What has to be true now is that the
    // canonical before/after state is a single optional MaterialId -- so a
    // command stores intent, never a derived effective material.
    Document document{"Part"};
    const MaterialId a = createOrFail(document, "SteelA", steelLike());
    const MaterialId b = createOrFail(document, "SteelB", steelLike());

    const std::optional<MaterialId> state0 = document.materialAssignment();
    REQUIRE_FALSE(state0.has_value());
    REQUIRE(features::assignMaterial(document, a));
    const std::optional<MaterialId> state1 = document.materialAssignment();
    REQUIRE(features::assignMaterial(document, b));
    const std::optional<MaterialId> state2 = document.materialAssignment();

    // Restoring a captured state is one call and restores exactly.
    REQUIRE(document.setMaterialAssignment(state1));
    REQUIRE(features::effectiveMaterial(document)->materialId() == a);
    REQUIRE(document.setMaterialAssignment(state0));
    REQUIRE(features::materialAssignment(document).state == MaterialAssignmentState::Unassigned);
    REQUIRE(document.setMaterialAssignment(state2));
    REQUIRE(features::effectiveMaterial(document)->materialId() == b);
}

TEST_CASE("MaterialAssignment_AFailedMutationLeavesThePreviousAssignmentExactlyAsItWas") {
    Document document{"Part"};
    const MaterialId a = createOrFail(document, "SteelA", steelLike());
    REQUIRE(features::assignMaterial(document, a));

    // An ID that names nothing, an ID that names a parameter, and the invalid
    // ID: each must fail and leave A in place.
    const Result<ParameterId> width = document.createParameter("width", 10_mm, units::mm);
    REQUIRE(width);
    for (const MaterialId bad : {MaterialId::fromValue(4471), MaterialId{},
                                 MaterialId::fromValue(width->value())}) {
        INFO("assigning " << bad.value());
        const Result<bool> failed = features::assignMaterial(document, bad);
        REQUIRE_FALSE(failed);
        REQUIRE(features::materialAssignment(document).material == a);
        REQUIRE(features::effectiveMaterial(document)->materialId() == a);
    }
}

// --- the mandatory no-rebinding fixtures ------------------------------------

TEST_CASE("MaterialAssignment_ADeletedMaterialNeverRebindsToAnotherOfTheSameName") {
    Document document{"Part"};
    const MaterialId a = createOrFail(document, "SteelA", steelLike("A"));
    const MaterialId b = createOrFail(document, "SteelB", steelLike("B"));
    REQUIRE(features::findMaterial(document, a)->definition().designation == "Steel");
    REQUIRE(features::findMaterial(document, b)->definition().designation == "Steel");

    REQUIRE(features::assignMaterial(document, a));
    REQUIRE(features::removeMaterial(document, a));

    // Unresolved A, and NOT B -- even though B carries the same designation and
    // is the only "Steel" left.
    const MaterialAssignment afterDelete = features::materialAssignment(document);
    REQUIRE(afterDelete.state == MaterialAssignmentState::Unresolved);
    REQUIRE(afterDelete.material == a);
    REQUIRE(afterDelete.material != b);
    REQUIRE(features::effectiveMaterial(document) == nullptr);

    // Adding a third of the same designation changes nothing.
    const MaterialId c = createOrFail(document, "SteelC", steelLike("C"));
    const MaterialAssignment afterAdd = features::materialAssignment(document);
    REQUIRE(afterAdd.state == MaterialAssignmentState::Unresolved);
    REQUIRE(afterAdd.material == a);
    REQUIRE(afterAdd.material != c);
    REQUIRE(features::effectiveMaterial(document) == nullptr);
}

TEST_CASE("MaterialAssignment_AnIdenticalReplacementDoesNotAdoptAStaleAssignment") {
    // Identity, not similarity, controls resolution. A and B are the same in
    // every field a user can see.
    Document document{"Part"};
    const MaterialDefinition same = steelLike("identical content");
    const MaterialId a = createOrFail(document, "SteelA", same);
    const MaterialId b = createOrFail(document, "SteelB", same);
    REQUIRE(features::findMaterial(document, a)->definition()
            == features::findMaterial(document, b)->definition());

    REQUIRE(features::assignMaterial(document, a));
    REQUIRE(features::removeMaterial(document, a));
    REQUIRE(features::materialAssignment(document).state == MaterialAssignmentState::Unresolved);

    // A third, also identical, and still unresolved.
    const MaterialId c = createOrFail(document, "SteelC", same);
    REQUIRE(features::materialAssignment(document).state == MaterialAssignmentState::Unresolved);
    REQUIRE(features::materialAssignment(document).material == a);
    REQUIRE(features::effectiveMaterial(document) == nullptr);
    REQUIRE(c != a);

    // Recovery is explicit: the user chooses. Nothing chose for them.
    REQUIRE(features::assignMaterial(document, c));
    REQUIRE(features::effectiveMaterial(document)->materialId() == c);
}

TEST_CASE("MaterialAssignment_ResolutionDoesNotDependOnPositionAmongTheMaterials") {
    Document document{"Part"};
    const MaterialId a = createOrFail(document, "SteelA", steelLike("A"));
    const MaterialId b = createOrFail(document, "SteelB", steelLike("B"));
    const MaterialId c = createOrFail(document, "SteelC", steelLike("C"));
    REQUIRE(features::materialIds(document) == std::vector<MaterialId>{a, b, c});

    REQUIRE(features::assignMaterial(document, b));
    REQUIRE(features::effectiveMaterial(document)->materialId() == b);

    // B was second. Remove the one before it, so it becomes first...
    REQUIRE(features::removeMaterial(document, a));
    REQUIRE(features::materialIds(document) == std::vector<MaterialId>{b, c});
    REQUIRE(features::effectiveMaterial(document)->materialId() == b);
    REQUIRE(features::effectiveMaterial(document)->definition().notes == "B");

    // ...and add two more, so it is neither first nor last.
    const MaterialId d = createOrFail(document, "SteelD", steelLike("D"));
    const MaterialId e = createOrFail(document, "SteelE", steelLike("E"));
    REQUIRE(features::materialIds(document) == std::vector<MaterialId>{b, c, d, e});
    REQUIRE(features::effectiveMaterial(document)->materialId() == b);
    REQUIRE(features::effectiveMaterial(document)->definition().notes == "B");
}

TEST_CASE("MaterialAssignment_ADuplicateIdIsRejectedAndLeavesTheAssignmentAlone") {
    Document document{"Part"};
    const MaterialId a = createOrFail(document, "SteelA", steelLike("A"));
    REQUIRE(features::assignMaterial(document, a));

    // The shape a loader takes: an object arriving under an ID already in use.
    Result<std::unique_ptr<Material>> duplicate = Material::create("SteelB", steelLike("B"));
    REQUIRE(duplicate);
    const Result<void> restored = document.restoreObject(a, std::move(*duplicate));
    REQUIRE_FALSE(restored);
    REQUIRE(restored.error().code == ErrorCode::AlreadyExists);

    // Nothing was overwritten, and the assignment still names the original.
    REQUIRE(features::effectiveMaterial(document)->materialId() == a);
    REQUIRE(features::effectiveMaterial(document)->definition().notes == "A");
    REQUIRE(features::materialCount(document) == 1);
}

// --- metadata and property edits do not move an assignment -----------------

TEST_CASE("MaterialAssignment_SurvivesARenameAndShowsTheCurrentName") {
    Document document{"Part"};
    const MaterialId a = createOrFail(document, "Steel", steelLike());
    REQUIRE(features::assignMaterial(document, a));

    REQUIRE(document.rename(a, "StructuralSteel"));
    MaterialDefinition redesignated = features::findMaterial(document, a)->definition();
    redesignated.designation = "Structural Steel";
    REQUIRE(features::setMaterialDefinition(document, a, redesignated));

    // The assignment did not need touching: it holds an ID, not a name.
    REQUIRE(features::materialAssignment(document).material == a);
    REQUIRE(features::materialAssignment(document).state == MaterialAssignmentState::Resolved);
    // And a display query reads the CURRENT name through the ID, never a cached
    // copy in the assignment.
    REQUIRE(features::effectiveMaterial(document)->name() == "StructuralSteel");
    REQUIRE(features::effectiveMaterial(document)->definition().designation == "Structural Steel");
}

TEST_CASE("MaterialAssignment_SurvivesAPropertyEditAndConsumersSeeTheNewValues") {
    Document document{"Part"};
    const MaterialId a = createOrFail(document, "Steel", steelLike());
    REQUIRE(features::assignMaterial(document, a));
    REQUIRE(*features::effectiveMaterial(document)->definition().mechanical.youngsModulus.value()
            == 210_GPa);

    materials::MechanicalProperties mechanical =
        features::findMaterial(document, a)->definition().mechanical;
    mechanical.youngsModulus = materials::MaterialProperty<ElasticModulus>::known(200_GPa);
    mechanical.yieldStrength = materials::MaterialProperty<Stress>::known(250_MPa);
    REQUIRE(features::setMaterialMechanical(document, a, mechanical));
    materials::ThermalProperties thermal =
        features::findMaterial(document, a)->definition().thermal;
    thermal.specificHeatCapacity =
        materials::MaterialProperty<SpecificHeatCapacity>::known(500_J_per_kg_K);
    REQUIRE(features::setMaterialThermal(document, a, thermal));

    // Same identity; current values. No property value was ever copied into the
    // assignment, so there is nothing to go stale.
    REQUIRE(features::materialAssignment(document).material == a);
    const Material* resolved = features::effectiveMaterial(document);
    REQUIRE(resolved->materialId() == a);
    REQUIRE(*resolved->definition().mechanical.youngsModulus.value() == 200_GPa);
    REQUIRE(*resolved->definition().mechanical.yieldStrength.value() == 250_MPa);
    REQUIRE(*resolved->definition().thermal.specificHeatCapacity.value() == 500_J_per_kg_K);
}

TEST_CASE("MaterialAssignment_ResolvesAgainIfTheSameMaterialComesBack") {
    // Recovery, on P15-MAT-001's terms: an ID is never reused, so the ONLY way a
    // material comes back is the same object being reinserted -- which is what an
    // undo of a delete does. No ID is forged to make this pass.
    Document document{"Part"};
    const MaterialId a = createOrFail(document, "Steel", steelLike());
    REQUIRE(features::assignMaterial(document, a));

    Result<std::unique_ptr<DocumentObject>> removed = document.removeObject(a);
    REQUIRE(removed);
    REQUIRE(features::materialAssignment(document).state == MaterialAssignmentState::Unresolved);

    REQUIRE(document.insertObject(std::move(*removed)));
    REQUIRE(features::materialAssignment(document).state == MaterialAssignmentState::Resolved);
    REQUIRE(features::effectiveMaterial(document)->materialId() == a);
    // The assignment was never rewritten; keeping the intent is what made this
    // possible.
    REQUIRE(features::materialAssignment(document).material == a);
}

// --- one material per document, and the library --------------------------

TEST_CASE("MaterialAssignment_ADocumentHasExactlyOneMaterialAssignment") {
    // One optional MaterialId, so two materials cannot be assigned at once:
    // assigning B replaces A rather than adding to it. There is no map that
    // could grow a second entry (ADR-026).
    Document document{"Part"};
    const MaterialId a = createOrFail(document, "SteelA", steelLike("A"));
    const MaterialId b = createOrFail(document, "SteelB", steelLike("B"));

    REQUIRE(features::assignMaterial(document, a));
    REQUIRE(features::assignMaterial(document, b));
    REQUIRE(features::materialAssignment(document).material == b);
    REQUIRE(features::effectiveMaterial(document)->materialId() == b);
    // A is still a material in the document; it is simply not the one assigned.
    REQUIRE(features::findMaterial(document, a) != nullptr);
}

TEST_CASE("MaterialAssignment_AssignsAnImportedLibraryMaterialThroughItsDocumentCopy") {
    // The qualified workflow (ADR-025): a library entry is imported, which makes
    // a document-owned material, and the assignment names THAT. There is no way
    // to assign a library entry directly -- it has no ObjectId to name.
    Document document{"Part"};
    const Result<materials::LibraryMaterial> entry =
        materials::findLibraryMaterial(materials::builtInLibraryName(), "al-6061-t6");
    REQUIRE(entry);
    const Result<MaterialId> imported =
        features::importLibraryMaterial(document, "Al6061T6", *entry);
    REQUIRE(imported);

    REQUIRE(features::assignMaterial(document, *imported));
    const Material* resolved = features::effectiveMaterial(document);
    REQUIRE(resolved->materialId() == *imported);
    REQUIRE(resolved->definition().origin == entry->key());

    // Editing the document's copy does not move the assignment, and the library
    // entry is untouched -- so no library change can alter what this part is
    // made of.
    MaterialDefinition edited = resolved->definition();
    edited.designation = "6061, supplier measured";
    REQUIRE(features::setMaterialDefinition(document, *imported, edited));
    REQUIRE(features::materialAssignment(document).material == *imported);
    REQUIRE(features::effectiveMaterial(document)->definition().designation
            == "6061, supplier measured");
    const Result<materials::LibraryMaterial> again =
        materials::findLibraryMaterial(materials::builtInLibraryName(), "al-6061-t6");
    REQUIRE(again);
    REQUIRE(*again == *entry);
}

TEST_CASE("MaterialAssignment_ALibraryOriginAndALocalMaterialOfTheSameNameStayDistinct") {
    Document document{"Part"};
    const Result<materials::LibraryMaterial> entry =
        materials::findLibraryMaterial(materials::builtInLibraryName(), "al-6061-t6");
    REQUIRE(entry);
    const Result<MaterialId> imported =
        features::importLibraryMaterial(document, "Al6061T6", *entry);
    REQUIRE(imported);

    // A hand-made material carrying the SAME designation as the imported one.
    MaterialDefinition handMade;
    handMade.designation = std::string{entry->designation()};
    const MaterialId local = createOrFail(document, "Al6061T6Local", handMade);
    REQUIRE(features::findMaterialsByDesignation(document, entry->designation()).size() == 2);

    // Assigning either one resolves to exactly it. There is no "library first"
    // or "local first" preference, because resolution is by ID alone.
    REQUIRE(features::assignMaterial(document, local));
    REQUIRE(features::effectiveMaterial(document)->materialId() == local);
    REQUIRE_FALSE(features::effectiveMaterial(document)->definition().origin.has_value());
    REQUIRE(features::assignMaterial(document, *imported));
    REQUIRE(features::effectiveMaterial(document)->materialId() == *imported);
    REQUIRE(features::effectiveMaterial(document)->definition().origin == entry->key());
}

// --- cross-document ---------------------------------------------------------

TEST_CASE("MaterialAssignment_AnAssignmentOnlyEverNamesAMaterialInItsOwnDocument") {
    // There is no cross-document material reference. A MaterialId is
    // document-local throughout BetterCAD, so an ID from elsewhere is not a
    // reference to elsewhere -- it either names this document's own material of
    // that number, or nothing. A real cross-document assignment needs an
    // ObjectReference carrying a DocumentId, which ADR-003 defers.
    Document first{"PartA"};
    Document second{"PartB"};
    const MaterialId inFirst = createOrFail(first, "SteelA", steelLike("in A"));

    // The second document has no material at all, so the same number names
    // nothing there and the assignment is refused.
    const Result<bool> refused = features::assignMaterial(second, inFirst);
    REQUIRE_FALSE(refused);
    REQUIRE(refused.error().code == ErrorCode::NotFound);
    REQUIRE_THAT(refused.error().message, ContainsSubstring("in this document"));
    REQUIRE(features::materialAssignment(second).state == MaterialAssignmentState::Unassigned);

    // And when the second document DOES have a material of that number, the
    // assignment is about its own, not the first document's.
    const MaterialId inSecond = createOrFail(second, "SteelB", steelLike("in B"));
    REQUIRE(inSecond.value() == inFirst.value());
    REQUIRE(features::assignMaterial(second, inSecond));
    REQUIRE(features::effectiveMaterial(second)->definition().notes == "in B");
    REQUIRE(features::effectiveMaterial(first) == nullptr);
}

// --- regeneration -----------------------------------------------------------

TEST_CASE("MaterialAssignment_SurvivesModelRegenerationAndGeometryChanges") {
    BracketModel model;
    features::Regenerator regenerator;
    requireReport(regenerator, model.doc);

    const MaterialId steel = createOrFail(model.doc, "Steel", steelLike());
    REQUIRE(features::assignMaterial(model.doc, steel));
    const double volumeBefore = volumeMm3(regenerator, model.pad);

    // Change the model: a parameter edit that rebuilds features and changes the
    // volume.
    REQUIRE(model.doc.setParameterValue(model.width, 90_mm));
    const features::RegenerationReport report = requireReport(regenerator, model.doc);
    REQUIRE_FALSE(report.regenerated.empty());
    REQUIRE(report.failed.empty());
    REQUIRE(volumeMm3(regenerator, model.pad) != volumeBefore);

    // The assignment belongs to the document, not to any generated topology, so
    // regeneration cannot touch it.
    REQUIRE(features::materialAssignment(model.doc).state == MaterialAssignmentState::Resolved);
    REQUIRE(features::materialAssignment(model.doc).material == steel);
    REQUIRE(features::effectiveMaterial(model.doc)->materialId() == steel);
}

TEST_CASE("MaterialAssignment_SurvivesAFailedRegeneration") {
    BracketModel model;
    features::Regenerator regenerator;
    requireReport(regenerator, model.doc);

    const MaterialId steel = createOrFail(model.doc, "Steel", steelLike());
    REQUIRE(features::assignMaterial(model.doc, steel));

    // Break the model: a zero depth is not a solid.
    REQUIRE(model.doc.setParameterValue(model.depth, 0_mm));
    const features::RegenerationReport report = requireReport(regenerator, model.doc);
    REQUIRE_FALSE(report.failed.empty());

    // Canonical engineering intent is not erased by a temporary geometry
    // failure. A derived mass becomes unavailable; the assignment does not.
    REQUIRE(features::materialAssignment(model.doc).state == MaterialAssignmentState::Resolved);
    REQUIRE(features::effectiveMaterial(model.doc)->materialId() == steel);

    // And it is still there once the model builds again.
    REQUIRE(model.doc.setParameterValue(model.depth, 12_mm));
    const features::RegenerationReport recovered = requireReport(regenerator, model.doc);
    REQUIRE(recovered.failed.empty());
    REQUIRE(features::effectiveMaterial(model.doc)->materialId() == steel);
}

// --- assemblies, suppression, configurations -------------------------------

TEST_CASE("MaterialAssignment_TwoOccurrencesOfOnePartCannotDifferInMaterial") {
    // ADR-026: an occurrence cannot override a part's material in P15, because
    // the override would have to name a material across a document boundary and
    // external references are deferred (ADR-003). ComponentDefinition therefore
    // has no material field at all -- a compile-fail case proves the absence --
    // so there is no per-occurrence state to diverge.
    BracketModel model;
    features::Regenerator regenerator;
    requireReport(regenerator, model.doc);

    const MaterialId steel = createOrFail(model.doc, "Steel", steelLike());
    REQUIRE(features::assignMaterial(model.doc, steel));

    const ComponentId first = require(assembly::createComponent(
        model.doc, "Bolt1", {.part = ObjectReference{model.pad}}));
    const ComponentId second = require(assembly::createComponent(
        model.doc, "Bolt2", {.part = ObjectReference{model.pad}}));
    const ComponentId third = require(assembly::createComponent(
        model.doc, "Bolt3", {.part = ObjectReference{model.pad}}));
    REQUIRE(first != second);
    REQUIRE(second != third);

    // Every occurrence resolves through the one document-level assignment, so
    // they cannot disagree and adding occurrences changes nothing.
    REQUIRE(features::effectiveMaterial(model.doc)->materialId() == steel);
    REQUIRE(features::materialAssignment(model.doc).material == steel);
}

TEST_CASE("MaterialAssignment_SuppressingAComponentDoesNotRemoveTheMaterial") {
    BracketModel model;
    features::Regenerator regenerator;
    requireReport(regenerator, model.doc);
    const MaterialId steel = createOrFail(model.doc, "Steel", steelLike());
    REQUIRE(features::assignMaterial(model.doc, steel));

    const ComponentId bolt = require(assembly::createComponent(
        model.doc, "Bolt", {.part = ObjectReference{model.pad}}));

    // Suppression means "not in this build", not "material deleted".
    assembly::ComponentDefinition suppressed =
        assembly::findComponent(model.doc, bolt)->definition();
    suppressed.suppressed = true;
    REQUIRE(assembly::setComponentDefinition(model.doc, bolt, suppressed));
    REQUIRE(features::materialAssignment(model.doc).state == MaterialAssignmentState::Resolved);
    REQUIRE(features::effectiveMaterial(model.doc)->materialId() == steel);

    suppressed.suppressed = false;
    REQUIRE(assembly::setComponentDefinition(model.doc, bolt, suppressed));
    REQUIRE(features::effectiveMaterial(model.doc)->materialId() == steel);
}

TEST_CASE("MaterialAssignment_SwitchingConfigurationsNeverChangesWhichMaterialIsUsed") {
    // ADR-026, forced by audited semantics rather than chosen: a configuration
    // overrides free PARAMETER values, and a MaterialId is not a parameter
    // value, so the override mechanism cannot carry one.
    BracketModel model;
    features::Regenerator regenerator;
    requireReport(regenerator, model.doc);
    const MaterialId steel = createOrFail(model.doc, "Steel", steelLike());
    REQUIRE(features::assignMaterial(model.doc, steel));

    const ConfigurationId wide = require(model.doc.createConfiguration("Wide"));
    const ConfigurationId narrow = require(model.doc.createConfiguration("Narrow"));
    REQUIRE(model.doc.setConfigurationOverride(wide, model.width, 120_mm));
    REQUIRE(model.doc.setConfigurationOverride(narrow, model.width, 40_mm));

    // The same material under every configuration, and back again.
    for (const std::optional<ConfigurationId> active :
         {std::optional<ConfigurationId>{}, std::optional<ConfigurationId>{wide},
          std::optional<ConfigurationId>{narrow}, std::optional<ConfigurationId>{},
          std::optional<ConfigurationId>{wide}}) {
        REQUIRE(model.doc.setActiveConfiguration(active));
        requireReport(regenerator, model.doc);
        INFO("configuration " << (active ? std::to_string(active->value()) : std::string{"base"}));
        REQUIRE(features::materialAssignment(model.doc).state == MaterialAssignmentState::Resolved);
        REQUIRE(features::materialAssignment(model.doc).material == steel);
        REQUIRE(features::effectiveMaterial(model.doc)->materialId() == steel);
    }

    // The configuration really is in force -- the effective parameter value says
    // so -- and the assignment is still untouched.
    REQUIRE(model.doc.setActiveConfiguration(narrow));
    REQUIRE(model.doc.effectiveParameterValue(model.width)->siValue == 0.040);
    REQUIRE(features::materialAssignment(model.doc).material == steel);
    REQUIRE(model.doc.setActiveConfiguration(wide));
    REQUIRE(model.doc.effectiveParameterValue(model.width)->siValue == 0.120);
    REQUIRE(features::materialAssignment(model.doc).material == steel);

    // Nothing is asserted here about the part's VOLUME under a configuration.
    // ADR-026 reasons that "a configuration changes a part's dimensions, so it
    // changes volume and therefore mass -- through geometry, which is exactly
    // the existing derivation chain". That chain does not currently run: a
    // feature reading a FREE parameter that a configuration overrides is never
    // marked dirty, because the parameter's own revision does not change, so the
    // feature keeps its base-configuration geometry. Measured, and recorded as a
    // carried defect in TODO.md; it is a regeneration concern, not this
    // milestone's, and asserting the present behaviour here would pin a defect
    // as if it were the contract.
}

// --- determinism and the document copy ------------------------------------

TEST_CASE("MaterialAssignment_ResolvesToTheSameAnswerEveryTime") {
    Document document{"Part"};
    const MaterialId a = createOrFail(document, "SteelA", steelLike("A"));
    createOrFail(document, "SteelB", steelLike("B"));
    REQUIRE(features::assignMaterial(document, a));

    const MaterialAssignment first = features::materialAssignment(document);
    for (int pass = 0; pass < 20; ++pass) {
        REQUIRE(features::materialAssignment(document) == first);
        REQUIRE(features::effectiveMaterial(document)->materialId() == a);
    }

    // Unresolved is deterministic too, diagnostic included.
    REQUIRE(features::removeMaterial(document, a));
    const MaterialAssignment unresolved = features::materialAssignment(document);
    for (int pass = 0; pass < 20; ++pass) {
        REQUIRE(features::materialAssignment(document) == unresolved);
    }
}

TEST_CASE("MaterialAssignment_SurvivesADocumentClone") {
    Document document{"Part"};
    const MaterialId a = createOrFail(document, "Steel", steelLike());
    REQUIRE(features::assignMaterial(document, a));

    const Document copy = document.clone();
    REQUIRE(copy.materialAssignment() == a);
    REQUIRE(features::materialAssignment(copy).state == MaterialAssignmentState::Resolved);
    REQUIRE(features::effectiveMaterial(copy)->materialId() == a);

    // An unassigned document clones as unassigned.
    Document bare{"Bare"};
    REQUIRE(features::materialAssignment(bare.clone()).state
            == MaterialAssignmentState::Unassigned);
}

TEST_CASE("MaterialAssignment_BumpsTheDocumentRevisionOnlyWhenSomethingChanges") {
    Document document{"Part"};
    const MaterialId a = createOrFail(document, "SteelA", steelLike());
    const std::uint64_t before = document.revision();

    REQUIRE(features::assignMaterial(document, a));
    REQUIRE(document.revision() > before);
    const std::uint64_t assigned = document.revision();

    // The same assignment again is not a change.
    REQUIRE(features::assignMaterial(document, a));
    REQUIRE(document.revision() == assigned);
    // Nor is removing when there is nothing to remove.
    REQUIRE(features::removeMaterialAssignment(document));
    const std::uint64_t removed = document.revision();
    REQUIRE(features::removeMaterialAssignment(document));
    REQUIRE(document.revision() == removed);
}

TEST_CASE("MaterialAssignment_WalksTheWholeStateTableInOrder") {
    // The qualification state table, as one sequence, so the evidence has a
    // single test behind it rather than a claim assembled from several.
    Document document{"Part"};
    const MaterialId a = createOrFail(document, "SteelA", steelLike("A"));

    const auto state = [&] { return features::materialAssignment(document).state; };
    const auto named = [&] { return features::materialAssignment(document).material; };

    // No assignment -> Unassigned
    REQUIRE(state() == MaterialAssignmentState::Unassigned);
    REQUIRE_FALSE(named().has_value());

    // Assign A -> Resolved A
    REQUIRE(features::assignMaterial(document, a));
    REQUIRE(state() == MaterialAssignmentState::Resolved);
    REQUIRE(named() == a);

    // Rename A -> Resolved A
    REQUIRE(document.rename(a, "StructuralSteel"));
    REQUIRE(state() == MaterialAssignmentState::Resolved);
    REQUIRE(named() == a);

    // Delete A -> Unresolved A
    REQUIRE(features::removeMaterial(document, a));
    REQUIRE(state() == MaterialAssignmentState::Unresolved);
    REQUIRE(named() == a);

    // B of the same designation exists -> still Unresolved A
    const MaterialId b = createOrFail(document, "SteelB", steelLike("B"));
    REQUIRE(state() == MaterialAssignmentState::Unresolved);
    REQUIRE(named() == a);
    REQUIRE(named() != b);

    // C with identical content added -> still Unresolved A
    const MaterialId c = createOrFail(document, "SteelC", steelLike("A"));
    // C carries exactly the content A had -- designation, standard, family,
    // notes, density, modulus, conductivity. Indistinguishable except by ID.
    REQUIRE(features::findMaterial(document, c)->definition() == steelLike("A"));
    REQUIRE(state() == MaterialAssignmentState::Unresolved);
    REQUIRE(named() == a);
    REQUIRE(named() != c);

    // Replace with B explicitly -> Resolved B
    REQUIRE(features::assignMaterial(document, b));
    REQUIRE(state() == MaterialAssignmentState::Resolved);
    REQUIRE(named() == b);

    // Remove assignment -> Unassigned
    REQUIRE(features::removeMaterialAssignment(document));
    REQUIRE(state() == MaterialAssignmentState::Unassigned);
    REQUIRE_FALSE(named().has_value());
}
