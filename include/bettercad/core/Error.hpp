#pragma once

#include <bettercad/core/Export.hpp>

#include <expected>
#include <string>
#include <string_view>
#include <utility>

namespace bettercad {

/// Broad category of a recoverable failure. Details belong in the message.
enum class ErrorCode {
    InvalidArgument,    ///< A value or name is malformed or out of range.
    NotFound,           ///< The referenced item does not exist.
    AlreadyExists,      ///< An item with the same identity or name exists.
    DimensionMismatch,  ///< A quantity has the wrong physical dimension.
    ParseError,         ///< Text or file content could not be parsed.
    FailedPrecondition, ///< The operation is not valid in the current state.
    IoError,            ///< Reading or writing a file failed.
    Internal,           ///< A bug or an unexpected failure of a dependency.
};

[[nodiscard]] BETTERCAD_CORE_EXPORT std::string_view toString(ErrorCode code) noexcept;

/// A recoverable failure: a machine-readable code and a message for people.
struct Error {
    ErrorCode code;
    std::string message;

    friend bool operator==(const Error&, const Error&) = default;
};

/// Outcome of an operation that can fail in expected ways (bad input, missing
/// references). Programming errors are not reported through Result.
template <typename T = void>
using Result = std::expected<T, Error>;

[[nodiscard]] inline std::unexpected<Error> makeError(ErrorCode code, std::string message) {
    return std::unexpected<Error>(Error{code, std::move(message)});
}

} // namespace bettercad
