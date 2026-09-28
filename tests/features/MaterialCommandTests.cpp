#include "features/FeatureTestSupport.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Command.hpp>
#include <bettercad/core/document/Commands.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/materials/Completeness.hpp>
#include <bettercad/core/materials/MaterialLibrary.hpp>
#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/core/materials/Provenance.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/MassProperties.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/MaterialCommands.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinRel;
using features::AssignMaterialCommand;
using features::CreateMaterialCommand;
using features::DeleteMaterialCommand;
using features::EditMaterialCommand;
using features::MaterialAssignmentState;
using features::MaterialDefinition;
using features::RemoveMaterialAssignmentCommand;
using materials::MaterialProperty;
using materials::MechanicalPropertyKind;
using materials::PropertyProvenance;
using materials::SourceKind;
using materials::ThermalPropertyKind;

// P15-CMD-001: undoable material edits.
//
// THE CENTRAL CLAIM THESE TESTS EXIST TO CHECK: the command history holds canonical
// engineering intent and nothing derived. So undoing a density edit gives the old
// mass back -- not because the old mass was stored, but because the restored density
// is multiplied by the geometry again. The same for the derived shear modulus and for
// a completeness report. Three integration tests prove the recomputation, and a
// fourth walks the command objects' own state to show the derived values are not in
// them.
//
// IDENTITY IS INTENT, NOT AN IMPLEMENTATION DETAIL. An assignment names a material by
// MaterialId (ADR-025), so a redo that minted a new ID would leave every reference
// dangling. Every command test that creates or deletes checks the ID survives.

namespace {

/// A material with metadata, both property halves and mixed provenance, so an undo
/// has something exact to restore rather than a near-empty struct.
MaterialDefinition steel() {
    MaterialDefinition definition;
    definition.designation = "Steel";
    definition.standard = "EN 10025-2";
    definition.family = "Carbon Steel";
    definition.notes = "Synthetic fixture.";
    definition.mechanical.density = MaterialProperty<Density>::known(Density::fromSi(7850.0));
    definition.mechanical.youngsModulus = MaterialProperty<ElasticModulus>::known(200_GPa);
    definition.mechanical.poissonRatio =
        MaterialProperty<PoissonRatio>::known(PoissonRatio::of(0.30));
    definition.mechanical.yieldStrength = MaterialProperty<Stress>::known(235_MPa);
    // Thermal conductivity deliberately UNKNOWN, so a Known <-> Unknown round trip
    // has something to exercise.
    PropertyProvenance measured;
    measured.kind = SourceKind::Measured;
    measured.source = "Synthetic test report";
    measured.revision = "Rev A";
    measured.date = materials::Date::of(2024, 3, 17);
    definition.provenance.mechanical[MechanicalPropertyKind::Density] = measured;
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

/// Executes @p command through the history, requiring success, and hands back the
/// raw pointer so a test can read the command's own state afterwards.
template <typename CommandType, typename... Args>
CommandType* run(CommandHistory& history, Document& document, Args&&... args) {
    auto command = std::make_unique<CommandType>(std::forward<Args>(args)...);
    CommandType* raw = command.get();
    const Result<void> result = history.execute(document, std::move(command));
    INFO((result ? std::string{} : result.error().message));
    REQUIRE(result.has_value());
    return raw;
}

void requireUndo(CommandHistory& history, Document& document) {
    const Result<void> result = history.undo(document);
    INFO((result ? std::string{} : result.error().message));
    REQUIRE(result.has_value());
}

void requireRedo(CommandHistory& history, Document& document) {
    const Result<void> result = history.redo(document);
    INFO((result ? std::string{} : result.error().message));
    REQUIRE(result.has_value());
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

    [[nodiscard]] double massKg() {
        const Result<features::PartMassProperties> mass =
            features::partMassProperties(document, regenerator, feature);
        REQUIRE(mass);
        return mass->mass.in(units::kg);
    }
};

} // namespace

// --- create -----------------------------------------------------------------

TEST_CASE("MaterialCommand_CreateExecutesUndoesAndRedoesWithTheSameIdentity",
          "[features][materialcmd]") {
    // S0: absent. S1: present, id = N. S2: absent. S3: present, id = N.
    Document document{"Part"};
    CommandHistory history;
    CHECK(features::materialCount(document) == 0);

    CreateMaterialCommand* create = run<CreateMaterialCommand>(history, document, "Steel", steel());
    const MaterialId id = create->materialId();
    CHECK(id.isValid());
    CHECK(features::materialCount(document) == 1);
    const MaterialDefinition s1 = definitionOf(document, id);
    CHECK(s1 == steel());
    CHECK_THAT(create->description(), ContainsSubstring("Steel"));

    requireUndo(history, document);
    CHECK(features::materialCount(document) == 0);
    CHECK(features::findMaterial(document, id) == nullptr);

    requireRedo(history, document);
    CHECK(features::materialCount(document) == 1);
    // THE IDENTITY GATE: the same MaterialId, not a new one.
    REQUIRE(features::findMaterial(document, id) != nullptr);
    CHECK(features::findMaterial(document, id)->materialId() == id);
    CHECK(create->materialId() == id);
    // S1 == S3, every field including provenance.
    CHECK(definitionOf(document, id) == s1);
    CHECK(definitionOf(document, id).provenance.mechanical.at(MechanicalPropertyKind::Density)
              .date == materials::Date::of(2024, 3, 17));
    CHECK(features::findMaterial(document, id)->name() == "Steel");
}

TEST_CASE("MaterialCommand_CreateSurvivesRepeatedUndoRedoWithoutDrift",
          "[features][materialcmd]") {
    Document document{"Part"};
    CommandHistory history;
    CreateMaterialCommand* create = run<CreateMaterialCommand>(history, document, "Steel", steel());
    const MaterialId id = create->materialId();
    const MaterialDefinition expected = definitionOf(document, id);

    for (int pass = 0; pass < 5; ++pass) {
        requireUndo(history, document);
        CHECK(features::findMaterial(document, id) == nullptr);
        requireRedo(history, document);
        REQUIRE(features::findMaterial(document, id) != nullptr);
        CHECK(features::findMaterial(document, id)->materialId() == id);
        CHECK(definitionOf(document, id) == expected);
    }
    CHECK(features::materialCount(document) == 1);
}

TEST_CASE("MaterialCommand_AFailedCreateChangesNothingAndDoesNotEnterHistory",
          "[features][materialcmd]") {
    Document document{"Part"};
    CommandHistory history;
    const MaterialId existing = createOrFail(document, "Steel", steel());
    run<CreateMaterialCommand>(history, document, "Aluminium", MaterialDefinition{});
    const std::size_t depth = history.undoCount();
    const std::size_t count = features::materialCount(document);
    const std::uint64_t revision = document.revision();

    // A duplicate name: names are unique across a document (P15-CUSTOM-001).
    auto duplicate = std::make_unique<CreateMaterialCommand>("Steel", steel());
    const Result<void> refusedName = history.execute(document, std::move(duplicate));
    REQUIRE_FALSE(refusedName);
    CHECK(refusedName.error().code == ErrorCode::AlreadyExists);

    // An invalid property value: a zero density is not engineering data.
    MaterialDefinition invalid = steel();
    invalid.mechanical.density = MaterialProperty<Density>::known(Density::fromSi(0.0));
    auto bad = std::make_unique<CreateMaterialCommand>("Bad", invalid);
    const Result<void> refusedValue = history.execute(document, std::move(bad));
    REQUIRE_FALSE(refusedValue);

    // An invalid name.
    auto badName = std::make_unique<CreateMaterialCommand>("9 not an identifier", steel());
    CHECK_FALSE(history.execute(document, std::move(badName)).has_value());

    // NOTHING moved: not the document, not the count, not the revision, not the
    // history depth.
    CHECK(features::materialCount(document) == count);
    CHECK(document.revision() == revision);
    CHECK(history.undoCount() == depth);
    CHECK(definitionOf(document, existing) == steel());

    // And undo still targets the last SUCCESSFUL command, not a failed attempt.
    CHECK(history.undoDescription().has_value());
    CHECK_THAT(*history.undoDescription(), ContainsSubstring("Aluminium"));
    requireUndo(history, document);
    CHECK(features::materialCount(document) == 1);
    CHECK(features::findMaterial(document, existing) != nullptr);
}

TEST_CASE("MaterialCommand_AFailedCreateDoesNotDisturbTheRedoStack",
          "[features][materialcmd]") {
    // A failed command must not clear redo: nothing happened, so nothing that was
    // redoable should stop being redoable.
    Document document{"Part"};
    CommandHistory history;
    CreateMaterialCommand* create = run<CreateMaterialCommand>(history, document, "Steel", steel());
    const MaterialId id = create->materialId();
    requireUndo(history, document);
    REQUIRE(history.canRedo());

    auto badName = std::make_unique<CreateMaterialCommand>("0bad", steel());
    CHECK_FALSE(history.execute(document, std::move(badName)).has_value());
    CHECK(history.canRedo());

    requireRedo(history, document);
    REQUIRE(features::findMaterial(document, id) != nullptr);
    CHECK(features::findMaterial(document, id)->materialId() == id);
}

// --- delete -----------------------------------------------------------------

TEST_CASE("MaterialCommand_DeleteRestoresTheExactMaterialAndItsAssignment",
          "[features][materialcmd]") {
    Document document{"Part"};
    CommandHistory history;
    const MaterialId id = createOrFail(document, "Steel", steel());
    REQUIRE(features::assignMaterial(document, id));
    const MaterialDefinition before = definitionOf(document, id);

    run<DeleteMaterialCommand>(history, document, id);
    CHECK(features::findMaterial(document, id) == nullptr);
    // The qualified deletion policy: the assignment becomes Unresolved and still
    // names the material that went (P15-ASSIGN-001).
    CHECK(features::materialAssignment(document).state == MaterialAssignmentState::Unresolved);
    CHECK(features::materialAssignment(document).material == id);

    requireUndo(history, document);
    REQUIRE(features::findMaterial(document, id) != nullptr);
    // Exact canonical state, not a reconstruction from a name and an ID.
    CHECK(definitionOf(document, id) == before);
    CHECK(features::findMaterial(document, id)->name() == "Steel");
    CHECK(features::materialAssignment(document).state == MaterialAssignmentState::Resolved);
    CHECK(features::materialAssignment(document).material == id);

    requireRedo(history, document);
    CHECK(features::findMaterial(document, id) == nullptr);
    CHECK(features::materialAssignment(document).state == MaterialAssignmentState::Unresolved);
    CHECK(features::materialAssignment(document).material == id);
}

TEST_CASE("MaterialCommand_DeleteAndUndoNeverRebindAnAssignmentToASameNamedMaterial",
          "[features][materialcmd]") {
    // THE NO-REBIND FIXTURE, as a command sequence. Two materials with the same
    // DESIGNATION -- names are unique, designations are not (P15-CUSTOM-001) -- and
    // an assignment to the first. At no point may the assignment reach the second.
    Document document{"Part"};
    CommandHistory history;
    MaterialDefinition other = steel();
    other.notes = "A different material with the same designation.";
    const MaterialId a = createOrFail(document, "SteelA", steel());
    const MaterialId b = createOrFail(document, "SteelB", other);
    REQUIRE(definitionOf(document, a).designation == definitionOf(document, b).designation);
    REQUIRE(features::assignMaterial(document, a));

    run<DeleteMaterialCommand>(history, document, a);
    CHECK(features::materialAssignment(document).state == MaterialAssignmentState::Unresolved);
    CHECK(features::materialAssignment(document).material == a);
    CHECK(features::materialAssignment(document).material != b);
    CHECK(features::effectiveMaterial(document) == nullptr);

    requireUndo(history, document);
    CHECK(features::materialAssignment(document).state == MaterialAssignmentState::Resolved);
    CHECK(features::materialAssignment(document).material == a);
    REQUIRE(features::effectiveMaterial(document) != nullptr);
    CHECK(features::effectiveMaterial(document)->materialId() == a);

    requireRedo(history, document);
    CHECK(features::materialAssignment(document).state == MaterialAssignmentState::Unresolved);
    CHECK(features::materialAssignment(document).material == a);
    CHECK(features::materialAssignment(document).material != b);
    // B was never touched by any of it.
    CHECK(features::findMaterial(document, b) != nullptr);
    CHECK(definitionOf(document, b) == other);
}

TEST_CASE("MaterialCommand_DeleteRefusesAnIdThatIsNotAMaterial", "[features][materialcmd]") {
    // The precondition a generic object delete cannot express. Without it, this
    // would delete the sketch and report success.
    Document document{"Part"};
    CommandHistory history;
    auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
    const ObjectId notAMaterial = require(document.addObject(std::move(sketch)));
    const std::size_t objects = document.objectCount();

    auto command =
        std::make_unique<DeleteMaterialCommand>(MaterialId::fromValue(notAMaterial.value()));
    const Result<void> refused = history.execute(document, std::move(command));
    REQUIRE_FALSE(refused);
    CHECK(refused.error().code == ErrorCode::NotFound);
    CHECK(document.objectCount() == objects);
    CHECK(document.findObject(notAMaterial) != nullptr);
    CHECK(history.undoCount() == 0);

    // And an ID that names nothing at all.
    auto absent = std::make_unique<DeleteMaterialCommand>(MaterialId::fromValue(999));
    CHECK_FALSE(history.execute(document, std::move(absent)).has_value());
    CHECK(history.undoCount() == 0);
}

TEST_CASE("MaterialCommand_ALibraryEntryCannotBeDeletedBecauseItIsNotADocumentObject",
          "[features][materialcmd]") {
    // Built-in library entries are `static constexpr` with no ObjectId at all
    // (P15-MAT-001, P15-CUSTOM-001), so there is nothing for a delete command to
    // name. Protection is structural rather than a check: a compile-fail case in the
    // `custom` group proves a LibraryMaterial cannot even be added to a document.
    //
    // What CAN be deleted is a material IMPORTED from the library, which is a
    // document object like any other -- and deleting it leaves the library
    // untouched.
    Document document{"Part"};
    CommandHistory history;
    const Result<materials::LibraryMaterial> entry =
        materials::findLibraryMaterial("bettercad", "al-6061-t6");
    REQUIRE(entry);
    const materials::LibraryMaterial libraryBefore = *entry;
    const Result<MaterialId> imported =
        features::importLibraryMaterial(document, "Aluminium", *entry);
    REQUIRE(imported);

    run<DeleteMaterialCommand>(history, document, *imported);
    CHECK(features::findMaterial(document, *imported) == nullptr);
    // The library is exactly as it was, and still has its four entries.
    const Result<materials::LibraryMaterial> after =
        materials::findLibraryMaterial("bettercad", "al-6061-t6");
    REQUIRE(after);
    CHECK(*after == libraryBefore);
    CHECK(materials::builtInMaterials().size() == 4);

    requireUndo(history, document);
    REQUIRE(features::findMaterial(document, *imported) != nullptr);
    // Including the provenance the import recorded.
    CHECK(definitionOf(document, *imported).provenance.material.kind ==
          SourceKind::LibraryReference);
    CHECK(definitionOf(document, *imported).origin->entry == "al-6061-t6");
}

// --- edit -------------------------------------------------------------------

TEST_CASE("MaterialCommand_EditRestoresExactPriorStateIncludingUnknownProperties",
          "[features][materialcmd]") {
    // S0: density 7850, E 200 GPa, k UNKNOWN.
    // S1: density 7900, E 210 GPa, k known.
    // undo -> S0 exactly. redo -> S1 exactly.
    Document document{"Part"};
    CommandHistory history;
    const MaterialId id = createOrFail(document, "Steel", steel());
    const MaterialDefinition s0 = definitionOf(document, id);
    REQUIRE(s0.thermal.thermalConductivity.isUnknown());

    MaterialDefinition s1 = s0;
    s1.designation = "Custom Steel";
    s1.mechanical.density = MaterialProperty<Density>::known(Density::fromSi(7900.0));
    s1.mechanical.youngsModulus = MaterialProperty<ElasticModulus>::known(210_GPa);
    s1.thermal.thermalConductivity = MaterialProperty<ThermalConductivity>::known(50_W_per_m_K);

    run<EditMaterialCommand>(history, document, id, s1);
    CHECK(definitionOf(document, id) == s1);
    CHECK(features::findMaterial(document, id)->materialId() == id);

    requireUndo(history, document);
    CHECK(definitionOf(document, id) == s0);
    // UNKNOWN, not zero -- the failure this whole property model exists to prevent.
    CHECK(definitionOf(document, id).thermal.thermalConductivity.isUnknown());
    CHECK_FALSE(definitionOf(document, id).thermal.thermalConductivity.value().has_value());
    CHECK(definitionOf(document, id).designation == "Steel");
    CHECK_THAT(definitionOf(document, id).mechanical.density.value()->in(units::kg_per_m3),
               WithinRel(7850.0, 1e-12));

    requireRedo(history, document);
    CHECK(definitionOf(document, id) == s1);
    CHECK(definitionOf(document, id).thermal.thermalConductivity.isKnown());
    // The identity never moved through any of it.
    CHECK(features::findMaterial(document, id)->materialId() == id);
    CHECK(features::materialCount(document) == 1);
}

TEST_CASE("MaterialCommand_EditRestoresARemovedPropertyAsKnownNotZero",
          "[features][materialcmd]") {
    // Known -> removed -> undo restores the original Known -> redo removes again.
    Document document{"Part"};
    CommandHistory history;
    const MaterialId id = createOrFail(document, "Steel", steel());
    const MaterialDefinition before = definitionOf(document, id);
    REQUIRE(before.mechanical.yieldStrength.isKnown());

    MaterialDefinition removed = before;
    removed.mechanical.yieldStrength = {};
    run<EditMaterialCommand>(history, document, id, removed);
    CHECK(definitionOf(document, id).mechanical.yieldStrength.isUnknown());

    requireUndo(history, document);
    CHECK(definitionOf(document, id).mechanical.yieldStrength.isKnown());
    CHECK_THAT(definitionOf(document, id).mechanical.yieldStrength.value()->in(units::MPa),
               WithinRel(235.0, 1e-12));
    CHECK(definitionOf(document, id) == before);

    requireRedo(history, document);
    CHECK(definitionOf(document, id).mechanical.yieldStrength.isUnknown());
    CHECK_FALSE(definitionOf(document, id).mechanical.yieldStrength.value().has_value());
}

TEST_CASE("MaterialCommand_EditRestoresAValueAndItsProvenanceTogether",
          "[features][materialcmd]") {
    // The one that a value/metadata split would get wrong: undoing a value edit must
    // not leave the NEW provenance behind describing the OLD number.
    Document document{"Part"};
    CommandHistory history;
    const MaterialId id = createOrFail(document, "Steel", steel());
    const MaterialDefinition before = definitionOf(document, id);
    const PropertyProvenance measuredBefore =
        before.provenance.mechanical.at(MechanicalPropertyKind::Density);
    REQUIRE(measuredBefore.kind == SourceKind::Measured);
    REQUIRE(measuredBefore.revision == "Rev A");

    PropertyProvenance typedIn;
    typedIn.kind = SourceKind::UserEntered;
    typedIn.source = "corrected by hand";
    MaterialDefinition after = before;
    after.mechanical.density = MaterialProperty<Density>::known(Density::fromSi(7900.0));
    after.provenance.mechanical[MechanicalPropertyKind::Density] = typedIn;

    run<EditMaterialCommand>(history, document, id, after);
    CHECK(materials::effectiveProvenance(definitionOf(document, id).provenance,
                                        MechanicalPropertyKind::Density) == typedIn);
    CHECK_THAT(definitionOf(document, id).mechanical.density.value()->in(units::kg_per_m3),
               WithinRel(7900.0, 1e-12));

    requireUndo(history, document);
    // BOTH restored: the value AND the citation, together.
    CHECK_THAT(definitionOf(document, id).mechanical.density.value()->in(units::kg_per_m3),
               WithinRel(7850.0, 1e-12));
    const PropertyProvenance restored = materials::effectiveProvenance(
        definitionOf(document, id).provenance, MechanicalPropertyKind::Density);
    CHECK(restored == measuredBefore);
    CHECK(restored.kind == SourceKind::Measured);
    CHECK(restored.revision == "Rev A");
    CHECK(restored.date == materials::Date::of(2024, 3, 17));

    requireRedo(history, document);
    CHECK(materials::effectiveProvenance(definitionOf(document, id).provenance,
                                        MechanicalPropertyKind::Density) == typedIn);
}

TEST_CASE("MaterialCommand_AProvenanceOnlyEditChangesNoNumber", "[features][materialcmd]") {
    // Metadata alone, through the command path: the mass and the derived shear
    // modulus must not move, in either direction.
    BoxPart part;
    const MaterialId id = createOrFail(part.document, "Steel", steel());
    REQUIRE(features::assignMaterial(part.document, id));
    CommandHistory history;

    const double mass = part.massKg();
    const MaterialProperty<ElasticModulus> shear =
        materials::derivedShearModulus(definitionOf(part.document, id).mechanical);

    MaterialDefinition metadataOnly = definitionOf(part.document, id);
    PropertyProvenance handbook;
    handbook.kind = SourceKind::Handbook;
    handbook.source = "Synthetic handbook";
    metadataOnly.provenance.material = handbook;
    metadataOnly.provenance.mechanical[MechanicalPropertyKind::YoungsModulus] = handbook;
    run<EditMaterialCommand>(history, part.document, id, metadataOnly);

    CHECK(part.massKg() == mass);
    CHECK(materials::derivedShearModulus(definitionOf(part.document, id).mechanical) == shear);
    requireUndo(history, part.document);
    CHECK(part.massKg() == mass);
    CHECK(materials::derivedShearModulus(definitionOf(part.document, id).mechanical) == shear);
    requireRedo(history, part.document);
    CHECK(part.massKg() == mass);
    CHECK(materials::derivedShearModulus(definitionOf(part.document, id).mechanical) == shear);
}

TEST_CASE("MaterialCommand_AFailedEditLeavesTheMaterialExactlyAsItWas",
          "[features][materialcmd]") {
    Document document{"Part"};
    CommandHistory history;
    const MaterialId id = createOrFail(document, "Steel", steel());
    run<EditMaterialCommand>(history, document, id, steel());  // a successful no-op edit
    const MaterialDefinition before = definitionOf(document, id);
    const std::size_t depth = history.undoCount();
    const std::uint64_t revision = document.revision();

    // Several invalid edits, each required to change nothing at all.
    MaterialDefinition negativeModulus = before;
    negativeModulus.mechanical.youngsModulus = MaterialProperty<ElasticModulus>::known(-1_GPa);
    MaterialDefinition zeroDensity = before;
    zeroDensity.mechanical.density = MaterialProperty<Density>::known(Density::fromSi(0.0));
    MaterialDefinition incompressible = before;
    incompressible.mechanical.poissonRatio =
        MaterialProperty<PoissonRatio>::known(PoissonRatio::of(0.5));
    MaterialDefinition fakedDerived = before;
    fakedDerived.mechanical.density = MaterialProperty<Density>::derived(Density::fromSi(7850.0));

    for (const MaterialDefinition& invalid :
         {negativeModulus, zeroDensity, incompressible, fakedDerived}) {
        auto command = std::make_unique<EditMaterialCommand>(id, invalid);
        CHECK_FALSE(history.execute(document, std::move(command)).has_value());
        CHECK(definitionOf(document, id) == before);
        CHECK(document.revision() == revision);
        CHECK(history.undoCount() == depth);
    }
    // An edit of a material that is not there fails too, and touches nothing.
    auto absent = std::make_unique<EditMaterialCommand>(MaterialId::fromValue(999), before);
    CHECK_FALSE(history.execute(document, std::move(absent)).has_value());
    CHECK(history.undoCount() == depth);
}

TEST_CASE("MaterialCommand_UndoOrRedoBeforeExecuteIsRefusedRatherThanGuessed",
          "[features][materialcmd]") {
    Document document{"Part"};
    const MaterialId id = createOrFail(document, "Steel", steel());
    EditMaterialCommand edit{id, steel()};
    CHECK_FALSE(edit.undo(document).has_value());
    CHECK_FALSE(edit.redo(document).has_value());
    CreateMaterialCommand create{"Other", steel()};
    CHECK_FALSE(create.undo(document).has_value());
    CHECK_FALSE(create.redo(document).has_value());
    DeleteMaterialCommand del{id};
    CHECK_FALSE(del.undo(document).has_value());
    CHECK_FALSE(del.redo(document).has_value());
    AssignMaterialCommand assign{id};
    CHECK_FALSE(assign.undo(document).has_value());
    CHECK_FALSE(assign.redo(document).has_value());
    RemoveMaterialAssignmentCommand remove;
    CHECK_FALSE(remove.undo(document).has_value());
    CHECK_FALSE(remove.redo(document).has_value());
    // And the document is untouched by all ten refusals.
    CHECK(definitionOf(document, id) == steel());
    CHECK_FALSE(document.materialAssignment().has_value());
}

// --- assign and remove ------------------------------------------------------

TEST_CASE("MaterialCommand_AssignExecutesUndoesAndRedoes", "[features][materialcmd]") {
    Document document{"Part"};
    CommandHistory history;
    const MaterialId a = createOrFail(document, "Steel", steel());
    CHECK(features::materialAssignment(document).state == MaterialAssignmentState::Unassigned);

    run<AssignMaterialCommand>(history, document, a);
    CHECK(features::materialAssignment(document).state == MaterialAssignmentState::Resolved);
    CHECK(features::materialAssignment(document).material == a);

    requireUndo(history, document);
    CHECK(features::materialAssignment(document).state == MaterialAssignmentState::Unassigned);
    CHECK_FALSE(document.materialAssignment().has_value());

    requireRedo(history, document);
    CHECK(features::materialAssignment(document).material == a);
}

TEST_CASE("MaterialCommand_AssignReplacesByIdentityEvenWhenDesignationsMatch",
          "[features][materialcmd]") {
    // S0: P -> A. execute assign B. S1: P -> B. undo: S2 = P -> A. redo: S3 = P -> B.
    // A and B share a designation, so only identity can decide.
    Document document{"Part"};
    CommandHistory history;
    MaterialDefinition other = steel();
    other.notes = "B";
    const MaterialId a = createOrFail(document, "SteelA", steel());
    const MaterialId b = createOrFail(document, "SteelB", other);
    REQUIRE(definitionOf(document, a).designation == definitionOf(document, b).designation);
    REQUIRE(features::assignMaterial(document, a));

    run<AssignMaterialCommand>(history, document, b);
    CHECK(features::materialAssignment(document).material == b);
    CHECK(features::effectiveMaterial(document)->materialId() == b);

    requireUndo(history, document);
    CHECK(features::materialAssignment(document).material == a);
    CHECK(features::effectiveMaterial(document)->materialId() == a);

    requireRedo(history, document);
    CHECK(features::materialAssignment(document).material == b);
    CHECK(features::effectiveMaterial(document)->materialId() == b);

    // Now the other direction, so both A->B and B->A are covered.
    run<AssignMaterialCommand>(history, document, a);
    CHECK(features::materialAssignment(document).material == a);
    requireUndo(history, document);
    CHECK(features::materialAssignment(document).material == b);
}

TEST_CASE("MaterialCommand_AssignRefusesAMaterialThatIsNotInThisDocument",
          "[features][materialcmd]") {
    Document document{"Part"};
    CommandHistory history;
    const MaterialId a = createOrFail(document, "Steel", steel());
    REQUIRE(features::assignMaterial(document, a));

    auto absent = std::make_unique<AssignMaterialCommand>(MaterialId::fromValue(999));
    const Result<void> refused = history.execute(document, std::move(absent));
    REQUIRE_FALSE(refused);
    CHECK(refused.error().code == ErrorCode::NotFound);
    // The existing assignment is untouched and no history entry was made.
    CHECK(features::materialAssignment(document).material == a);
    CHECK(history.undoCount() == 0);

    // An ID that names a non-material object is equally refused.
    auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
    const ObjectId notAMaterial = require(document.addObject(std::move(sketch)));
    auto wrongKind =
        std::make_unique<AssignMaterialCommand>(MaterialId::fromValue(notAMaterial.value()));
    CHECK_FALSE(history.execute(document, std::move(wrongKind)).has_value());
    CHECK(features::materialAssignment(document).material == a);
    CHECK(history.undoCount() == 0);
}

TEST_CASE("MaterialCommand_RemoveAssignmentRestoresTheExactPreviousAssignment",
          "[features][materialcmd]") {
    Document document{"Part"};
    CommandHistory history;
    const MaterialId a = createOrFail(document, "Steel", steel());
    REQUIRE(features::assignMaterial(document, a));

    run<RemoveMaterialAssignmentCommand>(history, document);
    CHECK(features::materialAssignment(document).state == MaterialAssignmentState::Unassigned);
    CHECK(features::effectiveMaterial(document) == nullptr);

    requireUndo(history, document);
    CHECK(features::materialAssignment(document).state == MaterialAssignmentState::Resolved);
    CHECK(features::materialAssignment(document).material == a);

    requireRedo(history, document);
    CHECK(features::materialAssignment(document).state == MaterialAssignmentState::Unassigned);
}

TEST_CASE("MaterialCommand_RemovingAnAbsentAssignmentSucceedsAndChangesNothing",
          "[features][materialcmd]") {
    // Following SetActiveConfigurationCommand rather than inventing a convention:
    // the command records the previous state, and the document's revision only moves
    // on a real change.
    Document document{"Part"};
    CommandHistory history;
    createOrFail(document, "Steel", steel());
    CHECK_FALSE(document.materialAssignment().has_value());
    const std::uint64_t revision = document.revision();

    run<RemoveMaterialAssignmentCommand>(history, document);
    CHECK_FALSE(document.materialAssignment().has_value());
    CHECK(document.revision() == revision);
    requireUndo(history, document);
    CHECK_FALSE(document.materialAssignment().has_value());
    requireRedo(history, document);
    CHECK_FALSE(document.materialAssignment().has_value());
}

TEST_CASE("MaterialCommand_AssignmentCommandsCarryDirectIntentAndNothingEffective",
          "[features][materialcmd]") {
    // DIRECT VS EFFECTIVE. The canonical state is ONE optional MaterialId on the
    // Document (ADR-026): there is no per-body, per-occurrence or per-configuration
    // assignment, so there is no inherited intent for a remove to reach by mistake.
    // The effective material is derived from the direct one and is never stored.
    //
    // Proved here by showing that the document's own canonical field is what the
    // commands move, and that the derived answer follows it.
    Document document{"Part"};
    CommandHistory history;
    const MaterialId a = createOrFail(document, "Steel", steel());

    run<AssignMaterialCommand>(history, document, a);
    CHECK(document.materialAssignment() == a);                       // canonical
    CHECK(features::effectiveMaterial(document)->materialId() == a); // derived

    run<RemoveMaterialAssignmentCommand>(history, document);
    CHECK_FALSE(document.materialAssignment().has_value());
    CHECK(features::effectiveMaterial(document) == nullptr);

    requireUndo(history, document);
    CHECK(document.materialAssignment() == a);
    CHECK(features::effectiveMaterial(document)->materialId() == a);
}

// --- history semantics ------------------------------------------------------

TEST_CASE("MaterialCommand_ANewCommandInvalidatesRedo", "[features][materialcmd]") {
    // Create A, edit name to X, undo the edit, then edit to Y. The old "X" redo must
    // be gone, and the final state must be Y with no hidden branch.
    Document document{"Part"};
    CommandHistory history;
    CreateMaterialCommand* create = run<CreateMaterialCommand>(history, document, "Steel", steel());
    const MaterialId id = create->materialId();

    MaterialDefinition x = steel();
    x.designation = "X";
    run<EditMaterialCommand>(history, document, id, x);
    CHECK(definitionOf(document, id).designation == "X");

    requireUndo(history, document);
    CHECK(definitionOf(document, id).designation == "Steel");
    REQUIRE(history.canRedo());
    CHECK(history.redoCount() == 1);

    MaterialDefinition y = steel();
    y.designation = "Y";
    run<EditMaterialCommand>(history, document, id, y);
    // Redo is gone: the "X" edit can never come back.
    CHECK_FALSE(history.canRedo());
    CHECK(history.redoCount() == 0);
    CHECK_FALSE(history.redo(document).has_value());
    CHECK(definitionOf(document, id).designation == "Y");

    // Undo now reaches the "Y" edit, then the create -- not the discarded "X".
    requireUndo(history, document);
    CHECK(definitionOf(document, id).designation == "Steel");
    requireUndo(history, document);
    CHECK(features::findMaterial(document, id) == nullptr);
}

TEST_CASE("MaterialCommand_AFiveStepChainUndoesAndRedoesToExactlyTheSameStates",
          "[features][materialcmd]") {
    // create -> edit -> assign -> edit density -> remove assignment, then all the
    // way back and all the way forward, comparing whole-document snapshots.
    BoxPart part;
    CommandHistory history;
    const Document s0 = part.document.clone();

    CreateMaterialCommand* create =
        run<CreateMaterialCommand>(history, part.document, "Steel", steel());
    const MaterialId id = create->materialId();

    MaterialDefinition renamed = steel();
    renamed.designation = "Structural Steel";
    run<EditMaterialCommand>(history, part.document, id, renamed);
    run<AssignMaterialCommand>(history, part.document, id);

    MaterialDefinition denser = renamed;
    denser.mechanical.density = MaterialProperty<Density>::known(Density::fromSi(7900.0));
    run<EditMaterialCommand>(history, part.document, id, denser);
    const double massWithAssignment = part.massKg();

    run<RemoveMaterialAssignmentCommand>(history, part.document);
    const Document s5 = part.document.clone();
    CHECK(history.undoCount() == 5);

    // All the way back.
    for (int step = 0; step < 5; ++step) {
        requireUndo(history, part.document);
    }
    CHECK_FALSE(history.canUndo());
    CHECK(features::materialCount(part.document) == 0);
    CHECK_FALSE(part.document.materialAssignment().has_value());
    CHECK(equivalent(part.document, s0));

    // All the way forward.
    for (int step = 0; step < 5; ++step) {
        requireRedo(history, part.document);
    }
    CHECK_FALSE(history.canRedo());
    CHECK(equivalent(part.document, s5));
    // The identity is the one the create produced, through every step.
    REQUIRE(features::findMaterial(part.document, id) != nullptr);
    CHECK(features::findMaterial(part.document, id)->materialId() == id);
    CHECK(definitionOf(part.document, id) == denser);
    // And the mass recomputes to what it was, from restored intent.
    REQUIRE(features::assignMaterial(part.document, id));
    CHECK_THAT(part.massKg(), WithinRel(massWithAssignment, 1e-12));
}

TEST_CASE("MaterialCommand_DeleteThenUndoThenEditKeepsOneIdentityThroughout",
          "[features][materialcmd]") {
    // The chain that catches a restoration which quietly creates a different
    // identity: create, assign, delete, undo the delete, then edit.
    Document document{"Part"};
    CommandHistory history;
    CreateMaterialCommand* create = run<CreateMaterialCommand>(history, document, "Steel", steel());
    const MaterialId id = create->materialId();
    run<AssignMaterialCommand>(history, document, id);
    run<DeleteMaterialCommand>(history, document, id);
    requireUndo(history, document);

    // The assignment resolves the SAME material, and an edit of it succeeds.
    CHECK(features::materialAssignment(document).state == MaterialAssignmentState::Resolved);
    CHECK(features::materialAssignment(document).material == id);
    MaterialDefinition edited = steel();
    edited.designation = "Edited";
    run<EditMaterialCommand>(history, document, id, edited);
    CHECK(definitionOf(document, id).designation == "Edited");
    CHECK(features::findMaterial(document, id)->materialId() == id);
    CHECK(features::materialCount(document) == 1);
}

TEST_CASE("MaterialCommand_ASameNamedReplacementNeverCapturesAStaleAssignment",
          "[features][materialcmd]") {
    // A = Steel, P -> A, delete A, create B with the same designation. Undoing B's
    // creation, then A's deletion, must resolve A -- and at no point B.
    Document document{"Part"};
    CommandHistory history;
    CreateMaterialCommand* createA =
        run<CreateMaterialCommand>(history, document, "SteelA", steel());
    const MaterialId a = createA->materialId();
    run<AssignMaterialCommand>(history, document, a);
    run<DeleteMaterialCommand>(history, document, a);
    CHECK(features::materialAssignment(document).state == MaterialAssignmentState::Unresolved);

    CreateMaterialCommand* createB =
        run<CreateMaterialCommand>(history, document, "SteelB", steel());
    const MaterialId b = createB->materialId();
    CHECK(b != a);
    CHECK(definitionOf(document, b).designation == "Steel");
    // Still unresolved A -- an identical designation changes nothing.
    CHECK(features::materialAssignment(document).state == MaterialAssignmentState::Unresolved);
    CHECK(features::materialAssignment(document).material == a);

    requireUndo(history, document);  // undo create B
    CHECK(features::findMaterial(document, b) == nullptr);
    CHECK(features::materialAssignment(document).state == MaterialAssignmentState::Unresolved);
    CHECK(features::materialAssignment(document).material == a);

    requireUndo(history, document);  // undo delete A
    CHECK(features::materialAssignment(document).state == MaterialAssignmentState::Resolved);
    CHECK(features::materialAssignment(document).material == a);
    CHECK(features::effectiveMaterial(document)->materialId() == a);
}

TEST_CASE("MaterialCommand_HistoryRefusesASecondDocument", "[features][materialcmd]") {
    // The history binds to the first document it is used with. A command built for
    // one document must not mutate another, even when IDs collide numerically.
    Document first{"First"};
    Document second{"Second"};
    CommandHistory history;
    const MaterialId a = createOrFail(first, "Steel", steel());
    const MaterialId b = createOrFail(second, "Steel", steel());
    REQUIRE(a.value() == b.value());  // numerically identical, different documents

    run<AssignMaterialCommand>(history, first, a);
    auto command = std::make_unique<AssignMaterialCommand>(b);
    const Result<void> refused = history.execute(second, std::move(command));
    REQUIRE_FALSE(refused);
    CHECK_FALSE(second.materialAssignment().has_value());
    CHECK(first.materialAssignment() == a);
}

TEST_CASE("MaterialCommand_EnumerationOrderIsTheSameAfterAnUndoRedoRoundTrip",
          "[features][materialcmd]") {
    Document document{"Part"};
    CommandHistory history;
    CreateMaterialCommand* createA = run<CreateMaterialCommand>(history, document, "A", steel());
    CreateMaterialCommand* createB = run<CreateMaterialCommand>(history, document, "B", steel());
    const std::vector<MaterialId> expected{createA->materialId(), createB->materialId()};
    CHECK(features::materialIds(document) == expected);

    requireUndo(history, document);
    CHECK(features::materialIds(document) == std::vector<MaterialId>{createA->materialId()});
    requireRedo(history, document);
    // Ascending ID from the document's ordered map, so B returns to its place.
    CHECK(features::materialIds(document) == expected);
    for (int pass = 0; pass < 5; ++pass) {
        requireUndo(history, document);
        requireRedo(history, document);
        CHECK(features::materialIds(document) == expected);
    }
}

TEST_CASE("MaterialCommand_AnUndoneCreateDoesNotReleaseItsIdForReuse",
          "[features][materialcmd]") {
    // The allocator only ever counts up (P15-MAT-001), so an undone create does not
    // hand its ID back. That is what makes a redo safe: nothing else can have taken
    // the ID in the meantime -- and executing a new command clears redo anyway, so
    // the two guards are independent.
    Document document{"Part"};
    CommandHistory history;
    CreateMaterialCommand* createA = run<CreateMaterialCommand>(history, document, "A", steel());
    const MaterialId a = createA->materialId();
    requireUndo(history, document);
    REQUIRE(history.canRedo());

    CreateMaterialCommand* createB = run<CreateMaterialCommand>(history, document, "B", steel());
    const MaterialId b = createB->materialId();
    CHECK(b != a);
    CHECK(b.value() > a.value());
    // And the new command cleared redo, so A's creation cannot be replayed into a
    // document that has moved on.
    CHECK_FALSE(history.canRedo());
}

// --- derived state is not in the history ------------------------------------

TEST_CASE("MaterialCommand_MassRecomputesFromRestoredDensityRatherThanFromHistory",
          "[features][materialcmd]") {
    // 20 x 30 x 50 mm = 30000 mm^3 = 3e-5 m^3.
    // density 1000 -> 0.03 kg;  density 2000 -> 0.06 kg.
    BoxPart part;
    CommandHistory history;
    MaterialDefinition light;
    light.designation = "Synthetic";
    light.mechanical.density = MaterialProperty<Density>::known(Density::fromSi(1000.0));
    const MaterialId id = createOrFail(part.document, "Synthetic", light);
    REQUIRE(features::assignMaterial(part.document, id));

    const double m1 = part.massKg();
    CHECK_THAT(m1, WithinRel(0.03, 1e-12));

    MaterialDefinition heavy = light;
    heavy.mechanical.density = MaterialProperty<Density>::known(Density::fromSi(2000.0));
    run<EditMaterialCommand>(history, part.document, id, heavy);
    const double m2 = part.massKg();
    CHECK_THAT(m2, WithinRel(2.0 * m1, 1e-12));
    CHECK_THAT(m2, WithinRel(0.06, 1e-12));

    requireUndo(history, part.document);
    CHECK_THAT(part.massKg(), WithinRel(m1, 1e-12));
    requireRedo(history, part.document);
    CHECK_THAT(part.massKg(), WithinRel(m2, 1e-12));

    // The volume never moved, so the mass can only have followed the density.
    const Result<features::PartMassProperties> mass =
        features::partMassProperties(part.document, part.regenerator, part.feature);
    REQUIRE(mass);
    CHECK_THAT(mass->volume.in(units::mm3), WithinRel(30000.0, 1e-12));
}

TEST_CASE("MaterialCommand_DerivedModuliRecomputeFromRestoredElasticConstants",
          "[features][materialcmd]") {
    // G = E / (2(1 + nu)). Editing E moves G; undo moves it back. Nothing stores G:
    // MechanicalProperties has no slot for one (ADR-027), so it could not enter the
    // history even by mistake.
    Document document{"Part"};
    CommandHistory history;
    const MaterialId id = createOrFail(document, "Steel", steel());
    const auto shearGPa = [&] {
        const MaterialProperty<ElasticModulus> g =
            materials::derivedShearModulus(definitionOf(document, id).mechanical);
        REQUIRE(g.isDerived());
        return g.value()->in(units::GPa);
    };
    CHECK_THAT(shearGPa(), WithinRel(200.0 / 2.6, 1e-12));

    MaterialDefinition stiffer = steel();
    stiffer.mechanical.youngsModulus = MaterialProperty<ElasticModulus>::known(210_GPa);
    run<EditMaterialCommand>(history, document, id, stiffer);
    CHECK_THAT(shearGPa(), WithinRel(210.0 / 2.6, 1e-12));

    requireUndo(history, document);
    CHECK_THAT(shearGPa(), WithinRel(200.0 / 2.6, 1e-12));
    requireRedo(history, document);
    CHECK_THAT(shearGPa(), WithinRel(210.0 / 2.6, 1e-12));

    // Still DERIVED after the round trip, never promoted to a stored value.
    CHECK(materials::derivedShearModulus(definitionOf(document, id).mechanical).isDerived());
    CHECK_FALSE(materials::derivedShearModulus(definitionOf(document, id).mechanical).isKnown());
}

TEST_CASE("MaterialCommand_CompletenessRecomputesRatherThanBeingRestoredFromHistory",
          "[features][materialcmd]") {
    // k Unknown -> transient conduction Incomplete, missing the conductivity.
    // Set k -> Ready. Undo -> Incomplete again, and the report is rebuilt, not
    // remembered.
    Document document{"Part"};
    CommandHistory history;
    MaterialDefinition partial = steel();
    partial.thermal.specificHeatCapacity =
        MaterialProperty<SpecificHeatCapacity>::known(460_J_per_kg_K);
    const MaterialId id = createOrFail(document, "Steel", partial);

    const auto report = [&] {
        const Result<materials::CompletenessReport> result = features::materialCompleteness(
            document, id, materials::ConsumerKind::ThermalTransient);
        REQUIRE(result);
        return *result;
    };
    CHECK(report().state == materials::CompletenessState::Incomplete);
    CHECK(report().missingThermal ==
          std::vector{ThermalPropertyKind::ThermalConductivity});

    MaterialDefinition complete = partial;
    complete.thermal.thermalConductivity = MaterialProperty<ThermalConductivity>::known(50_W_per_m_K);
    run<EditMaterialCommand>(history, document, id, complete);
    CHECK(report().state == materials::CompletenessState::Ready);
    CHECK(report().missingThermal.empty());

    requireUndo(history, document);
    CHECK(report().state == materials::CompletenessState::Incomplete);
    CHECK(report().missingThermal ==
          std::vector{ThermalPropertyKind::ThermalConductivity});
    requireRedo(history, document);
    CHECK(report().state == materials::CompletenessState::Ready);
}

TEST_CASE("MaterialCommand_CommandsAreTheSizeOfTheirCanonicalStateAndNoLarger",
          "[features][materialcmd]") {
    // A structural check on what the commands can possibly hold. Each is bounded by
    // its declared members -- an ID, a name, a MaterialDefinition, an optional
    // wrapped command -- and a MaterialDefinition has no mass, volume, centroid,
    // inertia or completeness field (compile-fail cases in the `massp` and `prov`
    // groups prove those absences).
    //
    // So a derived value cannot be in a command payload: there is nowhere in the
    // type for it to sit. This asserts the bound rather than the exact number, so it
    // does not become a brittle sizeof() test.
    static_assert(sizeof(AssignMaterialCommand) <=
                  sizeof(Command) + 4 * sizeof(MaterialId) + 2 * sizeof(void*));
    static_assert(sizeof(RemoveMaterialAssignmentCommand) <=
                  sizeof(Command) + 4 * sizeof(MaterialId) + 2 * sizeof(void*));
    // EditMaterialCommand holds two definitions and an ID, and nothing else.
    static_assert(sizeof(EditMaterialCommand) <=
                  sizeof(Command) + sizeof(MaterialId) + 2 * sizeof(MaterialDefinition) +
                      4 * sizeof(void*));
    SUCCEED("command payloads are bounded by their canonical state");
}

TEST_CASE("MaterialCommand_TheSameSequenceFromTheSameStartGivesTheSameResult",
          "[features][materialcmd]") {
    // Determinism of the whole command path, compared as whole documents rather than
    // field by field.
    const auto build = [] {
        Document document(DocumentId::fromValue(*Uuid::parse("00000000-0000-4000-8000-000000000042")),
                          "Part");
        CommandHistory history;
        auto create = std::make_unique<CreateMaterialCommand>("Steel", steel());
        CreateMaterialCommand* raw = create.get();
        REQUIRE(history.execute(document, std::move(create)).has_value());
        const MaterialId id = raw->materialId();
        MaterialDefinition edited = steel();
        edited.designation = "Structural";
        REQUIRE(history.execute(document, std::make_unique<EditMaterialCommand>(id, edited))
                    .has_value());
        REQUIRE(history.execute(document, std::make_unique<AssignMaterialCommand>(id)).has_value());
        REQUIRE(history.undo(document).has_value());
        REQUIRE(history.redo(document).has_value());
        return document;
    };
    const Document first = build();
    const Document second = build();
    CHECK(equivalent(first, second));
}
