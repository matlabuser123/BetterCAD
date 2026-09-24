#pragma once

#include "Arguments.hpp"
#include "Cli.hpp"

#include <expected>
#include <iosfwd>
#include <span>
#include <string>
#include <string_view>

// The edit spine (P13-CLI-001, implementing ADR-009).
//
// Every document-changing command is a function over an ALREADY-LOADED
// document. Two drivers use the same functions:
//
//     single-shot   load -> apply one edit  -> save
//     batch         load -> apply N edits   -> save
//
// so the one-line form and the scripted form cannot disagree about what a
// command means -- the single-shot command is literally the batch of one.
//
// Atomicity is a property of that shape rather than of care taken at each
// call site: nothing is written until every edit has succeeded. A batch that
// fails at step seven saves nothing and names the line, so the file is
// exactly as it was and rerunning the corrected script is the whole recovery
// procedure.
namespace bettercad {
class Document;
}

namespace bettercad::cli {

/// Why an edit did not apply.
struct EditFailure {
    /// The command line was malformed (exit 2), rather than the edit itself
    /// rejected (exit 1). Both leave the document exactly as it was, so the
    /// distinction is for the engineer reading the message, not for recovery.
    bool usage = false;
    std::string message;
};

/// What an edit produced: the line to report, or why it did not apply.
using EditResult = std::expected<std::string, EditFailure>;

/// One edit. `args` excludes both the command name and the document path, so
/// the two drivers hand it exactly the same thing.
///
/// An edit must be all-or-nothing: on failure the document it was given is
/// unchanged. The P13-CMD-001 command objects give that for what they cover,
/// and every other edit checks before it changes.
using EditApply = EditResult (*)(Document& document, Args args);

struct EditCommand {
    std::string_view name;
    /// Without the document path, which every driver supplies: the batch
    /// file names it once, and the single-shot form takes it as the first
    /// positional argument.
    std::string_view usage;
    std::string_view summary;
    EditApply apply;
};

/// The assembly and configuration edits (P13-CLI-001).
[[nodiscard]] std::span<const EditCommand> assemblyEditCommands() noexcept;
/// The drawing edits (P14-CLI-001).
[[nodiscard]] std::span<const EditCommand> drawingEditCommands() noexcept;

/// Every edit, in the order help lists them: the assembly ones, then the
/// drawing ones. Two tables rather than one because a file that owns the
/// assembly verbs should not have to be opened to add a sheet verb; one
/// listing because a script does not care which file a verb came from.
[[nodiscard]] std::span<const EditCommand> editCommands() noexcept;

/// The edit called @p name, or nullptr.
[[nodiscard]] const EditCommand* findEditCommand(std::string_view name) noexcept;

/// The single-shot driver: takes `<file.bcad>` as the first argument, loads
/// it, applies @p command to it, and saves only if the edit succeeded.
[[nodiscard]] ExitCode runEdit(const EditCommand& command, Args args, std::ostream& out, std::ostream& err);

} // namespace bettercad::cli
