#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/geometry/Body.hpp>
#include <bettercad/core/geometry/Export.hpp>
#include <bettercad/core/math/RigidTransform.hpp>
#include <bettercad/core/math/Vector.hpp>

// Rigid motions of bodies: translations (linear patterns) and rotations
// (circular patterns). The references of features move with their own
// functions (translated() and transformed() in Edges.hpp, Faces.hpp and
// Hole.hpp).
namespace bettercad::geometry {

/// @p body moved by @p translation; @p body is not modified. The copy has
/// its own geometry, each coordinate moved by one addition. Fails with
/// FailedPrecondition for an empty body, InvalidArgument for a non-finite
/// translation, and Internal if the kernel fails.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Result<Body> translated(const Body& body, const Translation3D& translation);

/// @p body moved by @p motion; @p body is not modified. A pure translation
/// is translated() exactly. Otherwise the kernel applies @p motion's own
/// matrix, so a body and the references moved with the same motion agree.
/// Fails as translated() does, and with InvalidArgument for a non-finite
/// motion.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Result<Body> transformed(const Body& body, const RigidTransform3D& motion);

} // namespace bettercad::geometry
