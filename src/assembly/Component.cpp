#include <bettercad/assembly/Component.hpp>

#include <format>
#include <utility>

namespace bettercad::assembly {

Result<void> validate(const ComponentDefinition& definition) {
    if (!definition.part.object.isValid()) {
        return makeError(ErrorCode::InvalidArgument, "a component must name the part it places");
    }
    if (auto valid = validate(definition.part); !valid) {
        return valid;
    }
    // Only the literals can be checked here. A component of the placement
    // driven by a parameter is checked when the parameter is read, by
    // placementOf().
    if (auto valid = validate(definition.placement); !valid) {
        return valid;
    }
    return {};
}

Component::Component(std::string name, const ComponentDefinition& definition)
    : DocumentObject(std::move(name)), definition_(definition) {}

Result<std::unique_ptr<Component>> Component::create(std::string name, const ComponentDefinition& definition) {
    if (auto valid = validate(definition); !valid) {
        return std::unexpected(valid.error());
    }
    return std::unique_ptr<Component>(new Component(std::move(name), definition));
}

std::unique_ptr<DocumentObject> Component::clone() const {
    return std::unique_ptr<Component>(new Component(*this));
}

bool Component::contentEquals(const DocumentObject& other) const {
    const auto* component = dynamic_cast<const Component*>(&other);
    return component != nullptr && component->definition_ == definition_;
}

std::vector<ObjectId> Component::dependencies() const {
    // The part, and every parameter the placement is driven by, so that
    // editing one dirties this component and moves it. A suppressed
    // component keeps its edges: it still names its part, and unsuppressing
    // must not need a rebuild of the graph.
    //
    // An external part contributes no edge: an ObjectId cannot name a node in
    // another document, and a graph that pretended otherwise would be wrong
    // (ADR-003). Such a component is failed by the regeneration handler
    // instead, so the absence of an edge is never mistaken for nothing to do.
    std::vector<ObjectId> result;
    if (const auto local = localTarget(definition_.part)) {
        result.push_back(*local);
    }
    for (const ParameterId parameter : referencedParameters(definition_.placement)) {
        result.push_back(ObjectId{parameter});
    }
    return result;
}

Result<bool> Component::setDefinition(const ComponentDefinition& definition) {
    if (auto valid = validate(definition); !valid) {
        return std::unexpected(valid.error());
    }
    if (definition == definition_) {
        return false;
    }
    definition_ = definition;
    return true;
}

} // namespace bettercad::assembly
