#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/geometry/Body.hpp>
#include <bettercad/core/geometry/Export.hpp>
#include <bettercad/core/geometry/Profile.hpp>
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

} // namespace bettercad::geometry
