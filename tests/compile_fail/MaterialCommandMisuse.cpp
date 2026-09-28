// Build-failure tests for material commands (see CMakeLists.txt in this
// directory). Built once without any BETTERCAD_CF_* macro as a control, which must
// compile.
//
// The absences these prove are the milestone's central claims.
//
//   - A command carries CANONICAL INTENT ONLY. There is no member, constructor
//     parameter or setter anywhere that could hold a mass, a volume, an inertia
//     tensor or a completeness report, so a derived value cannot enter the undo
//     history even by mistake.
//   - Commands are typed on MaterialId. A name cannot be used to identify a
//     material, because two materials may share a designation; and an ObjectId
//     cannot stand in for a MaterialId, because an object is not a material just
//     because it has an ID.
//   - A library entry cannot be the subject of any command: it has no ObjectId
//     (P15-MAT-001), so there is nothing for a command to name.
//   - Commands are not copyable. The base deletes the copy operations, so history
//     cannot come to hold two owners of one command.
#include <bettercad/core/document/Command.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/materials/MaterialLibrary.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/MaterialCommands.hpp>
#include <bettercad/features/Materials.hpp>

using namespace bettercad;
using namespace bettercad::literals;

int main() {
    Document document{"Part"};
    CommandHistory history;

    // The control: the intended way to say all of this.
    const Result<MaterialId> existing = features::createMaterial(document, "Steel");
    if (!existing) {
        return 1;
    }
    features::MaterialDefinition definition;
    definition.designation = "Steel";
    [[maybe_unused]] const Result<void> created =
        history.execute(document, std::make_unique<features::CreateMaterialCommand>(
                                      "Other", definition));
    [[maybe_unused]] const Result<void> edited =
        history.execute(document, std::make_unique<features::EditMaterialCommand>(
                                      *existing, definition));
    [[maybe_unused]] const Result<void> assigned =
        history.execute(document, std::make_unique<features::AssignMaterialCommand>(*existing));
    [[maybe_unused]] const Result<void> removed = history.execute(
        document, std::make_unique<features::RemoveMaterialAssignmentCommand>());
    features::CreateMaterialCommand create{"Third", definition};

#if defined(BETTERCAD_CF_CREATE_HAS_NO_MASS)
    // No command has anywhere to put a derived value. This is what makes "the
    // history holds no derived state" structural rather than a promise.
    create.setMass(1.0 * units::kg);
#elif defined(BETTERCAD_CF_EDIT_HAS_NO_MASS)
    features::EditMaterialCommand edit{*existing, definition};
    edit.setMass(1.0 * units::kg);
#elif defined(BETTERCAD_CF_EDIT_HAS_NO_INERTIA)
    features::EditMaterialCommand edit{*existing, definition};
    [[maybe_unused]] const auto inertia = edit.inertia();
#elif defined(BETTERCAD_CF_EDIT_HAS_NO_COMPLETENESS)
    features::EditMaterialCommand edit{*existing, definition};
    [[maybe_unused]] const auto report = edit.completeness();
#elif defined(BETTERCAD_CF_DEFINITION_HAS_NO_VOLUME)
    // And neither does the canonical state a command stores.
    [[maybe_unused]] const auto volume = definition.volume;
#elif defined(BETTERCAD_CF_DELETE_TAKES_A_MATERIAL_ID)
    // A MaterialId widens TO ObjectId, never back, so a bare ObjectId cannot be
    // deleted as a material.
    [[maybe_unused]] const features::DeleteMaterialCommand wrong{ObjectId::fromValue(1)};
#elif defined(BETTERCAD_CF_ASSIGN_TAKES_A_MATERIAL_ID)
    [[maybe_unused]] const features::AssignMaterialCommand wrong{ObjectId::fromValue(1)};
#elif defined(BETTERCAD_CF_ASSIGN_TAKES_NO_NAME)
    // NEVER a name. Two materials may share a designation (P15-CUSTOM-001), so a
    // name-keyed command would be ambiguous by construction.
    [[maybe_unused]] const features::AssignMaterialCommand wrong{"Steel"};
#elif defined(BETTERCAD_CF_DELETE_TAKES_NO_NAME)
    [[maybe_unused]] const features::DeleteMaterialCommand wrong{"Steel"};
#elif defined(BETTERCAD_CF_EDIT_TAKES_NO_NAME)
    [[maybe_unused]] const features::EditMaterialCommand wrong{"Steel", definition};
#elif defined(BETTERCAD_CF_DELETE_TAKES_NO_LIBRARY_ENTRY)
    // A library entry has no ObjectId, so no command can name one -- which is why
    // built-in protection needs no runtime check.
    const Result<materials::LibraryMaterial> entry =
        materials::findLibraryMaterial("bettercad", "al-6061-t6");
    [[maybe_unused]] const features::DeleteMaterialCommand wrong{*entry};
#elif defined(BETTERCAD_CF_EDIT_TAKES_NO_LIBRARY_ENTRY)
    const Result<materials::LibraryMaterial> entry =
        materials::findLibraryMaterial("bettercad", "al-6061-t6");
    [[maybe_unused]] const features::EditMaterialCommand wrong{*entry, definition};
#elif defined(BETTERCAD_CF_COMMANDS_ARE_NOT_COPYABLE)
    // The base deletes the copy operations, so the history cannot end up holding two
    // owners of one command, and a command cannot be duplicated into two stacks.
    [[maybe_unused]] const features::CreateMaterialCommand copy = create;
#elif defined(BETTERCAD_CF_MATERIAL_ID_IS_NOT_AN_INT)
    // No naked-integer command target: an index or a loop counter cannot become a
    // material.
    [[maybe_unused]] const features::AssignMaterialCommand wrong{7};
#endif

    return 0;
}
