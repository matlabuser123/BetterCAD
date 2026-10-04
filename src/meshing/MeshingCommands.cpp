// Undoable meshing edits (P16-CMD-001).

#include <bettercad/meshing/MeshingCommands.hpp>

#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/units/Units.hpp>

#include <algorithm>
#include <format>
#include <utility>

namespace bettercad::meshing {
namespace {

[[nodiscard]] std::unexpected<Error> notExecuted(std::string_view what) {
    return std::unexpected(
        Error{ErrorCode::FailedPrecondition,
              std::format("cannot {} a meshing command that has not been executed", what)});
}

[[nodiscard]] std::unexpected<Error> noSuchControl(MeshControlId control) {
    return std::unexpected(Error{ErrorCode::NotFound,
                                 std::format("there is no meshing control {}", control.value())});
}

/// Applies @p definition to the control, through the document.
///
/// THROUGH Document::modifyObject, which type-checks the object, validates via
/// MeshControl::setDefinition before assigning, and bumps the object and
/// document revisions ONLY when something actually changed. A command that
/// reached into the object directly would bypass all three.
[[nodiscard]] Result<bool> apply(Document& document, MeshControlId control,
                                 const MeshControlDefinition& definition) {
    return document.modifyObject<MeshControl>(
        ObjectId::fromValue(control.value()),
        [&definition](MeshControl& object) { return object.setDefinition(definition); });
}

[[nodiscard]] std::string describeSize(const std::optional<Length>& size) {
    return size.has_value() ? std::format("{:.4g} mm", size->in(units::mm))
                            : std::string{"the default"};
}

} // namespace

const MeshControl* findMeshControl(const Document& document, MeshControlId id) noexcept {
    for (const DocumentObject& object : document.objects()) {
        if (object.id().value() == id.value()) {
            // A dynamic_cast rather than a trusted ID: an object of another
            // kind with this number is not a control, and answering nullptr is
            // what makes a sketch's ID fail a meshing command instead of
            // quietly working.
            return dynamic_cast<const MeshControl*>(&object);
        }
    }
    return nullptr;
}

std::vector<MeshControlId> meshControls(const Document& document) {
    std::vector<MeshControlId> controls;
    for (const DocumentObject& object : document.objects()) {
        if (dynamic_cast<const MeshControl*>(&object) != nullptr) {
            controls.push_back(MeshControlId::fromValue(object.id().value()));
        }
    }
    std::ranges::sort(controls, [](MeshControlId a, MeshControlId b) {
        return a.value() < b.value();
    });
    return controls;
}

// ---------------------------------------------------------------------------
// Create and delete: the generic commands, wrapped
// ---------------------------------------------------------------------------

CreateMeshControlCommand::CreateMeshControlCommand(std::string name,
                                                   MeshControlDefinition definition)
    : name_(std::move(name)), definition_(std::move(definition)) {}

std::string CreateMeshControlCommand::description() const {
    return std::format("Create meshing control '{}'", name_);
}

Result<void> CreateMeshControlCommand::execute(Document& document) {
    // MeshControl::create validates before anything is built, so an invalid
    // definition fails here and the document is never touched -- and because
    // CommandHistory records only a command whose execute() succeeded, the
    // failed attempt does not reach the history either.
    auto control = MeshControl::create(name_, definition_);
    if (!control.has_value()) {
        return std::unexpected(control.error());
    }
    add_.emplace(std::move(*control));
    if (auto added = add_->execute(document); !added.has_value()) {
        // Undo the emplace so a second attempt starts clean rather than
        // reusing a half-executed AddObjectCommand.
        add_.reset();
        return added;
    }
    return Result<void>{};
}

Result<void> CreateMeshControlCommand::undo(Document& document) {
    if (!add_.has_value()) {
        return notExecuted("undo");
    }
    return add_->undo(document);
}

Result<void> CreateMeshControlCommand::redo(Document& document) {
    if (!add_.has_value()) {
        return notExecuted("redo");
    }
    // AddObjectCommand::redo re-inserts under the SAME ObjectId, which is what
    // keeps a reference to this control valid across a redo.
    return add_->redo(document);
}

MeshControlId CreateMeshControlCommand::meshControlId() const noexcept {
    return add_.has_value() ? MeshControlId::fromValue(add_->objectId().value())
                            : MeshControlId{};
}

DeleteMeshControlCommand::DeleteMeshControlCommand(MeshControlId control) noexcept
    : control_(control) {}

std::string DeleteMeshControlCommand::description() const {
    return std::format("Delete meshing control {}", control_.value());
}

Result<void> DeleteMeshControlCommand::execute(Document& document) {
    // The precondition the generic command cannot express: that the ID really
    // names a control. Without it, deleting a sketch through a meshing command
    // would work.
    if (findMeshControl(document, control_) == nullptr) {
        return noSuchControl(control_);
    }
    remove_.emplace(ObjectId::fromValue(control_.value()));
    if (auto removed = remove_->execute(document); !removed.has_value()) {
        remove_.reset();
        return removed;
    }
    return Result<void>{};
}

Result<void> DeleteMeshControlCommand::undo(Document& document) {
    if (!remove_.has_value()) {
        return notExecuted("undo");
    }
    // DeleteObjectCommand::undo restores the object with the same ID and name,
    // and the configuration overrides that were said about it.
    return remove_->undo(document);
}

Result<void> DeleteMeshControlCommand::redo(Document& document) {
    if (!remove_.has_value()) {
        return notExecuted("redo");
    }
    return remove_->redo(document);
}

// ---------------------------------------------------------------------------
// The shape every definition edit takes
// ---------------------------------------------------------------------------

Result<void> MeshControlEditCommand::execute(Document& document) {
    const MeshControl* control = findMeshControl(document, control_);
    if (control == nullptr) {
        return noSuchControl(control_);
    }
    // Captured BEFORE the edit, and recorded only if the edit succeeds, so a
    // refused edit leaves before_ unset and the command unexecuted.
    const MeshControlDefinition before = control->definition();
    Result<MeshControlDefinition> after = edit(before);
    if (!after.has_value()) {
        return std::unexpected(after.error());
    }
    Result<bool> changed = apply(document, control_, *after);
    if (!changed.has_value()) {
        return std::unexpected(changed.error());
    }
    before_ = before;
    after_ = std::move(*after);
    changed_ = *changed;
    return Result<void>{};
}

Result<void> MeshControlEditCommand::undo(Document& document) {
    if (!before_.has_value()) {
        return notExecuted("undo");
    }
    // EXACTLY the definition that was there, as one value. Nothing is
    // reconstructed field by field, so there is no field to forget.
    Result<bool> restored = apply(document, control_, *before_);
    if (!restored.has_value()) {
        return std::unexpected(restored.error());
    }
    return Result<void>{};
}

Result<void> MeshControlEditCommand::redo(Document& document) {
    if (!after_.has_value()) {
        return notExecuted("redo");
    }
    Result<bool> reapplied = apply(document, control_, *after_);
    if (!reapplied.has_value()) {
        return std::unexpected(reapplied.error());
    }
    return Result<void>{};
}

// ---------------------------------------------------------------------------
// The edits
// ---------------------------------------------------------------------------

SetMeshControlDefinitionCommand::SetMeshControlDefinitionCommand(MeshControlId control,
                                                                 MeshControlDefinition definition)
    : MeshControlEditCommand(control), definition_(std::move(definition)) {}

std::string SetMeshControlDefinitionCommand::description() const {
    return "Edit meshing settings";
}

Result<MeshControlDefinition> SetMeshControlDefinitionCommand::edit(
    const MeshControlDefinition& before) const {
    (void)before;
    return definition_;
}

SetGlobalMeshSizeCommand::SetGlobalMeshSizeCommand(MeshControlId control,
                                                   std::optional<Length> target) noexcept
    : MeshControlEditCommand(control), target_(target) {}

std::string SetGlobalMeshSizeCommand::description() const {
    // NO ELEMENT COUNT IN A COMMAND NAME. A description is about the intent;
    // how many tetrahedra it happens to produce is derived and not yet known.
    return std::format("Set global mesh size to {}", describeSize(target_));
}

Result<MeshControlDefinition> SetGlobalMeshSizeCommand::edit(
    const MeshControlDefinition& before) const {
    MeshControlDefinition after = before;
    after.mesh.sizing.globalTargetSize = target_;
    return after;
}

AddLocalMeshSizingCommand::AddLocalMeshSizingCommand(MeshControlId control, FaceName face,
                                                     Length targetSize)
    : MeshControlEditCommand(control), face_(std::move(face)), targetSize_(targetSize) {}

std::string AddLocalMeshSizingCommand::description() const {
    return std::format("Add local mesh sizing ({:.4g} mm)", targetSize_.in(units::mm));
}

Result<MeshControlDefinition> AddLocalMeshSizingCommand::edit(
    const MeshControlDefinition& before) const {
    const auto existing = std::ranges::find_if(
        before.mesh.sizing.local,
        [this](const LocalMeshSizing& local) { return local.face == face_; });
    if (existing != before.mesh.sizing.local.end()) {
        // REFUSED HERE, with a precondition the caller can act on, rather than
        // left to surface as P16-SIZE-001's DuplicateFaceControl from inside
        // the validator. Same rule, named at the point of the mistake.
        return makeError(ErrorCode::AlreadyExists,
                         "a local mesh sizing control already exists for that face");
    }
    MeshControlDefinition after = before;
    after.mesh.sizing.local.push_back(LocalMeshSizing{.face = face_, .targetSize = targetSize_});
    return after;
}

EditLocalMeshSizingCommand::EditLocalMeshSizingCommand(MeshControlId control, FaceName face,
                                                       Length targetSize)
    : MeshControlEditCommand(control), face_(std::move(face)), targetSize_(targetSize) {}

std::string EditLocalMeshSizingCommand::description() const {
    return std::format("Edit local mesh sizing ({:.4g} mm)", targetSize_.in(units::mm));
}

Result<MeshControlDefinition> EditLocalMeshSizingCommand::edit(
    const MeshControlDefinition& before) const {
    MeshControlDefinition after = before;
    const auto target = std::ranges::find_if(
        after.mesh.sizing.local,
        [this](const LocalMeshSizing& local) { return local.face == face_; });
    if (target == after.mesh.sizing.local.end()) {
        // FOUND BY FACE, which is the identity, and never by position in the
        // vector: the order carries no meaning, so an index would name a
        // different control after any removal.
        return makeError(ErrorCode::NotFound,
                         "there is no local mesh sizing control for that face");
    }
    target->targetSize = targetSize_;
    return after;
}

RemoveLocalMeshSizingCommand::RemoveLocalMeshSizingCommand(MeshControlId control, FaceName face)
    : MeshControlEditCommand(control), face_(std::move(face)) {}

std::string RemoveLocalMeshSizingCommand::description() const {
    return "Remove local mesh sizing";
}

Result<MeshControlDefinition> RemoveLocalMeshSizingCommand::edit(
    const MeshControlDefinition& before) const {
    MeshControlDefinition after = before;
    const auto removed = std::ranges::remove_if(
        after.mesh.sizing.local,
        [this](const LocalMeshSizing& local) { return local.face == face_; });
    if (removed.begin() == after.mesh.sizing.local.end()) {
        return makeError(ErrorCode::NotFound,
                         "there is no local mesh sizing control for that face");
    }
    after.mesh.sizing.local.erase(removed.begin(), removed.end());
    return after;
}

AddBoundarySetCommand::AddBoundarySetCommand(MeshControlId control, NamedBoundarySet set)
    : MeshControlEditCommand(control), set_(std::move(set)) {}

std::string AddBoundarySetCommand::description() const {
    return std::format("Add boundary set '{}'", set_.name);
}

Result<MeshControlDefinition> AddBoundarySetCommand::edit(
    const MeshControlDefinition& before) const {
    const auto existing = std::ranges::find_if(
        before.boundarySets,
        [this](const NamedBoundarySet& set) { return set.id == set_.id; });
    if (existing != before.boundarySets.end()) {
        return makeError(ErrorCode::AlreadyExists,
                         std::format("a boundary set with identity {} already exists",
                                     set_.id.value()));
    }
    MeshControlDefinition after = before;
    after.boundarySets.push_back(set_);
    return after;
}

EditBoundarySetCommand::EditBoundarySetCommand(MeshControlId control, NamedBoundarySet set)
    : MeshControlEditCommand(control), set_(std::move(set)) {}

std::string EditBoundarySetCommand::description() const {
    return std::format("Edit boundary set '{}'", set_.name);
}

Result<MeshControlDefinition> EditBoundarySetCommand::edit(
    const MeshControlDefinition& before) const {
    MeshControlDefinition after = before;
    const auto target = std::ranges::find_if(
        after.boundarySets, [this](const NamedBoundarySet& set) { return set.id == set_.id; });
    if (target == after.boundarySets.end()) {
        return makeError(ErrorCode::NotFound,
                         std::format("there is no boundary set with identity {}", set_.id.value()));
    }
    // THE IDENTITY IS KEPT and the rest replaced: a set's ID is what a
    // reference to it names, so an edit must not mint a new one.
    *target = set_;
    return after;
}

RemoveBoundarySetCommand::RemoveBoundarySetCommand(MeshControlId control,
                                                   BoundarySetId set) noexcept
    : MeshControlEditCommand(control), set_(set) {}

std::string RemoveBoundarySetCommand::description() const {
    return std::format("Remove boundary set {}", set_.value());
}

Result<MeshControlDefinition> RemoveBoundarySetCommand::edit(
    const MeshControlDefinition& before) const {
    MeshControlDefinition after = before;
    const auto removed = std::ranges::remove_if(
        after.boundarySets, [this](const NamedBoundarySet& set) { return set.id == set_; });
    if (removed.begin() == after.boundarySets.end()) {
        return makeError(ErrorCode::NotFound,
                         std::format("there is no boundary set with identity {}", set_.value()));
    }
    after.boundarySets.erase(removed.begin(), removed.end());
    return after;
}

} // namespace bettercad::meshing
