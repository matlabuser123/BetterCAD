#pragma once

#include "Arguments.hpp"
#include "Edits.hpp"

#include <bettercad/core/Error.hpp>
#include <bettercad/core/document/Command.hpp>
#include <bettercad/core/document/Document.hpp>

#include <format>
#include <optional>
#include <string>
#include <string_view>

// The small vocabulary every edit shares (P14-CLI-001).
//
// These were AssemblyEdits.cpp's and are here because the drawing edits need
// exactly the same ones. Two ways to report a malformed argument would drift,
// and the difference between exit 1 and exit 2 -- "the edit was rejected"
// against "the command line was wrong" -- is precisely the thing that must
// not be decided twice.
namespace bettercad::cli {

/// The edit itself was refused: exit 1.
inline EditFailure rejected(std::string message) {
    return EditFailure{.usage = false, .message = std::move(message)};
}
inline EditFailure rejected(const Error& error) {
    return EditFailure{.usage = false, .message = error.message};
}
/// The command line could not be read: exit 2.
inline EditFailure malformed(std::string message) {
    return EditFailure{.usage = true, .message = std::move(message)};
}
/// Which of the two an Error is. InvalidArgument and DimensionMismatch come
/// from the CLI failing to read what it was given; everything else is the
/// model refusing what it was asked.
inline EditFailure fromParse(const Error& error) {
    const bool cliCouldNotRead =
        error.code == ErrorCode::InvalidArgument || error.code == ErrorCode::DimensionMismatch;
    return EditFailure{.usage = cliCouldNotRead, .message = error.message};
}
inline EditFailure fromParse(std::string_view option, const Error& error) {
    EditFailure failure = fromParse(error);
    failure.message = std::format("{}: {}", option, failure.message);
    return failure;
}

/// Runs @p command against @p document, reporting its own failure.
///
/// Every edit that has a command goes through one, so the CLI inherits that
/// milestone's validation and its all-or-nothing execution instead of
/// restating either (ADR-009).
inline std::optional<EditFailure> execute(Document& document, Command& command) {
    if (auto done = command.execute(document); !done) {
        return rejected(done.error());
    }
    return std::nullopt;
}

/// The first free `<stem><n>`, n from 1. Deterministic, so a script that
/// names nothing still produces the same document every time.
inline std::string freeName(const Document& document, std::string_view stem) {
    for (int n = 1;; ++n) {
        std::string candidate = std::format("{}{}", stem, n);
        if (document.findObjectByName(candidate) == nullptr &&
            document.parameters().findByName(candidate) == nullptr) {
            return candidate;
        }
    }
}

/// `--name` if given and valid, otherwise the first free `<stem><n>`.
inline Result<std::string> namedOr(const ParsedArguments& parsed, const Document& document,
                                   std::string_view stem) {
    const auto given = parsed.value("--name");
    if (!given) {
        return freeName(document, stem);
    }
    if (auto valid = validateIdentifier(*given, "object"); !valid) {
        return std::unexpected(valid.error());
    }
    return std::string{*given};
}

} // namespace bettercad::cli
