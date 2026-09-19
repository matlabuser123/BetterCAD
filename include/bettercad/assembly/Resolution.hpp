#pragma once

#include <bettercad/assembly/Export.hpp>
#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/ObjectReference.hpp>
#include <bettercad/core/document/ReferenceResolver.hpp>

#include <vector>

// Resolving a component's part, and reporting the ones that do not resolve
// (P13-REF-001).
//
// core resolves a reference to an object; this resolves it to a PART, which
// means also deciding whether the object it found is a kind that can be
// placed. That check needs features::SolidFeature, so it lives here rather
// than in core -- the split ADR-006 draws between the reference vocabulary
// and the resolution that knows what the kinds mean.
namespace bettercad {
class Document;
}

namespace bettercad::features {
class Regenerator;
class SolidFeature;
} // namespace bettercad::features

namespace bettercad::assembly {

/// The part @p reference names, resolved through @p resolver and checked for
/// a kind that produces a body.
///
/// Fails with NotFound when the reference does not resolve -- the message
/// names which of the four states it ended in -- and with InvalidArgument
/// when it resolves to something that is not a part. An internal reference
/// never consults the resolver.
[[nodiscard]] BETTERCAD_ASSEMBLY_EXPORT Result<const features::SolidFeature*>
resolvePart(const Document& document, const ObjectReference& reference, const ReferenceResolver* resolver = nullptr);

/// The part the component with @p id places. NotFound if there is no such
/// component.
[[nodiscard]] BETTERCAD_ASSEMBLY_EXPORT Result<const features::SolidFeature*>
resolveComponentPart(const Document& document, ComponentId id, const ReferenceResolver* resolver = nullptr);

/// A component whose part does not resolve, and why.
struct UnresolvedComponent {
    ComponentId component{};
    ReferenceState state = ReferenceState::DocumentUnavailable;

    friend bool operator==(const UnresolvedComponent&, const UnresolvedComponent&) = default;
};

/// Every component of @p document whose part does not resolve, in ascending
/// ID order.
///
/// This is how a document reports its unresolved references rather than
/// refusing to open: load it, ask, and show the answer. A component with an
/// internal part appears here only if that object is missing.
[[nodiscard]] BETTERCAD_ASSEMBLY_EXPORT std::vector<UnresolvedComponent>
unresolvedComponents(const Document& document, const ReferenceResolver* resolver = nullptr);

/// Registers the assembly regeneration handlers on @p regenerator, capturing
/// @p resolver for external references.
///
/// `features` does not know assemblies exist; assembly registers its own
/// handlers through the public interface, which is what makes its layer
/// workable (ADR-006).
///
/// The component handler exists to stop an unresolvable part being silent.
/// An external part is not a dependency edge, so the graph reports nothing
/// missing and a handler-less component would regenerate as if all were
/// well. With the handler, a component whose part does not resolve **fails**,
/// and its state says which of the four reasons it was.
BETTERCAD_ASSEMBLY_EXPORT void registerHandlers(features::Regenerator& regenerator,
                                                const ReferenceResolver* resolver = nullptr);

} // namespace bettercad::assembly
