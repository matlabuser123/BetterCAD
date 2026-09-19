#pragma once

#include <bettercad/assembly/Component.hpp>
#include <bettercad/assembly/Export.hpp>
#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>

#include <vector>

// The document-facing operations on components (P13-COMP-001).
//
// These are free functions rather than Document members because Document is
// core (layer 0) and assembly is layer 3: core must not know that assemblies
// exist (ADR-006). They use the ordinary document object lifecycle --
// addObject, findObjectAs, removeObject -- and add no registry, no allocator
// and no ownership of their own (ADR-002).
namespace bettercad {
class Document;
}

namespace bettercad::assembly {

/// Checks a definition against @p document: the part must be an object of
/// this document, and must be a kind that can be placed.
///
/// A part is anything whose regeneration produces a body -- in practice a
/// feature. A sketch, a datum, another component or a parameter is not a
/// part, and naming one is a modelling mistake rather than a regeneration
/// failure, so it is refused here rather than left to fail later.
///
/// Note what cannot be checked: an ObjectId carries no document identity, so
/// an ID taken from another document is either absent here (and refused as
/// unknown) or collides with a local object (and is indistinguishable from
/// it). That is not a gap in this function; it is why ADR-003 records that
/// cross-document references need a node identity wider than ObjectId.
[[nodiscard]] BETTERCAD_ASSEMBLY_EXPORT Result<void> checkComponent(const Document& document,
                                                                    const ComponentDefinition& definition);

/// Adds a component placing `definition.part`, and returns its ID.
///
/// Fails, changing nothing, if the definition is malformed, if the part is
/// not an object of this document, if it is not a placeable kind, or if the
/// name is taken. No ID is consumed by a failed call.
[[nodiscard]] BETTERCAD_ASSEMBLY_EXPORT Result<ComponentId> createComponent(Document& document, std::string name,
                                                                            const ComponentDefinition& definition);

/// Replaces the definition of the component with @p id, after checking it
/// against the document exactly as createComponent() does. Returns whether
/// anything changed.
///
/// This is the supported way to repoint or suppress a component. Reaching
/// past it -- Document::modifyObject with Component::setDefinition -- gets
/// only the definition's own validation, which cannot see the document and
/// so cannot tell a part from a sketch. Until components regenerate
/// (P13-REGEN-001) nothing downstream catches that, so the check lives
/// here, where the supported path runs it.
[[nodiscard]] BETTERCAD_ASSEMBLY_EXPORT Result<bool> setComponentDefinition(Document& document, ComponentId id,
                                                                            const ComponentDefinition& definition);

/// Moves the component with @p id to @p placement, leaving the rest of its
/// definition alone. Returns whether anything changed.
///
/// This is the supported way to place a component. It runs the same checks
/// as setComponentDefinition(), so a non-finite literal is refused and the
/// component keeps the placement it had.
///
/// The placement is intent, not a transform: what it means is computed by
/// placementOf(), and nothing stores the result (ADR-005).
[[nodiscard]] BETTERCAD_ASSEMBLY_EXPORT Result<bool> setComponentPlacement(Document& document, ComponentId id,
                                                                           const ComponentPlacement& placement);

/// The component with @p id, or nullptr if there is none of that ID or the
/// object of that ID is not a component.
[[nodiscard]] BETTERCAD_ASSEMBLY_EXPORT const Component* findComponent(const Document& document,
                                                                       ComponentId id) noexcept;

/// Every component in @p document, in ascending ID order.
///
/// Returns IDs rather than pointers or a container of objects: the document
/// owns the objects, and an ID stays valid across the mutations a caller is
/// likely to make while iterating.
[[nodiscard]] BETTERCAD_ASSEMBLY_EXPORT std::vector<ComponentId> components(const Document& document);

/// The components that place @p part, in ascending ID order. Empty if none
/// do, which is what makes it safe to delete the part.
[[nodiscard]] BETTERCAD_ASSEMBLY_EXPORT std::vector<ComponentId> componentsOf(const Document& document,
                                                                              ObjectId part);

/// Removes the component with @p id. Fails if there is no such component.
///
/// Removing a component removes only that placement: the part it named is
/// untouched, and other components of the same part are unaffected.
[[nodiscard]] BETTERCAD_ASSEMBLY_EXPORT Result<void> removeComponent(Document& document, ComponentId id);

} // namespace bettercad::assembly
