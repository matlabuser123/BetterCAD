#include <bettercad/core/document/Document.hpp>
#include <bettercad/sketch/Sketch.hpp>
#include <bettercad/sketch/SketchCommands.hpp>

#include <format>
#include <memory>
#include <utility>

namespace bettercad::sketch {

CreateSketchCommand::CreateSketchCommand(std::string name, const Frame3D& placement)
    : name_(std::move(name)), add_(std::make_unique<Sketch>(name_, placement)) {}

std::string CreateSketchCommand::description() const {
    return std::format("Create sketch '{}'", name_);
}

Result<void> CreateSketchCommand::execute(Document& document) {
    return add_.execute(document);
}

Result<void> CreateSketchCommand::undo(Document& document) {
    return add_.undo(document);
}

Result<void> CreateSketchCommand::redo(Document& document) {
    return add_.redo(document);
}

// --- ModifySketchCommand --------------------------------------------------------------------

ModifySketchCommand::ModifySketchCommand(SketchId sketch, std::string description, Edit edit)
    : sketch_(sketch), description_(std::move(description)), edit_(std::move(edit)) {}

ModifySketchCommand::~ModifySketchCommand() = default;

std::string ModifySketchCommand::description() const {
    return description_;
}

Result<void> ModifySketchCommand::execute(Document& document) {
    const auto* current = document.findObjectAs<Sketch>(ObjectId{sketch_});
    if (current == nullptr) {
        return makeError(ErrorCode::NotFound, std::format("{} is not a sketch in this document", sketch_));
    }
    if (!edit_) {
        return makeError(ErrorCode::InvalidArgument, "ModifySketchCommand has no edit");
    }
    auto before = std::make_unique<Sketch>(*current);
    auto after = std::make_unique<Sketch>(*current);
    if (auto edited = edit_(*after); !edited) {
        return edited;
    }
    if (auto applied = restore(document, *after); !applied) {
        return applied;
    }
    before_ = std::move(before);
    after_ = std::move(after);
    return {};
}

Result<void> ModifySketchCommand::undo(Document& document) {
    if (!before_) {
        return makeError(ErrorCode::FailedPrecondition, "cannot undo: the command has not been executed");
    }
    return restore(document, *before_);
}

Result<void> ModifySketchCommand::redo(Document& document) {
    if (!after_) {
        return makeError(ErrorCode::FailedPrecondition, "cannot redo: the command has not been executed");
    }
    return restore(document, *after_);
}

Result<void> ModifySketchCommand::restore(Document& document, const Sketch& state) {
    auto changed = document.modifyObject<Sketch>(ObjectId{sketch_},
                                                 [&](Sketch& sketch) { return sketch.restoreContent(state); });
    if (!changed) {
        return std::unexpected(changed.error());
    }
    return {};
}

} // namespace bettercad::sketch
