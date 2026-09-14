#pragma once

#include <bettercad/core/Error.hpp>

#include <optional>

namespace bettercad::test {

/// Error code of a failed result, std::nullopt on success. Unlike
/// result.error(), this is safe to call when the operation unexpectedly
/// succeeded, so a CHECK fails cleanly instead of invoking undefined behavior.
template <typename T>
[[nodiscard]] std::optional<ErrorCode> errorCode(const Result<T>& result) {
    if (result.has_value()) {
        return std::nullopt;
    }
    return result.error().code;
}

} // namespace bettercad::test
