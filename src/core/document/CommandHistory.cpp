#include <bettercad/core/document/Command.hpp>
#include <bettercad/core/document/Document.hpp>

#include <format>

namespace bettercad {

Command::~Command() = default;

Result<void> CommandHistory::bindTo(const Document& document) {
    if (!documentId_) {
        documentId_ = document.id();
        return {};
    }
    if (*documentId_ != document.id()) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("this command history belongs to {}, not {}", *documentId_,
                                     document.id()));
    }
    return {};
}

Result<void> CommandHistory::execute(Document& document, std::unique_ptr<Command> command) {
    if (command == nullptr) {
        return makeError(ErrorCode::InvalidArgument, "cannot execute a null command");
    }
    if (auto bound = bindTo(document); !bound) {
        return bound;
    }
    if (auto executed = command->execute(document); !executed) {
        return executed;
    }
    undo_.push_back(std::move(command));
    redo_.clear();
    if (limit_ != 0 && undo_.size() > limit_) {
        undo_.erase(undo_.begin());
    }
    return {};
}

Result<void> CommandHistory::undo(Document& document) {
    if (auto bound = bindTo(document); !bound) {
        return bound;
    }
    if (undo_.empty()) {
        return makeError(ErrorCode::FailedPrecondition, "nothing to undo");
    }
    if (auto undone = undo_.back()->undo(document); !undone) {
        return undone; // the command stays undoable; the document is unchanged
    }
    redo_.push_back(std::move(undo_.back()));
    undo_.pop_back();
    return {};
}

Result<void> CommandHistory::redo(Document& document) {
    if (auto bound = bindTo(document); !bound) {
        return bound;
    }
    if (redo_.empty()) {
        return makeError(ErrorCode::FailedPrecondition, "nothing to redo");
    }
    if (auto redone = redo_.back()->redo(document); !redone) {
        return redone;
    }
    undo_.push_back(std::move(redo_.back()));
    redo_.pop_back();
    return {};
}

std::optional<std::string> CommandHistory::undoDescription() const {
    if (undo_.empty()) {
        return std::nullopt;
    }
    return undo_.back()->description();
}

std::optional<std::string> CommandHistory::redoDescription() const {
    if (redo_.empty()) {
        return std::nullopt;
    }
    return redo_.back()->description();
}

void CommandHistory::clear() noexcept {
    undo_.clear();
    redo_.clear();
    documentId_.reset();
}

} // namespace bettercad
