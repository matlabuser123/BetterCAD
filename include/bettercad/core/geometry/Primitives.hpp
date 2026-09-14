#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/geometry/Body.hpp>
#include <bettercad/core/geometry/Export.hpp>
#include <bettercad/core/math/BoundingBox.hpp>
#include <bettercad/core/math/Direction.hpp>
#include <bettercad/core/math/Point.hpp>

// Primitive solids. Sizes must be positive, finite and larger than the
// kernel's precision; otherwise the functions fail with InvalidArgument.
namespace bettercad::geometry {

/// Box with one corner at the origin and edges along +X, +Y and +Z.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Result<Body> makeBox(Length dx, Length dy, Length dz);
/// Box with its minimum corner at @p corner and edges along +X, +Y and +Z.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Result<Body> makeBox(const Point3D& corner, Length dx,
                                                            Length dy, Length dz);

/// Cylinder on the +Z axis through the origin, from z = 0 to z = height.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Result<Body> makeCylinder(Length radius, Length height);
/// Cylinder whose base circle is centred on @p axis.origin, extending along
/// @p axis.direction by @p height.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Result<Body> makeCylinder(const Axis3D& axis, Length radius,
                                                                 Length height);

/// Sphere centred at the origin.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Result<Body> makeSphere(Length radius);
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Result<Body> makeSphere(const Point3D& center, Length radius);

} // namespace bettercad::geometry
