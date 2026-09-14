#pragma once

#include <string_view>

// Implemented by the build-time generated GitRevision.cpp.
namespace bettercad::detail {

std::string_view gitRevision() noexcept;
bool gitDirty() noexcept;

} // namespace bettercad::detail
