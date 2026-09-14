#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/geometry/Body.hpp>
#include <bettercad/core/geometry/Export.hpp>
#include <bettercad/core/math/Vector.hpp>

// Rigid motions of bodies. Only translations so far (linear patterns); the
// references of features move with their own functions (translated() in
// Edges.hpp, Faces.hpp and Hole.hpp).
namespace bettercad::geometry {

/// @p body moved by @p translation; @p body is not modified. The copy has
/// its own geometry, each coordinate moved by one addition. Fails with
/// FailedPrecondition for an empty body, InvalidArgument for a non-finite
/// translation, and Internal if the kernel fails.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Result<Body> translated(const Body& body, const Translation3D& translation);

} // namespace bettercad::geometry
