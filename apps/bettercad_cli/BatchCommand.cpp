#include "Commands.hpp"
#include "Edits.hpp"

#include <bettercad/core/document/Document.hpp>
#include <bettercad/io/DocumentFile.hpp>

#include <cctype>
#include <format>
#include <fstream>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// Scripted workflows (P13-CLI-001, implementing ADR-009).
//
// The failure this command exists to prevent is a script that HALF succeeds.
// Step seven of twenty fails, the file on disk holds the first six edits, and
// it is now neither the document the script describes nor the one it started
// from -- with nothing in it saying so. Rerunning applies the first six again
// on top of themselves, so the result is neither idempotent nor recoverable.
//
// So a batch loads once, applies every edit to the document in memory, and
// saves ONLY if all of them succeeded. Nothing partial ever reaches the file.
//
// There is no second language. A line is the same command line the CLI takes
// at the top level, minus the document path, looked up in the same table and
// run through the same EditApply. The only syntax a batch file adds is `#`
// for a comment and `"` for an argument with spaces in it.
namespace bettercad::cli {

namespace {

/// One line of a script, split into arguments.
struct Line {
    std::size_t number = 0;
    std::vector<std::string> tokens;
};

/// Splits @p text on whitespace, honouring double quotes, and stopping at an
/// unquoted `#`.
///
/// Fails with InvalidArgument on an unterminated quote, rather than silently
/// taking the rest of the line -- a script whose quoting is wrong must not
/// run and mean something else.
Result<std::vector<std::string>> tokenize(std::string_view text) {
    std::vector<std::string> tokens;
    std::string current;
    bool inToken = false;
    bool quoted = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (quoted) {
            if (c == '"') {
                quoted = false;
            } else {
                current.push_back(c);
            }
            continue;
        }
        if (c == '"') {
            quoted = true;
            inToken = true;
            continue;
        }
        if (c == '#') {
            break;
        }
        if (std::isspace(static_cast<unsigned char>(c)) != 0) {
            if (inToken) {
                tokens.push_back(std::move(current));
                current.clear();
                inToken = false;
            }
            continue;
        }
        current.push_back(c);
        inToken = true;
    }
    if (quoted) {
        return makeError(ErrorCode::InvalidArgument, "unterminated \" quote");
    }
    if (inToken) {
        tokens.push_back(std::move(current));
    }
    return tokens;
}

/// Every non-blank, non-comment line of @p path, with its line number kept so
/// a diagnostic can name it.
Result<std::vector<Line>> readScript(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return makeError(ErrorCode::NotFound, std::format("cannot read '{}'", displayPath(path)));
    }
    std::vector<Line> lines;
    std::string text;
    std::size_t number = 0;
    while (std::getline(file, text)) {
        ++number;
        // Scripts written on Windows arrive with CR; a trailing CR in an
        // argument would make a name that cannot be found.
        if (!text.empty() && text.back() == '\r') {
            text.pop_back();
        }
        auto tokens = tokenize(text);
        if (!tokens) {
            return makeError(tokens.error().code, std::format("line {}: {}", number, tokens.error().message));
        }
        if (tokens->empty()) {
            continue;
        }
        lines.push_back(Line{.number = number, .tokens = std::move(*tokens)});
    }
    if (file.bad()) {
        return makeError(ErrorCode::IoError, std::format("error reading '{}'", displayPath(path)));
    }
    return lines;
}

} // namespace

ExitCode runBatch(Args args, std::ostream& out, std::ostream& err) {
    auto parsed = parseArguments(args, {{"--dry-run", false}});
    if (!parsed) {
        return usageError("batch", kBatchUsage, parsed.error().message, err);
    }
    if (parsed->positional().size() != 2) {
        return usageError("batch", kBatchUsage, "expected a document file and a script file", err);
    }
    const std::filesystem::path path = pathFromArgument(parsed->positional()[0]);
    const std::filesystem::path scriptPath = pathFromArgument(parsed->positional()[1]);
    const bool dryRun = parsed->has("--dry-run");

    auto script = readScript(scriptPath);
    if (!script) {
        return failure("batch", script.error().message, err);
    }
    auto document = io::loadDocument(path);
    if (!document) {
        return failure("batch", document.error().message, err);
    }

    // Reported only after every edit has succeeded, so that a failed batch
    // never prints a list of things that look as though they happened.
    std::ostringstream applied;
    for (const Line& line : *script) {
        const EditCommand* command = findEditCommand(line.tokens.front());
        if (command == nullptr) {
            return failure("batch",
                           std::format("{}:{}: '{}' is not an edit; run '{} help' for the list",
                                       displayPath(scriptPath), line.number, line.tokens.front(), kProgramName),
                           err);
        }
        const std::vector<std::string_view> arguments(line.tokens.begin() + 1, line.tokens.end());
        auto result = command->apply(*document, arguments);
        if (!result) {
            // Nothing is written. The loaded copy is discarded here and the
            // file on disk is exactly as it was, so rerunning the corrected
            // script is the whole recovery procedure.
            err << std::format("{} batch: {}:{}: {}\n", kProgramName, displayPath(scriptPath), line.number,
                               result.error().message);
            if (result.error().usage) {
                err << std::format("Usage: {} {}\n", kProgramName, command->usage);
            }
            err << std::format("Nothing was written; {} is unchanged.\n", displayPath(path));
            return result.error().usage ? ExitCode::UsageError : ExitCode::Failure;
        }
        applied << std::format("  {:>4}  {}\n", line.number, *result);
    }

    if (!dryRun) {
        if (auto saved = io::saveDocument(*document, path); !saved) {
            return failure("batch", saved.error().message, err);
        }
    }
    out << std::format("{} {} from {}:\n", dryRun ? "Checked" : "Applied",
                       script->size() == 1 ? "1 edit" : std::format("{} edits", script->size()),
                       displayPath(scriptPath));
    out << applied.str();
    out << std::format("{} {}\n", dryRun ? "Not written (--dry-run):" : "Wrote", displayPath(path));
    return ExitCode::Success;
}

} // namespace bettercad::cli
