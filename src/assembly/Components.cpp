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
    const DocumentObject* part = document.findObject(definition.part);
    if (part == nullptr) {
        return makeError(ErrorCode::NotFound,
                         std::format("a component cannot place {}, which is not an object of this document",
                                     definition.part));
    }
    // A part is something that produces a body. Naming a sketch, a datum or
    // another component is a modelling mistake, so it is refused now rather
    // than left to fail at regeneration with a less useful message.
    if (dynamic_cast<const features::SolidFeature*>(part) == nullptr) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("a component cannot place {} ('{}'): a {} produces no body",
                                     definition.part, part->name(), part->typeName()));
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
    if (definition.part == ObjectId{id}) {
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
