#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Export.hpp>
#include <bettercad/core/Id.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace bettercad {

class Document;

/// A reversible edit of a document. User edits are expressed as commands, not
/// as direct document calls from GUI actions, so every edit can be undone.
///
/// Contract:
/// - execute() applies the edit for the first time. If it fails, the
///   document is unchanged and the command is discarded.
/// - undo() exactly reverts a successful execute() or redo().
/// - redo() re-applies the edit and reproduces the state execute() produced,
///   including the same IDs.
class BETTERCAD_CORE_EXPORT Command {
public:
    virtual ~Command();
    Command(const Command&) = delete;
    Command& operator=(const Command&) = delete;

    /// Short description for undo/redo menus, e.g. "Create parameter 'width'".
    [[nodiscard]] virtual std::string description() const = 0;
    [[nodiscard]] virtual Result<void> execute(Document& document) = 0;
    [[nodiscard]] virtual Result<void> undo(Document& document) = 0;
    [[nodiscard]] virtual Result<void> redo(Document& document) = 0;

protected:
    Command() = default;
};

/// Undo/redo stacks for one document.
///
/// The history binds to the first document it is used with and refuses to
/// operate on any other. Executing a new command clears the redo stack.
class BETTERCAD_CORE_EXPORT CommandHistory {
public:
    /// @param limit  Maximum number of undoable commands kept; 0 = unlimited.
    explicit CommandHistory(std::size_t limit = 0) noexcept : limit_(limit) {}

    /// Executes @p command and, if it succeeds, records it for undo.
    Result<void> execute(Document& document, std::unique_ptr<Command> command);
    Result<void> undo(Document& document);
    Result<void> redo(Document& document);

    [[nodiscard]] bool canUndo() const noexcept { return !undo_.empty(); }
    [[nodiscard]] bool canRedo() const noexcept { return !redo_.empty(); }
    [[nodiscard]] std::size_t undoCount() const noexcept { return undo_.size(); }
    [[nodiscard]] std::size_t redoCount() const noexcept { return redo_.size(); }
    [[nodiscard]] std::optional<std::string> undoDescription() const;
    [[nodiscard]] std::optional<std::string> redoDescription() const;

    /// Forgets all commands and the document binding.
    void clear() noexcept;

private:
    Result<void> bindTo(const Document& document);

    std::size_t limit_;
    std::optional<DocumentId> documentId_;
    std::vector<std::unique_ptr<Command>> undo_;
    std::vector<std::unique_ptr<Command>> redo_;
};

} // namespace bettercad
