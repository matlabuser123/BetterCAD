#include <bettercad/assembly/Components.hpp>

#include <bettercad/core/document/Document.hpp>
#include <bettercad/features/Feature.hpp>

#include <algorithm>
#include <format>
#include <utility>

namespace bettercad::assembly {

Result<void> checkComponent(const Document& document, const ComponentDefinition& definition) {
    if (auto valid = validate(definition); !valid) {
        return valid;
    }
    if (!isInternal(definition.part)) {
        // An external part cannot be checked here. The document that owns it
        // may not be available, and reaching for it behind the caller's back
        // is exactly the implicit filesystem access ADR-003 forbids. It is
        // checked when it is resolved, and a component whose part does not
        // resolve fails regeneration rather than looking fine.
        return {};
    }
    const DocumentObject* part = document.findObject(definition.part.object);
    if (part == nullptr) {
        return makeError(ErrorCode::NotFound,
                         std::format("a component cannot place {}, which is not an object of this document",
                                     definition.part.object));
    }
    // A part is something that produces a body. Naming a sketch, a datum or
    // another component is a modelling mistake, so it is refused now rather
    // than left to fail at regeneration with a less useful message.
    if (dynamic_cast<const features::SolidFeature*>(part) == nullptr) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("a component cannot place {} ('{}'): a {} produces no body",
                                     definition.part.object, part->name(), part->typeName()));
    }
    return {};
}

Result<ComponentId> createComponent(Document& document, std::string name, const ComponentDefinition& definition) {
    // Checked before anything is built, so a rejected component consumes no
    // ID and leaves the document untouched.
    if (auto valid = checkComponent(document, definition); !valid) {
        return std::unexpected(valid.error());
    }
    auto component = Component::create(std::move(name), definition);
    if (!component) {
        return std::unexpected(component.error());
    }
    auto id = document.addObject(std::move(*component));
    if (!id) {
        return std::unexpected(id.error());
    }
    return ComponentId::fromValue(id->value());
}

Result<bool> setComponentDefinition(Document& document, ComponentId id, const ComponentDefinition& definition) {
    if (findComponent(document, id) == nullptr) {
        return makeError(ErrorCode::NotFound, std::format("there is no component {}", id));
    }
    // A component may not place itself, and the graph's cycle detection is
    // too late to give a useful message.
    if (localTarget(definition.part) == std::optional<ObjectId>{ObjectId{id}}) {
        return makeError(ErrorCode::InvalidArgument, std::format("{} cannot place itself", id));
    }
    if (auto valid = checkComponent(document, definition); !valid) {
        return std::unexpected(valid.error());
    }
    auto changed = document.modifyObject<Component>(
        id, [&](Component& component) { return component.setDefinition(definition).value_or(false); });
    if (!changed) {
        return std::unexpected(changed.error());
    }
    return *changed;
}

Result<bool> setComponentPlacement(Document& document, ComponentId id, const ComponentPlacement& placement) {
    const Component* component = findComponent(document, id);
    if (component == nullptr) {
        return makeError(ErrorCode::NotFound, std::format("there is no component {}", id));
    }
    ComponentDefinition definition = component->definition();
    definition.placement = placement;
    // Through the checked path, so moving a component runs exactly the
    // checks that creating one does.
    return setComponentDefinition(document, id, definition);
}

const Component* findComponent(const Document& document, ComponentId id) noexcept {
    return document.findObjectAs<Component>(id);
}

std::vector<ComponentId> components(const Document& document) {
    std::vector<ComponentId> found;
    // objects() is ascending by ID, so the result is ordered without sorting.
    for (const DocumentObject& object : document.objects()) {
        if (dynamic_cast<const Component*>(&object) != nullptr) {
            found.push_back(ComponentId::fromValue(object.id().value()));
        }
    }
    return found;
}

std::vector<ComponentId> componentsOf(const Document& document, ObjectId part) {
    std::vector<ComponentId> found;
    for (const DocumentObject& object : document.objects()) {
        const auto* component = dynamic_cast<const Component*>(&object);
        if (component != nullptr && component->definition().part == part) {
            found.push_back(ComponentId::fromValue(object.id().value()));
        }
    }
    return found;
}

Result<void> removeComponent(Document& document, ComponentId id) {
    if (findComponent(document, id) == nullptr) {
        return makeError(ErrorCode::NotFound, std::format("there is no component {}", id));
    }
    auto removed = document.removeObject(id);
    if (!removed) {
        return std::unexpected(removed.error());
    }
    return {};
}

} // namespace bettercad::assembly
