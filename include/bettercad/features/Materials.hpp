#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/materials/MaterialLibrary.hpp>
#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/core/materials/ThermalProperties.hpp>
#include <bettercad/features/Export.hpp>
#include <bettercad/features/Material.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
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

/// Replaces only the mechanical properties of the material with @p id, leaving
/// its name, designation, standard, family, notes and origin alone. Returns
/// whether anything changed.
///
/// Fails, changing nothing, if a property is outside its physical range or is
/// not finite; the diagnostic lists every problem, not the first. Properties
/// that are Unknown are never a reason to fail.
///
/// It cannot change identity. @p id names the material and is not part of what
/// is replaced, so editing a Young's modulus leaves the MaterialId exactly as it
/// was.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<bool>
setMaterialMechanical(Document& document, MaterialId id,
                      const materials::MechanicalProperties& properties);

/// The four isotropic elastic constants of the material with @p id, all
/// concrete: E and nu as supplied, G and K derived from them (ADR-027).
///
/// This is the boundary a structural solver consumes (P17). It hands over a
/// complete set or it fails -- never a partial one, and never a fabricated
/// default. There is no `nu = 0.3` fallback anywhere behind it.
///
/// The diagnostic names the material, then EVERY input that is missing or out of
/// range, so a run fails once with the whole list rather than at the first gap.
/// It names the missing INPUT, not the derived constant: told that the shear
/// modulus is unavailable, a user has nothing to do about it; told that the
/// Poisson ratio is unknown, they do.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<materials::LinearElasticConstants>
requireLinearElasticConstants(const Document& document, MaterialId id);

/// The density of the material with @p id.
///
/// Separate from requireLinearElasticConstants() because the consumers are
/// different: a linear-elastic stiffness needs E and nu and not density, while a
/// mass needs density and neither of the others. One combined requirement would
/// fail a stiffness calculation over a missing density it never uses.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<Density> requireDensity(const Document& document,
                                                                      MaterialId id);

/// Replaces only the thermal properties of the material with @p id, leaving its
/// name, metadata and MECHANICAL properties alone. Returns whether anything
/// changed.
///
/// Fails, changing nothing, if a property is outside its physical range or is not
/// finite; the diagnostic lists every problem. It cannot change identity, and it
/// cannot disturb the mechanical half -- including the density, which lives
/// there and which thermal consumers read from there.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<bool>
setMaterialThermal(Document& document, MaterialId id,
                   const materials::ThermalProperties& properties);

/// What steady-state conduction needs from a material: its thermal conductivity.
///
/// A future Fourier-law consumer wants `k` for `q = -k grad(T)` and nothing else
/// from the material, so this asks for nothing else. Fails naming the material
/// and the missing property; never returns a default conductivity.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<ThermalConductivity>
requireThermalConductivity(const Document& document, MaterialId id);

/// What transient conduction needs: density, specific heat capacity and thermal
/// conductivity, all concrete.
///
/// The density comes from the material's ONE density property, which lives with
/// the mechanical properties -- this is the join, and the reason there is no
/// second density beside it.
///
/// Fails with one diagnostic naming the material and EVERY missing input, so a
/// run fails once with the whole list rather than at the first gap.
struct TransientConductionProperties {
    Density density{};
    SpecificHeatCapacity specificHeatCapacity{};
    ThermalConductivity thermalConductivity{};

    friend bool operator==(const TransientConductionProperties&,
                           const TransientConductionProperties&) = default;
};

[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<TransientConductionProperties>
requireTransientConductionProperties(const Document& document, MaterialId id);

/// The thermal expansion coefficient of the material with @p id.
///
/// What a thermo-mechanical consumer needs to turn a temperature change into a
/// strain. Separate from the conduction requirements, because expansion needs no
/// conductivity and conduction needs no expansion.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<ThermalExpansionCoefficient>
requireThermalExpansion(const Document& document, MaterialId id);

// --- Material assignment (P15-ASSIGN-001, ADR-026) --------------------------
//
// A material assignment is document-level INTENT: one optional MaterialId for
// the part, held by the Document like the active configuration. There is no
// Part document object and no body document object to hang it on -- a part IS a
// document -- and ADR-026 rejected an assignment object because there is exactly
// one of them and nothing references it.
//
// What follows from that, and is not hidden:
//   * ONE material per document. Per-body materials are deferred, and the
//     mechanism is named: a body's only persistent handle is its producing
//     feature's ObjectId, so a later override can be keyed by that.
//   * NO assembly occurrence override. ComponentDefinition has no material
//     field, so an occurrence cannot differ from its part, and a compile-fail
//     case proves the field is absent rather than merely unused. It needs
//     external part references (ADR-003), which are deferred.
//   * NO configuration dependence. A configuration overrides free PARAMETER
//     values; a MaterialId is not one. Switching configurations, and suppressing
//     a component, leave the assignment exactly as it is.
//   * NO inheritance, so the effective material IS the direct assignment. There
//     is no fallback layer and no default material anywhere.

/// What state a document's material assignment is in.
///
/// The last three are different answers and are never collapsed into one. This
/// mirrors drawing::ResolutionState, which cannot be reused directly because it
/// is layer 4 and this is layer 2; the meanings are deliberately the same.
enum class MaterialAssignmentState : std::uint8_t {
    /// Nothing was assigned. A normal resting state, not a fault.
    Unassigned,
    /// The assigned material exists and was found.
    Resolved,
    /// An assignment exists and its material is not there NOW -- deleted. The
    /// INTENT is kept, unchanged, so it resolves again if that exact material
    /// comes back. Nothing rewrites it to point at something else.
    Unresolved,
    /// The assignment names an object that exists but is not a material. Only
    /// reachable through Document::setMaterialAssignment(), which does not
    /// validate; features::assignMaterial() refuses to create one.
    Invalid,
};

/// "unassigned", "resolved", "unresolved" or "invalid".
[[nodiscard]] BETTERCAD_FEATURES_EXPORT std::string_view
toString(MaterialAssignmentState state) noexcept;

/// A document's material assignment and what became of it.
struct MaterialAssignment {
    MaterialAssignmentState state = MaterialAssignmentState::Unassigned;
    /// What the document names. Kept even when Unresolved or Invalid, because
    /// the intent is what makes recovery possible; std::nullopt only when
    /// Unassigned.
    std::optional<MaterialId> material{};
    /// Empty when Unassigned or Resolved; otherwise why not, naming the ID.
    std::string diagnostic{};

    [[nodiscard]] bool resolved() const noexcept {
        return state == MaterialAssignmentState::Resolved;
    }

    friend bool operator==(const MaterialAssignment&, const MaterialAssignment&) = default;
};

/// Assigns the material with @p id to @p document. Returns whether anything
/// changed.
///
/// Fails, changing nothing, unless @p id names a material IN THIS DOCUMENT. It
/// never creates a material, and it never allocates an ID: an assignment is a
/// reference, so assigning cannot disturb identity.
///
/// A MaterialId from another document is not a cross-document reference and is
/// not treated as one. IDs are document-local throughout BetterCAD, so such an
/// ID either names this document's own material of that number or names nothing;
/// either way the result is about THIS document. A real cross-document
/// assignment needs an ObjectReference carrying a DocumentId, which ADR-003
/// defers.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<bool> assignMaterial(Document& document,
                                                                    MaterialId id);

/// Removes the assignment, leaving the document with no material. Returns
/// whether anything changed; removing when there was none changes nothing and is
/// not an error.
///
/// This is the only way an assignment goes away. Deleting the material does NOT
/// remove it -- that leaves it Unresolved, with the intent intact.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<bool> removeMaterialAssignment(Document& document);

/// The assignment and its state.
///
/// Resolution happens here and nowhere else, so there is one answer to "what is
/// this part made of". Nothing in it resolves by designation, by name, by
/// position, or by similarity of content.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT MaterialAssignment
materialAssignment(const Document& document);

/// The material @p document is made of, or nullptr when there is none.
///
/// ADR-027 names this call, so a downstream solver finds what the architecture
/// promised. With no inheritance in P15 it is the direct assignment; the name is
/// the one that survives if inheritance is ever added.
///
/// nullptr covers Unassigned, Unresolved and Invalid alike. A caller that needs
/// to tell them apart asks materialAssignment(); a caller that needs to FAIL
/// with a reason asks requireEffectiveMaterial().
[[nodiscard]] BETTERCAD_FEATURES_EXPORT const Material* effectiveMaterial(
    const Document& document);

/// The material @p document is made of, or an error saying which problem it is.
///
/// What P15-MASS-001, P17 and P18 consume: a mass request on a part with no
/// material is a diagnostic naming the part, never a zero (ADR-026). The
/// diagnostic distinguishes "nothing was assigned" from "the assigned material
/// is gone", because those need different things from the user. Never nullptr on
/// success.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<const Material*>
requireEffectiveMaterial(const Document& document);

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
