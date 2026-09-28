#include <bettercad/features/MaterialCommands.hpp>

#include <bettercad/core/document/Document.hpp>
#include <bettercad/features/Materials.hpp>

#include <format>
#include <utility>

namespace bettercad::features {

namespace {

/// The diagnostic for undo() or redo() on a command that never ran, matching what
/// the core commands say.
[[nodiscard]] std::unexpected<Error> notExecuted(std::string_view what) {
    return makeError(ErrorCode::FailedPrecondition,
                     std::format("cannot {}: the command has not been executed", what));
}

} // namespace

// --- CreateMaterialCommand --------------------------------------------------

CreateMaterialCommand::CreateMaterialCommand(std::string name, MaterialDefinition definition)
    : name_(std::move(name)), definition_(std::move(definition)) {}

std::string CreateMaterialCommand::description() const {
    return std::format("Create material '{}'", name_);
}

Result<void> CreateMaterialCommand::execute(Document& document) {
    // Material::create validates the definition before anything is built, so an
    // invalid definition fails here and the document is never touched -- and
    // because CommandHistory only records a command whose execute() succeeded, the
    // failed attempt does not reach the history either.
    auto material = Material::create(name_, definition_);
    if (!material) {
        return std::unexpected(material.error());
    }
    add_.emplace(std::move(*material));
    if (auto added = add_->execute(document); !added) {
        // Undo the emplace so a second attempt starts clean rather than reusing a
        // half-executed AddObjectCommand.
        add_.reset();
        return added;
    }
    return {};
}

Result<void> CreateMaterialCommand::undo(Document& document) {
    if (!add_) {
        return notExecuted("undo");
    }
    return add_->undo(document);
}

Result<void> CreateMaterialCommand::redo(Document& document) {
    if (!add_) {
        return notExecuted("redo");
    }
    // AddObjectCommand::redo re-inserts under the SAME ObjectId, which is what
    // keeps an assignment that named this material valid across a redo.
    return add_->redo(document);
}

MaterialId CreateMaterialCommand::materialId() const noexcept {
    if (!add_) {
        return MaterialId{};
    }
    return MaterialId::fromValue(add_->objectId().value());
}

// --- DeleteMaterialCommand --------------------------------------------------

DeleteMaterialCommand::DeleteMaterialCommand(MaterialId id) noexcept : id_(id) {}

std::string DeleteMaterialCommand::description() const {
    return std::format("Delete material {}", id_);
}

Result<void> DeleteMaterialCommand::execute(Document& document) {
    // The precondition the generic command cannot express: this ID must name a
    // MATERIAL. Without it, a sketch's ID would delete the sketch and the command
    // would report success, having been asked to delete a material.
    if (findMaterial(document, id_) == nullptr) {
        return makeError(ErrorCode::NotFound, std::format("there is no material {}", id_));
    }
    remove_.emplace(ObjectId{id_});
    if (auto removed = remove_->execute(document); !removed) {
        remove_.reset();
        return removed;
    }
    return {};
}

Result<void> DeleteMaterialCommand::undo(Document& document) {
    if (!remove_) {
        return notExecuted("undo");
    }
    // DeleteObjectCommand kept the whole object, so the material comes back with
    // its ID, its name, every property, every provenance record and whatever the
    // configurations said about it. Nothing is reconstructed from a name.
    return remove_->undo(document);
}

Result<void> DeleteMaterialCommand::redo(Document& document) {
    if (!remove_) {
        return notExecuted("redo");
    }
    return remove_->redo(document);
}

// --- EditMaterialCommand ----------------------------------------------------

EditMaterialCommand::EditMaterialCommand(MaterialId id, MaterialDefinition definition)
    : id_(id), after_(std::move(definition)) {}

std::string EditMaterialCommand::description() const {
    return std::format("Edit material {}", id_);
}

Result<void> EditMaterialCommand::execute(Document& document) {
    const Material* material = findMaterial(document, id_);
    if (material == nullptr) {
        return makeError(ErrorCode::NotFound, std::format("there is no material {}", id_));
    }
    // Captured BEFORE the edit, and only recorded if the edit succeeds, so a
    // rejected edit leaves `before_` unset and the command unexecuted.
    const MaterialDefinition before = material->definition();
    if (auto applied = apply(document, after_); !applied) {
        return applied;
    }
    before_ = before;
    return {};
}

Result<void> EditMaterialCommand::undo(Document& document) {
    if (!before_) {
        return notExecuted("undo");
    }
    return apply(document, *before_);
}

Result<void> EditMaterialCommand::redo(Document& document) {
    if (!before_) {
        return notExecuted("redo");
    }
    return apply(document, after_);
}

Result<void> EditMaterialCommand::apply(Document& document, const MaterialDefinition& definition) {
    // Through the qualified path, so the same validation runs as for any other
    // definition edit and there is one place that writes a material's definition.
    // setMaterialDefinition replaces the definition WHOLE, including its
    // provenance, which is what makes an undo restore a value and its citation
    // together rather than one without the other.
    auto changed = setMaterialDefinition(document, id_, definition);
    if (!changed) {
        return std::unexpected(changed.error());
    }
    return {};
}

// --- AssignMaterialCommand --------------------------------------------------

AssignMaterialCommand::AssignMaterialCommand(MaterialId id) noexcept : id_(id) {}

std::string AssignMaterialCommand::description() const {
    return std::format("Assign material {}", id_);
}

Result<void> AssignMaterialCommand::execute(Document& document) {
    const std::optional<MaterialId> previous = document.materialAssignment();
    // assignMaterial refuses an ID that does not name a material in this document,
    // so the command cannot create an assignment that was never resolvable.
    if (auto assigned = assignMaterial(document, id_); !assigned) {
        return std::unexpected(assigned.error());
    }
    before_ = previous;
    return {};
}

Result<void> AssignMaterialCommand::undo(Document& document) {
    if (!before_) {
        return notExecuted("undo");
    }
    // Straight to the document's setter rather than through assignMaterial,
    // because the previous assignment may legitimately be std::nullopt and may
    // name a material that a later undo in the same chain will restore.
    auto set = document.setMaterialAssignment(*before_);
    if (!set) {
        return std::unexpected(set.error());
    }
    return {};
}

Result<void> AssignMaterialCommand::redo(Document& document) {
    if (!before_) {
        return notExecuted("redo");
    }
    auto assigned = assignMaterial(document, id_);
    if (!assigned) {
        return std::unexpected(assigned.error());
    }
    return {};
}

// --- RemoveMaterialAssignmentCommand ----------------------------------------

std::string RemoveMaterialAssignmentCommand::description() const {
    return "Remove material assignment";
}

Result<void> RemoveMaterialAssignmentCommand::execute(Document& document) {
    const std::optional<MaterialId> previous = document.materialAssignment();
    if (auto removed = removeMaterialAssignment(document); !removed) {
        return std::unexpected(removed.error());
    }
    before_ = previous;
    return {};
}

Result<void> RemoveMaterialAssignmentCommand::undo(Document& document) {
    if (!before_) {
        return notExecuted("undo");
    }
    auto set = document.setMaterialAssignment(*before_);
    if (!set) {
        return std::unexpected(set.error());
    }
    return {};
}

Result<void> RemoveMaterialAssignmentCommand::redo(Document& document) {
    if (!before_) {
        return notExecuted("redo");
    }
    auto removed = removeMaterialAssignment(document);
    if (!removed) {
        return std::unexpected(removed.error());
    }
    return {};
}

} // namespace bettercad::features
