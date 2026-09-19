#include <bettercad/assembly/Resolution.hpp>

#include <bettercad/assembly/Component.hpp>
#include <bettercad/assembly/Components.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/geometry/Body.hpp>
#include <bettercad/features/Feature.hpp>
#include <bettercad/features/Regenerator.hpp>

#include <format>
#include <optional>
#include <string>
#include <utility>

namespace bettercad::assembly {
namespace {

/// How a reference reads in a message: the object, and the document when it
/// is somewhere else.
std::string describe(const ObjectReference& reference) {
    if (isInternal(reference)) {
        return std::format("{}", reference.object);
    }
    return std::format("{} of document {}", reference.object, reference.document->value());
}

} // namespace

Result<const features::SolidFeature*> resolvePart(const Document& document, const ObjectReference& reference,
                                                  const ReferenceResolver* resolver) {
    if (auto valid = validate(reference); !valid) {
        return std::unexpected(valid.error());
    }
    const ResolvedReference found = resolve(reference, document, resolver);
    if (!found.resolved()) {
        return makeError(ErrorCode::NotFound,
                         std::format("a component cannot place {}: {}", describe(reference), toString(found.state)));
    }
    const auto* part = dynamic_cast<const features::SolidFeature*>(found.object);
    if (part == nullptr) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("a component cannot place {} ('{}'): a {} produces no body",
                                     describe(reference), found.object->name(), found.object->typeName()));
    }
    return part;
}

Result<const features::SolidFeature*> resolveComponentPart(const Document& document, ComponentId id,
                                                            const ReferenceResolver* resolver) {
    const Component* component = findComponent(document, id);
    if (component == nullptr) {
        return makeError(ErrorCode::NotFound, std::format("there is no component {}", id));
    }
    return resolvePart(document, component->definition().part, resolver);
}

std::vector<UnresolvedComponent> unresolvedComponents(const Document& document, const ReferenceResolver* resolver) {
    std::vector<UnresolvedComponent> found;
    for (const ComponentId id : components(document)) {
        const Component* component = findComponent(document, id);
        if (component == nullptr) {
            continue;
        }
        const ResolvedReference state = resolve(component->definition().part, document, resolver);
        if (!state.resolved()) {
            found.push_back({.component = id, .state = state.state});
        }
    }
    return found;
}

void registerHandlers(features::Regenerator& regenerator, const ReferenceResolver* resolver) {
    regenerator.registerHandler(
        std::string{Component::kTypeName},
        [resolver](Document& document, ObjectId object,
                   const features::Regenerator&) -> Result<std::optional<geometry::Body>> {
            const Component* component = findComponent(document, ComponentId::fromValue(object.value()));
            if (component == nullptr) {
                return makeError(ErrorCode::Internal, std::format("{} is not a component", object));
            }
            // The whole point of the handler: resolving is what turns an
            // unresolvable part into a reported failure instead of silence.
            auto part = resolvePart(document, component->definition().part, resolver);
            if (!part) {
                return std::unexpected(part.error());
            }
            // A component owns no geometry. The part's body is built by the
            // part's own features, exactly once, however many components
            // place it.
            return std::nullopt;
        });
}

} // namespace bettercad::assembly
