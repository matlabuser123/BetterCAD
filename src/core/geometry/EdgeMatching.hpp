#pragma once

#include <bettercad/core/geometry/Edges.hpp>

// Tolerant comparison of edge signatures, shared by edge resolution and
// request validation.
namespace bettercad::geometry::detail {

/// Positions match within the kernel's precision (1e-7 mm).
inline constexpr double kEdgeLengthTolerance = 1e-10; // metres
/// Directions match when the sine of the angle between them is below this.
inline constexpr double kEdgeAngularTolerance = 1e-9;

/// Whether the two signatures describe the same line or circle.
[[nodiscard]] bool sameCurve(const EdgeSignature& a, const EdgeSignature& b) noexcept;

} // namespace bettercad::geometry::detail
