#include <bettercad/assembly/Configurations.hpp>

#include <bettercad/assembly/Component.hpp>
#include <bettercad/assembly/Components.hpp>
#include <bettercad/assembly/Mate.hpp>
#include <bettercad/assembly/Mates.hpp>
#include <bettercad/core/document/Document.hpp>

#include <format>
#include <optional>

namespace bettercad::assembly {
namespace {

/// The base state with the active configuration's override applied, which is
/// the whole of ADR-007's semantics in one line.
template <typename ObjectT, typename IdT>
[[nodiscard]] bool effectiveSuppression(const Document& document, IdT id) noexcept {
    const ObjectT* object = document.findObjectAs<ObjectT>(id);
    if (object == nullptr) {
        return false;
    }
    if (const std::optional<bool> override = document.configurations().activeSuppressionFor(id)) {
        return *override;
    }
    return object->definition().suppressed;
}

} // namespace

bool isComponentSuppressed(const Document& document, ComponentId id) noexcept {
    return effectiveSuppression<Component>(document, id);
}

bool isMateSuppressed(const Document& document, MateId id) noexcept {
    return effectiveSuppression<Mate>(document, id);
}

std::vector<ComponentId> activeComponents(const Document& document) {
    std::vector<ComponentId> found;
    for (const ComponentId id : components(document)) {
        if (!isComponentSuppressed(document, id)) {
            found.push_back(id);
        }
    }
    return found;
}

bool isMateActive(const Document& document, MateId id) noexcept {
    const Mate* mate = findMate(document, id);
    if (mate == nullptr || isMateSuppressed(document, id)) {
        return false;
    }
    const MateDefinition& d = mate->definition();
    // A Fixed mate holds one component; every other kind names two pieces of
    // geometry, each on a component of its own.
    if (d.component.isValid()) {
        return !isComponentSuppressed(document, d.component);
    }
    for (const std::optional<MateTarget>& target : {d.a, d.b, d.a2, d.b2}) {
        if (target && isComponentSuppressed(document, target->component)) {
            return false;
        }
    }
    return true;
}

std::vector<MateId> activeMates(const Document& document) {
    std::vector<MateId> found;
    for (const MateId id : mates(document)) {
        if (isMateActive(document, id)) {
            found.push_back(id);
        }
    }
    return found;
}

Result<bool> suppressComponent(Document& document, ConfigurationId configuration, ComponentId component,
                               bool suppressed) {
    if (findComponent(document, component) == nullptr) {
        return makeError(ErrorCode::NotFound,
                         std::format("{} is not a component of this document", component));
    }
    return document.setConfigurationSuppression(configuration, component, suppressed);
}

Result<bool> suppressMate(Document& document, ConfigurationId configuration, MateId mate, bool suppressed) {
    if (findMate(document, mate) == nullptr) {
        return makeError(ErrorCode::NotFound, std::format("{} is not a mate of this document", mate));
    }
    return document.setConfigurationSuppression(configuration, mate, suppressed);
}

Result<bool> clearComponentSuppression(Document& document, ConfigurationId configuration,
                                       ComponentId component) {
    return document.clearConfigurationSuppression(configuration, component);
}

Result<bool> clearMateSuppression(Document& document, ConfigurationId configuration, MateId mate) {
    return document.clearConfigurationSuppression(configuration, mate);
}

} // namespace bettercad::assembly
