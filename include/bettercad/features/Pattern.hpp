#pragma once

#include <cstddef>

// What linear and circular patterns share in their public contract.
namespace bettercad::features {

/// Most instances a pattern (linear or circular) may have, the source
/// included. It guards against accidental input such as a count of 100000:
/// each instance is a boolean on the growing body, so building time grows
/// with the square of the count (measured on the development machine: 200
/// holes take over 2 minutes, 200 cubes over 30 s; see
/// docs/verification/P11-FEAT-005), and 500 instances take many minutes.
/// It can be raised once large patterns are made fast; raising it keeps
/// every saved file valid, which lowering it would not.
inline constexpr std::size_t kMaxPatternInstances = 500;

} // namespace bettercad::features
