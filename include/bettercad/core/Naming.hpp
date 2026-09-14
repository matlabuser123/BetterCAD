#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Export.hpp>

#include <cstddef>
#include <string_view>

namespace bettercad {

inline constexpr std::size_t kMaxNameLength = 64;

/// Checks that @p name is an identifier ([A-Za-z_][A-Za-z0-9_]*) of at most
/// kMaxNameLength characters. Names of parameters and document objects follow
/// this rule so that they can be referenced from expressions.
/// @param kind  Word used in error messages, e.g. "parameter".
[[nodiscard]] BETTERCAD_CORE_EXPORT Result<void> validateIdentifier(std::string_view name,
                                                                    std::string_view kind);

} // namespace bettercad
