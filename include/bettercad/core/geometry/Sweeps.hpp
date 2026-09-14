#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/geometry/Body.hpp>
#include <bettercad/core/geometry/Export.hpp>
#include <bettercad/core/geometry/Profile.hpp>
#include <bettercad/core/math/Direction.hpp>
#include <bettercad/core/units/Units.hpp>

namespace bettercad::geometry {

/// Solid swept by translating @p region along its plane normal between the
/// offsets @p from and @p to (to > from). For example [0, d] extrudes along
/// the normal, [-d, 0] against it and [-d/2, d/2] symmetrically.
///
/// Loop orientation is normalized (outer counter-clockwise, holes
/// clockwise). Fails with InvalidArgument for malformed loops or extents and
/// with Internal if the kernel cannot build a valid solid (for example
/// self-intersecting loops).
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Result<Body> makePrism(const PlanarRegion& region,
                                                              Length from, Length to);

/// Solid swept by rotating @p region about @p axis from angle @p from to
/// angle @p to, right-handed about the axis direction, with
/// 0 < to - from <= 360°. For example [0, a] revolves in the positive
/// sense, [-a, 0] in the negative sense and [-a/2, a/2] symmetrically about
/// the profile plane. A sweep of exactly 360° gives a closed solid of
/// revolution.
///
/// The axis must lie in the region's plane and the region must lie on one
/// side of it; touching the axis (e.g. an edge on the axis) is allowed.
/// Fails with InvalidArgument for an axis off the plane, a region that
/// crosses the axis, malformed loops or a sweep outside (0, 360°], and with
/// Internal if the kernel cannot build a valid solid.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Result<Body> makeRevolution(const PlanarRegion& region,
                                                                   const Axis3D& axis, Angle from, Angle to);

} // namespace bettercad::geometry
