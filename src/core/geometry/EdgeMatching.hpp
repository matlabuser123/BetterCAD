#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/geometry/Edges.hpp>

#include <string_view>
#include <vector>

// Tolerant comparison of edge signatures, shared by edge resolution and
// request validation.
namespace bettercad::geometry::detail {

/// Positions match within the kernel's precision (1e-7 mm).
inline constexpr double kEdgeLengthTolerance = 1e-10; // metres
/// Directions match when the sine of the angle between them is below this.
inline constexpr double kEdgeAngularTolerance = 1e-9;

/// Whether the two signatures describe the same line or circle.
[[nodiscard]] bool sameCurve(const EdgeSignature& a, const EdgeSignature& b) noexcept;

/// Checks the edges selected for an edge operation such as a chamfer or a
/// fillet (@p operation names it in messages): at least one, each valid, and
/// no two on the same curve. Fails with InvalidArgument.
[[nodiscard]] Result<void> validateEdgeSelection(std::string_view operation, const std::vector<EdgeSignature>& edges);

} // namespace bettercad::geometry::detail
