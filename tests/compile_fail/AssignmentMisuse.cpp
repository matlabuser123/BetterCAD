// Build-failure tests for material assignment (see CMakeLists.txt in this
// directory). Built once without any BETTERCAD_CF_* macro as a control, which
// must compile.
//
// Two of these prove an ABSENCE rather than a rejection: ComponentDefinition has
// no material field, and that is ADR-026's deferral of per-occurrence materials.
// A deferral nobody tests is indistinguishable from an oversight, and would be
// easy to undo by accident.
#include <bettercad/assembly/Component.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>

using namespace bettercad;

int main() {
    Document document{"Part"};

    // The control: the intended way to say all of this.
    const Result<MaterialId> steel = features::createMaterial(document, "Steel");
    if (!steel) {
        return 1;
    }
    [[maybe_unused]] const Result<bool> assigned = features::assignMaterial(document, *steel);
    [[maybe_unused]] const features::Material* resolved = features::effectiveMaterial(document);
    [[maybe_unused]] const features::MaterialAssignment assignment =
        features::materialAssignment(document);
    assembly::ComponentDefinition component;
    component.suppressed = true;

#if defined(BETTERCAD_CF_ASSIGN_OBJECT_ID)
    // An ObjectId is not a MaterialId. A MaterialId widens TO ObjectId, never
    // back: an object is not a material just because it has an ID.
    [[maybe_unused]] const Result<bool> wrong =
        features::assignMaterial(document, ObjectId::fromValue(1));
#elif defined(BETTERCAD_CF_ASSIGN_INTEGER)
    // No naked-integer assignment API: assignMaterial(document, 5) cannot be
    // written, so an index or a loop counter cannot become an assignment.
    [[maybe_unused]] const Result<bool> wrong = features::assignMaterial(document, 5);
#elif defined(BETTERCAD_CF_ASSIGN_COMPONENT_ID)
    // Nor is a ComponentId a MaterialId, though both widen to ObjectId. This is
    // the confusion an occurrence-level assignment would invite.
    [[maybe_unused]] const Result<bool> wrong =
        features::assignMaterial(document, ComponentId::fromValue(1));
#elif defined(BETTERCAD_CF_DOCUMENT_ASSIGN_OBJECT_ID)
    // The document's own unvalidated setter is still typed: it takes intent, not
    // any ID that happens to be lying around.
    [[maybe_unused]] const Result<bool> wrong =
        document.setMaterialAssignment(ObjectId::fromValue(1));
#elif defined(BETTERCAD_CF_OCCURRENCE_MATERIAL_FIELD)
    // ADR-026: an assembly occurrence cannot override its part's material in
    // P15, so there is no field to set. When external references arrive, this is
    // where the override belongs -- beside `suppressed` and `placement`.
    component.material = *steel;
#elif defined(BETTERCAD_CF_OCCURRENCE_MATERIAL_READ)
    // And none to read, either.
    [[maybe_unused]] const auto occurrenceMaterial = component.material;
#endif

    return 0;
}
