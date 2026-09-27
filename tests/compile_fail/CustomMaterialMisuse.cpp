// Build-failure tests for custom materials (see CMakeLists.txt in this
// directory). Built once without any BETTERCAD_CF_* macro as a control, which
// must compile.
//
// Two groups, and most of them prove an ABSENCE rather than a rejection.
//
// The first group is LIBRARY IMMUTABILITY. A runtime test can only show that a
// library entry did not change; these show that it cannot. The entries are
// `static constexpr` with no setters and only const accessors, so every mutation
// path a caller might reach for has to fail to compile -- and a milestone that
// added a setter "for convenience" would be caught here rather than by a reviewer
// noticing.
//
// The second group is THE ABSENCE OF AN OVERRIDE MODEL. ADR-025 chose snapshot
// semantics: a document owns its values and never consults the library again. So
// there is no base reference, no inherited state and no per-property override map,
// and these cases prove those are unrepresentable rather than merely unused. That
// matters because "sometimes copied, sometimes inherited, without explicit
// representation" is the failure mode P15-CUSTOM-001 exists to design out.
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/materials/MaterialLibrary.hpp>
#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>

using namespace bettercad;
using namespace bettercad::literals;

int main() {
    Document document{"Part"};

    // The control: the intended way to say all of this.
    const Result<materials::LibraryMaterial> entry =
        materials::findLibraryMaterial("bettercad", "al-6061-t6");
    if (!entry) {
        return 1;
    }
    const Result<MaterialId> imported =
        features::importLibraryMaterial(document, "Aluminium", *entry);
    if (!imported) {
        return 1;
    }
    [[maybe_unused]] const Result<MaterialId> cloned =
        features::cloneMaterial(document, *imported, "Copy");
    [[maybe_unused]] const Result<bool> removed = features::removeMaterialProperty(
        document, *imported, materials::MechanicalPropertyKind::Density);
    [[maybe_unused]] const bool has = features::hasMaterialProperty(
        document, *imported, materials::MechanicalPropertyKind::Density);
    const features::Material* material = features::findMaterial(document, *imported);
    if (material == nullptr) {
        return 1;
    }
    [[maybe_unused]] features::MaterialDefinition copy = material->definition();

#if defined(BETTERCAD_CF_LIBRARY_ENTRY_HAS_NO_SETTER)
    // No setter of any kind on a library entry. The engineering label is the one a
    // "just fix the spelling" change would reach for first.
    entry->setDesignation("Something Else");
#elif defined(BETTERCAD_CF_LIBRARY_ENTRY_FIELDS_ARE_PRIVATE)
    // Nor can the storage be reached directly.
    [[maybe_unused]] const auto peek = entry->designation_;
#elif defined(BETTERCAD_CF_LIBRARY_TABLE_IS_CONST)
    // builtInMaterials() hands out a span of CONST entries, so no mutable
    // reference into the table can be bound -- which is what an iterator or
    // subscript attack would need.
    materials::LibraryMaterial& mutableEntry = materials::builtInMaterials()[0];
    (void)mutableEntry;
#elif defined(BETTERCAD_CF_LIBRARY_ENTRY_IS_NOT_A_DEFINITION)
    // A library entry is not a MaterialDefinition and does not convert to one, let
    // alone to a mutable reference to one. Importing is the only route, and it
    // copies (ADR-025).
    features::MaterialDefinition& asDefinition = *entry;
    (void)asDefinition;
#elif defined(BETTERCAD_CF_LIBRARY_ENTRY_IS_NOT_A_DOCUMENT_OBJECT)
    // A library entry has no ObjectId and is not a document object, so it cannot
    // be put into a document as one. That absence is what makes "the library
    // cannot be edited through the document" structural.
    [[maybe_unused]] const auto added = document.addObject(*entry);
#elif defined(BETTERCAD_CF_LIBRARY_ENTRY_HAS_NO_PROPERTIES)
    // The built-in entries carry METADATA ONLY (ADR-028: a value needs a recorded
    // source). There is no property storage on a LibraryMaterial at all, so a
    // caller cannot read a density out of the library and cannot be tempted to.
    [[maybe_unused]] const auto density = entry->mechanical();
#elif defined(BETTERCAD_CF_FOUND_MATERIAL_IS_CONST)
    // findMaterial returns a CONST pointer. Editing goes through the document's
    // own modifyObject path, which is what bumps the revision, so a caller cannot
    // mutate a material behind the document's back.
    material->setDefinition(features::MaterialDefinition{});
#elif defined(BETTERCAD_CF_DEFINITION_HAS_NO_ID)
    // THE COPY TRAP, closed. A MaterialDefinition carries no identity -- the ID
    // lives on DocumentObject -- so an ordinary C++ copy of a definition cannot
    // duplicate a MaterialId. There is no field to copy.
    [[maybe_unused]] const auto stolen = copy.id;
#elif defined(BETTERCAD_CF_DEFINITION_HAS_NO_MATERIAL_ID)
    [[maybe_unused]] const auto stolen = copy.materialId;
#elif defined(BETTERCAD_CF_NO_BASE_MATERIAL_REFERENCE)
    // ADR-025 chose snapshots. A document material has no base it inherits from,
    // so there is nothing to ask for -- and therefore no way for some properties
    // to be copied while others silently come from elsewhere.
    [[maybe_unused]] const auto base = copy.baseMaterial;
#elif defined(BETTERCAD_CF_NO_OVERRIDE_MAP)
    // And no per-property override map, for the same reason.
    [[maybe_unused]] const auto overrides = copy.overrides;
#elif defined(BETTERCAD_CF_PROPERTY_IS_NEVER_INHERITED)
    // A property is Known, Unknown or Derived (ADR-027). `Inherited` is not a
    // state it can be in, so no caller has to handle one and no diagnostic has to
    // explain one.
    [[maybe_unused]] const bool inherited = copy.mechanical.density.isInherited();
#elif defined(BETTERCAD_CF_NO_INHERITED_PROPERTY_STATE)
    [[maybe_unused]] const auto state = materials::PropertyState::Inherited;
#elif defined(BETTERCAD_CF_DEFINITION_HAS_NO_MASS)
    // DERIVED DATA STAYS DERIVED. A material's canonical state is engineering
    // intent, so a mass -- which needs a geometry as well as a density -- has no
    // place in it, and cloning a material therefore cannot carry a stale one.
    [[maybe_unused]] const auto mass = copy.mass;
#elif defined(BETTERCAD_CF_DEFINITION_HAS_NO_VOLUME)
    [[maybe_unused]] const auto volume = copy.volume;
#elif defined(BETTERCAD_CF_MECHANICAL_HAS_NO_SHEAR_MODULUS_SLOT)
    // Nor is there a slot for a SUPPLIED shear modulus (ADR-027). G is exactly
    // determined by E and nu, so storing one would be a second source of truth that
    // could disagree with the first -- which is why "a supplied G silently
    // overwritten by an E edit" is unrepresentable here rather than merely
    // untested.
    copy.mechanical.shearModulus = {};
#elif defined(BETTERCAD_CF_MECHANICAL_HAS_NO_BULK_MODULUS_SLOT)
    copy.mechanical.bulkModulus = {};
#elif defined(BETTERCAD_CF_REMOVE_TAKES_A_TYPED_KIND)
    // Removal is keyed by the property KIND enumeration, not by an integer that a
    // loop counter could wander into.
    [[maybe_unused]] const Result<bool> wrong =
        features::removeMaterialProperty(document, *imported, 0);
#elif defined(BETTERCAD_CF_CLONE_TAKES_A_MATERIAL_ID)
    // A MaterialId widens TO ObjectId, never back: an object is not a material
    // just because it has an ID, so a bare ObjectId cannot be cloned as one.
    [[maybe_unused]] const Result<MaterialId> wrong =
        features::cloneMaterial(document, ObjectId::fromValue(1), "Copy");
#elif defined(BETTERCAD_CF_CLONE_DOES_NOT_TAKE_A_LIBRARY_ENTRY)
    // Cloning is document-to-document. A library entry is imported, not cloned,
    // and the two are different operations with different provenance.
    [[maybe_unused]] const Result<MaterialId> wrong =
        features::cloneMaterial(document, *entry, "Copy");
#endif

    return 0;
}
