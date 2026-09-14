#include <bettercad/core/Naming.hpp>

#include <algorithm>
#include <format>

namespace bettercad {

namespace {

constexpr bool isIdentifierStart(char c) noexcept {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

constexpr bool isIdentifierChar(char c) noexcept {
    return isIdentifierStart(c) || (c >= '0' && c <= '9');
}

} // namespace

Result<void> validateIdentifier(std::string_view name, std::string_view kind) {
    if (name.empty()) {
        return makeError(ErrorCode::InvalidArgument, std::format("{} name must not be empty", kind));
    }
    if (name.size() > kMaxNameLength) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("{} name '{}' is longer than {} characters", kind, name,
                                     kMaxNameLength));
    }
    if (!isIdentifierStart(name.front()) || !std::ranges::all_of(name, isIdentifierChar)) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("{} name '{}' is invalid: use letters, digits and '_', and "
                                     "do not start with a digit",
                                     kind, name));
    }
    return {};
}

} // namespace bettercad
