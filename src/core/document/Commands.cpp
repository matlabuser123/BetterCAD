#include <bettercad/core/document/Commands.hpp>
#include <bettercad/core/document/Document.hpp>

#include <format>

namespace bettercad {

namespace {

std::unexpected<Error> notExecuted(std::string_view what) {
    return makeError(ErrorCode::FailedPrecondition,
                     std::format("cannot {}: the command has not been executed", what));
}

} // namespace

// --- CreateParameterCommand ----------------------------------------------------

CreateParameterCommand::CreateParameterCommand(std::string name, double siValue,
                                               const UnitDescriptor& displayUnit)
    : name_(std::move(name)), siValue_(siValue), displayUnit_(displayUnit) {}

std::string CreateParameterCommand::description() const {
    return std::format("Create parameter '{}'", name_);
}

ParameterId CreateParameterCommand::parameterId() const noexcept {
    return created_ ? created_->id() : ParameterId{};
}

Result<void> CreateParameterCommand::execute(Document& document) {
    auto id = document.createParameter(name_, siValue_, displayUnit_);
    if (!id) {
        return std::unexpected(id.error());
    }
    created_ = *document.parameters().find(*id);
    return {};
}

Result<void> CreateParameterCommand::undo(Document& document) {
    if (!created_) {
        return notExecuted("undo");
    }
    auto removed = document.removeParameter(created_->id());
    if (!removed) {
        return std::unexpected(removed.error());
    }
    return {};
}

Result<void> CreateParameterCommand::redo(Document& document) {
    if (!created_) {
        return notExecuted("redo");
    }
    return document.insertParameter(*created_);
}

// --- ModifyParameterCommand ------------------------------------------------------

ModifyParameterCommand::ModifyParameterCommand(ParameterId id, ParameterChanges changes)
    : id_(id), changes_(std::move(changes)) {}

std::string ModifyParameterCommand::description() const {
    if (before_) {
        return std::format("Modify parameter '{}'", before_->name());
    }
    return std::format("Modify {}", id_);
}

Result<void> ModifyParameterCommand::execute(Document& document) {
    const Parameter* current = document.parameters().find(id_);
    if (current == nullptr) {
        return makeError(ErrorCode::NotFound, std::format("{} does not exist", id_));
    }

    // Apply every change to a copy first: all validation happens before the
    // document is touched.
    Parameter target = *current;
    if (changes_.name) {
        if (auto r = target.rename(*changes_.name); !r) {
            return std::unexpected(r.error());
        }
    }
    if (changes_.displayUnit) {
        if (auto r = target.setDisplayUnit(*changes_.displayUnit); !r) {
            return std::unexpected(r.error());
        }
    }
    if (changes_.value) {
        if (auto r = target.setSiValue(changes_.value->dimension, changes_.value->siValue); !r) {
            return std::unexpected(r.error());
        }
    }
    if (changes_.expression) {
        if (auto r = target.setExpression(*changes_.expression); !r) {
            return std::unexpected(r.error());
        }
    }
    // A driven parameter's value comes from its expression. A value may come
    // with a new expression (its starting value until evaluation), but not
    // on its own.
    if (changes_.value && !changes_.expression && target.expression()) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("parameter '{}' is driven by the expression '{}'; clear the expression to "
                                     "set its value",
                                     target.name(), *target.expression()));
    }

    Parameter before = *current;
    // restoreParameter() is atomic and also checks name uniqueness.
    if (auto applied = document.restoreParameter(target); !applied) {
        return std::unexpected(applied.error());
    }
    before_ = std::move(before);
    after_ = *document.parameters().find(id_);
    return {};
}

Result<void> ModifyParameterCommand::undo(Document& document) {
    if (!before_) {
        return notExecuted("undo");
    }
    auto restored = document.restoreParameter(*before_);
    if (!restored) {
        return std::unexpected(restored.error());
    }
    return {};
}

Result<void> ModifyParameterCommand::redo(Document& document) {
    if (!after_) {
        return notExecuted("redo");
    }
    auto restored = document.restoreParameter(*after_);
    if (!restored) {
        return std::unexpected(restored.error());
    }
    return {};
}

// --- AddObjectCommand ---------------------------------------------------------------

AddObjectCommand::AddObjectCommand(std::unique_ptr<DocumentObject> object)
    : prototype_(std::move(object)) {}

std::string AddObjectCommand::description() const {
    return prototype_ ? std::format("Add '{}'", prototype_->name()) : std::string{"Add object"};
}

Result<void> AddObjectCommand::execute(Document& document) {
    if (prototype_ == nullptr) {
        return makeError(ErrorCode::InvalidArgument, "AddObjectCommand has no object");
    }
    auto id = document.addObject(prototype_->clone());
    if (!id) {
        return std::unexpected(id.error());
    }
    id_ = *id;
    return {};
}

Result<void> AddObjectCommand::undo(Document& document) {
    if (!id_.isValid()) {
        return notExecuted("undo");
    }
    auto removed = document.removeObject(id_);
    if (!removed) {
        return std::unexpected(removed.error());
    }
    removed_ = std::move(*removed);
    return {};
}

Result<void> AddObjectCommand::redo(Document& document) {
    if (removed_ == nullptr) {
        return notExecuted("redo");
    }
    if (auto inserted = document.insertObject(removed_->clone()); !inserted) {
        return inserted;
    }
    removed_.reset();
    return {};
}

// --- DeleteObjectCommand -----------------------------------------------------------

std::string DeleteObjectCommand::description() const {
    return name_.empty() ? std::format("Delete {}", id_) : std::format("Delete '{}'", name_);
}

Result<void> DeleteObjectCommand::execute(Document& document) {
    if (const auto parameterId = document.asParameter(id_)) {
        auto removed = document.removeParameter(*parameterId);
        if (!removed) {
            return std::unexpected(removed.error());
        }
        name_ = removed->name();
        parameter_ = std::move(*removed);
        return {};
    }
    auto removed = document.removeObject(id_);
    if (!removed) {
        return std::unexpected(removed.error());
    }
    name_ = (*removed)->name();
    object_ = std::move(*removed);
    return {};
}

Result<void> DeleteObjectCommand::undo(Document& document) {
    if (parameter_) {
        if (auto inserted = document.insertParameter(*parameter_); !inserted) {
            return inserted;
        }
        parameter_.reset();
        return {};
    }
    if (object_) {
        if (auto inserted = document.insertObject(object_->clone()); !inserted) {
            return inserted;
        }
        object_.reset();
        return {};
    }
    return notExecuted("undo");
}

Result<void> DeleteObjectCommand::redo(Document& document) {
    if (name_.empty()) {
        return notExecuted("redo");
    }
    return execute(document);
}

// --- RenameObjectCommand -----------------------------------------------------------

RenameObjectCommand::RenameObjectCommand(ObjectId id, std::string newName)
    : id_(id), newName_(std::move(newName)) {}

std::string RenameObjectCommand::description() const {
    if (oldName_) {
        return std::format("Rename '{}' to '{}'", *oldName_, newName_);
    }
    return std::format("Rename {} to '{}'", id_, newName_);
}

Result<void> RenameObjectCommand::execute(Document& document) {
    const auto current = document.nameOf(id_);
    if (!current) {
        return makeError(ErrorCode::NotFound, std::format("{} does not exist", id_));
    }
    std::string oldName{*current};
    auto renamed = document.rename(id_, newName_);
    if (!renamed) {
        return std::unexpected(renamed.error());
    }
    oldName_ = std::move(oldName);
    return {};
}

Result<void> RenameObjectCommand::undo(Document& document) {
    if (!oldName_) {
        return notExecuted("undo");
    }
    auto renamed = document.rename(id_, *oldName_);
    if (!renamed) {
        return std::unexpected(renamed.error());
    }
    return {};
}

Result<void> RenameObjectCommand::redo(Document& document) {
    if (!oldName_) {
        return notExecuted("redo");
    }
    auto renamed = document.rename(id_, newName_);
    if (!renamed) {
        return std::unexpected(renamed.error());
    }
    return {};
}


// --- CreateConfigurationCommand --------------------------------------------------

CreateConfigurationCommand::CreateConfigurationCommand(std::string name) : name_(std::move(name)) {}

std::string CreateConfigurationCommand::description() const {
    return std::format("Create configuration '{}'", name_);
}

ConfigurationId CreateConfigurationCommand::configurationId() const noexcept {
    return created_ ? created_->id() : ConfigurationId{};
}

Result<void> CreateConfigurationCommand::execute(Document& document) {
    auto id = document.createConfiguration(name_);
    if (!id) {
        return std::unexpected(id.error());
    }
    created_ = *document.configurations().find(*id);
    return {};
}

Result<void> CreateConfigurationCommand::undo(Document& document) {
    if (!created_) {
        return notExecuted("undo");
    }
    auto removed = document.removeConfiguration(created_->id());
    if (!removed) {
        return std::unexpected(removed.error());
    }
    return {};
}

Result<void> CreateConfigurationCommand::redo(Document& document) {
    if (!created_) {
        return notExecuted("redo");
    }
    return document.insertConfiguration(*created_);
}

// --- ModifyConfigurationCommand --------------------------------------------------

ModifyConfigurationCommand::ModifyConfigurationCommand(ConfigurationId id, ConfigurationChanges changes)
    : id_(id), changes_(std::move(changes)) {}

std::string ModifyConfigurationCommand::description() const {
    return std::format("Modify configuration '{}'", before_ ? before_->name() : std::format("{}", id_));
}

Result<void> ModifyConfigurationCommand::execute(Document& document) {
    const Configuration* current = document.configurations().find(id_);
    if (current == nullptr) {
        return makeError(ErrorCode::NotFound, std::format("{} does not exist", id_));
    }
    // Every override is checked against the document before anything is
    // written, so a rejected edit leaves the configuration as it was.
    for (const auto& [parameter, value] : changes_.overrides) {
        if (auto ok = document.checkOverride(parameter, value); !ok) {
            return std::unexpected(ok.error());
        }
    }
    Configuration target = *current;
    if (changes_.name) {
        if (auto renamed = target.rename(*changes_.name); !renamed) {
            return std::unexpected(renamed.error());
        }
    }
    for (const ParameterId parameter : changes_.cleared) {
        if (auto cleared = target.clearOverride(parameter); !cleared) {
            return std::unexpected(cleared.error());
        }
    }
    for (const auto& [parameter, value] : changes_.overrides) {
        if (auto set = target.setOverride(parameter, value); !set) {
            return std::unexpected(set.error());
        }
    }
    Configuration previous = *current;
    if (auto restored = document.restoreConfiguration(target); !restored) {
        return std::unexpected(restored.error());
    }
    before_ = std::move(previous);
    after_ = *document.configurations().find(id_);
    return {};
}

Result<void> ModifyConfigurationCommand::undo(Document& document) {
    if (!before_) {
        return notExecuted("undo");
    }
    auto restored = document.restoreConfiguration(*before_);
    if (!restored) {
        return std::unexpected(restored.error());
    }
    return {};
}

Result<void> ModifyConfigurationCommand::redo(Document& document) {
    if (!after_) {
        return notExecuted("redo");
    }
    auto restored = document.restoreConfiguration(*after_);
    if (!restored) {
        return std::unexpected(restored.error());
    }
    return {};
}

// --- DeleteConfigurationCommand --------------------------------------------------

std::string DeleteConfigurationCommand::description() const {
    return std::format("Delete configuration '{}'", removed_ ? removed_->name() : std::format("{}", id_));
}

Result<void> DeleteConfigurationCommand::execute(Document& document) {
    wasActive_ = document.activeConfiguration() == id_;
    auto removed = document.removeConfiguration(id_);
    if (!removed) {
        return std::unexpected(removed.error());
    }
    removed_ = std::move(*removed);
    return {};
}

Result<void> DeleteConfigurationCommand::undo(Document& document) {
    if (!removed_) {
        return notExecuted("undo");
    }
    if (auto inserted = document.insertConfiguration(*removed_); !inserted) {
        return std::unexpected(inserted.error());
    }
    if (wasActive_) {
        if (auto set = document.setActiveConfiguration(removed_->id()); !set) {
            return std::unexpected(set.error());
        }
    }
    return {};
}

Result<void> DeleteConfigurationCommand::redo(Document& document) {
    if (!removed_) {
        return notExecuted("redo");
    }
    auto removed = document.removeConfiguration(id_);
    if (!removed) {
        return std::unexpected(removed.error());
    }
    return {};
}

// --- SetActiveConfigurationCommand -----------------------------------------------

std::string SetActiveConfigurationCommand::description() const {
    return id_ ? std::format("Activate {}", *id_) : std::string{"Activate the base configuration"};
}

Result<void> SetActiveConfigurationCommand::execute(Document& document) {
    const std::optional<ConfigurationId> previous = document.activeConfiguration();
    if (auto set = document.setActiveConfiguration(id_); !set) {
        return std::unexpected(set.error());
    }
    before_ = previous;
    return {};
}

Result<void> SetActiveConfigurationCommand::undo(Document& document) {
    if (!before_) {
        return notExecuted("undo");
    }
    auto set = document.setActiveConfiguration(*before_);
    if (!set) {
        return std::unexpected(set.error());
    }
    return {};
}

Result<void> SetActiveConfigurationCommand::redo(Document& document) {
    if (!before_) {
        return notExecuted("redo");
    }
    auto set = document.setActiveConfiguration(id_);
    if (!set) {
        return std::unexpected(set.error());
    }
    return {};
}

} // namespace bettercad
