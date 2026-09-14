#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/geometry/Profile.hpp>
#include <bettercad/features/Export.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <vector>

namespace bettercad::features {

/// Closed-profile detection: turns the sketch's non-construction lines, arcs
/// and circles into planar regions (outer loops with holes) on the sketch
/// plane.
///
/// Edges connect where their end points coincide within the sketch
/// tolerance, whether they share a point entity or were made coincident by
/// constraints. Every end must meet exactly one other end. Loops nested inside
/// other loops become holes; loops inside holes become separate regions.
///
/// Fails with FailedPrecondition if the sketch has no closed loop, an open
/// end or a branch (three or more edges meeting), and with InvalidArgument
/// for degenerate edges.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<std::vector<geometry::PlanarRegion>>
extractRegions(const sketch::Sketch& sketch);

} // namespace bettercad::features
