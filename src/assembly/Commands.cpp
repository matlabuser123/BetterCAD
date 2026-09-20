#include <bettercad/assembly/Commands.hpp>

#include <bettercad/assembly/Components.hpp>
#include <bettercad/assembly/Configurations.hpp>
#include <bettercad/assembly/Mates.hpp>
#include <bettercad/core/document/Document.hpp>

#include <format>
#include <utility>

namespace bettercad::assembly {
namespace {

[[nodiscard]] std::unexpected<Error> notExecuted(std::string_view what) {
    return makeError(ErrorCode::FailedPrecondition,
                     std::format("cannot {} a command that has not been executed", what));
}

/// Undo of a creation: take the object out and keep it, so that redo can put
/// the SAME one back. Restoring by ID is what lets a mate that named it
/// before an undo still name it after the redo.
[[nodiscard]] Result<std::unique_ptr<DocumentObject>> takeBack(Document& document, ObjectId id) {
    auto removed = document.removeObject(id);
    if (!removed) {
        return std::unexpected(removed.error());
    }
    return std::move(*removed);
}

} // namespace

// --- CreateComponentCommand ---------------------------------------------------------------------

CreateComponentCommand::CreateComponentCommand(std::string name, ComponentDefinition definition)
    : name_(std::move(name)), definition_(std::move(definition)) {}

std::string CreateComponentCommand::description() const {
    return std::format("Create component '{}'", name_);
}

Result<void> CreateComponentCommand::execute(Document& document) {
    auto id = createComponent(document, name_, definition_);
    if (!id) {
        return std::unexpected(id.error());
    }
    id_ = *id;
    return {};
}

Result<void> CreateComponentCommand::undo(Document& document) {
    if (!id_.isValid()) {
        return notExecuted("undo");
    }
    auto removed = takeBack(document, ObjectId{id_});
    if (!removed) {
        return std::unexpected(removed.error());
    }
    kept_ = std::move(*removed);
    return {};
}

Result<void> CreateComponentCommand::redo(Document& document) {
    if (kept_ == nullptr) {
        return notExecuted("redo");
    }
    if (auto inserted = document.insertObject(kept_->clone()); !inserted) {
        return inserted;
    }
    kept_.reset();
    return {};
}

// --- SetComponentPlacementCommand ---------------------------------------------------------------

SetComponentPlacementCommand::SetComponentPlacementCommand(ComponentId component,
                                                           ComponentPlacement placement)
    : component_(component), placement_(std::move(placement)) {}

std::string SetComponentPlacementCommand::description() const {
    return std::format("Move {}", component_);
}

Result<void> SetComponentPlacementCommand::execute(Document& document) {
    const Component* component = findComponent(document, component_);
    if (component == nullptr) {
        return makeError(ErrorCode::NotFound,
                         std::format("{} is not a component of this document", component_));
    }
    ComponentDefinition definition = component->definition();
    // Only the placement moves. Everything else the component says about
    // itself -- its part, its own suppression -- is left alone.
    const ComponentPlacement before = definition.placement;
    definition.placement = placement_;
    if (auto changed = setComponentDefinition(document, component_, definition); !changed) {
        return std::unexpected(changed.error());
    }
    before_ = before;
    return {};
}

Result<void> SetComponentPlacementCommand::undo(Document& document) {
    if (!before_) {
        return notExecuted("undo");
    }
    const Component* component = findComponent(document, component_);
    if (component == nullptr) {
        return makeError(ErrorCode::NotFound,
                         std::format("{} is not a component of this document", component_));
    }
    ComponentDefinition definition = component->definition();
    definition.placement = *before_;
    if (auto changed = setComponentDefinition(document, component_, definition); !changed) {
        return std::unexpected(changed.error());
    }
    before_.reset();
    return {};
}

Result<void> SetComponentPlacementCommand::redo(Document& document) {
    if (before_) {
        return notExecuted("redo");
    }
    return execute(document);
}

// --- CreateMateCommand --------------------------------------------------------------------------

CreateMateCommand::CreateMateCommand(std::string name, MateDefinition definition)
    : name_(std::move(name)), definition_(std::move(definition)) {}

std::string CreateMateCommand::description() const { return std::format("Create mate '{}'", name_); }

Result<void> CreateMateCommand::execute(Document& document) {
    auto id = createMate(document, name_, definition_);
    if (!id) {
        return std::unexpected(id.error());
    }
    id_ = *id;
    return {};
}

Result<void> CreateMateCommand::undo(Document& document) {
    if (!id_.isValid()) {
        return notExecuted("undo");
    }
    auto removed = takeBack(document, ObjectId{id_});
    if (!removed) {
        return std::unexpected(removed.error());
    }
    kept_ = std::move(*removed);
    return {};
}

Result<void> CreateMateCommand::redo(Document& document) {
    if (kept_ == nullptr) {
        return notExecuted("redo");
    }
    if (auto inserted = document.insertObject(kept_->clone()); !inserted) {
        return inserted;
    }
    kept_.reset();
    return {};
}

// --- SetMateDefinitionCommand -------------------------------------------------------------------

SetMateDefinitionCommand::SetMateDefinitionCommand(MateId mate, MateDefinition definition)
    : mate_(mate), definition_(std::move(definition)) {}

std::string SetMateDefinitionCommand::description() const { return std::format("Edit {}", mate_); }

Result<void> SetMateDefinitionCommand::execute(Document& document) {
    const Mate* mate = findMate(document, mate_);
    if (mate == nullptr) {
        return makeError(ErrorCode::NotFound, std::format("{} is not a mate of this document", mate_));
    }
    const MateDefinition before = mate->definition();
    if (auto changed = setMateDefinition(document, mate_, definition_); !changed) {
        return std::unexpected(changed.error());
    }
    before_ = before;
    return {};
}

Result<void> SetMateDefinitionCommand::undo(Document& document) {
    if (!before_) {
        return notExecuted("undo");
    }
    if (auto changed = setMateDefinition(document, mate_, *before_); !changed) {
        return std::unexpected(changed.error());
    }
    before_.reset();
    return {};
}

Result<void> SetMateDefinitionCommand::redo(Document& document) {
    if (before_) {
        return notExecuted("redo");
    }
    return execute(document);
}

// --- Suppression --------------------------------------------------------------------------------

namespace {

/// Applies a suppression override, or clears it when @p suppressed is empty.
template <typename IdT>
[[nodiscard]] Result<void> applySuppression(Document& document, ConfigurationId configuration, IdT id,
                                            std::optional<bool> suppressed) {
    if (suppressed) {
        if constexpr (std::is_same_v<IdT, ComponentId>) {
            if (auto set = suppressComponent(document, configuration, id, *suppressed); !set) {
                return std::unexpected(set.error());
            }
        } else {
            if (auto set = suppressMate(document, configuration, id, *suppressed); !set) {
                return std::unexpected(set.error());
            }
        }
        return {};
    }
    if constexpr (std::is_same_v<IdT, ComponentId>) {
        if (auto cleared = clearComponentSuppression(document, configuration, id); !cleared) {
            return std::unexpected(cleared.error());
        }
    } else {
        if (auto cleared = clearMateSuppression(document, configuration, id); !cleared) {
            return std::unexpected(cleared.error());
        }
    }
    return {};
}

/// What a configuration currently says about an object, which is what undo
/// must put back. Empty means it says nothing, which is a different state
/// from saying "not suppressed".
template <typename IdT>
[[nodiscard]] std::optional<bool> currentSuppression(const Document& document,
                                                     ConfigurationId configuration, IdT id) {
    const Configuration* found = document.configurations().find(configuration);
    return found == nullptr ? std::nullopt : found->suppressionFor(id);
}

} // namespace

SuppressComponentCommand::SuppressComponentCommand(ConfigurationId configuration, ComponentId component,
                                                   std::optional<bool> suppressed)
    : configuration_(configuration), component_(component), suppressed_(suppressed) {}

std::string SuppressComponentCommand::description() const {
    return suppressed_ ? std::format("{} {}", *suppressed_ ? "Suppress" : "Unsuppress", component_)
                       : std::format("Clear suppression of {}", component_);
}

Result<void> SuppressComponentCommand::execute(Document& document) {
    if (document.configurations().find(configuration_) == nullptr) {
        return makeError(ErrorCode::NotFound,
                         std::format("no configuration with ID {}", configuration_));
    }
    const std::optional<bool> before = currentSuppression(document, configuration_, component_);
    if (auto applied = applySuppression(document, configuration_, component_, suppressed_); !applied) {
        return applied;
    }
    before_ = before;
    return {};
}

Result<void> SuppressComponentCommand::undo(Document& document) {
    if (!before_) {
        return notExecuted("undo");
    }
    if (auto applied = applySuppression(document, configuration_, component_, *before_); !applied) {
        return applied;
    }
    before_.reset();
    return {};
}

Result<void> SuppressComponentCommand::redo(Document& document) {
    if (before_) {
        return notExecuted("redo");
    }
    return execute(document);
}

SuppressMateCommand::SuppressMateCommand(ConfigurationId configuration, MateId mate,
                                         std::optional<bool> suppressed)
    : configuration_(configuration), mate_(mate), suppressed_(suppressed) {}

std::string SuppressMateCommand::description() const {
    return suppressed_ ? std::format("{} {}", *suppressed_ ? "Suppress" : "Unsuppress", mate_)
                       : std::format("Clear suppression of {}", mate_);
}

Result<void> SuppressMateCommand::execute(Document& document) {
    if (document.configurations().find(configuration_) == nullptr) {
        return makeError(ErrorCode::NotFound,
                         std::format("no configuration with ID {}", configuration_));
    }
    const std::optional<bool> before = currentSuppression(document, configuration_, mate_);
    if (auto applied = applySuppression(document, configuration_, mate_, suppressed_); !applied) {
        return applied;
    }
    before_ = before;
    return {};
}

Result<void> SuppressMateCommand::undo(Document& document) {
    if (!before_) {
        return notExecuted("undo");
    }
    if (auto applied = applySuppression(document, configuration_, mate_, *before_); !applied) {
        return applied;
    }
    before_.reset();
    return {};
}

Result<void> SuppressMateCommand::redo(Document& document) {
    if (before_) {
        return notExecuted("redo");
    }
    return execute(document);
}

} // namespace bettercad::assembly
