#pragma once

#include "core/geometry/EdgeMatching.hpp"

#include <bettercad/core/geometry/Faces.hpp>

// Tolerant comparison of face signatures, with the edge-matching tolerances.
namespace bettercad::geometry::detail {

/// Whether the two signatures describe the same plane facing the same way.
[[nodiscard]] bool samePlane(const FaceSignature& a, const FaceSignature& b) noexcept;

} // namespace bettercad::geometry::detail
