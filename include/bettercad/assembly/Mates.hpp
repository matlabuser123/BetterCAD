#pragma once

#include <bettercad/assembly/Export.hpp>
#include <bettercad/assembly/Mate.hpp>
#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>

#include <string>
#include <vector>

// The document-facing operations on mates (P13-MATE-001).
//
// Free functions, not Document members, for the reason ADR-006 gives: core
// must not know assemblies exist. They use the ordinary document object
// lifecycle and add no registry and no allocator of their own.
namespace bettercad {
class Document;
}

namespace bettercad::assembly {

/// Checks a definition against @p document: the components exist and are
/// components, every object the targets name exists, and a target that names
/// a face of a part names one of *that component's* part.
///
/// The last check is the one a definition cannot make for itself. A face is
/// named by the feature that generated it, so a face target on component A
/// whose feature belongs to component B's part is a modelling mistake, not a
/// resolution failure, and is refused here.
///
/// Reference geometry that belongs to no part -- a principal plane, a datum,
/// a coordinate system -- is document-level and is not subject to that
/// check: it is exactly the geometry ADR-004 prefers a mate to use.
[[nodiscard]] BETTERCAD_ASSEMBLY_EXPORT Result<void> checkMate(const Document& document,
                                                               const MateDefinition& definition);

/// Adds a mate, and returns its ID. Fails, changing nothing, if the
/// definition is malformed or does not check out against the document. No ID
/// is consumed by a failed call.
[[nodiscard]] BETTERCAD_ASSEMBLY_EXPORT Result<MateId> createMate(Document& document, std::string name,
                                                                  const MateDefinition& definition);

/// Replaces the definition of the mate with @p id, after checking it exactly
/// as createMate() does. Returns whether anything changed.
///
/// This is the supported way to edit a mate. Reaching past it with
/// Document::modifyObject gets only the definition's own validation, which
/// cannot see the document -- the same residual P13-COMP-001 recorded for
/// components.
[[nodiscard]] BETTERCAD_ASSEMBLY_EXPORT Result<bool> setMateDefinition(Document& document, MateId id,
                                                                       const MateDefinition& definition);

/// The mate with @p id, or nullptr if there is none of that ID or the object
/// of that ID is not a mate.
[[nodiscard]] BETTERCAD_ASSEMBLY_EXPORT const Mate* findMate(const Document& document, MateId id) noexcept;

/// Every mate in @p document, in ascending ID order.
[[nodiscard]] BETTERCAD_ASSEMBLY_EXPORT std::vector<MateId> mates(const Document& document);

/// The mates that name @p component, in ascending ID order. Empty if none
/// do, which is what makes it safe to delete the component.
[[nodiscard]] BETTERCAD_ASSEMBLY_EXPORT std::vector<MateId> matesOf(const Document& document, ComponentId component);

/// Removes the mate with @p id. Fails if there is no such mate. Removing a
/// mate removes only the constraint: the components it related are untouched.
[[nodiscard]] BETTERCAD_ASSEMBLY_EXPORT Result<void> removeMate(Document& document, MateId id);

/// A mate naming objects the document does not have, and which they are.
struct UnresolvedMate {
    MateId mate{};
    /// The objects the mate names that are gone, in the order it names them.
    std::vector<ObjectId> missing{};

    friend bool operator==(const UnresolvedMate&, const UnresolvedMate&) = default;
};

/// Every mate of @p document naming an object that is not there, ascending.
///
/// A mate whose target is deleted becomes unresolved and stays that mate: it
/// is never rebound to other geometry, and the same objects becoming
/// available again resolves it. The dependency graph reports the same
/// absence as a missing reference, which is what blocks the mate rather than
/// letting it look satisfied.
[[nodiscard]] BETTERCAD_ASSEMBLY_EXPORT std::vector<UnresolvedMate> unresolvedMates(const Document& document);

} // namespace bettercad::assembly
