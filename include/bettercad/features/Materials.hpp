#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/materials/MaterialLibrary.hpp>
#include <bettercad/features/Export.hpp>
#include <bettercad/features/Material.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

// The document-facing operations on materials (P15-MAT-001).
//
// Free functions rather than Document members, because Document is core
// (layer 0) and features is layer 2: core must not know that materials exist.
// They use the ordinary document object lifecycle -- addObject, findObjectAs,
// removeObject -- and add no registry, no allocator and no ownership of their
// own, exactly as drawing::createSheet and assembly::createComponent do. The
// document's one IdAllocator is therefore what guarantees a MaterialId is never
// reused.
namespace bettercad {
class Document;
}

namespace bettercad::features {

/// Adds a material the user is defining themselves and returns its ID.
///
/// @p name follows the object identifier rule -- letters, digits and '_', not
/// starting with a digit -- and must not already be taken, because object names
/// are unique across a document. An engineering label like
/// "Aluminium 6061-T6" is not a legal name and belongs in
/// MaterialDefinition::designation; Document::uniqueName() exists for the case
/// where a sensible name is already in use.
///
/// Fails, changing nothing, if the definition is malformed or the name is
/// invalid or taken. No ID is consumed by a failed call, so a rejected create
/// does not perturb the IDs a later one hands out.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<MaterialId>
createMaterial(Document& document, std::string name, const MaterialDefinition& definition = {});

/// Imports a built-in library entry into @p document as a new material, and
/// returns its ID (ADR-025).
///
/// This COPIES the entry: the new material's metadata is its own, and the
/// entry's key and revision are recorded as its origin so that provenance can
/// answer where it came from. Nothing afterwards consults the library on the
/// document's behalf -- not at load, not while solving -- so a library
/// correction, a library addition or a missing library cannot change what this
/// document says. Two imports of one entry produce two materials with two IDs.
///
/// The library entry is unaffected, and cannot be otherwise: it is a compiled-in
/// constant.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<MaterialId>
importLibraryMaterial(Document& document, std::string name,
                      const materials::LibraryMaterial& entry);

/// Replaces the definition of the material with @p id. Returns whether anything
/// changed, so the document only bumps the revision on an effective change.
///
/// This is the supported way to change a designation, standard, family, notes or
/// origin. It cannot change identity: @p id names the material and is not part
/// of what is replaced. On failure the material keeps the definition it had.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<bool>
setMaterialDefinition(Document& document, MaterialId id, const MaterialDefinition& definition);

/// The material with @p id, or nullptr if there is none of that ID or the object
/// of that ID is not a material.
///
/// nullptr is the whole answer for a material that is not there. There is no
/// fallback to a default material, to the first material, or to anything that
/// happens to be named similarly: a MaterialId that does not resolve must stay
/// unresolved, or a stale reference silently becomes a different material.
///
/// The pointer stays valid until that material is removed from the document.
/// Adding or removing OTHER objects does not invalidate it -- the document holds
/// its objects in a node-based map, by unique_ptr -- so a caller may hold it
/// across unrelated edits.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT const Material* findMaterial(const Document& document,
                                                                     MaterialId id) noexcept;

/// Every material in @p document, in ascending ID order.
///
/// Ascending ID, from the document's ordered object map, so the order is the
/// same in every build and on every run and does not come from any container's
/// iteration order. It is presentation only: a material's identity is its
/// MaterialId, never its position here.
///
/// Named materialIds() rather than materials() because `materials` is the
/// namespace holding the library, and a function of that name in this namespace
/// would hide it.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT std::vector<MaterialId> materialIds(
    const Document& document);

/// How many materials @p document has.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT std::size_t materialCount(const Document& document);

/// Every material in @p document whose designation is exactly @p designation, in
/// ascending ID order.
///
/// Returns ALL of them, because a designation is not identity and duplicates are
/// legitimate: a document may hold two materials designated "Steel" with
/// different values. A caller that finds two has an ambiguity to resolve or to
/// show the user; it is never resolved here by picking one.
///
/// The comparison is exact. "Steel" does not match "steel" and does not match
/// " Steel": case-folding or trimming would quietly merge materials a user meant
/// to keep apart.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT std::vector<MaterialId>
findMaterialsByDesignation(const Document& document, std::string_view designation);

/// Removes the material with @p id. Fails if there is no such material.
///
/// The ID is not recycled: the document's allocator only ever counts up, so the
/// removed ID is never handed to a later material and a reference kept to it
/// stays unresolved for the life of the document. That is what makes it safe for
/// a later milestone's assignment to hold a MaterialId.
///
/// This milestone has nothing that refers to a material, so there is no
/// reference-integrity check to make here. When assignments exist
/// (P15-ASSIGN-001) the contract is that removing an assigned material is the
/// assignment's problem to report, not a reason for this to silently leave the
/// material in place.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<void> removeMaterial(Document& document,
                                                                    MaterialId id);

} // namespace bettercad::features
